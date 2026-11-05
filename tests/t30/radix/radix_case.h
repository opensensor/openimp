/* Random Radix parameter blocks shared by radix_diff (vendor comparison
 * under qemu-mipsel) and radix_cmdlist_test (host regression). */
#ifndef RADIX_CASE_H
#define RADIX_CASE_H

#include <stdint.h>
#include <string.h>

#include "t30/radix/t30_radix_sliceinfo.h"

static uint32_t radix_rng = 0x2083u;

static uint32_t radix_rnd(void)
{
    radix_rng ^= radix_rng << 13;
    radix_rng ^= radix_rng >> 17;
    radix_rng ^= radix_rng << 5;
    return radix_rng;
}

static const uint16_t radix_sizes[][2] = {
    { 1920, 1080 }, { 1280, 720 }, { 640, 360 }, { 2304, 1296 },
    { 2560, 1440 }, { 2592, 1944 }, { 384, 216 }, { 352, 288 },
    { 2048, 1536 }, { 1920, 1088 }, { 400, 224 }, { 2560, 1920 },
};

/* Case c: random bytes everywhere (pointer fields are set by the caller),
 * half of the cases with small values in the flag bytes, I or P, a real
 * picture size and a QP table of 0..63 words in a third of the cases. */
static void radix_make_case(int c, uint8_t *si, uint8_t *state, uint32_t *qpt)
{
    unsigned s = radix_rnd() % (sizeof(radix_sizes) / sizeof(radix_sizes[0]));

    for (unsigned i = 0; i < T30_RADIX_SI_SIZE; i++)
        si[i] = (uint8_t)radix_rnd();
    for (unsigned i = 0; i < 256; i++)
        state[i] = (uint8_t)radix_rnd();
    for (unsigned i = 0; i < 256; i++)
        qpt[i] = radix_rnd();
    if (c & 1)
        for (unsigned i = 0x36; i < 0x1f7; i++)
            si[i] &= (c & 2) ? 1 : 7;
    rsi_set_u8(si, RSI_FRAME_TYPE, (radix_rnd() & 1) ? 2 : 1);
    rsi_set_u16(si, RSI_FRAME_WIDTH, radix_sizes[s][0]);
    rsi_set_u16(si, RSI_FRAME_HEIGHT, radix_sizes[s][1]);
    rsi_set_u16(si, RSI_QP_TABLE_LEN,
                (uint16_t)(radix_rnd() % 3 == 0 ? radix_rnd() % 64 : 0));
}

/* FNV-1a over the list words */
static uint32_t radix_hash(const uint32_t *l, size_t n)
{
    uint32_t h = 2166136261u;

    for (size_t i = 0; i < n; i++)
        for (int b = 0; b < 4; b++) {
            h ^= (l[i] >> (8 * b)) & 0xff;
            h *= 16777619u;
        }
    return h;
}

#endif
