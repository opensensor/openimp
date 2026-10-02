#ifndef OPENIMP_HELIX_BITSTREAM_H
#define OPENIMP_HELIX_BITSTREAM_H

#include <stdint.h>

#include "dma_alloc.h"

/*
 * One bitstream buffer for every Helix job of the process (T21).
 *
 * The stock T21 libimp allocates a single bitstream buffer in EncoderInit
 * ("vpuBs", 1920 * 1080 bytes unless IMP_Encoder_SetPoolSize says
 * otherwise); every H.264 channel and the JPEG encoder have the VPU write
 * into it in turn (bsbufsem) and the data is copied out before the next
 * job.  Per-channel bitstream windows instead cost one window per channel
 * on top of it.
 *
 * Lock() takes the buffer for one job, growing it to at least `size` bytes
 * first; the caller builds, runs and copies out its job and then calls
 * Unlock().  The buffer lives at the top of the reserved arena and is kept
 * for the life of the process.
 */

/* Takes the buffer, at least size bytes; 0 with *dma filled (owned by this
 * module: never free it) and the lock held, -1 without the lock. */
int OpenIMP_HelixBitstream_Lock(uint32_t size, IMPDMABufferInfo *dma);
void OpenIMP_HelixBitstream_Unlock(void);

/* Grows the buffer to size bytes now (channel creation, as the stock
 * library allocates its encoder buffers at start-up). */
int OpenIMP_HelixBitstream_Reserve(uint32_t size);

/* Current size in bytes (0: not allocated). */
uint32_t OpenIMP_HelixBitstream_Size(void);

#endif
