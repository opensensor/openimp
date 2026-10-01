/*
 * Host test of the T23 ISP tuning API (src/isp/isp_t23_tuning.c).
 *
 * The library is built into this program with a recording ioctl().  The
 * template table below was generated from the disassembly of the stock T23
 * 1.3.0 uclibc libimp.so (IMP_ISP_MultiCamera_Tuning_* functions that only
 * issue the 0xc01056c6 tuning request): command ID, direction, payload kind
 * and whether the argument is NULL-checked.  The hand-written checks cover
 * the calls that convert data or use other requests.
 *
 * The code under test passes pointers through 32-bit words like the MIPS
 * library, so the program is linked without PIE and runs the tests on a
 * thread whose stack lies below 4 GiB.
 */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define PLATFORM_T23 1
/* Replace isp_tseries_dev.h: the 32-bit layout asserts do not hold on the
 * host, and the test only needs the fields the tuning API reads. */
#define OPENIMP_ISP_TSERIES_DEV_H
typedef struct ISPDevice {
    char dev_name[0x20];
    int32_t fd;
    uint32_t opened;
    uint8_t sensor_info[0x54];
    char tuning_path[0x20];
    int32_t tuning_fd;
    void *tuning;
    int32_t mem_fd;
    void *isp_base;
    int32_t tuning_state;
    void *sensor_alloc[2];
} ISPDevice;
ISPDevice *gISP;

#define ioctl test_ioctl
int test_ioctl(int fd, unsigned long nr, ...);
#include "../../src/isp/isp_t23_tuning.c"
#undef ioctl

/* ---- environment stubs ------------------------------------------------ */
int IMP_Log_Get_Option(void) { return 0; }
void imp_log_fun(int level, int option, int type, ...)
{
    (void)level; (void)option; (void)type;
}
static int idr_requests, changewait_calls, fps_num_seen, fps_den_seen;
int IMP_Encoder_RequestIDR(int chn) { (void)chn; idr_requests++; return 0; }
int32_t set_framesource_fps(int32_t n, int32_t d)
{
    fps_num_seen = n; fps_den_seen = d; return 0;
}
int32_t set_framesource_changewait_cnt(void) { changewait_calls++; return 0; }
int IMP_ISP_SetDefaultBinPath(const char *p) { (void)p; return 0; }
int IMP_ISP_GetDefaultBinPath(char *p) { (void)p; return 0; }

/* ---- recording ioctl -------------------------------------------------- */
static int calls;
static int last_fd;
static unsigned long last_nr;
static T23TuningReq last_req;
static T23Ctrl last_ctrl;
static int ioctl_ret;
static uint32_t get_value = 0x1234;         /* inline value for GETs */
static uint8_t get_block[0x200];            /* copied to a GET pointer */
static size_t get_block_len;
static uint8_t set_block[0x200];            /* copied from a SET pointer */
static size_t set_block_len;

int test_ioctl(int fd, unsigned long nr, ...)
{
    va_list ap;
    void *arg;

    va_start(ap, nr);
    arg = va_arg(ap, void *);
    va_end(ap);
    calls++;
    last_fd = fd;
    last_nr = nr;
    if (nr == T23_VIDIOC_TUNING) {
        T23TuningReq *req = arg;
        void *p = (void *)(uintptr_t)req->value;

        if (req->dir == 1 && get_block_len)
            memcpy(p, get_block, get_block_len);
        if (req->dir == 0 && set_block_len)
            memcpy(set_block, p, set_block_len);
        if (req->dir == 1 && !get_block_len)
            req->value = get_value;
        last_req = *req;
    } else if ((nr & 0xffffff00u) == 0xc0085600u) {
        T23Ctrl *ctrl = arg;

        if ((nr & 0xff) == 0x1b || (nr & 0xff) == 0x1d || (nr & 0xff) == 0x1f)
            ctrl->value = (int32_t)get_value;
        last_ctrl = *ctrl;
    }
    return ioctl_ret;
}

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { failures++;                 \
    fprintf(stderr, "FAIL %s:%d: ", __func__, __LINE__);                    \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static ISPDevice dev;
static uint8_t tuning_obj[0x400];
static uint8_t buf[0x400];

static void reset(void)
{
    memset(&dev, 0, sizeof(dev));
    dev.fd = 7;
    dev.tuning_fd = 9;
    dev.tuning = tuning_obj;
    dev.tuning_state = 2;
    dev.opened = 2;
    gISP = &dev;
    calls = 0;
    ioctl_ret = 0;
    get_value = 0x1234;
    get_block_len = set_block_len = 0;
    memset(&last_req, 0, sizeof(last_req));
}

/* ---- template calls --------------------------------------------------- */
typedef int (*t23_fn)(int, uintptr_t);
typedef int (*t23_fn1)(uintptr_t);
enum { K_SET, K_PTR, K_WORD };

static const struct {
    const char *name;
    t23_fn fn;
    uint32_t cid;
    int get;
    int kind;
    int nullchk;
} tmpl[] = {
    { "AE_GetROI", (t23_fn)IMP_ISP_MultiCamera_Tuning_AE_GetROI, 0x8000024, 1, K_PTR, 1 },
    { "AE_SetROI", (t23_fn)IMP_ISP_MultiCamera_Tuning_AE_SetROI, 0x8000024, 0, K_SET, 1 },
    { "Awb_SetRgbCoefft", (t23_fn)IMP_ISP_MultiCamera_Tuning_Awb_SetRgbCoefft, 0x8000008, 0, K_SET, 1 },
    { "EnableDefog", (t23_fn)IMP_ISP_MultiCamera_Tuning_EnableDefog, 0x80000a4, 0, K_SET, 0 },
    { "EnableDRC", (t23_fn)IMP_ISP_MultiCamera_Tuning_EnableDRC, 0x80000a3, 0, K_SET, 0 },
    { "GetAeComp", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetAeComp, 0x8000023, 1, K_WORD, 1 },
    { "GetAeHist", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetAeHist, 0x800002e, 1, K_PTR, 1 },
    { "GetAeHist_Origin", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetAeHist_Origin, 0x8000031, 1, K_PTR, 1 },
    { "GetAE_IT_MAX", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetAE_IT_MAX, 0x8000032, 1, K_WORD, 0 },
    { "GetAeLuma", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetAeLuma, 0x8000033, 1, K_WORD, 1 },
    { "GetAeMin", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetAeMin, 0x800002f, 1, K_PTR, 1 },
    { "GetAeState", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetAeState, 0x8000036, 1, K_PTR, 1 },
    { "GetAeTargetList", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetAeTargetList, 0x8000038, 1, K_PTR, 0 },
    { "GetAeWeight", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetAeWeight, 0x800002d, 1, K_PTR, 1 },
    { "GetAeZone", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetAeZone, 0x8000030, 1, K_PTR, 1 },
    { "GetAfHist", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetAfHist, 0x8000042, 1, K_PTR, 1 },
    { "GetAFMetrices", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetAFMetrices, 0x8000043, 1, K_PTR, 0 },
    { "GetAfWeight", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetAfWeight, 0x8000044, 1, K_PTR, 1 },
    { "GetAfZone", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetAfZone, 0x8000046, 1, K_PTR, 1 },
    { "GetAutoZoom", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetAutoZoom, 0x80000e8, 1, K_PTR, 1 },
    { "GetAwbClust", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetAwbClust, 0x800000e, 1, K_PTR, 1 },
    { "GetAWBCt", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetAWBCt, 0x800000d, 1, K_PTR, 1 },
    { "GetAwbCtTrend", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetAwbCtTrend, 0x800000f, 1, K_PTR, 0 },
    { "GetAwbHist", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetAwbHist, 0x8000007, 1, K_PTR, 1 },
    { "GetAwbWeight", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetAwbWeight, 0x8000006, 1, K_PTR, 1 },
    { "GetAwbZone", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetAwbZone, 0x800000b, 1, K_PTR, 1 },
    { "GetAwbZoneWeight", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetAwbZoneWeight, 0x8000010, 1, K_PTR, 1 },
    { "GetBacklightComp", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetBacklightComp, 0x8000037, 1, K_WORD, 1 },
    { "GetBcshHue", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetBcshHue, 0x8000101, 1, K_WORD, 1 },
    { "GetBlcAttr", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetBlcAttr, 0x80000a5, 1, K_PTR, 1 },
    { "GetCsc_Attr", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetCsc_Attr, 0x80000a6, 1, K_PTR, 1 },
    { "GetDefog_Strength", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetDefog_Strength, 0x8000039, 1, K_PTR, 1 },
    { "GetDPC_Strength", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetDPC_Strength, 0x8000062, 1, K_WORD, 0 },
    { "GetDRC_Strength", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetDRC_Strength, 0x80000a2, 1, K_WORD, 0 },
    { "GetExpr", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetExpr, 0x8000025, 1, K_PTR, 1 },
    { "GetFrontCrop", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetFrontCrop, 0x80000e3, 1, K_PTR, 1 },
    { "GetGamma", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetGamma, 0x800002b, 1, K_PTR, 0 },
    { "GetHiLightDepress", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetHiLightDepress, 0x800002a, 1, K_WORD, 1 },
    { "GetHVFlip", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetHVFlip, 0x80000e4, 1, K_WORD, 0 },
    { "GetMask", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetMask, 0x80000e5, 1, K_PTR, 1 },
    { "GetMaxAgain", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetMaxAgain, 0x8000028, 1, K_WORD, 1 },
    { "GetMaxDgain", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetMaxDgain, 0x8000029, 1, K_WORD, 1 },
    { "GetModuleControl", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetModuleControl, 0x80000e2, 1, K_PTR, 1 },
    { "GetSensorAttr", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetSensorAttr, 0x8000045, 1, K_PTR, 1 },
    { "GetWB", (t23_fn)IMP_ISP_MultiCamera_Tuning_GetWB, 0x8000004, 1, K_PTR, 1 },
    { "SetAeComp", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetAeComp, 0x8000023, 0, K_SET, 0 },
    { "SetAeFreeze", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetAeFreeze, 0x8000034, 0, K_SET, 0 },
    { "SetAeHist", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetAeHist, 0x800002e, 0, K_SET, 1 },
    { "SetAe_IT_MAX", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetAe_IT_MAX, 0x8000032, 0, K_SET, 0 },
    { "SetAeMin", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetAeMin, 0x800002f, 0, K_SET, 1 },
    { "SetAeTargetList", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetAeTargetList, 0x8000038, 0, K_SET, 0 },
    { "SetAeWeight", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetAeWeight, 0x800002d, 0, K_SET, 1 },
    { "SetAfHist", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetAfHist, 0x8000042, 0, K_SET, 1 },
    { "SetAfWeight", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetAfWeight, 0x8000044, 0, K_SET, 1 },
    { "SetAutoZoom", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetAutoZoom, 0x80000e8, 0, K_SET, 1 },
    { "SetAwbClust", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetAwbClust, 0x800000e, 0, K_SET, 1 },
    { "SetAwbCt", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetAwbCt, 0x800000d, 0, K_SET, 0 },
    { "SetAwbCtTrend", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetAwbCtTrend, 0x800000f, 0, K_SET, 0 },
    { "SetAwbHist", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetAwbHist, 0x8000007, 0, K_SET, 1 },
    { "SetAwbWeight", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetAwbWeight, 0x8000006, 0, K_SET, 1 },
    { "SetAwbZoneWeight", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetAwbZoneWeight, 0x8000010, 0, K_SET, 1 },
    { "SetBacklightComp", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetBacklightComp, 0x8000037, 0, K_SET, 0 },
    { "SetBcshHue", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetBcshHue, 0x8000101, 0, K_SET, 0 },
    { "SetCsc_Attr", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetCsc_Attr, 0x80000a6, 0, K_SET, 1 },
    { "SetDefog_Strength", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetDefog_Strength, 0x8000039, 0, K_SET, 1 },
    { "SetDPC_Strength", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetDPC_Strength, 0x8000062, 0, K_SET, 0 },
    { "SetDRC_Strength", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetDRC_Strength, 0x80000a2, 0, K_SET, 0 },
    { "SetExpr", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetExpr, 0x8000025, 0, K_SET, 1 },
    { "SetFrontCrop", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetFrontCrop, 0x80000e3, 0, K_SET, 1 },
    { "SetGamma", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetGamma, 0x800002b, 0, K_SET, 0 },
    { "SetHiLightDepress", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetHiLightDepress, 0x800002a, 0, K_SET, 0 },
    { "SetMaxAgain", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetMaxAgain, 0x8000028, 0, K_SET, 0 },
    { "SetMaxDgain", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetMaxDgain, 0x8000029, 0, K_SET, 0 },
    { "SetModuleControl", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetModuleControl, 0x80000e2, 0, K_SET, 1 },
    { "SetScalerLv", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetScalerLv, 0x80000e9, 0, K_SET, 0 },
    { "SetSinterStrength", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetSinterStrength, 0x8000086, 0, K_SET, 0 },
    { "SetTemperStrength", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetTemperStrength, 0x8000085, 0, K_SET, 0 },
    { "SetWB", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetWB, 0x8000004, 0, K_SET, 1 },
    { "SetWB_ALGO", (t23_fn)IMP_ISP_MultiCamera_Tuning_SetWB_ALGO, 0x800000c, 0, K_SET, 0 },
    { "SwitchBin", (t23_fn)IMP_ISP_MultiCamera_Tuning_SwitchBin, 0x8000185, 0, K_SET, 0 },
};

static const struct { t23_fn1 plain, sec; } wraps[] = {
    { (t23_fn1)IMP_ISP_Tuning_AE_GetROI, (t23_fn1)IMP_ISP_Tuning_AE_GetROI_Sec },
    { (t23_fn1)IMP_ISP_Tuning_AE_SetROI, (t23_fn1)IMP_ISP_Tuning_AE_SetROI_Sec },
    { (t23_fn1)IMP_ISP_Tuning_Awb_SetRgbCoefft, (t23_fn1)IMP_ISP_Tuning_Awb_SetRgbCoefft_Sec },
    { (t23_fn1)IMP_ISP_Tuning_EnableDefog, (t23_fn1)IMP_ISP_Tuning_EnableDefog_Sec },
    { (t23_fn1)IMP_ISP_Tuning_EnableDRC, (t23_fn1)IMP_ISP_Tuning_EnableDRC_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetAeComp, (t23_fn1)IMP_ISP_Tuning_GetAeComp_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetAeHist, (t23_fn1)IMP_ISP_Tuning_GetAeHist_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetAeHist_Origin, (t23_fn1)IMP_ISP_Tuning_GetAeHist_Origin_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetAE_IT_MAX, (t23_fn1)IMP_ISP_Tuning_GetAE_IT_MAX_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetAeLuma, (t23_fn1)IMP_ISP_Tuning_GetAeLuma_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetAeMin, (t23_fn1)IMP_ISP_Tuning_GetAeMin_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetAeState, (t23_fn1)IMP_ISP_Tuning_GetAeState_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetAeTargetList, (t23_fn1)IMP_ISP_Tuning_GetAeTargetList_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetAeWeight, (t23_fn1)IMP_ISP_Tuning_GetAeWeight_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetAeZone, (t23_fn1)IMP_ISP_Tuning_GetAeZone_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetAfHist, (t23_fn1)IMP_ISP_Tuning_GetAfHist_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetAFMetrices, (t23_fn1)IMP_ISP_Tuning_GetAFMetrices_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetAfWeight, (t23_fn1)IMP_ISP_Tuning_GetAfWeight_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetAfZone, (t23_fn1)IMP_ISP_Tuning_GetAfZone_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetAutoZoom, (t23_fn1)IMP_ISP_Tuning_GetAutoZoom_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetAwbClust, (t23_fn1)IMP_ISP_Tuning_GetAwbClust_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetAWBCt, (t23_fn1)IMP_ISP_Tuning_GetAWBCt_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetAwbCtTrend, (t23_fn1)IMP_ISP_Tuning_GetAwbCtTrend_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetAwbHist, (t23_fn1)IMP_ISP_Tuning_GetAwbHist_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetAwbWeight, (t23_fn1)IMP_ISP_Tuning_GetAwbWeight_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetAwbZone, (t23_fn1)IMP_ISP_Tuning_GetAwbZone_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetAwbZoneWeight, NULL },
    { (t23_fn1)IMP_ISP_Tuning_GetBacklightComp, (t23_fn1)IMP_ISP_Tuning_GetBacklightComp_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetBcshHue, (t23_fn1)IMP_ISP_Tuning_GetBcshHue_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetBlcAttr, (t23_fn1)IMP_ISP_Tuning_GetBlcAttr_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetCsc_Attr, (t23_fn1)IMP_ISP_Tuning_GetCsc_Attr_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetDefog_Strength, (t23_fn1)IMP_ISP_Tuning_GetDefog_Strength_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetDPC_Strength, (t23_fn1)IMP_ISP_Tuning_GetDPC_Strength_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetDRC_Strength, (t23_fn1)IMP_ISP_Tuning_GetDRC_Strength_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetExpr, (t23_fn1)IMP_ISP_Tuning_GetExpr_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetFrontCrop, (t23_fn1)IMP_ISP_Tuning_GetFrontCrop_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetGamma, (t23_fn1)IMP_ISP_Tuning_GetGamma_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetHiLightDepress, (t23_fn1)IMP_ISP_Tuning_GetHiLightDepress_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetHVFlip, (t23_fn1)IMP_ISP_Tuning_GetHVFlip_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetMask, (t23_fn1)IMP_ISP_Tuning_GetMask_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetMaxAgain, (t23_fn1)IMP_ISP_Tuning_GetMaxAgain_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetMaxDgain, (t23_fn1)IMP_ISP_Tuning_GetMaxDgain_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetModuleControl, (t23_fn1)IMP_ISP_Tuning_GetModuleControl_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetSensorAttr, (t23_fn1)IMP_ISP_Tuning_GetSensorAttr_Sec },
    { (t23_fn1)IMP_ISP_Tuning_GetWB, (t23_fn1)IMP_ISP_Tuning_GetWB_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetAeComp, (t23_fn1)IMP_ISP_Tuning_SetAeComp_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetAeFreeze, (t23_fn1)IMP_ISP_Tuning_SetAeFreeze_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetAeHist, (t23_fn1)IMP_ISP_Tuning_SetAeHist_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetAe_IT_MAX, (t23_fn1)IMP_ISP_Tuning_SetAe_IT_MAX_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetAeMin, (t23_fn1)IMP_ISP_Tuning_SetAeMin_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetAeTargetList, (t23_fn1)IMP_ISP_Tuning_SetAeTargetList_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetAeWeight, (t23_fn1)IMP_ISP_Tuning_SetAeWeight_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetAfHist, (t23_fn1)IMP_ISP_Tuning_SetAfHist_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetAfWeight, NULL },
    { (t23_fn1)IMP_ISP_Tuning_SetAutoZoom, (t23_fn1)IMP_ISP_Tuning_SetAutoZoom_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetAwbClust, (t23_fn1)IMP_ISP_Tuning_SetAwbClust_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetAwbCt, (t23_fn1)IMP_ISP_Tuning_SetAwbCt_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetAwbCtTrend, (t23_fn1)IMP_ISP_Tuning_SetAwbCtTrend_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetAwbHist, (t23_fn1)IMP_ISP_Tuning_SetAwbHist_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetAwbWeight, (t23_fn1)IMP_ISP_Tuning_SetAwbWeight_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetAwbZoneWeight, NULL },
    { (t23_fn1)IMP_ISP_Tuning_SetBacklightComp, (t23_fn1)IMP_ISP_Tuning_SetBacklightComp_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetBcshHue, (t23_fn1)IMP_ISP_Tuning_SetBcshHue_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetCsc_Attr, (t23_fn1)IMP_ISP_Tuning_SetCsc_Attr_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetDefog_Strength, (t23_fn1)IMP_ISP_Tuning_SetDefog_Strength_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetDPC_Strength, (t23_fn1)IMP_ISP_Tuning_SetDPC_Strength_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetDRC_Strength, (t23_fn1)IMP_ISP_Tuning_SetDRC_Strength_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetExpr, (t23_fn1)IMP_ISP_Tuning_SetExpr_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetFrontCrop, (t23_fn1)IMP_ISP_Tuning_SetFrontCrop_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetGamma, (t23_fn1)IMP_ISP_Tuning_SetGamma_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetHiLightDepress, (t23_fn1)IMP_ISP_Tuning_SetHiLightDepress_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetMaxAgain, (t23_fn1)IMP_ISP_Tuning_SetMaxAgain_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetMaxDgain, (t23_fn1)IMP_ISP_Tuning_SetMaxDgain_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetModuleControl, (t23_fn1)IMP_ISP_Tuning_SetModuleControl_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetScalerLv, (t23_fn1)IMP_ISP_Tuning_SetScalerLv_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetSinterStrength, (t23_fn1)IMP_ISP_Tuning_SetSinterStrength_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetTemperStrength, (t23_fn1)IMP_ISP_Tuning_SetTemperStrength_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetWB, (t23_fn1)IMP_ISP_Tuning_SetWB_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SetWB_ALGO, (t23_fn1)IMP_ISP_Tuning_SetWB_ALGO_Sec },
    { (t23_fn1)IMP_ISP_Tuning_SwitchBin, (t23_fn1)IMP_ISP_Tuning_SwitchBin_Sec },
};

static uintptr_t tmpl_arg(unsigned i)
{
    if (!strcmp(tmpl[i].name, "SetBcshHue"))
        return 0x5a;
    return (uintptr_t)buf;
}

static void check_request(unsigned i, int num, uintptr_t arg)
{
    CHECK(calls == 1, "%s: %d ioctls", tmpl[i].name, calls);
    CHECK(last_fd == 9 && last_nr == T23_VIDIOC_TUNING,
          "%s: fd %d nr 0x%lx", tmpl[i].name, last_fd, last_nr);
    CHECK(last_req.dir == tmpl[i].get && last_req.cid == tmpl[i].cid &&
          last_req.sensor == num,
          "%s: dir %d cid 0x%x sensor %d", tmpl[i].name, last_req.dir,
          last_req.cid, last_req.sensor);
    if (tmpl[i].kind != K_WORD)
        CHECK(last_req.value == (uint32_t)arg ||
              (tmpl[i].get && last_req.value == get_value),
              "%s: value 0x%x", tmpl[i].name, last_req.value);
}

static void test_templates(void)
{
    for (unsigned i = 0; i < sizeof(tmpl) / sizeof(tmpl[0]); i++) {
        uint32_t out = 0;
        uintptr_t arg = tmpl[i].kind == K_WORD ? (uintptr_t)&out
                                               : tmpl_arg(i);
        int ret;

        for (int num = 0; num < 4; num++) {
            reset();
            out = 0;
            ret = tmpl[i].fn(num, arg);
            CHECK(ret == 0, "%s(%d) = %d", tmpl[i].name, num, ret);
            check_request(i, num, arg);
            /* GetBcshHue stores one byte, all other inline reads a word */
            if (tmpl[i].kind == K_WORD)
                CHECK(out == (strcmp(tmpl[i].name, "GetBcshHue") ? 0x1234u
                                                                 : 0x34u),
                      "%s: out 0x%x", tmpl[i].name, out);
        }
        reset();
        CHECK(tmpl[i].fn(4, arg) == -4092 && calls == 0,
              "%s: sensor 4", tmpl[i].name);
        reset();
        dev.tuning_state = 1;
        CHECK(tmpl[i].fn(0, arg) == -1 && calls == 0,
              "%s: tuning stopped", tmpl[i].name);
        reset();
        dev.tuning = NULL;
        CHECK(tmpl[i].fn(0, arg) == -1 && calls == 0,
              "%s: no tuning", tmpl[i].name);
        reset();
        gISP = NULL;
        CHECK(tmpl[i].fn(0, arg) == -1 && tmpl[i].fn(5, arg) == -4092,
              "%s: no device", tmpl[i].name);
        reset();
        if (tmpl[i].nullchk)
            CHECK(tmpl[i].fn(0, 0) == -1 && calls == 0,
                  "%s: NULL accepted", tmpl[i].name);
        else if (tmpl[i].kind != K_WORD)
            CHECK(tmpl[i].fn(0, 0) == 0 && calls == 1,
                  "%s: stock passes NULL to the kernel", tmpl[i].name);
        reset();
        ioctl_ret = -1;
        CHECK(tmpl[i].fn(0, arg) == -1, "%s: ioctl error", tmpl[i].name);

        for (int sec = 0; sec < 2; sec++) {
            t23_fn1 w = sec ? wraps[i].sec : wraps[i].plain;

            if (!w)
                continue;
            reset();
            CHECK(w(arg) == 0, "%s wrapper %d", tmpl[i].name, sec);
            check_request(i, sec, arg);
        }
    }
}

/* ---- per-sensor V4L2 controls ----------------------------------------- */
static void test_bcsh(void)
{
    static const unsigned long set_nr[3] = {
        0xc008561c, 0xc008561e, 0xc0085620 };
    static const unsigned long get_nr[3] = {
        0xc008561b, 0xc008561d, 0xc008561f };
    unsigned char v = 0;

    for (int num = 0; num < 4; num++) {
        reset();
        CHECK(IMP_ISP_MultiCamera_Tuning_SetBrightness(num, 0x77) == 0,
              "brightness %d", num);
        if (num < 3)
            CHECK(calls == 1 && last_nr == set_nr[num] &&
                  last_ctrl.id == 0x980900 && last_ctrl.value == 0x77,
                  "brightness nr 0x%lx", last_nr);
        else
            CHECK(calls == 0, "sensor 3 has no control");
        reset();
        get_value = 0x1a5;
        CHECK(IMP_ISP_MultiCamera_Tuning_GetSaturation(num, &v) == 0,
              "saturation get %d", num);
        if (num < 3)
            CHECK(last_nr == get_nr[num] && last_ctrl.id == 0x980902 &&
                  v == 0xa5, "saturation 0x%lx %u", last_nr, v);
        else
            CHECK(calls == 0 && v == 0xff, "sensor 3 reads 255");
    }

    /* Contrast: mode 2 stores and applies; global mode 1 holds the value;
     * mode 0 reapplies the custom value; reads return the custom value. */
    reset();
    CHECK(IMP_ISP_Tuning_SetContrast(0x40) == 0 && calls == 1 &&
          last_nr == 0xc008561c && last_ctrl.id == 0x980901 &&
          last_ctrl.value == 0x40, "contrast set");
    CHECK(IMP_ISP_Tuning_SetContrast_internal(0x90, 1) == 0 &&
          last_ctrl.value == 0x90, "contrast mode 1");
    reset();
    CHECK(IMP_ISP_Tuning_SetContrast(0x50) == 0 && calls == 0,
          "contrast held in mode 1");
    CHECK(IMP_ISP_Tuning_SetContrast_internal(0, 0) == 0 && calls == 1 &&
          last_ctrl.value == 0x50, "contrast mode 0");
    CHECK(IMP_ISP_Tuning_GetContrast(&v) == 0 && v == 0x50,
          "contrast read %u", v);
    CHECK(IMP_ISP_Tuning_SetContrast_internal(0, 3) == -1, "mode 3");
    CHECK(openimp_t23_isp_custom_contrast() == 0x50, "worker contrast");
    CHECK(IMP_ISP_Tuning_GetSharpness_Sec(&v) == 0 && v == 0x80,
          "sharpness default");

    /* Anti-flicker skips an unchanged value and rejects values above 2. */
    reset();
    CHECK(IMP_ISP_Tuning_SetAntiFlickerAttr(0) == 0 && calls == 0,
          "antiflicker unchanged");
    CHECK(IMP_ISP_Tuning_SetAntiFlickerAttr(3) == -1, "antiflicker 3");
    CHECK(IMP_ISP_Tuning_SetAntiFlickerAttr_Sec(1) == 0 &&
          last_nr == 0xc008561e && last_ctrl.id == 0x980918 &&
          last_ctrl.value == 1, "antiflicker sec");
    {
        int attr = -1;

        reset();
        get_value = 2;
        CHECK(IMP_ISP_Tuning_GetAntiFlickerAttr(&attr) == 0 && attr == 2,
              "antiflicker get");
        CHECK(IMP_ISP_MultiCamera_Tuning_GetAntiFlickerAttr(3, &attr) == -1,
              "antiflicker sensor 3");
    }
    {
        unsigned char hue = 0;

        reset();
        get_value = 0x1ff;
        CHECK(IMP_ISP_Tuning_GetBcshHue(&hue) == 0 && hue == 0xff &&
              last_req.cid == 0x8000101, "hue byte");
    }
}

/* ---- conversions -------------------------------------------------------- */
static void test_ccm(void)
{
    T23CCMAttr ccm = { 1, 0, { 1.0f, -0.5f, 0.25f, 0, 0, 0, -1e-6f, 0, 2.0f } };
    T23KernelCCM k;

    reset();
    set_block_len = sizeof(T23KernelCCM);
    CHECK(IMP_ISP_Tuning_SetCCMAttr(&ccm) == 0 && last_req.cid == 0x8000100,
          "ccm set");
    memcpy(&k, set_block, sizeof(k));
    CHECK(k.manual_en == 1 && k.sat_en == 0, "ccm flags");
    CHECK(k.coef[0] == 0x400 && k.coef[1] == 0x3e00 && k.coef[2] == 0x100 &&
          k.coef[6] == 0 && k.coef[8] == 0x800,
          "ccm words %x %x %x %x %x", k.coef[0], k.coef[1], k.coef[2],
          k.coef[6], k.coef[8]);
    CHECK(ccm.ColorMatrix[1] == 0.5f, "stock in-place negation");

    reset();
    memset(&k, 0, sizeof(k));
    k.manual_en = 1;
    k.sat_en = -1;
    k.coef[0] = 0x400;
    k.coef[1] = 0x3e00;
    k.coef[2] = 0x2400;
    get_block_len = sizeof(k);
    memcpy(get_block, &k, sizeof(k));
    CHECK(IMP_ISP_Tuning_GetCCMAttr(&ccm) == 0, "ccm get");
    CHECK(ccm.ManualEn == 1 && ccm.SatEn == -1 &&
          ccm.ColorMatrix[0] == 1.0f && ccm.ColorMatrix[1] == -0.5f &&
          ccm.ColorMatrix[2] == -7.0f, "ccm floats %f %f %f",
          ccm.ColorMatrix[0], ccm.ColorMatrix[1], ccm.ColorMatrix[2]);
}

static void test_ae_attr(void)
{
    uint32_t in[9], out[9], k[0xb0 / 4];

    for (unsigned i = 0; i < 9; i++)
        in[i] = 0x100 + i;
    reset();
    set_block_len = sizeof(k);
    CHECK(IMP_ISP_Tuning_SetAeAttr(in) == 0 && last_req.cid == 0x8000035 &&
          last_req.dir == 0, "ae attr set");
    memcpy(k, set_block, sizeof(k));
    CHECK(k[0] == 0x100 && k[0x3c / 4] == 0x101 && k[0x0c / 4] == 0x102 &&
          k[0x40 / 4] == 0x103 && k[0x04 / 4] == 0x104 &&
          k[0x98 / 4] == 0x105 && k[0x08 / 4] == 0x106 &&
          k[0x44 / 4] == 0x107 && k[0x10 / 4] == 0x108, "ae scatter");
    reset();
    get_block_len = sizeof(k);
    memcpy(get_block, k, sizeof(k));
    CHECK(IMP_ISP_Tuning_GetAeAttr_Sec(out) == 0 && last_req.sensor == 1 &&
          !memcmp(in, out, sizeof(in)), "ae gather");
}

static void test_mask(void)
{
    T23MaskAttr mask;
    T23MaskBlockAttr blk;
    T23DrawBlockAttr draw;

    memset(&mask, 0, sizeof(mask));
    mask.chn[0][0].mask_value[0] = 255;     /* red */
    mask.chn[2][3].mask_value[2] = 255;     /* blue */
    mask.mask_type = 0;
    reset();
    CHECK(IMP_ISP_Tuning_SetMask(&mask) == 0 && last_req.cid == 0x80000e5,
          "mask set");
    CHECK(mask.chn[0][0].mask_value[0] == 76 &&
          mask.chn[0][0].mask_value[1] == 84 &&
          mask.chn[0][0].mask_value[2] == 255, "red -> %u %u %u",
          mask.chn[0][0].mask_value[0], mask.chn[0][0].mask_value[1],
          mask.chn[0][0].mask_value[2]);
    CHECK(mask.chn[1][1].mask_value[0] == 0 &&
          mask.chn[1][1].mask_value[1] == 128 &&
          mask.chn[1][1].mask_value[2] == 128, "black -> yuv");
    CHECK(mask.chn[2][3].mask_value[0] == 29 &&
          mask.chn[2][3].mask_value[1] == 255 &&
          mask.chn[2][3].mask_value[2] == 107, "blue -> %u %u %u",
          mask.chn[2][3].mask_value[0], mask.chn[2][3].mask_value[1],
          mask.chn[2][3].mask_value[2]);

    memset(&blk, 0, sizeof(blk));
    blk.chx = 1;
    blk.pinum = 2;
    blk.mask_en = 1;
    blk.mask_type = 0;
    blk.mask_value[0] = 255;
    reset();
    CHECK(IMP_ISP_Tuning_SetMaskBlock(&blk) == 0 &&
          last_req.cid == 0x8000183, "mask block set");
    CHECK(blk.mask_value[0] == 76, "mask block converted");
    reset();
    memset(blk.mask_value, 0, 3);
    blk.mask_type = 1;
    CHECK(IMP_ISP_Tuning_GetMaskBlock(&blk) == 0 && blk.mask_type == 0 &&
          blk.mask_value[0] == 76, "mask block cache");
    reset();
    ioctl_ret = -1;
    CHECK(IMP_ISP_Tuning_SetMaskBlock(&blk) == -4090, "mask block error");

    memset(&draw, 0, sizeof(draw));
    draw.type = 1;
    draw.cfg[0] = 1;
    draw.cfg[10] = 255;
    reset();
    CHECK(IMP_ISP_Tuning_SetDrawBlock(&draw) == 0 &&
          last_req.cid == 0x8000180 && draw.cfg[10] == 76, "draw block");
    reset();
    CHECK(IMP_ISP_Tuning_SetOSDAttr_Sec(buf) == 0 && last_req.sensor == 1 &&
          last_req.cid == 0x8000181, "osd attr");
    reset();
    ioctl_ret = -1;
    CHECK(IMP_ISP_Tuning_GetOSDBlock(buf) == -4090, "osd block error");
}

static void test_misc(void)
{
    T23WB wb = { 7, 0, 0 };
    uint32_t a = 0, b = 0;
    uint8_t ev[24];
    T23WaitFrameAttr wf = { 33, 0, 0 };

    reset();
    get_value = 0x01230456;
    CHECK(IMP_ISP_Tuning_GetWB_Statis(&wb) == 0 && wb.mode == 7 &&
          wb.rgain == 0x123 && wb.bgain == 0x456 &&
          last_req.cid == 0x8000005, "wb statis");
    reset();
    ioctl_ret = -1;
    wb.rgain = 1;
    CHECK(IMP_ISP_Tuning_GetWB_GOL_Statis(&wb) == -1 && wb.rgain == 1,
          "wb statis kept on error");

    reset();
    dev.tuning_state = 1;
    CHECK(IMP_ISP_Tuning_GetTotalGain(&a) == 0 && a == 0x1234,
          "total gain needs no running tuning");
    reset();
    ioctl_ret = -1;
    a = 5;
    CHECK(IMP_ISP_Tuning_GetTotalGain(&a) == -1 && a == 5,
          "total gain kept on error");

    reset();
    CHECK(IMP_ISP_Tuning_SetSensorFPS(25, 2) == 0 &&
          last_req.cid == 0x80000e0 && last_req.value == ((25u << 16) | 2) &&
          fps_num_seen == 25 && fps_den_seen == 2, "fps set");
    reset();
    get_value = (30u << 16) | 1;
    CHECK(IMP_ISP_Tuning_GetSensorFPS_Sec(&a, &b) == 0 && a == 30 && b == 1 &&
          last_req.sensor == 1, "fps get");

    reset();
    changewait_calls = 0;
    CHECK(IMP_ISP_Tuning_SetISPHflip(1) == 0 && last_req.cid == 0x980914 &&
          last_req.dir == 0 && changewait_calls == 1, "isp hflip");
    CHECK(IMP_ISP_Tuning_SetSensorVflip_Sec(1) == 0 &&
          last_req.cid == 0x8000187 && last_req.sensor == 1, "sensor vflip");
    reset();
    get_value = 1;
    CHECK(IMP_ISP_Tuning_GetISPVflip(&a) == 0 && a == 1 &&
          last_req.cid == 0x980915, "isp vflip get");
    reset();
    CHECK(IMP_ISP_Tuning_SetHVFLIP(3) == 0 && last_req.cid == 0x80000e4 &&
          last_req.value == 3, "hvflip");
    CHECK(IMP_ISP_Tuning_SetHVFLIP(4) == -1, "hvflip 4");

    reset();
    dev.tuning_state = 1;
    CHECK(IMP_ISP_Tuning_SetISPRunningMode(1) == 0 &&
          last_req.cid == 0x80000e1 && openimp_t23_isp_is_day() == 0,
          "running mode");
    CHECK(IMP_ISP_Tuning_SetISPCustomMode_Sec(1) == 0 &&
          last_req.cid == 0x80000e7 && last_req.sensor == 1, "custom mode");
    reset();
    get_value = 1;
    CHECK(IMP_ISP_Tuning_GetISPRunningMode(&a) == 0 && a == 1, "mode get");

    reset();
    get_block_len = sizeof(ev);
    memset(get_block, 0x11, sizeof(ev));
    memset(ev, 0, sizeof(ev));
    CHECK(IMP_ISP_Tuning_GetEVAttr(ev) == 0 && last_req.cid == 0x8000026 &&
          ev[0] == 0x11 && ev[23] == 0x11, "ev attr");

    reset();
    get_block_len = sizeof(wf);
    {
        T23WaitFrameAttr k = { 0, 0, 0x1122334455ull };

        memcpy(get_block, &k, sizeof(k));
    }
    set_block_len = 0;
    CHECK(IMP_ISP_Tuning_WaitFrame(&wf) == 0 && last_req.cid == 0x8000162 &&
          wf.cnt == 0x1122334455ull && wf.timeout == 33, "wait frame");

    reset();
    CHECK(IMP_ISP_Tuning_SetAfWeight_Sec(buf) == 0 && last_req.dir == 1 &&
          last_req.cid == 0x8000044 && last_req.sensor == 1,
          "stock SetAfWeight_Sec reads");
    reset();
    CHECK(IMP_ISP_SetFixedContraster_Sec(buf) == 0 &&
          last_req.cid == 0x8000102 && last_req.sensor == 1, "fixed contrast");
    CHECK(IMP_ISP_SetFixedContraster(NULL) == -1, "fixed contrast NULL");

    reset();
    CHECK(IMP_ISP_MultiCamera_Tuning_AwbSync(0, buf) == 0 && calls == 0,
          "awb sync sensor 0");
    CHECK(IMP_ISP_MultiCamera_Tuning_AwbSync(1, buf) == 0 &&
          last_req.cid == 0x8000011 && last_req.sensor == 1, "awb sync");

    reset();
    CHECK(IMP_ISP_Tuning_SetISPProcess(1) == 0 && last_nr == 0xc008561c &&
          last_ctrl.id == 0x8000164 && last_ctrl.value == 1, "isp process");
    reset();
    CHECK(IMP_ISP_Tuning_SetISPProcess(1) == 0 && calls == 0,
          "isp process unchanged");
    CHECK(IMP_ISP_Tuning_SetShading(1) == 0 && last_ctrl.id == 0x8000166,
          "shading");
    reset();
    CHECK(IMP_ISP_Tuning_GetNCUInfo(buf) == 0 && last_req.cid == 0x8000084 &&
          last_req.dir == 1, "ncu info");

    reset();
    {
        T23CameraInputMode m = { 0x12, 0 };

        CHECK(IMP_ISP_SetCameraInputMode(&m) == 0 && last_fd == 7 &&
              last_nr == 0xc00456c9, "camera input mode");
        m.mode = 0x19;
        CHECK(IMP_ISP_SetCameraInputMode(&m) == -1, "input mode nibble");
    }
    reset();
    CHECK(IMP_ISP_StreamCheck(buf) == 0 && last_nr == 0xc00456ec &&
          IMP_ISP_SetStreamOut(buf) == 0 && last_nr == 0xc00456ed &&
          IMP_ISP_SetFrameDrop_Sec(buf) == 0 && last_nr == 0xc00456ea &&
          IMP_ISP_GetFrameDrop_Sec(buf) == 0 && last_nr == 0xc00456eb,
          "device requests");
    CHECK(IMP_ISP_MultiCamera_SetSensorRegister(0, 1, 2) == 0 &&
          IMP_ISP_MultiCamera_GetFrameDrop(4, buf) == -4092 &&
          IMP_ISP_SetDefaultBinPath_Sec("x") == -1, "unsupported calls");
}

static void test_movestate(void)
{
    uint8_t ae[0x70];

    reset();
    get_value = 1;                          /* night */
    memset(t23_movestate.cfg, 0, sizeof(t23_movestate.cfg));
    t23_movestate.cfg[1] = 0x21;
    t23_movestate.cfg[13] = 0x2d;
    set_block_len = sizeof(ae);
    CHECK(IMP_ISP_Tuning_EnableMovestate() == 0 && last_req.dir == 0 &&
          last_req.cid == 0x800002c, "movestate enable");
    memcpy(ae, set_block, sizeof(ae));
    CHECK(ae[11] == 1 && ae[46] == 0x21 && ae[38] == 0x2d && ae[71] == 1 &&
          ae[94] == 1 && ae[10] == 0, "night block");
    CHECK(t23_movestate.mode == 1, "captured mode");
    reset();
    idr_requests = 0;
    get_value = 1;
    CHECK(IMP_ISP_Tuning_DisableMovestate() == 0 && idr_requests == 1 &&
          last_req.dir == 0 && last_req.cid == 0x800002c, "movestate restore");
    reset();
    get_value = 0;
    idr_requests = 0;
    CHECK(IMP_ISP_Tuning_DisableMovestate() == 0 && idr_requests == 1 &&
          last_req.dir == 1, "mode changed: no restore");
}

static void *run(void *unused)
{
    (void)unused;
    test_templates();
    test_bcsh();
    test_ccm();
    test_ae_attr();
    test_mask();
    test_misc();
    test_movestate();
    return NULL;
}

int main(void)
{
    const size_t stack_size = 1u << 20;
    void *stack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    pthread_attr_t attr;
    pthread_t thread;

    if (stack == MAP_FAILED || (uintptr_t)buf > 0xffffffffu) {
        fprintf(stderr, "needs a non-PIE x86-64 build\n");
        return 2;
    }
    pthread_attr_init(&attr);
    pthread_attr_setstack(&attr, stack, stack_size);
    if (pthread_create(&thread, &attr, run, NULL) != 0)
        return 2;
    pthread_join(thread, NULL);
    if (failures) {
        fprintf(stderr, "%d T23 ISP tuning checks failed\n", failures);
        return 1;
    }
    printf("T23 ISP tuning tests passed (%zu template calls)\n",
           sizeof(tmpl) / sizeof(tmpl[0]));
    return 0;
}
