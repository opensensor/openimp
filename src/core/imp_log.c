/*
 * imp_log.c -- built-in implementation of the two logging symbols the OEM
 * libimp imports from libalog.so / libsysutils.so:
 *
 *   int IMP_Log_Get_Option(void);
 *   int imp_log_fun(int level, int option, int type, const char *tag,
 *                   const char *file, int line, const char *func,
 *                   const char *fmt, ...);
 *
 * OpenIMP's own code calls these (OEM call shape), so exporting them from
 * libimp.so removes the need for any vendor libalog.so / libsysutils.so.
 *
 * Levels (OEM): 3 DBG, 4 INFO, 5 WARN, 6 ERR.  WARN/ERR always go to
 * stderr + syslog; DBG/INFO only when OPENIMP_DEBUG_TRACE is set.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include "../trace_control.h"

#define IMP_LOG_OPT_STDERR 0x1
#define IMP_LOG_OPT_SYSLOG 0x2

int IMP_Log_Get_Option(void)
{
    return IMP_LOG_OPT_STDERR | IMP_LOG_OPT_SYSLOG;
}

int imp_log_fun(int level, int option, int type, ...)
{
    va_list ap;
    const char *tag, *file, *func, *fmt, *base;
    int line, prio, n;
    char buf[512];
    static int inited;

    (void)type;
    if (level < 5 && !openimp_debug_trace_enabled())
        return 0;

    va_start(ap, type);
    tag = va_arg(ap, const char *);
    file = va_arg(ap, const char *);
    line = va_arg(ap, int);
    func = va_arg(ap, const char *);
    fmt = va_arg(ap, const char *);
    if (!fmt) {
        va_end(ap);
        return 0;
    }
    base = file ? strrchr(file, '/') : NULL;
    base = base ? base + 1 : (file ? file : "?");
    n = snprintf(buf, sizeof(buf), "[%s] %s:%d %s: ", tag ? tag : "IMP",
                 base, line, func ? func : "?");
    if (n < 0 || n >= (int)sizeof(buf))
        n = 0;
    vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
    va_end(ap);

    if (option & IMP_LOG_OPT_STDERR) {
        fputs(buf, stderr);
        if (buf[strlen(buf) - 1] != '\n')
            fputc('\n', stderr);
    }
    if (option & IMP_LOG_OPT_SYSLOG) {
        if (!inited) {
            openlog("libimp", LOG_PID | LOG_NDELAY, LOG_USER);
            inited = 1;
        }
        prio = level >= 6 ? LOG_ERR : level == 5 ? LOG_WARNING
             : level == 4 ? LOG_INFO : LOG_DEBUG;
        syslog(prio, "%s", buf);
    }
    return 0;
}
