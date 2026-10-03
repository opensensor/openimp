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
 * Opt-in: OPENIMP_REF_SHARE=1.  Saves one of the two reference pictures
 * minus 256 lines (1080p: 6.0 -> 3.7 MiB). */
#define T21_REF_RING_EXTRA_LINES 256u

typedef struct {
    uint32_t base_y, base_c;    /* ring start */
    uint32_t ring_y, ring_c;    /* ring sizes in bytes */
    uint32_t step_y, step_c;    /* bytes per picture step */
} T21RefRing;

typedef struct {
    uint32_t recon_y, recon_c;
    uint32_t ref_y, ref_c;
    uint32_t start_y, start_c;
    uint32_t end_y, end_c;
} T21RefRingPos;

/* Bytes for one macroblock-aligned mb_width x mb_height picture. */
static inline uint32_t t21_ref_ring_bytes(uint32_t mb_width,
                                          uint32_t mb_height)
{
    uint32_t stride = mb_width * 16u;

    return (mb_height * 16u + T21_REF_RING_EXTRA_LINES) * stride +
           (mb_height * 8u + T21_REF_RING_EXTRA_LINES / 2u) * stride;
}

static inline void t21_ref_ring_init(T21RefRing *r, uint32_t base,
                                     uint32_t mb_width, uint32_t mb_height)
{
    uint32_t stride = mb_width * 16u;

    r->ring_y = (mb_height * 16u + T21_REF_RING_EXTRA_LINES) * stride;
    r->ring_c = (mb_height * 8u + T21_REF_RING_EXTRA_LINES / 2u) * stride;
    r->step_y = T21_REF_RING_EXTRA_LINES * stride;
    r->step_c = T21_REF_RING_EXTRA_LINES / 2u * stride;
    r->base_y = base;
    r->base_c = base + r->ring_y;
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
}

#endif
