/* Rate-limited logging (trace_control.h, imp_log_int.h): a persistent fault
 * must give the first lines and then one in 1024, not one line per call. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "imp_log_int.h"

static int stderr_lines(const char *path)
{
    FILE *f = fopen(path, "r");
    int lines = 0, c;

    assert(f);
    while ((c = fgetc(f)) != EOF)
        lines += c == '\n';
    fclose(f);
    return lines;
}

static void log_one(int i)
{
    IMP_LOG_LIMITED(LOG_ERR, "Test", "bad handle %d", i);
}

int main(void)
{
    unsigned int counter = 0;
    int allowed = 0, i, saved, lines;
    char path[] = "/tmp/openimp-log-limit-XXXXXX";
    int fd = mkstemp(path);
    struct timespec t0, t1;
    double ns;

    /* the helper: 8 first, then 1024, 2048, ... */
    for (i = 1; i <= 100000; i++)
        allowed += openimp_log_allow(&counter);
    assert(allowed == 8 + 100000 / 1024);
    counter = 0;
    for (i = 1; i <= 8; i++)
        assert(openimp_log_allow(&counter));
    assert(!openimp_log_allow(&counter));

    /* the macro: one counter per call site, lines really limited */
    assert(fd >= 0);
    fflush(stderr);
    saved = dup(2);
    assert(saved >= 0 && dup2(fd, 2) >= 0);
    for (i = 0; i < 5000; i++)
        log_one(i);
    fflush(stderr);
    dup2(saved, 2);
    close(saved);
    close(fd);
    lines = stderr_lines(path);
    unlink(path);
    assert(lines == 8 + 5000 / 1024);

    /* cost of the check on the repeat path (informational) */
    counter = 100;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (i = 0; i < 10000000; i++)
        allowed += openimp_log_allow(&counter);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    ns = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e7;
    printf("log_limit_test: ok (%.1f ns per limited call on this host, %d)\n",
           ns, allowed > 0);
    return 0;
}
