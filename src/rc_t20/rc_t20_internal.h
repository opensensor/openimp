#ifndef OPENIMP_RC_T20_INTERNAL_H
#define OPENIMP_RC_T20_INTERNAL_H

#include <stdint.h>

#include "rc_t20.h"
#include <string.h>

/* Field access by OEM byte offset (all fields used are naturally aligned). */
#define RU8(b, o)  (*(uint8_t *)((uint8_t *)(b) + (o)))
#define RI8(b, o)  (*(int8_t *)((uint8_t *)(b) + (o)))
#define RU32(b, o) (*(uint32_t *)((uint8_t *)(b) + (o)))
#define RI32(b, o) (*(int32_t *)((uint8_t *)(b) + (o)))
#define RF32(b, o) (*(float *)((uint8_t *)(b) + (o)))

/* The OEM state block behind the configuration (OEM E+0x90000 + offset). */
#define RX(o) (RCT20_RX_BASE + (o))

/* Macroblock maps behind it, sized to n macroblocks (E+228 x E+232):
 *   MB_DELTA      8 bytes, + class 0..7: QP offset (OEM E+0x5014f; its
 *                 class-0 byte is the last luma byte there, always 0)
 *   MB_CLASS      int per macroblock (OEM E+336)
 *   MB_LUMA(n)    byte per macroblock: centre luma (OEM E+0x40150)
 *   MB_QPTAB(n)   VPU QP table, MB_QPTAB_SIZE(n) bytes (OEM E+0x50158,
 *                 256 KiB); the run-length table takes at most n + 2 bytes
 *   MB_MAP(n)     byte per macroblock: QP map scratch (OEM: malloc) */
#define MB_DELTA (RCT20_RX_BASE + 400u)
#define MB_CLASS (MB_DELTA + 8u)
#define MB_LUMA(n) (MB_CLASS + 4u * (uint32_t)(n))
#define MB_QPTAB(n) (MB_LUMA(n) + (((uint32_t)(n) + 3u) & ~3u))
#define MB_QPTAB_SIZE(n) ((((uint32_t)(n) + 2u + 3u) & ~3u) + 8u)
#define MB_MAP(n) (MB_QPTAB(n) + MB_QPTAB_SIZE(n))
#define RCT20_E_BYTES(n) (MB_MAP(n) + (uint32_t)(n))

/* MIPS trunc.w.s / trunc.w.d: an invalid conversion (NaN, out of range)
 * yields 0x7fffffff with the exception disabled (the C cast is undefined
 * there and gives 0x80000000 on x86). */
static inline int32_t rct20_trunc_d(double x)
{
    if (!(x > -2147483649.0 && x < 2147483648.0))
        return 0x7fffffff;
    return (int32_t)x;
}

static inline int32_t rct20_trunc_f(float x)
{
    return rct20_trunc_d((double)x);
}

/* 32-bit wrap-around arithmetic as the OEM code does it. */
static inline int32_t rct20_mul(int32_t a, int32_t b)
{
    return (int32_t)((uint32_t)a * (uint32_t)b);
}

/* Macroblock count E+228 x E+232 (0 when unset) */
static inline uint32_t rct20_mbs(const uint8_t *E)
{
    int32_t n = rct20_mul(RI32(E, 228), RI32(E, 232));

    return n > 0 ? (uint32_t)n : 0u;
}

static inline int32_t rct20_add(int32_t a, int32_t b)
{
    return (int32_t)((uint32_t)a + (uint32_t)b);
}

static inline int32_t rct20_sub(int32_t a, int32_t b)
{
    return (int32_t)((uint32_t)a - (uint32_t)b);
}

/* MIPS div: the OEM traps on a zero divisor (teq); never reached with a
 * valid configuration.  INT_MIN / -1 gives INT_MIN on MIPS. */
static inline int32_t rct20_div(int32_t a, int32_t b)
{
    if (b == 0)
        return 0;
    if (b == -1)
        return rct20_sub(0, a);
    return a / b;
}

static inline uint32_t rct20_divu(uint32_t a, uint32_t b)
{
    return b ? a / b : 0;
}

#endif
