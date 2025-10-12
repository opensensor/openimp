# H.264 NAL Unit Generation Implementation

## Overview

Implemented proper H.264 NAL unit generation in the software encoder based on Binary Ninja MCP analysis of the original `libimp.so` functions `AL_AVC_GenerateSPS` and `AL_AVC_GeneratePPS`.

## Problem

The previous software encoder was generating dummy data that looked like H.264 (had start codes and NAL headers) but wasn't actually valid H.264 that could be decoded by ffmpeg/mpv:

```
[ffmpeg/demuxer] rtsp: Could not find codec parameters for stream 0 (Video: h264, none): unspecified size
```

## Solution

Implemented proper H.264 NAL unit generation with:

1. **SPS (Sequence Parameter Set)** - Contains video parameters (resolution, profile, level)
2. **PPS (Picture Parameter Set)** - Contains picture parameters (entropy mode, QP settings)
3. **IDR Slice** - Intra-coded slice (keyframe)
4. **P Slice** - Predicted slice (inter-frame)

## Implementation Details

### Binary Ninja MCP Analysis

Used Binary Ninja MCP to analyze the original encoder functions:

```bash
# Functions analyzed:
- AL_AVC_GenerateSPS (address 0x490a8)
- AL_AVC_GeneratePPS (address 0x493ac)
- generateNals (address 0x4967c)
- WriteNal (address 0x46290)
- FlushNAL (address 0x47c70)
```

### H.264 NAL Unit Structure

Each NAL unit follows this structure:

```
[Start Code] [NAL Header] [RBSP Data] [Trailing Bits]
   4 bytes      1 byte      variable      1+ bits
```

**Start Code**: `0x00 0x00 0x00 0x01`

**NAL Header**:
- SPS: `0x67` (forbidden_zero_bit=0, nal_ref_idc=3, nal_unit_type=7)
- PPS: `0x68` (forbidden_zero_bit=0, nal_ref_idc=3, nal_unit_type=8)
- IDR: `0x65` (forbidden_zero_bit=0, nal_ref_idc=3, nal_unit_type=5)
- P:   `0x41` (forbidden_zero_bit=0, nal_ref_idc=2, nal_unit_type=1)

### SPS (Sequence Parameter Set)

Generated for every IDR frame (every 30 frames):

```c
- profile_idc = 66 (Baseline Profile)
- constraint_set0_flag = 1
- constraint_set1_flag = 1
- level_idc = 31 (Level 3.1)
- seq_parameter_set_id = 0
- log2_max_frame_num_minus4 = 0
- pic_order_cnt_type = 0
- log2_max_pic_order_cnt_lsb_minus4 = 0
- max_num_ref_frames = 1
- gaps_in_frame_num_value_allowed_flag = 0
- pic_width_in_mbs_minus1 = (width/16) - 1
- pic_height_in_map_units_minus1 = (height/16) - 1
- frame_mbs_only_flag = 1
- direct_8x8_inference_flag = 1
- frame_cropping_flag = 0
- vui_parameters_present_flag = 0
```

### PPS (Picture Parameter Set)

Generated for every IDR frame:

```c
- pic_parameter_set_id = 0
- seq_parameter_set_id = 0
- entropy_coding_mode_flag = 0 (CAVLC)
- bottom_field_pic_order_in_frame_present_flag = 0
- num_slice_groups_minus1 = 0
- num_ref_idx_l0_default_active_minus1 = 0
- num_ref_idx_l1_default_active_minus1 = 0
- weighted_pred_flag = 0
- weighted_bipred_idc = 0
- pic_init_qp_minus26 = 0
- pic_init_qs_minus26 = 0
- chroma_qp_index_offset = 0
- deblocking_filter_control_present_flag = 1
- constrained_intra_pred_flag = 0
- redundant_pic_cnt_present_flag = 0
```

### IDR Slice

Generated every 30 frames:

```c
- first_mb_in_slice = 0
- slice_type = 7 (I slice, all MBs are I)
- pic_parameter_set_id = 0
- frame_num = frame_counter & 0xF
- idr_pic_id = 0
- pic_order_cnt_lsb = 0
- no_output_of_prior_pics_flag = 0
- long_term_reference_flag = 0
- mb_skip_run = num_mbs - 1 (all macroblocks skipped)
```

### P Slice

Generated for non-IDR frames:

```c
- first_mb_in_slice = 0
- slice_type = 5 (P slice, all MBs are P)
- pic_parameter_set_id = 0
- frame_num = frame_counter & 0xF
- pic_order_cnt_lsb = frame_counter & 0xF
- mb_skip_run = num_mbs - 1 (all macroblocks skipped)
```

## Exp-Golomb Encoding

H.264 uses Exp-Golomb encoding for many syntax elements. Implementation:

```c
static void write_exp_golomb(uint8_t *buf, int *bit_pos, uint32_t value) {
    uint32_t v = value + 1;
    int leading_zeros = 0;
    uint32_t temp = v;
    
    /* Count leading zeros */
    while (temp > 1) {
        temp >>= 1;
        leading_zeros++;
    }
    
    /* Write leading zeros */
    for (int i = 0; i < leading_zeros; i++) {
        write_bit(buf, bit_pos, 0);
    }
    
    /* Write the value */
    for (int i = leading_zeros; i >= 0; i--) {
        write_bit(buf, bit_pos, (v >> i) & 1);
    }
}
```

## Frame Structure

**IDR Frame** (every 30 frames):
```
[SPS NAL] [PPS NAL] [IDR Slice NAL]
```

**P Frame** (other frames):
```
[P Slice NAL]
```

## Files Modified

- **`src/hw_encoder.c`** - Implemented H.264 NAL unit generation (lines 165-552)
  - Added `write_exp_golomb()` - Exp-Golomb encoding
  - Added `write_bit()` - Single bit writing
  - Added `write_bits()` - Multi-bit writing
  - Added `generate_h264_sps()` - SPS NAL generation
  - Added `generate_h264_pps()` - PPS NAL generation
  - Added `generate_h264_idr_slice()` - IDR slice generation
  - Added `generate_h264_p_slice()` - P slice generation
  - Modified `HW_Encoder_Encode_Software()` - Main encoder function

## Build

```bash
cd /home/matteius/openimp
make clean && make
```

## Deployment

```bash
scp lib/libimp.so root@<device-ip>:/usr/lib/
ssh root@<device-ip> "killall streamer; /usr/bin/streamer > /tmp/streamer.log 2>&1 &"
```

## Testing

```bash
# Test with mpv
mpv rtsp://admin:admin@<device-ip>:554/ch0

# Test with ffplay
ffplay rtsp://<device-ip>:554/ch0

# Test with ffprobe
ffprobe rtsp://<device-ip>:554/ch0
```

## Expected Behavior

After deploying this fix:

1. **ffmpeg/mpv can parse the stream**: No more "Could not find codec parameters" errors
2. **Video dimensions are detected**: Stream shows as "h264, 1920x1080" or "h264, 640x360"
3. **Playback works**: Video displays (even though it's dummy data, it should show a gray/black screen)
4. **RTSP consumer starts**: The thingino-streamer RTSP module should start sending RTP packets

## Limitations

This is still a **software simulation** encoder that generates **dummy macroblock data** (all macroblocks are skipped). The video will display as a gray/black screen because there's no actual image data.

For real video:
- Load the Ingenic kernel modules to enable hardware encoding
- Or integrate a real software encoder like libx264

## References

- H.264 Specification: ITU-T Rec. H.264 (04/2017)
- Binary Ninja MCP analysis of libimp.so v1.1.6
- Original functions: AL_AVC_GenerateSPS, AL_AVC_GeneratePPS

