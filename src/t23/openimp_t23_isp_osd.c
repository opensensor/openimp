/* T23 ISP OSD: pictures drawn by the ISP itself (isp_osd.h), plus the
 * IMP_OSD_*_ISP wrappers that hand ISP line/rect/cover regions to the ISP.
 *
 * As in the OEM libimp (isp_osd.c), a region is one of 8 picture slots per
 * sensor channel.  Its picture lives in rmem and is programmed into the ISP
 * with IMP_ISP_Tuning_SetOSDAttr + IMP_ISP_Tuning_SetOSDBlock (block pinum =
 * handle, osd_image = physical address).  Those ISP tuning entry points, and
 * the draw/mask block ones used by IMP_OSD_SetRgnAttr_ISP, belong to the ISP
 * layer (src/isp/isp_t23_tuning.c).  The OEM IMP_ISP_Tuning_{Create,Destroy,Set,Get,Show}OsdRgn and
 * IMP_ISP_Tuning_SetOsdPoolSize are tail calls into the functions below. */

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <imp/imp_framesource.h>

#include "dma_alloc.h"
#include "imp_log_int.h"
#include "t23/openimp_t23_osd_abi.h"

#define T23_ISP_OSD_CHANNELS 3
#define T23_ISP_OSD_PICS     8
#define T23_ISP_OSD_HIDDEN   9      /* OEM pinum of a not yet shown slot */

/* ISP layer: src/isp/isp_t23_tuning.c */
extern int IMP_ISP_Tuning_SetOSDAttr(T23ISPOSDAttr *);
extern int IMP_ISP_Tuning_SetOSDBlock(T23ISPOSDBlockAttr *);
extern int IMP_ISP_Tuning_SetOSDAttr_Sec(T23ISPOSDAttr *);
extern int IMP_ISP_Tuning_SetOSDBlock_Sec(T23ISPOSDBlockAttr *);
extern int IMP_ISP_MultiCamera_Tuning_SetOSDAttr(int, T23ISPOSDAttr *);
extern int IMP_ISP_MultiCamera_Tuning_SetOSDBlock(int, T23ISPOSDBlockAttr *);
extern int IMP_ISP_Tuning_SetDrawBlock(T23ISPDrawBlockAttr *);
extern int IMP_ISP_Tuning_SetMaskBlock(T23ISPMaskBlockAttr *);
extern int IMP_ISP_Tuning_GetOSDAttr(T23ISPOSDAttr *);
extern int IMP_ISP_Tuning_GetMaskBlock(T23ISPMaskBlockAttr *);
extern int IMP_ISP_Tuning_SetDrawBlock_Sec(T23ISPDrawBlockAttr *);
extern int IMP_ISP_Tuning_SetMaskBlock_Sec(T23ISPMaskBlockAttr *);
extern int IMP_ISP_Tuning_GetOSDAttr_Sec(T23ISPOSDAttr *);
extern int IMP_ISP_Tuning_GetMaskBlock_Sec(T23ISPMaskBlockAttr *);
extern int IMP_ISP_MultiCamera_Tuning_GetOSDAttr(int, T23ISPOSDAttr *);
extern int IMP_ISP_MultiCamera_Tuning_GetMaskBlock(int, T23ISPMaskBlockAttr *);
extern int IMP_ISP_MultiCamera_Tuning_SetDrawBlock(int, T23ISPDrawBlockAttr *);
extern int IMP_ISP_MultiCamera_Tuning_SetMaskBlock(int, T23ISPMaskBlockAttr *);

typedef struct {
    int busy;
    int shown;
    IMPIspOsdAttrAsm attr;
    uint8_t *virt;                  /* picture copy in rmem */
    uint32_t phys;
    uint32_t size;
} T23IspOsdSlot;

static pthread_mutex_t isp_osd_lock = PTHREAD_MUTEX_INITIALIZER;
static int isp_osd_ready;
static int isp_osd_pool_size = 0x100000;
static T23IspOsdSlot isp_osd[T23_ISP_OSD_CHANNELS][T23_ISP_OSD_PICS];

static int valid_slot(int chn, int handle)
{
    return chn >= 0 && chn < T23_ISP_OSD_CHANNELS && handle >= 0 &&
           handle < T23_ISP_OSD_PICS;
}

/* Program one slot into the ISP.  Called with isp_osd_lock held. */
static int notify_isp(int handle, T23IspOsdSlot *slot)
{
    T23ISPOSDAttr osd_attr = slot->attr.stsinglepicAttr.chnOSDAttr;
    T23ISPOSDBlockAttr block = slot->attr.stsinglepicAttr.pic;
    int sensor = slot->attr.stsinglepicAttr.sensornum;

    block.pinum = (uint8_t)handle;
    block.osd_enable = (uint8_t)(slot->shown ? 1 : 0);
    block.osd_image = (char *)(uintptr_t)slot->phys;
    if (sensor == 0) {
        return IMP_ISP_Tuning_SetOSDAttr(&osd_attr) < 0 ||
               IMP_ISP_Tuning_SetOSDBlock(&block) < 0 ? -1 : 0;
    }
    if (sensor == 1) {
        return IMP_ISP_Tuning_SetOSDAttr_Sec(&osd_attr) < 0 ||
               IMP_ISP_Tuning_SetOSDBlock_Sec(&block) < 0 ? -1 : 0;
    }
    if (sensor == 2) {
        return IMP_ISP_MultiCamera_Tuning_SetOSDAttr(2, &osd_attr) < 0 ||
               IMP_ISP_MultiCamera_Tuning_SetOSDBlock(2, &block) < 0 ? -1 : 0;
    }
    return -1;
}

static void free_slot_picture(T23IspOsdSlot *slot)
{
    if (slot->phys)
        DMA_FreePhys(slot->phys);
    slot->virt = NULL;
    slot->phys = 0;
    slot->size = 0;
}

int IMP_OSD_Init_ISP(void)
{
    /* The OEM pre-allocates one rmem pool of IMP_OSD_SetPoolSize_ISP bytes;
     * pictures get their own rmem buffers here instead. */
    pthread_mutex_lock(&isp_osd_lock);
    if (!isp_osd_ready) {
        memset(isp_osd, 0, sizeof(isp_osd));
        isp_osd_ready = 1;
    }
    pthread_mutex_unlock(&isp_osd_lock);
    return 0;
}

void IMP_OSD_Exit_ISP(void)
{
    pthread_mutex_lock(&isp_osd_lock);
    for (int c = 0; c < T23_ISP_OSD_CHANNELS; c++) {
        for (int h = 0; h < T23_ISP_OSD_PICS; h++) {
            T23IspOsdSlot *slot = &isp_osd[c][h];

            if (slot->busy && slot->shown) {
                slot->shown = 0;
                (void)notify_isp(h, slot);
            }
            free_slot_picture(slot);
        }
    }
    memset(isp_osd, 0, sizeof(isp_osd));
    isp_osd_ready = 0;
    pthread_mutex_unlock(&isp_osd_lock);
}

int IMP_OSD_SetPoolSize_ISP(int size)
{
    if (size <= 0)
        return -1;
    isp_osd_pool_size = size;
    return 0;
}

int IMP_OSD_CreateRgn_ISP(int chn, IMPIspOsdAttrAsm *attr)
{
    int handle;

    /* OEM: the attribute is given later with IMP_OSD_SetRgnAttr_PicISP; a
     * non-NULL one here is an error. */
    if (chn < 0 || chn >= T23_ISP_OSD_CHANNELS || attr)
        return -1;
    pthread_mutex_lock(&isp_osd_lock);
    if (!isp_osd_ready) {
        pthread_mutex_unlock(&isp_osd_lock);
        return -1;
    }
    for (handle = 0; handle < T23_ISP_OSD_PICS; handle++) {
        if (!isp_osd[chn][handle].busy)
            break;
    }
    if (handle == T23_ISP_OSD_PICS) {
        pthread_mutex_unlock(&isp_osd_lock);
        return -1;
    }
    memset(&isp_osd[chn][handle], 0, sizeof(isp_osd[chn][handle]));
    isp_osd[chn][handle].busy = 1;
    isp_osd[chn][handle].attr.stsinglepicAttr.pic.pinum = T23_ISP_OSD_HIDDEN;
    pthread_mutex_unlock(&isp_osd_lock);
    return handle;
}

static int picture_size(const IMPIspOsdAttrAsm *attr)
{
    uint32_t size = (uint32_t)attr->stsinglepicAttr.pic.osd_stride *
                    attr->stsinglepicAttr.pic.osd_height;

    return size ? (int)size : -1;
}

static int fits_frame(int chn, const IMPIspOsdAttrAsm *attr)
{
    /* OEM fsgetframeparam(): the picture must lie inside the frame of the
     * sensor's main FrameSource channel */
    const T23ISPOSDBlockAttr *pic = &attr->stsinglepicAttr.pic;
    IMPFSChnAttr fs;

    memset(&fs, 0, sizeof(fs));
    if (IMP_FrameSource_GetChnAttr(chn * 3, &fs) != 0 || fs.picWidth <= 0 ||
        fs.picHeight <= 0)
        return 1;                   /* channel not created yet: no check */
    return (int)pic->osd_left + pic->osd_width <= fs.picWidth &&
           (int)pic->osd_top + pic->osd_height <= fs.picHeight;
}

int IMP_OSD_SetRgnAttr_PicISP(int chn, int handle, IMPIspOsdAttrAsm *attr)
{
    T23IspOsdSlot *slot;
    int size;

    if (!valid_slot(chn, handle) || !attr ||
        attr->stsinglepicAttr.sensornum < chn || !fits_frame(chn, attr))
        return -1;
    size = picture_size(attr);
    if (size < 0)
        return -1;
    pthread_mutex_lock(&isp_osd_lock);
    slot = &isp_osd[chn][handle];
    if (!isp_osd_ready || !slot->busy || attr->type != ISP_OSD_REG_PIC) {
        pthread_mutex_unlock(&isp_osd_lock);
        return -1;
    }
    if (slot->size != (uint32_t)size) {
        IMPDMABufferInfo info;

        free_slot_picture(slot);
        memset(&info, 0, sizeof(info));
        if (DMA_AllocDescriptor(&info, size, "isp-osd") != 0 ||
            !info.virt_addr || !info.phys_addr) {
            pthread_mutex_unlock(&isp_osd_lock);
            return -1;
        }
        slot->virt = (uint8_t *)(uintptr_t)info.virt_addr;
        slot->phys = info.phys_addr;
        slot->size = (uint32_t)size;
    }
    slot->attr = *attr;
    if (attr->stsinglepicAttr.pic.osd_image)
        memcpy(slot->virt, attr->stsinglepicAttr.pic.osd_image, (size_t)size);
    else
        memset(slot->virt, 0, (size_t)size);
    DMA_RmemFlushCache(slot->virt, (uint32_t)size, 1);
    /* a new picture stays hidden until IMP_OSD_ShowRgn_ISP */
    slot->shown = 0;
    (void)notify_isp(handle, slot);
    pthread_mutex_unlock(&isp_osd_lock);
    return 0;
}

int IMP_OSD_GetRgnAttr_ISPPic(int chn, int handle, IMPIspOsdAttrAsm *attr)
{
    if (!valid_slot(chn, handle) || !attr)
        return -1;
    pthread_mutex_lock(&isp_osd_lock);
    if (!isp_osd_ready || !isp_osd[chn][handle].busy) {
        pthread_mutex_unlock(&isp_osd_lock);
        return -1;
    }
    *attr = isp_osd[chn][handle].attr;
    attr->stsinglepicAttr.pic.osd_enable =
        (uint8_t)(isp_osd[chn][handle].shown ? 1 : 0);
    pthread_mutex_unlock(&isp_osd_lock);
    return 0;
}

int IMP_OSD_UpdateRgnAttrData_ISP(int chn, int handle,
                                  IMPIspOsdAttrAsm *attr)
{
    T23IspOsdSlot *slot;
    int ret;

    if (!valid_slot(chn, handle) || !attr)
        return -1;
    pthread_mutex_lock(&isp_osd_lock);
    slot = &isp_osd[chn][handle];
    /* OEM: only the picture bytes change, the size must stay the same */
    if (!isp_osd_ready || !slot->busy || !slot->virt ||
        picture_size(attr) != (int)slot->size ||
        !attr->stsinglepicAttr.pic.osd_image) {
        pthread_mutex_unlock(&isp_osd_lock);
        return -1;
    }
    memcpy(slot->virt, attr->stsinglepicAttr.pic.osd_image, slot->size);
    DMA_RmemFlushCache(slot->virt, slot->size, 1);
    ret = notify_isp(handle, slot);
    pthread_mutex_unlock(&isp_osd_lock);
    return ret;
}

int IMP_OSD_ShowRgn_ISP(int chn, int handle, int show)
{
    T23IspOsdSlot *slot;
    int ret;

    if (!valid_slot(chn, handle))
        return -1;
    pthread_mutex_lock(&isp_osd_lock);
    slot = &isp_osd[chn][handle];
    if (!isp_osd_ready || !slot->busy || !slot->phys) {
        pthread_mutex_unlock(&isp_osd_lock);
        return -1;
    }
    slot->shown = !!show;
    ret = notify_isp(handle, slot);
    pthread_mutex_unlock(&isp_osd_lock);
    return ret;
}

int IMP_OSD_DestroyRgn_ISP(int chn, int handle)
{
    T23IspOsdSlot *slot;

    if (!valid_slot(chn, handle))
        return -1;
    pthread_mutex_lock(&isp_osd_lock);
    slot = &isp_osd[chn][handle];
    if (!isp_osd_ready || !slot->busy) {
        pthread_mutex_unlock(&isp_osd_lock);
        return -1;
    }
    if (slot->shown && slot->phys) {
        slot->shown = 0;
        (void)notify_isp(handle, slot);
    }
    free_slot_picture(slot);
    memset(slot, 0, sizeof(*slot));
    pthread_mutex_unlock(&isp_osd_lock);
    return 0;
}

/* ---- ISP line/rect/cover regions (IMP_OSD_SetRgnAttr_ISP family) ------ */

static pthread_mutex_t isp_draw_lock = PTHREAD_MUTEX_INITIALIZER;
static int isp_draw_show[3];

/* OEM: remember `show`, hand the ISP block over unchanged (its own enable
 * field decides), OSD_REG_ISP_PIC is refused (use the *_PicISP calls). */
static int set_rgn_attr_isp(int sensor, IMPOSDRgnAttr *attr, int show)
{
    int ret;

    if (!attr)
        return T23_OSD_ERR_PARAM;
    if (attr->type < OSD_REG_ISP_PIC || attr->type > OSD_REG_ISP_COVER)
        return T23_OSD_ERR_NOT_SUPPORT;
    pthread_mutex_lock(&isp_draw_lock);
    isp_draw_show[sensor] = !!show;
    if (attr->type == OSD_REG_ISP_LINE_RECT) {
        int (*set)(T23ISPDrawBlockAttr *) = sensor == 0
            ? IMP_ISP_Tuning_SetDrawBlock : IMP_ISP_Tuning_SetDrawBlock_Sec;

        ret = set(&attr->osdispdraw.stDrawAttr) >= 0
                  ? 0 : T23_OSD_ERR_NOT_SUPPORT;
    } else if (attr->type == OSD_REG_ISP_COVER) {
        int (*set)(T23ISPMaskBlockAttr *) = sensor == 0
            ? IMP_ISP_Tuning_SetMaskBlock : IMP_ISP_Tuning_SetMaskBlock_Sec;

        ret = set(&attr->osdispdraw.stCoverAttr) >= 0 ? 0 : -1;
    } else {
        ret = -1;
    }
    pthread_mutex_unlock(&isp_draw_lock);
    return ret;
}

int IMP_OSD_SetRgnAttr_ISP(IMPOSDRgnAttr *attr, int show)
{
    return set_rgn_attr_isp(0, attr, show);
}

int IMP_OSD_SetRgnAttr_ISP_Sec(IMPOSDRgnAttr *attr, int show)
{
    return set_rgn_attr_isp(1, attr, show);
}

static int get_rgn_attr_isp(int sensor, IMPOSDRgnAttr *attr, int *show)
{
    T23ISPOSDAttr osd_attr;
    int ret;

    if (!attr || !show)
        return -1;
    memset(&osd_attr, 0, sizeof(osd_attr));
    if (sensor == 0)
        ret = IMP_ISP_Tuning_GetOSDAttr(&osd_attr) < 0 ||
              IMP_ISP_Tuning_GetMaskBlock(&attr->osdispdraw.stCoverAttr) < 0
                  ? -1 : 0;
    else if (sensor == 1)
        ret = IMP_ISP_Tuning_GetOSDAttr_Sec(&osd_attr) < 0 ||
              IMP_ISP_Tuning_GetMaskBlock_Sec(&attr->osdispdraw.stCoverAttr) < 0
                  ? -1 : 0;
    else if (sensor == 2)
        ret = IMP_ISP_MultiCamera_Tuning_GetOSDAttr(sensor, &osd_attr) < 0 ||
              IMP_ISP_MultiCamera_Tuning_GetMaskBlock(
                  sensor, &attr->osdispdraw.stCoverAttr) < 0 ? -1 : 0;
    else
        ret = -1;
    if (ret == 0) {
        pthread_mutex_lock(&isp_draw_lock);
        *show = isp_draw_show[sensor];
        pthread_mutex_unlock(&isp_draw_lock);
    }
    return ret;
}

int IMP_OSD_GetRgnAttr_ISP(IMPOSDRgnAttr *attr, int *show)
{
    return get_rgn_attr_isp(0, attr, show);
}

int IMP_OSD_GetRgnAttr_ISP_Sec(IMPOSDRgnAttr *attr, int *show)
{
    return get_rgn_attr_isp(1, attr, show);
}

/* OEM: the multi-camera form goes to IMP_ISP_MultiCamera_Tuning_Set{Draw,
 * Mask}Block(vi, ...) for every sensor; NULL attr is IMP_ERR_OSD_NULL_PTR,
 * an ISP picture region (use the *_PicISP calls) or another type
 * IMP_ERR_OSD_NOT_SUPPORT, a failing ISP call -1. (The OEM also returns
 * IMP_ERR_OSD_UNEXIST before IMP_OSD is set up; OpenIMP has no such state
 * here.) */
int IMP_OSD_MultiCamera_SetRgnAttr_ISP(int vi, IMPOSDRgnAttr *attr, int show)
{
    int ret;

    if (!attr)
        return T23_OSD_ERR_NULL_PTR;
    if (attr->type < OSD_REG_ISP_PIC || attr->type > OSD_REG_ISP_COVER)
        return T23_OSD_ERR_NOT_SUPPORT;
    if (vi < 0 || vi > 2)
        return -1;
    pthread_mutex_lock(&isp_draw_lock);
    isp_draw_show[vi] = !!show;
    if (attr->type == OSD_REG_ISP_LINE_RECT)
        ret = IMP_ISP_MultiCamera_Tuning_SetDrawBlock(
                  vi, &attr->osdispdraw.stDrawAttr) >= 0 ? 0 : -1;
    else if (attr->type == OSD_REG_ISP_COVER)
        ret = IMP_ISP_MultiCamera_Tuning_SetMaskBlock(
                  vi, &attr->osdispdraw.stCoverAttr) >= 0 ? 0 : -1;
    else
        ret = T23_OSD_ERR_NOT_SUPPORT;
    pthread_mutex_unlock(&isp_draw_lock);
    return ret;
}

int IMP_OSD_MultiCamera_GetRgnAttr_ISP(int vi, IMPOSDRgnAttr *attr, int *show)
{
    if (vi < 0 || vi > 2)
        return -1;
    return get_rgn_attr_isp(vi, attr, show);
}
