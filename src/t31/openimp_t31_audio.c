/* T31 audio I/O over the stock /dev/dsp ABI.
 *
 * The codec driver owns capture and playback.  Audio effects remain in
 * gtxaspec's libaudioProcess-neo and are resolved lazily from the installed
 * libaudioProcess.so, matching the division of responsibilities used by the
 * OpenIMP T40/T41 implementation.
 *
 * T20/T21 build this file unchanged.  T23 builds it with PLATFORM_T23, which
 * swaps the kernel ABI for the T23 OSS3 one (see the T23 block below) and
 * keeps everything above the driver - the capture thread and its FIFO, the
 * BLOCK/NOBLOCK GetFrame semantics, software AI volume, effects - shared.
 */

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <imp/imp_audio.h>

#if defined(PLATFORM_T23)
/* T23 speaks the OSS3 "AMIC" /dev/dsp ABI of ingenic-sdk audio/t23/oss3
 * (include/audio_dsp.h), not the T31 one.  The numbers below are the ones the
 * OEM T23 libimp 1.3.0 issues from __ai_dev_init, __ai_dev_read,
 * __ao_dev_init, _ao_play_thread, __ai_dev_set_gain and friends:
 *   - parameters go through AI/AO_SET_PARAM {rate, format, channel} instead
 *     of SNDCTL_DSP_SPEED/CHANNELS/SETFMT, and AI and AO have separate
 *     ENABLE/DISABLE_STREAM requests;
 *   - GET_STREAM carries a fifth member, a pointer the driver stores the
 *     capture timeval into (20 bytes, like T41; T40 has 16);
 *   - gain requests take struct volume {channel, gain} (8 bytes, unlike the
 *     12-byte T40/T41 struct); channel 1 is MONO_LEFT;
 *   - GET_STREAM/SET_STREAM move whole driver fragments (20 ms with the
 *     default fragment_time) and return 0, not a byte count: a size that is
 *     not a fragment multiple is silently truncated;
 *   - the codec's digital volume hooks are empty, so - as in the OEM libimp,
 *     which computes a pow() gain for IMP_AO_SetVol - volume is applied in
 *     software on both directions.
 * The driver's DISABLE_STREAM also forgets the route's sample rate, so every
 * ENABLE_STREAM must be preceded by SET_PARAM (enabling a route whose rate is
 * 0 divides by zero in dsp_create_dma_chan). */
#define T23_AO_SET_GAIN           0x40085058UL /* AMIC_SPK_SET_GAIN */
#define T23_AI_SET_GAIN           0x4008505aUL /* AMIC_AI_SET_GAIN */
#define T31_AI_DISABLE_AEC        0x4004505cUL /* AMIC_DISABLE_AEC */
#define T31_AI_ENABLE_AEC         0x4004505dUL /* AMIC_ENABLE_AEC */
#define T23_AO_ENABLE_STREAM      0x4004505eUL
#define T23_AO_DISABLE_STREAM     0x4004505fUL
#define T23_AI_ENABLE_STREAM      0x40045060UL
#define T23_AI_DISABLE_STREAM     0x40045061UL
#define T31_AI_GET_STREAM         0x40145062UL
#define T31_AO_SET_STREAM         0x40085063UL
#define T31_AO_CLEAR_STREAM       0x40045064UL
#define T31_AO_SYNC_STREAM        0x40045065UL
#define T23_AO_SET_PARAM          0x4008506fUL
#define T23_AI_SET_PARAM          0x40085071UL
#define T23_PCM_FORMAT_S16        16U
#define T23_VOLUME_MONO_LEFT      1U
/* driver fragment = rate / 100 * frame bytes * fragment_time (module
 * parameter, default 2 x 10 ms) */
#define T23_FRAGMENT_10MS_UNITS   2U
#define T31_CAPTURE_DEFAULT_DEPTH 8
#define T31_CAPTURE_MAX_DEPTH     50
#define T31_CAPTURE_RETRY_US      20000
#define T31_CAPTURE_STOP_MS       500U

typedef struct {
    int32_t seconds;
    int32_t microseconds;
} T23AudioTimeval;

typedef struct {
    void *data;
    uint32_t size;
    void *aec;
    uint32_t aec_size;
    T23AudioTimeval *timestamp;
} T31AudioInputStream;

typedef struct {
    uint32_t rate;
    uint16_t format;
    uint16_t channel;
} T23AudioParameter;

typedef struct {
    uint32_t channel;
    uint32_t gain;
} T23AudioVolume;

_Static_assert(sizeof(T31AudioInputStream) == 20,
               "T23 audio input stream ABI mismatch");
_Static_assert(sizeof(T23AudioParameter) == 8,
               "T23 audio parameter ABI mismatch");
_Static_assert(sizeof(T23AudioVolume) == 8, "T23 audio volume ABI mismatch");
_Static_assert(T31_AI_GET_STREAM ==
                   _IOC(_IOC_READ, 'P', 98, sizeof(T31AudioInputStream)),
               "T23 AMIC_AI_GET_STREAM ioctl mismatch");
_Static_assert(T23_AI_SET_PARAM ==
                   _IOC(_IOC_READ, 'P', 113, sizeof(T23AudioParameter)),
               "T23 AMIC_AI_SET_PARAM ioctl mismatch");
_Static_assert(T23_AI_SET_GAIN ==
                   _IOC(_IOC_READ, 'P', 90, sizeof(T23AudioVolume)),
               "T23 AMIC_AI_SET_GAIN ioctl mismatch");
#else
#define T31_DSP_SPEED             0xc0045002UL
#define T31_DSP_SETFMT            0xc0045005UL
#define T31_DSP_CHANNELS          0xc0045006UL
#define T31_AO_SET_GAIN           0x4004505aUL
#define T31_AI_SET_GAIN           0x4004505bUL
#define T31_AI_DISABLE_AEC        0x40045064UL
#define T31_AI_ENABLE_AEC         0x40045065UL
#define T31_ENABLE_STREAM         0x40045066UL
#define T31_DISABLE_STREAM        0x40045067UL
#define T31_AI_GET_STREAM         0x400c5068UL
#define T31_AO_SET_STREAM         0x40085069UL
#define T31_AO_CLEAR_STREAM       0x4004506aUL
#define T31_AO_SYNC_STREAM        0x4004506bUL
#define T31_PCM_FORMAT_S16_LE     0x10
#define T31_CAPTURE_CHUNK_BYTES   1280U
#define T31_CAPTURE_DEFAULT_DEPTH 8
#define T31_CAPTURE_MAX_DEPTH     50
#define T31_CAPTURE_RETRY_US      20000
#define T31_CAPTURE_STOP_MS       500U

typedef struct {
    void *data;
    void *aec;
    uint32_t size;
} T31AudioInputStream;
#endif

typedef struct {
    void *data;
    uint32_t size;
} T31AudioOutputStream;

typedef struct {
    int16_t target_level_dbfs;
    int16_t compression_gain_db;
    uint8_t limiter_enable;
} T31WebRtcAgcConfig;

#if !defined(PLATFORM_T23)
_Static_assert(sizeof(T31AudioInputStream) == 12,
               "T31 audio input stream ABI mismatch");
#endif
_Static_assert(sizeof(T31AudioOutputStream) == 8,
               "T31 audio output stream ABI mismatch");

typedef void (*T31HpfCreate)(int16_t *, int16_t *, int16_t, int16_t, int, int);
typedef int (*T31HpfProcess)(int16_t *, int16_t *, int);
typedef void (*T31HpfFree)(void);
typedef void *(*T31NsCreate)(void);
typedef int (*T31NsSetConfig)(void *, int, int);
typedef void (*T31NsProcess)(void *, const float *const *, int,
                             float *const *);
typedef int (*T31NsFree)(void *);
typedef void *(*T31AgcCreate)(void);
typedef int (*T31AgcSetConfig)(void *, int, int, int, int,
                               T31WebRtcAgcConfig);
typedef int (*T31AgcProcess)(void *, const int16_t *const *, size_t, size_t,
                             int16_t *const *, int32_t, int32_t *, int16_t,
                             uint8_t *);
typedef int (*T31AgcFree)(void *);
#if !defined(PLATFORM_T23)
/* libaudioProcess(-neo) AEC: struct aec_frame {far, near, reserved, bytes},
 * whole 10 ms blocks of mono S16 are processed in place in "near" */
typedef struct {
    const int16_t *far_end;
    int16_t *near_end;
    void *reserved;
    int num_bytes;
} T31AecFrame;
typedef void *(*T31AecCreate)(int, const char *);
typedef int (*T31AecProcess)(void *, T31AecFrame *);
typedef int (*T31AecFree)(void *);
#endif

static struct {
    int ai_fd;
    int ao_fd;
    int ai_enabled;
    int ai_channel_enabled;
    int ao_enabled;
    int ao_channel_enabled;
    int ao_paused;
    int ai_muted;
    int ao_muted;
    IMPAudioIOAttr ai_attr;
    IMPAudioIOAttr ao_attr;
    IMPAudioIChnParam ai_channel;
    int ai_volume;
    int ai_gain;
    int ai_alc_gain;
    int ao_volume;
    int ao_gain;
    /* The record thread owns T31_AI_GET_STREAM; the fields below up to
     * capture_tail_time are shared with it under capture_lock. */
    pthread_t capture_thread;
    int capture_running;
    int capture_stop;
    int capture_exited;
    int capture_error;
    unsigned char *capture_buffer;
    size_t capture_capacity;
    size_t capture_valid;
    size_t capture_limit;
    size_t capture_frame_bytes;
    int64_t capture_tail_time;
    unsigned char *frame_buffer;
    size_t frame_capacity;
    int frame_outstanding;
    int sequence;
    char aec_profile[256];
    void *effects_library;
    T31HpfCreate hpf_create;
    T31HpfProcess hpf_process;
    T31HpfFree hpf_free;
    int16_t hpf_state[16];
    int hpf_enabled;
    T31NsCreate ns_create;
    T31NsSetConfig ns_set_config;
    T31NsProcess ns_process;
    T31NsFree ns_free;
    void *ns;
    int ns_enabled;
    T31AgcCreate agc_create;
    T31AgcSetConfig agc_set_config;
    T31AgcProcess agc_process;
    T31AgcFree agc_free;
    void *agc;
    int agc_mode;
    int agc_enabled;
#if !defined(PLATFORM_T23)
    /* With the driver's AEC on, GET_AI_STREAM also returns the playback
     * reference; it is queued in capture_ref at the same offsets as the
     * microphone data in capture_buffer (zeros where none was captured). */
    unsigned char *capture_ref;
    size_t capture_ref_capacity;
    int capture_ref_on;
    int ref_frames;                 /* IMP_AI_EnableAecRefFrame */
    unsigned char *ref_frame;       /* reference of the outstanding frame */
    size_t ref_frame_capacity;
    int ref_frame_valid;
    void *aec;                      /* IMP_AI_EnableAec processing */
    T31AecCreate aec_create;
    T31AecProcess aec_process;
    T31AecFree aec_free;
#endif
#if defined(PLATFORM_T23)
    /* GET_STREAM bounce buffer: whole driver fragments, up to one frame */
    unsigned char *capture_chunk;
    size_t capture_chunk_capacity;
    size_t capture_chunk_bytes;
#endif
    /* SendFrame re-blocks arbitrary frame sizes into whole driver periods */
    unsigned char *ao_period;
    size_t ao_period_capacity;
    size_t ao_period_bytes;
    size_t ao_period_valid;
} t31_audio = {
    .ai_fd = -1,
    .ao_fd = -1,
    .ai_volume = 60,
    .ao_volume = 60,
    .agc_mode = 3,
};

/* Largest SendFrame period (one IMPAudioIOAttr.numPerFrm frame). */
#define OPENIMP_AO_MAX_PERIOD_BYTES 65536U

static pthread_mutex_t t31_capture_lock = PTHREAD_MUTEX_INITIALIZER;
#if !defined(PLATFORM_T23)
static pthread_mutex_t t31_aec_lock = PTHREAD_MUTEX_INITIALIZER;
#endif
static pthread_cond_t t31_capture_cond;
static pthread_once_t t31_capture_once = PTHREAD_ONCE_INIT;

extern int64_t IMP_System_GetTimeStamp(void);

static int t31_valid_attr(const IMPAudioIOAttr *attribute)
{
    return attribute && attribute->samplerate > 0 &&
           attribute->bitwidth == AUDIO_BIT_WIDTH_16 &&
           (attribute->soundmode == AUDIO_SOUND_MODE_MONO ||
            attribute->soundmode == AUDIO_SOUND_MODE_STEREO) &&
           attribute->numPerFrm > 0;
}

#if defined(PLATFORM_T23)
static int t23_configure_fd(int fd, const IMPAudioIOAttr *attribute,
                            unsigned long set_param, unsigned long enable)
{
    T23AudioParameter parameter;

    if (fd < 0 || !t31_valid_attr(attribute))
        return -1;
    memset(&parameter, 0, sizeof(parameter));
    parameter.rate = (uint32_t)attribute->samplerate;
    parameter.format = T23_PCM_FORMAT_S16;
    parameter.channel =
        attribute->soundmode == AUDIO_SOUND_MODE_STEREO ? 2U : 1U;
    if (ioctl(fd, set_param, &parameter) != 0 || ioctl(fd, enable, 1) != 0)
        return -1;
    return 0;
}

static int t23_set_gain(int fd, unsigned long command, int value)
{
    T23AudioVolume volume;

    volume.channel = T23_VOLUME_MONO_LEFT;
    volume.gain = (uint32_t)value;
    return ioctl(fd, command, &volume);
}

/* driver fragment size for an attribute, see T23_FRAGMENT_10MS_UNITS */
static size_t t23_fragment_bytes(const IMPAudioIOAttr *attribute)
{
    size_t channels =
        attribute->soundmode == AUDIO_SOUND_MODE_STEREO ? 2U : 1U;

    return (size_t)(attribute->samplerate / 100) * channels *
           sizeof(int16_t) * T23_FRAGMENT_10MS_UNITS;
}
#else
static int t31_configure_fd(int fd, const IMPAudioIOAttr *attribute)
{
    int rate;
    int channels;
    int format = T31_PCM_FORMAT_S16_LE;

    if (fd < 0 || !t31_valid_attr(attribute))
        return -1;
    rate = attribute->samplerate;
    channels = attribute->soundmode == AUDIO_SOUND_MODE_STEREO ? 2 : 1;
    if (ioctl(fd, T31_DSP_SPEED, &rate) != 0 ||
        ioctl(fd, T31_DSP_CHANNELS, &channels) != 0 ||
        ioctl(fd, T31_DSP_SETFMT, &format) != 0 ||
        ioctl(fd, T31_ENABLE_STREAM, 1) != 0)
        return -1;
    return 0;
}
#endif

static int t31_effects_load(void)
{
    if (t31_audio.effects_library)
        return 0;
    t31_audio.effects_library =
        dlopen("libaudioProcess.so", RTLD_NOW | RTLD_LOCAL);
    if (!t31_audio.effects_library)
        return -1;
#define T31_EFFECT(name, symbol)                                              \
    do {                                                                      \
        *(void **)(&t31_audio.name) =                                         \
            dlsym(t31_audio.effects_library, symbol);                         \
        if (!t31_audio.name)                                                  \
            goto failure;                                                     \
    } while (0)
    T31_EFFECT(hpf_create, "audio_process_hpf_create");
    T31_EFFECT(hpf_process, "audio_process_hpf_process");
    T31_EFFECT(hpf_free, "audio_process_hpf_free");
    T31_EFFECT(ns_create, "audio_process_ns_create");
    T31_EFFECT(ns_set_config, "audio_process_ns_set_config");
    T31_EFFECT(ns_process, "audio_process_ns_process");
    T31_EFFECT(ns_free, "audio_process_ns_free");
    T31_EFFECT(agc_create, "audio_process_agc_create");
    T31_EFFECT(agc_set_config, "audio_process_agc_set_config");
    T31_EFFECT(agc_process, "audio_process_agc_process");
    T31_EFFECT(agc_free, "audio_process_agc_free");
#undef T31_EFFECT
    return 0;

failure:
#undef T31_EFFECT
    dlclose(t31_audio.effects_library);
    t31_audio.effects_library = NULL;
    return -1;
}

#if defined(PLATFORM_T23)
/* T23 extras (src/t23/openimp_t23_audio_ext.c): howling suppression ahead
 * of the effects below, and the WebRTC profile path for IMP_*_EnableAlgo. */
extern void openimp_t23_ai_pre_effects(int16_t *samples, int count,
                                       int sample_rate);

const char *openimp_t23_audio_profile(void)
{
    return t31_audio.aec_profile;
}
#endif

static void t31_process_effects(int16_t *samples, int count)
{
    int sample_rate = t31_audio.ai_attr.samplerate;
    int frame_samples = sample_rate / 100;
    int offset;

#if defined(PLATFORM_T23)
    openimp_t23_ai_pre_effects(samples, count, sample_rate);
#endif
    if (t31_audio.hpf_enabled)
        (void)t31_audio.hpf_process(t31_audio.hpf_state, samples, count);
    if (frame_samples <= 0 || frame_samples > 160)
        return;
    for (offset = 0; offset + frame_samples <= count; offset += frame_samples) {
        if (t31_audio.ns_enabled && sample_rate <= 16000) {
            float input[160];
            float output[160];
            const float *inputs[1] = { input };
            float *outputs[1] = { output };
            int i;

            for (i = 0; i < frame_samples; i++)
                input[i] = (float)samples[offset + i];
            t31_audio.ns_process(t31_audio.ns, inputs, 1, outputs);
            for (i = 0; i < frame_samples; i++) {
                if (output[i] > 32767.0f)
                    samples[offset + i] = 32767;
                else if (output[i] < -32768.0f)
                    samples[offset + i] = -32768;
                else
                    samples[offset + i] = (int16_t)output[i];
            }
        }
        if (t31_audio.agc_enabled && sample_rate <= 16000) {
            const int16_t *inputs[1] = { samples + offset };
            int16_t *outputs[1] = { samples + offset };
            int32_t output_level = 127;
            uint8_t saturated = 0;

            (void)t31_audio.agc_process(t31_audio.agc, inputs, 1,
                                        (size_t)frame_samples, outputs, 127,
                                        &output_level, 0, &saturated);
        }
    }
}

/* IMP volume is expressed in half-decibels; 60 represents unity. */
static void t31_apply_ai_volume(int16_t *samples, int count)
{
    int steps = t31_audio.ai_volume - 60;
    uint64_t gain = 65536;
    int i;

    if (t31_audio.ai_muted || t31_audio.ai_volume <= -30) {
        memset(samples, 0, (size_t)count * sizeof(*samples));
        return;
    }
    if (steps > 60)
        steps = 60;
    if (steps < -89)
        steps = -89;
    if (steps > 0) {
        for (i = 0; i < steps; i++)
            gain = (gain * 69419U + 32768U) >> 16;
    } else {
        for (i = 0; i > steps; i--)
            gain = (gain * 61870U + 32768U) >> 16;
    }
    for (i = 0; i < count; i++) {
        int64_t value = ((int64_t)samples[i] * (int64_t)gain) >> 16;
        if (value > 32767)
            value = 32767;
        else if (value < -32768)
            value = -32768;
        samples[i] = (int16_t)value;
    }
}

/* Same 0.5 dB/step curve as t31_apply_ai_volume, for the speaker path
 * (IMP_AO_SetVol/SetVolMute; the vendor applies a software gain in its play
 * thread too). */
static void t31_apply_ao_volume(int16_t *samples, int count, int volume,
                                int muted)
{
    int steps = volume - 60;
    uint64_t gain = 65536;
    int i;

    if (muted || volume <= -30) {
        memset(samples, 0, (size_t)count * sizeof(*samples));
        return;
    }
    if (!steps)
        return;
    if (steps > 60)
        steps = 60;
    if (steps < -89)
        steps = -89;
    if (steps > 0) {
        for (i = 0; i < steps; i++)
            gain = (gain * 69419U + 32768U) >> 16;
    } else {
        for (i = 0; i > steps; i--)
            gain = (gain * 61870U + 32768U) >> 16;
    }
    for (i = 0; i < count; i++) {
        int64_t value = ((int64_t)samples[i] * (int64_t)gain) >> 16;
        if (value > 32767)
            value = 32767;
        else if (value < -32768)
            value = -32768;
        samples[i] = (int16_t)value;
    }
}

static void t31_capture_init_cond(void)
{
    pthread_condattr_t attribute;

    pthread_condattr_init(&attribute);
    pthread_condattr_setclock(&attribute, CLOCK_MONOTONIC);
    pthread_cond_init(&t31_capture_cond, &attribute);
    pthread_condattr_destroy(&attribute);
}

static void t31_capture_deadline(struct timespec *deadline,
                                 unsigned int timeout_ms)
{
    clock_gettime(CLOCK_MONOTONIC, deadline);
    deadline->tv_sec += timeout_ms / 1000U;
    deadline->tv_nsec += (long)(timeout_ms % 1000U) * 1000000L;
    if (deadline->tv_nsec >= 1000000000L) {
        deadline->tv_sec++;
        deadline->tv_nsec -= 1000000000L;
    }
}

static size_t t31_frame_bytes(void)
{
    unsigned int channels;

    channels = t31_audio.ai_attr.soundmode == AUDIO_SOUND_MODE_STEREO ? 2U : 1U;
    return (size_t)t31_audio.ai_attr.numPerFrm * channels * sizeof(int16_t);
}

static int64_t t31_bytes_to_us(size_t bytes)
{
    unsigned int channels;
    int64_t rate;

    channels = t31_audio.ai_attr.soundmode == AUDIO_SOUND_MODE_STEREO ? 2U : 1U;
    rate = (int64_t)t31_audio.ai_attr.samplerate * channels * sizeof(int16_t);
    return rate > 0 ? (int64_t)bytes * 1000000 / rate : 0;
}

#if !defined(PLATFORM_T23)
static void t31_aec_release(void);

/* capture_ref must cover capture_buffer; t31_capture_lock held or the
 * capture thread stopped. A new buffer starts as silence. */
static int t31_ref_reserve(size_t capacity)
{
    void *buffer;

    if (!capacity ||
        (t31_audio.capture_ref && t31_audio.capture_ref_capacity >= capacity))
        return 0;
    buffer = calloc(1, capacity);
    if (!buffer)
        return -1;
    free(t31_audio.capture_ref);
    t31_audio.capture_ref = buffer;
    t31_audio.capture_ref_capacity = capacity;
    return 0;
}
#endif

/* Like the stock _ai_record_thread: the driver only offers a blocking
 * GET_STREAM (its poll handler returns -EINVAL), so capture runs here and
 * IMP_AI_GetFrame/IMP_AI_PollingFrame consume a bounded FIFO.  When the
 * consumer falls behind, whole frames are dropped from the oldest end. */
static void *t31_capture_main(void *argument)
{
#if defined(PLATFORM_T23)
    /* Up to one IMP frame per GET_STREAM, like the OEM __ai_dev_read, but
     * always whole driver fragments; the FIFO re-blocks into frames. */
    unsigned char *chunk = t31_audio.capture_chunk;
    const size_t chunk_size = t31_audio.capture_chunk_bytes;
    T23AudioTimeval capture_time;
#else
    unsigned char chunk[T31_CAPTURE_CHUNK_BYTES];
    unsigned char ref_chunk[T31_CAPTURE_CHUNK_BYTES];
    int with_ref;
#endif
    T31AudioInputStream stream;
    size_t align;

    (void)argument;
    align = t31_audio.ai_attr.soundmode == AUDIO_SOUND_MODE_STEREO ? 4U : 2U;
    pthread_mutex_lock(&t31_capture_lock);
    while (!t31_audio.capture_stop) {
        size_t size;
        int result;

#if !defined(PLATFORM_T23)
        with_ref = t31_audio.capture_ref_on && t31_audio.capture_ref;
#endif
        pthread_mutex_unlock(&t31_capture_lock);
        memset(&stream, 0, sizeof(stream));
        stream.data = chunk;
#if defined(PLATFORM_T23)
        stream.size = (uint32_t)chunk_size;
        stream.timestamp = &capture_time;
        result = ioctl(t31_audio.ai_fd, T31_AI_GET_STREAM, &stream);
        /* success means every requested fragment was copied; the driver
         * reports no count (and returns early, data untouched, when the
         * route was disabled under it - capture_stop is checked below) */
        size = result == 0 ? chunk_size : 0;
#else
        stream.size = sizeof(chunk);
        stream.aec = with_ref ? ref_chunk : NULL;
        result = ioctl(t31_audio.ai_fd, T31_AI_GET_STREAM, &stream);
        size = stream.size < sizeof(chunk) ? stream.size : sizeof(chunk);
#endif
        size -= size % align;
        pthread_mutex_lock(&t31_capture_lock);
        if (t31_audio.capture_stop)
            break;
        if (result != 0 || !size) {
            t31_audio.capture_error = 1;
            pthread_cond_broadcast(&t31_capture_cond);
            pthread_mutex_unlock(&t31_capture_lock);
            usleep(T31_CAPTURE_RETRY_US);
            pthread_mutex_lock(&t31_capture_lock);
            continue;
        }
        memcpy(t31_audio.capture_buffer + t31_audio.capture_valid, chunk, size);
#if !defined(PLATFORM_T23)
        if (t31_audio.capture_ref) {
            if (with_ref)
                memcpy(t31_audio.capture_ref + t31_audio.capture_valid,
                       ref_chunk, size);
            else
                memset(t31_audio.capture_ref + t31_audio.capture_valid, 0,
                       size);
        }
#endif
        t31_audio.capture_valid += size;
        if (t31_audio.capture_valid > t31_audio.capture_limit) {
            size_t frame = t31_audio.capture_frame_bytes;
            size_t drop = t31_audio.capture_valid - t31_audio.capture_limit;

            drop = (drop + frame - 1) / frame * frame;
            t31_audio.capture_valid -= drop;
            memmove(t31_audio.capture_buffer, t31_audio.capture_buffer + drop,
                    t31_audio.capture_valid);
#if !defined(PLATFORM_T23)
            if (t31_audio.capture_ref)
                memmove(t31_audio.capture_ref, t31_audio.capture_ref + drop,
                        t31_audio.capture_valid);
#endif
        }
        t31_audio.capture_tail_time = IMP_System_GetTimeStamp();
        t31_audio.capture_error = 0;
        pthread_cond_broadcast(&t31_capture_cond);
    }
    t31_audio.capture_exited = 1;
    pthread_cond_broadcast(&t31_capture_cond);
    pthread_mutex_unlock(&t31_capture_lock);
    return NULL;
}

static int t31_capture_start(void)
{
    size_t frame = t31_frame_bytes();
    size_t depth = T31_CAPTURE_DEFAULT_DEPTH;
    size_t capacity;

    pthread_once(&t31_capture_once, t31_capture_init_cond);
    if (t31_audio.capture_running)
        return 0;
    if (!frame || t31_audio.ai_fd < 0)
        return -1;
    if (t31_audio.ai_attr.frmNum >= 2)
        depth = t31_audio.ai_attr.frmNum < T31_CAPTURE_MAX_DEPTH
                    ? (size_t)t31_audio.ai_attr.frmNum
                    : T31_CAPTURE_MAX_DEPTH;
#if defined(PLATFORM_T23)
    {
        size_t fragment = t23_fragment_bytes(&t31_audio.ai_attr);
        size_t chunk = frame - frame % (fragment ? fragment : 1U);

        if (!fragment)
            return -1;
        if (!chunk)
            chunk = fragment;
        if (chunk > t31_audio.capture_chunk_capacity) {
            void *buffer = realloc(t31_audio.capture_chunk, chunk);
            if (!buffer)
                return -1;
            t31_audio.capture_chunk = buffer;
            t31_audio.capture_chunk_capacity = chunk;
        }
        t31_audio.capture_chunk_bytes = chunk;
        capacity = frame * depth + chunk;
    }
#else
    capacity = frame * depth + T31_CAPTURE_CHUNK_BYTES;
#endif
    if (capacity > t31_audio.capture_capacity) {
        void *buffer = realloc(t31_audio.capture_buffer, capacity);
        if (!buffer)
            return -1;
        t31_audio.capture_buffer = buffer;
        t31_audio.capture_capacity = capacity;
    }
#if !defined(PLATFORM_T23)
    if (t31_audio.capture_ref_on && t31_ref_reserve(capacity) != 0)
        return -1;
#endif
    t31_audio.capture_limit = frame * depth;
    t31_audio.capture_frame_bytes = frame;
    t31_audio.capture_valid = 0;
    t31_audio.capture_stop = 0;
    t31_audio.capture_exited = 0;
    t31_audio.capture_error = 0;
    if (pthread_create(&t31_audio.capture_thread, NULL, t31_capture_main,
                       NULL) != 0)
        return -1;
    t31_audio.capture_running = 1;
    return 0;
}

/* A running stream completes the thread's GET_STREAM within one chunk.  If
 * the driver has stalled, DISABLE_STREAM finishes the pending task node so
 * the thread can be joined; the stream is re-enabled for the caller. */
static void t31_capture_stop(void)
{
    struct timespec deadline;
    int exited;

    if (!t31_audio.capture_running)
        return;
    pthread_mutex_lock(&t31_capture_lock);
    t31_audio.capture_stop = 1;
    pthread_cond_broadcast(&t31_capture_cond);
    t31_capture_deadline(&deadline, T31_CAPTURE_STOP_MS);
    while (!t31_audio.capture_exited &&
           pthread_cond_timedwait(&t31_capture_cond, &t31_capture_lock,
                                  &deadline) == 0)
        ;
    exited = t31_audio.capture_exited;
    pthread_mutex_unlock(&t31_capture_lock);
    if (!exited) {
#if defined(PLATFORM_T23)
        /* DISABLE completes the driver waiter; it also zeroes the route
         * rate, so re-arm with SET_PARAM before ENABLE. */
        (void)ioctl(t31_audio.ai_fd, T23_AI_DISABLE_STREAM, 1);
        pthread_join(t31_audio.capture_thread, NULL);
        (void)t23_configure_fd(t31_audio.ai_fd, &t31_audio.ai_attr,
                               T23_AI_SET_PARAM, T23_AI_ENABLE_STREAM);
#else
        (void)ioctl(t31_audio.ai_fd, T31_DISABLE_STREAM, 1);
        pthread_join(t31_audio.capture_thread, NULL);
        (void)ioctl(t31_audio.ai_fd, T31_ENABLE_STREAM, 1);
#endif
    } else {
        pthread_join(t31_audio.capture_thread, NULL);
    }
    t31_audio.capture_running = 0;
    t31_audio.capture_valid = 0;
}

int IMP_AI_SetPubAttr(int device, IMPAudioIOAttr *attribute)
{
    if ((device != 0 && device != 1) || t31_audio.ai_enabled ||
        !t31_valid_attr(attribute))
        return -1;
    t31_audio.ai_attr = *attribute;
    t31_audio.capture_valid = 0;
    return 0;
}

int IMP_AI_GetPubAttr(int device, IMPAudioIOAttr *attribute)
{
    if ((device != 0 && device != 1) || !attribute)
        return -1;
    *attribute = t31_audio.ai_attr;
    return 0;
}

int IMP_AI_Enable(int device)
{
    int fd;

    if (device != 0 && device != 1)
        return -1;
    if (t31_audio.ai_enabled)
        return 0;
    fd = open("/dev/dsp", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
#if defined(PLATFORM_T23)
    if (t23_configure_fd(fd, &t31_audio.ai_attr, T23_AI_SET_PARAM,
                         T23_AI_ENABLE_STREAM) != 0) {
#else
    if (t31_configure_fd(fd, &t31_audio.ai_attr) != 0) {
#endif
        close(fd);
        return -1;
    }
    t31_audio.ai_fd = fd;
    t31_audio.ai_enabled = 1;
    t31_audio.capture_valid = 0;
    return 0;
}

int IMP_AI_Disable(int device)
{
    int result = 0;

    if (device != 0 && device != 1)
        return -1;
    t31_capture_stop();
    if (t31_audio.ai_fd >= 0) {
        if (t31_audio.ai_enabled)
#if defined(PLATFORM_T23)
            result = ioctl(t31_audio.ai_fd, T23_AI_DISABLE_STREAM, 1);
#else
            result = ioctl(t31_audio.ai_fd, T31_DISABLE_STREAM, 1);
#endif
        close(t31_audio.ai_fd);
    }
    t31_audio.ai_fd = -1;
    t31_audio.ai_enabled = 0;
    t31_audio.ai_channel_enabled = 0;
    t31_audio.frame_outstanding = 0;
    t31_audio.capture_valid = 0;
#if !defined(PLATFORM_T23)
    /* closing the descriptor dropped the driver's AEC reference */
    t31_aec_release();
    t31_audio.capture_ref_on = 0;
    t31_audio.ref_frames = 0;
    t31_audio.ref_frame_valid = 0;
#endif
    return result;
}

int IMP_AI_EnableChn(int device, int channel)
{
    if ((device != 0 && device != 1) || channel != 0 ||
        !t31_audio.ai_enabled || t31_capture_start() != 0)
        return -1;
    t31_audio.ai_channel_enabled = 1;
    return 0;
}

int IMP_AI_DisableChn(int device, int channel)
{
    if ((device != 0 && device != 1) || channel != 0)
        return -1;
    t31_capture_stop();
    t31_audio.ai_channel_enabled = 0;
    t31_audio.frame_outstanding = 0;
    t31_audio.capture_valid = 0;
    return 0;
}

int IMP_AI_SetChnParam(int device, int channel, IMPAudioIChnParam *parameter)
{
    if ((device != 0 && device != 1) || channel != 0 || !parameter)
        return -1;
    t31_audio.ai_channel = *parameter;
    return 0;
}

int IMP_AI_GetChnParam(int device, int channel, IMPAudioIChnParam *parameter)
{
    if ((device != 0 && device != 1) || channel != 0 || !parameter)
        return -1;
    *parameter = t31_audio.ai_channel;
    return 0;
}

int IMP_AI_PollingFrame(int device, int channel, unsigned int timeout_ms)
{
    struct timespec deadline;
    int ready;

    if ((device != 0 && device != 1) || channel != 0 ||
        !t31_audio.ai_channel_enabled || t31_audio.frame_outstanding ||
        !t31_audio.capture_running)
        return -1;
    t31_capture_deadline(&deadline, timeout_ms);
    pthread_mutex_lock(&t31_capture_lock);
    while (t31_audio.capture_valid < t31_audio.capture_frame_bytes &&
           !t31_audio.capture_exited &&
           pthread_cond_timedwait(&t31_capture_cond, &t31_capture_lock,
                                  &deadline) == 0)
        ;
    ready = t31_audio.capture_valid >= t31_audio.capture_frame_bytes;
    pthread_mutex_unlock(&t31_capture_lock);
    return ready ? 0 : -1;
}

int IMP_AI_GetFrame(int device, int channel, IMPAudioFrame *frame,
                    IMPBlock block)
{
    size_t bytes;
    int64_t timestamp;

    if ((device != 0 && device != 1) || channel != 0 || !frame ||
        !t31_audio.ai_channel_enabled || t31_audio.frame_outstanding ||
        !t31_audio.capture_running)
        return -1;
    bytes = t31_audio.capture_frame_bytes;
    if (bytes > t31_audio.frame_capacity) {
        void *buffer = realloc(t31_audio.frame_buffer, bytes);
        if (!buffer)
            return -1;
        t31_audio.frame_buffer = buffer;
        t31_audio.frame_capacity = bytes;
    }
#if !defined(PLATFORM_T23)
    if (t31_audio.capture_ref_on && bytes > t31_audio.ref_frame_capacity) {
        void *buffer = realloc(t31_audio.ref_frame, bytes);
        if (!buffer)
            return -1;
        t31_audio.ref_frame = buffer;
        t31_audio.ref_frame_capacity = bytes;
    }
#endif
    pthread_mutex_lock(&t31_capture_lock);
    while (block == BLOCK && t31_audio.capture_valid < bytes &&
           !t31_audio.capture_error && !t31_audio.capture_exited)
        pthread_cond_wait(&t31_capture_cond, &t31_capture_lock);
    if (t31_audio.capture_valid < bytes) {
        pthread_mutex_unlock(&t31_capture_lock);
        return -1;
    }
    memcpy(t31_audio.frame_buffer, t31_audio.capture_buffer, bytes);
#if !defined(PLATFORM_T23)
    t31_audio.ref_frame_valid = t31_audio.capture_ref_on &&
                                t31_audio.capture_ref &&
                                bytes <= t31_audio.ref_frame_capacity;
    if (t31_audio.ref_frame_valid)
        memcpy(t31_audio.ref_frame, t31_audio.capture_ref, bytes);
#endif
    t31_audio.capture_valid -= bytes;
    if (t31_audio.capture_valid)
        memmove(t31_audio.capture_buffer, t31_audio.capture_buffer + bytes,
                t31_audio.capture_valid);
#if !defined(PLATFORM_T23)
    if (t31_audio.capture_ref && t31_audio.capture_valid)
        memmove(t31_audio.capture_ref, t31_audio.capture_ref + bytes,
                t31_audio.capture_valid);
#endif
    /* Stamp the frame's capture end, not the time it was dequeued. */
    timestamp = t31_audio.capture_tail_time -
                t31_bytes_to_us(t31_audio.capture_valid);
    pthread_mutex_unlock(&t31_capture_lock);
#if !defined(PLATFORM_T23)
    /* echo cancellation first, like libimp's record path */
    if (t31_audio.ref_frame_valid) {
        pthread_mutex_lock(&t31_aec_lock);
        if (t31_audio.aec) {
            T31AecFrame aec_frame;

            aec_frame.far_end = (const int16_t *)(void *)t31_audio.ref_frame;
            aec_frame.near_end = (int16_t *)(void *)t31_audio.frame_buffer;
            aec_frame.reserved = t31_audio.frame_buffer;
            aec_frame.num_bytes = (int)bytes;
            (void)t31_audio.aec_process(t31_audio.aec, &aec_frame);
        }
        pthread_mutex_unlock(&t31_aec_lock);
    }
#endif
    t31_process_effects((int16_t *)t31_audio.frame_buffer,
                        (int)(bytes / sizeof(int16_t)));
    t31_apply_ai_volume((int16_t *)t31_audio.frame_buffer,
                        (int)(bytes / sizeof(int16_t)));
    memset(frame, 0, sizeof(*frame));
    frame->bitwidth = t31_audio.ai_attr.bitwidth;
    frame->soundmode = t31_audio.ai_attr.soundmode;
    frame->virAddr = (uint32_t *)(void *)t31_audio.frame_buffer;
    frame->timeStamp = timestamp;
    frame->seq = t31_audio.sequence++;
    frame->len = (int)bytes;
    t31_audio.frame_outstanding = 1;
    return 0;
}

int IMP_AI_ReleaseFrame(int device, int channel, IMPAudioFrame *frame)
{
    if ((device != 0 && device != 1) || channel != 0 || !frame ||
        !t31_audio.frame_outstanding ||
        frame->virAddr != (uint32_t *)(void *)t31_audio.frame_buffer)
        return -1;
    t31_audio.frame_outstanding = 0;
    return 0;
}

int IMP_AI_SetVol(int device, int channel, int value)
{
    if ((device != 0 && device != 1) || channel != 0 ||
        value < -30 || value > 120)
        return -1;
    t31_audio.ai_volume = value;
    return 0;
}

int IMP_AI_GetVol(int device, int channel, int *value)
{
    if ((device != 0 && device != 1) || channel != 0 || !value)
        return -1;
    *value = t31_audio.ai_volume;
    return 0;
}

int IMP_AI_SetGain(int device, int channel, int value)
{
    if ((device != 0 && device != 1) || channel != 0 ||
        value < 0 || value > 31)
        return -1;
#if defined(PLATFORM_T23)
    if (t31_audio.ai_fd >= 0 &&
        t23_set_gain(t31_audio.ai_fd, T23_AI_SET_GAIN, value) != 0)
        return -1;
#else
    if (t31_audio.ai_fd >= 0 &&
        ioctl(t31_audio.ai_fd, T31_AI_SET_GAIN, &value) != 0)
        return -1;
#endif
    t31_audio.ai_gain = value;
    return 0;
}

int IMP_AI_GetGain(int device, int channel, int *value)
{
    if ((device != 0 && device != 1) || channel != 0 || !value)
        return -1;
    *value = t31_audio.ai_gain;
    return 0;
}

int IMP_AI_SetAlcGain(int device, int channel, int value)
{
    int result = IMP_AI_SetGain(device, channel, value);
    if (result == 0)
        t31_audio.ai_alc_gain = value;
    return result;
}

int IMP_AI_GetAlcGain(int device, int channel, int *value)
{
    if ((device != 0 && device != 1) || channel != 0 || !value)
        return -1;
    *value = t31_audio.ai_alc_gain;
    return 0;
}

int IMP_AI_SetVolMute(int device, int channel, int mute)
{
    if ((device != 0 && device != 1) || channel != 0)
        return -1;
    t31_audio.ai_muted = mute != 0;
    return 0;
}

/* HPF state as libimp's _ai/_ao_InitializeFilter sets it up. Ingenic's
 * libaudioProcess runs WebRTC's HighPassFilter on {int16 y[4]; int16 x[2];
 * const int16 *ba;} and dereferences ba, which libimp points at
 * kFilterCoefficients (kFilterCoefficients8kHz at 8 kHz) or at
 * coefficients designed for a cut-off; libaudioProcess-neo overlays its
 * own float biquad on the same 32 bytes, designs it on the first call
 * (while b0 is still zero) and overwrites ba. A zero ba crashes the former. */
static const int16_t t31_hpf_coefficients[5] = {
    3665, -7330, 3665, 7285, -3280
};
static const int16_t t31_hpf_coefficients_8k[5] = {
    3798, -7596, 3798, 7807, -3733
};

static void t31_hpf_setup(int16_t state[16], const int16_t *coefficients)
{
    memset(state, 0, 16 * sizeof(int16_t));
    t31_audio.hpf_create(state + 4, state, 0, 0, 2, 4);
    memcpy((unsigned char *)state + 12, &coefficients, sizeof(coefficients));
}

int IMP_AI_EnableHpf(IMPAudioIOAttr *attribute)
{
    if (!t31_valid_attr(attribute) || t31_effects_load() != 0)
        return -1;
    t31_hpf_setup(t31_audio.hpf_state,
                  attribute->samplerate == 8000 ? t31_hpf_coefficients_8k
                                                : t31_hpf_coefficients);
    t31_audio.hpf_enabled = 1;
    return 0;
}

int IMP_AI_DisableHpf(void)
{
    if (t31_audio.hpf_enabled)
        t31_audio.hpf_free();
    t31_audio.hpf_enabled = 0;
    return 0;
}

/* libimp stores the cut-off for the next IMP_AI_EnableHpf and always
 * returns 0 (prudynt passes 0 to mean "default"). libaudioProcess-neo's HPF
 * designs its own fixed 300 Hz filter, so the value is only recorded. */
static int t31_hpf_cutoff;

int IMP_AI_SetHpfCoFrequency(int frequency)
{
    t31_hpf_cutoff = frequency;
    return 0;
}

int IMP_AI_EnableNs(IMPAudioIOAttr *attribute, int mode)
{
    /* Mode 4 is libaudioProcess-neo's non-pumping music/video profile. */
    if (!t31_valid_attr(attribute) || mode < 0 || mode > 4 ||
        t31_effects_load() != 0)
        return -1;
    if (!t31_audio.ns)
        t31_audio.ns = t31_audio.ns_create();
    if (!t31_audio.ns ||
        t31_audio.ns_set_config(t31_audio.ns, attribute->samplerate, mode) != 0)
        return -1;
    t31_audio.ns_enabled = 1;
    return 0;
}

int IMP_AI_DisableNs(void)
{
    if (t31_audio.ns)
        (void)t31_audio.ns_free(t31_audio.ns);
    t31_audio.ns = NULL;
    t31_audio.ns_enabled = 0;
    return 0;
}

int IMP_AI_EnableAgc(IMPAudioIOAttr *attribute, IMPAudioAgcConfig configuration)
{
    T31WebRtcAgcConfig config;

    if (!t31_valid_attr(attribute) || t31_effects_load() != 0)
        return -1;
    if (!t31_audio.agc)
        t31_audio.agc = t31_audio.agc_create();
    if (!t31_audio.agc)
        return -1;
    config.target_level_dbfs = (int16_t)configuration.TargetLevelDbfs;
    config.compression_gain_db = (int16_t)configuration.CompressionGaindB;
    config.limiter_enable = 1;
    if (t31_audio.agc_set_config(t31_audio.agc, 0, 255,
                                 t31_audio.agc_mode,
                                 attribute->samplerate, config) != 0)
        return -1;
    t31_audio.agc_enabled = 1;
    return 0;
}

int IMP_AI_DisableAgc(void)
{
    if (t31_audio.agc)
        (void)t31_audio.agc_free(t31_audio.agc);
    t31_audio.agc = NULL;
    t31_audio.agc_enabled = 0;
    return 0;
}

int IMP_AI_SetAgcMode(int mode)
{
    if (mode < 1 || mode > 3)
        mode = 2;
    t31_audio.agc_mode = mode;
    return 0;
}

int IMP_AI_Set_WebrtcProfileIni_Path(char *path)
{
    if (!path || strlen(path) >= sizeof(t31_audio.aec_profile))
        return -1;
    strcpy(t31_audio.aec_profile, path);
    return 0;
}

#if defined(PLATFORM_T23)
int IMP_AI_EnableAec(int ai_device, int ai_channel, int ao_device, int ao_channel)
{
    (void)ao_device;
    if ((ai_device != 0 && ai_device != 1) || ai_channel != 0 ||
        ao_channel != 0 || t31_audio.ai_fd < 0)
        return -1;
    {
        /* the driver stores the AI/AEC sample offset through the argument */
        int sample_offset = 0;

        return ioctl(t31_audio.ai_fd, T31_AI_ENABLE_AEC, &sample_offset);
    }
}

int IMP_AI_DisableAec(int ai_device, int ai_channel)
{
    if ((ai_device != 0 && ai_device != 1) || ai_channel != 0)
        return -1;
    return t31_audio.ai_fd >= 0
               ? ioctl(t31_audio.ai_fd, T31_AI_DISABLE_AEC, 0)
               : 0;
}

int IMP_AI_EnableAecRefFrame(int ai_device, int ai_channel, int ao_device,
                             int ao_channel)
{
    return IMP_AI_EnableAec(ai_device, ai_channel, ao_device, ao_channel);
}

int IMP_AI_DisableAecRefFrame(int ai_device, int ai_channel, int ao_device,
                              int ao_channel)
{
    (void)ao_device;
    (void)ao_channel;
    return IMP_AI_DisableAec(ai_device, ai_channel);
}

int IMP_AI_GetFrameAndRef(int device, int channel, IMPAudioFrame *frame,
                          IMPAudioFrame *reference, IMPBlock block)
{
    int result = IMP_AI_GetFrame(device, channel, frame, block);
    if (result == 0 && reference)
        memset(reference, 0, sizeof(*reference));
    return result;
}
#else
/* Turn the driver's playback reference on or off; the capture thread then
 * queues it next to the microphone data. */
static int t31_reference_set(int on)
{
    int result;

    if (t31_audio.ai_fd < 0)
        return -1;
    pthread_mutex_lock(&t31_capture_lock);
    if (on == t31_audio.capture_ref_on) {
        pthread_mutex_unlock(&t31_capture_lock);
        return 0;
    }
    if (on && t31_ref_reserve(t31_audio.capture_capacity) != 0) {
        pthread_mutex_unlock(&t31_capture_lock);
        return -1;
    }
    result = ioctl(t31_audio.ai_fd, on ? T31_AI_ENABLE_AEC : T31_AI_DISABLE_AEC,
                   on);
    if (result == 0) {
        t31_audio.capture_ref_on = on;
        if (on && t31_audio.capture_ref)
            memset(t31_audio.capture_ref, 0, t31_audio.capture_ref_capacity);
    }
    pthread_mutex_unlock(&t31_capture_lock);
    return result;
}

/* libaudioProcess' WebRTC AEC, as libimp loads it for IMP_AI_EnableAec */
static int t31_aec_load(void)
{
    if (t31_audio.aec_process)
        return 0;
    if (t31_effects_load() != 0)
        return -1;
    *(void **)(&t31_audio.aec_create) =
        dlsym(t31_audio.effects_library, "audio_process_aec_create");
    *(void **)(&t31_audio.aec_free) =
        dlsym(t31_audio.effects_library, "audio_process_aec_free");
    *(void **)(&t31_audio.aec_process) =
        dlsym(t31_audio.effects_library, "audio_process_aec_process");
    if (!t31_audio.aec_create || !t31_audio.aec_free ||
        !t31_audio.aec_process) {
        t31_audio.aec_create = NULL;
        t31_audio.aec_free = NULL;
        t31_audio.aec_process = NULL;
        return -1;
    }
    return 0;
}

static void t31_aec_release(void)
{
    pthread_mutex_lock(&t31_aec_lock);
    if (t31_audio.aec)
        (void)t31_audio.aec_free(t31_audio.aec);
    t31_audio.aec = NULL;
    pthread_mutex_unlock(&t31_aec_lock);
}

/* Echo cancellation needs a mono 8/16 kHz microphone. Without
 * libaudioProcess' AEC the driver reference is still switched on, which is
 * what OpenIMP did before, so the call keeps succeeding there. */
int IMP_AI_EnableAec(int ai_device, int ai_channel, int ao_device, int ao_channel)
{
    void *aec = NULL;
    int result;

    (void)ao_device;
    if ((ai_device != 0 && ai_device != 1) || ai_channel != 0 ||
        ao_channel != 0 || t31_audio.ai_fd < 0)
        return -1;
    pthread_mutex_lock(&t31_aec_lock);
    if (!t31_audio.aec &&
        t31_audio.ai_attr.soundmode == AUDIO_SOUND_MODE_MONO &&
        (t31_audio.ai_attr.samplerate == AUDIO_SAMPLE_RATE_8000 ||
         t31_audio.ai_attr.samplerate == AUDIO_SAMPLE_RATE_16000) &&
        t31_aec_load() == 0)
        aec = t31_audio.aec_create(t31_audio.ai_attr.samplerate,
                                   t31_audio.aec_profile[0]
                                       ? t31_audio.aec_profile
                                       : NULL);
    pthread_mutex_unlock(&t31_aec_lock);
    result = t31_reference_set(1);
    pthread_mutex_lock(&t31_aec_lock);
    if (result == 0 && aec && !t31_audio.aec) {
        t31_audio.aec = aec;
        aec = NULL;
    }
    if (aec)
        (void)t31_audio.aec_free(aec);
    pthread_mutex_unlock(&t31_aec_lock);
    return result;
}

int IMP_AI_DisableAec(int ai_device, int ai_channel)
{
    if ((ai_device != 0 && ai_device != 1) || ai_channel != 0)
        return -1;
    t31_aec_release();
    if (t31_audio.ref_frames || t31_audio.ai_fd < 0)
        return 0;
    return t31_reference_set(0);
}

/* Reference frames only, no processing (libimp: AEC and reference frames
 * are alternatives). */
int IMP_AI_EnableAecRefFrame(int ai_device, int ai_channel, int ao_device,
                             int ao_channel)
{
    (void)ao_device;
    if ((ai_device != 0 && ai_device != 1) || ai_channel != 0 ||
        ao_channel != 0 || t31_reference_set(1) != 0)
        return -1;
    t31_audio.ref_frames = 1;
    return 0;
}

int IMP_AI_DisableAecRefFrame(int ai_device, int ai_channel, int ao_device,
                              int ao_channel)
{
    int aec;

    (void)ao_device;
    (void)ao_channel;
    if ((ai_device != 0 && ai_device != 1) || ai_channel != 0)
        return -1;
    t31_audio.ref_frames = 0;
    pthread_mutex_lock(&t31_aec_lock);
    aec = t31_audio.aec != NULL;
    pthread_mutex_unlock(&t31_aec_lock);
    if (aec || t31_audio.ai_fd < 0)
        return 0;
    return t31_reference_set(0);
}

/* The reference is the playback signal the driver recorded alongside the
 * microphone frame (silence if the reference is off). */
int IMP_AI_GetFrameAndRef(int device, int channel, IMPAudioFrame *frame,
                          IMPAudioFrame *reference, IMPBlock block)
{
    int result = IMP_AI_GetFrame(device, channel, frame, block);

    if (result != 0 || !reference)
        return result;
    *reference = *frame;
    if (t31_audio.ref_frame_valid) {
        reference->virAddr = (uint32_t *)(void *)t31_audio.ref_frame;
    } else {
        memset(reference, 0, sizeof(*reference));
    }
    return 0;
}
#endif

int IMP_AO_SetPubAttr(int device, IMPAudioIOAttr *attribute)
{
    if (device != 0 || t31_audio.ao_enabled || !t31_valid_attr(attribute))
        return -1;
    t31_audio.ao_attr = *attribute;
    return 0;
}

int IMP_AO_GetPubAttr(int device, IMPAudioIOAttr *attribute)
{
    if (device != 0 || !attribute)
        return -1;
    *attribute = t31_audio.ao_attr;
    return 0;
}

/* Playback fragment of the driver. Both kernel ABIs copy whole fragments
 * only and keep a write that ends in a partial one waiting for good: the
 * T23 OSS3 driver in 20 ms units (T23_FRAGMENT_10MS_UNITS), the OSS2 driver
 * of T31/T30/T21/T20 in 10 ms units (xb_snd_dsp.c ao_copy_from_user,
 * xb47xx_i2s_v12.c SND_DSP_GET_REPLAY_FRAGMENTSIZE). */
static size_t t31_ao_fragment_bytes(const IMPAudioIOAttr *attribute)
{
#if defined(PLATFORM_T23)
    return t23_fragment_bytes(attribute);
#else
    size_t channels =
        attribute->soundmode == AUDIO_SOUND_MODE_STEREO ? 2U : 1U;

    return (size_t)(attribute->samplerate / 100) * channels * sizeof(int16_t);
#endif
}

int IMP_AO_Enable(int device)
{
    int fd;

    if (device != 0)
        return -1;
    if (t31_audio.ao_enabled)
        return 0;
    {
        size_t fragment = t31_ao_fragment_bytes(&t31_audio.ao_attr);
        size_t period = (size_t)t31_audio.ao_attr.numPerFrm *
                        (t31_audio.ao_attr.soundmode ==
                                 AUDIO_SOUND_MODE_STEREO ? 2U : 1U) *
                        sizeof(int16_t);

        /* write whole fragments only: round the period down to one */
        period -= period % fragment;
        if (!fragment || !period)
            period = fragment;
        if (!period || period > OPENIMP_AO_MAX_PERIOD_BYTES)
            return -1;
        if (period > t31_audio.ao_period_capacity) {
            void *buffer = realloc(t31_audio.ao_period, period);
            if (!buffer)
                return -1;
            t31_audio.ao_period = buffer;
            t31_audio.ao_period_capacity = period;
        }
        t31_audio.ao_period_bytes = period;
        t31_audio.ao_period_valid = 0;
    }
    fd = open("/dev/dsp", O_WRONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
#if defined(PLATFORM_T23)
    if (t23_configure_fd(fd, &t31_audio.ao_attr, T23_AO_SET_PARAM,
                         T23_AO_ENABLE_STREAM) != 0) {
#else
    if (t31_configure_fd(fd, &t31_audio.ao_attr) != 0) {
#endif
        close(fd);
        return -1;
    }
    t31_audio.ao_fd = fd;
    t31_audio.ao_enabled = 1;
    return 0;
}

int IMP_AO_Disable(int device)
{
    int result = 0;

    if (device != 0)
        return -1;
    if (t31_audio.ao_fd >= 0) {
        if (t31_audio.ao_enabled)
#if defined(PLATFORM_T23)
            result = ioctl(t31_audio.ao_fd, T23_AO_DISABLE_STREAM, 1);
#else
            result = ioctl(t31_audio.ao_fd, T31_DISABLE_STREAM, 1);
#endif
        close(t31_audio.ao_fd);
    }
    t31_audio.ao_fd = -1;
    t31_audio.ao_enabled = 0;
    t31_audio.ao_channel_enabled = 0;
    t31_audio.ao_paused = 0;
    t31_audio.ao_period_valid = 0;
    return result;
}

int IMP_AO_EnableChn(int device, int channel)
{
    if (device != 0 || channel != 0 || !t31_audio.ao_enabled)
        return -1;
    t31_audio.ao_channel_enabled = 1;
    return 0;
}

int IMP_AO_DisableChn(int device, int channel)
{
    if (device != 0 || channel != 0)
        return -1;
    t31_audio.ao_channel_enabled = 0;
    return 0;
}

/* AO effects, separate from the microphone ones as in libimp (ao.c has
 * its own handle_ao_hpf, AoCoefficients, handle_ao_agc and ao_agc_mode):
 * IMP_AO_SendFrame runs HPF, then AGC in 10 ms blocks, then the volume. */
static struct {
    int16_t hpf_state[16];
    int16_t hpf_coefficients[5];
    int hpf_cutoff;                 /* IMP_AO_SetHpfCoFrequency, 0 = default */
    int hpf_enabled;
    void *agc;
    int agc_mode;                   /* bss in libimp: 0 unless EnableAlgo */
    int agc_enabled;
    int sample_rate;
} t31_ao_fx;

static pthread_mutex_t t31_ao_fx_lock = PTHREAD_MUTEX_INITIALIZER;

/* tan() for 0 <= x < pi/2 from the sine and cosine series, so libimp
 * keeps not depending on libm. */
static double t31_tan(double x)
{
    double sine = x, cosine = 1.0, term_s = x, term_c = 1.0;
    int n;

    for (n = 1; n < 24; n++) {
        term_s *= -x * x / (double)((2 * n) * (2 * n + 1));
        term_c *= -x * x / (double)((2 * n - 1) * (2 * n));
        sine += term_s;
        cosine += term_c;
    }
    return sine / cosine;
}

/* libimp Hpf_gen_filter_coefficients: 2nd-order Butterworth high pass,
 * bilinear transform, 4096 = 1.0, same float/double steps. */
static void t31_hpf_design(int16_t coefficients[5], int sample_rate,
                           int cutoff)
{
    float k = (float)t31_tan((double)((float)cutoff / (float)sample_rate) *
                             3.14159265358979311600);
    double ks = (double)k * 1.41421356237309514547;
    float k2 = k * k;
    float denominator = (float)((double)k2 + ks + 1.0);
    float pole = (float)((double)k2 - ks + 1.0);
    float a1 = -((k + k) * k - 2.0f);
    int16_t b0 = (int16_t)(int)(1.0f / denominator * 4096.0f);

    coefficients[0] = b0;
    coefficients[1] = (int16_t)(-2 * b0);
    coefficients[2] = b0;
    coefficients[3] = (int16_t)(int)(a1 / denominator * 4096.0f);
    coefficients[4] = (int16_t)(int)(-pole / denominator * 4096.0f);
}

/* Called with t31_ao_fx_lock held. */
static void t31_ao_process_effects(int16_t *samples, int count)
{
    int frame_samples = t31_ao_fx.sample_rate / 100;
    int offset;

    if (t31_ao_fx.hpf_enabled)
        (void)t31_audio.hpf_process(t31_ao_fx.hpf_state, samples, count);
    if (!t31_ao_fx.agc_enabled || !t31_ao_fx.agc || frame_samples <= 0 ||
        frame_samples > 160)
        return;
    for (offset = 0; offset + frame_samples <= count; offset += frame_samples) {
        const int16_t *inputs[1] = { samples + offset };
        int16_t *outputs[1] = { samples + offset };
        int32_t output_level = 0;
        uint8_t saturated = 0;

        (void)t31_audio.agc_process(t31_ao_fx.agc, inputs, 1,
                                    (size_t)frame_samples, outputs, 127,
                                    &output_level, 0, &saturated);
    }
}

/* T23 extras (src/t23/openimp_t23_audio_ext.c): the [AGC_AO] set_mode of
 * the WebRTC profile, applied by the next IMP_AO_EnableAgc. */
void openimp_audio_set_ao_agc_mode(int mode);
void openimp_audio_set_ao_agc_mode(int mode)
{
    pthread_mutex_lock(&t31_ao_fx_lock);
    t31_ao_fx.agc_mode = mode;
    pthread_mutex_unlock(&t31_ao_fx_lock);
}

static int t31_ao_write_period(void)
{
    T31AudioOutputStream stream;
    int result;

    pthread_mutex_lock(&t31_ao_fx_lock);
    if (t31_ao_fx.hpf_enabled || t31_ao_fx.agc_enabled)
        t31_ao_process_effects((int16_t *)(void *)t31_audio.ao_period,
                               (int)(t31_audio.ao_period_valid /
                                     sizeof(int16_t)));
    pthread_mutex_unlock(&t31_ao_fx_lock);
    t31_apply_ao_volume((int16_t *)(void *)t31_audio.ao_period,
                     (int)(t31_audio.ao_period_valid / sizeof(int16_t)),
                     t31_audio.ao_volume, t31_audio.ao_muted);
    stream.data = t31_audio.ao_period;
    stream.size = (uint32_t)t31_audio.ao_period_valid;
    /* blocks until the driver took the whole period (OSS3: 800 ms timeout;
     * OSS2 returns 0 and the count in stream.size, a period is taken whole) */
    result = ioctl(t31_audio.ao_fd, T31_AO_SET_STREAM, &stream);
    t31_audio.ao_period_valid = 0;
    return result == 0 ? 0 : -1;
}

/* The driver takes whole fragments only, so frames of any length (a
 * backchannel delivers 20 ms RTP payloads, AAC 1024-sample blocks, ...) are
 * collected into fragment-aligned periods; a partial period stays queued
 * until more data arrives or IMP_AO_FlushChnBuf pads it out. */
int IMP_AO_SendFrame(int device, int channel, IMPAudioFrame *frame,
                     IMPBlock block)
{
    const unsigned char *data;
    size_t remaining;

    (void)block;
    if (device != 0 || channel != 0 || !frame || !frame->virAddr ||
        frame->len <= 0 || !t31_audio.ao_channel_enabled ||
        t31_audio.ao_paused || t31_audio.ao_fd < 0 ||
        !t31_audio.ao_period || !t31_audio.ao_period_bytes)
        return -1;
    data = (const unsigned char *)(const void *)frame->virAddr;
    remaining = (size_t)frame->len;
    while (remaining > 0) {
        size_t take = t31_audio.ao_period_bytes - t31_audio.ao_period_valid;

        if (take > remaining)
            take = remaining;
        memcpy(t31_audio.ao_period + t31_audio.ao_period_valid, data, take);
        t31_audio.ao_period_valid += take;
        data += take;
        remaining -= take;
        if (t31_audio.ao_period_valid == t31_audio.ao_period_bytes &&
            t31_ao_write_period() != 0)
            return -1;
    }
    return 0;
}

int IMP_AO_SetVol(int device, int channel, int value)
{
    if (device != 0 || channel != 0 || value < -30 || value > 120)
        return -1;
    t31_audio.ao_volume = value;
    return 0;
}

int IMP_AO_GetVol(int device, int channel, int *value)
{
    if (device != 0 || channel != 0 || !value)
        return -1;
    *value = t31_audio.ao_volume;
    return 0;
}

int IMP_AO_SetGain(int device, int channel, int value)
{
    if (device != 0 || channel != 0 || value < 0 || value > 31)
        return -1;
#if defined(PLATFORM_T23)
    if (t31_audio.ao_fd >= 0 &&
        t23_set_gain(t31_audio.ao_fd, T23_AO_SET_GAIN, value) != 0)
        return -1;
#else
    if (t31_audio.ao_fd >= 0 &&
        ioctl(t31_audio.ao_fd, T31_AO_SET_GAIN, &value) != 0)
        return -1;
#endif
    t31_audio.ao_gain = value;
    return 0;
}

int IMP_AO_GetGain(int device, int channel, int *value)
{
    if (device != 0 || channel != 0 || !value)
        return -1;
    *value = t31_audio.ao_gain;
    return 0;
}

int IMP_AO_SetVolMute(int device, int channel, int mute)
{
    if (device != 0 || channel != 0)
        return -1;
    t31_audio.ao_muted = mute != 0;
    return 0;
}

int IMP_AO_ClearChnBuf(int device, int channel)
{
    if (device != 0 || channel != 0 || t31_audio.ao_fd < 0)
        return -1;
    t31_audio.ao_period_valid = 0;
    return ioctl(t31_audio.ao_fd, T31_AO_CLEAR_STREAM, 1);
}

int IMP_AO_FlushChnBuf(int device, int channel)
{
    if (device != 0 || channel != 0 || t31_audio.ao_fd < 0)
        return -1;
    /* play out the queued partial period, padded with silence */
    if (t31_audio.ao_period_valid) {
        memset(t31_audio.ao_period + t31_audio.ao_period_valid, 0,
               t31_audio.ao_period_bytes - t31_audio.ao_period_valid);
        t31_audio.ao_period_valid = t31_audio.ao_period_bytes;
        if (t31_ao_write_period() != 0)
            return -1;
    }
    return ioctl(t31_audio.ao_fd, T31_AO_SYNC_STREAM, 1);
}

int IMP_AO_PauseChn(int device, int channel)
{
    if (device != 0 || channel != 0)
        return -1;
    t31_audio.ao_paused = 1;
    return 0;
}

int IMP_AO_ResumeChn(int device, int channel)
{
    if (device != 0 || channel != 0)
        return -1;
    t31_audio.ao_paused = 0;
    return 0;
}

int IMP_AO_QueryChnStat(int device, int channel, IMPAudioOChnState *status)
{
    if (device != 0 || channel != 0 || !status)
        return -1;
    memset(status, 0, sizeof(*status));
    status->chnTotalNum = t31_audio.ao_attr.frmNum;
    status->chnFreeNum = t31_audio.ao_attr.frmNum;
    return 0;
}

int IMP_AO_CacheSwitch(int device, int channel, int enable)
{
    return device == 0 && channel == 0 && (enable == 0 || enable == 1)
               ? 0
               : -1;
}

int IMP_AO_Soft_Mute(int device, int channel)
{
    return IMP_AO_SetVolMute(device, channel, 1);
}

int IMP_AO_Soft_UNMute(int device, int channel)
{
    return IMP_AO_SetVolMute(device, channel, 0);
}

/* The AO effects used to forward to the IMP_AI_* ones and so filtered the
 * microphone (and an AO disable switched the microphone's off). */
int IMP_AO_EnableHpf(IMPAudioIOAttr *attribute)
{
    int cutoff;

    if (!t31_valid_attr(attribute) || t31_effects_load() != 0)
        return -1;
    pthread_mutex_lock(&t31_ao_fx_lock);
    cutoff = t31_ao_fx.hpf_cutoff;
    if (cutoff < 0 || cutoff * 2 >= (int)attribute->samplerate) {
        pthread_mutex_unlock(&t31_ao_fx_lock);
        return -1;                  /* "HPF cut-off frequency is illegal" */
    }
    if (cutoff)
        t31_hpf_design(t31_ao_fx.hpf_coefficients, attribute->samplerate,
                       cutoff);
    else
        memcpy(t31_ao_fx.hpf_coefficients,
               attribute->samplerate == 8000 ? t31_hpf_coefficients_8k
                                             : t31_hpf_coefficients,
               sizeof(t31_ao_fx.hpf_coefficients));
    t31_hpf_setup(t31_ao_fx.hpf_state, t31_ao_fx.hpf_coefficients);
    t31_ao_fx.sample_rate = attribute->samplerate;
    t31_ao_fx.hpf_enabled = 1;
    pthread_mutex_unlock(&t31_ao_fx_lock);
    return 0;
}

int IMP_AO_DisableHpf(void)
{
    pthread_mutex_lock(&t31_ao_fx_lock);
    t31_ao_fx.hpf_enabled = 0;
    pthread_mutex_unlock(&t31_ao_fx_lock);
    return 0;
}

int IMP_AO_SetHpfCoFrequency(int frequency)
{
    if (frequency < 0)
        return -1;
    pthread_mutex_lock(&t31_ao_fx_lock);
    t31_ao_fx.hpf_cutoff = frequency;
    pthread_mutex_unlock(&t31_ao_fx_lock);
    return 0;
}

int IMP_AO_EnableAgc(IMPAudioIOAttr *attribute,
                     IMPAudioAgcConfig configuration)
{
    T31WebRtcAgcConfig config;
    int result = -1;

    if (!t31_valid_attr(attribute) || t31_effects_load() != 0)
        return -1;
    pthread_mutex_lock(&t31_ao_fx_lock);
    if (!t31_ao_fx.agc)
        t31_ao_fx.agc = t31_audio.agc_create();
    if (t31_ao_fx.agc) {
        config.target_level_dbfs = (int16_t)configuration.TargetLevelDbfs;
        config.compression_gain_db =
            (int16_t)configuration.CompressionGaindB;
        config.limiter_enable = 1;
        if (t31_audio.agc_set_config(t31_ao_fx.agc, 0, 255,
                                     t31_ao_fx.agc_mode,
                                     attribute->samplerate, config) == 0) {
            t31_ao_fx.sample_rate = attribute->samplerate;
            t31_ao_fx.agc_enabled = 1;
            result = 0;
        }
    }
    pthread_mutex_unlock(&t31_ao_fx_lock);
    return result;
}

int IMP_AO_DisableAgc(void)
{
    pthread_mutex_lock(&t31_ao_fx_lock);
    if (t31_ao_fx.agc)
        (void)t31_audio.agc_free(t31_ao_fx.agc);
    t31_ao_fx.agc = NULL;
    t31_ao_fx.agc_enabled = 0;
    pthread_mutex_unlock(&t31_ao_fx_lock);
    return 0;
}
