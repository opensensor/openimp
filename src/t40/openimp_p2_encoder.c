/* Standalone T40 public encoder lifecycle built on the recovered AL backend. */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* T21 and T30 expose the same legacy public encoder ABI and both use the
 * source-only Helix /dev/soc_vpu backend.  Keep that compatibility local to
 * the encoder translation unit: their ISP and FrameSource ioctls differ. */
#if defined(PLATFORM_T21)
#define PLATFORM_T30 1
#endif

#include "openimp_profile.h"
#include "trace_control.h"
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
#include "t23/openimp_t23_persist.h"
#endif

#include <imp/imp_common.h>
#include <imp/imp_encoder.h>

#define P2_MAX_GROUPS 8
#define P2_MAX_CHANNELS 8
#define P2_MAX_BINDS 16
#define P2_MAX_PUBLIC_PACKS 8
#define P2_PARAM_SIZE 0x794
/* T40 1.3.1 ends IMPEncoderStream after isVI and pads it to 28 bytes. T31
 * 1.1.6 includes the streamInfo/jpegInfo union, matching the compatibility
 * header. Write exactly the ABI selected for the target caller. */
#if defined(PLATFORM_T31) || defined(PLATFORM_T23) || defined(PLATFORM_T30)
#define P2_ENCODER_STREAM_ABI_SIZE sizeof(IMPEncoderStream)
#else
#define P2_ENCODER_STREAM_ABI_SIZE 28u
#endif

typedef enum {
    P2_CAPTURE_RELEASE_AFTER_SUBMIT,
    P2_CAPTURE_RELEASE_AFTER_COMPLETION,
} P2CaptureReleasePolicy;

static P2CaptureReleasePolicy p2_capture_release_policy(void)
{
#if defined(PLATFORM_T31) || defined(PLATFORM_T23) || defined(PLATFORM_T30)
    return P2_CAPTURE_RELEASE_AFTER_COMPLETION;
#elif defined(PLATFORM_T40) || defined(PLATFORM_T41)
    return P2_CAPTURE_RELEASE_AFTER_SUBMIT;
#else
#error "capture-buffer ownership must be established for this platform"
#endif
}

typedef struct {
    uint32_t phys_addr;
    uint32_t virt_addr;
    uint32_t length;
    uint64_t timestamp;
    uint32_t frame_type;
    uint32_t slice_type;
    uint32_t reserved[8];
} P2HWStream;

_Static_assert(offsetof(P2HWStream, timestamp) == 0x10,
               "hardware stream timestamp ABI mismatch");
_Static_assert(offsetof(P2HWStream, frame_type) == 0x18,
               "hardware stream frame type ABI mismatch");
_Static_assert(offsetof(P2HWStream, reserved) == 0x20,
               "hardware stream private metadata ABI mismatch");

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
#if defined(PLATFORM_T41)
    uint32_t direct_physical_address;
#endif
    void *pool;
    int64_t timestamp;
} P2SyntheticFrame;

#if defined(PLATFORM_T41)
_Static_assert(offsetof(P2SyntheticFrame, direct_physical_address) == 0x20,
               "T41 synthetic frame direct address ABI mismatch");
_Static_assert(offsetof(P2SyntheticFrame, pool) == 0x24,
               "T41 synthetic frame pool ABI mismatch");
_Static_assert(offsetof(P2SyntheticFrame, timestamp) == 0x28,
               "T41 synthetic frame timestamp ABI mismatch");
#endif

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
    IMPEncoderPack packs[P2_MAX_PUBLIC_PACKS];
    IMPEncoderJpegeQl jpeg_quality;
    uint32_t sequence;
    uint32_t stream_buf_size;
    int max_stream_count;
    int pool_id;
    int entropy_mode;
    int entropy_mode_set;
    int resize_mode;
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    IMPEncoderColor2GreyCfg color2grey;
    IMPEncoderROICfg roi[8];
    IMPEncoderAttrDenoise denoise;
    IMPEncoderSuperFrmCfg superframe;
    IMPEncoderH264TransCfg h264_transform;
    IMPEncoderH265TransCfg h265_transform;
    IMPEncoderQpgMode qpg_mode;
    int macroblock_rate_control;
#endif
    uint64_t next_frame_due_us;
    uint64_t output_timestamp_us;
    pthread_mutex_t lock;
} P2EncoderChannel;

static int p2_initialized;
static int p2_groups[P2_MAX_GROUPS];
static P2EncoderChannel p2_channels[P2_MAX_CHANNELS];
static P2Bind p2_binds[P2_MAX_BINDS];
static pthread_mutex_t p2_state_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t p2_core_lock = PTHREAD_MUTEX_INITIALIZER;

static void p2_startup_marker(const char *marker, size_t size)
{
    if (getenv("OPENIMP_STARTUP_TRACE"))
        (void)write(STDERR_FILENO, marker, size);
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    openimp_t23_persist_write(marker, size);
#endif
}

#define P2_STARTUP_MARKER(value) \
    p2_startup_marker((value), sizeof(value) - 1u)

static void p2_startup_trace(const char *format, ...)
{
    char message[256];
    va_list arguments;
    int length;

    if (!getenv("OPENIMP_STARTUP_TRACE")
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
        && !openimp_t23_persist_enabled()
#endif
    )
        return;
    va_start(arguments, format);
    length = vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    if (length > 0) {
        size_t size = (size_t)length;

        if (size >= sizeof(message))
            size = sizeof(message) - 1u;
        if (getenv("OPENIMP_STARTUP_TRACE")) {
            (void)write(STDERR_FILENO, message, size);
            (void)fsync(STDERR_FILENO);
        }
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
        openimp_t23_persist_write(message, size);
#endif
    }
}

static void p2_trace(const char *format, ...)
{
    char message[256];
    va_list arguments;
    int length;
    int trace_kmsg = openimp_debug_trace_enabled();
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    int trace_persist = openimp_t23_persist_enabled();
#else
    int trace_persist = 0;
#endif

    if (!trace_kmsg && !trace_persist)
        return;

    va_start(arguments, format);
    length = vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    if (length > 0) {
        size_t size = (size_t)length;

        if (size > sizeof(message))
            size = sizeof(message);
        if (trace_kmsg) {
            int fd = open("/dev/kmsg", O_WRONLY);

            if (fd >= 0) {
                write(fd, message, size);
                close(fd);
            }
        }
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
        openimp_t23_persist_write(message, size);
#endif
    }
}

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
#if !defined(PLATFORM_T41)
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
#endif

#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
static uint32_t p2_find_annexb_start4(const uint8_t *data, uint32_t offset,
                                      uint32_t length)
{
    while (offset + 4u <= length) {
        if (data[offset] == 0u && data[offset + 1u] == 0u &&
            data[offset + 2u] == 0u && data[offset + 3u] == 1u)
            return offset;
        offset++;
    }
    return length;
}

/* The legacy IMP ABI exposes one Annex-B NAL per pack.  Native Helix emits
 * SPS, PPS and the IDR slice into one owned allocation, so describe each NAL
 * as a separate view while retaining a single release owner. */
static uint32_t p2_fill_legacy_packs(P2EncoderChannel *channel,
                                     const P2HWStream *raw)
{
    const uint8_t *data = (const uint8_t *)(uintptr_t)raw->virt_addr;
    int64_t timestamp = raw->timestamp
        ? (int64_t)raw->timestamp : (int64_t)p2_monotonic_us();
    uint32_t count = 0;
    uint32_t begin;

    memset(channel->packs, 0, sizeof(channel->packs));
    if (!data || !raw->length)
        return 0;
    begin = p2_find_annexb_start4(data, 0, raw->length);
    if (channel->codec_type == IMP_ENC_TYPE_JPEG || begin != 0u) {
        channel->packs[0].phyAddr = raw->phys_addr;
        channel->packs[0].virAddr = raw->virt_addr;
        channel->packs[0].length = raw->length;
        channel->packs[0].timestamp = timestamp;
        channel->packs[0].frameEnd = true;
        channel->packs[0].dataType.h264Type = IMP_H264_NAL_UNKNOWN;
        return 1;
    }
    while (begin < raw->length && count < P2_MAX_PUBLIC_PACKS) {
        IMPEncoderPack *pack = &channel->packs[count];
        uint32_t next;
        uint32_t nal_header = begin + 4u;

        if (count + 1u == P2_MAX_PUBLIC_PACKS)
            next = raw->length;
        else
            next = p2_find_annexb_start4(data, nal_header, raw->length);
        if (next <= begin)
            break;
        pack->phyAddr = raw->phys_addr ? raw->phys_addr + begin : 0u;
        pack->virAddr = raw->virt_addr + begin;
        pack->length = next - begin;
        pack->timestamp = timestamp;
        pack->dataType.h264Type = nal_header < raw->length
            ? (IMPEncoderH264NaluType)(data[nal_header] & 0x1fu)
            : IMP_H264_NAL_UNKNOWN;
        count++;
        begin = next;
    }
    if (count)
        channel->packs[count - 1u].frameEnd = true;
    return count;
}
#endif

extern int AL_Codec_Encode_SetDefaultParam(void *param);
extern int AL_Codec_Encode_Create(void **codec, void *params);
extern int AL_Codec_Encode_SetStreamBufferCount(void *codec, int count);
extern int AL_Codec_Encode_SetStreamBufferSize(void *codec, int size);
extern int AL_Codec_Encode_SetFrameRate(void *codec, void *fps);
extern int AL_Codec_Encode_SetBitRate(void *codec, int target_bitrate,
                                     int max_bitrate);
extern int AL_Codec_Encode_SetRcParam(void *codec, void *rc_attr);
extern int AL_Codec_Encode_SetQpBounds(void *codec, int min_qp, int max_qp);
extern int AL_Codec_Encode_SetQpIPDelta(void *codec, int delta);
extern int AL_Codec_Encode_SetQp(void *codec, void *qp);
extern int AL_Codec_Encode_SetEntropyMode(void *codec, int mode);
extern int AL_Codec_Encode_SetGopParam(void *codec, void *gop);
extern int AL_Codec_Encode_SetGopLength(void *codec, int length);
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

static uint32_t p2_attr_codec_type(const IMPEncoderCHNAttr *attr)
{
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    switch (attr->encAttr.enType) {
    case PT_JPEG:
        return IMP_ENC_TYPE_JPEG;
    case PT_H265:
        return IMP_ENC_TYPE_HEVC;
    case PT_H264:
    default:
        return IMP_ENC_TYPE_AVC;
    }
#else
    return (((uint32_t)attr->encAttr.profile) >> 24) & 0xffu;
#endif
}

static uint32_t p2_attr_profile(const IMPEncoderCHNAttr *attr)
{
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    uint32_t codec_type = p2_attr_codec_type(attr);

    if (codec_type == IMP_ENC_TYPE_JPEG)
        return IMP_ENC_PROFILE_JPEG;
    if (codec_type == IMP_ENC_TYPE_HEVC)
        return IMP_ENC_PROFILE_HEVC_MAIN;
    switch (attr->encAttr.profile) {
    case 2:
    case IMP_ENC_AVC_PROFILE_IDC_HIGH:
        return IMP_ENC_PROFILE_AVC_HIGH;
    case 1:
    case IMP_ENC_AVC_PROFILE_IDC_MAIN:
        return IMP_ENC_PROFILE_AVC_MAIN;
    default:
        return IMP_ENC_PROFILE_AVC_BASELINE;
    }
#else
    return (uint32_t)attr->encAttr.profile;
#endif
}

static uint32_t p2_attr_width(const IMPEncoderCHNAttr *attr)
{
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    return attr->encAttr.picWidth;
#else
    return attr->encAttr.maxPicWidth;
#endif
}

static uint32_t p2_attr_height(const IMPEncoderCHNAttr *attr)
{
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    return attr->encAttr.picHeight;
#else
    return attr->encAttr.maxPicHeight;
#endif
}

static uint32_t p2_attr_gop_length(const IMPEncoderCHNAttr *attr)
{
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    return attr->rcAttr.maxGop;
#else
    return attr->gopAttr.uGopLength;
#endif
}

static uint32_t p2_attr_bitrate_kbps(const IMPEncoderCHNAttr *attr)
{
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    switch (attr->rcAttr.attrRcMode.rcMode) {
    case IMP_ENC_RC_MODE_CBR:
        return p2_attr_codec_type(attr) == IMP_ENC_TYPE_HEVC
            ? attr->rcAttr.attrRcMode.attrH265Cbr.outBitRate
            : attr->rcAttr.attrRcMode.attrH264Cbr.outBitRate;
    case IMP_ENC_RC_MODE_VBR:
    case IMP_ENC_RC_MODE_SMART:
        return p2_attr_codec_type(attr) == IMP_ENC_TYPE_HEVC
            ? attr->rcAttr.attrRcMode.attrH265Vbr.maxBitRate
            : attr->rcAttr.attrRcMode.attrH264Vbr.maxBitRate;
    default:
        return 0;
    }
#else
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
#endif
}

static void p2_codec_params(unsigned char *params, const IMPEncoderCHNAttr *attr)
{
    uint32_t profile = p2_attr_profile(attr);
    uint32_t codec_type = p2_attr_codec_type(attr);
    uint32_t fps_num = attr->rcAttr.outFrmRate.frmRateNum;
    uint32_t fps_den = attr->rcAttr.outFrmRate.frmRateDen;
    uint32_t bitrate = p2_attr_bitrate_kbps(attr) * 1000u;
    int qp = 26;
    int min_qp = 15;
    int max_qp = 45;

    p2_startup_trace("openimp/P2 startup: codec params defaults begin\n");
    AL_Codec_Encode_SetDefaultParam(params);
    p2_startup_trace("openimp/P2 startup: codec params defaults done\n");
    *(uint16_t *)(params + 0x08) = (uint16_t)p2_attr_width(attr);
    *(uint16_t *)(params + 0x0a) = (uint16_t)p2_attr_height(attr);
    *(uint16_t *)(params + 0x0c) = (uint16_t)p2_attr_width(attr);
    *(uint16_t *)(params + 0x0e) = (uint16_t)p2_attr_height(attr);
    *(uint32_t *)(params + 0x20) = profile;
    *(uint32_t *)(params + 0x6c) = (uint32_t)attr->rcAttr.attrRcMode.rcMode;
    *(uint16_t *)(params + 0x78) = (uint16_t)(fps_num ? fps_num : 25u);
    *(uint16_t *)(params + 0x7a) = (uint16_t)((fps_den ? fps_den : 1u) * 1000u);
    *(uint32_t *)(params + 0x7c) = bitrate;
    *(uint32_t *)(params + 0xb0) = p2_attr_gop_length(attr);

#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    if (attr->rcAttr.attrRcMode.rcMode == IMP_ENC_RC_MODE_FIXQP) {
        qp = codec_type == IMP_ENC_TYPE_HEVC
            ? (int)attr->rcAttr.attrRcMode.attrH265FixQp.qp
            : (int)attr->rcAttr.attrRcMode.attrH264FixQp.qp;
    } else if (attr->rcAttr.attrRcMode.rcMode == IMP_ENC_RC_MODE_CBR) {
        if (codec_type == IMP_ENC_TYPE_HEVC) {
            min_qp = (int)attr->rcAttr.attrRcMode.attrH265Cbr.minQp;
            max_qp = (int)attr->rcAttr.attrRcMode.attrH265Cbr.maxQp;
        } else {
            min_qp = (int)attr->rcAttr.attrRcMode.attrH264Cbr.minQp;
            max_qp = (int)attr->rcAttr.attrRcMode.attrH264Cbr.maxQp;
        }
        qp = (min_qp + max_qp) / 2;
    } else {
        if (codec_type == IMP_ENC_TYPE_HEVC) {
            min_qp = (int)attr->rcAttr.attrRcMode.attrH265Vbr.minQp;
            max_qp = (int)attr->rcAttr.attrRcMode.attrH265Vbr.maxQp;
        } else {
            min_qp = (int)attr->rcAttr.attrRcMode.attrH264Vbr.minQp;
            max_qp = (int)attr->rcAttr.attrRcMode.attrH264Vbr.maxQp;
        }
        qp = (min_qp + max_qp) / 2;
    }
#else
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
#endif
    if (codec_type == IMP_ENC_TYPE_JPEG && qp < 1)
        qp = 25;
    if (qp < 1 || qp > 51)
        qp = 26;
    if (attr->rcAttr.attrRcMode.rcMode == IMP_ENC_RC_MODE_FIXQP) {
        min_qp = qp;
        max_qp = qp;
    }
    if (min_qp < 1 || min_qp > 51)
        min_qp = 15;
    if (max_qp < 1 || max_qp > 51)
        max_qp = 45;
    *(uint16_t *)(params + 0x84) = (uint16_t)qp;
    *(uint8_t *)(params + 0x86) = (uint8_t)min_qp;
    *(uint16_t *)(params + 0x88) = (uint16_t)max_qp;
    p2_startup_trace("openimp/P2 startup: codec params done rc=%u bitrate=%u qp=%d/%d/%d\n",
                     (unsigned int)attr->rcAttr.attrRcMode.rcMode,
                     (unsigned int)bitrate, qp, min_qp, max_qp);
}

int EncoderInit(void)
{
    int i;

    p2_startup_trace("openimp/P2 startup: EncoderInit entry initialized=%d\n",
                     p2_initialized);
    pthread_mutex_lock(&p2_state_lock);
    p2_startup_trace("openimp/P2 startup: EncoderInit state lock acquired\n");
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
        p2_startup_trace("openimp/P2 startup: EncoderInit channel locks initialized\n");
    }
    pthread_mutex_unlock(&p2_state_lock);
    p2_startup_trace("openimp/P2 startup: EncoderInit done\n");
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
            p2_trace("openimp/P2: Bind %d.%d.%d -> %d.%d.%d\n",
                     source->deviceID, source->groupID, source->outputID,
                     destination->deviceID, destination->groupID,
                     destination->outputID);
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
    p2_trace("openimp/P2: CreateGroup group=%d\n", group);
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

    P2_STARTUP_MARKER("openimp/P2 marker C0 CreateChn entry\n");
    if (!p2_valid_channel(channel) || !attr)
        return -1;
    P2_STARTUP_MARKER("openimp/P2 marker C1 args valid\n");
    EncoderInit();
    P2_STARTUP_MARKER("openimp/P2 marker C2 EncoderInit returned\n");
    p2_startup_trace("openimp/P2 startup: CreateChn after EncoderInit\n");
    p2_trace("openimp/P2: CreateChn begin ch=%d profile=0x%x size=%ux%u\n",
             channel, (unsigned int)p2_attr_profile(attr),
             (unsigned int)p2_attr_width(attr),
             (unsigned int)p2_attr_height(attr));
    ch = &p2_channels[channel];
    P2_STARTUP_MARKER("openimp/P2 marker C3 before channel lock\n");
    p2_startup_trace("openimp/P2 startup: CreateChn locking channel\n");
    pthread_mutex_lock(&ch->lock);
    P2_STARTUP_MARKER("openimp/P2 marker C4 channel locked\n");
    p2_startup_trace("openimp/P2 startup: CreateChn channel lock acquired\n");
    if (ch->created) {
        pthread_mutex_unlock(&ch->lock);
        return -1;
    }
    P2_STARTUP_MARKER("openimp/P2 marker C5 before codec params\n");
    p2_codec_params(params, attr);
    P2_STARTUP_MARKER("openimp/P2 marker C6 codec params returned\n");
    p2_startup_trace("openimp/P2 startup: CreateChn calling codec create\n");
    P2_STARTUP_MARKER("openimp/P2 marker C7 before codec create\n");
    if (AL_Codec_Encode_Create(&ch->codec, params) != 0) {
        pthread_mutex_unlock(&ch->lock);
        return -1;
    }
    P2_STARTUP_MARKER("openimp/P2 marker C8 codec create returned\n");
    p2_startup_trace("openimp/P2 startup: CreateChn codec created %p\n",
                     ch->codec);
#if !defined(PLATFORM_T23) && !defined(PLATFORM_T30)
    if (attr->rcAttr.attrRcMode.rcMode == IMP_ENC_RC_MODE_CBR)
        AL_Codec_Encode_SetQpIPDelta(
            ch->codec, attr->rcAttr.attrRcMode.attrCbr.iIPDelta);
    else if (attr->rcAttr.attrRcMode.rcMode == IMP_ENC_RC_MODE_VBR)
        AL_Codec_Encode_SetQpIPDelta(
            ch->codec, attr->rcAttr.attrRcMode.attrVbr.iIPDelta);
#endif
    if (AL_Codec_Encode_SetStreamBufferCount(ch->codec,
                                             ch->max_stream_count) != 0) {
        AL_Codec_Encode_Destroy(ch->codec);
        ch->codec = NULL;
        pthread_mutex_unlock(&ch->lock);
        return -1;
    }
    p2_startup_trace("openimp/P2 startup: CreateChn stream count set=%d\n",
                     ch->max_stream_count);
    if (ch->stream_buf_size != 0u &&
        AL_Codec_Encode_SetStreamBufferSize(ch->codec,
                                            (int)ch->stream_buf_size) != 0) {
        AL_Codec_Encode_Destroy(ch->codec);
        ch->codec = NULL;
        pthread_mutex_unlock(&ch->lock);
        return -1;
    }
    ch->attr = *attr;
    ch->codec_type = (int)p2_attr_codec_type(attr);
    ch->created = 1;
    ch->group = -1;
    ch->source_channel = -1;
    if (ch->entropy_mode_set &&
        AL_Codec_Encode_SetEntropyMode(ch->codec, ch->entropy_mode) != 0) {
        AL_Codec_Encode_Destroy(ch->codec);
        ch->codec = NULL;
        ch->created = 0;
        pthread_mutex_unlock(&ch->lock);
        return -1;
    }
    pthread_mutex_unlock(&ch->lock);
    p2_trace("openimp/P2: CreateChn done ch=%d codec=%p\n",
             channel, ch->codec);
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
    p2_trace("openimp/P2: RegisterChn group=%d ch=%d source=%d\n",
             group, channel, ch->source_channel);
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
    ch->output_timestamp_us = 0;
    ch->receiving = 1;
    pthread_mutex_unlock(&ch->lock);
    p2_trace("openimp/P2: StartRecv ch=%d source=%d\n",
             channel, ch->source_channel);
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
    ch->output_timestamp_us = 0;
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

int IMP_Encoder_PollingStream(int channel, uint32_t timeout_ms)
{
    static unsigned int trace_count;
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    static unsigned int t23_avc_trace_count;
#endif
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
    uint64_t frame_deadline_us;
    uint64_t wait_us;
    uint64_t timeout_us;
    int core_locked = 0;
    int result = -1;
    OpenIMPProfileStamp poll_profile;

    if (!p2_valid_channel(channel))
        return -1;
    poll_profile = openimp_profile_begin();
    if (__sync_add_and_fetch(&trace_count, 1u) <= 8u)
        p2_trace("openimp/P2: PollingStream enter ch=%d timeout=%u\n",
                 channel, timeout_ms);
    ch = &p2_channels[channel];
    pthread_mutex_lock(&ch->lock);
    if (ch->raw_stream) {
        pthread_mutex_unlock(&ch->lock);
        openimp_profile_end(OPENIMP_PROFILE_ENCODER_POLL, poll_profile);
        return 0;
    }
    if (!ch->created || !ch->registered || !ch->receiving || !ch->codec) {
        pthread_mutex_unlock(&ch->lock);
        openimp_profile_end(OPENIMP_PROFILE_ENCODER_POLL, poll_profile);
        return -1;
    }
    /*
     * Pace every encoder channel at its requested output rate.  FrameSource
     * channels can still publish at sensor rate on T31, so relying on capture
     * cadence alone lets a low-rate substream compete equally with the main
     * stream for the single AVPU.  The userspace VBM ready queue preserves a
     * completed frame while we wait, unlike the old blocking WAIT_FRAME path
     * that could race past a sensor edge.
     */
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
        openimp_profile_end(OPENIMP_PROFILE_ENCODER_POLL, poll_profile);
        return -1;
    }
    if (wait_us)
        p2_sleep_us(wait_us);

    pthread_mutex_lock(&ch->lock);
    if (!ch->created || !ch->registered || !ch->receiving || !ch->codec ||
        ch->raw_stream) {
        pthread_mutex_unlock(&ch->lock);
        openimp_profile_end(OPENIMP_PROFILE_ENCODER_POLL, poll_profile);
        return ch->raw_stream ? 0 : -1;
    }
    if (interval_us) {
        now_us = p2_monotonic_us();
        if (!ch->next_frame_due_us ||
            now_us > ch->next_frame_due_us + interval_us)
            ch->next_frame_due_us = now_us + interval_us;
        else
            ch->next_frame_due_us += interval_us;
    }
    pthread_mutex_unlock(&ch->lock);

    if (ch->codec_type == IMP_ENC_TYPE_JPEG) {
        memset(&ch->synthetic_frame, 0, sizeof(ch->synthetic_frame));
        ch->synthetic_frame.index = -1;
        ch->synthetic_frame.pool_index = -1;
        ch->synthetic_frame.width = p2_attr_width(&ch->attr);
        ch->synthetic_frame.height = p2_attr_height(&ch->attr);
        ch->synthetic_frame.pixel_format = 10u; /* PIX_FMT_NV12 */
        frame = &ch->synthetic_frame;
    } else {
        /*
         * IMP_FrameSource_GetFrame() exposes the userspace VBM ready queue
         * and is intentionally non-blocking.  PollingStream, however, is a
         * blocking API and Raptor passes a one-second timeout.  Returning as
         * soon as the queue is momentarily empty makes both encoder threads
         * spin thousands of times between sensor frames and can starve the
         * capture/network paths on a single-core T31.
         */
        frame_deadline_us = p2_monotonic_us() + timeout_us;
        while (IMP_FrameSource_GetFrame(ch->source_channel, &frame) != 0) {
            if (!timeout_ms || p2_monotonic_us() >= frame_deadline_us)
                goto done;
            p2_sleep_us(1000u);
        }
    }
    if (trace_count <= 8u)
        p2_trace("openimp/P2: PollingStream frame ch=%d source=%d frame=%p\n",
                 channel, ch->source_channel, frame);

    /*
     * Wait for this channel's source frame without owning the shared AVPU
     * lock.  Holding the lock across the sensor-cadence wait lets whichever
     * channel won it first repeatedly reacquire it and starve the other
     * stream for seconds at a time.  Only command submission and completion
     * collection need to be serialized across encoder instances.
     */
    pthread_mutex_lock(&p2_core_lock);
    core_locked = 1;
    if (AL_Codec_Encode_Process(ch->codec, frame, frame) != 0)
        goto done;
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    if (ch->codec_type == IMP_ENC_TYPE_AVC &&
        __sync_add_and_fetch(&t23_avc_trace_count, 1u) <= 16u)
        p2_trace("openimp/P2: encode returned frame=%u ptr=%p\n",
                 t23_avc_trace_count - 1u, frame);
#endif
    /*
     * Capture ownership differs at the stock-driver seam.  T31 1080p can
     * outlast one sensor interval, so it must retain the source until AVPU
     * completion.  T40 completes inside the interval and must return the
     * frame immediately after submission; retaining it halves the capture
     * cadence and starves the ISP temporal filters.
     */
    if (p2_capture_release_policy() == P2_CAPTURE_RELEASE_AFTER_SUBMIT &&
        frame != &ch->synthetic_frame &&
        IMP_FrameSource_ReleaseFrame(ch->source_channel, frame) == 0)
        frame = NULL;
    retries = timeout_ms ? timeout_ms : 2000u;
    if (retries < 2000u)
        retries = 2000u;
    for (retry = 0; retry < retries; retry++) {
        int get_result =
            AL_Codec_Encode_GetStream(ch->codec, &stream, &user);
        if (get_result == 0)
            break;
        if (get_result > 0)
            goto done;
        usleep(1000);
    }
    if (!stream)
        goto done;
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    if (ch->codec_type == IMP_ENC_TYPE_AVC && t23_avc_trace_count <= 16u)
        p2_trace("openimp/P2: stream dequeued frame=%u stream=%p user=%p\n",
                 t23_avc_trace_count - 1u, stream, user);
#endif
    openimp_profile_count(OPENIMP_PROFILE_GETSTREAM_RETRIES,
                          (uint64_t)retry);
    if (p2_capture_release_policy() == P2_CAPTURE_RELEASE_AFTER_COMPLETION &&
        frame != &ch->synthetic_frame &&
        IMP_FrameSource_ReleaseFrame(ch->source_channel, frame) == 0)
        frame = NULL;
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    if (ch->codec_type == IMP_ENC_TYPE_AVC && t23_avc_trace_count <= 16u)
        p2_trace("openimp/P2: capture released frame=%u remaining=%p\n",
                 t23_avc_trace_count - 1u, frame);
#endif
    pthread_mutex_lock(&ch->lock);
    ch->source_frame = frame ? frame : &ch->synthetic_frame;
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
    if (core_locked)
        pthread_mutex_unlock(&p2_core_lock);
    openimp_profile_end(OPENIMP_PROFILE_ENCODER_POLL, poll_profile);
    return result;
}

int IMP_Encoder_PollingModuleStream(uint32_t *channel_bitmap,
                                    uint32_t timeout_ms)
{
    static unsigned int trace_count;
    uint32_t ready = 0;
    int channel;

    if (!channel_bitmap)
        return -1;
    EncoderInit();
    for (channel = 0; channel < P2_MAX_CHANNELS; channel++) {
        P2EncoderChannel *ch = &p2_channels[channel];

        pthread_mutex_lock(&ch->lock);
        if (ch->raw_stream)
            ready |= 1u << channel;
        pthread_mutex_unlock(&ch->lock);
    }
    if (!ready) {
        for (channel = 0; channel < P2_MAX_CHANNELS; channel++) {
            P2EncoderChannel *ch = &p2_channels[channel];
            int active;

            pthread_mutex_lock(&ch->lock);
            active = ch->created && ch->registered && ch->receiving;
            pthread_mutex_unlock(&ch->lock);
            if (active &&
                IMP_Encoder_PollingStream(channel, timeout_ms) == 0) {
                ready |= 1u << channel;
                break;
            }
        }
    }
    *channel_bitmap = ready;
    if (__sync_add_and_fetch(&trace_count, 1) <= 4)
        p2_trace("openimp/P2: PollModule timeout=%u ready=0x%x\n",
                 timeout_ms, ready);
    return ready ? 0 : -1;
}

int IMP_Encoder_GetStream(int channel, IMPEncoderStream *stream, int block)
{
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    static unsigned int t23_get_trace_count;
    uint32_t pack_count;
#endif
    P2EncoderChannel *ch;
    P2HWStream *raw;
#if !defined(PLATFORM_T23) && !defined(PLATFORM_T30)
    uint32_t fps_num;
    uint32_t fps_den;
    uint64_t frame_interval_us;
    uint64_t source_timestamp_us;
#endif
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
#if defined(PLATFORM_T41)
        raw->frame_type == 0u;
#else
        p2_h264_stream_is_idr((const uint8_t *)(uintptr_t)raw->virt_addr,
                              raw->length);
#endif
    memset(stream, 0, P2_ENCODER_STREAM_ABI_SIZE);
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    pack_count = p2_fill_legacy_packs(ch, raw);
    if (!pack_count) {
        pthread_mutex_unlock(&ch->lock);
        return -1;
    }
    stream->pack = ch->packs;
    stream->packCount = pack_count;
    stream->seq = ch->sequence++;
    stream->refType = is_idr ? IMP_Encoder_FSTYPE_IDR
                             : IMP_Encoder_FSTYPE_SBASE;
    if (__sync_add_and_fetch(&t23_get_trace_count, 1u) <= 16u)
        p2_trace("openimp/P2: get public frame=%u ch=%d seq=%u "
                 "raw=%p addr=0x%08x len=%u packs=%u idr=%d nal=%d\n",
                 t23_get_trace_count - 1u, channel, stream->seq,
                 (void *)raw, ch->packs[0].virAddr, raw->length, pack_count,
                 is_idr, (int)ch->packs[0].dataType.h264Type);
#else
    memset(&ch->packs[0], 0, sizeof(ch->packs[0]));
    ch->packs[0].offset = 0;
    ch->packs[0].length = raw->length;
    source_timestamp_us = raw->timestamp
        ? raw->timestamp : p2_monotonic_us();
    fps_num = ch->attr.rcAttr.outFrmRate.frmRateNum;
    fps_den = ch->attr.rcAttr.outFrmRate.frmRateDen;
    if (!fps_num || !fps_den) {
        fps_num = 25u;
        fps_den = 1u;
    }
    frame_interval_us = (1000000ull * fps_den) / fps_num;
    if (!frame_interval_us)
        frame_interval_us = 1u;
    if (!ch->output_timestamp_us)
        ch->output_timestamp_us = source_timestamp_us;
    else
        ch->output_timestamp_us += frame_interval_us;
    ch->packs[0].timestamp = (int64_t)ch->output_timestamp_us;
    ch->packs[0].frameEnd = true;
    ch->packs[0].sliceType = is_idr ? IMP_ENC_SLICE_I : IMP_ENC_SLICE_P;
    ch->packs[0].nalType.h264NalType = ch->codec_type == IMP_ENC_TYPE_JPEG
        ? IMP_H264_NAL_UNKNOWN
        : (is_idr ? IMP_H264_NAL_SLICE_IDR : IMP_H264_NAL_SLICE);
    stream->phyAddr = raw->phys_addr;
    stream->virAddr = raw->virt_addr;
    stream->streamSize = raw->length;
    stream->pack = &ch->packs[0];
    stream->packCount = 1;
    stream->seq = ch->sequence++;
    stream->isVI = false;
#endif
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

int IMP_Encoder_ReleaseStream(int channel, IMPEncoderStream *stream)
{
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    static unsigned int t23_release_trace_count;
#endif
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
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    if (__sync_add_and_fetch(&t23_release_trace_count, 1u) <= 16u)
        p2_trace("openimp/P2: release public frame=%u raw=%p user=%p "
                 "source=%p\n", t23_release_trace_count - 1u, raw, user,
                 frame);
#endif
    result = AL_Codec_Encode_ReleaseStream(ch->codec, raw, user);
    if (frame != &ch->synthetic_frame &&
        IMP_FrameSource_ReleaseFrame(ch->source_channel, frame) != 0)
        result = -1;
    return result;
}

int IMP_Encoder_RequestIDR(int channel)
{
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    static unsigned int t23_idr_request_count;
#endif

    if (!p2_valid_channel(channel) || !p2_channels[channel].codec)
        return -1;
    if (p2_channels[channel].codec_type == IMP_ENC_TYPE_JPEG)
        return 0;
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    /* IMP_Encoder_YuvRequestIDR can leave the standalone Helix encoder in a
     * permanently asserted IRQ state when it is called after streaming has
     * begun.  Raptor requests an IDR whenever an RTSP client joins, and the
     * next YuvEncode then wedges in the stock IRQ handler.  The configured
     * maxGop already emits regular SPS/PPS/IDR access units, so acknowledge
     * the hint and let the natural GOP provide the next safe join point. */
    if (__sync_add_and_fetch(&t23_idr_request_count, 1u) <= 16u)
        p2_trace("openimp/P2: T23 IDR request deferred to natural GOP "
                 "ch=%d request=%u\n", channel, t23_idr_request_count);
    return 0;
#else
    return AL_Codec_Encode_RequestIDR(p2_channels[channel].codec);
#endif
}

int IMP_Encoder_Query(int channel, IMPEncoderCHNStat *stat)
{
    if (!p2_valid_channel(channel) || !stat || !p2_channels[channel].created)
        return -1;
    memset(stat, 0, sizeof(*stat));
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    stat->registered = p2_channels[channel].registered != 0;
#endif
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

#if !defined(PLATFORM_T23) && !defined(PLATFORM_T30)
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
        attr->rcAttr.attrRcMode.attrCbr.iIPDelta = -1;
    } else {
        attr->rcAttr.attrRcMode.attrVbr.uTargetBitRate = (uint32_t)bitrate;
        attr->rcAttr.attrRcMode.attrVbr.uMaxBitRate = (uint32_t)bitrate;
        attr->rcAttr.attrRcMode.attrVbr.iInitialQP = 26;
        attr->rcAttr.attrRcMode.attrVbr.iMinQP = 15;
        attr->rcAttr.attrRcMode.attrVbr.iMaxQP = 45;
        attr->rcAttr.attrRcMode.attrVbr.iIPDelta = -1;
    }
    return 0;
}
#endif

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
    if (!p2_valid_channel(channel) || !quality)
        return -1;
    p2_channels[channel].jpeg_quality = *quality;
    return 0;
}

int IMP_Encoder_GetJpegeQl(int channel, IMPEncoderJpegeQl *quality)
{
    if (!p2_valid_channel(channel) || !quality)
        return -1;
    *quality = p2_channels[channel].jpeg_quality;
    return 0;
}

int IMP_Encoder_SetMaxStreamCnt(int channel, int count)
{
    P2EncoderChannel *ch;

    if (!p2_valid_channel(channel) || count <= 0 || count > 16)
        return -1;
    EncoderInit();
    ch = &p2_channels[channel];
    pthread_mutex_lock(&ch->lock);
    if (ch->codec &&
        AL_Codec_Encode_SetStreamBufferCount(ch->codec, count) != 0) {
        pthread_mutex_unlock(&ch->lock);
        return -1;
    }
    ch->max_stream_count = count;
    pthread_mutex_unlock(&ch->lock);
    p2_trace("openimp/P2: SetMaxStreamCnt ch=%d count=%d\n",
             channel, count);
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
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
int IMP_Encoder_SetChnFrmRate(int channel, const IMPEncoderFrmRate *rate)
#else
int IMP_Encoder_SetChnFrmRate(int channel, IMPEncoderFrmRate *rate,
                              IMPEncoderFrmRate *unused)
#endif
{
    P2EncoderChannel *ch;

#if !defined(PLATFORM_T23) && !defined(PLATFORM_T30)
    (void)unused;
#endif
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
    ch->output_timestamp_us = 0;
    if (AL_Codec_Encode_SetFrameRate(ch->codec, (void *)rate) != 0) {
        pthread_mutex_unlock(&ch->lock);
        return -1;
    }
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
int IMP_Encoder_GetChnFrmRate(int channel, IMPEncoderFrmRate *rate)
#else
int IMP_Encoder_GetChnFrmRate(int channel, IMPEncoderFrmRate *rate,
                              IMPEncoderFrmRate *unused)
#endif
{
#if !defined(PLATFORM_T23) && !defined(PLATFORM_T30)
    (void)unused;
#endif
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
    P2EncoderChannel *ch;
    IMPEncoderRcAttr codec_rc;
#if !defined(PLATFORM_T23) && !defined(PLATFORM_T30)
    uint32_t target_kbps = 0;
    uint32_t maximum_kbps = 0;
#endif

    if (!p2_valid_channel(channel) || !mode)
        return -1;
    ch = &p2_channels[channel];
    pthread_mutex_lock(&ch->lock);
    if (!ch->created) {
        pthread_mutex_unlock(&ch->lock);
        return -1;
    }

    ch->attr.rcAttr.attrRcMode = *mode;
    codec_rc = ch->attr.rcAttr;
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    if (codec_rc.attrRcMode.rcMode < IMP_ENC_RC_MODE_FIXQP ||
        codec_rc.attrRcMode.rcMode >= IMP_ENC_RC_MODE_INV) {
        pthread_mutex_unlock(&ch->lock);
        return -1;
    }
#else
    switch (codec_rc.attrRcMode.rcMode) {
    case IMP_ENC_RC_MODE_CBR:
        target_kbps = codec_rc.attrRcMode.attrCbr.uTargetBitRate;
        codec_rc.attrRcMode.attrCbr.uTargetBitRate =
            target_kbps > UINT32_MAX / 1000u ? UINT32_MAX :
            target_kbps * 1000u;
        break;
    case IMP_ENC_RC_MODE_VBR:
        target_kbps = codec_rc.attrRcMode.attrVbr.uTargetBitRate;
        maximum_kbps = codec_rc.attrRcMode.attrVbr.uMaxBitRate;
        codec_rc.attrRcMode.attrVbr.uTargetBitRate =
            target_kbps > UINT32_MAX / 1000u ? UINT32_MAX :
            target_kbps * 1000u;
        codec_rc.attrRcMode.attrVbr.uMaxBitRate =
            maximum_kbps > UINT32_MAX / 1000u ? UINT32_MAX :
            maximum_kbps * 1000u;
        break;
    case IMP_ENC_RC_MODE_CAPPED_VBR:
        target_kbps = codec_rc.attrRcMode.attrCappedVbr.uTargetBitRate;
        maximum_kbps = codec_rc.attrRcMode.attrCappedVbr.uMaxBitRate;
        codec_rc.attrRcMode.attrCappedVbr.uTargetBitRate =
            target_kbps > UINT32_MAX / 1000u ? UINT32_MAX :
            target_kbps * 1000u;
        codec_rc.attrRcMode.attrCappedVbr.uMaxBitRate =
            maximum_kbps > UINT32_MAX / 1000u ? UINT32_MAX :
            maximum_kbps * 1000u;
        break;
    case IMP_ENC_RC_MODE_CAPPED_QUALITY:
        target_kbps = codec_rc.attrRcMode.attrCappedQuality.uTargetBitRate;
        maximum_kbps = codec_rc.attrRcMode.attrCappedQuality.uMaxBitRate;
        codec_rc.attrRcMode.attrCappedQuality.uTargetBitRate =
            target_kbps > UINT32_MAX / 1000u ? UINT32_MAX :
            target_kbps * 1000u;
        codec_rc.attrRcMode.attrCappedQuality.uMaxBitRate =
            maximum_kbps > UINT32_MAX / 1000u ? UINT32_MAX :
            maximum_kbps * 1000u;
        break;
    case IMP_ENC_RC_MODE_FIXQP:
        break;
    default:
        pthread_mutex_unlock(&ch->lock);
        return -1;
    }
#endif

    if (AL_Codec_Encode_SetRcParam(ch->codec, &codec_rc) != 0) {
        pthread_mutex_unlock(&ch->lock);
        return -1;
    }
    pthread_mutex_unlock(&ch->lock);
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
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    switch (mode->rcMode) {
    case IMP_ENC_RC_MODE_CBR:
        if (p2_channels[channel].codec_type == IMP_ENC_TYPE_HEVC)
            mode->attrH265Cbr.outBitRate = target;
        else
            mode->attrH264Cbr.outBitRate = target;
        break;
    case IMP_ENC_RC_MODE_VBR:
    case IMP_ENC_RC_MODE_SMART:
        if (p2_channels[channel].codec_type == IMP_ENC_TYPE_HEVC)
            mode->attrH265Vbr.maxBitRate = maximum;
        else
            mode->attrH264Vbr.maxBitRate = maximum;
        break;
    default:
        return -1;
    }
#else
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
#endif
    if (AL_Codec_Encode_SetBitRate(p2_channels[channel].codec,
                                   (int)(target * 1000u),
                                   (int)(maximum * 1000u)) != 0)
        return -1;
    return 0;
}

int IMP_Encoder_GetChnGopAttr(int channel, IMPEncoderGopAttr *gop)
{
    if (!p2_valid_channel(channel) || !gop || !p2_channels[channel].created)
        return -1;
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    memset(gop, 0, sizeof(*gop));
    gop->gopLength = p2_channels[channel].attr.rcAttr.maxGop;
#else
    *gop = p2_channels[channel].attr.gopAttr;
#endif
    return 0;
}

int IMP_Encoder_SetChnGopAttr(int channel, IMPEncoderGopAttr *gop)
{
    if (!p2_valid_channel(channel) || !gop || !p2_channels[channel].created)
        return -1;
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    p2_channels[channel].attr.rcAttr.maxGop = gop->gopLength;
#else
    p2_channels[channel].attr.gopAttr = *gop;
#endif
    return AL_Codec_Encode_SetGopParam(p2_channels[channel].codec, gop);
}

int IMP_Encoder_SetChnGopLength(int channel, int length)
{
    if (!p2_valid_channel(channel) || length <= 0 ||
        !p2_channels[channel].created)
        return -1;
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    p2_channels[channel].attr.rcAttr.maxGop = (uint32_t)length;
#else
    p2_channels[channel].attr.gopAttr.uGopLength = (uint16_t)length;
#endif
    return AL_Codec_Encode_SetGopLength(p2_channels[channel].codec, length);
}

#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
int IMP_Encoder_SetGOPSize(int channel, const IMPEncoderGOPSizeCfg *gop)
{
    if (!gop || gop->gopsize <= 0)
        return -1;
    return IMP_Encoder_SetChnGopLength(channel, gop->gopsize);
}
#endif

static void p2_set_qp_bounds(IMPEncoderAttrRcMode *mode, int minimum,
                             int maximum)
{
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    if (mode->rcMode == IMP_ENC_RC_MODE_CBR) {
        mode->attrH264Cbr.minQp = (uint32_t)minimum;
        mode->attrH264Cbr.maxQp = (uint32_t)maximum;
    } else if (mode->rcMode == IMP_ENC_RC_MODE_VBR ||
               mode->rcMode == IMP_ENC_RC_MODE_SMART) {
        mode->attrH264Vbr.minQp = (uint32_t)minimum;
        mode->attrH264Vbr.maxQp = (uint32_t)maximum;
    }
#else
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
#endif
}

int IMP_Encoder_SetChnQpBounds(int channel, int minimum, int maximum)
{
    if (!p2_valid_channel(channel) || minimum < 0 || maximum > 51 ||
        minimum > maximum || !p2_channels[channel].created)
        return -1;
    p2_set_qp_bounds(&p2_channels[channel].attr.rcAttr.attrRcMode,
                     minimum, maximum);
    return AL_Codec_Encode_SetQpBounds(p2_channels[channel].codec,
                                       minimum, maximum);
}

int IMP_Encoder_SetChnQpBoundsPerFrame(int channel, int minimum_i,
                                       int maximum_i, int minimum_p,
                                       int maximum_p)
{
    int minimum;
    int maximum;

    if (minimum_i < 0 || maximum_i > 51 || minimum_i > maximum_i ||
        minimum_p < 0 || maximum_p > 51 || minimum_p > maximum_p)
        return -1;

    /* The shared backend currently exposes one hardware QP window.  Use the
     * union of the requested I/P windows so neither frame class is clipped.
     */
    minimum = minimum_i < minimum_p ? minimum_i : minimum_p;
    maximum = maximum_i > maximum_p ? maximum_i : maximum_p;
    return IMP_Encoder_SetChnQpBounds(channel, minimum, maximum);
}

int IMP_Encoder_SetChnMaxPictureSize(int channel, uint32_t maximum_i,
                                     uint32_t maximum_p)
{
    IMPEncoderAttrRcMode *mode;
    uint32_t maximum;

    if (!p2_valid_channel(channel) || !p2_channels[channel].created ||
        !maximum_i || !maximum_p)
        return -1;
    mode = &p2_channels[channel].attr.rcAttr.attrRcMode;
    maximum = maximum_i > maximum_p ? maximum_i : maximum_p;
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    (void)mode;
    (void)maximum;
#else
    if (mode->rcMode == IMP_ENC_RC_MODE_CBR)
        mode->attrCbr.uMaxPictureSize = maximum;
    else if (mode->rcMode == IMP_ENC_RC_MODE_VBR)
        mode->attrVbr.uMaxPictureSize = maximum;
    else if (mode->rcMode == IMP_ENC_RC_MODE_CAPPED_VBR)
        mode->attrCappedVbr.uMaxPictureSize = maximum;
    else if (mode->rcMode == IMP_ENC_RC_MODE_CAPPED_QUALITY)
        mode->attrCappedQuality.uMaxPictureSize = maximum;
#endif
    return 0;
}

int IMP_Encoder_SetChnQp(int channel, int qp_value)
{
    IMPEncoderQp qp;

    if (!p2_valid_channel(channel) || qp_value < 0 || qp_value > 51 ||
        !p2_channels[channel].created)
        return -1;
    qp.qp_i = (uint32_t)qp_value;
    qp.qp_p = (uint32_t)qp_value;
    qp.qp_b = (uint32_t)qp_value;
    return AL_Codec_Encode_SetQp(p2_channels[channel].codec, &qp);
}

int IMP_Encoder_SetChnQpIPDelta(int channel, int delta)
{
    if (!p2_valid_channel(channel) || !p2_channels[channel].created)
        return -1;
    return AL_Codec_Encode_SetQpIPDelta(p2_channels[channel].codec, delta);
}

int IMP_Encoder_SetChnEntropyMode(int channel, IMPEncoderEntropyMode mode)
{
    P2EncoderChannel *ch;
    int result = 0;

    if (!p2_valid_channel(channel) ||
        (mode != IMP_ENC_ENTROPY_MODE_CAVLC &&
         mode != IMP_ENC_ENTROPY_MODE_CABAC))
        return -1;
    EncoderInit();
    ch = &p2_channels[channel];
    pthread_mutex_lock(&ch->lock);
    ch->entropy_mode = (int)mode;
    ch->entropy_mode_set = 1;
    if (ch->created)
        result = AL_Codec_Encode_SetEntropyMode(ch->codec, (int)mode);
    pthread_mutex_unlock(&ch->lock);
    return result;
}

int IMP_Encoder_SetChnResizeMode(int channel, int enabled)
{
    if (!p2_valid_channel(channel) || (enabled != 0 && enabled != 1))
        return -1;
    EncoderInit();
    p2_channels[channel].resize_mode = enabled;
    return 0;
}

int IMP_Encoder_GetChnEvalInfo(int channel, void *info)
{
    if (!p2_valid_channel(channel) || !info ||
        !p2_channels[channel].created)
        return -1;
    errno = ENOTSUP;
    return -1;
}

#if defined(PLATFORM_T31)
int IMP_Encoder_GetChnAveBitrate(int channel, IMPEncoderStream *stream,
                                 int frames, double *bitrate)
#else
int IMP_Encoder_GetChnAveBitrate(int channel, IMPEncoderStream *stream,
                                 int frames, int *bitrate)
#endif
{
    uint32_t fps_num;
    uint32_t fps_den;
    uint64_t bits_per_second;

    if (!p2_valid_channel(channel) || !stream || frames <= 0 || !bitrate ||
        !p2_channels[channel].created)
        return -1;
    fps_num = p2_channels[channel].attr.rcAttr.outFrmRate.frmRateNum;
    fps_den = p2_channels[channel].attr.rcAttr.outFrmRate.frmRateDen;
    if (!fps_num || !fps_den) {
        fps_num = 25;
        fps_den = 1;
    }
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    bits_per_second = (uint64_t)(stream->packCount && stream->pack
        ? stream->pack[0].length : 0u) * 8u * fps_num / fps_den;
#else
    bits_per_second = (uint64_t)stream->streamSize * 8u * fps_num / fps_den;
#endif
#if defined(PLATFORM_T31)
    *bitrate = (double)bits_per_second;
#else
    *bitrate = bits_per_second > INT32_MAX
        ? INT32_MAX : (int)bits_per_second;
#endif
    return 0;
}

#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
int IMP_Encoder_GetChnEncType(int channel, IMPPayloadType *type)
#else
int IMP_Encoder_GetChnEncType(int channel, IMPEncoderEncType *type)
#endif
{
    if (!p2_valid_channel(channel) || !type || !p2_channels[channel].created)
        return -1;
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    switch (p2_channels[channel].codec_type) {
    case IMP_ENC_TYPE_JPEG:
        *type = PT_JPEG;
        break;
    case IMP_ENC_TYPE_HEVC:
        *type = PT_H265;
        break;
    default:
        *type = PT_H264;
        break;
    }
#else
    *type = (IMPEncoderEncType)p2_channels[channel].codec_type;
#endif
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

#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
static P2EncoderChannel *p2_legacy_config_channel(int channel)
{
    if (!p2_valid_channel(channel) || !p2_channels[channel].created)
        return NULL;
    return &p2_channels[channel];
}

int IMP_Encoder_SetChnColor2Grey(int channel,
                                const IMPEncoderColor2GreyCfg *config)
{
    P2EncoderChannel *ch = p2_legacy_config_channel(channel);

    if (!ch || !config)
        return -1;
    pthread_mutex_lock(&ch->lock);
    ch->color2grey = *config;
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

int IMP_Encoder_GetChnColor2Grey(int channel,
                                IMPEncoderColor2GreyCfg *config)
{
    P2EncoderChannel *ch = p2_legacy_config_channel(channel);

    if (!ch || !config)
        return -1;
    pthread_mutex_lock(&ch->lock);
    *config = ch->color2grey;
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

int IMP_Encoder_SetChnROI(int channel, const IMPEncoderROICfg *config)
{
    P2EncoderChannel *ch = p2_legacy_config_channel(channel);

    if (!ch || !config || config->u32Index >= 8u)
        return -1;
    pthread_mutex_lock(&ch->lock);
    ch->roi[config->u32Index] = *config;
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

int IMP_Encoder_GetChnROI(int channel, IMPEncoderROICfg *config)
{
    P2EncoderChannel *ch = p2_legacy_config_channel(channel);
    uint32_t index;

    if (!ch || !config || config->u32Index >= 8u)
        return -1;
    index = config->u32Index;
    pthread_mutex_lock(&ch->lock);
    *config = ch->roi[index];
    config->u32Index = index;
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

int IMP_Encoder_SetChnDenoise(int channel,
                             const IMPEncoderAttrDenoise *config)
{
    P2EncoderChannel *ch = p2_legacy_config_channel(channel);

    if (!ch || !config)
        return -1;
    pthread_mutex_lock(&ch->lock);
    ch->denoise = *config;
    ch->attr.rcAttr.attrDenoise = *config;
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

int IMP_Encoder_GetChnDenoise(int channel, IMPEncoderAttrDenoise *config)
{
    P2EncoderChannel *ch = p2_legacy_config_channel(channel);

    if (!ch || !config)
        return -1;
    pthread_mutex_lock(&ch->lock);
    *config = ch->denoise;
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

int IMP_Encoder_InsertUserData(int channel, void *data, uint32_t size)
{
    P2EncoderChannel *ch = p2_legacy_config_channel(channel);

    if (!ch || !data || !size || size > 1024u)
        return -1;
    p2_trace("openimp/P2: accepted pending user data ch=%d size=%u\n",
             channel, (unsigned int)size);
    return 0;
}

int IMP_Encoder_SetMbRC(int channel, int enabled)
{
    P2EncoderChannel *ch = p2_legacy_config_channel(channel);

    if (!ch || (enabled != 0 && enabled != 1))
        return -1;
    pthread_mutex_lock(&ch->lock);
    ch->macroblock_rate_control = enabled;
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

int IMP_Encoder_GetMbRC(int channel, int *enabled)
{
    P2EncoderChannel *ch = p2_legacy_config_channel(channel);

    if (!ch || !enabled)
        return -1;
    pthread_mutex_lock(&ch->lock);
    *enabled = ch->macroblock_rate_control;
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

int IMP_Encoder_SetSuperFrameCfg(int channel,
                                const IMPEncoderSuperFrmCfg *config)
{
    P2EncoderChannel *ch = p2_legacy_config_channel(channel);

    if (!ch || !config)
        return -1;
    pthread_mutex_lock(&ch->lock);
    ch->superframe = *config;
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

int IMP_Encoder_GetSuperFrameCfg(int channel,
                                IMPEncoderSuperFrmCfg *config)
{
    P2EncoderChannel *ch = p2_legacy_config_channel(channel);

    if (!ch || !config)
        return -1;
    pthread_mutex_lock(&ch->lock);
    *config = ch->superframe;
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

int IMP_Encoder_SetH264TransCfg(int channel,
                               const IMPEncoderH264TransCfg *config)
{
    P2EncoderChannel *ch = p2_legacy_config_channel(channel);

    if (!ch || !config || config->chroma_qp_index_offset < -12 ||
        config->chroma_qp_index_offset > 12)
        return -1;
    pthread_mutex_lock(&ch->lock);
    ch->h264_transform = *config;
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

int IMP_Encoder_GetH264TransCfg(int channel,
                               IMPEncoderH264TransCfg *config)
{
    P2EncoderChannel *ch = p2_legacy_config_channel(channel);

    if (!ch || !config)
        return -1;
    pthread_mutex_lock(&ch->lock);
    *config = ch->h264_transform;
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

int IMP_Encoder_SetH265TransCfg(int channel,
                               const IMPEncoderH265TransCfg *config)
{
    P2EncoderChannel *ch = p2_legacy_config_channel(channel);

    if (!ch || !config || config->chroma_cr_qp_offset < -12 ||
        config->chroma_cr_qp_offset > 12 ||
        config->chroma_cb_qp_offset < -12 ||
        config->chroma_cb_qp_offset > 12)
        return -1;
    pthread_mutex_lock(&ch->lock);
    ch->h265_transform = *config;
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

int IMP_Encoder_GetH265TransCfg(int channel,
                               IMPEncoderH265TransCfg *config)
{
    P2EncoderChannel *ch = p2_legacy_config_channel(channel);

    if (!ch || !config)
        return -1;
    pthread_mutex_lock(&ch->lock);
    *config = ch->h265_transform;
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

int IMP_Encoder_SetQpgMode(int channel, const IMPEncoderQpgMode *mode)
{
    P2EncoderChannel *ch = p2_legacy_config_channel(channel);

    if (!ch || !mode || *mode < ENC_QPG_CLOSE || *mode > ENC_QPG_SASM_TAB)
        return -1;
    pthread_mutex_lock(&ch->lock);
    ch->qpg_mode = *mode;
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

int IMP_Encoder_GetQpgMode(int channel, IMPEncoderQpgMode *mode)
{
    P2EncoderChannel *ch = p2_legacy_config_channel(channel);

    if (!ch || !mode)
        return -1;
    pthread_mutex_lock(&ch->lock);
    *mode = ch->qpg_mode;
    pthread_mutex_unlock(&ch->lock);
    return 0;
}
#endif

int IMP_Encoder_GetFd(int channel)
{
    (void)channel;
    errno = ENOSYS;
    return -1;
}
