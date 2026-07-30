/* P3 T40 control plane: ISP tuning and direct system-register access. */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <imp/imp_isp.h>

#define TISP_VIDIOC_G_CTRL 0xc008561bU
#define TISP_VIDIOC_S_CTRL 0xc008561cU
#define TISP_VIDIOC_TUNING 0xc00c56c6U

#define TISP_CID_BRIGHTNESS 0x980900
#define TISP_CID_CONTRAST 0x980901
#define TISP_CID_SATURATION 0x980902
#define TISP_CID_HFLIP 0x980914
#define TISP_CID_VFLIP 0x980915
#define TISP_CID_SHARPNESS 0x98091b
#define TISP_CID_SENSOR_FPS 0x080000e0
#define TISP_CID_RUNNING_MODE 0x080000e1
#define TISP_CID_BCSH_HUE 0x08000101
#define TISP_CID_ISP_PROCESS 0x08000164

typedef struct {
    int32_t id;
    int32_t value;
} P3V4L2Control;

typedef struct {
    int32_t command;
    int32_t subcommand;
    int32_t value;
} P3TuningValue;

extern int OpenIMP_P1_TuningIOCtl(uint32_t command, void *argument);

#define P3_T40_PARAM_DIR "/sys/module/tx_isp_t40_recovered/parameters/"
#define P3_AE_FRAME_INTERVAL 15U
#define P3_AE_TARGET_LUMA 105U
#define P3_AE_LUMA_HYSTERESIS 8U
#define P3_AE_MIN_IT_LINES 64U
#define P3_AE_DEFAULT_IT_LINES 1488U
#define P3_AE_MAX_IT_LINES 1919U
#define P3_AE_MAX_AGAIN_INDEX 25U
#define P3_SENSOR_FPS 30U
#define P3_SENSOR_FRAME_LINES 1920U

static const uint32_t p3_gc4653_again_log2[] = {
    0U,      14995U,  31177U,  47704U,  65535U,  81158U,  97241U,
    113239U, 131070U, 147006U, 162776U, 178774U, 196605U, 212541U,
    228311U, 244420U, 262140U, 278154U, 293912U, 309955U, 327675U,
    343689U, 359480U, 375518U, 393210U, 409243U,
};

static pthread_mutex_t p3_ae_lock = PTHREAD_MUTEX_INITIALIZER;

static struct {
    uint32_t frame_count;
    uint32_t luma;
    uint32_t luma_ema;
    uint32_t u_mean;
    uint32_t v_mean;
    uint32_t integration_lines;
    uint32_t again_index;
    uint32_t min_it_lines;
    uint32_t max_it_lines;
    uint32_t max_again_index;
    int automatic;
    int driver_ready;
} p3_ae = {
    .integration_lines = P3_AE_DEFAULT_IT_LINES,
    .min_it_lines = P3_AE_MIN_IT_LINES,
    .max_it_lines = P3_AE_MAX_IT_LINES,
    .max_again_index = P3_AE_MAX_AGAIN_INDEX,
    .automatic = 1,
};

static int p3_param_write_u32(const char *name, uint32_t value)
{
    char path[192];
    char text[32];
    int text_len;
    int fd;
    ssize_t written;

    if (!name)
        return -1;
    if (snprintf(path, sizeof(path), "%s%s", P3_T40_PARAM_DIR, name) >=
        (int)sizeof(path))
        return -1;
    text_len = snprintf(text, sizeof(text), "%u\n", value);
    if (text_len <= 0 || text_len >= (int)sizeof(text))
        return -1;
    fd = open(path, O_WRONLY);
    if (fd < 0)
        return -1;
    written = write(fd, text, (size_t)text_len);
    close(fd);
    return written == text_len ? 0 : -1;
}

static uint32_t p3_ae_lines_to_us(uint32_t lines)
{
    uint64_t scaled = (uint64_t)lines * 1000000ULL;

    return (uint32_t)((scaled + (P3_SENSOR_FPS * P3_SENSOR_FRAME_LINES / 2U)) /
                      (P3_SENSOR_FPS * P3_SENSOR_FRAME_LINES));
}

static uint32_t p3_ae_us_to_lines(uint32_t usec)
{
    uint64_t scaled = (uint64_t)usec * P3_SENSOR_FPS *
                      P3_SENSOR_FRAME_LINES;

    return (uint32_t)((scaled + 500000ULL) / 1000000ULL);
}

static uint32_t p3_ae_gain_log2(uint32_t index)
{
    if (index >= sizeof(p3_gc4653_again_log2) /
                     sizeof(p3_gc4653_again_log2[0]))
        index = (uint32_t)(sizeof(p3_gc4653_again_log2) /
                           sizeof(p3_gc4653_again_log2[0]) - 1U);
    return p3_gc4653_again_log2[index];
}

static uint32_t p3_ae_gain_index(uint32_t gain_log2)
{
    uint32_t index;
    uint32_t count = (uint32_t)(sizeof(p3_gc4653_again_log2) /
                                sizeof(p3_gc4653_again_log2[0]));

    for (index = 1; index < count; index++) {
        uint32_t midpoint =
            p3_gc4653_again_log2[index - 1U] +
            (p3_gc4653_again_log2[index] -
             p3_gc4653_again_log2[index - 1U]) / 2U;

        if (gain_log2 < midpoint)
            return index - 1U;
    }
    return count - 1U;
}

static int p3_ae_apply_locked(void)
{
    uint32_t packed =
        (p3_ae.again_index << 16) | (p3_ae.integration_lines & 0xffffU);
    uint32_t gain_log2 = p3_ae_gain_log2(p3_ae.again_index);
    uint32_t dns_ev = p3_ae.again_index * 49152U;
    int result;

    if (dns_ev > 491520U)
        dns_ev = 491520U;
    if (!p3_ae.driver_ready) {
        result = p3_param_write_u32("enable_ae_sensor_apply", 1U);
        if (result != 0)
            return result;
        (void)p3_param_write_u32("ae_sensor_apply_max_again_index",
                                 p3_ae.max_again_index);
        p3_ae.driver_ready = 1;
    }
    result = p3_param_write_u32("ae_sensor_apply_force_packed", packed);
    if (result == 0) {
        (void)p3_param_write_u32("dns_gain_ev", dns_ev);
        (void)p3_param_write_u32("lsc_lit_gain", gain_log2);
        (void)p3_param_write_u32("ccm_ev", (gain_log2 + 2240U) / 16U);
    }
    return result;
}

/*
 * The encoder consumes the completed NV12 frame and therefore has a stable
 * luma sample even while the recovered ISP's native AE statistics path is
 * inactive.  Keep the policy here, in libimp, and use the open driver's
 * sensor-apply ABI only as the final hardware transport.
 */
void OpenIMP_P3_FrameStats(uint32_t luma, uint32_t u_mean, uint32_t v_mean)
{
    uint32_t step;
    int changed = 0;

    pthread_mutex_lock(&p3_ae_lock);
    p3_ae.frame_count++;
    p3_ae.luma = luma;
    p3_ae.u_mean = u_mean;
    p3_ae.v_mean = v_mean;
    if (!p3_ae.luma_ema)
        p3_ae.luma_ema = luma;
    else
        p3_ae.luma_ema = (p3_ae.luma_ema * 3U + luma) / 4U;

    if ((p3_ae.frame_count % P3_AE_FRAME_INTERVAL) != 0U) {
        pthread_mutex_unlock(&p3_ae_lock);
        return;
    }

    if (!p3_ae.driver_ready)
        changed = 1;
    if (p3_ae.automatic &&
        p3_ae.luma_ema + P3_AE_LUMA_HYSTERESIS < P3_AE_TARGET_LUMA) {
        if (p3_ae.integration_lines < p3_ae.max_it_lines) {
            uint32_t next =
                p3_ae.integration_lines + p3_ae.integration_lines / 4U + 8U;

            p3_ae.integration_lines =
                next > p3_ae.max_it_lines ? p3_ae.max_it_lines : next;
            changed = 1;
        } else if (p3_ae.again_index < p3_ae.max_again_index) {
            step = p3_ae.luma_ema < P3_AE_TARGET_LUMA / 4U ? 4U :
                   p3_ae.luma_ema < P3_AE_TARGET_LUMA / 2U ? 2U : 1U;
            if (step > p3_ae.max_again_index - p3_ae.again_index)
                step = p3_ae.max_again_index - p3_ae.again_index;
            p3_ae.again_index += step;
            changed = 1;
        }
    } else if (p3_ae.automatic &&
               p3_ae.luma_ema > P3_AE_TARGET_LUMA +
                                    P3_AE_LUMA_HYSTERESIS) {
        if (p3_ae.again_index > 0U) {
            step = p3_ae.luma_ema > P3_AE_TARGET_LUMA * 2U ? 2U : 1U;
            if (step > p3_ae.again_index)
                step = p3_ae.again_index;
            p3_ae.again_index -= step;
            changed = 1;
        } else if (p3_ae.integration_lines > p3_ae.min_it_lines) {
            uint32_t next =
                p3_ae.integration_lines - p3_ae.integration_lines / 4U;

            p3_ae.integration_lines =
                next < p3_ae.min_it_lines ? p3_ae.min_it_lines : next;
            changed = 1;
        }
    }
    if (changed)
        (void)p3_ae_apply_locked();
    pthread_mutex_unlock(&p3_ae_lock);
}

int32_t IMP_ISP_Tuning_GetAeExprInfo(IMPVI_NUM num,
                                     IMPISPAEExprInfo *exprinfo)
{
    uint32_t gain_log2;
    uint32_t integration_us;

    if (num != IMPVI_MAIN || !exprinfo)
        return -1;
    pthread_mutex_lock(&p3_ae_lock);
    gain_log2 = p3_ae_gain_log2(p3_ae.again_index);
    integration_us = p3_ae_lines_to_us(p3_ae.integration_lines);
    memset(exprinfo, 0, sizeof(*exprinfo));
    exprinfo->AeIntegrationTimeUnit = ISP_CORE_EXPR_UNIT_US;
    exprinfo->AeMode = p3_ae.automatic ? IMPISP_TUNING_OPS_TYPE_AUTO :
                                        IMPISP_TUNING_OPS_TYPE_MANUAL;
    exprinfo->AeIntegrationTimeMode = exprinfo->AeMode;
    exprinfo->AeAGainManualMode = exprinfo->AeMode;
    exprinfo->AeIntegrationTime = integration_us;
    exprinfo->AeAGain = gain_log2;
    exprinfo->AeDGain = 1024U;
    exprinfo->AeIspDGain = 1024U;
    exprinfo->AeMinIntegrationTime = p3_ae_lines_to_us(p3_ae.min_it_lines);
    exprinfo->AeMaxIntegrationTime = p3_ae_lines_to_us(p3_ae.max_it_lines);
    exprinfo->AeMinAGain = 0U;
    exprinfo->AeMaxAGain = p3_ae_gain_log2(p3_ae.max_again_index);
    exprinfo->TotalGainDb = gain_log2;
    exprinfo->ExposureValue =
        (uint32_t)(((uint64_t)integration_us * (gain_log2 + 65536U)) >>
                   16);
    exprinfo->EVLog2 = gain_log2;
    pthread_mutex_unlock(&p3_ae_lock);
    return 0;
}

int32_t IMP_ISP_Tuning_SetAeExprInfo(IMPVI_NUM num,
                                     IMPISPAEExprInfo *exprinfo)
{
    uint32_t integration_lines;

    if (num != IMPVI_MAIN || !exprinfo)
        return -1;
    pthread_mutex_lock(&p3_ae_lock);
    if (exprinfo->AeMaxIntegrationTimeMode ==
        IMPISP_TUNING_OPS_TYPE_MANUAL) {
        integration_lines =
            exprinfo->AeIntegrationTimeUnit == ISP_CORE_EXPR_UNIT_LINE ?
            exprinfo->AeMaxIntegrationTime :
            p3_ae_us_to_lines(exprinfo->AeMaxIntegrationTime);
        if (integration_lines >= P3_AE_MIN_IT_LINES &&
            integration_lines <= P3_AE_MAX_IT_LINES)
            p3_ae.max_it_lines = integration_lines;
    }
    if (exprinfo->AeMaxAGainMode == IMPISP_TUNING_OPS_TYPE_MANUAL) {
        p3_ae.max_again_index = p3_ae_gain_index(exprinfo->AeMaxAGain);
        if (p3_ae.max_again_index > P3_AE_MAX_AGAIN_INDEX)
            p3_ae.max_again_index = P3_AE_MAX_AGAIN_INDEX;
    }
    p3_ae.automatic =
        exprinfo->AeMode != IMPISP_TUNING_OPS_TYPE_MANUAL;
    if (exprinfo->AeIntegrationTimeMode ==
        IMPISP_TUNING_OPS_TYPE_MANUAL) {
        integration_lines =
            exprinfo->AeIntegrationTimeUnit == ISP_CORE_EXPR_UNIT_LINE ?
            exprinfo->AeIntegrationTime :
            p3_ae_us_to_lines(exprinfo->AeIntegrationTime);
        if (integration_lines < p3_ae.min_it_lines)
            integration_lines = p3_ae.min_it_lines;
        if (integration_lines > p3_ae.max_it_lines)
            integration_lines = p3_ae.max_it_lines;
        p3_ae.integration_lines = integration_lines;
    }
    if (exprinfo->AeAGainManualMode == IMPISP_TUNING_OPS_TYPE_MANUAL) {
        p3_ae.again_index = p3_ae_gain_index(exprinfo->AeAGain);
        if (p3_ae.again_index > p3_ae.max_again_index)
            p3_ae.again_index = p3_ae.max_again_index;
    }
    integration_lines = (uint32_t)p3_ae_apply_locked();
    pthread_mutex_unlock(&p3_ae_lock);
    return (int32_t)integration_lines;
}

static struct {
    unsigned char brightness;
    unsigned char contrast;
    unsigned char saturation;
    unsigned char sharpness;
    unsigned char hue;
    uint32_t fps_num;
    uint32_t fps_den;
    IMPISPHVFLIP flip;
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

static int p3_v4l2_set(int32_t id, int32_t value)
{
    P3V4L2Control control = { id, value };
    return OpenIMP_P1_TuningIOCtl(TISP_VIDIOC_S_CTRL, &control);
}

static int p3_v4l2_get(int32_t id, int32_t *value)
{
    P3V4L2Control control = { id, -1 };
    int result;

    if (!value)
        return -1;
    result = OpenIMP_P1_TuningIOCtl(TISP_VIDIOC_G_CTRL, &control);
    if (result == 0)
        *value = control.value;
    return result;
}

static int p3_tuning_set(int32_t id, int32_t value)
{
    P3TuningValue request = { 0, id, value };
    return OpenIMP_P1_TuningIOCtl(TISP_VIDIOC_TUNING, &request);
}

static int p3_tuning_get(int32_t id, int32_t *value)
{
    P3TuningValue request = { 1, id, 0 };
    int result;

    if (!value)
        return -1;
    result = OpenIMP_P1_TuningIOCtl(TISP_VIDIOC_TUNING, &request);
    if (result == 0)
        *value = request.value;
    return result;
}

#define P3_BCSH_ACCESSORS(Name, field, id)                                    \
    int32_t IMP_ISP_Tuning_Set##Name(IMPVI_NUM num, unsigned char *value)     \
    {                                                                         \
        int result;                                                           \
        if (num != IMPVI_MAIN || !value)                                     \
            return -1;                                                        \
        result = p3_v4l2_set((id), *value);                                  \
        if (result == 0)                                                      \
            p3_controls.field = *value;                                       \
        return result;                                                        \
    }                                                                         \
    int32_t IMP_ISP_Tuning_Get##Name(IMPVI_NUM num, unsigned char *value)     \
    {                                                                         \
        int32_t current = 0;                                                  \
        int result;                                                           \
        if (num != IMPVI_MAIN || !value)                                     \
            return -1;                                                        \
        result = p3_v4l2_get((id), &current);                                \
        if (result == 0)                                                      \
            p3_controls.field = (unsigned char)current;                       \
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
    result = p3_tuning_set(TISP_CID_BCSH_HUE, *value);
    if (result == 0)
        p3_controls.hue = *value;
    return result;
}

int32_t IMP_ISP_Tuning_GetBcshHue(IMPVI_NUM num, unsigned char *value)
{
    int32_t current = 0;
    int result;

    if (num != IMPVI_MAIN || !value)
        return -1;
    result = p3_tuning_get(TISP_CID_BCSH_HUE, &current);
    if (result == 0)
        p3_controls.hue = (unsigned char)current;
    *value = p3_controls.hue;
    return result;
}

int32_t IMP_ISP_Tuning_SetSensorFPS(IMPVI_NUM num, uint32_t *numerator,
                                    uint32_t *denominator)
{
    int result;

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

int32_t IMP_ISP_Tuning_GetSensorFPS(IMPVI_NUM num, uint32_t *numerator,
                                    uint32_t *denominator)
{
    int32_t value = 0;
    int result;

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

int32_t IMP_ISP_Tuning_SetHVFLIP(IMPVI_NUM num, IMPISPHVFLIP *flip)
{
    int result;

    if (num != IMPVI_MAIN || !flip)
        return -1;
    result = p3_v4l2_set(TISP_CID_HFLIP, *flip & 1);
    if (result == 0)
        result = p3_v4l2_set(TISP_CID_VFLIP, (*flip >> 1) & 1);
    if (result == 0)
        p3_controls.flip = *flip;
    return result;
}

int32_t IMP_ISP_Tuning_GetHVFlip(IMPVI_NUM num, IMPISPHVFLIP *flip)
{
    int32_t horizontal = 0;
    int32_t vertical = 0;
    int result;

    if (num != IMPVI_MAIN || !flip)
        return -1;
    result = p3_v4l2_get(TISP_CID_HFLIP, &horizontal);
    if (result == 0)
        result = p3_v4l2_get(TISP_CID_VFLIP, &vertical);
    if (result == 0)
        p3_controls.flip = (IMPISPHVFLIP)((horizontal ? 1 : 0) |
                                           (vertical ? 2 : 0));
    *flip = p3_controls.flip;
    return result;
}

int32_t IMP_ISP_Tuning_SetISPRunningMode(IMPVI_NUM num,
                                         IMPISPRunningMode *mode)
{
    int result;

    if (num != IMPVI_MAIN || !mode)
        return -1;
    result = p3_tuning_set(TISP_CID_RUNNING_MODE, *mode);
    if (result == 0)
        p3_controls.running_mode = *mode;
    return result;
}

int32_t IMP_ISP_Tuning_GetISPRunningMode(IMPVI_NUM num,
                                         IMPISPRunningMode *mode)
{
    int32_t current = 0;
    int result;

    if (num != IMPVI_MAIN || !mode)
        return -1;
    result = p3_tuning_get(TISP_CID_RUNNING_MODE, &current);
    if (result == 0)
        p3_controls.running_mode = (IMPISPRunningMode)current;
    *mode = p3_controls.running_mode;
    return result;
}

int32_t IMP_ISP_Tuning_SetISPBypass(IMPVI_NUM num,
                                    IMPISPTuningOpsMode *enable)
{
    if (num != IMPVI_MAIN || !enable)
        return -1;
    return p3_tuning_set(TISP_CID_ISP_PROCESS, *enable);
}

int32_t IMP_ISP_Tuning_SetAntiFlickerAttr(IMPVI_NUM num,
                                          IMPISPAntiflickerAttr *attribute)
{
    if (num != IMPVI_MAIN || !attribute)
        return -1;
    p3_controls.antiflicker = *attribute;
    return 0;
}

int32_t IMP_ISP_Tuning_GetAntiFlickerAttr(IMPVI_NUM num,
                                          IMPISPAntiflickerAttr *attribute)
{
    if (num != IMPVI_MAIN || !attribute)
        return -1;
    *attribute = p3_controls.antiflicker;
    return 0;
}

int32_t IMP_ISP_SetDefaultBinPath(IMPVI_NUM num, char *path)
{
    if (num != IMPVI_MAIN || !path ||
        strlen(path) >= sizeof(p3_controls.bin_path))
        return -1;
    strcpy(p3_controls.bin_path, path);
    return 0;
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
