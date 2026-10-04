/* Real src/dma_alloc.c reserved-arena path on a private buffer: the peak
 * line after a stream started and on every new peak (never otherwise), and
 * the shortage line with the missing size and an rmem= suggestion. */
#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#define syslog test_syslog
static int test_syslog(int prio, const char *fmt, ...);
#include "../../src/dma_alloc.c"
#undef syslog

static char lines[16][256];
static int line_prio[16];
static int nlines;

static int test_syslog(int prio, const char *fmt, ...)
{
    va_list ap;

    assert(nlines < 16);
    va_start(ap, fmt);
    vsnprintf(lines[nlines], sizeof(lines[0]), fmt, ap);
    va_end(ap);
    line_prio[nlines++] = prio;
    return 0;
}

static int count(const char *needle)
{
    int i, n = 0;

    for (i = 0; i < nlines; i++)
        if (strstr(lines[i], needle))
            n++;
    return n;
}

static unsigned char arena[1024 * 1024] __attribute__((aligned(4096)));

int main(void)
{
    IMPDMABufferInfo a, b, c, big;
    size_t peak = 0;

    g_dma_initialized = 1;
    g_rmem_supported = 1;
    g_mem_fd = open("/dev/null", O_RDWR);
    g_is_rmem = 1;
    g_rmem_virt_base = arena;
    g_rmem_base_phys = 0x1000000;
    g_rmem_size = sizeof(arena);

    /* Start-up allocations are silent. */
    assert(!DMA_AllocDescriptor(&a, 100 * 1024, "pool"));
    assert(nlines == 0);
    DMA_RmemStreamStarted();
    assert(nlines == 1 && line_prio[0] == LOG_INFO);
    assert(!strcmp(lines[0], "[DMA] rmem peak 100 KB of 1024 KB (free 924 KB)"));
    DMA_RmemStreamStarted();
    assert(nlines == 1);                      /* once, not per call */

    /* A new peak logs, a repeat of the old level does not. */
    assert(!DMA_AllocDescriptor(&b, 200 * 1024, "encoder"));
    assert(nlines == 2);
    assert(!strcmp(lines[1], "[DMA] rmem peak 300 KB of 1024 KB (free 724 KB)"));
    assert(!DMA_FreePhys(b.phys_addr));
    for (int i = 0; i < 1000; i++) {          /* per-frame churn: silent */
        assert(!DMA_AllocDescriptor(&c, 200 * 1024, "frame"));
        assert(!DMA_FreePhys(c.phys_addr));
    }
    assert(nlines == 2);
    assert(!DMA_RmemPeak(&peak) && peak == 300 * 1024);

    /* Shortage: 2000 KB wanted, 924 KB free behind the pool. */
    assert(DMA_AllocDescriptor(&big, 2000 * 1024, "big") < 0);
    assert(count("[DMA] rmem allocation of 2000 KB for big failed: free 924 KB, "
                 "largest block 924 KB; raise rmem by at least 1076 KB "
                 "(suggest rmem=3M)") == 1);
    for (int i = 2; i < nlines; i++)
        if (strstr(lines[i], "allocation of"))
            assert(line_prio[i] == LOG_ERR);
    assert(!DMA_FreePhys(a.phys_addr));
    puts("rmem high-water and shortage logging passed");
    return 0;
}
