/**
 * DMA Allocator Interface
 * Based on reverse engineering of libimp.so v1.1.6
 */

#ifndef DMA_ALLOC_H
#define DMA_ALLOC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Allocate DMA buffer
 * @param name Buffer name (also used to return buffer info)
 * @param size Buffer size in bytes
 * @param tag Tag string for debugging
 * @return 0 on success, -1 on failure
 */
int IMP_Alloc(char *name, int size, char *tag);

/**
 * Allocate DMA buffer from a specific pool
 * @param pool_id Pool ID
 * @param name Buffer name (also used to return buffer info)
 * @param size Buffer size in bytes
 * @param tag Tag string for debugging
 * @return 0 on success, -1 on failure
 */
int IMP_PoolAlloc(int pool_id, char *name, int size, char *tag);

/**
 * Free DMA buffer
 * @param phys_addr Physical address of buffer to free
 * @return 0 on success, -1 on failure
 */
int IMP_Free(uint32_t phys_addr);

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

#ifdef __cplusplus
}
#endif

#endif /* DMA_ALLOC_H */

