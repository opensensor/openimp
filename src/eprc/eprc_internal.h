#ifndef OPENIMP_EPRC_INTERNAL_H
#define OPENIMP_EPRC_INTERNAL_H

#include <stdint.h>
#include <math.h>
#include <string.h>

/* Field access by OEM byte offset.  The OEM blocks are naturally aligned
 * for every field type used here, except the gop-sized arrays (see EAU32). */
#define EU8(b, o)  (*(uint8_t *)((uint8_t *)(b) + (o)))
#define EI8(b, o)  (*(int8_t *)((uint8_t *)(b) + (o)))
#define EU16(b, o) (*(uint16_t *)((uint8_t *)(b) + (o)))
#define EI16(b, o) (*(int16_t *)((uint8_t *)(b) + (o)))
#define EU32(b, o) (*(uint32_t *)((uint8_t *)(b) + (o)))
#define EI32(b, o) (*(int32_t *)((uint8_t *)(b) + (o)))
#define EF32(b, o) (*(float *)((uint8_t *)(b) + (o)))
#define EF64(b, o) (*(double *)((uint8_t *)(b) + (o)))

/* OEM pointer slot: written as the OEM does (32-bit), never read back. */
#define EPTR(b, o, ptr) (EU32(b, o) = (uint32_t)(uintptr_t)(ptr))

/* The OEM places int arrays behind odd-sized byte arrays, so they can be
 * misaligned (the kernel fixes that up for the OEM). */
static inline int32_t eprc_ld32(const uint8_t *q)
{
    int32_t v;
    __builtin_memcpy(&v, q, 4);
    return v;
}

static inline void eprc_st32(uint8_t *q, int32_t v)
{
    __builtin_memcpy(q, &v, 4);
}

/* 2^k for the integer exponents of update_qp / FRAME_REPEATE_JUDGE
 * (pow(2.0, (qp - 4) / 6)): powers of two are exact, so the table gives
 * the very doubles pow() returns; pow() only outside -64..64. */
static inline double eprc_pow2i(int32_t k)
{
    static const double eprc_pow2_tab[129] = {
        0x1p-64, 0x1p-63, 0x1p-62, 0x1p-61, 0x1p-60, 0x1p-59, 0x1p-58, 0x1p-57,
        0x1p-56, 0x1p-55, 0x1p-54, 0x1p-53, 0x1p-52, 0x1p-51, 0x1p-50, 0x1p-49,
        0x1p-48, 0x1p-47, 0x1p-46, 0x1p-45, 0x1p-44, 0x1p-43, 0x1p-42, 0x1p-41,
        0x1p-40, 0x1p-39, 0x1p-38, 0x1p-37, 0x1p-36, 0x1p-35, 0x1p-34, 0x1p-33,
        0x1p-32, 0x1p-31, 0x1p-30, 0x1p-29, 0x1p-28, 0x1p-27, 0x1p-26, 0x1p-25,
        0x1p-24, 0x1p-23, 0x1p-22, 0x1p-21, 0x1p-20, 0x1p-19, 0x1p-18, 0x1p-17,
        0x1p-16, 0x1p-15, 0x1p-14, 0x1p-13, 0x1p-12, 0x1p-11, 0x1p-10, 0x1p-9,
        0x1p-8, 0x1p-7, 0x1p-6, 0x1p-5, 0x1p-4, 0x1p-3, 0x1p-2, 0x1p-1, 0x1p0,
        0x1p1, 0x1p2, 0x1p3, 0x1p4, 0x1p5, 0x1p6, 0x1p7, 0x1p8, 0x1p9, 0x1p10,
        0x1p11, 0x1p12, 0x1p13, 0x1p14, 0x1p15, 0x1p16, 0x1p17, 0x1p18, 0x1p19,
        0x1p20, 0x1p21, 0x1p22, 0x1p23, 0x1p24, 0x1p25, 0x1p26, 0x1p27, 0x1p28,
        0x1p29, 0x1p30, 0x1p31, 0x1p32, 0x1p33, 0x1p34, 0x1p35, 0x1p36, 0x1p37,
        0x1p38, 0x1p39, 0x1p40, 0x1p41, 0x1p42, 0x1p43, 0x1p44, 0x1p45, 0x1p46,
        0x1p47, 0x1p48, 0x1p49, 0x1p50, 0x1p51, 0x1p52, 0x1p53, 0x1p54, 0x1p55,
        0x1p56, 0x1p57, 0x1p58, 0x1p59, 0x1p60, 0x1p61, 0x1p62, 0x1p63, 0x1p64
    };

    if (k >= -64 && k <= 64)
        return eprc_pow2_tab[k + 64];
    return pow(2.0, (double)k);
}

#endif
