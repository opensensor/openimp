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

int main(void)
{
    test_oem_main_idr_oracle();
    test_oem_main_p_oracle();
    test_oem_subchannel_oracle();
    test_empty_and_bounds();
    puts("T41 software rate-control feedback: OK");
    return 0;
}
