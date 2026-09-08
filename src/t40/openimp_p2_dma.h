#ifndef OPENIMP_P2_DMA_H
#define OPENIMP_P2_DMA_H
#include <stdint.h>

/* Internal shared arena owner. Callers borrow its mapping, never munmap it.
 * Allocation/free go through DMA_AllocDescriptor/DMA_FreePhys for all users.
 * This owner never calls back into P1, keeping the P1 -> arena lock order.
 */
int OpenIMP_P2_DMARegion(uint32_t *base, uint32_t *size, void **mapping);
int OpenIMP_P2_DMAState(uint32_t *base, uint32_t *used);

#endif
