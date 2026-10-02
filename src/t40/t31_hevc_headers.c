#include "t31_hevc_headers.h"

#include <string.h>

#define HEVC_RBSP_MAX 256u

typedef struct {
    uint8_t data[HEVC_RBSP_MAX];
    uint32_t bits;
    int overflow;
} HevcBits;

static void hevc_bits_init(HevcBits *bits)
{
    memset(bits, 0, sizeof(*bits));
}

static void put_bit(HevcBits *bits, uint32_t value)
{
    uint32_t byte = bits->bits >> 3;

    if (byte >= HEVC_RBSP_MAX) {
        bits->overflow = 1;
        return;
    }
    if (value & 1u)
        bits->data[byte] |= (uint8_t)(0x80u >> (bits->bits & 7u));
    bits->bits++;
}

static void put_bits(HevcBits *bits, uint32_t value, unsigned int count)
{
    while (count--)
        put_bit(bits, (value >> count) & 1u);
}

static void put_ue(HevcBits *bits, uint32_t value)
{
    uint64_t code = (uint64_t)value + 1u;
    int length = 0;
    int bit;

    while ((code >> (length + 1)) != 0u)
        length++;
    put_bits(bits, 0u, (unsigned int)length);
    for (bit = length; bit >= 0; bit--)
        put_bit(bits, (uint32_t)(code >> bit) & 1u);
}

static void put_se(HevcBits *bits, int32_t value)
{
    put_ue(bits, value > 0 ? (uint32_t)value * 2u - 1u
                           : (uint32_t)(-(int64_t)value) * 2u);
}

/* rbsp_trailing_bits() and byte_alignment() have the same shape. */
static void put_trailing_bits(HevcBits *bits)
{
    put_bit(bits, 1u);
    while (bits->bits & 7u)
        put_bit(bits, 0u);
}

static int hevc_emit_nal(uint8_t *dst, size_t capacity, uint32_t nal_type,
                         const HevcBits *bits)
{
    uint32_t length = bits->bits >> 3;
    uint32_t zeros = 0u;
    size_t pos = 0u;
    uint32_t index;

    if (!dst || bits->overflow || (bits->bits & 7u) || nal_type > 63u)
        return -1;
    /* Worst case: one emulation byte per two payload bytes. */
    if (capacity < 6u + (size_t)length + (size_t)length / 2u + 1u)
        return -1;
    dst[pos++] = 0x00u;
    dst[pos++] = 0x00u;
    dst[pos++] = 0x00u;
    dst[pos++] = 0x01u;
    /* forbidden_zero_bit, nal_unit_type, nuh_layer_id 0, temporal id 0. */
    dst[pos++] = (uint8_t)(nal_type << 1);
    dst[pos++] = 0x01u;
    for (index = 0u; index < length; index++) {
        uint8_t value = bits->data[index];

        if (zeros >= 2u && value <= 0x03u) {
            dst[pos++] = 0x03u;
            zeros = 0u;
        }
        dst[pos++] = value;
        zeros = value == 0u ? zeros + 1u : 0u;
    }
    return (int)pos;
}

static int valid_config(const OpenIMPT31HevcConfig *config)
{
    return config && config->width && config->height &&
           config->log2_min_cb_size >= 3u &&
           config->log2_ctb_size >= config->log2_min_cb_size &&
           config->log2_ctb_size <= 6u &&
           config->log2_min_tb_size >= 2u &&
           config->log2_max_tb_size >= config->log2_min_tb_size &&
           config->log2_max_tb_size <= 5u &&
           config->log2_min_tb_size < config->log2_min_cb_size &&
           config->log2_max_poc_lsb >= 4u &&
           config->log2_max_poc_lsb <= 16u &&
           config->max_merge_cand >= 1u && config->max_merge_cand <= 5u &&
           config->width <= 8192u && config->height <= 8192u;
}

static uint32_t gcd_u32(uint32_t a, uint32_t b)
{
    while (b) {
        uint32_t t = a % b;

        a = b;
        b = t;
    }
    return a;
}

uint8_t openimp_t31_hevc_level_idc(uint32_t width, uint32_t height,
                                   uint32_t fps_num, uint32_t fps_den,
                                   uint32_t bitrate)
{
    /* Tables A.6/A.8: general_level_idc, MaxLumaPs, MaxLumaSr and the Main
     * tier MaxBR in kbit/s (CpbBrVclFactor 1000 for the Main profile). */
    static const struct {
        uint8_t idc;
        uint32_t max_luma_ps;
        uint64_t max_luma_sr;
        uint32_t max_br_kbps;
    } levels[] = {
        { 30u, 36864u, 552960u, 128u },
        { 60u, 122880u, 3686400u, 1500u },
        { 63u, 245760u, 7372800u, 3000u },
        { 90u, 552960u, 16588800u, 6000u },
        { 93u, 983040u, 33177600u, 10000u },
        { 120u, 2228224u, 66846720u, 12000u },
        { 123u, 2228224u, 133693440u, 20000u },
        { 150u, 8912896u, 267386880u, 25000u },
        { 153u, 8912896u, 534773760u, 40000u },
        { 156u, 8912896u, 1069547520u, 60000u },
        { 180u, 35651584u, 1069547520u, 60000u },
        { 183u, 35651584u, 2139095040u, 120000u },
        { 186u, 35651584u, 4278190080u, 240000u },
    };
    uint64_t picture = (uint64_t)width * height;
    uint64_t rate;
    unsigned int index;

    if (!fps_num)
        fps_num = 25u;
    if (!fps_den)
        fps_den = 1u;
    rate = (picture * fps_num + fps_den - 1u) / fps_den;
    for (index = 0u; index < sizeof(levels) / sizeof(levels[0]); index++) {
        /* Each dimension must also stay below sqrt(8 * MaxLumaPs). */
        uint64_t limit = (uint64_t)levels[index].max_luma_ps * 8u;

        if (picture <= levels[index].max_luma_ps &&
            rate <= levels[index].max_luma_sr &&
            (uint64_t)width * width <= limit &&
            (uint64_t)height * height <= limit &&
            (uint64_t)bitrate <= (uint64_t)levels[index].max_br_kbps * 1000u)
            return levels[index].idc;
    }
    return 186u;
}

void openimp_t31_hevc_default_config(OpenIMPT31HevcConfig *config,
                                     uint32_t width, uint32_t height,
                                     uint32_t fps_num, uint32_t fps_den)
{
    if (!config)
        return;
    memset(config, 0, sizeof(*config));
    config->width = width;
    config->height = height;
    config->fps_num = fps_num ? fps_num : 25u;
    config->fps_den = fps_den ? fps_den : 1u;
    config->log2_ctb_size = 5u;
    config->log2_min_cb_size = 3u;
    config->log2_max_tb_size = 5u;
    config->log2_min_tb_size = 2u;
    config->max_transform_depth_inter = 1u;
    config->max_transform_depth_intra = 1u;
    config->log2_max_poc_lsb = 8u;
    config->level_idc = openimp_t31_hevc_level_idc(
        width, height, config->fps_num, config->fps_den, 0u);
    config->max_merge_cand = 5u;
    config->tmvp_enabled = 1u;
    config->strong_intra_smoothing = 1u;
    config->loop_filter_across_slices = 1u;
    config->beta_offset_div2 = -1;
    config->tc_offset_div2 = -1;
}

static void put_profile_tier_level(HevcBits *bits,
                                   const OpenIMPT31HevcConfig *config)
{
    unsigned int flag;

    put_bits(bits, 0u, 2);                 /* general_profile_space */
    put_bit(bits, config->tier);           /* general_tier_flag */
    put_bits(bits, 1u, 5);                 /* general_profile_idc: Main */
    for (flag = 0u; flag < 32u; flag++)    /* Main is Main10-decodable */
        put_bit(bits, flag == 1u || flag == 2u);
    put_bit(bits, 1u);                     /* progressive_source */
    put_bit(bits, 0u);                     /* interlaced_source */
    put_bit(bits, 0u);                     /* non_packed_constraint */
    put_bit(bits, 1u);                     /* frame_only_constraint */
    put_bits(bits, 0u, 32);                /* general_reserved_zero_43bits */
    put_bits(bits, 0u, 11);
    put_bit(bits, 0u);                     /* general_inbld/reserved */
    put_bits(bits, config->level_idc, 8);
}

int openimp_t31_hevc_write_vps(uint8_t *dst, size_t capacity,
                               const OpenIMPT31HevcConfig *config)
{
    HevcBits bits;

    if (!valid_config(config))
        return -1;
    hevc_bits_init(&bits);
    put_bits(&bits, 0u, 4);         /* vps_video_parameter_set_id */
    put_bit(&bits, 1u);             /* vps_base_layer_internal_flag */
    put_bit(&bits, 1u);             /* vps_base_layer_available_flag */
    put_bits(&bits, 0u, 6);         /* vps_max_layers_minus1 */
    put_bits(&bits, 0u, 3);         /* vps_max_sub_layers_minus1 */
    put_bit(&bits, 1u);             /* vps_temporal_id_nesting_flag */
    put_bits(&bits, 0xffffu, 16);   /* vps_reserved_0xffff_16bits */
    put_profile_tier_level(&bits, config);
    put_bit(&bits, 0u);             /* vps_sub_layer_ordering_info_present */
    put_ue(&bits, 1u);              /* vps_max_dec_pic_buffering_minus1 */
    put_ue(&bits, 0u);              /* vps_max_num_reorder_pics */
    put_ue(&bits, 0u);              /* vps_max_latency_increase_plus1 */
    put_bits(&bits, 0u, 6);         /* vps_max_layer_id */
    put_ue(&bits, 0u);              /* vps_num_layer_sets_minus1 */
    put_bit(&bits, 0u);             /* vps_timing_info_present_flag */
    put_bit(&bits, 0u);             /* vps_extension_flag */
    put_trailing_bits(&bits);
    return hevc_emit_nal(dst, capacity, OPENIMP_T31_HEVC_NAL_VPS, &bits);
}

int openimp_t31_hevc_write_sps(uint8_t *dst, size_t capacity,
                               const OpenIMPT31HevcConfig *config)
{
    HevcBits bits;
    uint32_t min_cb;
    uint32_t coded_w;
    uint32_t coded_h;
    uint32_t tick;
    uint32_t scale;
    uint32_t divisor;

    if (!valid_config(config))
        return -1;
    min_cb = 1u << config->log2_min_cb_size;
    coded_w = (config->width + min_cb - 1u) & ~(min_cb - 1u);
    coded_h = (config->height + min_cb - 1u) & ~(min_cb - 1u);

    hevc_bits_init(&bits);
    put_bits(&bits, 0u, 4);         /* sps_video_parameter_set_id */
    put_bits(&bits, 0u, 3);         /* sps_max_sub_layers_minus1 */
    put_bit(&bits, 1u);             /* sps_temporal_id_nesting_flag */
    put_profile_tier_level(&bits, config);
    put_ue(&bits, 0u);              /* sps_seq_parameter_set_id */
    put_ue(&bits, 1u);              /* chroma_format_idc: 4:2:0 */
    put_ue(&bits, coded_w);
    put_ue(&bits, coded_h);
    put_bit(&bits, coded_w != config->width || coded_h != config->height);
    if (coded_w != config->width || coded_h != config->height) {
        /* Offsets in chroma samples (SubWidthC = SubHeightC = 2). */
        put_ue(&bits, 0u);
        put_ue(&bits, (coded_w - config->width) >> 1);
        put_ue(&bits, 0u);
        put_ue(&bits, (coded_h - config->height) >> 1);
    }
    put_ue(&bits, 0u);              /* bit_depth_luma_minus8 */
    put_ue(&bits, 0u);              /* bit_depth_chroma_minus8 */
    put_ue(&bits, config->log2_max_poc_lsb - 4u);
    put_bit(&bits, 1u);             /* sps_sub_layer_ordering_info_present */
    put_ue(&bits, 1u);              /* sps_max_dec_pic_buffering_minus1 */
    put_ue(&bits, 0u);              /* sps_max_num_reorder_pics */
    put_ue(&bits, 0u);              /* sps_max_latency_increase_plus1 */
    put_ue(&bits, config->log2_min_cb_size - 3u);
    put_ue(&bits, config->log2_ctb_size - config->log2_min_cb_size);
    put_ue(&bits, config->log2_min_tb_size - 2u);
    put_ue(&bits, config->log2_max_tb_size - config->log2_min_tb_size);
    put_ue(&bits, config->max_transform_depth_inter);
    put_ue(&bits, config->max_transform_depth_intra);
    put_bit(&bits, 0u);             /* scaling_list_enabled_flag */
    put_bit(&bits, 0u);             /* amp_enabled_flag */
    put_bit(&bits, 0u);             /* sample_adaptive_offset_enabled_flag */
    put_bit(&bits, 0u);             /* pcm_enabled_flag */
    put_ue(&bits, 0u);              /* num_short_term_ref_pic_sets */
    put_bit(&bits, 0u);             /* long_term_ref_pics_present_flag */
    put_bit(&bits, config->tmvp_enabled ? 1u : 0u);
    put_bit(&bits, config->strong_intra_smoothing ? 1u : 0u);

    put_bit(&bits, 1u);             /* vui_parameters_present_flag */
    put_bit(&bits, 0u);             /* aspect_ratio_info_present_flag */
    put_bit(&bits, 0u);             /* overscan_info_present_flag */
    put_bit(&bits, 1u);             /* video_signal_type_present_flag */
    put_bits(&bits, 5u, 3);         /* video_format: unspecified */
    put_bit(&bits, 0u);             /* video_full_range_flag */
    put_bit(&bits, 1u);             /* colour_description_present_flag */
    put_bits(&bits, 1u, 8);         /* BT.709 primaries */
    put_bits(&bits, 1u, 8);         /* BT.709 transfer */
    put_bits(&bits, 1u, 8);         /* BT.709 matrix */
    put_bit(&bits, 0u);             /* chroma_loc_info_present_flag */
    put_bit(&bits, 0u);             /* neutral_chroma_indication_flag */
    put_bit(&bits, 0u);             /* field_seq_flag */
    put_bit(&bits, 0u);             /* frame_field_info_present_flag */
    put_bit(&bits, 0u);             /* default_display_window_flag */
    tick = config->fps_den ? config->fps_den : 1u;
    scale = config->fps_num ? config->fps_num : 25u;
    divisor = gcd_u32(tick, scale);
    put_bit(&bits, 1u);             /* vui_timing_info_present_flag */
    put_bits(&bits, tick / divisor, 32);
    put_bits(&bits, scale / divisor, 32);
    put_bit(&bits, 0u);             /* vui_poc_proportional_to_timing */
    put_bit(&bits, 0u);             /* vui_hrd_parameters_present_flag */
    put_bit(&bits, 0u);             /* bitstream_restriction_flag */

    put_bit(&bits, 0u);             /* sps_extension_present_flag */
    put_trailing_bits(&bits);
    return hevc_emit_nal(dst, capacity, OPENIMP_T31_HEVC_NAL_SPS, &bits);
}

int openimp_t31_hevc_write_pps(uint8_t *dst, size_t capacity,
                               const OpenIMPT31HevcConfig *config)
{
    HevcBits bits;

    if (!valid_config(config))
        return -1;
    hevc_bits_init(&bits);
    put_ue(&bits, 0u);              /* pps_pic_parameter_set_id */
    put_ue(&bits, 0u);              /* pps_seq_parameter_set_id */
    put_bit(&bits, 0u);             /* dependent_slice_segments_enabled */
    put_bit(&bits, 0u);             /* output_flag_present_flag */
    put_bits(&bits, 0u, 3);         /* num_extra_slice_header_bits */
    put_bit(&bits, 0u);             /* sign_data_hiding_enabled_flag */
    put_bit(&bits, config->cabac_init_present ? 1u : 0u);
    put_ue(&bits, 0u);              /* num_ref_idx_l0_default_active_minus1 */
    put_ue(&bits, 0u);              /* num_ref_idx_l1_default_active_minus1 */
    put_se(&bits, 0);               /* init_qp_minus26 */
    put_bit(&bits, 0u);             /* constrained_intra_pred_flag */
    put_bit(&bits, 0u);             /* transform_skip_enabled_flag */
    put_bit(&bits, config->cu_qp_delta_enabled ? 1u : 0u);
    if (config->cu_qp_delta_enabled)
        put_ue(&bits, config->diff_cu_qp_delta_depth);
    put_se(&bits, 0);               /* pps_cb_qp_offset */
    put_se(&bits, 0);               /* pps_cr_qp_offset */
    put_bit(&bits, 0u);             /* pps_slice_chroma_qp_offsets_present */
    put_bit(&bits, 0u);             /* weighted_pred_flag */
    put_bit(&bits, 0u);             /* weighted_bipred_flag */
    put_bit(&bits, 0u);             /* transquant_bypass_enabled_flag */
    put_bit(&bits, 0u);             /* tiles_enabled_flag */
    put_bit(&bits, 0u);             /* entropy_coding_sync_enabled_flag */
    put_bit(&bits, config->loop_filter_across_slices ? 1u : 0u);
    put_bit(&bits, 1u);             /* deblocking_filter_control_present */
    put_bit(&bits, 0u);             /* deblocking_filter_override_enabled */
    put_bit(&bits, 0u);             /* pps_deblocking_filter_disabled_flag */
    put_se(&bits, config->beta_offset_div2);
    put_se(&bits, config->tc_offset_div2);
    put_bit(&bits, 0u);             /* pps_scaling_list_data_present_flag */
    put_bit(&bits, 0u);             /* lists_modification_present_flag */
    put_ue(&bits, 0u);              /* log2_parallel_merge_level_minus2 */
    put_bit(&bits, 0u);             /* slice_segment_header_extension */
    put_bit(&bits, 0u);             /* pps_extension_present_flag */
    put_trailing_bits(&bits);
    return hevc_emit_nal(dst, capacity, OPENIMP_T31_HEVC_NAL_PPS, &bits);
}

int openimp_t31_hevc_write_slice_header(uint8_t *dst, size_t capacity,
                                        const OpenIMPT31HevcConfig *config,
                                        const OpenIMPT31HevcSlice *slice)
{
    HevcBits bits;
    uint32_t nal_type;

    if (!valid_config(config) || !slice || slice->qp < 0 || slice->qp > 51)
        return -1;
    if (!slice->is_idr &&
        (slice->ref_poc_delta == 0u || slice->ref_poc_delta > 32768u))
        return -1;
    nal_type = slice->is_idr ? OPENIMP_T31_HEVC_NAL_IDR_W_RADL
                             : OPENIMP_T31_HEVC_NAL_TRAIL_R;

    hevc_bits_init(&bits);
    put_bit(&bits, 1u);             /* first_slice_segment_in_pic_flag */
    if (slice->is_idr)
        put_bit(&bits, 0u);         /* no_output_of_prior_pics_flag */
    put_ue(&bits, 0u);              /* slice_pic_parameter_set_id */
    put_ue(&bits, slice->is_idr ? OPENIMP_T31_HEVC_SLICE_I
                                : OPENIMP_T31_HEVC_SLICE_P);
    if (!slice->is_idr) {
        put_bits(&bits,
                 slice->poc & ((1u << config->log2_max_poc_lsb) - 1u),
                 config->log2_max_poc_lsb);
        put_bit(&bits, 0u);         /* short_term_ref_pic_set_sps_flag */
        /* st_ref_pic_set(0): one preceding reference picture. */
        put_ue(&bits, 1u);          /* num_negative_pics */
        put_ue(&bits, 0u);          /* num_positive_pics */
        put_ue(&bits, slice->ref_poc_delta - 1u);
        put_bit(&bits, 1u);         /* used_by_curr_pic_s0_flag */
        if (config->tmvp_enabled)
            put_bit(&bits, 1u);     /* slice_temporal_mvp_enabled_flag */
        put_bit(&bits, 0u);         /* num_ref_idx_active_override_flag */
        if (config->cabac_init_present)
            put_bit(&bits, slice->cabac_init_flag ? 1u : 0u);
        /* One L0 reference: collocated_ref_idx is inferred. */
        put_ue(&bits, 5u - config->max_merge_cand);
    }
    put_se(&bits, slice->qp - 26);  /* slice_qp_delta (init_qp 26) */
    if (config->loop_filter_across_slices)
        put_bit(&bits, 1u);         /* slice_loop_filter_across_slices */
    put_trailing_bits(&bits);       /* byte_alignment() */
    return hevc_emit_nal(dst, capacity, nal_type, &bits);
}

int openimp_t31_hevc_write_parameter_sets(uint8_t *dst, size_t capacity,
                                          const OpenIMPT31HevcConfig *config)
{
    int vps;
    int sps;
    int pps;

    if (!dst)
        return -1;
    vps = openimp_t31_hevc_write_vps(dst, capacity, config);
    if (vps < 0)
        return -1;
    sps = openimp_t31_hevc_write_sps(dst + vps, capacity - (size_t)vps,
                                     config);
    if (sps < 0)
        return -1;
    pps = openimp_t31_hevc_write_pps(dst + vps + sps,
                                     capacity - (size_t)vps - (size_t)sps,
                                     config);
    if (pps < 0)
        return -1;
    return vps + sps + pps;
}
