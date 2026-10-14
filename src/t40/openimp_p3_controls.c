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
extern int OpenIMP_P1_TuningWaitIOCtl(uint32_t command, void *argument);
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

#if !defined(PLATFORM_T41)
/* T40 IMP_ISP_WDR_OPEN (vendor 1.3.1 0x8be48), before the first
 * AddSensor: the ISP device must be open and attr set (-8 for either);
 * rmode LINEAR sends {sensor number (byte), imode} with the ISP-device
 * ioctl 0xc0045432 (-1 if it fails; the vendor then keeps imode in its
 * sensor record, which OpenIMP has no use for); rmode WDR switches WDR on
 * like IMP_ISP_WDR_ENABLE, whose result the vendor ignores (0); any other
 * rmode -9.  OpenIMP sends the WDR switch on the open device directly,
 * because its WDR_ENABLE needs a running sensor. */
extern int OpenIMP_P1_IspIOCtlOpened(uint32_t command, void *argument);
#define TISP_IOCTL_WDR_OPEN_LINEAR 0xc0045432U
struct p3_wdr_open_req {
    uint8_t sensor;
    uint8_t pad[3];
    int32_t imode;
};
_Static_assert(sizeof(struct p3_wdr_open_req) == 8, "WDR_OPEN request is 8 bytes");

int32_t IMP_ISP_WDR_OPEN(IMPVI_NUM num, IMPISPWdrOpenAttr *attr)
{
    if (!attr)
        return -8;
    if (attr->rmode == IMPISP_TYPE_RUN_LINEAR) {
        struct p3_wdr_open_req req;

        memset(&req, 0, sizeof(req));
        req.sensor = (uint8_t)num;
        req.imode = (int32_t)attr->imode;
        if (OpenIMP_P1_IspIOCtlOpened(TISP_IOCTL_WDR_OPEN_LINEAR, &req) != 0)
            return errno == ENODEV ? -8 : -1;
        return 0;
    }
    if (attr->rmode == IMPISP_TYPE_RUN_WDR) {
        int32_t zero = 0;

        pthread_mutex_lock(&p3_wdr_lock);
        if (OpenIMP_P1_IspIOCtlOpened(TISP_IOCTL_SET_WDR_ENABLE, &zero) == 0)
            p3_wdr_mode = IMPISP_TUNING_OPS_MODE_ENABLE;
        pthread_mutex_unlock(&p3_wdr_lock);
        return 0;
    }
    return -9;
}
#endif

#if !defined(PLATFORM_T41)
/* T40 IMP_ISP_Bypass_Bind (vendor 1.3.1 0x8bacc): num < 4 (-4), ISP
 * device open (-8), attr (-9); a 12-byte request, per bypass entry i
 * {num * 3 + i, enable, vc_index, dma_index}, goes to the ISP-device ioctl
 * 0xc0045431 (-1 if it fails).  Header: after IMP_ISP_Open; not together
 * with IMP_ISP_Tuning_SetISPBypass. */
#define TISP_IOCTL_BYPASS_BIND 0xc0045431U

int32_t IMP_ISP_Bypass_Bind(IMPVI_NUM num, IMPISPBypassAttr *attr)
{
    uint8_t req[12];
    unsigned i;

    if ((unsigned)num >= 4U)
        return -4;
    if (!attr)
        return -9;
    for (i = 0; i < 3U; i++) {
        req[i * 4U] = (uint8_t)((unsigned)num * 3U + i);
        req[i * 4U + 1U] = attr->bypass[i].enable;
        req[i * 4U + 2U] = attr->bypass[i].vc_index;
        req[i * 4U + 3U] = attr->bypass[i].dma_index;
    }
    if (OpenIMP_P1_IspIOCtlOpened(TISP_IOCTL_BYPASS_BIND, req) != 0)
        return errno == ENODEV ? -8 : -1;
    return 0;
}
#endif

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

/* IMP_ISP_Tuning_GetAwbStatistics: vendor T40 1.3.1 (libimp 0x7e2c0) and T41
 * 1.2.6 (0x55e78) build {num, 1, 0x8000011, awb_statis} on the stack and send
 * it with the pointer tuning ioctl (0xc0105436 / 0xc0105435); the stock
 * driver's tisp_g_awb_statis_attr -> tisp_awb_get_zone copies three 900-byte
 * planes (15x15 u32 each: zone means sum / count of the raw, pre-white-balance
 * R, G and B AWB statistics; T41 1.2.x state +60096/+60996/+61896, T40 the
 * same copy) to the 2700-byte block.  The open tx-isp-t41 fills the planes
 * the same way (t41_awb_long_frame) and answers -EAGAIN unless they come
 * from a frame of the running stream within the last second; the vendor T40
 * driver copies its state regardless (all zero before the first statistics
 * frame).  Contract of this call: 0 only with a block that holds statistics
 * (the driver's error, or an all-zero block, is -1 with errno EAGAIN), so a
 * consumer (timps night colour trigger) never takes zeros or a stale copy for
 * a measurement. */
_Static_assert(sizeof(IMPISPStatisZone) == 900, "IMPISPStatisZone is 15x15 u32");
_Static_assert(sizeof(IMPISPAWBStatisInfo) == 2700, "IMPISPAWBStatisInfo is 3 planes");
_Static_assert(offsetof(IMPISPAWBStatisInfo, awb_g) == 900 &&
               offsetof(IMPISPAWBStatisInfo, awb_b) == 1800, "plane order R, G, B");

/* Runtime capability marker for streamers (weak extern or dlsym): present and 1
 * only in a libimp whose GetAwbStatistics was verified against the vendor
 * semantics above and returns 0 only with real statistics (T40 on the vendor
 * kernel driver, T41 with the open tx-isp-t41 of claude/awbstats or later). */
__attribute__((visibility("default"))) const int OpenIMP_Cap_AwbStatistics = 1;

static int p3_block_is_zero(const void *block, size_t size)
{
    const unsigned char *p = block;
    size_t i;

    for (i = 0; i < size; i++)
        if (p[i])
            return 0;
    return 1;
}

int32_t IMP_ISP_Tuning_GetAwbStatistics(IMPVI_NUM num,
                                         IMPISPAWBStatisInfo *awb_statis)
{
    int result = p3_tuning_pointer(num, 1, TISP_CID_AWB_STATISTICS, awb_statis);

    if (result == 0 && p3_block_is_zero(awb_statis, sizeof(*awb_statis))) {
        errno = EAGAIN;
        return -1;
    }
    return result;
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
    IMPISPTuningOpsMode isp_bypass;
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

#if defined(PLATFORM_T41)
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
#else
/* Vendor T40 1.3.1 IMP_ISP_Tuning_SetBcshHue (0x75b7c) / GetBcshHue
 * (0x7603c): Set checks num < 4 (-4), the pointer (-9) and that the ISP
 * tuning device is running (-5), sends the byte pointer with control
 * 0x08000081 (-6 on failure) and caches the byte at tuning device +20+num;
 * Get only returns that cached byte and 0 (no ioctl; the old OpenIMP getter
 * asked the driver, which only echoed the pointer target).  The kernel maps
 * 0..255 onto a 0..120 degree hue angle (tisp_bcsh_s_hue). */
static unsigned char p3_hue_cache[4] = {128, 128, 128, 128};

int32_t IMP_ISP_Tuning_SetBcshHue(IMPVI_NUM num, unsigned char *value)
{
    if ((unsigned)num >= 4U)
        return -4;
    if (!value)
        return -9;
    if (!OpenIMP_P1_TuningEnabled())
        return -5;
    if (p3_tuning_pointer(num, 0, TISP_CID_BCSH_HUE, value) != 0)
        return -6;
    p3_hue_cache[num] = *value;
    return 0;
}

int32_t IMP_ISP_Tuning_GetBcshHue(IMPVI_NUM num, unsigned char *value)
{
    if ((unsigned)num >= 4U || !value)
        return -1;     /* the vendor would fault; stay safe */
    *value = p3_hue_cache[num];
    return 0;
}
#endif

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

#if !defined(PLATFORM_T41)
/* T40 IMP_ISP_StartNightMode (vendor 1.3.1 0x89834): num < 4 (-4), the ISP
 * device must be open (-8), then the ISP-device ioctl 0xc004542e with a
 * pointer to the sensor number (-1 if it fails).  The header: call it after
 * IMP_ISP_Open and before IMP_ISP_AddSensor, so the sensor starts in night
 * mode; the stock kernel keeps the flag. */
extern int OpenIMP_P1_IspIOCtlOpened(uint32_t command, void *argument);
#define TISP_IOCTL_START_NIGHT_MODE 0xc004542eU

int32_t IMP_ISP_StartNightMode(IMPVI_NUM num)
{
    int32_t sensor = (int32_t)num;

    if ((unsigned)num >= 4U)
        return -4;
    if (OpenIMP_P1_IspIOCtlOpened(TISP_IOCTL_START_NIGHT_MODE, &sensor) != 0)
        return errno == ENODEV ? -8 : -1;
    return 0;
}
#endif

#if !defined(PLATFORM_T41)
/* T40 IMP_ISP_SetDrawAttr / GetDrawAttr (vendor 1.3.1 libimp 0x83478 /
 * 0x82f34): IMPISPDrawAttr is 2 ISP output channels x 16 units of 28 bytes
 * (type at +0, color_type at +4, enable at +8, colour at +18: r g b, then
 * y u v at +21).  Set: num < 4 (-4), attr (-9), tuning device running (-5);
 * for every unit of type line/range/window that is enabled with an RGB
 * colour the YUV bytes are computed in the caller's struct
 * (IMP_ISP_Tuning_DumpMask), the 896 bytes are kept per input and the
 * pointer goes to the kernel (control 0x080000a0, -6 on failure).  Get:
 * state check, ioctl get into the caller's struct (-6), then color_type and
 * the RGB bytes of every unit come back from the Set copy (the driver hands
 * back its own YUV form).  Same pattern as SetMask above. */
#define P3_DRAW_SIZE 896
#define P3_DRAW_UNIT 28
#define TISP_CID_DRAW 0x080000a0
_Static_assert(sizeof(IMPISPDrawAttr) == P3_DRAW_SIZE, "draw attr is 896 bytes");
_Static_assert(sizeof(IMPISPDrawUnitAttr) == P3_DRAW_UNIT, "draw unit is 28 bytes");
_Static_assert(offsetof(IMPISPDrawUnitAttr, draw_cfg) + offsetof(IMPISPDrawWindAttr, wind_color) == 18,
               "draw colour at 18");
static unsigned char p3_draw_cache[4][P3_DRAW_SIZE];

int32_t IMP_ISP_SetDrawAttr(IMPVI_NUM num, IMPISPDrawAttr *attr)
{
    unsigned char *bytes = (unsigned char *)attr;
    unsigned unit;

    if ((unsigned)num >= 4U)
        return -4;
    if (!attr)
        return -9;
    if (!OpenIMP_P1_TuningEnabled())
        return -5;
    for (unit = 0; unit < 32; unit++) {
        unsigned char *u = bytes + unit * P3_DRAW_UNIT;
        uint32_t type, color_type;

        memcpy(&type, u, sizeof(type));
        memcpy(&color_type, u + 4, sizeof(color_type));
        if (type <= IMP_ISP_DRAW_WIND && u[8] && color_type == 0) {
            unsigned char c[3] = { u[18], u[19], u[20] };

            isp_mask_rgb_to_yuv(c);
            memcpy(u + 21, c, 3);
        }
    }
    memcpy(p3_draw_cache[num], bytes, P3_DRAW_SIZE);
    if (p3_tuning_pointer(num, 0, TISP_CID_DRAW, attr) != 0)
        return -6;
    return 0;
}

int32_t IMP_ISP_GetDrawAttr(IMPVI_NUM num, IMPISPDrawAttr *attr)
{
    unsigned char *bytes = (unsigned char *)attr;
    unsigned unit;

    if ((unsigned)num >= 4U)
        return -4;
    if (!attr)
        return -9;
    if (!OpenIMP_P1_TuningEnabled())
        return -5;
    if (p3_tuning_pointer(num, 1, TISP_CID_DRAW, attr) != 0)
        return -6;
    for (unit = 0; unit < 32; unit++) {
        unsigned char *u = bytes + unit * P3_DRAW_UNIT;
        const unsigned char *c = p3_draw_cache[num] + unit * P3_DRAW_UNIT;

        memcpy(u + 4, c + 4, 4);
        memcpy(u + 18, c + 18, 3);
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
{
    int result;

    if (num != IMPVI_MAIN || !flip)
        return -1;
    result = p3_tuning_pointer(num, 0, TISP_CID_HVFLIP, flip);
    if (result == 0)
        p3_controls.flip = *flip;
    return result;
}

int32_t IMP_ISP_Tuning_GetHVFLIP(IMPVI_NUM num, IMPISPHVFLIPAttr *flip)
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
#else
/* Vendor T40 1.3.1 IMP_ISP_Tuning_SetHVFLIP (0x76080): num < 4 (-4), the
 * pointer (-9), tuning device running (-5), mode < IMPISP_FLIP_MODE_BUTT
 * (9, else -4); the request carries the pointer to the mode (control
 * 0x08000073, -6 on failure) and the mode is cached per input.  The kernel
 * (tx_isp_hvflip_s_attr) applies modes 1..3 in the MSCA and 4..8 through
 * the sensor.  GetHVFlip (0x765d8) never asks the driver: the stock kernel
 * has no getter for this control (the old OpenIMP getter returned -1); it
 * returns the cached mode (0 before any Set) and 0. */
static IMPISPHVFLIP p3_flip_cache[4];

int32_t IMP_ISP_Tuning_SetHVFLIP(IMPVI_NUM num, IMPISPHVFLIP *flip)
{
    if ((unsigned)num >= 4U)
        return -4;
    if (!flip)
        return -9;
    if (!OpenIMP_P1_TuningEnabled())
        return -5;
    if ((uint32_t)*flip >= (uint32_t)IMPISP_FLIP_MODE_BUTT)
        return -4;
    if (p3_tuning_pointer(num, 0, TISP_CID_HVFLIP, flip) != 0)
        return -6;
    p3_flip_cache[num] = *flip;
    return 0;
}

int32_t IMP_ISP_Tuning_GetHVFlip(IMPVI_NUM num, IMPISPHVFLIP *flip)
{
    if ((unsigned)num >= 4U)
        return -4;
    if (!flip)
        return -9;
    *flip = p3_flip_cache[num];
    return 0;
}
#endif

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

/*
 * More T40 1.3.1 pointer pass-throughs (vendor libimp disassembly, the same
 * ioctl request as above; each vendor function checks the sensor number,
 * the ISP device, the pointer and "tuning running" and then hands the public
 * structure to the kernel unchanged):
 *   AfStatistics 0x8000031 (StatisConfig and WaitFrameDone: below, shared
 *   with T41),
 *   AFMetricesInfo 0x8000034, FaceAe 0x8000035, FaceAwb 0x8000014,
 *   HLDCAttr 0x8000097, FixedContraster 0x8000098 (set only), AeExpList
 *   0x8000099, AeSpeed 0x800009a.
 */
#define TISP_CID_AF_STATIS          0x08000031
#define TISP_CID_AF_METRICS         0x08000034
#define TISP_CID_FACE_AE            0x08000035
#define TISP_CID_FACE_AWB           0x08000014
#define TISP_CID_HLDC               0x08000097
#define TISP_CID_FIXED_CONTRAST     0x08000098
#define TISP_CID_AE_EXP_LIST        0x08000099
#define TISP_CID_AE_SPEED           0x0800009a

P3_T40_POINTER_PAIR(FaceAe, IMPISPFaceAttr, TISP_CID_FACE_AE)
P3_T40_POINTER_PAIR(FaceAwb, IMPISPFaceAttr, TISP_CID_FACE_AWB)
P3_T40_POINTER_PAIR(HLDCAttr, IMPISPHLDCAttr, TISP_CID_HLDC)
P3_T40_POINTER_PAIR(AeExpList, IMPISPAeExpListAttr, TISP_CID_AE_EXP_LIST)
P3_T40_POINTER_PAIR(AeSpeed, IMPISPAeSpeedAttr, TISP_CID_AE_SPEED)

int32_t IMP_ISP_Tuning_GetAfStatistics(IMPVI_NUM num, IMPISPAFStatisInfo *info)
{
    return p3_tuning_pointer(num, 1, TISP_CID_AF_STATIS, info);
}

int32_t IMP_ISP_Tuning_GetAFMetricesInfo(IMPVI_NUM num,
                                         IMPISPAFMetricsInfo *metric)
{
    return p3_tuning_pointer(num, 1, TISP_CID_AF_METRICS, metric);
}

int32_t IMP_ISP_Tuning_SetFixedContraster(IMPVI_NUM num,
                                          IMPISPFixedContrastAttr *attr)
{
    return p3_tuning_pointer(num, 0, TISP_CID_FIXED_CONTRAST, attr);
}

/* Vendor IMP_ISP_SetOSDAttr / GetOSDAttr (T40 name without "Tuning"):
 * pointer pass-through of the public structure, control 0x80000a1. */
#define TISP_CID_OSD_ATTR 0x080000a1

int32_t IMP_ISP_SetOSDAttr(IMPVI_NUM num, IMPISPOSDAttr *attr)
{
    return p3_tuning_pointer(num, 0, TISP_CID_OSD_ATTR, attr);
}

int32_t IMP_ISP_GetOSDAttr(IMPVI_NUM num, IMPISPOSDAttr *attr)
{
    return p3_tuning_pointer(num, 1, TISP_CID_OSD_ATTR, attr);
}

#if !defined(PLATFORM_T41)
/* T40 IMP_OSD_SetRgnAttr_ISP / GetRgnAttr_ISP (vendor 1.3.1 0xbe6e4 /
 * 0xbea24): the IMPOSDRgnAttr of an ISP-drawn region carries an
 * IMPOSDIspDraw at +48: IMPISPDrawAttr (+48), IMPISPOSDAttr (+944) and
 * IMPISPMASKAttr (+1352).  Set: sensornum < 2, attr, type
 * OSD_REG_ISP_PIC/LINE_RECT/COVER (10..12) else -1; bosdshow (0: ISP OSD
 * only, 1: also IPU OSD) is kept for GetRgnAttr_ISP; LINE_RECT goes to
 * IMP_ISP_SetDrawAttr, COVER to IMP_ISP_Tuning_SetMask, PIC is refused
 * with -1 (vendor: pictures go through IMP_OSD_SetRgnAttr_PicISP); any
 * failure is -1.  Get: sensornum < 2, attr and pbosdshow, then
 * GetDrawAttr, GetOSDAttr and GetMask into the three parts (-1 if one
 * fails).  The vendor also refuses the calls before the OSD module exists
 * (IMP_System_Init); OpenIMP has no such module state. */
#define P3_OSD_ISP_DRAW 48
#define P3_OSD_ISP_PIC 944
#define P3_OSD_ISP_COVER 1352
_Static_assert(P3_OSD_ISP_DRAW + sizeof(IMPISPDrawAttr) == P3_OSD_ISP_PIC,
               "IMPISPOSDAttr follows the draw attr");
/* IMPISPOSDAttr holds picture pointers: 408 bytes on the 32-bit target */
_Static_assert(sizeof(void *) != 4 ||
               P3_OSD_ISP_PIC + sizeof(IMPISPOSDAttr) == P3_OSD_ISP_COVER,
               "IMPISPMASKAttr follows the OSD attr");
static int p3_osd_isp_show;
static pthread_mutex_t p3_osd_isp_lock = PTHREAD_MUTEX_INITIALIZER;

int IMP_OSD_SetRgnAttr_ISP(int sensornum, void *prAttr, int bosdshow)
{
    unsigned char *attr = prAttr;
    uint32_t type;
    int result = -1;

    if ((unsigned)sensornum >= 2U || !attr)
        return -1;
    memcpy(&type, attr, sizeof(type));
    if (type < 10U || type > 12U)
        return -1;
    pthread_mutex_lock(&p3_osd_isp_lock);
    p3_osd_isp_show = bosdshow != 0;
    if (type == 11U)
        result = IMP_ISP_SetDrawAttr((IMPVI_NUM)sensornum,
                                     (IMPISPDrawAttr *)(attr + P3_OSD_ISP_DRAW)) < 0 ? -1 : 0;
    else if (type == 12U)
        result = IMP_ISP_Tuning_SetMask((IMPVI_NUM)sensornum,
                                        (IMPISPMASKAttr *)(attr + P3_OSD_ISP_COVER)) < 0 ? -1 : 0;
    pthread_mutex_unlock(&p3_osd_isp_lock);
    return result;
}

int IMP_OSD_GetRgnAttr_ISP(int sensornum, void *prAttr, int *pbosdshow)
{
    unsigned char *attr = prAttr;

    if ((unsigned)sensornum >= 2U || !attr || !pbosdshow)
        return -1;
    pthread_mutex_lock(&p3_osd_isp_lock);
    *pbosdshow = p3_osd_isp_show;
    pthread_mutex_unlock(&p3_osd_isp_lock);
    if (IMP_ISP_GetDrawAttr((IMPVI_NUM)sensornum,
                            (IMPISPDrawAttr *)(attr + P3_OSD_ISP_DRAW)) < 0)
        return -1;
    if (IMP_ISP_GetOSDAttr((IMPVI_NUM)sensornum,
                           (IMPISPOSDAttr *)(attr + P3_OSD_ISP_PIC)) < 0)
        return -1;
    if (IMP_ISP_Tuning_GetMask((IMPVI_NUM)sensornum,
                               (IMPISPMASKAttr *)(attr + P3_OSD_ISP_COVER)) < 0)
        return -1;
    return 0;
}
#endif

/* Vendor IMP_ISP_SetPreDqtime (T40 1.3.1 0x8aa04): the caller's pointer goes
 * to the ISP node as ioctl 0xc0045430 (TISP_VIDIOC_SET_PREDQTIME); sensor
 * number >= 4 is -4, no ISP device -8, NULL -9, a failing ioctl -6. */
#define TISP_IOCTL_SET_PREDQTIME 0xc0045430U

int32_t IMP_ISP_SetPreDqtime(IMPVI_NUM num, uint32_t *dqtime)
{
    if ((uint32_t)num >= 4U)
        return -4;
    if (!dqtime)
        return -9;           /* vendor: ISP device check (-8) comes first */
    if (OpenIMP_P1_IspIOCtl(TISP_IOCTL_SET_PREDQTIME, dqtime) != 0)
        return errno == ENODEV ? -8 : -6;
    return 0;
}

/*
 * CSC: the T40 wire block differs from the T41 one above (vendor T40 1.3.1
 * Set/GetISPCSCAttr).  Control 0x8000096, the block is
 *   u32 gamut (0..4, 4 = user), 9 x u32 matrix, 6 bytes {offset[2], clip[4]}.
 * Set: gamut >= 5 fails before the ioctl; for the user gamut every
 * coefficient is (int)(c * 1024 + 0.5) & 0x3ff, otherwise only the gamut
 * word is defined.  Get: for the user gamut the words are read as signed
 * ints times 1/1024 and the coefficients 3, 4, 7 and 8 are negated (as the
 * vendor library does), the six bytes are copied; any other gamut zeroes
 * bytes 4..47 of the structure.
 */
#define TISP_CID_CSC_T40 0x08000096

int32_t IMP_ISP_Tuning_SetISPCSCAttr(IMPVI_NUM num, IMPISPCSCAttr *csc)
{
    unsigned char wire[64];
    unsigned int i;

    if (!csc || (uint32_t)csc->ColorGamut >= 5U)
        return -1;
    memset(wire, 0, sizeof(wire));
    memcpy(wire, &csc->ColorGamut, 4);
    if (csc->ColorGamut == IMP_ISP_CG_USER) {
        for (i = 0; i < 9U; i++) {
            uint32_t w = (uint32_t)(int32_t)((double)(csc->Matrix.CscCoef[i] *
                                                      1024.0f) + 0.5) & 0x3ffU;

            memcpy(wire + 4 + 4 * i, &w, 4);
        }
        memcpy(wire + 40, csc->Matrix.CscOffset, 2);
        memcpy(wire + 42, csc->Matrix.CscClip, 4);
    }
    return p3_tuning_pointer(num, 0, TISP_CID_CSC_T40, wire);
}

int32_t IMP_ISP_Tuning_GetISPCSCAttr(IMPVI_NUM num, IMPISPCSCAttr *csc)
{
    unsigned char wire[64];
    unsigned int i;
    int32_t gamut;
    int32_t result;

    if (!csc)
        return -1;
    memset(wire, 0, sizeof(wire));
    result = p3_tuning_pointer(num, 1, TISP_CID_CSC_T40, wire);
    if (result != 0)
        return result;
    memcpy(&gamut, wire, 4);
    if (gamut == IMP_ISP_CG_USER) {
        for (i = 0; i < 9U; i++) {
            int32_t w;

            memcpy(&w, wire + 4 + 4 * i, 4);
            csc->Matrix.CscCoef[i] = (float)w * (1.0f / 1024.0f);
        }
        memcpy(csc->Matrix.CscOffset, wire + 40, 2);
        memcpy(csc->Matrix.CscClip, wire + 42, 4);
        csc->Matrix.CscCoef[3] = -csc->Matrix.CscCoef[3];
        csc->Matrix.CscCoef[4] = -csc->Matrix.CscCoef[4];
        csc->Matrix.CscCoef[7] = -csc->Matrix.CscCoef[7];
        csc->Matrix.CscCoef[8] = -csc->Matrix.CscCoef[8];
    } else {
        memset((unsigned char *)csc + 4, 0, 44);
    }
    memcpy(&csc->ColorGamut, &gamut, 4);
    return 0;
}

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

#if defined(PLATFORM_T41)
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
#else
/* Vendor T40 1.3.1 IMP_ISP_Tuning_SetISPRunningMode (0x7686c): num < 4
 * (-4), the pointer (-9), mode < IMPISP_RUNNING_MODE_BUTT (3, else -4); the
 * set request carries the POINTER to the mode (control 0x08000071) and the
 * ioctl result is returned as is.  GetISPRunningMode (0x76cbc) sends the
 * get request without a payload and the kernel answers with the mode IN the
 * payload word (the old OpenIMP getter passed a pointer, the kernel
 * overwrote it with the mode and the caller's variable stayed untouched,
 * so it read back garbage).  The vendor additionally re-arms its
 * frame-source "change wait" counter after a set, which only drops the
 * first frames after a switch; not reproduced. */
int32_t IMP_ISP_Tuning_SetISPRunningMode(IMPVI_NUM num,
                                         IMPISPRunningMode *mode)
{
    int result;

    if ((unsigned)num >= 4U)
        return -4;
    if (!mode)
        return -9;
    if ((uint32_t)*mode >= (uint32_t)IMPISP_RUNNING_MODE_BUTT)
        return -4;
    result = p3_tuning_pointer(num, 0, TISP_CID_RUNNING_MODE, mode);
    if (result == 0)
        p3_controls.running_mode = *mode;
    return result;
}

int32_t IMP_ISP_Tuning_GetISPRunningMode(IMPVI_NUM num,
                                         IMPISPRunningMode *mode)
{
    int32_t value = 0;

    if ((unsigned)num >= 4U)
        return -4;
    if (!mode)
        return -9;
    if (!OpenIMP_P1_TuningEnabled())
        return -5;
    if (p3_tuning_scalar(num, 1, TISP_CID_RUNNING_MODE, &value) != 0)
        return -6;
    *mode = (IMPISPRunningMode)value;
    p3_controls.running_mode = *mode;
    return 0;
}
#endif

int32_t IMP_ISP_Tuning_SetISPBypass(IMPVI_NUM num,
                                    IMPISPTuningOpsMode *enable)
{
    if (num != IMPVI_MAIN || !enable)
        return -1;
    /*
     * OEM T40 stores this policy flag in libimp; it does not issue a tuning
     * ioctl until a separate custom-ISP configuration is supplied.  The
     * vendor GetISPBypass reads the stored word back (0 before any Set).
     */
    p3_controls.isp_bypass = *enable;
    return 0;
}

#if !defined(PLATFORM_T41)   /* the T41 vendor name is IMP_ISP_GetISPBypass */
int32_t IMP_ISP_Tuning_GetISPBypass(IMPVI_NUM num,
                                    IMPISPTuningOpsMode *enable)
{
    if (num != IMPVI_MAIN || !enable)
        return -1;
    *enable = p3_controls.isp_bypass;
    return 0;
}
#endif

#if defined(PLATFORM_T41)
/*
 * Vendor T41 1.2.6 IMP_ISP_SetISPBypass (0x47084) / IMP_ISP_GetISPBypass
 * (0x46c48), the T41 names of the bypass pair (the T40 pair above has
 * "Tuning" in the name).  Set: pointer NULL -4087, sensor number >= 2
 * -4084; with the ISP device open the 8-byte block {value, sensor} goes to
 * the ISP node as ioctl 0x80085418 (TISP_VIDIOC_BYPASS_SENSOR), a failing
 * ioctl is -4095 and the value is remembered; without an open ISP device the
 * vendor only remembers it.  Get returns the remembered value (0 before any
 * Set).  The open-tx-isp T41 driver does not serve the ioctl yet (it answers
 * -ENOTTY), so Set fails with -4095 there, as on a vendor library with an
 * unsupporting kernel.  OpenIMP does not reproduce the vendor's "ISP device
 * not open" error for Get.
 */
#define TISP_IOCTL_BYPASS_SENSOR 0x80085418U
static IMPISPTuningOpsMode p3_t41_bypass[2];

int32_t IMP_ISP_SetISPBypass(IMPVI_NUM num, IMPISPTuningOpsMode *enable)
{
    uint32_t request[2];

    if (!enable)
        return -4087;
    if ((uint32_t)num >= 2U)
        return -4084;
    request[0] = (uint32_t)*enable;
    request[1] = (uint32_t)num;
    if (OpenIMP_P1_IspIOCtl(TISP_IOCTL_BYPASS_SENSOR, request) != 0 &&
        errno != ENODEV)
        return -4095;
    p3_t41_bypass[num] = *enable;
    return 0;
}

int32_t IMP_ISP_GetISPBypass(IMPVI_NUM num, IMPISPTuningOpsMode *enable)
{
    if (!enable)
        return -4087;
    if ((uint32_t)num >= 2U)
        return -4084;
    *enable = p3_t41_bypass[num];
    return 0;
}
#endif

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

/*
 * T40 1.3.1 and T41 1.2.6 (vendor libimp disassembly of both, same control
 * numbers): StatisConfig 0x8000000 is a pointer pass-through of the 72-byte
 * IMPISPStatisConfig; WaitFrameDone 0x8000091 a get with a scratch block.
 */
#define TISP_CID_STATIS_CONFIG   0x08000000
#define TISP_CID_WAIT_FRAME_DONE 0x08000091

int32_t IMP_ISP_Tuning_SetStatisConfig(IMPVI_NUM num,
                                       IMPISPStatisConfig *statis_config)
{
    return p3_tuning_pointer(num, 0, TISP_CID_STATIS_CONFIG, statis_config);
}

int32_t IMP_ISP_Tuning_GetStatisConfig(IMPVI_NUM num,
                                       IMPISPStatisConfig *statis_config)
{
    return p3_tuning_pointer(num, 1, TISP_CID_STATIS_CONFIG, statis_config);
}

/* Vendor WaitFrameDone: a 24-byte scratch block whose first word is the
 * caller's timeout goes out as a get; on success the kernel's frame counter
 * (offset 8, 8 bytes) and IRQ type (offset 16) are copied back to the
 * caller's structure (offset 4 stays untouched).  The wait runs outside the
 * P1 lock; a timeout returns -1 with errno ETIMEDOUT (open T41 driver; the
 * vendor driver returns 0 and the caller compares cnt). */
int32_t IMP_ISP_Tuning_WaitFrameDone(IMPVI_NUM num, IMPISPWaitFrameAttr *attr)
{
    unsigned char scratch[24];
    P3TuningRequest request;
    int32_t result;

    if (!attr || num < IMPVI_MAIN || num >= IMPVI_BUTT)
        return -1;
    memset(scratch, 0, sizeof(scratch));
    memcpy(scratch, attr, 4);
    request.vinum = num;
    request.direction = 1;
    request.control = TISP_CID_WAIT_FRAME_DONE;
    request.payload = (uintptr_t)scratch;
    result = OpenIMP_P1_TuningWaitIOCtl(TISP_VIDIOC_DEFAULT_TUNING, &request);
    if (result == 0)
        memcpy((unsigned char *)attr + 8, scratch + 8, 12);
    return result;
}

#if defined(PLATFORM_T41)
/*
 * T41 1.2.6 tuning controls (vendor libimp 1.2.6 disassembly: each checks
 * the sensor number, the ISP device, the pointer and "tuning running" and
 * hands the public structure to the tuning ioctl unchanged).  The 1.2.0
 * headers this file is built with do not have the types; the local ones
 * below have the 1.2.6 layouts (checked by the static asserts).
 *   AwbCtTrendOffset 0x8000015, AwbConvergeStep 0x8000016,
 *   AeConvergeStep 0x8000028, GetAeEvList 0x8000038, GetAeAtList
 *   0x8000039, TmoCurve 0x8000063.
 * Not provided: GetAeBv (0x80000b3) and GetAEFlickerFlag (0x800003a); the
 * open driver does not keep the vendor-AE state they report.
 */
#define TISP_CID_AWB_CT_TREND  0x08000015
#define TISP_CID_AWB_CONVERGE  0x08000016
#define TISP_CID_AE_CONVERGE   0x08000028
#define TISP_CID_AE_EV_LIST    0x08000038
#define TISP_CID_AE_AT_LIST    0x08000039
#define TISP_CID_TMO_CURVE     0x08000063

typedef struct { uint16_t ae_step[4]; } P3AeConvergeStep;
typedef struct { uint32_t awb_step[5]; } P3AwbConvergeStep;
typedef struct {
    uint16_t thres_hm;
    uint16_t thres_lm;
    IMPISPAWBGain gain_h;
    IMPISPAWBGain gain_m;
    IMPISPAWBGain gain_l;
} P3AwbCtTrendOffset;
typedef struct {
    IMPISPTuningOpsMode enable;
    uint64_t v[15];
} P3AeList;
typedef struct {
    uint8_t transfer_flag;
    uint8_t id;
    uint16_t value[201];
} P3TmoCurve;

_Static_assert(sizeof(P3AeConvergeStep) == 8, "IMPISPAeConvergeStep is 8 bytes");
_Static_assert(sizeof(P3AwbConvergeStep) == 20, "IMPISPAwbConvergeStep is 20 bytes");
_Static_assert(sizeof(P3AwbCtTrendOffset) == 28, "IMPISPAwbCtTrendOffset is 28 bytes");
_Static_assert(sizeof(P3AeList) == 128, "IMPISPAeEvList/AtList are 128 bytes");
_Static_assert(sizeof(P3TmoCurve) == 404, "IMPISPTmoCurve is 404 bytes");

int32_t IMP_ISP_Tuning_SetAeConvergeStep(IMPVI_NUM num, P3AeConvergeStep *step);
int32_t IMP_ISP_Tuning_GetAeConvergeStep(IMPVI_NUM num, P3AeConvergeStep *step);
int32_t IMP_ISP_Tuning_SetAwbConvergeStep(IMPVI_NUM num, P3AwbConvergeStep *step);
int32_t IMP_ISP_Tuning_GetAwbConvergeStep(IMPVI_NUM num, P3AwbConvergeStep *step);
int32_t IMP_ISP_Tuning_SetAwbCtTrendOffset(IMPVI_NUM num, P3AwbCtTrendOffset *offset);
int32_t IMP_ISP_Tuning_GetAwbCtTrendOffset(IMPVI_NUM num, P3AwbCtTrendOffset *offset);
int32_t IMP_ISP_Tuning_GetAeEvList(IMPVI_NUM num, P3AeList *attr);
int32_t IMP_ISP_Tuning_GetAEEvList(IMPVI_NUM num, P3AeList *attr);
int32_t IMP_ISP_Tuning_GetAeAtList(IMPVI_NUM num, P3AeList *attr);
int32_t IMP_ISP_Tuning_SetTmoCurve(IMPVI_NUM num, P3TmoCurve *attr);
int32_t IMP_ISP_Tuning_GetTmoCurve(IMPVI_NUM num, P3TmoCurve *attr);
int32_t IMP_ISP_Tuning_GetHVFlip(IMPVI_NUM num, IMPISPHVFLIPAttr *flip);

#define P3_T41_POINTER_PAIR(Name, Type, id)                                   \
    int32_t IMP_ISP_Tuning_Set##Name(IMPVI_NUM num, Type *attr)               \
    {                                                                         \
        return p3_tuning_pointer(num, 0, (id), attr);                         \
    }                                                                         \
    int32_t IMP_ISP_Tuning_Get##Name(IMPVI_NUM num, Type *attr)               \
    {                                                                         \
        return p3_tuning_pointer(num, 1, (id), attr);                         \
    }

P3_T41_POINTER_PAIR(AeConvergeStep, P3AeConvergeStep, TISP_CID_AE_CONVERGE)
P3_T41_POINTER_PAIR(AwbConvergeStep, P3AwbConvergeStep, TISP_CID_AWB_CONVERGE)
P3_T41_POINTER_PAIR(AwbCtTrendOffset, P3AwbCtTrendOffset, TISP_CID_AWB_CT_TREND)
P3_T41_POINTER_PAIR(TmoCurve, P3TmoCurve, TISP_CID_TMO_CURVE)

int32_t IMP_ISP_Tuning_GetAeEvList(IMPVI_NUM num, P3AeList *attr)
{
    return p3_tuning_pointer(num, 1, TISP_CID_AE_EV_LIST, attr);
}

/* The 1.2.5/1.2.6 English header declares GetAEEvList; the library
 * exports GetAeEvList.  Both names, one function. */
int32_t IMP_ISP_Tuning_GetAEEvList(IMPVI_NUM num, P3AeList *attr)
{
    return IMP_ISP_Tuning_GetAeEvList(num, attr);
}

int32_t IMP_ISP_Tuning_GetAeAtList(IMPVI_NUM num, P3AeList *attr)
{
    return p3_tuning_pointer(num, 1, TISP_CID_AE_AT_LIST, attr);
}

/* T41 1.0.1 name of GetHVFLIP (same IMPISPHVFLIPAttr). */
int32_t IMP_ISP_Tuning_GetHVFlip(IMPVI_NUM num, IMPISPHVFLIPAttr *flip)
{
    return IMP_ISP_Tuning_GetHVFLIP(num, flip);
}
#endif
