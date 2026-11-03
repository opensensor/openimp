/* apitest: OSD and IVS areas */
#include "apitest.h"

/* ------------------------------------------------------------------- OSD */
#define OGRP 0
#define PW 64
#define PH 32

#ifdef PLATFORM_T41
static int g_cb_count;
static int osd_cb(int grp, IMPFrameInfo *f) { (void)grp; (void)f; g_cb_count++; return 0; }
#endif

static void osd_flow(void)
{
    int r, h, h2, h3;
    unsigned char *pic = malloc(PW * PH * 4), *pic2 = malloc(PW * PH * 4);
    IMPOSDRgnAttr ra;
    IMPOSDGrpRgnAttr ga;
    int i;

    for (i = 0; i < PW * PH; i++) { ((uint32_t *)pic)[i] = 0xC0FF0000u | (i & 0xff); ((uint32_t *)pic2)[i] = 0xC000FF00u | (i & 0xff); }

    /* region 1: rectangle */
    if (!IMP_OSD_CreateRgn) { rep_na(FN(IMP_OSD_CreateRgn)); free(pic); free(pic2); return; }
    h = IMP_OSD_CreateRgn(NULL);
    rep(FN(IMP_OSD_CreateRgn), h, h >= 0 ? V_PASS : V_FAIL, "RECT region handle %d", h);
    if (h < 0) { free(pic); free(pic2); return; }
    NEED(IMP_OSD_RegisterRgn) {
        r = IMP_OSD_RegisterRgn(h, OGRP, NULL);
        RET0(IMP_OSD_RegisterRgn, r, "handle %d group %d", h, OGRP);
    }
    memset(&ra, 0, sizeof(ra));
    ra.type = OSD_REG_RECT;
    ra.rect.p0.x = 20; ra.rect.p0.y = 20; ra.rect.p1.x = 219; ra.rect.p1.y = 119;
    ra.data.lineRectData.color = 0xFFFF0000;
    ra.data.lineRectData.linewidth = 3;
    NEED(IMP_OSD_SetRgnAttr) {
        r = IMP_OSD_SetRgnAttr(h, &ra);
        RET0(IMP_OSD_SetRgnAttr, r, "RECT (20,20)-(219,119) red, line 3");
    }
    NEED(IMP_OSD_GetRgnAttr) {
        G(IMPOSDRgnAttr, g);
        r = IMP_OSD_GetRgnAttr(h, g); gchk(g);
        CHECK(IMP_OSD_GetRgnAttr, r, g->type == OSD_REG_RECT && g->rect.p0.x == 20 && g->rect.p1.x == 219,
              "type %d (%d,%d)-(%d,%d)", (int)g->type, g->rect.p0.x, g->rect.p0.y, g->rect.p1.x, g->rect.p1.y);
        gfree(g);
    }
    memset(&ga, 0, sizeof(ga));
    ga.show = 1; ga.layer = 1; ga.gAlphaEn = 1; ga.fgAlhpa = 255; ga.bgAlhpa = 0; ga.scalex = 1.0f; ga.scaley = 1.0f;
    NEED(IMP_OSD_SetGrpRgnAttr) {
        r = IMP_OSD_SetGrpRgnAttr(h, OGRP, &ga);
        RET0(IMP_OSD_SetGrpRgnAttr, r, "show 1 layer 1 alpha 255");
    }
    NEED(IMP_OSD_GetGrpRgnAttr) {
        G(IMPOSDGrpRgnAttr, g);
        r = IMP_OSD_GetGrpRgnAttr(h, OGRP, g); gchk(g);
        CHECK(IMP_OSD_GetGrpRgnAttr, r, g->show == 1 && g->layer == 1, "show %d layer %d alpha en %d fg %d bg %d", g->show, g->layer, g->gAlphaEn, g->fgAlhpa, g->bgAlhpa);
        gfree(g);
    }
    NEED(IMP_OSD_Start) { r = IMP_OSD_Start(OGRP); RET0(IMP_OSD_Start, r, "group %d", OGRP); }
    NEED(IMP_OSD_ShowRgn) {
        int r1 = IMP_OSD_ShowRgn(h, OGRP, 0), r2 = IMP_OSD_ShowRgn(h, OGRP, 1);
        rep(FN(IMP_OSD_ShowRgn), r1 | r2, (r1 | r2) == 0 ? V_PASS : V_FAIL, "hide %d, show %d", r1, r2);
    }

    /* region 2: BGRA picture, update data */
    h2 = IMP_OSD_CreateRgn(NULL);
    if (h2 >= 0) {
        IMPOSDRgnAttrData d;

        IMP_OSD_RegisterRgn(h2, OGRP, NULL);
        memset(&ra, 0, sizeof(ra));
        ra.type = OSD_REG_PIC; ra.fmt = PIX_FMT_BGRA;
        ra.rect.p0.x = 300; ra.rect.p0.y = 20; ra.rect.p1.x = 300 + PW - 1; ra.rect.p1.y = 20 + PH - 1;
        ra.data.picData.pData = pic;
        r = IMP_OSD_SetRgnAttr(h2, &ra);
        rep(LBL("IMP_OSD_SetRgnAttr (PIC BGRA)"), r, r == 0 ? V_PASS : V_FAIL, "64x32 picture region at (300,20)");
        IMP_OSD_SetGrpRgnAttr(h2, OGRP, &ga);
        memset(&d, 0, sizeof(d));
        d.picData.pData = pic2;
        NEED(IMP_OSD_UpdateRgnAttrData) { r = IMP_OSD_UpdateRgnAttrData(h2, &d); RET0(IMP_OSD_UpdateRgnAttrData, r, "new picture data"); }
        NEED(IMP_OSD_SetRgnAttrWithTimestamp) {
            G(IMPOSDRgnTimestamp, ts);
            ts->ts = (uint64_t)(IMP_System_GetTimeStamp ? IMP_System_GetTimeStamp() : 0); ts->minus = 100000; ts->plus = 100000;
            ra.data.picData.pData = pic2;
            r = IMP_OSD_SetRgnAttrWithTimestamp(h2, &ra, ts); gchk(ts);
            RET0(IMP_OSD_SetRgnAttrWithTimestamp, r, "ts now +-100 ms");
            gfree(ts);
        }
#if HAS_IMP_OSD_GetRegionLuma
        NEED(IMP_OSD_GetRegionLuma) {
            G(IMPOSDRgnAttr, g);
            r = IMP_OSD_GetRegionLuma(h2, g); gchk(g);
            RET0(IMP_OSD_GetRegionLuma, r, "region luma query");
            gfree(g);
        }
#endif
#if HAS_IMP_OSD_RgnCreate_Query
        NEED(IMP_OSD_RgnCreate_Query) {
            G(IMPOSDRgnCreateStat, st);
            r = IMP_OSD_RgnCreate_Query(h2, st); gchk(st);
            RET0(IMP_OSD_RgnCreate_Query, r, "status %d", st->status);
            gfree(st);
        }
        NEED(IMP_OSD_RgnRegister_Query) {
            G(IMPOSDRgnRegisterStat, st);
            r = IMP_OSD_RgnRegister_Query(h2, OGRP, st); gchk(st);
            RET0(IMP_OSD_RgnRegister_Query, r, "status %d", st->status);
            gfree(st);
        }
#endif
    }

    /* region 3: cover */
    h3 = IMP_OSD_CreateRgn(NULL);
    if (h3 >= 0) {
        IMP_OSD_RegisterRgn(h3, OGRP, NULL);
        memset(&ra, 0, sizeof(ra));
        ra.type = OSD_REG_COVER;
        ra.rect.p0.x = 400; ra.rect.p0.y = 200; ra.rect.p1.x = 499; ra.rect.p1.y = 259;
        ra.data.coverData.color = 0xFF0000FF;
        r = IMP_OSD_SetRgnAttr(h3, &ra);
        rep(LBL("IMP_OSD_SetRgnAttr (COVER)"), r, r == 0 ? V_PASS : V_FAIL, "cover (400,200)-(499,259) blue");
        IMP_OSD_SetGrpRgnAttr(h3, OGRP, &ga);
    }

#ifdef PLATFORM_T41
    NEED(IMP_OSD_SetGroupCallback) {
        g_cb_count = 0;
        r = IMP_OSD_SetGroupCallback(OGRP, osd_cb);
        enc_pull_h264(10);
        rep(FN(IMP_OSD_SetGroupCallback), r, (r == 0 && g_cb_count > 0) ? V_PASS : V_FAIL, "callback invoked %d times for 10 encoded frames", g_cb_count);
        IMP_OSD_SetGroupCallback(OGRP, NULL);
    }
#endif
    {
        int fl = enc_pull_h264(10);
        rep(LBL("encoder stream with OSD regions"), 0, fl == 10 ? V_PASS : V_FAIL, "%d of 10 frames while 3 regions are shown", fl);
    }
#if HAS_IMP_OSD_SetMosaic
    NEED(IMP_OSD_SetMosaic) {
        size_t sz = (size_t)SUB_W * SUB_H * 3 / 2;
        unsigned char *fr = gnew(sz);
        G(IMPOSDMosaicAttr, m);

        memset(fr, 0x60, sz);
        m->x = 64; m->y = 64; m->mosaic_width = 64; m->mosaic_height = 64; m->frame_width = SUB_W; m->frame_height = SUB_H; m->mosaic_min_size = 16;
        r = IMP_OSD_SetMosaic(fr, m); gchk(fr); gchk(m);
        RET0(IMP_OSD_SetMosaic, r, "64x64 mosaic on a %dx%d NV12 test buffer", SUB_W, SUB_H);
        gfree(fr); gfree(m);
    }
#endif
#if HAS_IMP_OSD_SetRgnAttr_ISP && !(defined(PLATFORM_T41) && !defined(APITEST_T40))   /* T41: case rgnisp (at_cases.c) */
    rep(FN(IMP_OSD_SetRgnAttr_ISP), 0, V_SKIP, "ISP-side OSD block, needs an IMPOSDIspDraw set-up of its own");
    rep(FN(IMP_OSD_GetRgnAttr_ISP), 0, V_SKIP, "see SetRgnAttr_ISP");
#endif

    /* teardown: hide, stop, unregister, destroy */
    NEED(IMP_OSD_ShowRgn) { r = IMP_OSD_ShowRgn(h, OGRP, 0); if (r) RET0(IMP_OSD_ShowRgn, r, "hide for teardown"); }
    NEED(IMP_OSD_Stop) { r = IMP_OSD_Stop(OGRP); RET0(IMP_OSD_Stop, r, "group %d", OGRP); }
    NEED(IMP_OSD_UnRegisterRgn) {
        r = IMP_OSD_UnRegisterRgn(h, OGRP);
        RET0(IMP_OSD_UnRegisterRgn, r, "RECT region");
        if (h2 >= 0) IMP_OSD_UnRegisterRgn(h2, OGRP);
        if (h3 >= 0) IMP_OSD_UnRegisterRgn(h3, OGRP);
    }
    NEED(IMP_OSD_DestroyRgn) {
        IMP_OSD_DestroyRgn(h);
        if (h2 >= 0) IMP_OSD_DestroyRgn(h2);
        if (h3 >= 0) IMP_OSD_DestroyRgn(h3);
        rep(FN(IMP_OSD_DestroyRgn), 0, V_PASS, "3 regions destroyed (void function)");
    }
    free(pic); free(pic2);
}

void t_osd(void)
{
    int r;

    rep_area("osd");
    NEED(IMP_OSD_SetPoolSize) { r = IMP_OSD_SetPoolSize(128 * 1024); RET0(IMP_OSD_SetPoolSize, r, "128 KB (before CreateGroup)"); }
    if (!IMP_OSD_CreateGroup) { rep_na(FN(IMP_OSD_CreateGroup)); return; }
    if (enc_chain_up() != 0) {
        rep(FN(IMP_OSD_CreateGroup), -1, V_FAIL, "OSD/encoder chain could not be built, OSD flow not run");
        return;
    }
    rep(FN(IMP_OSD_CreateGroup), 0, V_PASS, "group %d (in the FS -> OSD -> ENC chain)", OGRP);
    NEED(IMP_OSD_AttachToGroup) rep(FN(IMP_OSD_AttachToGroup), 0, V_SKIP, "deprecated, the chain is built with IMP_System_Bind");
    osd_flow();
    enc_chain_down();
    rep(FN(IMP_OSD_DestroyGroup), 0, V_PASS, "group %d (chain teardown)", OGRP);
}

/* ------------------------------------------------------------------- IVS */
static int ivs_bind(int grp, int *outid, int first)
{
    int r, o;
    IMPCell fs, ivs = { DEV_ID_IVS, grp, 0 };

    for (o = 0; o < 2; o++) {
        fs.deviceID = DEV_ID_FS; fs.groupID = CH_SUB; fs.outputID = o;
        r = IMP_System_Bind(&fs, &ivs);
        if (r == 0) { *outid = o; if (first) rep(FN(IMP_System_Bind), 0, V_PASS, "FS ch%d output %d -> IVS group %d", CH_SUB, o, grp); return 0; }
    }
    rep(FN(IMP_System_Bind), r, V_FAIL, "FS ch%d -> IVS group %d (outputs 0 and 1 tried)", CH_SUB, grp);
    return -1;
}

static void ivs_unbind(int grp, int outid)
{
    IMPCell fs = { DEV_ID_FS, CH_SUB, 0 }, ivs = { DEV_ID_IVS, grp, 0 };
    int r;

    fs.outputID = outid;
    r = IMP_System_UnBind(&fs, &ivs);
    if (r) rep(FN(IMP_System_UnBind), r, V_FAIL, "FS -> IVS group %d", grp);
}

/* ---- minimal custom IVS algorithm (IMPIVSInterface of the vendor header) ---- */
#include <pthread.h>
struct ca_res { uint32_t w, h; int mean, seq; };
static struct { volatile int frames, rel_called, rel_bad, rel_ret, rel_res, init_called, exit_called, seq; struct ca_res res; } g_ca;
static pthread_mutex_t g_ca_mx;
static IMPIVSInterface g_ca_if;
static int g_ca_param[4];
static int ca_init(IMPIVSInterface *inf) { (void)inf; g_ca.init_called++; return 0; }
static void ca_exit(IMPIVSInterface *inf) { (void)inf; g_ca.exit_called++; }
static int ca_pre(IMPIVSInterface *inf, IMPFrameInfo *f) { (void)inf; (void)f; return 0; }
static int ca_proc(IMPIVSInterface *inf, IMPFrameInfo *f)
{
    uint8_t *y = (uint8_t *)(uintptr_t)f->virAddr;
    uint32_t n, step, sum = 0, cnt = 0, w = f->width, h = f->height;
    struct ca_res r;
    int rr;

    (void)inf;
    if (!y || !w || !h) return -1;
    step = (w * h) / 512; if (!step) step = 1;
    for (n = 0; n < w * h; n += step) { sum += y[n]; cnt++; }
    r.w = w; r.h = h; r.mean = cnt ? (int)(sum / cnt) : 0; r.seq = ++g_ca.seq;
    pthread_mutex_lock(&g_ca_mx); g_ca.res = r; pthread_mutex_unlock(&g_ca_mx);
    rr = IMP_IVS_ReleaseData((void *)(uintptr_t)f->virAddr);      /* frame no longer needed */
    g_ca.rel_ret = rr; g_ca.rel_called++; if (rr) g_ca.rel_bad++;
    g_ca.frames++;
    return 0;
}
static int ca_get(IMPIVSInterface *inf, void **res) { (void)inf; *res = &g_ca.res; return 0; }
static int ca_rel(IMPIVSInterface *inf, void *res) { (void)inf; (void)res; g_ca.rel_res++; return 0; }
static int ca_getp(IMPIVSInterface *inf, void *p) { (void)inf; memcpy(p, g_ca_param, sizeof(g_ca_param)); return 0; }
static int ca_setp(IMPIVSInterface *inf, void *p) { (void)inf; memcpy(g_ca_param, p, sizeof(g_ca_param)); return 0; }
static int ca_flush(IMPIVSInterface *inf) { (void)inf; return 0; }

extern int OpenIMP_FrameSource_DebugBuffers(int chn, int out[4]) __attribute__((weak));
static void ivs_diag(const char *when)
{
    int d[4] = { -1, -1, -1, -1 };

    if (!getenv("APITEST_IVS_DIAG") || !OpenIMP_FrameSource_DebugBuffers) return;
    if (OpenIMP_FrameSource_DebugBuffers(CH_SUB, d) == 0)
        rep(LBL("IVS diag"), 0, V_PASS, "%s: ch%d pool %d frames, ready queue %d, in userspace %d, last pull %d ms ago", when, CH_SUB, d[0], d[1], d[2], d[3]);
    else
        rep(LBL("IVS diag"), 0, V_PASS, "%s: ch%d has no pool", when, CH_SUB);
}

void t_ivs(void)
{
    int r, o0 = 0, o1 = 0, i, got;
    IMPIVSInterface *mv = NULL, *bm = NULL;

    rep_area("ivs");
    if (!IMP_IVS_CreateGroup) { rep_na(FN(IMP_IVS_CreateGroup)); return; }
    /* T30: `enc,ivs` in one process failed the IVS cases while `ivs` alone passed (camera b10/b11).  APITEST_IVS_SETTLE=1 waits
     * until the FrameSource pull window (1 s since the encoder's last pull) is over; APITEST_IVS_DIAG=1 prints the pool of ch1
     * (frames, ready queue, in userspace, ms since the last pull) before and after the result loop. */
    if (getenv("APITEST_IVS_SETTLE")) usleep(1500000);
    ivs_diag("ivs start");
    r = IMP_IVS_CreateGroup(0);
    RET0(IMP_IVS_CreateGroup, r, "group 0");
    if (r) return;

    /* motion detection */
    NEED(IMP_IVS_CreateMoveInterface) {
        IMP_IVS_MoveParam *p = (IMP_IVS_MoveParam *)gnew(sizeof(IMP_IVS_MoveParam));

        p->sense[0] = 4; p->skipFrameCnt = 5; p->frameInfo.width = SUB_W; p->frameInfo.height = SUB_H;
        p->roiRect[0].p0.x = 0; p->roiRect[0].p0.y = 0; p->roiRect[0].p1.x = SUB_W - 1; p->roiRect[0].p1.y = SUB_H - 1;
        p->roiRectCnt = 1;
        mv = IMP_IVS_CreateMoveInterface(p);
        gchk(p);
        rep(FN(IMP_IVS_CreateMoveInterface), mv ? 0 : -1, mv ? V_PASS : V_FAIL, "1 ROI %dx%d, sense 4, skip 5 -> %p", SUB_W, SUB_H, (void *)mv);
        gfree(p);
    }
    if (mv) {
        r = IMP_IVS_CreateChn(0, mv); RET0(IMP_IVS_CreateChn, r, "chn 0 with the move interface");
        r = IMP_IVS_RegisterChn(0, 0); RET0(IMP_IVS_RegisterChn, r, "group 0 chn 0");
        r = IMP_IVS_StartRecvPic(0); RET0(IMP_IVS_StartRecvPic, r, "chn 0");
        if (ivs_bind(0, &o0, 1) == 0) {
            NEED(IMP_IVS_PollingResult) {
                r = IMP_IVS_PollingResult(0, 3000);
                CHECK(IMP_IVS_PollingResult, r, 1, "chn 0 timeout 3000 ms");
                if (r == 0) {
                    void **pres = (void **)gnew(sizeof(void *));
                    r = IMP_IVS_GetResult(0, pres); gchk(pres);
                    if (r == 0 && *pres) {
                        IMP_IVS_MoveOutput *mo = (IMP_IVS_MoveOutput *)*pres;
                        int v = mo->retRoi[0];
                        CHECK(IMP_IVS_GetResult, r, v == 0 || v == 1, "retRoi[0] = %d (scene static: 0 expected unless something moves)", v);
                    } else rep(FN(IMP_IVS_GetResult), r, V_FAIL, "no result pointer");
                    r = IMP_IVS_ReleaseResult(0, *pres);
                    RET0(IMP_IVS_ReleaseResult, r, "");
                    /* more results in a row */
                    got = 0;
                    for (i = 0; i < 8; i++) {
                        if (IMP_IVS_PollingResult(0, 2000) != 0) break;
                        if (IMP_IVS_GetResult(0, pres) != 0) break;
                        IMP_IVS_ReleaseResult(0, *pres);
                        got++;
                    }
                    ivs_diag("after result loop");
                    rep(LBL("IVS move: results keep coming"), 0, got == 8 ? V_PASS : V_FAIL, "%d of 8 further results", got);
                    gfree(pres);
                }
            }
            NEED(IMP_IVS_GetParam) {
                IMP_IVS_MoveParam *g = (IMP_IVS_MoveParam *)gnew(sizeof(IMP_IVS_MoveParam));
                IMP_IVS_MoveParam *n = (IMP_IVS_MoveParam *)gnew(sizeof(IMP_IVS_MoveParam));
                int r2;

                r = IMP_IVS_GetParam(0, g); gchk(g);
                CHECK(IMP_IVS_GetParam, r, g->sense[0] == 4 && g->roiRectCnt == 1, "sense %d roiCnt %d skip %d", g->sense[0], g->roiRectCnt, g->skipFrameCnt);
                NEED(IMP_IVS_SetParam) {
                    *n = *g; n->sense[0] = 2;
                    r2 = IMP_IVS_SetParam(0, n);
                    /* libimp copies the param into the interface and applies it
                     * with the next frame the channel accepts; GetParam reads the
                     * algorithm's copy, so wait for two results first */
                    NEED(IMP_IVS_PollingResult) {
                        void **pr = (void **)gnew(sizeof(void *));
                        int k;
                        for (k = 0; k < 2; k++) {
                            if (IMP_IVS_PollingResult(0, 2000) != 0) break;
                            if (IMP_IVS_GetResult(0, pr) == 0) IMP_IVS_ReleaseResult(0, *pr);
                        }
                        gfree(pr);
                    }
                    r = IMP_IVS_GetParam(0, g); gchk(g);
                    CHECK(IMP_IVS_SetParam, r2, r == 0 && g->sense[0] == 2, "sense 2, read back %d", g->sense[0]);
                }
                gfree(g); gfree(n);
            }
            ivs_unbind(0, o0);
        }
        r = IMP_IVS_StopRecvPic(0); RET0(IMP_IVS_StopRecvPic, r, "chn 0");
        r = IMP_IVS_UnRegisterChn(0); RET0(IMP_IVS_UnRegisterChn, r, "chn 0");
        r = IMP_IVS_DestroyChn(0); RET0(IMP_IVS_DestroyChn, r, "chn 0");
        NEED(IMP_IVS_DestroyMoveInterface) { IMP_IVS_DestroyMoveInterface(mv); rep(FN(IMP_IVS_DestroyMoveInterface), 0, V_PASS, "void function"); }
    }

    /* base move (SAD based) */
    NEED(IMP_IVS_CreateBaseMoveInterface) {
        IMP_IVS_BaseMoveParam *p = (IMP_IVS_BaseMoveParam *)gnew(sizeof(IMP_IVS_BaseMoveParam));

        p->skipFrameCnt = 5; p->referenceNum = 1; p->sadMode = 0; p->sense = 3;
        p->frameInfo.width = SUB_W; p->frameInfo.height = SUB_H;
        bm = IMP_IVS_CreateBaseMoveInterface(p);
        gchk(p);
        rep(FN(IMP_IVS_CreateBaseMoveInterface), bm ? 0 : -1, bm ? V_PASS : V_FAIL, "skip 5 ref 1 sad 0 sense 3 -> %p", (void *)bm);
        gfree(p);
    }
    if (bm) {
        r = IMP_IVS_CreateGroup(1); if (r) rep(FN(IMP_IVS_CreateGroup), r, V_FAIL, "group 1");
        r = IMP_IVS_CreateChn(1, bm); if (r) rep(FN(IMP_IVS_CreateChn), r, V_FAIL, "chn 1 base move");
        r = IMP_IVS_RegisterChn(1, 1); if (r) rep(FN(IMP_IVS_RegisterChn), r, V_FAIL, "group 1 chn 1");
        r = IMP_IVS_StartRecvPic(1); if (r) rep(FN(IMP_IVS_StartRecvPic), r, V_FAIL, "chn 1");
        if (ivs_bind(1, &o1, 0) == 0) {
            r = IMP_IVS_PollingResult(1, 3000);
            if (r == 0) {
                void **pres = (void **)gnew(sizeof(void *));
                r = IMP_IVS_GetResult(1, pres); gchk(pres);
                if (r == 0 && *pres) {
                    IMP_IVS_BaseMoveOutput *bo = (IMP_IVS_BaseMoveOutput *)*pres;
                    rep(LBL("IMP_IVS_GetResult (base move)"), r, V_PASS, "ret %d datalen %d data %p", bo->ret, bo->datalen, (void *)bo->data);
                    IMP_IVS_ReleaseResult(1, *pres);
                } else rep(LBL("IMP_IVS_GetResult (base move)"), r, V_FAIL, "no result");
                gfree(pres);
            } else rep(LBL("IMP_IVS_PollingResult (base move)"), r, V_FAIL, "no result within 3 s");
            ivs_unbind(1, o1);
        }
        IMP_IVS_StopRecvPic(1); IMP_IVS_UnRegisterChn(1); IMP_IVS_DestroyChn(1);
        r = IMP_IVS_DestroyGroup(1); if (r) rep(FN(IMP_IVS_DestroyGroup), r, V_FAIL, "group 1");
        NEED(IMP_IVS_DestroyBaseMoveInterface) { IMP_IVS_DestroyBaseMoveInterface(bm); rep(FN(IMP_IVS_DestroyBaseMoveInterface), 0, V_PASS, "void function"); }
    }
    /* custom algorithm (IMPIVSInterface implemented here, as a streamer or an algorithm vendor does): processAsync gets the
     * frame, measures it, releases it with IMP_IVS_ReleaseData(frame->virAddr) and leaves a result for GetResult/ReleaseResult */
    NEED(IMP_IVS_ReleaseData) {
        IMPIVSInterface *ci = &g_ca_if;

        memset(ci, 0, sizeof(*ci));
        memset((void *)&g_ca, 0, sizeof(g_ca));
        pthread_mutex_init(&g_ca_mx, NULL);
        ci->param = &g_ca_param; ci->paramSize = sizeof(g_ca_param); ci->pixfmt = PIX_FMT_NV12;
        ci->init = ca_init; ci->exit = ca_exit; ci->preProcessSync = ca_pre; ci->processAsync = ca_proc;
        ci->getResult = ca_get; ci->releaseResult = ca_rel; ci->getParam = ca_getp; ci->setParam = ca_setp;
        ci->flushFrame = ca_flush;
        r = IMP_IVS_CreateGroup(1); if (r) rep(FN(IMP_IVS_CreateGroup), r, V_FAIL, "group 1 (custom algorithm)");
        if (r) { rep(FN(IMP_IVS_ReleaseData), r, V_FAIL, "no group for the custom algorithm"); }
        else {
            r = IMP_IVS_CreateChn(2, ci); if (r) rep(FN(IMP_IVS_CreateChn), r, V_FAIL, "chn 2 custom algorithm");
            r = IMP_IVS_RegisterChn(1, 2); if (r) rep(FN(IMP_IVS_RegisterChn), r, V_FAIL, "group 1 chn 2");
            r = IMP_IVS_StartRecvPic(2); if (r) rep(FN(IMP_IVS_StartRecvPic), r, V_FAIL, "chn 2");
            if (ivs_bind(1, &o1, 0) == 0) {
                void **pres = (void **)gnew(sizeof(void *));

                r = IMP_IVS_PollingResult(2, 4000);
                got = 0;
                if (r == 0) {
                    r = IMP_IVS_GetResult(2, pres); gchk(pres);
                    if (r == 0 && *pres) {
                        struct ca_res *cr = (struct ca_res *)*pres;
                        got = 1;
                        rep(LBL("IMP_IVS_GetResult (custom)"), r, (cr->mean > 0 && cr->w > 0) ? V_PASS : V_FAIL, "own result: frame %ux%u mean luma %d", cr->w, cr->h, cr->mean);
                        r = IMP_IVS_ReleaseResult(2, *pres);
                        rep(LBL("IMP_IVS_ReleaseResult (custom)"), r, (r == 0 && g_ca.rel_res) ? V_PASS : V_FAIL, "releaseResult of the algorithm called %d time(s)", g_ca.rel_res);
                    } else rep(LBL("IMP_IVS_GetResult (custom)"), r, V_FAIL, "no result pointer");
                } else rep(LBL("IMP_IVS_PollingResult (custom)"), r, V_FAIL, "no result within 4 s");
                for (i = 0; i < 20 && g_ca.frames < 5; i++) usleep(200000);
                rep(FN(IMP_IVS_ReleaseData), g_ca.rel_ret,
                    (got && g_ca.frames >= 1 && g_ca.rel_called == g_ca.frames && g_ca.rel_bad == 0 && g_ca.init_called) ? V_PASS : V_FAIL,
                    "processAsync got %d frames, released each with IMP_IVS_ReleaseData(frame->virAddr): %d calls, %d returned non-zero; init %d",
                    g_ca.frames, g_ca.rel_called, g_ca.rel_bad, g_ca.init_called);
                gfree(pres);
                ivs_unbind(1, o1);
            } else rep(FN(IMP_IVS_ReleaseData), -1, V_FAIL, "custom algorithm: no frame source bound");
            IMP_IVS_StopRecvPic(2); IMP_IVS_UnRegisterChn(2); IMP_IVS_DestroyChn(2);
            r = IMP_IVS_DestroyGroup(1); if (r) rep(FN(IMP_IVS_DestroyGroup), r, V_FAIL, "group 1");
            if (!g_ca.exit_called) rep(LBL("IVS custom algorithm exit"), 0, V_FAIL, "DestroyChn did not call the algorithm exit()");
        }
    }
    r = IMP_IVS_DestroyGroup(0);
    RET0(IMP_IVS_DestroyGroup, r, "group 0");
}
