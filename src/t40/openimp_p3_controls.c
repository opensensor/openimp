/* P3 T40 control plane: ISP tuning and direct system-register access. */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <imp/imp_isp.h>

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
#if defined(PLATFORM_T41)
#define TISP_CID_MASK_BLOCK 0x08000074
#define TISP_CID_SCALER_LV 0x080000a6
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
extern int OpenIMP_P1_SetDefaultBinPath(IMPVI_NUM num, const char *path);

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
int32_t IMP_ISP_Tuning_SetMaskBlock(IMPVI_NUM num,
                                    IMPISPMaskBlockAttr *mask)
{
    return p3_tuning_pointer(num, 0, TISP_CID_MASK_BLOCK, mask);
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

int32_t IMP_ISP_GetDefaultBinPath(IMPVI_NUM num, char *path)
{
    if (num != IMPVI_MAIN || !path)
        return -1;
    strcpy(path, p3_controls.bin_path);
    return 0;
}

int IMP_ISP_Tuning_SetOsdPoolSize(int size)
{
    return size >= 0 ? 0 : -1;
}

int IMP_OSD_SetPoolSize(int size)
{
    return size >= 0 ? 0 : -1;
}

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
    fd = open("/dev/mem", O_RDWR | O_SYNC);
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
