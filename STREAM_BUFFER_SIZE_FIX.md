# Stream Buffer Size Fix

## Problem
RTSP clients (ffprobe, VLC) can connect to the stream but hang at "hello state=0" and never receive/decode frames. The SDP is correct (1920x1080, H264, 30fps), but no actual frame data is being transmitted.

## Root Cause Analysis

### Binary Ninja MCP Investigation
Using Binary Ninja MCP to decompile `libimp.so v1.1.6`, we discovered:

1. **Stream Buffer Metadata Structure Size**: `0x188` (392 bytes)
   - From `IMP_Encoder_GetStream_Impl` at line:
   ```c
   int32_t* $s5_8 = $a1_7 + *($fp_5 + 0x1094d8) s% $v0_18 * 0x188
   ```

2. **Stream Buffer Size Storage**: Offset `0x1094c4` from channel base
   - From `IMP_Encoder_GetStreamBufSize`:
   ```c
   *arg2 = *(arg1 * 0x308 + 0x1094c4)
   ```

3. **Stream Buffer Allocation**: In `IMP_Encoder_CreateChn`
   ```c
   int32_t $v0_24 = *($s2_3 + 0x1094c0) * 0x38
   void* $v0_25 = calloc($v0_24 * 7, 1)
   *($s2_3 + 0x1094d4) = $v0_25
   ```
   - Allocates `stream_buf_count * 0x38 * 7` bytes for metadata structures
   - The `0x38` (56 bytes) is NOT the encoded data buffer size

4. **Actual Stream Buffer Size Usage**:
   ```c
   int32_t $v0_28 = *($s2_3 + 0x1094c4) << 3
   ```
   - The stream buffer size is shifted left by 3 (multiplied by 8)
   - This suggests the stored value is in units of 8 bytes

## Changes Made

### 1. codec.c - Default Stream Buffer Size
**File**: `src/codec.c`
**Line**: 221
**Change**:
```c
// OLD:
enc->stream_buf_size = 0x38;        /* Stream buffer size */

// NEW:
enc->stream_buf_size = 0x20000;     /* 128KB stream buffer size (for encoded H.264 data) */
```

**Rationale**: 
- `0x38` (56 bytes) is way too small for encoded H.264 frames
- For 1920x1080:
  - I-frames can be 100-500KB
  - P-frames can be 10-50KB
- `0x20000` (128KB) provides reasonable buffer space

### 2. imp_encoder.c - Stream Buffer Metadata Allocation
**File**: `src/imp_encoder.c`
**Lines**: 542-545
**Change**:
```c
// OLD:
int stream_size = 0x38; /* Size per stream buffer */
size_t total_size = stream_cnt * stream_size * 7;

// NEW:
int stream_size = 0x188; /* Size per stream buffer metadata structure (0x188 from BN MCP) */
size_t total_size = stream_cnt * stream_size;
```

**Rationale**:
- `0x188` (392 bytes) is the correct size for the stream buffer metadata structure
- Removed the `* 7` multiplier as it was incorrect based on BN MCP analysis

## Testing Status

### Current Status
- Library compiled successfully
- Deployed to device at 192.168.50.211
- Streamer restarted
- **NEEDS TESTING**: ffprobe/VLC connection to verify frames are now being transmitted

### Test Commands
```bash
# Test with ffprobe
ffprobe -v debug -rtsp_flags prefer_tcp -i rtsp://admin:admin@192.168.50.211:554/ch0

# Test with VLC
vlc rtsp://admin:admin@192.168.50.211:554/ch0
```

### Expected Behavior After Fix
- ffprobe should progress past "hello state=0"
- Should see "Stream #0:0: Video: h264" output
- Should display frame information and codec details
- VLC should display video

## Next Steps

1. **Verify the fix works** by testing RTSP stream with ffprobe/VLC
2. **If still not working**, investigate:
   - Check if stream buffer size needs to be set via `IMP_Encoder_SetStreamBufSize()` before channel creation
   - Verify the software encoder in `hw_encoder.c` is generating valid H.264 NAL units
   - Check if the frame data is being properly copied to the stream buffers
3. **Monitor logs** on device:
   ```bash
   ssh root@192.168.50.211 "tail -f /tmp/streamer.log"
   ```

## Related Files
- `src/codec.c` - Codec layer, sets default stream buffer size
- `src/imp_encoder.c` - Encoder implementation, allocates stream buffers
- `src/hw_encoder.c` - Software H.264 encoder fallback
- `include/imp/imp_encoder.h` - Encoder API definitions

## Binary Ninja MCP Functions Analyzed
- `IMP_Encoder_CreateChn` (0x836e0)
- `IMP_Encoder_GetStream_Impl` (0x84c5c)
- `IMP_Encoder_GetStreamBufSize` (0x86750)
- `AL_Codec_Encode_GetSrcStreamCntAndSize` (0x7a6ac)

