/*
 * dmic_t30_test - IMP_DMIC_* of the T30 build (src/t31/openimp_t31_dmic.c
 * with -DPLATFORM_T30) against a fake /dev/dsp.  Checks the attribute rules
 * of T30 libimp 1.0.5 (one microphone per device, 10 ms frames, device 0
 * only), the driver call sequence (speed, channels, format, DMIC stream
 * enable, DMIC_GET_AI_STREAM frames, gain) and the frame hand-over.  No
 * AEC (libaudioProcess is not loaded).
 */
#include "../../src/t31/openimp_t31_dmic.c"

#include <stdarg.h>
#include <stdio.h>

#define FAKE_FD 91

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static int seen_speed, seen_channels, seen_fmt, seen_enable, seen_gain = -1;
static int frames_served;

int64_t IMP_System_GetTimeStamp(void) { return 123456; }

int __real_open(const char *path, int flags, ...);
int __wrap_open(const char *path, int flags, ...)
{
    if (!strcmp(path, "/dev/dsp"))
        return FAKE_FD;
    return __real_open(path, flags);
}

int __real_close(int fd);
int __wrap_close(int fd)
{
    return fd == FAKE_FD ? 0 : __real_close(fd);
}

int __real_ioctl(int fd, unsigned long request, ...);
int __wrap_ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    void *arg;

    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);
    if (fd != FAKE_FD)
        return __real_ioctl(fd, request, arg);
    switch (request) {
    case DMIC_DSP_SPEED: seen_speed = *(int *)arg; return 0;
    case DMIC_DSP_CHANNELS: seen_channels = *(int *)arg; return 0;
    case DMIC_DSP_SETFMT: seen_fmt = *(int *)arg; return 0;
    case DMIC_EXT_ENABLE_STREAM: seen_enable = (int)(intptr_t)arg; return 0;
    case DMIC_SET_GAIN: seen_gain = (int)(intptr_t)arg; return 0;
    case DMIC_GET_STREAM: {
        DmicStream *st = arg;
        int16_t *p = st->data;
        uint32_t i;

        usleep(2000);
        for (i = 0; i < st->size / 2; i++)
            p[i] = (int16_t)(1000 + frames_served);
        frames_served++;
        return 0;
    }
    default:
        errno = EINVAL;
        return -1;
    }
}

int main(void)
{
    IMPDmicAttr attr = { .samplerate = DMIC_SAMPLE_RATE_16000,
                         .bitwidth = DMIC_BIT_WIDTH_16,
                         .soundmode = DMIC_SOUND_MODE_MONO,
                         .frmNum = 4, .numPerFrm = 640, .chnCnt = 1 };
    IMPDmicAttr got;
    IMPDmicChnParam prm = { .usrFrmDepth = 3 };
    IMPDmicChnFrame frm;
    int v;

    /* attribute rules (T30 1.0.5: chnCnt < 2, whole 10 ms frames, dev 0) */
    CHECK(IMP_DMIC_SetPubAttr(1, &attr) == -1);
    CHECK(IMP_DMIC_SetPubAttr(0, NULL) == -1);
    attr.chnCnt = 2;
    CHECK(IMP_DMIC_SetPubAttr(0, &attr) == -1);          /* T31 would take it */
    attr.chnCnt = 0;
    CHECK(IMP_DMIC_SetPubAttr(0, &attr) == -1);
    attr.chnCnt = 1;
    attr.numPerFrm = 100;
    CHECK(IMP_DMIC_SetPubAttr(0, &attr) == -1);          /* 6 ms */
    attr.numPerFrm = 640;
    CHECK(IMP_DMIC_SetPubAttr(0, &attr) == 0);
    memset(&got, 0, sizeof(got));
    CHECK(IMP_DMIC_GetPubAttr(0, &got) == 0 && got.chnCnt == 1 &&
          got.numPerFrm == 640 && got.samplerate == 16000);
    CHECK(IMP_DMIC_SetUserInfo(0, 3, 0) == 0);
    CHECK(IMP_DMIC_SetUserInfo(0, 4, 0) == -1);          /* aec id < 4 */
    CHECK(IMP_DMIC_SetUserInfo(0, -1, 0) == -1);
    CHECK(IMP_DMIC_SetUserInfo(1, 0, 0) == -1);

    /* before Enable: nothing works on the channel */
    CHECK(IMP_DMIC_GetChnParam(0, 0, &prm) == -1);
    CHECK(IMP_DMIC_EnableChn(0, 0) == -1);
    CHECK(IMP_DMIC_SetGain(0, 0, 5) == -1);

    CHECK(IMP_DMIC_Enable(0) == 0);
    CHECK(seen_speed == 16000 && seen_channels == 1 && seen_fmt == 16 &&
          seen_enable == 1);
    CHECK(IMP_DMIC_SetPubAttr(0, &attr) == -1);          /* enabled */
    CHECK(IMP_DMIC_SetChnParam(0, 0, &prm) == 0);
    prm.usrFrmDepth = 1;
    CHECK(IMP_DMIC_SetChnParam(0, 0, &prm) == -1);       /* depth >= 2 */
    prm.usrFrmDepth = 9;
    CHECK(IMP_DMIC_SetChnParam(0, 0, &prm) == -1);       /* <= frmNum */
    memset(&prm, 0, sizeof(prm));
    CHECK(IMP_DMIC_GetChnParam(0, 0, &prm) == 0 && prm.usrFrmDepth == 3);
    CHECK(IMP_DMIC_SetGain(0, 0, 99) == 0 && seen_gain == 31);   /* clamp */
    CHECK(IMP_DMIC_GetGain(0, 0, &v) == 0 && v == 31);
    CHECK(IMP_DMIC_SetVol(0, 0, 500) == 0);
    CHECK(IMP_DMIC_GetVol(0, 0, &v) == 0 && v == 120);
    CHECK(IMP_DMIC_SetVol(0, 0, 60) == 0);

    CHECK(IMP_DMIC_EnableChn(0, 0) == 0);
    CHECK(IMP_DMIC_PollingFrame(0, 0, 2000) == 0);
    memset(&frm, 0, sizeof(frm));
    CHECK(IMP_DMIC_GetFrame(0, 0, &frm, BLOCK) == 0);
    CHECK(frm.rawFrame.len == 640 * 2 && frm.rawFrame.virAddr != NULL &&
          frm.rawFrame.timeStamp == 123456 && frm.aecFrame.len == 0);
    CHECK(((int16_t *)frm.rawFrame.virAddr)[0] >= 1000);
    CHECK(IMP_DMIC_ReleaseFrame(0, 0, &frm) == 0);
    CHECK(IMP_DMIC_GetFrameAndRef(0, 0, &frm, NULL, NOBLOCK) == -1);
    CHECK(IMP_DMIC_DisableChn(0, 0) == 0);
    CHECK(IMP_DMIC_Disable(0) == 0);
    CHECK(frames_served > 0);

    if (failures) {
        fprintf(stderr, "dmic_t30_test: %d failure(s)\n", failures);
        return 1;
    }
    puts("dmic_t30_test: PASS");
    return 0;
}
