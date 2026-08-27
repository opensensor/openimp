#include "t31_rate_control.h"

#include <limits.h>
#include <string.h>

#define T31_QP_SCALE_UP_Q16   73562u
#define T31_QP_SCALE_DOWN_Q16 58386u
#define T31_MODEL_REFERENCE_QP 30u
#define T31_GOP_EMA_OLD_WEIGHT 15u
#define T31_LOWER_QP_PERCENT   80u
#define T31_RAISE_QP_PERCENT   110u
#define T31_OVER_TARGET_GOPS   3u
#define T31_UNDER_TARGET_GOPS  6u

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

static uint32_t t31_select_model_qp(uint32_t model_bits,
                                    uint32_t target_bits,
                                    uint32_t min_qp, uint32_t max_qp)
{
    uint32_t qp;

    for (qp = min_qp; qp <= max_qp; ++qp) {
        uint32_t prediction = t31_scale_bits_for_qp(
            model_bits, (int)qp - (int)T31_MODEL_REFERENCE_QP);

        /* One-QP fixed-point scaling is not perfectly reciprocal; accept a
         * 0.1% round-trip tolerance so an exact target does not gain a QP. */
        if ((uint64_t)prediction * 1000u <=
            (uint64_t)target_bits * 1001u)
            return qp;
    }
    return max_qp;
}

static int64_t t31_saturating_add_i64(int64_t left, int64_t right)
{
    if (right > 0 && left > INT64_MAX - right)
        return INT64_MAX;
    if (right < 0 && left < INT64_MIN - right)
        return INT64_MIN;
    return left + right;
}

static uint64_t t31_saturating_add_u64(uint64_t left, uint64_t right)
{
    return UINT64_MAX - left < right ? UINT64_MAX : left + right;
}

static uint32_t t31_select_recovery_qp(OpenIMPT31RateController *controller)
{
    uint32_t desired = controller->current_qp;
    uint32_t current_prediction;
    uint32_t qp;

    if (controller->smoothed_gop_model_bits == 0u)
        return controller->current_qp;

    current_prediction = t31_scale_bits_for_qp(
        controller->smoothed_gop_model_bits,
        (int)controller->current_qp - (int)T31_MODEL_REFERENCE_QP);

    /* Spending extra bits is harmless to picture quality, so let the
     * persistence counter below decide when overload really requires a
     * higher QP.  This smoothed path only recovers quality after sustained
     * headroom. */
    if ((uint64_t)current_prediction * 100u >=
        (uint64_t)controller->target_bits * T31_LOWER_QP_PERCENT)
        return controller->current_qp;

    for (qp = controller->min_qp; qp <= controller->max_qp; ++qp) {
        uint32_t prediction = t31_scale_bits_for_qp(
            controller->smoothed_gop_model_bits,
            (int)qp - (int)T31_MODEL_REFERENCE_QP);

        if (prediction <= controller->target_bits) {
            desired = qp;
            break;
        }
    }

    if (desired < controller->current_qp)
        desired = controller->current_qp - 1u;
    return t31_clamp_qp(desired, controller->min_qp,
                        controller->max_qp);
}

static void t31_complete_gop(OpenIMPT31RateController *controller)
{
    uint64_t average;
    uint32_t measured_prediction;
    uint32_t previous_qp;

    if (controller->gop_pictures == 0u)
        return;

    average = controller->gop_model_bits / controller->gop_pictures;
    if (average > UINT32_MAX)
        average = UINT32_MAX;
    measured_prediction = t31_scale_bits_for_qp(
        (uint32_t)average,
        (int)controller->current_qp - (int)T31_MODEL_REFERENCE_QP);
    if ((uint64_t)measured_prediction * 100u >
        (uint64_t)controller->target_bits * T31_RAISE_QP_PERCENT) {
        ++controller->over_target_gops;
        controller->under_target_gops = 0u;
    } else if ((uint64_t)measured_prediction * 100u <
               (uint64_t)controller->target_bits *
                   T31_LOWER_QP_PERCENT) {
        ++controller->under_target_gops;
        controller->over_target_gops = 0u;
    } else {
        controller->over_target_gops = 0u;
        controller->under_target_gops = 0u;
    }
    previous_qp = controller->current_qp;
    if (controller->completed_gops == 0u) {
        /*
         * The configured initial QP is only a safe way to obtain the first
         * GOP.  Use that GOP's normalized measurement immediately so startup
         * cannot spend tens of seconds far above the requested bitrate.  All
         * subsequent adjustments retain the deliberately slow persistence
         * behavior that prevents visible pumping during scene changes.
         */
        controller->smoothed_gop_model_bits = (uint32_t)average;
        controller->current_qp = t31_select_model_qp(
            controller->smoothed_gop_model_bits, controller->target_bits,
            controller->min_qp, controller->max_qp);
        controller->over_target_gops = 0u;
        controller->under_target_gops = 0u;
    } else {
        controller->smoothed_gop_model_bits = (uint32_t)(
            ((uint64_t)controller->smoothed_gop_model_bits *
                 T31_GOP_EMA_OLD_WEIGHT +
             average + T31_GOP_EMA_OLD_WEIGHT / 2u) /
            (T31_GOP_EMA_OLD_WEIGHT + 1u));
    }
    if (controller->completed_gops != 0u &&
        controller->over_target_gops >= T31_OVER_TARGET_GOPS) {
        if (controller->current_qp < controller->max_qp)
            ++controller->current_qp;
        controller->over_target_gops = 0u;
        controller->under_target_gops = 0u;
    } else if (controller->completed_gops != 0u &&
               controller->under_target_gops >= T31_UNDER_TARGET_GOPS) {
        if (controller->current_qp > controller->min_qp)
            --controller->current_qp;
        controller->over_target_gops = 0u;
        controller->under_target_gops = 0u;
    } else if (controller->completed_gops != 0u) {
        controller->current_qp = t31_select_recovery_qp(controller);
        if (controller->current_qp != previous_qp) {
            controller->over_target_gops = 0u;
            controller->under_target_gops = 0u;
        }
    }
    controller->picture_target_bits = controller->target_bits;
    controller->gop_model_bits = 0u;
    controller->gop_pictures = 0u;
    ++controller->completed_gops;
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
    controller->smoothed_gop_model_bits = t31_scale_bits_for_qp(
        controller->target_bits,
        (int)T31_MODEL_REFERENCE_QP - (int)controller->current_qp);
    controller->initialized = 1;
    return 0;
}

int openimp_t31_rate_controller_set_bitrate(
    OpenIMPT31RateController *controller, uint32_t bitrate)
{
    uint64_t target_bits;
    uint32_t model_bits;
    uint32_t selected_qp;

    if (!controller || !controller->initialized || bitrate == 0u ||
        controller->fps_num == 0u || controller->fps_den == 0u)
        return -1;

    target_bits = ((uint64_t)bitrate * controller->fps_den +
                   controller->fps_num / 2u) / controller->fps_num;
    if (target_bits == 0u || target_bits > UINT32_MAX)
        return -1;

    /* smoothed_gop_model_bits is normalized to the reference QP and remains
     * valid across a target-rate change.  At startup, before a completed GOP
     * exists, derive the same normalized estimate from the old target. */
    model_bits = controller->smoothed_gop_model_bits;
    if (model_bits == 0u) {
        model_bits = t31_scale_bits_for_qp(
            controller->target_bits,
            (int)T31_MODEL_REFERENCE_QP -
                (int)controller->current_qp);
    }

    selected_qp = t31_select_model_qp(
        model_bits, (uint32_t)target_bits,
        controller->min_qp, controller->max_qp);

    controller->bitrate = bitrate;
    controller->target_bits = (uint32_t)target_bits;
    controller->picture_target_bits = controller->target_bits;
    controller->current_qp = selected_qp;
    controller->gop_model_bits = 0u;
    controller->gop_pictures = 0u;
    controller->over_target_gops = 0u;
    controller->under_target_gops = 0u;
    controller->virtual_buffer_bits = 0;
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

    /* An early IDR closes the preceding GOP.  The normal fixed-length path
     * below closes it after the last P picture, before the next IDR is
     * submitted, so its selected QP applies consistently to that next GOP. */
    if (is_idr && controller->gop_pictures > 0u) {
        /* Consumers commonly request a second IDR immediately after encoder
         * creation.  Do not teach the startup model from that one-picture
         * GOP: an IDR alone badly overestimates steady inter-frame cost. */
        if (controller->completed_gops == 0u &&
            controller->gop_pictures <
                (controller->gop_length + 1u) / 2u) {
            controller->gop_model_bits = 0u;
            controller->gop_pictures = 0u;
        } else {
            t31_complete_gop(controller);
        }
    }

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

    controller->gop_model_bits = t31_saturating_add_u64(
        controller->gop_model_bits,
        t31_scale_bits_for_qp(
            completed_bits,
            (int)T31_MODEL_REFERENCE_QP - (int)used_qp));
    ++controller->gop_pictures;
    if (controller->gop_pictures >= controller->gop_length)
        t31_complete_gop(controller);
    return 0;
}

uint32_t openimp_t31_rate_controller_qp(
    const OpenIMPT31RateController *controller)
{
    return controller && controller->initialized
        ? controller->current_qp : 0u;
}
