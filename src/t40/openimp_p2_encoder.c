/* Standalone T40 public encoder lifecycle built on the recovered AL backend. */

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <imp/imp_common.h>
#include <imp/imp_encoder.h>

#define P2_MAX_GROUPS 8
#define P2_MAX_CHANNELS 8
#define P2_MAX_BINDS 16
#define P2_PARAM_SIZE 0x794
/* The T40 1.3.1 public ABI ends IMPEncoderStream after isVI and pads the
 * structure to 28 bytes.  openimp's cross-platform compatibility header also
 * carries newer stream-info fields after isVI, so sizeof(IMPEncoderStream)
 * must not be used when writing into a T40 caller's stack object. */
#define P2_T40_ENCODER_STREAM_ABI_SIZE 28u

typedef struct {
    uint32_t phys_addr;
    uint32_t virt_addr;
    uint32_t length;
    uint64_t timestamp;
    uint32_t frame_type;
    uint32_t slice_type;
    uint32_t reserved[8];
} P2HWStream;

/* The recovered codec only reads the first 0x20 bytes of a frame before its
 * software JPEG path.  JPEG channels share an encoder group with AVC in rvd;
 * the OEM graph fans one FrameSource frame out to both consumers, whereas the
 * P1 public GetFrame API is a dequeue.  Give the metadata-only JPEG fallback
 * its own frame descriptor so it cannot consume (and deadlock) AVC capture. */
typedef struct {
    int32_t index;
    int32_t pool_index;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    uint32_t size;
    uint32_t physical_address;
    uint32_t virtual_address;
    void *pool;
    int64_t timestamp;
} P2SyntheticFrame;

typedef struct {
    int active;
    IMPCell source;
    IMPCell destination;
} P2Bind;

typedef struct {
    int created;
    int registered;
    int receiving;
    int group;
    int source_channel;
    int codec_type;
    void *codec;
    void *raw_stream;
    void *codec_user;
    void *source_frame;
    P2SyntheticFrame synthetic_frame;
    IMPEncoderCHNAttr attr;
    IMPEncoderPack pack;
    uint32_t sequence;
    uint32_t stream_buf_size;
    int max_stream_count;
    int pool_id;
    uint64_t next_frame_due_us;
    pthread_mutex_t lock;
} P2EncoderChannel;

static int p2_initialized;
static int p2_groups[P2_MAX_GROUPS];
static P2EncoderChannel p2_channels[P2_MAX_CHANNELS];
static P2Bind p2_binds[P2_MAX_BINDS];
static pthread_mutex_t p2_state_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t p2_core_lock = PTHREAD_MUTEX_INITIALIZER;

static uint64_t p2_monotonic_us(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (uint64_t)now.tv_sec * 1000000u +
           (uint64_t)now.tv_nsec / 1000u;
}

static void p2_sleep_us(uint64_t delay_us)
{
    struct timespec delay;

    delay.tv_sec = (time_t)(delay_us / 1000000u);
    delay.tv_nsec = (long)((delay_us % 1000000u) * 1000u);
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR)
        ;
}

/* New-SDK callers consume one public pack per completed frame.  Classify the
 * actual Annex-B slice here, outside the AVPU completion thread, so Raptor can
 * gate a new RTSP client on a real IDR instead of treating every P picture as
 * a keyframe. */
static int p2_h264_stream_is_idr(const uint8_t *stream, uint32_t length)
{
    uint32_t offset;

    if (!stream || length < 5u)
        return 0;
    for (offset = 0; offset + 4u < length; offset++) {
        uint32_t nal_offset;
        uint8_t nal_type;

        if (stream[offset] != 0u || stream[offset + 1u] != 0u)
            continue;
        if (stream[offset + 2u] == 1u)
            nal_offset = offset + 3u;
        else if (stream[offset + 2u] == 0u && stream[offset + 3u] == 1u)
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

extern int AL_Codec_Encode_SetDefaultParam(void *param);
extern int AL_Codec_Encode_Create(void **codec, void *params);
extern int AL_Codec_Encode_Destroy(void *codec);
extern int AL_Codec_Encode_Process(void *codec, void *frame, void *user_data);
extern int AL_Codec_Encode_GetStream(void *codec, void **stream, void **user_data);
extern int AL_Codec_Encode_ReleaseStream(void *codec, void *stream, void *user_data);
extern int AL_Codec_Encode_RequestIDR(void *codec);
extern int IMP_FrameSource_GetFrame(int channel, void **frame);
extern int IMP_FrameSource_ReleaseFrame(int channel, void *frame);

static int p2_valid_group(int group)
{
    return group >= 0 && group < P2_MAX_GROUPS;
}

static int p2_valid_channel(int channel)
{
    return channel >= 0 && channel < P2_MAX_CHANNELS;
}

static int p2_cell_equal(const IMPCell *a, const IMPCell *b)
{
    return a->deviceID == b->deviceID && a->groupID == b->groupID &&
           a->outputID == b->outputID;
}

static int p2_find_source_channel(int encoder_group)
{
    IMPCell cursor = { DEV_ID_ENC, encoder_group, 0 };
    int depth;

    for (depth = 0; depth < P2_MAX_BINDS; depth++) {
        int i;
        int found = 0;

        if (cursor.deviceID == DEV_ID_FS)
            return cursor.groupID;
        for (i = 0; i < P2_MAX_BINDS; i++) {
            if (p2_binds[i].active &&
                p2_cell_equal(&p2_binds[i].destination, &cursor)) {
                cursor = p2_binds[i].source;
                found = 1;
                break;
            }
        }
        if (!found)
            break;
    }
    return encoder_group;
}

static uint32_t p2_attr_bitrate_kbps(const IMPEncoderCHNAttr *attr)
{
    switch (attr->rcAttr.attrRcMode.rcMode) {
    case IMP_ENC_RC_MODE_CBR:
        return attr->rcAttr.attrRcMode.attrCbr.uTargetBitRate;
    case IMP_ENC_RC_MODE_VBR:
        return attr->rcAttr.attrRcMode.attrVbr.uTargetBitRate;
    case IMP_ENC_RC_MODE_CAPPED_VBR:
        return attr->rcAttr.attrRcMode.attrCappedVbr.uTargetBitRate;
    case IMP_ENC_RC_MODE_CAPPED_QUALITY:
        return attr->rcAttr.attrRcMode.attrCappedQuality.uTargetBitRate;
    default:
        return 0;
    }
}

static void p2_codec_params(unsigned char *params, const IMPEncoderCHNAttr *attr)
{
    uint32_t profile = (uint32_t)attr->encAttr.profile;
    uint32_t codec_type = (profile >> 24) & 0xffu;
    uint32_t fps_num = attr->rcAttr.outFrmRate.frmRateNum;
    uint32_t fps_den = attr->rcAttr.outFrmRate.frmRateDen;
    uint32_t bitrate = p2_attr_bitrate_kbps(attr) * 1000u;
    int qp = 26;
    int min_qp = 15;
    int max_qp = 45;

    AL_Codec_Encode_SetDefaultParam(params);
    *(uint16_t *)(params + 0x08) = attr->encAttr.maxPicWidth;
    *(uint16_t *)(params + 0x0a) = attr->encAttr.maxPicHeight;
    *(uint16_t *)(params + 0x0c) = attr->encAttr.maxPicWidth;
    *(uint16_t *)(params + 0x0e) = attr->encAttr.maxPicHeight;
    *(uint32_t *)(params + 0x20) = profile;
    *(uint32_t *)(params + 0x6c) = (uint32_t)attr->rcAttr.attrRcMode.rcMode;
    *(uint16_t *)(params + 0x78) = (uint16_t)(fps_num ? fps_num : 25u);
    *(uint16_t *)(params + 0x7a) = (uint16_t)((fps_den ? fps_den : 1u) * 1000u);
    *(uint32_t *)(params + 0x7c) = bitrate;
    *(uint32_t *)(params + 0xb0) = attr->gopAttr.uGopLength;

    if (attr->rcAttr.attrRcMode.rcMode == IMP_ENC_RC_MODE_FIXQP) {
        qp = attr->rcAttr.attrRcMode.attrFixQp.iInitialQP;
    } else if (attr->rcAttr.attrRcMode.rcMode == IMP_ENC_RC_MODE_CBR) {
        qp = attr->rcAttr.attrRcMode.attrCbr.iInitialQP;
        min_qp = attr->rcAttr.attrRcMode.attrCbr.iMinQP;
        max_qp = attr->rcAttr.attrRcMode.attrCbr.iMaxQP;
    } else {
        qp = attr->rcAttr.attrRcMode.attrVbr.iInitialQP;
        min_qp = attr->rcAttr.attrRcMode.attrVbr.iMinQP;
        max_qp = attr->rcAttr.attrRcMode.attrVbr.iMaxQP;
    }
    if (codec_type == IMP_ENC_TYPE_JPEG && qp < 1)
        qp = 25;
    if (qp < 1 || qp > 51)
        qp = 26;
    if (min_qp < 1 || min_qp > 51)
        min_qp = 15;
    if (max_qp < 1 || max_qp > 51)
        max_qp = 45;
    *(uint16_t *)(params + 0x84) = (uint16_t)qp;
    *(uint8_t *)(params + 0x86) = (uint8_t)min_qp;
    *(uint16_t *)(params + 0x88) = (uint16_t)max_qp;
}

int EncoderInit(void)
{
    int i;

    pthread_mutex_lock(&p2_state_lock);
    if (!p2_initialized) {
        memset(p2_groups, 0, sizeof(p2_groups));
        memset(p2_channels, 0, sizeof(p2_channels));
        memset(p2_binds, 0, sizeof(p2_binds));
        for (i = 0; i < P2_MAX_CHANNELS; i++) {
            p2_channels[i].group = -1;
            p2_channels[i].source_channel = -1;
            p2_channels[i].max_stream_count = 4;
            p2_channels[i].pool_id = -1;
            pthread_mutex_init(&p2_channels[i].lock, NULL);
        }
        p2_initialized = 1;
    }
    pthread_mutex_unlock(&p2_state_lock);
    return 0;
}

int EncoderExit(void)
{
    int i;

    if (!p2_initialized)
        return 0;
    for (i = P2_MAX_CHANNELS - 1; i >= 0; i--)
        if (p2_channels[i].created)
            return -1;
    pthread_mutex_lock(&p2_state_lock);
    for (i = 0; i < P2_MAX_CHANNELS; i++)
        pthread_mutex_destroy(&p2_channels[i].lock);
    p2_initialized = 0;
    pthread_mutex_unlock(&p2_state_lock);
    return 0;
}

int IMP_System_Bind(IMPCell *source, IMPCell *destination)
{
    int i;

    if (!source || !destination)
        return -1;
    EncoderInit();
    pthread_mutex_lock(&p2_state_lock);
    for (i = 0; i < P2_MAX_BINDS; i++) {
        if (p2_binds[i].active &&
            p2_cell_equal(&p2_binds[i].source, source) &&
            p2_cell_equal(&p2_binds[i].destination, destination)) {
            pthread_mutex_unlock(&p2_state_lock);
            return 0;
        }
    }
    for (i = 0; i < P2_MAX_BINDS; i++) {
        if (!p2_binds[i].active) {
            p2_binds[i].active = 1;
            p2_binds[i].source = *source;
            p2_binds[i].destination = *destination;
            pthread_mutex_unlock(&p2_state_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&p2_state_lock);
    return -1;
}

int IMP_System_UnBind(IMPCell *source, IMPCell *destination)
{
    int i;

    if (!source || !destination)
        return -1;
    pthread_mutex_lock(&p2_state_lock);
    for (i = 0; i < P2_MAX_BINDS; i++) {
        if (p2_binds[i].active &&
            p2_cell_equal(&p2_binds[i].source, source) &&
            p2_cell_equal(&p2_binds[i].destination, destination)) {
            memset(&p2_binds[i], 0, sizeof(p2_binds[i]));
            pthread_mutex_unlock(&p2_state_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&p2_state_lock);
    return -1;
}

int IMP_System_GetBindbyDest(IMPCell *destination, IMPCell *source)
{
    int i;

    if (!source || !destination)
        return -1;
    pthread_mutex_lock(&p2_state_lock);
    for (i = 0; i < P2_MAX_BINDS; i++) {
        if (p2_binds[i].active &&
            p2_cell_equal(&p2_binds[i].destination, destination)) {
            *source = p2_binds[i].source;
            pthread_mutex_unlock(&p2_state_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&p2_state_lock);
    return -1;
}

int IMP_Encoder_CreateGroup(int group)
{
    if (!p2_valid_group(group))
        return -1;
    EncoderInit();
    pthread_mutex_lock(&p2_state_lock);
    p2_groups[group] = 1;
    pthread_mutex_unlock(&p2_state_lock);
    return 0;
}

int IMP_Encoder_DestroyGroup(int group)
{
    int i;

    if (!p2_valid_group(group) || !p2_groups[group])
        return -1;
    for (i = 0; i < P2_MAX_CHANNELS; i++)
        if (p2_channels[i].registered && p2_channels[i].group == group)
            return -1;
    p2_groups[group] = 0;
    return 0;
}

int IMP_Encoder_CreateChn(int channel, IMPEncoderCHNAttr *attr)
{
    P2EncoderChannel *ch;
    unsigned char params[P2_PARAM_SIZE];

    if (!p2_valid_channel(channel) || !attr)
        return -1;
    EncoderInit();
    ch = &p2_channels[channel];
    pthread_mutex_lock(&ch->lock);
    if (ch->created) {
        pthread_mutex_unlock(&ch->lock);
        return -1;
    }
    p2_codec_params(params, attr);
    if (AL_Codec_Encode_Create(&ch->codec, params) != 0) {
        pthread_mutex_unlock(&ch->lock);
        return -1;
    }
    ch->attr = *attr;
    ch->codec_type = (((uint32_t)attr->encAttr.profile) >> 24) & 0xffu;
    ch->created = 1;
    ch->group = -1;
    ch->source_channel = -1;
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

int IMP_Encoder_DestroyChn(int channel)
{
    P2EncoderChannel *ch;

    if (!p2_valid_channel(channel))
        return -1;
    ch = &p2_channels[channel];
    pthread_mutex_lock(&ch->lock);
    if (!ch->created || ch->registered || ch->raw_stream || ch->source_frame) {
        pthread_mutex_unlock(&ch->lock);
        return -1;
    }
    if (ch->codec)
        AL_Codec_Encode_Destroy(ch->codec);
    ch->codec = NULL;
    ch->created = 0;
    ch->receiving = 0;
    ch->group = -1;
    ch->source_channel = -1;
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

int IMP_Encoder_RegisterChn(int group, int channel)
{
    P2EncoderChannel *ch;

    if (!p2_valid_group(group) || !p2_valid_channel(channel))
        return -1;
    ch = &p2_channels[channel];
    pthread_mutex_lock(&ch->lock);
    if (!ch->created || ch->registered) {
        pthread_mutex_unlock(&ch->lock);
        return -1;
    }
    ch->registered = 1;
    ch->group = group;
    ch->source_channel = p2_find_source_channel(group);
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

int IMP_Encoder_UnRegisterChn(int channel)
{
    P2EncoderChannel *ch;

    if (!p2_valid_channel(channel))
        return -1;
    ch = &p2_channels[channel];
    pthread_mutex_lock(&ch->lock);
    if (!ch->created || !ch->registered || ch->receiving || ch->raw_stream) {
        pthread_mutex_unlock(&ch->lock);
        return -1;
    }
    ch->registered = 0;
    ch->group = -1;
    ch->source_channel = -1;
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

int IMP_Encoder_StartRecvPic(int channel)
{
    P2EncoderChannel *ch;

    if (!p2_valid_channel(channel))
        return -1;
    ch = &p2_channels[channel];
    pthread_mutex_lock(&ch->lock);
    if (!ch->registered) {
        pthread_mutex_unlock(&ch->lock);
        return -1;
    }
    ch->source_channel = p2_find_source_channel(ch->group);
    ch->next_frame_due_us = 0;
    ch->receiving = 1;
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

int IMP_Encoder_StopRecvPic(int channel)
{
    P2EncoderChannel *ch;

    if (!p2_valid_channel(channel))
        return -1;
    ch = &p2_channels[channel];
    pthread_mutex_lock(&ch->lock);
    if (!ch->created) {
        pthread_mutex_unlock(&ch->lock);
        return -1;
    }
    ch->receiving = 0;
    ch->next_frame_due_us = 0;
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

int IMP_Encoder_PollingStream(int channel, uint32_t timeout_ms)
{
    P2EncoderChannel *ch;
    void *frame = NULL;
    void *stream = NULL;
    void *user = NULL;
    unsigned int retry;
    unsigned int retries;
    uint32_t fps_num;
    uint32_t fps_den;
    uint64_t interval_us;
    uint64_t now_us;
    uint64_t wait_us;
    uint64_t timeout_us;
    int result = -1;

    if (!p2_valid_channel(channel))
        return -1;
    ch = &p2_channels[channel];
    pthread_mutex_lock(&ch->lock);
    if (ch->raw_stream) {
        pthread_mutex_unlock(&ch->lock);
        return 0;
    }
    if (!ch->created || !ch->registered || !ch->receiving || !ch->codec) {
        pthread_mutex_unlock(&ch->lock);
        return -1;
    }
    fps_num = ch->attr.rcAttr.outFrmRate.frmRateNum;
    fps_den = ch->attr.rcAttr.outFrmRate.frmRateDen;
    if (!fps_num || !fps_den) {
        fps_num = 25u;
        fps_den = 1u;
    }
    interval_us = (1000000ull * fps_den) / fps_num;
    if (!interval_us)
        interval_us = 1u;
    if (interval_us > 60000000u)
        interval_us = 60000000u;
    now_us = p2_monotonic_us();
    wait_us = ch->next_frame_due_us > now_us
        ? ch->next_frame_due_us - now_us : 0u;
    pthread_mutex_unlock(&ch->lock);

    timeout_us = (uint64_t)timeout_ms * 1000u;
    if (wait_us && (!timeout_ms || wait_us > timeout_us)) {
        if (timeout_us)
            p2_sleep_us(timeout_us);
        return -1;
    }
    if (wait_us)
        p2_sleep_us(wait_us);

    pthread_mutex_lock(&ch->lock);
    if (!ch->created || !ch->registered || !ch->receiving || !ch->codec ||
        ch->raw_stream) {
        pthread_mutex_unlock(&ch->lock);
        return ch->raw_stream ? 0 : -1;
    }
    now_us = p2_monotonic_us();
    ch->next_frame_due_us = now_us + interval_us;
    pthread_mutex_unlock(&ch->lock);

    pthread_mutex_lock(&p2_core_lock);
    if (ch->codec_type == IMP_ENC_TYPE_JPEG) {
        memset(&ch->synthetic_frame, 0, sizeof(ch->synthetic_frame));
        ch->synthetic_frame.index = -1;
        ch->synthetic_frame.pool_index = -1;
        ch->synthetic_frame.width = ch->attr.encAttr.maxPicWidth;
        ch->synthetic_frame.height = ch->attr.encAttr.maxPicHeight;
        ch->synthetic_frame.pixel_format = 10u; /* PIX_FMT_NV12 */
        frame = &ch->synthetic_frame;
    } else if (IMP_FrameSource_GetFrame(ch->source_channel, &frame) != 0) {
        goto done;
    }
    if (AL_Codec_Encode_Process(ch->codec, frame, frame) != 0)
        goto done;
    retries = timeout_ms ? timeout_ms : 2000u;
    if (retries < 2000u)
        retries = 2000u;
    for (retry = 0; retry < retries; retry++) {
        if (AL_Codec_Encode_GetStream(ch->codec, &stream, &user) == 0)
            break;
        usleep(1000);
    }
    if (!stream)
        goto done;
    pthread_mutex_lock(&ch->lock);
    ch->source_frame = frame;
    ch->raw_stream = stream;
    ch->codec_user = user;
    pthread_mutex_unlock(&ch->lock);
    frame = NULL;
    stream = NULL;
    result = 0;

done:
    if (stream)
        AL_Codec_Encode_ReleaseStream(ch->codec, stream, user);
    if (frame && frame != &ch->synthetic_frame)
        IMP_FrameSource_ReleaseFrame(ch->source_channel, frame);
    pthread_mutex_unlock(&p2_core_lock);
    return result;
}

int IMP_Encoder_GetStream(int channel, IMPEncoderStream *stream, int block)
{
    P2EncoderChannel *ch;
    P2HWStream *raw;
    int is_idr;

    if (!p2_valid_channel(channel) || !stream)
        return -1;
    ch = &p2_channels[channel];
    if (!ch->raw_stream && block && IMP_Encoder_PollingStream(channel, 1000) != 0)
        return -1;
    pthread_mutex_lock(&ch->lock);
    raw = (P2HWStream *)ch->raw_stream;
    if (!raw) {
        pthread_mutex_unlock(&ch->lock);
        return -1;
    }
    is_idr = ch->codec_type != IMP_ENC_TYPE_JPEG &&
        p2_h264_stream_is_idr((const uint8_t *)(uintptr_t)raw->virt_addr,
                              raw->length);
    memset(stream, 0, P2_T40_ENCODER_STREAM_ABI_SIZE);
    memset(&ch->pack, 0, sizeof(ch->pack));
    ch->pack.offset = 0;
    ch->pack.length = raw->length;
    ch->pack.timestamp = raw->timestamp
        ? (int64_t)raw->timestamp : (int64_t)p2_monotonic_us();
    ch->pack.frameEnd = true;
    ch->pack.sliceType = is_idr ? IMP_ENC_SLICE_I : IMP_ENC_SLICE_P;
    ch->pack.nalType.h264NalType = ch->codec_type == IMP_ENC_TYPE_JPEG
        ? IMP_H264_NAL_UNKNOWN
        : (is_idr ? IMP_H264_NAL_SLICE_IDR : IMP_H264_NAL_SLICE);
    stream->phyAddr = raw->phys_addr;
    stream->virAddr = raw->virt_addr;
    stream->streamSize = raw->length;
    stream->pack = &ch->pack;
    stream->packCount = 1;
    stream->seq = ch->sequence++;
    stream->isVI = false;
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

int IMP_Encoder_ReleaseStream(int channel, IMPEncoderStream *stream)
{
    P2EncoderChannel *ch;
    void *raw;
    void *user;
    void *frame;
    int result;

    if (!p2_valid_channel(channel) || !stream)
        return -1;
    ch = &p2_channels[channel];
    pthread_mutex_lock(&ch->lock);
    raw = ch->raw_stream;
    user = ch->codec_user;
    frame = ch->source_frame;
    ch->raw_stream = NULL;
    ch->codec_user = NULL;
    ch->source_frame = NULL;
    pthread_mutex_unlock(&ch->lock);
    if (!raw || !frame)
        return -1;
    result = AL_Codec_Encode_ReleaseStream(ch->codec, raw, user);
    if (frame != &ch->synthetic_frame &&
        IMP_FrameSource_ReleaseFrame(ch->source_channel, frame) != 0)
        result = -1;
    return result;
}

int IMP_Encoder_RequestIDR(int channel)
{
    if (!p2_valid_channel(channel) || !p2_channels[channel].codec)
        return -1;
    if (p2_channels[channel].codec_type == IMP_ENC_TYPE_JPEG)
        return 0;
    return AL_Codec_Encode_RequestIDR(p2_channels[channel].codec);
}

int IMP_Encoder_Query(int channel, IMPEncoderCHNStat *stat)
{
    if (!p2_valid_channel(channel) || !stat || !p2_channels[channel].created)
        return -1;
    memset(stat, 0, sizeof(*stat));
    stat->leftPics = p2_channels[channel].raw_stream ? 1u : 0u;
    stat->curPacks = p2_channels[channel].raw_stream ? 1u : 0u;
    return 0;
}

int IMP_Encoder_GetChnAttr(int channel, IMPEncoderChnAttr *attr)
{
    if (!p2_valid_channel(channel) || !attr || !p2_channels[channel].created)
        return -1;
    *attr = p2_channels[channel].attr;
    return 0;
}

int IMP_Encoder_SetDefaultParam(IMPEncoderChnAttr *attr, IMPEncoderProfile profile,
                                IMPEncoderRcMode rc_mode, int width, int height,
                                int fps_num, int fps_den, int gop_length,
                                int max_same_scene, int quality, int bitrate)
{
    uint32_t codec_type;

    if (!attr || width <= 0 || height <= 0 || fps_num <= 0 || fps_den <= 0)
        return -1;
    memset(attr, 0, sizeof(*attr));
    codec_type = ((uint32_t)profile >> 24) & 0xffu;
    attr->encAttr.profile = profile;
    attr->encAttr.level = 51;
    attr->encAttr.maxPicWidth = (uint16_t)width;
    attr->encAttr.maxPicHeight = (uint16_t)height;
    attr->encAttr.picFormat = 0x188u;
    attr->rcAttr.attrRcMode.rcMode = rc_mode;
    attr->rcAttr.outFrmRate.frmRateNum = (uint32_t)fps_num;
    attr->rcAttr.outFrmRate.frmRateDen = (uint32_t)fps_den;
    attr->gopAttr.uGopLength = (uint16_t)gop_length;
    attr->gopAttr.uMaxSameSenceCnt = (uint32_t)max_same_scene;
    if (codec_type == IMP_ENC_TYPE_JPEG || rc_mode == IMP_ENC_RC_MODE_FIXQP) {
        attr->rcAttr.attrRcMode.attrFixQp.iInitialQP =
            (int16_t)((quality >= 1 && quality <= 99) ? quality : 25);
    } else if (rc_mode == IMP_ENC_RC_MODE_CBR) {
        attr->rcAttr.attrRcMode.attrCbr.uTargetBitRate = (uint32_t)bitrate;
        attr->rcAttr.attrRcMode.attrCbr.iInitialQP = 26;
        attr->rcAttr.attrRcMode.attrCbr.iMinQP = 15;
        attr->rcAttr.attrRcMode.attrCbr.iMaxQP = 45;
    } else {
        attr->rcAttr.attrRcMode.attrVbr.uTargetBitRate = (uint32_t)bitrate;
        attr->rcAttr.attrRcMode.attrVbr.uMaxBitRate = (uint32_t)bitrate;
        attr->rcAttr.attrRcMode.attrVbr.iInitialQP = 26;
        attr->rcAttr.attrRcMode.attrVbr.iMinQP = 15;
        attr->rcAttr.attrRcMode.attrVbr.iMaxQP = 45;
    }
    return 0;
}

int IMP_Encoder_FlushStream(int channel)
{
    return p2_valid_channel(channel) && p2_channels[channel].created ? 0 : -1;
}

int IMP_Encoder_SetbufshareChn(int channel, int share_channel)
{
    return p2_valid_channel(channel) && p2_valid_channel(share_channel) ? 0 : -1;
}

int IMP_Encoder_SetJpegeQl(int channel, IMPEncoderJpegeQl *quality)
{
    return p2_valid_channel(channel) && quality ? 0 : -1;
}

int IMP_Encoder_SetMaxStreamCnt(int channel, int count)
{
    if (!p2_valid_channel(channel) || count <= 0)
        return -1;
    EncoderInit();
    p2_channels[channel].max_stream_count = count;
    return 0;
}

int IMP_Encoder_GetMaxStreamCnt(int channel, int *count)
{
    if (!p2_valid_channel(channel) || !count)
        return -1;
    EncoderInit();
    *count = p2_channels[channel].max_stream_count;
    return 0;
}

int IMP_Encoder_SetStreamBufSize(int channel, int size)
{
    if (!p2_valid_channel(channel) || size <= 0)
        return -1;
    EncoderInit();
    if (p2_channels[channel].created)
        return -1;
    p2_channels[channel].stream_buf_size = (uint32_t)size;
    return 0;
}

int IMP_Encoder_GetStreamBufSize(int channel, int *size)
{
    if (!p2_valid_channel(channel) || !size)
        return -1;
    EncoderInit();
    *size = (int)p2_channels[channel].stream_buf_size;
    return 0;
}

/* The compatibility header carries an obsolete third frame-rate argument;
 * T40 callers pass two.  Only the second argument is read, so the function is
 * ABI-correct for the T40 two-argument call while retaining a compilable
 * prototype in this standalone unit. */
int IMP_Encoder_SetChnFrmRate(int channel, IMPEncoderFrmRate *rate,
                              IMPEncoderFrmRate *unused)
{
    P2EncoderChannel *ch;

    (void)unused;
    if (!p2_valid_channel(channel) || !rate || !rate->frmRateNum ||
        !rate->frmRateDen)
        return -1;
    ch = &p2_channels[channel];
    pthread_mutex_lock(&ch->lock);
    if (!ch->created) {
        pthread_mutex_unlock(&ch->lock);
        return -1;
    }
    ch->attr.rcAttr.outFrmRate = *rate;
    ch->next_frame_due_us = 0;
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

int IMP_Encoder_GetChnFrmRate(int channel, IMPEncoderFrmRate *rate,
                              IMPEncoderFrmRate *unused)
{
    (void)unused;
    if (!p2_valid_channel(channel) || !rate || !p2_channels[channel].created)
        return -1;
    *rate = p2_channels[channel].attr.rcAttr.outFrmRate;
    return 0;
}

int IMP_Encoder_GetChnAttrRcMode(int channel, IMPEncoderAttrRcMode *mode)
{
    if (!p2_valid_channel(channel) || !mode || !p2_channels[channel].created)
        return -1;
    *mode = p2_channels[channel].attr.rcAttr.attrRcMode;
    return 0;
}

int IMP_Encoder_SetChnAttrRcMode(int channel, IMPEncoderAttrRcMode *mode)
{
    if (!p2_valid_channel(channel) || !mode || !p2_channels[channel].created)
        return -1;
    p2_channels[channel].attr.rcAttr.attrRcMode = *mode;
    return 0;
}

int IMP_Encoder_SetChnBitRate(int channel, int bitrate, int max_bitrate)
{
    IMPEncoderAttrRcMode *mode;
    uint32_t target;
    uint32_t maximum;

    if (!p2_valid_channel(channel) || bitrate <= 0 ||
        !p2_channels[channel].created)
        return -1;
    target = (uint32_t)bitrate / 1000U;
    maximum = max_bitrate > 0 ? (uint32_t)max_bitrate / 1000U : target;
    if (!target)
        target = 1;
    if (!maximum)
        maximum = target;
    mode = &p2_channels[channel].attr.rcAttr.attrRcMode;
    switch (mode->rcMode) {
    case IMP_ENC_RC_MODE_CBR:
        mode->attrCbr.uTargetBitRate = target;
        break;
    case IMP_ENC_RC_MODE_VBR:
        mode->attrVbr.uTargetBitRate = target;
        mode->attrVbr.uMaxBitRate = maximum;
        break;
    case IMP_ENC_RC_MODE_CAPPED_VBR:
        mode->attrCappedVbr.uTargetBitRate = target;
        mode->attrCappedVbr.uMaxBitRate = maximum;
        break;
    case IMP_ENC_RC_MODE_CAPPED_QUALITY:
        mode->attrCappedQuality.uTargetBitRate = target;
        mode->attrCappedQuality.uMaxBitRate = maximum;
        break;
    default:
        return -1;
    }
    return 0;
}

int IMP_Encoder_GetChnGopAttr(int channel, IMPEncoderGopAttr *gop)
{
    if (!p2_valid_channel(channel) || !gop || !p2_channels[channel].created)
        return -1;
    *gop = p2_channels[channel].attr.gopAttr;
    return 0;
}

int IMP_Encoder_SetChnGopAttr(int channel, IMPEncoderGopAttr *gop)
{
    if (!p2_valid_channel(channel) || !gop || !p2_channels[channel].created)
        return -1;
    p2_channels[channel].attr.gopAttr = *gop;
    return 0;
}

int IMP_Encoder_SetChnGopLength(int channel, int length)
{
    if (!p2_valid_channel(channel) || length <= 0 ||
        !p2_channels[channel].created)
        return -1;
    p2_channels[channel].attr.gopAttr.uGopLength = (uint16_t)length;
    return 0;
}

static void p2_set_qp_bounds(IMPEncoderAttrRcMode *mode, int minimum,
                             int maximum)
{
    if (mode->rcMode == IMP_ENC_RC_MODE_CBR) {
        mode->attrCbr.iMinQP = (int16_t)minimum;
        mode->attrCbr.iMaxQP = (int16_t)maximum;
    } else if (mode->rcMode == IMP_ENC_RC_MODE_VBR) {
        mode->attrVbr.iMinQP = (int16_t)minimum;
        mode->attrVbr.iMaxQP = (int16_t)maximum;
    } else if (mode->rcMode == IMP_ENC_RC_MODE_CAPPED_VBR) {
        mode->attrCappedVbr.iMinQP = (int16_t)minimum;
        mode->attrCappedVbr.iMaxQP = (int16_t)maximum;
    } else if (mode->rcMode == IMP_ENC_RC_MODE_CAPPED_QUALITY) {
        mode->attrCappedQuality.iMinQP = (int16_t)minimum;
        mode->attrCappedQuality.iMaxQP = (int16_t)maximum;
    }
}

int IMP_Encoder_SetChnQpBounds(int channel, int minimum, int maximum)
{
    if (!p2_valid_channel(channel) || minimum < 0 || maximum > 51 ||
        minimum > maximum || !p2_channels[channel].created)
        return -1;
    p2_set_qp_bounds(&p2_channels[channel].attr.rcAttr.attrRcMode,
                     minimum, maximum);
    return 0;
}

int IMP_Encoder_GetChnEncType(int channel, IMPEncoderEncType *type)
{
    if (!p2_valid_channel(channel) || !type || !p2_channels[channel].created)
        return -1;
    *type = (IMPEncoderEncType)p2_channels[channel].codec_type;
    return 0;
}

int IMP_Encoder_SetPool(int channel, int pool_id)
{
    if (!p2_valid_channel(channel) || pool_id < -1)
        return -1;
    EncoderInit();
    p2_channels[channel].pool_id = pool_id;
    return 0;
}

int IMP_Encoder_GetPool(int channel)
{
    if (!p2_valid_channel(channel))
        return -1;
    EncoderInit();
    return p2_channels[channel].pool_id;
}

int IMP_Encoder_GetFd(int channel)
{
    (void)channel;
    errno = ENOSYS;
    return -1;
}
