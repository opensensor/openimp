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
#include <time.h>
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

/*
 * OSD groups. `group` is a caller-supplied index and every library in the set
 * rejects 4 and above. IMP_OSD_CreateGroup/Start/Stop open with
 * `slti v0,a0,4`; DestroyGroup uses the same constant against a saved copy of
 * the argument (T23 1.1.0 `0x9a9ec slti v0,s1,4`, because its log call clobbers
 * a0 first).
 *
 *   T31 1.1.6  0xc1cf8  slti v0,a0,4      T21 1.0.33  0x7ac08  slti v0,a0,4
 *   T31 1.1.1  0xa6b80  slti v0,a0,4      T23 1.1.0   0x9a690  slti v0,a0,4
 *   T40 1.0.2  0xa5250  slti v0,a0,4      T30 1.0.5   0xfd4e4  slti v0,a0,4
 *   T41 1.0.1  0xc1fac  slti v0,a0,4
 *
 * The region-to-group calls carry the same constant on their group argument:
 * IMP_OSD_RegisterRgn and SetGrpRgnAttr both do `slti a0,512` for the handle
 * and then `slti a1,4` for the group (T31 1.1.6 `0xc2bdc`, `0xc5390`).
 *
 * OpenIMP accepted 0..15 - four times the vendor's table - letting a caller
 * create and start groups the vendor can never have. Only group 0 is used
 * anywhere in this tree (tests/api_test.c:162), so nothing existing breaks.
 */
#define T31_OSD_GROUPS  4
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
    IMP_LOG(LOG_WARNING, "OSD", "T31 IPU OSD backend disabled: %s (errno=%d %s, "
                 "%d consecutive IPU errors)", reason, errno,
                 strerror(errno), osd_ipu_errors);
}

static int t31_osd_rect_size(const IMPOSDRgnAttr *attr, uint32_t *w, uint32_t *h)
{
    /* 64-bit: corner coordinates near INT_MIN/INT_MAX must not wrap into a
     * plausible size and then size a copy */
    long long rw = (long long)attr->rect.p1.x - attr->rect.p0.x + 1;
    long long rh = (long long)attr->rect.p1.y - attr->rect.p0.y + 1;

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
            IMP_LOG(LOG_WARNING, "OSD", "no memory for a %ux%u OSD bitmap", w, h);
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
            IMP_LOG(LOG_WARNING, "OSD", "no rmem for a %ux%u OSD bitmap", w, h);
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

/*
 * T21: the first IPU blend after an idle gap (on-demand JPEG channel wake)
 * writes nothing into the frame; later blends do (proven on device, stock
 * driver too).  For the first OSD_RETRY_WINDOW applies after such a gap one
 * glyph pixel plus the row of the first PIC region of each IPU pass is read
 * (cache invalidated) before and after the op; if memory did not change the
 * op is repeated (OSD_RETRY_MAX times).  If it still did not take, the apply
 * returns -1 so a JPEG caller can drop the frame instead of handing out a
 * snapshot without overlay.  OPENIMP_OSD_RETRY=0 disables, =1 forces on;
 * default on for T21 only.  OPENIMP_OSD_RETRY_IDLE_MS (1000) is the gap.
 */
#define OSD_RETRY_WINDOW_MS 3000   /* verify this long after an idle gap */
#define OSD_RETRY_MAX    3

static int osd_retry_cfg = -1;          /* -1 unread, 0 off, 1 on */
static int osd_retry_idle_ms = 1000;
static int osd_retry_active;
static int osd_retry_win_ms = OSD_RETRY_WINDOW_MS;
static double osd_retry_last, osd_retry_open;
static unsigned int osd_retry_ops;
static int osd_retry_done;     /* a probe saw the IPU write: window closed */

struct osd_probe {
    const uint8_t *row;
    uint32_t w, px, before_sum, before_px;
};

static double osd_mono_now(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
}

static void osd_retry_tick(void)
{
    double now;

    if (osd_retry_cfg < 0) {
        const char *v = getenv("OPENIMP_OSD_RETRY");
        const char *g = getenv("OPENIMP_OSD_RETRY_IDLE_MS");
#if defined(PLATFORM_T21)
        osd_retry_cfg = v && *v ? atoi(v) != 0 : 1;
#else
        osd_retry_cfg = v && *v ? atoi(v) != 0 : 0;
#endif
        if (g && *g)
            osd_retry_idle_ms = atoi(g);
        g = getenv("OPENIMP_OSD_RETRY_WINDOW_MS");
        if (g && *g)
            osd_retry_win_ms = atoi(g);
    }
    if (!osd_retry_cfg)
        return;
    now = osd_mono_now();
    if (osd_retry_last == 0.0 || (now - osd_retry_last) * 1000.0 >= osd_retry_idle_ms) {
        osd_retry_open = now;
        osd_retry_ops = 0;
        osd_retry_done = 0;
    }
    osd_retry_last = now;
    osd_retry_active = !osd_retry_done && (now - osd_retry_open) * 1000.0 < osd_retry_win_ms;
}

static uint32_t osd_probe_sum(const uint8_t *row, uint32_t n)
{
    uint32_t sum = 0, i;

    for (i = 0; i < n; i++)
        sum += row[i];
    return sum;
}

/* Arm a probe on the strongest-alpha pixel of the first PIC region. */
static int osd_probe_begin(struct osd_probe *pr, const struct t31_osd_region *r,
                           uint32_t virt, uint32_t width, uint32_t height)
{
    uint32_t rw = 0, rh = 0, x, y, bx = 0, by = 0, amax = 0;
    const uint8_t *bm;

    if ((r->attr.type != OSD_REG_PIC && r->attr.type != OSD_REG_PIC_RMEM) ||
        r->active < 0 || !(bm = r->bitmap[r->active].virt) || !virt ||
        t31_osd_rect_size(&r->attr, &rw, &rh) != 0 || !rw || !rh)
        return 0;
    for (y = 0; y < rh; y++)
        for (x = 0; x < rw; x++) {
            uint32_t a = bm[(y * rw + x) * 4u + 3u];

            if (a > amax) { amax = a; bx = x; by = y; }
        }
    if (amax < 128u || (uint32_t)r->attr.rect.p0.y + by >= height)
        return 0;
    pr->row = (const uint8_t *)(uintptr_t)(virt +
              ((uint32_t)r->attr.rect.p0.y + by) * width +
              (uint32_t)r->attr.rect.p0.x);
    pr->w = rw;
    pr->px = bx;
    DMA_RmemFlushCache((void *)pr->row, rw, 2 /* invalidate */);
    pr->before_sum = osd_probe_sum(pr->row, rw);
    pr->before_px = pr->row[bx];
    return 1;
}

/* 1 when the IPU op changed the probed row/pixel. */
static int osd_probe_changed(const struct osd_probe *pr)
{
    DMA_RmemFlushCache((void *)pr->row, pr->w, 2);
    return osd_probe_sum(pr->row, pr->w) != pr->before_sum ||
           pr->row[pr->px] != pr->before_px;
}

int openimp_t31_osd_apply(int group, void *frame)
{
    return openimp_t31_osd_apply_ex(group, frame, 0u);
}

int openimp_t31_osd_apply_ex(int group, void *frame, unsigned int flags)
{
    const uint8_t *fi = frame;
    uint32_t width, height, phys, virt, bg_h, fsize;
    struct t31_ipu_param p;
    int order[T31_OSD_REGIONS];
    uint32_t band_y0[T31_OSD_REGIONS], band_y1[T31_OSD_REGIONS];
    int cpu_order[T31_OSD_REGIONS];
    int count = 0, cpu_count = 0, i;
    int ipu_enabled;
    int result = 0;

    if (!frame || !valid_osd_group(group))
        return 0;
    /* LINE/RECT/BITMAP are drawn on the CPU and must keep working even when
     * the IPU backend is off (OPENIMP_T31_OSD=0, or a failed /dev/ipu open /
     * IPU error disabled it): only the PIC/COVER IPU pass is gated on it. */
    ipu_enabled = t31_osd_backend_enabled();
    memcpy(&width, fi + 0x08, 4);
    memcpy(&height, fi + 0x0c, 4);
    memcpy(&phys, fi + 0x18, 4);
    memcpy(&virt, fi + 0x1c, 4);
    memcpy(&fsize, fi + 0x14, 4);
    if (!phys || !width || !height || (width & 15u))
        return 0;
    bg_h = (height + 15u) & ~15u;

    pthread_mutex_lock(&osd_lock);
    if (!osd_groups[group].created || !osd_groups[group].started) {
        pthread_mutex_unlock(&osd_lock);
        return 0;
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
            t31_osd_rect_size(&r->attr, &w, &h) != 0)
            continue;
        if (r->attr.rect.p0.x < 0 || r->attr.rect.p0.y < 0 ||
            (uint32_t)r->attr.rect.p0.x + w > width ||
            (uint32_t)r->attr.rect.p0.y + h > height) {
            /* Diagnostic: such a region is never drawn. Log the first few
             * and then every 1000th. */
            static unsigned int oob_count;

            if (oob_count < 8u || oob_count % 1000u == 0u)
                IMP_LOG_INFO("OSD", "group %d region %d (type %d) skipped: "
                             "rect at %d,%d size %ux%u outside frame %ux%u "
                             "(%u times)", group, i, (int)r->attr.type,
                             r->attr.rect.p0.x, r->attr.rect.p0.y, w, h,
                             width, height, oob_count + 1u);
            oob_count++;
            continue;
        }
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
     * buffer holds the stock layout (UV plane at align16(height) rows).
     * One band per plane on purpose: the kernel caps the cost of a large
     * range (whole-L1 blast from 32 KiB, whole-L2 blast from scache_size in
     * arch/mips/xburst/core/sc-jz.c), while every extra range is one more
     * rmem ioctl. Flushing only the drawn spans was measured on the host at
     * thousands of ioctls per frame for a full-frame RECT - slower. */
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
    if (ipu_enabled && count > 0 && osd_ipu_fd < 0) {
        osd_ipu_fd = open("/dev/ipu", O_RDWR | O_CLOEXEC);
        if (osd_ipu_fd < 0) {
            pthread_mutex_unlock(&osd_lock);
            t31_osd_backend_disable("cannot open /dev/ipu");
            return 0;
        }
    }
    if (ipu_enabled && count > 0)
        osd_retry_tick();
    if (ipu_enabled && count > 0 && osd_retry_active && virt) {
        /* the IPU takes the frame record's phys as bg; the probe reads virt.
         * If the two do not map the same memory the blend lands elsewhere:
         * trust the virt mapping (what the CPU/JPEG copy reads). */
        static unsigned int phys_logs;
        uint32_t vp = DMA_VirtToPhys((const void *)(uintptr_t)virt);
        uint32_t rec[8];

        memcpy(rec, fi + 0x10, sizeof(rec));
        if (phys_logs++ < 24u)
            IMP_TRACE_LOG("OSD", "grp %d frame phys 0x%08x virt 0x%08x -> v2p 0x%08x%s "
                         "rec+10: %08x %08x %08x %08x %08x %08x %08x %08x",
                         group, phys, virt, vp, vp && vp != phys ? " MISMATCH" : "",
                         rec[0], rec[1], rec[2], rec[3], rec[4], rec[5], rec[6], rec[7]);
        if (vp && vp != phys)
            phys = vp;
    }
    for (i = 0; ipu_enabled && i < count; i += 4) {
        int ipu_ret;
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
        {
            struct osd_probe pr;
            int probing = osd_retry_active &&
                          osd_probe_begin(&pr, &osd_regions[order[i]], virt,
                                          width, height);
            int tries = 0;

            ipu_ret = ioctl(osd_ipu_fd, T31_IPU_START, &p);
            osd_retry_ops++;
            while (probing && ipu_ret >= 0 && !osd_probe_changed(&pr)) {
                static unsigned int retry_logs;

                if (tries++ >= OSD_RETRY_MAX) {
                    result = -1;
                    if (retry_logs++ < 8u)
                        IMP_TRACE_LOG("OSD", "IPU blend without effect after %d "
                                     "retries, frame flagged", OSD_RETRY_MAX);
                    break;
                }
                if (retry_logs++ < 8u)
                    IMP_TRACE_LOG("OSD", "IPU blend without effect, retry %d", tries);
                usleep(10000);  /* failure looks time-based: space the retries */
                ipu_ret = ioctl(osd_ipu_fd, T31_IPU_START, &p);
                osd_retry_ops++;
            }
            if (probing && tries <= OSD_RETRY_MAX && ipu_ret >= 0)
                osd_retry_done = 1;
            if (probing) {
                static unsigned int pass_logs;

                if (pass_logs++ < 40u)
                    IMP_TRACE_LOG("OSD", "probe grp %d +%.0f ms after wake, op #%u, "
                                 "retries %d: %s", group,
                                 (osd_mono_now() - osd_retry_open) * 1000.0,
                                 osd_retry_ops, tries > OSD_RETRY_MAX ? OSD_RETRY_MAX : tries,
                                 result < 0 ? "NO EFFECT" : "ok");
            }
        }
        if (ipu_ret < 0) {
            int ipu_errno = errno;
            static unsigned int ipu_fail_count;

            if (ipu_fail_count < 8u || ipu_fail_count % 1000u == 0u)
                IMP_LOG(LOG_WARNING, "OSD", "IPU_START failed: ret=-1 errno=%d (%s), "
                             "bg %ux%u, %d layer(s) in this pass, "
                             "consecutive errors %d (%u failures total)",
                             ipu_errno, strerror(ipu_errno), width, bg_h, n,
                             osd_ipu_errors + 1, ipu_fail_count + 1u);
            ipu_fail_count++;
            if (++osd_ipu_errors >= T31_OSD_MAX_IPU_ERRORS) {
                pthread_mutex_unlock(&osd_lock);
                t31_osd_backend_disable("repeated IPU errors");
                return 0;
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
     * blended bands so CPU readers (JPEG copy) see the overlay.  A caller
     * whose CPU readers invalidate for themselves skips this: on the
     * encoder path it was two rmem ioctls per region on every frame (about
     * 0.2 ms of CPU per frame on T31, 0.3 ms on T21) for a frame that only
     * DMA reads afterwards. */
    if (ipu_enabled && count > 0 && virt &&
        !(flags & OPENIMP_T31_OSD_DMA_ONLY)) {
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
    return result;
}
int IMP_OSD_SetPoolSize(int size)
{
    /* vendor 0xc5dc8 tests the size with blez: zero is rejected too */
    if (size <= 0)
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

#if defined(PLATFORM_T21) || defined(PLATFORM_T30)
/* OEM T21/T20/T30 IMP_OSD_AttachToGroup (T30 1.0.5 0xfd8ac is the same
 * code): a thin wrapper around system_attach();
 * <0 is logged and returned as -1.  system_attach() inserts "from" between
 * "to" and the cell that currently feeds it, i.e. src->to becomes
 * src->from->to, and rolls the binds back on a failure.  The encoder looks
 * its OSD group up at RegisterChn/StartRecvPic, so attach before starting
 * the channel. */
int IMP_OSD_AttachToGroup(IMPCell *from, IMPCell *to)
{
    IMPCell source;

    if (!from || !to)
        return -1;
    if (IMP_System_GetBindbyDest(to, &source) != 0 ||
        IMP_System_UnBind(&source, to) != 0)
        return -1;
    if (IMP_System_Bind(&source, from) != 0) {
        (void)IMP_System_Bind(&source, to);
        return -1;
    }
    if (IMP_System_Bind(from, to) != 0) {
        (void)IMP_System_UnBind(&source, from);
        (void)IMP_System_Bind(&source, to);
        return -1;
    }
    return 0;
}
#endif /* PLATFORM_T21 || PLATFORM_T30 */
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
    fd = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
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
