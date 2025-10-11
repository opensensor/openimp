/**
 * IMP Audio Module Implementation
 * Based on reverse engineering of libimp.so v1.1.6
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <imp/imp_audio.h>

#define LOG_AUD(fmt, ...) fprintf(stderr, "[Audio] " fmt "\n", ##__VA_ARGS__)

/* Audio device ioctls - from decompilation */
#define AUDIO_SET_SAMPLERATE    0xc0045002
#define AUDIO_SET_VOLUME        0xc0045006
#define AUDIO_SET_GAIN          0xc0045005
#define AUDIO_ENABLE_AEC        0x40045066

/* Audio device structure - 0x260 bytes per device */
#define MAX_AUDIO_DEVICES 2
#define AUDIO_DEV_SIZE 0x260
#define MAX_AUDIO_CHANNELS 1

typedef struct {
    int fd;                     /* 0x08: Device file descriptor (/dev/dsp) */
    uint8_t data_0c[0x4];       /* 0x0c-0x0f: Padding */
    IMPAudioIOAttr attr;        /* 0x10: Audio attributes (0x18 bytes) */
    uint8_t data_28[0x4];       /* 0x28-0x2b: Padding */
    uint8_t enabled;            /* 0x2c: Enable flag */
    uint8_t data_2d[0x3];       /* Padding */
    uint8_t data_30[0x4];       /* 0x30-0x33 */
    pthread_t thread;           /* 0x34: Thread */
    uint8_t data_38[0x1f8];     /* Rest of device data */
    pthread_mutex_t mutex;      /* 0x230: Mutex */
    pthread_cond_t cond;        /* 0x248: Condition */
} AudioDevice;

typedef struct {
    uint8_t data_00[0x38];      /* 0x00-0x37: Header */
    uint8_t enabled;            /* 0x3c from base+0x260: Channel enable */
    uint8_t data_39[0x1cf];     /* Rest of channel data */
} AudioChannel;

/* Global audio state - starts at 0x10b228 */
typedef struct {
    AudioDevice devices[MAX_AUDIO_DEVICES];     /* 0x00: 2 devices */
    AudioChannel channels[MAX_AUDIO_DEVICES][MAX_AUDIO_CHANNELS]; /* Channels per device */
} AudioState;

/* Global variables */
static AudioState *g_audio_state = NULL;
static pthread_mutex_t audio_mutex = PTHREAD_MUTEX_INITIALIZER;
static int audio_initialized = 0;

/* Initialize audio module */
static void audio_init(void) {
    if (audio_initialized) return;

    g_audio_state = (AudioState*)calloc(1, sizeof(AudioState));
    if (g_audio_state == NULL) {
        LOG_AUD("Failed to allocate audio state");
        return;
    }

    /* Initialize all devices */
    for (int i = 0; i < MAX_AUDIO_DEVICES; i++) {
        g_audio_state->devices[i].enabled = 0;
        g_audio_state->devices[i].fd = -1;
    }

    audio_initialized = 1;
}

/* __ai_dev_init - Initialize audio input device
 * Based on decompilation at 0xa63bc */
static int __ai_dev_init(AudioDevice *dev) {
    if (dev == NULL) {
        return -1;
    }

    /* Open /dev/dsp device */
    dev->fd = open("/dev/dsp", O_RDWR | O_NONBLOCK);
    if (dev->fd < 0) {
        LOG_AUD("__ai_dev_init: Failed to open /dev/dsp: %s", strerror(errno));
        return -1;
    }

    /* Set sample rate via ioctl */
    if (ioctl(dev->fd, AUDIO_SET_SAMPLERATE, &dev->attr) != 0) {
        LOG_AUD("__ai_dev_init: Failed to set samplerate: %s", strerror(errno));
        close(dev->fd);
        dev->fd = -1;
        return -1;
    }

    /* Set volume */
    int volume = 1;
    if (ioctl(dev->fd, AUDIO_SET_VOLUME, &volume) != 0) {
        LOG_AUD("__ai_dev_init: Failed to set volume: %s", strerror(errno));
        close(dev->fd);
        dev->fd = -1;
        return -1;
    }

    /* Set gain */
    int gain = 0x10;
    if (ioctl(dev->fd, AUDIO_SET_GAIN, &gain) != 0) {
        LOG_AUD("__ai_dev_init: Failed to set gain: %s", strerror(errno));
        close(dev->fd);
        dev->fd = -1;
        return -1;
    }

    /* Enable AEC (Acoustic Echo Cancellation) */
    if (ioctl(dev->fd, AUDIO_ENABLE_AEC, 1) != 0) {
        LOG_AUD("__ai_dev_init: Failed to enable AEC: %s", strerror(errno));
        close(dev->fd);
        dev->fd = -1;
        return -1;
    }

    LOG_AUD("__ai_dev_init: Initialized device (fd=%d)", dev->fd);
    return 0;
}

/* __ai_dev_deinit - Deinitialize audio input device
 * Based on decompilation at 0xa6718 */
static int __ai_dev_deinit(AudioDevice *dev) {
    if (dev == NULL) {
        return 0;
    }

    if (dev->fd > 0) {
        close(dev->fd);
        dev->fd = -1;
        LOG_AUD("__ai_dev_deinit: Closed device");
    }

    return 0;
}

/* Audio thread - captures audio data
 * Based on decompilation at 0xae220 */
static void *audio_thread(void *arg) {
    AudioDevice *dev = (AudioDevice*)arg;

    LOG_AUD("audio_thread: started");

    while (dev->enabled) {
        /* Wait for condition signal or timeout */
        pthread_mutex_lock(&dev->mutex);

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1; /* 1 second timeout */

        pthread_cond_timedwait(&dev->cond, &dev->mutex, &ts);
        pthread_mutex_unlock(&dev->mutex);

        if (!dev->enabled) {
            break;
        }

        /* Read audio data from device */
        /* This would normally read from dev->fd and process the audio */
        /* For now, just sleep to simulate work */
        usleep(10000); /* 10ms */
    }

    LOG_AUD("audio_thread: stopped");
    return NULL;
}

/* Audio Input (AI) Functions */

/* IMP_AI_SetPubAttr - based on decompilation at 0xa8638 */
int IMP_AI_SetPubAttr(int audioDevId, IMPAudioIOAttr *attr) {
    if (attr == NULL) {
        LOG_AUD("AI_SetPubAttr failed: NULL attr");
        return -1;
    }

    if (audioDevId < 0 || audioDevId >= MAX_AUDIO_DEVICES) {
        LOG_AUD("AI_SetPubAttr failed: invalid device %d", audioDevId);
        return -1;
    }

    if (attr->soundmode < 0 || attr->soundmode >= 2) {
        LOG_AUD("AI_SetPubAttr failed: invalid soundmode %d", attr->soundmode);
        return -1;
    }

    pthread_mutex_lock(&audio_mutex);
    audio_init();

    if (g_audio_state == NULL) {
        pthread_mutex_unlock(&audio_mutex);
        return -1;
    }

    /* Validate frame time - must be divisible by 10ms */
    uint32_t frame_time_ms = (attr->numPerFrm * 1000) / attr->samplerate;
    if ((frame_time_ms % 10) != 0) {
        LOG_AUD("AI_SetPubAttr failed: invalid frame time %u ms", frame_time_ms);
        pthread_mutex_unlock(&audio_mutex);
        return -1;
    }

    /* Copy attributes */
    memcpy(&g_audio_state->devices[audioDevId].attr, attr, sizeof(IMPAudioIOAttr));

    pthread_mutex_unlock(&audio_mutex);

    LOG_AUD("AI_SetPubAttr: dev=%d, rate=%d, bits=%d, mode=%d",
            audioDevId, attr->samplerate, attr->bitwidth, attr->soundmode);
    return 0;
}

/* IMP_AI_GetPubAttr - based on decompilation at 0xa884c */
int IMP_AI_GetPubAttr(int audioDevId, IMPAudioIOAttr *attr) {
    if (attr == NULL) {
        LOG_AUD("AI_GetPubAttr failed: NULL attr");
        return -1;
    }

    if (audioDevId < 0 || audioDevId >= MAX_AUDIO_DEVICES) {
        LOG_AUD("AI_GetPubAttr failed: invalid device %d", audioDevId);
        return -1;
    }

    pthread_mutex_lock(&audio_mutex);

    if (g_audio_state == NULL) {
        pthread_mutex_unlock(&audio_mutex);
        return -1;
    }

    memcpy(attr, &g_audio_state->devices[audioDevId].attr, sizeof(IMPAudioIOAttr));

    pthread_mutex_unlock(&audio_mutex);

    LOG_AUD("AI_GetPubAttr: dev=%d", audioDevId);
    return 0;
}

/* IMP_AI_Enable - based on decompilation at 0xa895c */
int IMP_AI_Enable(int audioDevId) {
    if (audioDevId < 0 || audioDevId >= MAX_AUDIO_DEVICES) {
        LOG_AUD("AI_Enable failed: invalid device %d", audioDevId);
        return -1;
    }

    pthread_mutex_lock(&audio_mutex);

    audio_init();

    if (g_audio_state == NULL) {
        pthread_mutex_unlock(&audio_mutex);
        return -1;
    }

    if (g_audio_state->devices[audioDevId].enabled) {
        LOG_AUD("AI_Enable: device %d already enabled", audioDevId);
        pthread_mutex_unlock(&audio_mutex);
        return 0;
    }

    /* Check sample rate if set - from decompilation, only 16kHz supported */
    if (g_audio_state->devices[audioDevId].attr.samplerate != 0 &&
        g_audio_state->devices[audioDevId].attr.samplerate != 16000) {
        LOG_AUD("AI_Enable failed: only 16kHz supported, got %d",
                g_audio_state->devices[audioDevId].attr.samplerate);
        pthread_mutex_unlock(&audio_mutex);
        return -1;
    }

    /* Initialize audio device */
    if (__ai_dev_init(&g_audio_state->devices[audioDevId]) != 0) {
        LOG_AUD("AI_Enable: Failed to initialize device");
        pthread_mutex_unlock(&audio_mutex);
        return -1;
    }

    /* Initialize mutex and condition variable */
    pthread_mutex_init(&g_audio_state->devices[audioDevId].mutex, NULL);
    pthread_cond_init(&g_audio_state->devices[audioDevId].cond, NULL);

    /* Mark as enabled before creating thread */
    g_audio_state->devices[audioDevId].enabled = 1;

    /* Create audio capture thread */
    int ret = pthread_create(&g_audio_state->devices[audioDevId].thread, NULL,
                             audio_thread, &g_audio_state->devices[audioDevId]);
    if (ret != 0) {
        LOG_AUD("AI_Enable: Failed to create thread: %s", strerror(errno));
        g_audio_state->devices[audioDevId].enabled = 0;
        pthread_cond_destroy(&g_audio_state->devices[audioDevId].cond);
        pthread_mutex_destroy(&g_audio_state->devices[audioDevId].mutex);
        __ai_dev_deinit(&g_audio_state->devices[audioDevId]);
        pthread_mutex_unlock(&audio_mutex);
        return -1;
    }

    pthread_mutex_unlock(&audio_mutex);

    LOG_AUD("AI_Enable: dev=%d", audioDevId);
    return 0;
}

/* IMP_AI_Disable - based on decompilation at 0xa8d04 */
int IMP_AI_Disable(int audioDevId) {
    if (audioDevId < 0 || audioDevId >= MAX_AUDIO_DEVICES) {
        LOG_AUD("AI_Disable failed: invalid device %d", audioDevId);
        return -1;
    }

    pthread_mutex_lock(&audio_mutex);

    if (g_audio_state == NULL || !g_audio_state->devices[audioDevId].enabled) {
        LOG_AUD("AI_Disable: device %d not enabled", audioDevId);
        pthread_mutex_unlock(&audio_mutex);
        return 0;
    }

    /* Signal thread to stop */
    g_audio_state->devices[audioDevId].enabled = 0;

    /* Wake up thread if it's waiting */
    pthread_cond_signal(&g_audio_state->devices[audioDevId].cond);

    pthread_mutex_unlock(&audio_mutex);

    /* Wait for thread to finish */
    pthread_join(g_audio_state->devices[audioDevId].thread, NULL);

    pthread_mutex_lock(&audio_mutex);

    /* Deinitialize device */
    __ai_dev_deinit(&g_audio_state->devices[audioDevId]);

    /* Destroy mutex and condition variable */
    pthread_cond_destroy(&g_audio_state->devices[audioDevId].cond);
    pthread_mutex_destroy(&g_audio_state->devices[audioDevId].mutex);

    pthread_mutex_unlock(&audio_mutex);

    LOG_AUD("AI_Disable: dev=%d", audioDevId);
    return 0;
}

/* IMP_AI_EnableChn - based on decompilation at 0xad8c8 */
int IMP_AI_EnableChn(int audioDevId, int aiChn) {
    if (audioDevId < 0 || audioDevId >= MAX_AUDIO_DEVICES) {
        LOG_AUD("AI_EnableChn failed: invalid device %d", audioDevId);
        return -1;
    }

    if (aiChn != 0) {
        LOG_AUD("AI_EnableChn failed: invalid channel %d (only 0 supported)", aiChn);
        return -1;
    }

    pthread_mutex_lock(&audio_mutex);

    if (g_audio_state == NULL || !g_audio_state->devices[audioDevId].enabled) {
        LOG_AUD("AI_EnableChn failed: device %d not enabled", audioDevId);
        pthread_mutex_unlock(&audio_mutex);
        return -1;
    }

    if (g_audio_state->channels[audioDevId][aiChn].enabled) {
        LOG_AUD("AI_EnableChn: channel already enabled");
        pthread_mutex_unlock(&audio_mutex);
        return 0;
    }

    /* Channel initialization - based on decompilation at 0xad8c8
     * The real implementation does:
     * 1. pthread_mutex_init() for channel mutex (offset 0x50 from channel base)
     * 2. pthread_cond_init() for two condition variables (offsets 0x70, 0xa0)
     * 3. audio_buf_alloc() - allocates circular buffer for audio frames
     *    - Buffer size = numPerFrm * 2 (stereo)
     *    - Number of buffers = frame buffer count from device
     * 4. _ai_set_tmpbuf_size() - sets temporary buffer size for AEC
     * 5. _ai_alloc_tmpbuf() - allocates temporary processing buffer
     * 6. Initializes buffer pointers and counters
     * 7. pthread_mutex_init() for 4 additional mutexes (offsets 0x140, 0x188, 0x158, 0x170)
     * 8. _ai_thread_post() - signals audio thread to start processing
     *
     * For our implementation, we mark the channel as enabled.
     * Full buffer management would require implementing the audio_buf_* functions
     * and temporary buffer allocation for AEC processing. */

    g_audio_state->channels[audioDevId][aiChn].enabled = 1;

    pthread_mutex_unlock(&audio_mutex);

    LOG_AUD("AI_EnableChn: dev=%d, chn=%d", audioDevId, aiChn);
    return 0;
}

/* IMP_AI_DisableChn - based on decompilation at 0xa9024 */
int IMP_AI_DisableChn(int audioDevId, int aiChn) {
    if (audioDevId < 0 || audioDevId >= MAX_AUDIO_DEVICES) {
        LOG_AUD("AI_DisableChn failed: invalid device %d", audioDevId);
        return -1;
    }

    if (aiChn != 0) {
        LOG_AUD("AI_DisableChn failed: invalid channel %d", aiChn);
        return -1;
    }

    pthread_mutex_lock(&audio_mutex);

    if (g_audio_state == NULL || !g_audio_state->devices[audioDevId].enabled) {
        LOG_AUD("AI_DisableChn failed: device %d not enabled", audioDevId);
        pthread_mutex_unlock(&audio_mutex);
        return -1;
    }

    if (!g_audio_state->channels[audioDevId][aiChn].enabled) {
        LOG_AUD("AI_DisableChn: channel not enabled");
        pthread_mutex_unlock(&audio_mutex);
        return 0;
    }

    /* Channel cleanup - based on decompilation at 0xa9024
     * The real implementation does:
     * 1. pthread_mutex_lock() on device mutex
     * 2. Sets channel enabled flag to 0
     * 3. Sets channel disable wait flag
     * 4. pthread_cond_timedwait() with 5 second timeout to wait for channel to finish
     * 5. audio_buf_free() - frees the circular audio buffer
     * 6. pthread_mutex_unlock() on device mutex
     * 7. pthread_mutex_destroy() for all 4 channel mutexes
     * 8. Frees temporary buffer if allocated
     * 9. Clears temporary buffer pointer
     *
     * For our implementation, we mark the channel as disabled.
     * Full cleanup would require implementing the audio_buf_free() function
     * and proper synchronization with the audio thread. */

    g_audio_state->channels[audioDevId][aiChn].enabled = 0;

    pthread_mutex_unlock(&audio_mutex);

    LOG_AUD("AI_DisableChn: dev=%d, chn=%d", audioDevId, aiChn);
    return 0;
}

int IMP_AI_SetChnParam(int audioDevId, int aiChn, IMPAudioIChnParam *attr) {
    if (attr == NULL) return -1;
    LOG_AUD("AI_SetChnParam: dev=%d, chn=%d", audioDevId, aiChn);
    return 0;
}

int IMP_AI_GetChnParam(int audioDevId, int aiChn, IMPAudioIChnParam *attr) {
    if (attr == NULL) return -1;
    LOG_AUD("AI_GetChnParam: dev=%d, chn=%d", audioDevId, aiChn);
    memset(attr, 0, sizeof(*attr));
    return 0;
}

int IMP_AI_SetVol(int audioDevId, int aiChn, int vol) {
    LOG_AUD("AI_SetVol: dev=%d, chn=%d, vol=%d", audioDevId, aiChn, vol);
    return 0;
}

int IMP_AI_GetVol(int audioDevId, int aiChn, int *vol) {
    if (vol == NULL) return -1;
    *vol = 60;
    return 0;
}

int IMP_AI_SetGain(int audioDevId, int aiChn, int gain) {
    LOG_AUD("AI_SetGain: dev=%d, chn=%d, gain=%d", audioDevId, aiChn, gain);
    return 0;
}

int IMP_AI_GetGain(int audioDevId, int aiChn, int *gain) {
    if (gain == NULL) return -1;
    *gain = 28;
    return 0;
}

int IMP_AI_SetAlcGain(int audioDevId, int aiChn, int gain) {
    LOG_AUD("AI_SetAlcGain: dev=%d, chn=%d, gain=%d", audioDevId, aiChn, gain);
    return 0;
}

int IMP_AI_PollingFrame(int audioDevId, int aiChn, uint32_t timeoutMs) {
    /* Return timeout */
    return -1;
}

int IMP_AI_GetFrame(int audioDevId, int aiChn, IMPAudioFrame *frame, IMPBlock block) {
    if (frame == NULL) return -1;
    /* No frame available */
    return -1;
}

int IMP_AI_ReleaseFrame(int audioDevId, int aiChn, IMPAudioFrame *frame) {
    if (frame == NULL) return -1;
    return 0;
}

int IMP_AI_EnableNs(IMPAudioIOAttr *attr, int level) {
    if (attr == NULL) return -1;
    LOG_AUD("AI_EnableNs: level=%d", level);
    return 0;
}

int IMP_AI_DisableNs(void) {
    LOG_AUD("AI_DisableNs");
    return 0;
}

int IMP_AI_EnableHpf(void) {
    LOG_AUD("AI_EnableHpf");
    return 0;
}

int IMP_AI_DisableHpf(void) {
    LOG_AUD("AI_DisableHpf");
    return 0;
}

int IMP_AI_EnableAgc(IMPAudioIOAttr *attr, IMPAudioAgcConfig config) {
    if (attr == NULL) return -1;
    LOG_AUD("AI_EnableAgc: target=%d, gain=%d", 
            config.TargetLevelDbfs, config.CompressionGaindB);
    return 0;
}

int IMP_AI_DisableAgc(void) {
    LOG_AUD("AI_DisableAgc");
    return 0;
}

/* Audio Encoder (AENC) Functions */

int IMP_AENC_RegisterEncoder(int *handle, IMPAudioEncEncoder *encoder) {
    if (handle == NULL || encoder == NULL) return -1;
    LOG_AUD("AENC_RegisterEncoder: %s", encoder->name);
    *handle = 100;  /* Dummy handle */
    return 0;
}

int IMP_AENC_UnRegisterEncoder(int *handle) {
    if (handle == NULL) return -1;
    LOG_AUD("AENC_UnRegisterEncoder");
    return 0;
}

int IMP_AENC_CreateChn(int aeChn, IMPAudioEncChnAttr *attr) {
    if (attr == NULL) return -1;
    LOG_AUD("AENC_CreateChn: chn=%d, type=%d", aeChn, attr->type);
    return 0;
}

int IMP_AENC_DestroyChn(int aeChn) {
    LOG_AUD("AENC_DestroyChn: chn=%d", aeChn);
    return 0;
}

int IMP_AENC_SendFrame(int aeChn, IMPAudioFrame *frame) {
    if (frame == NULL) return -1;
    return 0;
}

int IMP_AENC_PollingStream(int aeChn, uint32_t timeoutMs) {
    /* Return timeout */
    return -1;
}

int IMP_AENC_GetStream(int aeChn, IMPAudioStream *stream, IMPBlock block) {
    if (stream == NULL) return -1;
    /* No stream available */
    return -1;
}

int IMP_AENC_ReleaseStream(int aeChn, IMPAudioStream *stream) {
    if (stream == NULL) return -1;
    return 0;
}

/* Audio Decoder (ADEC) Functions */

int IMP_ADEC_RegisterDecoder(int *handle, IMPAudioDecDecoder *decoder) {
    if (handle == NULL || decoder == NULL) return -1;
    LOG_AUD("ADEC_RegisterDecoder: %s", decoder->name);
    *handle = 200;  /* Dummy handle */
    return 0;
}

int IMP_ADEC_UnRegisterDecoder(int *handle) {
    if (handle == NULL) return -1;
    LOG_AUD("ADEC_UnRegisterDecoder");
    return 0;
}

int IMP_ADEC_CreateChn(int adChn, IMPAudioDecChnAttr *attr) {
    if (attr == NULL) return -1;
    LOG_AUD("ADEC_CreateChn: chn=%d, type=%d", adChn, attr->type);
    return 0;
}

int IMP_ADEC_DestroyChn(int adChn) {
    LOG_AUD("ADEC_DestroyChn: chn=%d", adChn);
    return 0;
}

int IMP_ADEC_SendStream(int adChn, IMPAudioStream *stream, IMPBlock block) {
    if (stream == NULL) return -1;
    return 0;
}

int IMP_ADEC_GetStream(int adChn, IMPAudioStream *stream, IMPBlock block) {
    if (stream == NULL) return -1;
    /* No stream available */
    return -1;
}

int IMP_ADEC_ReleaseStream(int adChn, IMPAudioStream *stream) {
    if (stream == NULL) return -1;
    return 0;
}

