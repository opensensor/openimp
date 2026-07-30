/* Loader-completeness shims for RVD/RAD features outside the active P3 gate.
 * Real P0-P3 implementations live in their subsystem translation units.
 * These return ENOTSUP instead of pretending that inactive P4 features work. */

#include <errno.h>
#include <stddef.h>
#include <stdio.h>

#define P3_REPORT_UNSUPPORTED(name)                                           \
    fprintf(stderr, "openimp-p3: unsupported call: %s\n", #name)

#define P3_UNSUPPORTED(name)                                                  \
    int name(void)                                                            \
    {                                                                         \
        P3_REPORT_UNSUPPORTED(name);                                          \
        errno = ENOTSUP;                                                      \
        return -1;                                                            \
    }

#define P3_UNSUPPORTED_PTR(name)                                              \
    void *name(void)                                                          \
    {                                                                         \
        P3_REPORT_UNSUPPORTED(name);                                          \
        errno = ENOTSUP;                                                      \
        return NULL;                                                          \
    }

P3_UNSUPPORTED(IMP_ADEC_ClearChnBuf)
P3_UNSUPPORTED(IMP_ADEC_CreateChn)
P3_UNSUPPORTED(IMP_ADEC_DestroyChn)
P3_UNSUPPORTED(IMP_ADEC_GetStream)
P3_UNSUPPORTED(IMP_ADEC_PollingStream)
P3_UNSUPPORTED(IMP_ADEC_RegisterDecoder)
P3_UNSUPPORTED(IMP_ADEC_ReleaseStream)
P3_UNSUPPORTED(IMP_ADEC_SendStream)
P3_UNSUPPORTED(IMP_ADEC_UnRegisterDecoder)
P3_UNSUPPORTED(IMP_AENC_CreateChn)
P3_UNSUPPORTED(IMP_AENC_DestroyChn)
P3_UNSUPPORTED(IMP_AENC_GetStream)
P3_UNSUPPORTED(IMP_AENC_PollingStream)
P3_UNSUPPORTED(IMP_AENC_RegisterEncoder)
P3_UNSUPPORTED(IMP_AENC_ReleaseStream)
P3_UNSUPPORTED(IMP_AENC_SendFrame)
P3_UNSUPPORTED(IMP_AENC_UnRegisterEncoder)

P3_UNSUPPORTED(IMP_DMIC_Disable)
P3_UNSUPPORTED(IMP_DMIC_DisableAec)
P3_UNSUPPORTED(IMP_DMIC_DisableAecRefFrame)
P3_UNSUPPORTED(IMP_DMIC_DisableChn)
P3_UNSUPPORTED(IMP_DMIC_Enable)
P3_UNSUPPORTED(IMP_DMIC_EnableAec)
P3_UNSUPPORTED(IMP_DMIC_EnableAecRefFrame)
P3_UNSUPPORTED(IMP_DMIC_EnableChn)
P3_UNSUPPORTED(IMP_DMIC_GetChnParam)
P3_UNSUPPORTED(IMP_DMIC_GetFrame)
P3_UNSUPPORTED(IMP_DMIC_GetFrameAndRef)
P3_UNSUPPORTED(IMP_DMIC_GetGain)
P3_UNSUPPORTED(IMP_DMIC_GetPubAttr)
P3_UNSUPPORTED(IMP_DMIC_GetVol)
P3_UNSUPPORTED(IMP_DMIC_PollingFrame)
P3_UNSUPPORTED(IMP_DMIC_ReleaseFrame)
P3_UNSUPPORTED(IMP_DMIC_SetChnParam)
P3_UNSUPPORTED(IMP_DMIC_SetGain)
P3_UNSUPPORTED(IMP_DMIC_SetPubAttr)
P3_UNSUPPORTED(IMP_DMIC_SetUserInfo)
P3_UNSUPPORTED(IMP_DMIC_SetVol)

P3_UNSUPPORTED(IMP_FrameSource_GetDelay)
P3_UNSUPPORTED(IMP_FrameSource_GetI2dAttr)
P3_UNSUPPORTED(IMP_FrameSource_GetMaxDelay)
P3_UNSUPPORTED(IMP_FrameSource_GetTimedFrame)
P3_UNSUPPORTED(IMP_FrameSource_SetDelay)
P3_UNSUPPORTED(IMP_FrameSource_SetI2dAttr)
P3_UNSUPPORTED(IMP_FrameSource_SetMaxDelay)
P3_UNSUPPORTED(IMP_FrameSource_SnapFrame)

P3_UNSUPPORTED(IMP_ISP_GetFrameDrop)
P3_UNSUPPORTED(IMP_ISP_GetSensorRegister)
P3_UNSUPPORTED(IMP_ISP_SetFrameDrop)
P3_UNSUPPORTED(IMP_ISP_SetSensorRegister)
P3_UNSUPPORTED(IMP_ISP_Tuning_CreateOsdRgn)
P3_UNSUPPORTED(IMP_ISP_Tuning_DestroyOsdRgn)
P3_UNSUPPORTED(IMP_ISP_Tuning_GetAeStatistics)
P3_UNSUPPORTED(IMP_ISP_Tuning_GetAeWeight)
P3_UNSUPPORTED(IMP_ISP_Tuning_GetAfWeight)
P3_UNSUPPORTED(IMP_ISP_Tuning_GetAwbAttr)
P3_UNSUPPORTED(IMP_ISP_Tuning_GetAwbGlobalStatistics)
P3_UNSUPPORTED(IMP_ISP_Tuning_GetAwbStatistics)
P3_UNSUPPORTED(IMP_ISP_Tuning_GetAwbWeight)
P3_UNSUPPORTED(IMP_ISP_Tuning_GetCCMAttr)
P3_UNSUPPORTED(IMP_ISP_Tuning_GetGammaAttr)
P3_UNSUPPORTED(IMP_ISP_Tuning_GetMask)
P3_UNSUPPORTED(IMP_ISP_Tuning_GetModuleControl)
P3_UNSUPPORTED(IMP_ISP_Tuning_GetSensorAttr)
P3_UNSUPPORTED(IMP_ISP_Tuning_SetAeWeight)
P3_UNSUPPORTED(IMP_ISP_Tuning_SetAfWeight)
P3_UNSUPPORTED(IMP_ISP_Tuning_SetAwbAttr)
P3_UNSUPPORTED(IMP_ISP_Tuning_SetAwbWeight)
P3_UNSUPPORTED(IMP_ISP_Tuning_SetCCMAttr)
P3_UNSUPPORTED(IMP_ISP_Tuning_SetGammaAttr)
P3_UNSUPPORTED(IMP_ISP_Tuning_SetMask)
P3_UNSUPPORTED(IMP_ISP_Tuning_SetModuleControl)
P3_UNSUPPORTED(IMP_ISP_Tuning_SetOsdRgnAttr)
P3_UNSUPPORTED(IMP_ISP_Tuning_SetVideoDrop)
P3_UNSUPPORTED(IMP_ISP_Tuning_ShowOsdRgn)
P3_UNSUPPORTED(IMP_ISP_WDR_ENABLE)
P3_UNSUPPORTED(IMP_ISP_WDR_ENABLE_GET)

P3_UNSUPPORTED_PTR(IMP_IVS_CreateBaseMoveInterface)
P3_UNSUPPORTED(IMP_IVS_CreateChn)
P3_UNSUPPORTED(IMP_IVS_CreateGroup)
P3_UNSUPPORTED_PTR(IMP_IVS_CreateMoveInterface)
P3_UNSUPPORTED(IMP_IVS_DestroyBaseMoveInterface)
P3_UNSUPPORTED(IMP_IVS_DestroyChn)
P3_UNSUPPORTED(IMP_IVS_DestroyGroup)
P3_UNSUPPORTED(IMP_IVS_DestroyMoveInterface)
P3_UNSUPPORTED(IMP_IVS_GetParam)
P3_UNSUPPORTED(IMP_IVS_GetResult)
P3_UNSUPPORTED(IMP_IVS_PollingResult)
P3_UNSUPPORTED(IMP_IVS_RegisterChn)
P3_UNSUPPORTED(IMP_IVS_ReleaseData)
P3_UNSUPPORTED(IMP_IVS_ReleaseResult)
P3_UNSUPPORTED(IMP_IVS_SetParam)
P3_UNSUPPORTED(IMP_IVS_StartRecvPic)
P3_UNSUPPORTED(IMP_IVS_StopRecvPic)
P3_UNSUPPORTED(IMP_IVS_UnRegisterChn)

P3_UNSUPPORTED(IMP_OSD_CreateGroup)
P3_UNSUPPORTED(IMP_OSD_CreateRgn)
P3_UNSUPPORTED(IMP_OSD_DestroyGroup)
P3_UNSUPPORTED(IMP_OSD_DestroyRgn)
P3_UNSUPPORTED(IMP_OSD_GetGrpRgnAttr)
P3_UNSUPPORTED(IMP_OSD_GetRgnAttr)
P3_UNSUPPORTED(IMP_OSD_RegisterRgn)
P3_UNSUPPORTED(IMP_OSD_SetGrpRgnAttr)
P3_UNSUPPORTED(IMP_OSD_SetRgnAttr)
P3_UNSUPPORTED(IMP_OSD_SetRgnAttrWithTimestamp)
P3_UNSUPPORTED(IMP_OSD_ShowRgn)
P3_UNSUPPORTED(IMP_OSD_Start)
P3_UNSUPPORTED(IMP_OSD_Stop)
P3_UNSUPPORTED(IMP_OSD_UnRegisterRgn)
P3_UNSUPPORTED(IMP_OSD_UpdateRgnAttrData)
