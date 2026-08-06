#include "t41_rate_control.h"

#include <string.h>

static uint32_t openimp_t41_rate_control_read_u32(const uint8_t *base,
                                                  size_t offset)
{
    uint32_t value;

    memcpy(&value, base + offset, sizeof(value));
    return value;
}

int openimp_t41_rate_control_extract_feedback(
    const void *slice_status, size_t slice_status_size,
    uint32_t completed_bits, OpenIMPT41RateControlFeedback *feedback)
{
    const uint8_t *status = (const uint8_t *)slice_status;
    uint32_t block_count;
    uint32_t bit_denominator;

    if (!status || !feedback ||
        slice_status_size < OPENIMP_T41_RATE_CONTROL_INPUT_SIZE)
        return -1;

    /* Exact OEM oOIo normalization:
     *   count = status[0x24] + 4 * status[0x28]
     *         + 16 * status[0x2c] + 64 * status[0x30]
     * A main-channel frame reports 32640 here (240 by 136 8x8 blocks),
     * while the 640x360 channel reports 3680 (80 by 46 padded blocks). */
    block_count = openimp_t41_rate_control_read_u32(status, 0x24u);
    block_count += openimp_t41_rate_control_read_u32(status, 0x28u) << 2;
    block_count += openimp_t41_rate_control_read_u32(status, 0x2cu) << 4;
    block_count += openimp_t41_rate_control_read_u32(status, 0x30u) << 6;
    if (block_count == 0u)
        block_count = 1u;
    bit_denominator = completed_bits != 0u ? completed_bits : 1u;

    feedback->block_count = block_count;
    feedback->field_20_percent =
        openimp_t41_rate_control_read_u32(status, 0x20u) * 100u /
        block_count;
    feedback->field_1c_percent =
        openimp_t41_rate_control_read_u32(status, 0x1cu) * 100u /
        block_count;
    feedback->field_14_bit_percent =
        openimp_t41_rate_control_read_u32(status, 0x14u) * 100u /
        bit_denominator;
    feedback->field_18_quarters_per_block =
        openimp_t41_rate_control_read_u32(status, 0x18u) * 25u /
        block_count;
    return 0;
}
