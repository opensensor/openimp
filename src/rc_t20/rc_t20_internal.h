#ifndef OPENIMP_RC_T20_INTERNAL_H
#define OPENIMP_RC_T20_INTERNAL_H

#include <stdint.h>
#include <string.h>

/* Field access by OEM byte offset (all fields used are naturally aligned). */
#define RU8(b, o)  (*(uint8_t *)((uint8_t *)(b) + (o)))
#define RI8(b, o)  (*(int8_t *)((uint8_t *)(b) + (o)))
#define RU32(b, o) (*(uint32_t *)((uint8_t *)(b) + (o)))
#define RI32(b, o) (*(int32_t *)((uint8_t *)(b) + (o)))
#define RF32(b, o) (*(float *)((uint8_t *)(b) + (o)))

/* The OEM state block behind the configuration: 0x90000 + offset. */
#define RX(o) (0x90000 + (o))

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
