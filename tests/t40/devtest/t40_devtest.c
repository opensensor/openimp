/* T40 device API test: one process, one ISP/sensor session, many functions.
 * Prints "RES <function> <PASS|PART|FAIL> <evidence>" lines on stdout.
 * Build: mipsel toolchain with -I<T40 1.3.1 headers>, link -limp (OpenIMP or
 * vendor, selected at run time by LD_LIBRARY_PATH).  Picture data is only
 * measured in memory (mean luma, chroma, gradient); nothing is written.
 * Never calls IMP_ISP_Tuning_GetAeExpList (hangs the vendor kernel).
 * usage: t40_devtest [phase-mask-hex]   (default: all phases)
 *   phases: 1 lifecycle+framesource  2 encoder  4 isp tuning  8 stats/awb
 *           10 system  20 osd  40 sysutils (vendor libsysutils)  */
#include <errno.h>
#include <poll.h>
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

#define CH 1            /* scaled sub channel, 1280x720 */
#define FW 1280
#define FH 720
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

static struct pm grab(int settle_ms)
{
    struct pm a, s;
    int i, k = 0;
    memset(&s, 0, sizeof s);
    usleep(settle_ms * 1000);
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

    alarm(420);

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
                /* current = mirrored */
                if (IMP_FrameSource_GetFrame(CH, &f1) == 0 && f1) {
                    w = f1->width; h = f1->height; Y = (const uint8_t *)(uintptr_t)f1->virAddr;
                    for (y = 0; y < h; y += 8) for (x = 0; x < w / 2; x += 8) { la += Y[y * w + x]; ra += Y[y * w + w - 1 - x]; n++; }
                    la /= n; ra /= n;
                    IMP_ISP_Tuning_GetHVFlip(IMPVI_MAIN, &fl2);
                    IMP_FrameSource_ReleaseFrame(CH, f1);
                }
                fl2 = IMPISP_FLIP_NORMAL_MODE; IMP_ISP_Tuning_SetHVFLIP(IMPVI_MAIN, &fl2);
                usleep(1500000);
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

down:
    /* ===== teardown (also lifecycle evidence) ===== */
    if (recv_on) { r = IMP_Encoder_StopRecvPic(CH); R("Encoder_StopRecvPic", OK(!r), "rc=%d", r); }
    if (fs_on) { r = IMP_FrameSource_DisableChn(CH); R("FrameSource_DisableChn", OK(!r), "rc=%d", r); }
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
