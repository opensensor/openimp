/*
 * T40/T41 FrameSource I2D (hardware rotate / flip / mirror).
 *
 * The I2D is a 2D DMA block driven by the built-in kernel driver
 * drivers/video/ingenic_i2d (/dev/i2d).  IOCTL_I2D_START takes one
 * struct p1_i2d_param, runs the block and waits for its frame-done IRQ.
 * Vendor libimp (T41 1.2.6 0x72210, T40 1.3.1 i2d_resourcedeal) runs it
 * once per captured NV12 frame when IMPFSI2DAttr.i2d_enable is set and
 * hands the I2D output to the consumers instead of the capture buffer.
 * picWidth/picHeight stay the capture (pre-rotation) size; a 90/270
 * channel delivers picHeight x picWidth frames.
 *
 * Pure helpers so tests/t40 and tests/t41 can check the layout.
 */
#ifndef OPENIMP_T40_P1_I2D_H
#define OPENIMP_T40_P1_I2D_H

#include <stdint.h>

/* _IO('D', 106) with the MIPS _IOC_NONE direction bit (1 << 29). */
#define P1_I2D_IOC_START 0x2000446aU
#define P1_I2D_PIX_NV12 10U      /* kernel PIX_FMT_NV12 (= IMP value) */
#define P1_I2D_H_ALIGN 16U

/* ingenic_i2d.h struct i2d_param (T40 libimp writes the first 15 words;
 * the T41 driver also reads h_align). */
struct p1_i2d_param {
    uint32_t src_w;
    uint32_t src_h;
    uint32_t data_type;
    uint32_t rotate_enable;
    uint32_t rotate_angle;
    uint32_t flip_enable;
    uint32_t mirr_enable;
    uint32_t src_addr_y;
    uint32_t src_addr_uv;        /* ignored: the driver derives it */
    uint32_t dst_addr_y;
    uint32_t dst_addr_uv;        /* ignored: the driver derives it */
    uint32_t src_y_strid;
    uint32_t src_uv_strid;
    uint32_t dst_y_strid;
    uint32_t dst_uv_strid;
    uint32_t h_align;
};

_Static_assert(sizeof(struct p1_i2d_param) == 64, "i2d_param ABI");

struct p1_i2d_geometry {
    uint32_t out_w, out_h;       /* frame size handed to consumers */
    uint32_t src_pitch;          /* capture buffer pitch */
    uint32_t dst_pitch;          /* I2D output pitch (encoder source pitch) */
    uint32_t dst_size;           /* bytes of one I2D output buffer */
    uint32_t transpose;          /* 90 / 270 */
};

static inline uint32_t p1_i2d_align16(uint32_t v)
{
    return (v + 15u) & ~15u;
}

/* The words are ints set by the application: only 1 enables (vendor
 * compares i2d_enable / rotate_enable with 1). */
static inline int p1_i2d_rotating(int rotate_enable, int angle)
{
    return rotate_enable == 1 && angle != 0;
}

/* An I2D pass changes the picture: enabled and at least one operation. */
static inline int p1_i2d_active(int i2d_enable, int flip, int mirr,
                                int rotate_enable, int angle)
{
    return i2d_enable == 1 &&
           (flip == 1 || mirr == 1 || p1_i2d_rotating(rotate_enable, angle));
}

/*
 * Output geometry of a w x h NV12 capture frame.  Returns 0, or -1 when
 * the driver or the encoder cannot take it: angle not 0/90/180/270,
 * w/h not 8 aligned (vendor: "for encode's requirement I2D need frame w/h
 * 8 align"), or a 90/270 width that is not 16 aligned (the driver puts
 * the rotated chroma right after src_w luma lines, the encoder reads it
 * after ALIGN16(src_w) lines).
 */
static inline int p1_i2d_geometry(uint32_t w, uint32_t h, int rotate_enable,
                                  int angle, struct p1_i2d_geometry *g)
{
    uint32_t lines;

    if (!g || !w || !h || w > 8192u || h > 8192u || (w | h) & 7u)
        return -1;
    if (rotate_enable == 1 && angle != 0 && angle != 90 && angle != 180 &&
        angle != 270)
        return -1;
    g->transpose = rotate_enable == 1 && (angle == 90 || angle == 270);
    if (g->transpose && (w & 15u))
        return -1;
    g->out_w = g->transpose ? h : w;
    g->out_h = g->transpose ? w : h;
    g->src_pitch = p1_i2d_align16(w);
    g->dst_pitch = p1_i2d_align16(g->out_w);
    lines = p1_i2d_align16(g->out_h);
    g->dst_size = g->dst_pitch * lines + g->dst_pitch * (lines / 2u);
    return 0;
}

static inline void p1_i2d_fill(struct p1_i2d_param *p, uint32_t w, uint32_t h,
                               int flip, int mirr, int rotate_enable,
                               int angle, const struct p1_i2d_geometry *g,
                               uint32_t src_phys, uint32_t dst_phys)
{
    p->src_w = w;
    p->src_h = h;
    p->data_type = P1_I2D_PIX_NV12;
    p->rotate_enable = rotate_enable == 1;
    p->rotate_angle = rotate_enable == 1 ? (uint32_t)angle : 0u;
    p->flip_enable = flip == 1;
    p->mirr_enable = mirr == 1;
    p->src_addr_y = src_phys;
    p->src_addr_uv = 0;
    p->dst_addr_y = dst_phys;
    p->dst_addr_uv = 0;
    p->src_y_strid = g->src_pitch;
    p->src_uv_strid = g->src_pitch;
    p->dst_y_strid = g->dst_pitch;
    p->dst_uv_strid = g->dst_pitch;
    p->h_align = P1_I2D_H_ALIGN;
}

/*
 * Software model of the I2D on an NV12 picture (driver register
 * semantics: rotation clockwise by angle, then mirror = left/right,
 * flip = top/bottom).  Used by the host test to check that the layout
 * handed to the encoder matches what the block writes; the library never
 * runs it.
 */
static inline void p1_i2d_model(const uint8_t *src, uint8_t *dst,
                                const struct p1_i2d_param *p,
                                const struct p1_i2d_geometry *g)
{
    uint32_t w = p->src_w, h = p->src_h, x, y;
    uint32_t sp = p->src_y_strid, dp = p->dst_y_strid;
    const uint8_t *suv = src + sp * p1_i2d_align16(h);
    uint8_t *duv = dst + dp * (g->transpose ? w : p1_i2d_align16(h));
    uint32_t angle = p->rotate_enable ? p->rotate_angle : 0u;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            uint32_t ox, oy;

            switch (angle) {
            case 90:  ox = h - 1u - y; oy = x; break;
            case 180: ox = w - 1u - x; oy = h - 1u - y; break;
            case 270: ox = y; oy = w - 1u - x; break;
            default:  ox = x; oy = y; break;
            }
            if (p->mirr_enable)
                ox = g->out_w - 1u - ox;
            if (p->flip_enable)
                oy = g->out_h - 1u - oy;
            dst[oy * dp + ox] = src[y * sp + x];
            if (!(x & 1u) && !(y & 1u)) {
                /* chroma pair of the 2x2 block lands at the block corner */
                uint32_t cx = ox & ~1u, cy = oy & ~1u;

                duv[(cy / 2u) * dp + cx] = suv[(y / 2u) * sp + x];
                duv[(cy / 2u) * dp + cx + 1u] = suv[(y / 2u) * sp + x + 1u];
            }
        }
    }
}

#endif
