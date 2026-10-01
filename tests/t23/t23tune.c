/*
 * t23tune: on-camera CLI test tool for the new T23 tuning CIDs
 * (MaxAgain, MaxDgain, AE_IT_MAX, SensorAttr, EnableDRC, EnableDefog,
 * SinterStrength) plus the existing EXPR/EV/TotalGain/AeLuma getters.
 *
 * Runs as a second process alongside timps: it opens only the tuning
 * node /dev/isp-m0 directly (no IMP_ISP_Open, no sensor/frame channels),
 * the same way tests/t23/isp_tuning_device_test.c does, and links
 * OpenIMP's T23 tuning layer (src/isp/isp_t23_tuning.c) so every call
 * goes through the same ioctl path as libimp.
 *
 * Build (static, uclibc toolchain of the camera's thingino output):
 *   make -C tests/t23 t23tune CROSS_COMPILE=<output>/host/bin/mipsel-linux-
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "../../src/isp/isp_t23_tuning.c"

ISPDevice *gISP;

/* ---- libimp pieces the tuning layer calls (stubs, as in the device test) */
int IMP_Log_Get_Option(void) { return 0; }

void imp_log_fun(int level, int option, int type, ...)
{
    va_list ap;
    const char *fmt;

    (void)option;
    (void)type;
    va_start(ap, type);
    (void)va_arg(ap, const char *);
    (void)va_arg(ap, const char *);
    (void)va_arg(ap, int);
    (void)va_arg(ap, const char *);
    fmt = va_arg(ap, const char *);
    fprintf(stderr, "  libimp[%d]: ", level);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

int IMP_Encoder_RequestIDR(int chn) { (void)chn; return 0; }
int32_t set_framesource_fps(int32_t n, int32_t d) { (void)n; (void)d; return 0; }
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

/* Local mirrors of the public IMPISPSENSORAttr / IMPISPExpr.g_attr layouts
 * (see include/imp/imp_isp.h); using our own struct names instead of the
 * public ones avoids redeclaring IMP_ISP_Tuning_* with conflicting types,
 * since isp_t23_tuning.c's T23_WRAP1 macro already declares them as
 * void* / scalar (same approach as isp_tuning_device_test.c). */
struct sensor_attr { uint32_t fps, width, height; };
struct expr_g_attr { uint32_t mode; uint16_t it, it_min, it_max, line_us; };

/* ---- commands ------------------------------------------------------------ */
static void show(void)
{
    struct sensor_attr sattr;
    uint32_t maxagain = 0, maxdgain = 0, itmax = 0, ev[6], gain = 0;
    struct expr_g_attr expr;
    int ret;

    memset(&sattr, 0, sizeof(sattr));
    ret = IMP_ISP_Tuning_GetSensorAttr(&sattr);
    printf("GetSensorAttr ret %d fps %u width %u height %u\n", ret,
           sattr.fps, sattr.width, sattr.height);

    ret = IMP_ISP_Tuning_GetMaxAgain(&maxagain);
    printf("GetMaxAgain ret %d value %u (log2 Q5)\n", ret, maxagain);

    ret = IMP_ISP_Tuning_GetMaxDgain(&maxdgain);
    printf("GetMaxDgain ret %d value %u\n", ret, maxdgain);

    ret = IMP_ISP_Tuning_GetAE_IT_MAX(&itmax);
    printf("GetAE_IT_MAX ret %d value %u (lines)\n", ret, itmax);

    memset(&expr, 0, sizeof(expr));
    ret = IMP_ISP_Tuning_GetExpr(&expr);
    printf("GetExpr ret %d it %u it_min %u it_max %u line_us %u\n", ret,
           expr.it, expr.it_min, expr.it_max, expr.line_us);

    memset(ev, 0, sizeof(ev));
    ret = IMP_ISP_Tuning_GetEVAttr(ev);
    printf("GetEVAttr ret %d ev %u expr_us %u ev_log2 %u again %u dgain %u "
           "gain_log2 %u\n", ret, ev[0], ev[1], ev[2], ev[3], ev[4], ev[5]);

    ret = IMP_ISP_Tuning_GetTotalGain(&gain);
    printf("GetTotalGain ret %d value %u ([24.8], 256 = 1x)\n", ret, gain);
}

static int do_u32(const char *label, uint32_t arg,
                  int (*set)(uint32_t), int (*get)(uint32_t *))
{
    uint32_t back = 0;
    int r1 = set(arg);
    int r2 = get(&back);

    printf("Set%s(%u) ret %d; read back %u ret %d\n", label, arg, r1, back,
           r2);
    return r1 ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr,
            "usage: %s show | maxagain N | itmax LINES | drc 0|1 | "
            "defog 0|1 | sinter N\n", argv[0]);
        return 2;
    }
    if (open_isp())
        return 1;

    if (!strcmp(argv[1], "show")) {
        show();
        return 0;
    }
    if (argc < 3) {
        fprintf(stderr, "%s: missing value\n", argv[1]);
        return 2;
    }
    uint32_t v = (uint32_t)strtoul(argv[2], NULL, 0);

    if (!strcmp(argv[1], "maxagain"))
        return do_u32("MaxAgain", v, IMP_ISP_Tuning_SetMaxAgain,
                      IMP_ISP_Tuning_GetMaxAgain);
    if (!strcmp(argv[1], "itmax"))
        return do_u32("Ae_IT_MAX", v, IMP_ISP_Tuning_SetAe_IT_MAX,
                      IMP_ISP_Tuning_GetAE_IT_MAX);
    if (!strcmp(argv[1], "drc")) {
        uint32_t back = 0;
        int r1 = IMP_ISP_Tuning_EnableDRC((int)v);
        int r2 = t23_xfer(&dev, 0, 1, T23_CID_ENABLE_DRC, &back);

        printf("EnableDRC(%u) ret %d; read back %u ret %d\n", v, r1, back,
               r2);
        return r1 ? 1 : 0;
    }
    if (!strcmp(argv[1], "defog")) {
        uint32_t back = 0;
        int r1 = IMP_ISP_Tuning_EnableDefog((int)v);
        int r2 = t23_xfer(&dev, 0, 1, T23_CID_ENABLE_DEFOG, &back);

        printf("EnableDefog(%u) ret %d; read back %u ret %d\n", v, r1, back,
               r2);
        return r1 ? 1 : 0;
    }
    if (!strcmp(argv[1], "sinter")) {
        uint32_t back = 0;
        int r1 = IMP_ISP_Tuning_SetSinterStrength(v);
        int r2 = t23_xfer(&dev, 0, 1, T23_CID_SINTER_STRENGTH, &back);

        printf("SetSinterStrength(%u) ret %d; read back %u ret %d\n", v, r1,
               back, r2);
        return r1 ? 1 : 0;
    }
    fprintf(stderr, "unknown command: %s\n", argv[1]);
    return 2;
}
