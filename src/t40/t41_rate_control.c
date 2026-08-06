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

int openimp_t41_rate_control_window_update(
    OpenIMPT41RateControlWindow *window, uint32_t completed_bits)
{
    OpenIMPT41RateControlWindow next;
    uint64_t product;
    uint32_t accumulator_18;
    uint32_t accumulator_20;
    uint32_t remainder_1c;
    uint32_t remainder_24;
    uint32_t reduction_units = 0u;
    uint32_t threshold;
    uint32_t value;

    if (!window || window->words[3] == 0u || window->words[4] == 0u)
        return -1;
    next = *window;

    accumulator_18 = next.words[6];
    accumulator_20 = next.words[8];
    threshold = next.words[1];
    if ((((const uint8_t *)&next.words[5])[0]) == 0u) {
        value = accumulator_20 - threshold;
        if (accumulator_18 < value) {
            next.words[15] += value - accumulator_18;
            accumulator_18 = value;
        }
    }

    product = (uint64_t)completed_bits * 90000u;
    value = next.words[7] + (uint32_t)(product % next.words[4]);
    remainder_1c = value % next.words[4];
    accumulator_18 += value / next.words[4] +
                      (uint32_t)(product / next.words[4]);

    product = (uint64_t)next.words[2] * 90000u;
    value = next.words[9] + (uint32_t)(product % next.words[3]);
    remainder_24 = value % next.words[3];
    accumulator_20 += value / next.words[3] +
                      (uint32_t)(product / next.words[3]);

    if (threshold < accumulator_20 && threshold < accumulator_18) {
        uint32_t lower = accumulator_20 < accumulator_18
            ? accumulator_20 : accumulator_18;

        reduction_units = (lower - threshold) / 90000u;
        value = reduction_units * 90000u;
        accumulator_18 -= value;
        accumulator_20 -= value;
    }

    next.words[10] += reduction_units;
    next.words[6] = accumulator_18;
    next.words[8] = accumulator_20;
    ++next.words[14];
    value = next.words[12] + completed_bits;
    next.words[13] += value < next.words[12];
    next.words[12] = value;

    /* With flag +0x15 clear, OEM keeps the later of the two fractional
     * accumulators.  The cross-product preserves its exact tie-break without
     * introducing floating-point arithmetic. */
    if (((const uint8_t *)&next.words[5])[1] == 0u &&
        (accumulator_20 < accumulator_18 ||
         (accumulator_20 == accumulator_18 &&
          (uint64_t)next.words[4] * remainder_24 <
              (uint64_t)remainder_1c * next.words[3]))) {
        next.words[8] = accumulator_18;
        next.words[9] = remainder_1c;
    } else {
        next.words[7] = remainder_1c;
        next.words[9] = remainder_24;
    }

    *window = next;
    return 0;
}
