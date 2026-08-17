/* SPDX-License-Identifier: LGPL-2.1-or-later */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "codec.h"
#include <openimp/openimp_avc.h>

#define OPENIMP_AVC_PARAM_SIZE 0x794u
#define OPENIMP_AVC_TYPE_AVC 0u
#define OPENIMP_AVC_GET_DMA_PHY _IOWR('q', 18, struct openimp_avc_dma_info)
#define OPENIMP_AVC_EXTERNAL_FRAME_MAGIC 0x56344c32u

struct openimp_avc_dma_info {
    uint32_t fd;
    uint32_t size;
    uint32_t physical_address;
};

typedef struct OpenIMPAVCRawFrame {
    int32_t index;
    int32_t pool_index;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    uint32_t size;
    uint32_t physical_address;
    uint32_t virtual_address;
#if defined(PLATFORM_T41)
    uint32_t direct_physical_address;
#endif
    void *pool;
    int64_t timestamp;
    uint32_t external_frame_magic;
} OpenIMPAVCRawFrame;

typedef struct OpenIMPAVCRawStream {
    uint32_t physical_address;
    uint32_t virtual_address;
    uint32_t length;
    uint64_t timestamp;
    uint32_t frame_type;
    uint32_t slice_type;
    uint32_t reserved[8];
} OpenIMPAVCRawStream;

_Static_assert(offsetof(OpenIMPAVCRawFrame, width) == 0x08,
               "codec frame width ABI mismatch");
_Static_assert(offsetof(OpenIMPAVCRawFrame, physical_address) == 0x18,
               "codec frame physical-address ABI mismatch");
#if defined(PLATFORM_T41)
_Static_assert(offsetof(OpenIMPAVCRawFrame, timestamp) == 0x28,
               "T41 codec frame timestamp ABI mismatch");
#endif
_Static_assert(offsetof(OpenIMPAVCRawStream, timestamp) == 0x10,
               "codec stream timestamp ABI mismatch");

struct OpenIMPAVCEncoder {
    void *codec;
    OpenIMPAVCConfig config;
    OpenIMPAVCRawFrame frame;
    void *submitted_cookie;
    void *dequeued_stream;
    void *dequeued_user;
    int submitted;
};

static int openimp_avc_config_valid(const OpenIMPAVCConfig *config)
{
    if (!config || !config->width || !config->height ||
        config->width > UINT16_MAX || config->height > UINT16_MAX ||
        !config->fps_num || !config->fps_den || !config->bitrate ||
        !config->gop_length || config->fps_num > UINT16_MAX ||
        config->fps_den > UINT16_MAX / 1000u ||
        config->stream_buffer_count > 16u ||
        (config->profile != OPENIMP_AVC_PROFILE_BASELINE &&
         config->profile != OPENIMP_AVC_PROFILE_MAIN &&
         config->profile != OPENIMP_AVC_PROFILE_HIGH) ||
        config->rate_control > OPENIMP_AVC_RATE_VBR ||
        !config->initial_qp || config->initial_qp > 51u ||
        !config->min_qp || config->min_qp > config->initial_qp ||
        config->initial_qp > config->max_qp || config->max_qp > 51u ||
        config->entropy_coding > 1u)
        return 0;
    return 1;
}

static void openimp_avc_fill_params(uint8_t *params,
                                    const OpenIMPAVCConfig *config)
{
    uint32_t profile = (OPENIMP_AVC_TYPE_AVC << 24) | config->profile;
    uint32_t rate_control = config->rate_control;
    uint16_t width = (uint16_t)config->width;
    uint16_t height = (uint16_t)config->height;
    uint16_t fps_num = (uint16_t)config->fps_num;
    uint16_t fps_den = (uint16_t)(config->fps_den * 1000u);
    uint16_t initial_qp = config->initial_qp;
    uint16_t max_qp = config->max_qp;

    AL_Codec_Encode_SetDefaultParam(params);
    memcpy(params + 0x08, &width, sizeof(width));
    memcpy(params + 0x0a, &height, sizeof(height));
    memcpy(params + 0x0c, &width, sizeof(width));
    memcpy(params + 0x0e, &height, sizeof(height));
    memcpy(params + 0x20, &profile, sizeof(profile));
    memcpy(params + 0x6c, &rate_control, sizeof(rate_control));
    memcpy(params + 0x78, &fps_num, sizeof(fps_num));
    memcpy(params + 0x7a, &fps_den, sizeof(fps_den));
    memcpy(params + 0x7c, &config->bitrate, sizeof(config->bitrate));
    memcpy(params + 0xb0, &config->gop_length,
           sizeof(config->gop_length));
    memcpy(params + 0x84, &initial_qp, sizeof(initial_qp));
    memcpy(params + 0x86, &config->min_qp, sizeof(uint8_t));
    memcpy(params + 0x88, &max_qp, sizeof(max_qp));
}

#if !defined(PLATFORM_T41)
static int openimp_avc_is_idr(const uint8_t *stream, uint32_t length)
{
    uint32_t offset;

    if (!stream || length < 5u)
        return 0;
    for (offset = 0; offset + 4u < length; ++offset) {
        uint32_t nal_offset;
        uint8_t nal_type;

        if (stream[offset] || stream[offset + 1u])
            continue;
        if (stream[offset + 2u] == 1u)
            nal_offset = offset + 3u;
        else if (!stream[offset + 2u] && stream[offset + 3u] == 1u)
            nal_offset = offset + 4u;
        else
            continue;
        if (nal_offset >= length)
            break;
        nal_type = stream[nal_offset] & 0x1fu;
        if (nal_type == 5u)
            return 1;
        if (nal_type == 1u)
            return 0;
    }
    return 0;
}
#endif

int OpenIMP_AVC_Create(OpenIMPAVCEncoder **encoder,
                       const OpenIMPAVCConfig *config)
{
    OpenIMPAVCEncoder *created;
    uint8_t params[OPENIMP_AVC_PARAM_SIZE];
    uint32_t stream_count;

    if (!encoder || !openimp_avc_config_valid(config))
        return -EINVAL;
    *encoder = NULL;
    created = calloc(1, sizeof(*created));
    if (!created)
        return -ENOMEM;
    memset(params, 0, sizeof(params));
    openimp_avc_fill_params(params, config);
    if (AL_Codec_Encode_Create(&created->codec, params) != 0)
        goto fail;
    stream_count = config->stream_buffer_count
        ? config->stream_buffer_count : 4u;
    if (AL_Codec_Encode_SetStreamBufferCount(created->codec,
                                              (int)stream_count) != 0)
        goto fail_codec;
    if (config->stream_buffer_size &&
        AL_Codec_Encode_SetStreamBufferSize(created->codec,
                    (int)config->stream_buffer_size) != 0)
        goto fail_codec;
    if (AL_Codec_Encode_SetEntropyMode(created->codec,
                                       config->entropy_coding ? 2 : 1) != 0)
        goto fail_codec;
    created->config = *config;
    created->frame.index = -1;
    created->frame.pool_index = -1;
    *encoder = created;
    return 0;

fail_codec:
    AL_Codec_Encode_Destroy(created->codec);
fail:
    free(created);
    return -EIO;
}

int OpenIMP_AVC_Destroy(OpenIMPAVCEncoder *encoder)
{
    int ret;

    if (!encoder || encoder->submitted || encoder->dequeued_stream)
        return -EBUSY;
    ret = AL_Codec_Encode_Destroy(encoder->codec);
    free(encoder);
    return ret == 0 ? 0 : -EIO;
}

int OpenIMP_AVC_Submit(OpenIMPAVCEncoder *encoder,
                       const OpenIMPAVCFrame *frame)
{
    if (!encoder || !frame || encoder->submitted ||
        encoder->dequeued_stream || !frame->physical_address ||
        !frame->virtual_address ||
        frame->virtual_address > UINT32_MAX ||
        frame->width != encoder->config.width ||
        frame->height != encoder->config.height ||
        frame->pixel_format != OPENIMP_AVC_PIXFMT_NV12 ||
        frame->size < frame->width * frame->height * 3u / 2u)
        return -EINVAL;

    encoder->frame.width = frame->width;
    encoder->frame.height = frame->height;
    encoder->frame.pixel_format = frame->pixel_format;
    encoder->frame.size = frame->size;
    encoder->frame.physical_address = frame->physical_address;
    encoder->frame.virtual_address = (uint32_t)frame->virtual_address;
#if defined(PLATFORM_T41)
    encoder->frame.direct_physical_address = frame->physical_address;
#endif
    encoder->frame.timestamp = (int64_t)frame->timestamp;
    encoder->frame.external_frame_magic = OPENIMP_AVC_EXTERNAL_FRAME_MAGIC;
    encoder->submitted_cookie = frame->cookie;
    if (AL_Codec_Encode_Process(encoder->codec, &encoder->frame,
                                frame->cookie) != 0) {
        encoder->submitted_cookie = NULL;
        return -EIO;
    }
    encoder->submitted = 1;
    return 0;
}

int OpenIMP_AVC_Dequeue(OpenIMPAVCEncoder *encoder,
                        OpenIMPAVCPacket *packet, uint32_t timeout_ms)
{
    struct timespec delay = { 0, 1000000L };
    OpenIMPAVCRawStream *raw;
    uint32_t attempt;
    uint32_t attempts;
    void *stream = NULL;
    void *user = NULL;

    if (!encoder || !packet || !encoder->submitted ||
        encoder->dequeued_stream)
        return -EINVAL;
    attempts = timeout_ms ? timeout_ms : 1u;
    for (attempt = 0; attempt < attempts; ++attempt) {
        int ret = AL_Codec_Encode_GetStream(encoder->codec, &stream, &user);

        if (ret == 0 && stream)
            break;
        if (ret > 0)
            return -EIO;
        if (attempt + 1u < attempts)
            while (nanosleep(&delay, &delay) != 0 && errno == EINTR)
                ;
    }
    if (!stream)
        return -EAGAIN;

    raw = (OpenIMPAVCRawStream *)stream;
    memset(packet, 0, sizeof(*packet));
    packet->data = (const uint8_t *)(uintptr_t)raw->virtual_address;
    packet->length = raw->length;
    packet->physical_address = raw->physical_address;
    packet->timestamp = raw->timestamp;
#if defined(PLATFORM_T41)
    packet->keyframe = raw->frame_type == 0u;
#else
    packet->keyframe = openimp_avc_is_idr(packet->data, packet->length);
#endif
    packet->cookie = encoder->submitted_cookie;
    packet->private_stream = stream;
    packet->private_user = user;
    encoder->dequeued_stream = stream;
    encoder->dequeued_user = user;
    return 0;
}

int OpenIMP_AVC_Release(OpenIMPAVCEncoder *encoder,
                        OpenIMPAVCPacket *packet)
{
    int ret;

    if (!encoder || !packet ||
        packet->private_stream != encoder->dequeued_stream ||
        packet->private_user != encoder->dequeued_user)
        return -EINVAL;
    ret = AL_Codec_Encode_ReleaseStream(encoder->codec,
                                        encoder->dequeued_stream,
                                        encoder->dequeued_user);
    encoder->submitted = 0;
    encoder->dequeued_stream = NULL;
    encoder->dequeued_user = NULL;
    encoder->submitted_cookie = NULL;
    memset(packet, 0, sizeof(*packet));
    return ret == 0 ? 0 : -EIO;
}

int OpenIMP_AVC_RequestIDR(OpenIMPAVCEncoder *encoder)
{
    if (!encoder)
        return -EINVAL;
    return AL_Codec_Encode_RequestIDR(encoder->codec) == 0 ? 0 : -EIO;
}

int OpenIMP_AVC_SetBitrate(OpenIMPAVCEncoder *encoder, uint32_t bitrate)
{
    if (!encoder || !bitrate)
        return -EINVAL;
    if (bitrate > INT_MAX)
        return -ERANGE;
    if (AL_Codec_Encode_SetBitRate(encoder->codec, (int)bitrate,
                                   (int)bitrate) != 0)
        return -EIO;
    encoder->config.bitrate = bitrate;
    return 0;
}

int OpenIMP_AVC_ImportDMABuf(int dma_buf_fd, uint32_t size,
                             uint32_t *physical_address)
{
    struct openimp_avc_dma_info info;
    int avpu_fd;
    int ret;

    if (dma_buf_fd < 0 || !size || !physical_address)
        return -EINVAL;
    avpu_fd = open("/dev/avpu", O_RDWR | O_CLOEXEC);
    if (avpu_fd < 0)
        return -errno;
    memset(&info, 0, sizeof(info));
    info.fd = (uint32_t)dma_buf_fd;
    info.size = size;
    ret = ioctl(avpu_fd, OPENIMP_AVC_GET_DMA_PHY, &info);
    if (ret != 0)
        ret = -errno;
    else if (!info.physical_address)
        ret = -EIO;
    else {
        *physical_address = info.physical_address;
        ret = 0;
    }
    close(avpu_fd);
    return ret;
}
