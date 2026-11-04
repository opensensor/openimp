/*
 * su_base_shutdown.c - SU_Base_Shutdown
 *
 * Vendor behaviour (libsysutils of T10/T20 3.12.0, T40 ISVP 1.3.1, T41 ISVP
 * 1.2.5, disassembly of SU_Base_Shutdown): call sync(), then kill(1, 17),
 * return 0 whatever kill() said.  Signal 17 is SIGUSR2 on MIPS Linux (SIGCHLD
 * is 18), which busybox init treats as "power off".  OpenIMP does the same by
 * default, so the camera powers off like it does with the vendor library:
 *
 *   OPENIMP_SU_SHUTDOWN unset / "poweroff" / "vendor"   sync() + SIGUSR2 (vendor)
 *   OPENIMP_SU_SHUTDOWN=reboot                          sync() + SIGTERM (busybox: reboot)
 *
 * Beyond the vendor: -1 (errno set) when kill() failed; the vendor returns 0.
 * Returns 0 when the signal was delivered to init.  The call returns before
 * init has finished.  Never call this on a real camera in a test.
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

#if defined(__mips__)
_Static_assert(SIGUSR2 == 17, "vendor SU_Base_Shutdown sends signal 17 = SIGUSR2");
#endif

int SU_Base_Shutdown(void)
{
    const char *mode = getenv("OPENIMP_SU_SHUTDOWN");
    int sig = SIGUSR2; /* vendor: kill(1, 17) = SIGUSR2 on MIPS */

    if (mode && !strcmp(mode, "reboot"))
        sig = SIGTERM;

    OPENIMP_SU_SYNC();
    return OPENIMP_SU_KILL(1, sig) ? -1 : 0;
}
