# Added Symbols for Thingino Streamer

This document lists all the symbols that were added to OpenIMP to support the thingino-streamer binary.

## Summary

Added **16 new IMP symbols** that were missing from the device's libimp.so:
- **11 ISP Tuning Get functions** - Image quality parameter getters
- **5 Encoder functions** - Advanced encoder configuration

**Note**: The `sft_*` font rendering functions are already present in the device's existing `libsysutils.so`, so we don't need to provide them.

## ISP Tuning Get Functions

These functions retrieve current ISP image quality settings. All are based on Binary Ninja MCP decompilations from libimp.so v1.1.6.

### Functions Added to `src/imp_isp.c`:

1. **`IMP_ISP_Tuning_GetBrightness(unsigned char *pbright)`**
   - Returns current brightness value (0-255)
   - Default: 128 (middle value)

2. **`IMP_ISP_Tuning_GetContrast(unsigned char *pcontrast)`**
   - Returns current contrast value (0-255)
   - Default: 128 (middle value)

3. **`IMP_ISP_Tuning_GetSharpness(unsigned char *psharpness)`**
   - Returns current sharpness value (0-255)
   - Default: 128 (middle value)

4. **`IMP_ISP_Tuning_GetSaturation(unsigned char *psat)`**
   - Returns current saturation value (0-255)
   - Default: 128 (middle value)

5. **`IMP_ISP_Tuning_GetAeComp(int *pcomp)`**
   - Returns current AE compensation value
   - Default: 0 (no compensation)

6. **`IMP_ISP_Tuning_GetBacklightComp(uint32_t *pstrength)`**
   - Returns current backlight compensation strength
   - Default: 0 (off)

7. **`IMP_ISP_Tuning_GetHiLightDepress(uint32_t *pstrength)`**
   - Returns current highlight depress strength
   - Default: 0 (off)

8. **`IMP_ISP_Tuning_GetBcshHue(unsigned char *phue)`**
   - Returns current hue value (0-255)
   - Default: 128 (middle value)

9. **`IMP_ISP_Tuning_GetEVAttr(IMPISPEVAttr *attr)`**
   - Returns current EV (Exposure Value) attributes
   - Returns zeroed structure

10. **`IMP_ISP_Tuning_GetWB_Statis(IMPISPWB *wb)`**
    - Returns white balance statistics
    - Default: AUTO mode, rgain=256, bgain=256

11. **`IMP_ISP_Tuning_GetWB_GOL_Statis(IMPISPWB *wb)`**
    - Returns white balance GOL statistics
    - Default: AUTO mode, rgain=256, bgain=256

### New Type Added to `include/imp/imp_isp.h`:

```c
typedef struct {
    uint32_t ev[6];  /**< EV values */
} IMPISPEVAttr;
```

## Encoder Functions

These functions provide advanced encoder configuration. Based on Binary Ninja MCP decompilations.

### Functions Added to `src/imp_encoder.c`:

1. **`IMP_Encoder_SetChnQp(int encChn, IMPEncoderQp *qp)`**
   - Sets QP (Quantization Parameter) for encoder channel
   - Calls `AL_Codec_Encode_SetQp()` internally
   - Decompiled from address 0x87f5c

2. **`IMP_Encoder_SetChnGopLength(int encChn, int gopLength)`**
   - Sets GOP (Group of Pictures) length
   - Stores at offset 0x3d0 in channel structure
   - Decompiled from address 0x88810

3. **`IMP_Encoder_SetChnEntropyMode(int encChn, int mode)`**
   - Sets entropy coding mode (0=CAVLC, 1=CABAC)
   - Stores at offset 0x3fc in channel structure
   - Decompiled from address 0x88cb4

4. **`IMP_Encoder_SetMaxStreamCnt(int encChn, int cnt)`**
   - Sets maximum stream count
   - Stores at offset 0x4c0 in channel structure
   - Decompiled from address 0x8648c

5. **`IMP_Encoder_SetStreamBufSize(int encChn, int size)`**
   - Sets stream buffer size
   - Stores at offset 0x4c4 in channel structure
   - Decompiled from address 0x86650

### New Type Added to `include/imp/imp_encoder.h`:

```c
typedef struct {
    uint32_t qp_i;  /**< I frame QP */
    uint32_t qp_p;  /**< P frame QP */
    uint32_t qp_b;  /**< B frame QP */
} IMPEncoderQp;
```

### Codec Function Added to `src/codec.c`:

- **`AL_Codec_Encode_SetQp(void *codec, void *qp)`**
  - Internal codec function for setting QP parameters
  - Called by `IMP_Encoder_SetChnQp()`

## Font Rendering Functions (sft_*)

**Note**: The device already has the `sft_*` font rendering functions in its existing `libsysutils.so`. These functions are provided by the `libschrift` library and are already available on the device. We do not need to implement them in OpenIMP.

## Testing

All 16 IMP symbols compile successfully and are exported in `lib/libimp.so`.

The thingino-streamer binary should now be able to resolve all required IMP symbols when using this library.

## Implementation Notes

1. **ISP Get Functions**: Return sensible default values. In a full implementation, these would read from ISP hardware registers via ioctl calls to `/dev/tx-isp`.

2. **Encoder Functions**: Store configuration in channel structure. In a full implementation, these would configure the hardware encoder via the AL_Codec interface.

## References

- Binary Ninja MCP decompilations from `libimp.so` v1.1.6 (T31 platform)
- Ingenic T31 SDK documentation
- OpenIMP reverse engineering project

