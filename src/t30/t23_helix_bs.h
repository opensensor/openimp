#ifndef OPENIMP_T23_HELIX_BS_H
#define OPENIMP_T23_HELIX_BS_H

#include <stdint.h>

#include "dma_alloc.h"

/*
 * T23: one Helix bitstream area for the native H.264 channels and the
 * hardware JPEG encoder, like the stock "vpuBs" that the T23 libimp shares
 * between its H.264 and JPEG jobs under bsbufsem.
 *
 * The first H.264 channel allocates it directly below its EMC scratch (the
 * arena is filled top-down): the Helix core runs past the window end when a
 * picture overflows, and the spill then lands in that scratch, never in a
 * reference picture.  The area is only used while that guard sits right
 * above it; when the guard's channel is destroyed first, this module keeps
 * the scratch allocation as the guard until the last H.264 channel leaves.
 * Further H.264 channels with a window that fits use the same area.
 *
 * The JPEG encoder borrows the area per picture when it exists
 * (JPGC_MAX_BS stops the JPEG core at its limit, so it never spills) and
 * falls back to a buffer of its own otherwise.
 *
 * Lock() serializes the jobs: taken from the command list / slice header to
 * the copy of the bitstream out of the area.
 */

/* 0 and *dma filled (owned by this module) with the lock held when the
 * area exists and holds size bytes; -1 without the lock otherwise. */
int OpenIMP_T23_HelixBs_Lock(uint32_t size, IMPDMABufferInfo *dma);
void OpenIMP_T23_HelixBs_Unlock(void);
/* Size of the area, 0 when no H.264 channel holds it. */
uint32_t OpenIMP_T23_HelixBs_Size(void);
/* helix_jpeg.c: called by the encoder when the area was created. */
void OpenIMP_HelixJpeg_AreaReady(void);

#endif
