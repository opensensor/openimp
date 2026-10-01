/* Host test of the native T23 Helix encoder (src/t30/t30_helix_encoder.c
 * built with PLATFORM_T23) end to end, against a fake /dev/soc_vpu.
 *
 * The fake VPU checks each submitted job the way the kernel and the core
 * would see it: the T23 channel_node size and ioctl numbers, the Helix
 * core id, a terminated command list in reserved memory, every DMA address
 * inside an allocation, every Helix-internal address in the T23 window.
 * It then produces real CABAC slice data for the picture the command list
 * describes - every macroblock I_PCM (a lossless copy of the input frame)
 * for an IDR, every macroblock skipped for a P picture - initialised from
 * the QP and slice type found in the command list.  The encoder wraps that
 * into SPS/PPS/slice NAL units exactly as it does on the camera, so a
 * stock H.264 decoder can check the bitstream assembly bit for bit:
 *
 *   helix_stream_test encode OUT.h264 OUT.ref   # writes the stream
 *   ffmpeg -i OUT.h264 -f rawvideo -pix_fmt nv12 OUT.nv12
 *   helix_stream_test verify OUT.nv12 OUT.ref   # compares the pictures
 *
 * The encode step also injects a failed job (bitstream-full status) and an
 * IDR request and checks GOP, IDR and rate-control behaviour. */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "t30/t30_helix_encoder.h"
#include "t30/h264enc/common.h"
#include "dma_alloc.h"

#define WIDTH 640u
#define HEIGHT 360u
#define MBW ((WIDTH + 15u) / 16u)
#define MBH ((HEIGHT + 15u) / 16u)
#define LUMA (MBW * 16u * MBH * 16u)
#define FRAME_BYTES (LUMA * 3u / 2u)
#define FRAMES 16u
#define GOP 5u

#define FAKE_FD 4242
#define RMEM_PHYS 0x02a00000u
#define RMEM_SIZE (24u << 20)

/* ------------------------------------------------------------------ */
/* fake reserved memory                                                */

static uint8_t *rmem;
static uint32_t rmem_used;
static struct { uint32_t phys, size; } allocations[32];
static unsigned int allocation_count;

static uint32_t phys_of(const void *virt)
{
    return RMEM_PHYS + (uint32_t)((const uint8_t *)virt - rmem);
}

static void *virt_of(uint32_t phys)
{
    assert(phys >= RMEM_PHYS && phys - RMEM_PHYS < RMEM_SIZE);
    return rmem + (phys - RMEM_PHYS);
}

static int in_allocation(uint32_t phys, uint32_t length)
{
    unsigned int i;

    for (i = 0; i < allocation_count; i++)
        if (allocations[i].size && phys >= allocations[i].phys &&
            phys - allocations[i].phys + length <= allocations[i].size)
            return 1;
    return 0;
}

int DMA_AllocDescriptor(IMPDMABufferInfo *info, int size, const char *tag)
{
    uint32_t aligned = ((uint32_t)size + 4095u) & ~4095u;

    (void)tag;
    if (rmem_used + aligned > RMEM_SIZE || allocation_count >= 32u)
        return -1;
    memset(info, 0, sizeof(*info));
    info->virt_addr = (uint32_t)(uintptr_t)(rmem + rmem_used);
    info->phys_addr = RMEM_PHYS + rmem_used;
    info->size = (uint32_t)size;
    allocations[allocation_count].phys = info->phys_addr;
    allocations[allocation_count++].size = (uint32_t)size;
    rmem_used += aligned;
    return 0;
}

int DMA_FreePhys(uint32_t phys)
{
    unsigned int i;

    for (i = 0; i < allocation_count; i++)
        if (allocations[i].phys == phys)
            allocations[i].size = 0;
    return 0;
}

uint32_t DMA_VirtToPhys(const void *virt)
{
    const uint8_t *p = virt;

    if (p >= rmem && p < rmem + RMEM_SIZE)
        return phys_of(p);
    return (uint32_t)(uintptr_t)virt;
}

static unsigned int input_writebacks;

int DMA_RmemFlushCache(void *virt, uint32_t size, int dir)
{
    uint8_t *p = virt;

    assert(p >= rmem && p + size <= rmem + RMEM_SIZE);
    assert(dir == 1 || dir == 2);
    if (dir == 1 && !in_allocation(phys_of(p), 1))
        input_writebacks++;     /* the capture frame, not an encoder buffer */
    return 0;
}

/* ------------------------------------------------------------------ */
/* CABAC arithmetic encoder (H.264 9.3.4), only what I_PCM/P_Skip need  */

static const uint8_t range_lps[64][4] = {
    {128,176,208,240},{128,167,197,227},{128,158,187,216},{123,150,178,205},
    {116,142,169,195},{111,135,160,185},{105,128,152,175},{100,122,144,166},
    {95,116,137,158},{90,110,130,150},{85,104,123,142},{81,99,117,135},
    {77,94,111,128},{73,89,105,122},{69,85,100,116},{66,80,95,110},
    {62,76,90,104},{59,72,86,99},{56,69,81,94},{53,65,77,89},
    {51,62,73,85},{48,59,69,80},{46,56,66,76},{43,53,63,72},
    {41,50,59,69},{39,48,56,65},{37,45,54,62},{35,43,51,59},
    {33,41,48,56},{32,39,46,53},{30,37,43,50},{29,35,41,48},
    {27,33,39,45},{26,31,37,43},{24,30,35,41},{23,28,33,39},
    {22,27,32,37},{21,26,30,35},{20,24,29,33},{19,23,27,31},
    {18,22,26,30},{17,21,25,28},{16,20,23,27},{15,19,22,25},
    {14,18,21,24},{14,17,20,23},{13,16,19,22},{12,15,18,21},
    {12,14,17,20},{11,14,16,19},{11,13,15,18},{10,12,15,17},
    {10,12,14,16},{9,11,13,15},{9,11,12,14},{8,10,12,14},
    {8,9,11,13},{7,9,11,12},{7,9,10,12},{7,8,10,11},
    {6,8,9,11},{6,7,9,10},{6,7,8,9},{2,2,2,2},
};
static const uint8_t trans_lps[64] = {
    0,0,1,2,2,4,4,5,6,7,8,9,9,11,11,12,13,13,15,15,16,16,18,18,19,19,21,21,
    22,22,23,24,24,25,26,26,27,27,28,29,29,30,30,30,31,32,32,33,33,33,34,34,
    35,35,35,36,36,36,37,37,37,38,38,63,
};

typedef struct {
    uint8_t *out;
    uint32_t bits;
    uint32_t capacity;
    uint32_t low;
    uint32_t range;
    int outstanding;
    int first;
    uint8_t p_state[460];
    uint8_t mps[460];
} Cabac;

static void put(Cabac *c, unsigned int bit)
{
    assert(c->bits / 8u < c->capacity);
    if (bit)
        c->out[c->bits / 8u] |= (uint8_t)(0x80u >> (c->bits % 8u));
    c->bits++;
}

static void put_bit(Cabac *c, unsigned int bit)
{
    if (c->first)
        c->first = 0;
    else
        put(c, bit);
    while (c->outstanding > 0) {
        put(c, 1u - bit);
        c->outstanding--;
    }
}

static void renorm(Cabac *c)
{
    while (c->range < 256u) {
        if (c->low < 256u) {
            put_bit(c, 0);
        } else if (c->low >= 512u) {
            c->low -= 512u;
            put_bit(c, 1);
        } else {
            c->low -= 256u;
            c->outstanding++;
        }
        c->range <<= 1;
        c->low <<= 1;
    }
}

static void engine_init(Cabac *c)
{
    c->low = 0;
    c->range = 510;
    c->outstanding = 0;
    c->first = 1;
}

static void decision(Cabac *c, unsigned int ctx, unsigned int bin)
{
    unsigned int s = c->p_state[ctx];
    unsigned int lps = range_lps[s][(c->range >> 6) & 3u];

    c->range -= lps;
    if (bin != c->mps[ctx]) {
        c->low += c->range;
        c->range = lps;
        if (s == 0u)
            c->mps[ctx] = (uint8_t)(1u - c->mps[ctx]);
        c->p_state[ctx] = trans_lps[s];
    } else if (s < 62u) {
        c->p_state[ctx] = (uint8_t)(s + 1u);
    }
    renorm(c);
}

static void flush(Cabac *c)
{
    c->range = 2;
    renorm(c);
    put_bit(c, (c->low >> 9) & 1u);
    put(c, (c->low >> 8) & 1u);
    put(c, 1);
}

static void terminate(Cabac *c, unsigned int bin)
{
    c->range -= 2;
    if (bin) {
        c->low += c->range;
        flush(c);
    } else {
        renorm(c);
    }
}

/* ------------------------------------------------------------------ */
/* fake /dev/soc_vpu                                                    */

typedef struct {
    uint32_t clist, vlist, mdelay, channel_id;
    int32_t vpu_id;
    uint32_t codecdir, workphase, status, output_len, dma_addr;
    int32_t thread_id;
    uint32_t cmpx, n_flag, ncu_addr;
    uint32_t frame_type, overflow_cnt, ivdc_mem_line, data_threshold;
    uint32_t max_bs_act, reserved;
    uint64_t time;
} FakeNode;

enum { FAULT_NONE, FAULT_BSFULL, FAULT_TIMEOUT };
static int next_fault;
static unsigned int jobs;
static unsigned int channels_open;
static uint32_t last_qp_seen[2];

int __real_open(const char *path, int flags, ...);
int __real_close(int fd);

int __wrap_open(const char *path, int flags, ...)
{
    if (strcmp(path, "/dev/soc_vpu") == 0)
        return FAKE_FD;
    return __real_open(path, flags, 0644);
}

int __wrap_close(int fd)
{
    if (fd == FAKE_FD)
        return 0;
    return __real_close(fd);
}

static uint32_t reg_value(const uint32_t *list, size_t pairs, uint32_t reg)
{
    size_t i;

    for (i = 0; i < pairs; i++)
        if ((list[2 * i + 1] & 0xffffcu) == reg)
            return list[2 * i];
    fprintf(stderr, "fake VPU: register 0x%05x missing\n", reg);
    abort();
}

static void check_addresses(const uint32_t *list, size_t pairs, int p)
{
    static const uint32_t dma_registers[] = {
        0x40010, 0x40014, 0x10014, 0x10018, 0x60008, 0x6000c, 0x60014,
        0x60018, 0xb0008, 0xb000c, 0xb0014, 0xb0018, 0xb0030, 0xb0034,
        0x90024, 0x30004, 0x30018, 0x3004c, 0x30050, 0x30054, 0x30058,
    };
    size_t i;
    unsigned int r;

    for (r = 0; r < sizeof(dma_registers) / sizeof(dma_registers[0]); r++) {
        uint32_t v = reg_value(list, pairs, dma_registers[r]);

        if (!in_allocation(v, 1) &&
            !(v >= reg_value(list, pairs, 0x40010) &&
              v < reg_value(list, pairs, 0x40010) + FRAME_BYTES)) {
            fprintf(stderr, "fake VPU: 0x%05x=0x%08x outside memory\n",
                    dma_registers[r], v);
            abort();
        }
    }
    if (p) {
        assert(in_allocation(reg_value(list, pairs, 0x5006c), 1));
        assert(in_allocation(reg_value(list, pairs, 0x50070), 1));
    }
    for (i = 0; i < pairs; i++) {
        uint32_t reg = list[2 * i + 1] & 0xffffcu;
        uint32_t v = list[2 * i];

        if ((reg >= 0x92000u && reg < 0x92800u) || reg == 0x80198u ||
            reg == 0x8002cu || reg == 0x80174u ||
            (reg >= 0x80800u && reg < 0x80980u) ||
            (reg >= 0x40040u && reg < 0x40100u))
            continue;
        if ((v >> 24) == 0x13u && (v >> 20) != 0x131u) {
            fprintf(stderr, "fake VPU: 0x%05x=0x%08x outside the T23 "
                    "Helix window\n", reg, v);
            abort();
        }
    }
}

static uint32_t encode_picture(const uint32_t *list, size_t pairs, int p,
                               uint8_t *out, uint32_t capacity)
{
    uint32_t mbs = reg_value(list, pairs, 0x90008);
    uint32_t mbw = (mbs >> 16) & 0xffu;
    uint32_t mbh = mbs >> 24;
    uint32_t sde = reg_value(list, pairs, 0x90018);
    uint32_t qp = (sde >> 8) & 0x3fu;
    uint32_t strides = reg_value(list, pairs, 0x40038);
    uint32_t stride = strides >> 16;
    const uint8_t *luma = virt_of(reg_value(list, pairs, 0x40010));
    const uint8_t *chroma = virt_of(reg_value(list, pairs, 0x40014));
    h264_cabac_t contexts;
    Cabac c;
    uint32_t x, y, i, j;

    assert(mbw == MBW && mbh == MBH && stride == WIDTH);
    assert((sde & 0xffu) == (p ? 0x32u : 0x31u));
    last_qp_seen[p] = qp;
    memset(&c, 0, sizeof(c));
    memset(out, 0, capacity);
    c.out = out;
    c.capacity = capacity;
    /* the encoder writes cabac_init_idc 0 */
    h264_cabac_context_init(&contexts, p ? SLICE_TYPE_P : SLICE_TYPE_I,
                            (int)qp, 0);
    for (i = 0; i < 460u; i++) {
        unsigned int m = contexts.state[i];

        c.p_state[i] = (uint8_t)(m <= 63u ? 63u - m : m - 64u);
        c.mps[i] = (uint8_t)(m > 63u);
    }
    engine_init(&c);
    for (y = 0; y < mbh; y++) {
        for (x = 0; x < mbw; x++) {
            int last = x + 1u == mbw && y + 1u == mbh;

            if (p) {
                decision(&c, 11, 1);            /* mb_skip_flag */
            } else {
                /* mb_type I_PCM: bin0 = 1, then the terminate bin */
                decision(&c, 3u + (x > 0u) + (y > 0u), 1);
                terminate(&c, 1);
                while (c.bits % 8u)
                    put(&c, 0);                 /* pcm_alignment_zero_bit */
                for (j = 0; j < 16u; j++)
                    for (i = 0; i < 16u; i++) {
                        assert(c.bits / 8u < capacity);
                        out[c.bits / 8u] =
                            luma[(y * 16u + j) * stride + x * 16u + i];
                        c.bits += 8;
                    }
                for (j = 0; j < 2u; j++)        /* Cb then Cr */
                    for (i = 0; i < 64u; i++) {
                        uint32_t row = y * 8u + i / 8u;
                        uint32_t col = x * 8u + i % 8u;

                        out[c.bits / 8u] =
                            chroma[row * stride + col * 2u + j];
                        c.bits += 8;
                    }
                engine_init(&c);
            }
            terminate(&c, last ? 1u : 0u);      /* end_of_slice_flag */
        }
    }
    return (c.bits + 7u) / 8u;
}

int __wrap_ioctl(int fd, unsigned long request, ...)
{
    va_list args;
    FakeNode *node;
    const uint32_t *list;
    size_t pairs = 0;
    int p;
    uint32_t bitstream;

    va_start(args, request);
    node = va_arg(args, FakeNode *);
    va_end(args);
    assert(fd == FAKE_FD);
    switch (request) {
    case 0xc0586300u:                       /* REQUEST */
        assert(node->mdelay >= 100u && node->mdelay <= 20000u);
        node->clist = 0x8000u + channels_open;
        node->channel_id = channels_open++;
        return 0;
    case 0xc0586301u:                       /* RELEASE */
        assert(node->workphase == 2u);
        channels_open--;
        return 0;
    case 0xc0586302u:                       /* RUN */
        break;
    default:
        fprintf(stderr, "fake VPU: unexpected ioctl 0x%lx\n", request);
        abort();
    }
    jobs++;
    assert(node->vpu_id == 0x02000001 && node->codecdir == 0u);
    assert(node->thread_id == -1 && node->frame_type == 0u);
    assert((node->dma_addr & 0x7fu) == 0u);
    assert(in_allocation(node->dma_addr, 8));
    list = virt_of(node->dma_addr);
    while (1) {
        uint32_t command = list[2 * pairs + 1];

        assert(command & 0x80000000u);
        assert(in_allocation(node->dma_addr, (uint32_t)(pairs + 1u) * 8u));
        pairs++;
        if (command & 0x40000000u)
            break;
    }
    p = (reg_value(list, pairs, 0x40000) >> 4) & 1;
    assert(pairs == (p ? 1034u : 1015u));
    check_addresses(list, pairs, p);

    if (next_fault == FAULT_TIMEOUT) {
        next_fault = FAULT_NONE;
        errno = EIO;
        return -1;
    }
    bitstream = reg_value(list, pairs, 0x90024);
    assert(in_allocation(bitstream, 1u << 20));
    node->output_len = encode_picture(list, pairs, p, virt_of(bitstream),
                                      1u << 20);
    node->status = 0x301u;
    if (next_fault == FAULT_BSFULL) {
        next_fault = FAULT_NONE;
        node->status |= 1u << 20;
    }
    return 0;
}

/* ------------------------------------------------------------------ */

static uint8_t pattern(uint32_t frame, uint32_t plane, uint32_t x,
                       uint32_t y)
{
    return (uint8_t)(plane == 0u ? x * 3u + y * 5u + frame * 29u
                                 : (plane == 1u ? x * 7u + frame * 13u + 64u
                                                : y * 11u + frame * 3u));
}

static void make_frame(uint8_t *mem, uint32_t frame)
{
    uint32_t x, y;

    for (y = 0; y < MBH * 16u; y++)
        for (x = 0; x < WIDTH; x++)
            mem[y * WIDTH + x] = pattern(frame, 0, x, y);
    for (y = 0; y < MBH * 8u; y++)
        for (x = 0; x < WIDTH / 2u; x++) {
            mem[LUMA + y * WIDTH + 2u * x] = pattern(frame, 1, x, y);
            mem[LUMA + y * WIDTH + 2u * x + 1u] = pattern(frame, 2, x, y);
        }
}

/* NAL unit types of an access unit, in order */
static unsigned int nal_types(const uint8_t *d, uint32_t n, int *types)
{
    unsigned int count = 0;
    uint32_t i;

    for (i = 0; i + 4u < n; i++)
        if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 0 && d[i + 3] == 1) {
            types[count++] = d[i + 4] & 0x1f;
            i += 3;
        }
    return count;
}

static int encode(const char *stream_path, const char *ref_path)
{
    HWEncoderParams params;
    T30HelixEncoder *encoder = NULL;
    FILE *stream = fopen(stream_path, "wb");
    FILE *ref = fopen(ref_path, "wb");
    uint8_t *frame_mem;
    uint32_t frame;
    uint32_t last_idr = 0;
    unsigned int written = 0;
    unsigned int idrs = 0;

    assert(stream && ref);
    memset(&params, 0, sizeof(params));
    params.width = WIDTH;
    params.height = HEIGHT;
    params.fps_num = 15;
    params.fps_den = 1;
    params.gop_length = GOP;
    params.rc_mode = HW_RC_MODE_CBR;
    params.bitrate = 500000;
    params.qp = 30;
    params.min_qp = 20;
    params.max_qp = 45;
    assert(OpenIMP_T30_HelixCreate(&encoder, &params) == 0);
    assert(channels_open == 1u);

    /* the capture frame lives in reserved memory, outside the encoder's
     * allocations, like an OpenIMP FrameSource buffer */
    frame_mem = rmem + RMEM_SIZE - FRAME_BYTES - 4096u;
    for (frame = 0; frame < FRAMES; frame++) {
        IMPFrameInfo info;
        HWStreamBuffer *out = NULL;
        int types[8];
        unsigned int n;
        int expect_idr;

        make_frame(frame_mem, frame);
        memset(&info, 0, sizeof(info));
        info.width = WIDTH;
        info.height = HEIGHT;
        info.pixfmt = 0x3231564eu;
        info.size = FRAME_BYTES;
        info.virAddr = (uint32_t)(uintptr_t)frame_mem;
        info.phyAddr = phys_of(frame_mem);
        info.timeStamp = (int64_t)frame * 66666;

        if (frame == 7u)
            assert(OpenIMP_T30_HelixRequestIDR(encoder) == 0);
        if (frame == 9u) {
            /* runtime bitrate change goes through the rate controller */
            HWEncoderParams change = params;

            change.bitrate = 250000;
            assert(OpenIMP_T30_HelixReconfigure(encoder, &change) == 0);
        }
        if (frame == 12u) {
            /* a frame-rate change re-sends SPS/PPS with an IDR */
            HWEncoderParams change = params;

            change.bitrate = 250000;
            change.fps_num = 10;
            assert(OpenIMP_T30_HelixReconfigure(encoder, &change) == 0);
        }
        if (frame == 10u)
            next_fault = FAULT_BSFULL;
        if (frame == 14u)
            next_fault = FAULT_TIMEOUT;
        if (frame == 10u || frame == 14u) {
            assert(OpenIMP_T30_HelixEncode(encoder, &info, &out) == -1);
            assert(OpenIMP_T30_HelixFailures(encoder) == 1u);
            continue;
        }
        assert(OpenIMP_T30_HelixEncode(encoder, &info, &out) == 0);
        assert(OpenIMP_T30_HelixFailures(encoder) == 0u);
        n = nal_types((const uint8_t *)(uintptr_t)out->virt_addr,
                      out->length, types);
        /* GOP 5; 7 requested; 11 and 15 follow failed pictures; 12
         * follows a frame-rate change */
        expect_idr = frame == 0u || frame == 5u || frame == 7u ||
                     frame == 11u || frame == 12u || frame == 15u;
        if (expect_idr) {
            assert(n == 3u && types[0] == 7 && types[1] == 8 &&
                   types[2] == 5);
            assert(out->frame_type == HW_FRAME_TYPE_I);
            last_idr = frame;
            idrs++;
        } else {
            assert(n == 1u && types[0] == 1);
            assert(out->frame_type == HW_FRAME_TYPE_P);
        }
        assert(out->timestamp == (uint64_t)info.timeStamp);
        assert(fwrite((const void *)(uintptr_t)out->virt_addr, 1,
                      out->length, stream) == out->length);
        /* what the decoder must show: the last IDR's input */
        assert(fwrite(&last_idr, sizeof(last_idr), 1, ref) == 1u);
        written++;
        free((void *)(uintptr_t)out->virt_addr);
        free(out);
    }
    /* a frame outside reserved memory or too small never reaches the VPU */
    {
        IMPFrameInfo bad;
        HWStreamBuffer *out = NULL;
        static uint8_t not_rmem[16];
        unsigned int before = jobs;

        memset(&bad, 0, sizeof(bad));
        bad.virAddr = (uint32_t)(uintptr_t)not_rmem;
        bad.phyAddr = 0x02000000u;
        bad.size = FRAME_BYTES;
        assert(OpenIMP_T30_HelixEncode(encoder, &bad, &out) == -1);
        bad.virAddr = (uint32_t)(uintptr_t)frame_mem;
        bad.phyAddr = phys_of(frame_mem);
        bad.size = FRAME_BYTES - 1u;
        assert(OpenIMP_T30_HelixEncode(encoder, &bad, &out) == -1);
        assert(jobs == before);
    }
    assert(input_writebacks >= written);
    OpenIMP_T30_HelixDestroy(encoder);
    assert(channels_open == 0u);
    fclose(stream);
    fclose(ref);
    printf("encoded %u access units (%u IDR) in %u VPU jobs, "
           "last QP I=%u P=%u\n", written, idrs, jobs, last_qp_seen[0],
           last_qp_seen[1]);
    return 0;
}

static int verify(const char *decoded_path, const char *ref_path)
{
    FILE *decoded = fopen(decoded_path, "rb");
    FILE *ref = fopen(ref_path, "rb");
    static uint8_t picture[WIDTH * HEIGHT * 3u / 2u];
    uint32_t source;
    unsigned int count = 0;

    assert(decoded && ref);
    while (fread(&source, sizeof(source), 1, ref) == 1u) {
        uint32_t x, y;

        if (fread(picture, 1, sizeof(picture), decoded) != sizeof(picture)) {
            fprintf(stderr, "decoded stream ends after %u pictures\n",
                    count);
            return 1;
        }
        for (y = 0; y < HEIGHT; y++)
            for (x = 0; x < WIDTH; x++)
                if (picture[y * WIDTH + x] != pattern(source, 0, x, y)) {
                    fprintf(stderr, "picture %u luma (%u,%u) = %u, want "
                            "%u\n", count, x, y, picture[y * WIDTH + x],
                            pattern(source, 0, x, y));
                    return 1;
                }
        for (y = 0; y < HEIGHT / 2u; y++)
            for (x = 0; x < WIDTH / 2u; x++) {
                const uint8_t *uv = picture + WIDTH * HEIGHT +
                                    y * WIDTH + 2u * x;

                if (uv[0] != pattern(source, 1, x, y) ||
                    uv[1] != pattern(source, 2, x, y)) {
                    fprintf(stderr, "picture %u chroma (%u,%u) wrong\n",
                            count, x, y);
                    return 1;
                }
            }
        count++;
    }
    if (fread(picture, 1, 1, decoded) != 0u) {
        fprintf(stderr, "decoder produced extra pictures\n");
        return 1;
    }
    printf("decoded %u pictures, all identical to the expected input\n",
           count);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 4)
        return 2;
    /* the encoder keeps addresses in 32-bit words, as on MIPS */
    mallopt(M_MMAP_MAX, 0);
    rmem = mmap(NULL, RMEM_SIZE, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    assert(rmem != MAP_FAILED);
    if (strcmp(argv[1], "encode") == 0)
        return encode(argv[2], argv[3]);
    if (strcmp(argv[1], "verify") == 0)
        return verify(argv[2], argv[3]);
    return 2;
}
