/**
 * Hardware Encoder Implementation
 * Interface to Ingenic hardware H.264/H.265 encoder
 * Based on reverse engineering of libimp.so v1.1.6
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include "hw_encoder.h"

#define LOG_HW(fmt, ...) fprintf(stderr, "[HW_Encoder] " fmt "\n", ##__VA_ARGS__)

/* Global flag to force next frame to be IDR */
static int g_force_idr = 0;

/**
 * Initialize hardware encoder
 */
int HW_Encoder_Init(int *fd, HWEncoderParams *params) {
    if (fd == NULL || params == NULL) {
        return -1;
    }

    /* Try to open hardware encoder device - try multiple possible paths */
    const char *device_paths[] = {
        HW_ENCODER_DEVICE,
        HW_ENCODER_DEVICE_ALT1,
        HW_ENCODER_DEVICE_ALT2,
        HW_ENCODER_DEVICE_ALT3,
        NULL
    };

    int dev_fd = -1;
    const char *opened_device = NULL;

    for (int i = 0; device_paths[i] != NULL; i++) {
        dev_fd = open(device_paths[i], O_RDWR);
        if (dev_fd >= 0) {
            opened_device = device_paths[i];
            LOG_HW("Opened hardware encoder device: %s (fd=%d)", opened_device, dev_fd);
            /* IMPORTANT: /dev/avpu does not speak our legacy VENC ioctls.
             * The vendor path uses an AL layer. Avoid issuing unknown ioctls
             * to the avpu driver and fall back to software until AL is wired.
             */
            if (strcmp(opened_device, "/dev/avpu") == 0) {
                LOG_HW("/dev/avpu requires AL layer; skipping legacy VENC ioctls (fallback to SW)");
                close(dev_fd);
                *fd = -1;
                return -1;
            }
            break;
        }
    }

    if (dev_fd < 0) {
        LOG_HW("Failed to open hardware encoder (tried /dev/venc, /dev/jz-venc, /dev/h264enc)");
        LOG_HW("Hardware encoder not available, using software fallback");
        *fd = -1;
        return -1;
    }

    /* Log parameters before attempting initialization */
    LOG_HW("Attempting to initialize hardware encoder:");
    LOG_HW("  Codec: %s (type=%u)", params->codec_type == HW_CODEC_H264 ? "H.264" :
                          params->codec_type == HW_CODEC_H265 ? "H.265" : "JPEG",
                          params->codec_type);
    LOG_HW("  Profile: %u (%s)", params->profile,
                          params->profile == HW_PROFILE_BASELINE ? "Baseline" :
                          params->profile == HW_PROFILE_MAIN ? "Main" :
                          params->profile == HW_PROFILE_HIGH ? "High" : "Unknown");
    LOG_HW("  Resolution: %ux%u", params->width, params->height);
    LOG_HW("  FPS: %u/%u", params->fps_num, params->fps_den);
    LOG_HW("  GOP: %u", params->gop_length);
    LOG_HW("  RC Mode: %u", params->rc_mode);
    LOG_HW("  Bitrate: %u bps", params->bitrate);

    /* Initialize encoder with parameters */
    if (ioctl(dev_fd, VENC_IOCTL_INIT, params) < 0) {
        LOG_HW("VENC_IOCTL_INIT failed: %s", strerror(errno));
        LOG_HW("Hardware encoder initialization failed, falling back to software");
        close(dev_fd);
        *fd = -1;
        return -1;
    }

    LOG_HW("Hardware encoder initialized successfully on %s", opened_device);

    *fd = dev_fd;
    return 0;
}

/**
 * Deinitialize hardware encoder
 */
int HW_Encoder_Deinit(int fd) {
    if (fd < 0) {
        return 0; /* Already closed or not initialized */
    }

    /* Deinitialize encoder */
    if (ioctl(fd, VENC_IOCTL_DEINIT, NULL) < 0) {
        LOG_HW("VENC_IOCTL_DEINIT failed: %s", strerror(errno));
    }

    close(fd);
    LOG_HW("Hardware encoder deinitialized");
    return 0;
}

/**
 * Encode a frame
 */
int HW_Encoder_Encode(int fd, HWFrameBuffer *frame) {
    if (fd < 0 || frame == NULL) {
        return -1;
    }

    /* Submit frame for encoding */
    if (ioctl(fd, VENC_IOCTL_ENCODE, frame) < 0) {
        LOG_HW("VENC_IOCTL_ENCODE failed: %s", strerror(errno));
        return -1;
    }

    LOG_HW("Frame submitted for encoding: %ux%u, phys=0x%x, ts=%llu",
           frame->width, frame->height, frame->phys_addr,
           (unsigned long long)frame->timestamp);

    return 0;
}

/**
 * Get encoded stream
 */
int HW_Encoder_GetStream(int fd, HWStreamBuffer *stream, int timeout_ms) {
    if (fd < 0 || stream == NULL) {
        return -1;
    }

    /* Clear stream buffer */
    memset(stream, 0, sizeof(HWStreamBuffer));

    /* Get encoded stream from hardware */
    /* Note: timeout_ms is passed via stream->reserved[0] */
    stream->reserved[0] = timeout_ms;

    if (ioctl(fd, VENC_IOCTL_GET_STREAM, stream) < 0) {
        if (errno == EAGAIN || errno == ETIMEDOUT) {
            /* No stream available */
            return -1;
        }
        LOG_HW("VENC_IOCTL_GET_STREAM failed: %s", strerror(errno));
        return -1;
    }

    LOG_HW("Got encoded stream: length=%u, type=%s, ts=%llu",
           stream->length,
           stream->frame_type == HW_FRAME_TYPE_I ? "I" :
           stream->frame_type == HW_FRAME_TYPE_P ? "P" : "B",
           (unsigned long long)stream->timestamp);

    return 0;
}

/**
 * Release stream buffer
 */
int HW_Encoder_ReleaseStream(int fd, HWStreamBuffer *stream) {
    if (fd < 0 || stream == NULL) {
        return -1;
    }

    /* Release stream buffer back to hardware */
    if (ioctl(fd, VENC_IOCTL_RELEASE, stream) < 0) {
        LOG_HW("VENC_IOCTL_RELEASE failed: %s", strerror(errno));
        return -1;
    }

    LOG_HW("Stream buffer released");
    return 0;
}

/**
 * Set encoder parameters
 */
int HW_Encoder_SetParams(int fd, HWEncoderParams *params) {
    if (fd < 0 || params == NULL) {
        return -1;
    }

    /* Set encoder parameters */
    if (ioctl(fd, VENC_IOCTL_SET_PARAM, params) < 0) {
        LOG_HW("VENC_IOCTL_SET_PARAM failed: %s", strerror(errno));
        return -1;
    }

    LOG_HW("Encoder parameters updated");
    return 0;
}

/**
 * Write Exp-Golomb encoded value
 */
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
        int byte_pos = (*bit_pos) / 8;
        int bit_offset = 7 - ((*bit_pos) % 8);
        buf[byte_pos] &= ~(1 << bit_offset);
        (*bit_pos)++;
    }

    /* Write the value */
    for (int i = leading_zeros; i >= 0; i--) {
        int byte_pos = (*bit_pos) / 8;
        int bit_offset = 7 - ((*bit_pos) % 8);
        if (v & (1 << i)) {
            buf[byte_pos] |= (1 << bit_offset);
        } else {
            buf[byte_pos] &= ~(1 << bit_offset);
        }
        (*bit_pos)++;
    }
}

/**
 * Write single bit
 */
static void write_bit(uint8_t *buf, int *bit_pos, int value) {
    int byte_pos = (*bit_pos) / 8;
    int bit_offset = 7 - ((*bit_pos) % 8);
    if (value) {
        buf[byte_pos] |= (1 << bit_offset);
    } else {
        buf[byte_pos] &= ~(1 << bit_offset);
    }
    (*bit_pos)++;
}

/**
 * Write multiple bits
 */
static void write_bits(uint8_t *buf, int *bit_pos, uint32_t value, int num_bits) {
    for (int i = num_bits - 1; i >= 0; i--) {
        write_bit(buf, bit_pos, (value >> i) & 1);
    }
}

/**
 * Generate H.264 SPS (Sequence Parameter Set) NAL unit
 * Based on AL_AVC_GenerateSPS from Binary Ninja decompilation
 */
static int generate_h264_sps(uint8_t *buf, int width, int height) {
    int pos = 0;

    /* Start code */
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;
    buf[pos++] = 0x01;

    /* NAL header: SPS (0x67 = 0x60 | 0x07) */
    buf[pos++] = 0x67;

    /* SPS data starts here */
    int bit_pos = pos * 8;
    memset(&buf[pos], 0, 100); /* Clear buffer for bit operations */

    /* profile_idc = 66 (Baseline) */
    write_bits(buf, &bit_pos, 66, 8);

    /* constraint flags */
    write_bit(buf, &bit_pos, 1); /* constraint_set0_flag */
    write_bit(buf, &bit_pos, 1); /* constraint_set1_flag */
    write_bit(buf, &bit_pos, 0); /* constraint_set2_flag */
    write_bit(buf, &bit_pos, 0); /* constraint_set3_flag */
    write_bits(buf, &bit_pos, 0, 4); /* reserved_zero_4bits */

    /* level_idc = 31 (Level 3.1) */
    write_bits(buf, &bit_pos, 31, 8);

    /* seq_parameter_set_id */
    write_exp_golomb(buf, &bit_pos, 0);

    /* log2_max_frame_num_minus4 */
    write_exp_golomb(buf, &bit_pos, 0);

    /* pic_order_cnt_type */
    write_exp_golomb(buf, &bit_pos, 0);

    /* log2_max_pic_order_cnt_lsb_minus4 */
    write_exp_golomb(buf, &bit_pos, 0);

    /* max_num_ref_frames */
    write_exp_golomb(buf, &bit_pos, 1);

    /* gaps_in_frame_num_value_allowed_flag */
    write_bit(buf, &bit_pos, 0);

    /* pic_width_in_mbs_minus1 / pic_height_in_map_units_minus1 (use ceil) */
    int mb_width  = (width  + 15) / 16;
    int mb_height = (height + 15) / 16;
    write_exp_golomb(buf, &bit_pos, mb_width - 1);
    write_exp_golomb(buf, &bit_pos, mb_height - 1);

    /* frame_mbs_only_flag */
    write_bit(buf, &bit_pos, 1);

    /* direct_8x8_inference_flag */
    write_bit(buf, &bit_pos, 1);

    /* frame_cropping_flag + offsets when not multiple of 16 (4:2:0 crop units = 2) */
    int crop_right  = (mb_width  * 16 - width)  / 2;
    int crop_bottom = (mb_height * 16 - height) / 2;
    int need_crop = (crop_right > 0) || (crop_bottom > 0);
    write_bit(buf, &bit_pos, need_crop ? 1 : 0);
    if (need_crop) {
        /* frame_crop_left_offset */  write_exp_golomb(buf, &bit_pos, 0);
        /* frame_crop_right_offset */ write_exp_golomb(buf, &bit_pos, crop_right);
        /* frame_crop_top_offset */   write_exp_golomb(buf, &bit_pos, 0);
        /* frame_crop_bottom_offset */write_exp_golomb(buf, &bit_pos, crop_bottom);
    }

    /* vui_parameters_present_flag */
    write_bit(buf, &bit_pos, 1);

    /* --- VUI parameters (timing + HRD) --- */
    /* aspect_ratio_info_present_flag */ write_bit(buf, &bit_pos, 0);
    /* overscan_info_present_flag */     write_bit(buf, &bit_pos, 0);
    /* video_signal_type_present_flag */ write_bit(buf, &bit_pos, 0);
    /* chroma_location_info_present_flag */ write_bit(buf, &bit_pos, 0);

    /* timing_info_present_flag */
    write_bit(buf, &bit_pos, 1);
    /* num_units_in_tick (default 1) */ write_bits(buf, &bit_pos, 1, 32);
    /* time_scale = 60 (for 30fps fixed) */ write_bits(buf, &bit_pos, 60, 32);
    /* fixed_frame_rate_flag */ write_bit(buf, &bit_pos, 1);

    /* nal_hrd_parameters_present_flag */
    write_bit(buf, &bit_pos, 1);
    /* hrd: cpb_cnt_minus1 */ write_exp_golomb(buf, &bit_pos, 0);
    /* bit_rate_scale(4) */ write_bits(buf, &bit_pos, 4, 4);
    /* cpb_size_scale(4) */ write_bits(buf, &bit_pos, 4, 4);
    /* sched_sel_idx = 0 entry */
    /* bit_rate_value_minus1 */ write_exp_golomb(buf, &bit_pos, 0);
    /* cpb_size_value_minus1 */ write_exp_golomb(buf, &bit_pos, 0);
    /* cbr_flag */ write_bit(buf, &bit_pos, 1);
    /* initial_cpb_removal_delay_length_minus1 (5) -> 31 for 32-bit fields */ write_bits(buf, &bit_pos, 31, 5);
    /* cpb_removal_delay_length_minus1 (5) -> 31 */ write_bits(buf, &bit_pos, 31, 5);
    /* dpb_output_delay_length_minus1 (5) -> 31 */ write_bits(buf, &bit_pos, 31, 5);
    /* time_offset_length (5) */ write_bits(buf, &bit_pos, 0, 5);

    /* vcl_hrd_parameters_present_flag */ write_bit(buf, &bit_pos, 0);
    /* low_delay_hrd_flag (present if any hrd present) */ write_bit(buf, &bit_pos, 0);

    /* pic_struct_present_flag (required for PicTiming SEI) */ write_bit(buf, &bit_pos, 1);
    /* bitstream_restriction_flag */ write_bit(buf, &bit_pos, 0);

    /* RBSP trailing bits */
    write_bit(buf, &bit_pos, 1);
    while (bit_pos % 8 != 0) { write_bit(buf, &bit_pos, 0); }

    return bit_pos / 8;
}

/**
 * Generate H.264 PPS (Picture Parameter Set) NAL unit
 * Based on AL_AVC_GeneratePPS from Binary Ninja decompilation
 */
static int generate_h264_pps(uint8_t *buf) {
    int pos = 0;

    /* Start code */
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;
    buf[pos++] = 0x01;

    /* NAL header: PPS (0x68 = 0x60 | 0x08) */
    buf[pos++] = 0x68;

    /* PPS data starts here */
    int bit_pos = pos * 8;
    memset(&buf[pos], 0, 50); /* Clear buffer for bit operations */

    /* pic_parameter_set_id */
    write_exp_golomb(buf, &bit_pos, 0);

    /* seq_parameter_set_id */
    write_exp_golomb(buf, &bit_pos, 0);

    /* entropy_coding_mode_flag (0 = CAVLC) */
    write_bit(buf, &bit_pos, 0);

    /* bottom_field_pic_order_in_frame_present_flag */
    write_bit(buf, &bit_pos, 0);

    /* num_slice_groups_minus1 */
    write_exp_golomb(buf, &bit_pos, 0);

    /* num_ref_idx_l0_default_active_minus1 */
    write_exp_golomb(buf, &bit_pos, 0);

    /* num_ref_idx_l1_default_active_minus1 */
    write_exp_golomb(buf, &bit_pos, 0);

    /* weighted_pred_flag */
    write_bit(buf, &bit_pos, 0);

    /* weighted_bipred_idc */
    write_bits(buf, &bit_pos, 0, 2);

    /* pic_init_qp_minus26 */
    write_exp_golomb(buf, &bit_pos, 0);

    /* pic_init_qs_minus26 */
    write_exp_golomb(buf, &bit_pos, 0);

    /* chroma_qp_index_offset */
    write_exp_golomb(buf, &bit_pos, 0);

    /* deblocking_filter_control_present_flag */
    write_bit(buf, &bit_pos, 1);

    /* constrained_intra_pred_flag */
    write_bit(buf, &bit_pos, 0);

    /* redundant_pic_cnt_present_flag */
    write_bit(buf, &bit_pos, 0);

    /* RBSP trailing bits */
    write_bit(buf, &bit_pos, 1);

    /* Byte align */
    while (bit_pos % 8 != 0) {
        write_bit(buf, &bit_pos, 0);
    }

    return bit_pos / 8;
}

/**
 * Generate H.264 IDR slice NAL unit
 */
static int generate_h264_idr_slice(uint8_t *buf, int width, int height, uint32_t frame_num) {
    int pos = 0;

    /* Start code */
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;
    buf[pos++] = 0x01;

    /* NAL header: IDR slice (0x65 = 0x60 | 0x05) */
    buf[pos++] = 0x65;

    /* Slice header */
    int bit_pos = pos * 8;
    memset(&buf[pos], 0, 100);

    /* first_mb_in_slice */
    write_exp_golomb(buf, &bit_pos, 0);

    /* slice_type (7 = I slice, all macroblocks are I) */
    write_exp_golomb(buf, &bit_pos, 7);

    /* pic_parameter_set_id */
    write_exp_golomb(buf, &bit_pos, 0);

    /* frame_num */
    write_bits(buf, &bit_pos, frame_num & 0xF, 4);

    /* idr_pic_id */
    write_exp_golomb(buf, &bit_pos, 0);

    /* pic_order_cnt_lsb */
    write_bits(buf, &bit_pos, 0, 4);

    /* dec_ref_pic_marking */
    write_bit(buf, &bit_pos, 0); /* no_output_of_prior_pics_flag */
    write_bit(buf, &bit_pos, 0); /* long_term_reference_flag */

    /* Slice data (minimal dummy macroblock data) */
    /* mb_skip_run for skipped macroblocks */
    int num_mbs = (width / 16) * (height / 16);
    write_exp_golomb(buf, &bit_pos, num_mbs - 1);

    /* RBSP trailing bits */
    write_bit(buf, &bit_pos, 1);

    /* Byte align */
    while (bit_pos % 8 != 0) {
        write_bit(buf, &bit_pos, 0);
    }

    return bit_pos / 8;
}

/**
 * Generate H.264 P slice NAL unit
 */
static int generate_h264_p_slice(uint8_t *buf, int width, int height, uint32_t frame_num) {
    int pos = 0;

    /* Start code */
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;
    buf[pos++] = 0x01;

    /* NAL header: P slice (0x41 = 0x40 | 0x01) */
    buf[pos++] = 0x41;

    /* Slice header */
    int bit_pos = pos * 8;
    memset(&buf[pos], 0, 100);

    /* first_mb_in_slice */
    write_exp_golomb(buf, &bit_pos, 0);

    /* slice_type (5 = P slice, all macroblocks are P) */
    write_exp_golomb(buf, &bit_pos, 5);

    /* pic_parameter_set_id */
    write_exp_golomb(buf, &bit_pos, 0);

    /* frame_num */
    write_bits(buf, &bit_pos, frame_num & 0xF, 4);

    /* pic_order_cnt_lsb */
    write_bits(buf, &bit_pos, frame_num & 0xF, 4);

    /* Slice data (minimal dummy macroblock data) */
    /* mb_skip_run for skipped macroblocks */
    int num_mbs = (width / 16) * (height / 16);
    write_exp_golomb(buf, &bit_pos, num_mbs - 1);

    /* RBSP trailing bits */
    write_bit(buf, &bit_pos, 1);

    /* Byte align */
    while (bit_pos % 8 != 0) {
        write_bit(buf, &bit_pos, 0);
    }

    return bit_pos / 8;
}

/* --- BN MCP-compatible NAL writing helpers (AUD + EPB insertion) --- */
static int write_nal_epb(uint8_t *dst, uint8_t nal_header, const uint8_t *rbsp, int rbsp_len) {
    int pos = 0;
    /* Annex B start code */
    dst[pos++] = 0x00; dst[pos++] = 0x00; dst[pos++] = 0x00; dst[pos++] = 0x01;
    /* NAL header */
    dst[pos++] = nal_header;
    /* Emulation prevention: insert 0x03 after 00 00 before {00,01,02,03} */
    int zeros = 0;
    for (int i = 0; i < rbsp_len; i++) {
        uint8_t b = rbsp[i];
        if (zeros >= 2 && b <= 0x03) {
            dst[pos++] = 0x03;
            zeros = 0;
        }
        dst[pos++] = b;
        zeros = (b == 0x00) ? (zeros + 1) : 0;
    }
    return pos;
}

static int build_aud_rbsp(uint8_t *rbsp, int is_idr) {
    /* primary_pic_type: 0 = I, 1 = P/I (no B in our stream) */
    int bit_pos = 0;
    memset(rbsp, 0, 8);
    uint32_t primary_pic_type = is_idr ? 0 : 1;
    for (int i = 2; i >= 0; i--) {
        int byte_pos = bit_pos / 8;
        int bit_off = 7 - (bit_pos % 8);
        if ((primary_pic_type >> i) & 1) rbsp[byte_pos] |= (1 << bit_off);
        bit_pos++;
    }
    int byte_pos = bit_pos / 8; int bit_off = 7 - (bit_pos % 8);
    rbsp[byte_pos] |= (1 << bit_off); bit_pos++;
    while (bit_pos % 8) {
        byte_pos = bit_pos / 8; bit_off = 7 - (bit_pos % 8);
        rbsp[byte_pos] &= ~(1 << bit_off); bit_pos++;
    }
    return bit_pos / 8;
}

/* --- SEI helpers (buffering_period + picture_timing) --- */
static int sei_write_header(uint8_t *dst, int payload_type, int payload_size) {
    int pos = 0;
    int t = payload_type;
    while (t >= 255) { dst[pos++] = 255; t -= 255; }
    dst[pos++] = (uint8_t)t;
    int s = payload_size;
    while (s >= 255) { dst[pos++] = 255; s -= 255; }
    dst[pos++] = (uint8_t)s;
    return pos;
}

static int build_sei_buffering_period_rbsp(uint8_t *rbsp, uint32_t init_delay, uint32_t init_offset) {
    uint8_t payload[32]; memset(payload, 0, sizeof(payload));
    int bit_pos = 0;
    write_exp_golomb(payload, &bit_pos, 0); /* seq_parameter_set_id */
    write_bits(payload, &bit_pos, init_delay, 32);
    write_bits(payload, &bit_pos, init_offset, 32);
    while (bit_pos % 8) write_bit(payload, &bit_pos, 0);
    int payload_len = bit_pos / 8;

    int pos = 0;
    pos += sei_write_header(rbsp + pos, 0 /*buffering_period*/, payload_len);
    memcpy(rbsp + pos, payload, payload_len); pos += payload_len;
    rbsp[pos++] = 0x80; /* rbsp_trailing_bits */
    return pos;
}

static int build_sei_picture_timing_rbsp(uint8_t *rbsp, uint32_t cpb_removal_delay,
                                         uint32_t dpb_output_delay, int pic_struct) {
    uint8_t payload[32]; memset(payload, 0, sizeof(payload));
    int bit_pos = 0;
    write_bits(payload, &bit_pos, cpb_removal_delay, 32);
    write_bits(payload, &bit_pos, dpb_output_delay, 32);
    write_bits(payload, &bit_pos, pic_struct & 0xF, 4);
    while (bit_pos % 8) write_bit(payload, &bit_pos, 0);
    int payload_len = bit_pos / 8;

    int pos = 0;
    pos += sei_write_header(rbsp + pos, 1 /*pic_timing*/, payload_len);
    memcpy(rbsp + pos, payload, payload_len); pos += payload_len;
    rbsp[pos++] = 0x80; /* rbsp_trailing_bits */
    return pos;
}

/* Pack existing generator output (with startcode+header) using EPB-accurate writer */
static int repack_with_epb(uint8_t *dst, const uint8_t *src_nal, int src_len) {
    if (src_len < 6) return 0; /* too small */
    uint8_t header = src_nal[4];
    const uint8_t *rbsp = src_nal + 5;
    int rbsp_len = src_len - 5;
    return write_nal_epb(dst, header, rbsp, rbsp_len);
}

/**
 * Software fallback encoder with BN MCP-like AU sequencing and EPB insertion
 */
int HW_Encoder_Encode_Software(HWFrameBuffer *frame, HWStreamBuffer *stream) {
    if (frame == NULL || stream == NULL) {
        return -1;
    }

    static uint32_t frame_counter = 0;

    /* Allocate buffer for NAL units */
    uint8_t *nal_buffer = (uint8_t*)malloc(8192);
    if (nal_buffer == NULL) {
        LOG_HW("Software encoding: failed to allocate NAL buffer");
        return -1;
    }

    uint8_t tmp[4096];
    int total_size = 0;

    /* Decide frame type */
    int is_idr = ((frame_counter % 30) == 0) || g_force_idr;
    if (g_force_idr) {
        LOG_HW("Software encoding: Forcing IDR frame (requested by IMP_Encoder_RequestIDR)");
        g_force_idr = 0;
    }

    /* AUD first (nal_unit_type=9, nal_ref_idc=0) */
    uint8_t aud_rbsp[8];
    int aud_rbsp_len = build_aud_rbsp(aud_rbsp, is_idr);
    total_size += write_nal_epb(nal_buffer + total_size, 0x09, aud_rbsp, aud_rbsp_len);
    /* SEI: buffering_period on IDR, pic_timing every AU */
    static uint32_t g_cpb_removal_delay = 0;
    uint8_t sei_rbsp_out[64];
    int sei_len_out = 0;
    if (is_idr) {
        sei_len_out = build_sei_buffering_period_rbsp(sei_rbsp_out, 0, 0);
        total_size += write_nal_epb(nal_buffer + total_size, 0x06, sei_rbsp_out, sei_len_out);
    }
    sei_len_out = build_sei_picture_timing_rbsp(sei_rbsp_out, g_cpb_removal_delay++, 0, 0);
    total_size += write_nal_epb(nal_buffer + total_size, 0x06, sei_rbsp_out, sei_len_out);


    if (is_idr) {
        /* SPS */
        int sps_len_raw = generate_h264_sps(tmp, frame->width, frame->height);
        total_size += repack_with_epb(nal_buffer + total_size, tmp, sps_len_raw);
        /* PPS */
        int pps_len_raw = generate_h264_pps(tmp);
        total_size += repack_with_epb(nal_buffer + total_size, tmp, pps_len_raw);
        /* IDR slice */
        int idr_len_raw = generate_h264_idr_slice(tmp, frame->width, frame->height, frame_counter);
        total_size += repack_with_epb(nal_buffer + total_size, tmp, idr_len_raw);

        stream->frame_type = HW_FRAME_TYPE_I;
        stream->slice_type = 0;

        LOG_HW("Software encoding: IDR frame %u, total=%d bytes", frame_counter, total_size);
    } else {
        /* P slice */
        int p_len_raw = generate_h264_p_slice(tmp, frame->width, frame->height, frame_counter);
        total_size += repack_with_epb(nal_buffer + total_size, tmp, p_len_raw);

        stream->frame_type = HW_FRAME_TYPE_P;
        stream->slice_type = 1;

        LOG_HW("Software encoding: P frame %u, total=%d bytes", frame_counter, total_size);
    }

    /* Populate stream buffer */
    stream->virt_addr = (uint32_t)(uintptr_t)nal_buffer;
    stream->phys_addr = 0;
    stream->length = total_size;
    stream->timestamp = frame->timestamp;

    frame_counter++;
    return 0;
}

/**
 * Request IDR frame on next encode
 */
void HW_Encoder_RequestIDR(void) {
    g_force_idr = 1;
    LOG_HW("RequestIDR: next frame will be IDR");
}

