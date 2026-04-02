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

/* One-time syslog init (safe to call multiple times) */
static inline void imp_log_ensure_init(void) {
    static int inited = 0;
    if (!inited) {
        openlog("libimp", LOG_PID | LOG_NDELAY, LOG_USER);
        inited = 1;
    }
}

/*
 * IMP_LOG(priority, tag, fmt, ...)
 *   priority: LOG_ERR, LOG_WARNING, LOG_INFO, LOG_DEBUG
 *   tag:      short module name, e.g. "ISP", "System", "Encoder"
 */
#define IMP_LOG(prio, tag, fmt, ...) do {                         \
    imp_log_ensure_init();                                        \
    syslog((prio), "[%s] " fmt, (tag), ##__VA_ARGS__);            \
    fprintf(stderr, "[%s] " fmt "\n", (tag), ##__VA_ARGS__);      \
} while (0)

/* Convenience per-module macros -- drop-in replacements */
#define LOG_ISP(fmt, ...)    IMP_LOG(LOG_INFO, "IMP_ISP", fmt, ##__VA_ARGS__)
#define LOG_SYS(fmt, ...)    IMP_LOG(LOG_INFO, "System",  fmt, ##__VA_ARGS__)
#define LOG_ENC(fmt, ...)    IMP_LOG(LOG_INFO, "Encoder", fmt, ##__VA_ARGS__)
#define LOG_FS(fmt, ...)     IMP_LOG(LOG_INFO, "FS",      fmt, ##__VA_ARGS__)
#define LOG_AUD(fmt, ...)    IMP_LOG(LOG_INFO, "Audio",   fmt, ##__VA_ARGS__)
#define LOG_OSD(fmt, ...)    IMP_LOG(LOG_INFO, "OSD",     fmt, ##__VA_ARGS__)
#define LOG_IVS(fmt, ...)    IMP_LOG(LOG_INFO, "IVS",     fmt, ##__VA_ARGS__)
#define LOG_CODEC(fmt, ...)  IMP_LOG(LOG_INFO, "Codec",   fmt, ##__VA_ARGS__)
#define LOG_HW(fmt, ...)     IMP_LOG(LOG_INFO, "HW_Enc",  fmt, ##__VA_ARGS__)
#define LOG_DMA(fmt, ...)    IMP_LOG(LOG_INFO, "DMA",     fmt, ##__VA_ARGS__)

#endif /* IMP_LOG_INT_H */
