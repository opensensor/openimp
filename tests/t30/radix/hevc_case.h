/* Random HEVC header values shared by hevc_diff (vendor comparison under
 * qemu-mipsel) and radix_hevc_test (host regression). */
#ifndef HEVC_CASE_H
#define HEVC_CASE_H

#include <stdint.h>
#include <string.h>

#include "t30/radix/t30_radix_hevc.h"

static uint32_t rng = 0x208u;
static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}
static uint32_t rr(uint32_t lo, uint32_t hi) { return lo + rnd() % (hi - lo + 1); }
static int32_t rs(int32_t lo, int32_t hi) { return lo + (int32_t)(rnd() % (uint32_t)(hi - lo + 1)); }


/* --- random values ------------------------------------------------------ */
static void rand_ptl(T30HevcPtl *p)
{
    memset(p, 0, sizeof(*p));
    p->profile_idc = (rnd() & 1) ? rr(4, 5) : rr(0, 31);
    p->level_idc = rr(0, 255);
    p->bit_depth_constraint = rr(0, 16);
    p->chroma_format_constraint = rr(0, 3);
    p->tier_flag = rnd() & 1;
    p->progressive_source = rnd() & 1;
    p->interlaced_source = rnd() & 1;
    p->non_packed_constraint = rnd() & 1;
    p->frame_only_constraint = rnd() & 1;
    for (int i = 0; i < 32; i++)
    p->compat[i] = rnd() & 1;
    p->intra_constraint = rnd() & 1;
    p->one_picture_only = rnd() & 1;
    p->lower_bit_rate = rnd() & 1;
}

static void rand_sps(T30HevcSps *s)
{
    T30HevcVui *v = &s->vui;

    memset(s, 0, sizeof(*s));
    rand_ptl(&s->ptl);
    s->max_sub_layers = (rnd() & 3) ? 1 : rr(1, 8);
    s->chroma_format_idc = rr(0, 6);
    s->width = rr(16, 4096);
    s->height = rr(16, 4096);
    s->conformance_window = rnd() & 1;
    s->conf_left = rr(0, 64);
    s->conf_right = rr(0, 64);
    s->conf_top = rr(0, 64);
    s->conf_bottom = rr(0, 64);
    s->log2_max_poc_lsb = rr(4, 16);
    s->max_dec_pic_buffering = rr(1, 16);
    s->num_reorder_pics = rr(0, 15);
    s->max_latency_increase = rr(0, 1000);
    s->log2_min_cb = rr(3, 6);
    s->log2_diff_max_min_cb = rr(0, 3);
    s->log2_min_tb = rr(2, 5);
    s->log2_max_tb = rr(s->log2_min_tb, 5);
    s->max_tu_depth_inter = rr(1, 4);
    s->max_tu_depth_intra = rr(1, 4);
    s->scaling_list_enabled = rnd() & 1;
    s->amp = rnd() & 1;
    s->sao = rnd() & 1;
    s->long_term_refs = rnd() & 1;
    s->temporal_mvp = rnd() & 1;
    s->strong_intra_smoothing = rnd() & 1;
    s->timing_info = rnd() & 1;
    s->hrd_flag_written = rnd() & 1;
    v->aspect_ratio_info_present = rnd() & 1;
    v->aspect_ratio_idc = (rnd() & 1) ? 255 : rr(0, 255);
    v->sar_width = rr(0, 65535);
    v->sar_height = rr(0, 65535);
    v->overscan_info_present = rnd() & 1;
    v->overscan_appropriate = rnd() & 1;
    v->video_signal_type_present = rnd() & 1;
    v->video_format = rr(0, 7);
    v->video_full_range = rnd() & 1;
    v->colour_description_present = rnd() & 1;
    v->colour_primaries = rr(0, 255);
    v->transfer_characteristics = rr(0, 255);
    v->matrix_coeffs = rr(0, 255);
    v->chroma_loc_info_present = rnd() & 1;
    v->chroma_loc_top = rr(0, 5);
    v->chroma_loc_bottom = rr(0, 5);
    v->field_seq = rnd() & 1;
    v->frame_field_info_present = rnd() & 1;
    v->default_display_window = rnd() & 1;
    v->def_disp_left = rr(0, 100);
    v->def_disp_right = rr(0, 100);
    v->def_disp_top = rr(0, 100);
    v->def_disp_bottom = rr(0, 100);
    v->num_units_in_tick = rnd();
    v->time_scale = rnd();
    v->hrd_present = rnd() & 1;
    v->hrd.bit_rate_scale = rr(0, 15);
    v->hrd.cpb_size_scale = rr(0, 15);
    v->hrd.initial_cpb_removal_delay_length = rr(1, 32);
    v->hrd.cpb_removal_delay_length = rr(1, 32);
    v->hrd.dpb_output_delay_length = rr(1, 32);
    v->hrd.bit_rate_value = rr(1, 1000000);
    v->hrd.cpb_size_value = rr(1, 1000000);
    v->hrd.cbr_flag = rnd() & 1;
}

static void rand_pps(T30HevcPps *p)
{
    memset(p, 0, sizeof(*p));
    p->sign_data_hiding = rnd() & 1;
    p->constrained_intra_pred = rnd() & 1;
    p->transform_skip = rnd() & 1;
    p->cu_qp_delta_enabled = rnd() & 1;
    p->diff_cu_qp_delta_depth = rr(0, 3);
    p->cb_qp_offset = rs(-12, 12);
    p->cr_qp_offset = rs(-12, 12);
    p->slice_chroma_qp_offsets_present = rnd() & 1;
    p->weighted_pred = rnd() & 1;
    p->weighted_bipred = rnd() & 1;
    p->transquant_bypass = rnd() & 1;
    p->entropy_coding_sync = rnd() & 1;
    p->loop_filter_across_slices = rnd() & 1;
    p->deblocking_control_present = rnd() & 1;
    p->deblocking_disabled = rnd() & 1;
    p->beta_offset_div2 = rs(-6, 6);
    p->tc_offset_div2 = rs(-6, 6);
    p->num_ref_idx_l0_default = rr(1, 15);
    p->num_ref_idx_l1_default = rr(1, 15);
    p->init_qp_minus26 = rs(-26, 25);
}


/* one case: VPS, SPS, PPS and a slice header that the writer supports */
static void hevc_case(T30HevcVps *v, T30HevcSps *sp, T30HevcPps *pp, T30HevcSlice *sl)
{
    T30HevcVps V;
    T30HevcSlice L;

    memset(&V, 0, sizeof(V));
    rand_ptl(&V.ptl);
    V.max_sub_layers = (rnd() & 3) ? 1 : rr(1, 8);
    V.max_dec_pic_buffering = rr(1, 16);
    V.num_reorder_pics = rr(0, 15);
    V.max_latency_increase = rr(0, 1000);
    rand_sps(sp);
    rand_pps(pp);
    memset(&L, 0, sizeof(L));
    {
        static const uint32_t nals[] = { 0, 1, 19, 21 };
        int32_t d = 0;

        L.nal_unit_type = nals[rnd() % 4];
        L.slice_type = rr(0, 2);
        L.poc = rs(0, 1000);
        L.last_idr_poc = rs(0, L.poc);
        L.num_sps_rps = rr(0, 64);
        L.rps_idx = (rnd() & 1) || !L.num_sps_rps ? -1 : (int32_t)rr(0, L.num_sps_rps - 1);
        L.num_negative = rr(0, 4);
        L.num_positive = rr(0, 3);
        for (uint32_t i = 0; i < L.num_negative; i++)
            L.delta_poc[i] = d -= (int32_t)rr(1, 5);
        d = 0;
        for (uint32_t i = L.num_negative; i < L.num_negative + L.num_positive; i++)
            L.delta_poc[i] = d += (int32_t)rr(1, 5);
        for (uint32_t i = 0; i < L.num_negative + L.num_positive; i++)
            L.used[i] = L.nal_unit_type == 21 ? 0 : (rnd() & 1);
        L.collocated_from_l0 = rnd() & 1;
        L.collocated_ref_idx = rr(0, 3);
        L.num_ref_idx_l0 = L.slice_type == 2 ? 0 : rr(0, 4);
        L.num_ref_idx_l1 = L.slice_type == 2 ? 0 : rr(0, 4);
        L.max_num_merge_cand = rr(1, 5);
        L.slice_qp = rs(0, 51);
        L.cb_qp_offset = rs(-12, 12);
        L.cr_qp_offset = rs(-12, 12);
        L.loop_filter_across_slices = rnd() & 1;
        if (rnd() & 1) {
            L.address_bits = rr(1, 16);
            L.segment_address = rr(1, (1u << L.address_bits) - 1);
        }
    }
    *v = V;
    *sl = L;
}

/* FNV-1a over a writer's output: whole bytes, pending bit count and bits */
static uint32_t hevc_hash(uint32_t h, const uint8_t *buf, uint32_t len,
              uint32_t bits, uint8_t part)
{
    for (uint32_t i = 0; i < len; i++)
    h = (h ^ buf[i]) * 16777619u;
    h = (h ^ bits) * 16777619u;
    return (h ^ part) * 16777619u;
}

#endif
