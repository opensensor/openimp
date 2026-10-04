/* Replays the rmem allocation sequence recorded on a T41 (24 MB rmem, 1080p
 * main + 360p sub stream, OSD on both) through the real P2 DMA ledger.
 *
 * Built twice: with PLATFORM_T41 (split arena, best fit) every idle/restart
 * cycle of either channel must succeed; without it (T40 first fit) the test
 * asserts the fragmentation failure the device hit: the sub stream's capture
 * queue lands in the hole the idle main stream left, and the 2 x 3 MB main
 * queue no longer fits although enough memory is free in total. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>   /* declared before the rename below */

/* the last allocation failure report (the map lines that follow it are
 * not kept) */
static char last_log[512];
static void test_syslog(int priority, const char *format, ...)
{
    char line[512];
    va_list args;

    (void)priority;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (strstr(line, " failed: "))
        memcpy(last_log, line, sizeof(line));
}
#define syslog test_syslog
#include "../../src/t40/openimp_p2_dma.c"
#undef syslog

#define RMEM_SIZE (24U * 1024U * 1024U)
#define RMEM_BASE 0x02800000U
static unsigned char arena[16] __attribute__((aligned(4096)));

#define MAX_HELD 16
struct held { uint32_t phys[MAX_HELD]; unsigned int count; };

static int alloc_into(struct held *h, uint32_t size, const char *tag)
{
    IMPDMABufferInfo info;

    if (DMA_AllocDescriptor(&info, (int)size, tag) != 0)
        return -1;
    assert(info.phys_addr >= RMEM_BASE &&
           info.phys_addr + size <= RMEM_BASE + RMEM_SIZE);
    assert(h->count < MAX_HELD);
    h->phys[h->count++] = info.phys_addr;
    return 0;
}

static void free_all(struct held *h)
{
    while (h->count)
        assert(!DMA_FreePhys(h->phys[--h->count]));
}

static void check_ledger(void)
{
    unsigned int i, j;

    for (i = 0; i < P2_DMA_MAX_ALLOCS; ++i) {
        const struct p2_dma_allocation *a = &p2_dma.allocations[i];
        if (!a->active)
            continue;
        assert(!(a->start & 4095U) && a->size <= p2_dma.size - a->start);
        for (j = i + 1; j < P2_DMA_MAX_ALLOCS; ++j) {
            const struct p2_dma_allocation *b = &p2_dma.allocations[j];
            if (b->active)
                assert(a->start + a->size <= b->start ||
                       b->start + b->size <= a->start);
        }
    }
}

/* FrameSource enable: two capture buffers, all or nothing (P1 releases a
 * partial queue). */
static int capture_on(struct held *h, uint32_t size, const char *tag)
{
    if (alloc_into(h, size, tag) || alloc_into(h, size, tag)) {
        free_all(h);
        return -1;
    }
    return 0;
}

static void codec_on(struct held *h, uint32_t strm, uint32_t itm,
                     uint32_t rec)
{
    unsigned int i;

    for (i = 0; i < 4; ++i)
        assert(!alloc_into(h, strm, "AVPU_STRM"));
    assert(!alloc_into(h, 77824, "AVPU_CL"));
    assert(!alloc_into(h, 77824, "AVPU_CL_SUBMIT"));
    assert(!alloc_into(h, itm, "AVPU_ITM"));
    assert(!alloc_into(h, rec, "AVPU_REC"));
    assert(!alloc_into(h, 16384, "AVPU_EP3"));
}

int main(void)
{
    static const uint32_t osd[] = { 73728, 73728, 32768, 16384, 12288, 16384,
                                    8192, 16384, 73728, 32768, 12288, 8192 };
    struct held isp = { {0}, 0 }, osd0 = { {0}, 0 }, cap0 = { {0}, 0 };
    struct held cap1 = { {0}, 0 }, enc0 = { {0}, 0 }, enc1 = { {0}, 0 };
    unsigned int i, cycle;
    int failed = 0;

    p2_dma.mapping = arena;     /* never dereferenced by the ledger */
    p2_dma.base = RMEM_BASE;
    p2_dma.size = RMEM_SIZE;
    p2_dma.floor = p2_dma.next = 0;

    assert(!alloc_into(&isp, 8388608, "isp-mdns"));
    for (i = 0; i < sizeof(osd) / sizeof(osd[0]); ++i)
        assert(!alloc_into(&osd0, osd[i], "osd-bitmap"));
    /* start-up: main stream once on and idle again */
    assert(!capture_on(&cap0, 3133440, "capture0"));
    free_all(&cap0);
    /* main viewer: capture, then the encoder's long-lived buffers */
    assert(!capture_on(&cap0, 3133440, "capture0"));
    codec_on(&enc0, 950272, 36864, 3821568);
    assert(!capture_on(&cap1, 356352, "capture1"));
    codec_on(&enc1, 122880, 28672, 487424);
    check_ledger();
    /* 24176 KB of 24576 KB in use, as on the camera */
    assert(p2_dma_used_bytes() / 1024U == 24176U);

    for (cycle = 0; cycle < 5 && !failed; ++cycle) {
        free_all(&cap0);                        /* main idles */
        if (cycle & 1) {                        /* sub restarts meanwhile */
            free_all(&cap1);
            assert(!capture_on(&cap1, 356352, "capture1"));
        }
        if (capture_on(&cap0, 3133440, "capture0")) {
            failed = 1;
            break;
        }
        check_ledger();
        free_all(&cap1);                        /* sub idles, main stays */
        assert(!capture_on(&cap1, 356352, "capture1"));
        check_ledger();
    }
#if defined(PLATFORM_T41)
    assert(!failed && cycle == 5);
    /* a real shortage is reported with the missing amount */
    {
        struct held big = { {0}, 0 };
        assert(alloc_into(&big, 1024U * 1024U, "test-big") < 0);
            assert(strstr(last_log, "rmem allocation of 1024 KB for test-big "
                      "failed: used 24176 KB of 24576 KB, free 400 KB, "
                      "largest block 400 KB; raise rmem by at least 624 KB "
                      "(suggest rmem=25M)"));
    }
    printf("t41 rmem fit: 5 idle/restart cycles of both channels, "
           "%u KB used, largest free %u KB\n", p2_dma_used_bytes() / 1024U,
           p2_dma_largest_gap() / 1024U);
#else
    /* first fit fragments: plenty free in total, no 3 MB block */
    assert(failed && cycle == 1);
    assert(strstr(last_log, "for capture0 failed") &&
           strstr(last_log, "(fragmented)"));
    printf("t40 first fit: fragmentation reproduced in cycle %u: %s\n",
           cycle + 1, last_log);
#endif
    return 0;
}
