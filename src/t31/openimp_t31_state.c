/*
 * T31 platform state required by the stock ISP/FrameSource adapter.
 *
 * Encoding, rate control, and stream ownership live in the shared T40-derived
 * implementation. Keep this file limited to the small ABI seam that differs
 * on the stock T31 kernel.
 */

#include <stdint.h>
#include <time.h>

#include "core/globals.h"

FrameSourceState *gFrameSource;
ISPDevice *gISP;
Module *g_modules[6][IMP_MAX_GROUPS];

/* group.c uses this symbol only as the historical end marker in a legacy
 * clear helper. The shared System lifecycle does not call that helper. */
uint32_t g_block_info_addr;

uint64_t system_gettime(int clock_type)
{
    struct timespec now;
    clockid_t clock_id = clock_type == 0 ? CLOCK_REALTIME : CLOCK_MONOTONIC;

    if (clock_gettime(clock_id, &now) != 0)
        return 0;
    return (uint64_t)now.tv_sec * 1000000u +
           (uint64_t)now.tv_nsec / 1000u;
}

int32_t get_cpu_id(void)
{
#if defined(PLATFORM_T23)
    return 0x0f; /* T23-N */
#else
    return 0x15; /* T31X */
#endif
}

int32_t is_has_simd128(void)
{
    return 0;
}
