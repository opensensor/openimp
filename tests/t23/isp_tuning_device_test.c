/*
 * On-device test of the T23 ISP tuning API against the live tx-isp-t23
 * driver, while a streamer keeps the ISP running.
 *
 * The program contains OpenIMP's T23 tuning layer (src/isp/isp_t23_tuning.c)
 * and gives it an ISP device that only opens the tuning node /dev/isp-m0, so
 * it does not touch the streamer's sensor or frame channels.  Every call
 * goes through the same code path as in libimp.
 *
 *   t23_isp_tuning_device_test selftest
 *       set/readback cycles of the controls the kernel serves, restoring the
 *       original values; reports PASS/FAIL/INFO lines, exit 1 on a FAIL.
 *   t23_isp_tuning_device_test get NAME
 *   t23_isp_tuning_device_test set NAME VALUE
 *       one scalar call (sensor 0) for manual image checks; NAME is listed
 *       by running the program without arguments.
 *   t23_isp_tuning_device_test raw get|set CID [VALUE]
 *       one 0xc01056c6 request with an inline value (sensor 0).
 *
 * Build (static, uclibc toolchain of the camera's thingino output):
 *   make -C tests/t23 device CROSS_COMPILE=<output>/host/bin/mipsel-linux-
 */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../src/isp/isp_t23_tuning.c"

ISPDevice *gISP;

/* ---- libimp pieces the tuning layer calls ------------------------------ */
int IMP_Log_Get_Option(void) { return 0; }

void imp_log_fun(int level, int option, int type, ...)
{
    va_list ap;
    const char *fmt;

    (void)option;
    (void)type;
    va_start(ap, type);
    (void)va_arg(ap, const char *);         /* tag */
    (void)va_arg(ap, const char *);         /* file */
    (void)va_arg(ap, int);                  /* line */
    (void)va_arg(ap, const char *);         /* function */
    fmt = va_arg(ap, const char *);
    fprintf(stderr, "  libimp[%d]: ", level);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

int IMP_Encoder_RequestIDR(int chn)
{
    printf("  (IMP_Encoder_RequestIDR(%d) skipped: no encoder here)\n", chn);
    return 0;
}

int32_t set_framesource_fps(int32_t n, int32_t d)
{
    printf("  (frame source fps %d/%d not retimed here)\n", n, d);
    return 0;
}

int32_t set_framesource_changewait_cnt(void) { return 0; }
int IMP_ISP_SetDefaultBinPath(const char *p) { (void)p; return -1; }
int IMP_ISP_GetDefaultBinPath(char *p) { (void)p; return -1; }

/* ---- device ------------------------------------------------------------ */
static ISPDevice dev;
static uint8_t tuning_obj[0x400];

static int open_isp(void)
{
    memset(&dev, 0, sizeof(dev));
    strcpy(dev.dev_name, "tx-isp");
    strcpy(dev.tuning_path, "/dev/isp-m0");
    dev.fd = -1;
    dev.opened = 2;
    dev.tuning_fd = open(dev.tuning_path, O_RDWR);
    if (dev.tuning_fd < 0) {
        perror("open /dev/isp-m0");
        return -1;
    }
    dev.tuning = tuning_obj;
    dev.tuning_state = 2;
    gISP = &dev;
    return 0;
}

static int raw(int get, uint32_t cid, uint32_t *value)
{
    return t23_xfer(&dev, 0, get, cid, value);
}

static int fails;

#define RESULT(ok, ...) do { printf("%s ", (ok) ? "PASS" : "FAIL");        \
    printf(__VA_ARGS__); putchar('\n'); if (!(ok)) fails++; } while (0)
#define INFO(...) do { printf("INFO "); printf(__VA_ARGS__);               \
    putchar('\n'); } while (0)

static uint32_t read_param(const char *name)
{
    char path[128], line[32];
    FILE *f;
    uint32_t v = 0xffffffffu;

    snprintf(path, sizeof(path), "/sys/module/tx_isp_t23/parameters/%s", name);
    f = fopen(path, "r");
    if (f) {
        if (fgets(line, sizeof(line), f))
            v = (uint32_t)strtoul(line, NULL, 0);
        fclose(f);
    }
    return v;
}

/* Byte-wide BCSH controls through the per-sensor V4L2 numbers. */
static void cycle_u8(const char *name, int (*set)(unsigned char),
                     int (*get)(unsigned char *), unsigned char test)
{
    unsigned char orig = 0, back = 0;
    int r1 = get(&orig), r2 = set(test), r3 = get(&back);

    RESULT(r1 == 0 && r2 == 0 && r3 == 0 && back == test,
           "%s: orig %u set %u read %u (ret %d/%d/%d)", name, orig, test,
           back, r1, r2, r3);
    set(orig);
}

static void cycle_u32(const char *name, int (*set)(uint32_t),
                      int (*get)(uint32_t *), uint32_t test)
{
    uint32_t orig = 0, back = 0;
    int r1 = get(&orig), r2 = set(test), r3 = get(&back);

    RESULT(r1 == 0 && r2 == 0 && r3 == 0 && back == test,
           "%s: orig %u set %u read %u (ret %d/%d/%d)", name, orig, test,
           back, r1, r2, r3);
    set(orig);
}

static int set_aecomp(uint32_t v) { return IMP_ISP_Tuning_SetAeComp((int)v); }
static int set_running(uint32_t v)
{
    return IMP_ISP_Tuning_SetISPRunningMode((int)v);
}

static int get_sinter(uint32_t *v) { return raw(1, T23_CID_SINTER_STRENGTH, v); }

static void selftest(void)
{
    uint32_t a = 0, b = 0, v = 0;
    uint8_t ev[24];
    int ret;

    /* Kernel-served controls: set, read back, restore. */
    ret = IMP_ISP_Tuning_GetSensorFPS(&a, &b);
    RESULT(ret == 0 && a && b, "GetSensorFPS %u/%u", a, b);
    if (ret == 0) {
        ret = IMP_ISP_Tuning_SetSensorFPS(a, b);
        RESULT(ret == 0, "SetSensorFPS %u/%u (unchanged)", a, b);
    }
    cycle_u8("Brightness", IMP_ISP_Tuning_SetBrightness,
             IMP_ISP_Tuning_GetBrightness, 0x9c);
    cycle_u8("Saturation", IMP_ISP_Tuning_SetSaturation,
             IMP_ISP_Tuning_GetSaturation, 0x60);
    cycle_u8("Contrast", IMP_ISP_Tuning_SetContrast,
             IMP_ISP_Tuning_GetContrast, 0x90);
    cycle_u8("Sharpness", IMP_ISP_Tuning_SetSharpness,
             IMP_ISP_Tuning_GetSharpness, 0x70);
    cycle_u8("BcshHue", IMP_ISP_Tuning_SetBcshHue,
             IMP_ISP_Tuning_GetBcshHue, 0x70);
    cycle_u32("HiLightDepress", IMP_ISP_Tuning_SetHiLightDepress,
              IMP_ISP_Tuning_GetHiLightDepress, 3);
    cycle_u32("BacklightComp", IMP_ISP_Tuning_SetBacklightComp,
              IMP_ISP_Tuning_GetBacklightComp, 2);

    /* Controls routed by open-tx-isp claude/t23-isp-tuning (AE
     * compensation, sinter strength, running and custom mode, AE luma). */
    cycle_u32("AeComp", set_aecomp, IMP_ISP_Tuning_GetAeComp, 160);
    {
        uint32_t target, comp;

        IMP_ISP_Tuning_SetAeComp(160);
        comp = read_param("source_ae_compensation");
        target = read_param("source_ae_hlil_target");
        RESULT(comp == 160 && target == 75,
               "AeComp 160 -> source_ae_compensation %u target %u (75)",
               comp, target);
        IMP_ISP_Tuning_SetAeComp(128);
    }
    {
        uint32_t orig = 0;

        get_sinter(&orig);
        ret = IMP_ISP_Tuning_SetSinterStrength(0x60);
        get_sinter(&v);
        RESULT(ret == 0 && v == 0x60,
               "SinterStrength: orig %u set 96 read %u (ret %d)", orig, v,
               ret);
        IMP_ISP_Tuning_SetSinterStrength(orig);
    }
    cycle_u32("ISPRunningMode", set_running,
              IMP_ISP_Tuning_GetISPRunningMode, 1);
    for (int i = 0; i < 3; i++) {
        uint32_t luma = 0, param;

        ret = IMP_ISP_Tuning_GetAeLuma(&luma);
        param = read_param("source_ae_hlil_luma");
        param = param > 255 ? 255 : param;
        /* both reads race with the AE worker: allow a small step */
        RESULT(ret == 0 && luma > 0 && luma < 256 &&
               (luma > param ? luma - param : param - luma) <= 8,
               "GetAeLuma %u (source_ae_hlil_luma %u)", luma, param);
        sleep(1);
    }

    ret = IMP_ISP_Tuning_GetISPCustomMode(&v);
    INFO("GetISPCustomMode ret %d value %d (-1: the tuning bin has no "
         "custom bank)", ret, (int)v);

    /* Live AE read-back: the kernel's tisp_g_ev_attr() equivalent.  The
     * AE may step between the calls, so only report. */
    {
        struct {                    /* IMPISPExpr.g_attr */
            uint32_t mode;
            uint16_t it, it_min, it_max, line_us;
        } expr;
        const uint32_t *e = (const uint32_t *)ev;

        memset(&expr, 0, sizeof(expr));
        ret = IMP_ISP_Tuning_GetExpr(&expr);
        INFO("GetExpr ret %d mode %u it %u min %u max %u line %u us", ret,
             expr.mode, expr.it, expr.it_min, expr.it_max, expr.line_us);
        ret = IMP_ISP_Tuning_GetTotalGain(&v);
        INFO("GetTotalGain ret %d value %u ([24.8], 256 = 1x)", ret, v);
        memset(ev, 0, sizeof(ev));
        ret = IMP_ISP_Tuning_GetEVAttr(ev);
        INFO("GetEVAttr ret %d ev %u expr_us %u ev_log2 %u again %u "
             "dgain %u gain_log2 %u", ret, e[0], e[1], e[2], e[3], e[4],
             e[5]);
    }

    /* Error conventions of the stock library. */
    RESULT(IMP_ISP_MultiCamera_Tuning_SetBrightness(4, 1) == -4092,
           "sensor 4 -> -4092");
    RESULT(IMP_ISP_Tuning_GetAeLuma(NULL) == -1, "NULL pointer -> -1");

    /* Kernel missing: the driver acknowledges and ignores these. */
    ret = IMP_ISP_Tuning_SetDPC_Strength(200);
    v = 0;
    IMP_ISP_Tuning_GetDPC_Strength(&v);
    INFO("SetDPC_Strength(200) ret %d, GetDPC_Strength reads %u "
         "(kernel missing: no effect expected)", ret, v);
}

/* ---- single calls ------------------------------------------------------ */
static const struct {
    const char *name;
    int (*set)(uint32_t);
    int (*get)(uint32_t *);
    int bytes;  /* the public type is unsigned char */
} calls_tab[] = {
#define S32(n) (int (*)(uint32_t))IMP_ISP_Tuning_Set##n
#define G32(n) (int (*)(uint32_t *))IMP_ISP_Tuning_Get##n
    { "Brightness", S32(Brightness), G32(Brightness), 1 },
    { "Contrast", S32(Contrast), G32(Contrast), 1 },
    { "Saturation", S32(Saturation), G32(Saturation), 1 },
    { "Sharpness", S32(Sharpness), G32(Sharpness), 1 },
    { "BcshHue", S32(BcshHue), G32(BcshHue), 1 },
    { "AeComp", S32(AeComp), G32(AeComp), 0 },
    { "MaxAgain", S32(MaxAgain), G32(MaxAgain), 0 },
    { "MaxDgain", S32(MaxDgain), G32(MaxDgain), 0 },
    { "HiLightDepress", S32(HiLightDepress), G32(HiLightDepress), 0 },
    { "BacklightComp", S32(BacklightComp), G32(BacklightComp), 0 },
    { "DPC_Strength", S32(DPC_Strength), G32(DPC_Strength), 0 },
    { "DRC_Strength", S32(DRC_Strength), G32(DRC_Strength), 0 },
    { "ISPRunningMode", S32(ISPRunningMode), G32(ISPRunningMode), 0 },
    { "ISPCustomMode", S32(ISPCustomMode), G32(ISPCustomMode), 0 },
    { "ISPHflip", S32(ISPHflip), G32(ISPHflip), 0 },
    { "ISPVflip", S32(ISPVflip), G32(ISPVflip), 0 },
    { "SensorHflip", S32(SensorHflip), G32(SensorHflip), 0 },
    { "SensorVflip", S32(SensorVflip), G32(SensorVflip), 0 },
    { "HVFLIP", S32(HVFLIP), G32(HVFlip), 0 },
    { "AntiFlickerAttr", S32(AntiFlickerAttr), G32(AntiFlickerAttr), 0 },
    { "Ae_IT_MAX", S32(Ae_IT_MAX), G32(AE_IT_MAX), 0 },
    { "SinterStrength", S32(SinterStrength), get_sinter, 0 },
    { "TemperStrength", S32(TemperStrength), NULL, 0 },
    { "AeFreeze", S32(AeFreeze), NULL, 0 },
    { "AeLuma", NULL, G32(AeLuma), 0 },
    { "TotalGain", NULL, G32(TotalGain), 0 },
#undef S32
#undef G32
};

static void usage(const char *argv0)
{
    fprintf(stderr, "usage: %s selftest | get NAME | set NAME VALUE |\n"
                    "       raw get CID | raw set CID VALUE\nNAME:",
            argv0);
    for (unsigned i = 0; i < sizeof(calls_tab) / sizeof(calls_tab[0]); i++)
        fprintf(stderr, " %s", calls_tab[i].name);
    fputc('\n', stderr);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }
    if (open_isp())
        return 1;
    if (!strcmp(argv[1], "selftest")) {
        selftest();
        printf("%s: %d failure(s)\n", fails ? "FAILED" : "OK", fails);
        return fails ? 1 : 0;
    }
    if (!strcmp(argv[1], "raw") && argc >= 4) {
        int get = !strcmp(argv[2], "get");
        uint32_t v = argc > 4 ? (uint32_t)strtoul(argv[4], NULL, 0) : 0;
        int ret = raw(get, (uint32_t)strtoul(argv[3], NULL, 0), &v);

        printf("ret %d value %u (0x%x)\n", ret, v, v);
        return ret ? 1 : 0;
    }
    for (unsigned i = 0; i < sizeof(calls_tab) / sizeof(calls_tab[0]); i++) {
        uint32_t v = 0;
        int ret;

        if (argc < 3 || strcmp(argv[2], calls_tab[i].name))
            continue;
        if (!strcmp(argv[1], "get") && calls_tab[i].get) {
            ret = calls_tab[i].get(&v);
            if (calls_tab[i].bytes)
                v &= 0xff;
            printf("Get%s ret %d value %u (0x%x)\n", calls_tab[i].name, ret,
                   v, v);
            return ret ? 1 : 0;
        }
        if (!strcmp(argv[1], "set") && argc == 4 && calls_tab[i].set) {
            v = (uint32_t)strtoul(argv[3], NULL, 0);
            ret = calls_tab[i].set(v);
            printf("Set%s(%u) ret %d\n", calls_tab[i].name, v, ret);
            return ret ? 1 : 0;
        }
    }
    usage(argv[0]);
    return 2;
}
