#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "t30_annexb.h"

/* Straightforward reference: start code, NAL header, then one byte at a
 * time with emulation prevention over the concatenated RBSP. */
static uint32_t reference_nal(const uint8_t *rbsp, uint32_t length,
                              uint8_t *output, int type, int priority)
{
    uint32_t used = 0;
    unsigned int zeros = 0;
    uint32_t i;

    output[used++] = 0;
    output[used++] = 0;
    output[used++] = 0;
    output[used++] = 1;
    output[used++] = (uint8_t)((priority << 5) | type);
    for (i = 0; i < length; i++) {
        if (zeros >= 2u && rbsp[i] <= 3u) {
            output[used++] = 3;
            zeros = 0;
        }
        output[used++] = rbsp[i];
        zeros = rbsp[i] ? 0u : zeros + 1u;
    }
    return used;
}

static uint32_t next_random(uint32_t *state)
{
    *state = *state * 1103515245u + 12345u;
    return *state >> 8;
}

static void fill(uint8_t *data, uint32_t length, uint32_t *state,
                 unsigned int zero_percent)
{
    uint32_t i;

    for (i = 0; i < length; i++) {
        uint32_t r = next_random(state);

        if (r % 100u < zero_percent)
            data[i] = 0;
        else if (r % 7u == 0u)
            data[i] = (uint8_t)(1u + (r >> 8) % 3u);
        else
            data[i] = (uint8_t)(r >> 12);
    }
}

static void check_split(const uint8_t *rbsp, uint32_t length, uint32_t split)
{
    static uint8_t expected[1u << 16];
    static uint8_t actual[1u << 16];
    T30AnnexBWriter writer;
    uint32_t expected_length;
    uint32_t bound = t30_annexb_bound(length);

    assert(bound <= sizeof(actual));
    expected_length = reference_nal(rbsp, length, expected, 5, 3);
    assert(expected_length <= bound);

    memset(actual, 0xa5, sizeof(actual));
    assert(t30_annexb_begin(&writer, actual, bound, 5, 3) == 0);
    assert(t30_annexb_append(&writer, rbsp, split) == 0);
    assert(t30_annexb_append(&writer, rbsp + split, length - split) == 0);
    assert((uint32_t)(writer.output - actual) == expected_length);
    assert(memcmp(actual, expected, expected_length) == 0);

    /* Exactly enough room succeeds; one byte less fails without writing
     * past the end. */
    memset(actual, 0xa5, sizeof(actual));
    assert(t30_annexb_begin(&writer, actual, expected_length, 5, 3) == 0);
    assert(t30_annexb_append(&writer, rbsp, split) == 0);
    assert(t30_annexb_append(&writer, rbsp + split, length - split) == 0);
    assert((uint32_t)(writer.output - actual) == expected_length);

    memset(actual, 0xa5, sizeof(actual));
    assert(t30_annexb_begin(&writer, actual, expected_length - 1u, 5, 3) == 0);
    assert(t30_annexb_append(&writer, rbsp, split) != 0 ||
           t30_annexb_append(&writer, rbsp + split, length - split) != 0);
    assert(actual[expected_length - 1u] == 0xa5);
}

int main(void)
{
    static uint8_t rbsp[20000];
    static const uint8_t seam[] = { 0x41, 0x00, 0x00, 0x01, 0x00, 0x00,
                                    0x00, 0x00, 0x03, 0x00, 0x00, 0x02 };
    uint32_t state = 1u;
    unsigned int round;
    uint32_t split;

    /* Every split of a payload dense with escapes, including splits that
     * fall between the two zeros and the byte needing protection. */
    for (split = 0; split <= sizeof(seam); split++)
        check_split(seam, sizeof(seam), split);

    /* Worst case expansion: all zero bytes. */
    memset(rbsp, 0, 4096);
    check_split(rbsp, 4096, 1000);

    for (round = 0; round < 400u; round++) {
        uint32_t length = 1u + next_random(&state) % sizeof(rbsp);
        unsigned int zero_percent = (unsigned int)(round % 5u) * 20u;

        fill(rbsp, length, &state, zero_percent);
        check_split(rbsp, length, next_random(&state) % (length + 1u));
    }
    puts("T30 Annex-B writer tests passed");
    return 0;
}
