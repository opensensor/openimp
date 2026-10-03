#include "t41_command_builder.h"

#include <limits.h>
#include <string.h>

/*
 * T41 HEVC notes (vendor libimp T41 1.2.0: ChanParamToCmdRegs 0xc6dd0,
 * SliceParamToCmdRegsEnc1 0xc7128, encode1 0xb0104, PrepareCommand
 * 0xaf534; not yet checked against a live HEVC command capture).
 *
 * - The packers are codec independent except: cmd[0] bits 2:0 = codec,
 *   TU/CU/CTB log2 sizes from the channel (HEVC defaults CTB 32, CU 8,
 *   TU 4..32); cmd[25] bits 16-18/20-21/30 (deblocking override, core,
 *   tools bit 14; all 0 for one P-only slice) and cmd[28] bit 15 (0) are
 *   HEVC-only; the AVC-only cmd[28] bits stay 0.
 * - PrepareCommand copies the 60-byte entropy block at +0x800 for AVC only.
 * - encode1: PCM size per LCU (cmd[29]) 0x5000 for CTB 32; LCU geometry
 *   (cmd[5], [27], [32], [34]) and the HWRC grid/targets (InitHwRateCtrl,
 *   UpdateHwRateCtrlParam) on the CTB grid with the same formulas.
 * - Picture numbers cmd[8..13] are POCs: AVC 2n, HEVC n.
 * - InitMERange: HEVC horizontal range = min(width, 1920) instead of the
 *   AVC level value 1920 (cmd[153] for P); vertical and the per-type
 *   ranges in cmd[33] are channel caps and unchanged.
 * - AL_GetAllocSize_EncReference: the circular reconstruction buffer holds
 *   align64(h) + CTB + 32 lines; AL_RefMngr_UpdateOffsets advances by
 *   align64(h) lines modulo that, i.e. back by CTB + 32 lines.
 * - AL_GetLambda: HEVC_NEW_DEFAULT_LDA_TABLE in the same EP1 layout.
 * - Slice headers are software (OutputSlice); OpenIMP prewrites them with
 *   the T31 HEVC writer in front of the +0x220 payload.
 * - cmd[152] bit 6 is the channel scaling-list tool (on for AVC); HEVC
 *   runs without scaling lists.
 */

static uint32_t openimp_t41_align_up(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static void openimp_t41_get_hwrc_grid(uint32_t width, uint32_t height,
                                      uint32_t log2_ctb,
                                      uint32_t *group_count_out,
                                      uint32_t *columns_per_group_out)
{
    uint32_t ctb = 1u << log2_ctb;
    uint32_t lcu_w = (width + ctb - 1u) >> log2_ctb;
    uint32_t lcu_h = (height + ctb - 1u) >> log2_ctb;
    uint32_t group_count = lcu_w;
    uint32_t divisor;

    if (lcu_w == 0u) {
        *group_count_out = 1u;
        *columns_per_group_out = 1u;
        return;
    }

    /* InitHwRateCtrl: the same search on the CTB grid for both codecs
     * (lower bound 5 for CTB sizes other than 64). */
    divisor = lcu_w > 32u ? 32u : lcu_w - 1u;
    while (divisor >= 5u) {
        if ((lcu_w % divisor) == 0u) {
            uint32_t columns_per_group = lcu_w / divisor;

            if (columns_per_group < 0x41u &&
                (uint64_t)lcu_h * columns_per_group < 0x400u)
                group_count = divisor;
        }
        --divisor;
    }

    *group_count_out = group_count;
    *columns_per_group_out = lcu_w / group_count;
}

uint32_t openimp_t41_reconstruction_pitch(uint32_t width)
{
    uint64_t pitch = (((uint64_t)width + 0x3fu) >> 6) << 8;

    return pitch <= UINT32_MAX ? (uint32_t)pitch : 0u;
}

uint32_t openimp_t41_reconstruction_margin_rows(uint32_t log2_ctb)
{
    /* AL_GetAllocSize_EncReference with the circular-buffer option:
     * align64(height) + CTB + ((vertical range 16 + 3) >> 2) * 8 lines. */
    return (1u << log2_ctb) + 32u;
}

uint32_t openimp_t41_reconstruction_luma_size_ctb(uint32_t width,
                                                  uint32_t height,
                                                  uint32_t log2_ctb)
{
    uint64_t pitch = openimp_t41_reconstruction_pitch(width);
    uint64_t rows = (openimp_t41_align_up(height, 64u) +
                     openimp_t41_reconstruction_margin_rows(log2_ctb)) >> 2;
    uint64_t size = pitch * rows;

    return size <= UINT32_MAX ? (uint32_t)size : 0u;
}

uint32_t openimp_t41_reconstruction_chroma_size_ctb(uint32_t width,
                                                    uint32_t height,
                                                    uint32_t log2_ctb)
{
    return openimp_t41_reconstruction_luma_size_ctb(width, height,
                                                    log2_ctb) >> 1;
}

uint32_t openimp_t41_reconstruction_luma_size(uint32_t width,
                                              uint32_t height)
{
    return openimp_t41_reconstruction_luma_size_ctb(
        width, height, OPENIMP_T41_LOG2_CTB_AVC);
}

uint32_t openimp_t41_reconstruction_chroma_size(uint32_t width,
                                                uint32_t height)
{
    return openimp_t41_reconstruction_luma_size(width, height) >> 1;
}

uint32_t openimp_t41_reconstruction_map_luma_size(uint32_t width,
                                                  uint32_t height)
{
    uint64_t width_4k_tiles = ((uint64_t)width + 0xfffu) >> 12;
    uint64_t height_quads = openimp_t41_align_up(height, 64u) >> 2;
    uint64_t size = width_4k_tiles * 32u * height_quads;

    return size <= UINT32_MAX ? (uint32_t)size : 0u;
}

uint32_t openimp_t41_reconstruction_map_chroma_size(uint32_t width,
                                                    uint32_t height)
{
    uint32_t luma = openimp_t41_reconstruction_map_luma_size(width, height);

    return openimp_t41_align_up(luma >> 1, 0x200u);
}

uint32_t openimp_t41_reconstruction_map_slot_size(uint32_t width,
                                                  uint32_t height)
{
    uint64_t size = openimp_t41_reconstruction_map_luma_size(width, height);

    size += openimp_t41_reconstruction_map_chroma_size(width, height);
    return size <= UINT32_MAX ? (uint32_t)size : 0u;
}

uint32_t openimp_t41_motion_vector_slot_size(uint32_t width,
                                             uint32_t height)
{
    uint64_t lcu_w = ((uint64_t)width + 15u) >> 4;
    uint64_t lcu_h = ((uint64_t)height + 15u) >> 4;
    uint64_t size = (2u * lcu_w * lcu_h + 0x10u) << 4;

    return size <= UINT32_MAX ? (uint32_t)size : 0u;
}

uint32_t openimp_t41_reconstruction_manager_size_ctb(uint32_t width,
                                                     uint32_t height,
                                                     uint32_t log2_ctb)
{
    uint64_t size = openimp_t41_reconstruction_luma_size_ctb(width, height,
                                                             log2_ctb);

    size += openimp_t41_reconstruction_chroma_size_ctb(width, height,
                                                       log2_ctb);
    size += 2u * openimp_t41_reconstruction_map_slot_size(width, height);
    size += 0x100u;
    size += 2u * openimp_t41_motion_vector_slot_size(width, height);
    return size <= UINT32_MAX ? (uint32_t)size : 0u;
}

uint32_t openimp_t41_reconstruction_manager_size(uint32_t width,
                                                 uint32_t height)
{
    return openimp_t41_reconstruction_manager_size_ctb(
        width, height, OPENIMP_T41_LOG2_CTB_AVC);
}

uint32_t openimp_t41_hwrc_grid(uint32_t width, uint32_t height)
{
    return openimp_t41_hwrc_grid_ctb(width, height,
                                     OPENIMP_T41_LOG2_CTB_AVC);
}

uint32_t openimp_t41_hwrc_grid_ctb(uint32_t width, uint32_t height,
                                   uint32_t log2_ctb)
{
    uint32_t group_count;
    uint32_t columns_per_group;

    openimp_t41_get_hwrc_grid(width, height, log2_ctb, &group_count,
                              &columns_per_group);
    return 0xf4000000u |
           (((group_count - 1u) & 0x3ffu) << 6) |
           ((columns_per_group - 1u) & 0x3fu);
}

static int openimp_t41_params_are_valid(
    const OpenIMPT41CommandParams *params)
{
    uint32_t width_8;
    uint32_t height_8;
    uint32_t lcu_w;
    uint32_t lcu_h;

    if (!params || !params->width || !params->height ||
        !params->bitrate || !params->fps_num || !params->fps_den)
        return 0;

    width_8 = (params->width + 7u) >> 3;
    height_8 = (params->height + 7u) >> 3;
    lcu_w = (params->width + 15u) >> 4;
    lcu_h = (params->height + 15u) >> 4;
    if (!width_8 || width_8 > 0x800u ||
        !height_8 || height_8 > 0x800u ||
        !lcu_w || lcu_w > 0x400u || !lcu_h || lcu_h > 0x400u)
        return 0;

    if (params->min_qp > params->picture_qp ||
        params->picture_qp > params->max_qp ||
        params->min_qp > params->rate_control_qp ||
        params->rate_control_qp > params->max_qp ||
        params->max_qp > 51u)
        return 0;
    if (params->picture_number > UINT32_MAX / 2u)
        return 0;

    if (!params->source_y || !params->source_uv ||
        !params->reconstruction_y || !params->reconstruction_uv ||
        !params->reconstruction_map_luma ||
        !params->reconstruction_map_chroma ||
        !params->stream_buffer || !params->ep2 || !params->ep1 ||
        !params->mv_current || !params->ep3 ||
        params->stream_part_offset <= OPENIMP_T41_STREAM_PAYLOAD_OFFSET)
        return 0;

    if (!params->is_idr &&
        (!params->reference_y || !params->reference_uv ||
         !params->reference_map_luma || !params->reference_map_chroma ||
         !params->mv_previous || !params->picture_number))
        return 0;

    return 1;
}

/*
 * HEVC P pictures: InitMERange gives HEVC the picture width (capped at 1920)
 * as horizontal search range instead of AVC's level table (1920); the
 * vertical range stays at the 16-line channel cap of the circular reference
 * buffer.  SliceParamToCmdRegsEnc1 packs it as ((h >> 6) - 1) << 1.
 */
static uint32_t openimp_t41_hevc_me_word(uint32_t width)
{
    uint32_t h_range = width > 1920u ? 1920u : width;

    if (h_range < 64u)
        h_range = 64u;
    return 1u | ((((h_range >> 6) - 1u) & 0x3ffu) << 1) |
           (((16u >> 3) - 1u) << 13);
}

int openimp_t41_build_command(void *slot, size_t slot_size,
                              const OpenIMPT41CommandParams *params)
{
    uint32_t *cmd = (uint32_t *)slot;
    uint32_t *entropy;
    uint32_t width_8;
    uint32_t height_8;
    uint32_t log2_ctb;
    uint32_t ctb;
    uint32_t lcu_w;
    uint32_t lcu_h;
    uint32_t lcu_count;
    uint32_t picture_word;
    uint32_t picture_step;
    uint32_t luma_size;
    uint32_t chroma_size;
    uint32_t rec_pitch;
    uint32_t group_count;
    uint32_t columns_per_group;
    uint64_t target;
    uint64_t long_term;
    int hevc;
    int hwrc;

    if (!slot || ((uintptr_t)slot & 3u) != 0u ||
        !openimp_t41_command_slot_is_valid(slot_size) ||
        !openimp_t41_params_are_valid(params))
        return -1;

    hevc = params->codec_hevc != 0u;
    hwrc = !hevc || !params->hevc_no_hwrc;
    log2_ctb = hevc ? OPENIMP_T41_LOG2_CTB_HEVC : OPENIMP_T41_LOG2_CTB_AVC;
    ctb = 1u << log2_ctb;
    width_8 = (params->width + 7u) >> 3;
    height_8 = (params->height + 7u) >> 3;
    lcu_w = (params->width + ctb - 1u) >> log2_ctb;
    lcu_h = (params->height + ctb - 1u) >> log2_ctb;
    lcu_count = lcu_w * lcu_h;
    /* AVC frame POC is 2n; the HEVC POC steps by one. */
    picture_step = hevc ? 1u : 2u;
    picture_word = params->picture_number * picture_step;
    luma_size = openimp_t41_reconstruction_luma_size_ctb(
        params->width, params->height, log2_ctb);
    chroma_size = luma_size >> 1;
    rec_pitch = openimp_t41_reconstruction_pitch(params->width);
    openimp_t41_get_hwrc_grid(params->width, params->height, log2_ctb,
                              &group_count, &columns_per_group);

    target = params->bitrate;
    if (!params->is_idr)
        target = target * 5u / 7u;
    target = target * 95u / 100u;
    target = target * group_count / lcu_count;
    if ((uint64_t)params->bitrate >
        UINT64_MAX / params->fps_den / group_count)
        return -1;
    long_term = (uint64_t)params->bitrate * params->fps_den * group_count /
                ((uint64_t)lcu_count * params->fps_num);
    if (target > 0x00ffffffu || long_term > 0x00ffffffu)
        return -1;

    memset(slot, 0, OPENIMP_T41_CL_SLOT_SIZE);

    /* ChanParamToCmdRegs: codec in bits 2:0, log2 min TU - 2 (9:8),
     * log2 max TU - 2 (11:10), log2 min CU - 4 (22:20), log2 CTB - 4
     * (26:24); bit 31 is the last slice.  HEVC: TU 4..32, CU 8, CTB 32. */
    cmd[0] = hevc ? 0x81700c01u : 0x80700400u;
    cmd[1] = (((height_8 - 1u) & 0x7ffu) << 16) |
             ((width_8 - 1u) & 0x7ffu);
    cmd[2] = 0x00010006u;
    /* bit 30 temporal MV prediction, bits 9:8 uCabacInitIdc (the HEVC
     * slice cabac_init_flag), bit 10 CABAC, bits 6:4 merge candidates. */
    cmd[3] = 0x40000d50u;
    if (hevc && params->hevc_no_tmvp)
        cmd[3] &= ~0x40000000u;
    if (hevc && params->hevc_cabac_init_idc0)
        cmd[3] &= ~0x00000300u;
    cmd[5] = lcu_count - 1u;

    if (!params->is_idr) {
        cmd[8] = picture_word;
        cmd[9] = picture_word - picture_step;
        cmd[11] = picture_word - picture_step;
        cmd[12] = params->picture_number > 1u
                    ? picture_word - 2u * picture_step : 0xffffffffu;
    } else {
        cmd[12] = 0xffffffffu;
    }
    cmd[13] = 0xffffffffu;

    cmd[24] = (params->is_idr ? 0x21000000u : 0x11000000u) |
              (params->picture_qp << 16);
    cmd[25] = 0x00083f1fu;
    cmd[27] = (((lcu_h - 1u) & 0x3ffu) << 12) |
              ((lcu_w - 1u) & 0x3ffu) |
              (params->is_idr ? 0u : 0x400u);
    /* PCM bytes of one LCU (encode1): AVC 8*256 + 0x80 + 8*128,
     * HEVC (8*1024 + 8*512) * 5 / 3. */
    cmd[29] = hevc ? 0x00005000u : 0x00000c80u;

    cmd[32] = 0x80000000u |
              (((lcu_h - 1u) & 0x3ffu) << 12) |
              ((lcu_w - 1u) & 0x3ffu);
    if (!params->is_idr)
        cmd[33] = 0x12000000u;
    cmd[34] = ((lcu_w - 1u) & 0x3ffu) << 12;

    cmd[40] = params->source_y;
    cmd[42] = params->source_uv;
    cmd[52] = openimp_t41_align_up(params->width, 16u);
    cmd[53] = cmd[52];

    if (!params->is_idr) {
        cmd[66] = params->reference_y;
        cmd[68] = params->reference_uv;
        cmd[72] = params->reference_map_luma;
        cmd[74] = params->reference_map_chroma;
    }

    cmd[108] = params->reference_luma_offset;
    cmd[109] = params->reference_chroma_offset;
    cmd[110] = luma_size;
    cmd[111] = chroma_size;

    cmd[112] = params->reconstruction_y;
    cmd[114] = params->reconstruction_uv;
    cmd[118] = params->reconstruction_map_luma;
    cmd[120] = params->reconstruction_map_chroma;
    cmd[124] = params->reconstruction_luma_offset;
    cmd[125] = params->reconstruction_chroma_offset;
    cmd[126] = luma_size;
    cmd[127] = chroma_size;

    cmd[128] = 0x02000000u | rec_pitch;
    cmd[129] = cmd[128];
    cmd[130] = 0x00000200u;

    cmd[136] = params->stream_buffer;
    cmd[139] = params->stream_part_offset;
    cmd[140] = OPENIMP_T41_STREAM_PAYLOAD_OFFSET;
    cmd[141] = params->stream_part_offset -
               OPENIMP_T41_STREAM_PAYLOAD_OFFSET;

    /* bit 6 is the channel's scaling-list tool; HEVC runs without
     * scaling lists (SPS scaling_list_enabled_flag 0). */
    cmd[152] = hevc ? 0x000000b6u : 0x000000f6u;
    if (params->is_idr)
        cmd[153] = 0x007fe7ffu;
    else
        cmd[153] = hevc ? openimp_t41_hevc_me_word(params->width)
                        : 0x0000203bu;
    cmd[154] = params->ep2;
    cmd[156] = params->ep1;
    if (!params->is_idr)
        cmd[158] = params->mv_previous;
    cmd[160] = params->mv_current;

    if (hwrc) {
        cmd[176] = openimp_t41_hwrc_grid_ctb(params->width, params->height,
                                             log2_ctb);
        cmd[177] = (uint32_t)target;
        cmd[178] = 0x3f000000u | (uint32_t)long_term;
        cmd[179] = (params->min_qp << 24) |
                   (params->picture_qp << 16) |
                   (params->max_qp << 8) |
                   params->rate_control_qp;
        cmd[180] = ((params->is_idr || params->picture_number == 1u)
                        ? 0xc0000000u : 0x40000000u) |
                   (((columns_per_group * 4u + 1u) & 0xffu) << 20) |
                   (((uint32_t)target >> 7) & 0xffffu);
        cmd[181] = 1u;
    }
    cmd[182] = params->ep3;

    /* PrepareCommand copies the inline entropy (Enc2) block for AVC only;
     * the HEVC core writes the final CABAC stream itself. */
    if (!hevc) {
        entropy = cmd + OPENIMP_T41_CL_ENTROPY_OFFSET / sizeof(uint32_t);
        entropy[0] = 0x000a0c80u;
        entropy[1] = cmd[24] | 0x00000d06u;
        entropy[3] = cmd[34];
        entropy[4] = cmd[5];
    }
    return 0;
}
