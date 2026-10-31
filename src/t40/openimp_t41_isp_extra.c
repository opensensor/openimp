/*
 * T41 ISP calls of the vendor libimp 1.2.5 / 1.2.6 that no streamer uses
 * (T41 backlog #75, #76).  Each one is rebuilt from the vendor disassembly:
 *
 *  - tuning pointer pass-throughs (ioctl 0xc0105435, request {sensor,
 *    direction, control, pointer}, the public structure goes to the kernel
 *    unchanged) with the vendor checks and codes: NULL -4087, sensor >= 2
 *    -4084, tuning not enabled -4091, failed ioctl -4090;
 *  - ISP-device ioctls (the vendor ISPDevice fd): ISP not open -4088, NULL
 *    -4087, sensor >= 2 -4084, failed ioctl -4095 (the WDR and GPIO calls -4090);
 *  - four calls that need a user-space callback thread fed by the ISP module
 *    (SetAeAlgoFunc, SetAwbAlgoFunc, SetVicDoneCbFunc) or a RAW frame
 *    exchange with rmem buffers (RAW_RwControl) are exported as a documented
 *    ENOTSUP (-1, errno ENOTSUP): the open T41 module has no event path for
 *    them, and a program that links them fails at the first call instead of
 *    at load time.
 *
 * Whether the running ISP module serves a control is the module's business:
 * the open-tx-isp T41 module answers EPERM to a tuning control it does not
 * route ("tuning control 0x... not implemented"), which comes back here as
 * -4090.  Control numbers and structure sizes come from the vendor library
 * (comments at each call); the sizes are checked below.
 */
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <imp/imp_isp.h>

extern int OpenIMP_P1_TuningIOCtl(uint32_t command, void *argument);
extern int OpenIMP_P1_TuningEnabled(void);
extern int OpenIMP_P1_IspIOCtlOpened(uint32_t command, void *argument);

#define T41X_TUNING_IOCTL 0xc0105435U

#define T41X_E_GENERIC (-4095)     /* vendor 0xfffff001 */
#define T41X_E_IOCTL   (-4090)     /* 0xfffff006 */
#define T41X_E_NOTRUN  (-4091)     /* 0xfffff005 */
#define T41X_E_NOTOPEN (-4088)     /* 0xfffff008 */
#define T41X_E_PARAM   (-4087)     /* 0xfffff009 */
#define T41X_E_NUM     (-4084)     /* 0xfffff00c */

typedef struct {
    int32_t vinum;
    int32_t direction;
    int32_t control;
    uintptr_t payload;
} T41xRequest;

static int32_t t41x_tuning(IMPVI_NUM num, int direction, int control, void *attr)
{
    T41xRequest request;

    if (!attr)
        return T41X_E_PARAM;
    if ((unsigned)num >= 2U)
        return T41X_E_NUM;
    if (!OpenIMP_P1_TuningEnabled())
        return T41X_E_NOTRUN;
    request.vinum = num;
    request.direction = direction;
    request.control = control;
    request.payload = (uintptr_t)attr;
    return OpenIMP_P1_TuningIOCtl(T41X_TUNING_IOCTL, &request) ? T41X_E_IOCTL : 0;
}

/* ISP-device ioctl: only "the ISP device is open" is required here. */
static int32_t t41x_isp(IMPVI_NUM num, uint32_t command, void *attr, int needs_attr, int fail_code)
{
    if (needs_attr && !attr)
        return T41X_E_PARAM;
    if ((unsigned)num >= 2U)
        return T41X_E_NUM;
    if (OpenIMP_P1_IspIOCtlOpened(command, attr) != 0)
        return errno == ENODEV ? T41X_E_NOTOPEN : fail_code;
    return 0;
}

/* ---- tuning pointer pass-throughs -------------------------------------- */

/* sizes of the vendor structures (libimp 1.2.5 memcpy sizes) */
typedef struct { uint32_t global_weight, roi_weight, smooth; } T41xFaceAeWeiget;     /* 12 */
typedef struct { uint32_t global, roi; } T41xFaceAeLuma;                               /* 8 */
typedef struct { int flag; } T41xFlickerFlag;                                          /* 4 */
typedef struct {
    uint8_t tmo_roinum;
    uint16_t tmo_xcent[6], tmo_ycent[6], tmo_distep[6], tmo_uplumthr[6];
    uint8_t tmo_upk[6];
} T41xTmoFaceae;                                                                       /* 56 */
_Static_assert(sizeof(T41xFaceAeWeiget) == 12, "IMPISPFaceAeWeiget");
_Static_assert(sizeof(T41xFaceAeLuma) == 8, "IMPISPFaceAeLuma");
_Static_assert(sizeof(T41xFlickerFlag) == 4, "IMPISPFlickerFlag");
_Static_assert(sizeof(T41xTmoFaceae) == 56, "IMPISPTmoFaceae");
_Static_assert(sizeof(IMPISPAFMetricsInfo) == 12, "IMPISPAFMetricsInfo");
_Static_assert(sizeof(IMPISPAFStatisInfo) == 0x1518, "IMPISPAFStatisInfo");
_Static_assert(sizeof(IMPISPCsccrModeAttr) == 12, "IMPISPCsccrModeAttr");

/* Set/GetFaceAeWeiget: control 0x8000036 (vendor 1.2.5 0x7a... / 1.2.6) */
int32_t IMP_ISP_Tuning_SetFaceAeWeiget(IMPVI_NUM num, void *weiget);
int32_t IMP_ISP_Tuning_GetFaceAeWeiget(IMPVI_NUM num, void *weiget);
int32_t IMP_ISP_Tuning_SetFaceAeWeiget(IMPVI_NUM num, void *weiget)
{
    return t41x_tuning(num, 0, 0x08000036, weiget);
}

int32_t IMP_ISP_Tuning_GetFaceAeWeiget(IMPVI_NUM num, void *weiget)
{
    return t41x_tuning(num, 1, 0x08000036, weiget);
}

/* GetFaceAeLuma: control 0x8000037, 8 bytes {global, roi} */
int32_t IMP_ISP_Tuning_GetFaceAeLuma(IMPVI_NUM num, void *luma);
int32_t IMP_ISP_Tuning_GetFaceAeLuma(IMPVI_NUM num, void *luma)
{
    return t41x_tuning(num, 1, 0x08000037, luma);
}

/* GetAEFlickerFlag: control 0x800003a, one int */
int32_t IMP_ISP_Tuning_GetAEFlickerFlag(IMPVI_NUM num, void *flag);
int32_t IMP_ISP_Tuning_GetAEFlickerFlag(IMPVI_NUM num, void *flag)
{
    return t41x_tuning(num, 1, 0x0800003a, flag);
}

/* GetAeBv (1.2.6 only): control 0x80000b3, one int */
int32_t IMP_ISP_Tuning_GetAeBv(IMPVI_NUM num, int *bv);
int32_t IMP_ISP_Tuning_GetAeBv(IMPVI_NUM num, int *bv)
{
    return t41x_tuning(num, 1, 0x080000b3, bv);
}

/* SetTmoFaceae: control 0x8000062, 56 bytes */
int32_t IMP_ISP_Tuning_SetTmoFaceae(IMPVI_NUM num, void *attr);
int32_t IMP_ISP_Tuning_SetTmoFaceae(IMPVI_NUM num, void *attr)
{
    return t41x_tuning(num, 0, 0x08000062, attr);
}

/* GetAFMetricesInfo (control 0x8000034, 12 bytes), GetAfStatistics
 * (0x8000031, 0x1518 bytes) */
int32_t IMP_ISP_Tuning_GetAFMetricesInfo(IMPVI_NUM num, IMPISPAFMetricsInfo *metric)
{
    return t41x_tuning(num, 1, 0x08000034, metric);
}

int32_t IMP_ISP_Tuning_GetAfStatistics(IMPVI_NUM num, IMPISPAFStatisInfo *af_statis)
{
    return t41x_tuning(num, 1, 0x08000031, af_statis);
}

/* Set/GetCsccrMode: control 0x8000097, 12 bytes (mode, parameters) */
int32_t IMP_ISP_SetCsccrMode(IMPVI_NUM num, IMPISPCsccrModeAttr *attr)
{
    return t41x_tuning(num, 0, 0x08000097, attr);
}

int32_t IMP_ISP_GetCsccrMode(IMPVI_NUM num, IMPISPCsccrModeAttr *attr)
{
    return t41x_tuning(num, 1, 0x08000097, attr);
}

/* ---- ISP-device ioctls -------------------------------------------------- */

/* StartNightMode (ioctl 0xc004542e, the sensor number by pointer): call after
 * IMP_ISP_Open and before AddSensor so the sensor starts in night mode.  The
 * vendor stores the request when the ISP is not open yet and replays it from
 * AddSensor; OpenIMP requires the open ISP device (-4088). */
int32_t IMP_ISP_StartNightMode(IMPVI_NUM num)
{
    uint32_t sensor = (uint32_t)num;

    return t41x_isp(num, 0xc004542eU, &sensor, 0, T41X_E_GENERIC);
}

/* Set/GetInternalChnAttr: ioctl 0xc004542f / 0xc0045430, 24 bytes */
_Static_assert(sizeof(IMPISPInternalChnAttr) == 24, "IMPISPInternalChnAttr");

int32_t IMP_ISP_SetInternalChnAttr(IMPVI_NUM num, IMPISPInternalChnAttr *attr)
{
    return t41x_isp(num, 0xc004542fU, attr, 1, T41X_E_GENERIC);
}

int32_t IMP_ISP_GetInternalChnAttr(IMPVI_NUM num, IMPISPInternalChnAttr *attr)
{
    return t41x_isp(num, 0xc0045430U, attr, 1, T41X_E_GENERIC);
}

/* LDC_INIT (ioctl 0xc0045479, IMPISPLDCInitAttr 0xe1c bytes), LDC_SetAttr
 * (0xc004547a) and LDC_GetAttr (0xc004547b), IMPISPLDCAttr */
_Static_assert(sizeof(IMPISPLDCInitAttr) == 0xe1c, "IMPISPLDCInitAttr");

int32_t IMP_ISP_LDC_INIT(IMPVI_NUM num, IMPISPLDCInitAttr *attr)
{
    return t41x_isp(num, 0xc0045479U, attr, 1, T41X_E_GENERIC);
}

int32_t IMP_ISP_LDC_SetAttr(IMPVI_NUM num, IMPISPLDCAttr *attr)
{
    return t41x_isp(num, 0xc004547aU, attr, 1, T41X_E_GENERIC);
}

int32_t IMP_ISP_LDC_GetAttr(IMPVI_NUM num, IMPISPLDCAttr *attr)
{
    return t41x_isp(num, 0xc004547bU, attr, 1, T41X_E_GENERIC);
}

/* SET_GPIO_INIT_OR_FREE / SET_GPIO_STA (1.2.6): ioctl 0xc004548e / 0xc004548f,
 * IMPISPGPIO (10 + 10 + 1 halfwords = 42 bytes).  The vendor logs a failing
 * ioctl and still returns 0; here the failure is returned (-4090). */
int32_t IMP_ISP_SET_GPIO_INIT_OR_FREE(IMPVI_NUM num, void *attr);
int32_t IMP_ISP_SET_GPIO_STA(IMPVI_NUM num, void *attr);

int32_t IMP_ISP_SET_GPIO_INIT_OR_FREE(IMPVI_NUM num, void *attr)
{
    return t41x_isp(num, 0xc004548eU, attr, 1, T41X_E_IOCTL);
}

int32_t IMP_ISP_SET_GPIO_STA(IMPVI_NUM num, void *attr)
{
    return t41x_isp(num, 0xc004548fU, attr, 1, T41X_E_IOCTL);
}

/* WDR_OPEN (ioctl 0xc004548d, {sensor byte, imode}, 8 bytes): linear start
 * mode only; the WDR run mode switches WDR on through IMP_ISP_WDR_ENABLE
 * (whose result the vendor ignores).  Header: first call before AddSensor. */
struct t41x_wdr_open {
    uint8_t sensor;
    uint8_t pad[3];
    int32_t imode;
};
_Static_assert(sizeof(struct t41x_wdr_open) == 8, "WDR_OPEN request is 8 bytes");

int32_t IMP_ISP_WDR_OPEN(IMPVI_NUM num, IMPISPWdrOpenAttr *attr)
{
    if (!attr)
        return T41X_E_PARAM;
    if ((unsigned)num >= 2U)
        return T41X_E_NUM;
    if (attr->rmode == IMPISP_TYPE_RUN_LINEAR) {
        struct t41x_wdr_open req;

        memset(&req, 0, sizeof(req));
        req.sensor = (uint8_t)num;
        req.imode = (int32_t)attr->imode;
        if (OpenIMP_P1_IspIOCtlOpened(0xc004548dU, &req) != 0)
            return errno == ENODEV ? T41X_E_NOTOPEN : T41X_E_IOCTL;
        return 0;
    }
    if (attr->rmode == IMPISP_TYPE_RUN_WDR) {
        IMPISPTuningOpsMode mode = IMPISP_TUNING_OPS_MODE_ENABLE;

        (void)IMP_ISP_WDR_ENABLE(num, &mode);
        return 0;
    }
    return T41X_E_PARAM;
}

/* ---- documented ENOTSUP -------------------------------------------------- */

static int32_t t41x_unsupported(void)
{
    errno = ENOTSUP;
    return -1;
}

/* SetAeAlgoFunc / SetAwbAlgoFunc: the vendor registers user callbacks that a
 * thread runs on every statistics event of the ISP module (ioctl 0xc004541d /
 * 0xc004541e).  The open T41 module runs its own AE and AWB and has no such
 * event path. */
int32_t IMP_ISP_SetAeAlgoFunc(IMPVI_NUM num, IMPISPAeAlgoFunc *ae_func)
{
    (void)num;
    (void)ae_func;
    return t41x_unsupported();
}

int32_t IMP_ISP_SetAwbAlgoFunc(IMPVI_NUM num, IMPISPAwbAlgoFunc *awb_func)
{
    (void)num;
    (void)awb_func;
    return t41x_unsupported();
}

/* SetVicDoneCbFunc: a callback thread fed by the VIC frame-done event of the
 * ISP module (ioctl 0xc0045431); the open module has no such event path. */
int32_t IMP_ISP_SetVicDoneCbFunc(void (*cb)(void))
{
    (void)cb;
    return t41x_unsupported();
}

/* RAW_RwControl: RAW frames in and out of the ISP through rmem buffers and a
 * frame-done thread (control 0x80000a7); the open module has no RAW path. */
int32_t IMP_ISP_RAW_RwControl(IMPVI_NUM num, IMPISPRawRwAttr *attr)
{
    (void)num;
    (void)attr;
    return t41x_unsupported();
}
