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

static void test_oem_model_selection_primitives(void)
{
    OpenIMPT41RateControlWindow first_idr = { {
        0x016e3600u, 0x00034bc0u, 0x000003e8u, 0x000061a8u,
        0x007a1200u, 0x00000101u, 0x00014e1cu, 0x0030d400u,
        0x000359d0u, 0u, 0u, 0u, 0x007402c0u, 0u, 1u, 0u,
    } };
    uint32_t predicted = 0u;
    uint32_t predictions[3] = { 0u, 0u, 0u };
    uint32_t adaptive_bits = 0x00005ccfu;
    uint32_t previous_distance = 4u;
    int16_t qp = 0;
    OpenIMPT41RateControlModelSet models = {
        {
            { 320000u, 38u, 11225u },
            { 320000u, 38u, 11225u },
            { 320000u, 38u, 11225u },
        },
        38,
        2,
        4,
    };

    /* Each value below was also executed directly against the OEM MIPS
     * helper.  Together they cover both prediction directions, step caps,
     * both QP-search directions, and the signed history target. */
    assert(openimp_t41_rate_control_window_target(&first_idr) ==
           11917120);
    assert(openimp_t41_rate_control_window_target(NULL) == 0);
    first_idr.words[3] = 0u;
    assert(openimp_t41_rate_control_window_target(&first_idr) == 0);

    assert(openimp_t41_rate_control_predict_bits(
               320000u, 38u, 39u, 11225u, 4, &predicted) == 0);
    assert(predicted == 285077u);
    assert(openimp_t41_rate_control_predict_bits(
               320000u, 38u, 34u, 11225u, 4, &predicted) == 0);
    assert(predicted == 508036u);
    assert(openimp_t41_rate_control_predict_bits(
               100000u, 35u, 45u, 14000u, 3, &predicted) == 0);
    assert(predicted == 36442u);
    assert(openimp_t41_rate_control_predict_bits(
               1u, 1u, 1u, 0u, 1, &predicted) == -1);
    assert(openimp_t41_rate_control_predict_bits(
               1u, 1u, 1u, 1u, -1, &predicted) == -1);
    assert(openimp_t41_rate_control_predict_bits(
               1u, 1u, 1u, 1u, 1, NULL) == -1);

    assert(openimp_t41_rate_control_search_qp(
               320000u, 38, 212520u, 11225u, 34, 51, &qp) == 0);
    assert(qp == 42);
    assert(openimp_t41_rate_control_search_qp(
               402168u, 39, 320000u, 11225u, 34, 51, &qp) == 0);
    assert(qp == 41);
    assert(openimp_t41_rate_control_search_qp(
               100000u, 40, 320000u, 11225u, 34, 51, &qp) == 0);
    assert(qp == 34);
    assert(openimp_t41_rate_control_search_qp(
               1u, 0, 1u, 0u, 0, 51, &qp) == -1);
    assert(openimp_t41_rate_control_search_qp(
               1u, 0, 1u, 1u, 1, 51, &qp) == -1);
    assert(openimp_t41_rate_control_search_qp(
               1u, 0, 1u, 1u, 0, 51, NULL) == -1);

    /* Exact ooIo three-picture-class predictions from the first OEM call. */
    assert(openimp_t41_rate_control_predict_model_set(
               &models, 0, 24000000u, predictions) == 0);
    assert(predictions[0] == 253966u);
    assert(predictions[1] == 320000u);
    assert(predictions[2] == 320000u);
    assert(openimp_t41_rate_control_predict_model_set(
               &models, 3, 24000000u, predictions) == 0);
    assert(predictions[0] == 201559u);
    assert(predictions[1] == 226250u);
    assert(predictions[2] == 226250u);
    assert(openimp_t41_rate_control_predict_model_set(
               NULL, 0, 1u, predictions) == -1);
    assert(openimp_t41_rate_control_predict_model_set(
               &models, 0, 1u, NULL) == -1);

    /* OEM l1io maps 100% feedback to zero permitted bound distance.  The
     * four exact inverse-scale steps turn 0x5ccf into 0x3a74. */
    assert(openimp_t41_rate_control_adjust_model(
               &adaptive_bits, &previous_distance, 38, 34, 51,
               11225u, 1, 100u) == 0);
    assert(adaptive_bits == 0x00003a74u);
    assert(previous_distance == 0u);
    assert(openimp_t41_rate_control_adjust_model(
               NULL, &previous_distance, 38, 34, 51,
               11225u, 1, 100u) == -1);
}

static void test_oem_picture_model_scale_update(void)
{
    struct ScaleFixture {
        uint32_t previous_bits;
        int16_t previous_qp;
        uint32_t completed_bits;
        int16_t completed_qp;
        uint32_t current_scale;
        uint32_t expected_scale;
    };
    static const struct ScaleFixture fixtures[] = {
        /* Captured OEM P-picture transitions. */
        { 537464u, 38, 212520u, 38, 11225u, 11225u },
        { 212520u, 38, 402168u, 39, 11225u, 11202u },
        { 91960u, 39, 171800u, 35, 11202u, 11691u },
        { 171800u, 35, 451808u, 34, 11691u, 20000u },
        { 170096u, 35, 124080u, 34, 20000u, 15590u },
        { 31160u, 42, 42560u, 41, 20000u, 13658u },
        { 30856u, 41, 48432u, 40, 13658u, 15696u },

        /* One-step interior and lower-clamp paths derived directly from the
         * same OEM integer instructions. */
        { 100000u, 38, 80000u, 39, 11225u, 12500u },
        { 100000u, 38, 99000u, 39, 11225u, 11180u },
    };
    uint32_t scale;
    size_t index;

    for (index = 0u; index < sizeof(fixtures) / sizeof(fixtures[0]);
         ++index) {
        const struct ScaleFixture *fixture = &fixtures[index];

        scale = 0u;
        assert(openimp_t41_rate_control_update_model_scale(
                   fixture->previous_bits, fixture->previous_qp,
                   fixture->completed_bits, fixture->completed_qp,
                   fixture->current_scale, 11180u, 20000u,
                   &scale) == 0);
        assert(scale == fixture->expected_scale);
    }

    scale = 0x5a5a5a5au;
    assert(openimp_t41_rate_control_update_model_scale(
               0u, 38, 1u, 39, 11225u, 11180u, 20000u, &scale) == -1);
    assert(scale == 0x5a5a5a5au);
    assert(openimp_t41_rate_control_update_model_scale(
               1u, 38, 1u, 39, 11225u, 20000u, 11180u, &scale) == -1);
    assert(openimp_t41_rate_control_update_model_scale(
               1u, -1, 1u, 39, 11225u, 11180u, 20000u, &scale) == -1);
    assert(openimp_t41_rate_control_update_model_scale(
               1u, 38, 1u, 39, 11225u, 11180u, 20000u, NULL) == -1);
}

static void init_oem_p_model_updater(
    OpenIMPT41RateControlPSelector *selector,
    OpenIMPT41RateControlPModelUpdater *updater,
    int16_t current_qp, uint32_t p_bits, uint16_t p_qp,
    uint32_t p_scale, uint32_t feedback_bits, uint16_t feedback_qp,
    uint32_t feedback_bound_distance,
    uint32_t allocation_weight, uint32_t baseline_ratio,
    uint32_t mode_0, uint32_t mode_1, uint32_t mode_2,
    uint32_t feedback_reference, uint32_t previous_bits,
    uint32_t cumulative_qp, uint32_t completed_p_pictures,
    uint8_t negative_latch)
{
    memset(selector, 0, sizeof(*selector));
    memset(updater, 0, sizeof(*updater));
    selector->models.models[0] =
        (OpenIMPT41RateControlModel){ 320000u, 38u, 11225u };
    selector->models.models[1] =
        (OpenIMPT41RateControlModel){ p_bits, p_qp, p_scale };
    selector->models.models[2] =
        (OpenIMPT41RateControlModel){ 320000u, 38u, 11225u };
    selector->models.current_qp = current_qp;
    selector->models.first_model_qp_bias = 2;
    selector->models.max_qp_steps = 4;
    selector->min_qp = 34;
    selector->max_qp = 51;
    selector->adaptive_model_bits = allocation_weight;
    selector->residual_policy_mode = mode_2;
    selector->negative_delta_latch = negative_latch;

    updater->feedback_model = (OpenIMPT41RateControlModel){
        feedback_bits, feedback_qp, 11225u
    };
    updater->baseline_min_qp = 34;
    updater->baseline_max_qp = 51;
    updater->feedback_min_qp = 34;
    updater->feedback_max_qp = 51;
    updater->feedback_model_bound_distance = feedback_bound_distance;
    updater->lower_scale = 11180u;
    updater->upper_scale = 20000u;
    updater->baseline_ratio = baseline_ratio;
    updater->modes[0] = mode_0;
    updater->modes[1] = mode_1;
    updater->modes[2] = mode_2;
    updater->feedback_reference_percent = feedback_reference;
    updater->previous_completed_bits = previous_bits;
    updater->cumulative_qp = cumulative_qp;
    updater->completed_p_pictures = completed_p_pictures;
}

static void assert_oem_p_model_result(
    const OpenIMPT41RateControlPSelector *selector,
    const OpenIMPT41RateControlPModelUpdater *updater,
    uint32_t p_bits, uint16_t p_qp, uint32_t p_scale,
    uint32_t feedback_bits, uint16_t feedback_qp,
    uint32_t allocation_weight, uint32_t baseline_ratio,
    uint32_t mode_2, uint32_t previous_bits,
    uint32_t cumulative_qp, uint32_t completed_p_pictures,
    uint8_t negative_latch)
{
    assert(selector->models.models[1].bits == p_bits);
    assert(selector->models.models[1].qp == p_qp);
    assert(selector->models.models[1].scale == p_scale);
    assert(updater->feedback_model.bits == feedback_bits);
    assert(updater->feedback_model.qp == feedback_qp);
    assert(selector->adaptive_model_bits == allocation_weight);
    assert(updater->baseline_ratio == baseline_ratio);
    assert(updater->modes[0] == 1u);
    assert(updater->modes[1] == 1u);
    assert(updater->modes[2] == mode_2);
    assert(selector->residual_policy_mode == mode_2);
    assert(updater->previous_completed_bits == previous_bits);
    assert(updater->cumulative_qp == cumulative_qp);
    assert(updater->completed_p_pictures == completed_p_pictures);
    assert(selector->negative_delta_latch == negative_latch);
}

static void test_oem_p_picture_model_updater(void)
{
    OpenIMPT41RateControlPSelector selector;
    OpenIMPT41RateControlPModelUpdater updater;
    OpenIMPT41RateControlFeedback feedback = { 0 };

    /* Calls 2, 3, 7, and 8 from the production OEM trace cover feedback
     * refresh, mode-2 allocation update, a four-QP scale root, and the upper
     * scale hysteresis reset respectively. */
    init_oem_p_model_updater(
        &selector, &updater, 38, 320000u, 38u, 11225u,
        7602880u, 34u, 4, 23759u, 333u, 2u, 2u, 10u,
        60u, 7602880u, 0u, 0u, 0u);
    feedback.field_20_percent = 0u;
    feedback.field_1c_percent = 100u;
    assert(openimp_t41_rate_control_update_p_picture_model(
               &selector, &updater, 537464u, &feedback, 1) == 0);
    assert_oem_p_model_result(
        &selector, &updater, 537464u, 38u, 11225u,
        537464u, 38u, 23759u, 472u, 2u,
        537464u, 38u, 1u, 0u);

    init_oem_p_model_updater(
        &selector, &updater, 38, 537464u, 38u, 11225u,
        537464u, 38u, 0, 14964u, 472u, 1u, 1u, 2u,
        0u, 537464u, 38u, 1u, 0u);
    feedback.field_20_percent = 33u;
    feedback.field_1c_percent = 40u;
    assert(openimp_t41_rate_control_update_p_picture_model(
               &selector, &updater, 212520u, &feedback, 1) == 0);
    assert_oem_p_model_result(
        &selector, &updater, 212520u, 38u, 11225u,
        537464u, 38u, 2529u, 1195u, 1u,
        212520u, 76u, 2u, 0u);

    init_oem_p_model_updater(
        &selector, &updater, 35, 91960u, 39u, 11202u,
        402168u, 39u, 2, 5509u, 2460u, 1u, 1u, 1u,
        44u, 91960u, 78u, 2u, 1u);
    feedback.field_20_percent = 36u;
    feedback.field_1c_percent = 32u;
    assert(openimp_t41_rate_control_update_p_picture_model(
               &selector, &updater, 171800u, &feedback, 1) == 0);
    assert_oem_p_model_result(
        &selector, &updater, 171800u, 35u, 11691u,
        402168u, 39u, 5509u, 2090u, 1u,
        171800u, 113u, 3u, 1u);

    init_oem_p_model_updater(
        &selector, &updater, 34, 171800u, 35u, 11691u,
        402168u, 39u, 1, 4907u, 2090u, 1u, 1u, 1u,
        36u, 171800u, 113u, 3u, 1u);
    feedback.field_20_percent = 3u;
    feedback.field_1c_percent = 70u;
    assert(openimp_t41_rate_control_update_p_picture_model(
               &selector, &updater, 451808u, &feedback, 1) == 0);
    assert_oem_p_model_result(
        &selector, &updater, 451808u, 34u, 20000u,
        402168u, 39u, 4907u, 892u, 1u,
        451808u, 147u, 4u, 0u);

    /* Exercise the feedback-discontinuity averaging path, which is dormant
     * in the captured steady-state segment. */
    init_oem_p_model_updater(
        &selector, &updater, 38, 100000u, 38u, 11225u,
        100000u, 38u, 0, 1000u, 1000u, 1u, 1u, 1u,
        100u, 300000u, 0u, 0u, 0u);
    feedback.field_20_percent = 0u;
    feedback.field_1c_percent = 0u;
    assert(openimp_t41_rate_control_update_p_picture_model(
               &selector, &updater, 100000u, &feedback, 0) == 0);
    assert(selector.models.models[1].bits == 200000u);

    {
        OpenIMPT41RateControlPSelector original_selector = selector;
        OpenIMPT41RateControlPModelUpdater original_updater = updater;

        assert(openimp_t41_rate_control_update_p_picture_model(
                   &selector, &updater, 0u, &feedback, 1) == -1);
        assert(memcmp(&selector, &original_selector,
                      sizeof(selector)) == 0);
        assert(memcmp(&updater, &original_updater,
                      sizeof(updater)) == 0);
    }
}

static void test_oem_p_picture_selector(void)
{
    struct SelectorFixture {
        uint32_t bits;
        uint32_t feedback;
        uint32_t model_bits[3];
        int16_t model_qp[3];
        uint32_t model_scale[3];
        int16_t current_qp;
        uint32_t remaining;
        uint32_t adaptive_bits;
        int32_t compensation_bits;
        int32_t history_bits;
        uint32_t policy_mode;
        uint8_t negative_latch;
        uint8_t low_feedback_latch;
        uint32_t expected_target;
        int64_t expected_residual;
        int32_t expected_delta;
        int16_t expected_qp;
        uint8_t expected_negative_latch;
        uint8_t expected_low_feedback_latch;
    };
    static const struct SelectorFixture fixtures[] = {
        { 537464u, 100u, { 320000u, 537464u, 320000u },
          { 38, 38, 38 }, { 11225u, 11225u, 11225u }, 38, 48u,
          14964u, -32685, 11699656, 2u, 0u, 1u,
          217455u, 10810072, 0, 38, 0u, 0u },
        { 212520u, 40u, { 320000u, 212520u, 320000u },
          { 38, 38, 38 }, { 11225u, 11225u, 11225u }, 38, 47u,
          3575u, -32685, 11807136, 1u, 0u, 0u,
          271642u, 17948767, 1, 39, 0u, 0u },
        { 91960u, 41u, { 320000u, 91960u, 320000u },
          { 38, 39, 38 }, { 11225u, 11202u, 11225u }, 39, 47u,
          5509u, 3725, 11877152, 1u, 0u, 0u,
          297254u, 20111458, -4, 35, 1u, 0u },
        { 124080u, 17u, { 320000u, 124080u, 320000u },
          { 38, 34, 38 }, { 11225u, 15590u, 11225u }, 34, 43u,
          4370u, 3725, 12239368, 1u, 1u, 0u,
          303518u, 13031772, -2, 34, 1u, 1u },
        { 1095568u, 72u, { 320000u, 1095568u, 320000u },
          { 38, 34, 38 }, { 11225u, 20000u, 11225u }, 34, 39u,
          4369u, 3725, 11315944, 1u, 0u, 1u,
          303524u, 2432368, 1, 35, 0u, 1u },
        { 211168u, 17u, { 320000u, 211168u, 320000u },
          { 38, 37, 38 }, { 11225u, 20000u, 11225u }, 37, 32u,
          6175u, 3725, 10511152, 1u, 0u, 1u,
          293711u, 17372464, 1, 38, 0u, 1u },
        { 42560u, 20u, { 320000u, 42560u, 320000u },
          { 38, 41, 38 }, { 11225u, 13658u, 11225u }, 41, 24u,
          6924u, 3725, 11515432, 1u, 1u, 1u,
          289827u, 18447568, 0, 41, 0u, 1u },
        { 27328u, 7u, { 320000u, 27328u, 320000u },
          { 38, 39, 38 }, { 11225u, 20000u, 11225u }, 39, 15u,
          6924u, 3725, 14045704, 1u, 0u, 1u,
          289827u, 18435784, -1, 38, 1u, 1u },
    };
    size_t index;

    for (index = 0u; index < sizeof(fixtures) / sizeof(fixtures[0]);
         ++index) {
        const struct SelectorFixture *fixture = &fixtures[index];
        OpenIMPT41RateControlPSelector selector;
        OpenIMPT41RateControlSelection selection;
        size_t model;

        memset(&selector, 0, sizeof(selector));
        for (model = 0u; model < 3u; ++model) {
            selector.models.models[model].bits = fixture->model_bits[model];
            selector.models.models[model].qp = fixture->model_qp[model];
            selector.models.models[model].scale =
                fixture->model_scale[model];
        }
        selector.models.current_qp = fixture->current_qp;
        selector.models.first_model_qp_bias = 2;
        selector.models.max_qp_steps = 4;
        selector.min_qp = 34;
        selector.max_qp = 51;
        selector.gop_length = 50u;
        selector.pictures_remaining = fixture->remaining;
        selector.allocation_budget_bits = 320000u;
        selector.residual_picture_bits = 320000u;
        selector.adaptive_model_bits = fixture->adaptive_bits;
        selector.allocation_compensation_bits = fixture->compensation_bits;
        selector.prediction_cap_bits = 24000000u;
        selector.buffer_budget_bits = 19200000u;
        selector.threshold_span_bits = 8000000u;
        selector.history_target_bits = fixture->history_bits;
        selector.residual_policy_mode = fixture->policy_mode;
        selector.negative_delta_latch = fixture->negative_latch;
        selector.low_feedback_latch = fixture->low_feedback_latch;

        assert(openimp_t41_rate_control_select_p_picture(
                   &selector, fixture->bits, fixture->feedback,
                   &selection) == 0);
        assert(selection.picture_target_bits == fixture->expected_target);
        assert(selection.residual_bits == fixture->expected_residual);
        assert(selection.qp_delta == fixture->expected_delta);
        assert(selection.selected_qp == fixture->expected_qp);
        assert(selector.models.current_qp == fixture->expected_qp);
        assert(selector.negative_delta_latch ==
               fixture->expected_negative_latch);
        assert(selector.low_feedback_latch ==
               fixture->expected_low_feedback_latch);
    }

    /* The first non-IDR call takes the positive four-step search before the
     * OEM mode-2 residual gate resets the final delta to zero. */
    {
        OpenIMPT41RateControlPSelector selector = {
            { {
                { 320000u, 38u, 11225u },
                { 537464u, 38u, 11225u },
                { 320000u, 38u, 11225u },
            }, 38, 2, 4 },
            34, 51, 50u, 48u, 320000u, 320000u, 14964u, -32685,
            24000000u, 19200000u, 8000000u, 11699656, 2u, 0u, 1u,
        };
        OpenIMPT41RateControlSelection selection;
        OpenIMPT41RateControlPSelector original = selector;

        assert(openimp_t41_rate_control_select_p_picture(
                   &selector, 537464u, 100u, &selection) == 0);
        assert(selection.adjusted_completed_bits == 338533u);
        assert(selection.predictions_before[1] == 537464u);
        assert(selection.predictions_after[1] == 338533u);

        selector = original;
        selector.gop_length = 1u;
        original = selector;
        assert(openimp_t41_rate_control_select_p_picture(
                   &selector, 537464u, 100u, &selection) == -1);
        assert(memcmp(&selector, &original, sizeof(selector)) == 0);
        selector.gop_length = 50u;
        original = selector;
        assert(openimp_t41_rate_control_select_p_picture(
                   &selector, 0u, 100u, &selection) == -1);
        assert(memcmp(&selector, &original, sizeof(selector)) == 0);
    }
}

static void test_oem_sequential_p_picture_completion(void)
{
    static const OpenIMPT41RateControlWindow after_call_2 = { {
        0x016e3600u, 0x00034bc0u, 0x000003e8u, 0x000061a8u,
        0x007a1200u, 0x00000101u, 0x000165bau, 0x006a3380u,
        0x000367e0u, 0u, 0u, 0u, 0x007c3638u, 0u, 2u, 0u,
    } };
    static const OpenIMPT41RateControlWindow after_call_3 = { {
        0x016e3600u, 0x00034bc0u, 0x000003e8u, 0x000061a8u,
        0x007a1200u, 0x00000101u, 0x00016f11u, 0x0057e400u,
        0x000375f0u, 0u, 0u, 0u, 0x007f7460u, 0u, 3u, 0u,
    } };
    static const OpenIMPT41RateControlWindow after_call_4 = { {
        0x016e3600u, 0x00034bc0u, 0x000003e8u, 0x000061a8u,
        0x007a1200u, 0x00000101u, 0x00018077u, 0x000c3500u,
        0x00038400u, 0u, 0u, 0u, 0x00857eb0u, 0u, 4u, 0u,
    } };
    OpenIMPT41RateControlWindow window = { {
        0x016e3600u, 0x00034bc0u, 0x000003e8u, 0x000061a8u,
        0x007a1200u, 0x00000101u, 0x00014e1cu, 0x0030d400u,
        0x000359d0u, 0u, 0u, 0u, 0x007402c0u, 0u, 1u, 0u,
    } };
    OpenIMPT41RateControlPSelector selector;
    OpenIMPT41RateControlPModelUpdater updater;
    OpenIMPT41RateControlSelection selection;
    OpenIMPT41RateControlFeedback feedback = { 0 };

    /* Start at the exact controller state following the trace's first IDR. */
    init_oem_p_model_updater(
        &selector, &updater, 38, 320000u, 38u, 11225u,
        7602880u, 34u, 4, 23759u, 333u, 2u, 2u, 10u,
        60u, 7602880u, 0u, 0u, 0u);
    selector.gop_length = 50u;
    selector.allocation_budget_bits = 320000u;
    selector.residual_picture_bits = 320000u;
    selector.allocation_compensation_bits = -32685;
    selector.prediction_cap_bits = 24000000u;
    selector.buffer_budget_bits = 19200000u;
    selector.threshold_span_bits = 8000000u;
    selector.low_feedback_latch = 1u;
    updater.last_picture_target_bits = 320000u;
    updater.gop_picture_count = 1u;
    updater.max_model_adjustment = 1u;

    feedback.field_20_percent = 0u;
    feedback.field_1c_percent = 100u;
    assert(openimp_t41_rate_control_complete_p_picture(
               &window, &selector, &updater, 537464u, &feedback,
               1, 1, &selection) == 0);
    assert(memcmp(&window, &after_call_2, sizeof(window)) == 0);
    assert(selection.picture_target_bits == 217455u);
    assert(selection.residual_bits == 10810072);
    assert(selection.qp_delta == 0);
    assert(selection.selected_qp == 38);
    assert(selector.models.current_qp == 38);
    assert_oem_p_model_result(
        &selector, &updater, 537464u, 38u, 11225u,
        537464u, 38u, 14964u, 472u, 2u,
        537464u, 38u, 1u, 0u);
    assert(updater.feedback_model_bound_distance == 0u);
    assert(updater.feedback_reference_percent == 0u);
    assert(updater.last_picture_target_bits == 320000u);
    assert(updater.gop_picture_count == 2u);
    assert(selector.low_feedback_latch == 0u);

    /* The following call consumes every state transition above.  Its mode-2
     * model refresh and selector output therefore test true coupling rather
     * than another independently seeded fixture. */
    feedback.field_20_percent = 33u;
    feedback.field_1c_percent = 40u;
    assert(openimp_t41_rate_control_complete_p_picture(
               &window, &selector, &updater, 212520u, &feedback,
               1, 1, &selection) == 0);
    assert(memcmp(&window, &after_call_3, sizeof(window)) == 0);
    assert(selection.picture_target_bits == 271642u);
    assert(selection.residual_bits == 17948767);
    assert(selection.qp_delta == 1);
    assert(selection.selected_qp == 39);
    assert(selector.models.current_qp == 39);
    assert_oem_p_model_result(
        &selector, &updater, 212520u, 38u, 11225u,
        537464u, 38u, 3575u, 1195u, 1u,
        212520u, 76u, 2u, 0u);
    assert(updater.feedback_model_bound_distance == 3u);
    assert(updater.feedback_reference_percent == 33u);
    assert(updater.last_picture_target_bits == 271642u);
    assert(updater.gop_picture_count == 3u);

    /* The next completion is the trace's second IDR.  It relearns the
     * allocation weight from the P model, refreshes the feedback model at the
     * bound-adjusted QP, and changes the signed GOP compensation. */
    feedback.field_20_percent = 0u;
    feedback.field_1c_percent = 100u;
    assert(openimp_t41_rate_control_complete_idr_picture(
               &window, &selector, &updater, 395856u, &feedback,
               1, &selection) == 0);
    assert(memcmp(&window, &after_call_4, sizeof(window)) == 0);
    assert(selection.picture_target_bits == 586217u);
    assert(selection.adjusted_completed_bits == 395856u);
    assert(selection.residual_bits == 190361);
    assert(selection.qp_delta == 0);
    assert(selection.selected_qp == 39);
    assert(selector.models.current_qp == 39);
    assert(selector.models.models[1].bits == 212520u);
    assert(selector.models.models[1].qp == 38u);
    assert(selector.models.models[1].scale == 11225u);
    assert(selector.adaptive_model_bits == 2090u);
    assert(selector.allocation_compensation_bits == 3725);
    assert(selector.residual_policy_mode == 1u);
    assert(updater.feedback_model.bits == 395856u);
    assert(updater.feedback_model.qp == 36u);
    assert(updater.feedback_model.scale == 11225u);
    assert(updater.feedback_model_bound_distance == 3u);
    assert(updater.baseline_ratio == 1195u);
    assert(updater.modes[0] == 2u);
    assert(updater.modes[1] == 2u);
    assert(updater.modes[2] == 1u);
    assert(updater.feedback_reference_percent == 33u);
    assert(updater.previous_completed_bits == 395856u);
    assert(updater.cumulative_qp == 0u);
    assert(updater.completed_p_pictures == 0u);
    assert(updater.last_picture_target_bits == 271642u);
    assert(updater.gop_picture_count == 1u);
}

static void test_coupled_controller(void)
{
    static const OpenIMPT41RateControlWindow first_idr_window = { {
        0x016e3600u, 0x00034bc0u, 0x000003e8u, 0x000061a8u,
        0x007a1200u, 0x00000101u, 0x00014e1cu, 0x0030d400u,
        0x000359d0u, 0u, 0u, 0u, 0x007402c0u, 0u, 1u, 0u,
    } };
    OpenIMPT41RateController controller;
    OpenIMPT41RateControlFeedback feedback = { 0 };

    assert(openimp_t41_rate_controller_init(
               &controller, 8000000u, 25u, 1u, 50u,
               34u, 51u, 38u) == 0);
    assert(controller.target_bits == 320000u);
    assert(controller.window.words[0] == 24000000u);
    assert(controller.window.words[1] == 216000u);
    assert(controller.window.words[2] == 1000u);
    assert(controller.window.words[3] == 25000u);
    assert(controller.window.words[4] == 8000000u);
    assert(controller.window.words[5] == 0x101u);
    assert(controller.window.words[8] == 216000u);
    assert(openimp_t41_rate_controller_qp(&controller) == 38u);
    assert(controller.selector.models.models[0].bits == 320000u);
    assert(controller.selector.models.models[1].bits == 320000u);
    assert(controller.selector.models.models[2].bits == 320000u);
    assert(controller.selector.adaptive_model_bits == 333u);
    assert(controller.selector.prediction_cap_bits == 24000000u);
    assert(controller.selector.buffer_budget_bits == 19200000u);
    assert(controller.selector.threshold_span_bits == 8000000u);
    assert(controller.updater.feedback_model_bound_distance == 4u);
    assert(controller.updater.lower_scale == 11180u);
    assert(controller.updater.upper_scale == 20000u);
    assert(controller.updater.modes[0] == 10u);
    assert(controller.updater.modes[1] == 10u);
    assert(controller.updater.modes[2] == 10u);

    /* Exact first production IDR, starting only from recovered constructor
     * defaults rather than a trace-seeded post-completion state. */
    feedback.field_20_percent = 0u;
    feedback.field_1c_percent = 100u;
    assert(openimp_t41_rate_controller_complete(
               &controller, 7602880u, 1, &feedback) == 0);
    assert(openimp_t41_rate_controller_qp(&controller) == 38u);
    assert(memcmp(&controller.window, &first_idr_window,
                  sizeof(controller.window)) == 0);
    assert(controller.selector.adaptive_model_bits == 23759u);
    assert(controller.selector.allocation_compensation_bits == -32685);
    assert(controller.selector.residual_policy_mode == 10u);
    assert(controller.updater.feedback_model.bits == 7602880u);
    assert(controller.updater.feedback_model.qp == 34u);
    assert(controller.updater.modes[0] == 2u);
    assert(controller.updater.modes[1] == 2u);
    assert(controller.updater.modes[2] == 10u);
    assert(controller.updater.previous_completed_bits == 7602880u);
    assert(controller.updater.gop_picture_count == 1u);
    assert(controller.last_selection.picture_target_bits == 5224699u);
    assert(controller.last_selection.residual_bits == -2378181);

    feedback.field_20_percent = 0u;
    feedback.field_1c_percent = 100u;
    assert(openimp_t41_rate_controller_complete(
               &controller, 537464u, 0, &feedback) == 0);
    assert(openimp_t41_rate_controller_qp(&controller) == 38u);
    feedback.field_20_percent = 33u;
    feedback.field_1c_percent = 40u;
    assert(openimp_t41_rate_controller_complete(
               &controller, 212520u, 0, &feedback) == 0);
    assert(openimp_t41_rate_controller_qp(&controller) == 39u);
    assert(controller.completed_pictures == 3u);
    assert(controller.completed_p_pictures == 2u);
}

static void test_controller_bounds_and_errors(void)
{
    OpenIMPT41RateController controller;

    assert(openimp_t41_rate_controller_init(
               &controller, 8000000u, 25u, 1u, 25u,
               34u, 51u, 99u) == 0);
    assert(openimp_t41_rate_controller_qp(&controller) == 51u);

    assert(openimp_t41_rate_controller_init(
               NULL, 1u, 1u, 1u, 1u, 0u, 51u, 1u) == -1);
    assert(openimp_t41_rate_controller_init(
               &controller, 0u, 1u, 1u, 1u, 0u, 51u, 1u) == -1);
    assert(openimp_t41_rate_controller_init(
               &controller, 1u, 1u, 1u, 1u, 52u, 51u, 1u) == -1);
    assert(openimp_t41_rate_controller_init(
               &controller, 1u, 1u, 1u, 0u, 0u, 51u, 1u) == -1);
    memset(&controller, 0, sizeof(controller));
    assert(openimp_t41_rate_controller_complete(
               &controller, 1u, 0, NULL) == -1);
    assert(openimp_t41_rate_controller_qp(NULL) == 0u);
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
    test_oem_model_selection_primitives();
    test_oem_picture_model_scale_update();
    test_oem_p_picture_model_updater();
    test_oem_p_picture_selector();
    test_oem_sequential_p_picture_completion();
    test_coupled_controller();
    test_controller_bounds_and_errors();
    puts("T41 software rate-control coupling: OK");
    return 0;
}
