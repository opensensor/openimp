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

#endif
