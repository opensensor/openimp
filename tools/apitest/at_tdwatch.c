/* Teardown stall watch, see at_tdwatch.h (issue #198).  No vendor headers: also built by the host test. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "at_tdwatch.h"

/* OpenIMP only (framesource_tseries.c); absent on the vendor libimp. */
extern unsigned int OpenIMP_FS_DebugState(void) __attribute__((weak));
extern const char *OpenIMP_FS_DebugHolder(void) __attribute__((weak));
extern unsigned int OpenIMP_FS_DebugLockOwner(void) __attribute__((weak));

static pthread_t g_th;
static int g_armed;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv;
static int g_stop;
static const char *g_label;
static const char *volatile g_step;
static volatile unsigned int g_step_ms;          /* ms since arm when the step was set */
static struct timespec g_t0;
static long g_limit_ms;
static char g_dir[256] = "/tmp";
static int g_reported;                            /* one block per process */
static char g_path[300];

static long ms_since(const struct timespec *a, const struct timespec *b)
{
    return (long)((b->tv_sec - a->tv_sec) * 1000 + (b->tv_nsec - a->tv_nsec) / 1000000);
}

static unsigned int ms_now(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned int)ms_since(&g_t0, &t);
}

void at_td_init(const char *json_path)
{
    const char *sl;

    if (!json_path) return;
    sl = strrchr(json_path, '/');
    if (sl && sl != json_path) {
        size_t n = (size_t)(sl - json_path);

        if (n < sizeof(g_dir)) { memcpy(g_dir, json_path, n); g_dir[n] = 0; }
    } else if (!sl) {
        strcpy(g_dir, ".");
    }
}

void at_td_step(const char *step)
{
    g_step_ms = ms_now();
    g_step = step;
}

static void slurp(const char *path, char *out, size_t n)
{
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    ssize_t r = -1;
    size_t i;

    out[0] = 0;
    if (fd >= 0) { r = read(fd, out, n - 1); close(fd); }
    if (r <= 0) { snprintf(out, n, "(unreadable)"); return; }
    out[r] = 0;
    for (i = 0; i < (size_t)r; i++) if (out[i] == '\n') out[i] = (i + 1 == (size_t)r) ? 0 : '|';
}

static void emit(FILE *f[2], const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void emit(FILE *f[2], const char *fmt, ...)
{
    char b[1024];
    va_list ap;
    int i;

    va_start(ap, fmt); vsnprintf(b, sizeof(b), fmt, ap); va_end(ap);
    for (i = 0; i < 2; i++) if (f[i]) { fputs(b, f[i]); fflush(f[i]); }
}

static void report(long el)
{
    FILE *f[2];
    struct timespec now;
    DIR *d;
    struct dirent *e;
    unsigned int owner = OpenIMP_FS_DebugLockOwner ? OpenIMP_FS_DebugLockOwner() : 0;
    char p[96], comm[64], stat[400], wch[96], sc[160], stk[400];

    snprintf(g_path, sizeof(g_path), "%s/teardown-stall-%d.txt", g_dir, (int)getpid());
    f[0] = stderr;
    f[1] = fopen(g_path, "w");
    clock_gettime(CLOCK_MONOTONIC, &now);
    emit(f, "\n[E] teardown stall (issue #198): '%s' not finished after %ld ms (limit %ld ms), monotonic %ld.%03ld s\n",
         g_label ? g_label : "?", el, g_limit_ms, (long)now.tv_sec, now.tv_nsec / 1000000);
    emit(f, "[E]   main thread step: %s (entered %u ms after arm, %ld ms ago)\n", g_step ? g_step : "-", g_step_ms,
         el - (long)g_step_ms);
    if (OpenIMP_FS_DebugState) emit(f, "[E]   fs state 0x%08x (per channel byte: worker step << 4 | DisableChn stage)\n", OpenIMP_FS_DebugState());
    else emit(f, "[E]   fs state: n/a (not OpenIMP)\n");
    if (OpenIMP_FS_DebugHolder) {
        const char *h = OpenIMP_FS_DebugHolder();

        emit(f, "[E]   g_fs_lock: %s, holder %s, owner tid %u\n", h ? "LOCKED" : "free", h ? h : "-", owner);
    } else {
        emit(f, "[E]   g_fs_lock: n/a (not OpenIMP)\n");
    }
    d = opendir("/proc/self/task");
    while (d && (e = readdir(d))) {
        unsigned int tid;
        char *rp;
        char st = '?';

        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        tid = (unsigned int)atoi(e->d_name);
        snprintf(p, sizeof(p), "/proc/self/task/%u/comm", tid);    slurp(p, comm, sizeof(comm));
        snprintf(p, sizeof(p), "/proc/self/task/%u/stat", tid);    slurp(p, stat, sizeof(stat));
        snprintf(p, sizeof(p), "/proc/self/task/%u/wchan", tid);   slurp(p, wch, sizeof(wch));
        snprintf(p, sizeof(p), "/proc/self/task/%u/syscall", tid); slurp(p, sc, sizeof(sc));
        snprintf(p, sizeof(p), "/proc/self/task/%u/stack", tid);   slurp(p, stk, sizeof(stk));
        rp = strrchr(stat, ')');
        if (rp && rp[1] == ' ') st = rp[2];
        emit(f, "[E]   task %u%s '%s' state %c wchan %s syscall %s stack %s\n", tid,
             (tid == owner && owner) ? " (g_fs_lock owner)" : (tid == (unsigned int)getpid() ? " (main)" : ""),
             comm, st, wch, sc, stk);
    }
    if (d) closedir(d);
    emit(f, "[E] teardown stall: report ends; waiting on, hard timeout unchanged\n");
    if (f[1]) fclose(f[1]);
    printf("[E] teardown stall report: %s\n", g_path);
    fflush(stdout);
}

static void *watch(void *arg)
{
    struct timespec dl;
    int rc = 0;
    sigset_t all;

    (void)arg;
    sigfillset(&all);
    pthread_sigmask(SIG_BLOCK, &all, NULL);        /* the 60 s alarm lands on the main thread */
    pthread_mutex_lock(&g_mu);
    clock_gettime(CLOCK_REALTIME, &dl);
    dl.tv_sec += g_limit_ms / 1000; dl.tv_nsec += (g_limit_ms % 1000) * 1000000L;
    if (dl.tv_nsec >= 1000000000L) { dl.tv_sec++; dl.tv_nsec -= 1000000000L; }
    while (!g_stop && rc != ETIMEDOUT) rc = pthread_cond_timedwait(&g_cv, &g_mu, &dl);
    if (!g_stop && !g_reported) {
        struct timespec n;

        g_reported = 1;
        pthread_mutex_unlock(&g_mu);
        clock_gettime(CLOCK_MONOTONIC, &n);
        report(ms_since(&g_t0, &n));
        pthread_mutex_lock(&g_mu);
        while (!g_stop) pthread_cond_wait(&g_cv, &g_mu);
        if (g_path[0]) {
            FILE *f = fopen(g_path, "a");
            long el;

            clock_gettime(CLOCK_MONOTONIC, &n);
            el = ms_since(&g_t0, &n);
            if (f) { fprintf(f, "[E] teardown finished after %ld ms\n", el); fclose(f); }
            fprintf(stderr, "[E] teardown finished after %ld ms (stall report was written)\n", el);
        }
    }
    pthread_mutex_unlock(&g_mu);
    return NULL;
}

void at_td_arm(const char *label)
{
    const char *ms = getenv("APITEST_TEARDOWN_WATCH_MS");
    pthread_condattr_t ca;

    if (g_armed || getenv("APITEST_NO_TEARDOWN_WATCH")) return;
    g_limit_ms = ms ? atol(ms) : 8000;
    if (g_limit_ms <= 0) g_limit_ms = 8000;
    clock_gettime(CLOCK_MONOTONIC, &g_t0);
    g_label = label; g_step = NULL; g_step_ms = 0; g_stop = 0; g_path[0] = 0;
    pthread_condattr_init(&ca);
    pthread_condattr_setclock(&ca, CLOCK_REALTIME);
    pthread_cond_init(&g_cv, &ca);
    pthread_condattr_destroy(&ca);
    if (pthread_create(&g_th, NULL, watch, NULL) == 0) g_armed = 1;
}

void at_td_disarm(void)
{
    if (!g_armed) return;
    pthread_mutex_lock(&g_mu);
    g_stop = 1;
    pthread_cond_signal(&g_cv);
    pthread_mutex_unlock(&g_mu);
    pthread_join(g_th, NULL);
    pthread_cond_destroy(&g_cv);
    g_armed = 0;
}
