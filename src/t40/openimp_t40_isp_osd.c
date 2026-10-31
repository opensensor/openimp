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
 *
 * T41 (vendor libimp 1.2.6 src/imp/osd/isp_osd.c, same file built with
 * PLATFORM_T41): the region table, pool, picture copy, overlap check and
 * show logic are the same as T40, with these differences:
 *  - "chn" is 0 or 1 (vendor: MAXSUPCHNMUN 1, the check is chn < 2; the
 *    vendor allocates one channel table, so chn 1 writes past it - here
 *    both have their own table).  The picture is checked against the
 *    frame-source channel chn; the ISP draws it in the main stream (MSCA
 *    output 0) coordinates, before the outputs are scaled.
 *  - IMPIspOsdAttrAsm is { type, IMPISPOSDSingleAttr { IMPISPOSDAttr
 *    chnOSDAttr, IMPISPOSDBlockAttr pic } } (36 bytes); the handle must
 *    equal pic.pinum.
 *  - OsdnotifyIsp sends each region as two tuning controls on sensor 0:
 *    IMP_ISP_Tuning_SetOSDAttr (0x080000a1, the 12-byte picture format)
 *    and IMP_ISP_Tuning_SetOSDBlock (0x080000a2, the 20-byte block with the
 *    physical picture address).  open-tx-isp T41 stores both and writes the
 *    MSCA OSD registers with the next frame's shadow commit.
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
#define TISP_CID_OSD_ATTR 0x080000a1        /* T41 IMP_ISP_Tuning_Set/GetOSDAttr */
#define TISP_CID_OSD_BLOCK 0x080000a2       /* T41 IMP_ISP_Tuning_Set/GetOSDBlock */

#if defined(PLATFORM_T41)
#define ISP_OSD_CHANNELS 2                  /* vendor chn < 2 */
typedef IMPISPOSDBlockAttr IspOsdPic;
#define RGN_PICNUM(a) ((a)->stsinglepicAttr.pic.pinum)
#else
#define ISP_OSD_CHANNELS 6                  /* MAXISPCHNMUN */
typedef IMPISPOSDPicAttr IspOsdPic;
#define RGN_PICNUM(a) ((a)->stsinglepicAttr.pic_num)
#endif
#define ISP_OSD_REGIONS 8                   /* MAXISPOSDPIC */
#define ISP_OSD_DEFAULT_POOL 128            /* vendor .data default */

#if UINTPTR_MAX == 0xffffffffu
#if defined(PLATFORM_T41)
/* The driver copies 12 and 20 bytes. */
_Static_assert(sizeof(IMPISPOSDAttr) == 12, "IMPISPOSDAttr");
_Static_assert(sizeof(IMPISPOSDBlockAttr) == 20, "IMPISPOSDBlockAttr");
_Static_assert(sizeof(IMPIspOsdAttrAsm) == 36, "IMPIspOsdAttrAsm");
#else
/* The driver copies 44 bytes; the library keeps 48-byte attributes. */
_Static_assert(sizeof(IMPISPSingleOSDAttr) == 44, "IMPISPSingleOSDAttr");
_Static_assert(sizeof(IMPIspOsdAttrAsm) == 48, "IMPIspOsdAttrAsm");
#endif
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

#if defined(PLATFORM_T41)
extern int OpenIMP_P1_TuningEnabled(void);

/* Vendor T41 1.2.6 IMP_ISP_Tuning_Set/GetOSDAttr (isp_tseries 0x16a28) and
 * Set/GetOSDBlock (0x173c4): pointer pass-through of the 12-byte
 * IMPISPOSDAttr / 20-byte IMPISPOSDBlockAttr, controls 0x080000a1 /
 * 0x080000a2.  Vendor return codes: NULL -4087, sensor >= 2 -4084, tuning
 * not enabled -4091, failed ioctl -4090. */
static int32_t isp_osd_tuning(IMPVI_NUM num, int direction, int control,
                              void *attr)
{
    if (!attr)
        return -4087;
    if ((unsigned)num >= 2U)
        return -4084;
    if (!OpenIMP_P1_TuningEnabled())
        return -4091;
    return isp_osd_ioctl(num, direction, control, attr) ? -4090 : 0;
}

int32_t IMP_ISP_Tuning_SetOSDAttr(IMPVI_NUM num, IMPISPOSDAttr *attr)
{
    return isp_osd_tuning(num, 0, TISP_CID_OSD_ATTR, attr);
}

int32_t IMP_ISP_Tuning_GetOSDAttr(IMPVI_NUM num, IMPISPOSDAttr *attr)
{
    return isp_osd_tuning(num, 1, TISP_CID_OSD_ATTR, attr);
}

int32_t IMP_ISP_Tuning_SetOSDBlock(IMPVI_NUM num, IMPISPOSDBlockAttr *attr)
{
    return isp_osd_tuning(num, 0, TISP_CID_OSD_BLOCK, attr);
}

int32_t IMP_ISP_Tuning_GetOSDBlock(IMPVI_NUM num, IMPISPOSDBlockAttr *attr)
{
    return isp_osd_tuning(num, 1, TISP_CID_OSD_BLOCK, attr);
}

/* Vendor T41 1.2.5 IMP_ISP_Tuning_Set/GetDrawBlock (0x6910c / 0x68af4):
 * pointer pass-through of the 32-byte IMPISPDrawBlockAttr, control
 * 0x080000a0, the same checks and codes as the OSD pair above.  The
 * vendor also keeps the attribute for replay at the next AddSensor, which
 * OpenIMP has no use for. */
#define TISP_CID_DRAW_BLOCK 0x080000a0
_Static_assert(sizeof(IMPISPDrawBlockAttr) == 32, "IMPISPDrawBlockAttr");

int32_t IMP_ISP_Tuning_SetDrawBlock(IMPVI_NUM num, IMPISPDrawBlockAttr *attr)
{
    return isp_osd_tuning(num, 0, TISP_CID_DRAW_BLOCK, attr);
}

int32_t IMP_ISP_Tuning_GetDrawBlock(IMPVI_NUM num, IMPISPDrawBlockAttr *attr)
{
    return isp_osd_tuning(num, 1, TISP_CID_DRAW_BLOCK, attr);
}

/* Vendor T41 1.2.5 IMP_OSD_SetRgnAttr_ISP (0xaaa68) / GetRgnAttr_ISP
 * (0xaae78): the IMPOSDRgnAttr of an ISP-drawn region carries an
 * IMPOSDIspDraw at +48 { IMPISPDrawBlockAttr (+48, 32 bytes),
 * IMPISPOSDBlockAttr (+80, 20 bytes), IMPISPMaskBlockAttr (+100) }.
 * Set: sensor < 2 (0x80020001), attr (0x80020010), type 10..12 (else
 * 0x80020040); bosdshow is kept for Get; type 11 (line/rectangle) goes to
 * IMP_ISP_Tuning_SetDrawBlock (failure 0x80020040), type 12 (cover) to
 * IMP_ISP_Tuning_SetMaskBlock (failure -1), type 10 (picture) is refused
 * with 0x80020040 (pictures go through IMP_OSD_SetRgnAttr_PicISP).  Get:
 * sensor, attr and pbosdshow, then GetDrawBlock, GetOSDAttr (the 12-byte
 * format, written at +80) and GetMaskBlock (any failure 0x80020040).
 * Difference: the vendor answers 0x80020008 before IMP_System_Init
 * created its OSD module; OpenIMP has no such module state. */
#define P3_OSD_ISP_DRAW 48
#define P3_OSD_ISP_PIC 80
#define P3_OSD_ISP_COVER 100
#define P3_OSD_ISP_ERR_NUM 0x80020001
#define P3_OSD_ISP_ERR_PARAM 0x80020010
#define P3_OSD_ISP_ERR_OP 0x80020040
_Static_assert(P3_OSD_ISP_DRAW + sizeof(IMPISPDrawBlockAttr) == P3_OSD_ISP_PIC,
               "the OSD block follows the draw block");
_Static_assert(P3_OSD_ISP_PIC + sizeof(IMPISPOSDBlockAttr) == P3_OSD_ISP_COVER,
               "the mask block follows the OSD block");
static int isp_osd_rgn_show;
static pthread_mutex_t isp_osd_rgn_lock = PTHREAD_MUTEX_INITIALIZER;

int IMP_OSD_SetRgnAttr_ISP(int sensornum, void *prAttr, int bosdshow)
{
    unsigned char *attr = prAttr;
    uint32_t type;
    int result;

    if ((unsigned)sensornum >= 2U)
        return (int)P3_OSD_ISP_ERR_NUM;
    if (!attr)
        return (int)P3_OSD_ISP_ERR_PARAM;
    memcpy(&type, attr, sizeof(type));
    if (type < 10U || type > 12U)
        return (int)P3_OSD_ISP_ERR_OP;
    pthread_mutex_lock(&isp_osd_rgn_lock);
    isp_osd_rgn_show = bosdshow != 0;
    if (type == 11U)
        result = IMP_ISP_Tuning_SetDrawBlock((IMPVI_NUM)sensornum,
                     (IMPISPDrawBlockAttr *)(attr + P3_OSD_ISP_DRAW)) < 0 ?
                     (int)P3_OSD_ISP_ERR_OP : 0;
    else if (type == 12U)
        result = IMP_ISP_Tuning_SetMaskBlock((IMPVI_NUM)sensornum,
                     (IMPISPMaskBlockAttr *)(attr + P3_OSD_ISP_COVER)) < 0 ?
                     -1 : 0;
    else
        result = (int)P3_OSD_ISP_ERR_OP;
    pthread_mutex_unlock(&isp_osd_rgn_lock);
    return result;
}

int IMP_OSD_GetRgnAttr_ISP(int sensornum, void *prAttr, int *pbosdshow)
{
    unsigned char *attr = prAttr;

    if ((unsigned)sensornum >= 2U)
        return (int)P3_OSD_ISP_ERR_NUM;
    if (!attr || !pbosdshow)
        return (int)P3_OSD_ISP_ERR_PARAM;
    pthread_mutex_lock(&isp_osd_rgn_lock);
    *pbosdshow = isp_osd_rgn_show;
    pthread_mutex_unlock(&isp_osd_rgn_lock);
    if (IMP_ISP_Tuning_GetDrawBlock((IMPVI_NUM)sensornum,
            (IMPISPDrawBlockAttr *)(attr + P3_OSD_ISP_DRAW)) < 0 ||
        IMP_ISP_Tuning_GetOSDAttr((IMPVI_NUM)sensornum,
            (IMPISPOSDAttr *)(attr + P3_OSD_ISP_PIC)) < 0 ||
        IMP_ISP_Tuning_GetMaskBlock((IMPVI_NUM)sensornum,
            (IMPISPMaskBlockAttr *)(attr + P3_OSD_ISP_COVER)) < 0)
        return (int)P3_OSD_ISP_ERR_OP;
    return 0;
}
#else
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
#endif

static int rects_cross(const IspOsdPic *a, const IspOsdPic *b)
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
        IspOsdPic *a = &list[i]->attr.stsinglepicAttr.pic;

        if (!a->osd_enable)
            continue;
        for (j = i + 1; j < n; j++) {
            IspOsdPic *b = &list[j]->attr.stsinglepicAttr.pic;

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
        int handle = (int)(r - isp_osd.rgn[chn]);
#if defined(PLATFORM_T41)
        IMPISPOSDSingleAttr single;

        if (RGN_PICNUM(&r->attr) != handle || !r->phys)
            continue;
        single = r->attr.stsinglepicAttr;
        single.pic.osd_image = (char *)(uintptr_t)r->phys;
        if (IMP_ISP_Tuning_SetOSDAttr((IMPVI_NUM)r->num,
                                      &single.chnOSDAttr) < 0) {
            fprintf(stderr, "openimp-isposd: IMP_ISP_Tuning_SetOSDAttr err "
                    "(chn%d handle%d)\n", chn, handle);
            continue;
        }
        if (IMP_ISP_Tuning_SetOSDBlock((IMPVI_NUM)r->num, &single.pic) < 0)
            fprintf(stderr, "openimp-isposd: IMP_ISP_Tuning_SetOSDBlock err "
                    "(chn%d handle%d)\n", chn, handle);
#else
        IMPISPSingleOSDAttr single;

        if (RGN_PICNUM(&r->attr) != handle || !r->phys)
            continue;
        single = r->attr.stsinglepicAttr;
        single.pic.osd_image = (char *)(uintptr_t)r->phys;
        if (IMP_ISP_SetSingleOSDAttr((IMPVI_NUM)r->num, &single) < 0)
            fprintf(stderr, "openimp-isposd: IMP_ISP_SetSingleOSDAttr err "
                    "(chn%d handle%d)\n", chn, handle);
#endif
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
#if defined(PLATFORM_T41)
    r->num = 0;                     /* one ISP, one OSD bank */
#else
    r->num = chn / 3;
#endif
    pthread_mutex_unlock(&isp_osd.lock);
    return handle;
}

int IMP_OSD_SetRgnAttr_PicISP(int chn, int handle, IMPIspOsdAttrAsm *attr)
{
    IMPFSChnAttr fs;
    IspOsdRegion *r;
    const IspOsdPic *pic;
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

#if !defined(PLATFORM_T41)
/* T40 IMP_ISP_GetRaw (vendor 1.3.1 0x8a410): writes the sensor number into
 * attr->sensor_id, num < 4 (-4), ISP device open (-8), raw_bit 8 (w*h bytes)
 * or 16 (w*h*2) else -1; one rmem buffer per sensor (the 148-byte IMP_Alloc
 * descriptor, -2 if it cannot be allocated), its paddr/vaddr/size go into
 * attr->buf, then attr goes to the ISP-device ioctl 0xc004542f, which has
 * the kernel write one raw frame there (-1 if it fails).  Differences: the
 * vendor keeps the first buffer even when a later call needs more (the
 * kernel would write past it); OpenIMP allocates a larger one then.  After
 * the ioctl the CPU cache of the buffer is invalidated so attr->buf.vaddr
 * shows what the DMA wrote.  NULL attr is -1 (the vendor would fault). */
extern int OpenIMP_P1_IspIOCtlOpened(uint32_t command, void *argument);
#define TISP_IOCTL_GET_RAW 0xc004542fU
static IMPDMABufferInfo isp_raw_buf[4];
static pthread_mutex_t isp_raw_lock = PTHREAD_MUTEX_INITIALIZER;

int32_t IMP_ISP_GetRaw(IMPVI_NUM num, IMPISPGETRawiConfig *attr)
{
    IMPDMABufferInfo *b;
    uint32_t size;

    if (!attr)
        return -1;
    attr->sensor_id = (unsigned short)num;
    if ((unsigned)num >= 4U)
        return -4;
    if (attr->raw_bit == 8)
        size = attr->raw_width * attr->raw_height;
    else if (attr->raw_bit == 16)
        size = attr->raw_width * attr->raw_height * 2U;
    else
        return -1;
    if (!size)
        return -1;
    pthread_mutex_lock(&isp_raw_lock);
    b = &isp_raw_buf[num];
    if (b->phys_addr && b->size < size) {
        DMA_FreePhys(b->phys_addr);
        memset(b, 0, sizeof(*b));
    }
    if (!b->phys_addr &&
        (DMA_AllocDescriptor(b, (int)size, "isp-raw") != 0 ||
         !b->phys_addr || !b->virt_addr)) {
        memset(b, 0, sizeof(*b));
        pthread_mutex_unlock(&isp_raw_lock);
        return -2;
    }
    attr->buf.paddr = b->phys_addr;
    attr->buf.vaddr = b->virt_addr;
    attr->buf.buf_size = b->size;
    if (OpenIMP_P1_IspIOCtlOpened(TISP_IOCTL_GET_RAW, attr) != 0) {
        pthread_mutex_unlock(&isp_raw_lock);
        return errno == ENODEV ? -8 : -1;
    }
    DMA_RmemFlushCache((void *)(uintptr_t)b->virt_addr, size, 2);
    pthread_mutex_unlock(&isp_raw_lock);
    return 0;
}
#endif /* !PLATFORM_T41 */
