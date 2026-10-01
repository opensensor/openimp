/*
 * ao_sendframe_test - IMP_AO_SendFrame against a fake OSS2 /dev/dsp.
 *
 * Builds src/t31/openimp_t31_audio.c (T31 / T20 / T21 / T30 path) with open,
 * ioctl and close wrapped. The fake SNDCTL_EXT_SET_AO_STREAM behaves like
 * xb_snd_dsp.c dsp_ioctl_ao_stream: it returns 0 on success and writes the
 * number of bytes it took back into stream.size. To exercise the loop it
 * takes at most FAKE_CHUNK bytes per call.
 *
 * Checks:
 *   - SendFrame succeeds and the driver receives every byte unchanged at
 *     the default volume (60);
 *   - SetVol 120 makes the output louder, SetVolMute silences it;
 *   - a driver error (-1) makes SendFrame fail.
 */
#define _GNU_SOURCE
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <imp/imp_audio.h>

#define FAKE_FD    77
#define FAKE_CHUNK 1000u

#define T31_AO_SET_STREAM 0x40085069UL

typedef struct {
    void *data;
    uint32_t size;
} FakeOutputStream;

static uint8_t sink[65536];
static size_t sink_len;
static int fail_writes;
static int set_calls;

int __real_open(const char *path, int flags, ...);
int __real_ioctl(int fd, unsigned long request, void *arg);
int __real_close(int fd);

int __wrap_open(const char *path, int flags, ...)
{
    if (!strcmp(path, "/dev/dsp"))
        return FAKE_FD;
    return __real_open(path, flags, 0);
}

int __wrap_close(int fd)
{
    return fd == FAKE_FD ? 0 : __real_close(fd);
}

int __wrap_ioctl(int fd, unsigned long request, void *arg)
{
    if (fd != FAKE_FD)
        return __real_ioctl(fd, request, arg);
    if (request == T31_AO_SET_STREAM) {
        FakeOutputStream *stream = arg;
        uint32_t take = stream->size < FAKE_CHUNK ? stream->size : FAKE_CHUNK;

        set_calls++;
        if (fail_writes)
            return -1;
        if (sink_len + take > sizeof(sink))
            take = (uint32_t)(sizeof(sink) - sink_len);
        memcpy(sink + sink_len, stream->data, take);
        sink_len += take;
        stream->size = take;
        return 0;               /* OSS2: success is 0, not a byte count */
    }
    return 0;                   /* SPEED/CHANNELS/SETFMT/ENABLE/GAIN... */
}

int64_t IMP_System_GetTimeStamp(void)
{
    return 0;
}

static int failures;

#define CHECK(cond, ...) do {                                         \
        if (!(cond)) {                                                \
            failures++;                                               \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);      \
            fprintf(stderr, __VA_ARGS__);                             \
            fputc('\n', stderr);                                      \
        }                                                             \
    } while (0)

static int16_t pcm[2400];

static int send(void)
{
    IMPAudioFrame frame;

    memset(&frame, 0, sizeof(frame));
    frame.virAddr = (uint32_t *)(void *)pcm;
    frame.len = (int)sizeof(pcm);
    sink_len = 0;
    set_calls = 0;
    return IMP_AO_SendFrame(0, 0, &frame, BLOCK);
}

static long peak(void)
{
    long max = 0;
    size_t i;

    for (i = 0; i + 1 < sink_len; i += 2) {
        int16_t v;
        long a;

        memcpy(&v, sink + i, sizeof(v));
        a = v < 0 ? -(long)v : v;
        if (a > max)
            max = a;
    }
    return max;
}

int main(void)
{
    IMPAudioIOAttr attr;
    size_t i;

    for (i = 0; i < sizeof(pcm) / sizeof(pcm[0]); i++)
        pcm[i] = (int16_t)((i % 32) < 16 ? 4000 : -4000);

    memset(&attr, 0, sizeof(attr));
    attr.samplerate = AUDIO_SAMPLE_RATE_16000;
    attr.bitwidth = AUDIO_BIT_WIDTH_16;
    attr.soundmode = AUDIO_SOUND_MODE_MONO;
    attr.frmNum = 20;
    attr.numPerFrm = 640;
    attr.chnCnt = 1;
    CHECK(IMP_AO_SetPubAttr(0, &attr) == 0, "SetPubAttr");
    CHECK(IMP_AO_Enable(0) == 0, "Enable");
    CHECK(IMP_AO_EnableChn(0, 0) == 0, "EnableChn");

    CHECK(send() == 0, "SendFrame at the default volume");
    CHECK(sink_len == sizeof(pcm) && !memcmp(sink, pcm, sizeof(pcm)),
          "driver got %zu of %zu bytes, unchanged at volume 60", sink_len,
          sizeof(pcm));
    CHECK(set_calls == (int)((sizeof(pcm) + FAKE_CHUNK - 1) / FAKE_CHUNK),
          "%d SET_AO_STREAM calls for partial takes", set_calls);

    CHECK(IMP_AO_SetVol(0, 0, 120) == 0, "SetVol 120");
    CHECK(send() == 0 && sink_len == sizeof(pcm), "SendFrame at volume 120");
    CHECK(peak() > 4000, "volume 120 peak %ld, input 4000", peak());

    CHECK(IMP_AO_SetVol(0, 0, 20) == 0, "SetVol 20");
    CHECK(send() == 0 && peak() < 4000, "volume 20 peak %ld", peak());

    CHECK(IMP_AO_SetVolMute(0, 0, 1) == 0, "SetVolMute");
    CHECK(send() == 0 && peak() == 0, "muted peak %ld", peak());
    CHECK(IMP_AO_SetVolMute(0, 0, 0) == 0 && IMP_AO_SetVol(0, 0, 60) == 0,
          "unmute");

    fail_writes = 1;
    CHECK(send() != 0, "SendFrame must fail when the driver fails");
    fail_writes = 0;

    CHECK(IMP_AO_DisableChn(0, 0) == 0, "DisableChn");
    CHECK(IMP_AO_Disable(0) == 0, "Disable");
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("T31 AO SendFrame (OSS2): all checks passed\n");
    return 0;
}
