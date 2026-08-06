#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "t40/t41_rate_control.h"

static void write_u32(uint8_t *base, size_t offset, uint32_t value)
{
    memcpy(base + offset, &value, sizeof(value));
}

static void assert_feedback(const uint8_t *status, uint32_t completed_bits,
                            uint32_t block_count, uint32_t field_20_percent,
                            uint32_t field_1c_percent,
                            uint32_t field_14_bit_percent,
                            uint32_t field_18_quarters_per_block)
{
    OpenIMPT41RateControlFeedback feedback;

    memset(&feedback, 0xa5, sizeof(feedback));
    assert(openimp_t41_rate_control_extract_feedback(
               status, 0x70u, completed_bits, &feedback) == 0);
    assert(feedback.block_count == block_count);
    assert(feedback.field_20_percent == field_20_percent);
    assert(feedback.field_1c_percent == field_1c_percent);
    assert(feedback.field_14_bit_percent == field_14_bit_percent);
    assert(feedback.field_18_quarters_per_block ==
           field_18_quarters_per_block);
}

static void test_oem_main_idr_oracle(void)
{
    uint8_t status[0x70u] = { 0 };

    /* First 1920x1080 completion from an interposed OEM o1II call. */
    write_u32(status, 0x14u, 0x000ce9d1u);
    write_u32(status, 0x18u, 0x00025e40u);
    write_u32(status, 0x1cu, 0x00007f80u);
    write_u32(status, 0x20u, 0x00000000u);
    write_u32(status, 0x24u, 0x00003a2cu);
    write_u32(status, 0x28u, 0x00001155u);
    assert_feedback(status, 941192u, 32640u, 0u, 100u, 89u, 118u);
}

static void test_oem_main_p_oracle(void)
{
    uint8_t status[0x70u] = { 0 };

    /* Second 1920x1080 completion exercises nonzero +0x20 feedback. */
    write_u32(status, 0x14u, 0x0000cccbu);
    write_u32(status, 0x18u, 0x0000555cu);
    write_u32(status, 0x1cu, 0x000014c0u);
    write_u32(status, 0x20u, 0x00003e8cu);
    write_u32(status, 0x24u, 0x000013ccu);
    write_u32(status, 0x28u, 0x00001aedu);
    assert_feedback(status, 82584u, 32640u, 49u, 16u, 63u, 16u);
}

static void test_oem_subchannel_oracle(void)
{
    uint8_t status[0x70u] = { 0 };

    write_u32(status, 0x14u, 0x000005a0u);
    write_u32(status, 0x18u, 0x00000330u);
    write_u32(status, 0x1cu, 0x00000244u);
    write_u32(status, 0x20u, 0x00000af4u);
    write_u32(status, 0x24u, 0x0000020cu);
    write_u32(status, 0x28u, 0x00000315u);
    assert_feedback(status, 4064u, 3680u, 76u, 15u, 35u, 5u);
}

static void test_empty_and_bounds(void)
{
    uint8_t status[0x70u] = { 0 };
    OpenIMPT41RateControlFeedback feedback;

    assert_feedback(status, 0u, 1u, 0u, 0u, 0u, 0u);
    assert(openimp_t41_rate_control_extract_feedback(
               NULL, sizeof(status), 1u, &feedback) == -1);
    assert(openimp_t41_rate_control_extract_feedback(
               status, OPENIMP_T41_RATE_CONTROL_INPUT_SIZE - 1u,
               1u, &feedback) == -1);
    assert(openimp_t41_rate_control_extract_feedback(
               status, sizeof(status), 1u, NULL) == -1);
}

static void test_oem_main_window_oracle(void)
{
    static const uint32_t remaining_bits[] = {
        82584u, 183768u, 365224u, 57064u, 45440u, 217912u,
        64136u, 63432u, 340776u, 76120u, 78632u, 462304u,
        86912u, 61096u, 341384u, 54120u, 34080u, 364408u,
    };
    static const OpenIMPT41RateControlWindow expected = { {
        0x00895440u, 0x00034bc0u, 0x000003e8u, 0x000061a8u,
        0x002dc6c0u, 0x00000101u, 0x0001eb42u, 0x001d4c00u,
        0x00046500u, 0u, 0u, 0u, 0x003ff758u, 0u, 0x14u, 0u,
    } };
    OpenIMPT41RateControlWindow window = { {
        0x00895440u, 0x00034bc0u, 0x000003e8u, 0x000061a8u,
        0x002dc6c0u, 0x00000101u, 0u, 0u,
        0x00034bc0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    } };

    assert(openimp_t41_rate_control_window_update(&window, 941192u) == 0);
    assert(window.words[6] == 0x00006e4bu);
    assert(window.words[7] == 0x0022ca40u);
    assert(window.words[8] == 0x000359d0u);
    assert(window.words[9] == 0u);
    assert(window.words[10] == 0u);
    assert(window.words[12] == 0x000e5c88u);
    assert(window.words[13] == 0u);
    assert(window.words[14] == 1u);

    assert(openimp_t41_rate_control_window_update(&window, 271504u) == 0);
    assert(window.words[6] == 0x00008e1cu);
    assert(window.words[7] == 0x00284880u);
    assert(window.words[8] == 0x000367e0u);
    assert(window.words[9] == 0u);
    assert(window.words[12] == 0x00128118u);
    assert(window.words[14] == 2u);

    for (size_t i = 0u;
         i < sizeof(remaining_bits) / sizeof(remaining_bits[0]); ++i)
        assert(openimp_t41_rate_control_window_update(
                   &window, remaining_bits[i]) == 0);
    assert(memcmp(&window, &expected, sizeof(window)) == 0);
}

static void test_oem_subchannel_window_oracle(void)
{
    static const uint32_t remaining_bits[] = {
        15704u, 4064u, 13856u, 63392u, 5112u, 4408u,
        22696u, 5120u, 2632u, 29600u, 4904u, 2240u,
        32128u, 5840u, 2816u, 33656u, 4480u, 1208u, 41376u,
    };
    static const OpenIMPT41RateControlWindow expected = { {
        0x002dc6c0u, 0x00034bc0u, 0x000003e8u, 0x000061a8u,
        0x000f4240u, 0x00000101u, 0x0000755au, 0u,
        0x00046500u, 0u, 0u, 0u, 0x000517e8u, 0u, 0x14u, 0u,
    } };
    OpenIMPT41RateControlWindow window = { {
        0x002dc6c0u, 0x00034bc0u, 0x000003e8u, 0x000061a8u,
        0x000f4240u, 0x00000101u, 0u, 0u,
        0x00034bc0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    } };

    assert(openimp_t41_rate_control_window_update(&window, 38568u) == 0);
    assert(window.words[6] == 0x00000d8fu);
    assert(window.words[7] == 0x0001d4c0u);
    assert(window.words[8] == 0x000359d0u);
    assert(window.words[9] == 0u);
    assert(window.words[12] == 0x000096a8u);
    assert(window.words[13] == 0u);
    assert(window.words[14] == 1u);

    for (size_t i = 0u;
         i < sizeof(remaining_bits) / sizeof(remaining_bits[0]); ++i)
        assert(openimp_t41_rate_control_window_update(
                   &window, remaining_bits[i]) == 0);
    assert(memcmp(&window, &expected, sizeof(window)) == 0);
}

static void test_window_bounds(void)
{
    OpenIMPT41RateControlWindow window = { { 0 } };
    OpenIMPT41RateControlWindow original;

    original = window;
    assert(openimp_t41_rate_control_window_update(NULL, 1u) == -1);
    assert(openimp_t41_rate_control_window_update(&window, 1u) == -1);
    assert(memcmp(&window, &original, sizeof(window)) == 0);
    window.words[3] = 1u;
    original = window;
    assert(openimp_t41_rate_control_window_update(&window, 1u) == -1);
    assert(memcmp(&window, &original, sizeof(window)) == 0);
}

int main(void)
{
    test_oem_main_idr_oracle();
    test_oem_main_p_oracle();
    test_oem_subchannel_oracle();
    test_empty_and_bounds();
    test_oem_main_window_oracle();
    test_oem_subchannel_window_oracle();
    test_window_bounds();
    puts("T41 software rate-control feedback: OK");
    return 0;
}
