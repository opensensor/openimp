/*
 * T23 1.3.0 compatibility services.
 *
 * The T23 SDK exposes single-camera tuning calls and parallel
 * IMP_ISP_MultiCamera_* entry points.  Raptor is built against the latter,
 * even on one-sensor products.  OpenIMP has one ISP instance today, so map
 * VI_MAIN to the working single-camera implementation and reject VI_SEC.
 *
 * ISP-integrated OSD is separate from the ordinary IMP_OSD bind-chain used
 * by Raptor.  Keep its small handle API ABI-correct for configurations which
 * probe it; the actual ISP overlay plane is not implemented yet.
 */

#include <errno.h>
#include <stdint.h>

#include <imp/imp_isp.h>
#include <imp/imp_osd.h>

#define T23_VI_MAIN 0
#define T23_ISP_OSD_HANDLES 8

static uint8_t isp_osd_handles[T23_ISP_OSD_HANDLES];

extern int IMP_ISP_Tuning_GetTotalGain(uint32_t *gain);
extern int IMP_ISP_Tuning_SetAeFreeze(int enable);

static int t23_check_vi(int vi)
{
    return vi == T23_VI_MAIN ? 0 : -ENODEV;
}

int IMP_ISP_MultiCamera_SetSwitchgpio(void *info)
{
    /* A one-sensor T23 has no sensor-switch GPIO to program. */
    (void)info;
    return 0;
}

#define T23_TUNING_WRAP_U8(name)                                           \
    int IMP_ISP_MultiCamera_Tuning_##name(int vi, unsigned char value)     \
    {                                                                      \
        int ret = t23_check_vi(vi);                                        \
        return ret ? ret : IMP_ISP_Tuning_##name(value);                   \
    }

#define T23_TUNING_WRAP_U32(name)                                          \
    int IMP_ISP_MultiCamera_Tuning_##name(int vi, uint32_t value)          \
    {                                                                      \
        int ret = t23_check_vi(vi);                                        \
        return ret ? ret : IMP_ISP_Tuning_##name(value);                   \
    }

T23_TUNING_WRAP_U8(SetBrightness)
T23_TUNING_WRAP_U8(SetContrast)
T23_TUNING_WRAP_U8(SetSaturation)
T23_TUNING_WRAP_U8(SetSharpness)
T23_TUNING_WRAP_U8(SetBcshHue)

T23_TUNING_WRAP_U32(SetSinterStrength)
T23_TUNING_WRAP_U32(SetTemperStrength)
T23_TUNING_WRAP_U32(SetMaxAgain)
T23_TUNING_WRAP_U32(SetMaxDgain)

int IMP_ISP_MultiCamera_Tuning_SetAeComp(int vi, int value)
{
    int ret = t23_check_vi(vi);
    return ret ? ret : IMP_ISP_Tuning_SetAeComp(value);
}

int IMP_ISP_MultiCamera_Tuning_SetAeFreeze(int vi,
                                           IMPISPTuningOpsMode mode)
{
    int ret = t23_check_vi(vi);
    return ret ? ret : IMP_ISP_Tuning_SetAeFreeze((int)mode);
}

int IMP_ISP_MultiCamera_Tuning_SetAntiFlickerAttr(
    int vi, IMPISPAntiflickerAttr attr)
{
    int ret = t23_check_vi(vi);
    return ret ? ret : IMP_ISP_Tuning_SetAntiFlickerAttr(attr);
}

int IMP_ISP_MultiCamera_Tuning_SetHVFLIP(int vi, IMPISPHVFLIP mode)
{
    int ret = t23_check_vi(vi);
    return ret ? ret : IMP_ISP_Tuning_SetHVFLIP(mode);
}

int IMP_ISP_MultiCamera_Tuning_SetISPCustomMode(
    int vi, IMPISPTuningOpsMode mode)
{
    int ret = t23_check_vi(vi);
    return ret ? ret : IMP_ISP_Tuning_SetISPCustomMode(mode);
}

int IMP_ISP_MultiCamera_Tuning_SetISPRunningMode(
    int vi, IMPISPRunningMode mode)
{
    int ret = t23_check_vi(vi);
    return ret ? ret : IMP_ISP_Tuning_SetISPRunningMode(mode);
}

int IMP_ISP_MultiCamera_Tuning_SetSensorFPS(int vi, uint32_t numerator,
                                            uint32_t denominator)
{
    int ret = t23_check_vi(vi);
    return ret ? ret : IMP_ISP_Tuning_SetSensorFPS(numerator, denominator);
}

int IMP_ISP_MultiCamera_Tuning_GetTotalGain(int vi, uint32_t *gain)
{
    int ret = t23_check_vi(vi);

    if (ret)
        return ret;
    if (!gain)
        return -EINVAL;
    return IMP_ISP_Tuning_GetTotalGain(gain);
}

int IMP_ISP_Tuning_SetOsdPoolSize(int size)
{
    return size >= 0 ? 0 : -EINVAL;
}

int IMP_ISP_Tuning_CreateOsdRgn(int channel, IMPIspOsdAttrAsm *attr)
{
    int handle;

    (void)attr;
    if (channel != T23_VI_MAIN)
        return -ENODEV;
    for (handle = 0; handle < T23_ISP_OSD_HANDLES; handle++) {
        if (!isp_osd_handles[handle]) {
            isp_osd_handles[handle] = 1;
            return handle;
        }
    }
    return -ENOSPC;
}

int IMP_ISP_Tuning_DestroyOsdRgn(int channel, int handle)
{
    if (channel != T23_VI_MAIN)
        return -ENODEV;
    if (handle < 0 || handle >= T23_ISP_OSD_HANDLES ||
        !isp_osd_handles[handle])
        return -EINVAL;
    isp_osd_handles[handle] = 0;
    return 0;
}

int IMP_ISP_Tuning_SetOsdRgnAttr(int channel, int handle,
                                 IMPIspOsdAttrAsm *attr)
{
    (void)attr;
    if (channel != T23_VI_MAIN)
        return -ENODEV;
    return handle >= 0 && handle < T23_ISP_OSD_HANDLES &&
           isp_osd_handles[handle] ? 0 : -EINVAL;
}

int IMP_ISP_Tuning_ShowOsdRgn(int channel, int handle, int show)
{
    (void)show;
    return IMP_ISP_Tuning_SetOsdRgnAttr(channel, handle, 0);
}

int IMP_OSD_MultiCamera_SetRgnAttr_ISP(int vi, IMPOSDRgnAttr *attr,
                                       int reserved)
{
    (void)attr;
    (void)reserved;
    return t23_check_vi(vi);
}
