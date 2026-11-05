/*
 * CABAC context initialisation for the T30 Radix encoder.
 *
 * The Radix parameter block carries the initial context states of the
 * slice (block offset 0x11c points to them; t30_radix_cmdlist.c sends
 * them to the BC and SDE blocks).  The vendor libimp 1.0.5 takes them from
 * its entropy coder after Entropy::resetEntropy (0x6476c): 156 bytes, one per
 * context, (state << 1) | MPS, from the HEVC initValue tables (H.265 9.3.2.2)
 * for the slice type and slice QP.
 */
#ifndef T30_RADIX_CABAC_H
#define T30_RADIX_CABAC_H

#include <stdint.h>

#define T30_RADIX_CABAC_CONTEXTS 156

/* slice_type: 0 B, 1 P, 2 I (the order of the block's frame_type) */
void t30_radix_cabac_init(uint8_t state[T30_RADIX_CABAC_CONTEXTS],
                          int slice_type, int qp);

#endif
