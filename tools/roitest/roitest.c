/* roitest - encoder ROI on-device test (T21/T23 family A, T31/T41 family B).
 *
 *   roitest SENSOR I2C_ADDR W H OUTDIR PHASE...
 *   PHASE = label:seconds:spec        label starting with '~': no IDR request
 *   spec  = none | region[+region...]
 *   region= idx,mode,qp,x,y,w,h       mode a = absolute QP, d = delta QP
 *                                     pixels; family A p1 = x+w-1, y+h-1
 *
 * Every phase first disables all regions, applies the spec, requests an IDR
 * (unless '~'), then writes the H.264 stream to OUTDIR/label.h264 and prints
 * one result line:  [R] label frames bytes kbps idr_avg p_avg
 * Env: ROITEST_FS_FIRST=1 (enable FS before creating the encoder), ROITEST_OUT=WxH (scale ch0), ROITEST_RC=cbr|fixqp (cbr), ROITEST_KBPS (2000), ROITEST_QP (30),
 *      ROITEST_FPS (15), ROITEST_MINQP/ROITEST_MAXQP (15/45).
 * T40/T41 extras (spec part after ';'): z<ch>=l,t,w,h | z<ch>=off  SetAutoZoom on output ch
 *      (sensor px), zr<ch>=... same with stop/set/restart of output ch (vendor way), zc<ch>=N,ms cycle
 *      N calls over 3 windows, s<ch>=mode,level SetScalerLv.  ROITEST_CH1=WxH adds output 1 + encoder 1
 *      (ROITEST_CH1_QP=n: FixQP n reference on ch1, ROITEST_CH1_OUT=path: ch1 stream for all phases)
 *      (stream OUTDIR/label_ch1.h264), ROITEST_ZPRE=ch,l,t,w,h AutoZoom before the first start.
 * T31/T40/T41: ROITEST_RC=vbr|cvbr|cq (VBR, CappedVBR, CappedQuality) with ROITEST_MAXKBPS
 *      (uMaxBitRate, default SetDefaultParam) and ROITEST_PSNR (uMaxPSNR dB).
 * T40/T41: ROITEST_I2D=flip,mirror,rotate_enable,angle sets the ch0 I2D attribute (CreateChn
 *      attr + SetI2dAttr, vendor semantics: picture size stays the capture size, the encoder
 *      gets the rotated size for 90/270).  Output 1 gets no I2D unless ROITEST_CH1_I2D (same
 *      format, ch1 encoder takes the rotated size for 90/270).
 * T41: ROITEST_OSD=x,y puts a 128x64 BGRA test picture (red top-left square, green
 *      bottom-right square, white border) at x,y of the frame the OSD group sees (ch0;
 *      FS -> OSD group 0 -> encoder), to check OSD placement with ROITEST_I2D.
 * OUTDIR "-": the ch0 stream of all phases goes to stdout (pipe it to the host, the camera's
 *      /tmp is small); every other output goes to stderr.
 * Stop the streamer first; the camera's /tmp is RAM, use short phases.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <imp/imp_common.h>
#include <imp/imp_system.h>
#include <imp/imp_isp.h>
#include <imp/imp_framesource.h>
#include <imp/imp_encoder.h>
#if defined(PLATFORM_T40)
# define PLATFORM_T41 1   /* same API shape (IMPVI_NUM, family B ROI) */
# define SOC_T40 1
#endif
#ifdef PLATFORM_T41
#include <imp/imp_osd.h>
#endif

#if defined(PLATFORM_T31) || defined(PLATFORM_T41)
# define ENC_NEW 1
#endif
#if defined(PLATFORM_T31) || (defined(PLATFORM_T41) && !defined(SOC_T40))
/* OpenIMP-only family B API (beyond vendor on T31; T41 1.2.0 headers lack it) */
# define ROI_WIN 10
typedef struct { uint32_t x, y, w, h; } RRect;
typedef struct { bool enable; RRect rect; int mode; int8_t qp; } RWin;
typedef struct { RWin st_roi[ROI_WIN]; } RAttr;

# define HAVE_B 1
#elif defined(PLATFORM_T41)
# define HAVE_B 1
typedef IMPEncoderRoiAttr RAttr;
#endif

#ifdef HAVE_B
/* weak: a libimp without the family B API gets a clear message */
int IMP_Encoder_SetChnRoiAttr(int encChn, RAttr *a) __attribute__((weak));
int IMP_Encoder_GetChnRoiAttr(int encChn, RAttr *a) __attribute__((weak));
#endif
/* the vendor libsysutils/libalog expect these from the application or libimp */
#include <stdarg.h>
__attribute__((weak)) int IMP_Log_Get_Option(void) { return 0; }
/* OEM signature (level, option, type, tag, file, line, func, fmt, ...): with
 * --export-dynamic this definition replaces libimp's own, so it must take
 * the same arguments (the old (level, tag, fmt) form crashed on T23 with
 * ROITEST_LOG=1: fmt was the integer "type") */
__attribute__((weak)) int imp_log_fun(int level, int option, int type, const char *tag, const char *file,
                                      int line, const char *func, const char *fmt, ...)
{
    va_list ap;
    (void)level; (void)option; (void)type; (void)tag; (void)file; (void)line; (void)func;
    if (!fmt || !getenv("ROITEST_LOG")) return 0;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    return 0;
}
#define CK(e, m) do { int r_ = (e); if (r_ < 0) { printf("[E] %s -> %d\n", m, r_); exit(1); } } while (0)

static int64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static int nal_has_idr(const unsigned char *b, size_t n)
{
    size_t i;
    for (i = 0; i + 4 < n; i++)
        if (b[i] == 0 && b[i + 1] == 0 && b[i + 2] == 1 && (b[i + 3] & 0x1f) == 5)
            return 1;
    return 0;
}

typedef struct { int idx, abs, qp, x, y, w, h; } Reg;

static int parse_spec(const char *s, Reg *r)
{
    int n = 0;
    char buf[256], *tok, *sv = NULL;

    if (!strcmp(s, "none")) return 0;
    snprintf(buf, sizeof(buf), "%s", s);
    for (tok = strtok_r(buf, "+", &sv); tok && n < 10; tok = strtok_r(NULL, "+", &sv)) {
        char m;
        if (sscanf(tok, "%d,%c,%d,%d,%d,%d,%d", &r[n].idx, &m, &r[n].qp, &r[n].x, &r[n].y, &r[n].w, &r[n].h) != 7) {
            printf("[E] bad region '%s'\n", tok);
            exit(2);
        }
        r[n].abs = m == 'a';
        n++;
    }
    return n;
}

static void apply_roi(int chn, const Reg *r, int n)
{
    int i, ret;
#ifdef HAVE_B
    RAttr a;
    memset(&a, 0, sizeof(a));
    for (i = 0; i < n; i++) {
        a.st_roi[r[i].idx].enable = true;
        a.st_roi[r[i].idx].rect.x = r[i].x; a.st_roi[r[i].idx].rect.y = r[i].y;
        a.st_roi[r[i].idx].rect.w = r[i].w; a.st_roi[r[i].idx].rect.h = r[i].h;
        a.st_roi[r[i].idx].mode = r[i].abs ? 1 : 0;
        a.st_roi[r[i].idx].qp = (int8_t)r[i].qp;
    }
    if (!IMP_Encoder_SetChnRoiAttr) { printf("[R] SetChnRoiAttr not exported\n"); return; }
    ret = IMP_Encoder_SetChnRoiAttr(chn, &a);
    printf("[R] SetChnRoiAttr(%d regions) ret=%d\n", n, ret);
    memset(&a, 0, sizeof(a));
    ret = IMP_Encoder_GetChnRoiAttr(chn, &a);
    for (i = 0; i < n; i++)
        printf("[R]   get win%d en=%d %u,%u %ux%u mode=%d qp=%d (ret %d)\n", r[i].idx, a.st_roi[r[i].idx].enable,
               a.st_roi[r[i].idx].rect.x, a.st_roi[r[i].idx].rect.y, a.st_roi[r[i].idx].rect.w,
               a.st_roi[r[i].idx].rect.h, (int)a.st_roi[r[i].idx].mode, a.st_roi[r[i].idx].qp, ret);
#else
    IMPEncoderROICfg c;
    for (i = 0; i < 8; i++) {
        memset(&c, 0, sizeof(c));
        c.u32Index = i;
        IMP_Encoder_SetChnROI(chn, &c);
    }
    for (i = 0; i < n; i++) {
        memset(&c, 0, sizeof(c));
        c.u32Index = r[i].idx; c.bEnable = true; c.bRelatedQp = !r[i].abs; c.s32Qp = r[i].qp;
        c.rect.p0.x = r[i].x; c.rect.p0.y = r[i].y;
        c.rect.p1.x = r[i].x + r[i].w - 1; c.rect.p1.y = r[i].y + r[i].h - 1;
        ret = IMP_Encoder_SetChnROI(chn, &c);
        memset(&c, 0, sizeof(c));
        c.u32Index = r[i].idx;
        {
            int g = IMP_Encoder_GetChnROI(chn, &c);
            printf("[R]   reg%d set=%d get=%d en=%d rel=%d qp=%d (%d,%d)-(%d,%d)\n", r[i].idx, ret, g, c.bEnable,
                   c.bRelatedQp, c.s32Qp, c.rect.p0.x, c.rect.p0.y, c.rect.p1.x, c.rect.p1.y);
        }
    }
#endif
}

#ifdef PLATFORM_T41
/* AutoZoom / ScalerLv (T40/T41 ISP tuning; OpenIMP pass-through to the
 * kernel CIDs 0x08000077 / 0x080000a6).  Own layouts so the T40 headers
 * (no ScalerLv type) build the same tool. */
typedef struct { int32_t en[3], l[3], t[3], w[3], h[3]; } RZoom;   /* = IMPISPAutoZoom, 60 bytes */
#pragma weak IMP_ISP_Tuning_SetAutoZoom
#pragma weak IMP_ISP_Tuning_GetAutoZoom
#ifdef SOC_T40
# define SLV_FN IMP_ISP_SetScalerLv
#pragma weak IMP_ISP_SetScalerLv
#else
# define SLV_FN IMP_ISP_Tuning_SetScalerLv   /* T41 1.2.x name */
#pragma weak IMP_ISP_Tuning_SetScalerLv
#endif
static RZoom zcur;
static int sensor_w, sensor_h;

static int do_zoom(int ch, int on, int l, int t, int w, int h)
{
    RZoom g;
    int r, rg;

    if (!IMP_ISP_Tuning_SetAutoZoom) { printf("[Z] SetAutoZoom not exported\n"); return -1; }
    zcur.en[ch] = on;
    zcur.l[ch] = on ? l : 0; zcur.t[ch] = on ? t : 0;
    zcur.w[ch] = on ? w : sensor_w; zcur.h[ch] = on ? h : sensor_h;
    r = IMP_ISP_Tuning_SetAutoZoom(IMPVI_MAIN, (IMPISPAutoZoom *)(void *)&zcur);
    memset(&g, 0, sizeof(g));
    rg = IMP_ISP_Tuning_GetAutoZoom ? IMP_ISP_Tuning_GetAutoZoom(IMPVI_MAIN, (IMPISPAutoZoom *)(void *)&g) : -99;
    printf("[Z] SetAutoZoom ch%d en=%d %d,%d %dx%d ret=%d get=%d -> %d,%d %dx%d\n", ch, on, l, t, w, h, r, rg,
           g.l[ch], g.t[ch], g.w[ch], g.h[ch]);
    return r;
}

static void do_slv(int ch, int mode, int level)
{
    IMPISPScalerLvAttr a;
    int r;

    if (!SLV_FN) { printf("[Z] SetScalerLv not exported\n"); return; }
    memset(&a, 0, sizeof(a));
    a.chx = (uint8_t)ch; a.mode = (IMPISPScalerMode)mode; a.level = (uint8_t)level;
    r = SLV_FN(IMPVI_MAIN, &a);
    printf("[Z] SetScalerLv ch%d mode=%d level=%d ret=%d\n", ch, mode, level, r);
}
#endif

/* output 1 (ROITEST_CH1) */
static int ch1_on, ch1_w, ch1_h, jpeg_on;

static void restart_out(int ch, int stop)
{
    /* vendor way for a larger geometry change: encoder stop, output off,
     * (caller sets), output on, encoder on */
    if (stop) {
        IMP_Encoder_StopRecvPic(ch);
        IMP_FrameSource_DisableChn(ch);
    } else {
        IMP_FrameSource_EnableChn(ch);
        IMP_Encoder_StartRecvPic(ch);
        IMP_Encoder_RequestIDR(ch);
    }
}

static void apply_extra(const char *ex)
{
#ifdef PLATFORM_T41
    char buf[256], *tok, *sv = NULL;
    int ch, a[5];

    snprintf(buf, sizeof(buf), "%s", ex);
    for (tok = strtok_r(buf, ";", &sv); tok; tok = strtok_r(NULL, ";", &sv)) {
        if (tok[0] == 'z' && tok[1] == 'c') continue;   /* cycle: in the loop */
        if (tok[0] == 'z' && tok[1] == 'r' && sscanf(tok + 2, "%d=%d,%d,%d,%d", &ch, &a[0], &a[1], &a[2], &a[3]) == 5) {
            restart_out(ch, 1);
            do_zoom(ch, 1, a[0], a[1], a[2], a[3]);
            restart_out(ch, 0);
        } else if (tok[0] == 'z' && sscanf(tok + 1, "%d=%d,%d,%d,%d", &ch, &a[0], &a[1], &a[2], &a[3]) == 5)
            do_zoom(ch, 1, a[0], a[1], a[2], a[3]);
        else if (tok[0] == 'z' && sscanf(tok + 1, "%d=off", &ch) == 1)
            do_zoom(ch, 0, 0, 0, 0, 0);
        else if (tok[0] == 's' && sscanf(tok + 1, "%d=%d,%d", &ch, &a[0], &a[1]) == 3)
            do_slv(ch, a[0], a[1]);
        else
            printf("[E] bad extra '%s'\n", tok);
    }
#else
    (void)ex;
#endif
}

static void dump_counters(const char *lab)
{
    /* open-tx-isp T41 MSCA counters (absent on other drivers) */
    static const char *n[] = { "msca_ctl_live", "msca_ctl_deferred", "msca_ctl_failed", "msca_latch_timeouts",
                               "msca_live_disables" };
    char path[128], v[32];
    unsigned i;

    printf("[C] %s", lab);
    for (i = 0; i < sizeof(n) / sizeof(n[0]); i++) {
        FILE *f;
        snprintf(path, sizeof(path), "/sys/module/tx_isp_t41/parameters/%s", n[i]);
        f = fopen(path, "r");
        if (!f) continue;
        if (fgets(v, sizeof(v), f)) { v[strcspn(v, "\n")] = 0; printf(" %s=%s", n[i] + 5, v); }
        fclose(f);
    }
    printf("\n");
}

#ifdef PLATFORM_T41
static int osd_h;
#endif
#ifdef ENC_NEW
static void *jpeg_thread(void *arg)
{
    int tries;
    (void)arg;
    for (tries = 0; tries < 60 && jpeg_on == 2; tries++) {
        IMPEncoderStream js;
        if (IMP_Encoder_PollingStream(4, 500) != 0) continue;
        memset(&js, 0, sizeof(js));
        if (IMP_Encoder_GetStream(4, &js, true) == 0) {
            FILE *jf = fopen(getenv("ROITEST_CH1_JPEG"), "wb");
            uint32_t k;
            if (jf) {
                const unsigned char *base = (const unsigned char *)(uintptr_t)js.virAddr;
                for (k = 0; k < js.packCount; k++)
                    fwrite(base + js.pack[k].offset, 1, js.pack[k].length, jf);
                fclose(jf);
            }
            IMP_Encoder_ReleaseStream(4, &js);
            fprintf(stderr, "[R] jpeg snapshot %u bytes\n", js.streamSize);
            IMP_Encoder_StopRecvPic(4);
            jpeg_on = 3;
        }
    }
    return NULL;
}
#endif

static long drain(int chn, unsigned char *buf, FILE *f, int *frames, int *idrs, long *idr_b, long *p_b)
{
    IMPEncoderStream s;
    size_t len = 0;
    uint32_t k;

    if (IMP_Encoder_PollingStream(chn, ch1_on ? 100 : 1000) != 0) return -1;
    memset(&s, 0, sizeof(s));
    if (IMP_Encoder_GetStream(chn, &s, true) != 0) return -1;
    for (k = 0; k < s.packCount; k++) {
        const IMPEncoderPack *p = &s.pack[k];
#ifdef ENC_NEW
        const unsigned char *base = (const unsigned char *)(uintptr_t)s.virAddr;
        size_t rem = s.streamSize > p->offset ? s.streamSize - p->offset : 0;
        if (!p->length || len + p->length > (2u << 20)) continue;
        if (rem && rem < p->length) {
            memcpy(buf + len, base + p->offset, rem);
            memcpy(buf + len + rem, base, p->length - rem);
        } else
            memcpy(buf + len, base + p->offset, p->length);
#else
        if (!p->length || len + p->length > (2u << 20)) continue;
        memcpy(buf + len, (const void *)(uintptr_t)p->virAddr, p->length);
#endif
        len += p->length;
    }
    IMP_Encoder_ReleaseStream(chn, &s);
    {
        /* camera /tmp is RAM: cap every file (ROITEST_MAXKB, 1024) and the
         * run (4x that); frames beyond are counted, not written */
        static long total;
        static long cap = -1;
        long fsz = f ? ftell(f) : 0;
        if (cap < 0) cap = 1024L * (getenv("ROITEST_MAXKB") ? atol(getenv("ROITEST_MAXKB")) : 1024L);
        static long tcap = -1;
        if (tcap < 0) tcap = getenv("ROITEST_TOTALKB") ? 1024L * atol(getenv("ROITEST_TOTALKB")) : 4 * cap;
        if (f && fsz + (long)len <= cap && total + (long)len <= tcap) {
            fwrite(buf, 1, len, f);
            total += (long)len;
        }
    }
    (*frames)++;
    if (nal_has_idr(buf, len)) { (*idrs)++; *idr_b += len; } else *p_b += len;
    return (long)len;
}

static FILE *stream_out;

static void mk_enc(int chn, int W, int H, int fps, int fixqp, int qp, int kbps, int minqp, int maxqp)
{
#ifdef ENC_NEW
    {
        IMPEncoderChnAttr a;
        const char *rcs = fixqp ? NULL : getenv("ROITEST_RC");
        IMPEncoderRcMode m = fixqp ? IMP_ENC_RC_MODE_FIXQP : IMP_ENC_RC_MODE_CBR;

        if (rcs && !strcmp(rcs, "vbr")) m = IMP_ENC_RC_MODE_VBR;
        if (rcs && !strcmp(rcs, "cvbr")) m = IMP_ENC_RC_MODE_CAPPED_VBR;
        if (rcs && !strcmp(rcs, "cq")) m = IMP_ENC_RC_MODE_CAPPED_QUALITY;
        memset(&a, 0, sizeof(a));
        CK(IMP_Encoder_SetDefaultParam(&a, IMP_ENC_PROFILE_AVC_HIGH, m,
                                       W, H, fps, 1, fps * 2, 2, fixqp ? qp : -1, fixqp ? 0 : kbps), "SetDefaultParam");
        if (m == IMP_ENC_RC_MODE_VBR || m == IMP_ENC_RC_MODE_CAPPED_VBR || m == IMP_ENC_RC_MODE_CAPPED_QUALITY) {
            a.rcAttr.attrRcMode.attrCappedVbr.iMinQP = minqp;
            a.rcAttr.attrRcMode.attrCappedVbr.iMaxQP = maxqp;
            if (getenv("ROITEST_MAXKBPS"))
                a.rcAttr.attrRcMode.attrCappedVbr.uMaxBitRate = atoi(getenv("ROITEST_MAXKBPS"));
            if (m != IMP_ENC_RC_MODE_VBR && getenv("ROITEST_PSNR"))
                a.rcAttr.attrRcMode.attrCappedVbr.uMaxPSNR = atoi(getenv("ROITEST_PSNR"));
            fprintf(stderr, "[R] rc mode %d target %u max %u psnr %u qp %d..%d\n", (int)m,
                    a.rcAttr.attrRcMode.attrCappedVbr.uTargetBitRate, a.rcAttr.attrRcMode.attrCappedVbr.uMaxBitRate,
                    m == IMP_ENC_RC_MODE_VBR ? 0u : a.rcAttr.attrRcMode.attrCappedVbr.uMaxPSNR, minqp, maxqp);
        } else if (fixqp) {
            a.rcAttr.attrRcMode.attrFixQp.iInitialQP = qp;
#if defined(PLATFORM_T41) && !defined(SOC_T40)
            /* ROITEST_FQRANGE=1: keep MINQP..MAXQP so the AVPU clamp
             * (cmd[179]) leaves room for the ROI deltas */
            a.rcAttr.attrRcMode.attrFixQp.iMinQP = getenv("ROITEST_FQRANGE") ? minqp : qp;
            a.rcAttr.attrRcMode.attrFixQp.iMaxQP = getenv("ROITEST_FQRANGE") ? maxqp : qp;
#endif
        } else {
            a.rcAttr.attrRcMode.attrCbr.iMinQP = minqp;
            a.rcAttr.attrRcMode.attrCbr.iMaxQP = maxqp;
        }
        CK(IMP_Encoder_CreateChn(chn, &a), "Enc_CreateChn");
    }
#else
    {
        IMPEncoderCHNAttr a;
        memset(&a, 0, sizeof(a));
        a.encAttr.picWidth = W; a.encAttr.picHeight = H;
        a.encAttr.enType = PT_H264; a.encAttr.profile = 1;
        a.rcAttr.outFrmRate.frmRateNum = fps; a.rcAttr.outFrmRate.frmRateDen = 1;
        a.rcAttr.maxGop = fps * 2;
        if (fixqp) {
            a.rcAttr.attrRcMode.rcMode = ENC_RC_MODE_FIXQP;
            a.rcAttr.attrRcMode.attrH264FixQp.qp = qp;
        } else {
            a.rcAttr.attrRcMode.rcMode = ENC_RC_MODE_CBR;
            a.rcAttr.attrRcMode.attrH264Cbr.maxQp = maxqp; a.rcAttr.attrRcMode.attrH264Cbr.minQp = minqp;
            a.rcAttr.attrRcMode.attrH264Cbr.outBitRate = kbps;
            a.rcAttr.attrRcMode.attrH264Cbr.frmQPStep = 3; a.rcAttr.attrRcMode.attrH264Cbr.gopQPStep = 15;
        }
        a.rcAttr.attrHSkip.hSkipAttr.skipType = IMP_Encoder_STYPE_N1X;
        a.rcAttr.attrHSkip.hSkipAttr.m = fps * 2 - 1; a.rcAttr.attrHSkip.hSkipAttr.n = 1;
        a.rcAttr.attrHSkip.maxHSkipType = IMP_Encoder_STYPE_N1X;
        CK(IMP_Encoder_CreateChn(chn, &a), "Enc_CreateChn");
    }
#endif
}

int main(int argc, char **argv)
{
    int W, H, fps, kbps, qp, minqp, maxqp, i, fixqp, chn = 0;
    const char *rc = getenv("ROITEST_RC"), *outdir;
    IMPSensorInfo sn;
    IMPFSChnAttr fa;
    IMPCell fs = { DEV_ID_FS, 0, 0 }, enc = { DEV_ID_ENC, 0, 0 };
    unsigned char *buf = malloc(2 << 20);

    if (argc < 7) { fprintf(stderr, "usage: %s SENSOR ADDR W H OUTDIR label:secs:spec...\n", argv[0]); return 2; }
    W = atoi(argv[3]); H = atoi(argv[4]); outdir = argv[5];
    if (!strcmp(outdir, "-")) {
        /* stream on stdout, everything else on stderr */
        stream_out = fdopen(dup(1), "wb");
        dup2(2, 1);
        setvbuf(stdout, NULL, _IOLBF, 0);
    }
    fps = getenv("ROITEST_FPS") ? atoi(getenv("ROITEST_FPS")) : 15;
    kbps = getenv("ROITEST_KBPS") ? atoi(getenv("ROITEST_KBPS")) : 2000;
    qp = getenv("ROITEST_QP") ? atoi(getenv("ROITEST_QP")) : 30;
    minqp = getenv("ROITEST_MINQP") ? atoi(getenv("ROITEST_MINQP")) : 15;
    maxqp = getenv("ROITEST_MAXQP") ? atoi(getenv("ROITEST_MAXQP")) : 45;
    fixqp = rc && !strcmp(rc, "fixqp");
    setvbuf(stdout, NULL, _IOLBF, 0);

    memset(&sn, 0, sizeof(sn));
    strncpy(sn.name, argv[1], sizeof(sn.name) - 1);
    sn.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C;
    strncpy(sn.i2c.type, argv[1], sizeof(sn.i2c.type) - 1);
    sn.i2c.addr = (int)strtol(argv[2], NULL, 0);
    sn.rst_gpio = sn.pwdn_gpio = sn.power_gpio = -1;
#ifdef SOC_T40
    {   /* ROITEST_SENSOR_EXT=bus,rst,vi,mclk,boot (Eufy sc830ai: 1,91,0,1,4) */
        int bus = 1, rst = 91, vi = 0, mclk = 1, boot = 4;
        if (getenv("ROITEST_SENSOR_EXT"))
            sscanf(getenv("ROITEST_SENSOR_EXT"), "%i,%i,%i,%i,%i", &bus, &rst, &vi, &mclk, &boot);
        sn.i2c.i2c_adapter_id = bus;
        sn.rst_gpio = rst;
        sn.video_interface = vi ? IMPISP_SENSOR_VI_MIPI_CSI1 : IMPISP_SENSOR_VI_MIPI_CSI0;
        sn.mclk = mclk == 2 ? IMPISP_SENSOR_MCLK2 : IMPISP_SENSOR_MCLK1;
        sn.default_boot = boot;
    }
#endif
#ifdef PLATFORM_T41
    CK(IMP_ISP_Open(), "ISP_Open");
    CK(IMP_ISP_AddSensor(IMPVI_MAIN, &sn), "AddSensor");
    CK(IMP_ISP_EnableSensor(IMPVI_MAIN, &sn), "EnableSensor");
#else
    CK(IMP_ISP_Open(), "ISP_Open");
    CK(IMP_ISP_AddSensor(&sn), "AddSensor");
    CK(IMP_ISP_EnableSensor(), "EnableSensor");
#endif
    CK(IMP_System_Init(), "System_Init");
    CK(IMP_ISP_EnableTuning(), "EnableTuning");
#ifdef PLATFORM_T41
    sensor_w = W; sensor_h = H;
    for (i = 0; i < 3; i++) { zcur.w[i] = W; zcur.h[i] = H; }
#endif

    memset(&fa, 0, sizeof(fa));
    fa.picWidth = W; fa.picHeight = H; fa.pixFmt = PIX_FMT_NV12;
    fa.outFrmRateNum = fps; fa.outFrmRateDen = 1; fa.nrVBs = getenv("ROITEST_NRVB") ? atoi(getenv("ROITEST_NRVB")) : 2; fa.type = FS_PHY_CHANNEL;
    if (getenv("ROITEST_OUT")) {   /* WxH: scale the channel (encoder size) */
        int ow = 0, oh = 0;
        sscanf(getenv("ROITEST_OUT"), "%dx%d", &ow, &oh);
        if (ow > 0 && oh > 0) {
            fa.scaler.enable = 1; fa.scaler.outwidth = ow; fa.scaler.outheight = oh;
#if defined(PLATFORM_T41) || defined(PLATFORM_T31)
            fa.picWidth = ow; fa.picHeight = oh;   /* T31/T40/T41: picture = output size */
#endif
            W = ow; H = oh;
        }
    }
#ifdef PLATFORM_T41
    if (getenv("ROITEST_I2D")) {
        int v[4] = {0, 0, 0, 0};
        sscanf(getenv("ROITEST_I2D"), "%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3]);
        fa.i2dattr.i2d_enable = 1; fa.i2dattr.flip_enable = v[0]; fa.i2dattr.mirr_enable = v[1];
        fa.i2dattr.rotate_enable = v[2]; fa.i2dattr.rotate_angle = v[3];
    }
#endif
    CK(IMP_FrameSource_CreateChn(0, &fa), "FS_CreateChn");
#ifdef PLATFORM_T41
    if (getenv("ROITEST_I2D")) {
        int r = IMP_FrameSource_SetI2dAttr(0, &fa.i2dattr);
        fprintf(stderr, "[I2D] SetI2dAttr flip %d mirror %d rotate %d/%d -> %d\n",
                fa.i2dattr.flip_enable, fa.i2dattr.mirr_enable, fa.i2dattr.rotate_enable,
                fa.i2dattr.rotate_angle, r);
        if (fa.i2dattr.rotate_enable == 1 && (fa.i2dattr.rotate_angle == 90 || fa.i2dattr.rotate_angle == 270)) {
            int t = W; W = H; H = t;   /* the encoder takes the rotated picture */
        }
    }
#endif
    if (getenv("ROITEST_CH1") && sscanf(getenv("ROITEST_CH1"), "%dx%d", &ch1_w, &ch1_h) == 2) {
        IMPFSChnAttr f1 = fa;
        f1.picWidth = ch1_w; f1.picHeight = ch1_h;
        f1.scaler.enable = 1; f1.scaler.outwidth = ch1_w; f1.scaler.outheight = ch1_h;
        f1.crop.enable = 0;
#ifdef PLATFORM_T41
        /* the I2D attribute is per channel: f1 = fa must not rotate ch1 too
         * (a 90/270 ch1 frame does not fit its landscape encoder) */
        memset(&f1.i2dattr, 0, sizeof(f1.i2dattr));
        if (getenv("ROITEST_CH1_I2D")) {
            int v[4] = {0, 0, 0, 0};
            sscanf(getenv("ROITEST_CH1_I2D"), "%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3]);
            f1.i2dattr.i2d_enable = 1; f1.i2dattr.flip_enable = v[0]; f1.i2dattr.mirr_enable = v[1];
            f1.i2dattr.rotate_enable = v[2]; f1.i2dattr.rotate_angle = v[3];
        }
#endif
        CK(IMP_FrameSource_CreateChn(1, &f1), "FS_CreateChn1");
#ifdef PLATFORM_T41
        if (f1.i2dattr.i2d_enable) {
            int r = IMP_FrameSource_SetI2dAttr(1, &f1.i2dattr);
            fprintf(stderr, "[I2D] ch1 SetI2dAttr rotate %d/%d -> %d\n",
                    f1.i2dattr.rotate_enable, f1.i2dattr.rotate_angle, r);
            if (f1.i2dattr.rotate_enable == 1 &&
                (f1.i2dattr.rotate_angle == 90 || f1.i2dattr.rotate_angle == 270)) {
                int t = ch1_w; ch1_w = ch1_h; ch1_h = t;
            }
        }
#endif
        ch1_on = 1;
    }
#ifdef PLATFORM_T41
    if (getenv("ROITEST_ZPRE")) {
        int zc, z[4];
        if (sscanf(getenv("ROITEST_ZPRE"), "%d,%d,%d,%d,%d", &zc, &z[0], &z[1], &z[2], &z[3]) == 5)
            do_zoom(zc, 1, z[0], z[1], z[2], z[3]);
        dump_counters("zpre");
    }
#endif
    if (getenv("ROITEST_FS_FIRST")) {   /* apitest order: FS enabled before the encoder exists */
        CK(IMP_FrameSource_EnableChn(0), "FS_Enable");
        sleep(2);
    }

    mk_enc(chn, W, H, fps, fixqp, qp, kbps, minqp, maxqp);
    CK(IMP_Encoder_CreateGroup(0), "CreateGroup");
    CK(IMP_Encoder_RegisterChn(0, chn), "RegisterChn");
#ifdef PLATFORM_T41
    if (getenv("ROITEST_OSD")) {
        IMPCell osd = { DEV_ID_OSD, 0, 0 };
        static uint32_t pic[128 * 64];
        IMPOSDRgnAttr ra;
        IMPOSDGrpRgnAttr ga;
        int ox = 16, oy = 16, px, py, h;
        sscanf(getenv("ROITEST_OSD"), "%d,%d", &ox, &oy);
        for (py = 0; py < 64; py++)
            for (px = 0; px < 128; px++) {
                uint32_t c = 0xff202020u;
                if (px < 3 || py < 3 || px >= 125 || py >= 61) c = 0xffffffffu;
                else if (px < 40 && py < 40) c = 0xffff0000u;
                else if (px >= 100 && py >= 40) c = 0xff00ff00u;
                pic[py * 128 + px] = c;
            }
        CK(IMP_OSD_CreateGroup(0), "OSD_CreateGroup");
        memset(&ra, 0, sizeof(ra));
        ra.type = OSD_REG_PIC;
        ra.rect.p0.x = ox; ra.rect.p0.y = oy;
        ra.rect.p1.x = ox + 127; ra.rect.p1.y = oy + 63;
        ra.fmt = PIX_FMT_BGRA;
        ra.data.picData.pData = pic;
        h = osd_h = IMP_OSD_CreateRgn(&ra);
        fprintf(stderr, "[OSD] CreateRgn -> %d\n", h);
        memset(&ga, 0, sizeof(ga));
        ga.show = 1; ga.gAlphaEn = 1; ga.fgAlhpa = 255; ga.layer = 1;
        fprintf(stderr, "[OSD] Register -> %d SetRgnAttr -> %d\n", IMP_OSD_RegisterRgn(h, 0, &ga),
                IMP_OSD_SetRgnAttr(h, &ra));
        fprintf(stderr, "[OSD] ShowRgn -> %d\n", IMP_OSD_ShowRgn(h, 0, 1));
        CK(IMP_System_Bind(&fs, &osd), "Bind fs-osd");
        CK(IMP_System_Bind(&osd, &enc), "Bind osd-enc");
        CK(IMP_OSD_Start(0), "OSD_Start");
    } else
#endif
    CK(IMP_System_Bind(&fs, &enc), "Bind");
    if (ch1_on) {
        IMPCell fs1 = { DEV_ID_FS, 1, 0 }, enc1 = { DEV_ID_ENC, 1, 0 };
        /* ROITEST_CH1_QP=n: encoder 1 is a FixQP reference of the same
         * pictures (picture checks of a rate-control mode on ch0) */
        if (getenv("ROITEST_CH1_QP"))
            mk_enc(1, ch1_w, ch1_h, fps, 1, atoi(getenv("ROITEST_CH1_QP")), kbps, minqp, maxqp);
        else
            mk_enc(1, ch1_w, ch1_h, fps, fixqp, qp, kbps, minqp, maxqp);
        CK(IMP_Encoder_CreateGroup(1), "CreateGroup1");
        CK(IMP_Encoder_RegisterChn(1, 1), "RegisterChn1");
#ifdef ENC_NEW
        /* ROITEST_CH1_JPEG=path (a FIFO read by the host): one snapshot JPEG
         * of output 1 from a JPEG encoder (chn 4, same group), taken ~5 s
         * into the first phase, like timps' snapshot channel */
        if (getenv("ROITEST_CH1_JPEG")) {
            IMPEncoderChnAttr ja;
            memset(&ja, 0, sizeof(ja));
            CK(IMP_Encoder_SetDefaultParam(&ja, IMP_ENC_PROFILE_JPEG, IMP_ENC_RC_MODE_FIXQP,
                                           ch1_w, ch1_h, fps, 1, 0, 0, 75, 0), "SetDefaultParam jpeg");
            CK(IMP_Encoder_CreateChn(4, &ja), "Enc_CreateChn jpeg");
            CK(IMP_Encoder_RegisterChn(1, 4), "RegisterChn jpeg");
            jpeg_on = 1;
        }
#endif
#ifdef PLATFORM_T41
        if (getenv("ROITEST_OSD")) {   /* the same picture on ch1 (JPEG path), group 1 */
            IMPCell osd1 = { DEV_ID_OSD, 1, 0 };
            IMPOSDGrpRgnAttr ga1;
            memset(&ga1, 0, sizeof(ga1));
            ga1.show = 1; ga1.gAlphaEn = 1; ga1.fgAlhpa = 255; ga1.layer = 1;
            CK(IMP_OSD_CreateGroup(1), "OSD_CreateGroup1");
            fprintf(stderr, "[OSD] Register1 -> %d\n", IMP_OSD_RegisterRgn(osd_h, 1, &ga1));
            CK(IMP_System_Bind(&fs1, &osd1), "Bind fs1-osd1");
            CK(IMP_System_Bind(&osd1, &enc1), "Bind osd1-enc1");
            CK(IMP_OSD_Start(1), "OSD_Start1");
        } else
#endif
        CK(IMP_System_Bind(&fs1, &enc1), "Bind1");
    }
    if (!getenv("ROITEST_FS_FIRST")) CK(IMP_FrameSource_EnableChn(0), "FS_Enable");
    CK(IMP_Encoder_StartRecvPic(chn), "StartRecvPic");
    if (ch1_on) {
        CK(IMP_FrameSource_EnableChn(1), "FS_Enable1");
        CK(IMP_Encoder_StartRecvPic(1), "StartRecvPic1");
    }
    dump_counters("start");
    printf("[T] roitest %dx%d %s %s\n", W, H, fixqp ? "FixQP" : "CBR", fixqp ? "" : "");
    sleep(3);

    for (i = 6; i < argc; i++) {
        char *lab = strdup(argv[i]), *secs, *spec, *ex;
        Reg reg[10];
        int n, idr_req = 1, frames = 0, idrs = 0, f1n = 0, i1 = 0, zn = 0, zms = 0, zdone = 0, gap = 0;
        long bytes = 0, idr_b = 0, p_b = 0, b1 = 0, ib1 = 0, pb1 = 0, len;
        int64_t t_end, t0, tz = 0, tlast;
        char path[512];
        FILE *f, *f1 = NULL;

        secs = strchr(lab, ':'); *secs++ = 0;
        spec = strchr(secs, ':'); *spec++ = 0;
        ex = strchr(spec, ';');
        if (ex) *ex++ = 0;
        if (lab[0] == '~') { idr_req = 0; lab++; }
        n = parse_spec(spec, reg);
        apply_roi(chn, reg, n);
        if (ex) {
            char *c = strstr(ex, "zc");
            apply_extra(ex);
            if (c) sscanf(c, "zc%*d=%d,%d", &zn, &zms);
        }
        if (idr_req) IMP_Encoder_RequestIDR(chn);
        snprintf(path, sizeof(path), "%s/%s.h264", outdir, lab);
        f = stream_out ? stream_out : fopen(path, "wb");
        if (ch1_on) {
            /* ROITEST_CH1_OUT=path (e.g. a FIFO read by the host): one
             * stream for all phases, like OUTDIR "-" for ch0 */
            static FILE *ch1_out;
            if (getenv("ROITEST_CH1_OUT")) {
                if (!ch1_out) ch1_out = fopen(getenv("ROITEST_CH1_OUT"), "wb");
                f1 = ch1_out;
            } else {
                snprintf(path, sizeof(path), "%s/%s_ch1.h264", outdir, lab);
                f1 = fopen(path, "wb");
            }
            if (idr_req) IMP_Encoder_RequestIDR(1);
        }
        t0 = tlast = now_ms();
        t_end = t0 + 1000 * atoi(secs);
        while (now_ms() < t_end) {
#ifdef PLATFORM_T41
            if (zn && zdone < zn && now_ms() >= tz) {
                /* zc<ch>=N,ms: full -> centre half -> bottom-right half */
                static const int fr[3][4] = { { 0, 0, 4, 4 }, { 1, 1, 2, 2 }, { 2, 2, 2, 2 } };
                int zc = 1, k = zdone % 3;
                char *c = strstr(ex, "zc");
                sscanf(c, "zc%d", &zc);
                do_zoom(zc, 1, sensor_w * fr[k][0] / 4 & ~1, sensor_h * fr[k][1] / 4 & ~1,
                        sensor_w * fr[k][2] / 4 & ~1, sensor_h * fr[k][3] / 4 & ~1);
                zdone++;
                tz = now_ms() + zms;
            }
#endif
#ifdef ENC_NEW
            if (jpeg_on == 1 && now_ms() >= t0 + 5000) {
                /* own thread: the ch1 video poll must go on at 100 ms, or the
                 * JPEG channel stops being fed from the video channel (fan-out
                 * idle) and reads the framesource itself - timps' order */
                pthread_t jt;
                fprintf(stderr, "[R] jpeg StartRecvPic -> %d\n", IMP_Encoder_StartRecvPic(4));
                jpeg_on = 2;
                pthread_create(&jt, NULL, jpeg_thread, NULL);
                pthread_detach(jt);
            }
#endif
            len = drain(chn, buf, f, &frames, &idrs, &idr_b, &p_b);
            if (len >= 0) bytes += len;
            if (ch1_on) {
                long l1 = drain(1, buf, f1, &f1n, &i1, &ib1, &pb1);
                if (l1 >= 0) {
                    int64_t t = now_ms();
                    b1 += l1;
                    if (t - tlast > gap) gap = (int)(t - tlast);
                    tlast = t;
                }
            }
        }
        if (f && f != stream_out) fclose(f);
        else if (f) fflush(f);
        if (f1 && !getenv("ROITEST_CH1_OUT")) fclose(f1);
        else if (f1) fflush(f1);
        printf("[R] %s frames=%d bytes=%ld kbps=%.0f idrs=%d idr_avg=%ld p_avg=%ld\n", lab, frames, bytes,
               bytes * 8.0 / (double)(now_ms() - t0), idrs, idrs ? idr_b / idrs : 0,
               frames > idrs ? p_b / (frames - idrs) : 0);
        if (ch1_on)
            printf("[R] %s_ch1 frames=%d bytes=%ld idrs=%d max_gap_ms=%d zoom_calls=%d\n", lab, f1n, b1, i1, gap, zdone);
        dump_counters(lab);
    }
    apply_roi(chn, NULL, 0);
    if (ch1_on) {
        IMPCell fs1 = { DEV_ID_FS, 1, 0 }, enc1 = { DEV_ID_ENC, 1, 0 };
        IMP_Encoder_StopRecvPic(1);
        IMP_FrameSource_DisableChn(1);
        IMP_System_UnBind(&fs1, &enc1);
        IMP_Encoder_UnRegisterChn(1);
        IMP_Encoder_DestroyChn(1);
        IMP_Encoder_DestroyGroup(1);
        IMP_FrameSource_DestroyChn(1);
    }
    IMP_Encoder_StopRecvPic(chn);
    IMP_FrameSource_DisableChn(0);
    IMP_System_UnBind(&fs, &enc);
    IMP_Encoder_UnRegisterChn(chn);
    IMP_Encoder_DestroyChn(chn);
    IMP_Encoder_DestroyGroup(0);
    IMP_FrameSource_DestroyChn(0);
    IMP_System_Exit();
    IMP_ISP_DisableTuning();
#ifdef PLATFORM_T41
    IMP_ISP_DisableSensor(IMPVI_MAIN);
    IMP_ISP_DelSensor(IMPVI_MAIN, &sn);
#else
    IMP_ISP_DisableSensor();
    IMP_ISP_DelSensor(&sn);
#endif
    IMP_ISP_Close();
    return 0;
}
