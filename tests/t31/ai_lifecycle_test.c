/*
 * ai_lifecycle_test - the AI/AO device lifecycle of openimp_t31_audio.c
 * (T31 / T21 / T20 / T30 OSS2 path) against a fake /dev/dsp and the fake
 * libaudioProcess.so (fake_audio_process.c, found through LD_LIBRARY_PATH).
 *
 * Checks:
 *   - device/channel numbers out of range are refused;
 *   - Enable/EnableChn/DisableChn/Disable cycles leak no descriptor and
 *     work again after an injected open or configuration failure;
 *   - the AEC reference queue follows a capture queue that grew since the
 *     reference was first used (larger frames on a later EnableChn);
 *   - the microphone effects (HPF, NS, AGC) switched on and off from one
 *     thread while another one pulls frames;
 *   - an AO HPF cut-off beyond any sample rate is refused.
 * Meant for ASan/UBSan and TSan as well (make sanitize).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <imp/imp_audio.h>

#define FAKE_FD 77

#define T31_DSP_SPEED       0xc0045002UL
#define T31_AI_DISABLE_AEC  0x40045064UL
#define T31_AI_ENABLE_AEC   0x40045065UL
#define T31_AI_GET_STREAM   0x400c5068UL
#define T31_AO_SET_STREAM   0x40085069UL

typedef struct {
    void *data;
    void *aec;
    uint32_t size;
} FakeInputStream;

static int failures;

#define CHECK(cond, ...) do {                                           \
        if (!(cond)) {                                                  \
            failures++;                                                 \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);        \
            fprintf(stderr, __VA_ARGS__);                               \
            fputc('\n', stderr);                                        \
        }                                                               \
    } while (0)

static pthread_mutex_t fake_lock = PTHREAD_MUTEX_INITIALIZER;
static int open_count;          /* /dev/dsp descriptors open */
static int fail_open;
static int fail_speed;
static int aec_in_driver;
static uint32_t chunk_bytes = 320;  /* bytes per GET_STREAM */

int __real_open(const char *path, int flags, ...);
int __real_ioctl(int fd, unsigned long request, void *arg);
int __real_close(int fd);

int __wrap_open(const char *path, int flags, ...)
{
    if (strcmp(path, "/dev/dsp"))
        return __real_open(path, flags, 0);
    pthread_mutex_lock(&fake_lock);
    if (fail_open) {
        pthread_mutex_unlock(&fake_lock);
        errno = EBUSY;
        return -1;
    }
    open_count++;
    pthread_mutex_unlock(&fake_lock);
    return FAKE_FD;
}

int __wrap_close(int fd)
{
    if (fd != FAKE_FD)
        return __real_close(fd);
    pthread_mutex_lock(&fake_lock);
    open_count--;
    pthread_mutex_unlock(&fake_lock);
    return 0;
}

int __wrap_ioctl(int fd, unsigned long request, void *arg)
{
    if (fd != FAKE_FD)
        return __real_ioctl(fd, request, arg);
    if (request == T31_DSP_SPEED && fail_speed)
        return -1;
    if (request == T31_AI_ENABLE_AEC) {
        aec_in_driver = 1;
        return 0;
    }
    if (request == T31_AI_DISABLE_AEC) {
        aec_in_driver = 0;
        return 0;
    }
    if (request == T31_AI_GET_STREAM) {
        FakeInputStream *stream = arg;
        uint32_t n = chunk_bytes < stream->size ? chunk_bytes : stream->size;

        memset(stream->data, 0x11, n);
        if (stream->aec)
            memset(stream->aec, 0x22, n);
        stream->size = n;
        usleep(500);
        return 0;
    }
    if (request == T31_AO_SET_STREAM)
        return 0;
    return 0;                   /* CHANNELS/SETFMT/ENABLE/DISABLE/GAIN... */
}

int64_t IMP_System_GetTimeStamp(void)
{
    return 0;
}

static IMPAudioIOAttr attr_for(int rate, int samples, int frames)
{
    IMPAudioIOAttr attr;

    memset(&attr, 0, sizeof(attr));
    attr.samplerate = rate;
    attr.bitwidth = AUDIO_BIT_WIDTH_16;
    attr.soundmode = AUDIO_SOUND_MODE_MONO;
    attr.frmNum = frames;
    attr.numPerFrm = samples;
    attr.chnCnt = 1;
    return attr;
}

static int pull_frames(int count)
{
    int got = 0;

    while (count-- > 0) {
        IMPAudioFrame frame;

        if (IMP_AI_PollingFrame(0, 0, 1000) != 0 ||
            IMP_AI_GetFrame(0, 0, &frame, BLOCK) != 0)
            break;
        IMP_AI_ReleaseFrame(0, 0, &frame);
        got++;
    }
    return got;
}

static void test_bad_numbers(void)
{
    IMPAudioIOAttr attr = attr_for(16000, 160, 8);
    int value;

    CHECK(IMP_AI_SetPubAttr(-1, &attr) != 0, "AI SetPubAttr -1");
    CHECK(IMP_AI_SetPubAttr(2, &attr) != 0, "AI SetPubAttr 2");
    CHECK(IMP_AI_Enable(-1) != 0 && IMP_AI_Enable(2) != 0, "AI Enable");
    CHECK(IMP_AI_EnableChn(0, 1) != 0 && IMP_AI_EnableChn(0, -1) != 0,
          "AI EnableChn channel");
    CHECK(IMP_AI_SetVol(0, 0, 121) != 0 && IMP_AI_SetVol(0, 0, -31) != 0,
          "AI SetVol range");
    CHECK(IMP_AI_SetGain(0, 0, 32) != 0 && IMP_AI_SetGain(0, 0, -1) != 0,
          "AI SetGain range");
    CHECK(IMP_AI_GetVol(0, 1, &value) != 0, "AI GetVol channel");
    CHECK(IMP_AO_SetPubAttr(1, &attr) != 0, "AO SetPubAttr 1");
    CHECK(IMP_AO_Enable(1) != 0 && IMP_AO_Enable(-1) != 0, "AO Enable");
    CHECK(IMP_AO_SetVol(0, 0, 121) != 0, "AO SetVol range");
    CHECK(open_count == 0, "%d descriptors open", open_count);
}

static void test_cycles(void)
{
    IMPAudioIOAttr attr = attr_for(16000, 160, 8);
    int i;

    for (i = 0; i < 30; i++) {
        CHECK(IMP_AI_SetPubAttr(0, &attr) == 0, "SetPubAttr");
        CHECK(IMP_AI_Enable(0) == 0, "Enable");
        CHECK(IMP_AI_EnableChn(0, 0) == 0, "EnableChn");
        CHECK(pull_frames(2) == 2, "frames (cycle %d)", i);
        CHECK(IMP_AI_DisableChn(0, 0) == 0, "DisableChn");
        CHECK(IMP_AI_Disable(0) == 0, "Disable");
    }
    /* failures on the way up leave nothing open, the next Enable works */
    fail_open = 1;
    CHECK(IMP_AI_Enable(0) != 0, "Enable with open failing");
    fail_open = 0;
    fail_speed = 1;
    CHECK(IMP_AI_Enable(0) != 0, "Enable with configuration failing");
    fail_speed = 0;
    CHECK(open_count == 0, "%d descriptors left after failed Enable",
          open_count);
    CHECK(IMP_AI_Enable(0) == 0 && IMP_AI_EnableChn(0, 0) == 0 &&
          pull_frames(2) == 2, "Enable after failures");
    IMP_AI_DisableChn(0, 0);
    IMP_AI_Disable(0);
    CHECK(open_count == 0, "%d descriptors left", open_count);
}

/* The reference queue was sized for 10 ms frames; the next channel runs
 * 100 ms frames with a deeper queue, and the reference is still kept. */
static void test_reference_grows(void)
{
    IMPAudioIOAttr small = attr_for(16000, 160, 2);
    IMPAudioIOAttr large = attr_for(16000, 1600, 20);

    CHECK(IMP_AI_SetPubAttr(0, &small) == 0 && IMP_AI_Enable(0) == 0 &&
          IMP_AI_EnableChn(0, 0) == 0, "small channel");
    CHECK(IMP_AI_EnableAecRefFrame(0, 0, 0, 0) == 0, "EnableAecRefFrame");
    CHECK(pull_frames(3) == 3, "small frames");
    CHECK(IMP_AI_DisableAecRefFrame(0, 0, 0, 0) == 0, "DisableAecRefFrame");
    IMP_AI_DisableChn(0, 0);
    IMP_AI_Disable(0);

    chunk_bytes = 1280;
    CHECK(IMP_AI_SetPubAttr(0, &large) == 0 && IMP_AI_Enable(0) == 0 &&
          IMP_AI_EnableChn(0, 0) == 0, "large channel");
    usleep(60000);              /* let the queue fill well past 10 ms frames */
    CHECK(pull_frames(25) == 25, "large frames");
    CHECK(IMP_AI_EnableAecRefFrame(0, 0, 0, 0) == 0, "EnableAecRefFrame 2");
    CHECK(pull_frames(25) == 25, "large frames with reference");
    IMP_AI_DisableAecRefFrame(0, 0, 0, 0);
    IMP_AI_DisableChn(0, 0);
    IMP_AI_Disable(0);
    chunk_bytes = 320;
}

static int effects_stop;

static void *effects_toggler(void *arg)
{
    IMPAudioIOAttr attr = attr_for(16000, 160, 8);
    IMPAudioAgcConfig agc = { .TargetLevelDbfs = 10, .CompressionGaindB = 9 };

    (void)arg;
    while (!__atomic_load_n(&effects_stop, __ATOMIC_RELAXED)) {
        IMP_AI_EnableNs(&attr, 2);
        IMP_AI_EnableAgc(&attr, agc);
        IMP_AI_EnableHpf(&attr);
        usleep(200);
        IMP_AI_DisableNs();
        IMP_AI_DisableAgc();
        IMP_AI_DisableHpf();
    }
    return NULL;
}

static void test_effects_while_capturing(void)
{
    IMPAudioIOAttr attr = attr_for(16000, 160, 8);
    pthread_t thread;
    int got;

    CHECK(IMP_AI_SetPubAttr(0, &attr) == 0 && IMP_AI_Enable(0) == 0 &&
          IMP_AI_EnableChn(0, 0) == 0, "channel");
    if (IMP_AI_EnableNs(&attr, 2) != 0) {
        CHECK(0, "fake libaudioProcess.so not found (LD_LIBRARY_PATH)");
        return;
    }
    IMP_AI_DisableNs();
    pthread_create(&thread, NULL, effects_toggler, NULL);
    got = pull_frames(300);
    __atomic_store_n(&effects_stop, 1, __ATOMIC_RELAXED);
    pthread_join(thread, NULL);
    CHECK(got == 300, "%d of 300 frames", got);
    IMP_AI_DisableChn(0, 0);
    IMP_AI_Disable(0);
}

static void test_ao_hpf_cutoff(void)
{
    IMPAudioIOAttr attr = attr_for(16000, 160, 8);

    CHECK(IMP_AO_SetHpfCoFrequency(INT_MAX) == 0, "SetHpfCoFrequency");
    CHECK(IMP_AO_EnableHpf(&attr) != 0, "AO HPF cut-off INT_MAX accepted");
    CHECK(IMP_AO_SetHpfCoFrequency(8000) == 0 &&
          IMP_AO_EnableHpf(&attr) != 0, "AO HPF at Nyquist accepted");
    CHECK(IMP_AO_SetHpfCoFrequency(300) == 0 &&
          IMP_AO_EnableHpf(&attr) == 0, "AO HPF 300 Hz refused");
    IMP_AO_DisableHpf();
    IMP_AO_SetHpfCoFrequency(0);
}

int main(void)
{
    test_bad_numbers();
    test_cycles();
    test_reference_grows();
    test_effects_while_capturing();
    test_ao_hpf_cutoff();
    CHECK(open_count == 0, "%d descriptors left at exit", open_count);
    if (failures) {
        fprintf(stderr, "AI lifecycle: %d check(s) failed\n", failures);
        return 1;
    }
    printf("AI lifecycle tests passed\n");
    return 0;
}
