/* T40/T41 FrameSource I2D: geometry, ioctl parameter block and the NV12
 * layout the encoder reads (shared by tests/t40 and tests/t41). */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "t40/p1_i2d.h"

static void geometry(void)
{
    struct p1_i2d_geometry g;

    /* flip / mirror / 180: size kept, pitch ALIGN16 */
    assert(!p1_i2d_geometry(640, 360, 0, 0, &g));
    assert(g.out_w == 640 && g.out_h == 360 && !g.transpose);
    assert(g.src_pitch == 640 && g.dst_pitch == 640);
    assert(g.dst_size == 640 * 368 + 640 * 184);
    assert(!p1_i2d_geometry(1920, 1080, 1, 180, &g));
    assert(g.out_w == 1920 && g.out_h == 1080 && !g.transpose);
    /* 90 / 270: swapped, pitch ALIGN16(src_h) (vendor T40 ALIGN16(h)) */
    assert(!p1_i2d_geometry(640, 360, 1, 90, &g));
    assert(g.transpose && g.out_w == 360 && g.out_h == 640);
    assert(g.src_pitch == 640 && g.dst_pitch == 368);
    assert(g.dst_size == 368 * 640 + 368 * 320);
    assert(p1_i2d_geometry(2880, 1620, 1, 270, &g) < 0);   /* 1620 % 8 */
    assert(!p1_i2d_geometry(1920, 1080, 1, 270, &g));
    assert(g.out_w == 1080 && g.out_h == 1920 && g.dst_pitch == 1088);
    /* rotate_enable != 1: the angle is ignored, as by the vendor */
    assert(!p1_i2d_geometry(640, 360, 0, 90, &g) && !g.transpose);
    /* refused */
    assert(p1_i2d_geometry(642, 360, 0, 0, &g) < 0);       /* 8 align */
    assert(p1_i2d_geometry(640, 364, 1, 90, &g) < 0);
    assert(p1_i2d_geometry(1000, 360, 1, 90, &g) < 0);     /* 16 align w */
    assert(!p1_i2d_geometry(1000, 360, 1, 180, &g));
    assert(p1_i2d_geometry(640, 360, 1, 45, &g) < 0);
    assert(p1_i2d_geometry(0, 360, 0, 0, &g) < 0);
    assert(p1_i2d_geometry(640, 360, 0, 0, NULL) < 0);
}

static void active(void)
{
    assert(!p1_i2d_active(0, 1, 0, 0, 0));      /* i2d_enable off */
    assert(!p1_i2d_active(1, 0, 0, 0, 0));      /* nothing to do */
    assert(!p1_i2d_active(1, 0, 0, 1, 0));      /* rotate by 0 */
    assert(!p1_i2d_active(1, 0, 0, 0, 90));     /* rotate not enabled */
    assert(p1_i2d_active(1, 1, 0, 0, 0));
    assert(p1_i2d_active(1, 0, 1, 0, 0));
    assert(p1_i2d_active(1, 1, 1, 0, 0));       /* CreateChn attr: 180 */
    assert(p1_i2d_active(1, 0, 0, 1, 270));
    assert(!p1_i2d_active(2, 1, 0, 0, 0));
}

static void param(void)
{
    struct p1_i2d_geometry g;
    struct p1_i2d_param p;

    assert(P1_I2D_IOC_START == ((1u << 29) | ('D' << 8) | 106u));
    assert(!p1_i2d_geometry(640, 360, 1, 90, &g));
    memset(&p, 0xa5, sizeof(p));
    p1_i2d_fill(&p, 640, 360, 0, 0, 1, 90, &g, 0x1000000, 0x2000000);
    assert(p.src_w == 640 && p.src_h == 360 && p.data_type == 10);
    assert(p.rotate_enable == 1 && p.rotate_angle == 90);
    assert(!p.flip_enable && !p.mirr_enable);
    assert(p.src_addr_y == 0x1000000 && !p.src_addr_uv);
    assert(p.dst_addr_y == 0x2000000 && !p.dst_addr_uv);
    assert(p.src_y_strid == 640 && p.src_uv_strid == 640);
    assert(p.dst_y_strid == 368 && p.dst_uv_strid == 368);
    assert(p.h_align == 16);
    /* the angle word is sent only with rotate_enable (vendor) */
    assert(!p1_i2d_geometry(640, 360, 0, 270, &g));
    p1_i2d_fill(&p, 640, 360, 1, 0, 0, 270, &g, 1, 2);
    assert(!p.rotate_enable && !p.rotate_angle && p.flip_enable == 1);
}

/* NV12 test picture: Y = x + 3y, U/V = block coordinates */
static void pattern(uint8_t *b, uint32_t w, uint32_t h, uint32_t pitch)
{
    uint32_t x, y;
    uint8_t *uv = b + pitch * p1_i2d_align16(h);

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            b[y * pitch + x] = (uint8_t)(x + 3u * y);
    for (y = 0; y < h / 2u; y++)
        for (x = 0; x < w; x += 2u) {
            uv[y * pitch + x] = (uint8_t)(x / 2u);
            uv[y * pitch + x + 1u] = (uint8_t)(100u + y);
        }
}

static uint8_t *run(const uint8_t *src, uint32_t w, uint32_t h, int flip,
                    int mirr, int rot, int angle, struct p1_i2d_geometry *g)
{
    struct p1_i2d_param p;
    uint8_t *dst;

    assert(!p1_i2d_geometry(w, h, rot, angle, g));
    dst = calloc(1, g->dst_size);
    assert(dst);
    p1_i2d_fill(&p, w, h, flip, mirr, rot, angle, g, 0, 0);
    p1_i2d_model(src, dst, &p, g);
    return dst;
}

static void layout(void)
{
    const uint32_t w = 64, h = 48, sp = 64;
    uint8_t *src = calloc(1, sp * 48u * 3u / 2u);
    struct p1_i2d_geometry g, g2;
    uint8_t *r90, *back, *r180, *fm;
    uint32_t x, y;

    assert(src);
    pattern(src, w, h, sp);
    /* 90 clockwise: output (h - 1 - y, x); chroma after ALIGN16(out_h) =
     * src_w lines, where the encoder (avpu_get_nv12_src_uv_offset) reads */
    r90 = run(src, w, h, 0, 0, 1, 90, &g);
    assert(g.out_w == 48 && g.out_h == 64 && g.dst_pitch == 48);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            assert(r90[x * g.dst_pitch + (h - 1u - y)] == src[y * sp + x]);
    {
        const uint8_t *uv = r90 + g.dst_pitch * p1_i2d_align16(g.out_h);

        /* source block (bx, by) -> output block (h/2 - 1 - by, bx) */
        assert(uv[(5u) * g.dst_pitch + 2u * (h / 2u - 1u - 3u)] == 5u);
        assert(uv[(5u) * g.dst_pitch + 2u * (h / 2u - 1u - 3u) + 1u] ==
               100u + 3u);
    }
    /* 270 undoes 90 */
    back = run(r90, g.out_w, g.out_h, 0, 0, 1, 270, &g2);
    assert(g2.out_w == w && g2.out_h == h && g2.dst_pitch == sp);
    assert(!memcmp(back, src, sp * h));                       /* luma */
    assert(!memcmp(back + sp * h, src + sp * h, sp * h / 2u)); /* chroma */
    /* 180 == flip + mirror (the timps CreateChn attribute) */
    r180 = run(src, w, h, 0, 0, 1, 180, &g);
    fm = run(src, w, h, 1, 1, 0, 0, &g2);
    assert(g.dst_size == g2.dst_size);
    assert(!memcmp(r180, fm, g.dst_size));
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            assert(r180[(h - 1u - y) * g.dst_pitch + (w - 1u - x)] ==
                   src[y * sp + x]);
    free(src);
    free(r90);
    free(back);
    free(r180);
    free(fm);
}

int main(void)
{
    geometry();
    active();
    param();
    layout();
    printf("i2d_test: ok\n");
    return 0;
}
