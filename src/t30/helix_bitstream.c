/* One Helix bitstream buffer shared by all jobs; see helix_bitstream.h. */
#include <pthread.h>
#include <stddef.h>
#include <string.h>

#include "imp_log_int.h"
#include "t30/helix_bitstream.h"

static struct {
    pthread_mutex_t lock;
    IMPDMABufferInfo dma;
} helix_bs = { .lock = PTHREAD_MUTEX_INITIALIZER };

static int helix_bs_alloc(uint32_t size)
{
    IMPDMABufferInfo next;

    memset(&next, 0, sizeof(next));
    if (DMA_AllocDescriptorTop(&next, (int)size, "helix-bs") != 0 ||
        !next.phys_addr || !next.virt_addr) {
        memset(&next, 0, sizeof(next));
        return -1;
    }
    /* No dirty line of an earlier user may be evicted over what the VPU
     * writes: drop the range from the cache. */
    if (DMA_RmemFlushCache((void *)(uintptr_t)next.virt_addr, size, 2) != 0) {
        DMA_FreePhys(next.phys_addr);
        return -1;
    }
    helix_bs.dma = next;
    return 0;
}

/* Called with the lock held. */
static int helix_bs_grow_locked(uint32_t size)
{
    uint32_t old_size = helix_bs.dma.size;
    size_t used = 0, total = 0, largest = 0;

    size = (size + 4095u) & ~4095u;
    if (size > (uint32_t)INT32_MAX)
        return -1;
    if (helix_bs.dma.phys_addr && helix_bs.dma.size >= size)
        return 0;
    /* Nobody uses the old buffer while the lock is held: release it first,
     * so growing needs the difference and not both buffers. */
    if (helix_bs.dma.phys_addr) {
        DMA_FreePhys(helix_bs.dma.phys_addr);
        memset(&helix_bs.dma, 0, sizeof(helix_bs.dma));
    }
    if (helix_bs_alloc(size) == 0) {
        (void)DMA_RmemStats(&used, &total, &largest);
        IMP_LOG_INFO("Encoder", "Helix: shared bitstream buffer %u bytes "
                     "at 0x%08x%s (rmem used %zu of %zu, largest free "
                     "block %zu)", size, helix_bs.dma.phys_addr,
                     old_size ? ", grown" : "", used, total, largest);
        return 0;
    }
    (void)DMA_RmemStats(&used, &total, &largest);
    IMP_LOG_ERR("Encoder", "Helix: no rmem for a %u-byte shared bitstream "
                "buffer (rmem used %zu of %zu, largest free block %zu)",
                size, used, total, largest);
    if (old_size)
        (void)helix_bs_alloc(old_size);
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

uint32_t OpenIMP_HelixBitstream_Size(void)
{
    uint32_t size;

    pthread_mutex_lock(&helix_bs.lock);
    size = helix_bs.dma.size;
    pthread_mutex_unlock(&helix_bs.lock);
    return size;
}
