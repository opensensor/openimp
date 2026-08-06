#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "t40/t41_command_builder.h"
#include "t40/t41_command_layout.h"
#include "t40/t41_hw_rate_control.h"

typedef struct ExpectedWord {
    uint16_t index;
    uint32_t value;
} ExpectedWord;

static void assert_command(const OpenIMPT41CommandParams *params,
                           const ExpectedWord *words, size_t word_count)
{
    uint32_t slot[OPENIMP_T41_CL_SLOT_SIZE / sizeof(uint32_t)];
    uint32_t expected[OPENIMP_T41_CL_SLOT_SIZE / sizeof(uint32_t)];
    size_t i;

    memset(slot, 0xa5, sizeof(slot));
    memset(expected, 0, sizeof(expected));
    for (i = 0; i < word_count; ++i)
        expected[words[i].index] = words[i].value;

    assert(openimp_t41_build_command(slot, sizeof(slot), params) == 0);
    for (i = 0; i < sizeof(slot) / sizeof(slot[0]); ++i) {
        if (slot[i] != expected[i]) {
            fprintf(stderr,
                    "command mismatch word %zu: got %08x expected %08x\n",
                    i, slot[i], expected[i]);
            assert(slot[i] == expected[i]);
        }
    }
}

static void test_main_idr_oracle(void)
{
    const OpenIMPT41CommandParams params = {
        .width = 1920, .height = 1080, .bitrate = 3000000,
        .fps_num = 25, .fps_den = 1,
        .min_qp = 34, .picture_qp = 34, .max_qp = 51,
        .rate_control_qp = 34,
        .picture_number = 0, .is_idr = 1,
        .source_y = 0x06b0ad00, .source_uv = 0x06d08d00,
        .reference_luma_offset = 0x000f0000,
        .reference_chroma_offset = 0x00078000,
        .reconstruction_y = 0x0634b200,
        .reconstruction_uv = 0x0655fa00,
        .reconstruction_map_luma = 0x06669e00,
        .reconstruction_map_chroma = 0x0666c000,
        .reconstruction_luma_offset = 0x000d9800,
        .reconstruction_chroma_offset = 0x0006cc00,
        .stream_buffer = 0x066fe700,
        .stream_part_offset = 0x000e7a80,
        .ep2 = 0x066f6680, .ep1 = 0x066f0000,
        .mv_current = 0x06670700, .ep3 = 0x06335d00,
    };
    static const ExpectedWord expected[] = {
        {0, 0x80700400}, {1, 0x008600ef}, {2, 0x00010006},
        {3, 0x40000d50}, {5, 0x00001fdf},
        {12, 0xffffffff}, {13, 0xffffffff},
        {24, 0x21220000}, {25, 0x00083f1f}, {27, 0x00043077},
        {29, 0x00000c80}, {32, 0x80043077},
        {34, 0x00077000}, {40, 0x06b0ad00}, {42, 0x06d08d00},
        {52, 0x00000780}, {53, 0x00000780},
        {108, 0x000f0000}, {109, 0x00078000},
        {110, 0x00214800}, {111, 0x0010a400},
        {112, 0x0634b200}, {114, 0x0655fa00},
        {118, 0x06669e00}, {120, 0x0666c000},
        {124, 0x000d9800}, {125, 0x0006cc00},
        {126, 0x00214800}, {127, 0x0010a400},
        {128, 0x02001e00}, {129, 0x02001e00}, {130, 0x00000200},
        {136, 0x066fe700}, {139, 0x000e7a80}, {140, 0x00000220},
        {141, 0x000e7860}, {152, 0x000000f6}, {153, 0x007fe7ff},
        {154, 0x066f6680}, {156, 0x066f0000}, {160, 0x06670700},
        {176, 0xf40001ce}, {177, 0x00000aea}, {178, 0x3f000075},
        {179, 0x22223322}, {180, 0xc3d00015}, {181, 0x00000001},
        {182, 0x06335d00},
        {512, 0x000a0c80}, {513, 0x21220d06},
        {515, 0x00077000}, {516, 0x00001fdf},
    };

    assert_command(&params, expected, sizeof(expected) / sizeof(expected[0]));
}

static void test_sub_first_p_oracle(void)
{
    const OpenIMPT41CommandParams params = {
        .width = 640, .height = 360, .bitrate = 1000000,
        .fps_num = 25, .fps_den = 1,
        .min_qp = 34, .picture_qp = 34, .max_qp = 51,
        .rate_control_qp = 38,
        .picture_number = 1, .is_idr = 0,
        .source_y = 0x0715b100, .source_uv = 0x07194900,
        .reference_y = 0x068ecd00, .reference_uv = 0x06930500,
        .reference_map_luma = 0x06952100,
        .reference_map_chroma = 0x06952d00,
        .reference_luma_offset = 0x0000f000,
        .reference_chroma_offset = 0x00007800,
        .reconstruction_y = 0x068ecd00,
        .reconstruction_uv = 0x06930500,
        .reconstruction_map_luma = 0x06953300,
        .reconstruction_map_chroma = 0x06953f00,
        .reconstruction_luma_offset = 0x00007800,
        .reconstruction_chroma_offset = 0x00003c00,
        .stream_buffer = 0x0699af00,
        .stream_part_offset = 0x00030b00,
        .ep2 = 0x06969280, .ep1 = 0x06962d00,
        .mv_previous = 0x06954600, .mv_current = 0x0695ba00,
        .ep3 = 0x068d6300,
    };
    static const ExpectedWord expected[] = {
        {0, 0x80700400}, {1, 0x002c004f}, {2, 0x00010006},
        {3, 0x40000d50}, {5, 0x00000397}, {8, 0x00000002},
        {12, 0xffffffff}, {13, 0xffffffff},
        {24, 0x11220000}, {25, 0x00083f1f}, {27, 0x00016427},
        {29, 0x00000c80}, {32, 0x80016027}, {33, 0x12000000},
        {34, 0x00027000}, {40, 0x0715b100}, {42, 0x07194900},
        {52, 0x00000280}, {53, 0x00000280},
        {66, 0x068ecd00}, {68, 0x06930500},
        {72, 0x06952100}, {74, 0x06952d00},
        {108, 0x0000f000}, {109, 0x00007800},
        {110, 0x00043800}, {111, 0x00021c00},
        {112, 0x068ecd00}, {114, 0x06930500},
        {118, 0x06953300}, {120, 0x06953f00},
        {124, 0x00007800}, {125, 0x00003c00},
        {126, 0x00043800}, {127, 0x00021c00},
        {128, 0x02000a00}, {129, 0x02000a00}, {130, 0x00000200},
        {136, 0x0699af00}, {139, 0x00030b00}, {140, 0x00000220},
        {141, 0x000308e0}, {152, 0x000000f6}, {153, 0x0000203b},
        {154, 0x06969280}, {156, 0x06962d00},
        {158, 0x06954600}, {160, 0x0695ba00},
        {176, 0xf4000107}, {177, 0x00000e67}, {178, 0x3f0000d9},
        {179, 0x22223326}, {180, 0xc210001c}, {181, 0x00000001},
        {182, 0x068d6300},
        {512, 0x000a0c80}, {513, 0x11220d06},
        {515, 0x00027000}, {516, 0x00000397},
    };

    assert_command(&params, expected, sizeof(expected) / sizeof(expected[0]));
}

static void test_builder_validation(void)
{
    uint32_t slot[OPENIMP_T41_CL_SLOT_SIZE / sizeof(uint32_t)];
    OpenIMPT41CommandParams params;

    memset(&params, 0, sizeof(params));
    assert(openimp_t41_build_command(slot, sizeof(slot), &params) == -1);
    assert(openimp_t41_build_command(NULL, sizeof(slot), &params) == -1);
    assert(openimp_t41_build_command(slot, sizeof(slot) - 1u, &params) == -1);
    assert(openimp_t41_build_command((uint8_t *)slot + 1u,
                                    sizeof(slot) - 1u, &params) == -1);

    assert(openimp_t41_reconstruction_pitch(1920) == 0x1e00u);
    assert(openimp_t41_reconstruction_luma_size(1920, 1080) == 0x214800u);
    assert(openimp_t41_reconstruction_chroma_size(1920, 1080) == 0x10a400u);
    assert(openimp_t41_reconstruction_pitch(640) == 0x0a00u);
    assert(openimp_t41_reconstruction_luma_size(640, 360) == 0x43800u);
    assert(openimp_t41_reconstruction_chroma_size(640, 360) == 0x21c00u);
    assert(openimp_t41_reconstruction_map_luma_size(1920, 1080) == 0x2200u);
    assert(openimp_t41_reconstruction_map_chroma_size(1920, 1080) == 0x1200u);
    assert(openimp_t41_reconstruction_map_slot_size(1920, 1080) == 0x3400u);
    assert(openimp_t41_motion_vector_slot_size(1920, 1080) == 0x3fd00u);
    assert(openimp_t41_reconstruction_manager_size(1920, 1080) == 0x3a4f00u);
    assert(openimp_t41_reconstruction_map_luma_size(640, 360) == 0x0c00u);
    assert(openimp_t41_reconstruction_map_chroma_size(640, 360) == 0x0600u);
    assert(openimp_t41_reconstruction_map_slot_size(640, 360) == 0x1200u);
    assert(openimp_t41_motion_vector_slot_size(640, 360) == 0x7400u);
    assert(openimp_t41_reconstruction_manager_size(640, 360) == 0x76100u);
    assert(openimp_t41_hwrc_grid(1920, 1080) == 0xf40001ceu);
    assert(openimp_t41_hwrc_grid(640, 360) == 0xf4000107u);

    /* Wyze Cam v4 full-resolution path (2.5K QHD). */
    assert(openimp_t41_reconstruction_pitch(2560) == 0x2800u);
    assert(openimp_t41_reconstruction_luma_size(2560, 1440) == 0x3b6000u);
    assert(openimp_t41_reconstruction_chroma_size(2560, 1440) == 0x1db000u);
    assert(openimp_t41_reconstruction_map_luma_size(2560, 1440) == 0x2e00u);
    assert(openimp_t41_reconstruction_map_chroma_size(2560, 1440) == 0x1800u);
    assert(openimp_t41_reconstruction_map_slot_size(2560, 1440) == 0x4600u);
    assert(openimp_t41_motion_vector_slot_size(2560, 1440) == 0x70900u);
    assert(openimp_t41_reconstruction_manager_size(2560, 1440) == 0x67af00u);
    assert(openimp_t41_hwrc_grid(2560, 1440) == 0xf40003c9u);

    assert(openimp_t41_next_rate_control_qp(
               34, 34, 51, 86000, 3000000, 25, 1) == 38u);
    assert(openimp_t41_next_rate_control_qp(
               38, 34, 51, 45000, 3000000, 25, 1) == 40u);
    assert(openimp_t41_next_rate_control_qp(
               50, 34, 51, 70000, 3000000, 25, 1) == 51u);
    assert(openimp_t41_next_rate_control_qp(
               51, 34, 51, 2000, 3000000, 25, 1) == 47u);
    assert(openimp_t41_next_rate_control_qp(
               35, 34, 51, 2000, 3000000, 25, 1) == 34u);
    assert(openimp_t41_next_rate_control_qp(
               38, 34, 51, 0, 3000000, 25, 1) == 38u);
}

static void test_encoding_status_oracle(void)
{
    uint32_t slot[OPENIMP_T41_CL_SLOT_SIZE / sizeof(uint32_t)];
    uint8_t slice[OPENIMP_T41_SLICE_STATUS_SIZE];
    uint8_t *status = (uint8_t *)slot + OPENIMP_T41_CL_STATUS_OFFSET;
    uint32_t value;
    uint16_t half;
    size_t i;

    memset(slot, 0, sizeof(slot));
    memset(slice, 0xa5, sizeof(slice));
    ((uint32_t *)slot)[0x8d] = 0x1000u;
    ((uint32_t *)status)[0x00] = 0x0200u;
    for (i = 2u; i <= 8u; ++i)
        ((uint32_t *)status)[i] = 0x11000000u + (uint32_t)i;
    for (i = 10u; i <= 23u; ++i)
        ((uint32_t *)status)[i] = 0x22000000u + (uint32_t)i;
    ((uint32_t *)status)[9] = 0x56781234u;
    ((uint32_t *)status)[12] = 0x9abc5634u;

    assert(openimp_t41_command_extract_encoding_status(
               slot, sizeof(slot), slice, sizeof(slice)) == 0);
    assert(slice[2] == 0u);
    assert(slice[3] == 0xa5u);
    memcpy(&value, slice + 0x04, sizeof(value));
    assert(value == 0xa5a5a5a5u);
    memcpy(&value, slice + 0x14, sizeof(value));
    assert(value == 0x11000002u);
    memcpy(&value, slice + 0x30, sizeof(value));
    assert(value == 0x00001234u);
    memcpy(&value, slice + 0x10, sizeof(value));
    assert(value == 0x00005678u);
    memcpy(&value, slice + 0x34, sizeof(value));
    assert(value == 0x2200000au);
    memcpy(&value, slice + 0x38, sizeof(value));
    assert(value == 0x0200000bu);
    memcpy(&half, slice + 0x3e, sizeof(half));
    assert(half == 0x0034u);
    memcpy(&half, slice + 0x40, sizeof(half));
    assert(half == 0x0056u);
    memcpy(&half, slice + 0x42, sizeof(half));
    assert(half == 0x9abcu);
    memcpy(&value, slice + 0x6c, sizeof(value));
    assert(value == 0x22000017u);

    ((uint32_t *)status)[0x00] = 0x2000u;
    assert(openimp_t41_command_extract_encoding_status(
               slot, sizeof(slot), slice, sizeof(slice)) == 0);
    assert(slice[2] == 1u);
    ((uint32_t *)slot)[3] = 1u << 11;
    assert(openimp_t41_command_extract_encoding_status(
               slot, sizeof(slot), slice, sizeof(slice)) == 0);
    assert(slice[2] == 0u);

    ((uint32_t *)slot)[3] = 0u;
    ((uint32_t *)slot)[0x8d] = 0u;
    ((uint32_t *)status)[0x00] = 0x20u;
    assert(openimp_t41_command_extract_encoding_status(
               slot, sizeof(slot), slice, sizeof(slice)) == 0);
    assert(slice[2] == 0u);

    assert(openimp_t41_command_extract_encoding_status(
               NULL, sizeof(slot), slice, sizeof(slice)) == -1);
    assert(openimp_t41_command_extract_encoding_status(
               slot, sizeof(slot) - 1u, slice, sizeof(slice)) == -1);
    assert(openimp_t41_command_extract_encoding_status(
               slot, sizeof(slot), slice, sizeof(slice) - 1u) == -1);
}

static void test_rate_control_statistics_oracle(void)
{
    uint32_t slice[OPENIMP_T41_SLICE_STATUS_SIZE / sizeof(uint32_t)];
    uint32_t stats[OPENIMP_T41_RC_STATS_SIZE / sizeof(uint32_t)];
    uint16_t *slice_half = (uint16_t *)slice;
    uint16_t *stats_half = (uint16_t *)stats;

    memset(slice, 0xa5, sizeof(slice));
    memset(stats, 0, sizeof(stats));
    slice[0x04u / 4u] = 0x10000001u;
    slice[0x08u / 4u] = 0x10000002u;
    slice[0x0cu / 4u] = 0x10000003u;
    slice[0x1cu / 4u] = 0x10000004u;
    slice[0x20u / 4u] = 0x10000005u;
    slice[0x24u / 4u] = 0x10000006u;
    slice[0x28u / 4u] = 0x10000007u;
    slice[0x2cu / 4u] = 0x10000008u;
    slice[0x38u / 4u] = 0x10000009u;
    slice_half[0x3eu / 2u] = 0x1234u;
    slice_half[0x40u / 2u] = 0x5678u;

    assert(openimp_t41_slice_status_extract_rate_control(
               slice, sizeof(slice), stats, sizeof(stats)) == 0);
    assert(stats[0] == 0x10000001u);
    assert(stats[1] == 0x10000002u);
    assert(stats[2] == 0x10000003u);
    assert(stats[3] == 0x10000004u);
    assert(stats[4] == 0x10000005u);
    assert(stats[5] == 0x10000006u);
    assert(stats[6] == 0x10000007u);
    assert(stats[7] == 0x10000008u);
    assert(stats[8] == 0x10000009u);
    assert(stats_half[0x24u / 2u] == 0x1234u);
    assert(stats_half[0x26u / 2u] == 0x5678u);

    assert(openimp_t41_slice_status_extract_rate_control(
               NULL, sizeof(slice), stats, sizeof(stats)) == -1);
    assert(openimp_t41_slice_status_extract_rate_control(
               slice, sizeof(slice) - 1u, stats, sizeof(stats)) == -1);
    assert(openimp_t41_slice_status_extract_rate_control(
               slice, sizeof(slice), stats, sizeof(stats) - 1u) == -1);
}

static void test_entropy_status_oracle(void)
{
    uint32_t slot[OPENIMP_T41_CL_SLOT_SIZE / sizeof(uint32_t)];
    uint32_t slice[OPENIMP_T41_SLICE_STATUS_SIZE / sizeof(uint32_t)];
    uint8_t *slice_bytes = (uint8_t *)slice;
    uint16_t *slice_half = (uint16_t *)slice;

    memset(slot, 0, sizeof(slot));
    memset(slice, 0xa5, sizeof(slice));
    slot[OPENIMP_T41_CL_ENTROPY_STATUS_OFFSET / 4u] = 0x80000101u;
    slot[(OPENIMP_T41_CL_ENTROPY_STATUS_OFFSET + 4u) / 4u] = 0x11223344u;
    assert(openimp_t41_command_extract_entropy_status(
               slot, sizeof(slot), slice, sizeof(slice), 0x140u) == 0);
    assert(slice_bytes[0] == 1u);
    assert(slice_bytes[1] == 0u);
    assert(slice_bytes[2] == 0xa5u);
    assert(slice[0x08u / 4u] == 0x00000101u);
    assert(slice[0x0cu / 4u] == 0x11223344u);
    assert(openimp_t41_command_extract_entropy_status(
               slot, sizeof(slot), slice, sizeof(slice), 0x120u) == 0);
    assert(slice_bytes[1] == 1u);

    slot[3] = 1u << 11;
    ((uint8_t *)slot)[0x12u] = 8u;
    slot[OPENIMP_T41_CL_STATUS_OFFSET / 4u] = 0x80000100u;
    slot[(OPENIMP_T41_CL_STATUS_OFFSET + 4u) / 4u] = 0x55667788u;
    slice_half[0x42u / 2u] = 2u;
    assert(openimp_t41_command_extract_entropy_status(
               slot, sizeof(slot), slice, sizeof(slice), 0x140u) == 0);
    assert(slice_bytes[0] == 1u);
    assert(slice_bytes[1] == 0u);
    assert(slice[0x08u / 4u] == 0x00000100u);
    assert(slice[0x0cu / 4u] == 0x55667788u);
    assert(openimp_t41_command_extract_entropy_status(
               slot, sizeof(slot), slice, sizeof(slice), 0x120u) == 0);
    assert(slice_bytes[1] == 1u);

    assert(openimp_t41_command_extract_entropy_status(
               NULL, sizeof(slot), slice, sizeof(slice), 0x140u) == -1);
    assert(openimp_t41_command_extract_entropy_status(
               slot, sizeof(slot) - 1u, slice, sizeof(slice), 0x140u) == -1);
    assert(openimp_t41_command_extract_entropy_status(
               slot, sizeof(slot), slice, sizeof(slice) - 1u, 0x140u) == -1);
}

static void test_combined_rate_control_status_oracle(void)
{
    uint32_t slot[OPENIMP_T41_CL_SLOT_SIZE / sizeof(uint32_t)];
    uint32_t slice[OPENIMP_T41_SLICE_STATUS_SIZE / sizeof(uint32_t)];
    uint32_t stats[OPENIMP_T41_RC_STATS_SIZE / sizeof(uint32_t)];
    uint8_t *status = (uint8_t *)slot + OPENIMP_T41_CL_STATUS_OFFSET;
    uint32_t *entropy = slot + OPENIMP_T41_CL_ENTROPY_STATUS_OFFSET / 4u;
    uint16_t *stats_half = (uint16_t *)stats;

    memset(slot, 0, sizeof(slot));
    memset(slice, 0xa5, sizeof(slice));
    memset(stats, 0xa5, sizeof(stats));
    slot[0x234u / 4u] = 0x1000u;
    entropy[0] = 0x80000321u;
    entropy[1] = 0x10203040u;
    ((uint32_t *)status)[0x10u / 4u] = 0x11111111u;
    ((uint32_t *)status)[0x14u / 4u] = 0x22222222u;
    ((uint32_t *)status)[0x18u / 4u] = 0x33333333u;
    ((uint32_t *)status)[0x1cu / 4u] = 0x44444444u;
    ((uint32_t *)status)[0x20u / 4u] = 0x55555555u;
    ((uint32_t *)status)[0x2cu / 4u] = 0x06789abcu;
    ((uint32_t *)status)[0x30u / 4u] = 0x00002b26u;

    assert(openimp_t41_command_extract_rate_control_status(
               slot, sizeof(slot), 0x1000u,
               slice, sizeof(slice), stats, sizeof(stats)) == 0);
    assert(((uint8_t *)slice)[0] == 1u);
    assert(((uint8_t *)slice)[1] == 0u);
    assert(((uint8_t *)slice)[2] == 0u);
    assert(stats[0] == 0u);
    assert(stats[1] == 0x321u);
    assert(stats[2] == 0x10203040u);
    assert(stats[3] == 0x11111111u);
    assert(stats[4] == 0x22222222u);
    assert(stats[5] == 0x33333333u);
    assert(stats[6] == 0x44444444u);
    assert(stats[7] == 0x55555555u);
    assert(stats[8] == 0x06789abcu);
    assert(stats_half[0x24u / 2u] == 0x0026u);
    assert(stats_half[0x26u / 2u] == 0x002bu);

    assert(openimp_t41_command_extract_rate_control_status(
               NULL, sizeof(slot), 0x1000u,
               slice, sizeof(slice), stats, sizeof(stats)) == -1);
    assert(openimp_t41_command_extract_rate_control_status(
               slot, sizeof(slot) - 1u, 0x1000u,
               slice, sizeof(slice), stats, sizeof(stats)) == -1);
    assert(openimp_t41_command_extract_rate_control_status(
               slot, sizeof(slot), 0x1000u,
               slice, sizeof(slice) - 1u, stats, sizeof(stats)) == -1);
    assert(openimp_t41_command_extract_rate_control_status(
               slot, sizeof(slot), 0x1000u,
               slice, sizeof(slice), stats, sizeof(stats) - 1u) == -1);
}

static void test_hwrc_level_lifecycle(void)
{
    uint32_t ring[OPENIMP_T41_EP3_RING_SIZE / sizeof(uint32_t)];
    OpenIMPT41HWRCLevelState state;
    size_t p_level = (OPENIMP_T41_EP3_SLOT_STRIDE +
                      OPENIMP_T41_EP3_LEVEL_OFFSET) / sizeof(uint32_t);
    size_t idr_level = (2u * OPENIMP_T41_EP3_SLOT_STRIDE +
                        OPENIMP_T41_EP3_LEVEL_OFFSET) / sizeof(uint32_t);

    memset(ring, 0xa5, sizeof(ring));
    openimp_t41_hwrc_level_init(&state);
    assert(state.level == 0u);
    assert(openimp_t41_hwrc_level_set_buffer(
               &state, ring, sizeof(ring), 1u) == 0);
    assert(ring[p_level] == 0u);

    ring[idr_level] = 0x08ab0c89u;
    assert(openimp_t41_hwrc_level_update(
               &state, ring, sizeof(ring), 2u) == 0);
    assert(state.level == 0x08ab0c89u);
    assert(openimp_t41_hwrc_level_set_buffer(
               &state, ring, sizeof(ring), 1u) == 0);
    assert(ring[p_level] == 0x08ab0c89u);
    assert(ring[(OPENIMP_T41_EP3_SLOT_STRIDE +
                 OPENIMP_T41_EP3_HISTORY_OFFSET) / sizeof(uint32_t)] ==
           0xa5a5a5a5u);

    assert(openimp_t41_hwrc_level_update(
               NULL, ring, sizeof(ring), 1u) == -1);
    assert(openimp_t41_hwrc_level_update(
               &state, NULL, sizeof(ring), 1u) == -1);
    assert(openimp_t41_hwrc_level_update(
               &state, ring, sizeof(ring),
               OPENIMP_T41_EP3_SLOT_COUNT) == -1);
    assert(openimp_t41_hwrc_level_set_buffer(
               &state, ring, OPENIMP_T41_EP3_LEVEL_OFFSET, 0u) == -1);
}

static void test_hwrc_ring_initialization(void)
{
    uint32_t ring[OPENIMP_T41_EP3_RING_SIZE / sizeof(uint32_t)];
    size_t slot0 = 0u;
    size_t slot1 = OPENIMP_T41_EP3_SLOT_STRIDE / sizeof(uint32_t);
    size_t slot2 = 2u * slot1;
    size_t level = OPENIMP_T41_EP3_LEVEL_OFFSET / sizeof(uint32_t);

    memset(ring, 0xa5, sizeof(ring));
    assert(openimp_t41_hwrc_ring_init(
               ring, sizeof(ring), 3000000u) ==
           OPENIMP_T41_EP3_PER_CORE_SIZE *
               OPENIMP_T41_EP3_SLOT_COUNT);
    assert(ring[slot0] == 0x0023d763u);
    assert(ring[slot1] == 0x002e9803u);
    assert(ring[slot2] == 0x00413b38u);
    assert(ring[slot2 + 1u] == 0x0c090706u);
    assert(ring[slot2 + 11u * 8u] == 1u);
    assert(ring[slot2 + 11u * 8u + 1u] == 0xf8fafbfcu);
    assert(ring[slot2 + 0x200u / 4u] == 0x5bu);
    assert(ring[slot2 + 0x398u / 4u] == 0xb504f3u);
    assert(ring[slot0 + level] == 0u);
    assert(ring[slot1 + level] == 0u);
    assert(ring[slot2 + level] == 0u);
    assert(ring[OPENIMP_T41_EP3_RING_SIZE / sizeof(uint32_t) - 1u] == 0u);

    assert(openimp_t41_hwrc_ring_init(
               NULL, sizeof(ring), 3000000u) == 0u);
    assert(openimp_t41_hwrc_ring_init(
               ring, sizeof(ring) - 1u, 3000000u) == 0u);
}

int main(void)
{
    uint8_t slot[OPENIMP_T41_CL_SLOT_SIZE];
    uint32_t status_phys = 0;
    size_t i;
    size_t previous_end = 0;

    assert(openimp_t41_command_slot_is_valid(sizeof(slot)));
    assert(!openimp_t41_command_slot_is_valid(sizeof(slot) - 1u));
    assert(openimp_t41_command_status_ptr(slot, sizeof(slot)) ==
           slot + OPENIMP_T41_CL_STATUS_OFFSET);
    assert(openimp_t41_command_status_const_ptr(slot, sizeof(slot)) ==
           slot + OPENIMP_T41_CL_STATUS_OFFSET);
    assert(openimp_t41_command_status_ptr(NULL, sizeof(slot)) == NULL);

    assert(openimp_t41_command_status_phys(0x06337200u, &status_phys) == 0);
    assert(status_phys == 0x063377c0u);
    assert(openimp_t41_command_status_phys(UINT32_MAX, &status_phys) == -1);
    assert(openimp_t41_command_status_phys(0u, NULL) == -1);

    assert(openimp_t41_command_range_count == 17u);
    for (i = 0; i < openimp_t41_command_range_count; ++i) {
        size_t start = (size_t)openimp_t41_command_ranges[i].word_offset * 4u;
        size_t end = start +
            (size_t)openimp_t41_command_ranges[i].word_count * 4u;

        assert(openimp_t41_command_ranges[i].word_count != 0u);
        assert(start >= previous_end);
        assert(end <= OPENIMP_T41_CL_STATUS_OFFSET);
        previous_end = end;
    }
    assert(previous_end == 0x03b8u);
    assert(OPENIMP_T41_CL_STATUS_OFFSET + OPENIMP_T41_CL_STATUS_SIZE <=
           OPENIMP_T41_CL_ENTROPY_OFFSET);
    assert(OPENIMP_T41_CL_ENTROPY_OFFSET + OPENIMP_T41_CL_ENTROPY_SIZE <=
           OPENIMP_T41_CL_SLOT_SIZE);

    test_main_idr_oracle();
    test_sub_first_p_oracle();
    test_builder_validation();
    test_encoding_status_oracle();
    test_rate_control_statistics_oracle();
    test_entropy_status_oracle();
    test_combined_rate_control_status_oracle();
    test_hwrc_ring_initialization();
    test_hwrc_level_lifecycle();

    puts("T41 command layout and builder: OK");
    return 0;
}
