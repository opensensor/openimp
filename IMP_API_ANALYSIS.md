# IMP API Analysis for prudynt-t

This document catalogs all IMP (Ingenic Media Platform) API functions and headers used by the prudynt-t project.

## Header Files Required

### IMP Headers
- `imp/imp_system.h` - System initialization, binding, versioning
- `imp/imp_isp.h` - Image Signal Processor control
- `imp/imp_framesource.h` - Frame source channel management
- `imp/imp_encoder.h` - Video encoding
- `imp/imp_audio.h` - Audio input/output/encoding/decoding
- `imp/imp_osd.h` - On-Screen Display
- `imp/imp_ivs.h` - Intelligent Video System (motion detection)
- `imp/imp_ivs_move.h` - IVS motion detection interface
- `imp/imp_common.h` - Common definitions

### Sysutils Headers
- `sysutils/su_base.h` - Base sysutils functions

## IMP Functions by Module

### IMP_System (imp_system.h)
- `IMP_System_Init()` - Initialize the system
- `IMP_System_Exit()` - Cleanup and exit
- `IMP_System_Bind(IMPCell *srcCell, IMPCell *dstCell)` - Bind two cells
- `IMP_System_UnBind(IMPCell *srcCell, IMPCell *dstCell)` - Unbind two cells
- `IMP_System_GetVersion(IMPVersion *pstVersion)` - Get library version
- `IMP_System_GetCPUInfo()` - Get CPU information string
- `IMP_System_GetTimeStamp()` - Get current timestamp
- `IMP_System_RebaseTimeStamp(uint64_t basets)` - Rebase timestamp

### IMP_ISP (imp_isp.h)
#### Core ISP Functions
- `IMP_ISP_Open()` - Open ISP
- `IMP_ISP_Close()` - Close ISP
- `IMP_ISP_AddSensor(IMPSensorInfo *pinfo)` - Add sensor (T21/T23/T31)
- `IMP_ISP_AddSensor(IMPVI vi, IMPSensorInfo *pinfo)` - Add sensor (T40/T41)
- `IMP_ISP_DelSensor(IMPSensorInfo *pinfo)` - Delete sensor (T21/T23/T31)
- `IMP_ISP_DelSensor(IMPVI vi, IMPSensorInfo *pinfo)` - Delete sensor (T40/T41)
- `IMP_ISP_EnableSensor()` - Enable sensor (T21/T23/T31)
- `IMP_ISP_EnableSensor(IMPVI vi, IMPSensorInfo *pinfo)` - Enable sensor (T40/T41)
- `IMP_ISP_DisableSensor()` - Disable sensor (T21/T23/T31)
- `IMP_ISP_DisableSensor(IMPVI vi)` - Disable sensor (T40/T41)
- `IMP_ISP_EnableTuning()` - Enable ISP tuning
- `IMP_ISP_DisableTuning()` - Disable ISP tuning

#### ISP Tuning Functions
- `IMP_ISP_Tuning_SetSensorFPS(uint32_t fps_num, uint32_t fps_den)` - Set sensor FPS
- `IMP_ISP_Tuning_GetSensorFPS(uint32_t *fps_num, uint32_t *fps_den)` - Get sensor FPS
- `IMP_ISP_Tuning_SetAntiFlickerAttr(IMPISPAntiflickerAttr attr)` - Set anti-flicker
- `IMP_ISP_Tuning_SetISPRunningMode(IMPISPRunningMode mode)` - Set running mode (day/night)
- `IMP_ISP_Tuning_GetISPRunningMode(IMPISPRunningMode *pmode)` - Get running mode
- `IMP_ISP_Tuning_SetISPBypass(IMPISPTuningOpsMode enable)` - Bypass ISP
- `IMP_ISP_Tuning_SetISPHflip(IMPISPTuningOpsMode mode)` - Horizontal flip
- `IMP_ISP_Tuning_SetISPVflip(IMPISPTuningOpsMode mode)` - Vertical flip
- `IMP_ISP_Tuning_SetBrightness(unsigned char bright)` - Set brightness
- `IMP_ISP_Tuning_SetContrast(unsigned char contrast)` - Set contrast
- `IMP_ISP_Tuning_SetSharpness(unsigned char sharpness)` - Set sharpness
- `IMP_ISP_Tuning_SetSaturation(unsigned char sat)` - Set saturation
- `IMP_ISP_Tuning_SetAeComp(int comp)` - Set AE compensation
- `IMP_ISP_Tuning_SetMaxAgain(uint32_t gain)` - Set max analog gain
- `IMP_ISP_Tuning_SetMaxDgain(uint32_t gain)` - Set max digital gain
- `IMP_ISP_Tuning_SetBacklightComp(uint32_t strength)` - Set backlight compensation
- `IMP_ISP_Tuning_SetDPC_Strength(uint32_t ratio)` - Set DPC strength
- `IMP_ISP_Tuning_SetDRC_Strength(uint32_t ratio)` - Set DRC strength
- `IMP_ISP_Tuning_SetHiLightDepress(uint32_t strength)` - Set highlight depress
- `IMP_ISP_Tuning_SetTemperStrength(uint32_t ratio)` - Set temporal denoise strength
- `IMP_ISP_Tuning_SetSinterStrength(uint32_t ratio)` - Set spatial denoise strength
- `IMP_ISP_Tuning_SetBcshHue(unsigned char hue)` - Set hue
- `IMP_ISP_Tuning_SetDefog_Strength(uint32_t strength)` - Set defog strength
- `IMP_ISP_Tuning_SetWB(IMPISPWB *wb)` - Set white balance
- `IMP_ISP_Tuning_GetWB(IMPISPWB *wb)` - Get white balance

### IMP_FrameSource (imp_framesource.h)
- `IMP_FrameSource_CreateChn(int chnNum, IMPFSChnAttr *chn_attr)` - Create channel
- `IMP_FrameSource_DestroyChn(int chnNum)` - Destroy channel
- `IMP_FrameSource_EnableChn(int chnNum)` - Enable channel
- `IMP_FrameSource_DisableChn(int chnNum)` - Disable channel
- `IMP_FrameSource_SetChnAttr(int chnNum, IMPFSChnAttr *chn_attr)` - Set channel attributes
- `IMP_FrameSource_GetChnAttr(int chnNum, IMPFSChnAttr *chn_attr)` - Get channel attributes
- `IMP_FrameSource_SetChnFifoAttr(int chnNum, IMPFSChnFifoAttr *attr)` - Set FIFO attributes
- `IMP_FrameSource_GetChnFifoAttr(int chnNum, IMPFSChnFifoAttr *attr)` - Get FIFO attributes
- `IMP_FrameSource_SetFrameDepth(int chnNum, int depth)` - Set frame depth
- `IMP_FrameSource_SetChnRotate(int chnNum, int rotation, int height, int width)` - Set rotation (T31 only)

### IMP_Encoder (imp_encoder.h)
- `IMP_Encoder_CreateGroup(int encGroup)` - Create encoder group
- `IMP_Encoder_DestroyGroup(int encGroup)` - Destroy encoder group
- `IMP_Encoder_CreateChn(int encChn, IMPEncoderChnAttr *attr)` - Create encoder channel
- `IMP_Encoder_DestroyChn(int encChn)` - Destroy encoder channel
- `IMP_Encoder_RegisterChn(int encGroup, int encChn)` - Register channel to group
- `IMP_Encoder_UnRegisterChn(int encChn)` - Unregister channel
- `IMP_Encoder_StartRecvPic(int encChn)` - Start receiving pictures
- `IMP_Encoder_StopRecvPic(int encChn)` - Stop receiving pictures
- `IMP_Encoder_GetStream(int encChn, IMPEncoderStream *stream, bool block)` - Get encoded stream
- `IMP_Encoder_ReleaseStream(int encChn, IMPEncoderStream *stream)` - Release stream
- `IMP_Encoder_PollingStream(int encChn, uint32_t timeoutMsec)` - Poll for stream
- `IMP_Encoder_Query(int encChn, IMPEncoderChnStat *stat)` - Query channel status
- `IMP_Encoder_RequestIDR(int encChn)` - Request IDR frame
- `IMP_Encoder_FlushStream(int encChn)` - Flush stream
- `IMP_Encoder_SetDefaultParam(IMPEncoderChnAttr *attr, ...)` - Set default parameters
- `IMP_Encoder_GetChnAttr(int encChn, IMPEncoderChnAttr *attr)` - Get channel attributes
- `IMP_Encoder_SetJpegeQl(int encChn, IMPEncoderJpegeQl *attr)` - Set JPEG quality
- `IMP_Encoder_SetbufshareChn(int srcChn, int dstChn)` - Set buffer sharing (T31/T40/T41)
- `IMP_Encoder_SetFisheyeEnableStatus(int encChn, int enable)` - Enable fisheye (T31)

### IMP_AI (Audio Input - imp_audio.h)
- `IMP_AI_Enable(int audioDevId)` - Enable audio device
- `IMP_AI_Disable(int audioDevId)` - Disable audio device
- `IMP_AI_SetPubAttr(int audioDevId, IMPAudioIOAttr *attr)` - Set public attributes
- `IMP_AI_GetPubAttr(int audioDevId, IMPAudioIOAttr *attr)` - Get public attributes
- `IMP_AI_EnableChn(int audioDevId, int aiChn)` - Enable channel
- `IMP_AI_DisableChn(int audioDevId, int aiChn)` - Disable channel
- `IMP_AI_SetChnParam(int audioDevId, int aiChn, IMPAudioIChnParam *attr)` - Set channel params
- `IMP_AI_GetChnParam(int audioDevId, int aiChn, IMPAudioIChnParam *attr)` - Get channel params
- `IMP_AI_SetVol(int audioDevId, int aiChn, int vol)` - Set volume
- `IMP_AI_GetVol(int audioDevId, int aiChn, int *vol)` - Get volume
- `IMP_AI_SetGain(int audioDevId, int aiChn, int gain)` - Set gain
- `IMP_AI_GetGain(int audioDevId, int aiChn, int *gain)` - Get gain
- `IMP_AI_SetAlcGain(int audioDevId, int aiChn, int gain)` - Set ALC gain (T21/T31/C100)
- `IMP_AI_PollingFrame(int audioDevId, int aiChn, uint32_t timeoutMs)` - Poll for frame
- `IMP_AI_GetFrame(int audioDevId, int aiChn, IMPAudioFrame *frame, IMPBlock block)` - Get frame
- `IMP_AI_ReleaseFrame(int audioDevId, int aiChn, IMPAudioFrame *frame)` - Release frame
- `IMP_AI_EnableNs(IMPAudioIOAttr *attr, int level)` - Enable noise suppression
- `IMP_AI_DisableNs()` - Disable noise suppression
- `IMP_AI_EnableHpf()` - Enable high-pass filter
- `IMP_AI_DisableHpf()` - Disable high-pass filter
- `IMP_AI_EnableAgc(IMPAudioIOAttr *attr, IMPAudioAgcConfig config)` - Enable AGC
- `IMP_AI_DisableAgc()` - Disable AGC

### IMP_AENC (Audio Encoder - imp_audio.h)
- `IMP_AENC_RegisterEncoder(int *handle, IMPAudioEncEncoder *encoder)` - Register encoder
- `IMP_AENC_UnRegisterEncoder(int *handle)` - Unregister encoder
- `IMP_AENC_CreateChn(int aeChn, IMPAudioEncChnAttr *attr)` - Create channel
- `IMP_AENC_DestroyChn(int aeChn)` - Destroy channel
- `IMP_AENC_SendFrame(int aeChn, IMPAudioFrame *frame)` - Send frame
- `IMP_AENC_PollingStream(int aeChn, uint32_t timeoutMs)` - Poll for stream
- `IMP_AENC_GetStream(int aeChn, IMPAudioStream *stream, IMPBlock block)` - Get stream
- `IMP_AENC_ReleaseStream(int aeChn, IMPAudioStream *stream)` - Release stream

### IMP_ADEC (Audio Decoder - imp_audio.h)
- `IMP_ADEC_RegisterDecoder(int *handle, IMPAudioDecDecoder *decoder)` - Register decoder
- `IMP_ADEC_UnRegisterDecoder(int *handle)` - Unregister decoder
- `IMP_ADEC_CreateChn(int adChn, IMPAudioDecChnAttr *attr)` - Create channel
- `IMP_ADEC_DestroyChn(int adChn)` - Destroy channel
- `IMP_ADEC_SendStream(int adChn, IMPAudioStream *stream, IMPBlock block)` - Send stream
- `IMP_ADEC_GetStream(int adChn, IMPAudioStream *stream, IMPBlock block)` - Get stream
- `IMP_ADEC_ReleaseStream(int adChn, IMPAudioStream *stream)` - Release stream

### IMP_OSD (On-Screen Display - imp_osd.h)
- `IMP_OSD_SetPoolSize(int size)` - Set OSD pool size
- `IMP_OSD_CreateGroup(int grpNum)` - Create OSD group
- `IMP_OSD_DestroyGroup(int grpNum)` - Destroy OSD group
- `IMP_OSD_CreateRgn(IMPRgnHandle handle, IMPOSDRgnAttr *prAttr)` - Create region
- `IMP_OSD_DestroyRgn(IMPRgnHandle handle)` - Destroy region
- `IMP_OSD_RegisterRgn(IMPRgnHandle handle, int grpNum, IMPOSDGrpRgnAttr *pgrAttr)` - Register region
- `IMP_OSD_UnRegisterRgn(IMPRgnHandle handle, int grpNum)` - Unregister region
- `IMP_OSD_SetRgnAttr(IMPRgnHandle handle, IMPOSDRgnAttr *prAttr)` - Set region attributes
- `IMP_OSD_GetRgnAttr(IMPRgnHandle handle, IMPOSDRgnAttr *prAttr)` - Get region attributes
- `IMP_OSD_SetGrpRgnAttr(IMPRgnHandle handle, int grpNum, IMPOSDGrpRgnAttr *pgrAttr)` - Set group region attr
- `IMP_OSD_GetGrpRgnAttr(IMPRgnHandle handle, int grpNum, IMPOSDGrpRgnAttr *pgrAttr)` - Get group region attr
- `IMP_OSD_UpdateRgnAttrData(IMPRgnHandle handle, IMPOSDRgnAttrData *prAttrData)` - Update region data
- `IMP_OSD_ShowRgn(IMPRgnHandle handle, int grpNum, int showFlag)` - Show/hide region
- `IMP_OSD_Start(int grpNum)` - Start OSD group
- `IMP_OSD_Stop(int grpNum)` - Stop OSD group

### IMP_IVS (Intelligent Video System - imp_ivs.h)
- `IMP_IVS_CreateGroup(int grpNum)` - Create IVS group
- `IMP_IVS_DestroyGroup(int grpNum)` - Destroy IVS group
- `IMP_IVS_CreateChn(int chnNum, IMPIVSInterface *handler)` - Create IVS channel
- `IMP_IVS_DestroyChn(int chnNum)` - Destroy IVS channel
- `IMP_IVS_RegisterChn(int grpNum, int chnNum)` - Register channel to group
- `IMP_IVS_UnRegisterChn(int chnNum)` - Unregister channel
- `IMP_IVS_StartRecvPic(int chnNum)` - Start receiving pictures
- `IMP_IVS_StopRecvPic(int chnNum)` - Stop receiving pictures
- `IMP_IVS_PollingResult(int chnNum, int timeoutMs)` - Poll for result
- `IMP_IVS_GetResult(int chnNum, void **result)` - Get result
- `IMP_IVS_ReleaseResult(int chnNum, void *result)` - Release result
- `IMP_IVS_CreateMoveInterface(IMPIVSInterface **interface, ...)` - Create move interface (imp_ivs_move.h)
- `IMP_IVS_DestroyMoveInterface(IMPIVSInterface *interface)` - Destroy move interface

### SU_Base (sysutils/su_base.h)
- `SU_Base_GetVersion(SUVersion *version)` - Get sysutils version

## Key Data Structures Needed

Based on usage, we need to define these structures:
- `IMPCell` - Device/Group/Output cell for binding
- `IMPVersion` - Version information
- `IMPSensorInfo` - Sensor configuration
- `IMPFSChnAttr` - Frame source channel attributes
- `IMPFSChnFifoAttr` - Frame source FIFO attributes
- `IMPEncoderChnAttr` / `IMPEncoderCHNAttr` - Encoder channel attributes
- `IMPEncoderStream` - Encoded stream data
- `IMPEncoderChnStat` / `IMPEncoderCHNStat` - Encoder channel statistics
- `IMPEncoderJpegeQl` - JPEG quality settings
- `IMPAudioIOAttr` - Audio I/O attributes
- `IMPAudioIChnParam` - Audio input channel parameters
- `IMPAudioFrame` - Audio frame data
- `IMPAudioStream` - Audio stream data
- `IMPAudioEncChnAttr` - Audio encoder channel attributes
- `IMPAudioDecChnAttr` - Audio decoder channel attributes
- `IMPAudioEncEncoder` - Audio encoder callbacks
- `IMPAudioDecDecoder` - Audio decoder callbacks
- `IMPAudioAgcConfig` - AGC configuration
- `IMPOSDRgnAttr` - OSD region attributes
- `IMPOSDGrpRgnAttr` - OSD group region attributes
- `IMPOSDRgnAttrData` - OSD region data
- `IMPIVSInterface` - IVS interface
- `IMPISPWB` - White balance settings
- `IMPISPAntiflickerAttr` - Anti-flicker attributes
- `SUVersion` - Sysutils version

## Enums and Constants
- `IMPBlock` - BLOCK / NOBLOCK
- `IMPISPRunningMode` - IMPISP_RUNNING_MODE_DAY / IMPISP_RUNNING_MODE_NIGHT
- `IMPISPTuningOpsMode` - IMPISP_TUNING_OPS_MODE_ENABLE / IMPISP_TUNING_OPS_MODE_DISABLE
- `IMPAudioPalyloadType` - PT_PCM, PT_G711A, PT_G711U, PT_G726, etc.
- `IMP_ENC_PROFILE_*` - H264/H265/JPEG profiles
- `IMP_ENC_RC_MODE_*` - FIXQP, CBR, VBR, CAPPED_VBR, CAPPED_QUALITY
- `PIX_FMT_*` - Pixel formats (NV12, etc.)
- `FS_PHY_CHANNEL` - Frame source channel type
- `DEV_ID_FS`, `DEV_ID_ENC`, `DEV_ID_OSD` - Device IDs
- `TX_SENSOR_CONTROL_INTERFACE_I2C` - Sensor control interface
- `IMPVI_MAIN` - Video input (T40/T41)

## Platform-Specific Differences

The code handles multiple platforms:
- **T21** - Older platform
- **T23** - Has audio hardware quirks
- **T31** - Most common, supports rotation
- **C100** - Similar to T31
- **T40/T41** - Newer platforms with different sensor API (IMPVI parameter)

Platform differences affect:
- Sensor add/enable/disable API signatures
- Availability of certain features (rotation, buffer sharing, AGC)
- Structure naming (IMPEncoderCHNAttr vs IMPEncoderChnAttr)

