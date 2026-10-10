/*
 * imp_log_int.h -- Internal logging for OpenIMP
 *
 * Uses syslog so output survives daemon(3) / fork+close(stderr).
 * Also writes to stderr when available (e.g. foreground mode).
 *
 * Usage: #include "imp_log_int.h"
 *        IMP_LOG(LOG_INFO, "ISP", "Opened device fd=%d", fd);
 */

#ifndef IMP_LOG_INT_H
#define IMP_LOG_INT_H

#include <stdio.h>
#include <syslog.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "trace_control.h"

/* One-time syslog init (safe to call multiple times) */
static inline void imp_log_ensure_init(void) {
    static int inited = 0;
    if (!inited) {
        openlog("libimp", LOG_PID | LOG_NDELAY, LOG_USER);
        inited = 1;
    }
}

/*
 * Runtime log level (syslog numbering 0..7, 5 = LOG_NOTICE).  A line is
 * written only when its priority is <= the level.
 *   OPENIMP_LOG_LEVEL=n  0..7; unset or invalid = LOG_NOTICE, so production
 *                        cameras log errors, warnings and one-shot status lines
 *   OPENIMP_DEBUG_TRACE  set (not "0"): LOG_DEBUG, everything as before
 * imp_log_level_parse() is the pure rule (host-tested); imp_log_level() caches
 * it in a static int, so the per-call cost is one load and one compare.
 */
#define IMP_LOG_LEVEL_DEFAULT LOG_NOTICE

static inline int imp_log_level_parse(const char *env, int trace)
{
    if (trace)
        return LOG_DEBUG;
    if (env && env[0] >= '0' && env[0] <= '7' && env[1] == '\0')
        return env[0] - '0';
    return IMP_LOG_LEVEL_DEFAULT;
}

static inline int imp_log_level(void)
{
    static int level = -1;

    if (level < 0)
        level = imp_log_level_parse(getenv("OPENIMP_LOG_LEVEL"),
                                    openimp_debug_trace_enabled());
    return level;
}

/*
 * IMP_LOG(priority, tag, fmt, ...)
 *   priority: LOG_ERR, LOG_WARNING, LOG_NOTICE, LOG_INFO, LOG_DEBUG
 *   tag:      short module name, e.g. "ISP", "System", "Encoder"
 * Dropped silently (no syslog, no stderr) above the runtime level.
 */
#define IMP_LOG(prio, tag, fmt, ...) do {                         \
    if ((prio) <= imp_log_level()) {                              \
        imp_log_ensure_init();                                    \
        syslog((prio), "[%s] " fmt, (tag), ##__VA_ARGS__);        \
        fprintf(stderr, "[%s] " fmt "\n", (tag), ##__VA_ARGS__);  \
    }                                                             \
} while (0)

/*
 * OEM-compatible logging macros matching imp_log_fun() severity levels:
 *   Level 3 = DBG, Level 5 = WARN, Level 6 = ERR
 */
#define IMP_LOG_DBG(tag, fmt, ...)  IMP_LOG(LOG_DEBUG,   tag, fmt, ##__VA_ARGS__)
#define IMP_LOG_WARN(tag, fmt, ...) IMP_LOG(LOG_WARNING, tag, fmt, ##__VA_ARGS__)
#define IMP_LOG_ERR(tag, fmt, ...)  IMP_LOG(LOG_ERR,     tag, fmt, ##__VA_ARGS__)
#define IMP_LOG_INFO(tag, fmt, ...) IMP_LOG(LOG_INFO,    tag, fmt, ##__VA_ARGS__)

/*
 * IMP_LOG_LIMITED(prio, tag, fmt, ...): IMP_LOG for a line a caller bug or a
 * driver fault can repeat on every call or frame; per call site the first 8
 * and then every 1024th line are written (see openimp_log_allow()).
 */
#define IMP_LOG_LIMITED(prio, tag, fmt, ...) do {                 \
    static unsigned int imp_log_limited_count;                    \
    if (openimp_log_allow(&imp_log_limited_count))                \
        IMP_LOG((prio), tag, fmt, ##__VA_ARGS__);                 \
} while (0)

/*
 * Codec, hardware, and DMA messages are reverse-engineering traces.  Some of
 * them run once or more per encoded frame, so sending them to syslog and
 * stderr by default can steal meaningful encode time on the single-core T31.
 */
#define IMP_TRACE_LOG(tag, fmt, ...) do {                         \
    if (openimp_debug_trace_enabled()) {                          \
        IMP_LOG(LOG_INFO, tag, fmt, ##__VA_ARGS__);               \
    }                                                             \
} while (0)

/* Convenience per-module macros -- drop-in replacements */
#define LOG_ISP(fmt, ...)    IMP_LOG(LOG_INFO, "IMP_ISP", fmt, ##__VA_ARGS__)
#define LOG_SYS(fmt, ...)    IMP_LOG(LOG_INFO, "System",  fmt, ##__VA_ARGS__)
#define LOG_ENC(fmt, ...)    IMP_LOG(LOG_INFO, "Encoder", fmt, ##__VA_ARGS__)
#define LOG_FS(fmt, ...)     IMP_LOG(LOG_INFO, "FS",      fmt, ##__VA_ARGS__)
#define LOG_AUD(fmt, ...)    IMP_LOG(LOG_INFO, "Audio",   fmt, ##__VA_ARGS__)
#define LOG_OSD(fmt, ...)    IMP_LOG(LOG_INFO, "OSD",     fmt, ##__VA_ARGS__)
#define LOG_IVS(fmt, ...)    IMP_LOG(LOG_INFO, "IVS",     fmt, ##__VA_ARGS__)
#define LOG_CODEC(fmt, ...)  IMP_TRACE_LOG("Codec",  fmt, ##__VA_ARGS__)
#define LOG_HW(fmt, ...)     IMP_TRACE_LOG("HW_Enc", fmt, ##__VA_ARGS__)
#define LOG_DMA(fmt, ...)    IMP_TRACE_LOG("DMA",    fmt, ##__VA_ARGS__)

#endif /* IMP_LOG_INT_H */
