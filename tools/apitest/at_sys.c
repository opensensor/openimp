/* apitest: System + FrameSource areas */
#include "apitest.h"

/* ----------------------------------------------------------------- System */
void t_system(void)
{
    int64_t t1, t2, t3;

    rep_area("sys");
    NEED(IMP_System_GetVersion) {
        G(IMPVersion, v);
        int r = IMP_System_GetVersion(v);
        gchk(v);
        CHECK(IMP_System_GetVersion, r, v->aVersion[0] != 0 && strlen(v->aVersion) < sizeof(v->aVersion), "\"%.60s\"", v->aVersion);
        gfree(v);
    }
    NEED(IMP_System_GetCPUInfo) {
        const char *c = IMP_System_GetCPUInfo();
        rep(FN(IMP_System_GetCPUInfo), 0, (c && !strncmp(c, SOC, 3)) ? V_PASS : V_FAIL, "\"%s\" (build for %s)", c ? c : "(null)", SOC);
    }
    NEED(IMP_System_GetTimeStamp) {
        t1 = IMP_System_GetTimeStamp(); usleep(100000); t2 = IMP_System_GetTimeStamp();
        rep(FN(IMP_System_GetTimeStamp), 0, (t2 - t1 > 80000 && t2 - t1 < 1000000) ? V_PASS : V_FAIL,
            "t=%lld us, +%lld us after a 100 ms sleep", (long long)t1, (long long)(t2 - t1));
    }
    NEED(IMP_System_RebaseTimeStamp) {
        int64_t base = 5000000, back, cur;
        int r;

        t1 = IMP_System_GetTimeStamp ? IMP_System_GetTimeStamp() : 0;
        r = IMP_System_RebaseTimeStamp(base);
        cur = IMP_System_GetTimeStamp ? IMP_System_GetTimeStamp() : 0;
        back = cur - base;
        /* restore the previous time base: continue at t1 + time spent */
        t3 = IMP_System_RebaseTimeStamp(t1 + 1000);
        CHECK(IMP_System_RebaseTimeStamp, r, !IMP_System_GetTimeStamp || (back >= 0 && back < 1000000),
              "base 5000000 -> GetTimeStamp %lld us; restored (ret %lld)", (long long)cur, (long long)t3);
    }
    NEED(IMP_System_ReadReg32) {
        /* CPM base (CPCCR) is mapped and stable on every Ingenic SoC */
        uint32_t a = IMP_System_ReadReg32(0x10000000), b = IMP_System_ReadReg32(0x10000000);
        rep(FN(IMP_System_ReadReg32), 0, a == b ? V_PASS : V_FAIL, "0x10000000 = 0x%08x / 0x%08x", a, b);
    }
    NEED(IMP_System_WriteReg32) {
        /* Harmless register on every Ingenic SoC (T10..T41): the scratch register SPR (offset 0x1c, 8 bit) of the UART
         * that carries the kernel console.  That UART is clocked (the console runs on it), the 8250 driver touches SPR
         * only in the one-time probe, so a different value written and put back has no side effect.  The UART number comes
         * from console=ttyS<n> on the kernel command line (all test boards: ttyS1 = 0x10031000); without it the case SKIPs. */
        char cl[1024] = "";
        FILE *f = fopen("/proc/cmdline", "r");
        const char *c;
        int uart = -1;

        if (f) { if (!fgets(cl, sizeof(cl), f)) cl[0] = 0; fclose(f); }
        c = strstr(cl, "console=ttyS");
        if (c) uart = c[12] - '0';
        if (uart < 0 || uart > 5) {
            rep(FN(IMP_System_WriteReg32), 0, V_SKIP, "no console=ttyS<n> on the kernel command line: no known-safe register");
        } else {
            uint32_t addr = 0x10030000u + (uint32_t)uart * 0x1000u + 0x1c;
            uint32_t orig = IMP_System_ReadReg32(addr) & 0xff, probe = (orig == 0xa5) ? 0x5a : 0xa5, got, same, back;

            IMP_System_WriteReg32(addr, probe);
            got = IMP_System_ReadReg32(addr) & 0xff;
            IMP_System_WriteReg32(addr, orig);              /* put the original value back (also: write of the same value) */
            same = IMP_System_ReadReg32(addr) & 0xff;
            IMP_System_WriteReg32(addr, orig);
            back = IMP_System_ReadReg32(addr) & 0xff;
            rep(FN(IMP_System_WriteReg32), 0, (got == probe && same == orig && back == orig) ? V_PASS : V_FAIL,
                "UART%d scratch SPR 0x%08x: 0x%02x -> wrote 0x%02x read 0x%02x -> restored 0x%02x / 0x%02x (round trip with ReadReg32)",
                uart, addr, orig, probe, got, same, back);
        }
    }
#if HAS_IMP_System_MemPoolRequest
    NEED(IMP_System_MemPoolRequest) {
        int r = IMP_System_MemPoolRequest(7, 65536, "apitest"), r2 = IMP_System_MemPoolRequest(7, 65536, "apitest");

        rep(FN(IMP_System_MemPoolRequest), r, r == 0 ? V_PASS : V_FAIL, "pool 7, 64 KB; second request of the same id returned %d", r2);
    }
#endif
#if HAS_IMP_System_MemPoolFree
    NEED(IMP_System_MemPoolFree) {
        int r = IMP_System_MemPoolFree(7);
        RET0(IMP_System_MemPoolFree, r, "pool 7");
    }
#endif
}

void t_system_exit(int unused)
{
    int r;

    (void)unused;
    r = IMP_ISP_DisableTuning(); RET0(IMP_ISP_DisableTuning, r, "");
    r = IMP_System_Exit(); RET0(IMP_System_Exit, r, "");
#ifdef PLATFORM_T41
    r = IMP_ISP_DisableSensor(IMPVI_MAIN); RET0(IMP_ISP_DisableSensor, r, "");
    r = IMP_ISP_DelSensor(IMPVI_MAIN, &g_sensor); RET0(IMP_ISP_DelSensor, r, "");
#else
    r = IMP_ISP_DisableSensor(); RET0(IMP_ISP_DisableSensor, r, "");
    r = IMP_ISP_DelSensor(&g_sensor); RET0(IMP_ISP_DelSensor, r, "");
#endif
    r = IMP_ISP_Close(); RET0(IMP_ISP_Close, r, "");
}

/* ------------------------------------------------------------ FrameSource */
static IMPFSChnAttr fs0, fs1;
static int g_fifo_set;      /* SetChnFifoAttr(maxdepth 2) took effect */

static void fs_attrs(void)
{
    memset(&fs0, 0, sizeof(fs0));
    fs0.picWidth = g_sw; fs0.picHeight = g_sh; fs0.pixFmt = PIX_FMT_NV12;
    fs0.outFrmRateNum = 15; fs0.outFrmRateDen = 1; fs0.nrVBs = 2;
#ifdef APITEST_T40
    if (getenv("APITEST_CH0_SCALE")) { fs0.scaler.enable = 1; fs0.scaler.outwidth = g_sw; fs0.scaler.outheight = g_sh; }   /* ch0 scaled from the native sensor size, as t40_devtest */
#endif
    fs0.type = FS_PHY_CHANNEL;
    fs1 = fs0;
    fs1.picWidth = SUB_W; fs1.picHeight = SUB_H; fs1.nrVBs = 3;
    fs1.scaler.enable = 1; fs1.scaler.outwidth = SUB_W; fs1.scaler.outheight = SUB_H;
}

void t_fs_pre(void)
{
    int r;

    rep_area("fs");
    fs_attrs();
    r = IMP_FrameSource_CreateChn(0, &fs0);
    RET0(IMP_FrameSource_CreateChn, r, "ch0 %dx%d NV12 15 fps (sensor size)", g_sw, g_sh);
    if (getenv("HSKIP_MAIN")) goto main_only;      /* case hskip with HSKIP_MAIN=1: ch0 only (see at_enc.c) */
    r = IMP_FrameSource_CreateChn(1, &fs1);
    RET0(IMP_FrameSource_CreateChn, r, "ch1 %dx%d scaled, nrVBs 3", SUB_W, SUB_H);
    /* settings that must be made between CreateChn and EnableChn */
    NEED(IMP_FrameSource_SetMaxDelay) { r = IMP_FrameSource_SetMaxDelay(1, 3); RET0(IMP_FrameSource_SetMaxDelay, r, "ch1 max 3"); }
    NEED(IMP_FrameSource_SetDelay) { r = IMP_FrameSource_SetDelay(1, 1); RET0(IMP_FrameSource_SetDelay, r, "ch1 delay 1"); }
    /* read back before SetChnFifoAttr: libimp's SetChnFifoAttr sets maxdelay = delay = maxdepth */
    NEED(IMP_FrameSource_GetMaxDelay) { G(int, d); r = IMP_FrameSource_GetMaxDelay(1, d); gchk(d); CHECK(IMP_FrameSource_GetMaxDelay, r, *d == 3, "max delay %d (set 3, before SetChnFifoAttr)", *d); gfree(d); }
    NEED(IMP_FrameSource_GetDelay) { G(int, d); r = IMP_FrameSource_GetDelay(1, d); gchk(d); CHECK(IMP_FrameSource_GetDelay, r, *d == 1, "delay %d (set 1, before SetChnFifoAttr)", *d); gfree(d); }
    NEED(IMP_FrameSource_SetChnFifoAttr) {
        IMPFSChnFifoAttr fa = { 2, FIFO_CACHE_PRIORITY };
        r = IMP_FrameSource_SetChnFifoAttr(1, &fa);
        RET0(IMP_FrameSource_SetChnFifoAttr, r, "ch1 maxdepth 2 cache-priority");
        g_fifo_set = r == 0;
    }
#if HAS_IMP_FrameSource_SetPool
# if defined(PLATFORM_T41)
    /* T41 1.2.5/1.2.6: the pool must exist (IMP_System_MemPoolRequest), else 0x80010040; the real binding is case fspool */
    NEED(IMP_FrameSource_SetPool) { r = IMP_FrameSource_SetPool(1, 0); rep(FN(IMP_FrameSource_SetPool), r, r == (int)0x80010040 ? V_PASS : V_FAIL, "ch1 pool 0 (not requested): ret 0x%x (vendor 0x80010040), ch1 stays in rmem", (unsigned)r); }
# else
    NEED(IMP_FrameSource_SetPool) { r = IMP_FrameSource_SetPool(1, 0); RET0(IMP_FrameSource_SetPool, r, "ch1 pool 0"); }
# endif
#endif
main_only:
    r = IMP_FrameSource_EnableChn(0);
    RET0(IMP_FrameSource_EnableChn, r, "ch0");
    if (!getenv("HSKIP_MAIN")) {
        r = IMP_FrameSource_EnableChn(1);
        RET0(IMP_FrameSource_EnableChn, r, "ch1");
    }
    /* the application can read frames (GetFrame) only with a frame depth on the vendor stacks (T41 OpenIMP follows that) */
    if (IMP_FrameSource_SetFrameDepth) { IMP_FrameSource_SetFrameDepth(0, 2); IMP_FrameSource_SetFrameDepth(1, 2); }
    sleep(3);       /* AE/AWB settle */
}

void t_fs_post(void)
{
    int r;

    if (!getenv("HSKIP_MAIN")) { r = IMP_FrameSource_DisableChn(1); RET0(IMP_FrameSource_DisableChn, r, "ch1"); }
    r = IMP_FrameSource_DisableChn(0); RET0(IMP_FrameSource_DisableChn, r, "ch0");
    if (!getenv("HSKIP_MAIN")) { r = IMP_FrameSource_DestroyChn(1); RET0(IMP_FrameSource_DestroyChn, r, "ch1"); }
    r = IMP_FrameSource_DestroyChn(0); RET0(IMP_FrameSource_DestroyChn, r, "ch0");
}

/* one frame of FS channel ch: size and timestamp (frame released again) */
int fs_peek(int ch, int *w, int *h, int64_t *ts)
{
    IMPFrameInfo *fr = NULL;

    if (!IMP_FrameSource_GetFrame) return -1;
    if (IMP_FrameSource_GetFrame(ch, &fr) < 0 || !fr) return -1;
    if (w) *w = fr->width;
    if (h) *h = fr->height;
    if (ts) *ts = fr->timeStamp;
    IMP_FrameSource_ReleaseFrame(ch, fr);
    return 0;
}

static unsigned luma_mean(const IMPFrameInfo *fr)
{
    const unsigned char *y = (const unsigned char *)(uintptr_t)fr->virAddr;
    unsigned long sum = 0, n = 0, i, tot = (unsigned long)fr->width * fr->height;

    if (!y || !tot) return 0;
    for (i = 0; i < tot; i += 97) { sum += y[i]; n++; }
    return n ? (unsigned)(sum / n) : 0;
}

void t_fs(void)
{
    int r, i;
    size_t nv12 = (size_t)SUB_W * SUB_H * 3 / 2;

    NEED(IMP_FrameSource_GetChnAttr) {
        G(IMPFSChnAttr, a);
        r = IMP_FrameSource_GetChnAttr(1, a);
        gchk(a);
        CHECK(IMP_FrameSource_GetChnAttr, r, a->picWidth == SUB_W && a->picHeight == SUB_H && a->pixFmt == PIX_FMT_NV12,
              "ch1 %dx%d fmt %d fps %d/%d nrVBs %d scaler %d (%dx%d)", a->picWidth, a->picHeight, a->pixFmt,
              a->outFrmRateNum, a->outFrmRateDen, a->nrVBs, a->scaler.enable, a->scaler.outwidth, a->scaler.outheight);
        NEED(IMP_FrameSource_SetChnAttr) {
            G(IMPFSChnAttr, b);
            int r2 = IMP_FrameSource_SetChnAttr(1, a), r3 = IMP_FrameSource_GetChnAttr(1, b);
            gchk(b);
            CHECK(IMP_FrameSource_SetChnAttr, r2, r3 == 0 && b->picWidth == a->picWidth && b->picHeight == a->picHeight &&
                  b->outFrmRateNum == a->outFrmRateNum, "round trip with unchanged attr, get ret %d", r3);
            gfree(b);
        }
        gfree(a);
    }
    NEED(IMP_FrameSource_SetFrameDepth) {
        G(int, d);
        r = IMP_FrameSource_SetFrameDepth(1, 2);
        RET0(IMP_FrameSource_SetFrameDepth, r, "ch1 depth 2");
        NEED(IMP_FrameSource_GetFrameDepth) {
            int r2 = IMP_FrameSource_GetFrameDepth(1, d);
            gchk(d);
            CHECK(IMP_FrameSource_GetFrameDepth, r2, *d == 2, "depth %d (set 2)", *d);
        }
        gfree(d);
    }
    /* delay / max delay / fifo / pool as set before EnableChn */
    /* after SetChnFifoAttr(maxdepth 2): libimp stores maxdelay = delay = 2 */
    if (g_fifo_set) {
        NEED(IMP_FrameSource_GetMaxDelay) { G(int, d); r = IMP_FrameSource_GetMaxDelay(1, d); gchk(d); 
#if defined(PLATFORM_T41) || defined(PLATFORM_T40)
            /* vendor T40/T41 (disassembly): max delay stays at the value set before */
            CHECK(IMP_FrameSource_GetMaxDelay, r, *d == 3, "max delay %d (stays 3 after SetChnFifoAttr maxdepth 2, as the vendor)", *d);
#else
            CHECK(IMP_FrameSource_GetMaxDelay, r, *d == 2, "max delay %d (SetChnFifoAttr maxdepth 2)", *d);
#endif
             gfree(d); }
        NEED(IMP_FrameSource_GetDelay) { G(int, d); r = IMP_FrameSource_GetDelay(1, d); gchk(d); CHECK(IMP_FrameSource_GetDelay, r, *d == 2, "delay %d (SetChnFifoAttr sets delay = maxdepth 2)", *d); gfree(d); }
    }
    NEED(IMP_FrameSource_GetChnFifoAttr) {
        G(IMPFSChnFifoAttr, f);
        r = IMP_FrameSource_GetChnFifoAttr(1, f);
        gchk(f);
        CHECK(IMP_FrameSource_GetChnFifoAttr, r, f->maxdepth == 2 && f->type == FIFO_CACHE_PRIORITY, "maxdepth %d type %d (set 2/0)", f->maxdepth, f->type);
        gfree(f);
    }
#if HAS_IMP_FrameSource_GetPool
# if defined(PLATFORM_T41)
    /* T41: SetPool(1, 0) was refused (pool 0 not requested), so no pool is bound: vendor 0x80010020 */
    NEED(IMP_FrameSource_GetPool) { r = IMP_FrameSource_GetPool(1); rep(FN(IMP_FrameSource_GetPool), r, r == (int)0x80010020 ? V_PASS : V_FAIL, "ch1: ret 0x%x (no pool bound, vendor 0x80010020)", (unsigned)r); }
# else
    NEED(IMP_FrameSource_GetPool) { r = IMP_FrameSource_GetPool(1); rep(FN(IMP_FrameSource_GetPool), r, r == 0 ? V_PASS : V_FAIL, "pool %d (set 0)", r); }
# endif
#endif
#if HAS_IMP_FrameSource_ChnStatQuery
    NEED(IMP_FrameSource_ChnStatQuery) {
        G(IMPFSChannelState, s);
        r = IMP_FrameSource_ChnStatQuery(1, s); gchk(s);
        CHECK(IMP_FrameSource_ChnStatQuery, r, *s == IMP_FSCHANNEL_STATE_RUN, "state %d (2 = run)", (int)*s);
        gfree(s);
    }
#endif
#if HAS_IMP_FrameSource_SetChnRotate
    NEED(IMP_FrameSource_SetChnRotate) { r = IMP_FrameSource_SetChnRotate(1, 0, SUB_W, SUB_H); RET0(IMP_FrameSource_SetChnRotate, r, "rotate 0 (no change)"); }
#endif
#if HAS_IMP_FrameSource_GetDirectModeAttr
    NEED(IMP_FrameSource_GetDirectModeAttr) {
        G(int, t);
        r = IMP_FrameSource_GetDirectModeAttr(1, t); gchk(t);
        RET0(IMP_FrameSource_GetDirectModeAttr, r, "threshold %d", *t);
        NEED(IMP_FrameSource_SetDirectModeAttr) { int r2 = IMP_FrameSource_SetDirectModeAttr(1, *t); RET0(IMP_FrameSource_SetDirectModeAttr, r2, "same value %d", *t); }
        gfree(t);
    }
#endif
#if HAS_IMP_FrameSource_GetI2dAttr
    NEED(IMP_FrameSource_GetI2dAttr) {
        G(IMPFSI2DAttr, a);
        r = IMP_FrameSource_GetI2dAttr(1, a); gchk(a);
        RET0(IMP_FrameSource_GetI2dAttr, r, "i2d %d flip %d mirr %d rot %d/%d", a->i2d_enable, a->flip_enable, a->mirr_enable, a->rotate_enable, a->rotate_angle);
        NEED(IMP_FrameSource_SetI2dAttr) { int r2 = IMP_FrameSource_SetI2dAttr(1, a); RET0(IMP_FrameSource_SetI2dAttr, r2, "same value"); }
        gfree(a);
    }
#endif
    /* GetFrame / ReleaseFrame: content, size, timestamps, rate */
    NEED(IMP_FrameSource_GetFrame) {
        int64_t first = 0, last = 0, tprev = 0, tmeas = 0;
        int n = 0, mono = 1, w = 0, h = 0, rr = 0;
        unsigned luma = 0, luma0 = 0;
        IMPFrameInfo **pf = (IMPFrameInfo **)gnew(sizeof(IMPFrameInfo *));
        int64_t t0 = now_us(), t1;

        for (i = 0; i < 16; i++) {
            *pf = NULL;
            r = IMP_FrameSource_GetFrame(1, pf);
            gchk(pf);
            if (r < 0 || !*pf) break;
            if (n == 0) { first = (*pf)->timeStamp; w = (*pf)->width; h = (*pf)->height; luma0 = luma_mean(*pf);
                          if (!((*pf)->virAddr && (*pf)->size >= nv12)) mono = 0; }
            { unsigned l = luma_mean(*pf); if (l > luma) luma = l; }   /* brightest of the 16: the first buffers can still be unwritten */
            if (n && (*pf)->timeStamp <= tprev) mono = 0;
            tprev = last = (*pf)->timeStamp;
            if (n == 4) tmeas = last;   /* frames 0..3 can be the ring of the first buffers (older than the sleep before) */
            if (IMP_FrameSource_ReleaseFrame) {
                rr = IMP_FrameSource_ReleaseFrame(1, *pf);
            }
            n++;
        }
        t1 = now_us();
        CHECK(IMP_FrameSource_GetFrame, r < 0 ? r : 0, n == 16 && w == SUB_W && h == SUB_H && mono && luma > 0,
              "%d frames %dx%d luma %u (first frame %u) ts monotonic %d, %.1f fps (wall), ts span %lld us", n, w, h, luma, luma0, mono,
              n > 1 ? (n - 1) * 1e6 / (double)(t1 - t0) : 0.0, (long long)(last - first));
        NEED(IMP_FrameSource_ReleaseFrame) rep(FN(IMP_FrameSource_ReleaseFrame), rr, rr == 0 ? V_PASS : V_FAIL, "last release");
        /* libimp hands out the channel at the sensor rate (outFrmRate is not a
         * FrameSource drop rate); apitest set the sensor to 15/1 as the vendor
         * samples do, so ch1 must run at ~15 fps */
        if (n == 16 && last > tmeas && tmeas) {
            double fps = 11.0 * 1e6 / (double)(last - tmeas);
            rep(LBL("FrameSource rate = sensor rate"), 0, fps > g_fps * 0.8 && fps < g_fps * 1.2 ? V_PASS : V_FAIL,
                "ch1 %.1f fps by timestamps (sensor %d fps in effect, channel outFrmRate 15/1)", fps, g_fps);
        }
        gfree(pf);
    }
    NEED(IMP_FrameSource_SnapFrame) {
        G(IMPFrameInfo, fi);
        unsigned char *buf = gnew(nv12);
        size_t k, nz = 0;

        r = IMP_FrameSource_SnapFrame(1, PIX_FMT_NV12, SUB_W, SUB_H, buf, fi);
        gchk(fi); gchk(buf);
        for (k = 0; k < nv12; k += 61) if (buf[k]) nz++;
        CHECK(IMP_FrameSource_SnapFrame, r, nz > 100, "%dx%d, %zu sampled non-zero bytes of %zu, info %ux%u", SUB_W, SUB_H, nz, nv12 / 61, fi->width, fi->height);
        gfree(fi); gfree(buf);
    }
    NEED(IMP_FrameSource_GetTimedFrame) {
        G(IMPFrameInfo, fi);
        G(IMPFrameTimestamp, ts);
        unsigned char *buf = gnew(nv12 * 2); /* the cache copies a whole capture buffer (sizeimage, aligned) */
        int w = 0, h = 0, r2 = -1;
        int64_t t = 0, t2 = 0, iv = 66666, target, got = 0;

        /* The call asks for any held frame within [ts-minus, ts+plus] (here +-200 ms, the vendor window);
         * the frame interval of ch1 (15 fps = 67 ms) is not the sensor interval fs_peek sees.
         * The FIFO holds the newest `delay` frames; a frame GetFrame handed
         * out has left it, so its time stamp is older than every held frame
         * (libimp: -1).  Ask for a time inside the held window instead:
         * now - half a frame, blocking (waits for the next frame if the
         * newest is older), then once more without block for exactly the
         * time stamp of the held frame it returned. */
        if (fs_peek(1, &w, &h, &t) == 0 && fs_peek(1, &w, &h, &t2) == 0 && t2 > t && t2 - t < 1000000) iv = t2 - t;
        target = (int64_t)IMP_System_GetTimeStamp() - iv / 2;
        ts->ts = (uint64_t)target; ts->minus = 200000; ts->plus = 200000;
        r = IMP_FrameSource_GetTimedFrame(1, ts, 1, buf, fi);
        gchk(fi); gchk(buf); gchk(ts);
        got = (int64_t)fi->timeStamp;
        CHECK(IMP_FrameSource_GetTimedFrame, r, got && (got - target <= (int64_t)ts->plus && target - got <= (int64_t)ts->minus) && fi->width == SUB_W,
              "ts now-%lld us, block: frame ts %+lld us from it, info %ux%u", (long long)(iv / 2), (long long)(got - target), fi->width, fi->height);
        if (r == 0 && got) {
            ts->ts = (uint64_t)got;
            r2 = IMP_FrameSource_GetTimedFrame(1, ts, 0, buf, fi);
            gchk(fi); gchk(buf);
            rep(LBL("IMP_FrameSource_GetTimedFrame (no block, held frame)"), r2, r2 == 0 && (int64_t)fi->timeStamp == got ? V_PASS : V_FAIL,
                "ts of the held frame -> %d, ts %s", r2, (int64_t)fi->timeStamp == got ? "equal" : "differs");
        }
        gfree(fi); gfree(ts); gfree(buf);
    }
#if HAS_IMP_FrameSource_GetFrameEx
    NEED(IMP_FrameSource_GetFrameEx) {
        IMPFrameInfo **pf = (IMPFrameInfo **)gnew(sizeof(IMPFrameInfo *));
        r = IMP_FrameSource_GetFrameEx(1, pf); gchk(pf);
        CHECK(IMP_FrameSource_GetFrameEx, r, *pf != NULL, "frame %p", (void *)*pf);
        NEED(IMP_FrameSource_ReleaseFrameEx) { int r2 = (r == 0 && *pf) ? IMP_FrameSource_ReleaseFrameEx(1, *pf) : -1; RET0(IMP_FrameSource_ReleaseFrameEx, r2, ""); }
        gfree(pf);
    }
#endif
    /* not run: need a topology of their own */
#if HAS_IMP_FrameSource_SetSource
    rep(FN(IMP_FrameSource_SetSource), 0, V_SKIP, "needs an FS_EXT_CHANNEL");
#endif
#if HAS_IMP_FrameSource_ExternInject_CreateChn
    rep(FN(IMP_FrameSource_ExternInject_CreateChn), 0, V_SKIP, "needs an injected-frame source");
    rep(FN(IMP_FrameSource_ExternInject_EnableChn), 0, V_SKIP, "see CreateChn");
    rep(FN(IMP_FrameSource_ExternInject_DisableChn), 0, V_SKIP, "see CreateChn");
    rep(FN(IMP_FrameSource_ExternInject_DestroyChn), 0, V_SKIP, "see CreateChn");
    rep(FN(IMP_FrameSource_DequeueBuffer), 0, V_SKIP, "inject channel only");
    rep(FN(IMP_FrameSource_QueueBuffer), 0, V_SKIP, "inject channel only");
#endif
#if HAS_IMP_FrameSource_SetYuvAlign
    rep(FN(IMP_FrameSource_SetYuvAlign), 0, V_SKIP, "changes the frame stride of the running pipeline");
#endif

#if defined(PLATFORM_T41) && !defined(APITEST_T40)
    /* T41 vendor libimp 1.2.5 (disassembled GetFrame/SetFrameDepth): depth 0 -> GetFrame returns 0 and does not touch *frame; depth N ->
     * N frames can be held, the (N+1)th GetFrame waits 2 s and fails with 0x80012000 (IMP_ERR_FS_OVERTIME); a ReleaseFrame frees a slot */
    NEED(IMP_FrameSource_SetFrameDepth) NEED(IMP_FrameSource_GetFrame) NEED(IMP_FrameSource_ReleaseFrame) {
        IMPFrameInfo **pf = (IMPFrameInfo **)gnew(sizeof(IMPFrameInfo *));
        IMPFrameInfo *f1 = NULL, *f2 = NULL, *f4 = NULL;
        int rs0, r0, r1, r2, r3, r4, rr1, rr2, rr4, ok0, ok1, ok2, ok3, ok4;
        int64_t t0, dt;

        rs0 = IMP_FrameSource_SetFrameDepth(1, 0);
        *pf = NULL; r0 = IMP_FrameSource_GetFrame(1, pf); gchk(pf); ok0 = rs0 == 0 && r0 == 0 && *pf == NULL;
        r = IMP_FrameSource_SetFrameDepth(1, 2);
        *pf = NULL; r1 = IMP_FrameSource_GetFrame(1, pf); f1 = *pf; ok1 = r == 0 && r1 == 0 && f1;
        *pf = NULL; r2 = IMP_FrameSource_GetFrame(1, pf); f2 = *pf; ok2 = r2 == 0 && f2 && f2 != f1;
        *pf = NULL; t0 = now_us(); r3 = IMP_FrameSource_GetFrame(1, pf); dt = now_us() - t0; gchk(pf);
        ok3 = r3 == (int)0x80012000 && *pf == NULL && dt > 1500000 && dt < 3500000;
        rr1 = f1 ? IMP_FrameSource_ReleaseFrame(1, f1) : -1;
        *pf = NULL; r4 = IMP_FrameSource_GetFrame(1, pf); f4 = *pf; ok4 = rr1 == 0 && r4 == 0 && f4;
        rr2 = f2 ? IMP_FrameSource_ReleaseFrame(1, f2) : -1;
        rr4 = f4 ? IMP_FrameSource_ReleaseFrame(1, f4) : -1;
        IMP_FrameSource_SetFrameDepth(1, 2);
        gfree(pf);
        rep(FN(IMP_FrameSource_SetFrameDepth), r3, (ok0 && ok1 && ok2 && ok3 && ok4 && rr2 == 0 && rr4 == 0) ? V_PASS : V_FAIL,
            "depth semantics as the vendor: depth 0 -> GetFrame ret %d, frame %s; depth 2 -> 2 frames held (%d/%d), third: ret 0x%x after %lld ms; after a Release the next works (%d)",
            r0, ok0 ? "untouched" : "CHANGED", r1, r2, (unsigned)r3, (long long)(dt / 1000), r4);
    }
#endif
    /* disable / enable cycle: frames must come back */
    NEED(IMP_FrameSource_SetFrameDepth) { IMP_FrameSource_SetFrameDepth(1, 0); }
    r = IMP_FrameSource_DisableChn(1);
    RET0(IMP_FrameSource_DisableChn, r, "ch1 (cycle test)");
    usleep(200000);
    r = IMP_FrameSource_EnableChn(1);
    RET0(IMP_FrameSource_EnableChn, r, "ch1 (cycle test)");
    NEED(IMP_FrameSource_GetFrame) {
        int w = 0, h = 0, rr;
        int64_t t = 0;
        NEED(IMP_FrameSource_SetFrameDepth) { IMP_FrameSource_SetFrameDepth(1, 1); }
        usleep(500000);
        rr = fs_peek(1, &w, &h, &t);
        rep(LBL("FrameSource disable/enable cycle"), rr, rr == 0 && w == SUB_W ? V_PASS : V_FAIL, "frame after re-enable %dx%d ts %lld", w, h, (long long)t);
#if defined(PLATFORM_T41)
        /* T40/T41 OpenIMP follows the vendor depth rule (depth 0: GetFrame returns 0 without a frame): the cases that follow read ch1 through
         * GetFrame, so the depth of the setup stays at 2 */
        NEED(IMP_FrameSource_SetFrameDepth) { IMP_FrameSource_SetFrameDepth(1, 2); }
#else
        NEED(IMP_FrameSource_SetFrameDepth) { IMP_FrameSource_SetFrameDepth(1, 0); }
#endif
    }
}
