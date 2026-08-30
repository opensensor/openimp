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
static int generate_h264_idr_slice(uint8_t *buf, size_t buf_size, int width, int height,
                                   uint32_t frame_num) {
    int pos = 0;

    if (buf == NULL || buf_size < 64 || width <= 0 || height <= 0) {
        return -1;
    }

    memset(buf, 0, buf_size);

    /* Start code */
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;
    buf[pos++] = 0x01;

    /* NAL header: IDR slice, nal_ref_idc=3, nal_unit_type=5 → 0x65 */
    buf[pos++] = 0x65;

    int bit_pos = pos * 8;

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
static int generate_h264_p_slice(uint8_t *buf, size_t buf_size, int width, int height,
                                 uint32_t frame_num) {
    int pos = 0;

    if (buf == NULL || buf_size < 64 || width <= 0 || height <= 0) {
        return -1;
    }

    memset(buf, 0, buf_size);

    /* Start code */
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;
    buf[pos++] = 0x00;
    buf[pos++] = 0x01;

    /* NAL header: non-IDR slice, nal_ref_idc=0, nal_unit_type=1 → 0x01
     * nal_ref_idc=0 means non-reference (fixes "reference frames exceeds max" error) */
    buf[pos++] = 0x01;

    int bit_pos = pos * 8;

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
static int write_nal_epb(uint8_t *dst, size_t dst_size, uint8_t nal_header,
                         const uint8_t *rbsp, int rbsp_len) {
    int pos = 0;
    if (dst == NULL || rbsp == NULL || rbsp_len < 0 || dst_size < 5) return -1;
    /* Annex B start code */
    dst[pos++] = 0x00; dst[pos++] = 0x00; dst[pos++] = 0x00; dst[pos++] = 0x01;
    /* NAL header */
    dst[pos++] = nal_header;
    /* Emulation prevention: insert 0x03 after 00 00 before {00,01,02,03} */
    int zeros = 0;
    for (int i = 0; i < rbsp_len; i++) {
        uint8_t b = rbsp[i];
        if (zeros >= 2 && b <= 0x03) {
            if ((size_t)pos >= dst_size) return -1;
            dst[pos++] = 0x03;
            zeros = 0;
        }
        if ((size_t)pos >= dst_size) return -1;
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
static int repack_with_epb(uint8_t *dst, size_t dst_size, const uint8_t *src_nal,
                           int src_len) {
    if (src_len < 6) return 0; /* too small */
    uint8_t header = src_nal[4];
    const uint8_t *rbsp = src_nal + 5;
    int rbsp_len = src_len - 5;
    return write_nal_epb(dst, dst_size, header, rbsp, rbsp_len);
}

/**
 * Software fallback encoder with BN MCP-like AU sequencing and EPB insertion
 */
/* Baseline JPEG encoder adapted from the public-domain stb_image_write JPEG
 * path. It consumes the ISP's native NV12/NV21 buffer directly, so preview
 * frames need neither an RGB conversion buffer nor a second capture dequeue. */
typedef struct {
    uint16_t code;
    uint8_t size;
} JPEGHuffmanCode;

typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
    int failed;
} JPEGWriter;

static const uint8_t jpeg_zigzag[64] = {
    0, 1, 5, 6, 14, 15, 27, 28, 2, 4, 7, 13, 16, 26, 29, 42,
    3, 8, 12, 17, 25, 30, 41, 43, 9, 11, 18, 24, 31, 40, 44, 53,
    10, 19, 23, 32, 39, 45, 52, 54, 20, 22, 33, 38, 46, 51, 55, 60,
    21, 34, 37, 47, 50, 56, 59, 61, 35, 36, 48, 49, 57, 58, 62, 63
};

static const uint8_t jpeg_y_quant[64] = {
    16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55,
    14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62,
    18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92,
    49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99
};

static const uint8_t jpeg_uv_quant[64] = {
    17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99
};

static const uint8_t jpeg_y_dc_counts[16] = {
    0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0
};
static const uint8_t jpeg_y_dc_values[12] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
};
static const uint8_t jpeg_y_ac_counts[16] = {
    0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d
};
static const uint8_t jpeg_y_ac_values[162] = {
    0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,
    0x13,0x51,0x61,0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,
    0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,0x24,0x33,0x62,0x72,
    0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,
    0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,
    0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,
    0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,
    0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
    0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,
    0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,
    0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,
    0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,
    0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,
    0xf5,0xf6,0xf7,0xf8,0xf9,0xfa
};
static const uint8_t jpeg_uv_dc_counts[16] = {
    0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0
};
static const uint8_t jpeg_uv_dc_values[12] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
};
static const uint8_t jpeg_uv_ac_counts[16] = {
    0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77
};
static const uint8_t jpeg_uv_ac_values[162] = {
    0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,
    0x51,0x07,0x61,0x71,0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,
    0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,0x15,0x62,0x72,0xd1,
    0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
    0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,
    0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,
    0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,
    0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
    0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,
    0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,
    0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,
    0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
    0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,
    0xf5,0xf6,0xf7,0xf8,0xf9,0xfa
};

static int jpeg_writer_reserve(JPEGWriter *writer, size_t extra)
{
    size_t required;
    size_t capacity;
    uint8_t *data;

    if (writer->failed || extra > SIZE_MAX - writer->size) {
        writer->failed = 1;
        return -1;
    }
    required = writer->size + extra;
    if (required <= writer->capacity)
        return 0;
    capacity = writer->capacity ? writer->capacity : 4096u;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2u) {
            capacity = required;
            break;
        }
        capacity *= 2u;
    }
    data = realloc(writer->data, capacity);
    if (!data) {
        writer->failed = 1;
        return -1;
    }
    writer->data = data;
    writer->capacity = capacity;
    return 0;
}

static void jpeg_writer_write(JPEGWriter *writer, const void *data, size_t size)
{
    if (jpeg_writer_reserve(writer, size) != 0)
        return;
    memcpy(writer->data + writer->size, data, size);
    writer->size += size;
}

static void jpeg_writer_byte(JPEGWriter *writer, uint8_t value)
{
    jpeg_writer_write(writer, &value, 1u);
}

static void jpeg_writer_be16(JPEGWriter *writer, uint16_t value)
{
    uint8_t bytes[2] = {(uint8_t)(value >> 8), (uint8_t)value};
    jpeg_writer_write(writer, bytes, sizeof(bytes));
}

static void jpeg_build_huffman(const uint8_t counts[16],
                               const uint8_t *values, size_t value_count,
                               JPEGHuffmanCode table[256])
{
    uint16_t code = 0;
    size_t value = 0;
    unsigned int length;

    memset(table, 0, sizeof(JPEGHuffmanCode) * 256u);
    for (length = 1; length <= 16; length++) {
        unsigned int index;

        for (index = 0; index < counts[length - 1] && value < value_count;
             index++, value++) {
            table[values[value]].code = code++;
            table[values[value]].size = (uint8_t)length;
        }
        code = (uint16_t)(code << 1);
    }
}

static void jpeg_write_bits(JPEGWriter *writer, uint32_t *bit_buffer,
                            unsigned int *bit_count, JPEGHuffmanCode bits)
{
    *bit_count += bits.size;
    *bit_buffer |= (uint32_t)bits.code << (24u - *bit_count);
    while (*bit_count >= 8u) {
        uint8_t value = (uint8_t)(*bit_buffer >> 16);

        jpeg_writer_byte(writer, value);
        if (value == 0xffu)
            jpeg_writer_byte(writer, 0);
        *bit_buffer <<= 8;
        *bit_count -= 8u;
    }
}

static JPEGHuffmanCode jpeg_value_bits(int value)
{
    JPEGHuffmanCode bits;
    unsigned int magnitude = (unsigned int)(value < 0 ? -value : value);

    bits.size = 0;
    while (magnitude) {
        bits.size++;
        magnitude >>= 1;
    }
    bits.code = (uint16_t)((value < 0 ? value - 1 : value) &
                           ((1u << bits.size) - 1u));
    return bits;
}

static void jpeg_dct(float *d0p, float *d1p, float *d2p, float *d3p,
                     float *d4p, float *d5p, float *d6p, float *d7p)
{
    float d0 = *d0p, d1 = *d1p, d2 = *d2p, d3 = *d3p;
    float d4 = *d4p, d5 = *d5p, d6 = *d6p, d7 = *d7p;
    float tmp0 = d0 + d7, tmp7 = d0 - d7;
    float tmp1 = d1 + d6, tmp6 = d1 - d6;
    float tmp2 = d2 + d5, tmp5 = d2 - d5;
    float tmp3 = d3 + d4, tmp4 = d3 - d4;
    float tmp10 = tmp0 + tmp3, tmp13 = tmp0 - tmp3;
    float tmp11 = tmp1 + tmp2, tmp12 = tmp1 - tmp2;
    float z1, z2, z3, z4, z5, z11, z13;

    d0 = tmp10 + tmp11;
    d4 = tmp10 - tmp11;
    z1 = (tmp12 + tmp13) * 0.707106781f;
    d2 = tmp13 + z1;
    d6 = tmp13 - z1;
    tmp10 = tmp4 + tmp5;
    tmp11 = tmp5 + tmp6;
    tmp12 = tmp6 + tmp7;
    z5 = (tmp10 - tmp12) * 0.382683433f;
    z2 = tmp10 * 0.541196100f + z5;
    z4 = tmp12 * 1.306562965f + z5;
    z3 = tmp11 * 0.707106781f;
    z11 = tmp7 + z3;
    z13 = tmp7 - z3;
    *d5p = z13 + z2;
    *d3p = z13 - z2;
    *d1p = z11 + z4;
    *d7p = z11 - z4;
    *d0p = d0;
    *d2p = d2;
    *d4p = d4;
    *d6p = d6;
}

static int jpeg_process_block(JPEGWriter *writer, uint32_t *bit_buffer,
                              unsigned int *bit_count, float block[64],
                              const float scale[64], int previous_dc,
                              const JPEGHuffmanCode dc_table[256],
                              const JPEGHuffmanCode ac_table[256])
{
    int quantized[64];
    int index;
    int last;
    int difference;

    for (index = 0; index < 64; index += 8)
        jpeg_dct(&block[index], &block[index + 1], &block[index + 2],
                 &block[index + 3], &block[index + 4], &block[index + 5],
                 &block[index + 6], &block[index + 7]);
    for (index = 0; index < 8; index++)
        jpeg_dct(&block[index], &block[index + 8], &block[index + 16],
                 &block[index + 24], &block[index + 32], &block[index + 40],
                 &block[index + 48], &block[index + 56]);
    for (index = 0; index < 64; index++) {
        float value = block[index] * scale[index];

        quantized[jpeg_zigzag[index]] =
            (int)(value < 0.0f ? value - 0.5f : value + 0.5f);
    }

    difference = quantized[0] - previous_dc;
    if (!difference) {
        jpeg_write_bits(writer, bit_buffer, bit_count, dc_table[0]);
    } else {
        JPEGHuffmanCode bits = jpeg_value_bits(difference);

        jpeg_write_bits(writer, bit_buffer, bit_count, dc_table[bits.size]);
        jpeg_write_bits(writer, bit_buffer, bit_count, bits);
    }

    for (last = 63; last > 0 && quantized[last] == 0; last--)
        ;
    if (!last) {
        jpeg_write_bits(writer, bit_buffer, bit_count, ac_table[0]);
        return quantized[0];
    }
    for (index = 1; index <= last; index++) {
        int start = index;
        int zeroes;
        JPEGHuffmanCode bits;

        while (index <= last && quantized[index] == 0)
            index++;
        zeroes = index - start;
        while (zeroes >= 16) {
            jpeg_write_bits(writer, bit_buffer, bit_count, ac_table[0xf0]);
            zeroes -= 16;
        }
        bits = jpeg_value_bits(quantized[index]);
        jpeg_write_bits(writer, bit_buffer, bit_count,
                        ac_table[(zeroes << 4) | bits.size]);
        jpeg_write_bits(writer, bit_buffer, bit_count, bits);
    }
    if (last != 63)
        jpeg_write_bits(writer, bit_buffer, bit_count, ac_table[0]);
    return quantized[0];
}

static void jpeg_write_dht(JPEGWriter *writer, uint8_t table_id,
                           const uint8_t counts[16], const uint8_t *values,
                           size_t value_count)
{
    jpeg_writer_byte(writer, 0xff);
    jpeg_writer_byte(writer, 0xc4);
    jpeg_writer_be16(writer, (uint16_t)(19u + value_count));
    jpeg_writer_byte(writer, table_id);
    jpeg_writer_write(writer, counts, 16u);
    jpeg_writer_write(writer, values, value_count);
}

int HW_Encoder_Encode_NV12_JPEG(HWFrameBuffer *frame,
                                HWStreamBuffer *stream,
                                uint32_t quality)
{
    static const float aasf[8] = {
        2.828427125f, 3.923141121f, 3.695518130f, 3.325878449f,
        2.828427125f, 2.222280933f, 1.530733729f, 0.780361288f
    };
    JPEGHuffmanCode y_dc_table[256], y_ac_table[256];
    JPEGHuffmanCode uv_dc_table[256], uv_ac_table[256];
    uint8_t y_table[64], uv_table[64];
    float y_scale[64], uv_scale[64];
    JPEGWriter writer;
    const uint8_t *luma;
    const uint8_t *chroma;
    uint32_t width, height, stride, padded_height, chroma_rows;
    uint32_t x, y;
    uint32_t bit_buffer = 0;
    unsigned int bit_count = 0;
    int dc_y = 0, dc_u = 0, dc_v = 0;
    int scale_quality;
    int nv21;
    unsigned int index;

    if (!frame || !stream || !frame->virt_addr || !frame->width ||
        !frame->height || frame->width > 65535u || frame->height > 65535u)
        return -1;
    width = frame->width;
    height = frame->height;
    stride = width;
    padded_height = (height + 15u) & ~15u;
    chroma_rows = (height + 1u) / 2u;
    if ((uint64_t)stride * padded_height +
            (uint64_t)stride * chroma_rows > frame->size)
        return -1;
    luma = (const uint8_t *)(uintptr_t)frame->virt_addr;
    chroma = luma + (size_t)stride * padded_height;
    nv21 = frame->pixfmt == 0x0bu || frame->pixfmt == 0x3132564eu;

    memset(&writer, 0, sizeof(writer));
    if (jpeg_writer_reserve(&writer,
                            (size_t)width * height / 4u + 4096u) != 0)
        return -1;
    quality = quality < 1u ? 75u : quality > 100u ? 100u : quality;
    scale_quality = quality < 50u ? 5000 / (int)quality
                                  : 200 - (int)quality * 2;
    for (index = 0; index < 64; index++) {
        int yq = (jpeg_y_quant[index] * scale_quality + 50) / 100;
        int uvq = (jpeg_uv_quant[index] * scale_quality + 50) / 100;
        unsigned int row = index / 8u;
        unsigned int column = index % 8u;

        yq = yq < 1 ? 1 : yq > 255 ? 255 : yq;
        uvq = uvq < 1 ? 1 : uvq > 255 ? 255 : uvq;
        y_table[jpeg_zigzag[index]] = (uint8_t)yq;
        uv_table[jpeg_zigzag[index]] = (uint8_t)uvq;
        y_scale[index] = 1.0f /
            (y_table[jpeg_zigzag[index]] * aasf[row] * aasf[column]);
        uv_scale[index] = 1.0f /
            (uv_table[jpeg_zigzag[index]] * aasf[row] * aasf[column]);
    }
    jpeg_build_huffman(jpeg_y_dc_counts, jpeg_y_dc_values,
                       sizeof(jpeg_y_dc_values), y_dc_table);
    jpeg_build_huffman(jpeg_y_ac_counts, jpeg_y_ac_values,
                       sizeof(jpeg_y_ac_values), y_ac_table);
    jpeg_build_huffman(jpeg_uv_dc_counts, jpeg_uv_dc_values,
                       sizeof(jpeg_uv_dc_values), uv_dc_table);
    jpeg_build_huffman(jpeg_uv_ac_counts, jpeg_uv_ac_values,
                       sizeof(jpeg_uv_ac_values), uv_ac_table);

    {
        static const uint8_t app0[] = {
            0xff,0xd8,0xff,0xe0,0x00,0x10,'J','F','I','F',0x00,0x01,0x01,
            0x01,0x00,0x48,0x00,0x48,0x00,0x00
        };
        uint8_t sof0[] = {
            0xff,0xc0,0x00,0x11,0x08,
            (uint8_t)(height >> 8),(uint8_t)height,
            (uint8_t)(width >> 8),(uint8_t)width,
            0x03,0x01,0x22,0x00,0x02,0x11,0x01,0x03,0x11,0x01
        };
        static const uint8_t sos[] = {
            0xff,0xda,0x00,0x0c,0x03,0x01,0x00,0x02,0x11,0x03,0x11,
            0x00,0x3f,0x00
        };

        jpeg_writer_write(&writer, app0, sizeof(app0));
        jpeg_writer_write(&writer, "\xff\xdb\x00\x43\x00", 5u);
        jpeg_writer_write(&writer, y_table, sizeof(y_table));
        jpeg_writer_write(&writer, "\xff\xdb\x00\x43\x01", 5u);
        jpeg_writer_write(&writer, uv_table, sizeof(uv_table));
        jpeg_writer_write(&writer, sof0, sizeof(sof0));
        jpeg_write_dht(&writer, 0x00, jpeg_y_dc_counts, jpeg_y_dc_values,
                       sizeof(jpeg_y_dc_values));
        jpeg_write_dht(&writer, 0x10, jpeg_y_ac_counts, jpeg_y_ac_values,
                       sizeof(jpeg_y_ac_values));
        jpeg_write_dht(&writer, 0x01, jpeg_uv_dc_counts, jpeg_uv_dc_values,
                       sizeof(jpeg_uv_dc_values));
        jpeg_write_dht(&writer, 0x11, jpeg_uv_ac_counts, jpeg_uv_ac_values,
                       sizeof(jpeg_uv_ac_values));
        jpeg_writer_write(&writer, sos, sizeof(sos));
    }

    for (y = 0; y < height; y += 16u) {
        for (x = 0; x < width; x += 16u) {
            unsigned int block_y, block_x;

            for (block_y = 0; block_y < 2; block_y++) {
                for (block_x = 0; block_x < 2; block_x++) {
                    float block[64];
                    unsigned int row, column;

                    for (row = 0; row < 8; row++) {
                        uint32_t source_y = y + block_y * 8u + row;

                        if (source_y >= height)
                            source_y = height - 1u;
                        for (column = 0; column < 8; column++) {
                            uint32_t source_x = x + block_x * 8u + column;

                            if (source_x >= width)
                                source_x = width - 1u;
                            block[row * 8u + column] =
                                (float)luma[(size_t)source_y * stride + source_x] - 128.0f;
                        }
                    }
                    dc_y = jpeg_process_block(&writer, &bit_buffer, &bit_count,
                                              block, y_scale, dc_y,
                                              y_dc_table, y_ac_table);
                }
            }
            {
                float u_block[64], v_block[64];
                unsigned int row, column;

                for (row = 0; row < 8; row++) {
                    uint32_t source_y = y / 2u + row;

                    if (source_y >= chroma_rows)
                        source_y = chroma_rows - 1u;
                    for (column = 0; column < 8; column++) {
                        uint32_t source_x = x / 2u + column;
                        size_t offset;

                        if (source_x >= (width + 1u) / 2u)
                            source_x = (width - 1u) / 2u;
                        offset = (size_t)source_y * stride + source_x * 2u;
                        u_block[row * 8u + column] =
                            (float)chroma[offset + (nv21 ? 1u : 0u)] - 128.0f;
                        v_block[row * 8u + column] =
                            (float)chroma[offset + (nv21 ? 0u : 1u)] - 128.0f;
                    }
                }
                dc_u = jpeg_process_block(&writer, &bit_buffer, &bit_count,
                                          u_block, uv_scale, dc_u,
                                          uv_dc_table, uv_ac_table);
                dc_v = jpeg_process_block(&writer, &bit_buffer, &bit_count,
                                          v_block, uv_scale, dc_v,
                                          uv_dc_table, uv_ac_table);
            }
        }
    }
    {
        JPEGHuffmanCode fill = {0x7f, 7};

        jpeg_write_bits(&writer, &bit_buffer, &bit_count, fill);
    }
    jpeg_writer_byte(&writer, 0xff);
    jpeg_writer_byte(&writer, 0xd9);
    if (writer.failed || writer.size > UINT32_MAX) {
        free(writer.data);
        return -1;
    }

    stream->virt_addr = (uint32_t)(uintptr_t)writer.data;
    stream->phys_addr = 0;
    stream->length = (uint32_t)writer.size;
    stream->timestamp = frame->timestamp;
    stream->frame_type = HW_FRAME_TYPE_I;
    stream->slice_type = 0;
    return 0;
}

int HW_Encoder_Encode_Software(HWFrameBuffer *frame, HWStreamBuffer *stream, uint32_t codec_type) {
    if (frame == NULL || stream == NULL) {
        return -1;
    }

    static uint32_t frame_counter = 0;

    if (codec_type == HW_CODEC_JPEG) {
        return HW_Encoder_Encode_NV12_JPEG(frame, stream, 75u);
    }

    if (codec_type != HW_CODEC_H264) {
        LOG_HW("Software encoding: unsupported codec_type=%u", codec_type);
        return -1;
    }

    /* The synthetic IDR writes six bits per macroblock.  The old fixed 4 KiB
     * scratch buffer overflowed at 1080p and the fixed 8 KiB AU buffer was too
     * small at 1440p after emulation-prevention insertion. */
    if (frame->width == 0 || frame->height == 0 ||
        frame->width > 16384 || frame->height > 16384) {
        LOG_HW("Software encoding: invalid dimensions %ux%u", frame->width, frame->height);
        return -1;
    }

    size_t mb_cols = (frame->width + 15u) / 16u;
    size_t mb_rows = (frame->height + 15u) / 16u;
    size_t num_mbs = mb_cols * mb_rows;
    /* SPS/PPS generators clear up to 100 bytes past their five-byte prefix. */
    size_t tmp_size = 128u + ((num_mbs * 6u + 7u) / 8u);
    size_t nal_size = tmp_size + (tmp_size / 2u) + 1024u;
    uint8_t *tmp = (uint8_t*)calloc(1, tmp_size);
    uint8_t *nal_buffer = (uint8_t*)malloc(nal_size);
    if (tmp == NULL || nal_buffer == NULL) {
        LOG_HW("Software encoding: failed to allocate H.264 buffers");
        free(tmp);
        free(nal_buffer);
        return -1;
    }

    int total_size = 0;
    int written;

    /* Decide frame type */
    int is_idr = ((frame_counter % 30) == 0) || g_force_idr;
    if (g_force_idr) {
        LOG_HW("Software encoding: Forcing IDR frame (requested by IMP_Encoder_RequestIDR)");
        g_force_idr = 0;
    }

    /* AUD (Access Unit Delimiter) — helps decoders find frame boundaries */
    uint8_t aud_rbsp[8];
    int aud_rbsp_len = build_aud_rbsp(aud_rbsp, is_idr);
    written = write_nal_epb(nal_buffer, nal_size, 0x09, aud_rbsp, aud_rbsp_len);
    if (written < 0) goto h264_buffer_error;
    total_size += written;


    if (is_idr) {
        /* SPS */
        int sps_len_raw = generate_h264_sps(tmp, frame->width, frame->height);
        written = repack_with_epb(nal_buffer + total_size, nal_size - (size_t)total_size,
                                  tmp, sps_len_raw);
        if (written < 0) goto h264_buffer_error;
        total_size += written;
        /* PPS */
        int pps_len_raw = generate_h264_pps(tmp);
        written = repack_with_epb(nal_buffer + total_size, nal_size - (size_t)total_size,
                                  tmp, pps_len_raw);
        if (written < 0) goto h264_buffer_error;
        total_size += written;
        /* IDR slice */
        int idr_len_raw = generate_h264_idr_slice(tmp, tmp_size, frame->width, frame->height,
                                                  frame_counter);
        if (idr_len_raw < 0) goto h264_buffer_error;
        written = repack_with_epb(nal_buffer + total_size, nal_size - (size_t)total_size,
                                  tmp, idr_len_raw);
        if (written < 0) goto h264_buffer_error;
        total_size += written;

        stream->frame_type = HW_FRAME_TYPE_I;
        stream->slice_type = 0;

        LOG_HW("Software encoding: IDR frame %u, total=%d bytes", frame_counter, total_size);
    } else {
        /* P slice */
        int p_len_raw = generate_h264_p_slice(tmp, tmp_size, frame->width, frame->height,
                                              frame_counter);
        if (p_len_raw < 0) goto h264_buffer_error;
        written = repack_with_epb(nal_buffer + total_size, nal_size - (size_t)total_size,
                                  tmp, p_len_raw);
        if (written < 0) goto h264_buffer_error;
        total_size += written;

        stream->frame_type = HW_FRAME_TYPE_P;
        stream->slice_type = 1;

        LOG_HW("Software encoding: P frame %u, total=%d bytes", frame_counter, total_size);
    }

    /* Populate stream buffer */
    stream->virt_addr = (uint32_t)(uintptr_t)nal_buffer;
    stream->phys_addr = 0;
    stream->length = total_size;
    stream->timestamp = frame->timestamp;

    free(tmp);
    frame_counter++;
    return 0;

h264_buffer_error:
    LOG_HW("Software encoding: H.264 access unit exceeded its checked buffer");
    free(tmp);
    free(nal_buffer);
    return -1;
}

/**
 * Request IDR frame on next encode
 */
void HW_Encoder_RequestIDR(void) {
    g_force_idr = 1;
    LOG_HW("RequestIDR: next frame will be IDR");
}
