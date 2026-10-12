/*
 * imp_log_emit.c -- out-of-line back end of IMP_LOG() (imp_log_int.h).
 *
 * The call site only compares the priority with openimp_log_level; this one
 * function formats the line and writes it to syslog and stderr, so no call
 * site carries getenv, openlog or two printf expansions.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "../imp_log_int.h"

int openimp_log_level = IMP_LOG_LEVEL_DEFAULT;

void imp_log_level_reload(void)
{
    const char *trace = getenv("OPENIMP_DEBUG_TRACE");

    openimp_log_level = imp_log_level_parse(getenv("OPENIMP_LOG_LEVEL"),
                                            trace && trace[0] != '\0' &&
                                            trace[0] != '0');
}

__attribute__((constructor)) static void imp_log_level_init(void)
{
    imp_log_level_reload();
}

void imp_log_emit(int prio, const char *tag, const char *fmt, ...)
{
    static int inited;
    char buf[512];
    va_list ap;
    size_t len;
    int n;

    n = snprintf(buf, sizeof(buf), "[%s] ", tag);
    if (n < 0 || (size_t)n >= sizeof(buf))
        n = 0;
    va_start(ap, fmt);
    vsnprintf(buf + n, sizeof(buf) - (size_t)n, fmt, ap);
    va_end(ap);

    if (!inited) {
        openlog("libimp", LOG_PID | LOG_NDELAY, LOG_USER);
        inited = 1;
    }
    syslog(prio, "%s", buf);
    len = strlen(buf);
    if (len > sizeof(buf) - 2)
        len = sizeof(buf) - 2;
    buf[len] = '\n';
    buf[len + 1] = '\0';
    fputs(buf, stderr);
}
