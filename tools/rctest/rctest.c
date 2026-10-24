/* rctest - T41 rate-control parameter API test (return codes, readback, effect).
 *
 *   rctest SENSOR I2C_ADDR W H OUTDIR OP...
 *
 * W,H = sensor size; RCTEST_OUT=WxH scales the frame source (encoder size).
 * The first OP must be c:... (create).  Every OP prints one or more lines
 *   [OP] <text> -> ret=0x........      and, for get-ops, the read-back.
 *
 * OPs
 *   c:key=val,...     SetDefaultParam + overrides + CreateChn + start
 *                     keys: mode fix|cbr|vbr|cvbr|cq, kbps, max (uMaxBitRate kbps),
 *                     minqp maxqp iqp ipd pbd opt maxpic psnr gop fps, h265=1
 *   get               read-back: RcMode, GopAttr, FrmRate
 *   set:key=val,...   GetChnAttrRcMode + overrides + SetChnAttrRcMode
 *   br:T,M            IMP_Encoder_SetChnBitRate (bit/s)
 *   qpb:MIN,MAX       SetChnQpBounds
 *   qpbf:a,b,c,d      SetChnQpBoundsPerFrame (minI,maxI,minP,maxP)
 *   maxpic:I,P        SetChnMaxPictureSize (kbit)
 *   gop:N             SetChnGopLength
 *   gopattr:key=val   GetChnGopAttr + overrides (mode len same lt freq ltrc) + SetChnGopAttr
 *   fps:N,D           SetChnFrmRate
 *   ipd:N             IMP_Encoder_SetChnQpIPDelta (OpenIMP extra, weak)
 *   idr               RequestIDR
 *   run:LABEL:SECS    capture LABEL.h264 + LABEL.csv (t_ms,bytes,idr), print stats
 *   sleep:S
 *   edge              argument/channel edge cases of all RC calls (return codes only)
 * Stop the streamer first.  Output files go to OUTDIR (camera /tmp is RAM).
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <imp/imp_common.h>
#include <imp/imp_system.h>
#include <imp/imp_isp.h>
#include <imp/imp_framesource.h>
#include <imp/imp_encoder.h>

__attribute__((weak)) int IMP_Log_Get_Option(void) { return 0; }
__attribute__((weak)) int imp_log_fun(int level, int option, int type, const char *tag, const char *file,
                                      int line, const char *func, const char *fmt, ...)
{
    va_list ap;
    (void)level; (void)option; (void)type; (void)tag; (void)file; (void)line; (void)func;
    if (!fmt || !getenv("RCTEST_LOG")) return 0;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    return 0;
}
int IMP_Encoder_SetChnQpIPDelta(int encChn, int delta) __attribute__((weak));
int IMP_Encoder_SetChnAttrRcMode(int encChn, IMPEncoderAttrRcMode *mode);

#define CK(e, m) do { int r_ = (e); if (r_ < 0) { printf("[E] %s -> 0x%08x\n", m, (unsigned)r_); exit(1); } } while (0)

static int chn = 0;
static char outdir[256];
static unsigned char *buf;

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

static const char *rcname(int m)
{
    switch (m) {
    case 0: return "FIXQP"; case 1: return "CBR"; case 2: return "VBR";
    case 4: return "CAPPED_VBR"; case 8: return "CAPPED_QUALITY"; default: return "?";
    }
}

static void print_rc(const char *tag, const IMPEncoderAttrRcMode *m)
{
    switch (m->rcMode) {
    case IMP_ENC_RC_MODE_FIXQP:
        printf("[G] %s mode=FIXQP iqp=%d min=%d max=%d\n", tag, m->attrFixQp.iInitialQP,
               m->attrFixQp.iMinQP, m->attrFixQp.iMaxQP);
        break;
    case IMP_ENC_RC_MODE_CBR:
        printf("[G] %s mode=CBR kbps=%u iqp=%d min=%d max=%d ipd=%d pbd=%d opt=0x%x maxpic=%u\n", tag,
               m->attrCbr.uTargetBitRate, m->attrCbr.iInitialQP, m->attrCbr.iMinQP, m->attrCbr.iMaxQP,
               m->attrCbr.iIPDelta, m->attrCbr.iPBDelta, (unsigned)m->attrCbr.eRcOptions,
               m->attrCbr.uMaxPictureSize);
        break;
    case IMP_ENC_RC_MODE_VBR:
        printf("[G] %s mode=VBR kbps=%u max=%u iqp=%d min=%d max=%d ipd=%d pbd=%d opt=0x%x maxpic=%u\n", tag,
               m->attrVbr.uTargetBitRate, m->attrVbr.uMaxBitRate, m->attrVbr.iInitialQP,
               m->attrVbr.iMinQP, m->attrVbr.iMaxQP, m->attrVbr.iIPDelta, m->attrVbr.iPBDelta,
               (unsigned)m->attrVbr.eRcOptions, m->attrVbr.uMaxPictureSize);
        break;
    default:
        printf("[G] %s mode=%s(%d) kbps=%u max=%u iqp=%d min=%d max=%d ipd=%d pbd=%d opt=0x%x maxpic=%u psnr=%u\n",
               tag, rcname(m->rcMode), (int)m->rcMode, m->attrCappedVbr.uTargetBitRate,
               m->attrCappedVbr.uMaxBitRate, m->attrCappedVbr.iInitialQP, m->attrCappedVbr.iMinQP,
               m->attrCappedVbr.iMaxQP, m->attrCappedVbr.iIPDelta, m->attrCappedVbr.iPBDelta,
               (unsigned)m->attrCappedVbr.eRcOptions, m->attrCappedVbr.uMaxPictureSize,
               m->attrCappedVbr.uMaxPSNR);
        break;
    }
}

static void do_get(const char *tag)
{
    IMPEncoderAttrRcMode m;
    IMPEncoderGopAttr g;
    IMPEncoderFrmRate f;
    int r;

    memset(&m, 0xa5, sizeof(m));
    r = IMP_Encoder_GetChnAttrRcMode(chn, &m);
    printf("[OP] get %s GetChnAttrRcMode -> ret=0x%08x\n", tag, (unsigned)r);
    if (r == 0) print_rc(tag, &m);
    memset(&g, 0xa5, sizeof(g));
    r = IMP_Encoder_GetChnGopAttr(chn, &g);
    printf("[OP] get %s GetChnGopAttr -> ret=0x%08x\n", tag, (unsigned)r);
    if (r == 0)
        printf("[G] %s gop mode=0x%x len=%u notify=%u same=%u lt=%d freqlt=%u ltrc=%d\n", tag,
               (unsigned)g.uGopCtrlMode, g.uGopLength, g.uNotifyUserLTInter, g.uMaxSameSenceCnt,
               g.bEnableLT, g.uFreqLT, g.bLTRC);
    memset(&f, 0xa5, sizeof(f));
    r = IMP_Encoder_GetChnFrmRate(chn, &f);
    printf("[OP] get %s GetChnFrmRate -> ret=0x%08x\n", tag, (unsigned)r);
    if (r == 0) printf("[G] %s fps=%u/%u\n", tag, f.frmRateNum, f.frmRateDen);
}

/* key=val list -> long (returns 1 if key present) */
static int kv(const char *list, const char *key, long *out)
{
    char tmp[512], *tok, *sv = NULL;
    snprintf(tmp, sizeof(tmp), "%s", list);
    for (tok = strtok_r(tmp, ",", &sv); tok; tok = strtok_r(NULL, ",", &sv)) {
        char *eq = strchr(tok, '=');
        if (!eq) continue;
        *eq = 0;
        if (!strcmp(tok, key)) {
            if (!strcmp(eq + 1, "fix")) *out = 0;
            else if (!strcmp(eq + 1, "cbr")) *out = 1;
            else if (!strcmp(eq + 1, "vbr")) *out = 2;
            else if (!strcmp(eq + 1, "cvbr")) *out = 4;
            else if (!strcmp(eq + 1, "cq")) *out = 8;
            else *out = strtol(eq + 1, NULL, 0);
            return 1;
        }
    }
    return 0;
}

/* apply overrides to the rc mode union (common prefix layout of cbr/vbr/capped) */
static void apply_rc(IMPEncoderAttrRcMode *m, const char *l)
{
    long v;
    switch (m->rcMode) {
    case IMP_ENC_RC_MODE_FIXQP:
        if (kv(l, "iqp", &v)) m->attrFixQp.iInitialQP = v;
        if (kv(l, "minqp", &v)) m->attrFixQp.iMinQP = v;
        if (kv(l, "maxqp", &v)) m->attrFixQp.iMaxQP = v;
        break;
    case IMP_ENC_RC_MODE_CBR:
        if (kv(l, "kbps", &v)) m->attrCbr.uTargetBitRate = v;
        if (kv(l, "iqp", &v)) m->attrCbr.iInitialQP = v;
        if (kv(l, "minqp", &v)) m->attrCbr.iMinQP = v;
        if (kv(l, "maxqp", &v)) m->attrCbr.iMaxQP = v;
        if (kv(l, "ipd", &v)) m->attrCbr.iIPDelta = v;
        if (kv(l, "pbd", &v)) m->attrCbr.iPBDelta = v;
        if (kv(l, "opt", &v)) m->attrCbr.eRcOptions = v;
        if (kv(l, "maxpic", &v)) m->attrCbr.uMaxPictureSize = v;
        break;
    case IMP_ENC_RC_MODE_VBR:
        if (kv(l, "kbps", &v)) m->attrVbr.uTargetBitRate = v;
        if (kv(l, "max", &v)) m->attrVbr.uMaxBitRate = v;
        if (kv(l, "iqp", &v)) m->attrVbr.iInitialQP = v;
        if (kv(l, "minqp", &v)) m->attrVbr.iMinQP = v;
        if (kv(l, "maxqp", &v)) m->attrVbr.iMaxQP = v;
        if (kv(l, "ipd", &v)) m->attrVbr.iIPDelta = v;
        if (kv(l, "pbd", &v)) m->attrVbr.iPBDelta = v;
        if (kv(l, "opt", &v)) m->attrVbr.eRcOptions = v;
        if (kv(l, "maxpic", &v)) m->attrVbr.uMaxPictureSize = v;
        break;
    default:
        if (kv(l, "kbps", &v)) m->attrCappedVbr.uTargetBitRate = v;
        if (kv(l, "max", &v)) m->attrCappedVbr.uMaxBitRate = v;
        if (kv(l, "iqp", &v)) m->attrCappedVbr.iInitialQP = v;
        if (kv(l, "minqp", &v)) m->attrCappedVbr.iMinQP = v;
        if (kv(l, "maxqp", &v)) m->attrCappedVbr.iMaxQP = v;
        if (kv(l, "ipd", &v)) m->attrCappedVbr.iIPDelta = v;
        if (kv(l, "pbd", &v)) m->attrCappedVbr.iPBDelta = v;
        if (kv(l, "opt", &v)) m->attrCappedVbr.eRcOptions = v;
        if (kv(l, "maxpic", &v)) m->attrCappedVbr.uMaxPictureSize = v;
        if (kv(l, "psnr", &v)) m->attrCappedVbr.uMaxPSNR = v;
        break;
    }
}

static int W, H, fps0 = 15;
static IMPCell fs_cell = { DEV_ID_FS, 0, 0 }, enc_cell = { DEV_ID_ENC, 0, 0 };

static void op_create(const char *l)
{
    IMPEncoderChnAttr a;
    long v, mode = 1, kbps = 2000, gop = 0, fps = fps0, iqp = -1;
    int r;

    kv(l, "mode", &mode);
    kv(l, "kbps", &kbps);
    kv(l, "fps", &fps);
    gop = fps * 2;
    kv(l, "gop", &gop);
    kv(l, "iqp", &iqp);
    memset(&a, 0, sizeof(a));
    r = IMP_Encoder_SetDefaultParam(&a, kv(l, "h265", &v) && v ? IMP_ENC_PROFILE_HEVC_MAIN : IMP_ENC_PROFILE_AVC_HIGH,
                                    (IMPEncoderRcMode)mode, W, H, fps, 1, gop, 2, (int)iqp,
                                    mode == 0 ? 0 : (uint32_t)kbps);
    printf("[OP] SetDefaultParam mode=%s kbps=%ld fps=%ld gop=%ld iqp=%ld -> ret=0x%08x\n", rcname(mode), kbps,
           fps, gop, iqp, (unsigned)r);
    print_rc("default", &a.rcAttr.attrRcMode);
    apply_rc(&a.rcAttr.attrRcMode, l);
    print_rc("create", &a.rcAttr.attrRcMode);
    r = IMP_Encoder_CreateChn(chn, &a);
    printf("[OP] CreateChn -> ret=0x%08x\n", (unsigned)r);
    if (r) exit(1);
    CK(IMP_Encoder_CreateGroup(0), "CreateGroup");
    CK(IMP_Encoder_RegisterChn(0, chn), "RegisterChn");
    CK(IMP_System_Bind(&fs_cell, &enc_cell), "Bind");
    CK(IMP_FrameSource_EnableChn(0), "FS_Enable");
    CK(IMP_Encoder_StartRecvPic(chn), "StartRecvPic");
    sleep(2);
    do_get("after-create");
    {
        IMPEncoderChnAttr b;
        memset(&b, 0, sizeof(b));
        r = IMP_Encoder_GetChnAttr(chn, &b);
        printf("[OP] GetChnAttr -> ret=0x%08x\n", (unsigned)r);
        if (r == 0) {
            print_rc("chnattr", &b.rcAttr.attrRcMode);
            printf("[G] chnattr fps=%u/%u gop mode=0x%x len=%u\n", b.rcAttr.outFrmRate.frmRateNum,
                   b.rcAttr.outFrmRate.frmRateDen, (unsigned)b.gopAttr.uGopCtrlMode, b.gopAttr.uGopLength);
        }
    }
}

static long drain(FILE *f, FILE *csv, int64_t t0, int *idrs, long *idr_b, long *p_b, int *frames)
{
    IMPEncoderStream s;
    size_t len = 0;
    uint32_t k;

    if (IMP_Encoder_PollingStream(chn, 1000) != 0) return -1;
    memset(&s, 0, sizeof(s));
    if (IMP_Encoder_GetStream(chn, &s, true) != 0) return -1;
    for (k = 0; k < s.packCount; k++) {
        const IMPEncoderPack *p = &s.pack[k];
        const unsigned char *base = (const unsigned char *)(uintptr_t)s.virAddr;
        size_t rem = s.streamSize > p->offset ? s.streamSize - p->offset : 0;
        if (!p->length || len + p->length > (2u << 20)) continue;
        if (rem && rem < p->length) {
            memcpy(buf + len, base + p->offset, rem);
            memcpy(buf + len + rem, base, p->length - rem);
        } else
            memcpy(buf + len, base + p->offset, p->length);
        len += p->length;
    }
    IMP_Encoder_ReleaseStream(chn, &s);
    if (f) fwrite(buf, 1, len, f);
    {
        int idr = nal_has_idr(buf, len);
        if (csv) fprintf(csv, "%lld,%zu,%d\n", (long long)(now_ms() - t0), len, idr);
        if (idr) { (*idrs)++; *idr_b += len; } else *p_b += len;
    }
    (*frames)++;
    return (long)len;
}

static void op_run(const char *label, int secs)
{
    char path[400];
    FILE *f, *csv;
    int frames = 0, idrs = 0;
    long bytes = 0, idr_b = 0, p_b = 0, l;
    int64_t t0 = now_ms(), end = t0 + 1000L * secs;

    snprintf(path, sizeof(path), "%s/%s.h264", outdir, label);
    f = fopen(path, "wb");
    snprintf(path, sizeof(path), "%s/%s.csv", outdir, label);
    csv = fopen(path, "w");
    if (csv) fprintf(csv, "t_ms,bytes,idr\n");
    while (now_ms() < end) {
        l = drain(f, csv, t0, &idrs, &idr_b, &p_b, &frames);
        if (l >= 0) bytes += l;
    }
    if (f) fclose(f);
    if (csv) fclose(csv);
    printf("[R] %s frames=%d bytes=%ld kbps=%.0f idrs=%d idr_avg=%ld p_avg=%ld\n", label, frames, bytes,
           bytes * 8.0 / (double)(now_ms() - t0), idrs, idrs ? idr_b / idrs : 0,
           frames > idrs ? p_b / (frames - idrs) : 0);
}

static void defaults(void)
{
    static const int modes[] = { 0, 1, 2, 4, 8 };
    static const struct { IMPEncoderProfile p; const char *n; } prof[] = {
        { IMP_ENC_PROFILE_AVC_HIGH, "AVC_HIGH" }, { IMP_ENC_PROFILE_AVC_MAIN, "AVC_MAIN" },
        { IMP_ENC_PROFILE_HEVC_MAIN, "HEVC" }, { IMP_ENC_PROFILE_JPEG, "JPEG" } };
    size_t i, j;
    int iqps[] = { -1, 30 };

    for (j = 0; j < sizeof(prof) / sizeof(prof[0]); j++)
        for (i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
            int k;
            for (k = 0; k < 2; k++) {
                IMPEncoderChnAttr a;
                char tag[96];
                int r;
                memset(&a, 0xa5, sizeof(a));
                r = IMP_Encoder_SetDefaultParam(&a, prof[j].p, (IMPEncoderRcMode)modes[i], 1280, 720, 15, 1, 30, 2,
                                                iqps[k], modes[i] == 0 ? 0 : 2000);
                snprintf(tag, sizeof(tag), "dflt %s %s iqp=%d", prof[j].n, rcname(modes[i]), iqps[k]);
                printf("[OP] SetDefaultParam %s -> ret=0x%08x\n", tag, (unsigned)r);
                if (r) continue;
                print_rc(tag, &a.rcAttr.attrRcMode);
                printf("[G] %s fps=%u/%u gop mode=0x%x len=%u same=%u lt=%d,%u,%d notify=%u ivdc=%d prof=0x%x pic=%ux%u fmt=%u opts=0x%x tools=0x%x buf=%u\n",
                       tag, a.rcAttr.outFrmRate.frmRateNum, a.rcAttr.outFrmRate.frmRateDen,
                       (unsigned)a.gopAttr.uGopCtrlMode, a.gopAttr.uGopLength, a.gopAttr.uMaxSameSenceCnt,
                       a.gopAttr.bEnableLT, a.gopAttr.uFreqLT, a.gopAttr.bLTRC, a.gopAttr.uNotifyUserLTInter,
                       a.bEnableIvdc, (unsigned)a.encAttr.eProfile, a.encAttr.uWidth, a.encAttr.uHeight,
                       (unsigned)a.encAttr.ePicFormat, a.encAttr.eEncOptions, a.encAttr.eEncTools, a.encAttr.bufSize);
            }
        }
}

static void edge(void)
{
    static const int bad[] = { 8, 100, 5 };
    IMPEncoderAttrRcMode m;
    IMPEncoderGopAttr g;
    IMPEncoderFrmRate fr = { 15, 1 };
    size_t i;
    int r;

    memset(&m, 0, sizeof(m));
    memset(&g, 0, sizeof(g));
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        int c = bad[i];
        fr.frmRateNum = 15; fr.frmRateDen = 1;
        printf("[E] chn=%d GetChnAttrRcMode 0x%08x SetChnAttrRcMode 0x%08x SetChnBitRate 0x%08x QpBounds 0x%08x "
               "QpBoundsPerFrame 0x%08x MaxPicSize 0x%08x GopLength 0x%08x GetGop 0x%08x SetGop 0x%08x "
               "GetFrmRate 0x%08x SetFrmRate 0x%08x\n", c,
               (unsigned)IMP_Encoder_GetChnAttrRcMode(c, &m), (unsigned)IMP_Encoder_SetChnAttrRcMode(c, &m),
               (unsigned)IMP_Encoder_SetChnBitRate(c, 1000000, 1000000), (unsigned)IMP_Encoder_SetChnQpBounds(c, 10, 40),
               (unsigned)IMP_Encoder_SetChnQpBoundsPerFrame(c, 10, 40, 10, 40),
               (unsigned)IMP_Encoder_SetChnMaxPictureSize(c, 1000, 1000), (unsigned)IMP_Encoder_SetChnGopLength(c, 30),
               (unsigned)IMP_Encoder_GetChnGopAttr(c, &g), (unsigned)IMP_Encoder_SetChnGopAttr(c, &g),
               (unsigned)IMP_Encoder_GetChnFrmRate(c, &fr), (unsigned)IMP_Encoder_SetChnFrmRate(c, &fr));
    }
    /* created channel, odd values */
    IMP_Encoder_GetChnAttrRcMode(chn, &m);
    printf("[E] chn=0 BitRate(0,0)=0x%08x BitRate(-5,-5)=0x%08x BitRate(500000,100000)=0x%08x\n",
           (unsigned)IMP_Encoder_SetChnBitRate(chn, 0, 0), (unsigned)IMP_Encoder_SetChnBitRate(chn, -5, -5),
           (unsigned)IMP_Encoder_SetChnBitRate(chn, 500000, 100000));
    printf("[E] chn=0 QpBounds(0,0)=0x%08x (60,70)=0x%08x (40,10)=0x%08x (-5,99)=0x%08x (1,51)=0x%08x\n",
           (unsigned)IMP_Encoder_SetChnQpBounds(chn, 0, 0), (unsigned)IMP_Encoder_SetChnQpBounds(chn, 60, 70),
           (unsigned)IMP_Encoder_SetChnQpBounds(chn, 40, 10), (unsigned)IMP_Encoder_SetChnQpBounds(chn, -5, 99),
           (unsigned)IMP_Encoder_SetChnQpBounds(chn, 1, 51));
    printf("[E] chn=0 QpBoundsPerFrame(0,0,0,0)=0x%08x (40,10,40,10)=0x%08x (70,80,70,80)=0x%08x\n",
           (unsigned)IMP_Encoder_SetChnQpBoundsPerFrame(chn, 0, 0, 0, 0),
           (unsigned)IMP_Encoder_SetChnQpBoundsPerFrame(chn, 40, 10, 40, 10),
           (unsigned)IMP_Encoder_SetChnQpBoundsPerFrame(chn, 70, 80, 70, 80));
    printf("[E] chn=0 MaxPicSize(0,0)=0x%08x (1,1)=0x%08x\n",
           (unsigned)IMP_Encoder_SetChnMaxPictureSize(chn, 0, 0), (unsigned)IMP_Encoder_SetChnMaxPictureSize(chn, 1, 1));
    printf("[E] chn=0 GopLength(0)=0x%08x (-1)=0x%08x (1)=0x%08x (65535)=0x%08x (100000)=0x%08x\n",
           (unsigned)IMP_Encoder_SetChnGopLength(chn, 0), (unsigned)IMP_Encoder_SetChnGopLength(chn, -1),
           (unsigned)IMP_Encoder_SetChnGopLength(chn, 1), (unsigned)IMP_Encoder_SetChnGopLength(chn, 65535),
           (unsigned)IMP_Encoder_SetChnGopLength(chn, 100000));
    fr.frmRateNum = 0; fr.frmRateDen = 1;
    r = IMP_Encoder_SetChnFrmRate(chn, &fr);
    printf("[E] chn=0 FrmRate(0/1)=0x%08x", (unsigned)r);
    fr.frmRateNum = 15; fr.frmRateDen = 0;
    printf(" (15/0)=0x%08x", (unsigned)IMP_Encoder_SetChnFrmRate(chn, &fr));
    fr.frmRateNum = 1000; fr.frmRateDen = 1;
    printf(" (1000/1)=0x%08x", (unsigned)IMP_Encoder_SetChnFrmRate(chn, &fr));
    fr.frmRateNum = 30; fr.frmRateDen = 2;
    printf(" (30/2)=0x%08x\n", (unsigned)IMP_Encoder_SetChnFrmRate(chn, &fr));
    /* restore sane state */
    IMP_Encoder_SetChnQpBounds(chn, 15, 45);
    fr.frmRateNum = fps0; fr.frmRateDen = 1;
    IMP_Encoder_SetChnFrmRate(chn, &fr);
    IMP_Encoder_SetChnGopLength(chn, fps0 * 2);
    IMP_Encoder_SetChnMaxPictureSize(chn, 4000, 4000);
}

int main(int argc, char **argv)
{
    int i;
    IMPSensorInfo sn;
    IMPFSChnAttr fa;

    if (argc < 7) { fprintf(stderr, "usage: %s SENSOR ADDR W H OUTDIR op...\n", argv[0]); return 2; }
    W = atoi(argv[3]); H = atoi(argv[4]);
    snprintf(outdir, sizeof(outdir), "%s", argv[5]);
    buf = malloc(2 << 20);
    setvbuf(stdout, NULL, _IOLBF, 0);
    memset(&sn, 0, sizeof(sn));
    strncpy(sn.name, argv[1], sizeof(sn.name) - 1);
    sn.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C;
    strncpy(sn.i2c.type, argv[1], sizeof(sn.i2c.type) - 1);
    sn.i2c.addr = (int)strtol(argv[2], NULL, 0);
    sn.rst_gpio = sn.pwdn_gpio = sn.power_gpio = -1;
    CK(IMP_ISP_Open(), "ISP_Open");
    CK(IMP_ISP_AddSensor(IMPVI_MAIN, &sn), "AddSensor");
    CK(IMP_ISP_EnableSensor(IMPVI_MAIN, &sn), "EnableSensor");
    CK(IMP_System_Init(), "System_Init");
    CK(IMP_ISP_EnableTuning(), "EnableTuning");
    if (getenv("RCTEST_FPS")) fps0 = atoi(getenv("RCTEST_FPS"));
    memset(&fa, 0, sizeof(fa));
    fa.picWidth = W; fa.picHeight = H; fa.pixFmt = PIX_FMT_NV12;
    fa.outFrmRateNum = fps0; fa.outFrmRateDen = 1; fa.nrVBs = 2; fa.type = FS_PHY_CHANNEL;
    if (getenv("RCTEST_OUT")) {
        int ow = 0, oh = 0;
        sscanf(getenv("RCTEST_OUT"), "%dx%d", &ow, &oh);
        if (ow > 0 && oh > 0) {
            fa.scaler.enable = 1; fa.scaler.outwidth = ow; fa.scaler.outheight = oh;
            fa.picWidth = ow; fa.picHeight = oh;
            W = ow; H = oh;
        }
    }
    CK(IMP_FrameSource_CreateChn(0, &fa), "FS_CreateChn");
    printf("[T] rctest %dx%d fps %d\n", W, H, fps0);

    for (i = 6; i < argc; i++) {
        const char *op = argv[i];
        int a, b, c, d;
        long v;
        char *p;
        int r;

        if (!strncmp(op, "c:", 2)) op_create(op + 2);
        else if (!strcmp(op, "get")) do_get("get");
        else if (!strncmp(op, "set:", 4)) {
            IMPEncoderAttrRcMode m;
            memset(&m, 0, sizeof(m));
            r = IMP_Encoder_GetChnAttrRcMode(chn, &m);
            if (kv(op + 4, "mode", &v) && (IMPEncoderRcMode)v != m.rcMode) {
                /* mode change: start from clean defaults of the new mode */
                memset(&m, 0, sizeof(m));
                m.rcMode = (IMPEncoderRcMode)v;
                m.attrCappedVbr.iInitialQP = -1; m.attrCappedVbr.iMinQP = 15; m.attrCappedVbr.iMaxQP = 45;
                m.attrCappedVbr.iIPDelta = -1; m.attrCappedVbr.iPBDelta = -1; m.attrCappedVbr.eRcOptions = 1;
                if (v == IMP_ENC_RC_MODE_CBR) {
                    m.attrCbr.iInitialQP = -1; m.attrCbr.iMinQP = 15; m.attrCbr.iMaxQP = 45;
                    m.attrCbr.iIPDelta = -1; m.attrCbr.iPBDelta = -1; m.attrCbr.eRcOptions = 1;
                } else if (v == IMP_ENC_RC_MODE_VBR) {
                    m.attrVbr.iInitialQP = -1; m.attrVbr.iMinQP = 15; m.attrVbr.iMaxQP = 45;
                    m.attrVbr.iIPDelta = -1; m.attrVbr.iPBDelta = -1; m.attrVbr.eRcOptions = 1;
                }
            }
            apply_rc(&m, op + 4);
            print_rc("set-in", &m);
            r = IMP_Encoder_SetChnAttrRcMode(chn, &m);
            printf("[OP] %s SetChnAttrRcMode -> ret=0x%08x\n", op, (unsigned)r);
        } else if (sscanf(op, "br:%d,%d", &a, &b) == 2)
            printf("[OP] %s -> ret=0x%08x\n", op, (unsigned)IMP_Encoder_SetChnBitRate(chn, a, b));
        else if (sscanf(op, "qpbf:%d,%d,%d,%d", &a, &b, &c, &d) == 4)
            printf("[OP] %s -> ret=0x%08x\n", op, (unsigned)IMP_Encoder_SetChnQpBoundsPerFrame(chn, a, b, c, d));
        else if (sscanf(op, "qpb:%d,%d", &a, &b) == 2)
            printf("[OP] %s -> ret=0x%08x\n", op, (unsigned)IMP_Encoder_SetChnQpBounds(chn, a, b));
        else if (sscanf(op, "maxpic:%d,%d", &a, &b) == 2)
            printf("[OP] %s -> ret=0x%08x\n", op, (unsigned)IMP_Encoder_SetChnMaxPictureSize(chn, a, b));
        else if (sscanf(op, "gop:%d", &a) == 1)
            printf("[OP] %s -> ret=0x%08x\n", op, (unsigned)IMP_Encoder_SetChnGopLength(chn, a));
        else if (!strncmp(op, "gopattr:", 8)) {
            IMPEncoderGopAttr g;
            memset(&g, 0, sizeof(g));
            IMP_Encoder_GetChnGopAttr(chn, &g);
            if (kv(op + 8, "mode", &v)) g.uGopCtrlMode = v;
            if (kv(op + 8, "len", &v)) g.uGopLength = v;
            if (kv(op + 8, "same", &v)) g.uMaxSameSenceCnt = v;
            if (kv(op + 8, "lt", &v)) g.bEnableLT = v;
            if (kv(op + 8, "freq", &v)) g.uFreqLT = v;
            if (kv(op + 8, "ltrc", &v)) g.bLTRC = v;
            printf("[OP] %s SetChnGopAttr -> ret=0x%08x\n", op, (unsigned)IMP_Encoder_SetChnGopAttr(chn, &g));
        } else if (sscanf(op, "fps:%d,%d", &a, &b) == 2) {
            IMPEncoderFrmRate f = { (uint32_t)a, (uint32_t)b };
            printf("[OP] %s -> ret=0x%08x\n", op, (unsigned)IMP_Encoder_SetChnFrmRate(chn, &f));
        } else if (sscanf(op, "ipd:%d", &a) == 1) {
            printf("[OP] %s -> ret=0x%08x\n", op,
                   IMP_Encoder_SetChnQpIPDelta ? (unsigned)IMP_Encoder_SetChnQpIPDelta(chn, a) : 0xdeadbeefu);
        } else if (!strcmp(op, "idr"))
            printf("[OP] idr -> ret=0x%08x\n", (unsigned)IMP_Encoder_RequestIDR(chn));
        else if (!strncmp(op, "run:", 4) && (p = strchr(op + 4, ':'))) {
            char lab[128];
            snprintf(lab, sizeof(lab), "%.*s", (int)(p - op - 4), op + 4);
            op_run(lab, atoi(p + 1));
        } else if (sscanf(op, "sleep:%d", &a) == 1) sleep(a);
        else if (!strcmp(op, "edge")) edge();
        else if (!strcmp(op, "defaults")) defaults();
        else printf("[E] unknown op %s\n", op);
    }

    IMP_Encoder_StopRecvPic(chn);
    IMP_FrameSource_DisableChn(0);
    IMP_System_UnBind(&fs_cell, &enc_cell);
    IMP_Encoder_UnRegisterChn(chn);
    IMP_Encoder_DestroyChn(chn);
    IMP_Encoder_DestroyGroup(0);
    IMP_FrameSource_DestroyChn(0);
    IMP_System_Exit();
    IMP_ISP_DisableTuning();
    IMP_ISP_DisableSensor(IMPVI_MAIN);
    IMP_ISP_DelSensor(IMPVI_MAIN, &sn);
    IMP_ISP_Close();
    return 0;
}
