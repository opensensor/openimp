/*
 * T30 Radix (H.265) command list builder.
 *
 * The Radix scheduler (CFGC ACM_CTRL, kernel drivers/video/soc_vpu/radix/
 * radix.c vpu_start) reads a list of (value, VLD bit 31 | TERM bit 30 |
 * register offset) word pairs.  t30_radix_build_cmdlist() writes the list
 * for one picture from its parameter block, like the vendor libimp 1.0.5
 * H265E_T30_SliceInit (0x3ad5c).
 */
#ifndef T30_RADIX_CMDLIST_H
#define T30_RADIX_CMDLIST_H

#include <stddef.h>
#include <stdint.h>

#include "t30/radix/t30_radix_sliceinfo.h"

/* Upper bound of the list in 32-bit words (pairs * 2): the fixed part is
 * 511 pairs for P and 373 for I, plus bu_len (<= 255) rate-control pairs and
 * 2 * qp_table_len QP-table pairs. */
#define T30_RADIX_CMDLIST_MAX_WORDS(qp_table_len) \
    (2u * (560u + 255u + 2u * (unsigned)(qp_table_len)))

/*
 * si:       the 0x1204-byte parameter block (t30_radix_sliceinfo.h).  Like the
 *           vendor function the builder stores md_bc_close (= !sobel_en).
 * list:     output, T30_RADIX_CMDLIST_MAX_WORDS() words.
 * state:    the CABAC context state bytes (at least 0x99); the vendor reads
 *           them through the pointer at 0x11c.
 * qp_table: the per-CTB QP table words when qp_table_len is non-zero (the
 *           vendor reads them through the pointer at 0xb4).
 *           NULL for either means "use the pointer in the block" (32-bit
 *           targets only).
 * Returns the number of words written (always even).
 */
size_t t30_radix_build_cmdlist(uint8_t *si, uint32_t *list,
                               const uint8_t *state,
                               const uint32_t *qp_table);

#endif
