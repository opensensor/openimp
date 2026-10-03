/*
 * T21 sinter (2D NR) strength mapping and the 0x800002c block layout.
 *
 * The stock libimp T21 1.0.33 IMP_ISP_Tuning_SetSinterStrength scales table
 * 109 through control 0x8000161 (value/100, vendor neutral 100), which the
 * OEM kernel ignores.  The open-tx-isp driver (claude/t21-tuning-controls)
 * instead implements SinterDnsAttr (CID 0x800002c): MANUAL scales every SDNS
 * noise-profile field by strength/128 (clamped to 255), AUTO = 128 = stock.
 *
 * SetSinterStrength(v) on T21 therefore uses the timps scale (0..255,
 * default 128 = neutral):
 *     v == 128  -> AUTO block  (exactly the stock picture)
 *     otherwise -> MANUAL block, strength = min(v, 255)
 * GetSinterStrength returns block[43] when MANUAL, else 128.
 *
 * Block (112 bytes, zeroed): [70] = 1 valid; MANUAL also [10] = [98] = 1 and
 * [43] = strength.
 */
#ifndef ISP_T21_SINTER_H
#define ISP_T21_SINTER_H

#include <stdint.h>
#include <string.h>

#define T21_SINTER_BLOCK_SIZE 112
#define T21_SINTER_NEUTRAL    128
#define T21_SINTER_B_MANUAL   10
#define T21_SINTER_B_STRENGTH 43
#define T21_SINTER_B_VALID    70
#define T21_SINTER_B_MANUAL2  98

static inline void t21_sinter_fill_block(uint8_t *blk, int manual,
                                         uint8_t strength)
{
    memset(blk, 0, T21_SINTER_BLOCK_SIZE);
    blk[T21_SINTER_B_VALID] = 1;
    if (manual) {
        blk[T21_SINTER_B_MANUAL] = 1;
        blk[T21_SINTER_B_MANUAL2] = 1;
        blk[T21_SINTER_B_STRENGTH] = strength;
    }
}

/* API value -> block (strength 128 = AUTO). */
static inline void t21_sinter_strength_block(uint32_t v, uint8_t *blk)
{
    if (v == T21_SINTER_NEUTRAL)
        t21_sinter_fill_block(blk, 0, 0);
    else
        t21_sinter_fill_block(blk, 1, v > 255 ? 255 : (uint8_t)v);
}

/* Block read back from the driver -> API value. */
static inline uint32_t t21_sinter_block_strength(const uint8_t *blk)
{
    return blk[T21_SINTER_B_MANUAL] ? blk[T21_SINTER_B_STRENGTH]
                                    : T21_SINTER_NEUTRAL;
}

#endif
