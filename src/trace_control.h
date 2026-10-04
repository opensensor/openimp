#ifndef OPENIMP_TRACE_CONTROL_H
#define OPENIMP_TRACE_CONTROL_H

#include <stdio.h>
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

/* OPENIMP_STARTUP_TRACE, looked up once: EncoderInit (and with it the
 * startup markers) also runs from per-frame entry points such as
 * IMP_Encoder_PollingModuleStream. */
static inline int openimp_startup_trace_enabled(void)
{
    static int enabled = -1;

    if (enabled < 0)
        enabled = getenv("OPENIMP_STARTUP_TRACE") != NULL;
    return enabled;
}

/*
 * Bench-only stderr chatter (per-frame buffer traffic, per-(re)start setup
 * dumps).  Same OPENIMP_DEBUG_TRACE switch as the kmsg probes; genuine
 * errors and one-shot summaries keep using plain fprintf(stderr, ...).
 */
#define OPENIMP_TRACE_STDERR(...) do {                            \
    if (openimp_debug_trace_enabled())                            \
        fprintf(stderr, __VA_ARGS__);                             \
} while (0)

/*
 * Rate limit for log lines that a persistent fault would repeat per frame
 * (a failing DQBUF/QBUF retried every millisecond, a caller passing a bad
 * handle in its loop).  One static counter per call site: the first 8 lines
 * and then every 1024th go through, so the cause is in the log without the
 * log filling stderr/syslog and stealing CPU.  Cost: one relaxed atomic add.
 *
 *   static unsigned int n;
 *   if (OPENIMP_LOG_ALLOW(&n)) fprintf(stderr, ...);
 */
static inline int openimp_log_allow(unsigned int *counter)
{
    unsigned int n = __atomic_add_fetch(counter, 1u, __ATOMIC_RELAXED);

    return n <= 8u || (n & 1023u) == 0u;
}

#define OPENIMP_LOG_ALLOW(counter) openimp_log_allow(counter)

#endif /* OPENIMP_TRACE_CONTROL_H */
