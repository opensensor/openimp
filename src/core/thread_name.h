#ifndef OPENIMP_CORE_THREAD_NAME_H
#define OPENIMP_CORE_THREAD_NAME_H

/*
 * Name the calling thread (shows in /proc/<pid>/task/<tid>/comm, top -H).
 * prctl(PR_SET_NAME) is used because it is available on uClibc, musl and
 * kernel 3.10 alike; the kernel truncates to 15 characters.  Must be called
 * from inside the thread.  Names follow the vendor libimp where an equivalent
 * thread exists ("FS(%d)-tick", "IVS(%d)-ivs_processing", "ai-_ai_record_thread"),
 * otherwise "oimp-<role>[-<chn>]".
 */

#include <stdio.h>
#include <sys/prctl.h>

#ifndef PR_SET_NAME
#define PR_SET_NAME 15
#endif

static inline void openimp_thread_setname(const char *name)
{
    (void)prctl(PR_SET_NAME, name, 0, 0, 0);
}

static inline void openimp_thread_setnamef(const char *fmt, int a)
{
    char buf[32];

    (void)snprintf(buf, sizeof(buf), fmt, a);
    openimp_thread_setname(buf);
}

#endif
