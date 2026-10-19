/* Runtime control of the test pattern (include/openimp_testpattern.h) as seen by the frame hooks.
 * The frame path takes no lock: openimp_testpattern_mode() is one relaxed atomic load. */
#ifndef OPENIMP_TESTPATTERN_RT_H
#define OPENIMP_TESTPATTERN_RT_H

#include <stdint.h>

extern int openimp_tp_mode_v;

/* 0 = off, 1 = static pattern, 2 = + moving element, 3 = motion detection test */
static inline int openimp_testpattern_mode(void)
{
    return __atomic_load_n(&openimp_tp_mode_v, __ATOMIC_RELAXED);
}

/* per hooked channel, owned by the hook */
struct openimp_tp_chan {
    uint8_t *cache;                   /* rendered static pattern */
    uint32_t w, h, pitch, lines, logo, epoch, counter;
};

/* Render the picture of the current mode into dst (NV12, luma pitch*lines, chroma after it).
 * want3 = 0: modes 1/2 (static pattern memcpy + overlay); want3 = 1: mode 3 only.
 * Returns 1 when dst was written (the caller then flushes the cache), 0 when the mode does not match. */
int openimp_tp_render(struct openimp_tp_chan *c, uint8_t *dst, uint32_t w, uint32_t h,
                      uint32_t pitch, uint32_t lines, int want3);

#endif
