#ifndef OPENIMP_TRACE_CONTROL_H
#define OPENIMP_TRACE_CONTROL_H

#include <stdlib.h>

/*
 * The reverse-engineering traces are useful on the bench, but opening,
 * writing, and closing /dev/kmsg several times per captured frame is not a
 * production-safe default on the single-core camera SoCs.  Keep the probes
 * available without putting them on the hot path unless explicitly enabled.
 */
static inline int openimp_debug_trace_enabled(void)
{
    static int enabled = -1;

    if (enabled < 0) {
        const char *value = getenv("OPENIMP_DEBUG_TRACE");

        enabled = value != NULL && value[0] != '\0' && value[0] != '0';
    }
    return enabled;
}

#endif /* OPENIMP_TRACE_CONTROL_H */
