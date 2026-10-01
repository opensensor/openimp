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
#include "openimp_t23_helix_bridge.h"

#define T23_VBM_ALIGN 4096u

_Static_assert(sizeof(IMPEncoderYuvIn) == 0x3c, "IMPEncoderYuvIn ABI mismatch");
_Static_assert(sizeof(IMPEncoderYuvOut) == 8, "IMPEncoderYuvOut ABI mismatch");

typedef struct {
    T23HelixBridge bridge;
    pthread_mutex_t lock;
} T23YuvEncoder;

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
    OpenIMP_T23_HelixExit(&encoder->bridge);
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
    uint32_t aligned_w, aligned_h, value[5];
    int result;

    if (!encoder || !cfg || ((cfg->x | cfg->y | cfg->w | cfg->h) & 1u) ||
        cfg->x + cfg->w > encoder->bridge.width ||
        cfg->y + cfg->h > encoder->bridge.height)
        return -1;
    aligned_w = (encoder->bridge.width + 15u) & ~15u;
    aligned_h = (encoder->bridge.height + 15u) & ~15u;
    if (cfg->x >= 510u || cfg->y >= 510u ||
        aligned_w - cfg->x - cfg->w >= 510u ||
        aligned_h - cfg->y - cfg->h >= 510u)
        return -1;
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
 * argument: dst must hold the worst case.  Unlike the OEM call, whose header
 * marks q unsupported, q (1..100) selects the quantisation tables. */
int IMP_Encoder_InputJpege(uint8_t *src, uint8_t *dst, int src_w, int src_h,
                           int q, int *stream_length)
{
    HWFrameBuffer frame;
    HWStreamBuffer stream;
    uint8_t *padded = NULL;
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
    memset(&frame, 0, sizeof(frame));
    frame.width = (uint32_t)src_w;
    frame.height = (uint32_t)src_h;
    frame.pixfmt = PIX_FMT_NV12;
    if (src_h % 16) {
        /* HW_Encoder_Encode_NV12_JPEG expects the framesource layout, whose
         * chroma plane follows a 16-aligned luma plane. */
        size_t padded_luma = (size_t)src_w * (((size_t)src_h + 15u) & ~15u);

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
    result = HW_Encoder_Encode_NV12_JPEG(&frame, &stream,
                                         q >= 1 && q <= 100 ? (uint32_t)q
                                                            : 75u);
    free(padded);
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
