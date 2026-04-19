/**
 * Sysutils Base Module Implementation
 *
 * Also hosts minimal stubs for the logging functions that every ported
 * libimp translation unit forward-declares but leaves undefined
 * (`imp_log_fun`, `IMP_Log_Get_Option`). In the stock firmware these
 * resolve from libsysutils.so at runtime; this file provides a
 * self-contained fallback so the ported shared library is loadable
 * even without the stock libsysutils.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysutils/su_base.h>
#include <syslog.h>

#define SU_VERSION "1.0.0"

int SU_Base_GetVersion(SUVersion *version) {
    if (version == NULL) {
        return -1;
    }

    snprintf(version->chr, sizeof(version->chr), "SU-%s", SU_VERSION);
    return 0;
}

/* ----- imp_log shim ----------------------------------------------------
 *
 * The binary routes all log calls through these two functions. In the
 * stock binary they are PLT stubs imported from libsysutils. Our stub
 * reads IMP_LOG env var (hex mask) once and writes via syslog + stderr.
 *
 * imp_log_fun signature (as used throughout the port):
 *   imp_log_fun(level, option, type, module, file, line, func, fmt, ...)
 *
 * Level map (observed in the binary):
 *   3 = DEBUG, 4 = INFO, 5 = WARNING, 6 = ERROR
 */

static int imp_log_option_cached = -1;

int IMP_Log_Get_Option(void)
{
    if (imp_log_option_cached == -1) {
        const char *env = getenv("IMP_LOG");
        if (env && *env) {
            imp_log_option_cached = (int)strtol(env, NULL, 0);
        } else {
            imp_log_option_cached = 0x20; /* default: errors only */
        }
    }
    return imp_log_option_cached;
}

static int imp_log_level_to_syslog(int level)
{
    switch (level) {
    case 3:  return LOG_DEBUG;
    case 4:  return LOG_INFO;
    case 5:  return LOG_WARNING;
    case 6:  return LOG_ERR;
    default: return LOG_NOTICE;
    }
}

void imp_log_fun(int level, int option, int type,
                 const char *module, const char *file, int line,
                 const char *func, const char *fmt, ...)
{
    static int opened = 0;
    char buf[512];
    va_list ap;

    (void)option;
    (void)type;
    (void)file;
    (void)line;
    (void)func;

    if (!opened) {
        openlog("libimp", LOG_PID | LOG_NDELAY, LOG_USER);
        opened = 1;
    }

    if (fmt == NULL) fmt = "";
    if (module == NULL) module = "?";

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    syslog(imp_log_level_to_syslog(level), "[%s] %s", module, buf);
    if (level >= 5) {
        fprintf(stderr, "[%s] %s", module, buf);
        if (buf[0] == '\0' || buf[strlen(buf) - 1] != '\n') fputc('\n', stderr);
    }
}
