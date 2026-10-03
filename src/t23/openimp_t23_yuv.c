/* T23 unbound encoder API: IMP_Encoder_Yuv*, IMP_Encoder_Vbm* and
 * IMP_Encoder_InputJpege.
 *
 * The OEM T23 libimp 1.3.0 exports these for callers that encode frames they
 * produced themselves (timps uses them for its software 90/270 rotate).
 * OpenIMP already drives every T23 H.264 stream through the OEM YuvEncode,
 * isolated in openimp-t23-helixd, so a YUV encoder handle is simply one more
 * Helix bridge session with caller-supplied rate control.  Vbm buffers come
 * from the OpenIMP rmem allocator, and InputJpege uses the software baseline
 * JPEG encoder in hw_encoder.c.
 */

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <imp/imp_common.h>
#include <imp/imp_encoder.h>

#include "dma_alloc.h"
#include "hw_encoder.h"
#include "imp_log_int.h"
#include "t30/helix_jpeg.h"
#include "t30/t30_helix_encoder.h"
#include "openimp_t23_helix_bridge.h"

#define T23_VBM_ALIGN 4096u

_Static_assert(sizeof(IMPEncoderYuvIn) == 0x3c, "IMPEncoderYuvIn ABI mismatch");
_Static_assert(sizeof(IMPEncoderYuvOut) == 8, "IMPEncoderYuvOut ABI mismatch");

typedef struct {
    T23HelixBridge bridge;
    pthread_mutex_t lock;
    /* native Helix backend (NULL: the OEM worker in bridge is used) */
    T30HelixEncoder *native;
    uint32_t width, height;
    /* macroblock-aligned NV12 copy for a frame the VPU cannot read in
     * place (height not a multiple of 16, or not in reserved memory) */
    void *staging;
    uint32_t staging_size;
} T23YuvEncoder;

/* Same backend choice as the bound channels (codec-t40.c): the native Helix
 * encoder unless OPENIMP_T23_ENCODER=worker asks for the OEM helper and it
 * is installed.  The open stack ships no helper, so without this the
 * unbound encoder (timps' software 90/270 rotate) could never start. */
static int t23_yuv_native_wanted(void)
{
    const char *value = getenv("OPENIMP_T23_ENCODER");

    if (value && strcmp(value, "worker") == 0 &&
        OpenIMP_T23_HelixHelperAvailable())
        return 0;
    return 1;
}

/* IMPEncoderYuvIn -> the native encoder's parameters, the same fields the
 * bound channels pass (codec-t40.c codec_store_rc_extras). */
static void t23_yuv_native_params(HWEncoderParams *hw, uint32_t width,
                                  uint32_t height, const IMPEncoderYuvIn *in)
{
    const IMPEncoderAttrRcMode *mode = &in->mode;

    memset(hw, 0, sizeof(*hw));
    hw->codec_type = IMP_ENC_TYPE_AVC;
    hw->width = width;
    hw->height = height;
    hw->fps_num = in->outFrmRate.frmRateNum ? in->outFrmRate.frmRateNum : 25u;
    hw->fps_den = in->outFrmRate.frmRateDen ? in->outFrmRate.frmRateDen : 1u;
    hw->gop_length = in->maxGop ? in->maxGop : 25u;
    switch (mode->rcMode) {
    case IMP_ENC_RC_MODE_FIXQP:
        hw->rc_mode = HW_RC_MODE_FIXQP;
        hw->qp = mode->attrH264FixQp.qp;
        hw->min_qp = hw->max_qp = hw->qp;
        hw->bitrate = 2000000u;
        break;
    case IMP_ENC_RC_MODE_VBR:
    case IMP_ENC_RC_MODE_SMART:
        hw->rc_mode = HW_RC_MODE_VBR;
        hw->bitrate = mode->attrH264Vbr.maxBitRate * 1000u;
        hw->min_qp = mode->attrH264Vbr.minQp;
        hw->max_qp = mode->attrH264Vbr.maxQp;
        hw->static_time = mode->attrH264Vbr.staticTime;
        hw->change_pos = mode->attrH264Vbr.changePos;
        hw->quality_level = mode->attrH264Vbr.qualityLvl;
        hw->frm_qp_step = mode->attrH264Vbr.frmQPStep;
        hw->gop_qp_step = mode->attrH264Vbr.gopQPStep;
        hw->bias_level = mode->attrH264Vbr.iBiasLvl;
        hw->rc_flags = HW_RC_FLAG_APP |
            (mode->attrH264Vbr.gopRelation ? HW_RC_FLAG_GOP_RELATION : 0u) |
            (mode->rcMode == IMP_ENC_RC_MODE_SMART ? HW_RC_FLAG_SMART : 0u);
        break;
    case IMP_ENC_RC_MODE_CBR:
    default:
        hw->rc_mode = HW_RC_MODE_CBR;
        hw->bitrate = mode->attrH264Cbr.outBitRate * 1000u;
        hw->min_qp = mode->attrH264Cbr.minQp;
        hw->max_qp = mode->attrH264Cbr.maxQp;
        hw->frm_qp_step = mode->attrH264Cbr.frmQPStep;
        hw->gop_qp_step = mode->attrH264Cbr.gopQPStep;
        hw->bias_level = mode->attrH264Cbr.iBiasLvl;
        hw->rc_flags = HW_RC_FLAG_APP |
            (mode->attrH264Cbr.adaptiveMode ? HW_RC_FLAG_ADAPTIVE : 0u) |
            (mode->attrH264Cbr.gopRelation ? HW_RC_FLAG_GOP_RELATION : 0u);
        break;
    }
    if (!hw->bitrate)
        hw->bitrate = 2000000u;
    if (hw->rc_mode != HW_RC_MODE_FIXQP) {
        /* start in the middle of the allowed QP range */
        uint32_t lo = hw->min_qp, hi = hw->max_qp;

        if (lo > hi) {
            hw->min_qp = hi;
            hw->max_qp = lo;
        }
        hw->qp = (hw->min_qp + hw->max_qp + 1u) / 2u;
    }
}

static int t23_yuv_native_init(T23YuvEncoder *encoder, uint32_t width,
                               uint32_t height, const IMPEncoderYuvIn *in)
{
    HWEncoderParams hw;
    uint32_t aligned_h = (height + 15u) & ~15u;

    if ((width & 15u) || width > 255u * 16u || height > 255u * 16u) {
        IMP_LOG_ERR("Encoder", "IMP_Encoder_YuvInit: %ux%u unsupported (width "
                    "must be a multiple of 16, both at most 4080)", width,
                    height);
        return -1;
    }
    t23_yuv_native_params(&hw, width, height, in);
    if (OpenIMP_T30_HelixCreate(&encoder->native, &hw) != 0) {
        encoder->native = NULL;
        IMP_LOG_ERR("Encoder", "IMP_Encoder_YuvInit: native Helix encoder "
                    "for %ux%u not created", width, height);
        return -1;
    }
    encoder->width = width;
    encoder->height = height;
    encoder->staging_size = width * aligned_h * 3u / 2u;
    return 0;
}

/* Hand the VPU a macroblock-aligned NV12 picture in reserved memory: the
 * caller's frame when it already is one, else a copy (packed NV12 with the
 * chroma plane right after width * height luma bytes, like the OEM call). */
static int t23_yuv_native_frame(T23YuvEncoder *encoder,
                                const IMPFrameInfo *in, IMPFrameInfo *out)
{
    uint32_t w = encoder->width, h = encoder->height;
    uint32_t aligned_h = (h + 15u) & ~15u;
    const uint8_t *src = (const uint8_t *)(uintptr_t)in->virAddr;
    uint8_t *dst;

    *out = *in;
    out->pixfmt = 0;            /* packed NV12 by the YuvEncode contract */
    if (h == aligned_h && in->phyAddr &&
        in->size >= encoder->staging_size &&
        DMA_VirtToPhys((const void *)(uintptr_t)in->virAddr) == in->phyAddr)
        return 0;
    if (in->size < w * h * 3u / 2u)
        return -1;
    if (!encoder->staging) {
        encoder->staging = IMP_Encoder_VbmAlloc(encoder->staging_size,
                                                T23_VBM_ALIGN);
        if (!encoder->staging)
            return -1;
        memset(encoder->staging, 0, encoder->staging_size);
    }
    dst = encoder->staging;
    memcpy(dst, src, (size_t)w * h);
    memcpy(dst + (size_t)w * aligned_h, src + (size_t)w * h,
           (size_t)w * h / 2u);
    out->virAddr = (uint32_t)(uintptr_t)dst;
    out->phyAddr = DMA_VirtToPhys(dst);
    out->size = encoder->staging_size;
    return 0;
}

int IMP_Encoder_YuvInit(void **h, int inWidth, int inHeight,
                        IMPEncoderYuvIn *encIn)
{
    T23YuvEncoder *encoder;

    if (!h)
        return -1;
    *h = NULL;
    /* The OEM YuvInit accepts PT_H264 only (PT_H265 is a log-and-fail stub
     * there); refuse early instead of spawning a worker to learn that. */
    if (!encIn || inWidth <= 0 || inHeight <= 0 || (inWidth & 1) ||
        (inHeight & 1) || encIn->type != PT_H264)
        return -1;
    encoder = calloc(1, sizeof(*encoder));
    if (!encoder)
        return -1;
    if (t23_yuv_native_wanted()) {
        /* the native encoder enforces its geometry (width a multiple of
         * 16) and clamps the rate-control fields like the bound channels */
        if (t23_yuv_native_init(encoder, (uint32_t)inWidth,
                                (uint32_t)inHeight, encIn) != 0) {
            free(encoder);
            return -1;
        }
        pthread_mutex_init(&encoder->lock, NULL);
        *h = encoder;
        return 0;
    }
    /* geometry and rate-control limits are enforced by the OEM YuvInit in
     * the worker; its failure fails this call */
    if (OpenIMP_T23_HelixInitYuv(&encoder->bridge, (uint32_t)inWidth,
                                 (uint32_t)inHeight, encIn) != 0) {
        free(encoder);
        return -1;
    }
    pthread_mutex_init(&encoder->lock, NULL);
    *h = encoder;
    return 0;
}

int IMP_Encoder_YuvEncode(void *h, IMPFrameInfo frame,
                          IMPEncoderYuvOut *encOut)
{
    T23YuvEncoder *encoder = h;
    uint32_t length;
    int result;

    if (encoder && encoder->native) {
        IMPFrameInfo input;
        HWStreamBuffer *stream = NULL;

        if (!encOut || !encOut->outAddr || !encOut->outLen ||
            !frame.virAddr || frame.width != encoder->width ||
            frame.height != encoder->height)
            return -1;
        pthread_mutex_lock(&encoder->lock);
        result = t23_yuv_native_frame(encoder, &frame, &input);
        if (result == 0)
            result = OpenIMP_T30_HelixEncode(encoder->native, &input,
                                             &stream);
        if (result == 0 && (!stream || !stream->virt_addr ||
                            stream->length > encOut->outLen)) {
            if (stream)
                IMP_LOG_ERR("Encoder", "YuvEncode: access unit %u exceeds "
                            "buffer %u", stream->length, encOut->outLen);
            result = -1;
        }
        if (result == 0) {
            memcpy(encOut->outAddr, (const void *)(uintptr_t)stream->virt_addr,
                   stream->length);
            encOut->outLen = stream->length;
        }
        pthread_mutex_unlock(&encoder->lock);
        return result == 0 ? 0 : -1;
    }
    if (!encoder || !encOut || !encOut->outAddr || !encOut->outLen ||
        !frame.virAddr || frame.size < encoder->bridge.input_size ||
        frame.width != encoder->bridge.width ||
        frame.height != encoder->bridge.height)
        return -1;
    length = encOut->outLen;
    pthread_mutex_lock(&encoder->lock);
    result = OpenIMP_T23_HelixEncodeInto(&encoder->bridge, &frame,
                                         encOut->outAddr, &length);
    pthread_mutex_unlock(&encoder->lock);
    if (result != 0)
        return -1;
    /* same contract as the OEM call: outAddr untouched, outLen = AU size */
    encOut->outLen = length;
    return 0;
}

int IMP_Encoder_YuvRequestIDR(void *h)
{
    T23YuvEncoder *encoder = h;
    int result;

    if (!encoder)
        return -1;
    pthread_mutex_lock(&encoder->lock);
    if (encoder->native)
        result = OpenIMP_T30_HelixRequestIDR(encoder->native);
    else
        result = OpenIMP_T23_HelixRequestIDR(&encoder->bridge);
    pthread_mutex_unlock(&encoder->lock);
    return result == 0 ? 0 : -1;
}

int IMP_Encoder_YuvExit(void *h)
{
    T23YuvEncoder *encoder = h;

    if (!encoder)
        return -1;
    pthread_mutex_lock(&encoder->lock);
    if (encoder->native) {
        OpenIMP_T30_HelixDestroy(encoder->native);
        encoder->native = NULL;
        if (encoder->staging)
            IMP_Encoder_VbmFree(encoder->staging);
    } else {
        OpenIMP_T23_HelixExit(&encoder->bridge);
    }
    pthread_mutex_unlock(&encoder->lock);
    pthread_mutex_destroy(&encoder->lock);
    free(encoder);
    return 0;
}

/* OEM IMP_Encoder_YuvSetCrop: even values inside the picture, every crop
 * margin below 510 pixels; i264e order {enable, x, w, y, h}. */
int IMP_Encoder_YuvSetCrop(void *h, IMPEncoderCropCfg *cfg)
{
    T23YuvEncoder *encoder = h;
    uint32_t aligned_w, aligned_h, value[5], pic_w, pic_h;
    int result;

    if (!encoder || !cfg)
        return -1;
    pic_w = encoder->native ? encoder->width : encoder->bridge.width;
    pic_h = encoder->native ? encoder->height : encoder->bridge.height;
    if (((cfg->x | cfg->y | cfg->w | cfg->h) & 1u) ||
        cfg->x + cfg->w > pic_w || cfg->y + cfg->h > pic_h)
        return -1;
    aligned_w = (pic_w + 15u) & ~15u;
    aligned_h = (pic_h + 15u) & ~15u;
    if (cfg->x >= 510u || cfg->y >= 510u ||
        aligned_w - cfg->x - cfg->w >= 510u ||
        aligned_h - cfg->y - cfg->h >= 510u)
        return -1;
    if (encoder->native) {
        /* SPS frame cropping: the same visible rectangle as the OEM i264e */
        pthread_mutex_lock(&encoder->lock);
        result = OpenIMP_T30_HelixSetCrop(encoder->native, cfg->enable != 0,
                                          cfg->x, cfg->y, cfg->w, cfg->h);
        pthread_mutex_unlock(&encoder->lock);
        return result == 0 ? 0 : -1;
    }
    value[0] = cfg->enable ? 1u : 0u;
    value[1] = cfg->x;
    value[2] = cfg->w;
    value[3] = cfg->y;
    value[4] = cfg->h;
    pthread_mutex_lock(&encoder->lock);
    result = OpenIMP_T23_HelixSetParam(&encoder->bridge, T23_I264E_CROP, 0,
                                       value, sizeof(value));
    pthread_mutex_unlock(&encoder->lock);
    return result == 0 ? 0 : -1;
}

int IMP_Encoder_YuvGetCrop(void *h, IMPEncoderCropCfg *cfg)
{
    T23YuvEncoder *encoder = h;
    uint32_t value[5];
    int result;

    if (!encoder || !cfg)
        return -1;
    memset(cfg, 0, sizeof(*cfg));
    if (encoder->native) {
        int enable;
        uint32_t x, y, w, h;

        pthread_mutex_lock(&encoder->lock);
        result = OpenIMP_T30_HelixGetCrop(encoder->native, &enable, &x, &y,
                                          &w, &h);
        pthread_mutex_unlock(&encoder->lock);
        if (result != 0)
            return -1;
        cfg->enable = enable;
        cfg->x = x;
        cfg->y = y;
        cfg->w = w;
        cfg->h = h;
        return 0;
    }
    memset(value, 0, sizeof(value));
    pthread_mutex_lock(&encoder->lock);
    result = OpenIMP_T23_HelixGetParam(&encoder->bridge, T23_I264E_CROP, 0,
                                       value, sizeof(value));
    pthread_mutex_unlock(&encoder->lock);
    if (result < 0)
        return -1;
    cfg->enable = value[0] != 0;
    cfg->x = value[1];
    cfg->w = value[2];
    cfg->y = value[3];
    cfg->h = value[4];
    return 0;
}

/* Physically contiguous, page-aligned buffers from the rmem allocator. */
void *IMP_Encoder_VbmAlloc(uint32_t size, uint32_t align)
{
    IMPDMABufferInfo info;

    if (!size || size > INT32_MAX || align > T23_VBM_ALIGN ||
        (align & (align - 1u)))
        return NULL;
    memset(&info, 0, sizeof(info));
    if (DMA_AllocDescriptor(&info, (int)size, "vbm") != 0 ||
        !info.virt_addr || !info.phys_addr)
        return NULL;
    if (align && (info.virt_addr & (align - 1u))) {
        DMA_FreePhys(info.phys_addr);
        return NULL;
    }
    return (void *)(uintptr_t)info.virt_addr;
}

void IMP_Encoder_VbmFree(void *vaddr)
{
    uint32_t physical;

    if (!vaddr)
        return;
    physical = DMA_VirtToPhys(vaddr);
    if (physical && physical != (uint32_t)(uintptr_t)vaddr)
        DMA_FreePhys(physical);
}

intptr_t IMP_Encoder_VbmV2P(intptr_t vaddr)
{
    uint32_t physical;

    if (!vaddr)
        return 0;
    physical = DMA_VirtToPhys((const void *)vaddr);
    /* DMA_VirtToPhys echoes addresses it does not own */
    return physical == (uint32_t)vaddr ? 0 : (intptr_t)physical;
}

intptr_t IMP_Encoder_VbmP2V(intptr_t paddr)
{
    return paddr ? (intptr_t)DMA_PhysToVirt((uint32_t)paddr) : 0;
}

/* Standalone JPEG of a packed NV12 frame (chroma right after the
 * src_w * src_h luma bytes).  Like the OEM call there is no output capacity
 * argument: dst must hold the worst case.  q (1..100) selects IJG-scaled
 * quantisation tables, as the OEM call does (MakeTables_Imp); other values
 * give quality 75.  The picture is coded on the Helix VPU, reading src in
 * place when it is VBM (rmem) memory, otherwise from a copy; the software
 * encoder (when built in) takes what the VPU cannot. */
int IMP_Encoder_InputJpege(uint8_t *src, uint8_t *dst, int src_w, int src_h,
                           int q, int *stream_length)
{
    HelixJpegFrame picture;
    HWStreamBuffer stream;
    uint8_t tables[128];
    uint32_t physical;
    size_t luma;
    size_t chroma;
    int result;

    if (stream_length)
        *stream_length = 0;
    if (!src || !dst || !stream_length || src_w <= 0 || src_h <= 0 ||
        (src_w & 1) || (src_h & 1) || src_w > 65535 || src_h > 65535)
        return -1;
    luma = (size_t)src_w * (size_t)src_h;
    chroma = luma / 2u;
    HelixJpeg_QualityTables(q >= 1 && q <= 100 ? (uint32_t)q : 75u, tables);
    physical = DMA_VirtToPhys(src);
    memset(&picture, 0, sizeof(picture));
    picture.virt_addr = (uint32_t)(uintptr_t)src;
    /* DMA_VirtToPhys echoes addresses it does not own */
    picture.phys_addr = physical != (uint32_t)(uintptr_t)src ? physical : 0u;
    picture.size = (uint32_t)(luma + chroma);
    picture.width = (uint32_t)src_w;
    picture.height = (uint32_t)src_h;
    picture.chroma_offset = (uint32_t)luma;
    picture.pixfmt = PIX_FMT_NV12;
    memset(&stream, 0, sizeof(stream));
    result = OpenIMP_HelixJpeg_Encode(&picture, tables, &stream);
#if OPENIMP_SW_JPEG
    if (result != 0) {
        HWFrameBuffer frame;
        uint8_t *padded = NULL;

        memset(&frame, 0, sizeof(frame));
        frame.width = (uint32_t)src_w;
        frame.height = (uint32_t)src_h;
        frame.pixfmt = PIX_FMT_NV12;
        if (src_h % 16) {
            /* HW_Encoder_Encode_NV12_JPEG expects the framesource layout,
             * whose chroma plane follows a 16-aligned luma plane. */
            size_t padded_luma = (size_t)src_w *
                                 (((size_t)src_h + 15u) & ~15u);

            padded = malloc(padded_luma + chroma);
            if (!padded)
                return -1;
            memcpy(padded, src, luma);
            memset(padded + luma, 0, padded_luma - luma);
            memcpy(padded + padded_luma, src + luma, chroma);
            frame.virt_addr = (uint32_t)(uintptr_t)padded;
            frame.size = (uint32_t)(padded_luma + chroma);
        } else {
            frame.virt_addr = (uint32_t)(uintptr_t)src;
            frame.size = (uint32_t)(luma + chroma);
        }
        memset(&stream, 0, sizeof(stream));
        result = HW_Encoder_Encode_NV12_JPEG_Tables(&frame, &stream, 75u,
                                                    tables);
        free(padded);
    }
#endif
    if (result != 0 || !stream.virt_addr || !stream.length ||
        stream.length > INT32_MAX) {
        free((void *)(uintptr_t)stream.virt_addr);
        return -1;
    }
    memcpy(dst, (const void *)(uintptr_t)stream.virt_addr, stream.length);
    *stream_length = (int)stream.length;
    free((void *)(uintptr_t)stream.virt_addr);
    return 0;
}
