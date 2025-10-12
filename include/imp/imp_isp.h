/**
 * IMP ISP (Image Signal Processor) Module
 * 
 * ISP control, sensor management, and image tuning
 */

#ifndef __IMP_ISP_H__
#define __IMP_ISP_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "imp_common.h"

/**
 * ISP running mode
 */
typedef enum {
    IMPISP_RUNNING_MODE_DAY = 0,        /**< Day mode */
    IMPISP_RUNNING_MODE_NIGHT = 1       /**< Night mode */
} IMPISPRunningMode;

/**
 * ISP tuning operation mode
 */
typedef enum {
    IMPISP_TUNING_OPS_MODE_DISABLE = 0, /**< Disable */
    IMPISP_TUNING_OPS_MODE_ENABLE = 1   /**< Enable */
} IMPISPTuningOpsMode;

/**
 * Anti-flicker attribute
 */
typedef enum {
    IMPISP_ANTIFLICKER_DISABLE = 0,     /**< Disable anti-flicker */
    IMPISP_ANTIFLICKER_50HZ = 1,        /**< 50Hz */
    IMPISP_ANTIFLICKER_60HZ = 2         /**< 60Hz */
} IMPISPAntiflickerAttr;

/**
 * White balance mode
 */
typedef enum {
    IMPISP_WB_MODE_AUTO = 0,            /**< Auto white balance */
    IMPISP_WB_MODE_MANUAL = 1           /**< Manual white balance */
} IMPISPWBMode;

/**
 * White balance structure
 */
typedef struct {
    IMPISPWBMode mode;                  /**< WB mode */
    uint16_t rgain;                     /**< Red gain */
    uint16_t bgain;                     /**< Blue gain */
} IMPISPWB;

/**
 * EV (Exposure Value) attributes
 */
typedef struct {
    uint32_t ev[6];                     /**< EV values */
} IMPISPEVAttr;

/**
 * Open ISP module
 * 
 * @return 0 on success, negative on error
 */
int IMP_ISP_Open(void);

/**
 * Close ISP module
 * 
 * @return 0 on success, negative on error
 */
int IMP_ISP_Close(void);

/**
 * Add sensor to ISP (T21/T23/T31/C100)
 * 
 * @param pinfo Sensor information
 * @return 0 on success, negative on error
 */
int IMP_ISP_AddSensor(IMPSensorInfo *pinfo);

/**
 * Add sensor to ISP (T40/T41 with VI parameter)
 * 
 * @param vi Video input interface
 * @param pinfo Sensor information
 * @return 0 on success, negative on error
 */
int IMP_ISP_AddSensor_VI(IMPVI vi, IMPSensorInfo *pinfo);

/**
 * Delete sensor from ISP (T21/T23/T31/C100)
 * 
 * @param pinfo Sensor information
 * @return 0 on success, negative on error
 */
int IMP_ISP_DelSensor(IMPSensorInfo *pinfo);

/**
 * Delete sensor from ISP (T40/T41 with VI parameter)
 * 
 * @param vi Video input interface
 * @param pinfo Sensor information
 * @return 0 on success, negative on error
 */
int IMP_ISP_DelSensor_VI(IMPVI vi, IMPSensorInfo *pinfo);

/**
 * Enable sensor (T21/T23/T31/C100)
 * 
 * @return 0 on success, negative on error
 */
int IMP_ISP_EnableSensor(void);

/**
 * Enable sensor (T40/T41 with VI parameter)
 * 
 * @param vi Video input interface
 * @param pinfo Sensor information
 * @return 0 on success, negative on error
 */
int IMP_ISP_EnableSensor_VI(IMPVI vi, IMPSensorInfo *pinfo);

/**
 * Disable sensor (T21/T23/T31/C100)
 * 
 * @return 0 on success, negative on error
 */
int IMP_ISP_DisableSensor(void);

/**
 * Disable sensor (T40/T41 with VI parameter)
 * 
 * @param vi Video input interface
 * @return 0 on success, negative on error
 */
int IMP_ISP_DisableSensor_VI(IMPVI vi);

/**
 * Enable ISP tuning
 * 
 * @return 0 on success, negative on error
 */
int IMP_ISP_EnableTuning(void);

/**
 * Disable ISP tuning
 * 
 * @return 0 on success, negative on error
 */
int IMP_ISP_DisableTuning(void);

/**
 * Set sensor frame rate
 * 
 * @param fps_num FPS numerator
 * @param fps_den FPS denominator
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_SetSensorFPS(uint32_t fps_num, uint32_t fps_den);

/**
 * Get sensor frame rate
 * 
 * @param fps_num Pointer to FPS numerator
 * @param fps_den Pointer to FPS denominator
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_GetSensorFPS(uint32_t *fps_num, uint32_t *fps_den);

/**
 * Set anti-flicker attribute
 * 
 * @param attr Anti-flicker attribute
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_SetAntiFlickerAttr(IMPISPAntiflickerAttr attr);

/**
 * Set ISP running mode (day/night)
 * 
 * @param mode Running mode
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_SetISPRunningMode(IMPISPRunningMode mode);

/**
 * Get ISP running mode
 * 
 * @param pmode Pointer to running mode
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_GetISPRunningMode(IMPISPRunningMode *pmode);

/**
 * Set ISP bypass mode
 * 
 * @param enable Enable/disable bypass
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_SetISPBypass(IMPISPTuningOpsMode enable);

/**
 * Set horizontal flip
 * 
 * @param mode Enable/disable
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_SetISPHflip(IMPISPTuningOpsMode mode);

/**
 * Set vertical flip
 * 
 * @param mode Enable/disable
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_SetISPVflip(IMPISPTuningOpsMode mode);

/**
 * Set brightness
 * 
 * @param bright Brightness value (0-255)
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_SetBrightness(unsigned char bright);

/**
 * Set contrast
 * 
 * @param contrast Contrast value (0-255)
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_SetContrast(unsigned char contrast);

/**
 * Set sharpness
 * 
 * @param sharpness Sharpness value (0-255)
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_SetSharpness(unsigned char sharpness);

/**
 * Set saturation
 * 
 * @param sat Saturation value (0-255)
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_SetSaturation(unsigned char sat);

/**
 * Set AE compensation
 * 
 * @param comp Compensation value
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_SetAeComp(int comp);

/**
 * Set maximum analog gain
 * 
 * @param gain Maximum analog gain
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_SetMaxAgain(uint32_t gain);

/**
 * Set maximum digital gain
 * 
 * @param gain Maximum digital gain
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_SetMaxDgain(uint32_t gain);

/**
 * Set backlight compensation
 * 
 * @param strength Backlight compensation strength
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_SetBacklightComp(uint32_t strength);

/**
 * Set DPC (Defect Pixel Correction) strength
 * 
 * @param ratio DPC strength ratio
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_SetDPC_Strength(uint32_t ratio);

/**
 * Set DRC (Dynamic Range Compression) strength
 * 
 * @param ratio DRC strength ratio
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_SetDRC_Strength(uint32_t ratio);

/**
 * Set highlight depress
 * 
 * @param strength Highlight depress strength
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_SetHiLightDepress(uint32_t strength);

/**
 * Set temporal denoise strength
 * 
 * @param ratio Temporal denoise ratio
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_SetTemperStrength(uint32_t ratio);

/**
 * Set spatial denoise strength
 * 
 * @param ratio Spatial denoise ratio
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_SetSinterStrength(uint32_t ratio);

/**
 * Set hue
 * 
 * @param hue Hue value (0-255)
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_SetBcshHue(unsigned char hue);

/**
 * Set defog strength
 * 
 * @param strength Defog strength
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_SetDefog_Strength(uint32_t strength);

/**
 * Set white balance
 * 
 * @param wb White balance structure
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_SetWB(IMPISPWB *wb);

/**
 * Get white balance
 *
 * @param wb Pointer to white balance structure
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_GetWB(IMPISPWB *wb);

/**
 * Get brightness
 *
 * @param pbright Pointer to brightness value
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_GetBrightness(unsigned char *pbright);

/**
 * Get contrast
 *
 * @param pcontrast Pointer to contrast value
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_GetContrast(unsigned char *pcontrast);

/**
 * Get sharpness
 *
 * @param psharpness Pointer to sharpness value
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_GetSharpness(unsigned char *psharpness);

/**
 * Get saturation
 *
 * @param psat Pointer to saturation value
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_GetSaturation(unsigned char *psat);

/**
 * Get AE compensation
 *
 * @param pcomp Pointer to compensation value
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_GetAeComp(int *pcomp);

/**
 * Get backlight compensation
 *
 * @param pstrength Pointer to backlight compensation strength
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_GetBacklightComp(uint32_t *pstrength);

/**
 * Get highlight depress
 *
 * @param pstrength Pointer to highlight depress strength
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_GetHiLightDepress(uint32_t *pstrength);

/**
 * Get hue
 *
 * @param phue Pointer to hue value
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_GetBcshHue(unsigned char *phue);

/**
 * Get EV attributes
 *
 * @param attr Pointer to EV attributes structure
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_GetEVAttr(IMPISPEVAttr *attr);

/**
 * Get white balance statistics
 *
 * @param wb Pointer to white balance structure
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_GetWB_Statis(IMPISPWB *wb);

/**
 * Get white balance GOL statistics
 *
 * @param wb Pointer to white balance structure
 * @return 0 on success, negative on error
 */
int IMP_ISP_Tuning_GetWB_GOL_Statis(IMPISPWB *wb);

#ifdef __cplusplus
}
#endif

#endif /* __IMP_ISP_H__ */

