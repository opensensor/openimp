/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef OPENIMP_AVC_H
#define OPENIMP_AVC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OpenIMPAVCEncoder OpenIMPAVCEncoder;

#define OPENIMP_AVC_PIXFMT_NV12 10u

typedef enum OpenIMPAVCRateControl {
    OPENIMP_AVC_RATE_FIXQP = 0,
    OPENIMP_AVC_RATE_CBR = 1,
    OPENIMP_AVC_RATE_VBR = 2,
} OpenIMPAVCRateControl;

typedef enum OpenIMPAVCProfile {
    OPENIMP_AVC_PROFILE_BASELINE = 66,
    OPENIMP_AVC_PROFILE_MAIN = 77,
    OPENIMP_AVC_PROFILE_HIGH = 100,
} OpenIMPAVCProfile;

typedef struct OpenIMPAVCConfig {
    uint32_t width;
    uint32_t height;
    uint32_t fps_num;
    uint32_t fps_den;
    uint32_t bitrate;
    uint32_t gop_length;
    uint32_t stream_buffer_count;
    uint32_t stream_buffer_size;
    uint8_t profile;
    uint8_t rate_control;
    uint8_t initial_qp;
    uint8_t min_qp;
    uint8_t max_qp;
    uint8_t entropy_coding;
} OpenIMPAVCConfig;

typedef struct OpenIMPAVCFrame {
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    uint32_t size;
    uint32_t physical_address;
    uintptr_t virtual_address;
    uint64_t timestamp;
    void *cookie;
} OpenIMPAVCFrame;

typedef struct OpenIMPAVCPacket {
    const uint8_t *data;
    uint32_t length;
    uint32_t physical_address;
    uint64_t timestamp;
    int keyframe;
    void *cookie;
    void *private_stream;
    void *private_user;
} OpenIMPAVCPacket;

/* Create an AVC encoder backed by the same AL/AVPU core used by the IMP API. */
int OpenIMP_AVC_Create(OpenIMPAVCEncoder **encoder,
                       const OpenIMPAVCConfig *config);
int OpenIMP_AVC_Destroy(OpenIMPAVCEncoder *encoder);

/* Submit one physically contiguous NV12 frame. The source must remain owned
 * by the caller until its packet is dequeued and released. */
int OpenIMP_AVC_Submit(OpenIMPAVCEncoder *encoder,
                       const OpenIMPAVCFrame *frame);
int OpenIMP_AVC_Dequeue(OpenIMPAVCEncoder *encoder,
                        OpenIMPAVCPacket *packet, uint32_t timeout_ms);
int OpenIMP_AVC_Release(OpenIMPAVCEncoder *encoder,
                        OpenIMPAVCPacket *packet);
int OpenIMP_AVC_RequestIDR(OpenIMPAVCEncoder *encoder);

/* Update the target bitrate for subsequent frames. The caller must serialize
 * this with Submit/Dequeue/Release for the same encoder. */
int OpenIMP_AVC_SetBitrate(OpenIMPAVCEncoder *encoder, uint32_t bitrate);

/* Resolve a contiguous DMA-BUF to the Ingenic AVPU bus address. Import all
 * capture buffers before the first Submit: the legacy AVPU driver permits a
 * single open channel and the codec claims it lazily on first use. */
int OpenIMP_AVC_ImportDMABuf(int dma_buf_fd, uint32_t size,
                             uint32_t *physical_address);

#ifdef __cplusplus
}
#endif

#endif /* OPENIMP_AVC_H */
