/*
 * HEVC parameter sets and slice header for the T30 Radix encoder.
 *
 * Radix writes only the slice data; VPS, SPS, PPS and the slice header come
 * from the library, as in the vendor libimp 1.0.5 (an x265 derivative,
 * Entropy::codeVPS 0x6270c, codeSPS 0x6325c, codeVUI 0x62e0c,
 * codeHrdParameters 0x62c54, codeProfileTier 0x623ec, codePPS 0x61ff0,
 * codeSliceHeader 0x63f54, codeShortTermRefPicSet 0x6387c).  The writers
 * below give the same RBSP bits as those functions for the same values,
 * including the vendor's choices that differ from a full H.265 writer:
 *  - VPS: vps_max_layer_id 0, one layer set, no timing, no extension data
 *    (the vendor writes 16 random bits there only when a debug file exists);
 *  - PTL: only the general profile/level; for two or more sub-layers the
 *    vendor writes one pair of sub-layer present flags (0, 0) and the
 *    reserved bits up to 8;
 *  - SPS: one SPS, 8-bit, no PCM, no short-term RPS in the SPS, VUI always
 *    present, no scaling list data;
 *  - VUI: with timing off the vendor still writes the HRD present flag (0)
 *    when hrd_flag_written is set (vendor default: timing on);
 *  - PPS: no tiles, no scaling list, no list modification, parallel merge
 *    level 2, cabac_init_present 0;
 *  - slice header: one slice per picture, no long-term pictures, no
 *    weighted prediction (rejected with -1), num_ref_idx_active_override 0,
 *    slice_sao_luma 1 / chroma 0 when SAO is on.
 * Output is the RBSP without the trailing bits; t30_radix_hevc_nal() adds
 * the NAL header, the emulation prevention bytes and the start code.
 */
#ifndef T30_RADIX_HEVC_H
#define T30_RADIX_HEVC_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *buf;
    size_t cap, len;            /* whole bytes written */
    uint32_t bits;              /* bits pending in 'cur' (0..7) */
    uint8_t cur;
    int overflow;
} T30HevcBits;

void t30_hevc_bits_init(T30HevcBits *b, uint8_t *buf, size_t cap);
void t30_hevc_put(T30HevcBits *b, uint32_t val, unsigned n);
void t30_hevc_ue(T30HevcBits *b, uint32_t val);
void t30_hevc_se(T30HevcBits *b, int32_t val);
/* rbsp_trailing_bits: 1 then zeros to the byte boundary */
void t30_hevc_trailing(T30HevcBits *b);

typedef struct {
    uint32_t profile_idc;       /* 1 Main */
    uint32_t level_idc;         /* 30 * level */
    uint32_t bit_depth_constraint, chroma_format_constraint; /* profiles 4, 5 */
    uint8_t tier_flag;
    uint8_t progressive_source, interlaced_source, non_packed_constraint,
            frame_only_constraint;
    uint8_t compat[32];
    uint8_t intra_constraint, one_picture_only, lower_bit_rate; /* profiles 4, 5 */
} T30HevcPtl;

typedef struct {
    T30HevcPtl ptl;
    uint32_t max_sub_layers;
    uint32_t max_dec_pic_buffering, num_reorder_pics, max_latency_increase;
} T30HevcVps;

typedef struct {
    uint32_t bit_rate_scale, cpb_size_scale;
    uint32_t initial_cpb_removal_delay_length, cpb_removal_delay_length,
             dpb_output_delay_length;
    uint32_t bit_rate_value, cpb_size_value;
    uint8_t cbr_flag;
} T30HevcHrd;

typedef struct {
    uint8_t aspect_ratio_info_present;
    uint32_t aspect_ratio_idc, sar_width, sar_height;
    uint8_t overscan_info_present, overscan_appropriate;
    uint8_t video_signal_type_present;
    uint32_t video_format;
    uint8_t video_full_range, colour_description_present;
    uint32_t colour_primaries, transfer_characteristics, matrix_coeffs;
    uint8_t chroma_loc_info_present;
    uint32_t chroma_loc_top, chroma_loc_bottom;
    uint8_t field_seq, frame_field_info_present;
    uint8_t default_display_window;
    uint32_t def_disp_left, def_disp_right, def_disp_top, def_disp_bottom;
    uint32_t num_units_in_tick, time_scale;   /* when timing_info is set */
    uint8_t hrd_present;
    T30HevcHrd hrd;
} T30HevcVui;

typedef struct {
    T30HevcPtl ptl;
    uint32_t max_sub_layers;
    uint32_t chroma_format_idc;               /* the vendor csp value, 1 = 4:2:0 */
    uint32_t width, height;                   /* pic_width/height_in_luma_samples */
    uint8_t conformance_window;
    uint32_t conf_left, conf_right, conf_top, conf_bottom; /* in luma samples */
    uint32_t log2_max_poc_lsb;
    uint32_t max_dec_pic_buffering, num_reorder_pics, max_latency_increase;
    uint32_t log2_min_cb, log2_diff_max_min_cb;
    uint32_t log2_max_tb, log2_min_tb;
    uint32_t max_tu_depth_inter, max_tu_depth_intra;
    uint8_t scaling_list_enabled;
    uint8_t amp, sao, long_term_refs, temporal_mvp, strong_intra_smoothing;
    uint8_t timing_info;                      /* vui_timing_info_present_flag */
    uint8_t hrd_flag_written;                 /* write vui_hrd_parameters_present */
    T30HevcVui vui;
} T30HevcSps;

typedef struct {
    uint8_t sign_data_hiding, constrained_intra_pred, transform_skip;
    uint8_t cu_qp_delta_enabled;
    uint32_t diff_cu_qp_delta_depth;
    int32_t cb_qp_offset, cr_qp_offset;
    uint8_t slice_chroma_qp_offsets_present;
    uint8_t weighted_pred, weighted_bipred, transquant_bypass;
    uint8_t entropy_coding_sync;
    uint8_t loop_filter_across_slices;
    uint8_t deblocking_control_present, deblocking_disabled;
    int32_t beta_offset_div2, tc_offset_div2;
    uint32_t num_ref_idx_l0_default, num_ref_idx_l1_default;
    int32_t init_qp_minus26;
} T30HevcPps;

#define T30_HEVC_MAX_REFS 16

typedef struct {
    uint32_t nal_unit_type;                   /* 19 IDR_W_RADL, 1 TRAIL_R, ... */
    uint32_t slice_type;                      /* 0 B, 1 P, 2 I */
    int32_t poc, last_idr_poc;
    int32_t rps_idx;                          /* < 0: RPS in the slice header */
    uint32_t num_sps_rps;
    uint32_t num_negative, num_positive;      /* the slice's RPS */
    int32_t delta_poc[T30_HEVC_MAX_REFS];     /* negatives first, then positives */
    uint8_t used[T30_HEVC_MAX_REFS];
    uint8_t collocated_from_l0;
    uint32_t collocated_ref_idx;
    uint32_t num_ref_idx_l0, num_ref_idx_l1;  /* active */
    uint32_t max_num_merge_cand;
    int32_t slice_qp;
    int32_t cb_qp_offset, cr_qp_offset;
    uint8_t loop_filter_across_slices;
    uint32_t segment_address, address_bits;
} T30HevcSlice;

void t30_hevc_write_vps(T30HevcBits *b, const T30HevcVps *vps);
void t30_hevc_write_sps(T30HevcBits *b, const T30HevcSps *sps);
void t30_hevc_write_pps(T30HevcBits *b, const T30HevcPps *pps);
/* -1 for what the writer does not support (weighted prediction, long-term
 * pictures, a random-access NAL type with used references) */
int t30_hevc_write_slice_header(T30HevcBits *b, const T30HevcSlice *s,
                                const T30HevcSps *sps, const T30HevcPps *pps);

/* NAL unit like the vendor's NALList::serialize (0xa0520): start code
 * (00 00 00 01 when long_start - the vendor uses it for the first NAL of an
 * access unit and for VPS, SPS, PPS - else 00 00 01), the two-byte header
 * (temporal id 0) and the RBSP with the vendor's emulation prevention.
 * Returns the bytes written, 0 for an empty RBSP or when 'cap' is below
 * 7 + len * 3 / 2. */
size_t t30_hevc_nal(uint8_t *out, size_t cap, uint32_t nal_unit_type,
                    const uint8_t *rbsp, size_t len, int long_start);

#endif
