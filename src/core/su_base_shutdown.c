/*
 * su_base_shutdown.c - SU_Base_Shutdown
 *
 * The vendor libsysutils does sync() and then kill(1, SIGCHLD).  busybox
 * init ignores SIGCHLD, so on a busybox system the call returns and the
 * camera keeps running.  OpenIMP keeps the same entry point and, after the
 * sync, sends the signal busybox init actually acts on:
 *
 *   OPENIMP_SU_SHUTDOWN unset / "poweroff"  SIGUSR2 (busybox: power off)
 *   OPENIMP_SU_SHUTDOWN=reboot              SIGTERM (busybox: reboot)
 *   OPENIMP_SU_SHUTDOWN=vendor              sync() + SIGCHLD, as the vendor
 *
 * Returns 0 when the signal was delivered to init, -1 (errno set) when
 * kill() failed.  The call returns before init has finished.
 */
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Host tests replace these two with fakes (-DOPENIMP_SU_KILL=...). */
#ifndef OPENIMP_SU_KILL
#define OPENIMP_SU_KILL kill
#endif
#ifndef OPENIMP_SU_SYNC
#define OPENIMP_SU_SYNC sync
#endif

int SU_Base_Shutdown(void)
{
    const char *mode = getenv("OPENIMP_SU_SHUTDOWN");
    int sig = SIGUSR2;

    if (mode && !strcmp(mode, "vendor"))
        sig = SIGCHLD;
    else if (mode && !strcmp(mode, "reboot"))
        sig = SIGTERM;

    OPENIMP_SU_SYNC();
    return OPENIMP_SU_KILL(1, sig) ? -1 : 0;
}
