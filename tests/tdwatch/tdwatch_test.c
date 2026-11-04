/* Host test of the apitest teardown stall watch (issue #198). */
#define _GNU_SOURCE
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include "at_tdwatch.h"

/* strong versions of the OpenIMP debug accessors: what a stalled FrameSource would answer */
static volatile unsigned int g_owner;
unsigned int OpenIMP_FS_DebugState(void) { return 0x00000013u; }
const char *OpenIMP_FS_DebugHolder(void) { return g_owner ? "IMP_FrameSource_DisableChn" : NULL; }
unsigned int OpenIMP_FS_DebugLockOwner(void) { return g_owner; }

static int g_fail;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; fprintf(stdout, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stdout, __VA_ARGS__); fputc('\n', stdout); } } while (0)

static char g_dir[256], g_err[300];
static int g_errfd;

static void *holder(void *a)
{
    (void)a;
    g_owner = (unsigned int)syscall(SYS_gettid);
    usleep(900000);
    return NULL;
}

static long errsize(void)
{
    struct stat st;

    return stat(g_err, &st) ? -1 : (long)st.st_size;
}

static int count(const char *buf, const char *needle)
{
    int n = 0;
    const char *p = buf;

    while ((p = strstr(p, needle))) { n++; p += strlen(needle); }
    return n;
}

static void slurp(const char *path, char *out, size_t n)
{
    FILE *f = fopen(path, "r");
    size_t r = f ? fread(out, 1, n - 1, f) : 0;

    out[r] = 0;
    if (f) fclose(f);
}

int main(int argc, char **argv)
{
    char json[300], rep[300], buf[16384];
    struct timespec a, b;
    long worst = 0;
    int i;
    pthread_t th;

    snprintf(g_dir, sizeof(g_dir), "%s", argc > 1 ? argv[1] : ".");
    snprintf(json, sizeof(json), "%s/results.json", g_dir);
    snprintf(g_err, sizeof(g_err), "%s/stderr.txt", g_dir);
    snprintf(rep, sizeof(rep), "%s/teardown-stall-%d.txt", g_dir, (int)getpid());
    unlink(rep);
    g_errfd = open(g_err, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    dup2(g_errfd, 2);
    at_td_init(json);

    /* 1. normal teardown: nothing written, cheap */
    setenv("APITEST_TEARDOWN_WATCH_MS", "300", 1);
    for (i = 0; i < 20; i++) {
        long us;

        clock_gettime(CLOCK_MONOTONIC, &a);
        at_td_arm("normal");
        at_td_step("IMP_System_Exit");
        at_td_disarm();
        clock_gettime(CLOCK_MONOTONIC, &b);
        us = (b.tv_sec - a.tv_sec) * 1000000L + (b.tv_nsec - a.tv_nsec) / 1000;
        if (us > worst) worst = us;
    }
    CHECK(worst < 5000, "arm+disarm worst %ld us (limit 5000)", worst);
    CHECK(errsize() == 0, "stderr not empty after normal teardowns (%ld B)", errsize());
    CHECK(access(rep, F_OK) != 0, "report file written by a normal teardown");
    printf("normal teardown: arm+disarm worst of 20 = %ld us\n", worst);

    /* 2. disabled: a stall writes nothing */
    setenv("APITEST_NO_TEARDOWN_WATCH", "1", 1);
    at_td_arm("disabled"); usleep(450000); at_td_disarm();
    CHECK(errsize() == 0 && access(rep, F_OK) != 0, "disabled watch wrote something");
    unsetenv("APITEST_NO_TEARDOWN_WATCH");

    /* 3. faked stall: sleep in a teardown stub, with a thread holding the (fake) lock */
    pthread_create(&th, NULL, holder, NULL);
    usleep(50000);
    at_td_arm("fake teardown");
    at_td_step("stub_sleep (IMP_FrameSource_DisableChn)");
    usleep(700000);
    at_td_disarm();
    pthread_join(th, NULL);
    CHECK(access(rep, F_OK) == 0, "no report file for a stalled teardown");
    slurp(rep, buf, sizeof(buf));
    CHECK(count(buf, "teardown stall (issue #198)") == 1, "report block count %d", count(buf, "teardown stall (issue #198)"));
    CHECK(strstr(buf, "main thread step: stub_sleep") != NULL, "step missing");
    CHECK(strstr(buf, "fs state 0x00000013") != NULL, "state word missing");
    CHECK(strstr(buf, "LOCKED, holder IMP_FrameSource_DisableChn") != NULL, "lock holder missing");
    CHECK(strstr(buf, "(g_fs_lock owner)") != NULL, "owner task not marked");
    CHECK(strstr(buf, "(main)") != NULL, "main task missing");
    CHECK(count(buf, "[E]   task ") >= 3, "tasks listed: %d", count(buf, "[E]   task "));
    CHECK(strstr(buf, "teardown finished after") != NULL, "finish line missing");
    CHECK(strstr(buf, "kmsg") == NULL, "kernel log touched");
    slurp(g_err, buf, sizeof(buf));
    CHECK(count(buf, "teardown stall (issue #198)") == 1, "stderr block count %d", count(buf, "teardown stall (issue #198)"));

    /* 4. a second stall in the same process adds no second block */
    at_td_arm("second"); usleep(450000); at_td_disarm();
    slurp(g_err, buf, sizeof(buf));
    CHECK(count(buf, "teardown stall (issue #198)") == 1, "second block written (%d)", count(buf, "teardown stall (issue #198)"));

    printf("tdwatch: %s\n", g_fail ? "FAILED" : "ok");
    unlink(rep);
    return g_fail ? 1 : 0;
}
