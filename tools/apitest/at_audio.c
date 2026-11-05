/* apitest: Audio area.  AI (microphone) is exercised fully; AO only by return
 * codes (no IMP_AO_SendFrame, nothing is played); AEC functions are not run
 * (they need the speaker path).  AENC/ADEC are software codecs (G.711A). */
#include <errno.h>
#include "apitest.h"

static int aidev(void)
{
    if (getenv("APITEST_AIDEV")) return atoi(getenv("APITEST_AIDEV"));
#ifdef PLATFORM_T23
    return 0;       /* T23: 0 = digital mic (as the streamer uses) */
#else
    return 1;
#endif
}

static unsigned rms16(const IMPAudioFrame *f)
{
    const int16_t *s = (const int16_t *)(const void *)f->virAddr;
    long n = f->len / 2, i;
    double acc = 0;

    if (!s || n <= 0) return 0;
    for (i = 0; i < n; i++) acc += (double)s[i] * s[i];
    return (unsigned)__builtin_sqrt(acc / (double)n);
}

/* read n frames; returns frames read, fills rms of the last and checks timestamps */
static int ai_frames(int dev, int n, unsigned *rms, int *ts_ok)
{
    int k, got = 0;
    int64_t last = -1;

    *ts_ok = 1;
    for (k = 0; k < n; k++) {
        G(IMPAudioFrame, f);
        int r = IMP_AI_PollingFrame ? IMP_AI_PollingFrame(dev, 0, 2000) : 0;

        if (r != 0 || IMP_AI_GetFrame(dev, 0, f, BLOCK) != 0) { gfree(f); break; }
        if (f->len <= 0 || !f->virAddr) *ts_ok = 0;
        if (last >= 0 && f->timeStamp <= last) *ts_ok = 0;
        if (getenv("APITEST_DEBUG_AI")) printf("[D] ai %d ts %lld len %d seq %d\n", k, (long long)f->timeStamp, f->len, f->seq);
        last = f->timeStamp;
        *rms = rms16(f);
        IMP_AI_ReleaseFrame(dev, 0, f);
        gfree(f);
        got++;
    }
    return got;
}


#if defined(PLATFORM_T30)
/* Custom codec registration (T30): a silent PCM copy codec.  The encoder copies the microphone frame into the
 * stream unchanged, the decoder copies the stream back; nothing is played.  PASS = registration returns a handle,
 * a channel of that type runs one frame through the callbacks with the bytes unchanged, unregistration returns 0. */
static int cp_enc_calls, cp_dec_calls;
static int cp_encode(void *enc, IMPAudioFrame *d, unsigned char *out, int *outlen)
{
    (void)enc;
    cp_enc_calls++;
    if (!d || !out || !outlen || d->len <= 0 || d->len > 2048) return -1;
    memcpy(out, (const void *)(uintptr_t)d->virAddr, d->len);
    *outlen = d->len;
    return 0;
}
static int cp_decode(void *dec, unsigned char *in, int inlen, unsigned short *out, int *outlen, int *chns)
{
    (void)dec;
    cp_dec_calls++;
    if (!in || !out || !outlen || inlen <= 0) return -1;
    memcpy(out, in, inlen);
    *outlen = inlen;
    if (chns) *chns = 1;
    return 0;
}
static int cp_getinfo(void *dec, void *info) { (void)dec; (void)info; return 0; }

static void codec_register_test(int dev)
{
    IMPAudioEncEncoder e;
    IMPAudioDecDecoder dd;
    IMPAudioEncChnAttr ea;
    IMPAudioDecChnAttr da;
    G(IMPAudioFrame, f);
    G(IMPAudioStream, es);
    G(IMPAudioStream, ds);
    unsigned char pcm[2048];
    int he = -1, hd = -1, r, r2, len = 0, same = 0, cap = 800, want;

    memset(&e, 0, sizeof(e)); e.maxFrmLen = 2048; snprintf(e.name, sizeof(e.name), "AT_PCMCOPY");
    e.encoderFrm = cp_encode;
    r = IMP_AENC_RegisterEncoder(&he, &e);
    CHECK(IMP_AENC_RegisterEncoder, r, he >= 0, "silent PCM copy encoder, handle %d", he);
    if (r == 0) {
        memset(&ea, 0, sizeof(ea)); ea.type = he; ea.bufSize = 20;
        r2 = IMP_AENC_CreateChn(1, &ea);
        if (r2 == 0 && IMP_AI_GetFrame(dev, 0, f, BLOCK) == 0) {
            len = f->len < (int)sizeof(pcm) ? f->len : (int)sizeof(pcm);
            memcpy(pcm, (const void *)(uintptr_t)f->virAddr, len);
            r2 = IMP_AENC_SendFrame(1, f);
            IMP_AI_ReleaseFrame(dev, 0, f);
            if (r2 == 0 && IMP_AENC_GetStream(1, es, BLOCK) == 0) {
                same = es->len == len && es->stream && !memcmp(es->stream, pcm, len);
                IMP_AENC_ReleaseStream(1, es);
            }
        }
        IMP_AENC_DestroyChn(1);
        r2 = IMP_AENC_UnRegisterEncoder(&he);
        CHECK(IMP_AENC_UnRegisterEncoder, r2, same && cp_enc_calls > 0,
              "after one frame through the registered encoder: %d callback calls, stream %s the %d byte PCM frame", cp_enc_calls, same ? "equals" : "DIFFERS from", len);
    } else rep(FN(IMP_AENC_UnRegisterEncoder), -1, V_SKIP, "encoder not registered");

    memset(&dd, 0, sizeof(dd)); snprintf(dd.name, sizeof(dd.name), "AT_PCMCOPY");
    dd.decodeFrm = cp_decode; dd.getFrmInfo = cp_getinfo;
    r = IMP_ADEC_RegisterDecoder(&hd, &dd);
    CHECK(IMP_ADEC_RegisterDecoder, r, hd >= 0, "silent PCM copy decoder, handle %d", hd);
    if (r == 0) {
        same = 0;
        { G(IMPAudioIOAttr, ga); if (IMP_AO_GetPubAttr(0, ga) == 0 && ga->numPerFrm > 0) cap = ga->numPerFrm * 2; gfree(ga); }
        if (len <= 0) { memset(pcm, 0, 640); len = 640; }
        want = len < cap ? len : cap;
        memset(&da, 0, sizeof(da)); da.type = hd; da.bufSize = 20; da.mode = ADEC_MODE_PACK;
        r2 = IMP_ADEC_CreateChn(1, &da);
        if (r2 == 0) {
            ds->stream = pcm; ds->len = want; ds->timeStamp = 0; ds->seq = 0;
            if (IMP_ADEC_SendStream(1, ds, BLOCK) == 0) {
                memset(ds, 0, sizeof(*ds));
                if (IMP_ADEC_GetStream(1, ds, BLOCK) == 0) {
                    same = ds->len == want && ds->stream && !memcmp(ds->stream, pcm, want);
                    IMP_ADEC_ReleaseStream(1, ds);
                }
            }
            IMP_ADEC_DestroyChn(1);
        }
        r2 = IMP_ADEC_UnRegisterDecoder(&hd);
        CHECK(IMP_ADEC_UnRegisterDecoder, r2, same && cp_dec_calls > 0,
              "after one stream through the registered decoder: %d callback calls, PCM %s the %d sent bytes (nothing played)", cp_dec_calls, same ? "equals" : "DIFFERS from", want);
    } else rep(FN(IMP_ADEC_UnRegisterDecoder), -1, V_SKIP, "decoder not registered");
    gfree(f); gfree(es); gfree(ds);
}
#endif

static void ai_flow(int dev)
{
    G(IMPAudioIOAttr, attr);
    G(IMPAudioIOAttr, got);
    int r, n, tsok = 1;
    unsigned rms = 0;

    attr->samplerate = AUDIO_SAMPLE_RATE_16000; attr->bitwidth = AUDIO_BIT_WIDTH_16; attr->soundmode = AUDIO_SOUND_MODE_MONO;
#ifdef PLATFORM_T23
    attr->frmNum = 2; attr->numPerFrm = 160;
#else
    attr->frmNum = 30; attr->numPerFrm = 640;
#endif
    attr->chnCnt = 1;

    r = IMP_AI_SetPubAttr(dev, attr); gchk(attr);
    RET0(IMP_AI_SetPubAttr, r, "dev %d 16 kHz mono 16 bit, %d frames of %d samples", dev, attr->frmNum, attr->numPerFrm);
    if (r) { gfree(attr); gfree(got); return; }
    r = IMP_AI_GetPubAttr(dev, got); gchk(got);
    CHECK(IMP_AI_GetPubAttr, r, got->samplerate == AUDIO_SAMPLE_RATE_16000 && got->bitwidth == AUDIO_BIT_WIDTH_16 && got->soundmode == AUDIO_SOUND_MODE_MONO,
          "rate %d width %d mode %d frmNum %d numPerFrm %d chnCnt %d", got->samplerate, got->bitwidth, got->soundmode, got->frmNum, got->numPerFrm, got->chnCnt);
    r = IMP_AI_Enable(dev); RET0(IMP_AI_Enable, r, "dev %d", dev);
    if (r) { gfree(attr); gfree(got); return; }
    {
        G(IMPAudioIChnParam, cp);
        G(IMPAudioIChnParam, cg);
        int r2;

#ifdef PLATFORM_T23
        cp->usrFrmDepth = 2;
#else
        cp->usrFrmDepth = 30;
#endif
        r2 = IMP_AI_SetChnParam(dev, 0, cp); gchk(cp);
        RET0(IMP_AI_SetChnParam, r2, "usrFrmDepth %d", cp->usrFrmDepth);
        r2 = IMP_AI_GetChnParam(dev, 0, cg); gchk(cg);
        CHECK(IMP_AI_GetChnParam, r2, cg->usrFrmDepth == cp->usrFrmDepth, "usrFrmDepth %d (set %d)", cg->usrFrmDepth, cp->usrFrmDepth);
        gfree(cp); gfree(cg);
    }
    r = IMP_AI_EnableChn(dev, 0); RET0(IMP_AI_EnableChn, r, "dev %d chn 0", dev);
    if (r) { IMP_AI_Disable(dev); gfree(attr); gfree(got); return; }

    /* frames */
    {
        G(IMPAudioFrame, f);
        int r2, pr = IMP_AI_PollingFrame ? IMP_AI_PollingFrame(dev, 0, 3000) : 0;

        NEED(IMP_AI_PollingFrame) rep(FN(IMP_AI_PollingFrame), pr, pr == 0 ? V_PASS : V_FAIL, "timeout 3000 ms");
        r2 = IMP_AI_GetFrame(dev, 0, f, BLOCK); gchk(f);
        CHECK(IMP_AI_GetFrame, r2, f->len > 0 && f->virAddr != NULL, "len %d bytes seq %d ts %lld rms %u bitwidth %d mode %d",
              f->len, f->seq, (long long)f->timeStamp, rms16(f), f->bitwidth, f->soundmode);
        r2 = IMP_AI_ReleaseFrame(dev, 0, f); gchk(f);
        RET0(IMP_AI_ReleaseFrame, r2, "");
        gfree(f);
    }
    n = ai_frames(dev, 25, &rms, &tsok);
    rep(LBL("AI frame stream"), 0, (n == 25 && tsok) ? V_PASS : V_FAIL, "%d of 25 frames, timestamps increasing %d, last rms %u", n, tsok, rms);

    /* volume / gain */
    NEED(IMP_AI_SetVol) {
        G(int, v); int was = 0, r2;

        NEED(IMP_AI_GetVol) { IMP_AI_GetVol(dev, 0, v); was = *v; }
        r2 = IMP_AI_SetVol(dev, 0, 60);
        r = IMP_AI_GetVol ? IMP_AI_GetVol(dev, 0, v) : -1; gchk(v);
        CHECK(IMP_AI_SetVol, r2, r == 0 && *v == 60, "set 60, read back %d (was %d)", *v, was);
        if (IMP_AI_GetVol) rep(FN(IMP_AI_GetVol), r, r == 0 ? V_PASS : V_FAIL, "%d", *v);
        IMP_AI_SetVol(dev, 0, was);
        gfree(v);
    }
    NEED(IMP_AI_SetVolMute) {
        unsigned r1, r0;

        r = IMP_AI_SetVolMute(dev, 0, 1); usleep(200000);
        ai_frames(dev, 3, &r1, &tsok);
        IMP_AI_SetVolMute(dev, 0, 0); usleep(200000);
        ai_frames(dev, 3, &r0, &tsok);
        RET0(IMP_AI_SetVolMute, r, "mute: rms %u, unmute: rms %u (informational)", r1, r0);
    }
    NEED(IMP_AI_SetGain) {
        G(int, g); int was = 0, r2;

        NEED(IMP_AI_GetGain) { IMP_AI_GetGain(dev, 0, g); was = *g; }
        r2 = IMP_AI_SetGain(dev, 0, 10);
        r = IMP_AI_GetGain ? IMP_AI_GetGain(dev, 0, g) : -1; gchk(g);
        CHECK(IMP_AI_SetGain, r2, r == 0 && *g == 10, "set 10, read back %d (was %d)", *g, was);
        if (IMP_AI_GetGain) rep(FN(IMP_AI_GetGain), r, r == 0 ? V_PASS : V_FAIL, "%d", *g);
        IMP_AI_SetGain(dev, 0, was);
        gfree(g);
    }
#if HAS_IMP_AI_SetAlcGain
    NEED(IMP_AI_SetAlcGain) {
        G(int, g); int was = 0, r2;

        NEED(IMP_AI_GetAlcGain) { IMP_AI_GetAlcGain(dev, 0, g); was = *g; }
        r2 = IMP_AI_SetAlcGain(dev, 0, 3);
        r = IMP_AI_GetAlcGain ? IMP_AI_GetAlcGain(dev, 0, g) : -1; gchk(g);
        CHECK(IMP_AI_SetAlcGain, r2, r == 0 && *g == 3, "set 3, read back %d (was %d)", *g, was);
        if (IMP_AI_GetAlcGain) rep(FN(IMP_AI_GetAlcGain), r, r == 0 ? V_PASS : V_FAIL, "%d", *g);
        IMP_AI_SetAlcGain(dev, 0, was);
        gfree(g);
    }
#endif
#if HAS_IMP_AI_SetDigitalGain
    NEED(IMP_AI_SetDigitalGain) {
        G(int, g); int was = 0, r2;

        NEED(IMP_AI_GetDigitalGain) { IMP_AI_GetDigitalGain(dev, 0, g); was = *g; }
        r2 = IMP_AI_SetDigitalGain(dev, 0, 5);
        r = IMP_AI_GetDigitalGain ? IMP_AI_GetDigitalGain(dev, 0, g) : -1; gchk(g);
        CHECK(IMP_AI_SetDigitalGain, r2, r == 0 && *g == 5, "set 5, read back %d (was %d)", *g, was);
        if (IMP_AI_GetDigitalGain) rep(FN(IMP_AI_GetDigitalGain), r, r == 0 ? V_PASS : V_FAIL, "%d", *g);
        IMP_AI_SetDigitalGain(dev, 0, was);
        gfree(g);
    }
#endif

    /* software filters: enable, frames must keep flowing, disable */
    NEED(IMP_AI_EnableNs) {
        r = IMP_AI_EnableNs(attr, NS_MODERATE);
        n = ai_frames(dev, 5, &rms, &tsok);
        CHECK(IMP_AI_EnableNs, r, n == 5, "moderate; %d of 5 frames follow, rms %u", n, rms);
        NEED(IMP_AI_DisableNs) { r = IMP_AI_DisableNs(); RET0(IMP_AI_DisableNs, r, ""); }
    }
    NEED(IMP_AI_EnableHpf) {
        r = IMP_AI_EnableHpf(attr);
        n = ai_frames(dev, 5, &rms, &tsok);
        CHECK(IMP_AI_EnableHpf, r, n == 5, "%d of 5 frames follow, rms %u", n, rms);
#if HAS_IMP_AI_SetHpfCoFrequency
        NEED(IMP_AI_SetHpfCoFrequency) { int r2 = IMP_AI_SetHpfCoFrequency(200); RET0(IMP_AI_SetHpfCoFrequency, r2, "200 Hz (no getter)"); }
#endif
        NEED(IMP_AI_DisableHpf) { r = IMP_AI_DisableHpf(); RET0(IMP_AI_DisableHpf, r, ""); }
    }
    NEED(IMP_AI_EnableAgc) {
        IMPAudioAgcConfig agc;

        agc.TargetLevelDbfs = 10; agc.CompressionGaindB = 20;
#if HAS_IMP_AI_SetAgcMode
        NEED(IMP_AI_SetAgcMode) { int r2 = IMP_AI_SetAgcMode(kAgcModeAdaptiveDigital); RET0(IMP_AI_SetAgcMode, r2, "adaptive digital (no getter)"); }
#endif
        r = IMP_AI_EnableAgc(attr, agc);
        n = ai_frames(dev, 5, &rms, &tsok);
        CHECK(IMP_AI_EnableAgc, r, n == 5, "target 10 dBfs gain 20 dB; %d of 5 frames follow, rms %u", n, rms);
        NEED(IMP_AI_DisableAgc) { r = IMP_AI_DisableAgc(); RET0(IMP_AI_DisableAgc, r, ""); }
    }
#if HAS_IMP_AI_EnableAlgo
    NEED(IMP_AI_EnableAlgo) {
        r = IMP_AI_EnableAlgo(dev, 0);
        n = ai_frames(dev, 5, &rms, &tsok);
        CHECK(IMP_AI_EnableAlgo, r, n == 5, "%d of 5 frames follow, rms %u", n, rms);
        NEED(IMP_AI_DisableAlgo) { r = IMP_AI_DisableAlgo(dev, 0); RET0(IMP_AI_DisableAlgo, r, ""); }
    }
#endif
#if HAS_IMP_AI_EnableHs
    NEED(IMP_AI_EnableHs) {
        r = IMP_AI_EnableHs();
        n = ai_frames(dev, 5, &rms, &tsok);
        CHECK(IMP_AI_EnableHs, r, n == 5, "%d of 5 frames follow", n);
        NEED(IMP_AI_DisableHs) { r = IMP_AI_DisableHs(); RET0(IMP_AI_DisableHs, r, ""); }
    }
#endif
#if HAS_IMP_AI_Set_WebrtcProfileIni_Path
    NEED(IMP_AI_Set_WebrtcProfileIni_Path) {
        if (access("/etc/webrtc_profile.ini", R_OK) == 0) {
            char path[] = "/etc/webrtc_profile.ini";
            r = IMP_AI_Set_WebrtcProfileIni_Path(path);
            RET0(IMP_AI_Set_WebrtcProfileIni_Path, r, "%s", path);
        } else rep(FN(IMP_AI_Set_WebrtcProfileIni_Path), 0, V_SKIP, "/etc/webrtc_profile.ini not present");
    }
#endif
#if HAS_IMP_AI_EnableGetRaw
    NEED(IMP_AI_EnableGetRaw) {
        G(IMPAudioFrame, f); G(IMPAudioFrame, raw);
        int r2;

        r = IMP_AI_EnableGetRaw(dev, 0);
        RET0(IMP_AI_EnableGetRaw, r, "");
        NEED(IMP_AI_GetFrameAndRaw) {
            r2 = IMP_AI_GetFrameAndRaw(dev, 0, f, raw, BLOCK); gchk(f); gchk(raw);
            CHECK(IMP_AI_GetFrameAndRaw, r2, f->len > 0 && raw->len > 0, "frame %d bytes, raw %d bytes", f->len, raw->len);
            if (r2 == 0) IMP_AI_ReleaseFrame(dev, 0, f);
        }
        NEED(IMP_AI_DisableGetRaw) { r = IMP_AI_DisableGetRaw(dev, 0); RET0(IMP_AI_DisableGetRaw, r, ""); }
        gfree(f); gfree(raw);
    }
#endif
#if HAS_IMP_Audio_Select_Codec && defined(PLATFORM_T41) && !defined(APITEST_T40)
    /* T41 1.2.6: device >= 2 or channel > 0 -> -1; else ioctl _IOW('P', 74, int) on the AI node with SELECT_INTERNAL (0) / SELECT_EXTERNAL (1).  Only the
     * internal codec is selected here (the board's default path, no routing change on a board with an internal codec); the external codec is not selected */
    NEED(IMP_Audio_Select_Codec) {
        int rd = IMP_Audio_Select_Codec(2, 0, 0), rc = IMP_Audio_Select_Codec(dev, 1, 0), ri;
        int en;

        errno = 0; ri = IMP_Audio_Select_Codec(dev, 0, 0); en = errno;

        if (ri != 0 && rd == -1 && rc == -1)
            rep(FN(IMP_Audio_Select_Codec), ri, V_NA, "the board's audio driver refuses the select-codec ioctl (errno %d; the dsp_ioctl of the image's audio module has no such command): device 2 -> %d and channel 1 -> %d are refused", en, rd, rc);
        else
            CHECK(IMP_Audio_Select_Codec, ri, rd == -1 && rc == -1, "internal codec on dev %d ch 0 -> %d; device 2 -> %d, channel 1 -> %d (both refused); the external codec is not selected", dev, ri, rd, rc);
    }
#elif HAS_IMP_Audio_Select_Codec
    rep(FN(IMP_Audio_Select_Codec), 0, V_SKIP, "switches the codec routing of the board");
#endif
    /* AENC / ADEC: software G.711A round trip of a real microphone frame */
    NEED(IMP_AENC_CreateChn) {
        IMPAudioEncChnAttr ea;
        IMPAudioDecChnAttr da;
        G(IMPAudioFrame, f);
        G(IMPAudioStream, es);
        G(IMPAudioStream, ds);
        int r2, enc_len = 0, ok = 0;
        unsigned char enc[2048];

        memset(&ea, 0, sizeof(ea)); ea.type = PT_G711A; ea.bufSize = 20; ea.value = 0;
        r = IMP_AENC_CreateChn(0, &ea);
        RET0(IMP_AENC_CreateChn, r, "chn 0 G.711A");
        if (r == 0 && IMP_AI_GetFrame(dev, 0, f, BLOCK) == 0) {
            r2 = IMP_AENC_SendFrame(0, f); gchk(f);
            RET0(IMP_AENC_SendFrame, r2, "%d byte PCM frame", f->len);
            IMP_AI_ReleaseFrame(dev, 0, f);
            NEED(IMP_AENC_PollingStream) { r2 = IMP_AENC_PollingStream(0, 2000); RET0(IMP_AENC_PollingStream, r2, "timeout 2000 ms"); }
            r2 = IMP_AENC_GetStream(0, es, BLOCK); gchk(es);
            CHECK(IMP_AENC_GetStream, r2, es->len > 0 && es->stream, "%d bytes of G.711A", es->len);
            if (r2 == 0 && es->len > 0 && es->len <= (int)sizeof(enc)) { memcpy(enc, es->stream, es->len); enc_len = es->len; ok = 1; }
            r2 = IMP_AENC_ReleaseStream(0, es); gchk(es);
            RET0(IMP_AENC_ReleaseStream, r2, "");
        } else if (r == 0) rep(FN(IMP_AENC_SendFrame), -1, V_SKIP, "no microphone frame");
        NEED(IMP_AENC_DestroyChn) { r2 = r == 0 ? IMP_AENC_DestroyChn(0) : -1; RET0(IMP_AENC_DestroyChn, r2, "chn 0"); }

        NEED(IMP_ADEC_CreateChn) {
            memset(&da, 0, sizeof(da)); da.type = PT_G711A; da.bufSize = 20; da.mode = ADEC_MODE_PACK; da.value = NULL;
            r = IMP_ADEC_CreateChn(0, &da);
            RET0(IMP_ADEC_CreateChn, r, "chn 0 G.711A pack mode");
            if (r == 0 && ok) {
                ds->stream = enc; ds->len = enc_len; ds->timeStamp = 0; ds->seq = 0;
                r2 = IMP_ADEC_SendStream(0, ds, BLOCK); gchk(ds);
                RET0(IMP_ADEC_SendStream, r2, "%d bytes", enc_len);
                NEED(IMP_ADEC_PollingStream) { r2 = IMP_ADEC_PollingStream(0, 2000); RET0(IMP_ADEC_PollingStream, r2, "timeout 2000 ms"); }
                memset(ds, 0, sizeof(*ds));
                r2 = IMP_ADEC_GetStream(0, ds, BLOCK); gchk(ds);
                /* A decoder node holds one AO frame (numPerFrm * 2 bytes, 800 while the AO device attributes are
                 * not set yet, as in the vendor libimp): the PCM is cut to the node size. */
                int cap = 800, want;
                { G(IMPAudioIOAttr, ga); if (IMP_AO_GetPubAttr(0, ga) == 0 && ga->numPerFrm > 0) cap = ga->numPerFrm * 2; gfree(ga); }
                want = enc_len * 2 < cap ? enc_len * 2 : cap;
                CHECK(IMP_ADEC_GetStream, r2, ds->len == want, "%d bytes PCM (expected %d = min(2 x %d, node %d))", ds->len, want, enc_len, cap);
                r2 = IMP_ADEC_ReleaseStream(0, ds); gchk(ds);
                RET0(IMP_ADEC_ReleaseStream, r2, "");
                NEED(IMP_ADEC_ClearChnBuf) { r2 = IMP_ADEC_ClearChnBuf(0); RET0(IMP_ADEC_ClearChnBuf, r2, ""); }
            }
            NEED(IMP_ADEC_DestroyChn) { r2 = r == 0 ? IMP_ADEC_DestroyChn(0) : -1; RET0(IMP_ADEC_DestroyChn, r2, "chn 0"); }
        }
#if defined(PLATFORM_T30)
        codec_register_test(dev);
#else
        rep(FN(IMP_AENC_RegisterEncoder), 0, V_SKIP, "custom codec registration (callbacks), not exercised");
        rep(FN(IMP_AENC_UnRegisterEncoder), 0, V_SKIP, "see RegisterEncoder");
        rep(FN(IMP_ADEC_RegisterDecoder), 0, V_SKIP, "custom codec registration (callbacks), not exercised");
        rep(FN(IMP_ADEC_UnRegisterDecoder), 0, V_SKIP, "see RegisterDecoder");
#endif
        gfree(f); gfree(es); gfree(ds);
    }

    r = IMP_AI_DisableChn(dev, 0); RET0(IMP_AI_DisableChn, r, "dev %d chn 0", dev);
    r = IMP_AI_Disable(dev); RET0(IMP_AI_Disable, r, "dev %d", dev);
    gfree(attr); gfree(got);
}

/* AO / AEC: SILENCE ONLY (user decision 2026-10-26): muted, volume at the minimum, only zero-filled PCM frames (a few), return codes and round trips. */
static void ao_flow(void)
{
    G(IMPAudioIOAttr, attr);
    G(IMPAudioIOAttr, got);
    int r, ao = 0;

    if (getenv("APITEST_AO") && !atoi(getenv("APITEST_AO"))) {      /* APITEST_AO=0 skips all AO/AEC calls */
        rep(LBL("AO functions"), 0, V_SKIP, "APITEST_AO=0");
        gfree(attr); gfree(got);
        return;
    }
    attr->samplerate = AUDIO_SAMPLE_RATE_16000; attr->bitwidth = AUDIO_BIT_WIDTH_16; attr->soundmode = AUDIO_SOUND_MODE_MONO;
    attr->frmNum = 10; attr->numPerFrm = 640; attr->chnCnt = 1;
    NEED(IMP_AO_SetPubAttr) { r = IMP_AO_SetPubAttr(ao, attr); gchk(attr); RET0(IMP_AO_SetPubAttr, r, "dev %d 16 kHz mono", ao); if (r) goto out; }
    NEED(IMP_AO_GetPubAttr) { r = IMP_AO_GetPubAttr(ao, got); gchk(got); CHECK(IMP_AO_GetPubAttr, r, got->samplerate == AUDIO_SAMPLE_RATE_16000, "rate %d mode %d", got->samplerate, got->soundmode); }
    NEED(IMP_AO_Enable) { r = IMP_AO_Enable(ao); RET0(IMP_AO_Enable, r, "dev %d", ao); if (r) goto out; }
    NEED(IMP_AO_EnableChn) { r = IMP_AO_EnableChn(ao, 0); RET0(IMP_AO_EnableChn, r, "chn 0"); }
    NEED(IMP_AO_SetVolMute) { r = IMP_AO_SetVolMute(ao, 0, 1); RET0(IMP_AO_SetVolMute, r, "rt: mute on (kept muted for all AO tests)"); }
    NEED(IMP_AO_SetVol) {
        G(int, v); int was = 0, r2, r3;
        NEED(IMP_AO_GetVol) { IMP_AO_GetVol(ao, 0, v); was = *v; }
        r2 = IMP_AO_SetVol(ao, 0, -30);          /* the minimum of the vendor range [-30, 120] (= mute); it stays there, the original is NOT restored while sound could follow */
        r = IMP_AO_GetVol ? IMP_AO_GetVol(ao, 0, v) : -1; gchk(v);
        r3 = IMP_AO_SetVol(ao, 0, -29);
        if (r3 == 0) IMP_AO_SetVol(ao, 0, -30);
        CHECK(IMP_AO_SetVol, r2 | r3, r == 0 && *v == -30, "rt: set the minimum -30, read back %d (was %d), -29 accepted; volume left at the minimum", *v, was);
        if (IMP_AO_GetVol) rep(FN(IMP_AO_GetVol), r, r == 0 ? V_PASS : V_FAIL, "%d", *v);
        gfree(v);
    }
    NEED(IMP_AO_SendFrame) {
        /* a few zero-filled PCM frames: silence */
        static uint32_t zbuf[1280 / 4];
        IMPAudioFrame zf; int k, rs = 0;

        memset(&zf, 0, sizeof(zf)); memset(zbuf, 0, sizeof(zbuf));
        zf.bitwidth = AUDIO_BIT_WIDTH_16; zf.soundmode = AUDIO_SOUND_MODE_MONO; zf.virAddr = zbuf; zf.len = 1280;
        for (k = 0; k < 3; k++) rs |= IMP_AO_SendFrame(ao, 0, &zf, BLOCK);
        rep(FN(IMP_AO_SendFrame), rs, rs == 0 ? V_PASS : V_FAIL, "rt: 3 zero-filled frames of 1280 bytes (silence), muted, volume minimum");
    }
    NEED(IMP_AO_SetGain) {
        G(int, g); int was = 0, r2;
        NEED(IMP_AO_GetGain) { IMP_AO_GetGain(ao, 0, g); was = *g; }
        r2 = IMP_AO_SetGain(ao, 0, was);
        r = IMP_AO_GetGain ? IMP_AO_GetGain(ao, 0, g) : -1; gchk(g);
        CHECK(IMP_AO_SetGain, r2, r == 0 && *g == was, "rt: same value written, read back %d (was %d)", *g, was);
        if (IMP_AO_GetGain) rep(FN(IMP_AO_GetGain), r, r == 0 ? V_PASS : V_FAIL, "%d", *g);
        IMP_AO_SetGain(ao, 0, was);
        gfree(g);
    }
#if HAS_IMP_AO_SetDigitalGain
    NEED(IMP_AO_SetDigitalGain) {
        G(int, g); int was = 0, r2;
        NEED(IMP_AO_GetDigitalGain) { IMP_AO_GetDigitalGain(ao, 0, g); was = *g; }
        r2 = IMP_AO_SetDigitalGain(ao, 0, was);
        r = IMP_AO_GetDigitalGain ? IMP_AO_GetDigitalGain(ao, 0, g) : -1; gchk(g);
        CHECK(IMP_AO_SetDigitalGain, r2, r == 0 && *g == was, "rt: same value written, read back %d (was %d)", *g, was);
        if (IMP_AO_GetDigitalGain) rep(FN(IMP_AO_GetDigitalGain), r, r == 0 ? V_PASS : V_FAIL, "%d", *g);
        IMP_AO_SetDigitalGain(ao, 0, was);
        gfree(g);
    }
#endif
    NEED(IMP_AO_Soft_Mute) { r = IMP_AO_Soft_Mute(ao, 0); RET0(IMP_AO_Soft_Mute, r, ""); }
    NEED(IMP_AO_Soft_UNMute) { r = IMP_AO_Soft_UNMute(ao, 0); RET0(IMP_AO_Soft_UNMute, r, ""); }
    NEED(IMP_AO_SetVolMute) { IMP_AO_SetVolMute(ao, 0, 1); }
    NEED(IMP_AO_PauseChn) { r = IMP_AO_PauseChn(ao, 0); RET0(IMP_AO_PauseChn, r, ""); }
    NEED(IMP_AO_ResumeChn) { r = IMP_AO_ResumeChn(ao, 0); RET0(IMP_AO_ResumeChn, r, ""); }
    NEED(IMP_AO_ClearChnBuf) { r = IMP_AO_ClearChnBuf(ao, 0); RET0(IMP_AO_ClearChnBuf, r, ""); }
    NEED(IMP_AO_FlushChnBuf) { r = IMP_AO_FlushChnBuf(ao, 0); RET0(IMP_AO_FlushChnBuf, r, ""); }
    NEED(IMP_AO_CacheSwitch) { r = IMP_AO_CacheSwitch(ao, 0, 0); RET0(IMP_AO_CacheSwitch, r, "cache off"); }
    NEED(IMP_AO_QueryChnStat) {
        G(IMPAudioOChnState, st);
        r = IMP_AO_QueryChnStat(ao, 0, st); gchk(st);
        CHECK(IMP_AO_QueryChnStat, r, st->chnTotalNum >= st->chnFreeNum, "total %d free %d busy %d", st->chnTotalNum, st->chnFreeNum, st->chnBusyNum);
        gfree(st);
    }
    NEED(IMP_AO_EnableAgc) {
        IMPAudioAgcConfig agc;
        agc.TargetLevelDbfs = 10; agc.CompressionGaindB = 20;
        r = IMP_AO_EnableAgc(attr, agc); RET0(IMP_AO_EnableAgc, r, "target 10 gain 20");
        NEED(IMP_AO_DisableAgc) { r = IMP_AO_DisableAgc(); RET0(IMP_AO_DisableAgc, r, ""); }
    }
    NEED(IMP_AO_EnableHpf) {
        r = IMP_AO_EnableHpf(attr); RET0(IMP_AO_EnableHpf, r, "");
#if HAS_IMP_AO_SetHpfCoFrequency
        NEED(IMP_AO_SetHpfCoFrequency) { r = IMP_AO_SetHpfCoFrequency(200); RET0(IMP_AO_SetHpfCoFrequency, r, "200 Hz (no getter)"); }
#endif
        NEED(IMP_AO_DisableHpf) { r = IMP_AO_DisableHpf(); RET0(IMP_AO_DisableHpf, r, ""); }
    }
#if HAS_IMP_AO_EnableAlgo
    NEED(IMP_AO_EnableAlgo) {
        r = IMP_AO_EnableAlgo(ao, 0); RET0(IMP_AO_EnableAlgo, r, "");
        NEED(IMP_AO_DisableAlgo) { r = IMP_AO_DisableAlgo(ao, 0); RET0(IMP_AO_DisableAlgo, r, ""); }
    }
#endif
    /* AEC round trip with the muted AO as reference: silence only */
#if HAS_IMP_AI_EnableAec
    NEED(IMP_AI_EnableAec) NEED(IMP_AI_SetPubAttr) NEED(IMP_AI_Enable) NEED(IMP_AI_EnableChn) {
        int adev = aidev(), r2, r3, r4 = 0, k, got = 0;
        G(IMPAudioFrame, fr); G(IMPAudioFrame, ref);
        static uint32_t zb[1280 / 4];
        IMPAudioFrame zf;

        memset(&zf, 0, sizeof(zf)); zf.bitwidth = AUDIO_BIT_WIDTH_16; zf.soundmode = AUDIO_SOUND_MODE_MONO; zf.virAddr = zb; zf.len = 1280;
        r = IMP_AI_SetPubAttr(adev, attr);
        if (r == 0) r = IMP_AI_Enable(adev);
        if (r == 0) r = IMP_AI_EnableChn(adev, 0);
        if (r == 0) {
            r2 = IMP_AI_EnableAec(adev, 0, ao, 0);
            rep(FN(IMP_AI_EnableAec), r2, r2 == 0 ? V_PASS : V_FAIL, "rt: AI dev %d chn 0 with the muted AO dev %d chn 0 as reference", adev, ao);
            for (k = 0; k < 3; k++) IMP_AO_SendFrame(ao, 0, &zf, BLOCK);
#if HAS_IMP_AI_GetFrameAndRef
            NEED(IMP_AI_GetFrameAndRef) {
                r3 = IMP_AI_GetFrameAndRef(adev, 0, fr, ref, BLOCK); gchk(fr); gchk(ref);
                got = (r3 == 0 && fr->len > 0);
                rep(FN(IMP_AI_GetFrameAndRef), r3, got ? V_PASS : V_FAIL, "rt: frame %d bytes, reference %d bytes (AO sends only zeros)", fr->len, ref->len);
                if (r3 == 0) IMP_AI_ReleaseFrame(adev, 0, fr);
            }
#endif
#if HAS_IMP_AI_EnableAecRefFrame
            NEED(IMP_AI_EnableAecRefFrame) { r4 = IMP_AI_EnableAecRefFrame(adev, 0, ao, 0); RET0(IMP_AI_EnableAecRefFrame, r4, "rt: reference frame on"); }
            NEED(IMP_AI_DisableAecRefFrame) { r4 = IMP_AI_DisableAecRefFrame(adev, 0, ao, 0); RET0(IMP_AI_DisableAecRefFrame, r4, "rt: reference frame off"); }
#endif
            NEED(IMP_AI_DisableAec) { r4 = IMP_AI_DisableAec(adev, 0); RET0(IMP_AI_DisableAec, r4, "rt: AEC off"); }
            IMP_AI_DisableChn(adev, 0); IMP_AI_Disable(adev);
        } else
            rep(FN(IMP_AI_EnableAec), r, V_FAIL, "AI enable for the AEC test failed");
        gfree(fr); gfree(ref);
    }
#endif
    NEED(IMP_AO_DisableChn) { r = IMP_AO_DisableChn(ao, 0); RET0(IMP_AO_DisableChn, r, "chn 0"); }
    NEED(IMP_AO_Disable) { r = IMP_AO_Disable(ao); RET0(IMP_AO_Disable, r, "dev %d", ao); }
out:
    gfree(attr); gfree(got);
}

void t_audio(void)
{
    int dev = aidev(), r;

    rep_area("audio");
    if (!IMP_AI_SetPubAttr) { rep_na(FN(IMP_AI_SetPubAttr)); return; }
    ai_flow(dev);
    ao_flow();
    (void)r;
}
