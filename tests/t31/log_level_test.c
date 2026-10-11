/* IMP_LOG level gate (imp_log_int.h): default LOG_WARNING, OPENIMP_LOG_LEVEL
 * override, OPENIMP_DEBUG_TRACE = everything.  The level is cached per
 * process, so each case runs in a forked child with its own environment. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "imp_log_int.h"

static void emit_all(void)
{
    IMP_LOG(LOG_ERR,     "T", "e");
    IMP_LOG(LOG_WARNING, "T", "w");
    IMP_LOG(LOG_NOTICE,  "T", "n");
    IMP_LOG(LOG_INFO,    "T", "i");
    IMP_LOG(LOG_DEBUG,   "T", "d");
    IMP_LOG_INFO("T", "i2 %d", 1);
}

/* number of stderr lines written by emit_all() under the given environment */
static int run_case(const char *level, const char *trace)
{
    char path[] = "/tmp/openimp-log-level-XXXXXX";
    int fd = mkstemp(path), lines = 0, c, st;
    pid_t pid;
    FILE *f;

    assert(fd >= 0);
    fflush(NULL);
    pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        if (level) setenv("OPENIMP_LOG_LEVEL", level, 1);
        else unsetenv("OPENIMP_LOG_LEVEL");
        if (trace) setenv("OPENIMP_DEBUG_TRACE", trace, 1);
        else unsetenv("OPENIMP_DEBUG_TRACE");
        dup2(fd, 2);
        emit_all();
        fflush(stderr);
        _exit(0);
    }
    assert(waitpid(pid, &st, 0) == pid && WIFEXITED(st) && !WEXITSTATUS(st));
    close(fd);
    f = fopen(path, "r");
    assert(f);
    while ((c = fgetc(f)) != EOF)
        lines += c == '\n';
    fclose(f);
    unlink(path);
    return lines;
}

int main(void)
{
    /* pure rule */
    assert(imp_log_level_parse(NULL, 0) == LOG_NOTICE);
    assert(imp_log_level_parse("", 0) == LOG_NOTICE);
    assert(imp_log_level_parse("x", 0) == LOG_NOTICE);
    assert(imp_log_level_parse("8", 0) == LOG_NOTICE);
    assert(imp_log_level_parse("44", 0) == LOG_NOTICE);
    assert(imp_log_level_parse("0", 0) == 0);
    assert(imp_log_level_parse("3", 0) == LOG_ERR);
    assert(imp_log_level_parse("7", 0) == LOG_DEBUG);
    assert(imp_log_level_parse("2", 1) == LOG_DEBUG);
    assert(imp_log_level_parse(NULL, 1) == LOG_DEBUG);

    /* default: ERR, WARNING, NOTICE (INFO and DEBUG, also via IMP_LOG_INFO, dropped) */
    assert(run_case(NULL, NULL) == 3);
    assert(run_case("garbage", NULL) == 3);
    assert(run_case(NULL, "0") == 3);          /* DEBUG_TRACE=0 is off */
    /* env override */
    assert(run_case("3", NULL) == 1);          /* errors only */
    assert(run_case("0", NULL) == 0);
    assert(run_case("4", NULL) == 2);
    assert(run_case("5", NULL) == 3);
    assert(run_case("6", NULL) == 5);          /* + INFO, also IMP_LOG_INFO */
    assert(run_case("7", NULL) == 6);
    /* OPENIMP_DEBUG_TRACE wins over a lower level, like before the knob */
    assert(run_case(NULL, "1") == 6);
    assert(run_case("3", "1") == 6);

    printf("log_level_test: ok\n");
    return 0;
}
