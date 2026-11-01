/* apitest: ISP non-image functions (getters, statistics, running mode round trips).
 * Image-changing tuning functions are covered by tools/imgfx. */
#include <unistd.h>
#include "apitest.h"

/* getter with a guarded out struct: PASS = ret 0 and guards intact; the reason shows fields */
static int skipfn(const char *n)       /* APITEST_SKIPFN=a,b: functions known to oops the driver (FAIL recorded by hand) */
{
    const char *e = getenv("APITEST_SKIPFN"), *p;
    size_t l = strlen(n);
    if (!e) return 0;
    for (p = e; (p = strstr(p, n)); p += l) if ((p == e || p[-1] == ',') && (p[l] == ',' || !p[l])) return 1;
    return 0;
}
#define ISPGET(f, T, ...) \
    NEED(f) if (skipfn(#f)) rep(FN(f), 0, V_SKIP, "APITEST_SKIPFN: oopses the driver"); else { G(T, p); int r_ = f(V0 p); gchk(p); \
        rep(FN(f), r_, r_ == 0 ? V_PASS : V_FAIL, __VA_ARGS__); gfree(p); }
/* getter of a single scalar */
#define ISPGETU(f, T, ...) \
    NEED(f) if (skipfn(#f)) rep(FN(f), 0, V_SKIP, "APITEST_SKIPFN: oopses the driver"); else { G(T, p); int r_ = f(V0 p); gchk(p); \
        rep(FN(f), r_, r_ == 0 ? V_PASS : V_FAIL, __VA_ARGS__); gfree(p); }

void t_isp(void)
{
    int r;

    rep_area("isp");

    /* ----------------------------------------------------------- sensor */
#if HAS_IMP_ISP_Tuning_GetSensorAttr
    ISPGET(IMP_ISP_Tuning_GetSensorAttr, IMPISPSENSORAttr, "hts %u vts %u fps %u %ux%u (argument %dx%d)", p->hts, p->vts, p->fps, p->width, p->height, g_sw, g_sh)
#endif
#if defined(APITEST_T40)
    NEED(IMP_ISP_GetSensorRegister) {
        G(uint32_t, reg); G(uint32_t, v);
        r = IMP_ISP_GetSensorRegister(IMPVI_MAIN, reg, v); gchk(v);
        RET0(IMP_ISP_GetSensorRegister, r, "reg 0x0 = 0x%x", *v);
        gfree(reg); gfree(v);
    }
#elif !defined(PLATFORM_T41)
    NEED(IMP_ISP_GetSensorRegister) if (skipfn("IMP_ISP_GetSensorRegister")) rep(FN(IMP_ISP_GetSensorRegister), 0, V_SKIP, "APITEST_SKIPFN: oopses the driver"); else {
        G(uint32_t, v);
        r = IMP_ISP_GetSensorRegister(0x0, v); gchk(v);
        RET0(IMP_ISP_GetSensorRegister, r, "reg 0x0 = 0x%x", *v);
        gfree(v);
    }
#else
    NEED(IMP_ISP_GetSensorRegister) {
        G(IMPISPSensorRegister, v);
        v->addr = 0x0;
        r = IMP_ISP_GetSensorRegister(IMPVI_MAIN, v); gchk(v);
        RET0(IMP_ISP_GetSensorRegister, r, "reg 0x0 read");
        gfree(v);
    }
#endif
    rep(LBL("IMP_ISP_SetSensorRegister"), 0, V_SKIP, "writes the sensor");

    /* ---------------------------------------------- fps / running mode / flicker round trips */
#if defined(APITEST_T40)
    NEED(IMP_ISP_Tuning_GetSensorFPS) {
        G(uint32_t, n); G(uint32_t, d);
        r = IMP_ISP_Tuning_GetSensorFPS(IMPVI_MAIN, n, d); gchk(n); gchk(d);
        CHECK(IMP_ISP_Tuning_GetSensorFPS, r, *n > 0 && *d > 0, "%u/%u", *n, *d);
        NEED(IMP_ISP_Tuning_SetSensorFPS) {
            G(uint32_t, n2); G(uint32_t, d2);
            int r2 = IMP_ISP_Tuning_SetSensorFPS(IMPVI_MAIN, n, d);
            IMP_ISP_Tuning_GetSensorFPS(IMPVI_MAIN, n2, d2); gchk(n2); gchk(d2);
            CHECK(IMP_ISP_Tuning_SetSensorFPS, r2, *n2 == *n && *d2 == *d, "same %u/%u written, read back %u/%u", *n, *d, *n2, *d2);
            gfree(n2); gfree(d2);
        }
        gfree(n); gfree(d);
    }
#elif !defined(PLATFORM_T41)
    NEED(IMP_ISP_Tuning_GetSensorFPS) {
        G(uint32_t, n); G(uint32_t, d);
        r = IMP_ISP_Tuning_GetSensorFPS(n, d); gchk(n); gchk(d);
        CHECK(IMP_ISP_Tuning_GetSensorFPS, r, *n > 0 && *d > 0, "%u/%u", *n, *d);
        NEED(IMP_ISP_Tuning_SetSensorFPS) {
            G(uint32_t, n2); G(uint32_t, d2);
            int r2 = IMP_ISP_Tuning_SetSensorFPS(*n, *d);
            IMP_ISP_Tuning_GetSensorFPS(n2, d2); gchk(n2); gchk(d2);
            CHECK(IMP_ISP_Tuning_SetSensorFPS, r2, *n2 == *n && *d2 == *d, "same %u/%u written, read back %u/%u", *n, *d, *n2, *d2);
            gfree(n2); gfree(d2);
        }
        gfree(n); gfree(d);
    }
#else
    NEED(IMP_ISP_Tuning_GetSensorFPS) {
        G(IMPISPSensorFps, f);
        r = IMP_ISP_Tuning_GetSensorFPS(IMPVI_MAIN, f); gchk(f);
        CHECK(IMP_ISP_Tuning_GetSensorFPS, r, f->num > 0 && f->den > 0, "%u/%u", f->num, f->den);
        NEED(IMP_ISP_Tuning_SetSensorFPS) {
            G(IMPISPSensorFps, f2);
            int r2 = IMP_ISP_Tuning_SetSensorFPS(IMPVI_MAIN, f);
            IMP_ISP_Tuning_GetSensorFPS(IMPVI_MAIN, f2); gchk(f2);
            CHECK(IMP_ISP_Tuning_SetSensorFPS, r2, f2->num == f->num && f2->den == f->den, "same %u/%u written, read back %u/%u", f->num, f->den, f2->num, f2->den);
            gfree(f2);
        }
        gfree(f);
    }
#endif
    NEED(IMP_ISP_Tuning_GetISPRunningMode) {
        G(IMPISPRunningMode, m);
        r = IMP_ISP_Tuning_GetISPRunningMode(V0 m); gchk(m);
        CHECK(IMP_ISP_Tuning_GetISPRunningMode, r, *m == (IMPISPRunningMode)g_mode_night, "mode %d (%s requested at start)", (int)*m, g_mode_night ? "night" : "day");
        NEED(IMP_ISP_Tuning_SetISPRunningMode) {
            G(IMPISPRunningMode, m2);
            IMPISPRunningMode other = *m == IMPISP_RUNNING_MODE_DAY ? IMPISP_RUNNING_MODE_NIGHT : IMPISP_RUNNING_MODE_DAY, back = *m;
            int r2, r3;
#ifdef PLATFORM_T41
            r2 = IMP_ISP_Tuning_SetISPRunningMode(IMPVI_MAIN, &other);
#else
            r2 = IMP_ISP_Tuning_SetISPRunningMode(other);
#endif
            r3 = IMP_ISP_Tuning_GetISPRunningMode(V0 m2); gchk(m2);
            CHECK(IMP_ISP_Tuning_SetISPRunningMode, r2, r3 == 0 && *m2 == other, "set %d, read back %d", (int)other, (int)*m2);
#ifdef PLATFORM_T41
            IMP_ISP_Tuning_SetISPRunningMode(IMPVI_MAIN, &back);
#else
            IMP_ISP_Tuning_SetISPRunningMode(back);
#endif
            sleep(1);
            gfree(m2);
        }
        gfree(m);
    }
    NEED(IMP_ISP_Tuning_GetAntiFlickerAttr) {
#ifdef PLATFORM_T41
        G(IMPISPAntiflickerAttr, a);
        r = IMP_ISP_Tuning_GetAntiFlickerAttr(IMPVI_MAIN, a); gchk(a);
        RET0(IMP_ISP_Tuning_GetAntiFlickerAttr, r, "mode %d freq %u", (int)a->mode, a->freq);
        NEED(IMP_ISP_Tuning_SetAntiFlickerAttr) {
            G(IMPISPAntiflickerAttr, a2);
            int r2 = IMP_ISP_Tuning_SetAntiFlickerAttr(IMPVI_MAIN, a);
            IMP_ISP_Tuning_GetAntiFlickerAttr(IMPVI_MAIN, a2); gchk(a2);
            CHECK(IMP_ISP_Tuning_SetAntiFlickerAttr, r2, a2->mode == a->mode && a2->freq == a->freq, "same mode %d freq %u written, read back %d/%u", (int)a->mode, a->freq, (int)a2->mode, a2->freq);
            gfree(a2);
        }
        gfree(a);
#else
        G(IMPISPAntiflickerAttr, a);
        r = IMP_ISP_Tuning_GetAntiFlickerAttr(a); gchk(a);
        RET0(IMP_ISP_Tuning_GetAntiFlickerAttr, r, "attr %d", (int)*a);
        NEED(IMP_ISP_Tuning_SetAntiFlickerAttr) {
            G(IMPISPAntiflickerAttr, a2);
            int r2 = IMP_ISP_Tuning_SetAntiFlickerAttr(*a);
            IMP_ISP_Tuning_GetAntiFlickerAttr(a2); gchk(a2);
            CHECK(IMP_ISP_Tuning_SetAntiFlickerAttr, r2, *a2 == *a, "same %d written, read back %d", (int)*a, (int)*a2);
            gfree(a2);
        }
        gfree(a);
#endif
    }
#if HAS_IMP_ISP_Tuning_GetMaxAgain && !defined(PLATFORM_T41)
    NEED(IMP_ISP_Tuning_GetMaxAgain) {
        G(uint32_t, g);
        r = IMP_ISP_Tuning_GetMaxAgain(g); gchk(g);
        RET0(IMP_ISP_Tuning_GetMaxAgain, r, "%u", *g);
        NEED(IMP_ISP_Tuning_SetMaxAgain) {
            G(uint32_t, g2);
            int r2 = IMP_ISP_Tuning_SetMaxAgain(*g);
            IMP_ISP_Tuning_GetMaxAgain(g2); gchk(g2);
            CHECK(IMP_ISP_Tuning_SetMaxAgain, r2, *g2 == *g, "same %u written, read back %u", *g, *g2);
            gfree(g2);
        }
        gfree(g);
    }
    NEED(IMP_ISP_Tuning_GetMaxDgain) {
        G(uint32_t, g);
        r = IMP_ISP_Tuning_GetMaxDgain(g); gchk(g);
        RET0(IMP_ISP_Tuning_GetMaxDgain, r, "%u", *g);
        NEED(IMP_ISP_Tuning_SetMaxDgain) {
            G(uint32_t, g2);
            int r2 = IMP_ISP_Tuning_SetMaxDgain(*g);
            IMP_ISP_Tuning_GetMaxDgain(g2); gchk(g2);
            CHECK(IMP_ISP_Tuning_SetMaxDgain, r2, *g2 == *g, "same %u written, read back %u", *g, *g2);
            gfree(g2);
        }
        gfree(g);
    }
#endif

    /* -------------------------------------------- exposure / white balance */
#if HAS_IMP_ISP_Tuning_GetExpr
    ISPGET(IMP_ISP_Tuning_GetExpr, IMPISPExpr, "mode %d it %u (min %u max %u) one line %u us", (int)p->g_attr.mode, p->g_attr.integration_time, p->g_attr.integration_time_min, p->g_attr.integration_time_max, p->g_attr.one_line_expr_in_us)
#endif
#if HAS_IMP_ISP_Tuning_GetTotalGain
    ISPGETU(IMP_ISP_Tuning_GetTotalGain, uint32_t, "%u", *p)
#endif
#if HAS_IMP_ISP_Tuning_GetEVAttr
    ISPGET(IMP_ISP_Tuning_GetEVAttr, IMPISPEVAttr, "ev %u expr %u us again %u dgain %u", p->ev, p->expr_us, p->again, p->dgain)
#endif
#if HAS_IMP_ISP_Tuning_GetIntegrationTime
    ISPGET(IMP_ISP_Tuning_GetIntegrationTime, IMPISPITAttr, "mode %d it %u max %u", (int)p->mode, p->integration_time, p->max_integration_time)
#endif
#if HAS_IMP_ISP_Tuning_GetWB
    ISPGET(IMP_ISP_Tuning_GetWB, IMPISPWB, "mode %d rgain %u bgain %u", (int)p->mode, p->rgain, p->bgain)
#endif
#if HAS_IMP_ISP_Tuning_GetWB_Statis
    ISPGET(IMP_ISP_Tuning_GetWB_Statis, IMPISPWB, "rgain %u bgain %u", p->rgain, p->bgain)
#endif
#if HAS_IMP_ISP_Tuning_GetWB_GOL_Statis
    ISPGET(IMP_ISP_Tuning_GetWB_GOL_Statis, IMPISPWB, "rgain %u bgain %u", p->rgain, p->bgain)
#endif
/* T40: oopsed the driver up to open-tx-isp 8d3504c8 (agg47, cid 0x0800009b, epc c03d1920); fixed on
 * claude/gate-1026 - on older T40 drivers run with APITEST_SKIPFN=IMP_ISP_Tuning_Awb_GetRgbCoefft */
#if HAS_IMP_ISP_Tuning_Awb_GetRgbCoefft
# ifdef PLATFORM_T41
    ISPGET(IMP_ISP_Tuning_Awb_GetRgbCoefft, IMPISPCoefftWb, "r %u g %u b %u", p->rgb_coefft_wb_r, p->rgb_coefft_wb_g, p->rgb_coefft_wb_b)
# else
    ISPGET(IMP_ISP_Tuning_Awb_GetRgbCoefft, IMPISPCOEFFTWB, "r %u g %u b %u", p->rgb_coefft_wb_r, p->rgb_coefft_wb_g, p->rgb_coefft_wb_b)
# endif
#endif
#if HAS_IMP_ISP_Tuning_GetAWBCt
    ISPGETU(IMP_ISP_Tuning_GetAWBCt, unsigned int, "color temperature %u K", *p)
#endif
#if HAS_IMP_ISP_Tuning_GetAwbClust
    ISPGET(IMP_ISP_Tuning_GetAwbClust, IMPISPAWBCluster, "cluster en %d tol en %d th %u [0] %u", (int)p->ClusterEn, (int)p->ToleranceEn, p->tolerance_th, p->awb_cluster[0])
#endif
#if HAS_IMP_ISP_Tuning_GetAwbCtTrend && !defined(PLATFORM_T41)
    ISPGET(IMP_ISP_Tuning_GetAwbCtTrend, IMPISPAWBCtTrend, "trend [0] %u [5] %u", p->trend_array[0], p->trend_array[5])
#endif

    /* ------------------------------------------------- AE / AF / AWB stats */
#if HAS_IMP_ISP_Tuning_GetAeLuma
    ISPGETU(IMP_ISP_Tuning_GetAeLuma, int, "luma %d", *p)
#endif
#if HAS_IMP_ISP_Tuning_GetAeMin
    ISPGET(IMP_ISP_Tuning_GetAeMin, IMPISPAEMin, "min it %u min again %u", p->min_it, p->min_again)
#endif
#if HAS_IMP_ISP_Tuning_GetAE_IT_MAX
    ISPGETU(IMP_ISP_Tuning_GetAE_IT_MAX, unsigned int, "max integration time %u", *p)
#endif
#if HAS_IMP_ISP_Tuning_GetAeState
    ISPGET(IMP_ISP_Tuning_GetAeState, IMPISPAEState, "stable %d target %u mean %u", (int)p->stable, p->target, p->ae_mean)
#endif
#if HAS_IMP_ISP_Tuning_GetAeAttr
    ISPGET(IMP_ISP_Tuning_GetAeAttr, IMPISPAEAttr, "freeze %d it manual %d it %u again %u", (int)p->AeFreezenEn, (int)p->AeItManualEn, p->AeIt, p->AeAGain)
#endif
#if HAS_IMP_ISP_Tuning_GetAeTargetList
    ISPGET(IMP_ISP_Tuning_GetAeTargetList, IMPISPAETargetList, "targets %u %u %u ...", p->at_list[0], p->at_list[1], p->at_list[2])
#endif
#if HAS_IMP_ISP_Tuning_GetAeStrategy
    ISPGETU(IMP_ISP_Tuning_GetAeStrategy, IMPISPAeStrategy, "strategy %d", (int)*p)
#endif
#if HAS_IMP_ISP_Tuning_GetBlcAttr && defined(PLATFORM_T23)
    /* stock T23 1.3.0: the BLC attribute control (0x80000a5) is not handled by the module, the call returns -1 */
    NEED(IMP_ISP_Tuning_GetBlcAttr) {
        G(IMPISPBlcAttr, p);
        r = IMP_ISP_Tuning_GetBlcAttr(p); gchk(p);
        rep(FN(IMP_ISP_Tuning_GetBlcAttr), r, r == -1 ? V_NA : (r == 0 ? V_PASS : V_FAIL), "stock T23 has no BLC attr control (ret %d)", r);
        gfree(p);
    }
#elif HAS_IMP_ISP_Tuning_GetBlcAttr
    ISPGET(IMP_ISP_Tuning_GetBlcAttr, IMPISPBlcAttr, "r %u gr %u gb %u b %u", p->black_level_r, p->black_level_gr, p->black_level_gb, p->black_level_b)
#endif
#if HAS_IMP_ISP_Tuning_GetAeHist
    ISPGET(IMP_ISP_Tuning_GetAeHist, IMPISPAEHist, "hist %u %u %u %u %u, nodes %ux%u", p->ae_hist[0], p->ae_hist[1], p->ae_hist[2], p->ae_hist[3], p->ae_hist[4], p->ae_stat_nodeh, p->ae_stat_nodev)
#endif
#if HAS_IMP_ISP_Tuning_GetAwbHist
    ISPGET(IMP_ISP_Tuning_GetAwbHist, IMPISPAWBHist, "stat r %u b %u sum %u, nodes %ux%u", p->awb_stat.r_gain, p->awb_stat.b_gain, p->awb_stat.awb_sum, p->awb_stat_nodeh, p->awb_stat_nodev)
#endif
#if HAS_IMP_ISP_Tuning_GetAfHist
    ISPGET(IMP_ISP_Tuning_GetAfHist, IMPISPAFHist, "nodes %ux%u", p->af_stat_nodeh, p->af_stat_nodev)
#endif
#if HAS_IMP_ISP_Tuning_GetAFMetrices
    ISPGETU(IMP_ISP_Tuning_GetAFMetrices, unsigned int, "metric %u", *p)
#endif
#if HAS_IMP_ISP_Tuning_GetAeWeight && !defined(PLATFORM_T41)
    ISPGET(IMP_ISP_Tuning_GetAeWeight, IMPISPWeight, "weight[7][7] %u", p->weight[7][7])
#endif
#if HAS_IMP_ISP_Tuning_GetAwbWeight
    ISPGET(IMP_ISP_Tuning_GetAwbWeight, IMPISPWeight, "weight[7][7] %u", p->weight[7][7])
#endif
#if HAS_IMP_ISP_Tuning_GetAfWeight
    ISPGET(IMP_ISP_Tuning_GetAfWeight, IMPISPWeight, "weight[7][7] %u", p->weight[7][7])
#endif
#if HAS_IMP_ISP_Tuning_AE_GetROI
# if defined(PLATFORM_T10) || defined(PLATFORM_T20) || defined(PLATFORM_T30)
    ISPGET(IMP_ISP_Tuning_AE_GetROI, IMPISPAERoi, "roi 0x%08x", p->value)
# elif !defined(PLATFORM_T41)
    ISPGET(IMP_ISP_Tuning_AE_GetROI, IMPISPWeight, "roi weight[7][7] %u", p->weight[7][7])
# endif
#endif
#if HAS_IMP_ISP_Tuning_GetAeZone
# if defined(PLATFORM_T10) || defined(PLATFORM_T20)
    /* the vendor T20 3.12.0 libimp sends {1, 0x800002f, ptr} and the stock tx-isp-t20 kernel module has no case for
     * 0x800002f in isp_core_ops_g_ctrl (jump table entry 47 = the -1 default): the vendor stack returns -1 too */
    NEED(IMP_ISP_Tuning_GetAeZone) { G(IMPISPAEZone, p); int r_ = IMP_ISP_Tuning_GetAeZone(V0 p); gchk(p);
        if (r_ == 0) rep(FN(IMP_ISP_Tuning_GetAeZone), r_, V_PASS, "zone[0] %u zone[112] %u", p->ae_sta_zone[0], p->ae_sta_zone[112]);
        else rep(FN(IMP_ISP_Tuning_GetAeZone), r_, V_NA, "-1 as on the vendor stack: the stock T10/T20 kernel module does not serve cid 0x800002f");
        gfree(p); }
# else
    ISPGET(IMP_ISP_Tuning_GetAeZone, IMPISPZone, "zone[7][7] %u", p->zone[7][7])
# endif
#endif
#if HAS_IMP_ISP_Tuning_GetAfZone
    ISPGET(IMP_ISP_Tuning_GetAfZone, IMPISPZone, "zone[7][7] %u", p->zone[7][7])
#endif
#if HAS_IMP_ISP_Tuning_GetAwbZone
# if defined(PLATFORM_T10) || defined(PLATFORM_T20)
    ISPGET(IMP_ISP_Tuning_GetAwbZone, IMPISPAWBZone, "zone[7][7] rg %u bg %u", p->awb_sta_zone[7][7].red_green, p->awb_sta_zone[7][7].blue_green)
# else
    ISPGET(IMP_ISP_Tuning_GetAwbZone, IMPISPAWBZone, "zone r[112] %u g %u b %u", p->zone_r[112], p->zone_g[112], p->zone_b[112])
# endif
#endif

    /* --------------------------------------------------- misc (new families) */
#if HAS_IMP_ISP_GetDefaultBinPath
    NEED(IMP_ISP_GetDefaultBinPath) {
        char *path = (char *)gnew(256);
        r = IMP_ISP_GetDefaultBinPath(V0 path); gchk(path);
        /* the driver only reports a path that SetDefaultBinPath stored: empty is the normal default */
#if defined(PLATFORM_T41) || defined(PLATFORM_T40)
        /* vendor: -1 when no path was stored (review N8) */
        if (r != 0) rep(FN(IMP_ISP_GetDefaultBinPath), r, V_NA, "-1 as the vendor: no default bin path stored");
        else
#endif
        CHECK(IMP_ISP_GetDefaultBinPath, r, 1, "\"%.100s\" (empty: none set)", path);
        gfree(path);
    }
#endif
#if HAS_IMP_ISP_GetFrameDrop
    ISPGET(IMP_ISP_GetFrameDrop, IMPISPFrameDropAttr, "ch0 enable %d lsize %u, ch1 enable %d, ch2 enable %d", (int)p->fdrop[0].enable, (unsigned)p->fdrop[0].lsize, (int)p->fdrop[1].enable, (int)p->fdrop[2].enable)
#endif

    /* ------------------------------ T10/T20/T21 calls of the 2026-10-10 gap work */
#if defined(PLATFORM_T10) || defined(PLATFORM_T20) || defined(PLATFORM_T21)
    NEED(IMP_ISP_Tuning_SetDPStrength) {
        r = IMP_ISP_Tuning_SetDPStrength(100);          /* 100 % = tuning file */
        RET0(IMP_ISP_Tuning_SetDPStrength, r, "100 %% (no getter)");
    }
    NEED(IMP_ISP_Tuning_SetAntiFogAttr) {
        r = IMP_ISP_Tuning_SetAntiFogAttr(0);
        RET0(IMP_ISP_Tuning_SetAntiFogAttr, r, "disable");
    }
# if defined(PLATFORM_T10) || defined(PLATFORM_T20)
    NEED(IMP_ISP_Tuning_GetISPHVflip) {
        G(IMPISPTuningOpsMode, h); G(IMPISPTuningOpsMode, v);
        r = IMP_ISP_Tuning_GetISPHVflip(h, v); gchk(h); gchk(v);
        RET0(IMP_ISP_Tuning_GetISPHVflip, r, "h %d v %d", (int)*h, (int)*v);
        NEED(IMP_ISP_Tuning_SetISPHVflip) {
            G(IMPISPTuningOpsMode, h2); G(IMPISPTuningOpsMode, v2);
            IMPISPTuningOpsMode h0 = *h, v0 = *v;
            int r2 = IMP_ISP_Tuning_SetISPHVflip(h0 ? IMPISP_TUNING_OPS_MODE_DISABLE : IMPISP_TUNING_OPS_MODE_ENABLE,
                                                 v0 ? IMPISP_TUNING_OPS_MODE_DISABLE : IMPISP_TUNING_OPS_MODE_ENABLE);
            int r3 = IMP_ISP_Tuning_GetISPHVflip(h2, v2); gchk(h2); gchk(v2);
            CHECK(IMP_ISP_Tuning_SetISPHVflip, r2, r3 == 0 && *h2 == !h0 && *v2 == !v0,
                  "from h %d v %d to h %d v %d, read back h %d v %d", (int)h0, (int)v0, (int)!h0, (int)!v0, (int)*h2, (int)*v2);
            IMP_ISP_Tuning_SetISPHVflip(h0, v0);        /* back */
            usleep(300000);
            gfree(h2); gfree(v2);
        }
        gfree(h); gfree(v);
    }
    NEED(IMP_ISP_Tuning_GetWDRAttr) {
        G(IMPISPTuningOpsMode, w);
        r = IMP_ISP_Tuning_GetWDRAttr(w); gchk(w);
        RET0(IMP_ISP_Tuning_GetWDRAttr, r, "mode %d", (int)*w);
        NEED(IMP_ISP_Tuning_SetWDRAttr) {
            /* the unchanged mode is answered from the vendor's cache (no driver call) */
            int r2 = IMP_ISP_Tuning_SetWDRAttr(*w);
            RET0(IMP_ISP_Tuning_SetWDRAttr, r2, "same mode %d written", (int)*w);
        }
        gfree(w);
    }
# endif
#endif

    /* -------------------------------------------------------------- T41 */
#ifdef PLATFORM_T41
#if HAS_IMP_ISP_Tuning_GetModule_Ratio
    ISPGET(IMP_ISP_Tuning_GetModule_Ratio, IMPISPModuleRatioAttr, "module ratio struct read")
#endif
#if HAS_IMP_ISP_Tuning_GetStatisConfig
    ISPGET(IMP_ISP_Tuning_GetStatisConfig, IMPISPStatisConfig, "statis config read")
#endif
#if HAS_IMP_ISP_Tuning_GetAeWeight
    ISPGET(IMP_ISP_Tuning_GetAeWeight, IMPISPAEWeightAttr, "ae weight struct read")
#endif
#if HAS_IMP_ISP_Tuning_GetAeStatistics
    ISPGET(IMP_ISP_Tuning_GetAeStatistics, IMPISPAEStatisInfo, "ae statistics read")
#endif
#if HAS_IMP_ISP_Tuning_GetAeExprInfo
    ISPGET(IMP_ISP_Tuning_GetAeExprInfo, IMPISPAEExprInfo, "ae expr info read")
#endif
#if HAS_IMP_ISP_Tuning_GetAeScenceAttr
    ISPGET(IMP_ISP_Tuning_GetAeScenceAttr, IMPISPAEScenceAttr, "ae scene attr read")
#endif
#if HAS_IMP_ISP_Tuning_GetAwbStatistics
    ISPGET(IMP_ISP_Tuning_GetAwbStatistics, IMPISPAWBStatisInfo, "awb statistics read")
#endif
#if HAS_IMP_ISP_Tuning_GetAwbGlobalStatistics
    ISPGET(IMP_ISP_Tuning_GetAwbGlobalStatistics, IMPISPAWBGlobalStatisInfo, "awb global statistics read")
#endif
#if HAS_IMP_ISP_Tuning_GetAwbAttr
    ISPGET(IMP_ISP_Tuning_GetAwbAttr, IMPISPWBAttr, "awb attr read")
#endif
#if HAS_IMP_ISP_Tuning_GetAfStatistics
    ISPGET(IMP_ISP_Tuning_GetAfStatistics, IMPISPAFStatisInfo, "af statistics read")
#endif
#if HAS_IMP_ISP_Tuning_GetAFMetricesInfo
    ISPGET(IMP_ISP_Tuning_GetAFMetricesInfo, IMPISPAFMetricsInfo, "af metrics read")
#endif
#if HAS_IMP_ISP_Tuning_GetAEFlickerFlag
    ISPGET(IMP_ISP_Tuning_GetAEFlickerFlag, IMPISPFlickerFlag, "flicker flag read")
#endif
#if HAS_IMP_ISP_Tuning_GetAeBv
    ISPGETU(IMP_ISP_Tuning_GetAeBv, int, "bv %d", *p)
#endif
#if HAS_IMP_ISP_Tuning_GetAeAtList
    ISPGET(IMP_ISP_Tuning_GetAeAtList, IMPISPAeAtList, "ae at list read")
#endif
#if HAS_IMP_ISP_Tuning_GetAeEvList
    ISPGET(IMP_ISP_Tuning_GetAeEvList, IMPISPAeEvList, "ae ev list read")
#endif
#if HAS_IMP_ISP_Tuning_GetAeExpList
    ISPGET(IMP_ISP_Tuning_GetAeExpList, IMPISPAeExpListAttr, "ae exp list read")
#endif
#if HAS_IMP_ISP_Tuning_GetAwbCtTrendOffset
    ISPGET(IMP_ISP_Tuning_GetAwbCtTrendOffset, IMPISPAwbCtTrendOffset, "awb ct trend offset read")
#endif
#if HAS_IMP_ISP_Tuning_GetAeConvergeStep
    ISPGET(IMP_ISP_Tuning_GetAeConvergeStep, IMPISPAeConvergeStep, "ae converge step read")
#endif
#if HAS_IMP_ISP_Tuning_GetAwbConvergeStep
    ISPGET(IMP_ISP_Tuning_GetAwbConvergeStep, IMPISPAwbConvergeStep, "awb converge step read")
#endif
#if HAS_IMP_ISP_Tuning_GetWdrOutputMode
    ISPGET(IMP_ISP_Tuning_GetWdrOutputMode, IMPISPWdrOutputMode, "wdr output mode read")
#endif
#if HAS_IMP_ISP_Tuning_GetTmoCurve
    ISPGET(IMP_ISP_Tuning_GetTmoCurve, IMPISPTmoCurve, "tmo curve read")
#endif
#if HAS_IMP_ISP_GetISPBypass
    ISPGET(IMP_ISP_GetISPBypass, IMPISPTuningOpsMode, "bypass %d", (int)*p)
#endif
#if HAS_IMP_ISP_GetInternalChnAttr
    ISPGET(IMP_ISP_GetInternalChnAttr, IMPISPInternalChnAttr, "internal channel attr read")
#endif
#if HAS_IMP_ISP_GetCsccrMode
    ISPGET(IMP_ISP_GetCsccrMode, IMPISPCsccrModeAttr, "csccr mode read")
#endif
#if HAS_IMP_ISP_LDC_GetAttr
    ISPGET(IMP_ISP_LDC_GetAttr, IMPISPLDCAttr, "ldc attr read")
#endif
#if HAS_IMP_ISP_Tuning_GetFaceAe
    ISPGET(IMP_ISP_Tuning_GetFaceAe, IMPISPFaceAttr, "face ae read")
#endif
#if HAS_IMP_ISP_Tuning_GetFaceAeLuma
    ISPGET(IMP_ISP_Tuning_GetFaceAeLuma, IMPISPFaceAeLuma, "face ae luma read")
#endif
#endif
    (void)r;
}
