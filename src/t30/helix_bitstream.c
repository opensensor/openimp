/* One Helix bitstream buffer shared by all jobs; see helix_bitstream.h. */
#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

#include "imp_log_int.h"
#include "t30/helix_bitstream.h"

static struct {
    pthread_mutex_t lock;
    IMPDMABufferInfo dma;
    uint32_t pool_size;        /* IMP_Encoder_SetPoolSize, 0 = default */
    uint32_t refused;          /* size that found no rmem (0 = none) */
    uint32_t skipped;          /* requests not retried since */
} helix_bs = { .lock = PTHREAD_MUTEX_INITIALIZER };

uint32_t OpenIMP_HelixBitstream_PoolSize(void)
{
    uint32_t size;

    pthread_mutex_lock(&helix_bs.lock);
    size = helix_bs.pool_size ? helix_bs.pool_size
                              : OPENIMP_HELIX_BS_DEFAULT_POOL;
    pthread_mutex_unlock(&helix_bs.lock);
    return size;
}

#if defined(PLATFORM_T21) && !defined(PLATFORM_T20) && \
    !defined(PLATFORM_T23)
/* Stock: stores the size for the next EncoderInit (IMP_System_Init);
 * call it before IMP_System_Init.  Sizes <= 0 are refused. */
int IMP_Encoder_SetPoolSize(int size)
{
    if (size <= 0)
        return -1;
    pthread_mutex_lock(&helix_bs.lock);
    helix_bs.pool_size = (uint32_t)size;
    pthread_mutex_unlock(&helix_bs.lock);
    return 0;
}
#endif

static int helix_bs_alloc(uint32_t size, IMPDMABufferInfo *next)
{
    memset(next, 0, sizeof(*next));
    if (DMA_AllocDescriptorTop(next, (int)size, "helix-bs") != 0 ||
        !next->phys_addr || !next->virt_addr) {
        memset(next, 0, sizeof(*next));
        return -1;
    }
    /* No dirty line of an earlier user may be evicted over what the VPU
     * writes: drop the range from the cache. */
    if (DMA_RmemFlushCache((void *)(uintptr_t)next->virt_addr, size, 2) != 0) {
        DMA_FreePhys(next->phys_addr);
        memset(next, 0, sizeof(*next));
        return -1;
    }
    return 0;
}

/* Called with the lock held.  The buffer is the pool size from the start
 * (stock vpuBs) and only grows when a smaller IMP_Encoder_SetPoolSize
 * leaves less than one H.264 window or one JPEG macroblock row; the larger
 * buffer is allocated before the old one is freed, so the old one is kept
 * if rmem has no room, and a size that failed is tried again only every
 * 64th request, not for every picture. */
static int helix_bs_grow_locked(uint32_t size)
{
    uint32_t old_size = helix_bs.dma.size;
    size_t used = 0, total = 0, largest = 0;
    IMPDMABufferInfo next;

    /* a new buffer has at least the pool size, as the stock vpuBs */
    if (!helix_bs.dma.phys_addr) {
        uint32_t pool = helix_bs.pool_size ? helix_bs.pool_size
                                           : OPENIMP_HELIX_BS_DEFAULT_POOL;

        if (size < pool)
            size = pool;
    }
    if (size > (uint32_t)INT32_MAX - 4095u)
        return -1;
    size = (size + 4095u) & ~4095u;
    if (helix_bs.dma.phys_addr && helix_bs.dma.size >= size)
        return 0;
    /* after a failure, try again only every 64th request */
    if (helix_bs.refused && size >= helix_bs.refused &&
        (++helix_bs.skipped & 63u))
        return -1;
    if (helix_bs_alloc(size, &next) == 0) {
        if (helix_bs.dma.phys_addr)
            DMA_FreePhys(helix_bs.dma.phys_addr);
        helix_bs.dma = next;
        helix_bs.refused = 0;
        (void)DMA_RmemStats(&used, &total, &largest);
        IMP_LOG_INFO("Encoder", "Helix: shared bitstream buffer %u bytes "
                     "at 0x%08x%s (rmem used %zu of %zu, largest free "
                     "block %zu)", size, helix_bs.dma.phys_addr,
                     old_size ? ", grown" : "", used, total, largest);
        return 0;
    }
    if (helix_bs.refused == size)
        return -1;          /* logged before */
    helix_bs.refused = size;
    helix_bs.skipped = 0;
    (void)DMA_RmemStats(&used, &total, &largest);
    IMP_LOG_ERR("Encoder", "Helix: no rmem for a %u-byte shared bitstream "
                "buffer%s (rmem used %zu of %zu, largest free block %zu)",
                size, old_size ? ", keeping the smaller one" : "", used,
                total, largest);
    return -1;
}

int OpenIMP_HelixBitstream_Lock(uint32_t size, IMPDMABufferInfo *dma)
{
    if (!dma || !size)
        return -1;
    pthread_mutex_lock(&helix_bs.lock);
    if (helix_bs_grow_locked(size) != 0) {
        pthread_mutex_unlock(&helix_bs.lock);
        return -1;
    }
    *dma = helix_bs.dma;
    return 0;
}

int OpenIMP_HelixBitstream_LockTimeout(uint32_t size, IMPDMABufferInfo *dma,
                                       uint32_t timeout_ms)
{
    struct timespec deadline;

    if (!dma || !size)
        return -1;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeout_ms / 1000u;
    deadline.tv_nsec += (long)(timeout_ms % 1000u) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }
    if (pthread_mutex_timedlock(&helix_bs.lock, &deadline) != 0)
        return -EBUSY;
    if (helix_bs_grow_locked(size) != 0) {
        pthread_mutex_unlock(&helix_bs.lock);
        return -1;
    }
    *dma = helix_bs.dma;
    return 0;
}

void OpenIMP_HelixBitstream_Unlock(void)
{
    pthread_mutex_unlock(&helix_bs.lock);
}

int OpenIMP_HelixBitstream_Reserve(uint32_t size)
{
    int ret;

    if (!size)
        return -1;
    pthread_mutex_lock(&helix_bs.lock);
    ret = helix_bs_grow_locked(size);
    pthread_mutex_unlock(&helix_bs.lock);
    return ret;
}

int OpenIMP_HelixBitstream_Init(void)
{
    return OpenIMP_HelixBitstream_Reserve(1u);
}

void OpenIMP_HelixBitstream_Exit(void)
{
    pthread_mutex_lock(&helix_bs.lock);
    if (helix_bs.dma.phys_addr)
        DMA_FreePhys(helix_bs.dma.phys_addr);
    memset(&helix_bs.dma, 0, sizeof(helix_bs.dma));
    helix_bs.refused = 0;
    pthread_mutex_unlock(&helix_bs.lock);
}

uint32_t OpenIMP_HelixBitstream_Size(void)
{
    uint32_t size;

    pthread_mutex_lock(&helix_bs.lock);
    size = helix_bs.dma.size;
    pthread_mutex_unlock(&helix_bs.lock);
    return size;
}
