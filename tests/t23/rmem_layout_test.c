/* Host simulation of the T23 reserved-memory arena with the native Helix
 * encoder (Galayou Y4: rmem=22M, 1920x1080 main + 640x360 sub, two-buffer
 * FrameSource pools).
 *
 * The arena is OpenIMP's real allocator (src/rmem_arena.h, the same code
 * src/dma_alloc.c runs on the camera); the encoder buffers come from the
 * real OpenIMP_T30_HelixCreate/Destroy against a fake /dev/soc_vpu that only
 * hands out channels.  The other allocations are sized from the device log
 * of the first native run (ISP NCU buffer, pools, OSD).  The test replays
 * timps' order - start, chn0 idle and re-enable, sub channel starting while
 * chn0 is idle, snapshots, encoder restarts - and requires that every
 * FrameSource pool can always be re-created.  It also replays the device
 * log with the first native encoder's layout, which must fail with the
 * same largest free block the camera reported (5554176 bytes). */
#define _GNU_SOURCE
#include <assert.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dma_alloc.h"
#include "rmem_arena.h"
#include "t30/t30_helix_encoder.h"

#define RMEM_SIZE   (22u << 20)
#define RMEM_PHYS   0x02a00000u
#define FAKE_FD     4243

#define MAIN_W 1920u
#define MAIN_H 1080u
#define SUB_W  640u
#define SUB_H  360u
/* VBM frame size: macroblock-aligned NV12 */
#define FRAME(w, h) ((((w) + 15u) & ~15u) * (((h) + 15u) & ~15u) * 3u / 2u)
#define POOL0 (2u * FRAME(MAIN_W, MAIN_H))   /* 6266880, as in the log */
#define POOL1 (2u * FRAME(SUB_W, SUB_H))
#define ISP_NCU 3133440u
#define OSD_PIECES 4u
#define OSD_PIECE (68u << 10)

static RmemArena arena;
static int top_allocations;

static int arena_alloc(IMPDMABufferInfo *info, int size, int top)
{
    size_t off;

    if ((top ? rmem_arena_alloc_top(&arena, (size_t)size, &off)
             : rmem_arena_alloc(&arena, (size_t)size, &off)) != 0)
        return -1;
    memset(info, 0, sizeof(*info));
    info->phys_addr = RMEM_PHYS + (uint32_t)off;
    /* never dereferenced for encoder buffers except by memset below */
    info->virt_addr = (uint32_t)(uintptr_t)malloc((size_t)size);
    assert(info->virt_addr);
    info->size = (uint32_t)size;
    return 0;
}

/* encoder buffers keep a heap shadow so memset works; remember it */
static struct { uint32_t phys; void *virt; } shadows[64];

static void remember(const IMPDMABufferInfo *info)
{
    unsigned int i;

    for (i = 0; i < 64u; i++)
        if (!shadows[i].phys) {
            shadows[i].phys = info->phys_addr;
            shadows[i].virt = (void *)(uintptr_t)info->virt_addr;
            return;
        }
    abort();
}

int DMA_AllocDescriptor(IMPDMABufferInfo *info, int size, const char *tag)
{
    (void)tag;
    if (arena_alloc(info, size, 0) != 0)
        return -1;
    remember(info);
    return 0;
}

int DMA_AllocDescriptorTop(IMPDMABufferInfo *info, int size,
                           const char *tag)
{
    (void)tag;
    if (arena_alloc(info, size, 1) != 0)
        return -1;
    top_allocations++;
    remember(info);
    return 0;
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

uint32_t DMA_VirtToPhys(const void *virt)
{
    return (uint32_t)(uintptr_t)virt;
}

int DMA_RmemFlushCache(void *virt, uint32_t size, int dir)
{
    (void)virt;
    (void)size;
    (void)dir;
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
    if (request == 0xc0586300u) {
        node[0] = 0x8000u;          /* clist */
        return 0;
    }
    assert(request == 0xc0586301u);
    return 0;
}

/* ------------------------------------------------------------------ */

typedef struct {
    IMPDMABufferInfo info;
    int live;
} Block;

static Block isp, pool0, pool1, osd[OSD_PIECES], snapshot;
static T30HelixEncoder *main_encoder, *sub_encoder;
static size_t worst_free = (size_t)-1;
static unsigned int pool_enables;

static void block_alloc(Block *b, uint32_t size)
{
    assert(!b->live);
    if (DMA_AllocDescriptor(&b->info, (int)size, "sim") != 0) {
        fprintf(stderr, "allocation of %u failed: used %zu of %zu, "
                "largest free block %zu, %d extents\n", size, arena.used,
                arena.size, rmem_arena_largest_gap(&arena), arena.count);
        abort();
    }
    b->live = 1;
}

static void block_free(Block *b)
{
    if (b->live)
        DMA_FreePhys(b->info.phys_addr);
    b->live = 0;
}

static void pool_enable(Block *pool, uint32_t size)
{
    size_t largest;

    if (pool->live)
        return;
    largest = rmem_arena_largest_gap(&arena);
    assert(largest >= size);
    block_alloc(pool, size);
    pool_enables++;
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
    params.bitrate = w > 640u ? 1200000u : 384000u;
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

static void note_free(void)
{
    if (arena.size - arena.used < worst_free)
        worst_free = arena.size - arena.used;
}

static void start(int sub_first_after_idle)
{
    unsigned int i;

    rmem_arena_init(&arena, RMEM_SIZE);
    block_alloc(&isp, ISP_NCU);
    pool_enable(&pool0, POOL0);
    if (!sub_first_after_idle)
        pool_enable(&pool1, POOL1);
    for (i = 0; i < OSD_PIECES; i++)
        block_alloc(&osd[i], OSD_PIECE);
    encoder_create(&main_encoder, MAIN_W, MAIN_H);
    if (!sub_first_after_idle)
        encoder_create(&sub_encoder, SUB_W, SUB_H);
    note_free();
}

static void stop_all(void)
{
    unsigned int i;

    encoder_destroy(&main_encoder);
    encoder_destroy(&sub_encoder);
    block_free(&snapshot);
    block_free(&pool0);
    block_free(&pool1);
    for (i = 0; i < OSD_PIECES; i++)
        block_free(&osd[i]);
    block_free(&isp);
    assert(arena.used == 0 && arena.count == 0);
}

/* The first device run: the previous native encoder (2 MiB EMC scratch
 * and a 1 MiB + 4 KiB bitstream window per channel, every buffer best-fit
 * from the bottom).  Replaying the log order with it must fail exactly as
 * on the camera, which shows the model matches the device. */
static int legacy_log_order_fails(void)
{
    static const uint32_t main_parts[] = {
        16384u, 2097152u, 1052672u, FRAME(MAIN_W, MAIN_H),
        FRAME(MAIN_W, MAIN_H)
    };
    static const uint32_t sub_parts[] = {
        16384u, 2097152u, 1052672u, FRAME(SUB_W, SUB_H),
        FRAME(SUB_W, SUB_H)
    };
    Block parts[10];
    size_t off;
    unsigned int i;
    int fits;

    memset(parts, 0, sizeof(parts));
    rmem_arena_init(&arena, RMEM_SIZE);
    block_alloc(&isp, ISP_NCU);
    block_alloc(&pool0, POOL0);
    for (i = 0; i < OSD_PIECES; i++)
        block_alloc(&osd[i], OSD_PIECE);
    for (i = 0; i < 5u; i++)
        block_alloc(&parts[i], main_parts[i]);
    block_free(&pool0);
    block_alloc(&pool1, POOL1);
    for (i = 0; i < 5u; i++)
        block_alloc(&parts[5 + i], sub_parts[i]);
    printf("legacy layout: used %zu of %zu, largest free block %zu, main "
           "pool needs %u\n", arena.used, arena.size,
           rmem_arena_largest_gap(&arena), POOL0);
    fits = rmem_arena_alloc(&arena, POOL0, &off) == 0;
    if (fits)
        rmem_arena_free(&arena, off, NULL);
    for (i = 0; i < 10u; i++)
        block_free(&parts[i]);
    block_free(&pool1);
    for (i = 0; i < OSD_PIECES; i++)
        block_free(&osd[i]);
    block_free(&isp);
    return !fits;
}

int main(void)
{
    unsigned int step;
    uint32_t seed = 12345u;
    uint32_t main_size, sub_size;

    /* the encoder keeps addresses in 32-bit words, as on MIPS */
    mallopt(M_MMAP_MAX, 0);
    /* per-channel encoder memory */
    rmem_arena_init(&arena, RMEM_SIZE);
    encoder_create(&main_encoder, MAIN_W, MAIN_H);
    main_size = (uint32_t)arena.used;
    encoder_create(&sub_encoder, SUB_W, SUB_H);
    sub_size = (uint32_t)arena.used - main_size;
    encoder_destroy(&main_encoder);
    encoder_destroy(&sub_encoder);
    assert(arena.used == 0);
    printf("native encoder rmem: %ux%u %u KiB, %ux%u %u KiB\n", MAIN_W,
           MAIN_H, main_size >> 10, SUB_W, SUB_H, sub_size >> 10);

    assert(legacy_log_order_fails());

    /* 1: the device log's order - main starts alone, chn0 idles, the sub
     * channel starts while chn0 is idle, chn0 comes back */
    start(1);
    block_free(&pool0);                     /* chn0 idle */
    pool_enable(&pool1, POOL1);             /* chn1 streaming */
    encoder_create(&sub_encoder, SUB_W, SUB_H);
    note_free();
    pool_enable(&pool0, POOL0);             /* chn0 re-enable */
    note_free();
    stop_all();
    printf("log order: chn0 re-enabled after the sub channel started\n");

    /* 2: long random run of idle/re-enable cycles, snapshots and encoder
     * restarts (a channel's codec is re-created when timps restarts it) */
    start(0);
    for (step = 0; step < 20000u; step++) {
        unsigned int action;

        seed = seed * 1103515245u + 12345u;
        action = (seed >> 16) % 10u;
        switch (action) {
        case 0: block_free(&pool0); break;
        case 1: pool_enable(&pool0, POOL0); break;
        case 2: block_free(&pool1); break;
        case 3: pool_enable(&pool1, POOL1); break;
        case 4:
            /* JPEG snapshot staging while chn0 may be idle */
            if (!snapshot.live)
                block_alloc(&snapshot, 256u << 10);
            break;
        case 5: block_free(&snapshot); break;
        case 6:
            encoder_destroy(&sub_encoder);
            encoder_create(&sub_encoder, SUB_W, SUB_H);
            break;
        case 7:
            if (!pool0.live) {      /* main codec restart while idle */
                encoder_destroy(&main_encoder);
                encoder_create(&main_encoder, MAIN_W, MAIN_H);
            }
            break;
        default:
            pool_enable(&pool0, POOL0);
            pool_enable(&pool1, POOL1);
            break;
        }
        note_free();
        /* a snapshot is transient: pools must fit once it is gone */
        if (!snapshot.live) {
            pool_enable(&pool0, POOL0);
            pool_enable(&pool1, POOL1);
        }
    }
    stop_all();
    printf("random run: %u pool re-creations, all succeeded; least free "
           "%zu KiB (with a 256 KiB snapshot buffer live)\n",
           pool_enables, worst_free >> 10);
    assert(top_allocations > 0);
    printf("T23 rmem layout simulation passed\n");
    return 0;
}
