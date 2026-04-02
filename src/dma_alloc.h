/**
 * DMA Allocator Interface
 * Based on reverse engineering of libimp.so v1.1.6
 */

#ifndef DMA_ALLOC_H
#define DMA_ALLOC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char name[96];
    char tag[32];
    uint32_t virt_addr;
    uint32_t phys_addr;
    uint32_t size;
    uint32_t flags;
    uint32_t pool_id;
} IMPDMABufferInfo;

/**
 * Allocate DMA buffer and return the OEM 0x94-byte descriptor.
 */
int DMA_AllocDescriptor(IMPDMABufferInfo *info_out, int size, const char *tag);

/**
 * Allocate DMA buffer from a specific pool and return the OEM descriptor.
 */
int DMA_PoolAllocDescriptor(int pool_id, IMPDMABufferInfo *info_out, int size, const char *tag);

/**
 * Free DMA buffer by physical address.
 */
int DMA_FreePhys(uint32_t phys_addr);

/**
 * Translate DMA physical address to virtual address.
 */
void *DMA_PhysToVirt(uint32_t phys_addr);

/**
 * Translate DMA virtual address to physical address.
 */
uint32_t DMA_VirtToPhys(const void *virt_addr);

/**
 * Get buffer information
 * @param info_out Output buffer for info (0x94 bytes)
 * @param phys_addr Physical address of buffer
 * @return 0 on success, -1 on failure
 */
int IMP_Get_Info(void *info_out, uint32_t phys_addr);

/**
 * Get pool ID for a FrameSource channel
 * @param chn Channel number
 * @return Pool ID, or -1 if no pool
 */
int IMP_FrameSource_GetPool(int chn);

/**
 * Flush cache for DMA buffer
 * @param phys_addr Physical address
 * @param size Buffer size
 * @return 0 on success, -1 on failure
 */
int IMP_Flush_Cache(uint32_t phys_addr, uint32_t size);

/**
 * Get RMEM base physical address if using /dev/rmem bump allocator
 * @param base_phys_out Output pointer for base physical address
 * @return 0 if RMEM is active and base is valid, -1 otherwise
 */
int DMA_Get_RMEM_Base(uint32_t *base_phys_out);

/**
 * Check if RMEM bump allocator is active
 * @return 1 if RMEM is active, 0 otherwise
 */
int DMA_Is_RMEM(void);

/**
 * OEM-compatible cache flush via /dev/rmem ioctl 0xc00c7200.
 * This is the exact path the stock libimp uses:
 *   Rtos_FlushCacheMemory → alloc_kmem_flush_cache → ioctl(rmem_fd, 0xc00c7200, {vaddr, size, dir})
 * @param virt_addr  Virtual (mmap'd) address
 * @param size       Size in bytes to flush
 * @param dir        1=WBACK (DMA_TO_DEVICE), 2=INV (DMA_FROM_DEVICE)
 * @return 0 on success, -1 on failure
 */
int DMA_RmemFlushCache(void *virt_addr, uint32_t size, int dir);

#ifdef __cplusplus
}
#endif

#endif /* DMA_ALLOC_H */

