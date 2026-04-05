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

#include "imp_log_int.h"

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
    /* OEM tries /dev/venc first, then jz-venc, h264enc, avpu last */
    const char *device_paths[] = {
        HW_ENCODER_DEVICE_ALT1,  /* /dev/venc */
        HW_ENCODER_DEVICE_ALT2,  /* /dev/jz-venc */
        HW_ENCODER_DEVICE_ALT3,  /* /dev/h264enc */
        HW_ENCODER_DEVICE,       /* /dev/avpu */
        NULL
    };

    int dev_fd = -1;
    const char *opened_device = NULL;

    for (int i = 0; device_paths[i] != NULL; i++) {
        dev_fd = open(device_paths[i], O_RDWR);
        if (dev_fd >= 0) {
            opened_device = device_paths[i];
            LOG_HW("Opened hardware encoder device: %s (fd=%d)", opened_device, dev_fd);
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

    /* vui_parameters_present_flag = 0 (no VUI — simpler, avoids SEI dependency) */
    write_bit(buf, &bit_pos, 0);

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
 * Produces a valid IDR with all macroblocks coded as I_4x4 with zero residual
 * (solid grey frame). This is the minimum valid H.264 IDR that decoders accept.
 */
static int generate_h264_idr_slice(uint8_t *buf, int width, int height, uint32_t frame_num) {
    int pos = 0;

    /* Start code */
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;
    buf[pos++] = 0x01;

    /* NAL header: IDR slice, nal_ref_idc=3, nal_unit_type=5 → 0x65 */
    buf[pos++] = 0x65;

    int bit_pos = pos * 8;
    memset(&buf[pos], 0, 4096);

    /* --- Slice header --- */
    write_exp_golomb(buf, &bit_pos, 0);       /* first_mb_in_slice */
    write_exp_golomb(buf, &bit_pos, 7);       /* slice_type = 7 (I, all MBs) */
    write_exp_golomb(buf, &bit_pos, 0);       /* pic_parameter_set_id */
    write_bits(buf, &bit_pos, frame_num & 0xF, 4); /* frame_num (log2_max=4) */
    write_exp_golomb(buf, &bit_pos, 0);       /* idr_pic_id */
    write_bits(buf, &bit_pos, 0, 4);          /* pic_order_cnt_lsb (log2_max=4) */

    /* dec_ref_pic_marking (IDR) */
    write_bit(buf, &bit_pos, 0);  /* no_output_of_prior_pics_flag */
    write_bit(buf, &bit_pos, 0);  /* long_term_reference_flag */

    /* slice_qp_delta = 0 (CRITICAL: missing before caused QP 81 errors) */
    write_exp_golomb(buf, &bit_pos, 0);       /* slice_qp_delta (signed, 0 = no change) */

    /* deblocking_filter_control: disable_deblocking=1 (simplest) */
    write_exp_golomb(buf, &bit_pos, 1);       /* disable_deblocking_filter_idc = 1 */

    /* --- Slice data: encode all MBs as I_16x16 prediction mode 0 (vertical),
     * CBP luma=0, CBP chroma=0. In the I slice mb_type table:
     *   mb_type=1 → I_16x16_0_0_0 (pred=DC(0), CBP_luma=0, CBP_chroma=0)
     * This is the simplest valid I macroblock — no sub-block modes, no
     * coded_block_pattern field, no residual. Just mb_type + chroma pred. */
    int num_mbs = ((width + 15) / 16) * ((height + 15) / 16);
    for (int mb = 0; mb < num_mbs; mb++) {
        /* mb_type = 3 → I_16x16_2_0_0 (Intra16x16, pred=2(DC), CBP_C=0, CBP_L=0)
         * DC prediction doesn't need top/left neighbors (fixes "top block unavailable") */
        write_exp_golomb(buf, &bit_pos, 3);

        /* Intra chroma prediction mode = 0 (DC) */
        write_exp_golomb(buf, &bit_pos, 0);

        /* For I_16x16 with CBP=0: no mb_qp_delta, no residual */
    }

    /* RBSP trailing bits */
    write_bit(buf, &bit_pos, 1);
    while (bit_pos % 8 != 0) write_bit(buf, &bit_pos, 0);

    return bit_pos / 8;
}

/**
 * Generate H.264 P slice NAL unit
 * All macroblocks are skipped (P_Skip), producing a "repeat previous frame" effect.
 */
static int generate_h264_p_slice(uint8_t *buf, int width, int height, uint32_t frame_num) {
    int pos = 0;

    /* Start code */
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;
    buf[pos++] = 0x01;

    /* NAL header: non-IDR slice, nal_ref_idc=0, nal_unit_type=1 → 0x01
     * nal_ref_idc=0 means non-reference (fixes "reference frames exceeds max" error) */
    buf[pos++] = 0x01;

    int bit_pos = pos * 8;
    memset(&buf[pos], 0, 200);

    /* --- Slice header --- */
    write_exp_golomb(buf, &bit_pos, 0);       /* first_mb_in_slice */
    write_exp_golomb(buf, &bit_pos, 0);       /* slice_type = 0 (P) */
    write_exp_golomb(buf, &bit_pos, 0);       /* pic_parameter_set_id */
    write_bits(buf, &bit_pos, frame_num & 0xF, 4); /* frame_num */
    write_bits(buf, &bit_pos, (frame_num * 2) & 0xF, 4); /* pic_order_cnt_lsb */

    /* num_ref_idx_active_override_flag = 0 (use PPS default) */
    write_bit(buf, &bit_pos, 0);

    /* ref_pic_list_modification_flag_l0 = 0 */
    write_bit(buf, &bit_pos, 0);

    /* dec_ref_pic_marking: ONLY present when nal_ref_idc != 0.
     * Our P slice uses nal_ref_idc=0 (0x01), so NO marking syntax here. */

    /* slice_qp_delta = 0 */
    write_exp_golomb(buf, &bit_pos, 0);

    /* deblocking: disable=1 */
    write_exp_golomb(buf, &bit_pos, 1);

    /* --- Slice data: all MBs are P_Skip --- */
    int num_mbs = ((width + 15) / 16) * ((height + 15) / 16);
    /* mb_skip_run = num_mbs (skip all) */
    write_exp_golomb(buf, &bit_pos, num_mbs);

    /* RBSP trailing bits */
    write_bit(buf, &bit_pos, 1);
    while (bit_pos % 8 != 0) write_bit(buf, &bit_pos, 0);

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
int HW_Encoder_Encode_Software(HWFrameBuffer *frame, HWStreamBuffer *stream, uint32_t codec_type) {
    if (frame == NULL || stream == NULL) {
        return -1;
    }

    static uint32_t frame_counter = 0;

    if (codec_type == HW_CODEC_JPEG) {
        /* JPEG software fallback: generate a minimal JFIF with grey fill.
         * Real JPEG encoding would need the actual pixel data, but this
         * provides a valid JPEG container so rvd/rhd don't reject it. */
        uint8_t *buf = (uint8_t*)malloc(1024);
        if (!buf) return -1;

        /* Minimal JPEG: SOI + APP0 (JFIF) + DQT + SOF0 + SOS + EOI */
        int pos = 0;
        buf[pos++] = 0xFF; buf[pos++] = 0xD8; /* SOI */
        /* APP0 JFIF marker */
        buf[pos++] = 0xFF; buf[pos++] = 0xE0;
        buf[pos++] = 0x00; buf[pos++] = 0x10; /* length=16 */
        buf[pos++]='J'; buf[pos++]='F'; buf[pos++]='I'; buf[pos++]='F'; buf[pos++]=0;
        buf[pos++] = 0x01; buf[pos++] = 0x01; /* version 1.1 */
        buf[pos++] = 0x00; /* aspect ratio units */
        buf[pos++] = 0x00; buf[pos++] = 0x01; /* X density */
        buf[pos++] = 0x00; buf[pos++] = 0x01; /* Y density */
        buf[pos++] = 0x00; buf[pos++] = 0x00; /* no thumbnail */
        /* Quantization table */
        buf[pos++] = 0xFF; buf[pos++] = 0xDB;
        buf[pos++] = 0x00; buf[pos++] = 0x43; /* length=67 */
        buf[pos++] = 0x00; /* table 0, 8-bit precision */
        for (int i = 0; i < 64; i++) buf[pos++] = 16; /* uniform QT */
        /* SOF0 */
        buf[pos++] = 0xFF; buf[pos++] = 0xC0;
        buf[pos++] = 0x00; buf[pos++] = 0x0B; /* length=11 */
        buf[pos++] = 0x08; /* 8-bit precision */
        buf[pos++] = (frame->height >> 8) & 0xFF; buf[pos++] = frame->height & 0xFF;
        buf[pos++] = (frame->width >> 8) & 0xFF; buf[pos++] = frame->width & 0xFF;
        buf[pos++] = 0x01; /* 1 component (greyscale) */
        buf[pos++] = 0x01; buf[pos++] = 0x11; buf[pos++] = 0x00;
        /* DHT (minimal Huffman table for DC) */
        buf[pos++] = 0xFF; buf[pos++] = 0xC4;
        buf[pos++] = 0x00; buf[pos++] = 0x1F; /* length=31 */
        buf[pos++] = 0x00; /* DC table 0 */
        /* counts: 0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0 */
        uint8_t dc_counts[] = {0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0};
        memcpy(buf+pos, dc_counts, 16); pos += 16;
        uint8_t dc_vals[] = {0,1,2,3,4,5,6,7,8,9,10,11};
        memcpy(buf+pos, dc_vals, 12); pos += 12;
        /* SOS */
        buf[pos++] = 0xFF; buf[pos++] = 0xDA;
        buf[pos++] = 0x00; buf[pos++] = 0x08; /* length=8 */
        buf[pos++] = 0x01; /* 1 component */
        buf[pos++] = 0x01; buf[pos++] = 0x00; /* comp 1, DC=0 AC=0 */
        buf[pos++] = 0x00; buf[pos++] = 0x3F; buf[pos++] = 0x00; /* Ss=0,Se=63,Ah/Al=0 */
        /* Minimal scan data: single grey MCU */
        buf[pos++] = 0x7F; buf[pos++] = 0xC0; /* DC=0 encoded + padding */
        /* EOI */
        buf[pos++] = 0xFF; buf[pos++] = 0xD9;

        stream->virt_addr = (uint32_t)(uintptr_t)buf;
        stream->phys_addr = 0;
        stream->length = pos;
        stream->timestamp = frame->timestamp;
        stream->frame_type = HW_FRAME_TYPE_I;
        stream->slice_type = 0;

        static int jpeg_count = 0;
        if (jpeg_count++ < 3) LOG_HW("Software JPEG: %ux%u, %d bytes", frame->width, frame->height, pos);
        return 0;
    }

    if (codec_type != HW_CODEC_H264) {
        LOG_HW("Software encoding: unsupported codec_type=%u", codec_type);
        return -1;
    }

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

    /* AUD (Access Unit Delimiter) — helps decoders find frame boundaries */
    uint8_t aud_rbsp[8];
    int aud_rbsp_len = build_aud_rbsp(aud_rbsp, is_idr);
    total_size += write_nal_epb(nal_buffer + total_size, 0x09, aud_rbsp, aud_rbsp_len);


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

