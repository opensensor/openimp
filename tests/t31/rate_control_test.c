#include <stdint.h>
#include <stdio.h>

#include "t40/t31_rate_control.h"

#define EXPECT(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "failed at line %d: %s\n", __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static uint32_t scale_bits(uint32_t bits, int qp_delta)
{
    uint64_t result = bits;

    while (qp_delta > 0) {
        result = (result * 58386u + 32768u) >> 16;
        --qp_delta;
    }
    while (qp_delta < 0) {
        result = (result * 73562u + 32768u) >> 16;
        ++qp_delta;
    }
    return (uint32_t)result;
}

static int test_validation(void)
{
    OpenIMPT31RateController controller;

    EXPECT(openimp_t31_rate_controller_init(
        NULL, 8000000u, 30u, 1u, 30u, 20u, 45u, 27u) != 0);
    EXPECT(openimp_t31_rate_controller_init(
        &controller, 0u, 30u, 1u, 30u, 20u, 45u, 27u) != 0);
    EXPECT(openimp_t31_rate_controller_init(
        &controller, 8000000u, 0u, 1u, 30u, 20u, 45u, 27u) != 0);
    EXPECT(openimp_t31_rate_controller_init(
        &controller, 8000000u, 30u, 1u, 30u, 46u, 45u, 27u) != 0);
    EXPECT(openimp_t31_rate_controller_init(
        &controller, 8000000u, 30u, 1u, 30u, 20u, 52u, 27u) != 0);
    EXPECT(openimp_t31_rate_controller_init(
        &controller, 8000000u, 30u, 1u, 30u, 20u, 45u, 27u) == 0);
    EXPECT(controller.target_bits == 266667u);
    EXPECT(controller.current_qp == 27u);
    EXPECT(openimp_t31_rate_controller_init(
        &controller, 8000000u, 30u, 1u, 30u, 34u, 45u, 26u) == 0);
    EXPECT(controller.current_qp == 34u);
    EXPECT(openimp_t31_rate_controller_init(
        &controller, 8000000u, 30u, 1u, 30u, 20u, 45u, 51u) == 0);
    EXPECT(controller.current_qp == 45u);
    EXPECT(openimp_t31_rate_controller_complete(
        &controller, 0u, 45u, 0) != 0);
    EXPECT(openimp_t31_rate_controller_complete(
        &controller, 266667u, 52u, 0) != 0);
    return 0;
}

static int test_steady_target(void)
{
    OpenIMPT31RateController controller;
    unsigned int frame;

    EXPECT(openimp_t31_rate_controller_init(
        &controller, 8000000u, 30u, 1u, 30u, 20u, 45u, 27u) == 0);
    for (frame = 0u; frame < 300u; ++frame) {
        uint32_t qp = openimp_t31_rate_controller_qp(&controller);

        EXPECT(openimp_t31_rate_controller_complete(
            &controller, controller.target_bits, qp,
            frame % 30u == 0u) == 0);
        EXPECT(openimp_t31_rate_controller_qp(&controller) == 27u);
    }
    EXPECT(controller.virtual_buffer_bits == 0);
    return 0;
}

/* Replay the measured recording's encoder shape: around 1.23 Mbit IDRs and
 * 394 kbit P pictures at the old static QPs (26/27).  The open-loop result is
 * about 12.4 Mbit/s.  A correct loop converges at QP 31 and remains near the
 * requested 8 Mbit/s without changing the public bitrate setting. */
static int test_recording_shape_converges(void)
{
    OpenIMPT31RateController controller;
    uint64_t total_bits = 0u;
    uint64_t settled_bits = 0u;
    unsigned int settled_frames = 0u;
    unsigned int frame;

    EXPECT(openimp_t31_rate_controller_init(
        &controller, 8000000u, 30u, 1u, 30u, 20u, 45u, 27u) == 0);
    for (frame = 0u; frame < 900u; ++frame) {
        int is_idr = frame % 30u == 0u;
        uint32_t p_qp = openimp_t31_rate_controller_qp(&controller);
        uint32_t used_qp = is_idr && p_qp > 20u ? p_qp - 1u : p_qp;
        uint32_t bits = is_idr
            ? scale_bits(1230000u, (int)used_qp - 26)
            : scale_bits(394000u, (int)used_qp - 27);
        uint32_t previous_qp = p_qp;
        uint32_t next_qp;

        total_bits += bits;
        if (frame >= 300u) {
            settled_bits += bits;
            ++settled_frames;
        }
        EXPECT(openimp_t31_rate_controller_complete(
            &controller, bits, used_qp, is_idr) == 0);
        next_qp = openimp_t31_rate_controller_qp(&controller);
        if (frame + 1u != 30u) {
            EXPECT(next_qp <= previous_qp + 1u);
            EXPECT(previous_qp <= next_qp + 1u);
        }
        if ((frame + 1u) % 30u != 0u)
            EXPECT(next_qp == previous_qp);
        EXPECT(next_qp >= 20u && next_qp <= 45u);
    }

    EXPECT(total_bits * 30u / 900u < 9200000u);
    EXPECT(settled_bits * 30u / settled_frames >= 7400000u);
    EXPECT(settled_bits * 30u / settled_frames <= 8500000u);
    EXPECT(openimp_t31_rate_controller_qp(&controller) >= 30u);
    EXPECT(openimp_t31_rate_controller_qp(&controller) <= 32u);
    return 0;
}

static int test_first_gop_bootstraps_scene_model(void)
{
    OpenIMPT31RateController controller;
    unsigned int frame;

    EXPECT(openimp_t31_rate_controller_init(
        &controller, 3000000u, 25u, 1u, 25u, 15u, 45u, 26u) == 0);
    for (frame = 0u; frame < 25u; ++frame) {
        uint32_t qp = openimp_t31_rate_controller_qp(&controller);
        uint32_t bits = scale_bits(120000u, (int)qp - 44);

        EXPECT(openimp_t31_rate_controller_complete(
            &controller, bits, qp, frame == 0u) == 0);
    }
    EXPECT(controller.completed_gops == 1u);
    EXPECT(openimp_t31_rate_controller_qp(&controller) == 44u);

    for (frame = 0u; frame < 25u; ++frame) {
        uint32_t qp = openimp_t31_rate_controller_qp(&controller);
        uint32_t bits = scale_bits(120000u, (int)qp - 44);

        EXPECT(openimp_t31_rate_controller_complete(
            &controller, bits, qp, frame == 0u) == 0);
    }
    EXPECT(openimp_t31_rate_controller_qp(&controller) == 44u);
    return 0;
}

static int test_startup_ignores_single_picture_gop(void)
{
    OpenIMPT31RateController controller;
    unsigned int frame;

    EXPECT(openimp_t31_rate_controller_init(
        &controller, 3000000u, 25u, 1u, 25u, 15u, 45u, 26u) == 0);
    EXPECT(openimp_t31_rate_controller_complete(
        &controller, 1500000u, 26u, 1) == 0);
    EXPECT(openimp_t31_rate_controller_complete(
        &controller, 1500000u, 26u, 1) == 0);
    EXPECT(controller.completed_gops == 0u);
    EXPECT(controller.gop_pictures == 1u);
    EXPECT(openimp_t31_rate_controller_qp(&controller) == 26u);

    for (frame = 1u; frame < 25u; ++frame) {
        uint32_t qp = openimp_t31_rate_controller_qp(&controller);
        uint32_t bits = scale_bits(120000u, (int)qp - 44);

        EXPECT(openimp_t31_rate_controller_complete(
            &controller, bits, qp, 0) == 0);
    }
    EXPECT(controller.completed_gops == 1u);
    EXPECT(openimp_t31_rate_controller_qp(&controller) >= 44u);
    return 0;
}

static int test_short_motion_burst_does_not_pump_qp(void)
{
    OpenIMPT31RateController controller;
    unsigned int frame;

    EXPECT(openimp_t31_rate_controller_init(
        &controller, 2000000u, 25u, 1u, 25u, 20u, 45u, 34u) == 0);
    for (frame = 0u; frame < 250u; ++frame) {
        int is_idr = frame % 25u == 0u;
        int motion = frame >= 100u && frame < 150u;
        uint32_t qp = openimp_t31_rate_controller_qp(&controller);
        uint32_t used_qp = is_idr && qp > 20u ? qp - 1u : qp;
        uint32_t bits_at_qp34 = is_idr ? 500000u
            : motion ? 120000u : 60000u;
        uint32_t bits = scale_bits(bits_at_qp34,
                                   (int)used_qp - 34);

        EXPECT(openimp_t31_rate_controller_complete(
            &controller, bits, used_qp, is_idr) == 0);
        EXPECT(openimp_t31_rate_controller_qp(&controller) == 34u);
    }
    EXPECT(controller.completed_gops == 10u);
    return 0;
}

static int test_sustained_motion_requires_fresh_persistence(void)
{
    OpenIMPT31RateController controller;
    unsigned int frame;

    EXPECT(openimp_t31_rate_controller_init(
        &controller, 2000000u, 25u, 1u, 25u, 20u, 45u, 34u) == 0);

    /* Calibrate the startup model on one nominal GOP. */
    for (frame = 0u; frame < 25u; ++frame) {
        int is_idr = frame == 0u;
        uint32_t qp = openimp_t31_rate_controller_qp(&controller);
        uint32_t used_qp = is_idr && qp > 20u ? qp - 1u : qp;
        uint32_t bits_at_qp34 = is_idr ? 500000u : 60000u;
        uint32_t bits = scale_bits(bits_at_qp34,
                                   (int)used_qp - 34);

        EXPECT(openimp_t31_rate_controller_complete(
            &controller, bits, used_qp, is_idr) == 0);
        EXPECT(openimp_t31_rate_controller_qp(&controller) == 34u);
    }

    for (frame = 0u; frame < 125u; ++frame) {
        int is_idr = frame % 25u == 0u;
        uint32_t qp = openimp_t31_rate_controller_qp(&controller);
        uint32_t used_qp = is_idr && qp > 20u ? qp - 1u : qp;
        uint32_t bits_at_qp34 = is_idr ? 500000u : 180000u;
        uint32_t bits = scale_bits(bits_at_qp34,
                                   (int)used_qp - 34);

        EXPECT(openimp_t31_rate_controller_complete(
            &controller, bits, used_qp, is_idr) == 0);
        EXPECT(openimp_t31_rate_controller_qp(&controller) <= 35u);
    }
    EXPECT(openimp_t31_rate_controller_qp(&controller) == 35u);
    return 0;
}

static int test_bounds_and_scene_changes(void)
{
    OpenIMPT31RateController controller;
    unsigned int frame;

    EXPECT(openimp_t31_rate_controller_init(
        &controller, 1000000u, 25u, 1u, 25u, 24u, 30u, 27u) == 0);
    for (frame = 0u; frame < 250u; ++frame) {
        uint32_t qp = openimp_t31_rate_controller_qp(&controller);

        EXPECT(openimp_t31_rate_controller_complete(
            &controller, 300000u, qp, frame % 25u == 0u) == 0);
    }
    EXPECT(openimp_t31_rate_controller_qp(&controller) == 30u);

    for (frame = 0u; frame < 1000u; ++frame) {
        uint32_t qp = openimp_t31_rate_controller_qp(&controller);

        EXPECT(openimp_t31_rate_controller_complete(
            &controller, 2000u, qp, frame % 25u == 0u) == 0);
    }
    EXPECT(openimp_t31_rate_controller_qp(&controller) == 24u);
    return 0;
}

static int test_large_completion_is_bounded(void)
{
    OpenIMPT31RateController controller;

    EXPECT(openimp_t31_rate_controller_init(
        &controller, 8000000u, 30u, 1u, 30u, 0u, 51u, 27u) == 0);
    EXPECT(openimp_t31_rate_controller_complete(
        &controller, UINT32_MAX, 27u, 0) == 0);
    EXPECT(controller.virtual_buffer_bits <=
           (int64_t)controller.target_bits * controller.gop_length * 8);
    EXPECT(openimp_t31_rate_controller_qp(&controller) == 27u);
    return 0;
}

static int test_runtime_bitrate_retarget(void)
{
    OpenIMPT31RateController controller;
    uint32_t model_bits;
    unsigned int frame;

    EXPECT(openimp_t31_rate_controller_set_bitrate(NULL, 350000u) != 0);
    EXPECT(openimp_t31_rate_controller_init(
        &controller, 3000000u, 25u, 1u, 25u, 18u, 45u, 28u) == 0);
    EXPECT(openimp_t31_rate_controller_set_bitrate(&controller, 0u) != 0);

    for (frame = 0u; frame < 50u; ++frame) {
        uint32_t qp = openimp_t31_rate_controller_qp(&controller);

        EXPECT(openimp_t31_rate_controller_complete(
            &controller, 120000u, qp, frame % 25u == 0u) == 0);
    }
    model_bits = controller.smoothed_gop_model_bits;
    EXPECT(model_bits != 0u);

    EXPECT(openimp_t31_rate_controller_set_bitrate(
        &controller, 350000u) == 0);
    EXPECT(controller.bitrate == 350000u);
    EXPECT(controller.target_bits == 14000u);
    EXPECT(controller.current_qp == 45u);
    EXPECT(controller.smoothed_gop_model_bits == model_bits);
    EXPECT(controller.gop_pictures == 0u);
    EXPECT(controller.virtual_buffer_bits == 0);

    EXPECT(openimp_t31_rate_controller_set_bitrate(
        &controller, 3000000u) == 0);
    EXPECT(controller.current_qp >= 28u);
    EXPECT(controller.current_qp <= 29u);
    return 0;
}

int main(void)
{
    if (test_validation() || test_steady_target() ||
        test_recording_shape_converges() ||
        test_first_gop_bootstraps_scene_model() ||
        test_startup_ignores_single_picture_gop() ||
        test_short_motion_burst_does_not_pump_qp() ||
        test_sustained_motion_requires_fresh_persistence() ||
        test_bounds_and_scene_changes() ||
        test_large_completion_is_bounded() ||
        test_runtime_bitrate_retarget())
        return 1;
    puts("T31 rate-control tests passed");
    return 0;
}
