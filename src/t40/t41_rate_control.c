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

static int32_t openimp_t41_divide_signed(int32_t numerator,
                                         int32_t denominator)
{
    /* C99 division truncates toward zero, as does the OEM MIPS `div`. */
    return numerator / denominator;
}

int32_t openimp_t41_rate_control_window_target(
    const OpenIMPT41RateControlWindow *window)
{
    uint64_t product;
    uint32_t measured_time;
    uint32_t target_time;
    uint32_t measured_fraction;
    uint32_t target_fraction;

    if (!window || window->words[3] == 0u)
        return 0;

    measured_time = window->words[6];
    measured_fraction = window->words[7];
    target_time = window->words[8];
    product = (uint64_t)window->words[4] * window->words[9];
    target_fraction = (uint32_t)(product / window->words[3]);

    /* Exact OEM lI1i recovery.  When the low byte at +0x14 is clear the
     * controller first catches the measured clock up to its latency floor. */
    if (((const uint8_t *)&window->words[5])[0] == 0u) {
        uint32_t floor = target_time - window->words[1];

        if (measured_time < floor) {
            measured_time = floor;
            measured_fraction = target_fraction;
        }
    }

    if (target_time >= measured_time &&
        (target_time != measured_time ||
         (uint64_t)window->words[9] * window->words[4] >=
             (uint64_t)measured_fraction * window->words[3])) {
        int32_t fractional =
            (int32_t)(target_fraction - measured_fraction);
        uint32_t whole = (uint32_t)(((uint64_t)
            (target_time - measured_time) * window->words[4]) / 90000u);

        return openimp_t41_divide_signed(fractional, 90000) +
               (int32_t)whole;
    } else {
        int32_t fractional =
            (int32_t)(measured_fraction - target_fraction);
        uint32_t whole = (uint32_t)(((uint64_t)
            (measured_time - target_time) * window->words[4]) / 90000u);

        return openimp_t41_divide_signed(fractional, -90000) -
               (int32_t)whole;
    }
}

int openimp_t41_rate_control_predict_bits(
    uint32_t model_bits, uint16_t model_qp, uint16_t requested_qp,
    uint32_t scale, int32_t max_qp_steps, uint32_t *predicted_bits)
{
    uint32_t steps;
    uint32_t distance;
    uint64_t product;

    if (!predicted_bits || scale == 0u || max_qp_steps < 0)
        return -1;

    distance = model_qp < requested_qp
        ? (uint32_t)requested_qp - model_qp
        : (uint32_t)model_qp - requested_qp;
    steps = distance < (uint32_t)max_qp_steps
        ? distance : (uint32_t)max_qp_steps;

    while (steps-- != 0u) {
        if (model_qp < requested_qp) {
            product = (uint64_t)model_bits * 10000u;
            model_bits = (uint32_t)(product / scale);
        } else {
            product = (uint64_t)model_bits * scale;
            model_bits = (uint32_t)(product / 10000u);
        }
    }
    *predicted_bits = model_bits;
    return 0;
}

static uint32_t openimp_t41_rate_control_clamp_scale(
    uint32_t scale, uint32_t lower_scale, uint32_t upper_scale)
{
    if (scale < lower_scale)
        return lower_scale;
    if (scale > upper_scale)
        return upper_scale;
    return scale;
}

int openimp_t41_rate_control_update_model_scale(
    uint32_t previous_bits, int16_t previous_qp,
    uint32_t completed_bits, int16_t completed_qp,
    uint32_t current_scale, uint32_t lower_scale, uint32_t upper_scale,
    uint32_t *updated_scale)
{
    uint64_t ratio;
    uint64_t product;
    uint32_t ratio_base;
    uint32_t distance;
    uint32_t scale;

    if (!updated_scale || previous_bits == 0u || completed_bits == 0u ||
        previous_qp < 0 || completed_qp < 0 ||
        current_scale == 0u || lower_scale == 0u ||
        lower_scale > upper_scale)
        return -1;

    if (previous_qp == completed_qp) {
        *updated_scale = current_scale;
        return 0;
    }

    if (completed_bits < previous_bits && previous_qp < completed_qp) {
        distance = (uint32_t)completed_qp - (uint32_t)previous_qp;
        product = (uint64_t)previous_bits * 10000u;
        ratio_base = (uint32_t)(product / completed_bits);
    } else if (previous_bits < completed_bits &&
               completed_qp < previous_qp) {
        distance = (uint32_t)previous_qp - (uint32_t)completed_qp;
        product = (uint64_t)completed_bits * 10000u;
        ratio_base = (uint32_t)(product / previous_bits);
    } else {
        /* OEM uses a wrapping 32-bit add before the divide. */
        *updated_scale = (current_scale + lower_scale) / 2u;
        return 0;
    }
    ratio = (uint64_t)ratio_base * 10000u;
    scale = current_scale;

    for (;;) {
        uint32_t power = 10000u;
        uint32_t candidate;
        uint32_t quotient;
        uint32_t numerator;
        uint32_t step;

        /* Fixed-point scale^(distance - 1).  Each OEM __udivdi3 return is
         * consumed through v0, so retain its low 32 bits at every step. */
        for (step = 1u; step < distance; ++step) {
            product = (uint64_t)power * scale;
            power = (uint32_t)(product / 10000u);
        }
        if (power == 0u) {
            /* The OEM's failed-root clamp falls through with its upper
             * bound in a0, so a vanished fixed-point power selects it. */
            *updated_scale = upper_scale;
            return 0;
        }

        quotient = (uint32_t)(ratio / power);
        numerator = (distance - 1u) * scale + quotient;
        candidate = numerator / distance;

        /* `candidate - scale + 1 < 3` is the OEM's unsigned test for a
         * change in [-1, 1].  It also stops before accepting an out-of-bound
         * Newton iterate; that final value is clamped exactly once. */
        if (candidate - scale + 1u < 3u ||
            candidate >= upper_scale || candidate <= lower_scale) {
            *updated_scale = openimp_t41_rate_control_clamp_scale(
                candidate, lower_scale, upper_scale);
            return 0;
        }
        scale = candidate;
    }
}

int openimp_t41_rate_control_search_qp(
    uint32_t model_bits, int16_t model_qp, uint32_t target_bits,
    uint32_t scale, int16_t min_qp, int16_t max_qp, int16_t *selected_qp)
{
    uint64_t product;

    if (!selected_qp || scale == 0u || min_qp > max_qp ||
        model_qp < min_qp || model_qp > max_qp)
        return -1;

    if (target_bits >= model_bits) {
        while (model_qp > min_qp && model_bits < target_bits) {
            --model_qp;
            product = (uint64_t)model_bits * scale;
            model_bits = (uint32_t)(product / 10000u);
        }
    } else {
        while (model_qp < max_qp && target_bits < model_bits) {
            ++model_qp;
            product = (uint64_t)model_bits * 10000u;
            model_bits = (uint32_t)(product / scale);
        }
    }
    *selected_qp = model_qp;
    return 0;
}

int openimp_t41_rate_control_predict_model_set(
    const OpenIMPT41RateControlModelSet *models, int32_t qp_delta,
    uint32_t prediction_cap, uint32_t predictions[3])
{
    size_t index;

    if (!models || !predictions || models->max_qp_steps < 0)
        return -1;

    for (index = 0u; index < 3u; ++index) {
        int64_t requested = (int64_t)models->current_qp + qp_delta;
        uint32_t predicted;

        if (index == 0u)
            requested += models->first_model_qp_bias;
        if (requested < 0)
            requested = 0;
        if (requested > (int64_t)UINT16_MAX)
            requested = (int64_t)UINT16_MAX;
        if (openimp_t41_rate_control_predict_bits(
                models->models[index].bits, models->models[index].qp,
                (uint16_t)requested, models->models[index].scale,
                models->max_qp_steps, &predicted) != 0)
            return -1;
        predictions[index] = predicted < prediction_cap
            ? predicted : prediction_cap;
    }
    return 0;
}

int openimp_t41_rate_control_adjust_model(
    uint32_t *model_bits, uint32_t *previous_bound_distance,
    int16_t current_qp, int16_t min_qp, int16_t max_qp,
    uint32_t scale, int32_t max_adjustment, uint32_t feedback_percent)
{
    int32_t distance_to_min;
    int32_t distance_to_max;
    int32_t bound_distance;
    int32_t permitted_adjustment;
    int32_t delta;
    uint64_t product;

    if (!model_bits || !previous_bound_distance || scale == 0u ||
        max_adjustment < 0 || max_adjustment > INT32_MAX / 4 ||
        *previous_bound_distance > INT32_MAX ||
        min_qp > current_qp || current_qp > max_qp)
        return -1;

    permitted_adjustment = max_adjustment;
    if (feedback_percent >= 81u) {
        permitted_adjustment = 0;
    } else if (feedback_percent < 61u) {
        if (feedback_percent >= 41u)
            permitted_adjustment *= 2;
        else if (feedback_percent >= 21u)
            permitted_adjustment *= 3;
        else
            permitted_adjustment *= 4;
    }

    distance_to_min = current_qp - min_qp;
    distance_to_max = max_qp - current_qp;
    bound_distance = distance_to_min < distance_to_max
        ? distance_to_min : distance_to_max;
    if (permitted_adjustment < bound_distance)
        bound_distance = permitted_adjustment;

    delta = bound_distance - (int32_t)*previous_bound_distance;
    while (delta < 0) {
        product = (uint64_t)*model_bits * 10000u;
        *model_bits = (uint32_t)(product / scale);
        ++delta;
    }
    while (delta > 0) {
        product = (uint64_t)*model_bits * scale;
        *model_bits = (uint32_t)(product / 10000u);
        --delta;
    }
    *previous_bound_distance = (uint32_t)bound_distance;
    return 0;
}

static int16_t openimp_t41_rate_control_clamp_qp_signed(
    int64_t qp, int16_t min_qp, int16_t max_qp)
{
    if (qp < min_qp)
        return min_qp;
    if (qp > max_qp)
        return max_qp;
    return (int16_t)qp;
}

int openimp_t41_rate_control_update_p_picture_model(
    OpenIMPT41RateControlPSelector *selector,
    OpenIMPT41RateControlPModelUpdater *updater,
    uint32_t completed_bits,
    const OpenIMPT41RateControlFeedback *feedback,
    int rotate_modes)
{
    OpenIMPT41RateControlPSelector next_selector;
    OpenIMPT41RateControlPModelUpdater next_updater;
    OpenIMPT41RateControlModel *p_model;
    uint32_t predicted;
    uint32_t model_bits;
    int16_t current_qp;
    int16_t requested_qp;

    if (!selector || !updater || !feedback || completed_bits == 0u ||
        selector->models.max_qp_steps < 0 ||
        selector->min_qp < 0 || selector->max_qp < 0 ||
        selector->models.current_qp < selector->min_qp ||
        selector->models.current_qp > selector->max_qp ||
        selector->models.models[0].qp > INT16_MAX ||
        selector->models.models[1].qp > INT16_MAX ||
        updater->feedback_model.qp > INT16_MAX ||
        updater->baseline_min_qp < 0 || updater->baseline_max_qp < 0 ||
        updater->feedback_min_qp < 0 || updater->feedback_max_qp < 0 ||
        updater->baseline_min_qp > updater->baseline_max_qp ||
        updater->feedback_min_qp > updater->feedback_max_qp ||
        updater->lower_scale == 0u ||
        updater->lower_scale > updater->upper_scale)
        return -1;

    next_selector = *selector;
    next_updater = *updater;
    current_qp = next_selector.models.current_qp;
    p_model = &next_selector.models.models[1];

    if (p_model->bits != 0u) {
        if (openimp_t41_rate_control_update_model_scale(
                p_model->bits, (int16_t)p_model->qp,
                completed_bits, current_qp, p_model->scale,
                next_updater.lower_scale, next_updater.upper_scale,
                &p_model->scale) != 0)
            return -1;
    }

    if (next_updater.feedback_model.bits != 0u) {
        requested_qp = openimp_t41_rate_control_clamp_qp_signed(
            (int64_t)current_qp -
                next_updater.feedback_model_bound_distance,
            next_updater.feedback_min_qp, next_updater.feedback_max_qp);
        if (openimp_t41_rate_control_predict_bits(
                next_updater.feedback_model.bits,
                next_updater.feedback_model.qp, (uint16_t)requested_qp,
                next_updater.feedback_model.scale,
                next_selector.models.max_qp_steps, &predicted) != 0)
            return -1;
        if (next_updater.modes[2] == 2u) {
            if (predicted / completed_bits >= 501u) {
                next_selector.adaptive_model_bits = 500000u;
            } else {
                next_selector.adaptive_model_bits = (uint32_t)(
                    (uint64_t)predicted * 1000u / completed_bits);
            }
        }
    }

    if (next_selector.models.models[0].bits != 0u) {
        const OpenIMPT41RateControlModel *baseline =
            &next_selector.models.models[0];

        requested_qp = openimp_t41_rate_control_clamp_qp_signed(
            (int64_t)current_qp +
                next_selector.models.first_model_qp_bias,
            next_updater.baseline_min_qp, next_updater.baseline_max_qp);
        if (openimp_t41_rate_control_predict_bits(
                baseline->bits, baseline->qp, (uint16_t)requested_qp,
                baseline->scale, next_selector.models.max_qp_steps,
                &predicted) != 0)
            return -1;
        if (predicted == 0u || completed_bits / predicted >= 501u) {
            next_updater.baseline_ratio = 2u;
        } else {
            next_updater.baseline_ratio = (uint32_t)(
                (uint64_t)predicted * 1000u / completed_bits);
        }
    }

    /* The OEM reuses byte +0x181 as selector hysteresis.  Its P updater
     * clears that byte when this picture-class scale reaches +0x1a0. */
    if (p_model->scale == next_updater.upper_scale)
        next_selector.negative_delta_latch = 0u;

    model_bits = completed_bits;
    if (next_updater.modes[0] == 1u &&
        ((int64_t)feedback->field_20_percent >
             (int64_t)next_updater.feedback_reference_percent + 60 ||
         (int64_t)feedback->field_20_percent <
             (int64_t)next_updater.feedback_reference_percent - 60 ||
         feedback->field_20_percent >= 99u)) {
        uint32_t sum = completed_bits +
                       next_updater.previous_completed_bits;
        int32_t signed_sum;

        /* `addu` deliberately wraps before the OEM's signed divide by two. */
        memcpy(&signed_sum, &sum, sizeof(signed_sum));
        model_bits = (uint32_t)(signed_sum / 2);
    }
    p_model->bits = model_bits;
    p_model->qp = (uint16_t)current_qp;

    if (feedback->field_1c_percent >= 81u) {
        next_updater.feedback_model.bits = completed_bits;
        next_updater.feedback_model.qp = (uint16_t)current_qp;
    }
    next_updater.previous_completed_bits = completed_bits;
    next_updater.cumulative_qp += (uint32_t)current_qp;
    ++next_updater.completed_p_pictures;

    if (rotate_modes) {
        uint32_t previous_mode = next_updater.modes[1];

        next_updater.modes[0] = 1u;
        next_updater.modes[1] = 1u;
        next_updater.modes[2] = previous_mode;
    }
    next_selector.residual_policy_mode = next_updater.modes[2];
    *selector = next_selector;
    *updater = next_updater;
    return 0;
}

static int openimp_t41_rate_control_residual(
    uint32_t pictures_remaining, uint32_t picture_bits,
    int32_t history_bits, uint32_t predicted_bits, int64_t *residual)
{
    uint64_t budget;
    uint64_t prediction;
    int64_t value;

    if (!residual || history_bits < 0 ||
        (pictures_remaining != 0u &&
         picture_bits > UINT64_MAX / pictures_remaining))
        return -1;
    budget = (uint64_t)pictures_remaining * picture_bits;
    prediction = (uint64_t)pictures_remaining * predicted_bits;
    if (budget > (uint64_t)INT64_MAX - (uint32_t)history_bits ||
        prediction > (uint64_t)INT64_MAX)
        return -1;
    value = (int64_t)budget + history_bits;
    *residual = value - (int64_t)prediction;
    return 0;
}

int openimp_t41_rate_control_select_p_picture(
    OpenIMPT41RateControlPSelector *selector, uint32_t completed_bits,
    uint32_t feedback_percent, OpenIMPT41RateControlSelection *selection)
{
    OpenIMPT41RateControlPSelector next;
    OpenIMPT41RateControlSelection result;
    uint64_t numerator;
    uint64_t quotient;
    uint64_t comparison;
    uint64_t refinement_threshold;
    int64_t target;
    int64_t residual;
    uint32_t denominator;
    uint32_t band;
    uint32_t scale;
    uint32_t quarter;
    uint32_t three_quarters;
    uint32_t max_positive_delta;
    int32_t candidate = 0;
    int skip_history_correction = 0;

    if (!selector || !selection || completed_bits == 0u ||
        selector->models.max_qp_steps <= 0 ||
        selector->models.max_qp_steps > INT16_MAX ||
        selector->min_qp > selector->models.current_qp ||
        selector->models.current_qp > selector->max_qp ||
        selector->gop_length < 2u || selector->pictures_remaining == 0u ||
        selector->pictures_remaining > selector->gop_length ||
        selector->prediction_cap_bits < selector->buffer_budget_bits ||
        selector->models.models[1].scale == 0u ||
        selector->gop_length > UINT32_MAX / 1000u ||
        selector->adaptive_model_bits >
            UINT32_MAX - (selector->gop_length - 1u) * 1000u)
        return -1;

    denominator = (selector->gop_length - 1u) * 1000u +
                  selector->adaptive_model_bits;
    if (denominator == 0u ||
        selector->allocation_budget_bits >
            UINT64_MAX / ((uint64_t)selector->gop_length * 1000u))
        return -1;
    numerator = (uint64_t)selector->gop_length * 1000u *
                selector->allocation_budget_bits;
    quotient = numerator / denominator;
    if (quotient > (uint64_t)INT64_MAX ||
        (selector->allocation_compensation_bits > 0 &&
         quotient > (uint64_t)INT64_MAX -
             (uint32_t)selector->allocation_compensation_bits))
        return -1;
    target = (int64_t)quotient +
             selector->allocation_compensation_bits;
    if (target < 1)
        target = 1;
    if (target > UINT32_MAX)
        return -1;

    memset(&result, 0, sizeof(result));
    result.picture_target_bits = (uint32_t)target;
    result.adjusted_completed_bits = completed_bits;
    if (openimp_t41_rate_control_predict_model_set(
            &selector->models, 0, selector->prediction_cap_bits,
            result.predictions_before) != 0 ||
        openimp_t41_rate_control_residual(
            selector->pictures_remaining,
            selector->residual_picture_bits,
            selector->history_target_bits, result.predictions_before[1],
            &residual) != 0)
        return -1;

    band = selector->threshold_span_bits / 10u;
    if (band > selector->prediction_cap_bits -
                   selector->buffer_budget_bits)
        band = selector->prediction_cap_bits -
               selector->buffer_budget_bits;
    scale = selector->models.models[1].scale;

    if (completed_bits < result.picture_target_bits &&
        residual > (int64_t)selector->buffer_budget_bits + band) {
        do {
            --candidate;
            result.adjusted_completed_bits = (uint32_t)(
                (uint64_t)result.adjusted_completed_bits * scale / 10000u);
            if (result.adjusted_completed_bits == 0u)
                return -1;
            comparison = (uint64_t)result.picture_target_bits * 10000u /
                         result.adjusted_completed_bits;
            if (scale >= comparison)
                break;
        } while (candidate > -selector->models.max_qp_steps);
    } else if (residual <
               (int64_t)selector->buffer_budget_bits -
                   (band < selector->buffer_budget_bits
                        ? band : selector->buffer_budget_bits)) {
        if (result.picture_target_bits < completed_bits) {
            do {
                ++candidate;
                result.adjusted_completed_bits = (uint32_t)(
                    (uint64_t)result.adjusted_completed_bits * 10000u /
                    scale);
                comparison =
                    (uint64_t)result.adjusted_completed_bits * 10000u /
                    result.picture_target_bits;
                if (scale >= comparison)
                    break;
            } while (candidate < selector->models.max_qp_steps);
        } else {
            candidate = 1;
        }
    }

    if (openimp_t41_rate_control_predict_model_set(
            &selector->models, candidate,
            selector->prediction_cap_bits,
            result.predictions_after) != 0 ||
        openimp_t41_rate_control_residual(
            selector->pictures_remaining,
            selector->residual_picture_bits,
            selector->history_target_bits, result.predictions_after[1],
            &residual) != 0)
        return -1;

    quarter = selector->prediction_cap_bits / 4u;
    three_quarters = quarter * 3u;
    refinement_threshold = quarter +
        (uint64_t)result.predictions_after[1] *
            selector->adaptive_model_bits / 1000u;
    if (residual < 0 || (uint64_t)residual < refinement_threshold) {
        if (candidate <= 0) {
            ++candidate;
            skip_history_correction = 1;
        } else if (residual > three_quarters) {
            --candidate;
            skip_history_correction = 1;
        }
    } else if (residual > three_quarters && candidate >= 0) {
        --candidate;
        skip_history_correction = 1;
    }

    if (!skip_history_correction) {
        uint32_t sixth = selector->prediction_cap_bits / 6u;

        if ((int64_t)selector->history_target_bits < sixth)
            ++candidate;
        else if ((int64_t)sixth * 5 < selector->history_target_bits)
            --candidate;
    }

    next = *selector;
    if (next.gop_length >= 3u) {
        if (candidate < 0) {
            next.negative_delta_latch = 1u;
        } else if (next.negative_delta_latch != 0u) {
            next.negative_delta_latch = 0u;
            candidate = feedback_percent < 30u && residual > 0 ? 0 : 1;
        }
        if (next.residual_policy_mode == 2u && residual > 0)
            candidate = 0;
    }

    max_positive_delta = (uint32_t)next.models.max_qp_steps;
    if (feedback_percent >= 86u && next.low_feedback_latch != 0u) {
        if ((int64_t)next.history_target_bits >=
            next.buffer_budget_bits / 2u) {
            if ((int64_t)next.history_target_bits >=
                (int64_t)next.buffer_budget_bits * 3 / 4) {
                max_positive_delta = candidate <= 0 ? 0u : 1u;
            } else {
                max_positive_delta /= 2u;
            }
        }
        next.low_feedback_latch = 0u;
    } else if (feedback_percent < 30u) {
        next.low_feedback_latch = 1u;
    }

    /* This asymmetry is intentional: the OEM block uses its dynamic positive
     * limit only to select whether the negative clamp executes.  Positive
     * candidates were already bounded by the search above. */
    if (candidate <= (int32_t)max_positive_delta &&
        candidate < -next.models.max_qp_steps)
        candidate = -next.models.max_qp_steps;

    target = (int64_t)next.models.current_qp + candidate;
    if (target < next.min_qp)
        target = next.min_qp;
    if (target > next.max_qp)
        target = next.max_qp;
    next.models.current_qp = (int16_t)target;
    result.residual_bits = residual;
    result.qp_delta = candidate;
    result.selected_qp = (int16_t)target;
    *selector = next;
    *selection = result;
    return 0;
}

int openimp_t41_rate_control_complete_p_picture(
    OpenIMPT41RateControlWindow *window,
    OpenIMPT41RateControlPSelector *selector,
    OpenIMPT41RateControlPModelUpdater *updater,
    uint32_t completed_bits,
    const OpenIMPT41RateControlFeedback *feedback,
    int adjust_feedback_model,
    int rotate_modes,
    OpenIMPT41RateControlSelection *selection)
{
    OpenIMPT41RateControlWindow next_window;
    OpenIMPT41RateControlPSelector next_selector;
    OpenIMPT41RateControlPModelUpdater next_updater;
    OpenIMPT41RateControlSelection next_selection;

    if (!window || !selector || !updater || !feedback || !selection ||
        completed_bits == 0u || selector->gop_length < 2u ||
        (adjust_feedback_model &&
         updater->max_model_adjustment > INT32_MAX))
        return -1;

    next_window = *window;
    next_selector = *selector;
    next_updater = *updater;
    if (openimp_t41_rate_control_update_p_picture_model(
            &next_selector, &next_updater, completed_bits, feedback,
            rotate_modes) != 0)
        return -1;

    if (adjust_feedback_model &&
        openimp_t41_rate_control_adjust_model(
            &next_selector.adaptive_model_bits,
            &next_updater.feedback_model_bound_distance,
            next_selector.models.current_qp,
            next_updater.feedback_min_qp,
            next_updater.feedback_max_qp,
            next_updater.feedback_model.scale,
            (int32_t)next_updater.max_model_adjustment,
            feedback->field_1c_percent) != 0)
        return -1;

    if (openimp_t41_rate_control_window_update(
            &next_window, completed_bits) != 0)
        return -1;
    ++next_updater.gop_picture_count;
    next_selector.pictures_remaining =
        next_updater.gop_picture_count < next_selector.gop_length
            ? next_selector.gop_length - next_updater.gop_picture_count
            : 1u;
    next_selector.history_target_bits =
        openimp_t41_rate_control_window_target(&next_window);

    if (openimp_t41_rate_control_select_p_picture(
            &next_selector, completed_bits, feedback->field_1c_percent,
            &next_selection) != 0)
        return -1;
    if (feedback->field_1c_percent < 81u)
        next_updater.last_picture_target_bits =
            next_selection.picture_target_bits;
    next_updater.feedback_reference_percent =
        feedback->field_20_percent;

    *window = next_window;
    *selector = next_selector;
    *updater = next_updater;
    *selection = next_selection;
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
