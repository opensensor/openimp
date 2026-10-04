/* T23 IMP_OSD (group OSD) in the T23 SDK 1.3.0 ABI.
 *
 * The OEM T23 libimp draws an OSD group into every frame that passes it on
 * the way from FrameSource to Encoder (osd_update): COVER and PIC regions go
 * through the IPU (jz_ipu_hal, the same jz_ipu_v13 driver and 0x98-byte
 * request as T31), lines, rectangles and mosaics are drawn by the CPU.  This
 * file does the same: PIC/BITMAP/PIC_RMEM pictures are kept as BGRA8888 in
 * rmem (converted once per update) and blended by /dev/ipu, line-type regions
 * and mosaics are written into the NV12 frame by the CPU.  The ISP-drawn
 * region types (OSD_REG_ISP_*) are handed to the ISP tuning layer.
 *
 * OPENIMP_T23_OSD=0 disables all drawing (state is still kept). */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <imp/imp_system.h>

#include "dma_alloc.h"
#include "imp_log_int.h"
#include "t23/openimp_t23_osd.h"
#include "t23/openimp_t23_osd_abi.h"
#include "t23/openimp_t23_osd_pack.h"

#define T23_OSD_GROUPS  9
#define T23_OSD_REGIONS 512
#define T23_OSD_MAX_IPU_ERRORS 10

#define T23_IPU_START            0x2000496aUL   /* _IO('I', 106) */
#define T23_IPU_FMT_BGRA_8888    5u
#define T23_IPU_FMT_NV12         0x18u

struct t23_ipu_osd_ch {
    uint32_t fmt, para, bak_argb, pos_x, pos_y, src_w, src_h, buf_p;
};

struct t23_ipu_param {
    uint32_t cmd, bg_w, bg_h, bg_fmt, bg_buf_p, out_fmt;
    struct t23_ipu_osd_ch ch[4];
};
_Static_assert(sizeof(struct t23_ipu_param) == 0x98,
               "jz_ipu_v13 struct ipu_param (T23 ipu_osd memset size)");

struct t23_osd_bitmap {
    uint8_t *virt;
    uint32_t phys;
    uint32_t size;
};

struct t23_osd_region {
    IMPOSDRgnAttr attr;
    IMPOSDGrpRgnAttr grp[T23_OSD_GROUPS];
    uint32_t registered;            /* bit per group */
    struct t23_osd_bitmap bitmap[2];
    int active;                     /* bitmap[] drawn, -1 none */
    int last;                       /* bitmap[] written last, -1 none */
    uint32_t pic_phys;              /* IPU source of the active picture */
    uint32_t pic_pitch;             /* its pitch in pixels = IPU src_w */
    uint32_t cover_word;            /* A,Y,U,V for COVER */
    uint8_t line_y, line_u, line_v, line_visible;
};

struct t23_osd_group {
    int created;
    int started;
};

static pthread_mutex_t osd_lock = PTHREAD_MUTEX_INITIALIZER;
static struct t23_osd_group osd_groups[T23_OSD_GROUPS];
static struct t23_osd_region *osd_regions[T23_OSD_REGIONS];
static int osd_pool_size;
static int osd_ipu_fd = -1;
static int osd_ipu_errors;
static int osd_ipu_disabled;
static int osd_state;               /* 0 unknown, 1 on, -1 off */

#if defined(PLATFORM_T41)
/* T41 builds this OSD for its IPU path (/dev/ipu, the same jz_ipu_v13 as
 * T23 and T31).  The T41 ISP OSD (OSD_REG_ISP_*) has its own ioctls and is
 * not implemented: those regions are accepted and not drawn. */
static int IMP_OSD_SetRgnAttr_ISP(IMPOSDRgnAttr *attr, int show)
{
    static int reported;

    (void)attr;
    (void)show;
    if (!reported) {
        reported = 1;
        IMP_LOG_INFO("OSD", "T41: ISP OSD regions are not drawn");
    }
    return -1;
}
#else
/* ISP drawing of OSD_REG_ISP_* regions lives in openimp_t23_isp_osd.c */
extern int IMP_OSD_SetRgnAttr_ISP(IMPOSDRgnAttr *attr, int show);
#endif

static int t23_osd_enabled(void)
{
    if (osd_state == 0) {
        const char *v = getenv("OPENIMP_T23_OSD");

        osd_state = v && v[0] == '0' && v[1] == '\0' ? -1 : 1;
        if (osd_state < 0)
            IMP_LOG_INFO("OSD", "T23 OSD drawing disabled by OPENIMP_T23_OSD=0");
    }
    return osd_state > 0;
}

static int valid_group(int group)
{
    return group >= 0 && group < T23_OSD_GROUPS;
}

static struct t23_osd_region *region_of(IMPRgnHandle handle)
{
    if (handle < 0 || handle >= T23_OSD_REGIONS)
        return NULL;
    return osd_regions[handle];
}

static int rect_size(const IMPOSDRgnAttr *attr, uint32_t *w, uint32_t *h)
{
    /* 64-bit: corner coordinates near INT_MIN/INT_MAX must not wrap */
    long long rw = (long long)attr->rect.p1.x - attr->rect.p0.x + 1;
    long long rh = (long long)attr->rect.p1.y - attr->rect.p0.y + 1;

    if (rw <= 0 || rh <= 0 || rw > 4096 || rh > 4096)
        return -1;
    *w = (uint32_t)rw;
    *h = (uint32_t)rh;
    return 0;
}

static void argb_to_yuv(uint32_t argb, uint8_t *y, uint8_t *u, uint8_t *v)
{
    int r = (int)(argb >> 16) & 255, g = (int)(argb >> 8) & 255;
    int b = (int)argb & 255;

    *y = (uint8_t)(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
    *u = (uint8_t)(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
    *v = (uint8_t)(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
}

/* Stock libimp colour word for an IPU mask (Y without the +16 offset). */
static uint32_t cover_word(uint32_t argb)
{
    int r = (int)(argb >> 16) & 255, g = (int)(argb >> 8) & 255;
    int b = (int)argb & 255;
    int y = (66 * r + 129 * g + 25 * b + 128) >> 8;
    int u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
    int v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;

    return (argb & 0xff000000u) | (uint32_t)(y & 255) << 16 |
           (uint32_t)(u & 255) << 8 | (uint32_t)(v & 255);
}

static void free_bitmaps(struct t23_osd_region *r)
{
    for (int i = 0; i < 2; i++) {
        if (r->bitmap[i].phys)
            DMA_FreePhys(r->bitmap[i].phys);
        memset(&r->bitmap[i], 0, sizeof(r->bitmap[i]));
    }
    r->active = r->last = -1;
    r->pic_phys = 0;
}

static int picture_format_ok(int type, int fmt)
{
    if (type == OSD_REG_BITMAP)
        return 1;                   /* 1 bpp, whatever fmt says */
    return fmt == T23_OSD_PIX_BGRA || fmt == T23_OSD_PIX_ARGB ||
           fmt == T23_OSD_PIX_RGBA || fmt == T23_OSD_PIX_ABGR ||
           fmt == T23_OSD_PIX_RGB555LE || fmt == T23_OSD_PIX_BGR555LE;
}

/* Called with osd_lock held after attr changed. */
static void load_region(struct t23_osd_region *r)
{
    const IMPOSDRgnAttr *a = &r->attr;
    const uint8_t *src = NULL;
    uint32_t w, h, size, pitch;
    struct t23_osd_bitmap *b;
    int fmt = a->type == OSD_REG_BITMAP ? T23_OSD_PIX_MONOWHITE : a->fmt;
    int next;

    switch (a->type) {
    case OSD_REG_COVER:
        r->cover_word = cover_word(a->data.coverData.color);
        return;
    case OSD_REG_HORIZONTAL_LINE:
    case OSD_REG_VERTICAL_LINE:
    case OSD_REG_RECT:
    case OSD_REG_FOUR_CORNER_RECT:
    case OSD_REG_SLASH:
        argb_to_yuv(a->data.lineRectData.color, &r->line_y, &r->line_u,
                    &r->line_v);
        r->line_visible = (a->data.lineRectData.color >> 24) != 0u;
        return;
    case OSD_REG_PIC:
    case OSD_REG_PIC_RMEM:
    case OSD_REG_BITMAP:
        break;
    default:
        return;
    }

    /* Until a picture matching the current rect is loaded draw nothing: the
     * old one may be smaller than the new rect. */
    r->active = -1;
    r->pic_phys = 0;
    if (rect_size(a, &w, &h) != 0 || !picture_format_ok(a->type, a->fmt))
        return;
    if (a->type == OSD_REG_PIC_RMEM) {
        /* The picture already is in rmem (IMP_OSD_Alloc returns its
         * physical address; a virtual rmem address is accepted too). */
        uintptr_t p = (uintptr_t)a->data.picData.pData;
        uint32_t phys = DMA_VirtToPhys((const void *)p);

        if (!p)
            return;
        if (phys == (uint32_t)p) {
            src = DMA_PhysToVirt((uint32_t)p);
            if (!src)
                return;
        } else {
            src = (const uint8_t *)p;
        }
        if (fmt == T23_OSD_PIX_BGRA && t23_osd_pic_pitch(w) == w) {
            r->pic_phys = DMA_VirtToPhys(src);
            if (r->pic_phys && r->pic_phys != (uint32_t)(uintptr_t)src) {
                /* make CPU writes of the caller visible to the IPU */
                DMA_RmemFlushCache((void *)(uintptr_t)src, w * h * 4u, 1);
                r->pic_pitch = w;
                r->active = 2;      /* in place, no own bitmap */
            } else {
                r->pic_phys = 0;
            }
            return;
        }
    } else {
        src = a->type == OSD_REG_BITMAP ? a->data.bitmapData
                                        : a->data.picData.pData;
    }
    if (!src)
        return;

    pitch = t23_osd_pic_pitch(w);
    size = pitch * h * 4u;
    next = r->last == 0 ? 1 : 0;
    b = &r->bitmap[next];
    if (b->size < size) {
        IMPDMABufferInfo info;

        if (b->phys)
            DMA_FreePhys(b->phys);
        memset(b, 0, sizeof(*b));
        memset(&info, 0, sizeof(info));
        if (DMA_AllocDescriptor(&info, (int)size, "osd-bitmap") != 0 ||
            !info.virt_addr || !info.phys_addr) {
            IMP_LOG_INFO("OSD", "no rmem for a %ux%u OSD picture", w, h);
            return;
        }
        b->virt = (uint8_t *)(uintptr_t)info.virt_addr;
        b->phys = info.phys_addr;
        b->size = size;
    }
    t23_osd_pack(b->virt, pitch, src, w, h, fmt);
    DMA_RmemFlushCache(b->virt, size, 1 /* write back */);
    r->active = r->last = next;
    r->pic_phys = b->phys;
    r->pic_pitch = pitch;
}

/* ---- CPU drawing into the NV12 frame --------------------------------- */

typedef struct {
    uint8_t *y;
    uint8_t *uv;
    uint32_t w, h;
    uint32_t band_y0, band_y1;      /* rows written, for the cache flush */
} T23Frame;

static void fill_box(T23Frame *f, int x0, int y0, int x1, int y1,
                     uint8_t yy, uint8_t u, uint8_t v)
{
    int x, y;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= (int)f->w) x1 = (int)f->w - 1;
    if (y1 >= (int)f->h) y1 = (int)f->h - 1;
    if (x0 > x1 || y0 > y1)
        return;
    for (y = y0; y <= y1; y++)
        memset(f->y + (uint32_t)y * f->w + (uint32_t)x0, yy,
               (size_t)(x1 - x0 + 1));
    for (y = y0 & ~1; y <= y1; y += 2) {
        uint8_t *p = f->uv + (uint32_t)(y / 2) * f->w;

        for (x = x0 & ~1; x <= x1; x += 2) {
            p[x] = u;
            p[x + 1] = v;
        }
    }
    if ((uint32_t)y0 < f->band_y0)
        f->band_y0 = (uint32_t)y0;
    if ((uint32_t)y1 + 1u > f->band_y1)
        f->band_y1 = (uint32_t)y1 + 1u;
}

static void draw_line_region(T23Frame *f, const struct t23_osd_region *r,
                             const IMPOSDGrpRgnAttr *g)
{
    const IMPOSDRgnAttr *a = &r->attr;
    int lw = (int)a->data.lineRectData.linewidth;
    int len = (int)a->data.lineRectData.linelength;
    int corner = (int)a->data.lineRectData.rectlinelength;
    int x0 = a->rect.p0.x, y0 = a->rect.p0.y;
    int x1 = a->rect.p1.x, y1 = a->rect.p1.y;
    uint8_t Y = r->line_y, U = r->line_u, V = r->line_v;

    (void)g;
    if (!r->line_visible)
        return;
    if (lw <= 0)
        lw = 1;
    switch (a->type) {
    case OSD_REG_HORIZONTAL_LINE:
        x0 = a->line.p0.x;
        y0 = a->line.p0.y;
        if (len <= 0)
            len = a->rect.p1.x - a->rect.p0.x + 1;
        fill_box(f, x0, y0, x0 + len - 1, y0 + lw - 1, Y, U, V);
        break;
    case OSD_REG_VERTICAL_LINE:
        x0 = a->line.p0.x;
        y0 = a->line.p0.y;
        if (len <= 0)
            len = a->rect.p1.y - a->rect.p0.y + 1;
        fill_box(f, x0, y0, x0 + lw - 1, y0 + len - 1, Y, U, V);
        break;
    case OSD_REG_RECT:
        fill_box(f, x0, y0, x1, y0 + lw - 1, Y, U, V);
        fill_box(f, x0, y1 - lw + 1, x1, y1, Y, U, V);
        fill_box(f, x0, y0, x0 + lw - 1, y1, Y, U, V);
        fill_box(f, x1 - lw + 1, y0, x1, y1, Y, U, V);
        break;
    case OSD_REG_FOUR_CORNER_RECT:
        if (corner <= 0)
            corner = (x1 - x0 + 1) / 4;
        fill_box(f, x0, y0, x0 + corner - 1, y0 + lw - 1, Y, U, V);
        fill_box(f, x0, y0, x0 + lw - 1, y0 + corner - 1, Y, U, V);
        fill_box(f, x1 - corner + 1, y0, x1, y0 + lw - 1, Y, U, V);
        fill_box(f, x1 - lw + 1, y0, x1, y0 + corner - 1, Y, U, V);
        fill_box(f, x0, y1 - lw + 1, x0 + corner - 1, y1, Y, U, V);
        fill_box(f, x0, y1 - corner + 1, x0 + lw - 1, y1, Y, U, V);
        fill_box(f, x1 - corner + 1, y1 - lw + 1, x1, y1, Y, U, V);
        fill_box(f, x1 - lw + 1, y1 - corner + 1, x1, y1, Y, U, V);
        break;
    case OSD_REG_SLASH: {
        /* p0 to p1, Bresenham with a square pen */
        int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
        int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
        int err = dx + dy, steps = 0;

        for (;;) {
            fill_box(f, x0, y0, x0 + lw - 1, y0 + lw - 1, Y, U, V);
            if ((x0 == x1 && y0 == y1) || ++steps > 8192)
                break;
            if (2 * err >= dy) { err += dy; x0 += sx; }
            if (2 * err <= dx) { err += dx; y0 += sy; }
        }
        break;
    }
    default:
        break;
    }
}

/* Vendor IMP_OSD_SetMosaic: luma only, every pixel of a mosaic cell takes
 * the value of the cell's top-left pixel. */
static int mosaic(uint8_t *frame, const IMPOSDMosaicAttr *m)
{
    int i, j, cell;

    if (((m->x | m->y | m->mosaic_width | m->mosaic_height | m->frame_width |
          m->frame_height) & 1) ||
        m->frame_width < m->x + m->mosaic_width ||
        m->frame_height < m->y + m->mosaic_height || m->mosaic_min_size <= 0)
        return T23_OSD_ERR_NOT_SUPPORT;
    cell = m->mosaic_min_size;
    for (i = 0; i < m->mosaic_width; i++) {
        int sx = i - i % cell;

        for (j = 0; j < m->mosaic_height; j++) {
            int sy = j - j % cell;

            frame[(m->y + j) * m->frame_width + m->x + i] =
                frame[(m->y + sy) * m->frame_width + m->x + sx];
        }
    }
    return 0;
}

int IMP_OSD_SetMosaic(unsigned char *frame_virAddr, IMPOSDMosaicAttr *attr)
{
    if (!frame_virAddr || !attr)
        return T23_OSD_ERR_NULL_PTR;
    return mosaic(frame_virAddr, attr);
}

/* ---- per-frame application -------------------------------------------- */

static int ipu_open_locked(void)
{
    if (osd_ipu_fd >= 0)
        return 0;
    if (osd_ipu_disabled)
        return -1;
    osd_ipu_fd = open("/dev/ipu", O_RDWR | O_CLOEXEC);
    if (osd_ipu_fd < 0) {
        osd_ipu_disabled = 1;
        IMP_LOG_INFO("OSD", "T23 OSD: cannot open /dev/ipu, pictures and "
                     "covers are not drawn");
        return -1;
    }
    return 0;
}

static int region_draws_ipu(const struct t23_osd_region *r)
{
    if (r->attr.type == OSD_REG_COVER)
        return 1;
    return (r->attr.type == OSD_REG_PIC || r->attr.type == OSD_REG_PIC_RMEM ||
            r->attr.type == OSD_REG_BITMAP) && r->active >= 0 && r->pic_phys;
}

void openimp_t23_osd_apply(int group, void *frame)
{
    const uint8_t *fi = frame;
    uint32_t width, height, phys, virt, bg_h;
    int order[T23_OSD_REGIONS];
    uint32_t band_y0[T23_OSD_REGIONS], band_y1[T23_OSD_REGIONS];
    int count = 0, i;
    T23Frame f;

    if (!frame || !valid_group(group) || !t23_osd_enabled())
        return;
    memcpy(&width, fi + 0x08, 4);
    memcpy(&height, fi + 0x0c, 4);
    memcpy(&phys, fi + 0x18, 4);
    memcpy(&virt, fi + 0x1c, 4);
    if (!width || !height || (width & 15u))
        return;
    bg_h = (height + 15u) & ~15u;

    pthread_mutex_lock(&osd_lock);
    if (!osd_groups[group].created || !osd_groups[group].started) {
        pthread_mutex_unlock(&osd_lock);
        return;
    }

    /* 1. IPU: covers and pictures, sorted by layer, four per request */
    for (i = 0; i < T23_OSD_REGIONS; i++) {
        const struct t23_osd_region *r = osd_regions[i];
        uint32_t w, h;
        int j;

        if (!r || !(r->registered & (1u << group)) || !r->grp[group].show ||
            !region_draws_ipu(r) || rect_size(&r->attr, &w, &h) != 0 ||
            r->attr.rect.p0.x < 0 || r->attr.rect.p0.y < 0 ||
            (uint32_t)r->attr.rect.p0.x + w > width ||
            (uint32_t)r->attr.rect.p0.y + h > height)
            continue;
        for (j = count; j > 0 &&
             osd_regions[order[j - 1]]->grp[group].layer >
                 r->grp[group].layer; j--)
            order[j] = order[j - 1];
        order[j] = i;
        count++;
    }
    if (count > 0 && (!phys || ipu_open_locked() != 0))
        count = 0;
    for (i = 0; i < count; i += 4) {
        struct t23_ipu_param p;
        int n = count - i < 4 ? count - i : 4;

        memset(&p, 0, sizeof(p));
        p.bg_w = width;
        p.bg_h = bg_h;
        p.bg_fmt = T23_IPU_FMT_NV12;
        p.out_fmt = T23_IPU_FMT_NV12;
        p.bg_buf_p = phys;
        for (int k = 0; k < n; k++) {
            const struct t23_osd_region *r = osd_regions[order[i + k]];
            const IMPOSDGrpRgnAttr *g = &r->grp[group];
            struct t23_ipu_osd_ch *c = &p.ch[k];
            int global = g->gAlphaEn > 0;
            uint32_t alpha = global ? (uint32_t)g->fgAlhpa & 0xffu : 0xffu;
            uint32_t w = 0, h = 0;

            rect_size(&r->attr, &w, &h);
            c->fmt = T23_IPU_FMT_BGRA_8888;
            c->pos_x = (uint32_t)r->attr.rect.p0.x;
            c->pos_y = (uint32_t)r->attr.rect.p0.y;
            c->src_w = w;
            c->src_h = h;
            if (r->attr.type == OSD_REG_COVER) {
                /* stock _ipu_set_osdx_mask */
                uint32_t word = r->cover_word, a = word >> 24;

                if (global) {
                    a = (((uint32_t)g->fgAlhpa & 0xffu) + 1u) * a >> 8;
                    word = (word & 0x00ffffffu) | (a > 255u ? 255u : a) << 24;
                }
                c->para = ((alpha << 3 | (global ? 0x02034005u : 0x02034001u)) &
                           0xfffc3fffu) | 0x00800000u;
                c->bak_argb = word;
            } else {
                /* stock _ipu_set_osdx_para, BGRA */
                c->para = alpha << 3 |
                          ((global ? 0x020347FDu : 0x020347F9u) & 0xfffff807u);
                c->buf_p = r->pic_phys;
                /* the IPU reads lines at the 16-pixel pitch; a padded
                 * picture at the right frame edge moves left so the
                 * transparent padding stays inside the frame */
                c->src_w = r->pic_pitch;
                if (c->pos_x + c->src_w > width)
                    c->pos_x = width - c->src_w;
            }
            p.cmd |= 1u << k;
        }
        if (ioctl(osd_ipu_fd, T23_IPU_START, &p) < 0) {
            if (++osd_ipu_errors >= T23_OSD_MAX_IPU_ERRORS) {
                osd_ipu_disabled = 1;
                close(osd_ipu_fd);
                osd_ipu_fd = -1;
                IMP_LOG_INFO("OSD", "T23 OSD: repeated IPU errors, pictures "
                             "and covers are no longer drawn");
                count = i;
                break;
            }
        } else {
            osd_ipu_errors = 0;
        }
    }
    for (i = 0; i < count; i++) {
        band_y0[i] = (uint32_t)osd_regions[order[i]]->attr.rect.p0.y;
        band_y1[i] = (uint32_t)osd_regions[order[i]]->attr.rect.p1.y + 1u;
    }
    /* The IPU wrote the frame behind the CPU cache; drop stale lines of the
     * blended bands before anything reads or draws them with the CPU. */
    if (virt) {
        for (i = 0; i < count; i++) {
            DMA_RmemFlushCache((void *)(uintptr_t)(virt + band_y0[i] * width),
                               (band_y1[i] - band_y0[i]) * width, 2);
            DMA_RmemFlushCache((void *)(uintptr_t)(virt + width * bg_h +
                                                   (band_y0[i] / 2u) * width),
                               ((band_y1[i] + 1u) / 2u - band_y0[i] / 2u) *
                                   width, 2);
        }
    }

    /* 2. CPU: mosaics and line-type regions */
    memset(&f, 0, sizeof(f));
    f.y = (uint8_t *)(uintptr_t)virt;
    f.uv = f.y ? f.y + width * bg_h : NULL;
    f.w = width;
    f.h = height;
    f.band_y0 = height;
    if (f.y) {
        for (i = 0; i < T23_OSD_REGIONS; i++) {
            const struct t23_osd_region *r = osd_regions[i];
            IMPOSDMosaicAttr m;

            if (!r || !(r->registered & (1u << group)) ||
                !r->grp[group].show)
                continue;
            switch (r->attr.type) {
            case OSD_REG_MOSAIC:
                m = r->attr.mosaicAttr;
                m.frame_width = (int)width;
                m.frame_height = (int)height;
                if (mosaic(f.y, &m) == 0) {
                    if ((uint32_t)m.y < f.band_y0)
                        f.band_y0 = (uint32_t)m.y;
                    if ((uint32_t)(m.y + m.mosaic_height) > f.band_y1)
                        f.band_y1 = (uint32_t)(m.y + m.mosaic_height);
                }
                break;
            case OSD_REG_HORIZONTAL_LINE:
            case OSD_REG_VERTICAL_LINE:
            case OSD_REG_RECT:
            case OSD_REG_FOUR_CORNER_RECT:
            case OSD_REG_SLASH:
                draw_line_region(&f, r, &r->grp[group]);
                break;
            default:
                break;
            }
        }
    }
    pthread_mutex_unlock(&osd_lock);

    /* Write CPU-drawn lines back so a later DMA reuse of the buffer cannot
     * be overwritten by their eviction (and DMA readers see them). */
    if (f.y && f.band_y1 > f.band_y0) {
        DMA_RmemFlushCache(f.y + f.band_y0 * width,
                           (f.band_y1 - f.band_y0) * width, 1);
        DMA_RmemFlushCache(f.uv + (f.band_y0 / 2u) * width,
                           ((f.band_y1 + 1u) / 2u - f.band_y0 / 2u) * width, 1);
    }
}

/* ---- public API ------------------------------------------------------- */

int IMP_OSD_SetPoolSize(int size)
{
    /* Pictures get their own rmem buffers here, so the pool size is only
     * recorded (the OEM pre-allocates a pool of this size). */
    if (size <= 0)
        return -1;
    pthread_mutex_lock(&osd_lock);
    osd_pool_size = size;
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_CreateGroup(int group)
{
    if (!valid_group(group))
        return T23_OSD_ERR_CHNID;
    pthread_mutex_lock(&osd_lock);
    osd_groups[group].created = 1;
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_DestroyGroup(int group)
{
    int i;

    if (!valid_group(group))
        return T23_OSD_ERR_CHNID;
    pthread_mutex_lock(&osd_lock);
    if (!osd_groups[group].created) {
        pthread_mutex_unlock(&osd_lock);
        return T23_OSD_ERR_UNEXIST;
    }
    /* like the OEM, regions still registered are dropped from the group */
    for (i = 0; i < T23_OSD_REGIONS; i++) {
        if (osd_regions[i])
            osd_regions[i]->registered &= ~(1u << group);
    }
    memset(&osd_groups[group], 0, sizeof(osd_groups[group]));
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_AttachToGroup(IMPCell *from, IMPCell *to)
{
    IMPCell source;

    /* OEM system_attach(): splice the OSD cell into the existing bind that
     * feeds `to`: src -> to becomes src -> from -> to, rolled back on error. */
    if (!from || !to || IMP_System_GetBindbyDest(to, &source) != 0 ||
        IMP_System_UnBind(&source, to) != 0)
        return T23_OSD_ERR_PERM;
    if (IMP_System_Bind(&source, from) != 0) {
        (void)IMP_System_Bind(&source, to);
        return T23_OSD_ERR_PERM;
    }
    if (IMP_System_Bind(from, to) != 0) {
        (void)IMP_System_UnBind(&source, from);
        (void)IMP_System_Bind(&source, to);
        return T23_OSD_ERR_PERM;
    }
    return 0;
}

IMPRgnHandle IMP_OSD_CreateRgn(IMPOSDRgnAttr *attr)
{
    struct t23_osd_region *r;
    int handle;

    pthread_mutex_lock(&osd_lock);
    for (handle = 0; handle < T23_OSD_REGIONS; handle++) {
        if (!osd_regions[handle])
            break;
    }
    if (handle == T23_OSD_REGIONS) {
        pthread_mutex_unlock(&osd_lock);
        return T23_OSD_ERR_NOMEM;
    }
    r = calloc(1, sizeof(*r));
    if (!r) {
        pthread_mutex_unlock(&osd_lock);
        return T23_OSD_ERR_NOMEM;
    }
    r->active = r->last = -1;
    if (attr) {
        r->attr = *attr;
        load_region(r);
    }
    osd_regions[handle] = r;
    pthread_mutex_unlock(&osd_lock);
    return handle;
}

void IMP_OSD_DestroyRgn(IMPRgnHandle handle)
{
    struct t23_osd_region *r;

    pthread_mutex_lock(&osd_lock);
    r = region_of(handle);
    if (r) {
        osd_regions[handle] = NULL;
        free_bitmaps(r);
        free(r);
    }
    pthread_mutex_unlock(&osd_lock);
}

static int check_region_group(IMPRgnHandle handle, int group,
                              struct t23_osd_region **out, int need_reg)
{
    struct t23_osd_region *r;

    if (!valid_group(group))
        return T23_OSD_ERR_CHNID;
    if (handle < 0 || handle >= T23_OSD_REGIONS)
        return T23_OSD_ERR_PARAM;
    r = osd_regions[handle];
    if (!r || !osd_groups[group].created ||
        (need_reg && !(r->registered & (1u << group))))
        return T23_OSD_ERR_NOT_CONFIG;
    *out = r;
    return 0;
}

int IMP_OSD_RegisterRgn(IMPRgnHandle handle, int group,
                        IMPOSDGrpRgnAttr *attr)
{
    struct t23_osd_region *r;
    int ret;

    pthread_mutex_lock(&osd_lock);
    ret = check_region_group(handle, group, &r, 0);
    if (ret == 0) {
        r->registered |= 1u << group;
        if (attr)
            r->grp[group] = *attr;
        else
            memset(&r->grp[group], 0, sizeof(r->grp[group]));
    }
    pthread_mutex_unlock(&osd_lock);
    if (ret == 0 && attr && attr->show &&
        (r->attr.type == OSD_REG_ISP_LINE_RECT ||
         r->attr.type == OSD_REG_ISP_COVER))
        (void)IMP_OSD_SetRgnAttr_ISP(&r->attr, 1);
    return ret;
}

int IMP_OSD_UnRegisterRgn(IMPRgnHandle handle, int group)
{
    struct t23_osd_region *r;
    int ret;

    pthread_mutex_lock(&osd_lock);
    ret = check_region_group(handle, group, &r, 1);
    if (ret == 0) {
        r->registered &= ~(1u << group);
        memset(&r->grp[group], 0, sizeof(r->grp[group]));
    }
    pthread_mutex_unlock(&osd_lock);
    return ret;
}

int IMP_OSD_SetRgnAttr(IMPRgnHandle handle, IMPOSDRgnAttr *attr)
{
    struct t23_osd_region *r;
    int isp_show = 0;
    IMPOSDRgnAttr isp_attr;

    if (handle < 0 || handle >= T23_OSD_REGIONS || !attr)
        return T23_OSD_ERR_PARAM;
    pthread_mutex_lock(&osd_lock);
    r = osd_regions[handle];
    if (!r) {
        pthread_mutex_unlock(&osd_lock);
        return T23_OSD_ERR_UNEXIST;
    }
    r->attr = *attr;
    load_region(r);
    if (attr->type == OSD_REG_ISP_LINE_RECT ||
        attr->type == OSD_REG_ISP_COVER) {
        for (int g = 0; g < T23_OSD_GROUPS; g++)
            if ((r->registered & (1u << g)) && r->grp[g].show)
                isp_show = 1;
        isp_attr = *attr;
    }
    pthread_mutex_unlock(&osd_lock);
    if (isp_show)
        (void)IMP_OSD_SetRgnAttr_ISP(&isp_attr, 1);
    return 0;
}

int IMP_OSD_SetRgnAttrWithTimestamp(IMPRgnHandle handle,
                                    IMPOSDRgnAttr *attr,
                                    IMPOSDRgnTimestamp *timestamp)
{
    /* The frame timestamp window is not tracked: the attribute applies from
     * the next frame on, as with IMP_OSD_SetRgnAttr. */
    if (!timestamp)
        return T23_OSD_ERR_NOT_SUPPORT;
    return IMP_OSD_SetRgnAttr(handle, attr);
}

int IMP_OSD_GetRgnAttr(IMPRgnHandle handle, IMPOSDRgnAttr *attr)
{
    struct t23_osd_region *r;

    if (handle < 0 || handle >= T23_OSD_REGIONS || !attr)
        return T23_OSD_ERR_PARAM;
    pthread_mutex_lock(&osd_lock);
    r = osd_regions[handle];
    if (!r) {
        pthread_mutex_unlock(&osd_lock);
        return T23_OSD_ERR_UNEXIST;
    }
    *attr = r->attr;
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_UpdateRgnAttrData(IMPRgnHandle handle, IMPOSDRgnAttrData *data)
{
    struct t23_osd_region *r;

    if (handle < 0 || handle >= T23_OSD_REGIONS || !data)
        return T23_OSD_ERR_PARAM;
    pthread_mutex_lock(&osd_lock);
    r = osd_regions[handle];
    if (!r) {
        pthread_mutex_unlock(&osd_lock);
        return T23_OSD_ERR_UNEXIST;
    }
    r->attr.data = *data;
    load_region(r);
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_SetGrpRgnAttr(IMPRgnHandle handle, int group,
                          IMPOSDGrpRgnAttr *attr)
{
    struct t23_osd_region *r;
    int ret;

    if (!attr)
        return T23_OSD_ERR_NULL_PTR;
    pthread_mutex_lock(&osd_lock);
    ret = check_region_group(handle, group, &r, 1);
    if (ret == 0)
        r->grp[group] = *attr;
    pthread_mutex_unlock(&osd_lock);
    return ret;
}

int IMP_OSD_GetGrpRgnAttr(IMPRgnHandle handle, int group,
                          IMPOSDGrpRgnAttr *attr)
{
    struct t23_osd_region *r;
    int ret;

    if (!attr)
        return T23_OSD_ERR_NULL_PTR;
    pthread_mutex_lock(&osd_lock);
    ret = check_region_group(handle, group, &r, 1);
    if (ret == 0)
        *attr = r->grp[group];
    pthread_mutex_unlock(&osd_lock);
    return ret;
}

int IMP_OSD_ShowRgn(IMPRgnHandle handle, int group, int show)
{
    struct t23_osd_region *r;
    IMPOSDRgnAttr isp_attr;
    int isp = 0;
    int ret;

    pthread_mutex_lock(&osd_lock);
    ret = check_region_group(handle, group, &r, 1);
    if (ret == 0) {
        r->grp[group].show = !!show;
        if (r->attr.type == OSD_REG_ISP_LINE_RECT ||
            r->attr.type == OSD_REG_ISP_COVER) {
            isp = 1;
            isp_attr = r->attr;
        }
    }
    pthread_mutex_unlock(&osd_lock);
    if (isp)
        (void)IMP_OSD_SetRgnAttr_ISP(&isp_attr, !!show);
    return ret;
}

int IMP_OSD_Start(int group)
{
    if (!valid_group(group))
        return T23_OSD_ERR_CHNID;
    pthread_mutex_lock(&osd_lock);
    osd_groups[group].started = 1;
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_Stop(int group)
{
    if (!valid_group(group))
        return T23_OSD_ERR_CHNID;
    pthread_mutex_lock(&osd_lock);
    osd_groups[group].started = 0;
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_RgnCreate_Query(IMPRgnHandle handle, IMPOSDRgnCreateStat *stat)
{
    if (!stat)
        return T23_OSD_ERR_NULL_PTR;
    if (handle < 0 || handle >= T23_OSD_REGIONS)
        return T23_OSD_ERR_PARAM;
    pthread_mutex_lock(&osd_lock);
    stat->status = osd_regions[handle] ? 1 : 0;
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_RgnRegister_Query(IMPRgnHandle handle, int group,
                              IMPOSDRgnRegisterStat *stat)
{
    if (!stat)
        return T23_OSD_ERR_NULL_PTR;
    if (handle < 0 || handle >= T23_OSD_REGIONS)
        return T23_OSD_ERR_PARAM;
    if (!valid_group(group))
        return T23_OSD_ERR_CHNID;
    pthread_mutex_lock(&osd_lock);
    stat->status = osd_regions[handle] &&
                   (osd_regions[handle]->registered & (1u << group)) ? 1 : 0;
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

/* Undocumented OEM helpers: copy a picture into rmem.  IMP_OSD_Alloc stores
 * the physical address (usable as OSD_REG_PIC_RMEM data), IMP_ISPOSD_Alloc
 * returns the virtual address of an uninitialised buffer (0 on failure). */
int IMP_OSD_Alloc(uint32_t *phys_out, const void *data, int size)
{
    IMPDMABufferInfo info;

    if (!phys_out || size <= 0)
        return -1;
    memset(&info, 0, sizeof(info));
    if (DMA_AllocDescriptor(&info, size, "osd") != 0 || !info.virt_addr)
        return -1;
    if (data)
        memcpy((void *)(uintptr_t)info.virt_addr, data, (size_t)size);
    DMA_RmemFlushCache((void *)(uintptr_t)info.virt_addr, (uint32_t)size, 1);
    *phys_out = info.phys_addr;
    return 0;
}

void *IMP_ISPOSD_Alloc(int size)
{
    IMPDMABufferInfo info;

    if (size <= 0)
        return NULL;
    memset(&info, 0, sizeof(info));
    if (DMA_AllocDescriptor(&info, size, "isposd") != 0)
        return NULL;
    return (void *)(uintptr_t)info.virt_addr;
}
