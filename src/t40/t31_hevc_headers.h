#ifndef OPENIMP_T31_HEVC_HEADERS_H
#define OPENIMP_T31_HEVC_HEADERS_H

#include <stddef.h>
#include <stdint.h>

/*
 * Host-written HEVC headers for the T31 AVPU (see docs/T31_HEVC.md).
 *
 * The AVPU HEVC core writes only slice_segment_data(); VPS, SPS, PPS and
 * the slice segment header come from the host, the way the vendor's
 * HEVC_GenerateSections / GenerateHevcSliceHeader do it.  Every syntax value
 * that the hardware also consumes through the command list (CTB/TU sizes,
 * merge candidates, temporal MV prediction, deblocking offsets, cu_qp_delta)
 * is a field of the configuration, so the codec can keep both in step.
 *
 * All writers return the number of bytes written to dst, including the
 * four-byte Annex B start code and the two-byte NAL header, with emulation
 * prevention applied, or -1 when dst is too small or the input is invalid.
 */

#define OPENIMP_T31_HEVC_NAL_TRAIL_R   1u
#define OPENIMP_T31_HEVC_NAL_IDR_W_RADL 19u
#define OPENIMP_T31_HEVC_NAL_CRA       21u
#define OPENIMP_T31_HEVC_NAL_VPS       32u
#define OPENIMP_T31_HEVC_NAL_SPS       33u
#define OPENIMP_T31_HEVC_NAL_PPS       34u
#define OPENIMP_T31_HEVC_NAL_AUD       35u

#define OPENIMP_T31_HEVC_SLICE_P 1u
#define OPENIMP_T31_HEVC_SLICE_I 2u

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t fps_num;
    uint32_t fps_den;
    uint8_t log2_ctb_size;          /* 5: 32x32 CTB */
    uint8_t log2_min_cb_size;       /* 3 */
    uint8_t log2_max_tb_size;       /* 5 */
    uint8_t log2_min_tb_size;       /* 2 */
    uint8_t max_transform_depth_inter;
    uint8_t max_transform_depth_intra;
    uint8_t log2_max_poc_lsb;       /* 4..16 */
    uint8_t level_idc;              /* general_level_idc (30 * level) */
    uint8_t tier;                   /* 0 = Main tier */
    uint8_t max_merge_cand;         /* 1..5 */
    uint8_t tmvp_enabled;           /* SPS and slice temporal MVP */
    uint8_t strong_intra_smoothing;
    uint8_t cabac_init_present;
    uint8_t cu_qp_delta_enabled;
    uint8_t diff_cu_qp_delta_depth;
    uint8_t loop_filter_across_slices;
    int8_t beta_offset_div2;
    int8_t tc_offset_div2;
} OpenIMPT31HevcConfig;

typedef struct {
    uint8_t is_idr;          /* IDR_W_RADL I slice, else TRAIL_R P slice */
    uint8_t cabac_init_flag; /* written only when cabac_init_present */
    uint32_t poc;            /* picture order count, 0 at the IDR */
    uint32_t ref_poc_delta;  /* poc - poc of the single L0 reference */
    int32_t qp;              /* slice QP, 0..51 */
} OpenIMPT31HevcSlice;

/* The vendor T31 defaults: CTB 32, CB 8..32, TB 4..32, transform depth 1,
 * five merge candidates, TMVP, deblocking -1/-1, no scaling lists, no SAO,
 * no AMP.  The level follows from the picture size and rate. */
void openimp_t31_hevc_default_config(OpenIMPT31HevcConfig *config,
                                     uint32_t width, uint32_t height,
                                     uint32_t fps_num, uint32_t fps_den);
uint8_t openimp_t31_hevc_level_idc(uint32_t width, uint32_t height,
                                   uint32_t fps_num, uint32_t fps_den);

int openimp_t31_hevc_write_vps(uint8_t *dst, size_t capacity,
                               const OpenIMPT31HevcConfig *config);
int openimp_t31_hevc_write_sps(uint8_t *dst, size_t capacity,
                               const OpenIMPT31HevcConfig *config);
int openimp_t31_hevc_write_pps(uint8_t *dst, size_t capacity,
                               const OpenIMPT31HevcConfig *config);
/* The slice segment header NAL up to and including byte_alignment(); the
 * hardware's slice_segment_data() follows it directly. */
int openimp_t31_hevc_write_slice_header(uint8_t *dst, size_t capacity,
                                        const OpenIMPT31HevcConfig *config,
                                        const OpenIMPT31HevcSlice *slice);
/* VPS + SPS + PPS, the IDR prefix. */
int openimp_t31_hevc_write_parameter_sets(uint8_t *dst, size_t capacity,
                                          const OpenIMPT31HevcConfig *config);

#endif
