/*
 * Host test for the Helix hardware JPEG encoder (src/t30/helix_jpeg.c).
 *
 * /dev/soc_vpu and the rmem allocator are fakes.  The fake RUN executes the
 * VDMA command list the encoder wrote: it collects the register writes,
 * checks the list layout, and then codes the picture the way the JPGC is
 * programmed -- EFE geometry, source planes and stride, the JPGC_QMEM
 * reciprocals and the JPGC_HUFE codes -- into the bitstream buffer.  A small
 * baseline decoder then reads the complete JFIF file back using only its
 * own header (DQT, SOF0, DHT, SOS), so a header table that disagrees with
 * the tables loaded into the core shows up as a broken picture.
 *
 * Addresses travel in 32-bit fields: the fakes hand out MAP_32BIT memory,
 * and the binary is linked without PIE.
 */

#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <malloc.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include <imp/imp_common.h>

#include "dma_alloc.h"
#include "t30/helix_jpeg.h"

#define FAKE_FD 78
#if defined(PLATFORM_T23)
#define CHANNEL_REQUEST 0xc0586300u
#define CHANNEL_RELEASE 0xc0586301u
#define CHANNEL_RUN     0xc0586302u
#define SRAM            0x131f0000u
#else
#define CHANNEL_REQUEST 0xc0386300u
#define CHANNEL_RELEASE 0xc0386301u
#define CHANNEL_RUN     0xc0386302u
#define SRAM            0x132f0000u
#endif

typedef struct {
    uint32_t clist, vlist, mdelay, channel_id;
    int32_t vpu_id;
    uint32_t codecdir, workphase, status, output_len, dma_addr;
    int32_t thread_id;
    uint32_t cmpx, n_flag, ncu_addr;
#if defined(PLATFORM_T23)
    uint32_t frame_type, overflow_cnt, ivdc_mem_line, data_threshold;
    uint32_t max_bs_act, reserved;
    uint64_t time;
#endif
} FakeChannel;

static const uint8_t zigzag[64] = {
    0, 1, 5, 6, 14, 15, 27, 28, 2, 4, 7, 13, 16, 26, 29, 42,
    3, 8, 12, 17, 25, 30, 41, 43, 9, 11, 18, 24, 31, 40, 44, 53,
    10, 19, 23, 32, 39, 45, 52, 54, 20, 22, 33, 38, 46, 51, 55, 60,
    21, 34, 37, 47, 50, 56, 59, 61, 35, 36, 48, 49, 57, 58, 62, 63
};

/* ---- fakes ---- */

typedef struct {
    void *mapping;
    uint32_t size;
} FakeAllocation;

static FakeAllocation allocations[16];
static unsigned int live_allocations;
static unsigned int runs, requests, releases;
static int fail_runs;            /* RUN returns -1 */
static uint32_t force_status;    /* reported instead of 0x11 */
static int force_overflow;       /* report a length >= the buffer */
static uint32_t regs[0x100000 / 4];
static uint32_t last_pairs;
static uint32_t last_raw_y;
static uint32_t last_bitstream_buffer;

static void *fake_map(uint32_t size)
{
    void *mapping = mmap(NULL, size, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);

    assert(mapping != MAP_FAILED);
    return mapping;
}

static int fake_alloc(IMPDMABufferInfo *info, int size)
{
    unsigned int i;
    void *mapping = fake_map((uint32_t)size);

    for (i = 0; i < 16u && allocations[i].mapping; i++)
        ;
    assert(i < 16u);
    allocations[i].mapping = mapping;
    allocations[i].size = (uint32_t)size;
    live_allocations++;
    memset(info, 0, sizeof(*info));
    info->virt_addr = (uint32_t)(uintptr_t)mapping;
    info->phys_addr = (uint32_t)(uintptr_t)mapping;
    info->size = (uint32_t)size;
    return 0;
}

int DMA_AllocDescriptor(IMPDMABufferInfo *info, int size, const char *tag)
{
    (void)tag;
    return fake_alloc(info, size);
}

int DMA_AllocDescriptorTop(IMPDMABufferInfo *info, int size, const char *tag)
{
    (void)tag;
    return fake_alloc(info, size);
}

int DMA_FreePhys(uint32_t phys_addr)
{
    unsigned int i;

    for (i = 0; i < 16u; i++)
        if ((uint32_t)(uintptr_t)allocations[i].mapping == phys_addr) {
            munmap(allocations[i].mapping, allocations[i].size);
            memset(&allocations[i], 0, sizeof(allocations[i]));
            live_allocations--;
            return 0;
        }
    assert(!"freeing unknown DMA buffer");
    return -1;
}

/* fake arena accounting: off (no arena) unless a test sets it */
static int rmem_stats_on;
static size_t rmem_largest;

int DMA_RmemStats(size_t *used, size_t *size, size_t *largest)
{
    if (!rmem_stats_on)
        return -1;
    if (used)
        *used = 1u << 20;
    if (size)
        *size = 23u << 20;
    if (largest)
        *largest = rmem_largest;
    return 0;
}

int DMA_RmemFlushCache(void *virt_addr, uint32_t size, int dir)
{
    (void)virt_addr;
    (void)size;
    assert(dir == 1 || dir == 2);
    return 0;
}

/* bytes from address to the end of the DMA buffer holding it */
static uint32_t allocation_size(uint32_t address)
{
    unsigned int i;

    for (i = 0; i < 16u; i++) {
        uint32_t base = (uint32_t)(uintptr_t)allocations[i].mapping;

        if (base && address >= base && address < base + allocations[i].size)
            return base + allocations[i].size - address;
    }
    assert(!"not a DMA buffer");
    return 0;
}

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

/* ---- JPGC model ---- */

typedef struct {
    uint8_t *out;
    uint32_t size;
    uint32_t capacity;
    uint32_t bits;
    int count;
} BitWriter;

static void put_byte(BitWriter *w, uint8_t byte)
{
    assert(w->size + 2u <= w->capacity);
    w->out[w->size++] = byte;
    if (byte == 0xff)
        w->out[w->size++] = 0x00;
}

static void put_bits(BitWriter *w, uint32_t code, int length)
{
    int i;

    for (i = length - 1; i >= 0; i--) {
        w->bits = (w->bits << 1) | ((code >> i) & 1u);
        if (++w->count == 8) {
            put_byte(w, (uint8_t)w->bits);
            w->bits = 0;
            w->count = 0;
        }
    }
}

static void put_hufe(BitWriter *w, uint32_t entry)
{
    int length = (int)(entry >> 8) + 1;
    uint32_t code = entry & 0xffu;

    assert(entry != 0xfffu);
    if (length > 8)
        code |= ((1u << (length - 8)) - 1u) << 8;
    put_bits(w, code, length);
}

static int magnitude_bits(int value)
{
    int bits = 0;

    value = value < 0 ? -value : value;
    while (value) {
        bits++;
        value >>= 1;
    }
    return bits;
}

static int quantize(double coefficient, uint32_t entry)
{
    double step;

    if (entry <= 1u)
        step = 1.0;
    else
        step = (double)(1u << (11u + (entry >> 11))) / (double)(entry & 0x7ffu);
    return (int)lround(coefficient / step);
}

static void code_block(BitWriter *w, const double block[64],
                       const uint32_t *qmem, const uint32_t *ac,
                       const uint32_t *dc, int *predictor)
{
    int coefficient[64];
    int u, v, x, y, run = 0, diff, bits;

    for (v = 0; v < 8; v++)
        for (u = 0; u < 8; u++) {
            double sum = 0.0;

            for (y = 0; y < 8; y++)
                for (x = 0; x < 8; x++)
                    sum += block[y * 8 + x] *
                           cos((2 * x + 1) * u * M_PI / 16.0) *
                           cos((2 * y + 1) * v * M_PI / 16.0);
            sum *= 0.25 * (u ? 1.0 : M_SQRT1_2) * (v ? 1.0 : M_SQRT1_2);
            coefficient[zigzag[v * 8 + u]] =
                quantize(sum, qmem[zigzag[v * 8 + u]]);
        }
    diff = coefficient[0] - *predictor;
    *predictor = coefficient[0];
    bits = magnitude_bits(diff);
    put_hufe(w, dc[bits]);
    if (bits)
        put_bits(w, (uint32_t)(diff < 0 ? diff - 1 : diff) &
                        ((1u << bits) - 1u), bits);
    for (u = 1; u < 64; u++) {
        if (!coefficient[u]) {
            run++;
            continue;
        }
        while (run > 15) {
            put_hufe(w, ac[161]);
            run -= 16;
        }
        bits = magnitude_bits(coefficient[u]);
        assert(bits >= 1 && bits <= 10);
        put_hufe(w, ac[run * 10 + bits - 1]);
        put_bits(w, (uint32_t)(coefficient[u] < 0 ? coefficient[u] - 1
                                                   : coefficient[u]) &
                        ((1u << bits) - 1u), bits);
        run = 0;
    }
    if (run)
        put_hufe(w, ac[160]);
}

static uint32_t reg(uint32_t address)
{
    return regs[(address & 0xffffcu) >> 2];
}

/* Executes the command list; returns the coded length. */
static uint32_t run_list(const uint32_t *list)
{
    uint32_t pair = 0;
    uint32_t mb_width, mb_height, stride, ctrl;
    const uint8_t *luma, *chroma;
    const uint32_t *qmem = &regs[0xe1400 >> 2];
    const uint32_t *hufe = &regs[0xe1800 >> 2];
    BitWriter writer;
    int dc[3] = { 0, 0, 0 };
    uint32_t mx, my, b;

    memset(regs, 0, sizeof(regs));
    for (;; pair++) {
        uint32_t address = list[pair * 2u + 1u];

        assert(pair < HELIX_JPEG_DESCRIPTOR_PAIRS);
        assert(address & 0x80000000u);
        regs[(address & 0xffffcu) >> 2] = list[pair * 2u];
        if (address & 0x40000000u)
            break;
    }
    last_pairs = pair + 1u;
    /* the list ends with the EFE start */
    assert((list[pair * 2u + 1u] & 0xffffcu) == 0x40000u);
    ctrl = reg(0x40000);
    assert((ctrl & 0x4003u) == 0x4003u);
    assert(reg(0xe0004) == 0xa0121u && reg(0xe0000) == 7u);
    assert(reg(0xe0010) == SRAM && reg(0x40030) == SRAM);
    assert(reg(0xe0030) == 0x30u && reg(0xe0034) == 7u &&
           reg(0xe0038) == 7u && reg(0xe002c) == 0u);
    mb_width = (reg(0x40004) & 0xffffu) + 1u;
    mb_height = (reg(0x40004) >> 16) + 1u;
    assert(reg(0xe0028) == mb_width * mb_height - 1u);
    stride = reg(0x40038) >> 16;
    assert((reg(0x40038) & 0xffffu) == stride);
    luma = (const uint8_t *)(uintptr_t)reg(0x40010);
    chroma = (const uint8_t *)(uintptr_t)reg(0x40014);
    memset(&writer, 0, sizeof(writer));
    writer.out = (uint8_t *)(uintptr_t)reg(0xe000c);
    writer.capacity = allocation_size(reg(0xe000c));
    last_raw_y = reg(0x40010);
    last_bitstream_buffer = writer.capacity;
    /* one job buffer: the command list, then the bitstream at +8 KiB */
    assert(reg(0xe000c) == (uint32_t)(uintptr_t)list + 0x2000u);
#if defined(PLATFORM_T23)
    /* the limit is the buffer, or less for the probe (guard behind) */
    assert((reg(0xe0068) & 0x80000000u) &&
           ((reg(0xe0068) & 0x7fffffffu) == writer.capacity ||
            getenv("OPENIMP_HELIX_JPEG_PROBE_MAX_BS_KB")));
#else
    /* only the JPGC_MAX_BS probe programs the limit here */
    assert(reg(0xe0068) == 0u || (reg(0xe0068) & 0x80000000u));
#endif
    for (my = 0; my < mb_height; my++)
        for (mx = 0; mx < mb_width; mx++) {
            double block[64];
            unsigned int i;

            for (b = 0; b < 4u; b++) {
                for (i = 0; i < 64u; i++)
                    block[i] = luma[(size_t)(my * 16u + (b / 2u) * 8u + i / 8u) *
                                        stride +
                                    mx * 16u + (b % 2u) * 8u + i % 8u] - 128.0;
                code_block(&writer, block, qmem, hufe, hufe + 352, &dc[0]);
            }
            for (b = 0; b < 2u; b++) {
                unsigned int offset = ((ctrl & 0xcu) == 0xcu) ? 1u - b : b;

                for (i = 0; i < 64u; i++)
                    block[i] = chroma[(size_t)(my * 8u + i / 8u) * stride +
                                      mx * 16u + (i % 8u) * 2u + offset] -
                               128.0;
                code_block(&writer, block, qmem + 64, hufe + 176, hufe + 368,
                           &dc[1 + b]);
            }
        }
    if (writer.count)
        put_bits(&writer, 0x7fu, 8 - writer.count);
    return writer.size;
}

int __real_ioctl(int fd, unsigned long request, ...);
int __wrap_ioctl(int fd, unsigned long request, ...)
{
    va_list args;
    FakeChannel *channel;

    va_start(args, request);
    channel = va_arg(args, FakeChannel *);
    va_end(args);
    if (fd != FAKE_FD)
        return -1;
    switch ((uint32_t)request) {
    case CHANNEL_REQUEST:
        assert(channel->codecdir == 0x10000u && channel->thread_id == -1);
        channel->clist = 0x1234u;
        channel->channel_id = 5u;
        requests++;
        return 0;
    case CHANNEL_RELEASE:
        assert(channel->workphase == 2u);
        releases++;
        return 0;
    case CHANNEL_RUN:
        runs++;
#if defined(PLATFORM_T20)
        assert(channel->vpu_id == -1);
#else
        assert(channel->vpu_id == 0x02000001);
#endif
        assert(channel->codecdir == 0x10000u && channel->dma_addr);
        if (fail_runs)
            return -1;
        channel->output_len =
            run_list((const uint32_t *)(uintptr_t)channel->dma_addr);
        if (force_overflow)
            channel->output_len = allocation_size(
                regs[0xe000c >> 2]);
        channel->status = force_status ? force_status : 0x11u;
        if (force_overflow)
            channel->status |= (1u << 20);
        return 0;
    }
    return -1;
}

/* ---- baseline decoder (reads only the file) ---- */

typedef struct {
    uint16_t code[256];
    uint8_t length[256];
    uint8_t symbol[256];
    int count;
} DecodeTable;

typedef struct {
    const uint8_t *data;
    size_t size, position;
    uint32_t bits;
    int count;
} BitReader;

static int read_bit(BitReader *r)
{
    if (!r->count) {
        uint8_t byte;

        assert(r->position < r->size);
        byte = r->data[r->position++];
        if (byte == 0xff) {
            assert(r->data[r->position] == 0x00);
            r->position++;
        }
        r->bits = byte;
        r->count = 8;
    }
    r->count--;
    return (int)((r->bits >> r->count) & 1u);
}

static int read_symbol(BitReader *r, const DecodeTable *t)
{
    uint32_t code = 0;
    int length, i;

    for (length = 1; length <= 16; length++) {
        code = (code << 1) | (uint32_t)read_bit(r);
        for (i = 0; i < t->count; i++)
            if (t->length[i] == length && t->code[i] == code)
                return t->symbol[i];
    }
    assert(!"bad Huffman code");
    return -1;
}

static int read_value(BitReader *r, int bits)
{
    int value = 0, i;

    for (i = 0; i < bits; i++)
        value = (value << 1) | read_bit(r);
    if (bits && value < (1 << (bits - 1)))
        value -= (1 << bits) - 1;
    return value;
}

static void decode_block(BitReader *r, const DecodeTable *dc,
                         const DecodeTable *ac, const uint8_t *qt,
                         int *predictor, double out[64])
{
    int coefficient[64] = { 0 };
    int k = 1, u, v, x, y;

    *predictor += read_value(r, read_symbol(r, dc));
    coefficient[0] = *predictor * qt[0];
    while (k < 64) {
        int symbol = read_symbol(r, ac);

        if (!symbol)
            break;
        k += symbol >> 4;
        assert(k < 64);
        coefficient[k] = read_value(r, symbol & 15) * qt[k];
        k++;
    }
    for (y = 0; y < 8; y++)
        for (x = 0; x < 8; x++) {
            double sum = 0.0;

            for (v = 0; v < 8; v++)
                for (u = 0; u < 8; u++)
                    sum += (u ? 1.0 : M_SQRT1_2) * (v ? 1.0 : M_SQRT1_2) *
                           coefficient[zigzag[v * 8 + u]] *
                           cos((2 * x + 1) * u * M_PI / 16.0) *
                           cos((2 * y + 1) * v * M_PI / 16.0);
            out[y * 8 + x] = sum / 4.0 + 128.0;
        }
}

/* Decodes a 4:2:0 JFIF file into planar Y and 2x2-subsampled Cb/Cr. */
static void decode_jpeg(const uint8_t *file, size_t size, uint32_t *width,
                        uint32_t *height, uint8_t **y_out, uint8_t **cb_out,
                        uint8_t **cr_out)
{
    uint8_t qt[2][64];
    DecodeTable tables[2][2];
    size_t p = 2;
    int have_app0 = 0;

    memset(tables, 0, sizeof(tables));
    assert(file[0] == 0xff && file[1] == 0xd8);
    for (;;) {
        uint8_t marker;
        size_t length;

        assert(file[p] == 0xff);
        marker = file[p + 1];
        length = ((size_t)file[p + 2] << 8) | file[p + 3];
        if (marker == 0xe0) {
            assert(!memcmp(file + p + 4, "JFIF", 5));
            have_app0 = 1;
        } else if (marker == 0xdb) {
            assert(length == 67 && file[p + 4] < 2u);
            memcpy(qt[file[p + 4]], file + p + 5, 64);
        } else if (marker == 0xc0) {
            assert(file[p + 4] == 8 && file[p + 9] == 3 &&
                   file[p + 11] == 0x22 && file[p + 14] == 0x11 &&
                   file[p + 17] == 0x11);
            *height = ((uint32_t)file[p + 5] << 8) | file[p + 6];
            *width = ((uint32_t)file[p + 7] << 8) | file[p + 8];
        } else if (marker == 0xc4) {
            uint8_t id = file[p + 4];
            DecodeTable *t = &tables[id >> 4][id & 15];
            const uint8_t *counts = file + p + 5;
            const uint8_t *symbols = counts + 16;
            uint16_t code = 0;
            int l, i, k = 0;

            for (l = 1; l <= 16; l++) {
                for (i = 0; i < counts[l - 1]; i++, k++) {
                    t->code[k] = code++;
                    t->length[k] = (uint8_t)l;
                    t->symbol[k] = symbols[k];
                }
                code <<= 1;
            }
            t->count = k;
        } else if (marker == 0xda) {
            p += 2 + length;
            break;
        }
        p += 2 + length;
    }
    assert(have_app0);
    {
        BitReader reader = { file + p, size - p - 2u, 0, 0, 0 };
        uint32_t mbw = (*width + 15u) / 16u, mbh = (*height + 15u) / 16u;
        uint32_t w = mbw * 16u, h = mbh * 16u, mx, my, b, i;
        int dc[3] = { 0, 0, 0 };
        double block[64];
        uint8_t *planes[3];

        planes[0] = calloc(1, (size_t)w * h);
        planes[1] = calloc(1, (size_t)w * h / 4u);
        planes[2] = calloc(1, (size_t)w * h / 4u);
        assert(file[size - 2] == 0xff && file[size - 1] == 0xd9);
        for (my = 0; my < mbh; my++)
            for (mx = 0; mx < mbw; mx++) {
                for (b = 0; b < 4u; b++) {
                    decode_block(&reader, &tables[0][0], &tables[1][0], qt[0],
                                 &dc[0], block);
                    for (i = 0; i < 64u; i++)
                        planes[0][(size_t)(my * 16u + (b / 2u) * 8u + i / 8u) * w +
                                  mx * 16u + (b % 2u) * 8u + i % 8u] =
                            (uint8_t)fmin(255.0, fmax(0.0, lround(block[i])));
                }
                for (b = 0; b < 2u; b++) {
                    decode_block(&reader, &tables[0][1], &tables[1][1], qt[1],
                                 &dc[1 + b], block);
                    for (i = 0; i < 64u; i++)
                        planes[1 + b][(size_t)(my * 8u + i / 8u) * (w / 2u) +
                                      mx * 8u + i % 8u] =
                            (uint8_t)fmin(255.0, fmax(0.0, lround(block[i])));
                }
            }
        /* all entropy data used: only the 1-padding of the last byte left */
        assert(reader.position == reader.size);
        *y_out = planes[0];
        *cb_out = planes[1];
        *cr_out = planes[2];
    }
}

/* ---- tests ---- */

static uint8_t source_pixel(uint32_t x, uint32_t y, unsigned int plane)
{
    double v = plane == 0 ? 128.0 + 90.0 * sin(x / 23.0) * cos(y / 17.0)
             : plane == 1 ? 110.0 + 40.0 * sin((x + y) / 31.0)
                          : 150.0 - 35.0 * cos((x - y) / 27.0);

    return (uint8_t)lround(v);
}

/* framesource layout: chroma at stride * aligned height */
static uint8_t *make_frame(uint32_t width, uint32_t height,
                           uint32_t chroma_offset, uint32_t *size_out,
                           int nv21)
{
    uint32_t size = chroma_offset + width * ((height + 1u) / 2u);
    uint8_t *frame = fake_map(size + 4096u);
    uint32_t x, y;

    memset(frame, 0x00, size);
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            frame[(size_t)y * width + x] = source_pixel(x, y, 0);
    for (y = 0; y < height / 2u; y++)
        for (x = 0; x < width / 2u; x++) {
            frame[chroma_offset + (size_t)y * width + x * 2u + (nv21 ? 1u : 0u)] =
                source_pixel(x * 2u, y * 2u, 1);
            frame[chroma_offset + (size_t)y * width + x * 2u + (nv21 ? 0u : 1u)] =
                source_pixel(x * 2u, y * 2u, 2);
        }
    *size_out = size;
    return frame;
}

static double psnr(const uint8_t *decoded, uint32_t decoded_stride,
                   uint32_t width, uint32_t height, unsigned int plane,
                   unsigned int subsample)
{
    double error = 0.0;
    uint32_t x, y;

    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++) {
            double d = (double)decoded[(size_t)y * decoded_stride + x] -
                       source_pixel(x * subsample, y * subsample, plane);
            error += d * d;
        }
    error /= (double)width * height;
    return error > 0.0 ? 10.0 * log10(255.0 * 255.0 / error) : 99.0;
}

static void check_picture(const HWStreamBuffer *stream, uint32_t width,
                          uint32_t height, double minimum)
{
    uint32_t w, h;
    uint8_t *y, *cb, *cr;
    double py, pcb, pcr;

    decode_jpeg((const uint8_t *)(uintptr_t)stream->virt_addr, stream->length,
                &w, &h, &y, &cb, &cr);
    assert(w == width && h == height);
    py = psnr(y, (w + 15u) & ~15u, width, height, 0, 1);
    pcb = psnr(cb, ((w + 15u) & ~15u) / 2u, width / 2u, height / 2u, 1, 2);
    pcr = psnr(cr, ((w + 15u) & ~15u) / 2u, width / 2u, height / 2u, 2, 2);
    printf("  %ux%u %u bytes PSNR Y %.1f Cb %.1f Cr %.1f dB\n", width, height,
           stream->length, py, pcb, pcr);
    assert(py > minimum && pcb > minimum && pcr > minimum);
    free(y);
    free(cb);
    free(cr);
}

static void test_tables(void)
{
    uint32_t huffman[384];
    uint8_t qt[128];
    unsigned int q;

    HelixJpeg_HuffmanTable(huffman);
    /* luma AC 0/1 = 00, 0/2 = 01, EOB = 1010, ZRL = 11111111001 */
    assert(huffman[0] == 0x100u && huffman[1] == 0x101u);
    assert(huffman[160] == 0x30au && huffman[161] == 0xaf9u);
    assert(huffman[162] == 0xfffu && huffman[168] == 0xfd0u &&
           huffman[175] == 0xfd7u);
    /* chroma AC EOB = 00, luma DC 0 = 00, 11 = 111111110 */
    assert(huffman[176 + 160] == 0x100u);
    assert(huffman[352] == 0x100u && huffman[363] == 0x8feu &&
           huffman[364] == 0xfffu);
    /* chroma DC 0..2 = 00, 01, 10 */
    assert(huffman[368] == 0x100u && huffman[369] == 0x101u &&
           huffman[370] == 0x102u);

    assert(HelixJpeg_QmemEntry(0) == 0u && HelixJpeg_QmemEntry(1) == 1u);
    assert(HelixJpeg_QmemEntry(2) == 0x400u);
    assert(HelixJpeg_QmemEntry(5) == 0xb33u);
    assert(HelixJpeg_QmemEntry(11) == 0x12e9u);
    assert(HelixJpeg_QmemEntry(25) == 0x251fu);
    for (q = 2; q < 256u; q++) {
        uint32_t entry = HelixJpeg_QmemEntry((uint8_t)q);
        double step = (double)(1u << (11u + (entry >> 11))) /
                      (double)(entry & 0x7ffu);

        assert((entry & 0x7ffu) >= 0x80u && (entry >> 11) <= 7u);
        assert(fabs(step - q) / q < 0.004);
    }

    /* quality 75: the table set the stock encoder starts with */
    HelixJpeg_QualityTables(75u, qt);
    assert(qt[0] == 8 && qt[1] == 6 && qt[15] == 20 && qt[63] == 50);
    assert(qt[64] == 9 && qt[64 + 63] == 50);
    HelixJpeg_QualityTables(100u, qt);
    assert(qt[0] == 1 && qt[127] == 1);
    HelixJpeg_QualityTables(1u, qt);
    assert(qt[0] == 255 && qt[127] == 255);
    HelixJpeg_QualityTables(0u, qt);
    assert(qt[0] == 8);
}

static void test_descriptor(void)
{
    static uint32_t words[HELIX_JPEG_DESCRIPTOR_PAIRS * 2u];
    uint8_t qt[128];
    HelixJpegSlice slice;
    int pairs, t23;

    HelixJpeg_QualityTables(75u, qt);
    for (t23 = 0; t23 < 2; t23++) {
        memset(&slice, 0, sizeof(slice));
        slice.variant = t23 ? HELIX_JPEG_T23 : HELIX_JPEG_T21;
        slice.raw_y = 0x02000000u;
        slice.raw_c = 0x021fe000u;
        slice.stride = 1920u;
        slice.width = 1920u;
        slice.mb_width = 120u;
        slice.mb_height = 68u;
        slice.raw_format = HELIX_JPEG_PLANE_NV12;
        slice.bitstream = 0x03000000u;
        slice.bitstream_limit = t23 ? 0x300000u : 0u;
        slice.qt = qt;
        pairs = HelixJpeg_BuildDescriptor(&slice, words,
                                          sizeof(words) / sizeof(words[0]));
        assert(pairs == (t23 ? 660 : 659));
        /* TCSM flush, clock open, then 384 Huffman and 256 QMEM words */
        assert(words[0] == 0 && words[1] == 0x800c0000u);
        assert(words[2] == 1 && words[3] == 0x800e0004u);
        assert(words[5] == 0x800e1800u && words[4] == 0x100u);
        assert(words[2 * 385 + 1] == 0x800e1dfcu);
        assert(words[2 * 386 + 1] == 0x800e1400u &&
               words[2 * 386] == HelixJpeg_QmemEntry(qt[0]));
        assert(words[2 * (386 + 128)] == 0u);
        assert(words[2 * 642] == 0 && words[2 * 642 + 1] == 0x800e0008u);
        assert(words[2 * 643] == 0x03000000u);
        assert(words[2 * 644] == (t23 ? 0x131f0000u : 0x132f0000u));
        assert(words[2 * 645] == 120u * 68u - 1u);
        if (t23)
            assert(words[2 * 650] == 0x80300000u &&
                   words[2 * 650 + 1] == 0x800e0068u);
        assert(words[2 * (pairs - 4)] == 0x00000000u &&
               words[2 * (pairs - 4) + 1] == 0x80040034u);
        assert(words[2 * (pairs - 3)] == ((1920u << 16) | 1920u));
        assert(words[2 * (pairs - 1)] == 0x400bu &&
               words[2 * (pairs - 1) + 1] == 0xc0040000u);
        /* EFE_GEOM: last MCU row and column */
        assert(words[2 * (pairs - 7)] == ((67u << 16) | 119u));
        /* too small a buffer is refused */
        assert(HelixJpeg_BuildDescriptor(&slice, words, 1000u) < 0);
    }
    slice.raw_format = HELIX_JPEG_PLANE_NV21;
    pairs = HelixJpeg_BuildDescriptor(&slice, words, 2u * 664u);
    assert(words[2 * (pairs - 1)] == 0x400fu);
    slice.raw_format = 3;
    assert(HelixJpeg_BuildDescriptor(&slice, words, 2u * 664u) < 0);
}

static void test_header(void)
{
    uint8_t header[HELIX_JPEG_HEADER_SIZE];
    uint8_t qt[128];

    HelixJpeg_QualityTables(50u, qt);
    assert(HelixJpeg_WriteHeader(header, sizeof(header), 1920u, 1080u, qt) ==
           HELIX_JPEG_HEADER_SIZE);
    assert(header[0] == 0xff && header[1] == 0xd8 && header[3] == 0xe0);
    assert(header[20] == 0xff && header[21] == 0xdb && header[24] == 0 &&
           !memcmp(header + 25, qt, 64));
    assert(header[89] == 0xff && header[90] == 0xdb && header[93] == 1);
    assert(header[158] == 0xff && header[159] == 0xc0 &&
           header[163] == 0x04 && header[164] == 0x38 &&
           header[165] == 0x07 && header[166] == 0x80);
    assert(header[HELIX_JPEG_HEADER_SIZE - 14] == 0xff &&
           header[HELIX_JPEG_HEADER_SIZE - 13] == 0xda);
    assert(HelixJpeg_WriteHeader(header, sizeof(header) - 1u, 1920u, 1080u,
                                 qt) == 0);
}

static void encode_and_check(uint32_t width, uint32_t height,
                             uint32_t chroma_offset, int dma, int nv21,
                             uint32_t quality, double minimum)
{
    HelixJpegFrame frame;
    HWStreamBuffer stream;
    uint8_t qt[128];
    uint32_t size;
    uint32_t layout = chroma_offset ? chroma_offset
                                    : width * ((height + 15u) & ~15u);
    uint8_t *pixels = make_frame(width, height, layout, &size, nv21);

    memset(&frame, 0, sizeof(frame));
    frame.virt_addr = (uint32_t)(uintptr_t)pixels;
    frame.phys_addr = dma ? frame.virt_addr : 0u;
    frame.size = size;
    frame.width = width;
    frame.height = height;
    frame.chroma_offset = chroma_offset;
    frame.pixfmt = nv21 ? PIX_FMT_NV21 : PIX_FMT_NV12;
    frame.timestamp = 1234u;
    HelixJpeg_QualityTables(quality, qt);
    assert(OpenIMP_HelixJpeg_Encode(&frame, qt, &stream) == 0);
    assert(stream.phys_addr == 0 && stream.timestamp == 1234u &&
           stream.frame_type == HW_FRAME_TYPE_I);
    check_picture(&stream, width, height, minimum);
    {
        /* HELIX_JPEG_TEST_DUMP=dir keeps the files for other decoders */
        const char *dir = getenv("HELIX_JPEG_TEST_DUMP");

        if (dir) {
            char path[512];
            FILE *file;

            snprintf(path, sizeof(path), "%s/helix_%ux%u_q%u%s.jpg", dir,
                     width, height, quality, nv21 ? "_nv21" : "");
            file = fopen(path, "wb");
            assert(file);
            assert(fwrite((const void *)(uintptr_t)stream.virt_addr, 1,
                          stream.length, file) == stream.length);
            fclose(file);
        }
    }
    free((void *)(uintptr_t)stream.virt_addr);
    munmap(pixels, size + 4096u);
}

static void test_encode(void)
{
    printf("encode:\n");
    /* framesource layout, height not a multiple of 16 (padding rows) */
    encode_and_check(320u, 232u, 0u, 1, 0, 75u, 30.0);
    assert(last_pairs == (HELIX_JPEG_VARIANT == HELIX_JPEG_T23 ? 660u : 659u));
    /* packed layout outside rmem: copied */
    encode_and_check(256u, 144u, 256u * 144u, 0, 0, 90u, 34.0);
    /* NV21 */
    encode_and_check(272u, 64u, 0u, 1, 1, 75u, 30.0);
    /* low quality still decodes */
    encode_and_check(256u, 32u, 0u, 1, 0, 10u, 20.0);
}

/* A framesource frame that ends after the visible chroma rows is read in
 * place (no copy); the bitstream buffer is a quarter of NV12 at q75. */
static void test_framesource_tail(void)
{
    HelixJpegFrame frame;
    HWStreamBuffer stream;
    uint8_t qt[128];
    uint32_t size;
    uint8_t *pixels = make_frame(1920u, 1080u, 1920u * 1088u, &size, 0);
    unsigned int before, run_count = runs;

    /* channel creation takes the bitstream buffer, like the stock encoder */
    assert(OpenIMP_HelixJpeg_Reserve(1920u, 1080u) == 0);
    assert(OpenIMP_HelixJpeg_Reserve(1000u, 1080u) < 0);
    assert(runs == run_count);
    before = live_allocations;

    memset(&frame, 0, sizeof(frame));
    frame.virt_addr = (uint32_t)(uintptr_t)pixels;
    frame.phys_addr = frame.virt_addr;
    frame.size = size;              /* 1920 * (1088 + 540) */
    frame.width = 1920u;
    frame.height = 1080u;
    HelixJpeg_QualityTables(75u, qt);
    assert(OpenIMP_HelixJpeg_Encode(&frame, qt, &stream) == 0);
    assert(last_raw_y == frame.phys_addr);
#if defined(PLATFORM_T23)
    /* JPGC_MAX_BS: a quarter of NV12 */
    assert(last_bitstream_buffer ==
           ((1920u * 1088u * 3u / 2u / 4u + 0xfffu) & ~0xfffu));
#else
    /* no hardware limit: the whole NV12 picture, as the stock library */
    assert(last_bitstream_buffer == 1920u * 1088u * 3u / 2u);
#endif
    assert(live_allocations == before);
    printf("  1920x1080 in place: %u bytes, bitstream buffer %u\n",
           stream.length, last_bitstream_buffer);
    free((void *)(uintptr_t)stream.virt_addr);
    /* a pool whose frame size is the kernel's width * height * 3 / 2,
     * shorter than the padded layout: still read in place */
    frame.size = 1920u * 1080u * 3u / 2u;
    assert(OpenIMP_HelixJpeg_Encode(&frame, qt, &stream) == 0);
    assert(last_raw_y == frame.phys_addr && live_allocations == before);
    check_picture(&stream, 1920u, 1080u, 30.0);
    free((void *)(uintptr_t)stream.virt_addr);
    munmap(pixels, size + 4096u);
}

#if defined(PLATFORM_T23)
/* JPGC_MAX_BS: a full quarter-NV12 bitstream repeats the job once with the
 * NV12 size, which is kept from then on. */
static void test_bitstream_full_retry(void)
{
    HelixJpegFrame frame;
    HWStreamBuffer stream;
    uint8_t qt[128];
    uint32_t size;
    uint8_t *pixels = make_frame(1024u, 768u, 1024u * 768u, &size, 0);
    unsigned int run_count = runs;

    memset(&frame, 0, sizeof(frame));
    frame.virt_addr = (uint32_t)(uintptr_t)pixels;
    frame.phys_addr = frame.virt_addr;
    frame.size = size;
    frame.width = 1024u;
    frame.height = 768u;
    HelixJpeg_QualityTables(75u, qt);
    force_overflow = 1;
    assert(OpenIMP_HelixJpeg_Encode(&frame, qt, &stream) < 0);
    assert(runs == run_count + 2u);
    force_overflow = 0;
    assert(OpenIMP_HelixJpeg_Encode(&frame, qt, &stream) == 0);
    assert(last_bitstream_buffer == 1024u * 768u * 3u / 2u);
    free((void *)(uintptr_t)stream.virt_addr);
    munmap(pixels, size + 4096u);
}
#endif

/* No allocation that would leave less than the reserve free in rmem. */
static void test_rmem_budget(void)
{
    HelixJpegFrame frame;
    HWStreamBuffer stream;
    uint8_t qt[128];
    uint32_t size;
    uint8_t *pixels = make_frame(256u, 144u, 256u * 144u, &size, 0);
    unsigned int before = live_allocations, run_count = runs, i;

    memset(&frame, 0, sizeof(frame));
    frame.virt_addr = (uint32_t)(uintptr_t)pixels;
    frame.size = size;              /* not DMA memory: needs a copy */
    frame.width = 256u;
    frame.height = 144u;
    frame.chroma_offset = 256u * 144u;
    HelixJpeg_QualityTables(75u, qt);
    rmem_stats_on = 1;
    rmem_largest = (512u << 10) + 4096u;  /* less than copy + reserve */
    for (i = 0; i < 4u; i++)
        assert(OpenIMP_HelixJpeg_Encode(&frame, qt, &stream) < 0);
    /* soft skips: no run, nothing held, hardware path still on */
    assert(runs == run_count && live_allocations == before);
    assert(OpenIMP_HelixJpeg_Available());
    rmem_largest = 64u << 20;
    assert(OpenIMP_HelixJpeg_Encode(&frame, qt, &stream) == 0);
    /* the per-job copy is gone again */
    assert(live_allocations == before);
    rmem_stats_on = 0;
    free((void *)(uintptr_t)stream.virt_addr);
    munmap(pixels, size + 4096u);
}

static void test_failures(void)
{
    HelixJpegFrame frame;
    HWStreamBuffer stream;
    uint8_t qt[128];
    uint32_t size;
    uint8_t *pixels = make_frame(256u, 32u, 256u * 32u, &size, 0);
    unsigned int i;

    memset(&frame, 0, sizeof(frame));
    frame.virt_addr = (uint32_t)(uintptr_t)pixels;
    frame.phys_addr = frame.virt_addr;
    frame.size = size;
    frame.width = 256u;
    frame.height = 32u;
    HelixJpeg_QualityTables(75u, qt);

    /* geometry the stock encoder refuses */
    frame.width = 240u;
    assert(OpenIMP_HelixJpeg_Encode(&frame, qt, &stream) < 0);
    frame.width = 264u;
    assert(OpenIMP_HelixJpeg_Encode(&frame, qt, &stream) < 0);
    frame.width = 256u;
    frame.height = 31u;
    assert(OpenIMP_HelixJpeg_Encode(&frame, qt, &stream) < 0);
    frame.height = 32u;
    /* a short buffer is never handed to the VPU */
    frame.size = 256u * 32u;
    i = runs;
    assert(OpenIMP_HelixJpeg_Encode(&frame, qt, &stream) < 0);
    assert(runs == i);
    frame.size = size;

    /* error status, overflow, ioctl failure: nothing allocated */
    force_status = 0x11u | (1u << 7);
    assert(OpenIMP_HelixJpeg_Encode(&frame, qt, &stream) < 0);
    force_status = 0;
    assert(OpenIMP_HelixJpeg_Encode(&frame, qt, &stream) == 0);
    free((void *)(uintptr_t)stream.virt_addr);
    /* ENDFLAG without JPGEND: the kernel's length is an H.264 one */
    force_status = 0x01u;
    assert(OpenIMP_HelixJpeg_Encode(&frame, qt, &stream) < 0);
    force_status = 0;
    assert(OpenIMP_HelixJpeg_Encode(&frame, qt, &stream) == 0);
    free((void *)(uintptr_t)stream.virt_addr);
    force_overflow = 1;
    i = runs;
    assert(OpenIMP_HelixJpeg_Encode(&frame, qt, &stream) < 0);
    /* the 256 KiB floor already holds this NV12 picture: no repeat */
    assert(runs == i + 1u);
    force_overflow = 0;
    assert(OpenIMP_HelixJpeg_Encode(&frame, qt, &stream) == 0);
    free((void *)(uintptr_t)stream.virt_addr);
    fail_runs = 1;
    for (i = 0; i < 3u; i++)
        assert(OpenIMP_HelixJpeg_Encode(&frame, qt, &stream) < 0);
#if OPENIMP_SW_JPEG
    /* three in a row: the software encoder takes over for good */
    assert(!OpenIMP_HelixJpeg_Available());
    assert(releases == 1u && live_allocations == 0u);
#else
    /* no software encoder: keep trying the VPU */
    assert(OpenIMP_HelixJpeg_Available());
    fail_runs = 0;
    assert(OpenIMP_HelixJpeg_Encode(&frame, qt, &stream) == 0);
    free((void *)(uintptr_t)stream.virt_addr);
    OpenIMP_HelixJpeg_Shutdown();
    assert(releases == 1u && live_allocations == 0u);
#endif
    fail_runs = 0;
    munmap(pixels, size + 4096u);
}

/* OPENIMP_HELIX_JPEG_PROBE_MAX_BS_KB: per-job probe buffer with the limit
 * programmed; a core that keeps to it leaves the guard alone. */
static void test_probe(void)
{
    HelixJpegFrame frame;
    HWStreamBuffer stream;
    uint8_t qt[128];
    uint32_t size;
    uint8_t *pixels = make_frame(256u, 144u, 256u * 144u, &size, 0);
    unsigned int before;

    OpenIMP_HelixJpeg_Shutdown();
    setenv("OPENIMP_HELIX_JPEG_PROBE_MAX_BS_KB", "16", 1);
    memset(&frame, 0, sizeof(frame));
    frame.virt_addr = (uint32_t)(uintptr_t)pixels;
    frame.phys_addr = frame.virt_addr;
    frame.size = size;
    frame.width = 256u;
    frame.height = 144u;
    HelixJpeg_QualityTables(75u, qt);
    assert(OpenIMP_HelixJpeg_Encode(&frame, qt, &stream) == 0);
    assert(reg(0xe0068) == (0x80000000u | (16u << 10)));
    check_picture(&stream, 256u, 144u, 30.0);
    free((void *)(uintptr_t)stream.virt_addr);
    before = live_allocations;
    OpenIMP_HelixJpeg_Shutdown();
    assert(live_allocations < before || before == 0u);
    unsetenv("OPENIMP_HELIX_JPEG_PROBE_MAX_BS_KB");
    munmap(pixels, size + 4096u);
}

int main(void)
{
    /* the stream carries 32-bit addresses: keep the heap low (no PIE, no
     * mmap-backed malloc) */
    mallopt(M_MMAP_THRESHOLD, 64 << 20);
    test_tables();
    test_descriptor();
    test_header();
    test_encode();
    test_framesource_tail();
    test_rmem_budget();
#if defined(PLATFORM_T23)
    test_bitstream_full_retry();
#endif
    assert(requests == 1u);
    test_failures();
    test_probe();
    printf("helix_jpeg_test (%s, software JPEG %s): ok\n",
           HELIX_JPEG_VARIANT == HELIX_JPEG_T23 ? "T23" : "T20/T21/T30",
           OPENIMP_SW_JPEG ? "built in" : "left out");
    return 0;
}
