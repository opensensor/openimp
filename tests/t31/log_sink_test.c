/* imp_log_fun sink 2 (logcat record) and sink 1 (log file): the message is
 * cut at the right offset also when the "[tag] file:line func: " prefix did
 * not fit the 512 byte line (the bare message is logged, no tag skip past
 * it), and the lazily opened device/file are opened once. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "imp_log_fun.h"

/* interposes libc: the child below plays a set-id process (euid != uid) */
static int fake_setid;
uid_t geteuid(void) { return fake_setid ? getuid() + 1 : getuid(); }

/* review L5: a set-id process ignores OPENIMP_LOGGER_DEV (the device would
 * be written with elevated rights); a normal process honours it */
static void setid_ignores_env(void)
{
    char dev[] = "/tmp/openimp-log-setid-XXXXXX";
    struct stat st;
    int fd = mkstemp(dev), status;
    pid_t pid;

    assert(fd >= 0);
    pid = fork();
    assert(pid >= 0);
    if (!pid) {
        fake_setid = 1;
        setenv("OPENIMP_LOGGER_DEV", dev, 1);
        imp_set_log_level(0);
        imp_log_fun(6, IMP_LOG_OP_ALL, IMP_LOG_OUT_SERVER, "tag", "f.c", 1, "fn", "x\n");
        _exit(0);
    }
    assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status));
    assert(fstat(fd, &st) == 0 && st.st_size == 0);
    unlink(dev);
    close(fd);
}

int main(void)
{
    char dev[] = "/tmp/openimp-log-sink-XXXXXX";
    char big[700], rec[2048];
    int fd = mkstemp(dev), n;
    char *msg;

    assert(fd >= 0);
    setid_ignores_env();
    setenv("OPENIMP_LOGGER_DEV", dev, 1);
    memset(big, 'T', sizeof(big) - 1);
    big[sizeof(big) - 1] = 0;
    imp_set_log_level(0);

    imp_log_fun(6, IMP_LOG_OP_ALL, IMP_LOG_OUT_SERVER, "tag", "dir/file.c", 7, "fn", "hello %d\n", 1);
    imp_log_fun(6, IMP_LOG_OP_ALL, IMP_LOG_OUT_SERVER, big, "dir/file.c", 7, "fn", "bare %s\n", "msg");
    n = (int)read(fd, rec, sizeof(rec) - 1);
    assert(n > 0);
    rec[n] = 0;
    /* record 1: prio byte, tag NUL, message NUL (without the "[tag]") */
    assert(rec[0] == 6 && !strcmp(rec + 1, "tag"));
    msg = rec + 1 + 4;
    assert(!strcmp(msg, "file.c:7 fn: hello 1"));
    /* record 2: the 700 byte tag, then the bare message */
    msg += strlen(msg) + 1;
    assert(msg[0] == 6 && !strncmp(msg + 1, big, 10));
    msg += 1 + strlen(msg + 1) + 1;
    printf("sink 2 messages: '%s'\n", msg);
    assert(!strcmp(msg, "bare msg"));
    unlink(dev);
    printf("log_sink_test OK\n");
    return 0;
}
