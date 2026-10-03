#ifndef OPENIMP_EPRC_INTERNAL_H
#define OPENIMP_EPRC_INTERNAL_H

#include <stdint.h>
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

#endif
