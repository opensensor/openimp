/* P3 T40 audio lifecycle over the open OSS3 /dev/dsp ABI.
 *
 * Ingenic's libaudioProcess is intentionally not copied into this tree.  The
 * effects entry points are resolved from the installed libaudioProcess-neo at
 * runtime, matching the OEM libimp contract while keeping this library's
 * hardware and DSP responsibilities separate.
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

#define P3_SIOR(number, type) _IOC(_IOC_READ, 'P', (number), sizeof(type))

typedef struct {
    uint32_t rate;
    uint16_t format;
    uint16_t channel;
} P3AudioParameter;

typedef struct {
    void *data;
    uint32_t size;
    void *aec;
    uint32_t aec_size;
} P3AudioInputStream;

typedef struct {
    void *data;
    uint32_t size;
} P3AudioOutputStream;

typedef struct {
    uint32_t channel;
    uint32_t gain[2];
} P3AudioVolume;

typedef struct {
    uint32_t channel;
    uint32_t mute;
} P3AudioMute;

typedef struct {
    int16_t target_level_dbfs;
    int16_t compression_gain_db;
    uint8_t limiter_enable;
} P3WebRtcAgcConfig;

#define AMIC_AI_SET_PARAM P3_SIOR(113, P3AudioParameter)
#define AMIC_AI_GET_PARAM P3_SIOR(112, P3AudioParameter)
#define AMIC_AO_SET_PARAM P3_SIOR(111, P3AudioParameter)
#define AMIC_AO_GET_PARAM P3_SIOR(110, P3AudioParameter)
#define AMIC_AO_SYNC_STREAM P3_SIOR(101, int)
#define AMIC_AO_CLEAR_STREAM P3_SIOR(100, int)
#define AMIC_AO_SET_STREAM P3_SIOR(99, P3AudioOutputStream)
#define AMIC_AI_GET_STREAM P3_SIOR(98, P3AudioInputStream)
#define AMIC_AI_DISABLE_STREAM P3_SIOR(97, int)
#define AMIC_AI_ENABLE_STREAM P3_SIOR(96, int)
#define AMIC_AO_DISABLE_STREAM P3_SIOR(95, int)
#define AMIC_AO_ENABLE_STREAM P3_SIOR(94, int)
#define AMIC_ENABLE_AEC P3_SIOR(93, int)
#define AMIC_DISABLE_AEC P3_SIOR(92, int)
#define AMIC_AI_SET_VOLUME P3_SIOR(91, P3AudioVolume)
#define AMIC_AI_SET_GAIN P3_SIOR(90, P3AudioVolume)
#define AMIC_SPK_SET_VOLUME P3_SIOR(89, P3AudioVolume)
#define AMIC_SPK_SET_GAIN P3_SIOR(88, P3AudioVolume)
#define AMIC_AI_GET_VOLUME P3_SIOR(86, P3AudioVolume)
#define AMIC_AI_GET_GAIN P3_SIOR(85, P3AudioVolume)
#define AMIC_SPK_GET_VOLUME P3_SIOR(84, P3AudioVolume)
#define AMIC_SPK_GET_GAIN P3_SIOR(83, P3AudioVolume)
#define AMIC_AI_SET_MUTE P3_SIOR(78, P3AudioMute)
#define AMIC_SPK_SET_MUTE P3_SIOR(77, P3AudioMute)

typedef void (*P3HpfCreate)(int16_t *, int16_t *, int16_t, int16_t, int, int);
typedef int (*P3HpfProcess)(int16_t *, int16_t *, int);
typedef void (*P3HpfFree)(void);
typedef void *(*P3NsCreate)(void);
typedef int (*P3NsSetConfig)(void *, int, int);
typedef void (*P3NsProcess)(void *, const float *const *, int,
                            float *const *);
typedef int (*P3NsFree)(void *);
typedef void *(*P3AgcCreate)(void);
typedef int (*P3AgcSetConfig)(void *, int, int, int, int,
                              P3WebRtcAgcConfig);
typedef int (*P3AgcProcess)(void *, const int16_t *const *, size_t, size_t,
                            int16_t *const *, int32_t, int32_t *, int16_t,
                            uint8_t *);
typedef int (*P3AgcFree)(void *);

static struct {
    int fd;
    int ai_enabled;
    int ai_channel_enabled;
    int ao_enabled;
    int ao_channel_enabled;
    int ao_paused;
    IMPAudioIOAttr ai_attr;
    IMPAudioIOAttr ao_attr;
    IMPAudioIChnParam ai_channel;
    int ai_volume;
    int ai_gain;
    int ao_volume;
    int ao_gain;
    unsigned char *frame_buffer;
    size_t frame_capacity;
    int frame_outstanding;
    int sequence;
    char aec_profile[128];
    void *effects_library;
    P3HpfCreate hpf_create;
    P3HpfProcess hpf_process;
    P3HpfFree hpf_free;
    int16_t hpf_state[16];
    int hpf_enabled;
    P3NsCreate ns_create;
    P3NsSetConfig ns_set_config;
    P3NsProcess ns_process;
    P3NsFree ns_free;
    void *ns;
    int ns_enabled;
    P3AgcCreate agc_create;
    P3AgcSetConfig agc_set_config;
    P3AgcProcess agc_process;
    P3AgcFree agc_free;
    void *agc;
    int agc_enabled;
} p3_audio = { .fd = -1 };

extern int64_t IMP_System_GetTimeStamp(void);

static int p3_audio_open(void)
{
    if (p3_audio.fd >= 0)
        return 0;
    p3_audio.fd = open("/dev/dsp", O_WRONLY | O_CLOEXEC);
    return p3_audio.fd >= 0 ? 0 : -1;
}

static void p3_audio_maybe_close(void)
{
    if (p3_audio.fd >= 0 && !p3_audio.ai_enabled && !p3_audio.ao_enabled) {
        close(p3_audio.fd);
        p3_audio.fd = -1;
    }
}

static int p3_audio_parameter(int command, const IMPAudioIOAttr *attribute)
{
    P3AudioParameter parameter;

    if (!attribute || attribute->samplerate <= 0 ||
        attribute->bitwidth != AUDIO_BIT_WIDTH_16 ||
        (attribute->soundmode != AUDIO_SOUND_MODE_MONO &&
         attribute->soundmode != AUDIO_SOUND_MODE_STEREO))
        return -1;
    if (p3_audio_open() != 0)
        return -1;
    parameter.rate = (uint32_t)attribute->samplerate;
    parameter.format = (uint16_t)attribute->bitwidth;
    parameter.channel = (uint16_t)attribute->soundmode;
    return ioctl(p3_audio.fd, command, &parameter);
}

static int p3_effects_load(void)
{
    if (p3_audio.effects_library)
        return 0;
    p3_audio.effects_library = dlopen("libaudioProcess.so", RTLD_NOW | RTLD_LOCAL);
    if (!p3_audio.effects_library)
        return -1;
#define P3_EFFECT(name, symbol)                                                \
    do {                                                                      \
        *(void **)(&p3_audio.name) = dlsym(p3_audio.effects_library, symbol); \
        if (!p3_audio.name)                                                   \
            return -1;                                                        \
    } while (0)
    P3_EFFECT(hpf_create, "audio_process_hpf_create");
    P3_EFFECT(hpf_process, "audio_process_hpf_process");
    P3_EFFECT(hpf_free, "audio_process_hpf_free");
    P3_EFFECT(ns_create, "audio_process_ns_create");
    P3_EFFECT(ns_set_config, "audio_process_ns_set_config");
    P3_EFFECT(ns_process, "audio_process_ns_process");
    P3_EFFECT(ns_free, "audio_process_ns_free");
    P3_EFFECT(agc_create, "audio_process_agc_create");
    P3_EFFECT(agc_set_config, "audio_process_agc_set_config");
    P3_EFFECT(agc_process, "audio_process_agc_process");
    P3_EFFECT(agc_free, "audio_process_agc_free");
#undef P3_EFFECT
    return 0;
}

static void p3_process_effects(int16_t *samples, int count)
{
    int sample_rate = p3_audio.ai_attr.samplerate;
    int frame_samples = sample_rate / 100;
    int offset;

    if (p3_audio.hpf_enabled)
        p3_audio.hpf_process(p3_audio.hpf_state, samples, count);
    if (frame_samples <= 0)
        return;
    for (offset = 0; offset + frame_samples <= count; offset += frame_samples) {
        if (p3_audio.ns_enabled && sample_rate <= 16000) {
            float input[160];
            float output[160];
            const float *inputs[1] = { input };
            float *outputs[1] = { output };
            int i;

            for (i = 0; i < frame_samples; i++)
                input[i] = (float)samples[offset + i];
            p3_audio.ns_process(p3_audio.ns, inputs, 1, outputs);
            for (i = 0; i < frame_samples; i++) {
                if (output[i] > 32767.0f)
                    samples[offset + i] = 32767;
                else if (output[i] < -32768.0f)
                    samples[offset + i] = -32768;
                else
                    samples[offset + i] = (int16_t)output[i];
            }
        }
        if (p3_audio.agc_enabled && sample_rate <= 16000) {
            const int16_t *inputs[1] = { samples + offset };
            int16_t *outputs[1] = { samples + offset };
            int32_t output_level = 127;
            uint8_t saturated = 0;

            (void)p3_audio.agc_process(p3_audio.agc, inputs, 1,
                                       (size_t)frame_samples, outputs, 127,
                                       &output_level, 0, &saturated);
        }
    }
}

int IMP_AI_SetPubAttr(int device, IMPAudioIOAttr *attribute)
{
    (void)device;
    if (p3_audio.ai_enabled || p3_audio_parameter(AMIC_AI_SET_PARAM, attribute))
        return -1;
    p3_audio.ai_attr = *attribute;
    return 0;
}

int IMP_AI_GetPubAttr(int device, IMPAudioIOAttr *attribute)
{
    (void)device;
    if (!attribute)
        return -1;
    *attribute = p3_audio.ai_attr;
    return 0;
}

int IMP_AI_Enable(int device)
{
    (void)device;
    if (p3_audio.ai_enabled)
        return 0;
    if (p3_audio_open() != 0 || ioctl(p3_audio.fd, AMIC_AI_ENABLE_STREAM, 1))
        return -1;
    p3_audio.ai_enabled = 1;
    return 0;
}

int IMP_AI_Disable(int device)
{
    int result = 0;

    (void)device;
    if (p3_audio.ai_enabled)
        result = ioctl(p3_audio.fd, AMIC_AI_DISABLE_STREAM, 1);
    p3_audio.ai_enabled = 0;
    p3_audio.ai_channel_enabled = 0;
    p3_audio.frame_outstanding = 0;
    p3_audio_maybe_close();
    return result;
}

int IMP_AI_EnableChn(int device, int channel)
{
    (void)device;
    if (!p3_audio.ai_enabled || channel != 0)
        return -1;
    p3_audio.ai_channel_enabled = 1;
    return 0;
}

int IMP_AI_DisableChn(int device, int channel)
{
    (void)device;
    if (channel != 0)
        return -1;
    p3_audio.ai_channel_enabled = 0;
    p3_audio.frame_outstanding = 0;
    return 0;
}

int IMP_AI_SetChnParam(int device, int channel, IMPAudioIChnParam *parameter)
{
    (void)device;
    if (channel != 0 || !parameter)
        return -1;
    p3_audio.ai_channel = *parameter;
    return 0;
}

int IMP_AI_GetChnParam(int device, int channel, IMPAudioIChnParam *parameter)
{
    (void)device;
    if (channel != 0 || !parameter)
        return -1;
    *parameter = p3_audio.ai_channel;
    return 0;
}

int IMP_AI_PollingFrame(int device, int channel, unsigned int timeout_ms)
{
    (void)device;
    (void)timeout_ms;
    return channel == 0 && p3_audio.ai_channel_enabled &&
                   !p3_audio.frame_outstanding
               ? 0
               : -1;
}

int IMP_AI_GetFrame(int device, int channel, IMPAudioFrame *frame,
                    IMPBlock block)
{
    P3AudioInputStream stream;
    size_t bytes;
    unsigned int channels;

    (void)device;
    (void)block;
    if (channel != 0 || !frame || !p3_audio.ai_channel_enabled ||
        p3_audio.frame_outstanding)
        return -1;
    channels = p3_audio.ai_attr.soundmode == AUDIO_SOUND_MODE_STEREO ? 2U : 1U;
    bytes = (size_t)p3_audio.ai_attr.numPerFrm * channels * sizeof(int16_t);
    if (!bytes)
        return -1;
    if (bytes > p3_audio.frame_capacity) {
        void *buffer = realloc(p3_audio.frame_buffer, bytes);
        if (!buffer)
            return -1;
        p3_audio.frame_buffer = buffer;
        p3_audio.frame_capacity = bytes;
    }
    memset(&stream, 0, sizeof(stream));
    stream.data = p3_audio.frame_buffer;
    stream.size = (uint32_t)bytes;
    if (ioctl(p3_audio.fd, AMIC_AI_GET_STREAM, &stream) != 0)
        return -1;
    p3_process_effects((int16_t *)p3_audio.frame_buffer,
                       (int)(bytes / sizeof(int16_t)));
    memset(frame, 0, sizeof(*frame));
    frame->bitwidth = p3_audio.ai_attr.bitwidth;
    frame->soundmode = p3_audio.ai_attr.soundmode;
    frame->virAddr = (uint32_t *)(void *)p3_audio.frame_buffer;
    frame->timeStamp = IMP_System_GetTimeStamp();
    frame->seq = p3_audio.sequence++;
    frame->len = (int)bytes;
    p3_audio.frame_outstanding = 1;
    return 0;
}

int IMP_AI_ReleaseFrame(int device, int channel, IMPAudioFrame *frame)
{
    (void)device;
    if (channel != 0 || !frame || !p3_audio.frame_outstanding ||
        frame->virAddr != (uint32_t *)(void *)p3_audio.frame_buffer)
        return -1;
    p3_audio.frame_outstanding = 0;
    return 0;
}

static int p3_set_volume(int command, int channel, int value)
{
    P3AudioVolume volume;

    if (p3_audio_open() != 0)
        return -1;
    memset(&volume, 0, sizeof(volume));
    volume.channel = (uint32_t)channel + 1U;
    volume.gain[0] = (uint32_t)value;
    volume.gain[1] = (uint32_t)value;
    return ioctl(p3_audio.fd, command, &volume);
}

static int p3_get_volume(int command, int channel, int *value)
{
    P3AudioVolume volume;

    if (!value || p3_audio_open() != 0)
        return -1;
    memset(&volume, 0, sizeof(volume));
    volume.channel = (uint32_t)channel + 1U;
    if (ioctl(p3_audio.fd, command, &volume) != 0)
        return -1;
    *value = (int)volume.gain[0];
    return 0;
}

int IMP_AI_SetVol(int device, int channel, int value)
{
    int result;
    (void)device;
    result = p3_set_volume(AMIC_AI_SET_VOLUME, channel, value);
    if (result == 0)
        p3_audio.ai_volume = value;
    return result;
}

int IMP_AI_GetVol(int device, int channel, int *value)
{
    int result;
    (void)device;
    result = p3_get_volume(AMIC_AI_GET_VOLUME, channel, value);
    if (result == 0)
        p3_audio.ai_volume = *value;
    return result;
}

int IMP_AI_SetGain(int device, int channel, int value)
{
    int result;
    (void)device;
    result = p3_set_volume(AMIC_AI_SET_GAIN, channel, value);
    if (result == 0)
        p3_audio.ai_gain = value;
    return result;
}

int IMP_AI_GetGain(int device, int channel, int *value)
{
    int result;
    (void)device;
    result = p3_get_volume(AMIC_AI_GET_GAIN, channel, value);
    if (result == 0)
        p3_audio.ai_gain = *value;
    return result;
}

int IMP_AI_SetVolMute(int device, int channel, int mute)
{
    P3AudioMute value = { (uint32_t)channel + 1U, (uint32_t)(mute != 0) };
    (void)device;
    return p3_audio_open() == 0 ? ioctl(p3_audio.fd, AMIC_AI_SET_MUTE, &value) : -1;
}

int IMP_AI_EnableHpf(IMPAudioIOAttr *attribute)
{
    if (!attribute || p3_effects_load() != 0)
        return -1;
    memset(p3_audio.hpf_state, 0, sizeof(p3_audio.hpf_state));
    p3_audio.hpf_create(p3_audio.hpf_state, p3_audio.hpf_state + 8,
                        0, 0, 8, 8);
    p3_audio.hpf_enabled = 1;
    return 0;
}

int IMP_AI_DisableHpf(void)
{
    if (p3_audio.hpf_enabled)
        p3_audio.hpf_free();
    p3_audio.hpf_enabled = 0;
    return 0;
}

int IMP_AI_SetHpfCoFrequency(int frequency)
{
    return frequency > 0 ? 0 : -1;
}

int IMP_AI_EnableNs(IMPAudioIOAttr *attribute, int mode)
{
    if (!attribute || mode < 0 || mode > 3 || p3_effects_load() != 0)
        return -1;
    if (!p3_audio.ns)
        p3_audio.ns = p3_audio.ns_create();
    if (!p3_audio.ns ||
        p3_audio.ns_set_config(p3_audio.ns, attribute->samplerate, mode) != 0)
        return -1;
    p3_audio.ns_enabled = 1;
    return 0;
}

int IMP_AI_DisableNs(void)
{
    if (p3_audio.ns)
        p3_audio.ns_free(p3_audio.ns);
    p3_audio.ns = NULL;
    p3_audio.ns_enabled = 0;
    return 0;
}

int IMP_AI_EnableAgc(IMPAudioIOAttr *attribute, IMPAudioAgcConfig configuration)
{
    P3WebRtcAgcConfig config;

    if (!attribute || p3_effects_load() != 0)
        return -1;
    if (!p3_audio.agc)
        p3_audio.agc = p3_audio.agc_create();
    if (!p3_audio.agc)
        return -1;
    config.target_level_dbfs = (int16_t)configuration.TargetLevelDbfs;
    config.compression_gain_db = (int16_t)configuration.CompressionGaindB;
    config.limiter_enable = 1;
    if (p3_audio.agc_set_config(p3_audio.agc, 0, 255, 2,
                                attribute->samplerate, config) != 0)
        return -1;
    p3_audio.agc_enabled = 1;
    return 0;
}

int IMP_AI_DisableAgc(void)
{
    if (p3_audio.agc)
        p3_audio.agc_free(p3_audio.agc);
    p3_audio.agc = NULL;
    p3_audio.agc_enabled = 0;
    return 0;
}

int IMP_AI_Set_WebrtcProfileIni_Path(char *path)
{
    if (!path || strlen(path) >= sizeof(p3_audio.aec_profile))
        return -1;
    strcpy(p3_audio.aec_profile, path);
    return 0;
}

int IMP_AI_EnableAec(int ai_device, int ai_channel, int ao_device, int ao_channel)
{
    (void)ai_device;
    (void)ai_channel;
    (void)ao_device;
    (void)ao_channel;
    return p3_audio_open() == 0 ? ioctl(p3_audio.fd, AMIC_ENABLE_AEC, 1) : -1;
}

int IMP_AI_DisableAec(int ai_device, int ai_channel)
{
    (void)ai_device;
    (void)ai_channel;
    return p3_audio.fd >= 0 ? ioctl(p3_audio.fd, AMIC_DISABLE_AEC, 1) : 0;
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
    (void)device;
    if (p3_audio.ao_enabled || p3_audio_parameter(AMIC_AO_SET_PARAM, attribute))
        return -1;
    p3_audio.ao_attr = *attribute;
    return 0;
}

int IMP_AO_GetPubAttr(int device, IMPAudioIOAttr *attribute)
{
    (void)device;
    if (!attribute)
        return -1;
    *attribute = p3_audio.ao_attr;
    return 0;
}

int IMP_AO_Enable(int device)
{
    (void)device;
    if (p3_audio.ao_enabled)
        return 0;
    if (p3_audio_open() != 0 || ioctl(p3_audio.fd, AMIC_AO_ENABLE_STREAM, 1))
        return -1;
    p3_audio.ao_enabled = 1;
    return 0;
}

int IMP_AO_Disable(int device)
{
    int result = 0;
    (void)device;
    if (p3_audio.ao_enabled)
        result = ioctl(p3_audio.fd, AMIC_AO_DISABLE_STREAM, 1);
    p3_audio.ao_enabled = 0;
    p3_audio.ao_channel_enabled = 0;
    p3_audio_maybe_close();
    return result;
}

int IMP_AO_EnableChn(int device, int channel)
{
    (void)device;
    if (!p3_audio.ao_enabled || channel != 0)
        return -1;
    p3_audio.ao_channel_enabled = 1;
    return 0;
}

int IMP_AO_DisableChn(int device, int channel)
{
    (void)device;
    if (channel != 0)
        return -1;
    p3_audio.ao_channel_enabled = 0;
    return 0;
}

int IMP_AO_SendFrame(int device, int channel, IMPAudioFrame *frame,
                     IMPBlock block)
{
    P3AudioOutputStream stream;
    (void)device;
    (void)block;
    if (channel != 0 || !frame || !frame->virAddr || frame->len <= 0 ||
        !p3_audio.ao_channel_enabled || p3_audio.ao_paused)
        return -1;
    stream.data = frame->virAddr;
    stream.size = (uint32_t)frame->len;
    return ioctl(p3_audio.fd, AMIC_AO_SET_STREAM, &stream);
}

int IMP_AO_SetVol(int device, int channel, int value)
{
    int result;
    (void)device;
    result = p3_set_volume(AMIC_SPK_SET_VOLUME, channel, value);
    if (result == 0)
        p3_audio.ao_volume = value;
    return result;
}

int IMP_AO_GetVol(int device, int channel, int *value)
{
    int result;
    (void)device;
    result = p3_get_volume(AMIC_SPK_GET_VOLUME, channel, value);
    if (result == 0)
        p3_audio.ao_volume = *value;
    return result;
}

int IMP_AO_SetGain(int device, int channel, int value)
{
    int result;
    (void)device;
    result = p3_set_volume(AMIC_SPK_SET_GAIN, channel, value);
    if (result == 0)
        p3_audio.ao_gain = value;
    return result;
}

int IMP_AO_GetGain(int device, int channel, int *value)
{
    int result;
    (void)device;
    result = p3_get_volume(AMIC_SPK_GET_GAIN, channel, value);
    if (result == 0)
        p3_audio.ao_gain = *value;
    return result;
}

int IMP_AO_SetVolMute(int device, int channel, int mute)
{
    P3AudioMute value = { (uint32_t)channel + 1U, (uint32_t)(mute != 0) };
    (void)device;
    return p3_audio_open() == 0 ? ioctl(p3_audio.fd, AMIC_SPK_SET_MUTE, &value) : -1;
}

int IMP_AO_ClearChnBuf(int device, int channel)
{
    (void)device;
    return channel == 0 && p3_audio.fd >= 0
               ? ioctl(p3_audio.fd, AMIC_AO_CLEAR_STREAM, 1)
               : -1;
}

int IMP_AO_FlushChnBuf(int device, int channel)
{
    (void)device;
    return channel == 0 && p3_audio.fd >= 0
               ? ioctl(p3_audio.fd, AMIC_AO_SYNC_STREAM, 1)
               : -1;
}

int IMP_AO_PauseChn(int device, int channel)
{
    (void)device;
    if (channel != 0)
        return -1;
    p3_audio.ao_paused = 1;
    return 0;
}

int IMP_AO_ResumeChn(int device, int channel)
{
    (void)device;
    if (channel != 0)
        return -1;
    p3_audio.ao_paused = 0;
    return 0;
}

int IMP_AO_QueryChnStat(int device, int channel, IMPAudioOChnState *status)
{
    (void)device;
    if (channel != 0 || !status)
        return -1;
    memset(status, 0, sizeof(*status));
    status->chnTotalNum = p3_audio.ao_attr.frmNum;
    status->chnFreeNum = p3_audio.ao_attr.frmNum;
    return 0;
}

int IMP_AO_CacheSwitch(int device, int channel, int enable)
{
    (void)device;
    return channel == 0 && (enable == 0 || enable == 1) ? 0 : -1;
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

int IMP_AO_EnableAgc(IMPAudioIOAttr *attribute, IMPAudioAgcConfig configuration)
{
    return IMP_AI_EnableAgc(attribute, configuration);
}

int IMP_AO_DisableAgc(void)
{
    return IMP_AI_DisableAgc();
}
