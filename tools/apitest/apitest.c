/* apitest - device test: every non-image vendor IMP/SU function that libimp exports.
 *
 *   apitest SENSOR I2C_ADDR W H [areas]       areas = comma list of
 *                                              sys,fs,enc,osd,ivs,audio,isp,su (default all)
 *   apitest --list                            vendor functions this build exercises
 *   apitest --areas | --cases                 area names / case names (area "cases", APITEST_CASES=a,b)
 *   apitest [--json FILE] SENSOR I2C W H ...  also write the run as one JSON report (function -> verdict + detail)
 *   apitest SENSOR I2C W H picture OUTDIR [filter]   the picture (former imgfx) cases; env IMGFX_STATS=1 ...
 *   apitest SENSOR I2C W H t40dev [mask]             T40 only: the former t40_devtest phases
 *
 * One line per function:   [A] <function> ret=<r> <PASS|FAIL|N/A|SKIP> <reason>
 * "FAIL OVERWRITE" = the function wrote outside the vendor struct of this SoC
 * (guard bytes around every out buffer).  Run with the streamer stopped.
 */
#include "apitest.h"

int g_pass, g_fail, g_na, g_skip;
int g_overwrites;
int g_sw, g_sh, g_mode_night;
int g_fps = 15;          /* the sensor rate in effect after SetSensorFPS 15/1 (some sensor drivers keep their only mode) */
IMPSensorInfo g_sensor;

/* ---------------------------------------------------------------- guards */
#define GUARD 64
#define GHDR 16
static int g_ovr_pending;

void *gnew(size_t sz)
{
    unsigned char *b = calloc(1, GHDR + GUARD + sz + GUARD);

    if (!b) { fprintf(stderr, "[E] out of memory\n"); exit(2); }
    *(size_t *)b = sz;
    memset(b + GHDR, 0xA5, GUARD);
    memset(b + GHDR + GUARD + sz, 0xA5, GUARD);
    return b + GHDR + GUARD;
}

static int gcheck(void *p)
{
    unsigned char *b = (unsigned char *)p - GUARD - GHDR;
    size_t sz = *(size_t *)b, i;
    int bad = 0;

    for (i = 0; i < GUARD; i++) {
        if (b[GHDR + i] != 0xA5) bad |= 1;
        if (b[GHDR + GUARD + sz + i] != 0xA5) bad |= 2;
    }
    return bad;
}

int gchk(void *p)
{
    int bad = gcheck(p);

    if (bad) {
        g_ovr_pending |= bad;
        g_overwrites++;
        /* restore so the same buffer reports once */
        { unsigned char *b = (unsigned char *)p - GUARD - GHDR; size_t sz = *(size_t *)b;
          memset(b + GHDR, 0xA5, GUARD); memset(b + GHDR + GUARD + sz, 0xA5, GUARD); }
    }
    return !bad;
}

void gfree(void *p)
{
    if (!p) return;
    gchk(p);
    free((unsigned char *)p - GUARD - GHDR);
}

int gtouched(void *p)
{
    unsigned char *b = (unsigned char *)p - GUARD - GHDR, *u = p;
    size_t sz = *(size_t *)b, i;

    for (i = 0; i < sz; i++) if (u[i]) return 1;
    return 0;
}

/* ---------------------------------------------------------------- report */
#define MAXFN 1200
static struct { const char *name; unsigned char tested; } g_fn[MAXFN];
static int g_nfn;
static const char *g_area = "init";
static struct { const char *name; int p, f, n, s; } g_ar[16];
static int g_nar;
static const char *g_last = "start";

static void note(const char *fn, int v)
{
    int i;

    for (i = 0; i < g_nfn; i++) if (!strcmp(g_fn[i].name, fn)) break;
    if (i == g_nfn) {
        if (g_nfn >= MAXFN) return;
        g_fn[g_nfn].name = fn; g_fn[g_nfn].tested = 0; g_nfn++;
    }
    if (v == V_PASS || v == V_FAIL) g_fn[i].tested = 1;
}

void rep_area(const char *name)
{
    g_area = name;
    if (g_nar < 16) { g_ar[g_nar].name = name; g_nar++; }
    printf("[T] area %s\n", name);
    fflush(stdout);
}

static void count(int v)
{
    int *c = v == V_PASS ? &g_pass : v == V_FAIL ? &g_fail : v == V_NA ? &g_na : &g_skip;

    (*c)++;
    if (g_nar) {
        if (v == V_PASS) g_ar[g_nar - 1].p++;
        else if (v == V_FAIL) g_ar[g_nar - 1].f++;
        else if (v == V_NA) g_ar[g_nar - 1].n++;
        else g_ar[g_nar - 1].s++;
    }
}

/* ---- JSON report (--json FILE): one entry per reported line */
static FILE *g_jf;
static int g_jn;
static void jstr(const char *s)
{
    fputc('"', g_jf);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') { fputc('\\', g_jf); fputc(*s, g_jf); }
        else if ((unsigned char)*s < 32) fprintf(g_jf, "\\u%04x", *s);
        else fputc(*s, g_jf);
    }
    fputc('"', g_jf);
}
static void jrep(const char *fn, long ret, int v, const char *msg)
{
    static const char *vs[] = { "PASS", "FAIL", "NA", "SKIP" };
    const char *vv = vs[v];

    if (!g_jf) return;
    if (v == V_PASS && !strncmp(msg, "rt:", 3)) vv = "RT";
    fprintf(g_jf, "%s{\"fn\":", g_jn++ ? ",\n" : "");
    jstr(fn);
    fprintf(g_jf, ",\"area\":"); jstr(g_area ? g_area : "");
    fprintf(g_jf, ",\"ret\":%ld,\"v\":\"%s\",\"detail\":", ret, vv); jstr(msg);
    fputc('}', g_jf);
    fflush(g_jf);
}

void rep(const char *fn, long ret, int v, const char *fmt, ...)
{
    static const char *vs[] = { "PASS", "FAIL", "N/A", "SKIP" };
    char msg[300];
    va_list ap;

    va_start(ap, fmt); vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    if (g_ovr_pending) {
        printf("[A] %s ret=%ld FAIL OVERWRITE (guard %s hit) %s\n", fn, ret,
               g_ovr_pending == 3 ? "front+back" : g_ovr_pending == 1 ? "front" : "back", msg);
        g_ovr_pending = 0;
        v = V_FAIL;
    } else {
        printf("[A] %s ret=%ld %s %s\n", fn, ret, vs[v], msg);
    }
    fflush(stdout);
    jrep(fn, ret, v, msg);
    note(fn, v);
    count(v);
    g_last = fn;
    alarm(60);                         /* progress */
}

void rep_na(const char *fn)
{
    printf("[A] %s ret=- N/A not exported by this libimp\n", fn);
    fflush(stdout);
    jrep(fn, 0, V_NA, "not exported by this libimp");
    note(fn, V_NA);
    count(V_NA);
    g_last = fn;
    alarm(60);
}

int64_t now_us(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}

static int g_jfd = -1;
static void jclose(void)
{
    if (g_jf) { fprintf(g_jf, "\n]}\n"); fclose(g_jf); g_jf = NULL; }
}

/* OpenIMP only (issue #198): where a FrameSource stop is, one byte per channel
 * (low nibble DisableChn stage, high nibble pooling worker step). */
extern unsigned int OpenIMP_FS_DebugState(void) __attribute__((weak));

static void on_alarm(int sig)
{
    static const char m[] = "\n[E] watchdog: no progress for 60 s, last function: ";
    static const char hx[] = "0123456789abcdef";
    char st[32];
    int i, n = 0;

    (void)sig;
    if (write(1, m, sizeof(m) - 1) < 0) _exit(3);
    if (write(1, g_last, strlen(g_last)) < 0) _exit(3);
    if (write(1, "\n", 1) < 0) _exit(3);
    if (OpenIMP_FS_DebugState) {
        unsigned int w = OpenIMP_FS_DebugState();

        memcpy(st, "[E] fs state 0x", 15); n = 15;
        for (i = 7; i >= 0; i--) st[n++] = hx[(w >> (4 * i)) & 0xf];
        st[n++] = '\n';
        if (write(1, st, (size_t)n) < 0) _exit(3);
    }
    if (g_jfd >= 0 && write(g_jfd, "\n],\"watchdog\":true}\n", 19) < 0) _exit(3);
    _exit(3);
}

void wdog(const char *what, int sec)
{
    if (what) g_last = what;
    alarm(sec);
}

/* ----------------------------------------------------------------- misc */
static int cmpstr(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

extern char __start_apifn[], __stop_apifn[];

static void list_fns(int summary)
{
    const char **b = (const char **)__start_apifn, **e = (const char **)__stop_apifn, **u;
    int n = (int)(e - b), i, cnt = 0;

    u = malloc(sizeof(char *) * (n + 1));
    memcpy(u, b, sizeof(char *) * n);
    qsort(u, n, sizeof(char *), cmpstr);
    for (i = 0; i < n; i++) {
        if (i && !strcmp(u[i], u[i - 1])) continue;
        cnt++;
        if (!summary) printf("%s\n", u[i]);
    }
    printf("[T] %s: %d distinct vendor functions in this build's test table\n", SOC, cnt);
    free(u);
}

static int want(const char *areas, const char *a)
{
    return !areas || strstr(areas, a) != NULL;
}

int main(int argc, char **argv)
{
    const char *areas;
    const char *jpath = NULL;
    int ret, i, tested = 0, na_only = 0;

    /* global options anywhere before the positional arguments: --json FILE */
    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--json") && i + 1 < argc) {
            jpath = argv[i + 1];
            memmove(&argv[i], &argv[i + 2], sizeof(char *) * (argc - i - 1));
            argc -= 2;
            break;
        }
    areas = argc > 5 ? argv[5] : NULL;
    if (argc == 2 && !strcmp(argv[1], "--list")) { list_fns(0); return 0; }
    if (argc == 2 && !strcmp(argv[1], "--areas")) {
        puts("sys\nfs\nenc\nosd\nivs\naudio\nisp\nsu\ncases\ndmic\npicture\n"
#ifdef APITEST_T40
             "t40dev\n"
#endif
             );
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--cases")) { cases_list(); return 0; }
    if (argc >= 6 && !strcmp(argv[5], "picture")) {      /* former imgfx: sensor i2c w h OUTDIR [filter] */
        char *pa[8] = { argv[0], argv[1], argv[2], argv[3], argv[4], argc > 6 ? argv[6] : (char *)"/tmp/at/img", argc > 7 ? argv[7] : NULL, NULL };
        return picture_main(argc > 7 ? 7 : 6, pa);
    }
#ifdef APITEST_T40
    if (argc >= 6 && !strcmp(argv[5], "t40dev")) {        /* former t40_devtest: [phase mask in hex] */
        char *pa[3] = { argv[0], argc > 6 ? argv[6] : NULL, NULL };
        return t40dev_main(argc > 6 ? 2 : 1, pa);
    }
#endif
    if (argc < 5) {
        fprintf(stderr, "apitest (%s) usage: %s sensor i2c_addr sensor_w sensor_h [areas]   |   %s --list\n"
                "  areas: sys,fs,enc,osd,ivs,audio,isp,su,cases,dmic (comma list, default all); picture OUTDIR [filter]; --areas --cases --json FILE\n", SOC, argv[0], argv[0]);
        return 2;
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    g_sw = atoi(argv[3]); g_sh = atoi(argv[4]);
    g_mode_night = (getenv("APITEST_MODE") && !strcmp(getenv("APITEST_MODE"), "night"));
    signal(SIGALRM, on_alarm);
    signal(SIGPIPE, SIG_IGN);
    alarm(60);

    memset(&g_sensor, 0, sizeof(g_sensor));
    strncpy(g_sensor.name, argv[1], sizeof(g_sensor.name) - 1);
    g_sensor.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C;
    strncpy(g_sensor.i2c.type, argv[1], sizeof(g_sensor.i2c.type) - 1);
    g_sensor.i2c.addr = (int)strtol(argv[2], NULL, 0);
    g_sensor.rst_gpio = g_sensor.pwdn_gpio = g_sensor.power_gpio = -1;
#ifdef APITEST_T40
    /* Board sensor description (Eufy .184, as at_t40dev.c and timps.conf): i2c adapter 1, reset gpio 91, MCLK1,
     * MIPI CSI0, default boot 4.  The open driver forwards it to the sensor driver too, and the sc830ai
     * picks its register set by default_boot: 0/1 are the 4-lane settings, 2..4 the 2-lane ones.  With the
     * zero description (boot 0 = 3840x2160 15 fps 4-lane) the 2-lane board delivers no frame at all
     * (agg54..56: FrameSource 0 frames, "no sensor signal").  APITEST_VENDOR_SINFO=0 sends the zero one. */
    {
        const char *vs = getenv("APITEST_VENDOR_SINFO");
        if (!vs || strcmp(vs, "0")) {
            g_sensor.i2c.i2c_adapter_id = 1;
            g_sensor.rst_gpio = 91;
            g_sensor.video_interface = IMPISP_SENSOR_VI_MIPI_CSI0;
            g_sensor.mclk = IMPISP_SENSOR_MCLK1;
            g_sensor.default_boot = 4;
        }
    }
#endif

    if (jpath && (g_jf = fopen(jpath, "w"))) {
        g_jfd = fileno(g_jf);
        fprintf(g_jf, "{\"tool\":\"apitest\",\"soc\":\"%s\",\"sensor\":\"%s\",\"areas\":\"%s\",\"funcs\":[\n", SOC, argv[1], areas ? areas : "all");
        atexit(jclose);
    }
    printf("[T] apitest %s sensor %s %dx%d areas %s\n", SOC, argv[1], g_sw, g_sh, areas ? areas : "all");
#ifdef APITEST_T40
    printf("[T] sensor info: i2c%d mclk%d vin %d boot %d rst %d\n", (int)g_sensor.i2c.i2c_adapter_id, (int)g_sensor.mclk,
           (int)g_sensor.video_interface, g_sensor.default_boot, g_sensor.rst_gpio);
#endif

    if (areas && !strcmp(areas, "dmic")) {      /* IMP_DMIC_*: no ISP / sensor needed (area dmic = the case of area cases) */
        rep_area("dmic");
        t_dmic();
        goto summary;
    }
    if (!IMP_ISP_Open || !IMP_System_Init) { printf("[E] libimp does not export IMP_ISP_Open / IMP_System_Init (not linked?)\n"); return 1; }
    rep_area("isp-init");
    ret = 0;
#ifdef PLATFORM_T41
    ret = IMP_ISP_Open();
    rep(FN(IMP_ISP_Open), ret, ret < 0 ? V_FAIL : V_PASS, "");
    if (ret >= 0) t_binpath_pre();
    if (ret >= 0) { ret = IMP_ISP_AddSensor(IMPVI_MAIN, &g_sensor); RET0(IMP_ISP_AddSensor, ret, "sensor %s", g_sensor.name); }
#if defined(APITEST_T40) || defined(PLATFORM_T41)
    if (ret >= 0) algo_pre_t40();          /* custom AE/AWB records (case customalgo_t40): the vendor starts them at EnableSensor */
#endif
    if (ret >= 0) { ret = IMP_ISP_EnableSensor(IMPVI_MAIN, &g_sensor); RET0(IMP_ISP_EnableSensor, ret, ""); }
#else
    ret = IMP_ISP_Open();
    rep(FN(IMP_ISP_Open), ret, ret < 0 ? V_FAIL : V_PASS, "");
    if (ret >= 0) { ret = IMP_ISP_AddSensor(&g_sensor); RET0(IMP_ISP_AddSensor, ret, "sensor %s", g_sensor.name); }
    if (ret >= 0) { ret = IMP_ISP_EnableSensor(); RET0(IMP_ISP_EnableSensor, ret, ""); }
#endif
    if (ret < 0) { printf("[E] ISP open/add/enable sensor failed\n"); return 1; }

    rep_area("sys");
    ret = IMP_System_Init();
    RET0(IMP_System_Init, ret, "");
    if (ret < 0) return 1;
    ret = IMP_ISP_EnableTuning();
    RET0(IMP_ISP_EnableTuning, ret, "");
    if (ret < 0) return 1;
    /* as the vendor samples (sample-common.c): the FrameSource channels run at
     * the sensor rate, their outFrmRate (15/1 here) does not drop frames */
#if defined(APITEST_T40)
    { uint32_t sn = 15, sd = 1; ret = (IMP_ISP_Tuning_SetSensorFPS && !getenv("APITEST_KEEP_FPS")) ? IMP_ISP_Tuning_SetSensorFPS(IMPVI_MAIN, &sn, &sd) : -1; }
#elif defined(PLATFORM_T41)
    { IMPISPSensorFps sf = { 15, 1 }; ret = IMP_ISP_Tuning_SetSensorFPS ? IMP_ISP_Tuning_SetSensorFPS(IMPVI_MAIN, &sf) : -1; }
#else
    ret = IMP_ISP_Tuning_SetSensorFPS ? IMP_ISP_Tuning_SetSensorFPS(15, 1) : -1;
#endif
    printf("[T] SetSensorFPS 15/1 -> %d\n", ret);
    if (IMP_ISP_Tuning_GetSensorFPS) {
#if defined(APITEST_T40)
        uint32_t gn = 0, gd = 0;
        if (IMP_ISP_Tuning_GetSensorFPS(IMPVI_MAIN, &gn, &gd) == 0 && gn > 0 && gd > 0) g_fps = (int)((gn + gd / 2) / gd);
#elif defined(PLATFORM_T41)
        IMPISPSensorFps gf = { 0, 0 };
        if (IMP_ISP_Tuning_GetSensorFPS(IMPVI_MAIN, &gf) == 0 && gf.num > 0 && gf.den > 0) g_fps = (int)((gf.num + gf.den / 2) / gf.den);
#else
        uint32_t gn = 0, gd = 0;
        if (IMP_ISP_Tuning_GetSensorFPS(&gn, &gd) == 0 && gn > 0 && gd > 0) g_fps = (int)((gn + gd / 2) / gd);
#endif
        printf("[T] sensor rate in effect: %d fps\n", g_fps);
    }
    { unsigned m = g_mode_night; int r;
#ifdef PLATFORM_T41
      r = IMP_ISP_Tuning_SetISPRunningMode(IMPVI_MAIN, (void *)&m);
#else
      r = IMP_ISP_Tuning_SetISPRunningMode(m);
#endif
      (void)r; }

    if (want(areas, "sys")) t_system();
    t_fs_pre();
    if (want(areas, "fs")) t_fs();
    if (want(areas, "enc")) t_enc();
    if (want(areas, "osd")) t_osd();
    if (want(areas, "ivs")) t_ivs();
    if (want(areas, "audio")) t_audio();
    if (want(areas, "isp")) { t_isp(); t_isp_rt(); }
    if (want(areas, "su")) t_su();
    if (want(areas, "cases")) t_cases();
    rep_area("teardown");
    t_fs_post();
    t_system_exit(0);
#if defined(APITEST_T40) || defined(PLATFORM_T41)
    algo_post_t40();
#endif

summary:
    for (i = 0; i < g_nfn; i++) { if (g_fn[i].tested) tested++; else na_only++; }
    printf("[T] summary %s: PASS %d FAIL %d N/A %d SKIP %d, OVERWRITE %d\n", SOC, g_pass, g_fail, g_na, g_skip, g_overwrites);
    printf("[T] functions: %d reported, %d exercised (PASS/FAIL), %d only N/A or SKIP\n", g_nfn, tested, na_only);
    for (i = 0; i < g_nar; i++)
        printf("[T] area %-8s PASS %d FAIL %d N/A %d SKIP %d\n", g_ar[i].name, g_ar[i].p, g_ar[i].f, g_ar[i].n, g_ar[i].s);
    alarm(0);
    return g_fail ? 1 : 0;
}
