/* T40 ISP-drawn OSD: IMP_ISP_Tuning_{SetOsdPoolSize,CreateOsdRgn,SetOsdRgnAttr,
 * GetOsdRgnAttr,ShowOsdRgn,DestroyOsdRgn}, the IMP_OSD_*_ISP calls behind them
 * and IMP_ISP_Set/GetSingleOSDAttr.
 *
 * Rebuilt from the vendor T40 libimp 1.3.0 (camera) / 1.3.1 disassembly
 * (src/imp/osd/isp_osd.c, src/imp/isp/isp_tseries.c) for the vendor
 * tx-isp-t40 kernel driver:
 *
 *  - The six IMP_ISP_Tuning_*Osd* exports are tail calls into
 *    IMP_OSD_{SetPoolSize,CreateRgn,SetRgnAttr_Pic,GetRgnAttr_ISPPic,ShowRgn,
 *    DestroyRgn}_ISP.
 *  - "chn" is the frame-source channel 0..5 (sensor = chn / 3), 8 regions
 *    ("handles") per channel.  The handle must equal pic.pic_num; the ISP
 *    channel (msca output 0/1) is attr.chx.
 *  - SetRgnAttr copies the picture (stride * height bytes) into rmem and
 *    clears the stored osd_enable; nothing goes to the driver.  ShowRgn sets
 *    osd_enable, drops later regions that overlap an earlier shown one
 *    (IspOsdAdjust_Pic) and sends every created region of the channel
 *    (OsdnotifyIsp) with the physical picture address in osd_image.
 *  - Driver: one tuning control per region, 0x080000a2
 *    (IMAGE_TUNING_CID_SINGLE_OSD_ATTR), payload the 44-byte
 *    IMPISPSingleOSDAttr; the kernel (tisp_s_osd_single_pic) programs the
 *    picture into the msca shadow registers of that output channel, so the
 *    picture is drawn after the scaler, in one stream only.
 *
 * Differences to the vendor (documented in docs/T40_STATUS.md):
 *  - The picture memory comes from the OpenIMP rmem allocator, one block per
 *    region, and is written back from the cache before the driver sees it.
 *    The vendor allocates a pool of SetOsdPoolSize bytes (default 128) at
 *    IMP_System_Init; OpenIMP counts the region sizes against the latest
 *    pool size instead, so a pool size set after IMP_System_Init still works.
 *  - A created region without a picture is not sent to the driver (the
 *    vendor sends it with a meaningless physical address).
 */

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <imp/imp_framesource.h>
#include <imp/imp_isp.h>

#include "dma_alloc.h"

#if defined(PLATFORM_T41)
#define TISP_VIDIOC_DEFAULT_TUNING 0xc0105435U
#else
#define TISP_VIDIOC_DEFAULT_TUNING 0xc0105436U
#endif
#define TISP_CID_SINGLE_OSD_ATTR 0x080000a2 /* IMP_ISP_Set/GetSingleOSDAttr */

#define ISP_OSD_CHANNELS 6                  /* MAXISPCHNMUN */
#define ISP_OSD_REGIONS 8                   /* MAXISPOSDPIC */
#define ISP_OSD_DEFAULT_POOL 128            /* vendor .data default */

#if UINTPTR_MAX == 0xffffffffu
/* The driver copies 44 bytes; the library keeps 48-byte attributes. */
_Static_assert(sizeof(IMPISPSingleOSDAttr) == 44, "IMPISPSingleOSDAttr");
_Static_assert(sizeof(IMPIspOsdAttrAsm) == 48, "IMPIspOsdAttrAsm");
#endif

extern int OpenIMP_P1_TuningIOCtl(uint32_t command, void *argument);

typedef struct {
    int32_t vinum;
    int32_t direction;
    int32_t control;
    uintptr_t payload;
} IspOsdRequest;

typedef struct {
    int created;
    unsigned seq;                   /* creation order = vendor busy list */
    int num;                        /* sensor: chn / 3 */
    unsigned char show;
    IMPIspOsdAttrAsm attr;          /* osd_image = the rmem copy */
    uint32_t phys;
    uint32_t size;                  /* stride * height of the copy */
} IspOsdRegion;

static struct {
    pthread_mutex_t lock;
    int pool_size;
    uint32_t pool_used;
    unsigned seq;
    unsigned char free_order[ISP_OSD_CHANNELS][ISP_OSD_REGIONS];
    int free_count[ISP_OSD_CHANNELS];
    int ready;
    IspOsdRegion rgn[ISP_OSD_CHANNELS][ISP_OSD_REGIONS];
} isp_osd = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .pool_size = ISP_OSD_DEFAULT_POOL,
};

static void isp_osd_prepare(void)
{
    int c, h;

    if (isp_osd.ready)
        return;
    for (c = 0; c < ISP_OSD_CHANNELS; c++) {
        for (h = 0; h < ISP_OSD_REGIONS; h++)
            isp_osd.free_order[c][h] = (unsigned char)h;
        isp_osd.free_count[c] = ISP_OSD_REGIONS;
    }
    isp_osd.ready = 1;
}

static int isp_osd_ioctl(int num, int direction, int control, void *payload)
{
    IspOsdRequest request;

    request.vinum = num;
    request.direction = direction;
    request.control = control;
    request.payload = (uintptr_t)payload;
    return OpenIMP_P1_TuningIOCtl(TISP_VIDIOC_DEFAULT_TUNING, &request);
}

/* Vendor IMP_ISP_SetSingleOSDAttr / GetSingleOSDAttr: sensor 0..3, pointer
 * pass-through of the 44-byte attribute; a failed ioctl is -6. */
int32_t IMP_ISP_SetSingleOSDAttr(IMPVI_NUM num, IMPISPSingleOSDAttr *attr)
{
    if ((unsigned)num >= 4U)
        return -4;
    if (!attr)
        return -9;
    return isp_osd_ioctl(num, 0, TISP_CID_SINGLE_OSD_ATTR, attr) ? -6 : 0;
}

int32_t IMP_ISP_GetSingleOSDAttr(IMPVI_NUM num, IMPISPSingleOSDAttr *attr)
{
    if ((unsigned)num >= 4U)
        return -4;
    if (!attr)
        return -9;
    return isp_osd_ioctl(num, 1, TISP_CID_SINGLE_OSD_ATTR, attr) ? -6 : 0;
}

static int rects_cross(const IMPISPOSDPicAttr *a, const IMPISPOSDPicAttr *b)
{
    /* vendor iscrossrect(): 16-bit max/min of the two rectangles */
    uint16_t l = a->osd_left > b->osd_left ? a->osd_left : b->osd_left;
    uint16_t r1 = (uint16_t)(a->osd_left + a->osd_width);
    uint16_t r2 = (uint16_t)(b->osd_left + b->osd_width);
    uint16_t r = r1 < r2 ? r1 : r2;
    uint16_t t = a->osd_top > b->osd_top ? a->osd_top : b->osd_top;
    uint16_t b1 = (uint16_t)(a->osd_top + a->osd_height);
    uint16_t b2 = (uint16_t)(b->osd_top + b->osd_height);
    uint16_t bo = b1 < b2 ? b1 : b2;

    return l < r && t < bo;
}

/* Created regions of a channel in creation order (the vendor busy list). */
static int isp_osd_busy(int chn, IspOsdRegion *list[ISP_OSD_REGIONS])
{
    int n = 0, h, i;

    for (h = 0; h < ISP_OSD_REGIONS; h++) {
        IspOsdRegion *r = &isp_osd.rgn[chn][h];

        if (!r->created)
            continue;
        for (i = n; i > 0 && list[i - 1]->seq > r->seq; i--)
            list[i] = list[i - 1];
        list[i] = r;
        n++;
    }
    return n;
}

/* IspOsdAdjust_Pic: a shown region that overlaps an earlier shown region of
 * the same channel is switched off. */
static void isp_osd_adjust(int chn)
{
    IspOsdRegion *list[ISP_OSD_REGIONS];
    int n = isp_osd_busy(chn, list), i, j;

    for (i = 0; i < n; i++) {
        IMPISPOSDPicAttr *a = &list[i]->attr.stsinglepicAttr.pic;

        if (!a->osd_enable)
            continue;
        for (j = i + 1; j < n; j++) {
            IMPISPOSDPicAttr *b = &list[j]->attr.stsinglepicAttr.pic;

            if (b->osd_enable && rects_cross(a, b)) {
                fprintf(stderr, "openimp-isposd: chn%d region %d overlaps an "
                        "earlier region, not shown\n", chn,
                        (int)(list[j] - isp_osd.rgn[chn]));
                b->osd_enable = 0;
                list[j]->show = 0;
            }
        }
    }
}

/* OsdnotifyIsp: every created region whose handle is its pic_num goes to the
 * driver, with the physical address of the picture copy. */
static void isp_osd_notify(int chn)
{
    IspOsdRegion *list[ISP_OSD_REGIONS];
    int n = isp_osd_busy(chn, list), i;

    for (i = 0; i < n; i++) {
        IspOsdRegion *r = list[i];
        IMPISPSingleOSDAttr single;
        int handle = (int)(r - isp_osd.rgn[chn]);

        if (r->attr.stsinglepicAttr.pic_num != handle || !r->phys)
            continue;
        single = r->attr.stsinglepicAttr;
        single.pic.osd_image = (char *)(uintptr_t)r->phys;
        if (IMP_ISP_SetSingleOSDAttr((IMPVI_NUM)r->num, &single) < 0)
            fprintf(stderr, "openimp-isposd: IMP_ISP_SetSingleOSDAttr err "
                    "(chn%d handle%d)\n", chn, handle);
    }
}

int IMP_OSD_SetPoolSize_ISP(int size)
{
    if (size <= 0)
        return -1;
    pthread_mutex_lock(&isp_osd.lock);
    isp_osd.pool_size = size;
    pthread_mutex_unlock(&isp_osd.lock);
    return 0;
}

int IMP_OSD_Init_ISP(void)
{
    pthread_mutex_lock(&isp_osd.lock);
    isp_osd_prepare();
    pthread_mutex_unlock(&isp_osd.lock);
    return 0;
}

int IMP_OSD_CreateRgn_ISP(int chn, IMPIspOsdAttrAsm *attr)
{
    IspOsdRegion *r;
    int handle;

    if ((unsigned)chn >= ISP_OSD_CHANNELS)
        return -1;
    if (attr) {
        fprintf(stderr, "openimp-isposd: pIspOsdAttr is not null, not "
                "supported\n");
        return -1;
    }
    pthread_mutex_lock(&isp_osd.lock);
    isp_osd_prepare();
    if (!isp_osd.free_count[chn]) {
        pthread_mutex_unlock(&isp_osd.lock);
        fprintf(stderr, "openimp-isposd: chn %d only supports %d pictures\n",
                chn, ISP_OSD_REGIONS);
        return -1;
    }
    handle = isp_osd.free_order[chn][0];
    memmove(&isp_osd.free_order[chn][0], &isp_osd.free_order[chn][1],
            (size_t)--isp_osd.free_count[chn]);
    r = &isp_osd.rgn[chn][handle];
    memset(r, 0, sizeof(*r));
    r->created = 1;
    r->seq = ++isp_osd.seq;
    r->num = chn / 3;
    pthread_mutex_unlock(&isp_osd.lock);
    return handle;
}

int IMP_OSD_SetRgnAttr_PicISP(int chn, int handle, IMPIspOsdAttrAsm *attr)
{
    IMPFSChnAttr fs;
    IspOsdRegion *r;
    const IMPISPOSDPicAttr *pic;
    uint32_t size;
    IMPDMABufferInfo info;
    void *virt;

    if ((unsigned)handle >= ISP_OSD_REGIONS ||
        (unsigned)chn >= ISP_OSD_CHANNELS || !attr)
        return -1;
    pic = &attr->stsinglepicAttr.pic;
    if (IMP_FrameSource_GetChnAttr(chn, &fs) != 0) {
        fprintf(stderr, "openimp-isposd: chn%d get frame param err\n", chn);
        return -1;
    }
    if ((uint32_t)pic->osd_top + pic->osd_height > (uint32_t)fs.picHeight ||
        (uint32_t)pic->osd_left + pic->osd_width > (uint32_t)fs.picWidth) {
        fprintf(stderr, "openimp-isposd: region %ux%u at %u,%u exceeds the "
                "%dx%d frame\n", pic->osd_width, pic->osd_height,
                pic->osd_left, pic->osd_top, fs.picWidth, fs.picHeight);
        return -1;
    }
    pthread_mutex_lock(&isp_osd.lock);
    r = &isp_osd.rgn[chn][handle];
    if (!r->created) {
        pthread_mutex_unlock(&isp_osd.lock);
        fprintf(stderr, "openimp-isposd: chn%d region %d hasn't been "
                "created\n", chn, handle);
        return -1;
    }
    size = (uint32_t)pic->osd_stride * pic->osd_height;
    if (!size || attr->type != ISP_OSD_REG_PIC) {
        pthread_mutex_unlock(&isp_osd.lock);
        fprintf(stderr, "openimp-isposd: chn%d region %d: type %d, stride %u "
                "height %u invalid\n", chn, handle, (int)attr->type,
                pic->osd_stride, pic->osd_height);
        return -1;
    }
    if (r->size && r->size != size) {
        pthread_mutex_unlock(&isp_osd.lock);
        fprintf(stderr, "openimp-isposd: chn%d region %d can't change the "
                "picture size, destroy and recreate it\n", chn, handle);
        return -1;
    }
    if (!r->size) {
        uint32_t need = (size + 7U) & ~7U;

        if (isp_osd.pool_used + need > (uint32_t)isp_osd.pool_size ||
            DMA_AllocDescriptor(&info, (int)size, "isp-osd") != 0 ||
            !info.virt_addr || !info.phys_addr) {
            pthread_mutex_unlock(&isp_osd.lock);
            fprintf(stderr, "openimp-isposd: chn%d handle%d: no pool memory "
                    "for %u bytes (pool %d, used %u)\n", chn, handle, size,
                    isp_osd.pool_size, isp_osd.pool_used);
            return -1;
        }
        isp_osd.pool_used += need;
        r->phys = info.phys_addr;
        r->size = size;
        virt = (void *)(uintptr_t)info.virt_addr;
    } else {
        virt = r->attr.stsinglepicAttr.pic.osd_image;
    }
    r->attr = *attr;
    if (pic->osd_image)
        memcpy(virt, pic->osd_image, size);
    else
        memset(virt, 0, size);
    DMA_RmemFlushCache(virt, size, 1);
    r->attr.stsinglepicAttr.pic.osd_enable = 0;
    r->attr.stsinglepicAttr.pic.osd_image = virt;
    pthread_mutex_unlock(&isp_osd.lock);
    return 0;
}

int IMP_OSD_GetRgnAttr_ISPPic(int chn, int handle, IMPIspOsdAttrAsm *attr)
{
    /* vendor: channel 0 or 1 only, the stored copy whether created or not */
    if ((unsigned)handle >= ISP_OSD_REGIONS || (unsigned)chn >= 2U || !attr)
        return -1;
    pthread_mutex_lock(&isp_osd.lock);
    *attr = isp_osd.rgn[chn][handle].attr;
    pthread_mutex_unlock(&isp_osd.lock);
    return 0;
}

int IMP_OSD_ShowRgn_ISP(int chn, int handle, int showFlag)
{
    IspOsdRegion *r;

    if ((unsigned)handle >= ISP_OSD_REGIONS ||
        (unsigned)chn >= ISP_OSD_CHANNELS)
        return -1;
    pthread_mutex_lock(&isp_osd.lock);
    r = &isp_osd.rgn[chn][handle];
    if (!r->created) {
        pthread_mutex_unlock(&isp_osd.lock);
        fprintf(stderr, "openimp-isposd: chn%d rgn %d is free, can't set "
                "showflag\n", chn, handle);
        return -1;
    }
    r->show = (unsigned char)showFlag;
    r->attr.stsinglepicAttr.pic.osd_enable = (uint8_t)showFlag;
    isp_osd_adjust(chn);
    isp_osd_notify(chn);
    pthread_mutex_unlock(&isp_osd.lock);
    return 0;
}

int IMP_OSD_DestroyRgn_ISP(int chn, int handle)
{
    IspOsdRegion *r;

    if ((unsigned)chn >= ISP_OSD_CHANNELS ||
        (unsigned)handle >= ISP_OSD_REGIONS)
        return -1;
    r = &isp_osd.rgn[chn][handle];
    if (r->show)
        IMP_OSD_ShowRgn_ISP(chn, handle, 0);
    pthread_mutex_lock(&isp_osd.lock);
    isp_osd_prepare();
    if (r->created) {
        if (r->phys) {
            if (DMA_FreePhys(r->phys) < 0)
                fprintf(stderr, "openimp-isposd: OSD_mem_free err\n");
            isp_osd.pool_used -= (r->size + 7U) & ~7U;
        }
        memset(r, 0, sizeof(*r));
        isp_osd.free_order[chn][isp_osd.free_count[chn]++] =
            (unsigned char)handle;
    }
    pthread_mutex_unlock(&isp_osd.lock);
    return 0;
}

void IMP_OSD_Exit_ISP(void)
{
    int c, h;

    for (c = 0; c < ISP_OSD_CHANNELS; c++)
        for (h = 0; h < ISP_OSD_REGIONS; h++)
            if (isp_osd.rgn[c][h].created)
                IMP_OSD_DestroyRgn_ISP(c, h);
}

int IMP_ISP_Tuning_SetOsdPoolSize(int size)
{
    return IMP_OSD_SetPoolSize_ISP(size);
}

int IMP_ISP_Tuning_CreateOsdRgn(int chn, IMPIspOsdAttrAsm *attr)
{
    return IMP_OSD_CreateRgn_ISP(chn, attr);
}

int IMP_ISP_Tuning_SetOsdRgnAttr(int chn, int handle, IMPIspOsdAttrAsm *attr)
{
    return IMP_OSD_SetRgnAttr_PicISP(chn, handle, attr);
}

int IMP_ISP_Tuning_GetOsdRgnAttr(int chn, int handle, IMPIspOsdAttrAsm *attr)
{
    return IMP_OSD_GetRgnAttr_ISPPic(chn, handle, attr);
}

int IMP_ISP_Tuning_ShowOsdRgn(int chn, int handle, int showFlag)
{
    return IMP_OSD_ShowRgn_ISP(chn, handle, showFlag);
}

int IMP_ISP_Tuning_DestroyOsdRgn(int chn, int handle)
{
    return IMP_OSD_DestroyRgn_ISP(chn, handle);
}
