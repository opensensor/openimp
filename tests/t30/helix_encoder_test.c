/*
 * Host test for the native T30 Helix encoder wrapper.
 *
 * /dev/soc_vpu and the rmem allocator are replaced by fakes: RUN writes a
 * synthetic CABAC payload into the bitstream window, so the test checks
 * what the wrapper itself owns -- access-unit layout, emulation prevention
 * across the header/payload seam, IDR and frame_num bookkeeping, failure
 * handling and runtime parameter changes.  The encoder's ABI keeps
 * addresses in 32-bit fields, so the fakes hand out memory from one fixed
 * low window (fake_rmem_map) and the binary is linked without PIE.
 *
 * Not MAP_32BIT: with ASLR the kernel places MAP_32BIT mappings bottom-up
 * from about 1 GiB, and the brk heap of a non-PIE binary anywhere in the
 * first GiB above its data.  When a mapping landed directly above the heap
 * top, the next heap growth failed, glibc fell back to mmap above 4 GiB
 * and the truncated stream buffer address crashed the test (about 2 % of
 * runs).  The window sits at 1.5 GiB, above the heap's reach.
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
#include "t30/t30_start_qp.h"
#include "t30/h264enc/common.h"
#if defined(PLATFORM_T20)
#include "rc_t20/rc_t20.h"
#include "rc_t10/rc_t10.h"
#include "t30/helix_bitstream.h"

int IMP_Encoder_SetPoolSize(int size);
#endif
#if defined(PLATFORM_T21) && !defined(PLATFORM_T20)
#include "t21/t21_h264_descriptor.h"
#include "t30/helix_bitstream.h"
int IMP_Encoder_SetPoolSize(int size);
#endif

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
#if defined(PLATFORM_T20)
static uint32_t run_40040, run_50040, run_50060, run_70064, run_5004c;   /* T20: the last command list's QP cap and ME words */
#endif
static uint32_t run_raw_c;          /* chroma input address of the last RUN */
static uint8_t payload[1u << 20];
static uint32_t payload_length;
static int run_result;
static int run_sets_length = 1;
static uint32_t run_length_override;
static unsigned int runs;
#if defined(PLATFORM_T21) && !defined(PLATFORM_T20)
static unsigned int reg_reads;
#endif
#if defined(PLATFORM_T30)
#include "eprc/eprc.h"
static unsigned int reg_reads;
#endif
#if defined(PLATFORM_T20)
static unsigned int reg_reads;
/* the command list of the last RUN: 0x4006c, the QP table writes, 0x80034 */
static uint32_t run_qptab_ctrl, run_qptab_words, run_80034;
static uint32_t run_qptab[4096];
static uint32_t frame_virt;     /* luma for the macroblock rate control */

/* 0x800e4 (two moving-macroblock counts) after RUN number n: a moving
 * scene every third picture */
static uint32_t t20_moving(unsigned int n)
{
    return n % 3u == 0u ? 0x02000200u : 0x00400040u;
}
#endif
static uint64_t flushed_before_run;
static uint64_t flushed_after_run;
static int in_run_window;

/* ---- fake rmem: one fixed window below the heap, first fit ---- */

#define FAKE_RMEM_BASE 0x60000000u
#define FAKE_RMEM_SIZE (256u << 20)
#define FAKE_RMEM_PAGE 4096u

static uint8_t *fake_rmem;
static struct { uintptr_t start, end; } fake_rmem_live[32];

static void *fake_rmem_map(size_t size)
{
    uintptr_t start, end;
    unsigned int i;
    int moved;

    if (!fake_rmem) {
        uintptr_t base;

        /* the next free 256 MiB window below 4 GiB, normally the first */
        for (base = FAKE_RMEM_BASE; base + FAKE_RMEM_SIZE <= 0xffffffffu;
             base += FAKE_RMEM_SIZE) {
            fake_rmem = mmap((void *)base, FAKE_RMEM_SIZE, PROT_NONE,
                             MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE |
                             MAP_FIXED_NOREPLACE, -1, 0);
            if (fake_rmem == (uint8_t *)base)
                break;
            if (fake_rmem != MAP_FAILED) /* old kernel: a hint only */
                munmap(fake_rmem, FAKE_RMEM_SIZE);
            fake_rmem = NULL;
        }
        assert(fake_rmem);
    }
    size = (size + FAKE_RMEM_PAGE - 1u) & ~(size_t)(FAKE_RMEM_PAGE - 1u);
    start = (uintptr_t)fake_rmem;
    do {
        moved = 0;
        end = start + size;
        for (i = 0; i < 32u; i++) {
            if (fake_rmem_live[i].end && start < fake_rmem_live[i].end &&
                fake_rmem_live[i].start < end) {
                start = fake_rmem_live[i].end;
                moved = 1;
                break;
            }
        }
    } while (moved);
    assert(end <= (uintptr_t)fake_rmem + FAKE_RMEM_SIZE);
    for (i = 0; i < 32u && fake_rmem_live[i].end; i++)
        ;
    assert(i < 32u);
    fake_rmem_live[i].start = start;
    fake_rmem_live[i].end = end;
    assert(mprotect((void *)start, size, PROT_READ | PROT_WRITE) == 0);
    return (void *)start;
}

/* freed memory faults again, as after munmap, and comes back zeroed */
static void fake_rmem_unmap(void *mapping, size_t size)
{
    uintptr_t start = (uintptr_t)mapping;
    unsigned int i;

    size = (size + FAKE_RMEM_PAGE - 1u) & ~(size_t)(FAKE_RMEM_PAGE - 1u);
    for (i = 0; i < 32u; i++)
        if (fake_rmem_live[i].start == start && fake_rmem_live[i].end)
            break;
    assert(i < 32u && fake_rmem_live[i].end == start + size);
    assert(madvise(mapping, size, MADV_DONTNEED) == 0);
    assert(mprotect(mapping, size, PROT_NONE) == 0);
    fake_rmem_live[i].start = fake_rmem_live[i].end = 0;
}

/* ---- fakes for the rmem allocator ---- */

int DMA_AllocDescriptor(IMPDMABufferInfo *info, int size, const char *tag)
{
    unsigned int i;
    void *mapping;

    mapping = fake_rmem_map((size_t)size);
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

/* The encoder keeps its buffers for the channel's lifetime: they must come
 * from the top of rmem, clear of the FrameSource pools. */
static unsigned int top_allocations;

int DMA_AllocDescriptorTop(IMPDMABufferInfo *info, int size, const char *tag)
{
    top_allocations++;
    return DMA_AllocDescriptor(info, size, tag);
}

int DMA_FreePhys(uint32_t phys_addr)
{
    unsigned int i;

    for (i = 0; i < 16u; i++) {
        if ((uint32_t)(uintptr_t)allocations[i].mapping == phys_addr) {
            fake_rmem_unmap(allocations[i].mapping, allocations[i].size);
            memset(&allocations[i], 0, sizeof(allocations[i]));
            return 0;
        }
    }
    assert(!"freeing unknown DMA buffer");
    return -1;
}

int DMA_RmemStats(size_t *used, size_t *size, size_t *largest)
{
    if (used)
        *used = 0;
    if (size)
        *size = 0;
    if (largest)
        *largest = 0;
    return 0;
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

#if defined(PLATFORM_T21)
/* T21, T20/T10: one bitstream buffer shared by every channel (stock
 * "vpuBs") */
#define BS_TAG "helix-bs"
#else
#define BS_TAG "t30-helix-bs"
#endif

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

/* EFE ROI (0x40044..0x40068) and chroma QP offset (0x40120) words of the
 * last command list; list_chroma_seen: 0x40120 was programmed */
static uint32_t list_roi[10];
static uint32_t list_chroma;
static int list_chroma_seen;

#if defined(PLATFORM_T21) && !defined(PLATFORM_T20)
/* 0x40074 and 0x40078..0x40090 of the last command list */
static uint32_t list_mbrc[8];
static uint32_t list_ctrl[2];       /* 0x400c0, 0x400c4 */
#endif

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
#if defined(PLATFORM_T21) && !defined(PLATFORM_T20)
    if (request == 0xc0386307u) {
        /* IOCTL_CHANNEL_WOR_VPU_REG: struct reg_info { paddr, value,
         * dir } read of a Helix statistics register (OEM T21 base) */
        uint32_t *reg = (uint32_t *)channel;

        assert(reg[0] >= 0x13200000u && reg[0] < 0x13300000u &&
               !(reg[0] & 3u) && reg[2] == 0u);
        reg[1] = reg[0] * 2654435761u >> 7;
        reg_reads++;
        return 0;
    }
#endif
#if defined(PLATFORM_T20)
    if (request == 0xc0386307u) {
        /* register read of the T20 rate control: node word 0 the
         * address, word 1 the value */
        uint32_t *node = (uint32_t *)channel;

        assert(node[0] == 0x132800e4u || node[0] == 0x132800e8u ||
               node[0] == 0x132800ecu);
        node[1] = node[0] == 0x132800e4u ? t20_moving(runs) : 0x1000u;
        reg_reads++;
        return 0;
    }
#endif
#if defined(PLATFORM_T30)
    if (request == 0xc0386307u) {
        /* register read of the T30 eprc statistics: node word 0 the
         * address, word 1 the value */
        uint32_t *node = (uint32_t *)channel;

        assert(node[0] >= 0x13240000u && node[0] < 0x13290000u && !(node[0] & 3u));
        node[1] = (node[0] * 2654435761u) >> 9;
        reg_reads++;
        return 0;
    }
#endif
    assert(request == T30_CHANNEL_RUN);
    runs++;
#if defined(PLATFORM_T20)
    {
        const uint32_t *d =
            (const uint32_t *)allocation("t30-helix-desc")->mapping;
        uint32_t i;

        run_qptab_words = 0;
        for (i = 0; i + 1u < allocation("t30-helix-desc")->size / 4u; i += 2u) {
            uint32_t reg = d[i + 1] & 0x3fffffffu;

            if (reg == 0x4006cu)
                run_qptab_ctrl = d[i];
            else if (reg == 0x80034u)
                run_80034 = d[i];
            else if (reg == 0x40040u)
                run_40040 = d[i];
            else if (reg == 0x50040u)
                run_50040 = d[i];
            else if (reg == 0x50060u)
                run_50060 = d[i];
            else if (reg == 0x5004cu)
                run_5004c = d[i];
            else if (reg == 0x70064u)
                run_70064 = d[i];
            else if (reg >= 0xc5800u && reg < 0xc5800u + 4u * 4096u &&
                     run_qptab_words < 4096u)
                run_qptab[run_qptab_words++] = d[i];
            if (d[i + 1] & 0x40000000u)     /* terminal command */
                break;
        }
    }
#endif
    in_run_window = 1;
    {
        /* the chroma input address (0x40014) of the command list */
        const FakeAllocation *list = allocation("t30-helix-desc");
        const uint32_t *w = list ? (const uint32_t *)list->mapping : NULL;
        size_t i;

        run_raw_c = 0;
        memset(list_roi, 0xa5, sizeof(list_roi));
        list_chroma = 0;
        list_chroma_seen = 0;
        for (i = 0; w && i + 1u < list->size / 4u; i += 2u) {
            uint32_t reg = w[i + 1u] & 0xffffcu;

            if (reg == 0x40014u && !run_raw_c)
                run_raw_c = w[i];
            else if (reg >= 0x40044u && reg <= 0x40068u)
                list_roi[(reg - 0x40044u) / 4u] = w[i];
            else if (reg == 0x40120u) {
                list_chroma = w[i];
                list_chroma_seen = 1;
            }
            if (w[i + 1u] & 0x40000000u)
                break;          /* terminal command */
        }
    }
#if defined(PLATFORM_T21) && !defined(PLATFORM_T20)
    {
        /* the macroblock rate-control registers of the command list
         * (value, register pairs) */
        const FakeAllocation *list = allocation("t30-helix-desc");
        const uint32_t *w = list ? (const uint32_t *)list->mapping : NULL;
        size_t i;

        memset(list_mbrc, 0, sizeof(list_mbrc));
        for (i = 0; w && i + 1u < 4096u; i += 2u) {
            uint32_t reg = w[i + 1u] & 0xffffcu;

            if (reg == 0x40074u)
                list_mbrc[0] = w[i];
            else if (reg == 0x400c0u || reg == 0x400c4u)
                list_ctrl[(reg - 0x400c0u) / 4u] = w[i];
            else if (reg >= 0x40078u && reg <= 0x40090u)
                list_mbrc[1u + (reg - 0x40078u) / 4u] = w[i];
        }
    }
#endif
    if (run_result) {
        errno = EIO;
        return -1;
    }
    memcpy((uint8_t *)allocation(BS_TAG)->mapping + 256u, payload,
           payload_length);
    if (run_sets_length)
        channel->output_len = run_length_override ? run_length_override
                                                  : payload_length;
    channel->status = 0x1;
    channel->cmpx = payload_length * 37u;
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

static int32_t bits_se(Bits *bits)
{
    uint32_t v = bits_ue(bits);

    return (v & 1u) ? (int32_t)((v + 1u) / 2u) : -(int32_t)(v / 2u);
}

typedef struct {
    int idr;
    uint32_t frame_num;
    uint32_t idr_pic_id;
    uint32_t level;
    uint32_t qp;                /* 26 (pic_init_qp) + slice_qp_delta */
    uint32_t slice_bytes;       /* slice header and payload (RBSP) */
    int reorder, mmco, ltr;     /* hs_parse: list modification, MMCO, long_term_reference_flag */
} PictureInfo;

/* 1: the pictures may carry the HSkip commands (reference list modification, MMCO, long-term flag) and a payload of
 * their own (BlackEnhance): parsed into PictureInfo instead of asserted away */
static int hs_parse;

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
#if defined(PLATFORM_T20)
    frame.virAddr = frame_virt;
#endif
    frame.timeStamp++;
    in_run_window = 0;
    if (OpenIMP_T30_HelixEncode(encoder, &frame, &stream) != 0) {
        assert(!stream);
        return -1;
    }
    assert(stream && stream->virt_addr && !stream->phys_addr);
    {
        /* HELIX_TEST_DUMP=file: append the access units (parameter sets
         * can be checked with ffmpeg -bsf:v trace_headers) */
        const char *dump = getenv("HELIX_TEST_DUMP");
        FILE *f = dump ? fopen(dump, "ab") : NULL;

        if (f) {
            (void)fwrite((const void *)(uintptr_t)stream->virt_addr, 1,
                         stream->length, f);
            fclose(f);
        }
    }
    split_access_unit((const uint8_t *)(uintptr_t)stream->virt_addr,
                      stream->length);
    info->idr = stream->frame_type == HW_FRAME_TYPE_I;
    info->reorder = info->mmco = info->ltr = 0;
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
    assert(hs_parse || slice_length > payload_length);
    assert(hs_parse || !memcmp(slice + slice_length - payload_length, payload,
                               payload_length));

    bits.data = slice;
    bits.offset = 0;
    assert(bits_ue(&bits) == 0u);                   /* first_mb_in_slice */
    slice_type = bits_ue(&bits);
#if defined(PLATFORM_T21)
    assert(slice_type == (info->idr ? 2u : 0u));
#else
    assert(slice_type == (info->idr ? 7u : 5u));
#endif
    assert(bits_ue(&bits) == 0u);                   /* pps id */
    info->frame_num = bits_read(&bits, 10);
    info->idr_pic_id = info->idr ? bits_ue(&bits) : 0u;
    if (!info->idr) {
        if (bits_read(&bits, 1))                    /* num_ref_idx override */
            (void)bits_ue(&bits);
        info->reorder = (int)bits_read(&bits, 1);   /* list reordering */
        if (info->reorder) {
            uint32_t idc;

            assert(hs_parse);
            while ((idc = bits_ue(&bits)) != 3u) {
                assert(idc == 2u);                  /* long_term_pic_num */
                assert(bits_ue(&bits) == 0u);
            }
        }
        info->mmco = (int)bits_read(&bits, 1);      /* MMCO */
        if (info->mmco) {
            uint32_t op;

            assert(hs_parse);
            while ((op = bits_ue(&bits)) != 0u) {
                assert(op == 1u || op == 6u);
                (void)bits_ue(&bits);               /* difference_of_pic_nums / long_term_frame_idx */
            }
        }
        (void)bits_ue(&bits);                       /* cabac_init_idc */
    } else {
        (void)bits_read(&bits, 1);                  /* no_output_of_prior_pics */
        info->ltr = (int)bits_read(&bits, 1);       /* long_term_reference_flag */
    }
    info->qp = (uint32_t)(26 + bits_se(&bits));
    info->slice_bytes = slice_length;
    assert(hs_parse || bits.offset <= (slice_length - payload_length) * 8u);
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

/* IMP_Encoder_SetChnColor2Grey (HWEncoderParams.color2grey): from the next
 * IDR on the VPU reads a grey (127) chroma plane instead of the frame's;
 * off frees it again at the following IDR */
static void test_color2grey(void)
{
    const uint32_t chroma = 0x10000000u + 640u * 368u;
    T30HelixEncoder *encoder = create(640, 360, 25, 4);
    HWEncoderParams p;
    PictureInfo info;
    FakeAllocation *grey;
    unsigned int i;
    uint32_t k;

    memset(&p, 0, sizeof(p));
    p.width = 640;
    p.height = 360;
    p.fps_num = 25;
    p.fps_den = 1;
    p.gop_length = 4;
    p.rc_mode = HW_RC_MODE_CBR;
    p.bitrate = 2000000;
    p.qp = 30;
    p.min_qp = 20;
    p.max_qp = 45;
    assert(encode(encoder, &info) == 0 && info.idr);
    assert(run_raw_c == chroma);
    p.color2grey = 1;
    assert(OpenIMP_T30_HelixUpdateParams(encoder, &p) == 0);
    for (i = 1; i < 4u; i++) {          /* the rest of the GOP keeps colour */
        assert(encode(encoder, &info) == 0 && !info.idr);
        assert(run_raw_c == chroma && !allocation("t30-helix-grey"));
    }
    assert(encode(encoder, &info) == 0 && info.idr);
    grey = allocation("t30-helix-grey");
    assert(grey && grey->size == 640u * 368u / 2u);
    for (k = 0; k < grey->size; k++)
        assert(((const uint8_t *)grey->mapping)[k] == 127u);
    assert(run_raw_c == (uint32_t)(uintptr_t)grey->mapping);
    p.color2grey = 0;
    assert(OpenIMP_T30_HelixUpdateParams(encoder, &p) == 0);
    assert(encode(encoder, &info) == 0 && !info.idr);
    assert(run_raw_c == (uint32_t)(uintptr_t)grey->mapping);
    for (i = 2; i < 4u; i++)
        assert(encode(encoder, &info) == 0 && !info.idr);
    assert(encode(encoder, &info) == 0 && info.idr);
    assert(run_raw_c == chroma && !allocation("t30-helix-grey"));
    /* on again, then destroyed while on: nothing leaks (main checks) */
    p.color2grey = 1;
    assert(OpenIMP_T30_HelixUpdateParams(encoder, &p) == 0);
    for (i = 1; i < 5u; i++)
        assert(encode(encoder, &info) == 0);
    assert(info.idr && allocation("t30-helix-grey"));
    OpenIMP_T30_HelixDestroy(encoder);
    assert(!allocation("t30-helix-grey"));
}

#if defined(PLATFORM_T20)
/* IMP_Encoder_SetSuperFrameCfg on T20: the OEM controller codes a picture
 * again (raised QP, up to three times) when it is above the threshold;
 * NONE never does */
static void test_t20_superframe(void)
{
    T30HelixEncoder *encoder;
    HWEncoderParams p;
    PictureInfo info;
    unsigned int i, before;

    unsetenv("OPENIMP_T20_RC");
    setenv("OPENIMP_T20_MBRC", "0", 1);     /* these checks compare against a controller without the macroblock table */
    encoder = create(640, 360, 25, 10);
    memset(&p, 0, sizeof(p));
    p.width = 640;
    p.height = 360;
    p.fps_num = 25;
    p.fps_den = 1;
    p.gop_length = 10;
    p.rc_mode = HW_RC_MODE_CBR;
    p.bitrate = 2000000;
    p.qp = 30;
    p.min_qp = 20;
    p.max_qp = 45;
    fill_payload(20000u, 30);           /* 160 kbit per picture */
    for (i = 0; i < 3u; i++) {          /* OEM thresholds: no second pass */
        before = runs;
        assert(encode(encoder, &info) == 0);
        assert(runs == before + 1u);
    }
    p.super_mode = HW_SUPERFRM_REENCODE;
    p.super_i_bits = 50000u;
    p.super_p_bits = 50000u;
    assert(OpenIMP_T30_HelixUpdateParams(encoder, &p) == 0);
    before = runs;
    assert(encode(encoder, &info) == 0);
    assert(runs > before + 1u && runs <= before + 4u);
    p.super_mode = HW_SUPERFRM_NONE;
    assert(OpenIMP_T30_HelixUpdateParams(encoder, &p) == 0);
    for (i = 0; i < 3u; i++) {
        before = runs;
        assert(encode(encoder, &info) == 0);
        assert(runs == before + 1u);
    }
    OpenIMP_T30_HelixDestroy(encoder);
    fill_payload(5000u, 30);
}
#endif

static void test_unaligned_width_rejected(void)
{
    HWEncoderParams params;
    T30HelixEncoder *encoder = NULL;

    memset(&params, 0, sizeof(params));
    params.width = 1918;
    params.height = 1080;
    params.fps_num = 25;
    params.fps_den = 1;
    params.gop_length = 25;
    params.bitrate = 2000000;
    assert(OpenIMP_T30_HelixCreate(&encoder, &params) != 0);
    assert(!encoder);
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

    /* A length beyond the bitstream window (a confused or timed-out VPU)
     * is a failure: nothing is read past the window. */
    run_length_override = allocation(BS_TAG)->size;
    assert(encode(encoder, &info) != 0);
    run_length_override = 0xfffffff0u;
    assert(encode(encoder, &info) != 0);
    run_length_override = 0;
    assert(encode(encoder, &info) == 0);
    assert(!info.idr && info.frame_num == 3u);

    /* Per picture, only the command list is cleaned before RUN and only
     * the bytes the VPU reported are invalidated after it. */
    flushed_before_run = 0;
    flushed_after_run = 0;
    assert(encode(encoder, &info) == 0);
    assert(flushed_before_run > 0u && flushed_before_run <= 2060u * 8u);
    assert(flushed_after_run == payload_length);

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

#if defined(PLATFORM_T20)
/* FIXQP (#191): the OEM i264e_ratecontrol_init codes I pictures at QP - 3, P pictures at QP, whatever
 * the QP range; the encoder front end hands a FIXQP channel the range [qp, qp] */
static void test_t20_fixqp(void)
{
    HWEncoderParams params;
    T30HelixEncoder *encoder = NULL;
    PictureInfo info;
    unsigned int n, idrs = 0;

    memset(&params, 0, sizeof(params));
    params.width = 640;
    params.height = 360;
    params.fps_num = 25;
    params.fps_den = 1;
    params.gop_length = 10;
    params.rc_mode = HW_RC_MODE_FIXQP;
    params.qp = 30;
    params.min_qp = params.max_qp = 30;
    assert(OpenIMP_T30_HelixCreate(&encoder, &params) == 0);
    for (n = 0; n < 25u; n++) {
        assert(encode(encoder, &info) == 0);
        assert(info.qp == (info.idr ? 27 : 30));
        /* T20 3.12.0 soc_vpu capture (FixQP 10..51): macroblock QP cap = QP + 3 for I and P, the ME words of
         * a P picture 0x8f3f5008 and the macroblock-aligned picture 640x360 -> 367<<16 | 639 */
        assert(run_40040 == 33u);
        assert(run_70064 == 1u);
        if (!info.idr) {
            assert(run_50040 == 0x8f3f5008u);
            assert(run_50060 == ((367u << 16) | 639u));
            assert(run_5004c == 0u);            /* 40 x 23 macroblocks: below 51 x 39 */
        }
        idrs += info.idr != 0;
    }
    assert(idrs == 3u);
    OpenIMP_T30_HelixDestroy(encoder);

    /* 0x5004c of a P picture (T20 3.12.0 hwicodec_pf_h264e_t20_enc): 0x88080303 from 51 x 39 macroblocks on (1080p), 0 below */
    {
        static const struct { unsigned int w, h; uint32_t v; } c5004c[] = {
            { 1920, 1080, 0x88080303u }, { 816, 624, 0x88080303u }, { 800, 624, 0u }, { 816, 608, 0u }, { 1280, 720, 0x88080303u },
        };
        unsigned int k;

        for (k = 0; k < sizeof(c5004c) / sizeof(c5004c[0]); k++) {
            params.width = c5004c[k].w;
            params.height = c5004c[k].h;
            assert(OpenIMP_T30_HelixCreate(&encoder, &params) == 0);
            run_5004c = 0xdeadbeefu;
            assert(encode(encoder, &info) == 0 && info.idr);
            assert(encode(encoder, &info) == 0 && !info.idr);
            assert(run_5004c == c5004c[k].v);
            OpenIMP_T30_HelixDestroy(encoder);
        }
        params.width = 640;
        params.height = 360;
    }

    /* 0x70064 is clear below slice QP 16: FixQP 18 codes the I pictures at 15 (clear) and the P pictures at 18 */
    params.qp = params.min_qp = params.max_qp = 18;
    assert(OpenIMP_T30_HelixCreate(&encoder, &params) == 0);
    for (n = 0; n < 12u; n++) {
        assert(encode(encoder, &info) == 0);
        assert(run_70064 == (info.idr ? 0u : 1u));
        assert(run_40040 == 21u);
    }
    OpenIMP_T30_HelixDestroy(encoder);
}

/* The OEM T20 rate control (src/rc_t20) chooses every slice QP from the
 * statistics the encoder hands it: the slice size, the channel's cmpx and
 * three VPU registers.  A second controller fed the same values here must
 * agree picture by picture (the default; OPENIMP_T20_RC=0 the GOP
 * controller). */
static void test_t20_rate_control(void)
{
    T30HelixEncoder *encoder;
    RcT20 rc;
    RcT20Params p;
    PictureInfo info;
    unsigned int i, reads;

    setenv("OPENIMP_T20_RC", "1", 1);
    setenv("OPENIMP_T20_MBRC", "0", 1);     /* the plumbing checks below run without the macroblock table */
    encoder = create(1280, 720, 25, 10);
    RCT20_DefaultParams(&p);
    p.method = 1;
    p.width = 1280;
    p.height = 720;
    p.gop = 10;
    p.fps_num = 25;
    p.fps_den = 1;
    p.qp = 30;
    p.min_qp = 20;
    p.max_qp = 45;
    p.bitrate = 2000;
    p.max_bitrate = 2000;
    p.static_time = 2;
    p.mb_rc = 0;
    /* CBR: the I-aware P budget is OpenIMP's default; =0 gives the OEM
     * decisions.  Both against a reference controller. */
    setenv("OPENIMP_T20_RC_IAWARE", "0", 1);
    OpenIMP_T30_HelixDestroy(encoder);
    encoder = create(1280, 720, 25, 10);
    p.iaware = 0;
    for (unsigned int pass = 0; pass < 2u; pass++) {
    if (pass == 1u) {
        OpenIMP_T30_HelixDestroy(encoder);
        RCT20_Free(&rc);
        unsetenv("OPENIMP_T20_RC_IAWARE");
        encoder = create(1280, 720, 25, 10);
        p.iaware = 1;
    }
    memset(&rc, 0, sizeof(rc));
    assert(RCT20_Init(&rc, &p) == 0);
    for (i = 0; i < 45u; i++) {
        RcT20Picture pic;
        RcT20Stats st;

        fill_payload(1500u + (i % 7u) * 2500u + (i > 25u ? 20000u : 0u), 30);
        reads = reg_reads;
        assert(encode(encoder, &info) == 0);
        RCT20_Start(&rc, info.idr, NULL, 0u, &pic);
        assert(pic.idr == info.idr);
        assert(info.qp == pic.qp);
        assert(reg_reads == reads + 3u);
        st.cmpx = payload_length * 37u;
        st.bits = info.slice_bytes * 8u;
        st.reg[0] = t20_moving(runs);
        st.reg[1] = 0x1000u;
        st.reg[2] = 0x1000u;
        assert(RCT20_End(&rc, &st, &pic) == 0);
    }
    }
    OpenIMP_T30_HelixDestroy(encoder);
    RCT20_Free(&rc);

    /* OPENIMP_T20_MBRC=1: the macroblock QP table from the luma plane goes
     * into the command list (0x4006c and VPU memory 0xc5800), with the
     * OEM's macroblock tuning (0x80034) in moving scenes */
    {
        uint8_t *luma = malloc(1280u * 736u * 3u / 2u + 64u);
        unsigned int tables = 0, tuned = 0;

        assert(luma && (uintptr_t)luma < 0xffffffffu);
        for (i = 0; i < 1280u * 736u * 3u / 2u + 64u; i++)
            luma[i] = (uint8_t)((i % 1280u) / 16u % 2u ? (i * 7u) >> 3
                                                          : 40u + (i / 1280u) % 9u);
        frame_virt = (uint32_t)(uintptr_t)luma;
        setenv("OPENIMP_T20_MBRC", "1", 1);
        encoder = create(1280, 720, 25, 10);
        p.mb_rc = 1;
        assert(RCT20_Init(&rc, &p) == 0);
        for (i = 0; i < 25u; i++) {
            RcT20Picture pic;
            RcT20Stats st;
            const uint32_t *table = NULL;
            uint32_t words;

            fill_payload(1500u + (i % 5u) * 4000u, 30);
            assert(encode(encoder, &info) == 0);
            RCT20_Start(&rc, info.idr, luma, 1280u, &pic);
            assert(info.qp == pic.qp);
            words = RCT20_QpTable(&rc, &table);
            assert(words && run_qptab_words == words);
            assert(run_qptab_ctrl == (0x80000000u | (words << 21) | 0xc5800u));
            assert(!memcmp(run_qptab, table, words * 4u));
            assert(run_80034 == (RCT20_Scene(&rc) ? 0x896783e6u : 0x8202u));
            tuned += RCT20_Scene(&rc) != 0u;
            tables++;
            st.cmpx = payload_length * 37u;
            st.bits = info.slice_bytes * 8u;
            st.reg[0] = t20_moving(runs);
            st.reg[1] = 0x1000u;
            st.reg[2] = 0x1000u;
            (void)RCT20_End(&rc, &st, &pic);
        }
        assert(tables == 25u && tuned > 0u && tuned < tables);
        OpenIMP_T30_HelixDestroy(encoder);
        RCT20_Free(&rc);
        unsetenv("OPENIMP_T20_MBRC");
        frame_virt = 0;
        free(luma);
    }

    /* IMP_Encoder_SetMbRC (HWEncoderParams.mb_rc) switches the macroblock
     * QP table at run time without the environment variable */
    {
        uint8_t *luma = malloc(1280u * 736u * 3u / 2u + 64u);
        HWEncoderParams mp;
        int k;

        assert(luma && (uintptr_t)luma < 0xffffffffu);
        for (i = 0; i < 1280u * 736u * 3u / 2u + 64u; i++)
            luma[i] = (uint8_t)(i * 13u >> 2);
        frame_virt = (uint32_t)(uintptr_t)luma;
        unsetenv("OPENIMP_T20_MBRC");
        encoder = create(1280, 720, 25, 10);
        memset(&mp, 0, sizeof(mp));
        mp.width = 1280;
        mp.height = 720;
        mp.fps_num = 25;
        mp.fps_den = 1;
        mp.gop_length = 10;
        mp.rc_mode = HW_RC_MODE_CBR;
        mp.bitrate = 2000000;
        mp.qp = 30;
        mp.min_qp = 20;
        mp.max_qp = 45;
        fill_payload(3000u, 30);
        run_qptab_words = 0;
        assert(encode(encoder, &info) == 0);
        assert(run_qptab_words > 0u);           /* default: on, as the OEM (i264e_param_default) */
        for (k = 0; k < 3; k++) {
            mp.mb_rc = k == 1 ? HW_MBRC_OFF : HW_MBRC_ON;
            assert(OpenIMP_T30_HelixUpdateParams(encoder, &mp) == 0);
            run_qptab_words = 0;
            assert(encode(encoder, &info) == 0);
            assert(k == 1 ? run_qptab_words == 0u : run_qptab_words > 0u);
            /* HW_MBRC_DEFAULT keeps the setting */
            mp.mb_rc = HW_MBRC_DEFAULT;
            assert(OpenIMP_T30_HelixUpdateParams(encoder, &mp) == 0);
            run_qptab_words = 0;
            assert(encode(encoder, &info) == 0);
            assert(k == 1 ? run_qptab_words == 0u : run_qptab_words > 0u);
        }
        OpenIMP_T30_HelixDestroy(encoder);
        frame_virt = 0;
        free(luma);
    }

    unsetenv("OPENIMP_T20_RC");     /* default: the OEM controller */
    encoder = create(1280, 720, 25, 10);
    reads = reg_reads;
    for (i = 0; i < 3u; i++)
        assert(encode(encoder, &info) == 0);
    assert(reg_reads > reads);
    OpenIMP_T30_HelixDestroy(encoder);

    setenv("OPENIMP_T20_RC", "0", 1);  /* =0: the GOP controller */
    encoder = create(1280, 720, 25, 10);
    reads = reg_reads;
    for (i = 0; i < 3u; i++)
        assert(encode(encoder, &info) == 0);
    assert(reg_reads == reads);
    OpenIMP_T30_HelixDestroy(encoder);
    unsetenv("OPENIMP_T20_RC");
    fill_payload(5000, 30);
}

/* On a T10 (OPENIMP_HELIX_SOC=t10) the OEM T10 controller runs by default
 * (OPENIMP_T10_RC=0: the GOP controller) (src/rc_t10) instead: no VPU register reads, every slice QP
 * as a second controller fed the slice size and cmpx chooses it. */
static void test_t10_rate_control(void)
{
    T30HelixEncoder *encoder;
    RcT10 rc;
    RcT10Params p;
    PictureInfo info;
    unsigned int i, reads;

    setenv("OPENIMP_HELIX_SOC", "t10", 1);
    setenv("OPENIMP_T10_RC", "1", 1);
    encoder = create(640, 360, 25, 10);
    RCT10_DefaultParams(&p);
    p.method = 1;
    p.width = 640;
    p.height = 360;
    p.gop = 10;
    p.fps_num = 25;
    p.fps_den = 1;
    p.qp = 30;
    p.min_qp = 20;
    p.max_qp = 45;
    p.bitrate = 2000;
    p.max_bitrate = 2000;
    p.static_time = 2;
    assert(RCT10_Init(&rc, &p) == 0);
    reads = reg_reads;
    for (i = 0; i < 45u; i++) {
        RcT10Picture pic;
        RcT10Stats st;

        fill_payload(1500u + (i % 7u) * 2500u + (i > 25u ? 20000u : 0u), 30);
        assert(encode(encoder, &info) == 0);
        RCT10_Start(&rc, info.idr, &pic);
        assert(pic.idr == info.idr);
        assert(info.qp == pic.qp);
        st.cmpx = payload_length * 37u;
        st.bits = info.slice_bytes * 8u;
        assert(RCT10_End(&rc, &st, &pic) == 0);
    }
    assert(reg_reads == reads);
    OpenIMP_T30_HelixDestroy(encoder);
    unsetenv("OPENIMP_T10_RC");
    unsetenv("OPENIMP_HELIX_SOC");
    fill_payload(5000, 30);
}
#endif

static void test_dma_footprint(void)
{
    unsigned int top_before = top_allocations;
    T30HelixEncoder *encoder = create(1920, 1080, 25, 25);

#if defined(PLATFORM_T20)
    /* room for the macroblock QP table of the T20 rate control */
    assert(allocation("t30-helix-desc")->size == 65536u);
#else
    assert(allocation("t30-helix-desc")->size == 16384u);
#endif
#if defined(PLATFORM_T21) && !defined(PLATFORM_T20)
    /* the stock 1 MiB EMC layout (996 KiB backed); the bitstream in the
     * shared buffer */
    assert(allocation("t30-helix-emc")->size == 0xf9000u);
    {
        uint32_t offsets[4];

        /* 0x3004c/0x30050/0x30054/0x30058 as the stock h264_api_enc
         * places them after 0x30018 */
        assert(T21_HelixScratchLayout(120, 68, offsets) == 0xf9000u);
        assert(offsets[0] == 0x30000u && offsets[1] == 0xb0000u &&
               offsets[2] == 0xd0000u && offsets[3] == 0xf8000u);
        /* the VPU writes 8 bytes per macroblock to 0x3004c */
        assert(T21_HelixScratchLayout(40, 23, offsets) == 143360u);
        assert(offsets[1] - offsets[0] >= 8u * 920u);
    }
    assert(allocation("t30-helix-bs") == NULL);
    assert(allocation("helix-bs")->size >= (1u << 20) + 4096u);
#else
    assert(allocation("t30-helix-emc") == NULL);
#if defined(PLATFORM_T20)
    /* T20/T10: no buffer of its own; the shared "vpuBs" is the stock
     * 0x12c000 pool (libimp 3.12.0), the 1 MiB window fits */
    assert(allocation("t30-helix-bs") == NULL);
    assert(allocation("helix-bs")->size == 0x12c000u);
    assert(OpenIMP_HelixBitstream_PoolSize() == 0x12c000u);
#endif
#endif
    OpenIMP_T30_HelixDestroy(encoder);
#if defined(PLATFORM_T20)
    /* a second channel adds no buffer; SetPoolSize stores like stock
     * (<= 0 refused), a pool below the window grows to the window */
    {
        T30HelixEncoder *second = create(1280, 720, 25, 25);
        PictureInfo info;

        assert(second && allocation("helix-bs")->size == 0x12c000u);
        assert(encode(second, &info) == 0);
        OpenIMP_T30_HelixDestroy(second);
    }
    assert(IMP_Encoder_SetPoolSize(0) == -1);
    assert(IMP_Encoder_SetPoolSize(-5) == -1);
    OpenIMP_HelixBitstream_Exit();
    assert(IMP_Encoder_SetPoolSize(600000) == 0);
    assert(OpenIMP_HelixBitstream_PoolSize() == 600000u);
    encoder = create(1920, 1080, 25, 25);
    assert(allocation("helix-bs")->size == (1u << 20));
    OpenIMP_T30_HelixDestroy(encoder);
    OpenIMP_HelixBitstream_Exit();
    assert(IMP_Encoder_SetPoolSize(0x12c000) == 0);
    assert(OpenIMP_HelixBitstream_Init() == 0);
#endif
#if defined(PLATFORM_T21) && !defined(PLATFORM_T20)
    /* 640x360: EMC scaled per macroblock (140 KiB), no buffer of its own
     * for the bitstream */
    encoder = create(640, 360, 25, 25);
    assert(allocation("t30-helix-emc")->size == 143360u);
    assert(allocation("t30-helix-bs") == NULL);
    OpenIMP_T30_HelixDestroy(encoder);
    /* the shared buffer is the stock pool size (1920 * 1080, page-rounded),
     * and IMP_Encoder_SetPoolSize keeps the 1080p window inside a smaller
     * pool: 600000 -> a 576 KiB window, no growth */
    assert(allocation("helix-bs")->size == 2076672u);
    OpenIMP_HelixBitstream_Exit();
    assert(IMP_Encoder_SetPoolSize(600000) == 0);
    encoder = create(1920, 1080, 25, 25);
    assert(allocation("helix-bs")->size == 602112u);
    {
        PictureInfo info;

        assert(encode(encoder, &info) == 0);
    }
    OpenIMP_T30_HelixDestroy(encoder);
    OpenIMP_HelixBitstream_Exit();
    assert(IMP_Encoder_SetPoolSize(0x1fa400) == 0);
    assert(OpenIMP_HelixBitstream_Init() == 0);
#endif
    /* every encoder buffer is long-lived: all of them from the top */
    assert(top_allocations > top_before);
}

static void test_large_frame_level(void)
{
    T30HelixEncoder *encoder = create(2560, 1440, 20, 25);
    PictureInfo info;

    assert(encode(encoder, &info) == 0);
    assert(info.idr && info.level == 50u);
    OpenIMP_T30_HelixDestroy(encoder);
}


/* The ISP writes only the visible lines of a macroblock-aligned NV12
 * buffer.  The encoder must replicate the last visible luma and chroma
 * rows into the padding it encodes (1080 -> 1088, 360 -> 368) and leave
 * the visible picture and macroblock-aligned frames untouched. */
static void check_bottom_padding(uint32_t width, uint32_t height)
{
    T30HelixEncoder *encoder = create(width, height, 25, 25);
    uint32_t aligned = (height + 15u) & ~15u;
    uint32_t chroma_height = (height + 1u) / 2u;
    size_t size = (size_t)width * aligned * 3u / 2u;
    uint8_t *buffer;
    uint8_t *expected;
    uint8_t *chroma;
    IMPFrameInfo frame;
    HWStreamBuffer *stream = NULL;
    uint64_t flushed;
    uint32_t row;
    size_t i;

    buffer = fake_rmem_map(size);
    expected = malloc(size);
    assert(expected);
    chroma = buffer + (size_t)width * aligned;
    for (i = 0; i < (size_t)width * height; i++)
        buffer[i] = (uint8_t)(16u + (i * 7u) % 200u);
    for (i = 0; i < (size_t)width * chroma_height; i++)
        chroma[i] = (i & 1u) ? 0xa5u : 0x5au;  /* known U/V, not grey */
    /* padding as an unwritten VBM buffer leaves it: zero (green) */
    memcpy(expected, buffer, size);
    for (row = height; row < aligned; row++)
        memcpy(expected + (size_t)width * row,
               expected + (size_t)width * (height - 1u), width);
    for (row = chroma_height; row < aligned / 2u; row++)
        memcpy(expected + (size_t)width * (aligned + row),
               expected + (size_t)width * (aligned + chroma_height - 1u),
               width);

    memset(&frame, 0, sizeof(frame));
    frame.width = width;
    frame.height = height;
    frame.size = (uint32_t)size;
    frame.phyAddr = (uint32_t)(uintptr_t)buffer;
    frame.virAddr = (uint32_t)(uintptr_t)buffer;
    frame.timeStamp = 1;
    in_run_window = 0;
    flushed = flushed_before_run;
    assert(OpenIMP_T30_HelixEncode(encoder, &frame, &stream) == 0);
    assert(stream);
    free((void *)(uintptr_t)stream->virt_addr);
    free(stream);
    assert(!memcmp(buffer, expected, size));
    for (i = 0; i < (size_t)width * (aligned / 2u); i++)
        assert(chroma[i] == ((i & 1u) ? 0xa5u : 0x5au));
    /* the padding the CPU wrote is written back before the VPU runs */
    assert(flushed_before_run - flushed >=
           (uint64_t)width * (aligned - height) +
           (uint64_t)width * (aligned / 2u - chroma_height));

    /* a frame too small for the aligned picture is never written */
    memset(buffer + (size_t)width * height, 0,
           (size_t)width * (aligned - height));
    frame.size = (uint32_t)(width * height * 3u / 2u);
    frame.timeStamp++;
    if (OpenIMP_T30_HelixEncode(encoder, &frame, &stream) == 0) {
        free((void *)(uintptr_t)stream->virt_addr);
        free(stream);
    }
    for (i = (size_t)width * height; i < (size_t)width * aligned; i++)
        assert(aligned == height || buffer[i] == 0u);

    free(expected);
    fake_rmem_unmap(buffer, size);
    OpenIMP_T30_HelixDestroy(encoder);
}

static void test_bottom_padding(void)
{
    check_bottom_padding(1920, 1080);
    check_bottom_padding(640, 360);
    check_bottom_padding(1280, 720);
}

/* Consecutive failures are counted for the caller's limit and reset by a
 * good picture; a frame too small for the visible picture is refused
 * before RUN and is not counted (a size of 0 is unknown and accepted). */
static void test_failure_count_and_input_size(void)
{
    T30HelixEncoder *encoder = create(640, 360, 25, 4);
    IMPFrameInfo frame;
    HWStreamBuffer *stream = NULL;
    PictureInfo info;
    unsigned int before;

    assert(OpenIMP_T30_HelixFailures(encoder) == 0u);
    run_result = -1;
    assert(encode(encoder, &info) != 0);
    assert(encode(encoder, &info) != 0);
    assert(encode(encoder, &info) != 0);
    assert(OpenIMP_T30_HelixFailures(encoder) == 3u);
    run_result = 0;
    assert(encode(encoder, &info) == 0);
    assert(OpenIMP_T30_HelixFailures(encoder) == 0u);

    memset(&frame, 0, sizeof(frame));
    frame.phyAddr = 0x10000000u;
    frame.size = 640u * 360u * 3u / 2u - 1u;
    before = runs;
    assert(OpenIMP_T30_HelixEncode(encoder, &frame, &stream) != 0);
    assert(!stream && runs == before);
    assert(OpenIMP_T30_HelixFailures(encoder) == 0u);
    frame.size = 640u * 360u * 3u / 2u;
    assert(OpenIMP_T30_HelixEncode(encoder, &frame, &stream) == 0);
    assert(stream && runs == before + 1u);
    free((void *)(uintptr_t)stream->virt_addr);
    free(stream);
    OpenIMP_T30_HelixDestroy(encoder);
}

#if defined(PLATFORM_T21) && !defined(PLATFORM_T20)
/* OPENIMP_T21_EPRC unset: the OEM picture rate controller (src/eprc) with
 * the statistics registers read after every picture; the IDR period follows
 * maxSameSceneCnt (same_scene_gops) in SMART rate control only (T21 1.0.33
 * i264e_validate_parameters sets it to 1 in every other mode, k = 4);
 * =0: the GOP controller, no reads. */
static void test_eprc(void)
{
    static const uint32_t modes[5] = { HW_RC_MODE_CBR, HW_RC_MODE_VBR,
                                       HW_RC_MODE_VBR, HW_RC_MODE_CBR,
                                       HW_RC_MODE_VBR };
    unsigned int k, frame, reads, pictures;

    for (k = 0; k < 5u; k++) {
        HWEncoderParams params;
        T30HelixEncoder *encoder = NULL;
        PictureInfo info;
        unsigned int idrs = 0;

        if (k != 3u)
            unsetenv("OPENIMP_T21_EPRC");
        else
            setenv("OPENIMP_T21_EPRC", "0", 1);
        memset(&params, 0, sizeof(params));
        params.width = 640;
        params.height = 360;
        params.fps_num = 15;
        params.fps_den = 1;
        params.gop_length = 5;
        params.rc_mode = modes[k];
        params.bitrate = 500000;
        params.qp = 30;
        params.min_qp = 20;
        params.max_qp = 51;
        params.rc_flags = HW_RC_FLAG_APP | (k == 1u || k == 2u ? HW_RC_FLAG_SMART : 0u);
        params.same_scene_gops = k == 1u || k == 4u ? 2u : 0u;
        assert(OpenIMP_T30_HelixCreate(&encoder, &params) == 0);
        reads = reg_reads;
        pictures = runs;
        for (frame = 0; frame < 20u; frame++) {
            assert(encode(encoder, &info) == 0);
            if (info.idr) {
                idrs++;
                assert(frame % (k == 1u ? 10u : 5u) == 0u);
            }
        }
        assert(idrs == (k == 1u ? 2u : 4u));
        pictures = runs - pictures;
        assert(pictures >= 20u);
        assert(reg_reads - reads == (k < 3u || k == 4u ? 25u * pictures : 0u));
        OpenIMP_T30_HelixDestroy(encoder);
    }
    unsetenv("OPENIMP_T21_EPRC");
}

/* IMP_Encoder_SetChnHSkip, T21 (docs/T21_HSKIP.md): in SMART rate control the pictures follow the OEM structure of
 * tests/hskip/hskip_vectors_t21.txt (frame_num, IDR, long-term flag, list modification, MMCO); BlackEnhance writes the
 * ENHANCE pictures in software (no VPU run, QP 0); every other rate-control mode keeps the plain GOP. */
static int hskip_vectors(unsigned int gop, unsigned int type, unsigned int *cnt, unsigned int *idr, unsigned int *ft,
                         unsigned int *ltr, unsigned int *reo, unsigned int *mm, unsigned int n)
{
    FILE *f = fopen("../hskip/hskip_vectors_t21.txt", "r");
    char line[256];
    unsigned int g, t, m, nn, same, i = 0;
    int ref, rec, idc;

    if (!f)
        return -1;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == 'S') {
            if (sscanf(line, "S %u %u %u %u %u", &g, &t, &m, &nn, &same) == 5 && g == gop && t == type &&
                m == gop - 1u && nn == 1u && same == 0u) {
                for (i = 0; i < n && fgets(line, sizeof(line), f); i++)
                    if (sscanf(line, "%u %u %u %d %d %u %u %d %u", &cnt[i], &idr[i], &ft[i], &ref, &rec, &ltr[i],
                               &reo[i], &idc, &mm[i]) != 9)
                        break;
                fclose(f);
                return i == n ? 0 : -1;
            }
        }
    }
    fclose(f);
    return -1;
}

static void test_t21_hskip(void)
{
    static const unsigned int types[4] = { 1u, 2u, 5u, 6u };
    unsigned int k, smart, black, frame;

    unsetenv("OPENIMP_T21_EPRC");
    hs_parse = 1;
    for (k = 0; k < 4u; k++) {
        for (smart = 0; smart < 2u; smart++) {
            for (black = 0; black < 2u; black++) {
                HWEncoderParams params;
                T30HelixEncoder *encoder = NULL;
                PictureInfo info;
                unsigned int cnt[24], idr[24], ft[24], ltr[24], reo[24], mm[24], runs0, blacks = 0;

                assert(hskip_vectors(8u, types[k], cnt, idr, ft, ltr, reo, mm, 24u) == 0);
                memset(&params, 0, sizeof(params));
                params.width = 640;
                params.height = 360;
                params.fps_num = 15;
                params.fps_den = 1;
                params.gop_length = 8;
                params.rc_mode = smart ? HW_RC_MODE_VBR : HW_RC_MODE_CBR;
                params.bitrate = 500000;
                params.qp = 30;
                params.min_qp = 20;
                params.max_qp = 51;
                params.rc_flags = HW_RC_FLAG_APP | (smart ? HW_RC_FLAG_SMART : 0u);
                params.hskip_type = types[k];
                params.hskip_m = 4;
                params.hskip_n = 1;
                params.hskip_flags = HW_HSKIP_SET | (black ? HW_HSKIP_BLACK : 0u);
                assert(OpenIMP_T30_HelixCreate(&encoder, &params) == 0);
                runs0 = runs;
                for (frame = 0; frame < 24u; frame++) {
                    fill_payload(200u + frame, 20u);
                    assert(encode(encoder, &info) == 0);
                    if (smart) {
                        assert(info.idr == (int)idr[frame]);
                        assert(info.frame_num == cnt[frame]);
                        assert(info.ltr == (int)ltr[frame]);
                        assert(info.reorder == (int)reo[frame]);
                        assert(info.mmco == (mm[frame] == 2u));
                        if (black && ft[frame] == 3u) {
                            assert(info.qp == 0u && info.slice_bytes < 60u);        /* the all-skip slice */
                            blacks++;
                        } else {
                            assert(info.slice_bytes > 200u);
                        }
                    } else {
                        assert(info.idr == (frame % 8u == 0u));
                        assert(info.frame_num == frame % 8u && !info.reorder && !info.mmco && info.slice_bytes > 200u);
                    }
                }
                assert(runs - runs0 == 24u - blacks);   /* a black picture does not reach the VPU */
                assert(!smart || !black || types[k] == 5u || types[k] == 6u || blacks > 0u);
                OpenIMP_T30_HelixDestroy(encoder);
            }
        }
    }
    {
        /* 1080p: the encoder is created with the shared reference ring; the structure switches to separate pictures at its IDR */
        HWEncoderParams params;
        T30HelixEncoder *encoder = NULL;
        PictureInfo info;
        unsigned int cnt[24], idr[24], ft[24], ltr[24], reo[24], mm[24];

        assert(hskip_vectors(8u, 5u, cnt, idr, ft, ltr, reo, mm, 24u) == 0);
        memset(&params, 0, sizeof(params));
        params.width = 1920;
        params.height = 1080;
        params.fps_num = 15;
        params.fps_den = 1;
        params.gop_length = 8;
        params.rc_mode = HW_RC_MODE_VBR;
        params.bitrate = 500000;
        params.qp = 30;
        params.min_qp = 20;
        params.max_qp = 51;
        params.rc_flags = HW_RC_FLAG_APP | HW_RC_FLAG_SMART;
        assert(OpenIMP_T30_HelixCreate(&encoder, &params) == 0);
        for (frame = 0; frame < 24u; frame++) {
            fill_payload(300u + frame, 20u);
            assert(encode(encoder, &info) == 0);
            assert(info.idr == (frame % 8u == 0u));
            if (frame == 11u) {
                /* IMP_Encoder_SetChnHSkip at run time: waits for the next IDR (frame 16) */
                params.hskip_type = 5;
                params.hskip_m = 7;
                params.hskip_n = 1;
                params.hskip_flags = HW_HSKIP_SET;
                assert(OpenIMP_T30_HelixUpdateParams(encoder, &params) == 0);
            }
            if (frame >= 16u) {
                assert(info.frame_num == cnt[frame - 16u] && info.reorder == (int)reo[frame - 16u] &&
                       info.mmco == (mm[frame - 16u] == 2u) && info.ltr == (int)ltr[frame - 16u]);
            } else {
                assert(!info.reorder && !info.mmco && !info.ltr);
            }
        }
        OpenIMP_T30_HelixDestroy(encoder);
    }
    hs_parse = 0;
}

/* IMP_Encoder_SetChnHSkip at run time (same_scene_gops): as the OEM
 * i264e_idr_reconfig the change waits for the next IDR, which follows the
 * old period, then the new period counts (no extra IDR).  SMART rate control
 * only (T21 1.0.33 i264e_validate_parameters); CBR keeps the plain GOP. */
static void test_eprc_runtime_hskip(void)
{
    HWEncoderParams params;
    T30HelixEncoder *encoder = NULL;
    PictureInfo info;
    unsigned int frame, smart;

    unsetenv("OPENIMP_T21_EPRC");
    for (smart = 0; smart < 2u; smart++) {
        memset(&params, 0, sizeof(params));
        params.width = 640;
        params.height = 360;
        params.fps_num = 15;
        params.fps_den = 1;
        params.gop_length = 5;
        params.rc_mode = smart ? HW_RC_MODE_VBR : HW_RC_MODE_CBR;
        params.bitrate = 500000;
        params.qp = 30;
        params.min_qp = 20;
        params.max_qp = 51;
        params.rc_flags = HW_RC_FLAG_APP | (smart ? HW_RC_FLAG_SMART : 0u);
        assert(OpenIMP_T30_HelixCreate(&encoder, &params) == 0);
        for (frame = 0; frame < 7u; frame++) {
            assert(encode(encoder, &info) == 0);
            assert(info.idr == (frame % 5u == 0u));
        }
        params.same_scene_gops = 2;
        assert(OpenIMP_T30_HelixUpdateParams(encoder, &params) == 0);
        for (; frame < 31u; frame++) {
            assert(encode(encoder, &info) == 0);
            if (smart)
                assert(info.idr == (frame == 10u || frame == 20u || frame == 30u));
            else
                assert(info.idr == (frame % 5u == 0u));
        }
        OpenIMP_T30_HelixDestroy(encoder);
    }
}
#endif

#if defined(PLATFORM_T21) && !defined(PLATFORM_T20)
/* eprc macroblock rate control: off by default (OPENIMP_EPRC_MBRC unset),
 * on with OPENIMP_EPRC_MBRC=1 from the second picture (the OEM's first
 * picture has none), IMP_Encoder_SetMbRC (HWEncoderParams.mb_rc) switches
 * it from the next picture. */
static void test_eprc_mbrc(void)
{
    HWEncoderParams params;
    T30HelixEncoder *encoder = NULL;
    PictureInfo info;
    unsigned int frame, k;

    unsetenv("OPENIMP_T21_EPRC");
    for (k = 0; k < 2u; k++) {
        if (k)
            setenv("OPENIMP_EPRC_MBRC", "1", 1);
        else
            unsetenv("OPENIMP_EPRC_MBRC");
        memset(&params, 0, sizeof(params));
        params.width = 640;
        params.height = 360;
        params.fps_num = 15;
        params.fps_den = 1;
        params.gop_length = 5;
        params.rc_mode = HW_RC_MODE_CBR;
        params.bitrate = 500000;
        params.qp = 30;
        params.min_qp = 20;
        params.max_qp = 51;
        params.rc_flags = HW_RC_FLAG_APP;
        assert(OpenIMP_T30_HelixCreate(&encoder, &params) == 0);
        for (frame = 0; frame < 4u; frame++) {
            assert(encode(encoder, &info) == 0);
            /* the OEM T21 picture control registers by picture type */
            assert(list_ctrl[0] == (info.idr ? 0x060407c1u : 0x030487c1u));
            assert(list_ctrl[1] == (info.idr ? 0x61615921u : 0x61615c21u));
            if (k && frame) {
                assert((list_mbrc[0] & 0xffu) == 0x15u);
                assert(list_mbrc[1] == 0x00044432u);
                assert(list_mbrc[2] == 0x012c0096u &&
                       list_mbrc[3] == 0x03e80190u &&
                       list_mbrc[4] == 0x0fa007d0u && list_mbrc[5] == 0u);
            } else {
                assert((list_mbrc[0] & 0xffu) == 0u && list_mbrc[1] == 0u &&
                       list_mbrc[2] == 0u);
            }
        }
        params.mb_rc = k ? HW_MBRC_OFF : HW_MBRC_ON;
        assert(OpenIMP_T30_HelixUpdateParams(encoder, &params) == 0);
        assert(encode(encoder, &info) == 0);
        assert(k ? list_mbrc[1] == 0u : list_mbrc[1] == 0x00044432u);
        params.mb_rc = HW_MBRC_DEFAULT;
        assert(OpenIMP_T30_HelixUpdateParams(encoder, &params) == 0);
        assert(encode(encoder, &info) == 0);
        assert(k ? list_mbrc[1] == 0u : list_mbrc[1] == 0x00044432u);
        OpenIMP_T30_HelixDestroy(encoder);
        encoder = NULL;
    }
    unsetenv("OPENIMP_EPRC_MBRC");
}
#endif

/* the chroma_qp_index_offset (and T21 second_chroma_qp_index_offset) of
 * the PPS of the last IDR access unit (au.rbsp[1]) */
static int32_t pps_chroma_offset(int32_t *second)
{
    Bits bits;
    int32_t offset;

    assert(au.types[1] == 8);
    bits.data = au.rbsp[1];
    bits.offset = 0;
    assert(bits_ue(&bits) == 0u && bits_ue(&bits) == 0u);
    (void)bits_read(&bits, 2);          /* cabac, bottom_field_pic_order */
    assert(bits_ue(&bits) == 0u);       /* one slice group */
    (void)bits_ue(&bits);
    (void)bits_ue(&bits);
    (void)bits_read(&bits, 3);          /* weighted prediction */
    (void)bits_se(&bits);               /* pic_init_qp */
    (void)bits_se(&bits);               /* pic_init_qs */
    offset = bits_se(&bits);
    *second = offset;
    (void)bits_read(&bits, 3);
#if defined(PLATFORM_T21) && !defined(PLATFORM_T20)
    {
        unsigned int list, j;

        assert(bits_read(&bits, 1) == 1u);  /* transform_8x8_mode */
        assert(bits_read(&bits, 1) == 1u);  /* pic_scaling_matrix_present */
        for (list = 0; list < 8u; list++) {
            if (!bits_read(&bits, 1))
                continue;
            {
                int32_t last = 8, next = 8;

                for (j = 0; j < (list < 6u ? 16u : 64u); j++) {
                    if (next)
                        next = (last + bits_se(&bits) + 256) % 256;
                    last = next ? next : last;
                }
            }
        }
        *second = bits_se(&bits);
    }
#endif
    /* rbsp_stop_one_bit, then zeros */
    assert(bits_read(&bits, 1) == 1u);
    while (bits.offset % 8u)
        assert(bits_read(&bits, 1) == 0u);
    assert(bits.offset / 8u == au.rbsp_length[1]);
    return offset;
}

/* IMP_Encoder_SetChnROI (OpenIMP_T30_HelixSetRoi) and
 * IMP_Encoder_SetH264TransCfg (HWEncoderParams.chroma_qp_offset): the OEM
 * EFE ROI words in every command list (T10/T20, T21 unless
 * OPENIMP_T21_ROI=0; T30: as the OEM), the chroma QP offset in the PPS and
 * 0x40120 from the next IDR on (T10: neither) */
static void test_roi_chroma(int t10)
{
    static const uint8_t roi[8][7] = {
        {1, 0, 20, 2, 5, 3, 6},         /* absolute QP 20 */
        {1, 1, (uint8_t)-5, 0, 39, 0, 1}, /* relative -5 */
        {0},
        {1, 1, 8, 10, 12, 20, 22},
        {0},
        {0, 1, 3, 1, 2, 3, 4},          /* disabled, rectangle kept */
        {0},
        {1, 0, 51, 7, 7, 9, 9},
    };
    static const uint8_t none[8][7];
    T30HelixEncoder *encoder;
    HWEncoderParams p;
    PictureInfo info;
    int32_t second;
    int roi_hw, chroma_hw;
    unsigned int i;

#if defined(PLATFORM_T21) && !defined(PLATFORM_T20)
    unsetenv("OPENIMP_T21_ROI");
#endif
    encoder = create(640, 360, 25, 4);
    memset(&p, 0, sizeof(p));
    p.width = 640;
    p.height = 360;
    p.fps_num = 25;
    p.fps_den = 1;
    p.gop_length = 4;
    p.rc_mode = HW_RC_MODE_CBR;
    p.bitrate = 2000000;
    p.qp = 30;
    p.min_qp = 20;
    p.max_qp = 45;
#if defined(PLATFORM_T20)
    roi_hw = 1;
    chroma_hw = !t10;
#elif defined(PLATFORM_T21)
    roi_hw = 1;                 /* beyond vendor; OPENIMP_T21_ROI=0: off */
    chroma_hw = 1;
#else
    roi_hw = 1;                 /* T30: OEM H264E_T30_SliceInit programs them */
    chroma_hw = 0;
#endif
    (void)t10;
    assert(encode(encoder, &info) == 0 && info.idr);
    assert(pps_chroma_offset(&second) == 0 && second == 0);
    for (i = 0; i < 10u; i++)
        assert(list_roi[i] == 0u);
    assert(!list_chroma_seen || list_chroma == 0u);
    assert(OpenIMP_T30_HelixSetRoi(encoder, roi) == 0);
    p.chroma_qp_offset = -3;
    assert(OpenIMP_T30_HelixUpdateParams(encoder, &p) == 0);
    /* ROI from the next picture; the chroma offset waits for the IDR */
    assert(encode(encoder, &info) == 0 && !info.idr);
    if (roi_hw) {
        /* qp << 2 | rel << 1 | en per region, 6-bit QP field */
        /* Helix_H264_RoiSanitize (src/t30/helix_roi.h) limits the QPs of
         * the picture: absolute inside [max QP - 25, min QP + 25], the
         * spread of all windows at most 25.  Absolute 51 (region 7) ->
         * slice QP + 13: T21 (picture QP 28) 41, T20 (QP 20) 33. */
        if (t10) {
            /* T10 test picture QP 44, no macroblock QP table (every macroblock outside the windows is at
             * the slice QP): absolute 20 kept (44 - 24, inside the 25 steps), the other windows lowered
             * for the 25 spread: relative +8 -> +1, absolute 51 -> 45 */
            assert(list_roi[0] == 0x0700ef51u);
            assert(list_roi[1] == (0x0eu << 8 | 0xb5u << 24));
        } else {
#if defined(PLATFORM_T20)
            assert(list_roi[1] == (0x0eu << 8 | 0x85u << 24));
#elif defined(PLATFORM_T21)
            assert(list_roi[1] == (0x0eu << 8 | 0xa5u << 24));
#else
            /* T30: picture QP 30 -> absolute 51 of region 7 becomes 43 */
            assert(list_roi[1] == (0x0eu << 8 | 0xadu << 24));
#endif
            assert(list_roi[0] == (0x51u | (0xecu | 3u)
                                   << 8 | 0x23u << 24));
        }
        assert(list_roi[2] == 0x06030502u && list_roi[3] == 0x01002700u);
        assert(list_roi[4] == 0u && list_roi[5] == 0x16140c0au);
        assert(list_roi[6] == 0u && list_roi[7] == 0x04030201u);
        assert(list_roi[8] == 0u && list_roi[9] == 0x09090707u);
    } else {
        for (i = 0; i < 10u; i++)
            assert(list_roi[i] == 0u);
    }
    assert(!list_chroma_seen || list_chroma == 0u);
    assert(encode(encoder, &info) == 0 && !info.idr);
    assert(encode(encoder, &info) == 0 && !info.idr);
    assert(encode(encoder, &info) == 0 && info.idr);
    assert(pps_chroma_offset(&second) == (chroma_hw ? -3 : 0));
    assert(second == (chroma_hw ? -3 : 0));
    assert(chroma_hw ? list_chroma_seen && list_chroma == 0x1du
                     : !list_chroma_seen || list_chroma == 0u);
    /* back to 0 and no regions */
    assert(OpenIMP_T30_HelixSetRoi(encoder, none) == 0);
    p.chroma_qp_offset = 12;
    assert(OpenIMP_T30_HelixUpdateParams(encoder, &p) == 0);
    for (i = 1; i < 4u; i++) {
        assert(encode(encoder, &info) == 0 && !info.idr);
        assert(list_roi[0] == 0u && list_roi[2] == 0u);
        assert(chroma_hw ? list_chroma == 0x1du : list_chroma == 0u);
    }
    assert(encode(encoder, &info) == 0 && info.idr);
    assert(pps_chroma_offset(&second) == (chroma_hw ? 12 : 0) &&
           second == (chroma_hw ? 12 : 0));
    assert(chroma_hw ? list_chroma == 0x0cu : list_chroma == 0u);
    p.chroma_qp_offset = 0;
    assert(OpenIMP_T30_HelixUpdateParams(encoder, &p) == 0);
    for (i = 0; i < 4u; i++)
        assert(encode(encoder, &info) == 0);
    assert(info.idr && pps_chroma_offset(&second) == 0 && second == 0);
    OpenIMP_T30_HelixDestroy(encoder);
#if defined(PLATFORM_T21) && !defined(PLATFORM_T20)
    /* OPENIMP_T21_ROI=0: the regions stay out of the T21 command list */
    setenv("OPENIMP_T21_ROI", "0", 1);
    encoder = create(640, 360, 25, 4);
    assert(OpenIMP_T30_HelixSetRoi(encoder, roi) == 0);
    assert(encode(encoder, &info) == 0 && info.idr);
    assert(list_roi[0] == 0u && list_roi[2] == 0u && list_roi[9] == 0u);
    OpenIMP_T30_HelixDestroy(encoder);
    unsetenv("OPENIMP_T21_ROI");
#endif
}


#if defined(PLATFORM_T20)
/* IMP_Encoder_SetFisheyeEnableStatus (src/fisheye, OEM i264e_icodec_enc): with the flag the application's ROI
 * table is cleared for every picture and region 0 (macroblocks x 0..3, y 0..1) is coded at QP min(QP, 15) on I
 * pictures and at whole seconds of the frame rate; without the flag the table is the application's */
static void test_fisheye(int t10)
{
    static const uint8_t roi[8][7] = { {1, 1, (uint8_t)-4, 0, 39, 0, 22}, {0}, {0}, {0}, {0}, {0}, {0}, {0} };
    HWEncoderParams params;
    T30HelixEncoder *encoder = NULL;
    PictureInfo info;
    unsigned int n, regions = 0, flag;

    for (flag = 0; flag < 2u; flag++) {
        memset(&params, 0, sizeof(params));
        params.width = 640;
        params.height = 360;
        params.fps_num = 25;
        params.fps_den = 1;
        params.gop_length = 20;
        params.rc_mode = HW_RC_MODE_FIXQP;
        params.qp = 30;
        params.min_qp = 20;
        params.max_qp = 45;
        params.fisheye = flag;
        assert(OpenIMP_T30_HelixCreate(&encoder, &params) == 0);
        assert(OpenIMP_T30_HelixSetRoi(encoder, roi) == 0);
        for (n = 0; n < 60u; n++) {
            int idr_or_second;

            assert(encode(encoder, &info) == 0);
            idr_or_second = info.idr || n % 25u == 0u;
            if (!flag) {
                /* the application's relative -4 window, no region 0 of ours */
                assert(n == 0u || list_roi[0] == (uint32_t)(((uint8_t)-4 << 2 | 3) & 0xff));
                continue;
            }
            if (idr_or_second) {
                /* qp << 2 | en for region 0, IDR coded at FixQP - 3 = 27, region QP 15: 0x3d; x 0..3, y 0..1 */
                assert(list_roi[0] == 0x3du && list_roi[2] == 0x01000300u);
                assert(list_roi[1] == 0u && list_roi[3] == 0u && list_roi[9] == 0u);
                regions++;
            } else {
                assert(list_roi[0] == 0u && list_roi[2] == 0u && list_roi[3] == 0u);
            }
        }
        OpenIMP_T30_HelixDestroy(encoder);
    }
    /* IDRs at 0, 20, 40; whole seconds at 0, 25, 50 */
    assert(regions == 5u);
    (void)t10;
}
#endif

#if defined(PLATFORM_T21) && !defined(PLATFORM_T20)
/* T21 IMP_Encoder_SetFisheyeEnableStatus (src/fisheye, docs/T21_FISHEYE.md): the vendor's i264e table never reaches the T21
 * hardware.  Without the flag the application's window is programmed (beyond vendor), with it the table is dropped and no region
 * 0 is coded, on I pictures and at whole seconds alike */
static void test_fisheye_t21(void)
{
    static const uint8_t roi[8][7] = { {1, 1, (uint8_t)-4, 0, 39, 0, 22}, {0}, {0}, {0}, {0}, {0}, {0}, {0} };
    HWEncoderParams params;
    T30HelixEncoder *encoder = NULL;
    PictureInfo info;
    unsigned int n, flag, late, idrs = 0;

    unsetenv("OPENIMP_T21_ROI");
    for (late = 0; late < 2u; late++)
    for (flag = 0; flag < 2u; flag++) {
        memset(&params, 0, sizeof(params));
        params.width = 640;
        params.height = 360;
        params.fps_num = 25;
        params.fps_den = 1;
        params.gop_length = 20;
        params.rc_mode = HW_RC_MODE_FIXQP;
        params.qp = 30;
        params.min_qp = 20;
        params.max_qp = 45;
        params.fisheye = late ? 0u : flag;      /* late: IMP_Encoder_CreateChn sets the flag after the encoder was made */
        assert(OpenIMP_T30_HelixCreate(&encoder, &params) == 0);
        if (late) {
            params.fisheye = flag;
            assert(OpenIMP_T30_HelixUpdateParams(encoder, &params) == 0);
        }
        assert(OpenIMP_T30_HelixSetRoi(encoder, roi) == 0);
        for (n = 0; n < 60u; n++) {
            unsigned int i;

            assert(encode(encoder, &info) == 0);
            if (info.idr)
                idrs++;
            if (!flag) {
                if (n > 0u)
                    assert(list_roi[0] != 0u);          /* the application's window */
                continue;
            }
            for (i = 0; i < 10u; i++)
                assert(list_roi[i] == 0u);
        }
        OpenIMP_T30_HelixDestroy(encoder);
    }
    assert(idrs == 12u);                                /* IDRs at 0, 20, 40 in all four runs */
}

/* T21 IMP_Encoder_SetH264TransCfg (docs/T21_H264TRANS.md): a value outside -12..12 is coded as -12 or 12 (vendor 1.0.33), a new
 * value is the encoder's own only from the next IDR picture on (the vendor's Get shows the applied value) */
static void test_chroma_clamp_t21(void)
{
    static const int ask[] = { 13, -13, 100, 5 }, want[] = { 12, -12, 12, 5 };
    T30HelixEncoder *encoder = create(640, 360, 25, 4);
    HWEncoderParams p;
    PictureInfo info;
    int32_t second;
    unsigned int k, i;

    memset(&p, 0, sizeof(p));
    p.width = 640;
    p.height = 360;
    p.fps_num = 25;
    p.fps_den = 1;
    p.gop_length = 4;
    p.rc_mode = HW_RC_MODE_CBR;
    p.bitrate = 2000000;
    p.qp = 30;
    p.min_qp = 20;
    p.max_qp = 45;
    assert(encode(encoder, &info) == 0 && info.idr);
    assert(OpenIMP_T30_HelixChromaQpOffset(encoder) == 0);
    for (k = 0; k < 4u; k++) {
        p.chroma_qp_offset = ask[k];
        assert(OpenIMP_T30_HelixUpdateParams(encoder, &p) == 0);
        for (i = 0; i < 3u; i++) {
            assert(encode(encoder, &info) == 0 && !info.idr);
            assert(OpenIMP_T30_HelixChromaQpOffset(encoder) == (k ? want[k - 1] : 0));
        }
        assert(encode(encoder, &info) == 0 && info.idr);
        assert(OpenIMP_T30_HelixChromaQpOffset(encoder) == want[k]);
        assert(pps_chroma_offset(&second) == want[k] && second == want[k]);
    }
    OpenIMP_T30_HelixDestroy(encoder);
}

int OpenIMP_T21_PpsWithChromaOffset(bs_t *bits, int offset);

/* the rewritten T21 PPS with offset 0 is the stock one bit for bit */
static void test_t21_pps_rewrite(void)
{
    static const uint8_t stock[] = {
        0xee, 0x3c, 0xe1, 0x00, 0x42, 0x42, 0x00, 0x84, 0x84, 0x04, 0x4c,
        0x52, 0x1b, 0x93, 0xc5, 0x7c, 0x9f, 0x93, 0xf9, 0x3f, 0x27, 0xc9,
        0xe6, 0xe4, 0xc9, 0x24, 0x2c, 0x22, 0x42, 0x90, 0x9c, 0x9e, 0x4f,
        0xaf, 0xc9, 0xfd, 0x7e, 0x4f, 0xaf, 0x27, 0x26, 0xa4, 0xc0,
    };
    uint8_t out[128];
    bs_t bits;

    memset(out, 0, sizeof(out));
    bs_init(&bits, out, sizeof(out));
    assert(OpenIMP_T21_PpsWithChromaOffset(&bits, 0) == 0);
    assert(bs_pos(&bits) == 8 * (int)sizeof(stock));
    assert(!memcmp(out, stock, sizeof(stock)));
}
#endif

#if defined(PLATFORM_T30)
/* OEM T30 picture rate control (src/eprc/eprc_t30.c, the default for CBR,
 * VBR and SMART): every picture's QP is the one a second controller chooses
 * when it is fed the slice sizes, the 25 statistics registers and the
 * channel's cmpx the encoder hands it; OPENIMP_T30_EPRC=0 (the GOP
 * controller) reads no register. */
static void test_t30_eprc(void)
{
    T30HelixEncoder *encoder;
    Eprc30 rc;
    EprcParams p;
    PictureInfo info;
    static uint8_t slice[EPRC30_SLICE_SIZE];
    unsigned int i, j, reads;

    unsetenv("OPENIMP_T30_EPRC");
    encoder = create(1280, 720, 25, 10);
    memset(&p, 0, sizeof(p));
    p.rc_mode = EPRC_MODE_CBR;
    p.width = 1280;
    p.height = 720;
    p.gop = 10;
    p.fps_num = 25;
    p.fps_den = 1;
    p.min_qp = 20;
    p.max_qp = 45;
    p.bitrate = 2000;
    p.max_bitrate = 2000;
    p.frm_qp_step = 3;
    p.gop_qp_step = 15;
    p.static_time = 2;
    p.change_pos = 80;
    p.quality = 4;
    p.init_qp = -1;
    assert(EPRC30_Init(&rc, &p, slice) == 0);
    for (i = 0; i < 45u; i++) {
        EprcPicture pic;
        uint32_t regs[EPRC30_STAT_REGS];

        fill_payload(1500u + (i % 7u) * 2500u + (i > 25u ? 20000u : 0u), 30);
        reads = reg_reads;
        assert(encode(encoder, &info) == 0);
        assert(EPRC30_FrameStart(&rc, info.idr, &pic) == 0);
        assert(info.qp == pic.qp);
        assert(reg_reads == reads + EPRC30_STAT_REGS);
        for (j = 0; j < EPRC30_STAT_REGS; j++)
            regs[j] = ((0x13200000u + EPRC30_StatRegs[j]) * 2654435761u) >> 9;
        assert(EPRC30_FrameEnd(&rc, info.slice_bytes, regs,
                               payload_length * 37u, &pic, 1) == 0);
    }
    /* a failed run finishes the started picture without a judge (the
     * controller stays in step), the next picture codes */
    run_result = -1;
    assert(encode(encoder, &info) != 0);
    run_result = 0;
    {
        EprcPicture pic;
        uint32_t regs[EPRC30_STAT_REGS];

        assert(EPRC30_FrameStart(&rc, 0, &pic) == 0);
        for (j = 0; j < EPRC30_STAT_REGS; j++)
            regs[j] = ((0x13200000u + EPRC30_StatRegs[j]) * 2654435761u) >> 9;
        /* the encoder passes its bitstream window and the last table */
        (void)EPRC30_FrameEnd(&rc, allocation(BS_TAG)->size - 256u,
                              regs, payload_length * 37u, &pic, 0);
        reads = reg_reads;
        assert(encode(encoder, &info) == 0);
        assert(EPRC30_FrameStart(&rc, info.idr, &pic) == 0);
        assert(info.qp == pic.qp);
    }
    OpenIMP_T30_HelixDestroy(encoder);
    EPRC30_Free(&rc);

    setenv("OPENIMP_T30_EPRC", "0", 1);
    encoder = create(1280, 720, 25, 10);
    reads = reg_reads;
    for (i = 0; i < 5u; i++)
        assert(encode(encoder, &info) == 0);
    assert(reg_reads == reads);
    OpenIMP_T30_HelixDestroy(encoder);
    unsetenv("OPENIMP_T30_EPRC");
}

/* OPENIMP_T30_START_QP (#216, OpenIMP extra, default off). */
static void test_t30_start_qp(void)
{
    T30HelixEncoder *encoder;
    PictureInfo info;
    int q2048_25, q2048_15, q1080, q720;

    /* the pure mapping */
    assert(t30_start_qp(NULL, 1280, 720, 25, 1, 2000) == -1);
    assert(t30_start_qp("", 1280, 720, 25, 1, 2000) == -1);
    assert(t30_start_qp("vendor", 1280, 720, 25, 1, 2000) == -1);
    assert(t30_start_qp("36", 1280, 720, 25, 1, 2000) == 36);
    assert(t30_start_qp("10", 1280, 720, 25, 1, 2000) == 10);
    assert(t30_start_qp("51", 1280, 720, 25, 1, 2000) == 51);
    assert(t30_start_qp("9", 1280, 720, 25, 1, 2000) == -1);
    assert(t30_start_qp("52", 1280, 720, 25, 1, 2000) == -1);
    assert(t30_start_qp("abc", 1280, 720, 25, 1, 2000) == -1);
    assert(t30_start_qp("30x", 1280, 720, 25, 1, 2000) == -1);
    assert(t30_start_qp("auto", 0, 720, 25, 1, 2000) == -1);
    assert(t30_start_qp("auto", 1280, 720, 25, 1, 0) == -1);
    /* auto: b39 points (2000 kbit/s); vendor 26 where it is fine */
    q720 = t30_start_qp("auto", 1280, 720, 25, 1, 2000);
    q1080 = t30_start_qp("auto", 1920, 1080, 25, 1, 2000);
    q2048_15 = t30_start_qp("auto", 2048, 1536, 15, 1, 2000);
    q2048_25 = t30_start_qp("auto", 2048, 1536, 25, 1, 2000);
    assert(q720 == 26);
    assert(t30_start_qp("auto", 640, 360, 25, 1, 2000) == 26);
    assert(q2048_25 == 41);                 /* b39: converged P QP 41-42 */
    assert(q1080 >= 30 && q1080 <= 38);     /* b39: IDR 30, P 32 */
    assert(q2048_15 >= 34 && q2048_15 <= 41);
    assert(q720 <= q2048_15 && q2048_15 <= q1080 && q1080 <= q2048_25); /* monotone in bpp */
    assert(t30_start_qp("auto", 2048, 1536, 25, 1, 100) == 46);     /* clamp */
    assert(t30_start_qp("auto", 2048, 1536, 25, 1, 200000) == 26);

    /* through the encoder: unset = vendor start (controller default 26),
     * a fixed integer is the first IDR's QP, an invalid value is vendor */
    unsetenv("OPENIMP_T30_EPRC");
    unsetenv("OPENIMP_T30_START_QP");
    encoder = create(1280, 720, 25, 10);
    fill_payload(1500u, 30);
    assert(encode(encoder, &info) == 0);
    assert(info.idr && info.qp == 26);
    OpenIMP_T30_HelixDestroy(encoder);
    setenv("OPENIMP_T30_START_QP", "36", 1);
    encoder = create(1280, 720, 25, 10);
    assert(encode(encoder, &info) == 0);
    assert(info.idr && info.qp == 36);
    OpenIMP_T30_HelixDestroy(encoder);
    setenv("OPENIMP_T30_START_QP", "99", 1);
    encoder = create(1280, 720, 25, 10);
    assert(encode(encoder, &info) == 0);
    assert(info.idr && info.qp == 26);
    OpenIMP_T30_HelixDestroy(encoder);
    unsetenv("OPENIMP_T30_START_QP");
    setenv("OPENIMP_T30_EPRC", "0", 1);
}
#endif

int main(void)
{
    unsigned int i;

    /* Keep every heap block below 4 GiB for the 32-bit address fields. */
    mallopt(M_MMAP_THRESHOLD, 1 << 30);
#if defined(PLATFORM_T30)
    setenv("OPENIMP_T30_EPRC", "0", 1);    /* the checks before test_t30_eprc are of the GOP controller */
#endif
    fill_payload(5000, 30);
    test_gop_and_failures();
    test_failure_count_and_input_size();
    test_runtime_parameters();
    test_large_frame_level();
    test_dma_footprint();
    test_bottom_padding();
    test_unaligned_width_rejected();
    test_color2grey();
    test_roi_chroma(0);
#if defined(PLATFORM_T30)
    test_t30_eprc();
    test_t30_start_qp();
#endif
#if defined(PLATFORM_T21) && !defined(PLATFORM_T20)
    test_t21_pps_rewrite();
    test_eprc();
    test_eprc_runtime_hskip();
    test_t21_hskip();
    test_eprc_mbrc();
    test_fisheye_t21();
    test_chroma_clamp_t21();
#endif
#if defined(PLATFORM_T20)
    test_t20_fixqp();
    test_t20_rate_control();
    test_t10_rate_control();
    test_t20_superframe();
    setenv("OPENIMP_HELIX_SOC", "t10", 1);
    test_roi_chroma(1);
    test_fisheye(1);
    unsetenv("OPENIMP_HELIX_SOC");
    test_fisheye(0);
#endif
    /* nothing leaks; the shared bitstream buffer is kept for the process */
    for (i = 0; i < 16u; i++)
        assert(!allocations[i].mapping ||
               !strcmp(allocations[i].tag, "helix-bs"));
#if defined(PLATFORM_T20)
    printf("T20 Helix encoder tests passed (%u runs)\n", runs);
#elif defined(PLATFORM_T21)
    printf("T21 Helix encoder tests passed (%u runs)\n", runs);
#else
    printf("T30 Helix encoder tests passed (%u runs)\n", runs);
#endif
    return 0;
}
