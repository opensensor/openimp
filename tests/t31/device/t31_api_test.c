/*
 * t31_api_test - on-device check of the T31 API additions (AENC/ADEC, DMIC,
 * AI AEC/reference frames, encoder/system/OSD extras).
 *
 * Without options only the parts that need no audio/video hardware run, so
 * it is safe next to a running streamer:
 *   - AENC -> ADEC round trip through every built-in codec (G.711A/U,
 *     G.726-16, ADPCM) and a registered user codec, with SNR and sizes;
 *   - MemPoolRequest/Free, IMPPixfmtToString, Fisheye flag, SetChnRotate,
 *     IMPDBG_Init, DMIC argument checks.
 *
 * Hardware tests (stop the streamer first: /etc/init.d/S95timps stop or
 * equivalent, they need /dev/dsp):
 *   -a  AI capture 3 s -> AENC G.711U -> ADEC -> AO playback; frame counts,
 *       timestamps and stream sizes
 *   -r  AEC reference frames: play a 1 kHz tone on AO while capturing with
 *       IMP_AI_EnableAecRefFrame; the reference energy must be > 0 while
 *       the tone plays (driver reference path)
 *   -e  AEC: play the tone, measure microphone energy without and with
 *       IMP_AI_EnableAec (needs libaudioProcess.so with audio_process_aec_*)
 *   -d  DMIC: SetPubAttr/Enable/EnableChn/GetFrame; on a kernel without
 *       CONFIG_JZ_TS_DMIC IMP_DMIC_Enable must return -1 (expected), with it
 *       frames must arrive
 *
 * Build (static, against the OpenIMP T31 objects of build-t31.sh):
 *   B=/mnt/NVMe/git/openimp-t31api/build/t31
 *   CC=.../host/bin/mipsel-linux-gcc
 *   $CC -std=gnu99 -static -O2 -Wall -I/mnt/NVMe/git/openimp-t31api/include \
 *     -o t31_api_test t31_api_test.c $B/[all .o] -lpthread -lrt -lm -ldl
 * (all objects in $B except openimp-tuningd's), then strip.
 * A static uClibc binary cannot dlopen libaudioProcess.so, so for -e build
 * it dynamically against the new libimp.so instead:
 *   $CC -std=gnu99 -O2 -rdynamic -I.../include -o t31_api_test_dyn \
 *     t31_api_test.c -L$B -limp -lpthread -lm -ldl
 *   LD_LIBRARY_PATH=/tmp ./t31_api_test_dyn -e   (libimp.so copied to /tmp)
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <imp/imp_audio.h>
#include <imp/imp_dmic.h>
#include <imp/imp_system.h>

extern char *IMPPixfmtToString(int pixfmt);
extern int IMP_Encoder_SetFisheyeEnableStatus(int encChn, int enable);
extern int IMP_Encoder_GetFisheyeEnableStatus(int encChn, int *enable);
extern int IMP_FrameSource_SetChnRotate(int chn, int rot, int w, int h);
extern int IMP_AI_IMPDBG_Init(void);
extern int IMP_AO_IMPDBG_Init(void);

static int failures;

/* libimp's logging back-end normally comes from libalog/libsysutils, which a
 * static link does not have. Quiet unless T31_API_TEST_LOG is set. */
int IMP_Log_Get_Option(void)
{
    return 0;
}

void imp_log_fun(int level, int option, int type, ...)
{
    (void)option;
    (void)type;
    if (getenv("T31_API_TEST_LOG"))
        fprintf(stderr, "imp_log level %d\n", level);
}

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        if (cond) {                                                           \
            printf("PASS ");                                                  \
        } else {                                                              \
            printf("FAIL ");                                                  \
            failures++;                                                       \
        }                                                                     \
        printf(__VA_ARGS__);                                                  \
        printf("\n");                                                         \
    } while (0)

#define RATE 8000
#define NUM_PER_FRM 320 /* 40 ms */

static IMPAudioIOAttr audio_attr(void)
{
    IMPAudioIOAttr attr;

    memset(&attr, 0, sizeof(attr));
    attr.samplerate = AUDIO_SAMPLE_RATE_8000;
    attr.bitwidth = AUDIO_BIT_WIDTH_16;
    attr.soundmode = AUDIO_SOUND_MODE_MONO;
    attr.frmNum = 20;
    attr.numPerFrm = NUM_PER_FRM;
    attr.chnCnt = 1;
    return attr;
}

static double energy(const int16_t *s, int n)
{
    double e = 0.0;
    int i;

    for (i = 0; i < n; i++)
        e += (double)s[i] * (double)s[i];
    return n ? e / n : 0.0;
}

static void tone(int16_t *s, int n, int *phase, double amp)
{
    int i;

    for (i = 0; i < n; i++, (*phase)++)
        s[i] = (int16_t)lrint(amp * sin(2.0 * M_PI * 1000.0 * *phase / RATE));
}

static int user_encode(void *enc, IMPAudioFrame *frame, unsigned char *out,
                       int *len)
{
    (void)enc;
    memcpy(out, frame->virAddr, (size_t)frame->len);
    *len = frame->len;
    return 0;
}

static void test_codecs(void)
{
    static const struct {
        IMPAudioPalyloadType type;
        const char *name;
        int bytes;
        double min_snr;
    } codecs[] = {
        { PT_G711A, "G711A", NUM_PER_FRM, 30.0 },
        { PT_G711U, "G711U", NUM_PER_FRM, 30.0 },
        { PT_ADPCM, "ADPCM", NUM_PER_FRM / 2, 15.0 },
        { PT_G726, "G726-16", NUM_PER_FRM / 4, 5.0 },
    };
    IMPAudioIOAttr attr = audio_attr();
    int16_t pcm[NUM_PER_FRM * 8];
    int16_t back[NUM_PER_FRM * 8];
    unsigned int c;
    int phase = 0;

    /* AENC/ADEC size their nodes from the AI/AO attributes */
    CHECK(IMP_AI_SetPubAttr(0, &attr) == 0, "AI SetPubAttr");
    CHECK(IMP_AO_SetPubAttr(0, &attr) == 0, "AO SetPubAttr");
    tone(pcm, NUM_PER_FRM * 8, &phase, 8000.0);

    for (c = 0; c < sizeof(codecs) / sizeof(codecs[0]); c++) {
        IMPAudioEncChnAttr enc = { codecs[c].type, 10, NULL };
        IMPAudioDecChnAttr dec = { codecs[c].type, 10, ADEC_MODE_PACK, NULL };
        int f;
        int ok = 1;
        double noise = 0.0;
        double signal = 0.0;
        int i;

        if (IMP_AENC_CreateChn(0, &enc) != 0 ||
            IMP_ADEC_CreateChn(0, &dec) != 0) {
            CHECK(0, "%s create", codecs[c].name);
            continue;
        }
        for (f = 0; f < 8; f++) {
            IMPAudioFrame frame;
            IMPAudioStream es;
            IMPAudioStream ds;

            memset(&frame, 0, sizeof(frame));
            frame.bitwidth = AUDIO_BIT_WIDTH_16;
            frame.soundmode = AUDIO_SOUND_MODE_MONO;
            frame.virAddr = (uint32_t *)(void *)(pcm + f * NUM_PER_FRM);
            frame.len = NUM_PER_FRM * 2;
            frame.timeStamp = 1000 + f;
            if (IMP_AENC_SendFrame(0, &frame) != 0 ||
                IMP_AENC_PollingStream(0, 100) != 0 ||
                IMP_AENC_GetStream(0, &es, BLOCK) != 0) {
                ok = 0;
                break;
            }
            ok &= es.len == codecs[c].bytes && es.timeStamp == 1000 + f;
            if (IMP_ADEC_SendStream(0, &es, BLOCK) != 0 ||
                IMP_ADEC_PollingStream(0, 100) != 0 ||
                IMP_ADEC_GetStream(0, &ds, BLOCK) != 0) {
                ok = 0;
                IMP_AENC_ReleaseStream(0, &es);
                break;
            }
            ok &= ds.len == NUM_PER_FRM * 2;
            memcpy(back + f * NUM_PER_FRM, ds.stream, NUM_PER_FRM * 2);
            ok &= IMP_ADEC_ReleaseStream(0, &ds) == 0;
            ok &= IMP_AENC_ReleaseStream(0, &es) == 0;
        }
        for (i = NUM_PER_FRM; i < NUM_PER_FRM * 8; i++) {
            double d = (double)pcm[i] - back[i];

            signal += (double)pcm[i] * pcm[i];
            noise += d * d;
        }
        CHECK(ok, "%s 8 frames encode/decode, %d bytes/frame", codecs[c].name,
              codecs[c].bytes);
        CHECK(noise > 0 && 10.0 * log10(signal / noise) > codecs[c].min_snr,
              "%s round-trip SNR %.1f dB (> %.0f)", codecs[c].name,
              noise > 0 ? 10.0 * log10(signal / noise) : 999.0,
              codecs[c].min_snr);
        IMP_ADEC_DestroyChn(0);
        IMP_AENC_DestroyChn(0);
    }

    {
        IMPAudioEncEncoder user;
        IMPAudioEncChnAttr enc;
        IMPAudioFrame frame;
        IMPAudioStream es;
        int handle = -1;

        memset(&user, 0, sizeof(user));
        user.type = (IMPAudioPalyloadType)6;
        snprintf(user.name, sizeof(user.name), "copy");
        user.encoderFrm = user_encode;
        CHECK(IMP_AENC_RegisterEncoder(&handle, &user) == 0 && handle == 6,
              "RegisterEncoder handle %d", handle);
        enc.type = (IMPAudioPalyloadType)handle;
        enc.bufSize = 4;
        enc.value = NULL;
        memset(&frame, 0, sizeof(frame));
        frame.virAddr = (uint32_t *)(void *)pcm;
        frame.len = NUM_PER_FRM * 2;
        CHECK(IMP_AENC_CreateChn(1, &enc) == 0 &&
                  IMP_AENC_SendFrame(1, &frame) == 0 &&
                  IMP_AENC_GetStream(1, &es, BLOCK) == 0 &&
                  es.len == NUM_PER_FRM * 2 &&
                  memcmp(es.stream, pcm, NUM_PER_FRM * 2) == 0 &&
                  IMP_AENC_ReleaseStream(1, &es) == 0,
              "user encoder through channel 1");
        IMP_AENC_DestroyChn(1);
        CHECK(IMP_AENC_UnRegisterEncoder(&handle) == 0, "UnRegisterEncoder");
    }
}

static void test_extras(void)
{
    int enable = -1;
    IMPDmicAttr dattr;

    CHECK(IMP_System_MemPoolRequest(3, 1 << 20, "test") == 0 &&
              IMP_System_MemPoolRequest(3, 1 << 20, "test") == -1 &&
              IMP_System_MemPoolFree(3) == 0 &&
              IMP_System_MemPoolFree(3) == -1,
          "MemPoolRequest/Free");
    CHECK(IMPPixfmtToString(10) && strcmp(IMPPixfmtToString(10), "NV12") == 0,
          "IMPPixfmtToString(PIX_FMT_NV12)");
    CHECK(IMP_Encoder_SetFisheyeEnableStatus(5, 1) == 0 &&
              IMP_Encoder_GetFisheyeEnableStatus(5, &enable) == 0 &&
              enable == 1 && IMP_Encoder_SetFisheyeEnableStatus(9, 1) == -1,
          "Fisheye flag before channel creation");
    IMP_Encoder_SetFisheyeEnableStatus(5, 0);
    CHECK(IMP_FrameSource_SetChnRotate(0, 1, 1280, 720) == -1 &&
              IMP_FrameSource_SetChnRotate(0, 0, 1280, 720) == 0,
          "SetChnRotate refuses rotation, accepts 0");
    CHECK(IMP_AI_IMPDBG_Init() == 0 && IMP_AO_IMPDBG_Init() == 0,
          "IMPDBG_Init");
    memset(&dattr, 0, sizeof(dattr));
    dattr.samplerate = DMIC_SAMPLE_RATE_16000;
    dattr.bitwidth = DMIC_BIT_WIDTH_16;
    dattr.soundmode = DMIC_SOUND_MODE_MONO;
    dattr.frmNum = 8;
    dattr.numPerFrm = 165; /* not 10 ms * n */
    dattr.chnCnt = 2;
    CHECK(IMP_DMIC_SetPubAttr(0, &dattr) == -1, "DMIC rejects 165 samples");
    dattr.numPerFrm = 320;
    dattr.chnCnt = 5;
    CHECK(IMP_DMIC_SetPubAttr(0, &dattr) == -1, "DMIC rejects 5 mics");
    CHECK(IMP_DMIC_SetUserInfo(0, 4, 1) == -1, "DMIC rejects aecDmicId 4");
}

static int audio_open(int with_ao)
{
    IMPAudioIOAttr attr = audio_attr();
    IMPAudioIChnParam param = { 10, 0 };

    if (IMP_AI_SetPubAttr(0, &attr) || IMP_AI_Enable(0) ||
        IMP_AI_SetChnParam(0, 0, &param) || IMP_AI_EnableChn(0, 0))
        return -1;
    if (with_ao && (IMP_AO_SetPubAttr(0, &attr) || IMP_AO_Enable(0) ||
                    IMP_AO_EnableChn(0, 0)))
        return -1;
    return 0;
}

static void audio_close(int with_ao)
{
    IMP_AI_DisableChn(0, 0);
    IMP_AI_Disable(0);
    if (with_ao) {
        IMP_AO_DisableChn(0, 0);
        IMP_AO_Disable(0);
    }
}

static void test_loopback(void)
{
    IMPAudioEncChnAttr enc = { PT_G711U, 10, NULL };
    IMPAudioDecChnAttr dec = { PT_G711U, 10, ADEC_MODE_PACK, NULL };
    int frames = 0;
    int ok = 1;
    int64_t last = 0;
    int i;

    if (audio_open(1) != 0) {
        CHECK(0, "AI/AO open (is the streamer stopped?)");
        return;
    }
    CHECK(IMP_AENC_CreateChn(0, &enc) == 0 && IMP_ADEC_CreateChn(0, &dec) == 0,
          "loopback channels");
    for (i = 0; i < 3 * RATE / NUM_PER_FRM; i++) {
        IMPAudioFrame frame;
        IMPAudioStream es;
        IMPAudioStream ds;
        IMPAudioFrame out;

        if (IMP_AI_PollingFrame(0, 0, 1000) || IMP_AI_GetFrame(0, 0, &frame, BLOCK)) {
            ok = 0;
            break;
        }
        ok &= frame.timeStamp > last;
        last = frame.timeStamp;
        ok &= IMP_AENC_SendFrame(0, &frame) == 0;
        IMP_AI_ReleaseFrame(0, 0, &frame);
        ok &= IMP_AENC_GetStream(0, &es, BLOCK) == 0 && es.len == NUM_PER_FRM;
        ok &= IMP_ADEC_SendStream(0, &es, BLOCK) == 0;
        IMP_AENC_ReleaseStream(0, &es);
        ok &= IMP_ADEC_GetStream(0, &ds, BLOCK) == 0;
        memset(&out, 0, sizeof(out));
        out.bitwidth = AUDIO_BIT_WIDTH_16;
        out.soundmode = AUDIO_SOUND_MODE_MONO;
        out.virAddr = (uint32_t *)(void *)ds.stream;
        out.len = ds.len;
        ok &= IMP_AO_SendFrame(0, 0, &out, BLOCK) == 0;
        IMP_ADEC_ReleaseStream(0, &ds);
        frames++;
    }
    CHECK(ok && frames == 3 * RATE / NUM_PER_FRM,
          "AI -> AENC(G711U) -> ADEC -> AO loopback, %d frames", frames);
    IMP_ADEC_DestroyChn(0);
    IMP_AENC_DestroyChn(0);
    audio_close(1);
}

/* play a tone for "frames" AI frames; returns the mean mic and ref energy */
static void tone_capture(int frames, int with_ref, double *mic, double *ref)
{
    int16_t out_pcm[NUM_PER_FRM];
    int phase = 0;
    int i;

    *mic = 0.0;
    *ref = 0.0;
    for (i = 0; i < frames; i++) {
        IMPAudioFrame out;
        IMPAudioFrame frame;
        IMPAudioFrame refframe;

        tone(out_pcm, NUM_PER_FRM, &phase, 12000.0);
        memset(&out, 0, sizeof(out));
        out.bitwidth = AUDIO_BIT_WIDTH_16;
        out.soundmode = AUDIO_SOUND_MODE_MONO;
        out.virAddr = (uint32_t *)(void *)out_pcm;
        out.len = sizeof(out_pcm);
        IMP_AO_SendFrame(0, 0, &out, BLOCK);
        if (with_ref) {
            if (IMP_AI_GetFrameAndRef(0, 0, &frame, &refframe, BLOCK))
                continue;
            if (i >= frames / 2 && refframe.virAddr)
                *ref += energy((int16_t *)(void *)refframe.virAddr,
                               refframe.len / 2) / (frames - frames / 2);
        } else if (IMP_AI_GetFrame(0, 0, &frame, BLOCK)) {
            continue;
        }
        if (i >= frames / 2)
            *mic += energy((int16_t *)(void *)frame.virAddr, frame.len / 2) /
                    (frames - frames / 2);
        IMP_AI_ReleaseFrame(0, 0, &frame);
    }
}

static void test_reference(void)
{
    double mic;
    double ref;

    if (audio_open(1) != 0) {
        CHECK(0, "AI/AO open (is the streamer stopped?)");
        return;
    }
    CHECK(IMP_AI_EnableAecRefFrame(0, 0, 0, 0) == 0, "EnableAecRefFrame");
    tone_capture(50, 1, &mic, &ref);
    printf("     mic energy %.0f, reference energy %.0f\n", mic, ref);
    CHECK(ref > 1000.0, "reference frames carry the playback tone");
    CHECK(IMP_AI_DisableAecRefFrame(0, 0, 0, 0) == 0, "DisableAecRefFrame");
    audio_close(1);
}

static void test_aec(void)
{
    double plain;
    double cancelled;
    double ref;

    if (audio_open(1) != 0) {
        CHECK(0, "AI/AO open (is the streamer stopped?)");
        return;
    }
    tone_capture(75, 0, &plain, &ref);
    CHECK(IMP_AI_EnableAec(0, 0, 0, 0) == 0, "EnableAec");
    tone_capture(75, 0, &cancelled, &ref);
    printf("     mic energy without AEC %.0f, with AEC %.0f (%.1f dB)\n",
           plain, cancelled,
           cancelled > 0 ? 10.0 * log10(plain / cancelled) : 99.0);
    CHECK(cancelled < plain / 4.0, "AEC removes >= 6 dB of the tone echo");
    CHECK(IMP_AI_DisableAec(0, 0) == 0, "DisableAec");
    audio_close(1);
}

static void test_dmic(void)
{
    IMPDmicAttr attr;
    IMPDmicChnParam param = { 4, 0 };
    IMPDmicChnFrame frame;
    int ret;
    int i;
    int got = 0;

    memset(&attr, 0, sizeof(attr));
    attr.samplerate = DMIC_SAMPLE_RATE_16000;
    attr.bitwidth = DMIC_BIT_WIDTH_16;
    attr.soundmode = DMIC_SOUND_MODE_MONO;
    attr.frmNum = 8;
    attr.numPerFrm = 640;
    attr.chnCnt = 1;
    CHECK(IMP_DMIC_SetPubAttr(0, &attr) == 0, "DMIC SetPubAttr");
    ret = IMP_DMIC_Enable(0);
    if (ret != 0) {
        printf("INFO DMIC_Enable = %d: audio driver without "
               "CONFIG_JZ_TS_DMIC (expected on Thingino)\n", ret);
        return;
    }
    CHECK(IMP_DMIC_SetChnParam(0, 0, &param) == 0 &&
              IMP_DMIC_EnableChn(0, 0) == 0,
          "DMIC channel");
    for (i = 0; i < 25; i++) {
        if (IMP_DMIC_PollingFrame(0, 0, 1000) == 0 &&
            IMP_DMIC_GetFrame(0, 0, &frame, BLOCK) == 0) {
            got += frame.rawFrame.len == 640 * 2;
            IMP_DMIC_ReleaseFrame(0, 0, &frame);
        }
    }
    CHECK(got == 25, "DMIC 25 frames of 40 ms");
    IMP_DMIC_DisableChn(0, 0);
    IMP_DMIC_Disable(0);
}

int main(int argc, char **argv)
{
    int opt;
    int loop = 0;
    int ref = 0;
    int aec = 0;
    int dmic = 0;

    while ((opt = getopt(argc, argv, "ared")) != -1) {
        switch (opt) {
        case 'a': loop = 1; break;
        case 'r': ref = 1; break;
        case 'e': aec = 1; break;
        case 'd': dmic = 1; break;
        default:
            fprintf(stderr, "usage: %s [-a] [-r] [-e] [-d]\n", argv[0]);
            return 2;
        }
    }
    test_codecs();
    test_extras();
    if (loop)
        test_loopback();
    if (ref)
        test_reference();
    if (aec)
        test_aec();
    if (dmic)
        test_dmic();
    printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
