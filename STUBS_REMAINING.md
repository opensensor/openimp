# Remaining Stubs in OpenIMP

## Summary

Most critical functionality is implemented. The remaining stubs are for:
1. **ISP Tuning Functions** - Image quality adjustments (brightness, contrast, etc.)
2. **IVS Module** - Intelligent Video Surveillance (motion detection, etc.)
3. **Audio Encoder/Decoder** - Audio encoding/decoding (AENC/ADEC)
4. **Audio Input Advanced** - GetFrame, ReleaseFrame, etc.

## ISP Module Stubs

### Core Functions ✅ IMPLEMENTED
- `IMP_ISP_Open()` - Opens `/dev/tx-isp` ✅
- `IMP_ISP_Close()` - Closes ISP device ✅
- `IMP_ISP_AddSensor()` - Registers sensor ✅
- `IMP_ISP_EnableSensor()` - Enables sensor ✅

### Tuning Functions ⚠️ STUBS (Log only)
These functions just log and return success. They don't actually adjust image quality:

- `IMP_ISP_Tuning_SetSensorFPS()` - Set sensor frame rate
- `IMP_ISP_Tuning_GetSensorFPS()` - Get sensor frame rate (returns 25/1)
- `IMP_ISP_Tuning_SetAntiFlickerAttr()` - Anti-flicker settings
- `IMP_ISP_Tuning_SetISPRunningMode()` - Day/night mode
- `IMP_ISP_Tuning_GetISPRunningMode()` - Get mode (returns DAY)
- `IMP_ISP_Tuning_SetISPBypass()` - Bypass ISP processing
- `IMP_ISP_Tuning_SetISPHflip()` - Horizontal flip
- `IMP_ISP_Tuning_SetISPVflip()` - Vertical flip
- `IMP_ISP_Tuning_SetBrightness()` - Brightness adjustment
- `IMP_ISP_Tuning_SetContrast()` - Contrast adjustment
- `IMP_ISP_Tuning_SetSharpness()` - Sharpness adjustment
- `IMP_ISP_Tuning_SetSaturation()` - Saturation adjustment
- `IMP_ISP_Tuning_SetAeComp()` - Auto-exposure compensation
- `IMP_ISP_Tuning_SetMaxAgain()` - Max analog gain
- `IMP_ISP_Tuning_SetMaxDgain()` - Max digital gain
- `IMP_ISP_Tuning_SetBacklightComp()` - Backlight compensation
- `IMP_ISP_Tuning_SetDPC_Strength()` - Dead pixel correction
- `IMP_ISP_Tuning_SetDRC_Strength()` - Dynamic range compression
- `IMP_ISP_Tuning_SetHiLightDepress()` - Highlight suppression
- `IMP_ISP_Tuning_SetTemperStrength()` - Temporal noise reduction
- `IMP_ISP_Tuning_SetSinterStrength()` - Spatial noise reduction
- `IMP_ISP_Tuning_SetBcshHue()` - Hue adjustment
- `IMP_ISP_Tuning_SetDefog_Strength()` - Defog strength
- `IMP_ISP_Tuning_SetWB()` - White balance
- `IMP_ISP_Tuning_GetWB()` - Get white balance (returns AUTO, 256, 256)

**Impact**: Video will work but image quality adjustments won't take effect. Default sensor settings will be used.

### Other ISP Stubs ⚠️
- `IMP_ISP_AddSensor_VI()` - Multi-sensor support
- `IMP_ISP_DelSensor()` - Remove sensor
- `IMP_ISP_DelSensor_VI()` - Remove sensor (multi)
- `IMP_ISP_EnableSensor_VI()` - Enable sensor (multi)
- `IMP_ISP_DisableSensor()` - Disable sensor
- `IMP_ISP_DisableSensor_VI()` - Disable sensor (multi)
- `IMP_ISP_EnableTuning()` - Enable tuning
- `IMP_ISP_DisableTuning()` - Disable tuning

**Impact**: Single sensor operation works. Multi-sensor and advanced tuning features not available.

## IVS Module Stubs ⚠️ ALL STUBS

The entire IVS (Intelligent Video Surveillance) module is stubbed:

- `IMP_IVS_CreateGroup()` - Create IVS group
- `IMP_IVS_DestroyGroup()` - Destroy IVS group
- `IMP_IVS_CreateChn()` - Create IVS channel
- `IMP_IVS_DestroyChn()` - Destroy IVS channel
- `IMP_IVS_RegisterChn()` - Register channel to group
- `IMP_IVS_UnRegisterChn()` - Unregister channel
- `IMP_IVS_StartRecvPic()` - Start receiving pictures
- `IMP_IVS_StopRecvPic()` - Stop receiving pictures
- `IMP_IVS_PollingResult()` - Poll for results (returns timeout)
- `IMP_IVS_GetResult()` - Get analysis result (returns no result)
- `IMP_IVS_ReleaseResult()` - Release result
- `IMP_IVS_CreateMoveInterface()` - Create motion detection interface
- `IMP_IVS_DestroyMoveInterface()` - Destroy motion detection interface

**Impact**: Motion detection and intelligent video analysis features won't work. Basic video streaming is unaffected.

## Audio Module Stubs

### Core Audio Input ✅ IMPLEMENTED
- `IMP_AI_SetPubAttr()` - Set audio attributes ✅
- `IMP_AI_GetPubAttr()` - Get audio attributes ✅
- `IMP_AI_Enable()` - Enable audio device ✅
- `IMP_AI_Disable()` - Disable audio device ✅
- `IMP_AI_EnableChn()` - Enable audio channel ✅
- `IMP_AI_DisableChn()` - Disable audio channel ✅
- `IMP_AI_SetVol()` - Set volume ✅
- `IMP_AI_SetGain()` - Set gain ✅

### Audio Input Advanced ⚠️ STUBS
- `IMP_AI_GetVol()` - Get volume (returns 60)
- `IMP_AI_GetGain()` - Get gain (returns 28)
- `IMP_AI_PollingFrame()` - Poll for audio frame (returns timeout)
- `IMP_AI_GetFrame()` - Get audio frame (returns no frame)
- `IMP_AI_ReleaseFrame()` - Release audio frame
- `IMP_AI_EnableAec()` - Enable acoustic echo cancellation
- `IMP_AI_DisableAec()` - Disable AEC
- `IMP_AI_EnableNs()` - Enable noise suppression
- `IMP_AI_DisableNs()` - Disable noise suppression
- `IMP_AI_EnableAgc()` - Enable automatic gain control
- `IMP_AI_DisableAgc()` - Disable AGC

**Impact**: Audio device opens and initializes, but actual audio capture doesn't work. Audio encoding won't function.

### Audio Encoder (AENC) ⚠️ ALL STUBS
- `IMP_AENC_RegisterEncoder()` - Register encoder (returns dummy handle 100)
- `IMP_AENC_UnRegisterEncoder()` - Unregister encoder
- `IMP_AENC_CreateChn()` - Create encoder channel
- `IMP_AENC_DestroyChn()` - Destroy encoder channel
- `IMP_AENC_SendFrame()` - Send frame to encoder
- `IMP_AENC_PollingStream()` - Poll for encoded stream (returns timeout)
- `IMP_AENC_GetStream()` - Get encoded stream (returns no stream)
- `IMP_AENC_ReleaseStream()` - Release encoded stream

**Impact**: Audio encoding doesn't work. Video-only streaming is unaffected.

### Audio Decoder (ADEC) ⚠️ ALL STUBS
- `IMP_ADEC_RegisterDecoder()` - Register decoder (returns dummy handle 200)
- `IMP_ADEC_UnRegisterDecoder()` - Unregister decoder
- `IMP_ADEC_CreateChn()` - Create decoder channel
- `IMP_ADEC_DestroyChn()` - Destroy decoder channel
- `IMP_ADEC_SendStream()` - Send stream to decoder
- `IMP_ADEC_GetStream()` - Get decoded stream (returns no stream)
- `IMP_ADEC_ReleaseStream()` - Release decoded stream

**Impact**: Audio decoding doesn't work. Audio playback features unavailable.

### Audio Output (AO) ⚠️ ALL STUBS
All AO functions are stubs (not listed individually).

**Impact**: Audio output/playback doesn't work.

## OSD Module Stubs

### Core OSD ✅ IMPLEMENTED
- `IMP_OSD_CreateGroup()` - Create OSD group ✅
- `IMP_OSD_DestroyGroup()` - Destroy OSD group ✅
- `IMP_OSD_CreateRgn()` - Create OSD region ✅
- `IMP_OSD_DestroyRgn()` - Destroy OSD region ✅
- `IMP_OSD_RegisterRgn()` - Register region to group ✅
- `IMP_OSD_UnRegisterRgn()` - Unregister region ✅
- `IMP_OSD_Start()` - Start OSD rendering ✅
- `IMP_OSD_Stop()` - Stop OSD rendering ✅

### OSD Configuration ⚠️ STUBS
- `IMP_OSD_SetRgnAttr()` - Set region attributes
- `IMP_OSD_GetRgnAttr()` - Get region attributes (returns zeros)
- `IMP_OSD_SetGrpRgnAttr()` - Set group region attributes
- `IMP_OSD_GetGrpRgnAttr()` - Get group region attributes (returns zeros)
- `IMP_OSD_UpdateRgnAttrData()` - Update region data
- `IMP_OSD_ShowRgn()` - Show/hide region

**Impact**: OSD groups and regions can be created but actual rendering/display doesn't work.

## What Works vs What Doesn't

### ✅ WORKS (Fully Implemented)
1. **System Module** - Module registry, binding, observers
2. **ISP Core** - Device opening, sensor registration, sensor enable
3. **FrameSource** - Channel creation, device opening, VBM pools, frame capture
4. **Encoder** - Channel management, codec integration, threading
5. **OSD Core** - Group/region creation, registration
6. **Audio Core** - Device initialization, channel enable/disable
7. **DMA** - Buffer allocation, memory mapping
8. **Hardware Encoder** - Integration with `/dev/jz-venc`

### ⚠️ PARTIALLY WORKS (Core works, advanced features stubbed)
1. **ISP** - Core works, tuning functions stubbed
2. **OSD** - Structure works, rendering stubbed
3. **Audio** - Device works, capture/encode stubbed

### ❌ DOESN'T WORK (All stubs)
1. **IVS** - Motion detection, intelligent analysis
2. **AENC/ADEC** - Audio encoding/decoding
3. **AO** - Audio output

## For Video Streaming (prudynt)

**Required for basic video streaming:**
- ✅ System
- ✅ ISP Core
- ✅ FrameSource
- ✅ Encoder
- ⚠️ ISP Tuning (optional, uses defaults)

**Not required for video streaming:**
- ❌ IVS
- ❌ Audio (AENC/ADEC/AO)
- ⚠️ OSD rendering (structure works)

**Conclusion**: Basic video streaming should work. Audio and advanced features won't work.

