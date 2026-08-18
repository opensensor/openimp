#include "t31_rate_control.h"

#include <limits.h>
#include <string.h>

#define T31_QP_SCALE_UP_Q16   73562u
#define T31_QP_SCALE_DOWN_Q16 58386u

static uint32_t t31_clamp_qp(uint32_t qp, uint32_t min_qp, uint32_t max_qp)
{
    if (qp < min_qp)
        return min_qp;
    if (qp > max_qp)
        return max_qp;
    return qp;
}

/* H.264 quantizer step size doubles every six QP.  Apply one-QP fixed-point
 * steps so the controller can compare measurements made at different QPs
 * without floating point or a scene-specific lookup table. */
static uint32_t t31_scale_bits_for_qp(uint32_t bits, int qp_delta)
{
    uint64_t scaled = bits;

    while (qp_delta > 0) {
        scaled = (scaled * T31_QP_SCALE_DOWN_Q16 + 32768u) >> 16;
        --qp_delta;
    }
    while (qp_delta < 0) {
        scaled = (scaled * T31_QP_SCALE_UP_Q16 + 32768u) >> 16;
        if (scaled > UINT32_MAX)
            return UINT32_MAX;
        ++qp_delta;
    }
    return (uint32_t)scaled;
}

static int64_t t31_saturating_add_i64(int64_t left, int64_t right)
{
    if (right > 0 && left > INT64_MAX - right)
        return INT64_MAX;
    if (right < 0 && left < INT64_MIN - right)
        return INT64_MIN;
    return left + right;
}

static uint32_t t31_picture_budget(OpenIMPT31RateController *controller)
{
    uint64_t gop_budget;
    uint64_t idr_bits;
    uint64_t p_budget;
    uint64_t recovery_divisor;
    uint64_t correction;
    uint64_t minimum;
    uint64_t maximum;

    if (controller->gop_length <= 1u ||
        controller->smoothed_idr_bits == 0u)
        p_budget = controller->target_bits;
    else {
        gop_budget = (uint64_t)controller->target_bits *
                     controller->gop_length;
        idr_bits = controller->smoothed_idr_bits;
        if (idr_bits >= gop_budget)
            p_budget = controller->target_bits / 4u;
        else
            p_budget = (gop_budget - idr_bits) /
                       (controller->gop_length - 1u);
    }

    /* Recover accumulated CBR error over two GOPs.  This is deliberately
     * slower than scene-complexity tracking, preventing one large IDR from
     * forcing a visible multi-QP swing across the immediately following
     * pictures. */
    recovery_divisor = (uint64_t)controller->gop_length * 2u;
    if (recovery_divisor == 0u)
        recovery_divisor = 1u;
    if (controller->virtual_buffer_bits > 0) {
        correction = (uint64_t)controller->virtual_buffer_bits /
                     recovery_divisor;
        p_budget = correction < p_budget ? p_budget - correction : 0u;
    } else if (controller->virtual_buffer_bits < 0) {
        uint64_t debt = (uint64_t)(-(controller->virtual_buffer_bits + 1)) +
                        1u;

        correction = debt / recovery_divisor;
        p_budget += correction;
    }

    minimum = controller->target_bits / 4u;
    if (minimum == 0u)
        minimum = 1u;
    maximum = (uint64_t)controller->target_bits * 3u / 2u;
    if (maximum < minimum)
        maximum = minimum;
    if (p_budget < minimum)
        p_budget = minimum;
    if (p_budget > maximum)
        p_budget = maximum;
    return (uint32_t)p_budget;
}

static uint32_t t31_select_qp(OpenIMPT31RateController *controller,
                              uint32_t target_bits)
{
    uint32_t desired = controller->max_qp;
    uint32_t current_prediction;
    uint32_t qp;

    if (controller->model_p_bits == 0u)
        return controller->current_qp;

    current_prediction = t31_scale_bits_for_qp(
        controller->model_p_bits,
        (int)controller->current_qp - (int)controller->model_p_qp);

    /* Asymmetric hysteresis: protect the bitrate ceiling promptly, but keep
     * a little more headroom before lowering QP and spending extra bits. */
    if ((uint64_t)current_prediction * 100u >=
            (uint64_t)target_bits * 92u &&
        (uint64_t)current_prediction * 100u <=
            (uint64_t)target_bits * 108u)
        return controller->current_qp;

    for (qp = controller->min_qp; qp <= controller->max_qp; ++qp) {
        uint32_t prediction = t31_scale_bits_for_qp(
            controller->model_p_bits,
            (int)qp - (int)controller->model_p_qp);

        if (prediction <= target_bits) {
            desired = qp;
            break;
        }
    }

    /* Do not expose a scene cut as a single large quality step. */
    if (desired > controller->current_qp + 2u)
        desired = controller->current_qp + 2u;
    else if (controller->current_qp > desired + 2u)
        desired = controller->current_qp - 2u;
    return t31_clamp_qp(desired, controller->min_qp,
                        controller->max_qp);
}

int openimp_t31_rate_controller_init(OpenIMPT31RateController *controller,
                                     uint32_t bitrate, uint32_t fps_num,
                                     uint32_t fps_den, uint32_t gop_length,
                                     uint32_t min_qp, uint32_t max_qp,
                                     uint32_t initial_qp)
{
    uint64_t target_bits;

    if (!controller || bitrate == 0u || fps_num == 0u || fps_den == 0u ||
        gop_length == 0u || min_qp > max_qp || max_qp > 51u)
        return -1;

    target_bits = ((uint64_t)bitrate * fps_den + fps_num / 2u) /
                  fps_num;
    if (target_bits == 0u || target_bits > UINT32_MAX)
        return -1;

    memset(controller, 0, sizeof(*controller));
    controller->bitrate = bitrate;
    controller->fps_num = fps_num;
    controller->fps_den = fps_den;
    controller->gop_length = gop_length;
    controller->target_bits = (uint32_t)target_bits;
    controller->min_qp = min_qp;
    controller->max_qp = max_qp;
    controller->current_qp = t31_clamp_qp(initial_qp, min_qp, max_qp);
    controller->picture_target_bits = controller->target_bits;
    controller->initialized = 1;
    return 0;
}

int openimp_t31_rate_controller_complete(
    OpenIMPT31RateController *controller, uint32_t completed_bits,
    uint32_t used_qp, int is_idr)
{
    int64_t error;
    int64_t limit;
    uint64_t limit_u64;

    if (!controller || !controller->initialized || completed_bits == 0u ||
        used_qp > 51u)
        return -1;

    error = (int64_t)completed_bits - controller->target_bits;
    controller->virtual_buffer_bits = t31_saturating_add_i64(
        controller->virtual_buffer_bits, error);
    limit_u64 = (uint64_t)controller->target_bits *
                controller->gop_length * 8u;
    limit = limit_u64 > (uint64_t)INT64_MAX
        ? INT64_MAX : (int64_t)limit_u64;
    if (controller->virtual_buffer_bits > limit)
        controller->virtual_buffer_bits = limit;
    if (controller->virtual_buffer_bits < -limit)
        controller->virtual_buffer_bits = -limit;

    ++controller->completed_pictures;
    if (is_idr) {
        if (controller->smoothed_idr_bits == 0u)
            controller->smoothed_idr_bits = completed_bits;
        else
            controller->smoothed_idr_bits = (uint32_t)(
                ((uint64_t)controller->smoothed_idr_bits * 3u +
                 completed_bits + 2u) / 4u);
    } else {
        uint32_t predicted_at_used;

        if (controller->model_p_bits == 0u) {
            controller->model_p_bits = completed_bits;
        } else {
            predicted_at_used = t31_scale_bits_for_qp(
                controller->model_p_bits,
                (int)used_qp - (int)controller->model_p_qp);
            controller->model_p_bits = (uint32_t)(
                ((uint64_t)predicted_at_used * 3u + completed_bits + 2u) /
                4u);
        }
        controller->model_p_qp = used_qp;
        controller->smoothed_p_bits = controller->model_p_bits;
        ++controller->completed_p_pictures;
    }

    controller->picture_target_bits = t31_picture_budget(controller);
    controller->current_qp = t31_select_qp(
        controller, controller->picture_target_bits);
    return 0;
}

uint32_t openimp_t31_rate_controller_qp(
    const OpenIMPT31RateController *controller)
{
    return controller && controller->initialized
        ? controller->current_qp : 0u;
}
