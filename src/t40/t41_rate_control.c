#include "t41_rate_control.h"

#include <limits.h>
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

static uint32_t openimp_t41_clamp_qp(uint32_t qp, uint32_t min_qp,
                                     uint32_t max_qp)
{
    if (qp < min_qp)
        return min_qp;
    if (qp > max_qp)
        return max_qp;
    return qp;
}

int openimp_t41_rate_controller_init(OpenIMPT41RateController *controller,
                                     uint32_t bitrate, uint32_t fps_num,
                                     uint32_t fps_den, uint32_t gop_length,
                                     uint32_t min_qp, uint32_t max_qp,
                                     uint32_t initial_qp)
{
    uint64_t target_bits;
    uint64_t fps_milli;
    uint64_t history_rate;

    if (!controller || !bitrate || !fps_num || !fps_den ||
        min_qp > max_qp || max_qp > 51u)
        return -1;

    target_bits = ((uint64_t)bitrate * fps_den + fps_num / 2u) / fps_num;
    fps_milli = (uint64_t)fps_num * 1000u / fps_den;
    history_rate = (uint64_t)bitrate * 3u;
    if (!target_bits || target_bits > UINT32_MAX || !fps_milli ||
        fps_milli > UINT32_MAX || history_rate > UINT32_MAX)
        return -1;

    memset(controller, 0, sizeof(*controller));
    controller->bitrate = bitrate;
    controller->fps_num = fps_num;
    controller->fps_den = fps_den;
    controller->gop_length = gop_length ? gop_length : 1u;
    controller->target_bits = (uint32_t)target_bits;
    controller->min_qp = min_qp;
    controller->max_qp = max_qp;
    controller->current_qp =
        openimp_t41_clamp_qp(initial_qp, min_qp, max_qp);

    /* Exact oIIo defaults recovered from the T41 controller object. */
    controller->window.words[0] = (uint32_t)history_rate;
    controller->window.words[1] = 216000u;
    controller->window.words[2] = 1000u;
    controller->window.words[3] = (uint32_t)fps_milli;
    controller->window.words[4] = bitrate;
    controller->window.words[5] = 0x101u;
    controller->window.words[8] = 216000u;
    controller->initialized = 1;
    return 0;
}

int openimp_t41_rate_controller_complete(
    OpenIMPT41RateController *controller, uint32_t completed_bits,
    int is_idr, const OpenIMPT41RateControlFeedback *feedback)
{
    int64_t target;
    int64_t limit;

    if (!controller || !controller->initialized || !completed_bits)
        return -1;
    if (openimp_t41_rate_control_window_update(
            &controller->window, completed_bits) != 0)
        return -1;
    if (feedback)
        controller->feedback = *feedback;
    ++controller->completed_pictures;

    /* IDR has its own hardware table and GOP allocation.  Feeding its burst
     * into a per-P selector was the old source of four-QP jumps. */
    if (is_idr)
        return 0;

    ++controller->completed_p_pictures;
    if (controller->smoothed_p_bits == 0u) {
        controller->smoothed_p_bits = completed_bits;
    } else {
        controller->smoothed_p_bits =
            (uint32_t)(((uint64_t)controller->smoothed_p_bits * 3u +
                        completed_bits + 2u) / 4u);
    }

    target = (int64_t)controller->target_bits;
    controller->virtual_buffer_bits += (int64_t)completed_bits - target;
    limit = target * 8;
    if (controller->virtual_buffer_bits > limit)
        controller->virtual_buffer_bits = limit;
    if (controller->virtual_buffer_bits < -limit)
        controller->virtual_buffer_bits = -limit;

    /* Two completed P pictures establish a model.  Thereafter use a 12%
     * upper and 28% lower hysteresis band, with the recovered texture
     * feedback suppressing quality increases on highly detailed pictures. */
    if (controller->completed_p_pictures < 2u)
        return 0;
    if ((uint64_t)controller->smoothed_p_bits * 100u >
            (uint64_t)controller->target_bits * 112u ||
        controller->virtual_buffer_bits > target * 2) {
        if (controller->current_qp < controller->max_qp)
            ++controller->current_qp;
        controller->virtual_buffer_bits -= target;
    } else if ((uint64_t)controller->smoothed_p_bits * 100u <
                   (uint64_t)controller->target_bits * 72u &&
               controller->virtual_buffer_bits < -target * 2 &&
               (!feedback || feedback->field_1c_percent < 70u)) {
        if (controller->current_qp > controller->min_qp)
            --controller->current_qp;
        controller->virtual_buffer_bits += target;
    }
    return 0;
}

uint32_t openimp_t41_rate_controller_qp(
    const OpenIMPT41RateController *controller)
{
    return controller && controller->initialized
        ? controller->current_qp : 0u;
}
