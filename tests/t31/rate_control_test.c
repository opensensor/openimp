#include <math.h>
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

static int test_quiet_startup_does_not_drop_qp(void)
{
    OpenIMPT31RateController controller;
    unsigned int frame;

    EXPECT(openimp_t31_rate_controller_init(
        &controller, 1000000u, 25u, 1u, 25u, 18u, 45u, 26u) == 0);

    /* A settling capture path can initially repeat nearly identical frames.
     * That first GOP must not teach the controller to jump to minimum QP. */
    for (frame = 0u; frame < 25u; ++frame) {
        uint32_t qp = openimp_t31_rate_controller_qp(&controller);

        EXPECT(openimp_t31_rate_controller_complete(
            &controller, 4000u, qp, frame == 0u) == 0);
    }
    EXPECT(controller.completed_gops == 1u);
    EXPECT(openimp_t31_rate_controller_qp(&controller) == 26u);
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

/* The OEM CappedVBR update (libimp 1.1.6 0x55540): mse1000 = max(1,
 * sse * 1000 / pixels), PSNR = (int)(1000 * log10(255^2 * 1000 / mse1000))
 * in dB * 100, all integer divisions. */
static uint32_t oem_psnr_x100(uint64_t sse, uint32_t pixels)
{
    uint64_t mse1000 = sse * 1000u / pixels;
    uint64_t ratio;

    if (mse1000 == 0u)
        mse1000 = 1u;
    ratio = 255u * 255u * 1000u / mse1000;
    return (uint32_t)(log10((double)ratio) * 1000.0);
}

static int test_psnr_matches_oem_formula(void)
{
    static const uint32_t pixels[] = { 640u * 360u, 1920u * 1080u,
                                       2560u * 1440u };
    static const uint32_t mse_x100[] = { 1u, 7u, 50u, 100u, 333u, 1000u,
                                         4200u, 25000u, 650000u };
    unsigned int p;
    unsigned int m;

    for (p = 0u; p < sizeof(pixels) / sizeof(pixels[0]); ++p) {
        for (m = 0u; m < sizeof(mse_x100) / sizeof(mse_x100[0]); ++m) {
            uint64_t sse = (uint64_t)pixels[p] * mse_x100[m] / 100u;
            uint32_t want = oem_psnr_x100(sse, pixels[p]);
            uint32_t got = openimp_t31_psnr_x100(sse, pixels[p], 255u);

            EXPECT(got + 1u >= want && got <= want + 1u);
        }
    }
    /* MSE 1: 48.13 dB; a perfect picture saturates like the OEM (78.13) */
    EXPECT(openimp_t31_psnr_x100(640u * 360u, 640u * 360u, 255u) == 4813u);
    EXPECT(openimp_t31_psnr_x100(1u, 640u * 360u, 255u) >= 7812u);
    EXPECT(openimp_t31_psnr_x100(1u, 640u * 360u, 255u) <= 7813u);
    EXPECT(openimp_t31_psnr_x100(1000u, 0u, 255u) == 0u);
    EXPECT(openimp_t31_psnr_x100(1000u, 100u, 0u) == 0u);
    return 0;
}

/* Feed a static scene far below the target: 25 fps, GOP 25, 20 GOPs. */
static uint32_t run_quiet_scene(OpenIMPT31RateController *controller,
                                uint32_t psnr_x100)
{
    unsigned int frame;

    for (frame = 0u; frame < 25u * 20u; ++frame) {
        uint32_t qp = openimp_t31_rate_controller_qp(controller);

        openimp_t31_rate_controller_note_psnr(controller, psnr_x100);
        if (openimp_t31_rate_controller_complete(
                controller, scale_bits(6000u, (int)qp - 30),
                qp, frame % 25u == 0u) != 0)
            return 0u;
    }
    return openimp_t31_rate_controller_qp(controller);
}

static int test_quality_cap_holds_qp(void)
{
    OpenIMPT31RateController free_run;
    OpenIMPT31RateController capped;
    OpenIMPT31RateController below_cap;
    OpenIMPT31RateController unmeasured;
    uint32_t free_qp;

    EXPECT(openimp_t31_rate_controller_set_quality_cap(NULL, 4200u) != 0);
    EXPECT(openimp_t31_rate_controller_init(
        &free_run, 1000000u, 25u, 1u, 25u, 20u, 45u, 35u) == 0);
    EXPECT(free_run.max_psnr_x100 == 0u);
    free_qp = run_quiet_scene(&free_run, 0u);
    EXPECT(free_qp != 0u && free_qp < 35u);
    EXPECT(free_run.quality_cap_holds == 0u);

    /* above the cap: the QP never drops below where it started */
    EXPECT(openimp_t31_rate_controller_init(
        &capped, 1000000u, 25u, 1u, 25u, 20u, 45u, 35u) == 0);
    EXPECT(openimp_t31_rate_controller_set_quality_cap(&capped, 4200u) == 0);
    EXPECT(run_quiet_scene(&capped, 4500u) == 35u);
    EXPECT(capped.quality_cap_holds > 0u);

    /* below the cap, and without a measurement, the controller is free */
    EXPECT(openimp_t31_rate_controller_init(
        &below_cap, 1000000u, 25u, 1u, 25u, 20u, 45u, 35u) == 0);
    EXPECT(openimp_t31_rate_controller_set_quality_cap(&below_cap,
                                                       4200u) == 0);
    EXPECT(run_quiet_scene(&below_cap, 3900u) == free_qp);
    EXPECT(below_cap.quality_cap_holds == 0u);
    EXPECT(openimp_t31_rate_controller_init(
        &unmeasured, 1000000u, 25u, 1u, 25u, 20u, 45u, 35u) == 0);
    EXPECT(openimp_t31_rate_controller_set_quality_cap(&unmeasured,
                                                       4200u) == 0);
    EXPECT(run_quiet_scene(&unmeasured, 0u) == free_qp);

    /* the cap never stops a QP increase: overload above the cap */
    {
        unsigned int frame;

        for (frame = 0u; frame < 25u * 12u; ++frame) {
            uint32_t qp = openimp_t31_rate_controller_qp(&capped);

            openimp_t31_rate_controller_note_psnr(&capped, 4500u);
            EXPECT(openimp_t31_rate_controller_complete(
                &capped, scale_bits(120000u, (int)qp - 30), qp,
                frame % 25u == 0u) == 0);
        }
        EXPECT(openimp_t31_rate_controller_qp(&capped) > 35u);
    }

    /* init() clears the cap */
    EXPECT(openimp_t31_rate_controller_init(
        &capped, 1000000u, 25u, 1u, 25u, 20u, 45u, 35u) == 0);
    EXPECT(capped.max_psnr_x100 == 0u && capped.quality_cap_holds == 0u);
    return 0;
}

int main(void)
{
    if (test_validation() || test_steady_target() ||
        test_recording_shape_converges() ||
        test_first_gop_bootstraps_scene_model() ||
        test_startup_ignores_single_picture_gop() ||
        test_quiet_startup_does_not_drop_qp() ||
        test_short_motion_burst_does_not_pump_qp() ||
        test_sustained_motion_requires_fresh_persistence() ||
        test_bounds_and_scene_changes() ||
        test_large_completion_is_bounded() ||
        test_runtime_bitrate_retarget() ||
        test_psnr_matches_oem_formula() ||
        test_quality_cap_holds_qp())
        return 1;
    puts("T31 rate-control tests passed");
    return 0;
}
