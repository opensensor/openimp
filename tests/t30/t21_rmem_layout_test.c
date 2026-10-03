/* Host simulation of the T21 reserved-memory arena with the native Helix
 * encoder (Victure PC420: rmem=23M, 1920x1080 main + 640x360 sub, two-buffer
 * FrameSource pools, a 1080p and a 360p JPEG channel).
 *
 * The arena is OpenIMP's real allocator (src/rmem_arena.h, the code
 * src/dma_alloc.c runs on the camera); the encoder buffers come from the
 * real OpenIMP_T30_HelixCreate/Destroy (PLATFORM_T21) against a fake
 * /dev/soc_vpu that only hands out channels, and the shared bitstream
 * buffer from the real src/t30/helix_bitstream.c.  The other allocations
 * are the device's (ISP NCU buffer, OSD bitmaps, JPEG reservation).
 *
 * timps disables an idle FrameSource and enables it again on demand, so the
 * two pools are freed and re-created all the time, in any order, also
 * while the other channel streams.  The test requires that every pool can
 * always be re-created with the encoders made at channel creation (as the
 * stock library does), and that the previous layout - per-channel 2 MiB EMC
 * and 1 MiB bitstream windows, a JPEG buffer of its own, everything
 * best-fit from the bottom, encoders made at their first picture - fails
 * the way the camera did (main pool not re-created after the sub channel
 * streamed). */
#define _GNU_SOURCE
#include <assert.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dma_alloc.h"
#include "rmem_arena.h"
#include "t30/helix_bitstream.h"
#include "t30/t30_helix_encoder.h"

#define RMEM_SIZE   (23u << 20)
#define RMEM_PHYS   0x02900000u
#define FAKE_FD     4244

#define MAIN_W 1920u
#define MAIN_H 1080u
#define SUB_W  640u
#define SUB_H  360u
/* VBM frame size: macroblock-aligned NV12 */
#define FRAME(w, h) ((((w) + 15u) & ~15u) * (((h) + 15u) & ~15u) * 3u / 2u)
#define POOL0 (2u * FRAME(MAIN_W, MAIN_H))   /* 6266880, as on the camera */
#define POOL1 (2u * FRAME(SUB_W, SUB_H))     /* 706560 */
/* tx-isp GET_BUF for 1920x1080: w*h*3/2 + 240*69*40 */
#define ISP_NCU 3772800u
/* Helix JPEG: 8 KiB command list + the 1080p NV12 picture */
#define JPEG_BS (0x2000u + FRAME(MAIN_W, MAIN_H))
/* now: command list + two 1080p macroblock rows at 2368 bytes per MCU */
#define JPEG_ROW (0x2000u + 2u * 120u * 2368u + 256u)
#define OSD_PIECES 8u

static const uint32_t osd_sizes[OSD_PIECES] = {
    67392u, 77376u, 27456u, 12000u, 10912u, 12496u, 4576u, 12000u
};

static RmemArena arena;
static int top_allocations;
static struct { uint32_t phys; void *virt; } shadows[64];

static int arena_alloc(IMPDMABufferInfo *info, int size, int top)
{
    size_t off;
    unsigned int i;
    void *virt;

    if ((top ? rmem_arena_alloc_top(&arena, (size_t)size, &off)
             : rmem_arena_alloc(&arena, (size_t)size, &off)) != 0)
        return -1;
    /* encoder buffers are cleared through virt_addr: back them */
    virt = malloc((size_t)size);
    assert(virt && (uintptr_t)virt < 0xffffffffu);
    memset(info, 0, sizeof(*info));
    info->phys_addr = RMEM_PHYS + (uint32_t)off;
    info->virt_addr = (uint32_t)(uintptr_t)virt;
    info->size = (uint32_t)size;
    for (i = 0; i < 64u; i++)
        if (!shadows[i].phys) {
            shadows[i].phys = info->phys_addr;
            shadows[i].virt = virt;
            return 0;
        }
    abort();
}

int DMA_AllocDescriptor(IMPDMABufferInfo *info, int size, const char *tag)
{
    (void)tag;
    return arena_alloc(info, size, 0);
}

int DMA_AllocDescriptorTop(IMPDMABufferInfo *info, int size,
                           const char *tag)
{
    (void)tag;
    if (arena_alloc(info, size, 1) != 0)
        return -1;
    top_allocations++;
    return 0;
}

int DMA_FreePhys(uint32_t phys)
{
    unsigned int i;

    assert(rmem_arena_free(&arena, phys - RMEM_PHYS, NULL) == 0);
    for (i = 0; i < 64u; i++)
        if (shadows[i].phys == phys) {
            free(shadows[i].virt);
            shadows[i].phys = 0;
        }
    return 0;
}

int DMA_RmemFlushCache(void *virt, uint32_t size, int dir)
{
    (void)virt;
    (void)size;
    (void)dir;
    return 0;
}

int DMA_RmemStats(size_t *used, size_t *size, size_t *largest)
{
    if (used)
        *used = arena.used;
    if (size)
        *size = arena.size;
    if (largest)
        *largest = rmem_arena_largest_gap(&arena);
    return 0;
}

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
    return fd == FAKE_FD ? 0 : __real_close(fd);
}

int __wrap_ioctl(int fd, unsigned long request, ...)
{
    va_list args;
    uint32_t *node;

    va_start(args, request);
    node = va_arg(args, uint32_t *);
    va_end(args);
    assert(fd == FAKE_FD);
    if (request == 0xc0386300u) {
        node[0] = 0x8000u;          /* clist */
        return 0;
    }
    assert(request == 0xc0386301u);
    return 0;
}

/* ------------------------------------------------------------------ */

typedef struct {
    IMPDMABufferInfo info;
    int live;
} Block;

static Block isp, pool0, pool1, osd[OSD_PIECES];
static T30HelixEncoder *main_encoder, *sub_encoder;
static size_t worst_free = (size_t)-1;
static unsigned int pool_enables;

static int block_try(Block *b, uint32_t size, int top)
{
    assert(!b->live);
    if (arena_alloc(&b->info, (int)size, top) != 0)
        return -1;
    b->live = 1;
    return 0;
}

static void block_alloc(Block *b, uint32_t size, int top)
{
    if (block_try(b, size, top) != 0) {
        fprintf(stderr, "allocation of %u failed: used %zu of %zu, "
                "largest free block %zu, %d extents\n", size, arena.used,
                arena.size, rmem_arena_largest_gap(&arena), arena.count);
        abort();
    }
}

static void block_free(Block *b)
{
    if (b->live)
        DMA_FreePhys(b->info.phys_addr);
    b->live = 0;
}

static void note_free(void)
{
    if (arena.size - arena.used < worst_free)
        worst_free = arena.size - arena.used;
}

static void pool_enable(Block *pool, uint32_t size)
{
    if (pool->live)
        return;
    block_alloc(pool, size, 0);
    pool_enables++;
    note_free();
}

static void encoder_create(T30HelixEncoder **encoder, uint32_t w,
                           uint32_t h)
{
    HWEncoderParams params;

    if (*encoder)
        return;
    memset(&params, 0, sizeof(params));
    params.width = w;
    params.height = h;
    params.fps_num = 25;
    params.fps_den = 1;
    params.gop_length = 50;
    params.rc_mode = HW_RC_MODE_CBR;
    params.bitrate = w > 640u ? 2000000u : 500000u;
    params.qp = 32;
    params.min_qp = 20;
    params.max_qp = 45;
    if (OpenIMP_T30_HelixCreate(encoder, &params) != 0) {
        fprintf(stderr, "encoder %ux%u create failed: used %zu, largest "
                "free block %zu\n", w, h, arena.used,
                rmem_arena_largest_gap(&arena));
        abort();
    }
}

static void encoder_destroy(T30HelixEncoder **encoder)
{
    OpenIMP_T30_HelixDestroy(*encoder);
    *encoder = NULL;
}

/* The previous layout replayed in the camera's order: JPEG buffer and OSD
 * at start-up, the main encoder made at chn0's first picture, chn0 idles,
 * the sub channel streams (its encoder made at its first picture), then
 * chn0 is enabled again.  Returns 1 if the main pool no longer fits. */
static int legacy_layout_fails(void)
{
    static const uint32_t main_parts[] = {
        16384u, 2097152u, 1048576u, FRAME(MAIN_W, MAIN_H),
        FRAME(MAIN_W, MAIN_H)
    };
    static const uint32_t sub_parts[] = {
        16384u, 2097152u, 1048576u, FRAME(SUB_W, SUB_H), FRAME(SUB_W, SUB_H)
    };
    Block jpeg, parts[10];
    unsigned int i;
    int fails;

    memset(&jpeg, 0, sizeof(jpeg));
    memset(parts, 0, sizeof(parts));
    rmem_arena_init(&arena, RMEM_SIZE);
    block_alloc(&isp, ISP_NCU, 0);
    block_alloc(&jpeg, JPEG_BS, 0);
    for (i = 0; i < OSD_PIECES; i++)
        block_alloc(&osd[i], osd_sizes[i], 0);
    block_alloc(&pool0, POOL0, 0);
    for (i = 0; i < 5u; i++)
        block_alloc(&parts[i], main_parts[i], 0);
    block_free(&pool0);                     /* chn0 idle */
    block_alloc(&pool1, POOL1, 0);          /* chn1 streams */
    for (i = 0; i < 5u; i++)
        block_alloc(&parts[5 + i], sub_parts[i], 0);
    fails = block_try(&pool0, POOL0, 0) != 0;
    printf("previous layout: chn0 re-enable after the sub channel "
           "streamed: needs %u, used %zu of %zu, largest free block %zu "
           "-> %s\n", POOL0, arena.used, arena.size,
           rmem_arena_largest_gap(&arena), fails ? "fails" : "fits");
    block_free(&pool0);
    for (i = 0; i < 10u; i++)
        block_free(&parts[i]);
    block_free(&pool1);
    for (i = 0; i < OSD_PIECES; i++)
        block_free(&osd[i]);
    block_free(&jpeg);
    block_free(&isp);
    assert(arena.used == 0);
    return fails;
}

/* timps' start-up: ISP, IMP_System_Init (the shared bitstream buffer, as
 * the stock EncoderInit's vpuBs), encoder channels (encoders made at
 * CreateChn) and JPEG channels, OSD regions, then chn0's pool. */
static void start(void)
{
    unsigned int i;

    rmem_arena_init(&arena, RMEM_SIZE);
    block_alloc(&isp, ISP_NCU, 0);
    assert(OpenIMP_HelixBitstream_Init() == 0);
    /* 1920 * 1080 bytes, page-rounded */
    assert(OpenIMP_HelixBitstream_Size() == 2076672u);
    encoder_create(&main_encoder, MAIN_W, MAIN_H);
    /* the JPEG channels need one macroblock row of the worst case
     * (src/t30/helix_jpeg.c stripes): no growth */
    assert(OpenIMP_HelixBitstream_Reserve(JPEG_ROW) == 0);
    encoder_create(&sub_encoder, SUB_W, SUB_H);
    assert(OpenIMP_HelixBitstream_Size() == 2076672u);
    for (i = 0; i < OSD_PIECES; i++)
        block_alloc(&osd[i], osd_sizes[i], 1);
    pool_enable(&pool0, POOL0);
}

int main(void)
{
    unsigned int step, i;
    uint32_t seed = 4711u;
    size_t used;

    /* the encoder keeps addresses in 32-bit words, as on MIPS */
    mallopt(M_MMAP_MAX, 0);

    assert(legacy_layout_fails());

    start();
    used = arena.used;
    printf("start-up: encoders, shared bitstream %u bytes, OSD and chn0 "
           "pool: used %zu of %zu\n", OpenIMP_HelixBitstream_Size(), used,
           arena.size);
    /* the camera's sequence: main -> sub -> main with chn0 idle while the
     * sub channel streams */
    block_free(&pool0);
    pool_enable(&pool1, POOL1);
    pool_enable(&pool0, POOL0);
    printf("both channels streaming: used %zu of %zu, %zu bytes free\n",
           arena.used, arena.size, arena.size - arena.used);
    /* 2068 KiB shared buffer instead of 3068 KiB, and the stock 1 MiB EMC
     * layout (996 KiB at 1080p, 140 KiB at 360p) instead of 2 MiB and
     * 260 KiB: 2.8 MB stay free (1.6 MB with the 2 MiB EMC) */
    assert(arena.size - arena.used >= 2800000u);

    /* random idle/re-enable cycles in any order, OSD bitmaps re-made
     * larger now and then (a longer text), codec restarts while idle */
    for (step = 0; step < 20000u; step++) {
        seed = seed * 1103515245u + 12345u;
        switch ((seed >> 16) % 6u) {
        case 0: block_free(&pool0); break;
        case 1: block_free(&pool1); break;
        case 2: block_free(&pool0); block_free(&pool1); break;
        case 3:
            i = (seed >> 8) % OSD_PIECES;
            block_free(&osd[i]);
            block_alloc(&osd[i], osd_sizes[i] + ((seed >> 4) & 0x3fffu), 1);
            break;
        case 4:
            if (!pool1.live) {
                encoder_destroy(&sub_encoder);
                encoder_create(&sub_encoder, SUB_W, SUB_H);
            }
            break;
        default:
            if (!pool0.live) {
                encoder_destroy(&main_encoder);
                encoder_create(&main_encoder, MAIN_W, MAIN_H);
            }
            break;
        }
        /* a channel comes back while the other one streams, either way */
        if (seed & 0x10u) {
            pool_enable(&pool0, POOL0);
            pool_enable(&pool1, POOL1);
        } else {
            pool_enable(&pool1, POOL1);
            pool_enable(&pool0, POOL0);
        }
    }
    printf("random run: %u pool re-creations, all succeeded; least free "
           "%zu KiB\n", pool_enables, worst_free >> 10);
    assert(top_allocations > 0);
    encoder_destroy(&main_encoder);
    encoder_destroy(&sub_encoder);
    block_free(&pool0);
    block_free(&pool1);
    for (i = 0; i < OSD_PIECES; i++)
        block_free(&osd[i]);
    block_free(&isp);
    /* only the shared bitstream buffer is kept */
    assert(arena.count == 1 && arena.used == OpenIMP_HelixBitstream_Size());
    printf("T21 rmem layout simulation passed\n");
    return 0;
}
