/* P3 T40 control plane: ISP tuning and direct system-register access. */

#include <stddef.h>
#include <errno.h>
#include <pthread.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <imp/imp_isp.h>

#include "../isp/isp_mask_rgb2yuv.h"

#if defined(PLATFORM_T41)
#define TISP_VIDIOC_DEFAULT_TUNING 0xc0105435U
#else
#define TISP_VIDIOC_DEFAULT_TUNING 0xc0105436U
#endif

/*
 * T40's OEM userspace ABI predates the newer 0x56xx tuning-node interface.
 * The stock driver owns all sensor-specific exposure and gain translation.
 */
#define TISP_CID_AE_EXPR_INFO 0x08000023
#define TISP_CID_AE_WEIGHT 0x08000021
#define TISP_CID_AE_STATISTICS 0x08000022
#define TISP_CID_AWB_ATTR 0x08000010
#define TISP_CID_AWB_STATISTICS 0x08000011
#define TISP_CID_AWB_WEIGHT 0x08000012
#define TISP_CID_AWB_GLOBAL_STATISTICS 0x08000013
#define TISP_CID_SENSOR_FPS 0x08000070
#define TISP_CID_RUNNING_MODE 0x08000071
#define TISP_CID_HVFLIP 0x08000073
#define TISP_CID_BCSH_HUE 0x08000081
#define TISP_CID_BRIGHTNESS 0x08000092
#define TISP_CID_SHARPNESS 0x08000093
#define TISP_CID_SATURATION 0x08000094
#define TISP_CID_CONTRAST 0x08000095
/* The same control numbers in the vendor T40 1.3.1 and T41 1.2.x libimp
 * (checked in both disassemblies); only the ioctl number differs. */
#define TISP_CID_AE_SCENCE 0x08000024
#define TISP_CID_GAMMA 0x08000025
#define TISP_CID_WDR_OUTPUT_MODE 0x08000054
#define TISP_CID_MODULE_CONTROL 0x08000072
#define TISP_CID_AUTOZOOM 0x08000077
#define TISP_CID_CCM 0x08000080
#define TISP_CID_SCALER_LV 0x080000a6
#define TISP_CID_MODULE_RATIO 0x080000a4
#define TISP_CID_MASK 0x08000074      /* T40 SetMask/GetMask, 288 bytes */
#if defined(PLATFORM_T41)
#define TISP_CID_MASK_BLOCK 0x08000074
#define TISP_CID_AWB_RGB_COEFFT 0x08000098
#define TISP_CID_ANTIFLICKER 0x08000026
#endif

typedef struct {
    int32_t vinum;
    int32_t direction;
    int32_t control;
    uintptr_t payload;
} P3TuningRequest;

extern int OpenIMP_P1_TuningIOCtl(uint32_t command, void *argument);
extern int OpenIMP_P1_TuningEnabled(void);
extern int OpenIMP_P1_SetDefaultBinPath(IMPVI_NUM num, const char *path);
extern int OpenIMP_P1_GetDefaultBinPath(IMPVI_NUM num, char *path, size_t size);

void OpenIMP_P3_FrameStats(uint32_t luma, uint32_t u_mean, uint32_t v_mean)
{
    /*
     * The stock ISP firmware owns AE/AWB statistics and policy.  Keep these
     * encoder-side samples diagnostic-only; they must never program a sensor.
     */
    (void)luma;
    (void)u_mean;
    (void)v_mean;
}

static int p3_tuning_pointer(IMPVI_NUM num, int32_t direction,
                             int32_t control, void *payload)
{
    P3TuningRequest request;

    if (num < IMPVI_MAIN || num >= IMPVI_BUTT || !payload)
        return -1;
    request.vinum = num;
    request.direction = direction;
    request.control = control;
    request.payload = (uintptr_t)payload;
    return OpenIMP_P1_TuningIOCtl(TISP_VIDIOC_DEFAULT_TUNING, &request);
}

static int p3_tuning_scalar(IMPVI_NUM num, int32_t direction,
                            int32_t control, int32_t *value)
{
    P3TuningRequest request;
    int result;

    if (num < IMPVI_MAIN || num >= IMPVI_BUTT || !value)
        return -1;
    request.vinum = num;
    request.direction = direction;
    request.control = control;
    request.payload = direction ? 0U : (uintptr_t)(uint32_t)*value;
    result = OpenIMP_P1_TuningIOCtl(TISP_VIDIOC_DEFAULT_TUNING, &request);
    if (result == 0 && direction)
        *value = (int32_t)request.payload;
    return result;
}

/* Vendor T41 1.2.0 and T40 1.3.1 (libimp disassembly of both): the pointer
 * tuning ioctl, get, control 0x8000033, filling {hts, vts, fps, width,
 * height}; only the ioctl number differs (TISP_VIDIOC_DEFAULT_TUNING). */
int32_t IMP_ISP_Tuning_GetSensorAttr(IMPVI_NUM num, IMPISPSENSORAttr *attr)
{
    return p3_tuning_pointer(num, 1, 0x8000033, attr);
}

/* ---- vendor ISPDevice-fd entries (T40 1.3.1 and T41 1.2.6 use the same
 * ioctl numbers; the stock driver owns the hardware access) ---- */
extern int OpenIMP_P1_IspIOCtl(uint32_t command, void *argument);
extern int OpenIMP_P1_GetSensorName(char name[32], int32_t *cbus_type);

#define TISP_IOCTL_SET_SENSOR_REGISTER 0xc040540dU
#define TISP_IOCTL_GET_SENSOR_REGISTER 0x8040540eU
#define TISP_IOCTL_SET_WDR_ENABLE      0x80045413U
#define TISP_IOCTL_SET_WDR_DISABLE     0x80045414U
#define TISP_IOCTL_SET_FRAME_DROP      0xc004542cU
#define TISP_IOCTL_GET_FRAME_DROP      0xc004542dU
#define TISP_CID_AF_WEIGHT             0x08000032

/* Vendor T41 1.2.6 request (64 bytes, IMP_ISP_Set/GetSensorRegister): name[32],
 * vinum at 32, bus type (1: I2C) at 36, 64-bit register at 48, 64-bit value
 * at 56.  SPI sensors are refused as the vendor does. */
struct p3_sensor_reg {
    char name[32];
    uint32_t vinum;
    int32_t type;
    uint32_t unused[2];
    uint32_t reg;
    uint32_t reg_hi;
    uint32_t value;
    uint32_t value_hi;
};
_Static_assert(sizeof(struct p3_sensor_reg) == 64, "sensor register request is 64 bytes");
_Static_assert(offsetof(struct p3_sensor_reg, vinum) == 32, "vinum at 32");
_Static_assert(offsetof(struct p3_sensor_reg, type) == 36, "bus type at 36");
_Static_assert(offsetof(struct p3_sensor_reg, reg) == 48, "reg at 48");
_Static_assert(offsetof(struct p3_sensor_reg, value) == 56, "value at 56");

/* Vendor frame drop request (40 bytes): channel word, then 3 x {enable,
 * lsize, fmark}; the driver reads and writes back all 40 bytes. */
struct p3_frame_drop_req {
    uint32_t ch;
    IMPISPFrameDropAttr attr;
};
_Static_assert(sizeof(IMPISPFrameDropAttr) == 36, "attr is 3 x 12 bytes");
_Static_assert(sizeof(struct p3_frame_drop_req) == 40, "frame drop request is 40 bytes");

static int p3_sensor_register(int set, uint32_t addr, uint32_t *value)
{
    struct p3_sensor_reg req;
    int result;

    memset(&req, 0, sizeof(req));
    if (!value || OpenIMP_P1_GetSensorName(req.name, &req.type) != 0 ||
        req.type != 1)
        return -1;
    req.vinum = 0;
    req.reg = addr;
    if (set)
        req.value = *value;
    result = OpenIMP_P1_IspIOCtl(set ? TISP_IOCTL_SET_SENSOR_REGISTER :
                                 TISP_IOCTL_GET_SENSOR_REGISTER, &req);
    if (result == 0 && !set)
        *value = req.value;
    return result;
}

#if defined(PLATFORM_T41)
/* T41 1.2.6: one {addr, value} structure */
int32_t IMP_ISP_SetSensorRegister(IMPVI_NUM num, IMPISPSensorRegister *reg)
{
    if (num != IMPVI_MAIN || !reg)
        return -1;
    return p3_sensor_register(1, reg->addr, &reg->value);
}

int32_t IMP_ISP_GetSensorRegister(IMPVI_NUM num, IMPISPSensorRegister *reg)
{
    if (num != IMPVI_MAIN || !reg)
        return -1;
    return p3_sensor_register(0, reg->addr, &reg->value);
}
#else
/* T40 1.3.1: the address and the value by pointer */
int32_t IMP_ISP_SetSensorRegister(IMPVI_NUM num, uint32_t *reg, uint32_t *value)
{
    if (num != IMPVI_MAIN || !reg)
        return -1;
    return p3_sensor_register(1, *reg, value);
}

int32_t IMP_ISP_GetSensorRegister(IMPVI_NUM num, uint32_t *reg, uint32_t *value)
{
    if (num != IMPVI_MAIN || !reg)
        return -1;
    return p3_sensor_register(0, *reg, value);
}
#endif

/* Frame drop: the vendor ioctl number has size 4 in its _IOC field but the
 * library passes the whole attribute pointer (same as the other T-series
 * vendor libs, e.g. 0xc00456e6 in docs/re); the driver reads the table.
 * The sensor request's size word is left 0 as there (not written). */
/* Vendor: the 3 x {enable, lsize, fmark} table goes to the ISP as is
 * (lsize 0..31 as the header states). */
static int p3_frame_drop_valid(const IMPISPFrameDropAttr *attr)
{
    unsigned int i;

    for (i = 0; i < 3U; i++)
        if (attr->fdrop[i].enable >= IMPISP_TUNING_OPS_MODE_BUTT ||
            attr->fdrop[i].lsize > 31U)
            return 0;
    return 1;
}

int32_t IMP_ISP_SetFrameDrop(IMPVI_NUM num, IMPISPFrameDropAttr *attr)
{
    struct p3_frame_drop_req req;

    if (num != IMPVI_MAIN || !attr || !p3_frame_drop_valid(attr))
        return -1;
    memset(&req, 0, sizeof(req));
    req.ch = (uint32_t)num;
    req.attr = *attr;
    return OpenIMP_P1_IspIOCtl(TISP_IOCTL_SET_FRAME_DROP, &req);
}

int32_t IMP_ISP_GetFrameDrop(IMPVI_NUM num, IMPISPFrameDropAttr *attr)
{
    struct p3_frame_drop_req req;
    int result;

    if (num != IMPVI_MAIN || !attr)
        return -1;
    memset(&req, 0, sizeof(req));
    req.ch = (uint32_t)num;
    result = OpenIMP_P1_IspIOCtl(TISP_IOCTL_GET_FRAME_DROP, &req);
    if (result == 0)
        *attr = req.attr;
    return result;
}

/* Vendor: AE/AWB/AF weights are 15x15 byte tables over the tuning node. */
int32_t IMP_ISP_Tuning_SetAfWeight(IMPVI_NUM num, IMPISPWeight *af_weight)
{
    return p3_tuning_pointer(num, 0, TISP_CID_AF_WEIGHT, af_weight);
}

int32_t IMP_ISP_Tuning_GetAfWeight(IMPVI_NUM num, IMPISPWeight *af_weight)
{
    return p3_tuning_pointer(num, 1, TISP_CID_AF_WEIGHT, af_weight);
}

/* IMP_ISP_Tuning_SwitchBin, from the vendor T40 1.3.1 and T41 1.2.6 libimp
 * disassembly: pointer tuning ioctl, set, control 0x80000a5, payload =
 * IMPISPBinAttr { u32 enable; char bname[64 (T40) / 128 (T41)]; } (68 / 132
 * bytes, copied by the kernel).  enable 1 loads bname, 0 returns to the
 * sensor's configured default bin.  Check order and return codes are the
 * vendor's (T40 small negatives; T41 the same numbers as -4096 + n, with a
 * sensor limit of 2 instead of 4).  The T41 library additionally keeps a
 * copy of the attribute for a replay after an ISP restart; that bookkeeping
 * is not reproduced.  Beyond the vendor: the name goes to the kernel in a
 * copy that is always NUL terminated. */
#define TISP_CID_SWITCH_BIN 0x080000a5
#if defined(PLATFORM_T41)
#define SB_MAX_NUM  2
#define SB_E(n)     (-4096 + (n))
#define SB_E_NUM    SB_E(12)
#else
#define SB_MAX_NUM  4
#define SB_E(n)     (-(n))
#define SB_E_NUM    SB_E(4)
#endif
#define SB_E_NULL   SB_E(9)
#define SB_E_STATE  SB_E(5)
#define SB_E_ENABLE SB_E(10)
#define SB_E_IOCTL  SB_E(6)

/* Pure request builder (host-tested): the vendor checks, the NUL-terminated
 * copy and the 16-byte envelope.  Returns 0 or the vendor error code. */
int32_t openimp_switch_bin_request(IMPVI_NUM num, const IMPISPBinAttr *attr,
                                   int isp_enabled, IMPISPBinAttr *copy,
                                   P3TuningRequest *request)
{
#if defined(PLATFORM_T41)
    if (!attr)
        return SB_E_NULL;
    if ((uint32_t)num >= SB_MAX_NUM)
        return SB_E_NUM;
#else
    if ((uint32_t)num >= SB_MAX_NUM)
        return SB_E_NUM;
    if (!attr)
        return SB_E_NULL;
#endif
    if (!isp_enabled)
        return SB_E_STATE;
    if ((uint32_t)attr->enable >= 2U)
        return SB_E_ENABLE;
    memcpy(copy, attr, sizeof(*copy));
    copy->bname[sizeof(copy->bname) - 1] = '\0';
    request->vinum = (int32_t)num;
    request->direction = 0;
    request->control = TISP_CID_SWITCH_BIN;
    request->payload = (uintptr_t)copy;
    return 0;
}

int32_t IMP_ISP_Tuning_SwitchBin(IMPVI_NUM num, IMPISPBinAttr *attr)
{
    IMPISPBinAttr copy;
    P3TuningRequest request;
    int32_t result;

    result = openimp_switch_bin_request(num, attr, OpenIMP_P1_TuningEnabled(),
                                        &copy, &request);
    if (result)
        return result;
    if (OpenIMP_P1_TuningIOCtl(TISP_VIDIOC_DEFAULT_TUNING, &request) != 0)
        return SB_E_IOCTL;
    return 0;
}

/* Vendor WDR_ENABLE: 1 / 0 switch the ISP's WDR mode by ioctl and the
 * library remembers the last mode; _GET answers from that memory (no
 * ioctl).  Any other value is accepted without effect. */
static IMPISPTuningOpsMode p3_wdr_mode;
static pthread_mutex_t p3_wdr_lock = PTHREAD_MUTEX_INITIALIZER;

int32_t IMP_ISP_WDR_ENABLE(IMPVI_NUM num, IMPISPTuningOpsMode *mode)
{
    int32_t zero = 0;
    int result;

    if (num != IMPVI_MAIN || !mode)
        return -1;
    if (*mode != IMPISP_TUNING_OPS_MODE_ENABLE &&
        *mode != IMPISP_TUNING_OPS_MODE_DISABLE)
        return 0;
    pthread_mutex_lock(&p3_wdr_lock);
    result = OpenIMP_P1_IspIOCtl(*mode == IMPISP_TUNING_OPS_MODE_ENABLE ?
                                 TISP_IOCTL_SET_WDR_ENABLE :
                                 TISP_IOCTL_SET_WDR_DISABLE, &zero);
    if (result == 0)
        p3_wdr_mode = *mode;
    pthread_mutex_unlock(&p3_wdr_lock);
    return result;
}

int32_t IMP_ISP_WDR_ENABLE_GET(IMPVI_NUM num, IMPISPTuningOpsMode *mode)
{
    if (num != IMPVI_MAIN || !mode)
        return -1;
    pthread_mutex_lock(&p3_wdr_lock);
    *mode = p3_wdr_mode;
    pthread_mutex_unlock(&p3_wdr_lock);
    return 0;
}

int32_t IMP_ISP_Tuning_GetAeExprInfo(IMPVI_NUM num,
                                     IMPISPAEExprInfo *exprinfo)
{
    return p3_tuning_pointer(num, 1, TISP_CID_AE_EXPR_INFO, exprinfo);
}

int32_t IMP_ISP_Tuning_SetAeExprInfo(IMPVI_NUM num,
                                     IMPISPAEExprInfo *exprinfo)
{
    return p3_tuning_pointer(num, 0, TISP_CID_AE_EXPR_INFO, exprinfo);
}

int32_t IMP_ISP_Tuning_SetAeWeight(IMPVI_NUM num,
                                    IMPISPAEWeightAttr *ae_weight)
{
    return p3_tuning_pointer(num, 0, TISP_CID_AE_WEIGHT, ae_weight);
}

int32_t IMP_ISP_Tuning_GetAeWeight(IMPVI_NUM num,
                                    IMPISPAEWeightAttr *ae_weight)
{
    return p3_tuning_pointer(num, 1, TISP_CID_AE_WEIGHT, ae_weight);
}

int32_t IMP_ISP_Tuning_GetAeStatistics(IMPVI_NUM num,
                                        IMPISPAEStatisInfo *ae_statis)
{
    return p3_tuning_pointer(num, 1, TISP_CID_AE_STATISTICS, ae_statis);
}

int32_t IMP_ISP_Tuning_SetAwbAttr(IMPVI_NUM num, IMPISPWBAttr *attr)
{
    return p3_tuning_pointer(num, 0, TISP_CID_AWB_ATTR, attr);
}

int32_t IMP_ISP_Tuning_GetAwbAttr(IMPVI_NUM num, IMPISPWBAttr *attr)
{
    return p3_tuning_pointer(num, 1, TISP_CID_AWB_ATTR, attr);
}

int32_t IMP_ISP_Tuning_GetAwbStatistics(IMPVI_NUM num,
                                         IMPISPAWBStatisInfo *awb_statis)
{
    return p3_tuning_pointer(num, 1, TISP_CID_AWB_STATISTICS, awb_statis);
}

int32_t IMP_ISP_Tuning_SetAwbWeight(IMPVI_NUM num, IMPISPWeight *awb_weight)
{
    return p3_tuning_pointer(num, 0, TISP_CID_AWB_WEIGHT, awb_weight);
}

int32_t IMP_ISP_Tuning_GetAwbWeight(IMPVI_NUM num, IMPISPWeight *awb_weight)
{
    return p3_tuning_pointer(num, 1, TISP_CID_AWB_WEIGHT, awb_weight);
}

int32_t IMP_ISP_Tuning_GetAwbGlobalStatistics(
    IMPVI_NUM num, IMPISPAWBGlobalStatisInfo *awb_statis)
{
    return p3_tuning_pointer(num, 1, TISP_CID_AWB_GLOBAL_STATISTICS,
                             awb_statis);
}

static struct {
    unsigned char brightness;
    unsigned char contrast;
    unsigned char saturation;
    unsigned char sharpness;
    unsigned char hue;
    uint32_t fps_num;
    uint32_t fps_den;
#if defined(PLATFORM_T41)
    IMPISPHVFLIPAttr flip;
#else
    IMPISPHVFLIP flip;
#endif
    IMPISPRunningMode running_mode;
    IMPISPAntiflickerAttr antiflicker;
    char bin_path[128];
} p3_controls = {
    .brightness = 128,
    .contrast = 128,
    .saturation = 128,
    .sharpness = 128,
    .hue = 128,
    .fps_num = 30,
    .fps_den = 1,
};

static int p3_tuning_set(int32_t id, int32_t value)
{
    return p3_tuning_scalar(IMPVI_MAIN, 0, id, &value);
}

static int p3_tuning_get(int32_t id, int32_t *value)
{
    return p3_tuning_scalar(IMPVI_MAIN, 1, id, value);
}

#if defined(PLATFORM_T41)
#define P3_BCSH_ACCESSORS(Name, field, id)                                    \
    int32_t IMP_ISP_Tuning_Set##Name(IMPVI_NUM num, unsigned char *value)     \
    {                                                                         \
        int result;                                                           \
        if (num != IMPVI_MAIN || !value)                                     \
            return -1;                                                        \
        result = p3_tuning_pointer(num, 0, (id), value);                     \
        if (result == 0)                                                      \
            p3_controls.field = *value;                                       \
        return result;                                                        \
    }                                                                         \
    int32_t IMP_ISP_Tuning_Get##Name(IMPVI_NUM num, unsigned char *value)     \
    {                                                                         \
        int result;                                                           \
        if (num != IMPVI_MAIN || !value)                                     \
            return -1;                                                        \
        result = p3_tuning_pointer(num, 1, (id), value);                     \
        if (result == 0)                                                      \
            p3_controls.field = *value;                                       \
        *value = p3_controls.field;                                           \
        return result;                                                        \
    }

#else
/* Vendor T40 1.3.1 (libimp disassembly): Set<Name> checks num < 4 (-4), the
 * value pointer (-9), sends the pointer tuning ioctl (-6 on failure) and, on
 * success, stores the byte in the tuning-device cache at <field offset> + num;
 * Get<Name> never talks to the driver, it returns that cached byte and 0
 * (a driver get of these controls fails, which made the OpenIMP getter -1).
 * Field offsets in the vendor tuning device: brightness 4, contrast 8,
 * sharpness 12, saturation 16.  The cache starts at the neutral 128. */
enum { p3_bcsh_brightness, p3_bcsh_contrast, p3_bcsh_sharpness,
       p3_bcsh_saturation };
static unsigned char p3_bcsh_cache[4][4] = {
    {128, 128, 128, 128}, {128, 128, 128, 128},
    {128, 128, 128, 128}, {128, 128, 128, 128},
};

static int p3_bcsh_set(IMPVI_NUM num, int field, int32_t id, unsigned char *value)
{
    if ((unsigned)num >= 4U)
        return -4;
    if (!value)
        return -9;
    if (p3_tuning_pointer(num, 0, id, value) != 0)
        return -6;
    p3_bcsh_cache[num][field] = *value;
    return 0;
}

static int p3_bcsh_get(IMPVI_NUM num, int field, unsigned char *value)
{
    if ((unsigned)num >= 4U || !value)
        return -1;     /* the vendor would fault; stay safe */
    *value = p3_bcsh_cache[num][field];
    return 0;
}

#define P3_BCSH_ACCESSORS(Name, field, id)                                   \
    int32_t IMP_ISP_Tuning_Set##Name(IMPVI_NUM num, unsigned char *value)    \
    {                                                                        \
        return p3_bcsh_set(num, p3_bcsh_##field, (id), value);               \
    }                                                                        \
    int32_t IMP_ISP_Tuning_Get##Name(IMPVI_NUM num, unsigned char *value)    \
    {                                                                        \
        return p3_bcsh_get(num, p3_bcsh_##field, value);                     \
    }
#endif

P3_BCSH_ACCESSORS(Brightness, brightness, TISP_CID_BRIGHTNESS)
P3_BCSH_ACCESSORS(Contrast, contrast, TISP_CID_CONTRAST)
P3_BCSH_ACCESSORS(Saturation, saturation, TISP_CID_SATURATION)
P3_BCSH_ACCESSORS(Sharpness, sharpness, TISP_CID_SHARPNESS)

int32_t IMP_ISP_Tuning_SetBcshHue(IMPVI_NUM num, unsigned char *value)
{
    int result;

    if (num != IMPVI_MAIN || !value)
        return -1;
    result = p3_tuning_pointer(num, 0, TISP_CID_BCSH_HUE, value);
    if (result == 0)
        p3_controls.hue = *value;
    return result;
}

int32_t IMP_ISP_Tuning_GetBcshHue(IMPVI_NUM num, unsigned char *value)
{
    int result;

    if (num != IMPVI_MAIN || !value)
        return -1;
    result = p3_tuning_pointer(num, 1, TISP_CID_BCSH_HUE, value);
    if (result == 0)
        p3_controls.hue = *value;
    *value = p3_controls.hue;
    return result;
}

#if !defined(PLATFORM_T41)
/* T40 IMP_ISP_Tuning_SetMask / GetMask (vendor 1.3.1 libimp disassembly).
 * IMPISPMASKAttr is 3 channels x 4 blocks of 24 bytes = 288 bytes: mask_en at
 * +0, mask_type at +12 (0 RGB, 1 YUV), the colour at +16 (r g b) and +19
 * (y u v).  Set: num < 4 (-4), mask (-9); for every enabled RGB block it
 * computes the YUV bytes from the RGB ones (IMP_ISP_Tuning_DumpMask, BT.601
 * full range, written to +19..+21, the RGB bytes stay), keeps a 288-byte copy
 * per input and sends the pointer tuning ioctl (control 0x08000074, payload =
 * the caller's struct; -6 if it fails).  Get: ioctl get into the caller's
 * struct, then mask_type and the RGB bytes of every block are restored from
 * the Set copy (the driver hands back its own YUV form).  The DumpMask
 * constants are not readable in the disassembly; the operation order is the
 * T31 one (isp_mask_rgb2yuv.h).  Device-untested. */
#define P3_MASK_SIZE 288
#define P3_MASK_BLOCK 24
_Static_assert(sizeof(IMPISPMASKAttr) == P3_MASK_SIZE, "mask attr is 288 bytes");
_Static_assert(sizeof(IMPISPMASKAttr) == 12 * P3_MASK_BLOCK, "12 blocks of 24");
_Static_assert(offsetof(IMPISP_MASK_BLOCK_PAR, mask_type) == 12, "type at 12");
_Static_assert(offsetof(IMPISP_MASK_BLOCK_PAR, mask_value) == 16, "colour at 16");
static unsigned char p3_mask_cache[4][P3_MASK_SIZE];

int32_t IMP_ISP_Tuning_SetMask(IMPVI_NUM num, IMPISPMASKAttr *mask)
{
    unsigned char *bytes = (unsigned char *)mask;
    unsigned block;

    if ((unsigned)num >= 4U)
        return -4;
    if (!mask)
        return -9;
    for (block = 0; block < 12; block++) {
        unsigned char *b = bytes + block * P3_MASK_BLOCK;
        uint32_t type;

        memcpy(&type, b + 12, sizeof(type));
        if (b[0] && type == 0) {
            unsigned char c[3] = { b[16], b[17], b[18] };

            isp_mask_rgb_to_yuv(c);
            memcpy(b + 19, c, 3);
        }
    }
    memcpy(p3_mask_cache[num], bytes, P3_MASK_SIZE);
    if (p3_tuning_pointer(num, 0, TISP_CID_MASK, mask) != 0)
        return -6;
    return 0;
}

int32_t IMP_ISP_Tuning_GetMask(IMPVI_NUM num, IMPISPMASKAttr *mask)
{
    unsigned char *bytes = (unsigned char *)mask;
    unsigned block;

    if ((unsigned)num >= 4U)
        return -4;
    if (!mask)
        return -9;
    if (p3_tuning_pointer(num, 1, TISP_CID_MASK, mask) != 0)
        return -6;
    for (block = 0; block < 12; block++) {
        unsigned char *b = bytes + block * P3_MASK_BLOCK;
        const unsigned char *c = p3_mask_cache[num] + block * P3_MASK_BLOCK;

        memcpy(b + 12, c + 12, 4);
        memcpy(b + 16, c + 16, 3);
    }
    return 0;
}
#endif

#if defined(PLATFORM_T41)
int32_t IMP_ISP_Tuning_SetSensorFPS(IMPVI_NUM num, IMPISPSensorFps *fps)
#else
int32_t IMP_ISP_Tuning_SetSensorFPS(IMPVI_NUM num, uint32_t *numerator,
                                    uint32_t *denominator)
#endif
{
    int result;
#if defined(PLATFORM_T41)
    uint32_t *numerator = fps ? &fps->num : NULL;
    uint32_t *denominator = fps ? &fps->den : NULL;
#endif

    if (num != IMPVI_MAIN || !numerator || !denominator || !*denominator ||
        *numerator > 0xffffU || *denominator > 0xffffU)
        return -1;
    result = p3_tuning_set(TISP_CID_SENSOR_FPS,
                           (int32_t)((*numerator << 16) | *denominator));
    if (result == 0) {
        p3_controls.fps_num = *numerator;
        p3_controls.fps_den = *denominator;
    }
    return result;
}

#if defined(PLATFORM_T41)
int32_t IMP_ISP_Tuning_GetSensorFPS(IMPVI_NUM num, IMPISPSensorFps *fps)
#else
int32_t IMP_ISP_Tuning_GetSensorFPS(IMPVI_NUM num, uint32_t *numerator,
                                    uint32_t *denominator)
#endif
{
    int32_t value = 0;
    int result;
#if defined(PLATFORM_T41)
    uint32_t *numerator = fps ? &fps->num : NULL;
    uint32_t *denominator = fps ? &fps->den : NULL;
#endif

    if (num != IMPVI_MAIN || !numerator || !denominator)
        return -1;
    result = p3_tuning_get(TISP_CID_SENSOR_FPS, &value);
    if (result == 0) {
        p3_controls.fps_num = ((uint32_t)value >> 16) & 0xffffU;
        p3_controls.fps_den = (uint32_t)value & 0xffffU;
    }
    *numerator = p3_controls.fps_num;
    *denominator = p3_controls.fps_den;
    return result;
}

#if defined(PLATFORM_T41)
int32_t IMP_ISP_Tuning_SetHVFLIP(IMPVI_NUM num, IMPISPHVFLIPAttr *flip)
#else
int32_t IMP_ISP_Tuning_SetHVFLIP(IMPVI_NUM num, IMPISPHVFLIP *flip)
#endif
{
    int result;

    if (num != IMPVI_MAIN || !flip)
        return -1;
    result = p3_tuning_pointer(num, 0, TISP_CID_HVFLIP, flip);
    if (result == 0)
        p3_controls.flip = *flip;
    return result;
}

#if defined(PLATFORM_T41)
int32_t IMP_ISP_Tuning_GetHVFLIP(IMPVI_NUM num, IMPISPHVFLIPAttr *flip)
#else
int32_t IMP_ISP_Tuning_GetHVFlip(IMPVI_NUM num, IMPISPHVFLIP *flip)
#endif
{
    int result;

    if (num != IMPVI_MAIN || !flip)
        return -1;
    result = p3_tuning_pointer(num, 1, TISP_CID_HVFLIP, flip);
    if (result == 0)
        p3_controls.flip = *flip;
    *flip = p3_controls.flip;
    return result;
}

#if defined(PLATFORM_T41)
/*
 * Vendor T41 libimp 1.2.6 IMP_ISP_Tuning_Set/GetMaskBlock (0x5dc78 /
 * 0x5e294): for an enabled block with mask_type RGB the colour is turned
 * into YUV in the caller's struct (IMP_ISP_Tuning_DumpMask: BT.601 full
 * range, same doubles as the T31 SetMask, written to mask_value.ayuv,
 * argb untouched); the 24-byte struct is copied into a per-sensor cache
 * and passed to the kernel (0x08000074), which only uses window and
 * ayuv.  GetMaskBlock asks the kernel (window, ayuv; the stock kernel
 * always reports mask_en 0) and then restores mask_type and argb from
 * the cache, also when the ioctl failed.  The cache index is chx * pinum,
 * as in the vendor library (blocks with the same product share a slot).
 */
#include "../isp/isp_mask_rgb2yuv.h"

#define P3_MASK_CACHE_SLOTS 12
static IMPISPMaskBlockAttr p3_mask_cache[IMPVI_BUTT][P3_MASK_CACHE_SLOTS];

int32_t IMP_ISP_Tuning_SetMaskBlock(IMPVI_NUM num,
                                    IMPISPMaskBlockAttr *mask)
{
    unsigned int slot;

    if (num < IMPVI_MAIN || num >= IMPVI_BUTT || !mask)
        return -1;
    if (mask->mask_en && mask->mask_type == IMPISP_MASK_TYPE_RGB) {
        uint8_t c[3];

        c[0] = mask->mask_value.argb.r_value;
        c[1] = mask->mask_value.argb.g_value;
        c[2] = mask->mask_value.argb.b_value;
        isp_mask_rgb_to_yuv(c);
        mask->mask_value.ayuv.y_value = c[0];
        mask->mask_value.ayuv.u_value = c[1];
        mask->mask_value.ayuv.v_value = c[2];
    }
    slot = (unsigned int)mask->chx * mask->pinum;
    if (slot < P3_MASK_CACHE_SLOTS)
        p3_mask_cache[num][slot] = *mask;
    return p3_tuning_pointer(num, 0, TISP_CID_MASK_BLOCK, mask);
}

int32_t IMP_ISP_Tuning_GetMaskBlock(IMPVI_NUM num,
                                    IMPISPMaskBlockAttr *mask)
{
    unsigned int slot;
    int result;

    if (num < IMPVI_MAIN || num >= IMPVI_BUTT || !mask)
        return -1;
    result = p3_tuning_pointer(num, 1, TISP_CID_MASK_BLOCK, mask);
    slot = (unsigned int)mask->chx * mask->pinum;
    if (slot < P3_MASK_CACHE_SLOTS) {
        mask->mask_type = p3_mask_cache[num][slot].mask_type;
        mask->mask_value.argb = p3_mask_cache[num][slot].mask_value.argb;
    }
    return result;
}

int32_t IMP_ISP_Tuning_SetScalerLv(IMPVI_NUM num, IMPISPScalerLvAttr *attr)
{
    return p3_tuning_pointer(num, 0, TISP_CID_SCALER_LV, attr);
}

int IMP_ISP_Tuning_Awb_SetRgbCoefft(IMPVI_NUM num, IMPISPCoefftWb *attr)
{
    return p3_tuning_pointer(num, 0, TISP_CID_AWB_RGB_COEFFT, attr);
}

int IMP_ISP_Tuning_Awb_GetRgbCoefft(IMPVI_NUM num, IMPISPCoefftWb *attr)
{
    return p3_tuning_pointer(num, 1, TISP_CID_AWB_RGB_COEFFT, attr);
}

/*
 * Vendor T41 libimp (1.1.0/1.2.5 control numbering, the one the driver and
 * the controls above use): each of these is a plain pointer pass-through of
 * the public structure on the tuning ioctl.  open-tx-isp implements
 *   AeScenceAttr  - AeTargetComp (0..255, 128 neutral) on the open AE,
 *   AeExprInfo    - AeMaxIntegrationTime / AeMaxAGain caps (SET),
 *   Module_Ratio  - SINTER (2D NR) and TEMPER (3D NR) strength;
 * and answers -EOPNOTSUPP for HLC/BLC, manual exposure, DRC/DPC/defog
 * ratios, CCM, gamma, CSC, module bypass and WDR output mode, so callers
 * see the failure instead of an acknowledged no-op.  AutoZoom (60 bytes,
 * en/left/top/width/height[3]) goes 1:1 to the kernel as in the vendor
 * library; open-tx-isp claude/t41-zoom-mask stores it and applies it at
 * the next output start unless the output may be reprogrammed live
 * (open-tx-isp driver/t41/README.md "MSCA zoom, mask and scaler level").
 */
#define TISP_CID_CSC 0x08000096

#define P3_T41_POINTER_PAIR(Name, Type, id)                                   \
    int32_t IMP_ISP_Tuning_Set##Name(IMPVI_NUM num, Type *attr)               \
    {                                                                         \
        return p3_tuning_pointer(num, 0, (id), attr);                         \
    }                                                                         \
    int32_t IMP_ISP_Tuning_Get##Name(IMPVI_NUM num, Type *attr)               \
    {                                                                         \
        return p3_tuning_pointer(num, 1, (id), attr);                         \
    }

P3_T41_POINTER_PAIR(AeScenceAttr, IMPISPAEScenceAttr, TISP_CID_AE_SCENCE)
P3_T41_POINTER_PAIR(Module_Ratio, IMPISPModuleRatioAttr, TISP_CID_MODULE_RATIO)
P3_T41_POINTER_PAIR(GammaAttr, IMPISPGammaAttr, TISP_CID_GAMMA)
P3_T41_POINTER_PAIR(ModuleControl, IMPISPModuleCtl, TISP_CID_MODULE_CONTROL)
P3_T41_POINTER_PAIR(AutoZoom, IMPISPAutoZoom, TISP_CID_AUTOZOOM)
P3_T41_POINTER_PAIR(WdrOutputMode, IMPISPWdrOutputMode, TISP_CID_WDR_OUTPUT_MODE)

/*
 * CCM and CSC do not travel as the public structures: the kernel (stock
 * tisp_s_ccm_attr / tisp_csc_api_set) takes
 *   CCM, 40 bytes: u8 ManualEn, u8 SatEn, 2 pad, 9 x s32 Q16 (65536 = 1.0)
 *   CSC, 92 bytes: s32 version (0..3 = ColorGamut, 6 = user), 9 x s32 RGB->YUV
 *     Q16, bytes {Y offset, UV offset, Y min, Y max}, bytes {UV min, UV max,
 *     0, 0}, 9 x s32 YUV->RGB Q16 (the inverse), then the two byte words again.
 */
static int32_t p3_q16(float value)
{
    float scaled = value * 65536.0f;

    if (scaled >= 2147483520.0f)
        return 0x7fffff00;
    if (scaled <= -2147483520.0f)
        return -0x7fffff00;
    return (int32_t)(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
}

static void p3_put32(unsigned char *p, int32_t v)
{
    uint32_t u = (uint32_t)v;

    p[0] = (unsigned char)u;
    p[1] = (unsigned char)(u >> 8);
    p[2] = (unsigned char)(u >> 16);
    p[3] = (unsigned char)(u >> 24);
}

static int32_t p3_get32(const unsigned char *p)
{
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

void OpenIMP_P3_CCMToWire(const IMPISPCCMAttr *attr, unsigned char wire[40])
{
    unsigned int i;

    memset(wire, 0, 40);
    wire[0] = (unsigned char)(attr->ManualEn ? 1 : 0);
    wire[1] = (unsigned char)(attr->SatEn ? 1 : 0);
    for (i = 0; i < 9; i++)
        p3_put32(wire + 4 + i * 4, p3_q16(attr->ColorMatrix[i]));
}

void OpenIMP_P3_CCMFromWire(const unsigned char wire[40], IMPISPCCMAttr *attr)
{
    unsigned int i;

    attr->ManualEn = wire[0] ? 1 : 0;
    attr->SatEn = wire[1] ? 1 : 0;
    for (i = 0; i < 9; i++)
        attr->ColorMatrix[i] = (float)p3_get32(wire + 4 + i * 4) / 65536.0f;
}

/* Inverse of a 3x3 matrix; 0 when singular. */
static int p3_invert3(const double m[9], double inv[9])
{
    double c0 = m[4] * m[8] - m[5] * m[7];
    double c1 = m[5] * m[6] - m[3] * m[8];
    double c2 = m[3] * m[7] - m[4] * m[6];
    double det = m[0] * c0 + m[1] * c1 + m[2] * c2;

    if (det > -1e-9 && det < 1e-9)
        return 0;
    inv[0] = c0 / det;
    inv[1] = (m[2] * m[7] - m[1] * m[8]) / det;
    inv[2] = (m[1] * m[5] - m[2] * m[4]) / det;
    inv[3] = c1 / det;
    inv[4] = (m[0] * m[8] - m[2] * m[6]) / det;
    inv[5] = (m[2] * m[3] - m[0] * m[5]) / det;
    inv[6] = c2 / det;
    inv[7] = (m[1] * m[6] - m[0] * m[7]) / det;
    inv[8] = (m[0] * m[4] - m[1] * m[3]) / det;
    return 1;
}

/* 0, or -1 when the request cannot be expressed (bad gamut, singular user
 * matrix). */
int OpenIMP_P3_CSCToWire(const IMPISPCSCAttr *attr, unsigned char wire[92])
{
    double fwd[9], inv[9];
    unsigned int i;

    memset(wire, 0, 92);
    if ((unsigned int)attr->ColorGamut > 4U)
        return -1;
    if (attr->ColorGamut != 4) {
        p3_put32(wire, (int32_t)attr->ColorGamut);
        return 0;
    }
    for (i = 0; i < 9; i++) {
        float f = attr->Matrix.CscCoef[i];

        if (!(f > -4.0f && f < 4.0f))
            return -1;
        fwd[i] = (double)p3_q16(f) / 65536.0;
    }
    if (!p3_invert3(fwd, inv))
        return -1;
    p3_put32(wire, 6);
    for (i = 0; i < 9; i++) {
        double r = inv[i];

        if (!(r > -4.0 && r < 4.0))
            return -1;
        p3_put32(wire + 4 + i * 4, p3_q16(attr->Matrix.CscCoef[i]));
        p3_put32(wire + 48 + i * 4,
                 (int32_t)(r * 65536.0 + (r >= 0.0 ? 0.5 : -0.5)));
    }
    wire[40] = attr->Matrix.CscOffset[0];	/* Y offset */
    wire[41] = attr->Matrix.CscOffset[1];	/* UV offset */
    wire[42] = attr->Matrix.CscClip[0];		/* Y min */
    wire[43] = attr->Matrix.CscClip[1];		/* Y max */
    wire[44] = attr->Matrix.CscClip[2];		/* UV min */
    wire[45] = attr->Matrix.CscClip[3];		/* UV max */
    memcpy(wire + 84, wire + 40, 4);
    memcpy(wire + 88, wire + 44, 4);
    return 0;
}

void OpenIMP_P3_CSCFromWire(const unsigned char wire[92], IMPISPCSCAttr *attr)
{
    int32_t version = p3_get32(wire);
    unsigned int i;

    memset(attr, 0, sizeof(*attr));
    attr->ColorGamut = (version >= 0 && version <= 3) ? (IMPISPCSCColorGamut)version
                                                       : IMP_ISP_CG_USER;
    for (i = 0; i < 9; i++)
        attr->Matrix.CscCoef[i] = (float)p3_get32(wire + 4 + i * 4) / 65536.0f;
    attr->Matrix.CscOffset[0] = wire[40];
    attr->Matrix.CscOffset[1] = wire[41];
    attr->Matrix.CscClip[0] = wire[42];
    attr->Matrix.CscClip[1] = wire[43];
    attr->Matrix.CscClip[2] = wire[44];
    attr->Matrix.CscClip[3] = wire[45];
}

int32_t IMP_ISP_Tuning_SetCCMAttr(IMPVI_NUM num, IMPISPCCMAttr *ccm)
{
    unsigned char wire[40];

    if (!ccm)
        return -1;
    OpenIMP_P3_CCMToWire(ccm, wire);
    return p3_tuning_pointer(num, 0, TISP_CID_CCM, wire);
}

int32_t IMP_ISP_Tuning_GetCCMAttr(IMPVI_NUM num, IMPISPCCMAttr *ccm)
{
    unsigned char wire[40];
    int result;

    if (!ccm)
        return -1;
    memset(wire, 0, sizeof(wire));
    result = p3_tuning_pointer(num, 1, TISP_CID_CCM, wire);
    if (result == 0)
        OpenIMP_P3_CCMFromWire(wire, ccm);
    return result;
}

int32_t IMP_ISP_Tuning_SetISPCSCAttr(IMPVI_NUM num, IMPISPCSCAttr *csc)
{
    unsigned char wire[92];

    if (!csc || OpenIMP_P3_CSCToWire(csc, wire))
        return -1;
    return p3_tuning_pointer(num, 0, TISP_CID_CSC, wire);
}

int32_t IMP_ISP_Tuning_GetISPCSCAttr(IMPVI_NUM num, IMPISPCSCAttr *csc)
{
    unsigned char wire[92];
    int result;

    if (!csc)
        return -1;
    memset(wire, 0, sizeof(wire));
    result = p3_tuning_pointer(num, 1, TISP_CID_CSC, wire);
    if (result == 0)
        OpenIMP_P3_CSCFromWire(wire, csc);
    return result;
}
#endif

#if !defined(PLATFORM_T41)
/*
 * T40 1.3.1 (vendor libimp disassembly): the pointer pass-throughs hand the
 * public structure to the tuning ioctl unchanged.  Control numbers equal the
 * T41 ones except Awb_Get/SetRgbCoefft (0x9b here, 0x98 on T41).  The vendor
 * kernel (tx_isp_t40) owns the semantics; the calls fail with its error.
 */
#define TISP_CID_AWB_RGB_COEFFT_T40 0x0800009b

#define P3_T40_POINTER_PAIR(Name, Type, id)                                   \
    int32_t IMP_ISP_Tuning_Set##Name(IMPVI_NUM num, Type *attr)               \
    {                                                                         \
        return p3_tuning_pointer(num, 0, (id), attr);                         \
    }                                                                         \
    int32_t IMP_ISP_Tuning_Get##Name(IMPVI_NUM num, Type *attr)               \
    {                                                                         \
        return p3_tuning_pointer(num, 1, (id), attr);                         \
    }

P3_T40_POINTER_PAIR(AeScenceAttr, IMPISPAEScenceAttr, TISP_CID_AE_SCENCE)
P3_T40_POINTER_PAIR(Module_Ratio, IMPISPModuleRatioAttr, TISP_CID_MODULE_RATIO)
P3_T40_POINTER_PAIR(ModuleControl, IMPISPModuleCtl, TISP_CID_MODULE_CONTROL)
P3_T40_POINTER_PAIR(AutoZoom, IMPISPAutoZoom, TISP_CID_AUTOZOOM)
P3_T40_POINTER_PAIR(WdrOutputMode, IMPISPWdrOutputMode,
                    TISP_CID_WDR_OUTPUT_MODE)

int IMP_ISP_Tuning_Awb_SetRgbCoefft(IMPVI_NUM num, IMPISPCoefftWb *attr)
{
    return p3_tuning_pointer(num, 0, TISP_CID_AWB_RGB_COEFFT_T40, attr);
}

int IMP_ISP_Tuning_Awb_GetRgbCoefft(IMPVI_NUM num, IMPISPCoefftWb *attr)
{
    return p3_tuning_pointer(num, 1, TISP_CID_AWB_RGB_COEFFT_T40, attr);
}

/* Vendor: the scaler level goes out as the public structure (IMP_ISP_ name
 * without Tuning_ on T40). */
int32_t IMP_ISP_SetScalerLv(IMPVI_NUM num, IMPISPScalerLvAttr *attr)
{
    return p3_tuning_pointer(num, 0, TISP_CID_SCALER_LV, attr);
}

/* Vendor Set: the curve type must be below IMP_ISP_GAMMA_CURVE_BUTT (4) or
 * the call fails before the ioctl; the 129-entry table is passed as is. */
int32_t IMP_ISP_Tuning_SetGammaAttr(IMPVI_NUM num, IMPISPGammaAttr *attr)
{
    if (!attr || (uint32_t)attr->Curve_type >= (uint32_t)IMP_ISP_GAMMA_CURVE_BUTT)
        return -1;
    return p3_tuning_pointer(num, 0, TISP_CID_GAMMA, attr);
}

int32_t IMP_ISP_Tuning_GetGammaAttr(IMPVI_NUM num, IMPISPGammaAttr *attr)
{
    return p3_tuning_pointer(num, 1, TISP_CID_GAMMA, attr);
}

/*
 * CCM: unlike the pass-throughs the vendor library converts.  The ioctl
 * payload is 40 bytes {int8 manual, int8 saturation, 2 pad, u32 matrix[9]};
 * each coefficient is a sign + 13 bit value in 1/1024 steps:
 *   c < -1e-5:  ((-trunc(|c| * 1024)) & 0x1fff) | 0x2000
 *   otherwise:   trunc(c * 1024) & 0x1fff
 * and back (Get): bit 13 set = -((-w) & 0x1fff) / 1024, else w / 1024.
 * (The vendor Set also rewrites the caller's negative coefficients as
 * positive values in place; that side effect is not reproduced.)
 */
struct p3_ccm_wire {
    int8_t manual;
    int8_t saturation;
    uint8_t pad[2];
    uint32_t matrix[9];
};

static uint32_t p3_ccm_encode(float c)
{
    if ((double)c < -1e-5) {
        int32_t m = (int32_t)(-c * 1024.0f);

        return ((uint32_t)(-m) & 0x1fffU) | 0x2000U;
    }
    return (uint32_t)(int32_t)(c * 1024.0f) & 0x1fffU;
}

static float p3_ccm_decode(uint32_t w)
{
    if (w & 0x2000U)
        return -(float)(int32_t)((0U - w) & 0x1fffU) * (1.0f / 1024.0f);
    return (float)(int32_t)w * (1.0f / 1024.0f);
}

int32_t IMP_ISP_Tuning_SetCCMAttr(IMPVI_NUM num, IMPISPCCMAttr *attr)
{
    struct p3_ccm_wire wire;
    unsigned int i;

    if (!attr)
        return -1;
    memset(&wire, 0, sizeof(wire));
    wire.manual = (int8_t)attr->ManualEn;
    wire.saturation = (int8_t)attr->SatEn;
    for (i = 0; i < 9U; i++)
        wire.matrix[i] = p3_ccm_encode(attr->ColorMatrix[i]);
    return p3_tuning_pointer(num, 0, TISP_CID_CCM, &wire);
}

int32_t IMP_ISP_Tuning_GetCCMAttr(IMPVI_NUM num, IMPISPCCMAttr *attr)
{
    struct p3_ccm_wire wire;
    unsigned int i;
    int result;

    if (!attr)
        return -1;
    memset(&wire, 0, sizeof(wire));
    result = p3_tuning_pointer(num, 1, TISP_CID_CCM, &wire);
    if (result != 0)
        return result;
    attr->ManualEn = (IMPISPTuningOpsMode)wire.manual;
    attr->SatEn = (IMPISPTuningOpsMode)wire.saturation;
    for (i = 0; i < 9U; i++)
        attr->ColorMatrix[i] = p3_ccm_decode(wire.matrix[i]);
    return 0;
}
#endif

int32_t IMP_ISP_Tuning_SetISPRunningMode(IMPVI_NUM num,
                                         IMPISPRunningMode *mode)
{
    int result;

    if (num != IMPVI_MAIN || !mode)
        return -1;
    result = p3_tuning_pointer(num, 0, TISP_CID_RUNNING_MODE, mode);
    if (result == 0)
        p3_controls.running_mode = *mode;
    return result;
}

int32_t IMP_ISP_Tuning_GetISPRunningMode(IMPVI_NUM num,
                                         IMPISPRunningMode *mode)
{
    int result;

    if (num != IMPVI_MAIN || !mode)
        return -1;
    result = p3_tuning_pointer(num, 1, TISP_CID_RUNNING_MODE, mode);
    if (result == 0)
        p3_controls.running_mode = *mode;
    *mode = p3_controls.running_mode;
    return result;
}

int32_t IMP_ISP_Tuning_SetISPBypass(IMPVI_NUM num,
                                    IMPISPTuningOpsMode *enable)
{
    if (num != IMPVI_MAIN || !enable)
        return -1;
    /*
     * OEM T40 stores this policy flag in libimp; it does not issue a tuning
     * ioctl until a separate custom-ISP configuration is supplied.
     */
    return 0;
}

int32_t IMP_ISP_Tuning_SetAntiFlickerAttr(IMPVI_NUM num,
                                          IMPISPAntiflickerAttr *attribute)
{
#if defined(PLATFORM_T41)
    int result;
#endif

    if (num != IMPVI_MAIN || !attribute)
        return -1;
#if defined(PLATFORM_T41)
    result = p3_tuning_pointer(num, 0, TISP_CID_ANTIFLICKER, attribute);
    if (result != 0)
        return result;
#endif
    p3_controls.antiflicker = *attribute;
    return 0;
}

int32_t IMP_ISP_Tuning_GetAntiFlickerAttr(IMPVI_NUM num,
                                          IMPISPAntiflickerAttr *attribute)
{
#if defined(PLATFORM_T41)
    int result;
#endif

    if (num != IMPVI_MAIN || !attribute)
        return -1;
#if defined(PLATFORM_T41)
    result = p3_tuning_pointer(num, 1, TISP_CID_ANTIFLICKER, attribute);
    if (result != 0)
        return result;
    p3_controls.antiflicker = *attribute;
#endif
    *attribute = p3_controls.antiflicker;
    return 0;
}

int32_t IMP_ISP_SetDefaultBinPath(IMPVI_NUM num, char *path)
{
    size_t length;
    int result;

    if (num != IMPVI_MAIN || !path)
        return -1;
    length = strlen(path);
    if (length >= sizeof(p3_controls.bin_path))
        return -1;
    result = OpenIMP_P1_SetDefaultBinPath(num, path);
    if (result == 0)
        memcpy(p3_controls.bin_path, path, length + 1U);
    return result;
}

/* Vendor IMP_ISP_GetDefaultBinPath returns the absolute path of the bin file the
 * running ISP was started with (T40 1.3.1/en imp_isp.h:369-396).  The old body
 * answered 0 for every call and copied the setter cache, so a caller that never
 * set a path got a successful return plus an empty string, and a caller that
 * follows the header's own example - char path[64] - could receive the 128-byte
 * setter cache.  Report the path OpenIMP really forwarded to the kernel
 * (OpenIMP_P1_GetDefaultBinPath, bounded by the vendor's 64-byte example) and
 * fail when libimp was never given one. */
int32_t IMP_ISP_GetDefaultBinPath(IMPVI_NUM num, char *path)
{
    if (num != IMPVI_MAIN || path == NULL)
        return -1;
    if (OpenIMP_P1_GetDefaultBinPath(num, path, 64U) == 0)
        return 0;

    /* No path was ever accepted by IMP_ISP_SetDefaultBinPath.  The default the
     * kernel then uses is a driver property (T40 module parameter
     * t40_tuning_bin_path, T41 "/etc/sensor/<sensor>-t41.bin") and unknown to
     * libimp, so an empty string must not be reported as success. */
    path[0] = '\0';
    return -1;
}

#if !defined(PLATFORM_T40)   /* T40: src/t40/openimp_t40_isp_osd.c */
int IMP_ISP_Tuning_SetOsdPoolSize(int size)
{
    return size >= 0 ? 0 : -1;
}
#endif

#if !defined(PLATFORM_T41) && !defined(PLATFORM_T40)   /* src/t23/openimp_t23_osd.c */
int IMP_OSD_SetPoolSize(int size)
{
    return size >= 0 ? 0 : -1;
}
#endif

static uint32_t p3_register_access(uint32_t address, const uint32_t *write_value,
                                   int *ok)
{
    long page_size = sysconf(_SC_PAGESIZE);
    uint32_t page_mask;
    uint32_t page_address;
    uint32_t page_offset;
    volatile uint32_t *reg;
    void *mapping;
    uint32_t result = 0;
    int fd;

    *ok = 0;
    if (page_size <= 0 || (address & 3U))
        return 0;
    page_mask = (uint32_t)page_size - 1U;
    page_address = address & ~page_mask;
    page_offset = address & page_mask;
    fd = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
    if (fd < 0)
        return 0;
    mapping = mmap(NULL, (size_t)page_size, PROT_READ | PROT_WRITE,
                   MAP_SHARED, fd, (off_t)page_address);
    if (mapping == MAP_FAILED) {
        close(fd);
        return 0;
    }
    reg = (volatile uint32_t *)((unsigned char *)mapping + page_offset);
    if (write_value) {
        *reg = *write_value;
        __sync_synchronize();
    }
    result = *reg;
    *ok = 1;
    munmap(mapping, (size_t)page_size);
    close(fd);
    return result;
}

uint32_t IMP_System_ReadReg32(uint32_t address)
{
    int ok;
    return p3_register_access(address, NULL, &ok);
}

int32_t IMP_System_WriteReg32(uint32_t address, uint32_t value)
{
    int ok;
    (void)p3_register_access(address, &value, &ok);
    return ok ? 0 : -1;
}
