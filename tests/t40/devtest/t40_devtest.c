/* T40 device API test: one process, one ISP/sensor session, many functions.
 * Prints "RES <function> <PASS|PART|FAIL> <evidence>" lines on stdout.
 * Build: mipsel toolchain with -I<T40 1.3.1 headers>, link -limp (OpenIMP or
 * vendor, selected at run time by LD_LIBRARY_PATH).  Picture data is only
 * measured in memory (mean luma, chroma, gradient); nothing is written.
 * Never calls IMP_ISP_Tuning_GetAeExpList (hangs the vendor kernel).
 * usage: t40_devtest [phase-mask-hex]   (default: all phases)
 *   phases: 1 lifecycle+framesource  2 encoder  4 isp tuning  8 stats/awb
 *           10 system  20 osd  40 sysutils (vendor libsysutils)  80 resize
 *           100 night 2026-10-12 fixes (Query fields, FrameRelease, SEI,
 *               DrawAttr; T40D_DUMP=<file> writes the SEI test stream)
 *           200 StartNightMode before AddSensor (run it on its own)
 *           400 OSD group with SetGroupCallback in the pipeline
 *           800 AI digital gain (microphone only, no playback)
 *           1000 WDR_OPEN LINEAR before AddSensor (run it on its own)
 *           4000 wip cells ISP (AE scene/caps, SensorFPS, CCM/LSC, flip modes, WDR/DRC, NR ratio, mask, getters)
 *           8000 encoder telemetry (Query histogram, telemetry, AveBitrate; 10000 adds the sensor flip modes to 4000)
 *           2000 GetRaw, one 8-bit sensor frame (about 8 MB rmem; on its own, last) */
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <imp/imp_common.h>
#include <imp/imp_system.h>
#include <imp/imp_isp.h>
#include <imp/imp_framesource.h>
#include <imp/imp_encoder.h>
#include <imp/imp_osd.h>
#include <imp/imp_audio.h>
#include <openimp/openimp_encoder_telemetry.h>
int OpenIMP_Encoder_GetTelemetry(int encChn, OpenIMPEncoderTelemetry *t) __attribute__((weak));
int IMP_Encoder_GetChnAveBitrate(int encChn, IMPEncoderStream *stream, int frames, int *bitrate) __attribute__((weak));
int IMP_Encoder_GetChnEvalInfo(int encChn, void *info) __attribute__((weak));

static void R(const char *fn, const char *v, const char *fmt, ...)
{
    va_list ap;
    printf("RES %s %s ", fn, v);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}
#define OK(c) ((c) ? "PASS" : "FAIL")

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

#ifndef CH
#define CH 1            /* scaled sub channel, 1280x720 */
#endif
#ifndef FW
#define FW 1280
#define FH 720
#endif
#define FPS 20

/* ---------- picture metrics (in memory only) ---------- */
struct pm { double y, sd, chroma, grad, u, v; int ok; unsigned w, h, size; };

static struct pm grab_one(void)
{
    struct pm m;
    IMPFrameInfo *f = NULL;
    unsigned x, y, w, h, st;
    const uint8_t *Y, *C;
    double sy = 0, syy = 0, sc = 0, sg = 0, su = 0, sv = 0;
    unsigned n = 0, nc = 0;

    memset(&m, 0, sizeof m);
    if (IMP_FrameSource_GetFrame(CH, &f) != 0 || !f)
        return m;
    w = f->width; h = f->height; m.w = w; m.h = h; m.size = f->size;
    st = w;
    Y = (const uint8_t *)(uintptr_t)f->virAddr;
    C = Y + st * ((h + 15) & ~15u);
    for (y = 0; y + 1 < h; y += 4)
        for (x = 0; x + 1 < w; x += 4) {
            int p = Y[y * st + x];
            sy += p; syy += (double)p * p; n++;
            sg += abs(p - (int)Y[y * st + x + 1]);
        }
    for (y = 0; y < h / 2; y += 4)
        for (x = 0; x + 1 < w; x += 8) {
            int u = C[y * st + x], v = C[y * st + x + 1];
            su += u; sv += v; sc += abs(u - 128) + abs(v - 128); nc++;
        }
    IMP_FrameSource_ReleaseFrame(CH, f);
    if (!n || !nc) return m;
    m.y = sy / n; m.sd = __builtin_sqrt(syy / n - m.y * m.y);
    m.grad = sg / n; m.chroma = sc / nc; m.u = su / nc; m.v = sv / nc; m.ok = 1;
    return m;
}

/* The channel has nrVBs = 2 capture buffers.  While nobody reads, both
 * are filled and then held by the driver, so the first two GetFrame calls
 * after a pause return pictures taken BEFORE the pause (before the control
 * under test was set).  The 2026-10-11 run measured those: HVFLIP looked
 * unmirrored and running mode / hue looked half-applied.  Drop them. */
#define STALE_FRAMES 3
static void drain_stale(void)
{
    int i;
    for (i = 0; i < STALE_FRAMES; i++) {
        IMPFrameInfo *f = NULL;
        if (IMP_FrameSource_GetFrame(CH, &f) == 0 && f)
            IMP_FrameSource_ReleaseFrame(CH, f);
    }
}

static struct pm grab(int settle_ms)
{
    struct pm a, s;
    int i, k = 0;
    memset(&s, 0, sizeof s);
    usleep(settle_ms * 1000);
    drain_stale();
    for (i = 0; i < 4; i++) {
        a = grab_one();
        if (!a.ok) continue;
        s.y += a.y; s.sd += a.sd; s.chroma += a.chroma; s.grad += a.grad;
        s.u += a.u; s.v += a.v; s.w = a.w; s.h = a.h; s.size = a.size; k++;
        usleep(60000);
    }
    if (k) { s.y /= k; s.sd /= k; s.chroma /= k; s.grad /= k; s.u /= k; s.v /= k; s.ok = 1; }
    return s;
}

/* ---------- encoder helpers ---------- */
struct es { unsigned frames, idr, bytes; unsigned idr_at[64]; unsigned nidr; };

static int hevc_mode;

static void stream_for(double secs, struct es *e)
{
    double t0 = now_s();
    memset(e, 0, sizeof *e);
    while (now_s() - t0 < secs) {
        IMPEncoderStream st;
        unsigned i, isidr = 0;
        if (IMP_Encoder_PollingStream(CH, 500) != 0) continue;
        if (IMP_Encoder_GetStream(CH, &st, 1) != 0) break;
        for (i = 0; i < st.packCount; i++) {
            e->bytes += st.pack[i].length;
            if (hevc_mode ? (st.pack[i].nalType.h265NalType == 19 || st.pack[i].nalType.h265NalType == 20)
                          : st.pack[i].nalType.h264NalType == 5)
                isidr = 1;
        }
        if (isidr) { e->idr++; if (e->nidr < 64) e->idr_at[e->nidr++] = e->frames; }
        e->frames++;
        IMP_Encoder_ReleaseStream(CH, &st);
    }
}
static double kbps(const struct es *e, double secs) { return e->bytes * 8.0 / 1000.0 / secs; }

static int med_gap(const struct es *e)
{
    unsigned i, a[64], n = 0;
    for (i = 1; i < e->nidr; i++) a[n++] = e->idr_at[i] - e->idr_at[i - 1];
    if (!n) return -1;
    for (i = 0; i < n; i++) { unsigned j; for (j = i + 1; j < n; j++) if (a[j] < a[i]) { unsigned t = a[i]; a[i] = a[j]; a[j] = t; } }
    return a[n / 2];
}

static void on_drop(void) { }
extern int IMP_System_MemPoolRequest(int, size_t, const char *) __attribute__((weak));
static double fabs_d(double x) { return x < 0 ? -x : x; }

/* ---------- wip-cells helpers (2026-10-24, phases 0x4000 / 0x8000) ---------- */
#define MX(f) ((unsigned)(FW * (f)))
#define MY(f) ((unsigned)(FH * (f)))
#define TW 16
#define TH 9
struct thumb { double v[TW * TH]; double mean; int ok; };

static int thumb_one(struct thumb *t)
{
    IMPFrameInfo *f = NULL;
    unsigned x, y, w, h;
    const uint8_t *Y;
    double sum[TW * TH]; unsigned cnt[TW * TH]; int i;

    if (IMP_FrameSource_GetFrame(CH, &f) != 0 || !f) return -1;
    w = f->width; h = f->height; Y = (const uint8_t *)(uintptr_t)f->virAddr;
    memset(sum, 0, sizeof sum); memset(cnt, 0, sizeof cnt);
    for (y = 0; y < h; y += 4) for (x = 0; x < w; x += 4) {
        int k = (y * TH / h) * TW + (x * TW / w);
        sum[k] += Y[y * w + x]; cnt[k]++;
    }
    IMP_FrameSource_ReleaseFrame(CH, f);
    t->mean = 0;
    for (i = 0; i < TW * TH; i++) { t->v[i] = cnt[i] ? sum[i] / cnt[i] : 0; t->mean += t->v[i]; }
    t->mean /= TW * TH; t->ok = 1;
    return 0;
}

static struct thumb thumb_grab(int settle_ms)
{
    struct thumb t, a; int i, k = 0, j;
    memset(&t, 0, sizeof t);
    usleep(settle_ms * 1000);
    drain_stale();
    for (i = 0; i < 3; i++) {
        memset(&a, 0, sizeof a);
        if (thumb_one(&a)) continue;
        for (j = 0; j < TW * TH; j++) t.v[j] += a.v[j];
        k++; usleep(60000);
    }
    if (k) { for (j = 0; j < TW * TH; j++) { t.v[j] /= k; t.mean += t.v[j]; } t.mean /= TW * TH; t.ok = 1; }
    return t;
}

/* mean absolute difference of the thumbnails with a transform of a (0 none, 1 H, 2 V, 3 HV) */
static double thumb_diff(const struct thumb *a, const struct thumb *b, int tr)
{
    int x, y; double s = 0;
    for (y = 0; y < TH; y++) for (x = 0; x < TW; x++) {
        int sx = (tr & 1) ? TW - 1 - x : x, sy = (tr & 2) ? TH - 1 - y : y;
        s += fabs_d(a->v[sy * TW + sx] - b->v[y * TW + x]);
    }
    return s / (TW * TH);
}

/* temporal noise of a static scene: mean |Y(t) - Y(t+1)| of consecutive frames */
static double noise_meas(int pairs)
{
    double acc = 0; int n = 0, p;
    static uint8_t prev[FW * FH / 9 + 64];
    drain_stale();
    for (p = 0; p < pairs + 1; p++) {
        IMPFrameInfo *f = NULL; unsigned x, y, w, h, k = 0; const uint8_t *Y; double s = 0; unsigned c = 0;
        if (IMP_FrameSource_GetFrame(CH, &f) != 0 || !f) continue;
        w = f->width; h = f->height; Y = (const uint8_t *)(uintptr_t)f->virAddr;
        for (y = 0; y < h; y += 3) for (x = 0; x < w; x += 3) {
            if (k >= sizeof prev) break;
            if (p) { s += abs((int)Y[y * w + x] - (int)prev[k]); c++; }
            prev[k++] = Y[y * w + x];
        }
        IMP_FrameSource_ReleaseFrame(CH, f);
        if (p && c) { acc += s / c; n++; }
        usleep(80000);
    }
    return n ? acc / n : -1;
}

static double region_y(unsigned rx, unsigned ry, unsigned rw, unsigned rh)
{
    IMPFrameInfo *f = NULL; unsigned x, y, w; const uint8_t *Y; double s = 0; unsigned n = 0;
    drain_stale();
    if (IMP_FrameSource_GetFrame(CH, &f) != 0 || !f) return -1;
    w = f->width; Y = (const uint8_t *)(uintptr_t)f->virAddr;
    for (y = ry; y < ry + rh && y < f->height; y += 2) for (x = rx; x < rx + rw && x < w; x += 2) { s += Y[y * w + x]; n++; }
    IMP_FrameSource_ReleaseFrame(CH, f);
    return n ? s / n : -1;
}

/* AE log line: ExprInfo + ScenceAttr at the moment */
static void ae_line(const char *tag)
{
    IMPISPAEExprInfo ei; IMPISPAEScenceAttr sa; int r1, r2;
    memset(&ei, 0, sizeof ei); memset(&sa, 0, sizeof sa);
    r1 = IMP_ISP_Tuning_GetAeExprInfo(IMPVI_MAIN, &ei);
    r2 = IMP_ISP_Tuning_GetAeScenceAttr(IMPVI_MAIN, &sa);
    printf("  AE[%s] rc=%d/%d IT=%u AG=%u DG=%u IspDG=%u totdB=%u EV=%u | luma=%u target=%u ae_mean=%u stable=%d | maxIT=%u/%u maxAG=%u/%u\n",
           tag, r1, r2, ei.AeIntegrationTime, ei.AeAGain, ei.AeDGain, ei.AeIspDGain, ei.TotalGainDb, ei.ExposureValue,
           sa.luma, sa.target, sa.ae_mean, sa.stable, ei.AeMaxIntegrationTimeMode, ei.AeMaxIntegrationTime,
           ei.AeMaxAGainMode, ei.AeMaxAGain);
}

struct qs { volatile int run; unsigned n, lp[8], sf[8], wd[2], nzb, nzp; };
static void *q_sampler(void *p)
{
    struct qs *q = p;
    while (q->run) {
        IMPEncoderChnStat s; memset(&s, 0xff, sizeof s);
        if (IMP_Encoder_Query(CH, &s) == 0) {
            q->n++; q->lp[s.leftPics > 7 ? 7 : s.leftPics]++; q->sf[s.leftStreamFrames > 7 ? 7 : s.leftStreamFrames]++;
            q->wd[s.work_done ? 1 : 0]++; if (s.leftStreamBytes) q->nzb++; if (s.curPacks) q->nzp++;
        }
        usleep(10000);
    }
    return NULL;
}


/* ---------- phase 0x100 helpers (night 2026-10-12 fixes) ---------- */
static const uint8_t sei_uuid[16] = { 0x6f, 0x70, 0x65, 0x6e, 0x69, 0x6d, 0x70, 0x2d,
                                      0x74, 0x34, 0x30, 0x2d, 0x73, 0x65, 0x69, 0x21 };
struct seiscan { unsigned frames, idr, sei, sei_idr, sei_bad, bytes; };

/* copy pack bytes out of the (possibly wrapping) stream ring */
static unsigned pack_bytes(const IMPEncoderStream *st, const IMPEncoderPack *pk, uint8_t *out, unsigned cap)
{
    unsigned n = pk->length < cap ? pk->length : cap, i;
    const uint8_t *base = (const uint8_t *)(uintptr_t)st->virAddr;
    for (i = 0; i < n; i++)
        out[i] = base[(pk->offset + i) % (st->streamSize ? st->streamSize : 0xffffffffu)];
    return n;
}

/* stream for secs; count SEI packs (H.264 type 6) that carry sei_uuid;
 * optionally append the raw stream to dump (host decode check) */
static void stream_sei(double secs, struct seiscan *z, FILE *dump)
{
    double t0 = now_s();
    static uint8_t buf[512 * 1024];
    memset(z, 0, sizeof *z);
    while (now_s() - t0 < secs) {
        IMPEncoderStream st;
        unsigned i, isidr = 0, hassei = 0;
        if (IMP_Encoder_PollingStream(CH, 500) != 0) continue;
        if (IMP_Encoder_GetStream(CH, &st, 1) != 0) break;
        for (i = 0; i < st.packCount; i++) {
            IMPEncoderPack *pk = &st.pack[i];
            unsigned n = pack_bytes(&st, pk, buf, sizeof buf), h = 0;
            z->bytes += pk->length;
            if (dump && n) fwrite(buf, 1, n, dump);
            if (pk->nalType.h264NalType == 5) isidr = 1;
            while (h + 3 < n && !(buf[h] == 0 && buf[h + 1] == 0 && buf[h + 2] == 1)) h++;
            h += 3;
            if (h < n && (buf[h] & 0x1f) == 6) {
                /* nal 6, payload 5, size, uuid */
                if (h + 3 + 16 <= n && buf[h + 1] == 5 && !memcmp(buf + h + 3, sei_uuid, 16)) hassei = 1;
                else z->sei_bad++;
            }
        }
        z->frames++;
        if (isidr) z->idr++;
        if (hassei) { z->sei++; if (isidr) z->sei_idr++; }
        IMP_Encoder_ReleaseStream(CH, &st);
    }
}

/* mean Y/U/V of a rectangle of the current (fresh) frame */
static int rect_mean(unsigned x0, unsigned y0, unsigned w, unsigned h, double *y, double *u, double *v)
{
    IMPFrameInfo *f = NULL;
    unsigned x, yy, st, n = 0, nc = 0;
    const uint8_t *Y, *C;
    double sy = 0, su = 0, sv = 0;
    drain_stale();
    if (IMP_FrameSource_GetFrame(CH, &f) != 0 || !f) return -1;
    st = f->width; Y = (const uint8_t *)(uintptr_t)f->virAddr; C = Y + st * ((f->height + 15) & ~15u);
    for (yy = y0; yy < y0 + h && yy < f->height; yy++)
        for (x = x0; x < x0 + w && x < f->width; x++) { sy += Y[yy * st + x]; n++; }
    for (yy = y0 / 2; yy < (y0 + h) / 2 && yy < f->height / 2; yy++)
        for (x = x0 & ~1u; x + 1 < x0 + w && x + 1 < f->width; x += 2) { su += C[yy * st + x]; sv += C[yy * st + x + 1]; nc++; }
    IMP_FrameSource_ReleaseFrame(CH, f);
    if (!n || !nc) return -1;
    *y = sy / n; *u = su / nc; *v = sv / nc;
    return 0;
}

static volatile int osd_cb_calls, osd_cb_group = -1;
static volatile unsigned osd_cb_w;
static int osd_cb(int group, IMPFrameInfo *frame)
{
    osd_cb_calls++; osd_cb_group = group; osd_cb_w = frame ? frame->width : 0;
    return 0;
}
static IMPCell osd_cell;
static int osd_made, osd_att, osd_cb_rc = -99;


/* microphone RMS over secs (AI dev 0 chn 0 must be enabled) */
static double mic_rms(double secs, unsigned *frames)
{
    double t0 = now_s(), acc = 0;
    unsigned long n = 0;
    *frames = 0;
    while (now_s() - t0 < secs) {
        IMPAudioFrame fr;
        int i;
        if (IMP_AI_PollingFrame(0, 0, 500) != 0) continue;
        if (IMP_AI_GetFrame(0, 0, &fr, BLOCK) != 0) break;
        for (i = 0; i < fr.len / 2; i++) { double v = ((int16_t *)fr.virAddr)[i]; acc += v * v; n++; }
        (*frames)++;
        IMP_AI_ReleaseFrame(0, 0, &fr);
    }
    return n ? __builtin_sqrt(acc / n) : -1;
}

static IMPSensorInfo sinfo;
static IMPEncoderChnAttr eattr;
static int fs_made, grp_made, chn_made, bound, fs_on, recv_on;
static IMPCell csrc, cdst;

int main(int argc, char **argv)
{
    unsigned mask = argc > 1 ? (unsigned)strtoul(argv[1], 0, 16) : 0xffu;
    int r, i, depth;
    IMPFSChnAttr fs;
    struct es e;
    double t;
    struct pm a, b, c;
    unsigned char u8, u8o;
    IMPEncoderStream st;

    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(900);

    /* ================= phase 1: lifecycle, system basics, framesource ================= */
    memset(&sinfo, 0, sizeof sinfo);
    strcpy(sinfo.name, "sc830ai");
    sinfo.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C;
    strcpy(sinfo.i2c.type, "sc830ai");
    sinfo.i2c.addr = 0x30;
    sinfo.i2c.i2c_adapter_id = 1;
    sinfo.rst_gpio = 91; sinfo.pwdn_gpio = -1; sinfo.power_gpio = -1;
    sinfo.video_interface = IMPISP_SENSOR_VI_MIPI_CSI0;
    sinfo.mclk = IMPISP_SENSOR_MCLK1;
    sinfo.default_boot = 4;

    r = IMP_ISP_Open();                       R("ISP_Open", OK(!r), "rc=%d", r);
    if (r) return 3;
    static int night_rc = -99;
    if (mask & 0x200)       /* header: after ISP_Open, before AddSensor */
        night_rc = IMP_ISP_StartNightMode(IMPVI_MAIN);
    static int wdr_open_rc = -99;
    if (mask & 0x1000) {    /* linear boot, WDR mode prepared (no WDR switch) */
        IMPISPWdrOpenAttr wo;
        wo.rmode = IMPISP_TYPE_RUN_LINEAR; wo.imode = IMPISP_TYPE_WDR_DOL;
        wdr_open_rc = IMP_ISP_WDR_OPEN(IMPVI_MAIN, &wo);
    }
    r = IMP_ISP_AddSensor(IMPVI_MAIN, &sinfo);    R("ISP_AddSensor", OK(!r), "rc=%d", r);
    r = IMP_ISP_EnableSensor(IMPVI_MAIN, &sinfo); R("ISP_EnableSensor", OK(!r), "rc=%d", r);
    if (r) goto down;
    r = IMP_System_Init();                    R("System_Init", OK(!r), "rc=%d", r);
    r = IMP_ISP_EnableTuning();               R("ISP_EnableTuning", OK(!r), "rc=%d", r);

    memset(&fs, 0, sizeof fs);
    fs.picWidth = FW; fs.picHeight = FH; fs.pixFmt = PIX_FMT_NV12;
    fs.scaler.enable = 1; fs.scaler.outwidth = FW; fs.scaler.outheight = FH;
    fs.outFrmRateNum = FPS; fs.outFrmRateDen = 1; fs.nrVBs = 2; fs.type = FS_PHY_CHANNEL;
    r = IMP_FrameSource_CreateChn(CH, &fs);   if (r) { R("FrameSource_CreateChn", "FAIL", "rc=%d", r); goto down; }
    fs_made = 1;
    IMP_FrameSource_SetChnAttr(CH, &fs);

    /* FrameDepth: before enable */
    {
        int d0 = -99, rs, rg, ok = 1;
        char buf[200] = "";
        static const int vals[] = { 0, 1, 2, 4 };
        for (i = 0; i < 4; i++) {
            d0 = -99;
            rs = IMP_FrameSource_SetFrameDepth(CH, vals[i]);
            rg = IMP_FrameSource_GetFrameDepth(CH, &d0);
            if (rs || rg || d0 != vals[i]) ok = 0;
            snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "set%d->rc%d/get%d(rc%d) ", vals[i], rs, d0, rg);
        }
        R("FrameSource_Get/SetFrameDepth", OK(ok), "readback %s", buf);
    }
    IMP_FrameSource_SetFrameDepth(CH, 2);

    /* encoder group + channel (bring-up shared by phase 2) */
    r = IMP_Encoder_CreateGroup(CH); if (r) { R("Encoder_CreateGroup", "FAIL", "rc=%d", r); goto down; }
    grp_made = 1;

    /* SetDefaultParam checks */
    {
        static const IMPEncoderProfile prof[] = { IMP_ENC_PROFILE_AVC_HIGH, IMP_ENC_PROFILE_HEVC_MAIN, IMP_ENC_PROFILE_JPEG };
        static const IMPEncoderRcMode rcm[] = { IMP_ENC_RC_MODE_FIXQP, IMP_ENC_RC_MODE_CBR, IMP_ENC_RC_MODE_VBR, IMP_ENC_RC_MODE_CAPPED_VBR };
        int bad = 0, n = 0, p, m;
        IMPEncoderChnAttr x;
        for (p = 0; p < 3; p++)
            for (m = 0; m < 4; m++) {
                if (p == 2 && m) break;
                memset(&x, 0xAA, sizeof x);
                r = IMP_Encoder_SetDefaultParam(&x, prof[p], rcm[m], FW, FH, FPS, 1, 40, 2, m ? -1 : 30, m ? 2000 : 0);
                n++;
                if (r || x.encAttr.eProfile != prof[p] || x.encAttr.uWidth != FW || x.encAttr.uHeight != FH ||
                    (p < 2 && (x.rcAttr.attrRcMode.rcMode != rcm[m] || x.rcAttr.outFrmRate.frmRateNum != FPS))) {
                    bad++;
                    printf("  SDP mismatch prof=%d rc=%d: rc=%d prof=%x %ux%u rcmode=%d fps=%u\n", p, m, r, x.encAttr.eProfile,
                           x.encAttr.uWidth, x.encAttr.uHeight, x.rcAttr.attrRcMode.rcMode, x.rcAttr.outFrmRate.frmRateNum);
                }
            }
        memset(&x, 0, sizeof x);
        IMP_Encoder_SetDefaultParam(&x, IMP_ENC_PROFILE_AVC_HIGH, IMP_ENC_RC_MODE_CBR, FW, FH, FPS, 1, 40, 2, -1, 2000);
        R("Encoder_SetDefaultParam", OK(!bad), "%d combos (AVC/HEVC x FixQP/CBR/VBR/CappedVBR, JPEG) mismatches=%d; CBR target=%u init=%d min=%d max=%d gop=%u",
          n, bad, x.rcAttr.attrRcMode.attrCbr.uTargetBitRate, x.rcAttr.attrRcMode.attrCbr.iInitialQP,
          x.rcAttr.attrRcMode.attrCbr.iMinQP, x.rcAttr.attrRcMode.attrCbr.iMaxQP, x.gopAttr.uGopLength);
        eattr = x;
        eattr.rcAttr.attrRcMode.attrCbr.uTargetBitRate = 2000;
    }

    /* pool + stream buffer size before channel create */
    if (mask & 2) {
        int rp, rg, pid = -9;
        rp = IMP_System_MemPoolRequest ? IMP_System_MemPoolRequest(5, 8 * 1024 * 1024, "encpool") : -1000;
        R("System_MemPoolRequest", OK(!rp), "rc=%d%s", rp, IMP_System_MemPoolRequest ? "" : " (symbol not exported)");
        {
            int n0 = -1, n1 = -1, rs, rg0, rg1;
            rg0 = IMP_Encoder_GetMaxStreamCnt(CH, &n0);
            rs = IMP_Encoder_SetMaxStreamCnt(CH, 5);
            rg1 = IMP_Encoder_GetMaxStreamCnt(CH, &n1);
            R("Encoder_Get/SetMaxStreamCnt", OK(!rg0 && !rs && !rg1 && n1 == 5), "before create: default %d, set 5 rc=%d, readback %d", n0, rs, n1);
        }
        r = IMP_Encoder_SetPool(CH, 5); rg = IMP_Encoder_GetPool(CH); pid = rg;
        R("Encoder_Get/SetPool", OK(!r && pid == 5), "SetPool rc=%d GetPool=%d", r, pid);
        {
            uint32_t sz = 0;
            int r1 = IMP_Encoder_SetStreamBufSize(CH, 3 * 1024 * 1024), r2 = IMP_Encoder_GetStreamBufSize(CH, &sz);
            R("Encoder_Get/SetStreamBufSize", OK(!r1 && !r2 && sz == 3 * 1024 * 1024), "set rc=%d get rc=%d size=%u", r1, r2, sz);
        }
    }
    r = IMP_Encoder_CreateChn(CH, &eattr);    R("Encoder_CreateChn", OK(!r), "rc=%d", r);
    if (r) goto down;
    chn_made = 1;
    r = IMP_Encoder_RegisterChn(CH, CH);      R("Encoder_RegisterChn", OK(!r), "rc=%d", r);
    csrc.deviceID = DEV_ID_FS; csrc.groupID = CH; csrc.outputID = 0;
    cdst.deviceID = DEV_ID_ENC; cdst.groupID = CH; cdst.outputID = 0;
    r = IMP_System_Bind(&csrc, &cdst);        R("System_Bind", OK(!r), "rc=%d", r);
    bound = !r;
    if ((mask & 0x400) && bound) {
        /* OSD group in the pipeline (FS -> OSD -> ENC) with a callback;
         * header: SetGroupCallback after CreateGroup, before EnableChn */
        osd_cell.deviceID = DEV_ID_OSD; osd_cell.groupID = CH; osd_cell.outputID = 0;
        osd_made = !IMP_OSD_CreateGroup(CH);
        osd_cb_rc = IMP_OSD_SetGroupCallback(CH, osd_cb);
        osd_att = !IMP_OSD_AttachToGroup(&osd_cell, &cdst);
        IMP_OSD_Start(CH);
        printf("  osd group %d made=%d callback rc=%d attached=%d\n", CH, osd_made, osd_cb_rc, osd_att);
    }
    {
        IMPCell got; memset(&got, 0xff, sizeof got);
        r = IMP_System_GetBindbyDest(&cdst, &got);
        R("System_GetBindbyDest", OK(!r && got.deviceID == DEV_ID_FS && got.groupID == CH), "rc=%d src dev=%d grp=%d out=%d", r, got.deviceID, got.groupID, got.outputID);
    }
    r = IMP_FrameSource_EnableChn(CH);        R("FrameSource_EnableChn", OK(!r), "rc=%d", r);
    fs_on = !r;

    /* Query before start */
    {
        IMPEncoderChnStat s; memset(&s, 0xff, sizeof s);
        r = IMP_Encoder_Query(CH, &s);
        printf("  Query before start: rc=%d registered=%d leftPics=%u leftBytes=%u leftFrames=%u packs=%u done=%u\n", r, s.registered, s.leftPics, s.leftStreamBytes, s.leftStreamFrames, s.curPacks, s.work_done);
    }
    r = IMP_Encoder_StartRecvPic(CH);         R("Encoder_StartRecvPic", OK(!r), "rc=%d", r);
    recv_on = !r;
    stream_for(2.0, &e);
    R("Encoder_PollingStream/GetStream/ReleaseStream", OK(e.frames >= 30), "%u frames in 2 s, %.0f kbps, %u IDR", e.frames, kbps(&e, 2.0), e.idr);

    /* FrameDepth with real frames */
    if (mask & 1) {
        IMPFrameInfo *f = NULL;
        double t1;
        int rgf0, rgf2;
        IMP_FrameSource_SetFrameDepth(CH, 2);
        usleep(300000);
        rgf2 = IMP_FrameSource_GetFrame(CH, &f);
        if (rgf2 == 0 && f) {
            printf("  frame %ux%u fmt=%u size=%u\n", f->width, f->height, f->pixfmt, f->size);
            IMP_FrameSource_ReleaseFrame(CH, f);
        }
        IMP_FrameSource_SetFrameDepth(CH, 0);
        usleep(300000);
        f = NULL; t1 = now_s();
        rgf0 = IMP_FrameSource_GetFrame(CH, &f);
        t1 = now_s() - t1;
        if (rgf0 == 0 && f) IMP_FrameSource_ReleaseFrame(CH, f);
        IMP_FrameSource_SetFrameDepth(CH, 2);
        R("FrameSource_Get/SetFrameDepth(GetFrame)", OK(rgf2 == 0), "depth2 GetFrame rc=%d; depth0 GetFrame rc=%d after %.2f s", rgf2, rgf0, t1);
    }

    if (mask & 2) {
        /* ===== phase 2: encoder ===== */
        IMPEncoderChnAttr ga; IMPEncoderEncType et = 99; int fd;
        IMPEncoderChnStat qs;
        uint32_t bm;

        memset(&ga, 0, sizeof ga);
        r = IMP_Encoder_GetChnAttr(CH, &ga);
        R("Encoder_GetChnAttr", OK(!r && ga.encAttr.uWidth == FW && ga.encAttr.uHeight == FH && ga.encAttr.eProfile == IMP_ENC_PROFILE_AVC_HIGH && ga.rcAttr.attrRcMode.rcMode == IMP_ENC_RC_MODE_CBR),
          "rc=%d %ux%u profile=%x rcmode=%d target=%u fps=%u/%u gop=%u", r, ga.encAttr.uWidth, ga.encAttr.uHeight, ga.encAttr.eProfile,
          ga.rcAttr.attrRcMode.rcMode, ga.rcAttr.attrRcMode.attrCbr.uTargetBitRate, ga.rcAttr.outFrmRate.frmRateNum, ga.rcAttr.outFrmRate.frmRateDen, ga.gopAttr.uGopLength);
        r = IMP_Encoder_GetChnEncType(CH, &et);
        R("Encoder_GetChnEncType", OK(!r && et == IMP_ENC_TYPE_AVC), "rc=%d type=%d (AVC=0)", r, et);

        fd = IMP_Encoder_GetFd(CH);
        {
            struct pollfd pf; int pr = -1;
            pf.fd = fd; pf.events = POLLIN;
            if (fd >= 0) { usleep(300000); pr = poll(&pf, 1, 1000); }
            R("Encoder_GetFd", OK(fd >= 0 && pr > 0 && (pf.revents & POLLIN)), "fd=%d poll=%d revents=%x", fd, pr, pf.revents);
        }
        bm = 1u << CH;
        r = IMP_Encoder_PollingModuleStream(&bm, 1000);
        R("Encoder_PollingModuleStream", OK(!r && (bm & (1u << CH))), "rc=%d bitmap=%x", r, bm);

        /* Query while streaming + after pause */
        memset(&qs, 0xff, sizeof qs);
        r = IMP_Encoder_Query(CH, &qs);
        {
            IMPEncoderChnStat q2;
            usleep(1200000);
            memset(&q2, 0xff, sizeof q2);
            r |= IMP_Encoder_Query(CH, &q2);
            R("Encoder_Query", (!r && q2.registered) ? (q2.leftStreamFrames > 0 ? "PASS" : "PART") : "FAIL", "running: registered=%d leftPics=%u; after 1.2 s unread: leftBytes=%u leftFrames=%u packs=%u done=%u",
              qs.registered, qs.leftPics, q2.leftStreamBytes, q2.leftStreamFrames, q2.curPacks, q2.work_done);
            /* FlushStream drops the queued frames */
            r = IMP_Encoder_FlushStream(CH);
            memset(&q2, 0xff, sizeof q2);
            i = IMP_Encoder_Query(CH, &q2);
            stream_for(1.0, &e);
            R("Encoder_FlushStream", OK(!r && !i && q2.leftStreamFrames <= 1 && e.frames > 10), "flush rc=%d, queued frames after flush=%u, stream continues %u frames/1 s", r, q2.leftStreamFrames, e.frames);
        }

        /* IDR request: long GOP so IDRs are rare */
        {
            IMPEncoderGopAttr g;
            IMP_Encoder_GetChnGopAttr(CH, &g);
            g.uGopLength = 400;
            IMP_Encoder_SetChnGopAttr(CH, &g);
            stream_for(3.0, &e);
            printf("  long GOP baseline: %u frames, %u IDR\n", e.frames, e.idr);
            {
                unsigned base_idr = e.idr, tot = 0, got_idr = 0;
                r = IMP_Encoder_RequestIDR(CH);
                stream_for(1.5, &e);
                tot = e.frames; got_idr = e.idr;
                R("Encoder_RequestIDR", OK(!r && got_idr >= 1), "rc=%d, IDR within next %u frames: %u (baseline in 3 s: %u, GOP 400)", r, tot, got_idr, base_idr);
            }
            /* GOP length 20 via SetChnGopAttr */
            g.uGopLength = 20;
            r = IMP_Encoder_SetChnGopAttr(CH, &g);
            stream_for(4.0, &e);
            {
                IMPEncoderGopAttr g2; int rg2 = IMP_Encoder_GetChnGopAttr(CH, &g2);
                R("Encoder_Get/SetChnGopAttr", OK(!r && !rg2 && g2.uGopLength == 20 && med_gap(&e) >= 18 && med_gap(&e) <= 22), "set rc=%d get=%u; median IDR spacing %d frames (%u IDR in %u frames)", r, g2.uGopLength, med_gap(&e), e.idr, e.frames);
            }
            r = IMP_Encoder_SetChnGopLength(CH, 40);
            stream_for(5.0, &e);
            R("Encoder_SetChnGopLength", OK(!r && med_gap(&e) >= 36 && med_gap(&e) <= 44), "rc=%d; median IDR spacing %d frames (%u IDR in %u frames)", r, med_gap(&e), e.idr, e.frames);
        }

        /* frame rate */
        {
            IMPEncoderFrmRate fr, fr2; int rg, rs; double secs = 4.0;
            rg = IMP_Encoder_GetChnFrmRate(CH, &fr);
            printf("  frmrate get rc=%d %u/%u\n", rg, fr.frmRateNum, fr.frmRateDen);
            fr2.frmRateNum = 10; fr2.frmRateDen = 1;
            rs = IMP_Encoder_SetChnFrmRate(CH, &fr2);
            usleep(500000);
            stream_for(secs, &e);
            {
                IMPEncoderFrmRate fr3 = { 0, 0 }; int rg3 = IMP_Encoder_GetChnFrmRate(CH, &fr3);
                double fps = e.frames / secs;
                R("Encoder_Get/SetChnFrmRate", OK(!rg && !rs && !rg3 && fr3.frmRateNum == 10 && fps > 8.5 && fps < 11.5), "get rc=%d %u/%u; set 10/1 rc=%d; readback %u/%u; measured %.1f fps over %.0f s", rg, fr.frmRateNum, fr.frmRateDen, rs, fr3.frmRateNum, fr3.frmRateDen, fps, secs);
            }
            rs = IMP_Encoder_SetChnFrmRate(CH, &fr);
            usleep(300000);
        }

        /* rc mode / bitrate / qp bounds */
        {
            IMPEncoderAttrRcMode rc, rc2; double secs = 4.0, k0, k1, k2;
            r = IMP_Encoder_GetChnAttrRcMode(CH, &rc);
            printf("  rcmode get rc=%d mode=%d cbr target=%u\n", r, rc.rcMode, rc.attrCbr.uTargetBitRate);
            stream_for(secs, &e); k0 = kbps(&e, secs);
            /* SetChnBitRate */
            r = IMP_Encoder_SetChnBitRate(CH, 600000, 800000);
            usleep(500000);
            stream_for(secs, &e); k1 = kbps(&e, secs);
            memset(&rc2, 0, sizeof rc2);
            IMP_Encoder_GetChnAttrRcMode(CH, &rc2);
            R("Encoder_SetChnBitRate", OK(!r && rc2.attrCbr.uTargetBitRate == 600 && k1 < k0 * 0.8), "rc=%d CBR target 2000 -> 600 kbps (arg in bit/s as in the header): measured %.0f -> %.0f kbps, readback target=%u", r, k0, k1, rc2.attrCbr.uTargetBitRate);
            /* SetChnAttrRcMode: CBR 3000 */
            rc2 = rc; rc2.rcMode = IMP_ENC_RC_MODE_CBR; rc2.attrCbr.uTargetBitRate = 3000;
            r = IMP_Encoder_SetChnAttrRcMode(CH, &rc2);
            usleep(500000);
            stream_for(secs, &e); k2 = kbps(&e, secs);
            {
                IMPEncoderAttrRcMode rc3; memset(&rc3, 0, sizeof rc3);
                i = IMP_Encoder_GetChnAttrRcMode(CH, &rc3);
                printf("  CBR 3000 set rc=%d get rc=%d mode=%d target=%u measured %.0f kbps\n", r, i, rc3.rcMode, rc3.attrCbr.uTargetBitRate, k2);
                R("Encoder_Get/SetChnAttrRcMode(CBR)", OK(!r && !i && rc3.rcMode == IMP_ENC_RC_MODE_CBR && rc3.attrCbr.uTargetBitRate == 3000 && k2 > k1 * 1.5), "set CBR 3000 rc=%d readback mode=%d target=%u, measured %.0f kbps (was %.0f)", r, rc3.rcMode, rc3.attrCbr.uTargetBitRate, k2, k1);
            }
            /* mode switch to FixQP (needed for ROI test) */
            memset(&rc2, 0, sizeof rc2);
            rc2.rcMode = IMP_ENC_RC_MODE_FIXQP; rc2.attrFixQp.iInitialQP = 34;
            r = IMP_Encoder_SetChnAttrRcMode(CH, &rc2);
            usleep(500000);
            stream_for(3.0, &e); k0 = kbps(&e, 3.0);
            {
                IMPEncoderAttrRcMode rc3; memset(&rc3, 0, sizeof rc3);
                i = IMP_Encoder_GetChnAttrRcMode(CH, &rc3);
                printf("  FixQP34 set rc=%d get rc=%d mode=%d qp=%d measured %.0f kbps\n", r, i, rc3.rcMode, rc3.attrFixQp.iInitialQP, k0);
                R("Encoder_Get/SetChnAttrRcMode(FixQP)", OK(!r && !i && rc3.rcMode == IMP_ENC_RC_MODE_FIXQP && rc3.attrFixQp.iInitialQP == 34), "mode switch CBR->FixQP34 rc=%d readback mode=%d qp=%d, %.0f kbps", r, rc3.rcMode, rc3.attrFixQp.iInitialQP, k0);
            }
            /* ROI in FixQP: full-frame delta -10 */
            {
                IMPEncoderRoiAttr ra; double ka, kb;
                memset(&ra, 0, sizeof ra);
                ra.st_roi[0].enable = 1; ra.st_roi[0].rect.x = 0; ra.st_roi[0].rect.y = 0;
                ra.st_roi[0].rect.w = FW - 1; ra.st_roi[0].rect.h = FH - 1; ra.st_roi[0].mode = IMP_ROI_QPMODE_DELTA; ra.st_roi[0].qp = -10;
                stream_for(3.0, &e); kb = kbps(&e, 3.0);
                r = IMP_Encoder_SetChnRoiAttr(CH, &ra);
                usleep(500000);
                stream_for(3.0, &e); ka = kbps(&e, 3.0);
                {
                    IMPEncoderRoiAttr rg; memset(&rg, 0, sizeof rg);
                    i = IMP_Encoder_GetChnRoiAttr(CH, &rg);
                    printf("  ROI set rc=%d get rc=%d win0 en=%d rect=%u,%u %ux%u mode=%d qp=%d; %.0f -> %.0f kbps\n", r, i, rg.st_roi[0].enable, rg.st_roi[0].rect.x, rg.st_roi[0].rect.y, rg.st_roi[0].rect.w, rg.st_roi[0].rect.h, rg.st_roi[0].mode, rg.st_roi[0].qp, kb, ka);
                    R("Encoder_Get/SetChnRoiAttr", (!r && !i && rg.st_roi[0].enable && rg.st_roi[0].qp == -10) ? (ka > kb * 1.5 ? "PASS" : "PART") : "FAIL",
                      "set rc=%d get rc=%d readback en=%d qp=%d; FixQP34 full-frame delta -10: %.0f -> %.0f kbps (x%.2f)", r, i, rg.st_roi[0].enable, rg.st_roi[0].qp, kb, ka, kb > 0 ? ka / kb : 0.0);
                }
                memset(&ra, 0, sizeof ra);
                IMP_Encoder_SetChnRoiAttr(CH, &ra);
            }
            /* back to CBR 2000 and test qp bounds */
            rc2 = rc; rc2.rcMode = IMP_ENC_RC_MODE_CBR; rc2.attrCbr.uTargetBitRate = 2000;
            IMP_Encoder_SetChnAttrRcMode(CH, &rc2);
            usleep(500000);
            r = IMP_Encoder_SetChnQpBounds(CH, 30, 40);
            {
                IMPEncoderAttrRcMode rc3; memset(&rc3, 0, sizeof rc3);
                i = IMP_Encoder_GetChnAttrRcMode(CH, &rc3);
                R("Encoder_SetChnQpBounds", OK(!r && !i && rc3.attrCbr.iMinQP == 30 && rc3.attrCbr.iMaxQP == 40), "rc=%d readback min=%d max=%d (mode %d)", r, rc3.attrCbr.iMinQP, rc3.attrCbr.iMaxQP, rc3.rcMode);
                IMP_Encoder_SetChnQpBounds(CH, 15, 45);
            }
        }

        /* max stream cnt: set before CreateChn (see pre-create block); after create the header says it is too late */
        {
            int n1 = -1, rs = IMP_Encoder_SetMaxStreamCnt(CH, 3), rg1 = IMP_Encoder_GetMaxStreamCnt(CH, &n1);
            printf("  SetMaxStreamCnt after create: rc=%d readback=%d (header: must be before create)\n", rs, n1);
            (void)rg1;
        }
    }
    if (mask & 0x80) {
        /* resize mode (own phase: hung the device run in phase 2) */
        {
            int a1 = IMP_Encoder_SetChnResizeMode(CH, 1), a0 = IMP_Encoder_SetChnResizeMode(CH, 0);
            stream_for(1.0, &e);
            R("Encoder_SetChnResizeMode", (!a1 && !a0 && e.frames > 10) ? "PART" : "FAIL", "set 1 rc=%d, set 0 rc=%d, stream continues (%u frames); effect not observable", a1, a0, e.frames);
        }
    }

    /* ===== phase 4: ISP tuning with picture metrics ===== */
    if (mask & 4) {
        IMPISPHVFLIP fl, fl2; IMPISPAntiflickerAttr af, af2; IMPISPRunningMode rm; IMPISPTuningOpsMode bp;
        unsigned char orig;

        usleep(1500000);
        a = grab(300);
        printf("  baseline frame %ux%u size=%u Y=%.1f sd=%.1f chroma=%.1f grad=%.2f U=%.1f V=%.1f\n", a.w, a.h, a.size, a.y, a.sd, a.chroma, a.grad, a.u, a.v);

        /* Brightness */
        { int rgo, rsl, rsh, gl = -1, gh = -1; struct pm pl, ph; unsigned char o = 0, g = 0;
          rgo = IMP_ISP_Tuning_GetBrightness(IMPVI_MAIN, &o);
          u8 = 40; rsl = IMP_ISP_Tuning_SetBrightness(IMPVI_MAIN, &u8); pl = grab(1500); IMP_ISP_Tuning_GetBrightness(IMPVI_MAIN, &g); gl = g;
          u8 = 220; rsh = IMP_ISP_Tuning_SetBrightness(IMPVI_MAIN, &u8); ph = grab(1500); IMP_ISP_Tuning_GetBrightness(IMPVI_MAIN, &g); gh = g;
          u8 = o; IMP_ISP_Tuning_SetBrightness(IMPVI_MAIN, &u8);
          R("ISP_Tuning_SetBrightness", OK(!rgo && !rsl && !rsh && gl == 40 && gh == 220 && ph.y > pl.y + 10), "get(default)=%u rc=%d; set40->get %d meanY=%.1f; set220->get %d meanY=%.1f", o, rgo, gl, pl.y, gh, ph.y); }
        /* Contrast */
        { int rgo, rsl, rsh, gl = -1, gh = -1; struct pm pl, ph; unsigned char o = 0, g = 0;
          rgo = IMP_ISP_Tuning_GetContrast(IMPVI_MAIN, &o);
          u8 = 40; rsl = IMP_ISP_Tuning_SetContrast(IMPVI_MAIN, &u8); pl = grab(1500); IMP_ISP_Tuning_GetContrast(IMPVI_MAIN, &g); gl = g;
          u8 = 220; rsh = IMP_ISP_Tuning_SetContrast(IMPVI_MAIN, &u8); ph = grab(1500); IMP_ISP_Tuning_GetContrast(IMPVI_MAIN, &g); gh = g;
          u8 = o; IMP_ISP_Tuning_SetContrast(IMPVI_MAIN, &u8);
          R("ISP_Tuning_Get/SetContrast", OK(!rgo && !rsl && !rsh && gl == 40 && gh == 220 && ph.sd > pl.sd + 3), "default=%u; set40->get %d sd(Y)=%.1f; set220->get %d sd(Y)=%.1f", o, gl, pl.sd, gh, ph.sd); }
        /* Saturation */
        { int rgo, rsl, rsh, gl = -1, gh = -1; struct pm pl, ph; unsigned char o = 0, g = 0;
          rgo = IMP_ISP_Tuning_GetSaturation(IMPVI_MAIN, &o);
          u8 = 20; rsl = IMP_ISP_Tuning_SetSaturation(IMPVI_MAIN, &u8); pl = grab(1500); IMP_ISP_Tuning_GetSaturation(IMPVI_MAIN, &g); gl = g;
          u8 = 240; rsh = IMP_ISP_Tuning_SetSaturation(IMPVI_MAIN, &u8); ph = grab(1500); IMP_ISP_Tuning_GetSaturation(IMPVI_MAIN, &g); gh = g;
          u8 = o; IMP_ISP_Tuning_SetSaturation(IMPVI_MAIN, &u8);
          R("ISP_Tuning_Get/SetSaturation", OK(!rgo && !rsl && !rsh && gl == 20 && gh == 240 && ph.chroma > pl.chroma + 2), "default=%u; set20->get %d chroma=%.1f; set240->get %d chroma=%.1f", o, gl, pl.chroma, gh, ph.chroma); }
        /* Sharpness */
        { int rgo, rsl, rsh, gl = -1, gh = -1; struct pm pl, ph; unsigned char o = 0, g = 0;
          rgo = IMP_ISP_Tuning_GetSharpness(IMPVI_MAIN, &o);
          u8 = 10; rsl = IMP_ISP_Tuning_SetSharpness(IMPVI_MAIN, &u8); pl = grab(1500); IMP_ISP_Tuning_GetSharpness(IMPVI_MAIN, &g); gl = g;
          u8 = 250; rsh = IMP_ISP_Tuning_SetSharpness(IMPVI_MAIN, &u8); ph = grab(1500); IMP_ISP_Tuning_GetSharpness(IMPVI_MAIN, &g); gh = g;
          u8 = o; IMP_ISP_Tuning_SetSharpness(IMPVI_MAIN, &u8);
          printf("  sharp: get rc=%d set rc=%d/%d\n", rgo, rsl, rsh);
          R("ISP_Tuning_Get/SetSharpness", (!rgo && !rsl && !rsh && gl == 10 && gh == 250) ? (ph.grad > pl.grad * 1.03 ? "PASS" : "PART") : "FAIL", "default=%u; set10->get %d grad=%.2f; set250->get %d grad=%.2f", o, gl, pl.grad, gh, ph.grad); }
        /* Hue */
        { int rgo, rsl, rsh, gl = -1, gh = -1; struct pm pl, ph; unsigned char o = 0, g = 0;
          rgo = IMP_ISP_Tuning_GetBcshHue(IMPVI_MAIN, &o);
          { struct pm p0 = grab(300); printf("  hue default %u: U=%.1f V=%.1f chroma=%.1f\n", o, p0.u, p0.v, p0.chroma); }
          u8 = 20; rsl = IMP_ISP_Tuning_SetBcshHue(IMPVI_MAIN, &u8); pl = grab(1500); IMP_ISP_Tuning_GetBcshHue(IMPVI_MAIN, &g); gl = g;
          u8 = 230; rsh = IMP_ISP_Tuning_SetBcshHue(IMPVI_MAIN, &u8); ph = grab(1500); IMP_ISP_Tuning_GetBcshHue(IMPVI_MAIN, &g); gh = g;
          u8 = o; IMP_ISP_Tuning_SetBcshHue(IMPVI_MAIN, &u8);
          R("ISP_Tuning_Get/SetBcshHue", OK(!rgo && !rsl && !rsh && gl == 20 && gh == 230 && (fabs_d(pl.u - ph.u) + fabs_d(pl.v - ph.v) > 1.5)), "default=%u; set20->get %d U=%.1f V=%.1f; set230->get %d U=%.1f V=%.1f", o, gl, pl.u, pl.v, gh, ph.u, ph.v); }
        (void)orig;

        /* HV flip (ISP modes) */
        {
            int rg0, rs, rg1, ok;
            struct pm base;
            rg0 = IMP_ISP_Tuning_GetHVFlip(IMPVI_MAIN, &fl);
            base = grab(500);
            fl2 = IMPISP_FLIP_H_MODE; rs = IMP_ISP_Tuning_SetHVFLIP(IMPVI_MAIN, &fl2);
            usleep(1500000);
            fl2 = (IMPISPHVFLIP)99; rg1 = IMP_ISP_Tuning_GetHVFlip(IMPVI_MAIN, &fl2);
            ok = !rg0 && !rs && !rg1 && fl2 == IMPISP_FLIP_H_MODE;
            R("ISP_Tuning_GetHVFlip", OK(!rg0 && !rg1), "get(default) rc=%d value=%d; after set H get rc=%d value=%d", rg0, fl, rg1, fl2);
            /* mirror check: compare left/right half luma before/after */
            {
                IMPFrameInfo *f1 = NULL; double lb = 0, rb = 0, la = 0, ra = 0; unsigned x, y, w, h, n = 0; const uint8_t *Y;
                /* current = mirrored (fresh frame, see drain_stale) */
                drain_stale();
                if (IMP_FrameSource_GetFrame(CH, &f1) == 0 && f1) {
                    w = f1->width; h = f1->height; Y = (const uint8_t *)(uintptr_t)f1->virAddr;
                    for (y = 0; y < h; y += 8) for (x = 0; x < w / 2; x += 8) { la += Y[y * w + x]; ra += Y[y * w + w - 1 - x]; n++; }
                    la /= n; ra /= n;
                    IMP_ISP_Tuning_GetHVFlip(IMPVI_MAIN, &fl2);
                    IMP_FrameSource_ReleaseFrame(CH, f1);
                }
                fl2 = IMPISP_FLIP_NORMAL_MODE; IMP_ISP_Tuning_SetHVFLIP(IMPVI_MAIN, &fl2);
                usleep(1500000);
                drain_stale();
                n = 0;
                if (IMP_FrameSource_GetFrame(CH, &f1) == 0 && f1) {
                    w = f1->width; h = f1->height; Y = (const uint8_t *)(uintptr_t)f1->virAddr;
                    for (y = 0; y < h; y += 8) for (x = 0; x < w / 2; x += 8) { lb += Y[y * w + x]; rb += Y[y * w + w - 1 - x]; n++; }
                    lb /= n; rb /= n;
                    IMP_FrameSource_ReleaseFrame(CH, f1);
                }
                IMP_ISP_Tuning_GetHVFlip(IMPVI_MAIN, &fl2);
                R("ISP_Tuning_Get/SetHVFLIP", OK(ok && fl2 == IMPISP_FLIP_NORMAL_MODE && ((la - ra) * (lb - rb) < 0 || (fabs_d(la - ra) < 3 && fabs_d(lb - rb) < 3))),
                  "set H rc=%d get=%d; left/right mean Y mirrored: L=%.1f R=%.1f, normal: L=%.1f R=%.1f; restored get=%d", rs, IMPISP_FLIP_H_MODE, la, ra, lb, rb, fl2);
                (void)base;
            }
        }

        /* anti flicker */
        {
            int rg0, rs1, rg1, rs2, rg2, rs3, rg3;
            memset(&af, 0xff, sizeof af);
            rg0 = IMP_ISP_Tuning_GetAntiFlickerAttr(IMPVI_MAIN, &af);
            af2.mode = IMPISP_ANTIFLICKER_NORMAL_MODE; af2.freq = 50;
            rs1 = IMP_ISP_Tuning_SetAntiFlickerAttr(IMPVI_MAIN, &af2);
            memset(&af2, 0, sizeof af2); rg1 = IMP_ISP_Tuning_GetAntiFlickerAttr(IMPVI_MAIN, &af2);
            int m1 = af2.mode, f1 = af2.freq;
            af2.mode = IMPISP_ANTIFLICKER_AUTO_MODE; af2.freq = 60;
            rs2 = IMP_ISP_Tuning_SetAntiFlickerAttr(IMPVI_MAIN, &af2);
            memset(&af2, 0, sizeof af2); rg2 = IMP_ISP_Tuning_GetAntiFlickerAttr(IMPVI_MAIN, &af2);
            int m2 = af2.mode, f2 = af2.freq;
            rs3 = IMP_ISP_Tuning_SetAntiFlickerAttr(IMPVI_MAIN, &af);
            memset(&af2, 0, sizeof af2); rg3 = IMP_ISP_Tuning_GetAntiFlickerAttr(IMPVI_MAIN, &af2);
            R("ISP_Tuning_Get/SetAntiFlickerAttr", OK(!rg0 && !rs1 && !rg1 && !rs2 && !rg2 && !rs3 && !rg3 && m1 == IMPISP_ANTIFLICKER_NORMAL_MODE && f1 == 50 && m2 == IMPISP_ANTIFLICKER_AUTO_MODE && f2 == 60 && af2.mode == af.mode),
              "default mode=%d freq=%u; set NORMAL/50 -> %d/%d; set AUTO/60 -> %d/%d; restore -> %d/%u (rc %d %d %d %d %d %d %d)", af.mode, af.freq, m1, f1, m2, f2, af2.mode, af2.freq, rg0, rs1, rg1, rs2, rg2, rs3, rg3);
        }

        /* running mode day->night->day (IR cut GPIOs left alone by the library? observe) */
        {
            int rg0, rs1, rg1, rs2, rg2;
            rg0 = IMP_ISP_Tuning_GetISPRunningMode(IMPVI_MAIN, &rm);
            int m0 = rm;
            a = grab(300);
            IMPISPRunningMode n = IMPISP_RUNNING_MODE_NIGHT;
            rs1 = IMP_ISP_Tuning_SetISPRunningMode(IMPVI_MAIN, &n);
            b = grab(2500);
            rm = 9; rg1 = IMP_ISP_Tuning_GetISPRunningMode(IMPVI_MAIN, &rm); int m1 = rm;
            n = IMPISP_RUNNING_MODE_DAY;
            rs2 = IMP_ISP_Tuning_SetISPRunningMode(IMPVI_MAIN, &n);
            c = grab(2500);
            rm = 9; rg2 = IMP_ISP_Tuning_GetISPRunningMode(IMPVI_MAIN, &rm);
            R("ISP_Tuning_Get/SetISPRunningMode", OK(!rg0 && !rs1 && !rg1 && !rs2 && !rg2 && m1 == IMPISP_RUNNING_MODE_NIGHT && rm == IMPISP_RUNNING_MODE_DAY),
              "default=%d; set NIGHT rc=%d get=%d (chroma day=%.1f night=%.1f then day again=%.1f, Y %.1f/%.1f/%.1f); set DAY get=%d", m0, rs1, m1, a.chroma, b.chroma, c.chroma, a.y, b.y, c.y, rm);
        }

        /* bypass: stored flag only (OEM T40 behaviour) */
        {
            IMPISPTuningOpsMode e1 = IMPISP_TUNING_OPS_MODE_ENABLE, e0 = IMPISP_TUNING_OPS_MODE_DISABLE, g = 9;
            int rg0 = IMP_ISP_Tuning_GetISPBypass(IMPVI_MAIN, &bp);
            int rs = IMP_ISP_Tuning_SetISPBypass(IMPVI_MAIN, &e1);
            int rg1 = IMP_ISP_Tuning_GetISPBypass(IMPVI_MAIN, &g);
            b = grab(1000);
            int rs0 = IMP_ISP_Tuning_SetISPBypass(IMPVI_MAIN, &e0);
            R("ISP_Tuning_SetISPBypass", (!rg0 && !rs && !rg1 && g == 1 && !rs0 && b.ok) ? "PART" : "FAIL", "default=%d, set 1 rc=%d, get=%d; stored flag only, picture unchanged (Y=%.1f vs %.1f); set 0 rc=%d", bp, rs, g, b.y, a.y, rs0);
        }

        /* video drop callback */
        {
            int rs = IMP_ISP_Tuning_SetVideoDrop(on_drop);
            stream_for(2.0, &e);
            int rs0 = IMP_ISP_Tuning_SetVideoDrop(NULL);
            R("ISP_Tuning_SetVideoDrop", (!rs && !rs0 && e.frames > 30) ? "PART" : "FAIL", "set cb rc=%d, 2 s streaming %u frames, set NULL rc=%d; callback firing on real video loss not triggered", rs, e.frames, rs0);
        }
    }

    /* ===== phase 8: statistics and AWB ===== */
    if (mask & 8) {
        static IMPISPAEStatisInfo ae;
        static IMPISPAWBStatisInfo aw;
        IMPISPAWBGlobalStatisInfo ag;
        IMPISPWBAttr wb; IMPISPWeight w1, w2;
        int rr;
        unsigned i2, nz = 0;
        unsigned long long sum = 0;

        memset(&ae, 0, sizeof ae);
        rr = IMP_ISP_Tuning_GetAeStatistics(IMPVI_MAIN, &ae);
        for (i2 = 0; i2 < 256; i2++) { sum += ae.ae_hist_256bin[i2]; if (ae.ae_hist_256bin[i2]) nz++; }
        R("ISP_Tuning_GetAeStatistics", OK(!rr && sum > 0 && nz > 5), "rc=%d hist256 sum=%llu nonzero bins=%u hist5=%u,%u,%u,%u,%u zone[7][7]=%u", rr, sum, nz, ae.ae_hist_5bin[0], ae.ae_hist_5bin[1], ae.ae_hist_5bin[2], ae.ae_hist_5bin[3], ae.ae_hist_5bin[4], ae.ae_statis.statis[7][7]);
        memset(&aw, 0, sizeof aw);
        rr = IMP_ISP_Tuning_GetAwbStatistics(IMPVI_MAIN, &aw);
        R("ISP_Tuning_GetAwbStatistics", OK(!rr && (aw.awb_r.statis[7][7] || aw.awb_g.statis[7][7])), "rc=%d zone[7][7] r=%u g=%u b=%u", rr, aw.awb_r.statis[7][7], aw.awb_g.statis[7][7], aw.awb_b.statis[7][7]);
        memset(&ag, 0, sizeof ag);
        rr = IMP_ISP_Tuning_GetAwbGlobalStatistics(IMPVI_MAIN, &ag);
        R("ISP_Tuning_GetAwbGlobalStatistics", OK(!rr && ag.statis_gol_gain.rgain && ag.statis_gol_gain.bgain), "rc=%d weight gain r=%u b=%u; global gain r=%u b=%u", rr, ag.statis_weight_gain.rgain, ag.statis_weight_gain.bgain, ag.statis_gol_gain.rgain, ag.statis_gol_gain.bgain);

        /* AWB attr: manual gains shift the colour balance */
        memset(&wb, 0, sizeof wb);
        rr = IMP_ISP_Tuning_GetAwbAttr(IMPVI_MAIN, &wb);
        printf("  awb get rc=%d mode=%d gain r=%u b=%u frz=%d ct=%u\n", rr, wb.mode, wb.gain_val.rgain, wb.gain_val.bgain, wb.awb_frz, wb.ct);
        {
            struct pm p0, p1; int rs, rg2; IMPISPWBAttr m = wb, back;
            p0 = grab(500);
            m.mode = ISP_CORE_WB_MODE_MANUAL;
            m.gain_val.rgain = ag.statis_gol_gain.rgain * 3 / 2; m.gain_val.bgain = ag.statis_gol_gain.bgain * 2 / 3;
            rs = IMP_ISP_Tuning_SetAwbAttr(IMPVI_MAIN, &m);
            p1 = grab(2000);
            memset(&back, 0, sizeof back);
            rg2 = IMP_ISP_Tuning_GetAwbAttr(IMPVI_MAIN, &back);
            R("ISP_Tuning_Get/SetAwbAttr", OK(!rr && !rs && !rg2 && back.mode == ISP_CORE_WB_MODE_MANUAL && (p1.v - p0.v) > 2),
              "get rc=%d mode=%d; set MANUAL r*1.5 b/1.5 rc=%d readback mode=%d gain r=%u b=%u; meanV %.1f -> %.1f meanU %.1f -> %.1f", rr, wb.mode, rs, back.mode, back.gain_val.rgain, back.gain_val.bgain, p0.v, p1.v, p0.u, p1.u);
            rs = IMP_ISP_Tuning_SetAwbAttr(IMPVI_MAIN, &wb);
            usleep(1500000);
            memset(&back, 0, sizeof back);
            IMP_ISP_Tuning_GetAwbAttr(IMPVI_MAIN, &back);
            printf("  awb restored mode=%d rc=%d\n", back.mode, rs);
        }
        memset(&w1, 0, sizeof w1);
        rr = IMP_ISP_Tuning_GetAwbWeight(IMPVI_MAIN, &w1);
        {
            int rs, rg2; struct pm p0, p1; unsigned x, y;
            memset(&w2, 0, sizeof w2);
            for (y = 0; y < 15; y++) for (x = 0; x < 15; x++) w2.weight[y][x] = (x < 7) ? 8 : 0;
            p0 = grab(300);
            rs = IMP_ISP_Tuning_SetAwbWeight(IMPVI_MAIN, &w2);
            usleep(1500000);
            p1 = grab(500);
            memset(&w2, 0xff, sizeof w2);
            rg2 = IMP_ISP_Tuning_GetAwbWeight(IMPVI_MAIN, &w2);
            R("ISP_Tuning_Get/SetAwbWeight", OK(!rr && !rs && !rg2 && w2.weight[3][2] == 8 && w2.weight[3][12] == 0), "get rc=%d w[7][7]=%u; set left-half-only rc=%d readback w[3][2]=%u w[3][12]=%u (colour U %.1f->%.1f V %.1f->%.1f, scene dependent)", rr, w1.weight[7][7], rs, w2.weight[3][2], w2.weight[3][12], p0.u, p1.u, p0.v, p1.v);
            IMP_ISP_Tuning_SetAwbWeight(IMPVI_MAIN, &w1);
        }
    }

    /* ===== phase 10: system ===== */
    if (mask & 0x10) {
        int64_t t1, t2; int rr; IMPVersion v;
        const char *cpu = IMP_System_GetCPUInfo();
        R("System_GetCPUInfo", OK(cpu && *cpu), "\"%s\"", cpu ? cpu : "(null)");
        memset(&v, 0, sizeof v);
        rr = IMP_System_GetVersion(&v);
        R("System_GetVersion", OK(!rr && v.aVersion[0]), "rc=%d \"%s\"", rr, v.aVersion);
        t1 = IMP_System_GetTimeStamp(); t = now_s(); usleep(1000000); t2 = IMP_System_GetTimeStamp();
        R("System_GetTimeStamp", OK(t2 - t1 > 950000 && t2 - t1 < 1100000), "%lld us over 1.0 s wall (now %lld)", (long long)(t2 - t1), (long long)t2);
        rr = IMP_System_RebaseTimeStamp(0);
        t2 = IMP_System_GetTimeStamp();
        R("System_RebaseTimeStamp", OK(!rr && t2 >= 0 && t2 < 2000000), "rc=%d, timestamp after rebase(0) = %lld us", rr, (long long)t2);
        {
            uint32_t v0 = IMP_System_ReadReg32(0x10000000), v1 = IMP_System_ReadReg32(0x10000000);
            R("System_ReadReg32", OK(v0 == v1 && v0 != 0 && v0 != 0xffffffffu), "CPM 0x10000000 = 0x%08x (twice 0x%08x)", v0, v1);
        }
        printf("  (System_WriteReg32 not exercised: no harmless writable register known)\n");
    }

    /* ===== phase 20: OSD pool ===== */
    if (mask & 0x20) {
        int rp = IMP_OSD_SetPoolSize(512 * 1024);
        IMPOSDRgnAttr ra;
        memset(&ra, 0, sizeof ra);
        ra.type = OSD_REG_RECT;
        ra.rect.p0.x = 100; ra.rect.p0.y = 100; ra.rect.p1.x = 299; ra.rect.p1.y = 199;
        ra.fmt = PIX_FMT_BGRA;
        ra.data.lineRectData.color = OSD_RED; ra.data.lineRectData.linewidth = 4;
        {
            int g = IMP_OSD_CreateGroup(0);
            IMPRgnHandle h = IMP_OSD_CreateRgn(NULL);
            int rr = -1, rs = -1;
            if (h != INVHANDLE) { }
            h = IMP_OSD_CreateRgn(&ra);
            if (h != INVHANDLE) {
                IMPOSDGrpRgnAttr gr; memset(&gr, 0, sizeof gr);
                gr.show = 1; gr.layer = 1; gr.gAlphaEn = 1; gr.fgAlhpa = 255;
                rr = IMP_OSD_RegisterRgn(h, 0, &gr);
                rs = IMP_OSD_ShowRgn(h, 0, 1);
                IMP_OSD_ShowRgn(h, 0, 0);
                IMP_OSD_UnRegisterRgn(h, 0);
                IMP_OSD_DestroyRgn(h);
            }
            IMP_OSD_DestroyGroup(0);
            R("OSD_SetPoolSize", OK(!rp && h != INVHANDLE && !rr && !rs), "SetPoolSize(512 KiB) rc=%d, then group %d, region create handle=%d register rc=%d show rc=%d", rp, g, h, rr, rs);
        }
    }

    /* ===== phase 0x100: fixes of the night 2026-10-12 (SEI, FrameRelease,
     * DrawAttr, Query fields); run with OpenIMP and with the vendor library ===== */
    if (mask & 0x100) {
        FILE *dump = NULL;
        const char *dp = getenv("T40D_DUMP");
        if (dp && *dp) dump = fopen(dp, "wb");
        /* Encoder_Query fields (vendor: leftStreamBytes and curPacks always 0) */
        {
            IMPEncoderChnStat q; int rq;
            stream_for(1.0, &e);
            memset(&q, 0xff, sizeof q);
            rq = IMP_Encoder_PollingStream(CH, 1000);
            r = IMP_Encoder_Query(CH, &q);
            R("Encoder_Query(fields)", OK(!rq && !r && q.registered && q.leftStreamFrames >= 1 && q.curPacks == 0 && q.leftStreamBytes == 0),
              "after PollingStream, before GetStream: rc=%d registered=%d leftPics=%u leftBytes=%u leftFrames=%u packs=%u done=%u",
              r, q.registered, q.leftPics, q.leftStreamBytes, q.leftStreamFrames, q.curPacks, q.work_done);
            stream_for(0.5, &e);
        }
        /* SetFrameRelease: rc only, stream must go on */
        {
            int r1 = IMP_Encoder_SetFrameRelease(CH, 1, 2), r2 = IMP_Encoder_SetFrameRelease(31, 1, 2);
            stream_for(1.0, &e);
            R("Encoder_SetFrameRelease", (!r1 && r2 && e.frames > 10) ? "PART" : "FAIL",
              "set 1/2 rc=%d, invalid channel rc=%d, stream continues %u frames/1 s; no visible effect by design", r1, r2, e.frames);
        }
        /* SEI: ALL, then INTRA (with IDR requests), then NONE */
        {
            IMPEncoderSeiAttr sa, sg; struct seiscan zall, zintra, znone;
            static uint8_t ud[200];
            int rs1, rs2, rs3, rg, rbad;
            for (i = 0; i < 200; i++) ud[i] = (uint8_t)(i % 7 == 0 ? 0 : i);   /* zeros: EPB path */
            memset(&sa, 0, sizeof sa);
            sa.SeiMode = IMP_ENC_SEI_MODE_ALL; sa.uuid = (uint8_t *)sei_uuid; sa.userData = ud; sa.userDataSize = 100;
            rs1 = IMP_Encoder_SetChnSeiAttr(CH, &sa);
            memset(&sg, 0, sizeof sg);
            rg = IMP_Encoder_GetChnSeiAttr(CH, &sg);
            stream_sei(3.0, &zall, dump);
            sa.SeiMode = IMP_ENC_SEI_MODE_INTRA;
            rs2 = IMP_Encoder_SetChnSeiAttr(CH, &sa);
            IMP_Encoder_RequestIDR(CH);
            stream_sei(2.0, &zintra, dump);
            sa.SeiMode = IMP_ENC_SEI_MODE_NONE;
            rs3 = IMP_Encoder_SetChnSeiAttr(CH, &sa);
            stream_sei(1.5, &znone, dump);
            sa.SeiMode = IMP_ENC_SEI_MODE_ALL; sa.uuid = NULL;
            rbad = IMP_Encoder_SetChnSeiAttr(CH, &sa);
            R("Encoder_Get/SetChnSeiAttr", OK(!rs1 && !rs2 && !rs3 && !rg && rbad && sg.SeiMode == IMP_ENC_SEI_MODE_ALL && sg.userDataSize == 100 &&
                                               /* up to 3 pictures encoded before a change may still
                                                * come out (the vendor library queues pictures) */
                                               zall.frames > 20 && zall.sei + 3 >= zall.frames && !zall.sei_bad &&
                                               zintra.idr >= 1 && zintra.sei - zintra.sei_idr <= 3 && zintra.sei_idr == zintra.idr &&
                                               znone.frames > 10 && znone.sei <= 3),
              "set rc=%d/%d/%d get rc=%d mode=%d size=%u; ALL: SEI in %u of %u frames (bad %u); INTRA: SEI in %u frames, %u IDR, %u on IDR; NONE: %u of %u; uuid NULL rc=%d",
              rs1, rs2, rs3, rg, sg.SeiMode, sg.userDataSize, zall.sei, zall.frames, zall.sei_bad, zintra.sei, zintra.idr, zintra.sei_idr, znone.sei, znone.frames, rbad);
        }
        if (dump) { fclose(dump); printf("  stream dumped to %s (H.264 Annex B, decode-check on the host)\n", dp); }
        /* ISP DrawAttr: a green window (RGB) in ISP output channel 1 (this
         * 1280x720 channel), 4-pixel line; compare the top edge rows */
        {
            static IMPISPDrawAttr da, dg;
            IMPISPDrawUnitAttr *u = &da.draw_chx[1][0];
            double y0, u0, v0, y1, u1, v1, y2, u2, v2;
            int rs, rgd, rs0, m0, m1, m2;
            m0 = rect_mean(200, 100, 400, 4, &y0, &u0, &v0);
            memset(&da, 0, sizeof da);
            u->type = IMP_ISP_DRAW_WIND; u->color_type = IMPISP_MASK_TYPE_RGB;
            u->draw_cfg.wind.wind_enable = 1;
            u->draw_cfg.wind.wind_left = 200; u->draw_cfg.wind.wind_top = 100;
            u->draw_cfg.wind.wind_width = 400; u->draw_cfg.wind.wind_height = 200;
            u->draw_cfg.wind.wind_color.mask_argb.g_value = 255;
            u->draw_cfg.wind.wind_line_width = 4; u->draw_cfg.wind.wind_alpha = 7;
            rs = IMP_ISP_SetDrawAttr(IMPVI_MAIN, &da);
            usleep(500000);
            m1 = rect_mean(200, 100, 400, 4, &y1, &u1, &v1);
            memset(&dg, 0, sizeof dg);
            rgd = IMP_ISP_GetDrawAttr(IMPVI_MAIN, &dg);
            u->draw_cfg.wind.wind_enable = 0;
            rs0 = IMP_ISP_SetDrawAttr(IMPVI_MAIN, &da);
            usleep(500000);
            m2 = rect_mean(200, 100, 400, 4, &y2, &u2, &v2);
            printf("  draw: yuv written by the library for green: %u/%u/%u\n", da.draw_chx[1][0].draw_cfg.wind.wind_color.mask_ayuv.y_value,
                   da.draw_chx[1][0].draw_cfg.wind.wind_color.mask_ayuv.u_value, da.draw_chx[1][0].draw_cfg.wind.wind_color.mask_ayuv.v_value);
            R("ISP_Get/SetDrawAttr", OK(!rs && !rgd && !rs0 && !m0 && !m1 && !m2 && dg.draw_chx[1][0].draw_cfg.wind.wind_color.mask_argb.g_value == 255 &&
                                         (fabs_d(u1 - u0) + fabs_d(v1 - v0) > 20) && (fabs_d(u2 - u0) + fabs_d(v2 - v0) < 10)),
              "set rc=%d get rc=%d (G=%u type=%d) off rc=%d; top edge Y/U/V before %.1f/%.1f/%.1f, drawn %.1f/%.1f/%.1f, off %.1f/%.1f/%.1f",
              rs, rgd, dg.draw_chx[1][0].draw_cfg.wind.wind_color.mask_argb.g_value, dg.draw_chx[1][0].color_type, rs0, y0, u0, v0, y1, u1, v1, y2, u2, v2);
        }
    }

    if (mask & 0x200) {
        IMPISPRunningMode m = (IMPISPRunningMode)9, d = IMPISP_RUNNING_MODE_DAY;
        int rg;
        a = grab(1500);
        rg = IMP_ISP_Tuning_GetISPRunningMode(IMPVI_MAIN, &m);
        IMP_ISP_Tuning_SetISPRunningMode(IMPVI_MAIN, &d);
        b = grab(2500);
        R("ISP_StartNightMode", (!night_rc && a.ok) ? "PART" : "FAIL",
          "rc=%d before AddSensor; first pictures: chroma=%.1f Y=%.1f, running mode get rc=%d mode=%d; after set DAY chroma=%.1f (night = low chroma)",
          night_rc, a.chroma, a.y, rg, m, b.chroma);
    }


    /* ===== phase 0x4000: T40 wip cells (#5 #6 #9 #13 #14 #16 #19 #43), 2026-10-24 ===== */
    if (mask & 0x4000) {
        usleep(2000000);
        /* ---- #43 getters: rc and values ---- */
        {
            IMPISPAEExprInfo ei; IMPISPAEWeightAttr aw; IMPISPWeight w; IMPISPFrameDropAttr fd; IMPISPAeSpeedAttr sp;
            IMPISPSENSORAttr sa; IMPISPAWBStatisInfo ws; IMPISPAEStatisInfo as; IMPISPWBAttr wb; IMPISPStatisConfig sc;
            IMPISPGammaAttr ga; IMPISPCSCAttr cs; uint32_t n = 0, d = 0, reg = 0x3107, val = 0;
            int rc[16], k = 0;
            memset(&ei, 0, sizeof ei); memset(&aw, 0, sizeof aw); memset(&w, 0, sizeof w); memset(&fd, 0, sizeof fd);
            memset(&sa, 0, sizeof sa); memset(&ws, 0, sizeof ws); memset(&as, 0, sizeof as); memset(&wb, 0, sizeof wb);
            memset(&sp, 0, sizeof sp); memset(&sc, 0, sizeof sc); memset(&ga, 0, sizeof ga); memset(&cs, 0, sizeof cs);
            printf("  G%d\n", k), rc[k++] = IMP_ISP_Tuning_GetAeExprInfo(IMPVI_MAIN, &ei);
            printf("  G%d\n", k), rc[k++] = IMP_ISP_Tuning_GetAeWeight(IMPVI_MAIN, &aw);
            printf("  G%d\n", k), rc[k++] = IMP_ISP_Tuning_GetAfWeight(IMPVI_MAIN, &w);
            printf("  G%d\n", k), rc[k++] = IMP_ISP_Tuning_GetSensorFPS(IMPVI_MAIN, &n, &d);
            printf("  G%d\n", k), rc[k++] = IMP_ISP_GetFrameDrop(IMPVI_MAIN, &fd);
            printf("  G%d\n", k), rc[k++] = IMP_ISP_GetSensorRegister(IMPVI_MAIN, &reg, &val);
            printf("  G%d\n", k), rc[k++] = IMP_ISP_Tuning_GetSensorAttr(IMPVI_MAIN, &sa);
            printf("  G%d\n", k), rc[k++] = IMP_ISP_Tuning_GetAwbStatistics(IMPVI_MAIN, &ws);
            printf("  G%d\n", k), rc[k++] = IMP_ISP_Tuning_GetAeStatistics(IMPVI_MAIN, &as);
            printf("  G%d\n", k), rc[k++] = IMP_ISP_Tuning_GetAwbAttr(IMPVI_MAIN, &wb);
            printf("  G%d\n", k), rc[k++] = IMP_ISP_Tuning_GetAeSpeed(IMPVI_MAIN, &sp);
            printf("  G%d\n", k), rc[k++] = IMP_ISP_Tuning_GetStatisConfig(IMPVI_MAIN, &sc);
            /* GetGammaAttr oopsed the kernel on agg43c (2026-10-24 12:25): only with mask 0x20000, on its own */
            printf("  G%d\n", k), rc[k++] = (mask & 0x20000) ? IMP_ISP_Tuning_GetGammaAttr(IMPVI_MAIN, &ga) : 0;
            printf("  G%d\n", k), rc[k++] = IMP_ISP_Tuning_GetISPCSCAttr(IMPVI_MAIN, &cs);
            printf("  W43 rc: AeExprInfo=%d AeWeight=%d AfWeight=%d SensorFPS=%d(%u/%u) FrameDrop=%d SensorReg=%d(0x%x=0x%x) SensorAttr=%d(hts %u vts %u fps %u %ux%u) AwbStat=%d AeStat=%d AwbAttr=%d AeSpeed=%d(%u/%u) StatisCfg=%d Gamma=%d(type %d) Csc=%d\n",
                   rc[0], rc[1], rc[2], rc[3], n, d, rc[4], rc[5], reg, val, rc[6], sa.hts, sa.vts, sa.fps, sa.width, sa.height,
                   rc[7], rc[8], rc[9], rc[10], sp.step_s, sp.step_d, rc[11], rc[12], ga.Curve_type, rc[13]);
            printf("  W43 AeExprInfo: IT=%u AG=%u DG=%u IspDG=%u totdB=%u EV=%u EVLog2=%u minAG=%u maxIT=%u maxAG=%u maxDG=%u maxIspDG=%u mode(IT/AG/DG/ISP)=%d/%d/%d/%d\n",
                   ei.AeIntegrationTime, ei.AeAGain, ei.AeDGain, ei.AeIspDGain, ei.TotalGainDb, ei.ExposureValue, ei.EVLog2,
                   ei.AeMinAGain, ei.AeMaxIntegrationTime, ei.AeMaxAGain, ei.AeMaxDgain, ei.AeMaxIspDGain,
                   ei.AeIntegrationTimeMode, ei.AeAGainManualMode, ei.AeDGainManualMode, ei.AeIspDGainManualMode);
            { int bad = 0, i; for (i = 0; i < 14; i++) if (rc[i]) bad++;
              R("W43_getters", bad ? "PART" : "PASS", "%d of 14 getters non-zero (see line above)", bad); }
        }

        /* ---- #5 AE scene (target comp, HLC, BLC) ---- */
        {
            IMPISPAEScenceAttr old, a, g; struct pm p0, plo, phi; int rs, rg;
            double yb, yh = 0, yl = 0, yblc = 0, yhlc = 0;
            memset(&old, 0, sizeof old); IMP_ISP_Tuning_GetAeScenceAttr(IMPVI_MAIN, &old);
            printf("  W5 default: HLC en%d/%u BLC en%d/%u TargetComp en%d/%u StartEv en%d/%u\n", old.AeHLCEn, old.AeHLCStrength, old.AeBLCEn, old.AeBLCStrength,
                   old.AeTargetCompEn, old.AeTargetComp, old.AeStartEn, old.AeStartEv);
            p0 = grab(3000); yb = p0.y; ae_line("base");
            memset(&a, 0, sizeof a); a.AeTargetCompEn = TISP_AE_SCENCE_GLOBAL_ENABLE; a.AeTargetComp = 60;
            rs = IMP_ISP_Tuning_SetAeScenceAttr(IMPVI_MAIN, &a);
            memset(&g, 0, sizeof g); rg = IMP_ISP_Tuning_GetAeScenceAttr(IMPVI_MAIN, &g);
            plo = grab(6000); yl = plo.y; ae_line("comp60");
            printf("  W5 comp 60: set rc=%d get rc=%d readback en%d comp %u Y=%.1f\n", rs, rg, g.AeTargetCompEn, g.AeTargetComp, plo.y);
            a.AeTargetComp = 200; rs = IMP_ISP_Tuning_SetAeScenceAttr(IMPVI_MAIN, &a);
            memset(&g, 0, sizeof g); rg = IMP_ISP_Tuning_GetAeScenceAttr(IMPVI_MAIN, &g);
            phi = grab(6000); yh = phi.y; ae_line("comp200");
            printf("  W5 comp 200: set rc=%d get rc=%d readback en%d comp %u Y=%.1f\n", rs, rg, g.AeTargetCompEn, g.AeTargetComp, phi.y);
            a.AeTargetComp = 128; rs = IMP_ISP_Tuning_SetAeScenceAttr(IMPVI_MAIN, &a);
            p0 = grab(6000); ae_line("comp128");
            printf("  W5 comp 128 (neutral): Y=%.1f (base %.1f)\n", p0.y, yb);
            R("W5_AeTargetComp", (yh > yl + 20 && fabs_d(p0.y - yb) < 8) ? "PASS" : (yh > yl + 5 ? "PART" : "FAIL"),
              "Y comp60=%.1f comp128=%.1f/%.1f comp200=%.1f", yl, p0.y, yb, yh);
            memset(&a, 0, sizeof a); a.AeTargetCompEn = TISP_AE_SCENCE_GLOBAL_ENABLE; a.AeTargetComp = 128;
            a.AeBLCEn = TISP_AE_SCENCE_GLOBAL_ENABLE; a.AeBLCStrength = 10;
            rs = IMP_ISP_Tuning_SetAeScenceAttr(IMPVI_MAIN, &a);
            memset(&g, 0, sizeof g); rg = IMP_ISP_Tuning_GetAeScenceAttr(IMPVI_MAIN, &g);
            p0 = grab(6000); yblc = p0.y; ae_line("blc10");
            R("W5_AeBLC", (rs == 0 && rg == 0 && g.AeBLCEn == TISP_AE_SCENCE_GLOBAL_ENABLE && g.AeBLCStrength == 10) ? (yblc > yb + 3 ? "PASS" : "PART") : "FAIL",
              "set rc=%d get rc=%d readback en%d str%u; Y=%.1f vs base %.1f (scene-dependent: raises a dark centre)", rs, rg, g.AeBLCEn, g.AeBLCStrength, yblc, yb);
            memset(&a, 0, sizeof a); a.AeTargetCompEn = TISP_AE_SCENCE_GLOBAL_ENABLE; a.AeTargetComp = 128;
            a.AeHLCEn = TISP_AE_SCENCE_GLOBAL_ENABLE; a.AeHLCStrength = 10;
            rs = IMP_ISP_Tuning_SetAeScenceAttr(IMPVI_MAIN, &a);
            memset(&g, 0, sizeof g); rg = IMP_ISP_Tuning_GetAeScenceAttr(IMPVI_MAIN, &g);
            p0 = grab(6000); yhlc = p0.y; ae_line("hlc10");
            R("W5_AeHLC", (rs == 0 && rg == 0 && g.AeHLCEn == TISP_AE_SCENCE_GLOBAL_ENABLE && g.AeHLCStrength == 10) ? (yhlc < yb - 3 ? "PASS" : "PART") : "FAIL",
              "set rc=%d get rc=%d readback en%d str%u; Y=%.1f vs base %.1f (scene-dependent: darkens bright spots)", rs, rg, g.AeHLCEn, g.AeHLCStrength, yhlc, yb);
            memset(&a, 0, sizeof a); a.AeHLCEn = old.AeHLCEn; a.AeBLCEn = old.AeBLCEn; a.AeTargetCompEn = old.AeTargetCompEn;
            a.AeTargetComp = old.AeTargetComp; a.AeHLCStrength = old.AeHLCStrength; a.AeBLCStrength = old.AeBLCStrength;
            IMP_ISP_Tuning_SetAeScenceAttr(IMPVI_MAIN, &a);
            p0 = grab(5000); ae_line("restored");
            printf("  W5 restored Y=%.1f\n", p0.y);
        }

        /* ---- #6 AE caps (IT max, gain max) and sensor FPS ---- */
        {
            IMPISPAEExprInfo e0, e1, e2; struct pm p0, p1, p2; int rs, rg; uint32_t n0 = 0, d0 = 0, n1 = 0, d1 = 0;
            memset(&e0, 0, sizeof e0);
            IMP_ISP_Tuning_GetAeExprInfo(IMPVI_MAIN, &e0);
            p0 = grab(3000); ae_line("caps-base");
            /* IT cap far below the current exposure */
            e1 = e0; e1.AeMaxIntegrationTimeMode = IMPISP_TUNING_OPS_TYPE_MANUAL; e1.AeMaxIntegrationTime = e0.AeIntegrationTime / 8 ? e0.AeIntegrationTime / 8 : 1;
            rs = IMP_ISP_Tuning_SetAeExprInfo(IMPVI_MAIN, &e1);
            memset(&e2, 0, sizeof e2); rg = IMP_ISP_Tuning_GetAeExprInfo(IMPVI_MAIN, &e2);
            p1 = grab(6000); ae_line("maxIT=base/8");
            printf("  W6 maxIT cap %u (was IT %u): set rc=%d get rc=%d readback mode%d max %u; Y %.1f -> %.1f\n", e1.AeMaxIntegrationTime, e0.AeIntegrationTime, rs, rg,
                   e2.AeMaxIntegrationTimeMode, e2.AeMaxIntegrationTime, p0.y, p1.y);
            { IMPISPAEExprInfo n2; memset(&n2, 0, sizeof n2); IMP_ISP_Tuning_GetAeExprInfo(IMPVI_MAIN, &n2);
              R("W6_AeMaxIntegrationTime", (rs == 0 && n2.AeIntegrationTime <= e1.AeMaxIntegrationTime + 2) ? "PASS" : ((rs == 0 && p1.y < p0.y - 5) ? "PART" : "FAIL"),
                "cap %u: IT now %u (was %u), AG %u->%u, Y %.1f -> %.1f", e1.AeMaxIntegrationTime, n2.AeIntegrationTime, e0.AeIntegrationTime, e0.AeAGain, n2.AeAGain, p0.y, p1.y); }
            /* gain cap on top: max analog gain at 1x (1024) */
            e1.AeMaxAGainMode = IMPISP_TUNING_OPS_TYPE_MANUAL; e1.AeMaxAGain = 1024;
            e1.AeMaxIspDGainMode = IMPISP_TUNING_OPS_TYPE_MANUAL; e1.AeMaxIspDGain = 1024;
            e1.AeMaxDgainMode = IMPISP_TUNING_OPS_TYPE_MANUAL; e1.AeMaxDgain = 1024;
            rs = IMP_ISP_Tuning_SetAeExprInfo(IMPVI_MAIN, &e1);
            p2 = grab(6000); ae_line("maxIT/8+gain1x");
            { IMPISPAEExprInfo n2; memset(&n2, 0, sizeof n2); IMP_ISP_Tuning_GetAeExprInfo(IMPVI_MAIN, &n2);
              R("W6_AeMaxGain", (rs == 0 && n2.AeAGain <= 1024 + 64 && n2.AeIspDGain <= 1024 + 64 && p2.y < p1.y + 2) ? ((p2.y < p1.y - 3) ? "PASS" : "PART") : "FAIL",
                "gain caps 1x: set rc=%d AG %u IspDG %u DG %u totdB %u; Y %.1f -> %.1f", rs, n2.AeAGain, n2.AeIspDGain, n2.AeDGain, n2.TotalGainDb, p1.y, p2.y); }
            /* restore */
            e0.AeMaxIntegrationTimeMode = IMPISP_TUNING_OPS_TYPE_AUTO; e0.AeMaxAGainMode = IMPISP_TUNING_OPS_TYPE_AUTO;
            e0.AeMaxIspDGainMode = IMPISP_TUNING_OPS_TYPE_AUTO; e0.AeMaxDgainMode = IMPISP_TUNING_OPS_TYPE_AUTO;
            e1 = e0; rs = IMP_ISP_Tuning_SetAeExprInfo(IMPVI_MAIN, &e1);
            p0 = grab(6000); ae_line("caps-restored");
            printf("  W6 caps restored rc=%d Y=%.1f\n", rs, p0.y);
            /* sensor FPS */
            rg = IMP_ISP_Tuning_GetSensorFPS(IMPVI_MAIN, &n0, &d0);
            { struct es e5, e6; uint32_t sn = 12, sd = 1; int rs2, rg2;
              stream_for(2.0, &e5);
              rs2 = IMP_ISP_Tuning_SetSensorFPS(IMPVI_MAIN, &sn, &sd);
              usleep(1500000);
              rg2 = IMP_ISP_Tuning_GetSensorFPS(IMPVI_MAIN, &n1, &d1);
              stream_for(4.0, &e6);
              { IMPISPSENSORAttr sa2; memset(&sa2, 0, sizeof sa2); IMP_ISP_Tuning_GetSensorAttr(IMPVI_MAIN, &sa2);
                printf("  W6 SensorFPS: before %u/%u get rc=%d; set 12/1 rc=%d, get rc=%d %u/%u; SensorAttr fps %u vts %u; stream fps before %.1f, at 12 -> %.1f\n",
                       n0, d0, rg, rs2, rg2, n1, d1, sa2.fps, sa2.vts, e5.frames / 2.0, e6.frames / 4.0); }
              R("W6_SensorFPS", (!rs2 && !rg2 && n1 == 12 && e6.frames / 4.0 < 13.5 && e6.frames / 4.0 > 10.5) ? "PASS" : (!rs2 && !rg2 && n1 == 12 ? "PART" : "FAIL"),
                "get %u/%u -> set 12/1 -> get %u/%u; delivered %.1f fps (was %.1f)", n0, d0, n1, d1, e6.frames / 4.0, e5.frames / 2.0);
              rs2 = IMP_ISP_Tuning_SetSensorFPS(IMPVI_MAIN, &n0, &d0); usleep(1500000);
              stream_for(2.0, &e5); printf("  W6 SensorFPS restored rc=%d, delivered %.1f fps\n", rs2, e5.frames / 2.0); }
        }

        /* ---- #9 CCM + LSC (module bypass) ---- */
        {
            IMPISPCCMAttr c0, c1, c2; IMPISPModuleCtl m0, m1; struct pm pa, pb; struct thumb ta, tb; int rg0, rs, rg1, rm0, rm1, rm2;
            double corner_a, corner_b, centre_a, centre_b;
            memset(&c0, 0, sizeof c0); rg0 = IMP_ISP_Tuning_GetCCMAttr(IMPVI_MAIN, &c0);
            printf("  W9 CCM default rc=%d manual=%d sat=%d matrix %.3f %.3f %.3f / %.3f %.3f %.3f / %.3f %.3f %.3f\n", rg0, c0.ManualEn, c0.SatEn,
                   c0.ColorMatrix[0], c0.ColorMatrix[1], c0.ColorMatrix[2], c0.ColorMatrix[3], c0.ColorMatrix[4], c0.ColorMatrix[5], c0.ColorMatrix[6], c0.ColorMatrix[7], c0.ColorMatrix[8]);
            pa = grab(2500);
            c1 = c0; c1.ManualEn = IMPISP_TUNING_OPS_MODE_ENABLE;
            { static const float id[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 }; memcpy(c1.ColorMatrix, id, sizeof id); }
            rs = IMP_ISP_Tuning_SetCCMAttr(IMPVI_MAIN, &c1);
            memset(&c2, 0, sizeof c2); rg1 = IMP_ISP_Tuning_GetCCMAttr(IMPVI_MAIN, &c2);
            pb = grab(3000);
            printf("  W9 CCM manual identity: set rc=%d get rc=%d manual=%d matrix %.3f %.3f %.3f / %.3f %.3f %.3f / %.3f %.3f %.3f; chroma %.1f -> %.1f U/V %.1f/%.1f -> %.1f/%.1f\n", rs, rg1, c2.ManualEn,
                   c2.ColorMatrix[0], c2.ColorMatrix[1], c2.ColorMatrix[2], c2.ColorMatrix[3], c2.ColorMatrix[4], c2.ColorMatrix[5], c2.ColorMatrix[6], c2.ColorMatrix[7], c2.ColorMatrix[8],
                   pa.chroma, pb.chroma, pa.u, pa.v, pb.u, pb.v);
            { double sc1 = pb.chroma;
              static const float rb[9] = { 1.6f, -0.3f, -0.3f, -0.3f, 1.0f, 0.3f, -0.3f, 0.3f, 1.0f };
              struct pm pc; memcpy(c1.ColorMatrix, rb, sizeof rb);
              rs = IMP_ISP_Tuning_SetCCMAttr(IMPVI_MAIN, &c1); pc = grab(3000);
              printf("  W9 CCM manual red-boost: set rc=%d U/V %.1f/%.1f chroma %.1f (identity chroma %.1f, default chroma %.1f)\n", rs, pc.u, pc.v, pc.chroma, sc1, pa.chroma);
              R("W9_CCM", (!rg0 && !rs && !rg1 && c2.ManualEn == 1 && (fabs_d(pc.v - pb.v) + fabs_d(pc.u - pb.u)) > 2.0) ? "PASS" : (!rs ? "PART" : "FAIL"),
                "Get dev; Set manual rc=%d readback manual=%d m00=%.2f; red-boost vs identity dU=%.1f dV=%.1f", rs, c2.ManualEn, c2.ColorMatrix[0], pc.u - pb.u, pc.v - pb.v); }
            rs = IMP_ISP_Tuning_SetCCMAttr(IMPVI_MAIN, &c0);
            pb = grab(3000);
            printf("  W9 CCM restored rc=%d U/V %.1f/%.1f (default %.1f/%.1f)\n", rs, pb.u, pb.v, pa.u, pa.v);
            /* LSC through the module-bypass key */
            memset(&m0, 0, sizeof m0); rm0 = IMP_ISP_Tuning_GetModuleControl(IMPVI_MAIN, &m0);
            ta = thumb_grab(1500);
            m1 = m0; m1.bitBypassLSC = 1;
            rm1 = IMP_ISP_Tuning_SetModuleControl(IMPVI_MAIN, &m1);
            tb = thumb_grab(2500);
            memset(&m1, 0, sizeof m1); rm2 = IMP_ISP_Tuning_GetModuleControl(IMPVI_MAIN, &m1);
            IMP_ISP_Tuning_SetModuleControl(IMPVI_MAIN, &m0);
            corner_a = (ta.v[0] + ta.v[TW - 1] + ta.v[(TH - 1) * TW] + ta.v[TH * TW - 1]) / 4;
            corner_b = (tb.v[0] + tb.v[TW - 1] + tb.v[(TH - 1) * TW] + tb.v[TH * TW - 1]) / 4;
            centre_a = ta.v[4 * TW + 7] ; centre_b = tb.v[4 * TW + 7];
            R("W9_LSC_bypass", (!rm0 && !rm1 && !rm2 && m1.bitBypassLSC == 1) ? ((fabs_d((corner_a / centre_a) - (corner_b / centre_b)) > 0.02) ? "PASS" : "PART") : "FAIL",
              "ModuleControl get key=0x%x, set bypass LSC rc=%d readback key=0x%x; corner/centre luma %.3f (LSC on) -> %.3f (bypassed); restored", m0.key, rm1, m1.key,
              corner_a / centre_a, corner_b / centre_b);
        }

        /* ---- #13 flip / mirror, all nine modes ---- */
        {
            static const char *nm[9] = { "NORMAL", "H", "V", "HV", "SENSOR_H", "SENSOR_V", "SENSOR_HV", "IHSV", "SHIV" };
            static const int want[9] = { 0, 1, 2, 3, 1, 2, 3, 2, 1 };  /* visible transform: ISP+sensor combined */
            struct thumb base, t; int m; char line[400] = ""; int good = 0, tested = 0;
            IMPISPHVFLIP fl;
            fl = IMPISP_FLIP_NORMAL_MODE; IMP_ISP_Tuning_SetHVFLIP(IMPVI_MAIN, &fl);
            base = thumb_grab(2500);
            for (m = 0; m < 9; m++) {
                IMPISPHVFLIP g = (IMPISPHVFLIP)99; int rs, rg, best = 0, tr; double dm[4], bd = 1e9;
                if (m >= 4 && !(mask & 0x10000)) continue;       /* sensor modes only on request */
                fl = (IMPISPHVFLIP)m;
                rs = IMP_ISP_Tuning_SetHVFLIP(IMPVI_MAIN, &fl);
                usleep(2500000);
                rg = IMP_ISP_Tuning_GetHVFlip(IMPVI_MAIN, &g);
                t = thumb_grab(1000);
                for (tr = 0; tr < 4; tr++) { dm[tr] = thumb_diff(&base, &t, tr); if (dm[tr] < bd) { bd = dm[tr]; best = tr; } }
                tested++; if (rs == 0 && rg == 0 && g == (IMPISPHVFLIP)m && best == want[m]) good++;
                snprintf(line + strlen(line), sizeof line - strlen(line), "%s:set%d/get%d(%d) best=%d(want %d) d=%.1f/%.1f/%.1f/%.1f; ", nm[m], rs, rg, g, best, want[m], dm[0], dm[1], dm[2], dm[3]);
                printf("  W13 %s\n", line + (strlen(line) > 100 ? strlen(line) - 100 : 0));
            }
            fl = IMPISP_FLIP_NORMAL_MODE; IMP_ISP_Tuning_SetHVFLIP(IMPVI_MAIN, &fl); usleep(2500000);
            t = thumb_grab(1500);
            R("W13_HVFLIP", good == tested ? "PASS" : (good ? "PART" : "FAIL"), "%d of %d modes mirror the picture as expected; restored NORMAL diff %.1f; %s", good, tested, thumb_diff(&base, &t, 0), line);
        }

        /* ---- #14 WDR / ADR / DRC ---- */
        {
            IMPISPTuningOpsMode we = (IMPISPTuningOpsMode)9; IMPISPWdrOutputMode wm; IMPISPModuleRatioAttr mr, mg; IMPISPModuleCtl m0, m1;
            struct pm p0, p1, p2, p3; int rw0, rw1, rw2, ro0, ro1, rm, i;
            struct thumb ta, tb;
            rw0 = IMP_ISP_WDR_ENABLE_GET(IMPVI_MAIN, &we);
            p0 = grab(2500);
            { IMPISPTuningOpsMode e1 = IMPISP_TUNING_OPS_MODE_ENABLE; rw1 = IMP_ISP_WDR_ENABLE(IMPVI_MAIN, &e1); }
            p1 = grab(3000); we = (IMPISPTuningOpsMode)9; rw2 = IMP_ISP_WDR_ENABLE_GET(IMPVI_MAIN, &we);
            printf("  W14 WDR_ENABLE(1) rc=%d GET rc=%d mode=%d; Y %.1f -> %.1f sd %.1f -> %.1f\n", rw1, rw2, we, p0.y, p1.y, p0.sd, p1.sd);
            { IMPISPTuningOpsMode e0 = IMPISP_TUNING_OPS_MODE_DISABLE; IMP_ISP_WDR_ENABLE(IMPVI_MAIN, &e0); }
            p2 = grab(2500);
            R("W14_WDR_ENABLE", (!rw0 && !rw1 && !rw2 && we == IMPISP_TUNING_OPS_MODE_ENABLE && fabs_d(p1.y - p0.y) < 6) ? "PART" : "FAIL",
              "rc %d/%d/%d readback %d; linear sensor: flag only, picture Y %.1f/%.1f/%.1f", rw0, rw1, rw2, we, p0.y, p1.y, p2.y);
            for (i = 0; i < 3; i++) {
                IMPISPWdrOutputMode set = (IMPISPWdrOutputMode)i; wm = (IMPISPWdrOutputMode)9;
                ro0 = IMP_ISP_Tuning_SetWdrOutputMode(IMPVI_MAIN, &set); ro1 = IMP_ISP_Tuning_GetWdrOutputMode(IMPVI_MAIN, &wm);
                printf("  W14 WdrOutputMode set %d rc=%d get rc=%d -> %d\n", i, ro0, ro1, wm);
            }
            wm = IMPISP_WDR_OUTPUT_MODE_FUS_FRAME; IMP_ISP_Tuning_SetWdrOutputMode(IMPVI_MAIN, &wm);
            /* DRC / ADR strength via Module_Ratio index 2 */
            memset(&mr, 0, sizeof mr); rm = IMP_ISP_Tuning_GetModule_Ratio(IMPVI_MAIN, &mr);
            printf("  W14 Module_Ratio default rc=%d: SINTER en%d/%u TEMPER en%d/%u DRC en%d/%u DPC en%d/%u DEFOG en%d/%u\n", rm,
                   mr.ratio_attr[0].en, mr.ratio_attr[0].ratio, mr.ratio_attr[1].en, mr.ratio_attr[1].ratio, mr.ratio_attr[2].en, mr.ratio_attr[2].ratio,
                   mr.ratio_attr[3].en, mr.ratio_attr[3].ratio, mr.ratio_attr[4].en, mr.ratio_attr[4].ratio);
            { static const int rv[3] = { 0, 128, 255 }; double yy[3], sdv[3]; int rs[3], rb[3], ok = 1;
              IMPISPModuleRatioAttr sv = mr;
              for (i = 0; i < 3; i++) {
                  IMPISPModuleRatioAttr s = mr; s.ratio_attr[IMP_ISP_MODULE_DRC].en = IMPISP_TUNING_OPS_MODE_ENABLE; s.ratio_attr[IMP_ISP_MODULE_DRC].ratio = rv[i];
                  rs[i] = IMP_ISP_Tuning_SetModule_Ratio(IMPVI_MAIN, &s);
                  memset(&mg, 0, sizeof mg); IMP_ISP_Tuning_GetModule_Ratio(IMPVI_MAIN, &mg); rb[i] = mg.ratio_attr[IMP_ISP_MODULE_DRC].ratio;
                  p3 = grab(4000); yy[i] = p3.y; sdv[i] = p3.sd;
                  if (rs[i] || rb[i] != rv[i]) ok = 0;
              }
              IMP_ISP_Tuning_SetModule_Ratio(IMPVI_MAIN, &sv);
              R("W14_DRC_ratio", ok ? ((fabs_d(yy[2] - yy[0]) > 2.0 || fabs_d(sdv[2] - sdv[0]) > 1.0) ? "PASS" : "PART") : "FAIL",
                "ratio 0/128/255 set rc %d/%d/%d readback %d/%d/%d; Y %.1f/%.1f/%.1f sd %.1f/%.1f/%.1f", rs[0], rs[1], rs[2], rb[0], rb[1], rb[2], yy[0], yy[1], yy[2], sdv[0], sdv[1], sdv[2]); }
            /* ADR module bypass */
            memset(&m0, 0, sizeof m0); IMP_ISP_Tuning_GetModuleControl(IMPVI_MAIN, &m0);
            ta = thumb_grab(1500); m1 = m0; m1.bitBypassADR = 1; ro0 = IMP_ISP_Tuning_SetModuleControl(IMPVI_MAIN, &m1);
            tb = thumb_grab(3000); IMP_ISP_Tuning_SetModuleControl(IMPVI_MAIN, &m0);
            R("W14_ADR_bypass", !ro0 ? (thumb_diff(&ta, &tb, 0) > 0.8 ? "PASS" : "PART") : "FAIL", "bypass ADR rc=%d, mean luma %.1f -> %.1f, thumbnail diff %.2f", ro0, ta.mean, tb.mean, thumb_diff(&ta, &tb, 0));
            (void)p2;
        }

        /* ---- #16 NR strength (Sinter 2D / Temper 3D) ---- */
        {
            IMPISPModuleRatioAttr mr, mg, sv; int idx, i; static const int rv[3] = { 0, 128, 255 };
            memset(&sv, 0, sizeof sv); IMP_ISP_Tuning_GetModule_Ratio(IMPVI_MAIN, &sv);
            for (idx = 0; idx < 2; idx++) {
                double nz[3], gr[3]; int rs[3], rb[3], ok = 1;
                for (i = 0; i < 3; i++) {
                    struct pm pp;
                    mr = sv; mr.ratio_attr[idx].en = IMPISP_TUNING_OPS_MODE_ENABLE; mr.ratio_attr[idx].ratio = rv[i];
                    rs[i] = IMP_ISP_Tuning_SetModule_Ratio(IMPVI_MAIN, &mr);
                    memset(&mg, 0, sizeof mg); IMP_ISP_Tuning_GetModule_Ratio(IMPVI_MAIN, &mg); rb[i] = mg.ratio_attr[idx].ratio;
                    usleep(3000000);
                    nz[i] = noise_meas(6); pp = grab(100); gr[i] = pp.grad;
                    if (rs[i] || rb[i] != rv[i]) ok = 0;
                }
                IMP_ISP_Tuning_SetModule_Ratio(IMPVI_MAIN, &sv);
                R(idx ? "W16_TemperRatio" : "W16_SinterRatio", ok ? ((nz[0] > nz[2] + 0.05) ? "PASS" : "PART") : "FAIL",
                  "ratio 0/128/255 set rc %d/%d/%d readback %d/%d/%d; temporal noise %.3f/%.3f/%.3f; grad %.2f/%.2f/%.2f", rs[0], rs[1], rs[2], rb[0], rb[1], rb[2], nz[0], nz[1], nz[2], gr[0], gr[1], gr[2]);
            }
        }

        /* ---- #19 ISP privacy mask ---- */
        {
            IMPISPMASKAttr ma, mg; int rs, rg; double y0, y1, y2, o0, o1; unsigned i;
            memset(&ma, 0, sizeof ma);
            y0 = region_y(MX(0.3125), MY(0.278), MX(0.234), MY(0.278)); o0 = region_y(MX(0.703), MY(0.625), MX(0.156), MY(0.208));
            ma.mask_chx[CH][0].mask_en = 1; ma.mask_chx[CH][0].mask_pos_left = MX(0.3125); ma.mask_chx[CH][0].mask_pos_top = MY(0.278);
            ma.mask_chx[CH][0].mask_width = MX(0.234); ma.mask_chx[CH][0].mask_height = MY(0.278); ma.mask_chx[CH][0].mask_type = IMPISP_MASK_TYPE_RGB;
            ma.mask_chx[CH][0].mask_value.mask_argb.r_value = 0; ma.mask_chx[CH][0].mask_value.mask_argb.g_value = 0; ma.mask_chx[CH][0].mask_value.mask_argb.b_value = 0;
            rs = IMP_ISP_Tuning_SetMask(IMPVI_MAIN, &ma);
            usleep(1500000);
            memset(&mg, 0, sizeof mg); rg = IMP_ISP_Tuning_GetMask(IMPVI_MAIN, &mg);
            y1 = region_y(MX(0.3125), MY(0.278), MX(0.234), MY(0.278)); o1 = region_y(MX(0.703), MY(0.625), MX(0.156), MY(0.208));
            printf("  W19 Set rc=%d Get rc=%d readback en%d pos %u,%u size %ux%u type %d; region Y %.1f -> %.1f, outside %.1f -> %.1f\n", rs, rg, mg.mask_chx[CH][0].mask_en,
                   mg.mask_chx[CH][0].mask_pos_left, mg.mask_chx[CH][0].mask_pos_top, mg.mask_chx[CH][0].mask_width, mg.mask_chx[CH][0].mask_height, mg.mask_chx[CH][0].mask_type, y0, y1, o0, o1);
            /* second block: red cover */
            ma.mask_chx[CH][1] = ma.mask_chx[CH][0]; ma.mask_chx[CH][1].mask_pos_left = MX(0.703); ma.mask_chx[CH][1].mask_pos_top = MY(0.625); ma.mask_chx[CH][1].mask_width = MX(0.156); ma.mask_chx[CH][1].mask_height = MY(0.208);
            ma.mask_chx[CH][1].mask_value.mask_argb.r_value = 255;
            rs |= IMP_ISP_Tuning_SetMask(IMPVI_MAIN, &ma); usleep(1500000);
            { struct pm pm2 = grab(500); (void)pm2; }
            y2 = region_y(MX(0.703), MY(0.625), MX(0.156), MY(0.208));
            printf("  W19 second block (red) region Y %.1f -> %.1f\n", o1, y2);
            for (i = 0; i < 12; i++) ((IMPISP_MASK_BLOCK_PAR *)&ma)[i].mask_en = 0;
            IMP_ISP_Tuning_SetMask(IMPVI_MAIN, &ma); usleep(1500000);
            { double y3 = region_y(MX(0.3125), MY(0.278), MX(0.234), MY(0.278));
              R("W19_Mask", (!rs && !rg && mg.mask_chx[CH][0].mask_en && y1 < 30 && fabs_d(o1 - o0) < 12 && y3 > y0 * 0.6) ? "PASS" : "FAIL",
                "cover in the sub stream (ch1): region Y %.1f -> %.1f (covered), outside %.1f -> %.1f, after disable %.1f; set/get rc %d/%d", y0, y1, o0, o1, y3, rs, rg); }
        }
    }

    /* ===== phase 0x8000: encoder telemetry (#44) ===== */
    if (mask & 0x8000) {
        struct qs q; pthread_t th; struct es e2; IMPEncoderChnStat s; int r1; OpenIMPEncoderTelemetry tt; int rt = -99;
        memset(&q, 0, sizeof q); q.run = 1;
        pthread_create(&th, NULL, q_sampler, &q);
        stream_for(5.0, &e2);
        q.run = 0; pthread_join(th, NULL);
        printf("  W44 %u Query samples in 5 s, %u frames: leftPics %u/%u/%u work_done 0:%u 1:%u leftStreamFrames 0:%u 1:%u leftBytes!=0 in %u curPacks!=0 in %u\n",
               q.n, e2.frames, q.lp[0], q.lp[1], q.lp[2], q.wd[0], q.wd[1], q.sf[0], q.sf[1], q.nzb, q.nzp);
        if (OpenIMP_Encoder_GetTelemetry) {
            memset(&tt, 0, sizeof tt); rt = OpenIMP_Encoder_GetTelemetry(CH, &tt);
            printf("  W44 telemetry rc=%d frames=%llu bytes=%llu idr=%llu enc_us last/avg/max=%u/%u/%u fps=%u.%02u kbps=%u (consumer %u frames %u bytes)\n", rt,
                   (unsigned long long)tt.frames, (unsigned long long)tt.bytes, (unsigned long long)tt.idr_frames, tt.encode_us_last, tt.encode_us_avg, tt.encode_us_max,
                   tt.fps_x100 / 100, tt.fps_x100 % 100, tt.avg_kbps, e2.frames, e2.bytes);
        }
        { /* polled, not fetched; held; evalinfo; avebitrate */
            IMPEncoderStream st2; memset(&st2, 0, sizeof st2);
            if (IMP_Encoder_PollingStream(CH, 1000) == 0) {
                memset(&s, 0xff, sizeof s); r1 = IMP_Encoder_Query(CH, &s);
                printf("  W44 polled-not-fetched: rc=%d pics=%u frames=%u bytes=%u packs=%u done=%u\n", r1, s.leftPics, s.leftStreamFrames, s.leftStreamBytes, s.curPacks, s.work_done);
                if (IMP_Encoder_GetStream(CH, &st2, 1) == 0) {
                    int br = -1, ra = -1, k2; char ev[512];
                    memset(&s, 0xff, sizeof s); IMP_Encoder_Query(CH, &s);
                    printf("  W44 held: pics=%u frames=%u bytes=%u packs=%u done=%u\n", s.leftPics, s.leftStreamFrames, s.leftStreamBytes, s.curPacks, s.work_done);
                    ra = -77; if (IMP_Encoder_GetChnAveBitrate) for (k2 = 0; k2 < 8; k2++) ra = IMP_Encoder_GetChnAveBitrate(CH, &st2, 5, &br);
                    memset(ev, 0, sizeof ev);
                    printf("  W44 AveBitrate rc=%d %d kbps (one stream x8 calls, window 5); EvalInfo rc=%d\n", ra, br, IMP_Encoder_GetChnEvalInfo ? IMP_Encoder_GetChnEvalInfo(CH, ev) : -77);
                    IMP_Encoder_ReleaseStream(CH, &st2);
                }
            }
        }
        R("W44_Query", (q.n > 100 && q.wd[0] > 0 && q.wd[1] > 0) ? "PASS" : "PART", "work_done 0 in %u of %u samples (encoder busy share), leftPics>0 in %u", q.wd[0], q.n, q.n - q.lp[0]);
        R("W44_Telemetry", (!rt && tt.frames > 50) ? "PASS" : "FAIL", "OpenIMP_Encoder_GetTelemetry rc=%d frames %llu vs consumer %u", rt, (unsigned long long)tt.frames, e2.frames);
    }

    if (mask & 0x800) {
        /* microphone only (no playback): AI digital gain 100 vs 220 */
        IMPAudioIOAttr aa; IMPAudioIChnParam cp; int re, rc, r1, r2, rg, g = -1; unsigned f1, f2; double lo, hi;
        memset(&aa, 0, sizeof aa);
        aa.samplerate = AUDIO_SAMPLE_RATE_16000; aa.bitwidth = AUDIO_BIT_WIDTH_16; aa.soundmode = AUDIO_SOUND_MODE_MONO;
        aa.frmNum = 20; aa.numPerFrm = 640; aa.chnCnt = 1;
        memset(&cp, 0, sizeof cp); cp.usrFrmDepth = 20;
        IMP_AI_SetPubAttr(0, &aa);
        re = IMP_AI_Enable(0);
        IMP_AI_SetChnParam(0, 0, &cp);
        rc = IMP_AI_EnableChn(0, 0);
        r1 = IMP_AI_SetDigitalGain(0, 0, 100);
        mic_rms(0.5, &f1);
        lo = mic_rms(1.5, &f1);
        r2 = IMP_AI_SetDigitalGain(0, 0, 220);
        mic_rms(0.5, &f2);
        hi = mic_rms(1.5, &f2);
        rg = IMP_AI_GetDigitalGain(0, 0, &g);
        IMP_AI_DisableChn(0, 0);
        IMP_AI_Disable(0);
        R("AI_Get/SetDigitalGain", OK(!re && !rc && !r1 && !r2 && !rg && g == 220 && f1 > 10 && f2 > 10 && hi > lo * 4),
          "enable rc=%d/%d; dgain 100: RMS %.1f (%u frames); dgain 220: RMS %.1f (%u frames); get rc=%d %d (60 steps = 30 dB, expect x30)",
          re, rc, lo, f1, hi, f2, rg, g);
    }

    if (mask & 0x2000) {
        /* one 8-bit raw frame of the sensor size (rmem: about 8 MB at 4K) */
        IMPISPSENSORAttr sa; IMPISPGETRawiConfig rc; int rs, rr; double mean = 0; unsigned nz = 0, k;
        memset(&sa, 0, sizeof sa); memset(&rc, 0, sizeof rc);
        rs = IMP_ISP_Tuning_GetSensorAttr(IMPVI_MAIN, &sa);
        rc.raw_width = sa.width; rc.raw_height = sa.height; rc.raw_bit = 8;
        rr = (!rs && sa.width && sa.height && sa.width * sa.height <= 3840u * 2160u) ? IMP_ISP_GetRaw(IMPVI_MAIN, &rc) : -99;
        if (!rr && rc.buf.vaddr) {
            const uint8_t *p8 = (const uint8_t *)(uintptr_t)rc.buf.vaddr;
            for (k = 0; k < rc.buf.buf_size; k += 64) { mean += p8[k]; if (p8[k]) nz++; }
            mean /= rc.buf.buf_size / 64;
        }
        stream_for(1.0, &e);
        R("ISP_GetRaw", OK(!rr && rc.buf.paddr && nz > rc.buf.buf_size / 64 / 2 && mean > 3 && mean < 252 && e.frames > 10),
          "sensor %ux%u (rc %d); GetRaw 8 bit rc=%d buf phys=0x%x size=%u; sampled mean %.1f, non-zero %u of %u; stream after %u frames/1 s",
          sa.width, sa.height, rs, rr, rc.buf.paddr, rc.buf.buf_size, mean, nz, rc.buf.buf_size / 64, e.frames);
    }

    if (mask & 0x1000) {
        IMPISPTuningOpsMode wg = (IMPISPTuningOpsMode)9;
        int rg = IMP_ISP_WDR_ENABLE_GET(IMPVI_MAIN, &wg);
        a = grab(500);
        stream_for(1.0, &e);
        R("ISP_WDR_OPEN", (!wdr_open_rc && a.ok && e.frames > 10) ? "PART" : "FAIL",
          "LINEAR/DOL before AddSensor rc=%d; sensor up, Y=%.1f, %u frames/1 s; WDR_ENABLE_GET rc=%d mode=%d (WDR itself not switched)",
          wdr_open_rc, a.y, e.frames, rg, wg);
    }

    if (mask & 0x400) {
        int c0 = osd_cb_calls;
        stream_for(2.0, &e);
        R("OSD_SetGroupCallback", OK(osd_made && !osd_cb_rc && osd_att && osd_cb_calls - c0 >= e.frames / 2 && osd_cb_group == CH && osd_cb_w == FW),
          "group %d rc=%d attached=%d; %d callbacks during %u encoded frames (2 s), group=%d width=%u",
          CH, osd_cb_rc, osd_att, osd_cb_calls - c0, e.frames, osd_cb_group, osd_cb_w);
    }

down:
    /* ===== teardown (also lifecycle evidence) ===== */
    if (recv_on) { r = IMP_Encoder_StopRecvPic(CH); R("Encoder_StopRecvPic", OK(!r), "rc=%d", r); }
    if (fs_on) { r = IMP_FrameSource_DisableChn(CH); R("FrameSource_DisableChn", OK(!r), "rc=%d", r); }
    if (osd_att) {
        /* FS -> OSD -> ENC: undo both binds, then the plain one is gone */
        IMP_OSD_Stop(CH);
        r = IMP_System_UnBind(&osd_cell, &cdst) | IMP_System_UnBind(&csrc, &osd_cell);
        R("System_UnBind(osd)", OK(!r), "rc=%d", r);
        bound = 0;
    }
    if (osd_made) IMP_OSD_DestroyGroup(CH);
    if (bound) { r = IMP_System_UnBind(&csrc, &cdst); R("System_UnBind", OK(!r), "rc=%d", r); }
    if (chn_made) { IMP_Encoder_UnRegisterChn(CH); r = IMP_Encoder_DestroyChn(CH); R("Encoder_DestroyChn", OK(!r), "rc=%d", r); }
    if (grp_made) IMP_Encoder_DestroyGroup(CH);
    if (fs_made) IMP_FrameSource_DestroyChn(CH);
    r = IMP_ISP_DisableTuning();              R("ISP_DisableTuning", OK(!r), "rc=%d", r);
    r = IMP_System_Exit();                    R("System_Exit", OK(!r), "rc=%d", r);
    r = IMP_ISP_DisableSensor(IMPVI_MAIN);    R("ISP_DisableSensor", OK(!r), "rc=%d", r);
    r = IMP_ISP_DelSensor(IMPVI_MAIN, &sinfo); R("ISP_DelSensor", OK(!r), "rc=%d", r);
    r = IMP_ISP_Close();                      R("ISP_Close", OK(!r), "rc=%d", r);
    printf("DONE\n");
    (void)t; (void)hevc_mode; (void)depth; (void)st; (void)u8o; (void)u8;
    return 0;
}
