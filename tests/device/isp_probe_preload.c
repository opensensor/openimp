/*
 * isp_probe_preload - LD_PRELOAD shim that lets a tester drive the
 * IMP_ISP_Tuning_* calls of a running streamer (timps, prudynt, ...) from
 * the shell, on T20, T21, T23 and T31 (OpenIMP or vendor libimp).
 *
 * A thread started from the library constructor creates the named pipe
 * /tmp/ispprobe.fifo (ISPPROBE_FIFO overrides it) and waits for command
 * lines there; nothing else is touched until a command arrives. Every
 * command is logged with the return value and, where there is a getter,
 * the value read back, to /tmp/ispprobe.log (ISPPROBE_LOG) and stderr.
 * The functions are looked up with dlsym at the time of the command, so a
 * function the library does not have is reported as "not available".
 * Commands run in the probe thread, i.e. concurrently with the streamer's
 * own calls - as an application thread would.
 *
 * Commands (numbers decimal or 0x hex):
 *   help
 *   brightness|contrast|saturation|sharpness N      (0..255, default 128)
 *   aecomp N            maxagain N         maxdgain N
 *   sinter N            temper N           (strength ratios, 100 = default)
 *   runningmode day|night|N
 *   colorfx N           (0 auto, 1 bw, 2 sepia, 3 negative, 9 vivid)
 *   scene N             (0 auto, 8 night, 11 sports, ...)
 *   wb MODE [RGAIN BGAIN]  (0 auto, 1 manual, 2..8 presets, 9 custom)
 *   rawdrc MODE [STRENGTH] (0 manual, 1 unlimit, ..., 5 disable)
 *   sinterattr auto|manual [STRENGTH]
 *   temperattr disable|auto|manual [STRENGTH]
 *   temperctl  disable|auto|manual [STRENGTH]
 *   get NAME            (any of the names above, also "get all")
 *
 * Build (any Thingino mipsel toolchain; nothing is linked but libc):
 *   $CC -std=gnu99 -O2 -Wall -fPIC -shared -o ispprobe.so \
 *       isp_probe_preload.c -ldl -lpthread
 * Run on the camera:
 *   /etc/init.d/S95timps stop
 *   LD_PRELOAD=/tmp/ispprobe.so timpsd -c /etc/timps.conf &
 *   echo "colorfx 2" > /tmp/ispprobe.fifo; cat /tmp/ispprobe.log
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* Vendor ABI (T20/T21/T23/T31 imp_isp.h). */
typedef struct {
    uint32_t mode;
    uint16_t rgain;
    uint16_t bgain;
} ProbeWB;

typedef struct {
    uint32_t mode;
    uint8_t drc_strength;
    uint8_t slop_max;
    uint8_t slop_min;
    uint16_t black_level;
    uint16_t white_level;
} ProbeDrc;

typedef struct {
    uint32_t enable;
    uint32_t type;
    uint8_t sinter_strength;
} ProbeSinter;

typedef struct {
    uint32_t type;
    uint8_t temper_strength;
} ProbeTemper;

enum kind {
    K_U8,       /* Set(unsigned char), Get(unsigned char *) */
    K_INT,      /* Set(int / uint32_t), Get(int * / uint32_t *) */
    K_WB,
    K_DRC,
    K_SINTER,
    K_TEMPER,
};

struct probe_cmd {
    const char *name;
    const char *set;
    const char *get;
    enum kind kind;
};

static const struct probe_cmd probe_cmds[] = {
    { "brightness", "IMP_ISP_Tuning_SetBrightness", "IMP_ISP_Tuning_GetBrightness", K_U8 },
    { "contrast", "IMP_ISP_Tuning_SetContrast", "IMP_ISP_Tuning_GetContrast", K_U8 },
    { "saturation", "IMP_ISP_Tuning_SetSaturation", "IMP_ISP_Tuning_GetSaturation", K_U8 },
    { "sharpness", "IMP_ISP_Tuning_SetSharpness", "IMP_ISP_Tuning_GetSharpness", K_U8 },
    { "aecomp", "IMP_ISP_Tuning_SetAeComp", "IMP_ISP_Tuning_GetAeComp", K_INT },
    { "maxagain", "IMP_ISP_Tuning_SetMaxAgain", "IMP_ISP_Tuning_GetMaxAgain", K_INT },
    { "maxdgain", "IMP_ISP_Tuning_SetMaxDgain", "IMP_ISP_Tuning_GetMaxDgain", K_INT },
    { "sinter", "IMP_ISP_Tuning_SetSinterStrength", "IMP_ISP_Tuning_GetSinterStrength", K_INT },
    { "temper", "IMP_ISP_Tuning_SetTemperStrength", "IMP_ISP_Tuning_GetTemperStrength", K_INT },
    { "runningmode", "IMP_ISP_Tuning_SetISPRunningMode", "IMP_ISP_Tuning_GetISPRunningMode", K_INT },
    { "colorfx", "IMP_ISP_Tuning_SetColorfxMode", "IMP_ISP_Tuning_GetColorfxMode", K_INT },
    { "scene", "IMP_ISP_Tuning_SetSceneMode", "IMP_ISP_Tuning_GetSceneMode", K_INT },
    { "wb", "IMP_ISP_Tuning_SetWB", "IMP_ISP_Tuning_GetWB", K_WB },
    { "rawdrc", "IMP_ISP_Tuning_SetRawDRC", "IMP_ISP_Tuning_GetRawDRC", K_DRC },
    { "sinterattr", "IMP_ISP_Tuning_SetSinterDnsAttr", "IMP_ISP_Tuning_GetSinterDnsAttr", K_SINTER },
    { "temperattr", "IMP_ISP_Tuning_SetTemperDnsAttr", "IMP_ISP_Tuning_GetTemperDnsAttr", K_TEMPER },
    { "temperctl", "IMP_ISP_Tuning_SetTemperDnsCtl", "IMP_ISP_Tuning_GetTemperDnsAttr", K_TEMPER },
};

#define NCMDS (sizeof(probe_cmds) / sizeof(probe_cmds[0]))

static FILE *probe_log_file;
static pthread_mutex_t probe_log_lock = PTHREAD_MUTEX_INITIALIZER;

static void probe_log(const char *format, ...)
    __attribute__((format(printf, 1, 2)));
static void probe_log(const char *format, ...)
{
    char line[512];
    struct timespec ts;
    va_list ap;

    va_start(ap, format);
    vsnprintf(line, sizeof(line), format, ap);
    va_end(ap);
    clock_gettime(CLOCK_MONOTONIC, &ts);
    pthread_mutex_lock(&probe_log_lock);
    if (probe_log_file) {
        fprintf(probe_log_file, "[%ld.%03ld] %s\n", (long)ts.tv_sec,
                ts.tv_nsec / 1000000L, line);
        fflush(probe_log_file);
    }
    fprintf(stderr, "ispprobe: %s\n", line);
    pthread_mutex_unlock(&probe_log_lock);
}

static void *probe_sym(const char *name)
{
    return name ? dlsym(RTLD_DEFAULT, name) : NULL;
}

static int probe_number(const char *text, long *value)
{
    char *end;

    if (!text)
        return -1;
    errno = 0;
    *value = strtol(text, &end, 0);
    return errno || end == text || *end ? -1 : 0;
}

/* auto/manual/disable or a number */
static int probe_mode(const char *text, int temper, long *value)
{
    if (!text)
        return -1;
    if (!strcasecmp(text, "disable")) {
        *value = 0;
        return temper ? 0 : -1;
    }
    if (!strcasecmp(text, "auto")) {
        *value = temper ? 1 : 0;
        return 0;
    }
    if (!strcasecmp(text, "manual")) {
        *value = temper ? 2 : 1;
        return 0;
    }
    return probe_number(text, value);
}

static const struct probe_cmd *probe_find(const char *name)
{
    size_t i;

    for (i = 0; i < NCMDS; i++)
        if (!strcasecmp(name, probe_cmds[i].name))
            return &probe_cmds[i];
    return NULL;
}

static void probe_get(const struct probe_cmd *c)
{
    void *fn = probe_sym(c->get);
    int ret;

    if (!fn) {
        probe_log("get %s: %s not available", c->name, c->get);
        return;
    }
    switch (c->kind) {
    case K_U8: {
        unsigned char v = 0;

        ret = ((int (*)(unsigned char *))fn)(&v);
        probe_log("get %s: ret=%d value=%u", c->name, ret, v);
        break;
    }
    case K_INT: {
        int32_t v = 0;

        ret = ((int (*)(int32_t *))fn)(&v);
        probe_log("get %s: ret=%d value=%d (0x%x)", c->name, ret, (int)v,
                  (unsigned int)v);
        break;
    }
    case K_WB: {
        ProbeWB v;

        memset(&v, 0, sizeof(v));
        ret = ((int (*)(ProbeWB *))fn)(&v);
        probe_log("get %s: ret=%d mode=%u rgain=%u bgain=%u", c->name, ret,
                  v.mode, v.rgain, v.bgain);
        break;
    }
    case K_DRC: {
        ProbeDrc v;

        memset(&v, 0, sizeof(v));
        ret = ((int (*)(ProbeDrc *))fn)(&v);
        probe_log("get %s: ret=%d mode=%u strength=%u slop_max=%u "
                  "slop_min=%u black=%u white=%u", c->name, ret, v.mode,
                  v.drc_strength, v.slop_max, v.slop_min, v.black_level,
                  v.white_level);
        break;
    }
    case K_SINTER: {
        ProbeSinter v;

        memset(&v, 0, sizeof(v));
        ret = ((int (*)(ProbeSinter *))fn)(&v);
        probe_log("get %s: ret=%d enable=%u type=%u strength=%u", c->name,
                  ret, v.enable, v.type, v.sinter_strength);
        break;
    }
    case K_TEMPER: {
        ProbeTemper v;

        memset(&v, 0, sizeof(v));
        ret = ((int (*)(ProbeTemper *))fn)(&v);
        probe_log("get %s: ret=%d type=%u strength=%u", c->name, ret,
                  v.type, v.temper_strength);
        break;
    }
    }
}

static void probe_set(const struct probe_cmd *c, char **arg, int nargs)
{
    void *fn = probe_sym(c->set);
    long a = 0, b = 0, d = 0;
    int ret;

    if (!fn) {
        probe_log("%s: %s not available", c->name, c->set);
        return;
    }
    switch (c->kind) {
    case K_U8:
        if (nargs < 1 || probe_number(arg[0], &a) || a < 0 || a > 255)
            goto usage;
        ret = ((int (*)(unsigned char))fn)((unsigned char)a);
        probe_log("%s %ld: ret=%d", c->name, a, ret);
        break;
    case K_INT:
        if (nargs < 1)
            goto usage;
        if (!strcmp(c->name, "runningmode") && !strcasecmp(arg[0], "day"))
            a = 0;
        else if (!strcmp(c->name, "runningmode") &&
                 !strcasecmp(arg[0], "night"))
            a = 1;
        else if (probe_number(arg[0], &a))
            goto usage;
        ret = ((int (*)(int32_t))fn)((int32_t)a);
        probe_log("%s %ld: ret=%d", c->name, a, ret);
        break;
    case K_WB: {
        ProbeWB v;

        if (nargs < 1 || probe_number(arg[0], &a) ||
            (nargs >= 3 && (probe_number(arg[1], &b) ||
                            probe_number(arg[2], &d))))
            goto usage;
        memset(&v, 0, sizeof(v));
        v.mode = (uint32_t)a;
        v.rgain = (uint16_t)b;
        v.bgain = (uint16_t)d;
        ret = ((int (*)(ProbeWB *))fn)(&v);
        probe_log("%s mode=%ld rgain=%ld bgain=%ld: ret=%d", c->name, a, b,
                  d, ret);
        break;
    }
    case K_DRC: {
        ProbeDrc v;

        if (nargs < 1 || probe_number(arg[0], &a) ||
            (nargs >= 2 && probe_number(arg[1], &b)))
            goto usage;
        memset(&v, 0, sizeof(v));
        v.mode = (uint32_t)a;
        v.drc_strength = (uint8_t)b;
        v.slop_max = 128;
        v.slop_min = 128;
        v.white_level = 0xfff;
        ret = ((int (*)(ProbeDrc *))fn)(&v);
        probe_log("%s mode=%ld strength=%ld: ret=%d", c->name, a, b, ret);
        break;
    }
    case K_SINTER: {
        ProbeSinter v;

        if (nargs < 1 || probe_mode(arg[0], 0, &a) ||
            (nargs >= 2 && probe_number(arg[1], &b)))
            goto usage;
        memset(&v, 0, sizeof(v));
        v.enable = 1;
        v.type = (uint32_t)a;
        v.sinter_strength = (uint8_t)b;
        ret = ((int (*)(ProbeSinter *))fn)(&v);
        probe_log("%s type=%ld strength=%ld: ret=%d", c->name, a, b, ret);
        break;
    }
    case K_TEMPER: {
        ProbeTemper v;

        if (nargs < 1 || probe_mode(arg[0], 1, &a) ||
            (nargs >= 2 && probe_number(arg[1], &b)))
            goto usage;
        memset(&v, 0, sizeof(v));
        v.type = (uint32_t)a;
        v.temper_strength = (uint8_t)b;
        ret = ((int (*)(ProbeTemper *))fn)(&v);
        probe_log("%s type=%ld strength=%ld: ret=%d", c->name, a, b, ret);
        break;
    }
    }
    probe_get(c);
    return;

usage:
    probe_log("%s: bad arguments (try \"help\")", c->name);
}

static void probe_help(void)
{
    size_t i;

    for (i = 0; i < NCMDS; i++)
        probe_log("  %-11s set %s%s, get %s%s", probe_cmds[i].name,
                  probe_cmds[i].set,
                  probe_sym(probe_cmds[i].set) ? "" : " (not available)",
                  probe_cmds[i].get,
                  probe_sym(probe_cmds[i].get) ? "" : " (not available)");
}

/* One command line; exported for the host test. */
void ispprobe_run_line(char *line);
void ispprobe_run_line(char *line)
{
    char *arg[8];
    int n = 0;
    char *save = NULL, *tok;

    for (tok = strtok_r(line, " \t\r\n", &save); tok && n < 8;
         tok = strtok_r(NULL, " \t\r\n", &save))
        arg[n++] = tok;
    if (!n || arg[0][0] == '#')
        return;
    if (!strcasecmp(arg[0], "help")) {
        probe_help();
    } else if (!strcasecmp(arg[0], "get")) {
        size_t i;

        if (n >= 2 && !strcasecmp(arg[1], "all")) {
            for (i = 0; i < NCMDS; i++)
                probe_get(&probe_cmds[i]);
        } else if (n >= 2 && probe_find(arg[1])) {
            probe_get(probe_find(arg[1]));
        } else {
            probe_log("get: unknown name %s", n >= 2 ? arg[1] : "(none)");
        }
    } else if (probe_find(arg[0])) {
        probe_set(probe_find(arg[0]), arg + 1, n - 1);
    } else {
        probe_log("unknown command %s (try \"help\")", arg[0]);
    }
}

static void *probe_thread(void *unused)
{
    const char *path = getenv("ISPPROBE_FIFO");
    char line[256];

    (void)unused;
    if (!path || !*path)
        path = "/tmp/ispprobe.fifo";
    if (mkfifo(path, 0666) != 0 && errno != EEXIST) {
        probe_log("cannot create %s: %s", path, strerror(errno));
        return NULL;
    }
    probe_log("ready, commands to %s", path);
    for (;;) {
        FILE *in = fopen(path, "r");    /* blocks until a writer opens */

        if (!in) {
            probe_log("cannot open %s: %s", path, strerror(errno));
            sleep(1);
            continue;
        }
        while (fgets(line, sizeof(line), in)) {
            line[strcspn(line, "\r\n")] = '\0';
            probe_log("> %s", line);
            ispprobe_run_line(line);
        }
        fclose(in);
    }
    return NULL;
}

__attribute__((constructor))
static void probe_init(void)
{
    const char *log = getenv("ISPPROBE_LOG");
    pthread_t thread;
    pthread_attr_t attr;

    if (getenv("ISPPROBE_DISABLE"))
        return;
    if (!log || !*log)
        log = "/tmp/ispprobe.log";
    probe_log_file = fopen(log, "a");
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&thread, &attr, probe_thread, NULL) != 0)
        probe_log("cannot start the probe thread");
    pthread_attr_destroy(&attr);
}
