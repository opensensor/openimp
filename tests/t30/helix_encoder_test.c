/*
 * Host test for the native T30 Helix encoder wrapper.
 *
 * /dev/soc_vpu and the rmem allocator are replaced by fakes: RUN writes a
 * synthetic CABAC payload into the bitstream window, so the test checks
 * what the wrapper itself owns -- access-unit layout, emulation prevention
 * across the header/payload seam, IDR and frame_num bookkeeping, failure
 * handling and runtime parameter changes.  The encoder's ABI keeps
 * addresses in 32-bit fields, so the fakes hand out low mappings
 * (MAP_32BIT) and the binary is linked without PIE.
 */

#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "dma_alloc.h"
#include "t30/t30_helix_encoder.h"

#define FAKE_FD 77
#define T30_CHANNEL_REQUEST 0xc0386300u
#define T30_CHANNEL_RELEASE 0xc0386301u
#define T30_CHANNEL_RUN     0xc0386302u

typedef struct {
    uint32_t clist;
    uint32_t vlist;
    uint32_t mdelay;
    uint32_t channel_id;
    int32_t vpu_id;
    uint32_t codecdir;
    uint32_t workphase;
    uint32_t status;
    uint32_t output_len;
    uint32_t dma_addr;
    int32_t thread_id;
    uint32_t cmpx;
    uint32_t n_flag;
    uint32_t ncu_addr;
} FakeChannel;

typedef struct {
    void *mapping;
    uint32_t size;
    char tag[32];
} FakeAllocation;

static FakeAllocation allocations[16];
static uint8_t payload[1u << 20];
static uint32_t payload_length;
static int run_result;
static int run_sets_length = 1;
static unsigned int runs;
static uint64_t flushed_before_run;
static uint64_t flushed_after_run;
static int in_run_window;

/* ---- fakes for the rmem allocator ---- */

int DMA_AllocDescriptor(IMPDMABufferInfo *info, int size, const char *tag)
{
    unsigned int i;
    void *mapping;

    mapping = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    assert(mapping != MAP_FAILED);
    for (i = 0; i < 16u; i++) {
        if (!allocations[i].mapping) {
            allocations[i].mapping = mapping;
            allocations[i].size = (uint32_t)size;
            snprintf(allocations[i].tag, sizeof(allocations[i].tag), "%s",
                     tag);
            break;
        }
    }
    assert(i < 16u);
    memset(info, 0, sizeof(*info));
    info->virt_addr = (uint32_t)(uintptr_t)mapping;
    info->phys_addr = (uint32_t)(uintptr_t)mapping;
    info->size = (uint32_t)size;
    return 0;
}

int DMA_FreePhys(uint32_t phys_addr)
{
    unsigned int i;

    for (i = 0; i < 16u; i++) {
        if ((uint32_t)(uintptr_t)allocations[i].mapping == phys_addr) {
            munmap(allocations[i].mapping, allocations[i].size);
            memset(&allocations[i], 0, sizeof(allocations[i]));
            return 0;
        }
    }
    assert(!"freeing unknown DMA buffer");
    return -1;
}

int DMA_RmemFlushCache(void *virt_addr, uint32_t size, int dir)
{
    (void)virt_addr;
    (void)dir;
    if (in_run_window)
        flushed_after_run += size;
    else
        flushed_before_run += size;
    return 0;
}

static FakeAllocation *allocation(const char *tag)
{
    unsigned int i;

    for (i = 0; i < 16u; i++)
        if (allocations[i].mapping && !strcmp(allocations[i].tag, tag))
            return &allocations[i];
    return NULL;
}

/* ---- fakes for /dev/soc_vpu (linked with --wrap) ---- */

int __real_open(const char *path, int flags, ...);
int __wrap_open(const char *path, int flags, ...)
{
    if (!strcmp(path, "/dev/soc_vpu"))
        return FAKE_FD;
    return __real_open(path, flags, 0);
}

int __real_close(int fd);
int __wrap_close(int fd)
{
    return fd == FAKE_FD ? 0 : __real_close(fd);
}

int __wrap_ioctl(int fd, unsigned long request, ...)
{
    FakeChannel *channel;
    va_list ap;

    va_start(ap, request);
    channel = va_arg(ap, FakeChannel *);
    va_end(ap);
    assert(fd == FAKE_FD);
    if (request == T30_CHANNEL_REQUEST) {
        channel->clist = 1;
        channel->channel_id = 3;
        return 0;
    }
    if (request == T30_CHANNEL_RELEASE)
        return 0;
    assert(request == T30_CHANNEL_RUN);
    runs++;
    in_run_window = 1;
    if (run_result) {
        errno = EIO;
        return -1;
    }
    memcpy((uint8_t *)allocation("t30-helix-bs")->mapping + 256u, payload,
           payload_length);
    if (run_sets_length)
        channel->output_len = payload_length;
    channel->status = 0x1;
    return 0;
}

/* ---- bitstream inspection ---- */

typedef struct {
    int types[8];
    uint8_t rbsp[8][1u << 21];
    uint32_t rbsp_length[8];
    unsigned int count;
} AccessUnit;

static AccessUnit au;

static void split_access_unit(const uint8_t *data, uint32_t length)
{
    uint32_t offset = 0;

    au.count = 0;
    while (offset < length) {
        uint32_t end;
        uint32_t zeros = 0;
        uint32_t out = 0;
        uint32_t i;

        assert(offset + 5u <= length);
        assert(!memcmp(data + offset, "\0\0\0\1", 4));
        for (end = offset + 4u; end + 4u <= length; end++)
            if (!memcmp(data + end, "\0\0\0\1", 4))
                break;
        if (end + 4u > length)
            end = length;
        assert(au.count < 8u);
        au.types[au.count] = data[offset + 4u] & 0x1f;
        for (i = offset + 5u; i < end; i++) {
            if (zeros >= 2u && data[i] == 3u) {
                /* An escape byte is always followed by 0..3. */
                assert(i + 1u < end && data[i + 1u] <= 3u);
                zeros = 0;
                continue;
            }
            assert(!(zeros >= 2u && data[i] <= 2u));
            au.rbsp[au.count][out++] = data[i];
            zeros = data[i] ? 0u : zeros + 1u;
        }
        au.rbsp_length[au.count] = out;
        au.count++;
        offset = end;
    }
}

typedef struct {
    const uint8_t *data;
    uint32_t offset;
} Bits;

static uint32_t bits_read(Bits *bits, unsigned int count)
{
    uint32_t value = 0;

    while (count--) {
        value = (value << 1) |
                ((bits->data[bits->offset / 8u] >> (7u - bits->offset % 8u)) &
                 1u);
        bits->offset++;
    }
    return value;
}

static uint32_t bits_ue(Bits *bits)
{
    unsigned int zeros = 0;

    while (!bits_read(bits, 1))
        zeros++;
    return (1u << zeros) - 1u + bits_read(bits, zeros);
}

typedef struct {
    int idr;
    uint32_t frame_num;
    uint32_t idr_pic_id;
    uint32_t level;
} PictureInfo;

/* Encode one picture and check its layout.  Returns 0 on success. */
static int encode(T30HelixEncoder *encoder, PictureInfo *info)
{
    static IMPFrameInfo frame;
    HWStreamBuffer *stream = NULL;
    const uint8_t *slice;
    uint32_t slice_length;
    unsigned int slice_index;
    Bits bits;
    uint32_t slice_type;

    frame.phyAddr = 0x10000000u;
    frame.timeStamp++;
    in_run_window = 0;
    if (OpenIMP_T30_HelixEncode(encoder, &frame, &stream) != 0) {
        assert(!stream);
        return -1;
    }
    assert(stream && stream->virt_addr && !stream->phys_addr);
    split_access_unit((const uint8_t *)(uintptr_t)stream->virt_addr,
                      stream->length);
    info->idr = stream->frame_type == HW_FRAME_TYPE_I;
    if (info->idr) {
        assert(au.count == 3u && au.types[0] == 7 && au.types[1] == 8 &&
               au.types[2] == 5);
        info->level = au.rbsp[0][2];
    } else {
        assert(au.count == 1u && au.types[0] == 1);
    }
    slice_index = au.count - 1u;
    slice = au.rbsp[slice_index];
    slice_length = au.rbsp_length[slice_index];

    /* The VPU payload must follow the escaped header unchanged. */
    assert(slice_length > payload_length);
    assert(!memcmp(slice + slice_length - payload_length, payload,
                   payload_length));

    bits.data = slice;
    bits.offset = 0;
    assert(bits_ue(&bits) == 0u);                   /* first_mb_in_slice */
    slice_type = bits_ue(&bits);
    assert(slice_type == (info->idr ? 7u : 5u));
    assert(bits_ue(&bits) == 0u);                   /* pps id */
    info->frame_num = bits_read(&bits, 10);
    info->idr_pic_id = info->idr ? bits_ue(&bits) : 0u;
    assert(bits.offset <= (slice_length - payload_length) * 8u);
    assert(stream->timestamp == (uint64_t)frame.timeStamp);

    free((void *)(uintptr_t)stream->virt_addr);
    free(stream);
    return 0;
}

static void fill_payload(uint32_t length, unsigned int zero_percent)
{
    static uint32_t state = 7u;
    uint32_t i;

    for (i = 0; i < length; i++) {
        state = state * 1103515245u + 12345u;
        payload[i] = (state >> 8) % 100u < zero_percent
            ? 0u : (uint8_t)(1u + (state >> 16) % 4u);
    }
    /* RBSP payloads end with a stop bit. */
    payload[length - 1u] = 0x80;
    payload_length = length;
}

static T30HelixEncoder *create(uint32_t width, uint32_t height,
                               uint32_t fps, uint32_t gop)
{
    HWEncoderParams params;
    T30HelixEncoder *encoder = NULL;

    memset(&params, 0, sizeof(params));
    params.width = width;
    params.height = height;
    params.fps_num = fps;
    params.fps_den = 1;
    params.gop_length = gop;
    params.rc_mode = HW_RC_MODE_CBR;
    params.bitrate = 2000000;
    params.qp = 30;
    params.min_qp = 20;
    params.max_qp = 45;
    assert(OpenIMP_T30_HelixCreate(&encoder, &params) == 0);
    return encoder;
}

static void test_gop_and_failures(void)
{
    T30HelixEncoder *encoder = create(1920, 1080, 25, 4);
    PictureInfo info;
    unsigned int i;

    fill_payload(5000, 30);
    for (i = 0; i < 9u; i++) {
        assert(encode(encoder, &info) == 0);
        assert(info.idr == (i % 4u == 0u));
        assert(info.frame_num == i % 4u);
        if (info.idr) {
            assert(info.idr_pic_id == (i / 4u) % 2u);
            assert(info.level == 40u);
        }
    }

    /* Picture 9 would be P frame_num 1.  A requested IDR survives a failed
     * RUN; idr_pic_id advances once, for the IDR actually emitted. */
    OpenIMP_T30_HelixRequestIDR(encoder);
    run_result = -1;
    assert(encode(encoder, &info) != 0);
    run_result = 0;
    assert(encode(encoder, &info) == 0);
    assert(info.idr && info.frame_num == 0u && info.idr_pic_id == 1u);

    /* A failed P picture does not advance frame_num. */
    run_result = -1;
    assert(encode(encoder, &info) != 0);
    run_result = 0;
    assert(encode(encoder, &info) == 0);
    assert(!info.idr && info.frame_num == 1u);

    /* A RUN that returns without a length is a failure, not a replay of
     * the previous picture's size. */
    run_sets_length = 0;
    assert(encode(encoder, &info) != 0);
    run_sets_length = 1;
    assert(encode(encoder, &info) == 0);
    assert(!info.idr && info.frame_num == 2u);

    /* Worst-case escaping: an all-zero payload ending in the stop byte. */
    fill_payload(200000, 100);
    assert(encode(encoder, &info) == 0);
    fill_payload(5000, 30);
    OpenIMP_T30_HelixDestroy(encoder);
}

static void test_runtime_parameters(void)
{
    T30HelixEncoder *encoder = create(1920, 1080, 25, 10);
    HWEncoderParams params;
    PictureInfo info;
    unsigned int i;

    for (i = 0; i < 3u; i++)
        assert(encode(encoder, &info) == 0);

    /* Shorter GOP takes effect without an extra IDR. */
    memset(&params, 0, sizeof(params));
    params.width = 1920;
    params.height = 1080;
    params.fps_num = 25;
    params.fps_den = 1;
    params.gop_length = 4;
    params.rc_mode = HW_RC_MODE_CBR;
    params.bitrate = 2000000;
    params.qp = 30;
    params.min_qp = 20;
    params.max_qp = 45;
    assert(OpenIMP_T30_HelixUpdateParams(encoder, &params) == 0);
    assert(encode(encoder, &info) == 0);
    assert(!info.idr && info.frame_num == 3u);
    assert(encode(encoder, &info) == 0);
    assert(info.idr && info.level == 40u);

    /* 1080p60 needs level 4.2: new SPS, so the next picture is an IDR. */
    assert(encode(encoder, &info) == 0 && !info.idr);
    params.fps_num = 60;
    assert(OpenIMP_T30_HelixUpdateParams(encoder, &params) == 0);
    assert(encode(encoder, &info) == 0);
    assert(info.idr && info.level == 42u);

    /* Unchanged parameters and zero fields are no-ops. */
    assert(encode(encoder, &info) == 0 && !info.idr);
    params.qp = 0;
    params.min_qp = 0;
    params.max_qp = 0;
    params.gop_length = 0;
    assert(OpenIMP_T30_HelixUpdateParams(encoder, &params) == 0);
    assert(encode(encoder, &info) == 0 && !info.idr);

    /* Bitrate-only and fixed-QP changes keep the stream running. */
    params.bitrate = 1000000;
    assert(OpenIMP_T30_HelixUpdateParams(encoder, &params) == 0);
    assert(encode(encoder, &info) == 0 && !info.idr);
    params.rc_mode = HW_RC_MODE_FIXQP;
    params.qp = 33;
    assert(OpenIMP_T30_HelixUpdateParams(encoder, &params) == 0);
    assert(encode(encoder, &info) == 0);
    OpenIMP_T30_HelixDestroy(encoder);
}

static void test_large_frame_level(void)
{
    T30HelixEncoder *encoder = create(2560, 1440, 20, 25);
    PictureInfo info;

    assert(encode(encoder, &info) == 0);
    assert(info.idr && info.level == 50u);
    OpenIMP_T30_HelixDestroy(encoder);
}

int main(void)
{
    unsigned int i;

    /* Keep every heap block below 4 GiB for the 32-bit address fields. */
    mallopt(M_MMAP_THRESHOLD, 1 << 30);
    fill_payload(5000, 30);
    test_gop_and_failures();
    test_runtime_parameters();
    test_large_frame_level();
    for (i = 0; i < 16u; i++)
        assert(!allocations[i].mapping);
    printf("T30 Helix encoder tests passed (%u runs)\n", runs);
    return 0;
}
