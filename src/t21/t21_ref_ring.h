#ifndef OPENIMP_T21_REF_RING_H
#define OPENIMP_T21_REF_RING_H

#include <stdint.h>

/* Shared reference/reconstruction ring of the T21-family Helix core (OEM
 * BUF_SHARE_CFG, libimp 1.0.33 T21 and 1.3.0 T23, same arithmetic).
 *
 * One ring per channel holds the picture plus 256 lines ((1117+1)*64 with
 * the OEM value 3).  Picture n is reconstructed 256 lines further from the
 * ring start than picture n-1: the luma position is end - (n * step) mod
 * ring (the start when the remainder is 0), the core wraps writes at the
 * ring end.  Picture n predicts from picture n-1.  Chroma uses the same
 * rule with half the lines.  An IDR restarts at n = 0 (recon at the start).
 *
 * The 0xb unit (reference/raw reader, registers 0xb0014/18 reference and
 * 0xb0030/34 ring start) has no ring end register: the OEM tells it in
 * bits 8..15 of 0xb0000 after how many macroblock rows from the reference
 * position the ring wraps (rows to the ring end minus one; 0xff, the
 * non-shared value, means never).  Without it the rows of the reference
 * that lie past the ring end are read from beyond the ring: large P
 * pictures whenever the reference wraps (4 of every 5.25 pictures at
 * 1080p) and chroma smears.
 *
 * Opt-in: OPENIMP_REF_SHARE=1.  Saves one of the two reference pictures
 * minus 256 lines (1080p: 6.0 -> 3.7 MiB). */
#define T21_REF_RING_EXTRA_LINES 256u
/* The vendor T23 (1.3.0, ring always on up to 1080p) places the chroma
 * ring 256 bytes after the luma ring end (0x60014 = 0x02ff6000, 0x6001c =
 * 0x0326c000, 0x60018 = 0x0326c100, 0x60020 = 0x033a7100 at 1080p). */
#define T21_REF_RING_CHROMA_GAP 0x100u

typedef struct {
    uint32_t base_y, base_c;    /* ring start */
    uint32_t ring_y, ring_c;    /* ring sizes in bytes */
    uint32_t step_y, step_c;    /* bytes per picture step */
    uint32_t stride;            /* luma bytes per line (mb_width * 16) */
} T21RefRing;

typedef struct {
    uint32_t recon_y, recon_c;
    uint32_t ref_y, ref_c;
    uint32_t start_y, start_c;
    uint32_t end_y, end_c;
    uint8_t wrap_rows;          /* 0xb0000 bits 8..15 (OEM BUF_SHARE_CFG) */
    uint8_t wrap_rows_t21;      /* same, with the T21 1.0.33 special case */
} T21RefRingPos;

/* Bytes for one macroblock-aligned mb_width x mb_height picture. */
static inline uint32_t t21_ref_ring_bytes(uint32_t mb_width,
                                          uint32_t mb_height)
{
    uint32_t stride = mb_width * 16u;

    return (mb_height * 16u + T21_REF_RING_EXTRA_LINES) * stride +
           T21_REF_RING_CHROMA_GAP +
           (mb_height * 8u + T21_REF_RING_EXTRA_LINES / 2u) * stride;
}

/* Bytes of the two separate reference pictures (one macroblock: 256 luma
 * + 128 chroma bytes per picture). */
static inline uint32_t t21_ref_pair_bytes(uint32_t mb_width,
                                          uint32_t mb_height)
{
    return 2u * mb_width * mb_height * 384u;
}

/* The ring only pays off when it is smaller than the two references. */
static inline int t21_ref_ring_saves(uint32_t mb_width, uint32_t mb_height)
{
    return t21_ref_ring_bytes(mb_width, mb_height) <
           t21_ref_pair_bytes(mb_width, mb_height);
}

static inline void t21_ref_ring_init(T21RefRing *r, uint32_t base,
                                     uint32_t mb_width, uint32_t mb_height)
{
    uint32_t stride = mb_width * 16u;

    r->ring_y = (mb_height * 16u + T21_REF_RING_EXTRA_LINES) * stride;
    r->ring_c = (mb_height * 8u + T21_REF_RING_EXTRA_LINES / 2u) * stride;
    r->step_y = T21_REF_RING_EXTRA_LINES * stride;
    r->step_c = T21_REF_RING_EXTRA_LINES / 2u * stride;
    r->stride = stride;
    r->base_y = base;
    r->base_c = base + r->ring_y + T21_REF_RING_CHROMA_GAP;
}

/* Address of picture n (n >= 0) in the plane described by base/ring/step. */
static inline uint32_t t21_ref_ring_addr(uint32_t base, uint32_t ring,
                                         uint32_t step, uint64_t n)
{
    uint32_t rem = (uint32_t)((n * step) % ring);

    return rem ? base + ring - rem : base;
}

/* Registers for picture n; n == 0 (IDR) has no previous picture, the OEM
 * then points the reference at the ring start as well. */
static inline void t21_ref_ring_pos(const T21RefRing *r, uint64_t n,
                                    T21RefRingPos *p)
{
    p->recon_y = t21_ref_ring_addr(r->base_y, r->ring_y, r->step_y, n);
    p->recon_c = t21_ref_ring_addr(r->base_c, r->ring_c, r->step_c, n);
    if (n) {
        p->ref_y = t21_ref_ring_addr(r->base_y, r->ring_y, r->step_y, n - 1u);
        p->ref_c = t21_ref_ring_addr(r->base_c, r->ring_c, r->step_c, n - 1u);
    } else {
        p->ref_y = p->recon_y;
        p->ref_c = p->recon_c;
    }
    p->start_y = r->base_y;
    p->start_c = r->base_c;
    p->end_y = r->base_y + r->ring_y;
    p->end_c = r->base_c + r->ring_c;
    /* OEM BUF_SHARE_CFG byte output: ((end - ref) / stride >> 4) - 1
     * (T23 1.3.0 always; T21 1.0.33 gives 255 for the first picture after
     * the IDR, whose reference is at the ring start).  n == 0 (IDR) keeps
     * the formula value, the reference is not read. */
    p->wrap_rows = (uint8_t)(((p->end_y - p->ref_y) / r->stride >> 4) - 1u);
    p->wrap_rows_t21 = n == 1u ? 0xffu : p->wrap_rows;
}

#endif
