/* T31 audio I/O over the stock /dev/dsp ABI.
 *
 * The codec driver owns capture and playback.  Audio effects remain in
 * gtxaspec's libaudioProcess-neo and are resolved lazily from the installed
 * libaudioProcess.so, matching the division of responsibilities used by the
 * OpenIMP T40/T41 implementation.
 */

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <imp/imp_audio.h>

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

typedef struct {
    void *data;
    void *aec;
    uint32_t size;
} T31AudioInputStream;

typedef struct {
    void *data;
    uint32_t size;
} T31AudioOutputStream;

typedef struct {
    int16_t target_level_dbfs;
    int16_t compression_gain_db;
    uint8_t limiter_enable;
} T31WebRtcAgcConfig;

_Static_assert(sizeof(T31AudioInputStream) == 12,
               "T31 audio input stream ABI mismatch");
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
    unsigned char *capture_buffer;
    size_t capture_capacity;
    size_t capture_valid;
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
} t31_audio = {
    .ai_fd = -1,
    .ao_fd = -1,
    .ai_volume = 60,
    .ao_volume = 60,
    .agc_mode = 3,
};

extern int64_t IMP_System_GetTimeStamp(void);

static int t31_valid_attr(const IMPAudioIOAttr *attribute)
{
    return attribute && attribute->samplerate > 0 &&
           attribute->bitwidth == AUDIO_BIT_WIDTH_16 &&
           (attribute->soundmode == AUDIO_SOUND_MODE_MONO ||
            attribute->soundmode == AUDIO_SOUND_MODE_STEREO) &&
           attribute->numPerFrm > 0;
}

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

static void t31_process_effects(int16_t *samples, int count)
{
    int sample_rate = t31_audio.ai_attr.samplerate;
    int frame_samples = sample_rate / 100;
    int offset;

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
    if (t31_configure_fd(fd, &t31_audio.ai_attr) != 0) {
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
    if (t31_audio.ai_fd >= 0) {
        if (t31_audio.ai_enabled)
            result = ioctl(t31_audio.ai_fd, T31_DISABLE_STREAM, 1);
        close(t31_audio.ai_fd);
    }
    t31_audio.ai_fd = -1;
    t31_audio.ai_enabled = 0;
    t31_audio.ai_channel_enabled = 0;
    t31_audio.frame_outstanding = 0;
    t31_audio.capture_valid = 0;
    return result;
}

int IMP_AI_EnableChn(int device, int channel)
{
    if ((device != 0 && device != 1) || channel != 0 ||
        !t31_audio.ai_enabled)
        return -1;
    t31_audio.ai_channel_enabled = 1;
    return 0;
}

int IMP_AI_DisableChn(int device, int channel)
{
    if ((device != 0 && device != 1) || channel != 0)
        return -1;
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
    (void)timeout_ms;
    return (device == 0 || device == 1) && channel == 0 &&
                   t31_audio.ai_channel_enabled &&
                   !t31_audio.frame_outstanding
               ? 0
               : -1;
}

static int t31_capture_fill(size_t required)
{
    T31AudioInputStream stream;
    size_t capacity = required + T31_CAPTURE_CHUNK_BYTES;

    if (capacity > t31_audio.capture_capacity) {
        void *buffer = realloc(t31_audio.capture_buffer, capacity);
        if (!buffer)
            return -1;
        t31_audio.capture_buffer = buffer;
        t31_audio.capture_capacity = capacity;
    }
    while (t31_audio.capture_valid < required) {
        memset(&stream, 0, sizeof(stream));
        stream.data = t31_audio.capture_buffer + t31_audio.capture_valid;
        stream.size = T31_CAPTURE_CHUNK_BYTES;
        if (ioctl(t31_audio.ai_fd, T31_AI_GET_STREAM, &stream) != 0)
            return -1;
        t31_audio.capture_valid += T31_CAPTURE_CHUNK_BYTES;
    }
    return 0;
}

int IMP_AI_GetFrame(int device, int channel, IMPAudioFrame *frame,
                    IMPBlock block)
{
    size_t bytes;
    unsigned int channels;

    (void)block;
    if ((device != 0 && device != 1) || channel != 0 || !frame ||
        !t31_audio.ai_channel_enabled || t31_audio.frame_outstanding)
        return -1;
    channels = t31_audio.ai_attr.soundmode == AUDIO_SOUND_MODE_STEREO ? 2U : 1U;
    bytes = (size_t)t31_audio.ai_attr.numPerFrm * channels * sizeof(int16_t);
    if (!bytes || t31_capture_fill(bytes) != 0)
        return -1;
    if (bytes > t31_audio.frame_capacity) {
        void *buffer = realloc(t31_audio.frame_buffer, bytes);
        if (!buffer)
            return -1;
        t31_audio.frame_buffer = buffer;
        t31_audio.frame_capacity = bytes;
    }
    memcpy(t31_audio.frame_buffer, t31_audio.capture_buffer, bytes);
    t31_audio.capture_valid -= bytes;
    if (t31_audio.capture_valid)
        memmove(t31_audio.capture_buffer, t31_audio.capture_buffer + bytes,
                t31_audio.capture_valid);
    t31_process_effects((int16_t *)t31_audio.frame_buffer,
                        (int)(bytes / sizeof(int16_t)));
    t31_apply_ai_volume((int16_t *)t31_audio.frame_buffer,
                        (int)(bytes / sizeof(int16_t)));
    memset(frame, 0, sizeof(*frame));
    frame->bitwidth = t31_audio.ai_attr.bitwidth;
    frame->soundmode = t31_audio.ai_attr.soundmode;
    frame->virAddr = (uint32_t *)(void *)t31_audio.frame_buffer;
    frame->timeStamp = IMP_System_GetTimeStamp();
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
    if (t31_audio.ai_fd >= 0 &&
        ioctl(t31_audio.ai_fd, T31_AI_SET_GAIN, &value) != 0)
        return -1;
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

int IMP_AI_EnableHpf(IMPAudioIOAttr *attribute)
{
    if (!t31_valid_attr(attribute) || t31_effects_load() != 0)
        return -1;
    memset(t31_audio.hpf_state, 0, sizeof(t31_audio.hpf_state));
    t31_audio.hpf_create(t31_audio.hpf_state, t31_audio.hpf_state + 8,
                         0, 0, 8, 8);
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

int IMP_AI_SetHpfCoFrequency(int frequency)
{
    return frequency > 0 ? 0 : -1;
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

int IMP_AI_EnableAec(int ai_device, int ai_channel, int ao_device, int ao_channel)
{
    (void)ao_device;
    if ((ai_device != 0 && ai_device != 1) || ai_channel != 0 ||
        ao_channel != 0 || t31_audio.ai_fd < 0)
        return -1;
    return ioctl(t31_audio.ai_fd, T31_AI_ENABLE_AEC, 1);
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

int IMP_AO_Enable(int device)
{
    int fd;

    if (device != 0)
        return -1;
    if (t31_audio.ao_enabled)
        return 0;
    fd = open("/dev/dsp", O_WRONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    if (t31_configure_fd(fd, &t31_audio.ao_attr) != 0) {
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
            result = ioctl(t31_audio.ao_fd, T31_DISABLE_STREAM, 1);
        close(t31_audio.ao_fd);
    }
    t31_audio.ao_fd = -1;
    t31_audio.ao_enabled = 0;
    t31_audio.ao_channel_enabled = 0;
    t31_audio.ao_paused = 0;
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

int IMP_AO_SendFrame(int device, int channel, IMPAudioFrame *frame,
                     IMPBlock block)
{
    T31AudioOutputStream stream;
    unsigned char *data;
    int remaining;

    (void)block;
    if (device != 0 || channel != 0 || !frame || !frame->virAddr ||
        frame->len <= 0 || !t31_audio.ao_channel_enabled ||
        t31_audio.ao_paused || t31_audio.ao_fd < 0)
        return -1;
    data = (unsigned char *)(void *)frame->virAddr;
    remaining = frame->len;
    while (remaining > 0) {
        int written;

        stream.data = data;
        stream.size = (uint32_t)remaining;
        written = ioctl(t31_audio.ao_fd, T31_AO_SET_STREAM, &stream);
        if (written < 0)
            return -1;
        if (written == 0)
            return -1;
        if (written > remaining)
            written = remaining;
        data += written;
        remaining -= written;
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
    if (t31_audio.ao_fd >= 0 &&
        ioctl(t31_audio.ao_fd, T31_AO_SET_GAIN, &value) != 0)
        return -1;
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
    return device == 0 && channel == 0 && t31_audio.ao_fd >= 0
               ? ioctl(t31_audio.ao_fd, T31_AO_CLEAR_STREAM, 1)
               : -1;
}

int IMP_AO_FlushChnBuf(int device, int channel)
{
    return device == 0 && channel == 0 && t31_audio.ao_fd >= 0
               ? ioctl(t31_audio.ao_fd, T31_AO_SYNC_STREAM, 1)
               : -1;
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

int IMP_AO_EnableHpf(IMPAudioIOAttr *attribute)
{
    return IMP_AI_EnableHpf(attribute);
}

int IMP_AO_DisableHpf(void)
{
    return IMP_AI_DisableHpf();
}

int IMP_AO_SetHpfCoFrequency(int frequency)
{
    return IMP_AI_SetHpfCoFrequency(frequency);
}

int IMP_AO_EnableAgc(IMPAudioIOAttr *attribute,
                     IMPAudioAgcConfig configuration)
{
    return IMP_AI_EnableAgc(attribute, configuration);
}

int IMP_AO_DisableAgc(void)
{
    return IMP_AI_DisableAgc();
}
