/*
 * HEVC parameter sets and slice header for the T30 Radix encoder, see
 * t30_radix_hevc.h.  Syntax order of ITU-T H.265 7.3 with the vendor's
 * fixed values; tests/t30/radix/hevc_diff checks every writer bit for bit
 * against the vendor libimp 1.0.5 functions named in the header.
 */
#include "t30/radix/t30_radix_hevc.h"

#include <string.h>

void t30_hevc_bits_init(T30HevcBits *b, uint8_t *buf, size_t cap)
{
    memset(b, 0, sizeof(*b));
    b->buf = buf;
    b->cap = cap;
}

void t30_hevc_put(T30HevcBits *b, uint32_t val, unsigned n)
{
    while (n--) {
        b->cur = (uint8_t)((b->cur << 1) | ((val >> n) & 1));
        if (++b->bits == 8) {
            if (b->len < b->cap)
                b->buf[b->len] = b->cur;
            else
                b->overflow = 1;
            b->len++;
            b->bits = 0;
            b->cur = 0;
        }
    }
}

void t30_hevc_ue(T30HevcBits *b, uint32_t val)
{
    uint32_t v = val + 1;
    unsigned len = 0;

    while ((v >> len) > 1)
        len++;
    t30_hevc_put(b, 0, len);
    t30_hevc_put(b, v, len + 1);
}

void t30_hevc_se(T30HevcBits *b, int32_t val)
{
    t30_hevc_ue(b, val > 0 ? (uint32_t)(2 * val - 1) : (uint32_t)(-2 * val));
}

void t30_hevc_trailing(T30HevcBits *b)
{
    t30_hevc_put(b, 1, 1);
    while (b->bits)
        t30_hevc_put(b, 0, 1);
}

/* profile_tier_level(1, max_sub_layers - 1) */
static void put_ptl(T30HevcBits *b, const T30HevcPtl *p, uint32_t max_sub_layers)
{
    t30_hevc_put(b, 0, 2);                      /* general_profile_space */
    t30_hevc_put(b, p->tier_flag, 1);
    t30_hevc_put(b, p->profile_idc, 5);
    for (int i = 0; i < 32; i++)
        t30_hevc_put(b, p->compat[i], 1);
    t30_hevc_put(b, p->progressive_source, 1);
    t30_hevc_put(b, p->interlaced_source, 1);
    t30_hevc_put(b, p->non_packed_constraint, 1);
    t30_hevc_put(b, p->frame_only_constraint, 1);
    if (p->profile_idc == 4 || p->profile_idc == 5) {
        uint32_t bd = p->bit_depth_constraint, cf = p->chroma_format_constraint;

        t30_hevc_put(b, bd < 13, 1);            /* max_12bit */
        t30_hevc_put(b, bd < 11, 1);            /* max_10bit */
        t30_hevc_put(b, bd < 9 && cf != 2, 1);  /* max_8bit (vendor rule) */
        t30_hevc_put(b, cf < 3, 1);             /* max_422chroma */
        t30_hevc_put(b, cf < 2, 1);             /* max_420chroma */
        t30_hevc_put(b, cf == 0, 1);            /* max_monochrome */
        t30_hevc_put(b, p->intra_constraint, 1);
        t30_hevc_put(b, p->one_picture_only, 1);
        t30_hevc_put(b, p->lower_bit_rate, 1);
        t30_hevc_put(b, 0, 16);
        t30_hevc_put(b, 0, 16);
        t30_hevc_put(b, 0, 3);
    } else {
        t30_hevc_put(b, 0, 16);
        t30_hevc_put(b, 0, 16);
        t30_hevc_put(b, 0, 12);
    }
    t30_hevc_put(b, p->level_idc, 8);
    if (max_sub_layers > 1) {
        t30_hevc_put(b, 0, 1);                  /* sub_layer_profile_present */
        t30_hevc_put(b, 0, 1);                  /* sub_layer_level_present */
        for (uint32_t i = max_sub_layers - 1; i < 8; i++)
            t30_hevc_put(b, 0, 2);
    }
}

void t30_hevc_write_vps(T30HevcBits *b, const T30HevcVps *v)
{
    t30_hevc_put(b, 0, 4);                      /* vps_video_parameter_set_id */
    t30_hevc_put(b, 3, 2);                      /* base layer internal + available */
    t30_hevc_put(b, 0, 6);                      /* vps_max_layers_minus1 */
    t30_hevc_put(b, v->max_sub_layers - 1, 3);
    t30_hevc_put(b, v->max_sub_layers == 1, 1); /* temporal_id_nesting */
    t30_hevc_put(b, 0xffff, 16);
    put_ptl(b, &v->ptl, v->max_sub_layers);
    t30_hevc_put(b, 1, 1);                      /* sub_layer_ordering_info_present */
    for (uint32_t i = 0; i < v->max_sub_layers; i++) {
        t30_hevc_ue(b, v->max_dec_pic_buffering - 1);
        t30_hevc_ue(b, v->num_reorder_pics);
        t30_hevc_ue(b, v->max_latency_increase + 1);
    }
    t30_hevc_put(b, 0, 6);                      /* vps_max_layer_id */
    t30_hevc_ue(b, 0);                          /* vps_num_layer_sets_minus1 */
    t30_hevc_put(b, 0, 1);                      /* vps_timing_info_present */
    t30_hevc_put(b, 0, 1);                      /* vps_extension */
}

static void put_hrd(T30HevcBits *b, const T30HevcHrd *h, uint32_t max_sub_layers)
{
    t30_hevc_put(b, 1, 1);                      /* nal_hrd_parameters_present */
    t30_hevc_put(b, 0, 1);                      /* vcl_hrd_parameters_present */
    t30_hevc_put(b, 0, 1);                      /* sub_pic_hrd_params_present */
    t30_hevc_put(b, h->bit_rate_scale, 4);
    t30_hevc_put(b, h->cpb_size_scale, 4);
    t30_hevc_put(b, h->initial_cpb_removal_delay_length - 1, 5);
    t30_hevc_put(b, h->cpb_removal_delay_length - 1, 5);
    t30_hevc_put(b, h->dpb_output_delay_length - 1, 5);
    for (uint32_t i = 0; i < max_sub_layers; i++) {
        t30_hevc_put(b, 1, 1);                  /* fixed_pic_rate_general */
        t30_hevc_ue(b, 0);                      /* elemental_duration_in_tc_minus1 */
        t30_hevc_ue(b, 0);                      /* cpb_cnt_minus1 */
        t30_hevc_ue(b, h->bit_rate_value - 1);
        t30_hevc_ue(b, h->cpb_size_value - 1);
        t30_hevc_put(b, h->cbr_flag, 1);
    }
}

static void put_vui(T30HevcBits *b, const T30HevcSps *s)
{
    const T30HevcVui *v = &s->vui;

    t30_hevc_put(b, v->aspect_ratio_info_present, 1);
    if (v->aspect_ratio_info_present) {
        t30_hevc_put(b, v->aspect_ratio_idc, 8);
        if (v->aspect_ratio_idc == 255) {
            t30_hevc_put(b, v->sar_width, 16);
            t30_hevc_put(b, v->sar_height, 16);
        }
    }
    t30_hevc_put(b, v->overscan_info_present, 1);
    if (v->overscan_info_present)
        t30_hevc_put(b, v->overscan_appropriate, 1);
    t30_hevc_put(b, v->video_signal_type_present, 1);
    if (v->video_signal_type_present) {
        t30_hevc_put(b, v->video_format, 3);
        t30_hevc_put(b, v->video_full_range, 1);
        t30_hevc_put(b, v->colour_description_present, 1);
        if (v->colour_description_present) {
            t30_hevc_put(b, v->colour_primaries, 8);
            t30_hevc_put(b, v->transfer_characteristics, 8);
            t30_hevc_put(b, v->matrix_coeffs, 8);
        }
    }
    t30_hevc_put(b, v->chroma_loc_info_present, 1);
    if (v->chroma_loc_info_present) {
        t30_hevc_ue(b, v->chroma_loc_top);
        t30_hevc_ue(b, v->chroma_loc_bottom);
    }
    t30_hevc_put(b, 0, 1);                      /* neutral_chroma_indication */
    t30_hevc_put(b, v->field_seq, 1);
    t30_hevc_put(b, v->frame_field_info_present, 1);
    t30_hevc_put(b, v->default_display_window, 1);
    if (v->default_display_window) {
        t30_hevc_ue(b, v->def_disp_left);
        t30_hevc_ue(b, v->def_disp_right);
        t30_hevc_ue(b, v->def_disp_top);
        t30_hevc_ue(b, v->def_disp_bottom);
    }
    t30_hevc_put(b, s->timing_info, 1);
    if (s->timing_info) {
        t30_hevc_put(b, v->num_units_in_tick, 32);
        t30_hevc_put(b, v->time_scale, 32);
        t30_hevc_put(b, 0, 1);                  /* poc_proportional_to_timing */
    }
    if (s->hrd_flag_written) {
        t30_hevc_put(b, v->hrd_present, 1);
        if (v->hrd_present)
            put_hrd(b, &v->hrd, s->max_sub_layers);
    } else {
        t30_hevc_put(b, 0, 1);
    }
    t30_hevc_put(b, 0, 1);                      /* bitstream_restriction */
}

void t30_hevc_write_sps(T30HevcBits *b, const T30HevcSps *s)
{
    t30_hevc_put(b, 0, 4);                      /* sps_video_parameter_set_id */
    t30_hevc_put(b, s->max_sub_layers - 1, 3);
    t30_hevc_put(b, s->max_sub_layers == 1, 1); /* temporal_id_nesting */
    put_ptl(b, &s->ptl, s->max_sub_layers);
    t30_hevc_ue(b, 0);                          /* sps_seq_parameter_set_id */
    t30_hevc_ue(b, s->chroma_format_idc);
    if (s->chroma_format_idc == 3)
        t30_hevc_put(b, 0, 1);                  /* separate_colour_plane */
    t30_hevc_ue(b, s->width);
    t30_hevc_ue(b, s->height);
    t30_hevc_put(b, s->conformance_window, 1);
    if (s->conformance_window) {
        /* the vendor's shifts by its colour space value */
        uint32_t c = s->chroma_format_idc;
        int hs, vs;

        if (c == 1 || c == 4 || c == 5) {
            hs = 1;
            vs = 1;
        } else {
            hs = (c & ~4u) == 2;
            vs = c == 6;
        }
        t30_hevc_ue(b, s->conf_left >> hs);
        t30_hevc_ue(b, s->conf_right >> hs);
        t30_hevc_ue(b, s->conf_top >> vs);
        t30_hevc_ue(b, s->conf_bottom >> vs);
    }
    t30_hevc_ue(b, 0);                          /* bit_depth_luma_minus8 */
    t30_hevc_ue(b, 0);                          /* bit_depth_chroma_minus8 */
    t30_hevc_ue(b, s->log2_max_poc_lsb - 4);
    t30_hevc_put(b, 1, 1);                      /* sub_layer_ordering_info_present */
    for (uint32_t i = 0; i < s->max_sub_layers; i++) {
        t30_hevc_ue(b, s->max_dec_pic_buffering - 1);
        t30_hevc_ue(b, s->num_reorder_pics);
        t30_hevc_ue(b, s->max_latency_increase + 1);
    }
    t30_hevc_ue(b, s->log2_min_cb - 3);
    t30_hevc_ue(b, s->log2_diff_max_min_cb);
    t30_hevc_ue(b, s->log2_min_tb - 2);
    t30_hevc_ue(b, s->log2_max_tb - s->log2_min_tb);
    t30_hevc_ue(b, s->max_tu_depth_inter - 1);
    t30_hevc_ue(b, s->max_tu_depth_intra - 1);
    t30_hevc_put(b, s->scaling_list_enabled, 1);
    if (s->scaling_list_enabled)
        t30_hevc_put(b, 0, 1);                  /* sps_scaling_list_data_present */
    t30_hevc_put(b, s->amp, 1);
    t30_hevc_put(b, s->sao, 1);
    t30_hevc_put(b, 0, 1);                      /* pcm_enabled */
    t30_hevc_ue(b, 0);                          /* num_short_term_ref_pic_sets */
    t30_hevc_put(b, s->long_term_refs, 1);
    if (s->long_term_refs)
        t30_hevc_ue(b, 0);                      /* num_long_term_ref_pics_sps */
    t30_hevc_put(b, s->temporal_mvp, 1);
    t30_hevc_put(b, s->strong_intra_smoothing, 1);
    t30_hevc_put(b, 1, 1);                      /* vui_parameters_present */
    put_vui(b, s);
    t30_hevc_put(b, 0, 1);                      /* sps_extension_present */
}

void t30_hevc_write_pps(T30HevcBits *b, const T30HevcPps *p)
{
    t30_hevc_ue(b, 0);                          /* pps_pic_parameter_set_id */
    t30_hevc_ue(b, 0);                          /* pps_seq_parameter_set_id */
    t30_hevc_put(b, 0, 1);                      /* dependent_slice_segments_enabled */
    t30_hevc_put(b, 0, 1);                      /* output_flag_present */
    t30_hevc_put(b, 0, 3);                      /* num_extra_slice_header_bits */
    t30_hevc_put(b, p->sign_data_hiding, 1);
    t30_hevc_put(b, 0, 1);                      /* cabac_init_present */
    t30_hevc_ue(b, p->num_ref_idx_l0_default - 1);
    t30_hevc_ue(b, p->num_ref_idx_l1_default - 1);
    t30_hevc_se(b, p->init_qp_minus26);
    t30_hevc_put(b, p->constrained_intra_pred, 1);
    t30_hevc_put(b, p->transform_skip, 1);
    t30_hevc_put(b, p->cu_qp_delta_enabled, 1);
    if (p->cu_qp_delta_enabled)
        t30_hevc_ue(b, p->diff_cu_qp_delta_depth);
    t30_hevc_se(b, p->cb_qp_offset);
    t30_hevc_se(b, p->cr_qp_offset);
    t30_hevc_put(b, p->slice_chroma_qp_offsets_present, 1);
    t30_hevc_put(b, p->weighted_pred, 1);
    t30_hevc_put(b, p->weighted_bipred, 1);
    t30_hevc_put(b, p->transquant_bypass, 1);
    t30_hevc_put(b, 0, 1);                      /* tiles_enabled */
    t30_hevc_put(b, p->entropy_coding_sync, 1);
    t30_hevc_put(b, p->loop_filter_across_slices, 1);
    t30_hevc_put(b, p->deblocking_control_present, 1);
    if (p->deblocking_control_present) {
        t30_hevc_put(b, 0, 1);                  /* deblocking_filter_override_enabled */
        t30_hevc_put(b, p->deblocking_disabled, 1);
        if (!p->deblocking_disabled) {
            t30_hevc_se(b, p->beta_offset_div2);
            t30_hevc_se(b, p->tc_offset_div2);
        }
    }
    t30_hevc_put(b, 0, 1);                      /* pps_scaling_list_data_present */
    t30_hevc_put(b, 0, 1);                      /* lists_modification_present */
    t30_hevc_ue(b, 0);                          /* log2_parallel_merge_level_minus2 */
    t30_hevc_put(b, 0, 1);                      /* slice_segment_header_extension */
    t30_hevc_put(b, 0, 1);                      /* pps_extension_present */
}

static void put_rps(T30HevcBits *b, const T30HevcSlice *s)
{
    int32_t prev = 0;

    t30_hevc_ue(b, s->num_negative);
    t30_hevc_ue(b, s->num_positive);
    for (uint32_t i = 0; i < s->num_negative; i++) {
        t30_hevc_ue(b, (uint32_t)(prev - s->delta_poc[i] - 1));
        prev = s->delta_poc[i];
        t30_hevc_put(b, s->used[i], 1);
    }
    prev = 0;
    for (uint32_t i = s->num_negative; i < s->num_negative + s->num_positive; i++) {
        t30_hevc_ue(b, (uint32_t)(s->delta_poc[i] - prev - 1));
        prev = s->delta_poc[i];
        t30_hevc_put(b, s->used[i], 1);
    }
}

int t30_hevc_write_slice_header(T30HevcBits *b, const T30HevcSlice *s,
                                const T30HevcSps *sps, const T30HevcPps *pps)
{
    uint32_t n = s->num_negative + s->num_positive;

    if (sps->long_term_refs || (pps->weighted_pred && s->slice_type == 1) ||
        (pps->weighted_bipred && s->slice_type == 0) ||
        n > T30_HEVC_MAX_REFS || s->max_num_merge_cand >= 6)
        return -1;
    if (s->nal_unit_type >= 16 && s->nal_unit_type <= 23 &&
        s->nal_unit_type != 19)
        for (uint32_t i = 0; i < n; i++)
            if (s->used[i])
                return -1;              /* the vendor aborts here */

    t30_hevc_put(b, s->segment_address == 0, 1); /* first_slice_segment_in_pic */
    if (s->nal_unit_type == 19 || s->nal_unit_type == 21)
        t30_hevc_put(b, 0, 1);                  /* no_output_of_prior_pics */
    t30_hevc_ue(b, 0);                          /* slice_pic_parameter_set_id */
    if (s->segment_address)
        t30_hevc_put(b, s->segment_address, s->address_bits);
    t30_hevc_ue(b, s->slice_type);
    if (s->nal_unit_type != 19) {
        uint32_t m = 1u << sps->log2_max_poc_lsb;

        t30_hevc_put(b, (uint32_t)(((s->poc - s->last_idr_poc) + (int32_t)m) %
                                   (int32_t)m), sps->log2_max_poc_lsb);
        if (s->rps_idx < 0) {
            t30_hevc_put(b, 0, 1);              /* short_term_ref_pic_set_sps_flag */
            put_rps(b, s);
        } else {
            t30_hevc_put(b, 1, 1);
            if (s->num_sps_rps > 1) {
                unsigned bits = 1;

                while ((1u << bits) < s->num_sps_rps)
                    bits++;
                t30_hevc_put(b, (uint32_t)s->rps_idx, bits);
            }
        }
        if (sps->temporal_mvp)
            t30_hevc_put(b, 1, 1);              /* slice_temporal_mvp_enabled */
    }
    if (sps->sao) {
        t30_hevc_put(b, 1, 1);                  /* slice_sao_luma */
        t30_hevc_put(b, 0, 1);                  /* slice_sao_chroma */
    }
    if (s->slice_type != 2) {
        t30_hevc_put(b, 0, 1);                  /* num_ref_idx_active_override */
        if (s->slice_type == 0)
            t30_hevc_put(b, 0, 1);              /* mvd_l1_zero */
        if (sps->temporal_mvp) {
            if (s->slice_type == 0)
                t30_hevc_put(b, s->collocated_from_l0, 1);
            if (s->collocated_from_l0 ? s->num_ref_idx_l0 > 1 : s->num_ref_idx_l1 > 1)
                t30_hevc_ue(b, s->collocated_ref_idx);
        }
        t30_hevc_ue(b, 5 - s->max_num_merge_cand);
    }
    t30_hevc_se(b, s->slice_qp - (26 + pps->init_qp_minus26));
    if (pps->slice_chroma_qp_offsets_present) {
        t30_hevc_se(b, s->cb_qp_offset);
        t30_hevc_se(b, s->cr_qp_offset);
    }
    if (sps->sao || !pps->deblocking_disabled)
        t30_hevc_put(b, s->loop_filter_across_slices, 1);
    return 0;
}

size_t t30_hevc_nal(uint8_t *out, size_t cap, uint32_t nal_unit_type,
                    const uint8_t *rbsp, size_t len, int long_start)
{
    size_t o = 0;

    if (!len || cap < 7 + len + len / 2)
        return 0;               /* worst case: an escape every second byte */
    if (long_start)
        out[o++] = 0;
    out[o++] = 0;
    out[o++] = 0;
    out[o++] = 1;
    out[o++] = (uint8_t)(nal_unit_type << 1);
    out[o++] = 1;                               /* nuh_temporal_id_plus1 */
    /* emulation prevention as the vendor (x265 NALList::serialize): before
     * appending byte i > 2, a written 00 00 0x (x <= 3) becomes 00 00 03 0x */
    for (size_t i = 0; i < len; i++) {
        if (i > 2 && !out[o - 2] && !out[o - 3] && out[o - 1] <= 3) {
            out[o] = out[o - 1];
            out[o - 1] = 3;
            o++;
        }
        out[o++] = rbsp[i];
    }
    if (!rbsp[len - 1])
        out[o++] = 3;           /* a final 0x00 is followed by 0x03 */
    return o;
}
