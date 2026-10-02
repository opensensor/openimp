/* T31 service APIs required by Raptor but not provided by the stock-driver
 * ISP/FrameSource seam.  Keep these implementations T31-only: the T40 build
 * has its own recovered service layer and older Thingino kernels retain their
 * existing vendor libraries. */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "dma_alloc.h"
#include "imp_log_int.h"
#if !defined(PLATFORM_T23)
/* T23 has its own OSD ABI and implementation (src/t23/openimp_t23_osd.c) */
#include "openimp_t31_osd.h"
#endif

#if !defined(PLATFORM_T23)
#include "openimp_t31_osd_abi.h"
#include "openimp_t31_osd_draw.h"
#endif
#include "imp/imp_system.h"

#define T31_OSD_GROUPS  16
#define T31_OSD_REGIONS 64

/*
 * T31's public H.264 stream is a CPU-owned Annex-B snapshot.  The AVPU writes
 * entropy data into rmem, but codec-t40.c must prepend headers and apply
 * emulation-prevention into stream_public_copy before GetStream returns it.
 * Consumers therefore cannot reconstruct the returned bytes from /dev/rmem.
 */
int OpenIMP_Encoder_StreamIsRmem(int channel)
{
    (void)channel;
    return 0;
}

#if !defined(PLATFORM_T23)
struct t31_osd_group {
    int created;
    int started;
};

struct t31_osd_bitmap {
    uint8_t *virt;
    uint32_t phys;
    uint32_t size;
};

struct t31_osd_region {
    int created;
    int group;
    IMPOSDRgnAttr attr;
    IMPOSDGrpRgnAttr group_attr;
    /* PIC bitmaps live in rmem, written back once per update: the IPU reads
     * them in place, so a frame costs no copy and no cache flush. Updates
     * go to the inactive buffer and swap under osd_lock. */
    struct t31_osd_bitmap bitmap[2];
    int active;                 /* bitmap[] drawn, -1 = none */
    int last;                   /* bitmap[] written last, -1 = none */
    uint32_t cover_word;        /* A,Y,U,V for COVER/LINE/RECT regions */
    /* BITMAP: own copy of the 1-byte mask, as stock CreateRgn/SetRgnAttr/
     * UpdateRgnAttrData copy it (the caller may free its buffer after the
     * call). Drawn on the CPU under osd_lock. */
    uint8_t *mask;
    uint32_t mask_size;
    int mask_ok;
};

static pthread_mutex_t osd_lock = PTHREAD_MUTEX_INITIALIZER;
static struct t31_osd_group osd_groups[T31_OSD_GROUPS];
static struct t31_osd_region osd_regions[T31_OSD_REGIONS];
static int osd_pool_size;

#endif /* !PLATFORM_T23 */

#if !defined(PLATFORM_T23)
/* OSD helpers; T23 has its own OSD (src/t23/openimp_t23_osd.c). */
static int t31_fail(int error)
{
    errno = error;
    return -1;
}

static int valid_osd_group(int group)
{
    return group >= 0 && group < T31_OSD_GROUPS;
}

static int valid_osd_region(IMPRgnHandle handle)
{
    return handle >= 0 && handle < T31_OSD_REGIONS;
}

/* ---- IPU backend (default on, OPENIMP_T31_OSD=0 disables) ------------- */

#define T31_IPU_START            0x2000496aUL   /* _IO('I', 106) */
#define T31_IPU_FMT_BGRA_8888    5u
#define T31_IPU_FMT_NV12         0x18u
#define T31_OSD_MAX_IPU_ERRORS   10

struct t31_ipu_osd_ch {
    uint32_t fmt, para, bak_argb, pos_x, pos_y, src_w, src_h, buf_p;
};

struct t31_ipu_param {
    uint32_t cmd, bg_w, bg_h, bg_fmt, bg_buf_p, out_fmt;
    struct t31_ipu_osd_ch ch[4];
};
_Static_assert(sizeof(struct t31_ipu_param) == 0x98, "jz_ipu_v13 struct ipu_param");

static int osd_ipu_fd = -1;
static int osd_ipu_errors;
static int osd_backend_state;       /* 0 unknown, 1 on, -1 off */

static int t31_osd_backend_enabled(void)
{
    if (osd_backend_state == 0) {
        const char *v = getenv("OPENIMP_T31_OSD");

        osd_backend_state = v && v[0] == '0' && v[1] == '\0' ? -1 : 1;
        if (osd_backend_state > 0)
            IMP_LOG_INFO("OSD", "T31 IPU OSD backend enabled (OPENIMP_T31_OSD=0 disables it)");
        else
            IMP_LOG_INFO("OSD", "T31 IPU OSD backend disabled by OPENIMP_T31_OSD=0");
    }
    return osd_backend_state > 0;
}

static void t31_osd_backend_disable(const char *reason)
{
    osd_backend_state = -1;
    IMP_LOG_INFO("OSD", "T31 IPU OSD backend disabled: %s", reason);
}

static int t31_osd_rect_size(const IMPOSDRgnAttr *attr, uint32_t *w, uint32_t *h)
{
    int rw = attr->rect.p1.x - attr->rect.p0.x + 1;
    int rh = attr->rect.p1.y - attr->rect.p0.y + 1;

    if (rw <= 0 || rh <= 0 || rw > 4096 || rh > 4096)
        return -1;
    *w = (uint32_t)rw;
    *h = (uint32_t)rh;
    return 0;
}

/* Stock libimp colour word for COVER (Y without the +16 offset). */
static uint32_t t31_osd_cover_word(uint32_t argb)
{
    int r = (int)(argb >> 16) & 255, g = (int)(argb >> 8) & 255, b = (int)argb & 255;
    int y = (66 * r + 129 * g + 25 * b + 128) >> 8;
    int u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
    int v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;

    return (argb & 0xff000000u) | (uint32_t)(y & 255) << 16 |
           (uint32_t)(u & 255) << 8 | (uint32_t)(v & 255);
}

static void t31_osd_free_bitmaps(struct t31_osd_region *r)
{
    for (int i = 0; i < 2; i++) {
        if (r->bitmap[i].phys)
            DMA_FreePhys(r->bitmap[i].phys);
        memset(&r->bitmap[i], 0, sizeof(r->bitmap[i]));
    }
    r->active = r->last = -1;
    free(r->mask);
    r->mask = NULL;
    r->mask_size = 0;
    r->mask_ok = 0;
}

/* Called with osd_lock held: copy the BITMAP mask (w*h bytes). */
static void t31_osd_load_mask(struct t31_osd_region *r)
{
    uint32_t w, h, size;

    r->mask_ok = 0;
    if (!r->attr.data.bitmapData || t31_osd_rect_size(&r->attr, &w, &h) != 0)
        return;
    size = w * h;
    if (r->mask_size < size) {
        uint8_t *m = malloc(size);

        if (!m) {
            IMP_LOG_INFO("OSD", "no memory for a %ux%u OSD bitmap", w, h);
            return;
        }
        free(r->mask);
        r->mask = m;
        r->mask_size = size;
    }
    memcpy(r->mask, r->attr.data.bitmapData, size);
    r->mask_ok = 1;
}

/* Called with osd_lock held after attr changed. */
static void t31_osd_load_region(struct t31_osd_region *r)
{
    uint32_t w, h, size;
    struct t31_osd_bitmap *b;
    int next;

    if (r->attr.type == OSD_REG_COVER) {
        r->cover_word = t31_osd_cover_word(r->attr.data.coverData.color);
        return;
    }
    if (r->attr.type == OSD_REG_LINE || r->attr.type == OSD_REG_RECT) {
        r->cover_word = t31_osd_cover_word(r->attr.data.lineRectData.color);
        r->active = -1;
        return;
    }
    if (r->attr.type == OSD_REG_BITMAP) {
        r->active = -1;
        t31_osd_load_mask(r);
        return;
    }
    /* Until a bitmap matching the current rect is loaded, draw nothing: the
     * old one may be smaller than the new rect. */
    r->active = -1;
    if ((r->attr.type != OSD_REG_PIC && r->attr.type != OSD_REG_PIC_RMEM) ||
        r->attr.fmt != T31_OSD_PIX_BGRA || !r->attr.data.picData.pData ||
        t31_osd_rect_size(&r->attr, &w, &h) != 0 || !t31_osd_backend_enabled())
        return;
    size = w * h * 4u;
    next = r->last == 0 ? 1 : 0;
    b = &r->bitmap[next];
    if (b->size < size) {
        IMPDMABufferInfo info;

        if (b->phys)
            DMA_FreePhys(b->phys);
        memset(b, 0, sizeof(*b));
        memset(&info, 0, sizeof(info));
#if defined(PLATFORM_T21)
        /* T20/T21: the FrameSource pools share the reserved arena and are
         * re-created at its bottom on every DisableChn/EnableChn; keep the
         * long-lived bitmaps at the top, as the stock OSD pool is
         * allocated once at start-up. */
        if (DMA_AllocDescriptorTop(&info, (int)size, "osd-bitmap") != 0 ||
#else
        if (DMA_AllocDescriptor(&info, (int)size, "osd-bitmap") != 0 ||
#endif
            !info.virt_addr || !info.phys_addr) {
            IMP_LOG_INFO("OSD", "no rmem for a %ux%u OSD bitmap", w, h);
            return;
        }
        b->virt = (uint8_t *)(uintptr_t)info.virt_addr;
        b->phys = info.phys_addr;
        b->size = size;
    }
    memcpy(b->virt, r->attr.data.picData.pData, size);
    DMA_RmemFlushCache(b->virt, size, 1 /* write back */);
    r->active = r->last = next;
}

/* Draw one LINE/RECT/BITMAP region into the frame like stock libimp: opaque,
 * on the CPU, offPos added, clipped. Needs fmt MONOWHITE for LINE/RECT. */
static void t31_osd_draw_cpu(struct osd_canvas *cv, const struct t31_osd_region *r)
{
    const IMPOSDRgnAttr *a = &r->attr;
    const IMPOSDGrpRgnAttr *g = &r->group_attr;

    switch (a->type) {
    case OSD_REG_LINE:
        if (a->fmt == T31_OSD_PIX_MONOWHITE)
            osd_draw_line(cv, a->rect.p0.x, a->rect.p0.y, a->rect.p1.x,
                          a->rect.p1.y, a->data.lineRectData.linewidth,
                          r->cover_word, g->offPos.x, g->offPos.y);
        break;
    case OSD_REG_RECT:
        if (a->fmt == T31_OSD_PIX_MONOWHITE)
            osd_draw_rect(cv, a->rect.p0.x, a->rect.p0.y, a->rect.p1.x,
                          a->rect.p1.y, a->data.lineRectData.linewidth,
                          r->cover_word, g->offPos.x, g->offPos.y);
        break;
    case OSD_REG_BITMAP: {
        uint32_t bw, bh;

        /* the copy matches the rect: SetRgnAttr reloads it */
        if (r->mask_ok && t31_osd_rect_size(a, &bw, &bh) == 0)
            osd_draw_bitmap(cv, r->mask, a->rect.p0.x, a->rect.p0.y,
                            bw, bh, g->offPos.x, g->offPos.y);
        break;
    }
    default:
        break;
    }
}

void openimp_t31_osd_apply(int group, void *frame)
{
    const uint8_t *fi = frame;
    uint32_t width, height, phys, virt, bg_h, fsize;
    struct t31_ipu_param p;
    int order[T31_OSD_REGIONS];
    uint32_t band_y0[T31_OSD_REGIONS], band_y1[T31_OSD_REGIONS];
    int cpu_order[T31_OSD_REGIONS];
    int count = 0, cpu_count = 0, i;

    if (!frame || !t31_osd_backend_enabled() || !valid_osd_group(group))
        return;
    memcpy(&width, fi + 0x08, 4);
    memcpy(&height, fi + 0x0c, 4);
    memcpy(&phys, fi + 0x18, 4);
    memcpy(&virt, fi + 0x1c, 4);
    memcpy(&fsize, fi + 0x14, 4);
    if (!phys || !width || !height || (width & 15u))
        return;
    bg_h = (height + 15u) & ~15u;

    pthread_mutex_lock(&osd_lock);
    if (!osd_groups[group].created || !osd_groups[group].started) {
        pthread_mutex_unlock(&osd_lock);
        return;
    }
    for (i = 0; i < T31_OSD_REGIONS; i++) {
        const struct t31_osd_region *r = &osd_regions[i];
        uint32_t w, h;
        int j;

        if (r->created && r->group == group && r->group_attr.show &&
            (r->attr.type == OSD_REG_LINE || r->attr.type == OSD_REG_RECT ||
             r->attr.type == OSD_REG_BITMAP)) {
            /* CPU-drawn types, in layer order; bounds are clipped later */
            for (j = cpu_count; j > 0 && osd_regions[cpu_order[j - 1]].group_attr.layer >
                                          r->group_attr.layer; j--)
                cpu_order[j] = cpu_order[j - 1];
            cpu_order[j] = i;
            cpu_count++;
            continue;
        }
        if (!r->created || r->group != group || !r->group_attr.show ||
            t31_osd_rect_size(&r->attr, &w, &h) != 0 ||
            r->attr.rect.p0.x < 0 || r->attr.rect.p0.y < 0 ||
            (uint32_t)r->attr.rect.p0.x + w > width ||
            (uint32_t)r->attr.rect.p0.y + h > height)
            continue;
        if (!(r->attr.type == OSD_REG_COVER ||
              ((r->attr.type == OSD_REG_PIC || r->attr.type == OSD_REG_PIC_RMEM) &&
               r->active >= 0)))
            continue;
        for (j = count; j > 0 && osd_regions[order[j - 1]].group_attr.layer >
                                  r->group_attr.layer; j--)
            order[j] = order[j - 1];
        order[j] = i;
        count++;
    }
    /* LINE/RECT/BITMAP first (stock order). The frame is cached rmem that
     * ISP DMA wrote: a line still cached from an earlier use of this buffer
     * (IVS/JPEG reads, an earlier OSD pass) would take the CPU stores and
     * then write its stale neighbour bytes back. So: find the touched rows
     * (dry pass), write back + invalidate them, draw, write them back for
     * the IPU pass and the encoder DMA. The CPU path only runs if the frame
     * buffer holds the stock layout (UV plane at align16(height) rows). */
    if (cpu_count > 0 && virt &&
        (!fsize || fsize >= width * bg_h + width * ((height + 1u) / 2u))) {
        struct osd_canvas cv;

        osd_canvas_init(&cv, (uint8_t *)(uintptr_t)virt, width, height, width,
                        (size_t)width * bg_h);
        if (osd_canvas_valid(&cv)) {
            cv.dry = 1;
            for (i = 0; i < cpu_count; i++)
                t31_osd_draw_cpu(&cv, &osd_regions[cpu_order[i]]);
            if (cv.ymin <= cv.ymax) {
                uint32_t y0 = (uint32_t)cv.ymin, y1 = (uint32_t)cv.ymax + 1u;
                void *yp = (void *)(uintptr_t)(virt + y0 * width);
                void *uvp = (void *)(uintptr_t)(virt + width * bg_h +
                                                (y0 / 2u) * width);
                uint32_t ylen = (y1 - y0) * width;
                uint32_t uvlen = ((y1 + 1u) / 2u - y0 / 2u) * width;

                DMA_RmemFlushCache(yp, ylen, 0 /* write back + invalidate */);
                DMA_RmemFlushCache(uvp, uvlen, 0);
                cv.dry = 0;
                for (i = 0; i < cpu_count; i++)
                    t31_osd_draw_cpu(&cv, &osd_regions[cpu_order[i]]);
                DMA_RmemFlushCache(yp, ylen, 1 /* write back */);
                DMA_RmemFlushCache(uvp, uvlen, 1);
            }
        }
    }
    if (count > 0 && osd_ipu_fd < 0) {
        osd_ipu_fd = open("/dev/ipu", O_RDWR | O_CLOEXEC);
        if (osd_ipu_fd < 0) {
            pthread_mutex_unlock(&osd_lock);
            t31_osd_backend_disable("cannot open /dev/ipu");
            return;
        }
    }
    for (i = 0; i < count; i += 4) {
        int n = count - i < 4 ? count - i : 4;

        memset(&p, 0, sizeof(p));
        p.bg_w = width;
        p.bg_h = bg_h;
        p.bg_fmt = T31_IPU_FMT_NV12;
        p.out_fmt = T31_IPU_FMT_NV12;
        p.bg_buf_p = phys;
        for (int k = 0; k < n; k++) {
            const struct t31_osd_region *r = &osd_regions[order[i + k]];
            const IMPOSDGrpRgnAttr *g = &r->group_attr;
            struct t31_ipu_osd_ch *c = &p.ch[k];
            int global = g->gAlphaEn > 0;
            uint32_t alpha = global ? (uint32_t)g->fgAlhpa & 0xffu : 0xffu;
            uint32_t w = 0, h = 0;

            t31_osd_rect_size(&r->attr, &w, &h);
            c->fmt = T31_IPU_FMT_BGRA_8888;
            c->pos_x = (uint32_t)r->attr.rect.p0.x;
            c->pos_y = (uint32_t)r->attr.rect.p0.y;
            c->src_w = w;
            c->src_h = h;
            if (r->attr.type == OSD_REG_COVER) {
                /* stock _ipu_set_osdx_mask (verified constants) */
                uint32_t word = r->cover_word, a = word >> 24;

                if (global) {
                    a = (((uint32_t)g->fgAlhpa & 0xffu) + 1u) * a >> 8;
                    word = (word & 0x00ffffffu) | (a > 255u ? 255u : a) << 24;
                }
                c->para = ((alpha << 3 | (global ? 0x02034005u : 0x02034001u)) &
                           0xfffc3fffu) | 0x00800000u;
                c->bak_argb = word;
            } else {
                /* stock _ipu_set_osdx_para, BGRA (verified constants) */
                c->para = alpha << 3 |
                          ((global ? 0x020347FDu : 0x020347F9u) & 0xfffff807u);
                c->buf_p = r->bitmap[r->active].phys;
            }
            p.cmd |= 1u << k;
        }
        if (ioctl(osd_ipu_fd, T31_IPU_START, &p) < 0) {
            if (++osd_ipu_errors >= T31_OSD_MAX_IPU_ERRORS) {
                pthread_mutex_unlock(&osd_lock);
                t31_osd_backend_disable("repeated IPU errors");
                return;
            }
        } else {
            osd_ipu_errors = 0;
        }
    }
    for (i = 0; i < count; i++) {
        band_y0[i] = (uint32_t)osd_regions[order[i]].attr.rect.p0.y;
        band_y1[i] = (uint32_t)osd_regions[order[i]].attr.rect.p1.y + 1u;
    }
    pthread_mutex_unlock(&osd_lock);

    /* The IPU wrote the frame behind the CPU cache; drop stale lines of the
     * blended bands so CPU readers (JPEG copy) see the overlay. */
    if (count > 0 && virt) {
        for (i = 0; i < count; i++) {
            uint32_t y0 = band_y0[i];
            uint32_t y1 = band_y1[i];

            DMA_RmemFlushCache((void *)(uintptr_t)(virt + y0 * width),
                               (y1 - y0) * width, 2 /* invalidate */);
            DMA_RmemFlushCache((void *)(uintptr_t)(virt + width * bg_h +
                                                   (y0 / 2u) * width),
                               ((y1 + 1u) / 2u - y0 / 2u) * width, 2);
        }
    }
}

int IMP_OSD_SetPoolSize(int size)
{
    if (size < 0)
        return t31_fail(EINVAL);
    pthread_mutex_lock(&osd_lock);
    osd_pool_size = size;
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_CreateGroup(int group)
{
    if (!valid_osd_group(group))
        return t31_fail(EINVAL);
    pthread_mutex_lock(&osd_lock);
    osd_groups[group].created = 1;
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_DestroyGroup(int group)
{
    int i;

    if (!valid_osd_group(group))
        return t31_fail(EINVAL);
    pthread_mutex_lock(&osd_lock);
    if (!osd_groups[group].created) {
        pthread_mutex_unlock(&osd_lock);
        return t31_fail(ENOENT);
    }
    for (i = 0; i < T31_OSD_REGIONS; i++) {
        if (osd_regions[i].created && osd_regions[i].group == group) {
            pthread_mutex_unlock(&osd_lock);
            return t31_fail(EBUSY);
        }
    }
    memset(&osd_groups[group], 0, sizeof(osd_groups[group]));
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

IMPRgnHandle IMP_OSD_CreateRgn(IMPOSDRgnAttr *attr)
{
    int handle;

    pthread_mutex_lock(&osd_lock);
    for (handle = 0; handle < T31_OSD_REGIONS; handle++) {
        if (!osd_regions[handle].created)
            break;
    }
    if (handle == T31_OSD_REGIONS) {
        pthread_mutex_unlock(&osd_lock);
        return t31_fail(ENOSPC);
    }
    memset(&osd_regions[handle], 0, sizeof(osd_regions[handle]));
    osd_regions[handle].created = 1;
    osd_regions[handle].group = -1;
    osd_regions[handle].active = -1;
    osd_regions[handle].last = -1;
    if (attr) {
        osd_regions[handle].attr = *attr;
        t31_osd_load_region(&osd_regions[handle]);
    }
    pthread_mutex_unlock(&osd_lock);
    return handle;
}

int IMP_OSD_DestroyRgn(IMPRgnHandle handle)
{
    if (!valid_osd_region(handle))
        return t31_fail(EINVAL);
    pthread_mutex_lock(&osd_lock);
    if (!osd_regions[handle].created) {
        pthread_mutex_unlock(&osd_lock);
        return t31_fail(ENOENT);
    }
    if (osd_regions[handle].group >= 0) {
        pthread_mutex_unlock(&osd_lock);
        return t31_fail(EBUSY);
    }
    t31_osd_free_bitmaps(&osd_regions[handle]);
    memset(&osd_regions[handle], 0, sizeof(osd_regions[handle]));
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_RegisterRgn(IMPRgnHandle handle, int group,
                        IMPOSDGrpRgnAttr *attr)
{
    if (!valid_osd_region(handle) || !valid_osd_group(group))
        return t31_fail(EINVAL);
    pthread_mutex_lock(&osd_lock);
    if (!osd_regions[handle].created || !osd_groups[group].created) {
        pthread_mutex_unlock(&osd_lock);
        return t31_fail(ENOENT);
    }
    if (osd_regions[handle].group >= 0 &&
        osd_regions[handle].group != group) {
        pthread_mutex_unlock(&osd_lock);
        return t31_fail(EBUSY);
    }
    osd_regions[handle].group = group;
    if (attr)
        osd_regions[handle].group_attr = *attr;
    else
        memset(&osd_regions[handle].group_attr, 0,
               sizeof(osd_regions[handle].group_attr));
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_UnRegisterRgn(IMPRgnHandle handle, int group)
{
    if (!valid_osd_region(handle) || !valid_osd_group(group))
        return t31_fail(EINVAL);
    pthread_mutex_lock(&osd_lock);
    if (!osd_regions[handle].created ||
        osd_regions[handle].group != group) {
        pthread_mutex_unlock(&osd_lock);
        return t31_fail(ENOENT);
    }
    osd_regions[handle].group = -1;
    memset(&osd_regions[handle].group_attr, 0,
           sizeof(osd_regions[handle].group_attr));
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_SetRgnAttr(IMPRgnHandle handle, IMPOSDRgnAttr *attr)
{
    if (!valid_osd_region(handle) || !attr)
        return t31_fail(EINVAL);
    pthread_mutex_lock(&osd_lock);
    if (!osd_regions[handle].created) {
        pthread_mutex_unlock(&osd_lock);
        return t31_fail(ENOENT);
    }
    osd_regions[handle].attr = *attr;
    t31_osd_load_region(&osd_regions[handle]);
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_SetRgnAttrWithTimestamp(IMPRgnHandle handle,
                                    IMPOSDRgnAttr *attr, void *timestamp)
{
    (void)timestamp;
    return IMP_OSD_SetRgnAttr(handle, attr);
}

int IMP_OSD_GetRgnAttr(IMPRgnHandle handle, IMPOSDRgnAttr *attr)
{
    if (!valid_osd_region(handle) || !attr)
        return t31_fail(EINVAL);
    pthread_mutex_lock(&osd_lock);
    if (!osd_regions[handle].created) {
        pthread_mutex_unlock(&osd_lock);
        return t31_fail(ENOENT);
    }
    *attr = osd_regions[handle].attr;
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_SetGrpRgnAttr(IMPRgnHandle handle, int group,
                           IMPOSDGrpRgnAttr *attr)
{
    if (!valid_osd_region(handle) || !valid_osd_group(group) || !attr)
        return t31_fail(EINVAL);
    pthread_mutex_lock(&osd_lock);
    if (!osd_regions[handle].created ||
        osd_regions[handle].group != group) {
        pthread_mutex_unlock(&osd_lock);
        return t31_fail(ENOENT);
    }
    osd_regions[handle].group_attr = *attr;
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_GetGrpRgnAttr(IMPRgnHandle handle, int group,
                           IMPOSDGrpRgnAttr *attr)
{
    if (!valid_osd_region(handle) || !valid_osd_group(group) || !attr)
        return t31_fail(EINVAL);
    pthread_mutex_lock(&osd_lock);
    if (!osd_regions[handle].created ||
        osd_regions[handle].group != group) {
        pthread_mutex_unlock(&osd_lock);
        return t31_fail(ENOENT);
    }
    *attr = osd_regions[handle].group_attr;
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_UpdateRgnAttrData(IMPRgnHandle handle,
                              IMPOSDRgnAttrData *data)
{
    if (!valid_osd_region(handle) || !data)
        return t31_fail(EINVAL);
    pthread_mutex_lock(&osd_lock);
    if (!osd_regions[handle].created) {
        pthread_mutex_unlock(&osd_lock);
        return t31_fail(ENOENT);
    }
    osd_regions[handle].attr.data = *data;
    t31_osd_load_region(&osd_regions[handle]);
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_ShowRgn(IMPRgnHandle handle, int group, int show)
{
    if (!valid_osd_region(handle) || !valid_osd_group(group))
        return t31_fail(EINVAL);
    pthread_mutex_lock(&osd_lock);
    if (!osd_regions[handle].created ||
        osd_regions[handle].group != group) {
        pthread_mutex_unlock(&osd_lock);
        return t31_fail(ENOENT);
    }
    osd_regions[handle].group_attr.show = !!show;
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_Start(int group)
{
    if (!valid_osd_group(group))
        return t31_fail(EINVAL);
    pthread_mutex_lock(&osd_lock);
    if (!osd_groups[group].created) {
        pthread_mutex_unlock(&osd_lock);
        return t31_fail(ENOENT);
    }
    osd_groups[group].started = 1;
    pthread_mutex_unlock(&osd_lock);
    return 0;
}

int IMP_OSD_Stop(int group)
{
    if (!valid_osd_group(group))
        return t31_fail(EINVAL);
    pthread_mutex_lock(&osd_lock);
    if (!osd_groups[group].created) {
        pthread_mutex_unlock(&osd_lock);
        return t31_fail(ENOENT);
    }
    osd_groups[group].started = 0;
    pthread_mutex_unlock(&osd_lock);
    return 0;
}
#endif /* !PLATFORM_T23 */


static uint32_t t31_register_access(uint32_t address,
                                    const uint32_t *write_value)
{
    long page_size = sysconf(_SC_PAGESIZE);
    uint32_t page_mask;
    uint32_t page_address;
    uint32_t page_offset;
    volatile uint32_t *reg;
    void *mapping;
    uint32_t result = 0;
    int fd;

    if (page_size <= 0 || (address & 3U))
        return 0;
    page_mask = (uint32_t)page_size - 1U;
    page_address = address & ~page_mask;
    page_offset = address & page_mask;
    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0)
        return 0;
    mapping = mmap(NULL, (size_t)page_size, PROT_READ | PROT_WRITE,
                   MAP_SHARED, fd, (off_t)page_address);
    if (mapping == MAP_FAILED) {
        close(fd);
        return 0;
    }
    reg = (volatile uint32_t *)((unsigned char *)mapping + page_offset);
    if (write_value) {
        *reg = *write_value;
        __sync_synchronize();
    }
    result = *reg;
    munmap(mapping, (size_t)page_size);
    close(fd);
    return result;
}

uint32_t IMP_System_ReadReg32(uint32_t address)
{
    return t31_register_access(address, NULL);
}

void IMP_System_WriteReg32(uint32_t address, uint32_t value)
{
    (void)t31_register_access(address, &value);
}
