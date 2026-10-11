/* Host test: SU_Base_Shutdown with a fake kill()/sync().  Build:
 *   cc -DOPENIMP_SU_KILL=fake_kill -DOPENIMP_SU_SYNC=fake_sync \
 *      tests/core/su_base_shutdown_test.c -o /tmp/su_base_shutdown_test */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

static int kill_pid, kill_sig, kill_calls, sync_calls, kill_fail;
static int order_ok;

static int fake_kill(pid_t pid, int sig)
{
    kill_pid = pid;
    kill_sig = sig;
    kill_calls++;
    order_ok = sync_calls == 1; /* sync must come first */
    if (kill_fail) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

static void fake_sync(void) { sync_calls++; }

#include "../../src/core/su_base_shutdown.c"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static void run(const char *env, int want_sig, int fail, int want_ret)
{
    int r;
    if (env) setenv("OPENIMP_SU_SHUTDOWN", env, 1); else unsetenv("OPENIMP_SU_SHUTDOWN");
    kill_calls = sync_calls = 0;
    kill_fail = fail;
    r = SU_Base_Shutdown();
    CHECK(r == want_ret);
    CHECK(kill_calls == 1 && sync_calls == 1 && order_ok);
    CHECK(kill_pid == 1);
    CHECK(kill_sig == want_sig);
    if (fail) CHECK(errno == EPERM);
}

int main(void)
{
    run(NULL, SIGTERM, 0, 0);
    run("reboot", SIGTERM, 0, 0);
    run("poweroff", SIGUSR2, 0, 0);
    run("vendor", SIGCHLD, 0, 0);
    run("junk", SIGTERM, 0, 0);
    run(NULL, SIGTERM, 1, -1);
    printf(failures ? "su_base_shutdown_test: %d FAIL\n" : "su_base_shutdown_test: PASS\n", failures);
    return failures != 0;
}
