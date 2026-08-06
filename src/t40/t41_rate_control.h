#ifndef OPENIMP_T41_RATE_CONTROL_H
#define OPENIMP_T41_RATE_CONTROL_H

#include <stddef.h>
#include <stdint.h>

/* The OEM software controller consumes fields through +0x30 of the combined
 * 0x70-byte slice status, not just the exported 0x28-byte statistics view. */
#define OPENIMP_T41_RATE_CONTROL_INPUT_SIZE 0x34u
#define OPENIMP_T41_RATE_CONTROL_WINDOW_WORDS 16u

typedef struct OpenIMPT41RateControlFeedback {
    uint32_t block_count;
    uint32_t field_20_percent;
    uint32_t field_1c_percent;
    uint32_t field_14_bit_percent;
    uint32_t field_18_quarters_per_block;
} OpenIMPT41RateControlFeedback;

/* Exact 0x40-byte accumulator embedded at OEM controller offset +0xa0.
 * Keep its still-obfuscated fields indexed until their public semantics are
 * proven; this preserves the measured layout without inventing ABI names. */
typedef struct OpenIMPT41RateControlWindow {
    uint32_t words[OPENIMP_T41_RATE_CONTROL_WINDOW_WORDS];
} OpenIMPT41RateControlWindow;

typedef struct OpenIMPT41RateControlModel {
    uint32_t bits;
    uint16_t qp;
    uint32_t scale;
} OpenIMPT41RateControlModel;

typedef struct OpenIMPT41RateControlModelSet {
    OpenIMPT41RateControlModel models[3];
    int16_t current_qp;
    int32_t first_model_qp_bias;
    int32_t max_qp_steps;
} OpenIMPT41RateControlModelSet;

/* Semantic state for the T41 OEM selector's normal P-picture path.  The
 * original routine stores these values at unrelated offsets in a 440-byte
 * private object; keeping the recovered policy in a named structure avoids
 * spreading that private ABI through the codec. */
typedef struct OpenIMPT41RateControlPSelector {
    OpenIMPT41RateControlModelSet models;
    int16_t min_qp;
    int16_t max_qp;
    uint32_t gop_length;
    uint32_t pictures_remaining;
    uint32_t allocation_budget_bits;
    uint32_t residual_picture_bits;
    uint32_t adaptive_model_bits;
    int32_t allocation_compensation_bits;
    uint32_t prediction_cap_bits;
    uint32_t buffer_budget_bits;
    uint32_t threshold_span_bits;
    int32_t history_target_bits;
    uint32_t residual_policy_mode;
    uint8_t negative_delta_latch;
    uint8_t low_feedback_latch;
} OpenIMPT41RateControlPSelector;

typedef struct OpenIMPT41RateControlSelection {
    uint32_t picture_target_bits;
    uint32_t adjusted_completed_bits;
    uint32_t predictions_before[3];
    uint32_t predictions_after[3];
    int64_t residual_bits;
    int32_t qp_delta;
    int16_t selected_qp;
} OpenIMPT41RateControlSelection;

/* Software-owned part of the T41 CBR loop.  The 0x40-byte history and
 * normalized hardware feedback match the recovered OEM stages exactly.
 * Selection is deliberately bounded to one QP per completed P picture so a
 * corrupt statistic cannot destabilize the hardware controller. */
typedef struct OpenIMPT41RateController {
    OpenIMPT41RateControlWindow window;
    OpenIMPT41RateControlFeedback feedback;
    uint32_t bitrate;
    uint32_t fps_num;
    uint32_t fps_den;
    uint32_t gop_length;
    uint32_t target_bits;
    uint32_t smoothed_p_bits;
    uint32_t completed_pictures;
    uint32_t completed_p_pictures;
    uint32_t min_qp;
    uint32_t max_qp;
    uint32_t current_qp;
    int64_t virtual_buffer_bits;
    int initialized;
} OpenIMPT41RateController;

/* Recover the first, exact normalization stage of the OEM CBR controller.
 * The field-based names are intentional until the four hardware counters'
 * semantic names are established.  Arithmetic follows the OEM's uint32_t
 * operations, including its one-unit guards for empty denominators. */
int openimp_t41_rate_control_extract_feedback(
    const void *slice_status, size_t slice_status_size,
    uint32_t completed_bits, OpenIMPT41RateControlFeedback *feedback);

/* Advance the OEM bitrate-history accumulator by one completed access unit.
 * Returns -1 without changing the window if either measured time/rate
 * denominator is zero. */
int openimp_t41_rate_control_window_update(
    OpenIMPT41RateControlWindow *window, uint32_t completed_bits);

/* Exact pure model-selection primitives used by the OEM 4,084-byte o1II
 * routine.  These are kept separate from the policy state while the
 * picture-class model updater is being recovered, which makes every integer
 * transition independently replayable against captured OEM calls. */
int32_t openimp_t41_rate_control_window_target(
    const OpenIMPT41RateControlWindow *window);

int openimp_t41_rate_control_predict_bits(
    uint32_t model_bits, uint16_t model_qp, uint16_t requested_qp,
    uint32_t scale, int32_t max_qp_steps, uint32_t *predicted_bits);

int openimp_t41_rate_control_search_qp(
    uint32_t model_bits, int16_t model_qp, uint32_t target_bits,
    uint32_t scale, int16_t min_qp, int16_t max_qp, int16_t *selected_qp);

int openimp_t41_rate_control_predict_model_set(
    const OpenIMPT41RateControlModelSet *models, int32_t qp_delta,
    uint32_t prediction_cap, uint32_t predictions[3]);

int openimp_t41_rate_control_adjust_model(
    uint32_t *model_bits, uint32_t *previous_bound_distance,
    int16_t current_qp, int16_t min_qp, int16_t max_qp,
    uint32_t scale, int32_t max_adjustment, uint32_t feedback_percent);

/* Exact policy followed by the captured OEM T41 CBR configuration for a
 * normal P picture (mode 2, one-QP steps, no enhanced/temporal cadence).
 * The picture-class model updater remains a separate stage: callers provide
 * its current models and allocation terms, and this routine reproduces the
 * larger o1II selector's target, candidate search, residual correction,
 * hysteresis latches, and final QP clamp. */
int openimp_t41_rate_control_select_p_picture(
    OpenIMPT41RateControlPSelector *selector, uint32_t completed_bits,
    uint32_t feedback_percent, OpenIMPT41RateControlSelection *selection);

int openimp_t41_rate_controller_init(OpenIMPT41RateController *controller,
                                     uint32_t bitrate, uint32_t fps_num,
                                     uint32_t fps_den, uint32_t gop_length,
                                     uint32_t min_qp, uint32_t max_qp,
                                     uint32_t initial_qp);

/* Complete one access unit and select the QP for the next picture.  IDR
 * payloads advance the exact history but do not directly perturb the P-frame
 * model because their separate EP3 picture-class budget is GOP-sized. */
int openimp_t41_rate_controller_complete(
    OpenIMPT41RateController *controller, uint32_t completed_bits,
    int is_idr, const OpenIMPT41RateControlFeedback *feedback);

uint32_t openimp_t41_rate_controller_qp(
    const OpenIMPT41RateController *controller);

#endif
