#include "t41_command_builder.h"

#include <limits.h>
#include <string.h>

static uint32_t openimp_t41_align_up(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static void openimp_t41_get_hwrc_grid(uint32_t width, uint32_t height,
                                      uint32_t *group_count_out,
                                      uint32_t *columns_per_group_out)
{
    uint32_t lcu_w = (width + 15u) >> 4;
    uint32_t lcu_h = (height + 15u) >> 4;
    uint32_t group_count = lcu_w;
    uint32_t divisor;

    if (lcu_w == 0u) {
        *group_count_out = 1u;
        *columns_per_group_out = 1u;
        return;
    }

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

uint32_t openimp_t41_reconstruction_luma_size(uint32_t width,
                                              uint32_t height)
{
    uint64_t pitch = openimp_t41_reconstruction_pitch(width);
    uint64_t rows = (openimp_t41_align_up(height, 64u) >> 2) + 12u;
    uint64_t size = pitch * rows;

    return size <= UINT32_MAX ? (uint32_t)size : 0u;
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

uint32_t openimp_t41_reconstruction_manager_size(uint32_t width,
                                                 uint32_t height)
{
    uint64_t size = openimp_t41_reconstruction_luma_size(width, height);

    size += openimp_t41_reconstruction_chroma_size(width, height);
    size += 2u * openimp_t41_reconstruction_map_slot_size(width, height);
    size += 0x100u;
    size += 2u * openimp_t41_motion_vector_slot_size(width, height);
    return size <= UINT32_MAX ? (uint32_t)size : 0u;
}

uint32_t openimp_t41_hwrc_grid(uint32_t width, uint32_t height)
{
    uint32_t group_count;
    uint32_t columns_per_group;

    openimp_t41_get_hwrc_grid(width, height, &group_count,
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
        params->picture_qp > params->max_qp || params->max_qp > 51u)
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

int openimp_t41_build_command(void *slot, size_t slot_size,
                              const OpenIMPT41CommandParams *params)
{
    uint32_t *cmd = (uint32_t *)slot;
    uint32_t *entropy;
    uint32_t width_8;
    uint32_t height_8;
    uint32_t lcu_w;
    uint32_t lcu_h;
    uint32_t lcu_count;
    uint32_t picture_word;
    uint32_t luma_size;
    uint32_t chroma_size;
    uint32_t rec_pitch;
    uint32_t group_count;
    uint32_t columns_per_group;
    uint64_t target;
    uint64_t long_term;

    if (!slot || ((uintptr_t)slot & 3u) != 0u ||
        !openimp_t41_command_slot_is_valid(slot_size) ||
        !openimp_t41_params_are_valid(params))
        return -1;

    width_8 = (params->width + 7u) >> 3;
    height_8 = (params->height + 7u) >> 3;
    lcu_w = (params->width + 15u) >> 4;
    lcu_h = (params->height + 15u) >> 4;
    lcu_count = lcu_w * lcu_h;
    picture_word = params->picture_number * 2u;
    luma_size = openimp_t41_reconstruction_luma_size(
        params->width, params->height);
    chroma_size = luma_size >> 1;
    rec_pitch = openimp_t41_reconstruction_pitch(params->width);
    openimp_t41_get_hwrc_grid(params->width, params->height,
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

    cmd[0] = 0x80700400u;
    cmd[1] = (((height_8 - 1u) & 0x7ffu) << 16) |
             ((width_8 - 1u) & 0x7ffu);
    cmd[2] = 0x00010006u;
    cmd[3] = 0x40000d50u;
    cmd[5] = lcu_count - 1u;

    if (!params->is_idr) {
        cmd[8] = picture_word;
        cmd[9] = picture_word - 2u;
        cmd[11] = picture_word - 2u;
        cmd[12] = params->picture_number > 1u
                    ? picture_word - 4u : 0xffffffffu;
    } else {
        cmd[12] = 0xffffffffu;
    }
    cmd[13] = 0xffffffffu;

    cmd[24] = params->is_idr ? 0x21220000u : 0x11220000u;
    cmd[25] = 0x00083f1fu;
    cmd[27] = (((lcu_h - 1u) & 0x3ffu) << 12) |
              ((lcu_w - 1u) & 0x3ffu) |
              (params->is_idr ? 0u : 0x400u);
    cmd[29] = 0x00000c80u;

    cmd[32] = 0x80000000u |
              (((lcu_h - 1u) & 0x3ffu) << 12) |
              ((lcu_w - 1u) & 0x3ffu);
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

    cmd[152] = 0x000000f6u;
    cmd[153] = 0x0000203bu;
    cmd[154] = params->ep2;
    cmd[156] = params->ep1;
    if (!params->is_idr)
        cmd[158] = params->mv_previous;
    cmd[160] = params->mv_current;

    cmd[176] = openimp_t41_hwrc_grid(params->width, params->height);
    cmd[177] = (uint32_t)target;
    cmd[178] = 0x3f000000u | (uint32_t)long_term;
    cmd[179] = (params->min_qp << 24) |
               (params->picture_qp << 16) |
               (params->max_qp << 8) |
               params->picture_qp;
    cmd[180] = ((params->is_idr || params->picture_number == 1u)
                    ? 0xc0000000u : 0x40000000u) |
               (((columns_per_group * 4u + 1u) & 0xffu) << 20) |
               (((uint32_t)target >> 7) & 0xffffu);
    cmd[181] = 1u;
    cmd[182] = params->ep3;

    entropy = cmd + OPENIMP_T41_CL_ENTROPY_OFFSET / sizeof(uint32_t);
    entropy[0] = 0x000a0c80u;
    entropy[1] = cmd[24] | 0x00000d06u;
    entropy[3] = cmd[34];
    entropy[4] = cmd[5];
    return 0;
}
