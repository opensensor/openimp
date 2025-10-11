/**
 * IMP ISP Module Implementation (Stub)
 */

#include <stdio.h>
#include <string.h>
#include <imp/imp_isp.h>

#define LOG_ISP(fmt, ...) fprintf(stderr, "[IMP_ISP] " fmt "\n", ##__VA_ARGS__)

static int isp_opened = 0;
static int sensor_enabled = 0;
static int tuning_enabled = 0;

/* Core ISP Functions */

int IMP_ISP_Open(void) {
    LOG_ISP("Open");
    isp_opened = 1;
    return 0;
}

int IMP_ISP_Close(void) {
    LOG_ISP("Close");
    isp_opened = 0;
    return 0;
}

int IMP_ISP_AddSensor(IMPSensorInfo *pinfo) {
    if (pinfo == NULL) return -1;
    LOG_ISP("AddSensor: %s", pinfo->name);
    return 0;
}

int IMP_ISP_AddSensor_VI(IMPVI vi, IMPSensorInfo *pinfo) {
    if (pinfo == NULL) return -1;
    LOG_ISP("AddSensor_VI: vi=%d, sensor=%s", vi, pinfo->name);
    return 0;
}

int IMP_ISP_DelSensor(IMPSensorInfo *pinfo) {
    if (pinfo == NULL) return -1;
    LOG_ISP("DelSensor: %s", pinfo->name);
    return 0;
}

int IMP_ISP_DelSensor_VI(IMPVI vi, IMPSensorInfo *pinfo) {
    if (pinfo == NULL) return -1;
    LOG_ISP("DelSensor_VI: vi=%d, sensor=%s", vi, pinfo->name);
    return 0;
}

int IMP_ISP_EnableSensor(void) {
    LOG_ISP("EnableSensor");
    sensor_enabled = 1;
    return 0;
}

int IMP_ISP_EnableSensor_VI(IMPVI vi, IMPSensorInfo *pinfo) {
    if (pinfo == NULL) return -1;
    LOG_ISP("EnableSensor_VI: vi=%d, sensor=%s", vi, pinfo->name);
    sensor_enabled = 1;
    return 0;
}

int IMP_ISP_DisableSensor(void) {
    LOG_ISP("DisableSensor");
    sensor_enabled = 0;
    return 0;
}

int IMP_ISP_DisableSensor_VI(IMPVI vi) {
    LOG_ISP("DisableSensor_VI: vi=%d", vi);
    sensor_enabled = 0;
    return 0;
}

int IMP_ISP_EnableTuning(void) {
    LOG_ISP("EnableTuning");
    tuning_enabled = 1;
    return 0;
}

int IMP_ISP_DisableTuning(void) {
    LOG_ISP("DisableTuning");
    tuning_enabled = 0;
    return 0;
}

/* ISP Tuning Functions */

int IMP_ISP_Tuning_SetSensorFPS(uint32_t fps_num, uint32_t fps_den) {
    LOG_ISP("SetSensorFPS: %u/%u", fps_num, fps_den);
    return 0;
}

int IMP_ISP_Tuning_GetSensorFPS(uint32_t *fps_num, uint32_t *fps_den) {
    if (fps_num == NULL || fps_den == NULL) return -1;
    *fps_num = 25;
    *fps_den = 1;
    return 0;
}

int IMP_ISP_Tuning_SetAntiFlickerAttr(IMPISPAntiflickerAttr attr) {
    LOG_ISP("SetAntiFlickerAttr: %d", attr);
    return 0;
}

int IMP_ISP_Tuning_SetISPRunningMode(IMPISPRunningMode mode) {
    LOG_ISP("SetISPRunningMode: %d", mode);
    return 0;
}

int IMP_ISP_Tuning_GetISPRunningMode(IMPISPRunningMode *pmode) {
    if (pmode == NULL) return -1;
    *pmode = IMPISP_RUNNING_MODE_DAY;
    return 0;
}

int IMP_ISP_Tuning_SetISPBypass(IMPISPTuningOpsMode enable) {
    LOG_ISP("SetISPBypass: %d", enable);
    return 0;
}

int IMP_ISP_Tuning_SetISPHflip(IMPISPTuningOpsMode mode) {
    LOG_ISP("SetISPHflip: %d", mode);
    return 0;
}

int IMP_ISP_Tuning_SetISPVflip(IMPISPTuningOpsMode mode) {
    LOG_ISP("SetISPVflip: %d", mode);
    return 0;
}

int IMP_ISP_Tuning_SetBrightness(unsigned char bright) {
    LOG_ISP("SetBrightness: %u", bright);
    return 0;
}

int IMP_ISP_Tuning_SetContrast(unsigned char contrast) {
    LOG_ISP("SetContrast: %u", contrast);
    return 0;
}

int IMP_ISP_Tuning_SetSharpness(unsigned char sharpness) {
    LOG_ISP("SetSharpness: %u", sharpness);
    return 0;
}

int IMP_ISP_Tuning_SetSaturation(unsigned char sat) {
    LOG_ISP("SetSaturation: %u", sat);
    return 0;
}

int IMP_ISP_Tuning_SetAeComp(int comp) {
    LOG_ISP("SetAeComp: %d", comp);
    return 0;
}

int IMP_ISP_Tuning_SetMaxAgain(uint32_t gain) {
    LOG_ISP("SetMaxAgain: %u", gain);
    return 0;
}

int IMP_ISP_Tuning_SetMaxDgain(uint32_t gain) {
    LOG_ISP("SetMaxDgain: %u", gain);
    return 0;
}

int IMP_ISP_Tuning_SetBacklightComp(uint32_t strength) {
    LOG_ISP("SetBacklightComp: %u", strength);
    return 0;
}

int IMP_ISP_Tuning_SetDPC_Strength(uint32_t ratio) {
    LOG_ISP("SetDPC_Strength: %u", ratio);
    return 0;
}

int IMP_ISP_Tuning_SetDRC_Strength(uint32_t ratio) {
    LOG_ISP("SetDRC_Strength: %u", ratio);
    return 0;
}

int IMP_ISP_Tuning_SetHiLightDepress(uint32_t strength) {
    LOG_ISP("SetHiLightDepress: %u", strength);
    return 0;
}

int IMP_ISP_Tuning_SetTemperStrength(uint32_t ratio) {
    LOG_ISP("SetTemperStrength: %u", ratio);
    return 0;
}

int IMP_ISP_Tuning_SetSinterStrength(uint32_t ratio) {
    LOG_ISP("SetSinterStrength: %u", ratio);
    return 0;
}

int IMP_ISP_Tuning_SetBcshHue(unsigned char hue) {
    LOG_ISP("SetBcshHue: %u", hue);
    return 0;
}

int IMP_ISP_Tuning_SetDefog_Strength(uint32_t strength) {
    LOG_ISP("SetDefog_Strength: %u", strength);
    return 0;
}

int IMP_ISP_Tuning_SetWB(IMPISPWB *wb) {
    if (wb == NULL) return -1;
    LOG_ISP("SetWB: mode=%d, rgain=%u, bgain=%u", wb->mode, wb->rgain, wb->bgain);
    return 0;
}

int IMP_ISP_Tuning_GetWB(IMPISPWB *wb) {
    if (wb == NULL) return -1;
    wb->mode = IMPISP_WB_MODE_AUTO;
    wb->rgain = 256;
    wb->bgain = 256;
    return 0;
}

