/*
 * T23 (SDK 1.3.0) ISP tuning API.
 *
 * The stock T23 libimp implements every tuning call once, as
 * IMP_ISP_MultiCamera_Tuning_X(IMPVI_NUM num, ...).  IMP_ISP_Tuning_X(...)
 * is that call with num 0 and IMP_ISP_Tuning_X_Sec(...) the same with
 * num 1.  This file mirrors that structure.  Every request, struct layout,
 * conversion and return code below was taken from the disassembly of the
 * stock T23 1.3.0 uclibc libimp.so (dl/ingenic-lib T23/lib/1.3.0); the
 * comments name the vendor behaviour where it is not obvious.
 *
 * Kernel interface (all on the tuning node, /dev/isp-m0):
 *  - VIDIOC_S_CTRL/G_CTRL style controls with one ioctl number per sensor:
 *    sensor 0 uses 0xc008561c/0xc008561b, sensor 1 0xc008561e/0xc008561d,
 *    sensor 2 0xc0085620/0xc008561f.  Sensor 3 has none; the stock library
 *    then only updates its cache.
 *  - The 16-byte tuning request 0xc01056c6
 *        { u32 dir (0 set, 1 get); u32 cid; u32 value_or_ptr; u32 sensor; }
 *
 * Return codes: a sensor number >= 4 gives -4092 (vendor CODE 4100), a
 * missing ISP device, tuning object or required pointer -1, a tuning state
 * other than "running" -1, otherwise the ioctl result.  The ISP-OSD, mask
 * block and draw block calls return -4090 (CODE 4102) on an ioctl failure.
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/ioctl.h>

#include "isp_tseries_dev.h"

int IMP_Log_Get_Option(void);
void imp_log_fun(int level, int option, int type, ...);
int IMP_Encoder_RequestIDR(int encChn);
int32_t set_framesource_fps(int32_t fps_num, int32_t fps_den);
int32_t set_framesource_changewait_cnt(void);
int IMP_ISP_SetDefaultBinPath(const char *path);
int IMP_ISP_GetDefaultBinPath(char *path);

#define T23_LOG_ERR(fn, ...)                                               \
    imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",                     \
                "/home_a/ingenic/isvp_t23/proj/sdk-lv3/src/imp/isp/"       \
                "isp_tseries.c", __LINE__, fn, __VA_ARGS__)

#define T23_VI_MAX 4
#define T23_ERR_VI_OVERFLOW (-4092) /* stock CODE 4100 */
#define T23_ERR_IOCTL (-4090)       /* stock CODE 4102 */

#define T23_VIDIOC_TUNING 0xc01056c6U
#define T23_VIDIOC_G_CTRL 0xc008561bU
#define T23_VIDIOC_S_CTRL 0xc008561cU
#define T23_VIDIOC_SET_CAMERA_INPUT_MODE 0xc00456c9U
#define T23_VIDIOC_SET_FRAME_DROP_SEC 0xc00456eaU
#define T23_VIDIOC_GET_FRAME_DROP_SEC 0xc00456ebU
#define T23_VIDIOC_STREAM_CHECK 0xc00456ecU
#define T23_VIDIOC_SET_STREAM_OUT 0xc00456edU
#define T23_VIDIOC_SET_SENSOR_REG 0x8038564fU
#define T23_VIDIOC_GET_SENSOR_REG 0xc0385650U

/* V4L2 controls, sent either as G/S_CTRL or inside the tuning request. */
#define T23_CID_BRIGHTNESS 0x00980900U
#define T23_CID_CONTRAST 0x00980901U
#define T23_CID_SATURATION 0x00980902U
#define T23_CID_HFLIP 0x00980914U
#define T23_CID_VFLIP 0x00980915U
#define T23_CID_POWER_LINE_FREQUENCY 0x00980918U
#define T23_CID_SHARPNESS 0x0098091bU

/* IMAGE_TUNING_CID_* of the T23 libimp. */
enum {
    T23_CID_WB_ATTR = 0x8000004,
    T23_CID_WB_STATIS = 0x8000005,
    T23_CID_AWB_WEIGHT = 0x8000006,
    T23_CID_AWB_HIST = 0x8000007,
    T23_CID_AWB_RGB_COEFFT = 0x8000008,
    T23_CID_WB_GOL_STATIS = 0x8000009,
    T23_CID_AWB_ZONE = 0x800000b,
    T23_CID_WB_ALGO = 0x800000c,
    T23_CID_AWB_CT = 0x800000d,
    T23_CID_AWB_CLUSTER = 0x800000e,
    T23_CID_AWB_CT_TREND = 0x800000f,
    T23_CID_AWB_ZONE_WEIGHT = 0x8000010,
    T23_CID_AWB_SYNC = 0x8000011,
    T23_CID_AE_COMP = 0x8000023,
    T23_CID_AE_ROI = 0x8000024,
    T23_CID_EXPR = 0x8000025,
    T23_CID_EV_ATTR = 0x8000026,
    T23_CID_TOTAL_GAIN = 0x8000027,
    T23_CID_MAX_AGAIN = 0x8000028,
    T23_CID_MAX_DGAIN = 0x8000029,
    T23_CID_HILIGHT_DEPRESS = 0x800002a,
    T23_CID_GAMMA = 0x800002b,
    T23_CID_MOVESTATE = 0x800002c,
    T23_CID_AE_WEIGHT = 0x800002d,
    T23_CID_AE_HIST = 0x800002e,
    T23_CID_AE_MIN = 0x800002f,
    T23_CID_AE_ZONE = 0x8000030,
    T23_CID_AE_HIST_ORIGIN = 0x8000031,
    T23_CID_AE_IT_MAX = 0x8000032,
    T23_CID_AE_LUMA = 0x8000033,
    T23_CID_AE_FREEZE = 0x8000034,
    T23_CID_AE_ATTR = 0x8000035,
    T23_CID_AE_STATE = 0x8000036,
    T23_CID_BACKLIGHT_COMP = 0x8000037,
    T23_CID_AE_TARGET_LIST = 0x8000038,
    T23_CID_DEFOG_STRENGTH = 0x8000039,
    T23_CID_AF_HIST = 0x8000042,
    T23_CID_AF_METRICES = 0x8000043,
    T23_CID_AF_WEIGHT = 0x8000044,
    T23_CID_SENSOR_ATTR = 0x8000045,
    T23_CID_AF_ZONE = 0x8000046,
    T23_CID_DPC_STRENGTH = 0x8000062,
    T23_CID_NCU_INFO = 0x8000084,
    T23_CID_TEMPER_STRENGTH = 0x8000085,
    T23_CID_SINTER_STRENGTH = 0x8000086,
    T23_CID_DRC_STRENGTH = 0x80000a2,
    T23_CID_ENABLE_DRC = 0x80000a3,
    T23_CID_ENABLE_DEFOG = 0x80000a4,
    T23_CID_BLC_ATTR = 0x80000a5,
    T23_CID_CSC_ATTR = 0x80000a6,
    T23_CID_SENSOR_FPS = 0x80000e0,
    T23_CID_RUNNING_MODE = 0x80000e1,
    T23_CID_MODULE_CONTROL = 0x80000e2,
    T23_CID_FRONT_CROP = 0x80000e3,
    T23_CID_HV_FLIP = 0x80000e4,
    T23_CID_MASK = 0x80000e5,
    T23_CID_CUSTOM_MODE = 0x80000e7,
    T23_CID_AUTO_ZOOM = 0x80000e8,
    T23_CID_SCALER_LV = 0x80000e9,
    T23_CID_CCM_ATTR = 0x8000100,
    T23_CID_BCSH_HUE = 0x8000101,
    T23_CID_FIXED_CONTRAST = 0x8000102,
    T23_CID_WAIT_FRAME = 0x8000162,
    T23_CID_ISP_PROCESS = 0x8000164,
    T23_CID_FW_FREEZE = 0x8000165,
    T23_CID_SHADING = 0x8000166,
    T23_CID_DRAW_BLOCK = 0x8000180,
    T23_CID_OSD_ATTR = 0x8000181,
    T23_CID_OSD_BLOCK = 0x8000182,
    T23_CID_MASK_BLOCK = 0x8000183,
    T23_CID_SWITCH_BIN = 0x8000185,
    T23_CID_SENSOR_HFLIP = 0x8000186,
    T23_CID_SENSOR_VFLIP = 0x8000187,
};

typedef struct {
    int32_t dir;    /* 0 set, 1 get */
    uint32_t cid;
    uint32_t value; /* inline value or user pointer */
    int32_t sensor;
} T23TuningReq;

typedef struct {
    uint32_t id;
    int32_t value;
} T23Ctrl;

/* Public structures the stock library converts.  Sizes match the T23
 * 1.3.0 headers (checked with the vendor header and the cross compiler). */
typedef struct {
    int32_t AeFreezenEn;
    int32_t AeItManualEn;
    uint32_t AeIt;
    int32_t AeAGainManualEn;
    uint32_t AeAGain;
    int32_t AeDGainManualEn;
    uint32_t AeDGain;
    int32_t AeIspDGainManualEn;
    uint32_t AeIspDGain;
} T23AEAttr;                                /* IMPISPAEAttr */

typedef struct {
    int32_t ManualEn;
    int32_t SatEn;
    float ColorMatrix[9];
} T23CCMAttr;                               /* IMPISPCCMAttr */

typedef struct {
    int8_t manual_en;
    int8_t sat_en;
    uint8_t reserved[2];
    uint32_t coef[9];                       /* 14-bit sign/magnitude Q10 */
} T23KernelCCM;

typedef struct {
    uint32_t ev[6];
} T23EVAttr;                                /* IMPISPEVAttr */

typedef struct {
    uint32_t mode;
    uint16_t rgain;
    uint16_t bgain;
} T23WB;                                    /* IMPISPWB */

typedef struct {
    uint32_t timeout;
    uint32_t reserved;
    uint64_t cnt;
} T23WaitFrameAttr;                         /* IMPISPWaitFrameAttr */

typedef struct {
    uint8_t mask_en;
    uint8_t pad0;
    uint16_t mask_pos_top;
    uint16_t mask_pos_left;
    uint16_t mask_width;
    uint16_t mask_height;
    uint8_t mask_value[3];
    uint8_t pad1;
} T23MaskBlockPar;                          /* IMPISP_MASK_BLOCK_PAR */

typedef struct {
    T23MaskBlockPar chn[3][4];
    int32_t mask_type;                      /* 0 RGB, 1 YUV */
} T23MaskAttr;                              /* IMPISPMASKAttr */

typedef struct {
    uint8_t chx;
    uint8_t pinum;
    uint8_t mask_en;
    uint8_t pad0;
    uint16_t mask_pos_top;
    uint16_t mask_pos_left;
    uint16_t mask_width;
    uint16_t mask_height;
    int32_t mask_type;
    uint8_t mask_value[3];
    uint8_t pad1;
} T23MaskBlockAttr;                         /* IMPISPMaskBlockAttr */

typedef struct {
    uint8_t pinum;
    uint8_t pad0[3];
    int32_t type;                           /* IMPISPDrawType */
    int32_t color_type;                     /* IMPISP_MASK_TYPE */
    uint8_t cfg[20];                        /* wind/rang/line: enable at 0,
                                               colour at 10 */
} T23DrawBlockAttr;                         /* IMPISPDrawBlockAttr */

typedef struct {
    uint32_t mode;
    uint32_t reserved;
} T23CameraInputMode;                       /* IMPISPCameraInputMode */

typedef struct {
    int32_t sensor_type;
    uint32_t sensor_info[8];
    uint32_t size;
    int32_t reg;
    int32_t zero0;
    int32_t val;
    int32_t zero1;
} T23SensorRegReq;

_Static_assert(sizeof(T23TuningReq) == 16, "T23 tuning request ABI");
_Static_assert(sizeof(T23Ctrl) == 8, "T23 control ABI");
_Static_assert(sizeof(T23AEAttr) == 36, "IMPISPAEAttr ABI");
_Static_assert(sizeof(T23CCMAttr) == 44, "IMPISPCCMAttr ABI");
_Static_assert(sizeof(T23KernelCCM) == 40, "T23 kernel CCM ABI");
_Static_assert(sizeof(T23EVAttr) == 24, "IMPISPEVAttr ABI");
_Static_assert(sizeof(T23WB) == 8, "IMPISPWB ABI");
_Static_assert(sizeof(T23WaitFrameAttr) == 16, "IMPISPWaitFrameAttr ABI");
_Static_assert(offsetof(T23WaitFrameAttr, cnt) == 8, "IMPISPWaitFrameAttr ABI");
_Static_assert(sizeof(T23MaskBlockPar) == 14, "IMP_ISP_MASK_BLOCK_PAR ABI");
_Static_assert(offsetof(T23MaskBlockPar, mask_value) == 10,
               "IMP_ISP_MASK_BLOCK_PAR ABI");
_Static_assert(sizeof(T23MaskAttr) == 172, "IMPISPMASKAttr ABI");
_Static_assert(offsetof(T23MaskAttr, mask_type) == 168, "IMPISPMASKAttr ABI");
_Static_assert(sizeof(T23MaskBlockAttr) == 20, "IMPISPMaskBlockAttr ABI");
_Static_assert(offsetof(T23MaskBlockAttr, mask_type) == 12,
               "IMPISPMaskBlockAttr ABI");
_Static_assert(offsetof(T23MaskBlockAttr, mask_value) == 16,
               "IMPISPMaskBlockAttr ABI");
_Static_assert(sizeof(T23DrawBlockAttr) == 32, "IMPISPDrawBlockAttr ABI");
_Static_assert(offsetof(T23DrawBlockAttr, cfg) == 12, "IMPISPDrawBlockAttr ABI");
_Static_assert(sizeof(T23CameraInputMode) == 8, "IMPISPCameraInputMode ABI");
_Static_assert(sizeof(T23SensorRegReq) == 0x38, "T23 sensor register ABI");

/* ------------------------------------------------------------------------
 * State the stock library keeps in its tuning object and globals.
 * ---------------------------------------------------------------------- */

/* custom_contrast/custom_sharpness are three-byte .data arrays in the stock
 * library ({0x80, 0x80, 0x00}); index 3 reads the padding byte after them. */
static uint8_t t23_custom_contrast[T23_VI_MAX] = { 0x80, 0x80, 0x00, 0x00 };
static uint8_t t23_custom_sharpness[T23_VI_MAX] = { 0x80, 0x80, 0x00, 0x00 };
static uint32_t t23_global_mode = 2;
static int32_t t23_is_isp_day = 1;
static uint8_t t23_brightness[T23_VI_MAX];
static uint8_t t23_saturation[T23_VI_MAX];
static uint8_t t23_contrast_cache[T23_VI_MAX];
static uint8_t t23_sharpness_cache[T23_VI_MAX];
static int32_t t23_antiflicker[T23_VI_MAX];
static uint32_t t23_fps_num;
static uint32_t t23_fps_den;
static int32_t t23_isp_process;
static int32_t t23_fw_freeze;
static void *t23_video_drop;
static uint32_t t23_camera_input_mode;
static T23MaskBlockAttr t23_mask_block[16];
static T23DrawBlockAttr t23_draw_block[T23_VI_MAX * 20];
static pthread_mutex_t t23_running_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t t23_deamon_mutex = PTHREAD_MUTEX_INITIALIZER;

/* gmovestate: the 0x70-byte AE block of CID 0x800002c, the running mode
 * it was captured in, and the 14 values of /etc/sensor/<name>move.txt. */
static struct {
    uint8_t ae[0x70];
    int32_t mode;
    uint32_t cfg[14];
} t23_movestate;
_Static_assert(sizeof(t23_movestate) == 0xac, "gmovestate size");

/* ------------------------------------------------------------------------
 * Common request paths
 * ---------------------------------------------------------------------- */

enum {
    T23F_NULLCHK = 1u << 0, /* the argument pointer must not be NULL */
    T23F_NOSTATE = 1u << 1, /* no "tuning running" check */
};

static int t23_prologue(const char *fn, int num, const void *arg,
                        unsigned flags, ISPDevice **out)
{
    ISPDevice *isp = gISP;

    if ((unsigned)num >= T23_VI_MAX) {
        T23_LOG_ERR(fn, "MSG: Number of sensors overflow!\n");
        return T23_ERR_VI_OVERFLOW;
    }
    if (isp == NULL || isp->tuning == NULL ||
        ((flags & T23F_NULLCHK) && arg == NULL)) {
        T23_LOG_ERR(fn, "%s(%d), tuning is NULL\n", fn, __LINE__);
        return -1;
    }
    if (!(flags & T23F_NOSTATE) && isp->tuning_state != 2) {
        T23_LOG_ERR(fn, "%s(%d), ispdev->tuning_state is ISPDEV_STATE_RUN\n",
                    fn, __LINE__);
        return -1;
    }
    *out = isp;
    return 0;
}

static int t23_xfer(ISPDevice *isp, int num, int get, uint32_t cid,
                    uint32_t *word)
{
    T23TuningReq req;
    int ret;

    req.dir = get ? 1 : 0;
    req.cid = cid;
    req.value = *word;
    req.sensor = num;
    ret = ioctl(isp->tuning_fd, T23_VIDIOC_TUNING, &req);
    *word = req.value;
    return ret;
}

/* Set a value or pass a pointer the kernel copies from. */
static int t23_set(const char *fn, int num, uint32_t cid, uint32_t word,
                   unsigned flags)
{
    ISPDevice *isp;
    int ret = t23_prologue(fn, num, (const void *)(uintptr_t)word, flags,
                           &isp);

    if (ret)
        return ret;
    ret = t23_xfer(isp, num, 0, cid, &word);
    if (ret)
        T23_LOG_ERR(fn, "%s(%d),ioctl cid 0x%x failed!\n", fn, __LINE__, cid);
    return ret;
}

/* Get through a pointer the kernel copies to. */
static int t23_get_ptr(const char *fn, int num, uint32_t cid, void *ptr,
                       unsigned flags)
{
    ISPDevice *isp;
    uint32_t word = (uint32_t)(uintptr_t)ptr;
    int ret = t23_prologue(fn, num, ptr, flags, &isp);

    if (ret)
        return ret;
    ret = t23_xfer(isp, num, 1, cid, &word);
    if (ret)
        T23_LOG_ERR(fn, "%s(%d),ioctl cid 0x%x failed!\n", fn, __LINE__, cid);
    return ret;
}

/* Get an inline value.  The stock library stores the returned word even
 * when the ioctl failed (then it is the request's initial value). */
static int t23_get_word(const char *fn, int num, uint32_t cid, uint32_t *out,
                        uint32_t init, unsigned flags)
{
    ISPDevice *isp;
    uint32_t word = init;
    int ret = t23_prologue(fn, num, out, flags, &isp);

    if (ret)
        return ret;
    ret = t23_xfer(isp, num, 1, cid, &word);
    if (ret)
        T23_LOG_ERR(fn, "%s(%d),ioctl cid 0x%x failed!\n", fn, __LINE__, cid);
    if (out)
        *out = word;
    return ret;
}

/* Per-sensor G_CTRL/S_CTRL numbers; sensor 3 has none (0). */
static uint32_t t23_ctrl_ioctl(int num, int get)
{
    switch (num) {
    case 0:
        return get ? 0xc008561bU : 0xc008561cU;
    case 1:
        return get ? 0xc008561dU : 0xc008561eU;
    case 2:
        return get ? 0xc008561fU : 0xc0085620U;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------------
 * Template entry points
 * ---------------------------------------------------------------------- */

#define T23_MC_SET(name, cid, type, flags)                                 \
    int IMP_ISP_MultiCamera_Tuning_##name(int num, type v)                 \
    {                                                                      \
        return t23_set(__func__, num, cid, (uint32_t)(uintptr_t)v, flags); \
    }

#define T23_MC_GET_PTR(name, cid, flags)                                   \
    int IMP_ISP_MultiCamera_Tuning_##name(int num, void *p)                \
    {                                                                      \
        return t23_get_ptr(__func__, num, cid, p, flags);                  \
    }

#define T23_MC_GET_WORD(name, cid, init, flags)                            \
    int IMP_ISP_MultiCamera_Tuning_##name(int num, uint32_t *p)            \
    {                                                                      \
        return t23_get_word(__func__, num, cid, p, init, flags);           \
    }

/* AE */
T23_MC_SET(SetAeComp, T23_CID_AE_COMP, int, 0)
T23_MC_GET_WORD(GetAeComp, T23_CID_AE_COMP, 0, T23F_NULLCHK)
T23_MC_GET_WORD(GetAeLuma, T23_CID_AE_LUMA, 0, T23F_NULLCHK)
T23_MC_SET(SetAeFreeze, T23_CID_AE_FREEZE, int, 0)
T23_MC_SET(SetExpr, T23_CID_EXPR, void *, T23F_NULLCHK)
T23_MC_GET_PTR(GetExpr, T23_CID_EXPR, T23F_NULLCHK)
T23_MC_SET(SetAeWeight, T23_CID_AE_WEIGHT, void *, T23F_NULLCHK)
T23_MC_GET_PTR(GetAeWeight, T23_CID_AE_WEIGHT, T23F_NULLCHK)
T23_MC_SET(AE_SetROI, T23_CID_AE_ROI, void *, T23F_NULLCHK)
T23_MC_GET_PTR(AE_GetROI, T23_CID_AE_ROI, T23F_NULLCHK)
T23_MC_SET(SetAeHist, T23_CID_AE_HIST, void *, T23F_NULLCHK)
T23_MC_GET_PTR(GetAeHist, T23_CID_AE_HIST, T23F_NULLCHK)
T23_MC_GET_PTR(GetAeHist_Origin, T23_CID_AE_HIST_ORIGIN, T23F_NULLCHK)
T23_MC_SET(SetAeMin, T23_CID_AE_MIN, void *, T23F_NULLCHK)
T23_MC_GET_PTR(GetAeMin, T23_CID_AE_MIN, T23F_NULLCHK)
T23_MC_SET(SetAe_IT_MAX, T23_CID_AE_IT_MAX, uint32_t, 0)
T23_MC_GET_WORD(GetAE_IT_MAX, T23_CID_AE_IT_MAX, 0, 0)
T23_MC_GET_PTR(GetAeZone, T23_CID_AE_ZONE, T23F_NULLCHK)
T23_MC_GET_PTR(GetAeState, T23_CID_AE_STATE, T23F_NULLCHK)
T23_MC_SET(SetAeTargetList, T23_CID_AE_TARGET_LIST, void *, 0)
T23_MC_GET_PTR(GetAeTargetList, T23_CID_AE_TARGET_LIST, 0)
T23_MC_SET(SetMaxAgain, T23_CID_MAX_AGAIN, uint32_t, 0)
T23_MC_GET_WORD(GetMaxAgain, T23_CID_MAX_AGAIN, 0, T23F_NULLCHK)
T23_MC_SET(SetMaxDgain, T23_CID_MAX_DGAIN, uint32_t, 0)
T23_MC_GET_WORD(GetMaxDgain, T23_CID_MAX_DGAIN, 0, T23F_NULLCHK)
T23_MC_SET(SetHiLightDepress, T23_CID_HILIGHT_DEPRESS, uint32_t, 0)
T23_MC_GET_WORD(GetHiLightDepress, T23_CID_HILIGHT_DEPRESS, 0, T23F_NULLCHK)
T23_MC_SET(SetBacklightComp, T23_CID_BACKLIGHT_COMP, uint32_t, 0)
T23_MC_GET_WORD(GetBacklightComp, T23_CID_BACKLIGHT_COMP, 0, T23F_NULLCHK)

/* AWB */
T23_MC_SET(SetWB, T23_CID_WB_ATTR, void *, T23F_NULLCHK)
T23_MC_GET_PTR(GetWB, T23_CID_WB_ATTR, T23F_NULLCHK)
T23_MC_SET(SetAwbClust, T23_CID_AWB_CLUSTER, void *, T23F_NULLCHK)
T23_MC_GET_PTR(GetAwbClust, T23_CID_AWB_CLUSTER, T23F_NULLCHK)
T23_MC_SET(SetAwbCtTrend, T23_CID_AWB_CT_TREND, void *, 0)
T23_MC_GET_PTR(GetAwbCtTrend, T23_CID_AWB_CT_TREND, 0)
T23_MC_SET(Awb_SetRgbCoefft, T23_CID_AWB_RGB_COEFFT, void *, T23F_NULLCHK)
T23_MC_SET(SetAwbWeight, T23_CID_AWB_WEIGHT, void *, T23F_NULLCHK)
T23_MC_GET_PTR(GetAwbWeight, T23_CID_AWB_WEIGHT, T23F_NULLCHK)
T23_MC_GET_PTR(GetAwbZone, T23_CID_AWB_ZONE, T23F_NULLCHK)
T23_MC_SET(SetAwbZoneWeight, T23_CID_AWB_ZONE_WEIGHT, void *, T23F_NULLCHK)
T23_MC_GET_PTR(GetAwbZoneWeight, T23_CID_AWB_ZONE_WEIGHT, T23F_NULLCHK)
T23_MC_SET(SetWB_ALGO, T23_CID_WB_ALGO, int, 0)
T23_MC_SET(SetAwbHist, T23_CID_AWB_HIST, void *, T23F_NULLCHK)
T23_MC_GET_PTR(GetAwbHist, T23_CID_AWB_HIST, T23F_NULLCHK)
T23_MC_SET(SetAwbCt, T23_CID_AWB_CT, void *, 0)
T23_MC_GET_PTR(GetAWBCt, T23_CID_AWB_CT, T23F_NULLCHK)

/* AF */
T23_MC_GET_PTR(GetAFMetrices, T23_CID_AF_METRICES, 0)
T23_MC_SET(SetAfHist, T23_CID_AF_HIST, void *, T23F_NULLCHK)
T23_MC_GET_PTR(GetAfHist, T23_CID_AF_HIST, T23F_NULLCHK)
T23_MC_SET(SetAfWeight, T23_CID_AF_WEIGHT, void *, T23F_NULLCHK)
T23_MC_GET_PTR(GetAfWeight, T23_CID_AF_WEIGHT, T23F_NULLCHK)
T23_MC_GET_PTR(GetAfZone, T23_CID_AF_ZONE, T23F_NULLCHK)

/* Image pipeline blocks */
T23_MC_SET(SetGamma, T23_CID_GAMMA, void *, 0)
T23_MC_GET_PTR(GetGamma, T23_CID_GAMMA, 0)
T23_MC_SET(SetBcshHue, T23_CID_BCSH_HUE, unsigned char, 0)
T23_MC_SET(SetTemperStrength, T23_CID_TEMPER_STRENGTH, uint32_t, 0)
T23_MC_SET(SetSinterStrength, T23_CID_SINTER_STRENGTH, uint32_t, 0)
T23_MC_SET(SetDPC_Strength, T23_CID_DPC_STRENGTH, uint32_t, 0)
T23_MC_GET_WORD(GetDPC_Strength, T23_CID_DPC_STRENGTH, 0, 0)
T23_MC_SET(SetDRC_Strength, T23_CID_DRC_STRENGTH, uint32_t, 0)
T23_MC_GET_WORD(GetDRC_Strength, T23_CID_DRC_STRENGTH, 0, 0)
T23_MC_SET(EnableDRC, T23_CID_ENABLE_DRC, int, 0)
T23_MC_SET(EnableDefog, T23_CID_ENABLE_DEFOG, int, 0)
T23_MC_SET(SetDefog_Strength, T23_CID_DEFOG_STRENGTH, void *, T23F_NULLCHK)
T23_MC_GET_PTR(GetDefog_Strength, T23_CID_DEFOG_STRENGTH, T23F_NULLCHK)
T23_MC_SET(SetCsc_Attr, T23_CID_CSC_ATTR, void *, T23F_NULLCHK)
T23_MC_GET_PTR(GetCsc_Attr, T23_CID_CSC_ATTR, T23F_NULLCHK)
T23_MC_GET_PTR(GetBlcAttr, T23_CID_BLC_ATTR, T23F_NULLCHK)
T23_MC_SET(SetModuleControl, T23_CID_MODULE_CONTROL, void *, T23F_NULLCHK)
T23_MC_GET_PTR(GetModuleControl, T23_CID_MODULE_CONTROL, T23F_NULLCHK)
T23_MC_GET_WORD(GetHVFlip, T23_CID_HV_FLIP, 0, 0)
T23_MC_GET_PTR(GetMask, T23_CID_MASK, T23F_NULLCHK)

/* Geometry, sensor, bins */
T23_MC_SET(SetFrontCrop, T23_CID_FRONT_CROP, void *, T23F_NULLCHK)
T23_MC_GET_PTR(GetFrontCrop, T23_CID_FRONT_CROP, T23F_NULLCHK)
T23_MC_SET(SetAutoZoom, T23_CID_AUTO_ZOOM, void *, T23F_NULLCHK)
T23_MC_GET_PTR(GetAutoZoom, T23_CID_AUTO_ZOOM, T23F_NULLCHK)
T23_MC_SET(SetScalerLv, T23_CID_SCALER_LV, void *, 0)
T23_MC_GET_PTR(GetSensorAttr, T23_CID_SENSOR_ATTR, T23F_NULLCHK)
T23_MC_SET(SwitchBin, T23_CID_SWITCH_BIN, void *, 0)

/* ------------------------------------------------------------------------
 * Hand-written entry points
 * ---------------------------------------------------------------------- */

/* Awb_GetRgbCoefft: plain pointer get without the ioctl error log. */
int IMP_ISP_MultiCamera_Tuning_Awb_GetRgbCoefft(int num, void *p)
{
    ISPDevice *isp;
    uint32_t word = (uint32_t)(uintptr_t)p;
    int ret = t23_prologue(__func__, num, p, T23F_NULLCHK, &isp);

    return ret ? ret : t23_xfer(isp, num, 1, T23_CID_AWB_RGB_COEFFT, &word);
}

/* AwbSync: sensors 0 and 2+ return 0 without a request; only sensor 1 is
 * synchronised (to sensor 0) by the kernel. */
int IMP_ISP_MultiCamera_Tuning_AwbSync(int num, void *p)
{
    ISPDevice *isp;
    uint32_t word = (uint32_t)(uintptr_t)p;
    int ret = t23_prologue(__func__, num, p, 0, &isp);

    if (ret)
        return ret;
    if (num != 1)
        return 0;
    ret = t23_xfer(isp, num, 0, T23_CID_AWB_SYNC, &word);
    if (ret)
        T23_LOG_ERR(__func__, "%s(%d),ioctl failed!\n", __func__, __LINE__);
    return ret;
}

/* GetBcshHue: the one inline read that stores a single byte. */
int IMP_ISP_MultiCamera_Tuning_GetBcshHue(int num, unsigned char *hue)
{
    ISPDevice *isp;
    uint32_t word = 0;
    int ret = t23_prologue(__func__, num, hue, T23F_NULLCHK, &isp);

    if (ret)
        return ret;
    ret = t23_xfer(isp, num, 1, T23_CID_BCSH_HUE, &word);
    if (ret)
        T23_LOG_ERR(__func__, "%s(%d),ioctl failed!\n", __func__, __LINE__);
    *hue = (uint8_t)word;
    return ret;
}

/* GetTotalGain: no tuning state check, stores the value only on success. */
int IMP_ISP_MultiCamera_Tuning_GetTotalGain(int num, uint32_t *gain)
{
    ISPDevice *isp;
    uint32_t word = 0;
    int ret = t23_prologue(__func__, num, gain, T23F_NOSTATE, &isp);

    if (ret)
        return ret;
    ret = t23_xfer(isp, num, 1, T23_CID_TOTAL_GAIN, &word);
    if (ret == 0 && gain)
        *gain = word;
    return ret;
}

/* WB statistics come back packed (r << 16 | b) and land in IMPISPWB's gain
 * fields; mode is left alone.  Stored only on success. */
static int t23_get_wb_statis(const char *fn, int num, uint32_t cid, void *p)
{
    ISPDevice *isp;
    uint32_t word = 0;
    int ret = t23_prologue(fn, num, p, T23F_NULLCHK, &isp);

    if (ret)
        return ret;
    ret = t23_xfer(isp, num, 1, cid, &word);
    if (ret == 0) {
        T23WB *wb = p;

        wb->rgain = (uint16_t)(word >> 16);
        wb->bgain = (uint16_t)word;
    }
    return ret;
}

int IMP_ISP_MultiCamera_Tuning_GetWB_Statis(int num, void *wb)
{
    return t23_get_wb_statis(__func__, num, T23_CID_WB_STATIS, wb);
}

int IMP_ISP_MultiCamera_Tuning_GetWB_GOL_Statis(int num, void *wb)
{
    return t23_get_wb_statis(__func__, num, T23_CID_WB_GOL_STATIS, wb);
}

/* GetEVAttr: the kernel fills a 24-byte block that is copied out even when
 * the request failed. */
int IMP_ISP_MultiCamera_Tuning_GetEVAttr(int num, void *p)
{
    ISPDevice *isp;
    T23EVAttr ev;
    uint32_t word = (uint32_t)(uintptr_t)&ev;
    int ret = t23_prologue(__func__, num, p, T23F_NULLCHK, &isp);

    if (ret)
        return ret;
    memset(&ev, 0, sizeof(ev));
    ret = t23_xfer(isp, num, 1, T23_CID_EV_ATTR, &word);
    if (ret)
        T23_LOG_ERR(__func__, "%s(%d),ioctl failed!\n", __func__, __LINE__);
    memcpy(p, &ev, sizeof(ev));
    return ret;
}

/* WaitFrame: timeout in, frame count (u64 at +8) out. */
int IMP_ISP_MultiCamera_Tuning_WaitFrame(int num, void *p)
{
    T23WaitFrameAttr *attr = p;
    ISPDevice *isp;
    T23WaitFrameAttr k;
    uint32_t word = (uint32_t)(uintptr_t)&k;
    int ret = t23_prologue(__func__, num, p, T23F_NULLCHK, &isp);

    if (ret)
        return ret;
    memset(&k, 0, sizeof(k));
    k.timeout = attr->timeout;
    ret = t23_xfer(isp, num, 1, T23_CID_WAIT_FRAME, &word);
    if (ret)
        T23_LOG_ERR(__func__, "%s(%d),ioctl failed!\n", __func__, __LINE__);
    attr->cnt = k.cnt;
    return ret;
}

/* SetAeAttr/GetAeAttr: the public nine-word attribute is scattered into the
 * kernel's 0xb0-byte AE manual block.  The stock library leaves the rest of
 * that block as uninitialised stack; it is zeroed here. */
#define T23_AE_BLOCK_SIZE 0xb0

static const uint8_t t23_ae_attr_offset[9] = {
    0x00, 0x3c, 0x0c, 0x40, 0x04, 0x98, 0x08, 0x44, 0x10,
};

int IMP_ISP_MultiCamera_Tuning_SetAeAttr(int num, void *p)
{
    const uint32_t *in = p;
    ISPDevice *isp;
    uint32_t k[T23_AE_BLOCK_SIZE / 4];
    uint32_t word = (uint32_t)(uintptr_t)k;
    int ret = t23_prologue(__func__, num, p, T23F_NULLCHK, &isp);

    if (ret)
        return ret;
    memset(k, 0, sizeof(k));
    for (unsigned i = 0; i < 9; i++)
        k[t23_ae_attr_offset[i] / 4] = in[i];
    ret = t23_xfer(isp, num, 0, T23_CID_AE_ATTR, &word);
    if (ret)
        T23_LOG_ERR(__func__, "%s(%d),ioctl failed!\n", __func__, __LINE__);
    return ret;
}

int IMP_ISP_MultiCamera_Tuning_GetAeAttr(int num, void *p)
{
    uint32_t *out = p;
    ISPDevice *isp;
    uint32_t k[T23_AE_BLOCK_SIZE / 4];
    uint32_t word = (uint32_t)(uintptr_t)k;
    int ret = t23_prologue(__func__, num, p, T23F_NULLCHK, &isp);

    if (ret)
        return ret;
    memset(k, 0, sizeof(k));
    ret = t23_xfer(isp, num, 1, T23_CID_AE_ATTR, &word);
    if (ret)
        T23_LOG_ERR(__func__, "%s(%d),ioctl failed!\n", __func__, __LINE__);
    for (unsigned i = 0; i < 9; i++)
        out[i] = k[t23_ae_attr_offset[i] / 4];
    return ret;
}

/* CCM: float matrix <-> 14-bit sign/magnitude Q10 words.  Like the stock
 * library, a negative coefficient below -1e-5 is negated in the caller's
 * matrix while it is converted (the vendor writes |x| back in place). */
int IMP_ISP_MultiCamera_Tuning_SetCCMAttr(int num, void *p)
{
    T23CCMAttr *in = p;
    ISPDevice *isp;
    T23KernelCCM k;
    uint32_t word = (uint32_t)(uintptr_t)&k;
    int ret = t23_prologue(__func__, num, p, T23F_NULLCHK, &isp);

    if (ret)
        return ret;
    memset(&k, 0, sizeof(k));
    for (unsigned i = 0; i < 9; i++) {
        float v = in->ColorMatrix[i];

        if ((double)v < -1e-5) {
            v = -v;
            in->ColorMatrix[i] = v;
            k.coef[i] = ((uint32_t)-(int32_t)(v * 1024.0f) & 0x1fffu) |
                        0x2000u;
        } else {
            k.coef[i] = (uint32_t)(int32_t)(v * 1024.0f) & 0x1fffu;
        }
    }
    k.manual_en = (int8_t)in->ManualEn;
    k.sat_en = (int8_t)in->SatEn;
    ret = t23_xfer(isp, num, 0, T23_CID_CCM_ATTR, &word);
    if (ret)
        T23_LOG_ERR(__func__, "%s(%d),ioctl failed!\n", __func__, __LINE__);
    return ret;
}

int IMP_ISP_MultiCamera_Tuning_GetCCMAttr(int num, void *p)
{
    T23CCMAttr *out = p;
    ISPDevice *isp;
    T23KernelCCM k;
    uint32_t word = (uint32_t)(uintptr_t)&k;
    int ret = t23_prologue(__func__, num, p, T23F_NULLCHK, &isp);

    if (ret)
        return ret;
    memset(&k, 0, sizeof(k));
    ret = t23_xfer(isp, num, 1, T23_CID_CCM_ATTR, &word);
    for (unsigned i = 0; i < 9; i++) {
        uint32_t w = k.coef[i];

        if (w & 0x2000u)
            out->ColorMatrix[i] =
                -(float)(int32_t)(-w & 0x1fffu) * 0.0009765625f;
        else
            out->ColorMatrix[i] = (float)(int32_t)w * 0.0009765625f;
    }
    out->ManualEn = k.manual_en;
    out->SatEn = k.sat_en;
    if (ret)
        T23_LOG_ERR(__func__, "%s(%d),ioctl failed!\n", __func__, __LINE__);
    return ret;
}

/* --- BCSH through the per-sensor V4L2 controls -------------------------- */

static int t23_ctrl_set(int num, uint32_t id, int32_t value)
{
    uint32_t nr = t23_ctrl_ioctl(num, 0);
    T23Ctrl ctrl = { id, value };

    return nr ? ioctl(gISP->tuning_fd, nr, &ctrl) : 0;
}

static int t23_set_bcs(const char *fn, int num, uint32_t id, uint8_t value,
                       uint8_t *cache)
{
    ISPDevice *isp;
    int ret = t23_prologue(fn, num, NULL, 0, &isp);

    if (ret)
        return ret;
    (void)isp;
    ret = t23_ctrl_set(num, id, value);
    if (ret < 0) {
        T23_LOG_ERR(fn, "%s(%d), set VIDIOC_S_CTRL failed\n", fn, __LINE__);
        return ret;
    }
    cache[num] = value;
    return ret;
}

/* Brightness/saturation readback: G_CTRL with -1 preset; sensor 3 has no
 * control and reads 255. */
static int t23_get_bcs(const char *fn, int num, uint32_t id, uint8_t *out,
                       uint8_t *cache)
{
    ISPDevice *isp;
    uint32_t nr;
    int ret = t23_prologue(fn, num, out, T23F_NULLCHK, &isp);
    T23Ctrl ctrl = { id, -1 };
    uint8_t value = 0xff;

    if (ret)
        return ret;
    nr = t23_ctrl_ioctl(num, 1);
    if (nr) {
        ret = ioctl(isp->tuning_fd, nr, &ctrl);
        if (ret) {
            T23_LOG_ERR(fn, "%s(%d), get VIDIOC_G_CTRL failed\n", fn,
                        __LINE__);
            return ret;
        }
        value = (uint8_t)ctrl.value;
    }
    *out = value;
    cache[num] = value;
    return 0;
}

int IMP_ISP_MultiCamera_Tuning_SetBrightness(int num, unsigned char v)
{
    return t23_set_bcs(__func__, num, T23_CID_BRIGHTNESS, v, t23_brightness);
}

int IMP_ISP_MultiCamera_Tuning_GetBrightness(int num, unsigned char *v)
{
    return t23_get_bcs(__func__, num, T23_CID_BRIGHTNESS, v, t23_brightness);
}

int IMP_ISP_MultiCamera_Tuning_SetSaturation(int num, unsigned char v)
{
    return t23_set_bcs(__func__, num, T23_CID_SATURATION, v, t23_saturation);
}

int IMP_ISP_MultiCamera_Tuning_GetSaturation(int num, unsigned char *v)
{
    return t23_get_bcs(__func__, num, T23_CID_SATURATION, v, t23_saturation);
}

/*
 * Contrast and sharpness keep a "custom" value per sensor and a global
 * mode: mode 2 is the normal API call (store, and apply unless the global
 * mode is 1), mode 1 applies the given value and sets the global mode to 1,
 * mode 0 re-applies the stored custom value and returns to mode 2.
 */
static int t23_set_cs_internal(const char *fn, int num, uint32_t id,
                               uint32_t value, int32_t mode,
                               uint8_t *custom, uint8_t *cache)
{
    ISPDevice *isp = gISP;
    uint8_t v = (uint8_t)value;
    int ret;

    if (isp == NULL || isp->tuning == NULL) {
        T23_LOG_ERR(fn, "%s(%d), tuning is NULL\n", fn, __LINE__);
        return -1;
    }
    if (isp->tuning_state != 2) {
        T23_LOG_ERR(fn, "%s(%d), ispdev->tuning_state is ISPDEV_STATE_RUN\n",
                    fn, __LINE__);
        return -1;
    }
    if ((unsigned)num >= T23_VI_MAX)
        return T23_ERR_VI_OVERFLOW;
    if (mode == 2) {
        custom[num] = v;
        if (t23_global_mode == 1)
            return 0;
    } else if (mode == 1) {
        t23_global_mode = 1;
    } else if (mode == 0) {
        v = custom[num];
        t23_global_mode = 2;
    } else {
        T23_LOG_ERR(fn, "%s(%d), We do not support this mode\n", fn,
                    __LINE__);
        return -1;
    }
    ret = t23_ctrl_set(num, id, v);
    if (ret < 0) {
        T23_LOG_ERR(fn, "%s(%d), set VIDIOC_S_CTRL failed\n", fn, __LINE__);
        return ret;
    }
    cache[num] = v;
    return ret;
}

int IMP_ISP_MultiCamera_Tuning_SetContrast_internal(int num, uint32_t v,
                                                    int32_t mode)
{
    return t23_set_cs_internal(__func__, num, T23_CID_CONTRAST, v, mode,
                               t23_custom_contrast, t23_contrast_cache);
}

int IMP_ISP_MultiCamera_Tuning_SetSharpness_internal(int num, uint32_t v,
                                                     int32_t mode)
{
    return t23_set_cs_internal(__func__, num, T23_CID_SHARPNESS, v, mode,
                               t23_custom_sharpness, t23_sharpness_cache);
}

int IMP_ISP_MultiCamera_Tuning_SetContrast(int num, unsigned char v)
{
    if ((unsigned)num >= T23_VI_MAX) {
        T23_LOG_ERR(__func__, "MSG: Number of sensors overflow!\n");
        return T23_ERR_VI_OVERFLOW;
    }
    return IMP_ISP_MultiCamera_Tuning_SetContrast_internal(num, v, 2);
}

int IMP_ISP_MultiCamera_Tuning_SetSharpness(int num, unsigned char v)
{
    if ((unsigned)num >= T23_VI_MAX) {
        T23_LOG_ERR(__func__, "MSG: Number of sensors overflow!\n");
        return T23_ERR_VI_OVERFLOW;
    }
    return IMP_ISP_MultiCamera_Tuning_SetSharpness_internal(num, v, 2);
}

/* Contrast/sharpness readback is the stored custom value, not the kernel. */
static int t23_get_cs(const char *fn, int num, uint8_t *out,
                      const uint8_t *custom, uint8_t *cache)
{
    ISPDevice *isp;
    int ret = t23_prologue(fn, num, out, T23F_NULLCHK, &isp);

    if (ret)
        return ret;
    *out = custom[num];
    cache[num] = custom[num];
    return 0;
}

int IMP_ISP_MultiCamera_Tuning_GetContrast(int num, unsigned char *v)
{
    return t23_get_cs(__func__, num, v, t23_custom_contrast,
                      t23_contrast_cache);
}

int IMP_ISP_MultiCamera_Tuning_GetSharpness(int num, unsigned char *v)
{
    return t23_get_cs(__func__, num, v, t23_custom_sharpness,
                      t23_sharpness_cache);
}

uint8_t openimp_t23_isp_custom_contrast(void)
{
    return t23_custom_contrast[0];
}

/* Anti-flicker: per-sensor V4L2 power-line control.  An unchanged value
 * returns 0 without a request. */
int IMP_ISP_MultiCamera_Tuning_SetAntiFlickerAttr(int num, int attr)
{
    ISPDevice *isp;
    int ret = t23_prologue(__func__, num, NULL, 0, &isp);

    if (ret)
        return ret;
    if (t23_antiflicker[num] == attr)
        return 0;
    if (attr < 0 || attr > 2) {
        T23_LOG_ERR(__func__, "%s(%d), not support this mode\n", __func__,
                    __LINE__);
        return -1;
    }
    ret = t23_ctrl_set(num, T23_CID_POWER_LINE_FREQUENCY, attr);
    if (ret) {
        T23_LOG_ERR(__func__, "%s(%d), set VIDIOC_S_CTRL failed\n", __func__,
                    __LINE__);
        return ret;
    }
    t23_antiflicker[num] = attr;
    return 0;
}

int IMP_ISP_MultiCamera_Tuning_GetAntiFlickerAttr(int num, int *pattr)
{
    ISPDevice *isp;
    uint32_t nr;
    T23Ctrl ctrl = { T23_CID_POWER_LINE_FREQUENCY, -1 };
    int ret = t23_prologue(__func__, num, pattr, T23F_NULLCHK, &isp);

    if (ret)
        return ret;
    nr = t23_ctrl_ioctl(num, 1);
    if (!nr)
        return -1;
    ret = ioctl(isp->tuning_fd, nr, &ctrl);
    if (ret) {
        T23_LOG_ERR(__func__, "%s(%d), get VIDIOC_G_CTRL failed\n", __func__,
                    __LINE__);
        return ret;
    }
    if (ctrl.value < 0 || ctrl.value > 2) {
        T23_LOG_ERR(__func__, "%s(%d), not support this mode\n", __func__,
                    __LINE__);
        return -1;
    }
    t23_antiflicker[num] = ctrl.value;
    *pattr = ctrl.value;
    return 0;
}

/* Flips.  ISP flips use V4L2 HFLIP/VFLIP ids inside the tuning request,
 * sensor flips their own CIDs; every set restarts the frame-source change
 * wait, also after a failed request. */
static int t23_set_flip(const char *fn, int num, uint32_t cid, int mode)
{
    ISPDevice *isp;
    uint32_t word = (uint32_t)mode;
    int ret = t23_prologue(fn, num, NULL, 0, &isp);

    if (ret)
        return ret;
    ret = t23_xfer(isp, num, 0, cid, &word);
    if (ret < 0)
        T23_LOG_ERR(fn, "%s(%d),ioctl cid 0x%x failed!\n", fn, __LINE__, cid);
    set_framesource_changewait_cnt();
    return ret;
}

int IMP_ISP_MultiCamera_Tuning_SetISPHflip(int num, int mode)
{
    return t23_set_flip(__func__, num, T23_CID_HFLIP, mode);
}

int IMP_ISP_MultiCamera_Tuning_SetISPVflip(int num, int mode)
{
    return t23_set_flip(__func__, num, T23_CID_VFLIP, mode);
}

int IMP_ISP_MultiCamera_Tuning_SetSensorHflip(int num, int mode)
{
    return t23_set_flip(__func__, num, T23_CID_SENSOR_HFLIP, mode);
}

int IMP_ISP_MultiCamera_Tuning_SetSensorVflip(int num, int mode)
{
    return t23_set_flip(__func__, num, T23_CID_SENSOR_VFLIP, mode);
}

/* ISP flip reads preset the value to -1, sensor flip reads to 0. */
int IMP_ISP_MultiCamera_Tuning_GetISPHflip(int num, uint32_t *mode)
{
    return t23_get_word(__func__, num, T23_CID_HFLIP, mode, 0xffffffffu,
                        T23F_NULLCHK);
}

int IMP_ISP_MultiCamera_Tuning_GetISPVflip(int num, uint32_t *mode)
{
    return t23_get_word(__func__, num, T23_CID_VFLIP, mode, 0xffffffffu,
                        T23F_NULLCHK);
}

int IMP_ISP_MultiCamera_Tuning_GetSensorHflip(int num, uint32_t *mode)
{
    return t23_get_word(__func__, num, T23_CID_SENSOR_HFLIP, mode, 0,
                        T23F_NULLCHK);
}

int IMP_ISP_MultiCamera_Tuning_GetSensorVflip(int num, uint32_t *mode)
{
    return t23_get_word(__func__, num, T23_CID_SENSOR_VFLIP, mode, 0,
                        T23F_NULLCHK);
}

/* HV flip: modes 0..3 in one request, no change-wait restart. */
int IMP_ISP_MultiCamera_Tuning_SetHVFLIP(int num, int hvflip)
{
    ISPDevice *isp;
    uint32_t word = (uint32_t)hvflip;
    int ret = t23_prologue(__func__, num, NULL, 0, &isp);

    if (ret)
        return ret;
    if ((uint32_t)hvflip >= 4) {
        T23_LOG_ERR(__func__, "FLIP Can not support this Mode!\n");
        return -1;
    }
    ret = t23_xfer(isp, num, 0, T23_CID_HV_FLIP, &word);
    if (ret)
        T23_LOG_ERR(__func__, "%s(%d),ioctl IMAGE_TUNING_CID_HV_FLIP!\n",
                    __func__, __LINE__);
    return ret;
}

/* Running mode / custom mode: serialised with the move-state calls, no
 * tuning state check; day/night bookkeeping for sensor 0. */
int IMP_ISP_MultiCamera_Tuning_SetISPRunningMode(int num, int mode)
{
    ISPDevice *isp;
    uint32_t word = (uint32_t)mode;
    int ret = t23_prologue(__func__, num, NULL, T23F_NOSTATE, &isp);

    if (ret)
        return ret;
    pthread_mutex_lock(&t23_running_mutex);
    ret = t23_xfer(isp, num, 0, T23_CID_RUNNING_MODE, &word);
    if (num == 0)
        t23_is_isp_day = mode == 0;
    set_framesource_changewait_cnt();
    pthread_mutex_unlock(&t23_running_mutex);
    return ret;
}

int IMP_ISP_MultiCamera_Tuning_SetISPCustomMode(int num, int mode)
{
    ISPDevice *isp;
    uint32_t word = (uint32_t)mode;
    int ret = t23_prologue(__func__, num, NULL, T23F_NOSTATE, &isp);

    if (ret)
        return ret;
    pthread_mutex_lock(&t23_running_mutex);
    ret = t23_xfer(isp, num, 0, T23_CID_CUSTOM_MODE, &word);
    set_framesource_changewait_cnt();
    pthread_mutex_unlock(&t23_running_mutex);
    return ret;
}

/* Mode reads store the value only on success. */
static int t23_get_mode(const char *fn, int num, uint32_t cid, uint32_t *out)
{
    ISPDevice *isp;
    uint32_t word = 0;
    int ret = t23_prologue(fn, num, out, T23F_NULLCHK, &isp);

    if (ret)
        return ret;
    ret = t23_xfer(isp, num, 1, cid, &word);
    if (ret == 0)
        *out = word;
    return ret;
}

int IMP_ISP_MultiCamera_Tuning_GetISPRunningMode(int num, uint32_t *mode)
{
    return t23_get_mode(__func__, num, T23_CID_RUNNING_MODE, mode);
}

int IMP_ISP_MultiCamera_Tuning_GetISPCustomMode(int num, uint32_t *mode)
{
    return t23_get_mode(__func__, num, T23_CID_CUSTOM_MODE, mode);
}

int openimp_t23_isp_is_day(void)
{
    return t23_is_isp_day;
}

/* Sensor frame rate: packed (num << 16 | den), no tuning state check; the
 * result also retimes the frame source.  The stock library additionally
 * notifies a registered custom AE algorithm; OpenIMP's custom AE hook does
 * not take that callback. */
int IMP_ISP_MultiCamera_Tuning_SetSensorFPS(int num, uint32_t fps_num,
                                            uint32_t fps_den)
{
    ISPDevice *isp;
    uint32_t word = (fps_num << 16) | fps_den;
    int ret = t23_prologue(__func__, num, NULL, T23F_NOSTATE, &isp);

    if (ret)
        return ret;
    ret = t23_xfer(isp, num, 0, T23_CID_SENSOR_FPS, &word);
    if (ret) {
        T23_LOG_ERR(__func__, "%s(%d),ioctl failed!\n", __func__, __LINE__);
        return ret;
    }
    t23_fps_num = fps_num;
    t23_fps_den = fps_den;
    set_framesource_fps((int32_t)fps_num, (int32_t)fps_den);
    return 0;
}

int IMP_ISP_MultiCamera_Tuning_GetSensorFPS(int num, uint32_t *fps_num,
                                            uint32_t *fps_den)
{
    ISPDevice *isp;
    uint32_t word = 0;
    int ret = t23_prologue(__func__, num, NULL, T23F_NOSTATE, &isp);

    if (ret)
        return ret;
    ret = t23_xfer(isp, num, 1, T23_CID_SENSOR_FPS, &word);
    if (ret) {
        T23_LOG_ERR(__func__, "%s(%d),ioctl failed!\n", __func__, __LINE__);
        return ret;
    }
    t23_fps_num = word >> 16;
    t23_fps_den = word & 0xffff;
    set_framesource_fps((int32_t)t23_fps_num, (int32_t)t23_fps_den);
    if (fps_num)
        *fps_num = t23_fps_num;
    if (fps_den)
        *fps_den = t23_fps_den;
    return 0;
}

/* --- Colour conversion used by masks and draw blocks ---------------------- */

/* trunc.w.d plus the compiler's unsigned fix-up, as the stock library. */
static uint8_t t23_to_u8(double v)
{
    if (v >= 2147483648.0)
        return (uint8_t)((uint32_t)(int32_t)(v - 2147483648.0) | 0x80000000u);
    return (uint8_t)(int32_t)v;
}

/* IMP_ISP_Tuning_DumpMask of the stock library: RGB -> YUV (BT.601 full range) in place. */
static void t23_rgb_to_yuv(uint8_t *c)
{
    double r = c[0], g = c[1], b = c[2];

    c[0] = t23_to_u8(r * 0.299 + g * 0.587 + b * 0.114);
    c[1] = t23_to_u8(r * -0.168736 - g * 0.331264 + b * 0.5 + 128.0);
    c[2] = t23_to_u8(r * 0.5 - g * 0.418688 - b * 0.081312 + 128.0);
}

/* SetMask: an RGB mask (type 0) is converted to YUV in the caller's struct
 * first, as the stock library does. */
int IMP_ISP_MultiCamera_Tuning_SetMask(int num, void *p)
{
    T23MaskAttr *mask = p;
    ISPDevice *isp;
    uint32_t word = (uint32_t)(uintptr_t)p;
    int ret = t23_prologue(__func__, num, p, T23F_NULLCHK, &isp);

    if (ret)
        return ret;
    if (mask->mask_type == 0) {
        for (unsigned c = 0; c < 3; c++)
            for (unsigned b = 0; b < 4; b++)
                t23_rgb_to_yuv(mask->chn[c][b].mask_value);
    }
    ret = t23_xfer(isp, num, 0, T23_CID_MASK, &word);
    if (ret)
        T23_LOG_ERR(__func__, "%s(%d),ioctl failed!\n", __func__, __LINE__);
    return ret;
}

/* Mask, draw and ISP-OSD blocks: no NULL check, -4090 on ioctl failure. */
static int t23_block_xfer(const char *fn, int num, int get, uint32_t cid,
                          void *p, ISPDevice *isp)
{
    uint32_t word = (uint32_t)(uintptr_t)p;

    if (t23_xfer(isp, num, get, cid, &word)) {
        T23_LOG_ERR(fn, "MSG: ioctl cid 0x%x failed!\n", cid);
        return T23_ERR_IOCTL;
    }
    return 0;
}

/* The stock library caches the block at index chx * pinum (sic). */
int IMP_ISP_MultiCamera_Tuning_SetMaskBlock(int num, void *p)
{
    T23MaskBlockAttr *attr = p;
    ISPDevice *isp;
    unsigned idx;
    int ret = t23_prologue(__func__, num, NULL, 0, &isp);

    if (ret)
        return ret;
    if (attr->mask_en && attr->mask_type == 0)
        t23_rgb_to_yuv(attr->mask_value);
    idx = (unsigned)attr->chx * attr->pinum;
    if (idx < sizeof(t23_mask_block) / sizeof(t23_mask_block[0]))
        t23_mask_block[idx] = *attr;
    return t23_block_xfer(__func__, num, 0, T23_CID_MASK_BLOCK, p, isp);
}

int IMP_ISP_MultiCamera_Tuning_GetMaskBlock(int num, void *p)
{
    T23MaskBlockAttr *attr = p;
    ISPDevice *isp;
    unsigned idx;
    int ret = t23_prologue(__func__, num, NULL, 0, &isp);

    if (ret)
        return ret;
    ret = t23_block_xfer(__func__, num, 1, T23_CID_MASK_BLOCK, p, isp);
    if (ret)
        return ret;
    idx = (unsigned)attr->chx * attr->pinum;
    if (idx < sizeof(t23_mask_block) / sizeof(t23_mask_block[0])) {
        attr->mask_type = t23_mask_block[idx].mask_type;
        memcpy(attr->mask_value, t23_mask_block[idx].mask_value, 3);
    }
    return 0;
}

/* Draw blocks: types 0..2 with an enabled RGB colour are converted to YUV.
 * The stock library always caches into slot 0 but reads slot
 * num * 20 + pinum back; that asymmetry is kept. */
int IMP_ISP_MultiCamera_Tuning_SetDrawBlock(int num, void *p)
{
    T23DrawBlockAttr *attr = p;
    ISPDevice *isp;
    int ret = t23_prologue(__func__, num, NULL, 0, &isp);

    if (ret)
        return ret;
    if ((uint32_t)attr->type <= 2 && attr->cfg[0] && attr->color_type == 0)
        t23_rgb_to_yuv(&attr->cfg[10]);
    t23_draw_block[0] = *attr;
    return t23_block_xfer(__func__, num, 0, T23_CID_DRAW_BLOCK, p, isp);
}

int IMP_ISP_MultiCamera_Tuning_GetDrawBlock(int num, void *p)
{
    T23DrawBlockAttr *attr = p;
    ISPDevice *isp;
    unsigned idx;
    int ret = t23_prologue(__func__, num, NULL, 0, &isp);

    if (ret)
        return ret;
    ret = t23_block_xfer(__func__, num, 1, T23_CID_DRAW_BLOCK, p, isp);
    if (ret)
        return ret;
    idx = (unsigned)num * 20 + attr->pinum;
    if (idx < sizeof(t23_draw_block) / sizeof(t23_draw_block[0])) {
        attr->color_type = t23_draw_block[idx].color_type;
        memcpy(&attr->cfg[10], &t23_draw_block[idx].cfg[10], 3);
    }
    return 0;
}

#define T23_MC_BLOCK(name, get, cid)                                       \
    int IMP_ISP_MultiCamera_Tuning_##name(int num, void *p)                \
    {                                                                      \
        ISPDevice *isp;                                                    \
        int ret = t23_prologue(__func__, num, NULL, 0, &isp);              \
        return ret ? ret : t23_block_xfer(__func__, num, get, cid, p, isp); \
    }

T23_MC_BLOCK(SetOSDAttr, 0, T23_CID_OSD_ATTR)
T23_MC_BLOCK(GetOSDAttr, 1, T23_CID_OSD_ATTR)
T23_MC_BLOCK(SetOSDBlock, 0, T23_CID_OSD_BLOCK)
T23_MC_BLOCK(GetOSDBlock, 1, T23_CID_OSD_BLOCK)

/* Fixed contrast: IMPISPFixedContrastAttr pointer. */
int IMP_ISP_MultiCamera_SetFixedContraster(int num, void *attr)
{
    return t23_set(__func__, num, T23_CID_FIXED_CONTRAST,
                   (uint32_t)(uintptr_t)attr, T23F_NULLCHK);
}

/* --- Move state --------------------------------------------------------- */

/* IMP_ISP_EnableTuning: clear gmovestate and load the 14 hex values of
 * /etc/sensor/<sensor>move.txt into its configuration words. */
void openimp_t23_isp_tuning_enabled(void)
{
    ISPDevice *isp = gISP;
    char path[96];
    FILE *fp;
    uint32_t *c = t23_movestate.cfg;

    memset(&t23_movestate, 0, sizeof(t23_movestate));
    if (isp == NULL)
        return;
    /* IMPSensorInfo.name, char[32], first in the copied sensor info */
    snprintf(path, sizeof(path), "%s%.32smove.txt", "/etc/sensor/",
             (const char *)isp->sensor_info);
    fp = fopen(path, "r");
    if (fp == NULL)
        return;
    if (fscanf(fp, "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x "
                   "%02x %02x %02x %02x",
               &c[0], &c[1], &c[2], &c[3], &c[4], &c[5], &c[6], &c[7],
               &c[8], &c[9], &c[10], &c[11], &c[12], &c[13]) != 14)
        T23_LOG_ERR(__func__, "%s: short move configuration\n", path);
    fclose(fp);
}

/*
 * EnableMovestate reads the current 0x70-byte AE block of CID 0x800002c,
 * keeps it with the running mode, then sends a copy of zeros with the
 * day or night move-state limits of move.txt and the enable bytes the
 * stock library sets.  It returns 0 once past the argument checks, also
 * when a request fails.
 */
int IMP_ISP_MultiCamera_Tuning_EnableMovestate(int num)
{
    ISPDevice *isp;
    uint8_t ae[0x70];
    uint32_t word;
    uint32_t mode = 0;
    const uint32_t *c = t23_movestate.cfg;
    int ret = t23_prologue(__func__, num, NULL, 0, &isp);

    if (ret)
        return ret;
    pthread_mutex_lock(&t23_running_mutex);
    if (IMP_ISP_MultiCamera_Tuning_GetISPRunningMode(num, &mode) != 0) {
        T23_LOG_ERR(__func__, "%s(%d),GetISPRunningMode is failed!\n",
                    __func__, __LINE__);
        goto out;
    }
    t23_movestate.mode = (int32_t)mode;
    memset(ae, 0, sizeof(ae));
    memset(t23_movestate.ae, 0, sizeof(t23_movestate.ae));
    word = (uint32_t)(uintptr_t)t23_movestate.ae;
    if (t23_xfer(isp, num, 1, T23_CID_MOVESTATE, &word) != 0) {
        T23_LOG_ERR(__func__, "%s(%d)\n", __func__, __LINE__);
        goto out;
    }
    if (mode != 0) {
        ae[11] = 1;
        ae[46] = (uint8_t)c[1];
        ae[44] = (uint8_t)c[5];
        ae[45] = (uint8_t)c[4];
        ae[35] = (uint8_t)c[9];
        ae[36] = (uint8_t)c[8];
        ae[38] = (uint8_t)c[13];
        ae[39] = (uint8_t)c[12];
    } else {
        const uint8_t *g = t23_movestate.ae;
        uint32_t div = ((uint32_t)g[33] * g[29]) / 30u;
        uint16_t it;

        memcpy(&it, g + 26, sizeof(it));
        it = (uint16_t)((3u * it) / 5u);
        memcpy(ae + 26, &it, sizeof(it));
        /* The stock library divides unguarded (a zero divisor traps). */
        ae[43] = (uint8_t)((div ? ((uint32_t)g[32] * g[28]) / div : 0) + 1);
        ae[11] = 1;
        ae[82] = 1;
        ae[10] = 1;
        ae[70] = 1;
        ae[98] = 1;
        ae[46] = (uint8_t)c[0];
        ae[44] = (uint8_t)c[3];
        ae[45] = (uint8_t)c[2];
        ae[35] = (uint8_t)c[7];
        ae[36] = (uint8_t)c[6];
        ae[38] = (uint8_t)c[11];
        ae[39] = (uint8_t)c[10];
    }
    ae[71] = ae[101] = ae[99] = ae[100] = 1;
    ae[90] = ae[91] = ae[93] = ae[94] = 1;
    word = (uint32_t)(uintptr_t)ae;
    if (t23_xfer(isp, num, 0, T23_CID_MOVESTATE, &word) != 0)
        T23_LOG_ERR(__func__, "%s(%d)\n", __func__, __LINE__);
out:
    pthread_mutex_unlock(&t23_running_mutex);
    return 0;
}

/* DisableMovestate restores the captured AE block if the running mode is
 * still the one it was captured in, then requests an IDR on channel 0. */
int IMP_ISP_MultiCamera_Tuning_DisableMovestate(int num)
{
    ISPDevice *isp;
    uint32_t mode = 0;
    uint32_t word;
    int ret = t23_prologue(__func__, num, NULL, 0, &isp);

    if (ret)
        return ret;
    pthread_mutex_lock(&t23_running_mutex);
    if (IMP_ISP_MultiCamera_Tuning_GetISPRunningMode(num, &mode) != 0) {
        T23_LOG_ERR(__func__, "%s(%d),GetISPRunningMode is failed!\n",
                    __func__, __LINE__);
    } else if ((int32_t)mode == t23_movestate.mode) {
        uint8_t *g = t23_movestate.ae;

        g[71] = g[82] = g[70] = g[101] = g[99] = 1;
        g[100] = g[90] = g[91] = g[93] = g[94] = 1;
        word = (uint32_t)(uintptr_t)g;
        if (t23_xfer(isp, num, 0, T23_CID_MOVESTATE, &word) != 0)
            T23_LOG_ERR(__func__, "%s(%d)\n", __func__, __LINE__);
    }
    pthread_mutex_unlock(&t23_running_mutex);
    IMP_Encoder_RequestIDR(0);
    return 0;
}

/* --- Calls without a MultiCamera tuning request ----------------------- */

int IMP_ISP_MultiCamera_SetSensorRegister(int num, uint32_t reg,
                                          uint32_t value)
{
    (void)reg;
    (void)value;
    puts("Now we don't support set the MultiCamera SensorRegister!!!");
    if ((unsigned)num >= T23_VI_MAX) {
        T23_LOG_ERR(__func__, "MSG: Number of sensors overflow!\n");
        return T23_ERR_VI_OVERFLOW;
    }
    return 0;
}

int IMP_ISP_MultiCamera_GetSensorRegister(int num, uint32_t reg,
                                          uint32_t *value)
{
    (void)reg;
    (void)value;
    puts("Now we don't support get the MultiCamera SensorRegister!!!");
    if ((unsigned)num >= T23_VI_MAX) {
        T23_LOG_ERR(__func__, "MSG: Number of sensors overflow!\n");
        return T23_ERR_VI_OVERFLOW;
    }
    return 0;
}

int IMP_ISP_MultiCamera_SetFrameDrop(int num, void *attr)
{
    (void)attr;
    puts("Now we don't support set the MultiCamera FrameDrop!!!");
    if ((unsigned)num >= T23_VI_MAX) {
        T23_LOG_ERR(__func__, "MSG: Number of sensors overflow!\n");
        return T23_ERR_VI_OVERFLOW;
    }
    return 0;
}

int IMP_ISP_MultiCamera_GetFrameDrop(int num, void *attr)
{
    (void)attr;
    puts("Now we don't support get the MultiCamera FrameDrop!!!");
    if ((unsigned)num >= T23_VI_MAX) {
        T23_LOG_ERR(__func__, "MSG: Number of sensors overflow!\n");
        return T23_ERR_VI_OVERFLOW;
    }
    return 0;
}

/* Bin path: only sensor 0 can change it; the getter ignores num. */
int IMP_ISP_MultiCamera_SetDefaultBinPath(int num, char *path)
{
    if (gISP == NULL || path == NULL) {
        T23_LOG_ERR(__func__, "[ %s:%d ] ISPDEV cannot open\n", __func__,
                    __LINE__);
        return -1;
    }
    if (num != 0) {
        imp_log_fun(5, IMP_Log_Get_Option(), 2, "IMP-ISP", __FILE__,
                    __LINE__, __func__,
                    "[ %s:%d ] This interface currently does not support "
                    "sub-cameras or the third camera channel\n",
                    __func__, __LINE__);
        return 0;
    }
    return IMP_ISP_SetDefaultBinPath(path);
}

int IMP_ISP_MultiCamera_GetDefaultBinPath(int num, char *path)
{
    (void)num;
    return IMP_ISP_GetDefaultBinPath(path);
}

int IMP_ISP_SetDefaultBinPath_Sec(char *path)
{
    (void)path;
    puts("Now we don't support set the MultiCamera DefaultBinPath!!!");
    return -1;
}

int IMP_ISP_GetDefaultBinPath_Sec(char *path)
{
    (void)path;
    puts("Now we don't support get the MultiCamera DefaultBinPath!!!");
    return -1;
}

/* Second-sensor register access: the plain request with bit 31 of the
 * register address set. */
static int t23_sensor_reg_check(const char *fn, ISPDevice *isp)
{
    int32_t type;

    if (isp == NULL) {
        T23_LOG_ERR(fn, "ISPDEV cannot open\n");
        return -1;
    }
    if (isp->opened < 2) {
        T23_LOG_ERR(fn, "Sensor doesn't Run!\n");
        return -1;
    }
    memcpy(&type, isp->sensor_info + 0x20, sizeof(type));
    if (type == 0) {
        T23_LOG_ERR(fn, "There isn't sensor!\n");
        return -1;
    }
    if (type != 1) {
        T23_LOG_ERR(fn, "Don't support spi sensor!\n");
        return -1;
    }
    return 0;
}

static void t23_sensor_reg_fill(T23SensorRegReq *req, ISPDevice *isp,
                                uint32_t reg)
{
    memset(req, 0, sizeof(*req));
    req->sensor_type = 1;
    memcpy(req->sensor_info, isp->sensor_info, sizeof(req->sensor_info));
    req->reg = (int32_t)(reg | 0x80000000u);
}

int IMP_ISP_SetSensorRegister_Sec(uint32_t reg, uint32_t value)
{
    ISPDevice *isp = gISP;
    T23SensorRegReq req;
    int ret;

    if (t23_sensor_reg_check(__func__, isp))
        return -1;
    t23_sensor_reg_fill(&req, isp, reg);
    req.val = (int32_t)value;
    ret = ioctl(isp->fd, T23_VIDIOC_SET_SENSOR_REG, &req);
    if (ret)
        puts("sorry,g_register failed!");
    return ret;
}

int IMP_ISP_GetSensorRegister_Sec(uint32_t reg, uint32_t *value)
{
    ISPDevice *isp = gISP;
    T23SensorRegReq req;
    int ret;

    if (t23_sensor_reg_check(__func__, isp))
        return -1;
    t23_sensor_reg_fill(&req, isp, reg);
    ret = ioctl(isp->fd, T23_VIDIOC_GET_SENSOR_REG, &req);
    if (ret) {
        puts("sorry,g_register failed!");
        return ret;
    }
    if (value)
        *value = (uint32_t)req.val;
    return 0;
}

/* Second-sensor frame drop.  The stock library then recomputes its frame
 * source drop ratios (check_fps_drop); OpenIMP's frame source does not
 * keep those. */
int IMP_ISP_SetFrameDrop_Sec(void *attr)
{
    ISPDevice *isp = gISP;
    int ret;

    if (isp == NULL || attr == NULL) {
        T23_LOG_ERR(__func__, "%s(%d), ispdev is NULL\n", __func__, __LINE__);
        return -1;
    }
    ret = ioctl(isp->fd, T23_VIDIOC_SET_FRAME_DROP_SEC, attr);
    if (ret < 0)
        T23_LOG_ERR(__func__, "%s(%d), ioctl failed\n", __func__, __LINE__);
    return ret;
}

int IMP_ISP_GetFrameDrop_Sec(void *attr)
{
    ISPDevice *isp = gISP;
    int ret;

    if (isp == NULL || attr == NULL) {
        T23_LOG_ERR(__func__, "%s(%d), ispdev is NULL\n", __func__, __LINE__);
        return -1;
    }
    ret = ioctl(isp->fd, T23_VIDIOC_GET_FRAME_DROP_SEC, attr);
    if (ret < 0)
        T23_LOG_ERR(__func__, "%s(%d), ioctl failed\n", __func__, __LINE__);
    return ret;
}

/* Dual-sensor input mode: mode < 73 with a low nibble below 9. */
int IMP_ISP_SetCameraInputMode(void *p)
{
    T23CameraInputMode *mode = p;
    ISPDevice *isp = gISP;
    int ret;

    if (isp == NULL || mode == NULL) {
        T23_LOG_ERR(__func__, "%s(%d), ispdev is NULL\n", __func__, __LINE__);
        return -1;
    }
    if (mode->mode >= 73 || (mode->mode & 0xf) >= 9) {
        T23_LOG_ERR(__func__, "%s(%d), not support mode %u\n", __func__,
                    __LINE__, mode->mode);
        return -1;
    }
    ret = ioctl(isp->fd, T23_VIDIOC_SET_CAMERA_INPUT_MODE, mode);
    if (ret) {
        T23_LOG_ERR(__func__, "%s(%d), ioctl failed\n", __func__, __LINE__);
        return ret;
    }
    t23_camera_input_mode = mode->mode;
    return 0;
}

static int t23_dev_ioctl(const char *fn, uint32_t nr, void *arg)
{
    ISPDevice *isp = gISP;
    int ret;

    if (isp == NULL || arg == NULL) {
        T23_LOG_ERR(fn, "%s(%d), ispdev is NULL\n", fn, __LINE__);
        return -1;
    }
    ret = ioctl(isp->fd, nr, arg);
    if (ret)
        T23_LOG_ERR(fn, "%s(%d), ioctl failed\n", fn, __LINE__);
    return ret;
}

int IMP_ISP_StreamCheck(void *state)
{
    return t23_dev_ioctl(__func__, T23_VIDIOC_STREAM_CHECK, state);
}

int IMP_ISP_SetStreamOut(void *state)
{
    return t23_dev_ioctl(__func__, T23_VIDIOC_SET_STREAM_OUT, state);
}

/* --- Single-camera calls without a MultiCamera form --------------------- */

/* ISP process, firmware freeze and shading are S_CTRL controls of sensor 0;
 * process and freeze skip unchanged values. */
static int t23_cached_ctrl(const char *fn, uint32_t id, int32_t value,
                           int32_t *cache)
{
    ISPDevice *isp = gISP;
    T23Ctrl ctrl = { id, value };
    int ret;

    if (isp == NULL || isp->tuning == NULL || isp->tuning_state != 2) {
        T23_LOG_ERR(fn, "%s(%d), tuning is NULL\n", fn, __LINE__);
        return -1;
    }
    if (cache && *cache == value)
        return 0;
    ret = ioctl(isp->tuning_fd, T23_VIDIOC_S_CTRL, &ctrl);
    if (ret == 0 && cache)
        *cache = value;
    return ret;
}

int IMP_ISP_Tuning_SetISPProcess(int process)
{
    return t23_cached_ctrl(__func__, T23_CID_ISP_PROCESS, process,
                           &t23_isp_process);
}

int IMP_ISP_Tuning_SetFWFreeze(int freeze)
{
    return t23_cached_ctrl(__func__, T23_CID_FW_FREEZE, freeze,
                           &t23_fw_freeze);
}

int IMP_ISP_Tuning_SetShading(int enable)
{
    int ret = t23_cached_ctrl(__func__, T23_CID_SHADING, enable, NULL);

    printf("##### %s,%s,%d en = %d, ret = %d\n", __func__, __FILE__,
           __LINE__, enable, ret);
    return ret;
}

int IMP_ISP_Tuning_GetNCUInfo(void *info)
{
    return t23_get_ptr(__func__, 0, T23_CID_NCU_INFO, info, T23F_NULLCHK);
}

/* Returns the sensor-0 NCU allocation made by AddSensor (gISP + 0xb0). */
int IMP_ISP_Tuning_GetNCUAlloc(void)
{
    ISPDevice *isp = gISP;

    return isp ? (int)(intptr_t)isp->sensor_alloc[0] : 0;
}

/* The tuning daemon's video-drop callback. */
int IMP_ISP_Tuning_SetVideoDrop(void *attr)
{
    ISPDevice *isp = gISP;

    if (isp == NULL || isp->tuning_state != 2)
        return -1;
    pthread_mutex_lock(&t23_deamon_mutex);
    t23_video_drop = attr;
    pthread_mutex_unlock(&t23_deamon_mutex);
    return 0;
}

void *openimp_t23_isp_video_drop(void)
{
    void *attr;

    pthread_mutex_lock(&t23_deamon_mutex);
    attr = t23_video_drop;
    pthread_mutex_unlock(&t23_deamon_mutex);
    return attr;
}

uint32_t openimp_t23_isp_camera_input_mode(void)
{
    return t23_camera_input_mode;
}

/* ------------------------------------------------------------------------
 * Sensor 0 (IMP_ISP_Tuning_X) and sensor 1 (IMP_ISP_Tuning_X_Sec) forms
 * ---------------------------------------------------------------------- */

#define T23_WRAP0(name)                                                    \
    int IMP_ISP_Tuning_##name(void)                                        \
    {                                                                      \
        return IMP_ISP_MultiCamera_Tuning_##name(0);                       \
    }                                                                      \
    int IMP_ISP_Tuning_##name##_Sec(void)                                  \
    {                                                                      \
        return IMP_ISP_MultiCamera_Tuning_##name(1);                       \
    }

#define T23_WRAP1(name, type)                                              \
    int IMP_ISP_Tuning_##name(type a)                                      \
    {                                                                      \
        return IMP_ISP_MultiCamera_Tuning_##name(0, a);                    \
    }                                                                      \
    int IMP_ISP_Tuning_##name##_Sec(type a)                                \
    {                                                                      \
        return IMP_ISP_MultiCamera_Tuning_##name(1, a);                    \
    }

#define T23_WRAP2(name, ta, tb)                                            \
    int IMP_ISP_Tuning_##name(ta a, tb b)                                  \
    {                                                                      \
        return IMP_ISP_MultiCamera_Tuning_##name(0, a, b);                 \
    }                                                                      \
    int IMP_ISP_Tuning_##name##_Sec(ta a, tb b)                            \
    {                                                                      \
        return IMP_ISP_MultiCamera_Tuning_##name(1, a, b);                 \
    }

T23_WRAP2(SetSensorFPS, uint32_t, uint32_t)
T23_WRAP2(GetSensorFPS, uint32_t *, uint32_t *)
T23_WRAP1(SetAntiFlickerAttr, int)
T23_WRAP1(GetAntiFlickerAttr, int *)
T23_WRAP1(SetBrightness, unsigned char)
T23_WRAP1(GetBrightness, unsigned char *)
T23_WRAP1(SetContrast, unsigned char)
T23_WRAP1(GetContrast, unsigned char *)
T23_WRAP1(SetSharpness, unsigned char)
T23_WRAP1(GetSharpness, unsigned char *)
T23_WRAP1(SetBcshHue, unsigned char)
T23_WRAP1(GetBcshHue, unsigned char *)
T23_WRAP1(SetSaturation, unsigned char)
T23_WRAP1(GetSaturation, unsigned char *)
T23_WRAP1(GetTotalGain, uint32_t *)
T23_WRAP1(SetISPHflip, int)
T23_WRAP1(GetISPHflip, uint32_t *)
T23_WRAP1(SetISPVflip, int)
T23_WRAP1(GetISPVflip, uint32_t *)
T23_WRAP1(SetSensorHflip, int)
T23_WRAP1(GetSensorHflip, uint32_t *)
T23_WRAP1(SetSensorVflip, int)
T23_WRAP1(GetSensorVflip, uint32_t *)
T23_WRAP1(SetISPRunningMode, int)
T23_WRAP1(GetISPRunningMode, uint32_t *)
T23_WRAP1(SetISPCustomMode, int)
T23_WRAP1(GetISPCustomMode, uint32_t *)
T23_WRAP1(SetGamma, void *)
T23_WRAP1(GetGamma, void *)
T23_WRAP1(SetAeComp, int)
T23_WRAP1(GetAeComp, uint32_t *)
T23_WRAP1(GetAeLuma, uint32_t *)
T23_WRAP1(SetAeFreeze, int)
T23_WRAP1(SetExpr, void *)
T23_WRAP1(GetExpr, void *)
T23_WRAP1(SetWB, void *)
T23_WRAP1(GetWB, void *)
T23_WRAP1(GetWB_Statis, void *)
T23_WRAP1(GetWB_GOL_Statis, void *)
T23_WRAP1(SetAwbClust, void *)
T23_WRAP1(GetAwbClust, void *)
T23_WRAP1(SetAwbCtTrend, void *)
T23_WRAP1(GetAwbCtTrend, void *)
T23_WRAP1(Awb_GetRgbCoefft, void *)
T23_WRAP1(Awb_SetRgbCoefft, void *)
T23_WRAP1(SetMaxAgain, uint32_t)
T23_WRAP1(GetMaxAgain, uint32_t *)
T23_WRAP1(SetMaxDgain, uint32_t)
T23_WRAP1(GetMaxDgain, uint32_t *)
T23_WRAP1(SetHiLightDepress, uint32_t)
T23_WRAP1(GetHiLightDepress, uint32_t *)
T23_WRAP1(SetBacklightComp, uint32_t)
T23_WRAP1(GetBacklightComp, uint32_t *)
T23_WRAP1(SetTemperStrength, uint32_t)
T23_WRAP1(SetSinterStrength, uint32_t)
T23_WRAP1(GetEVAttr, void *)
T23_WRAP0(EnableMovestate)
T23_WRAP0(DisableMovestate)
T23_WRAP1(SetAeWeight, void *)
T23_WRAP1(GetAeWeight, void *)
T23_WRAP1(AE_GetROI, void *)
T23_WRAP1(AE_SetROI, void *)
T23_WRAP1(SetAwbWeight, void *)
T23_WRAP1(GetAwbWeight, void *)
T23_WRAP1(GetAwbZone, void *)
T23_WRAP1(SetWB_ALGO, int)
T23_WRAP1(SetAeHist, void *)
T23_WRAP1(GetAeHist, void *)
T23_WRAP1(GetAeHist_Origin, void *)
T23_WRAP1(GetAwbHist, void *)
T23_WRAP1(SetAwbHist, void *)
T23_WRAP1(GetAFMetrices, void *)
T23_WRAP1(GetAfHist, void *)
T23_WRAP1(SetAfHist, void *)
T23_WRAP1(GetAfWeight, void *)
T23_WRAP1(GetAfZone, void *)
T23_WRAP1(WaitFrame, void *)
T23_WRAP1(SetAeMin, void *)
T23_WRAP1(GetAeMin, void *)
T23_WRAP1(SetAe_IT_MAX, uint32_t)
T23_WRAP1(GetAE_IT_MAX, uint32_t *)
T23_WRAP1(GetAeZone, void *)
T23_WRAP1(SetAeTargetList, void *)
T23_WRAP1(GetAeTargetList, void *)
T23_WRAP1(SetModuleControl, void *)
T23_WRAP1(GetModuleControl, void *)
T23_WRAP1(SetFrontCrop, void *)
T23_WRAP1(GetFrontCrop, void *)
T23_WRAP1(SetDPC_Strength, uint32_t)
T23_WRAP1(GetDPC_Strength, uint32_t *)
T23_WRAP1(SetDRC_Strength, uint32_t)
T23_WRAP1(GetDRC_Strength, uint32_t *)
T23_WRAP1(SetHVFLIP, int)
T23_WRAP1(GetHVFlip, uint32_t *)
T23_WRAP1(SetMask, void *)
T23_WRAP1(GetMask, void *)
T23_WRAP1(GetSensorAttr, void *)
T23_WRAP1(EnableDRC, int)
T23_WRAP1(EnableDefog, int)
T23_WRAP1(SetAwbCt, void *)
T23_WRAP1(GetAWBCt, void *)
T23_WRAP1(SetCCMAttr, void *)
T23_WRAP1(GetCCMAttr, void *)
T23_WRAP1(SetAeAttr, void *)
T23_WRAP1(GetAeAttr, void *)
T23_WRAP1(GetAeState, void *)
T23_WRAP1(SetScalerLv, void *)
T23_WRAP1(GetBlcAttr, void *)
T23_WRAP1(SetDefog_Strength, void *)
T23_WRAP1(GetDefog_Strength, void *)
T23_WRAP1(SetCsc_Attr, void *)
T23_WRAP1(GetCsc_Attr, void *)
T23_WRAP1(SetAutoZoom, void *)
T23_WRAP1(GetAutoZoom, void *)
T23_WRAP1(SetMaskBlock, void *)
T23_WRAP1(GetMaskBlock, void *)
T23_WRAP1(SetOSDAttr, void *)
T23_WRAP1(GetOSDAttr, void *)
T23_WRAP1(SetOSDBlock, void *)
T23_WRAP1(GetOSDBlock, void *)
T23_WRAP1(SetDrawBlock, void *)
T23_WRAP1(GetDrawBlock, void *)
T23_WRAP1(SwitchBin, void *)

/* SetAfWeight_Sec of the stock library calls the *Get* path (a vendor bug
 * that is kept for parity); the plain call sets. */
int IMP_ISP_Tuning_SetAfWeight(void *a)
{
    return IMP_ISP_MultiCamera_Tuning_SetAfWeight(0, a);
}

int IMP_ISP_Tuning_SetAfWeight_Sec(void *a)
{
    return IMP_ISP_MultiCamera_Tuning_GetAfWeight(1, a);
}

/* Zone weights exist for sensor 0 only. */
int IMP_ISP_Tuning_SetAwbZoneWeight(void *a)
{
    return IMP_ISP_MultiCamera_Tuning_SetAwbZoneWeight(0, a);
}

int IMP_ISP_Tuning_GetAwbZoneWeight(void *a)
{
    return IMP_ISP_MultiCamera_Tuning_GetAwbZoneWeight(0, a);
}

/* Plain internal contrast/sharpness entries keep OpenIMP's earlier export. */
int IMP_ISP_Tuning_SetContrast_internal(uint32_t v, int32_t mode)
{
    return IMP_ISP_MultiCamera_Tuning_SetContrast_internal(0, v, mode);
}

int IMP_ISP_Tuning_SetSharpness_internal(uint32_t v, int32_t mode)
{
    return IMP_ISP_MultiCamera_Tuning_SetSharpness_internal(0, v, mode);
}

int IMP_ISP_SetFixedContraster(void *attr)
{
    return IMP_ISP_MultiCamera_SetFixedContraster(0, attr);
}

int IMP_ISP_SetFixedContraster_Sec(void *attr)
{
    return IMP_ISP_MultiCamera_SetFixedContraster(1, attr);
}
