/*
 * time64_shim.c — provide the musl time64 ABI symbols on top of legacy
 * 32-bit-timespec kernel syscalls.
 *
 * The host toolchain targets musl with _REDIR_TIME64, so the compiler
 * rewrites every call to clock_gettime/nanosleep/select/sem_timedwait
 * into __*_time64 variants. On older rootfs (e.g. Wyze Cam v3 stock,
 * kernel 3.10) those symbols don't exist in libc — the GOT entries
 * resolve to NULL at load time and the first call segfaults.
 *
 * We provide local definitions so libimp.so resolves its own references
 * to these functions instead of leaving them UND. Implementations use
 * raw 32-bit-timespec syscalls and hand-convert to/from the musl time64
 * struct layout that the rest of libimp.so sees.
 *
 * struct timespec on MIPS32 little-endian musl (time64) is:
 *   offset 0:  int64_t tv_sec     (8 bytes)
 *   offset 8:  long    tv_nsec    (4 bytes)
 *   offset 12: padding             (4 bytes)
 * Total: 16 bytes.
 */

#define _GNU_SOURCE
#include <time.h>
#include <sys/select.h>
#include <semaphore.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>
#include <stdint.h>

/* Legacy 32-bit kernel structs */
struct ts32 { int32_t tv_sec; int32_t tv_nsec; };
struct tv32 { int32_t tv_sec; int32_t tv_usec; };

/* MIPS o32 syscall numbers (from <sys/syscall.h>) — the musl headers
 * rename clock_gettime to clock_gettime32, which we use directly. */
#ifndef __NR_clock_gettime32
#define __NR_clock_gettime32 4263
#endif
#ifndef __NR_nanosleep
#define __NR_nanosleep 4166
#endif
#ifndef __NR__newselect
#define __NR__newselect 4142
#endif

/* clock_gettime — musl's time64 entry point */
int __clock_gettime64(clockid_t clk, struct timespec *ts)
{
	struct ts32 t32;
	long r = syscall(__NR_clock_gettime32, (long)clk, (long)&t32);
	if (r < 0)
		return -1;
	ts->tv_sec = (int64_t)t32.tv_sec;
	ts->tv_nsec = (long)t32.tv_nsec;
	return 0;
}

/* nanosleep — musl's time64 entry point */
int __nanosleep_time64(const struct timespec *req, struct timespec *rem)
{
	struct ts32 r32;
	struct ts32 rm32 = {0, 0};

	/* Clamp tv_sec to INT32_MAX if the caller gave us a 64-bit value
	 * that overflows — this matches what the kernel does anyway. */
	if (req->tv_sec > (int64_t)INT32_MAX)
		r32.tv_sec = INT32_MAX;
	else if (req->tv_sec < 0)
		r32.tv_sec = 0;
	else
		r32.tv_sec = (int32_t)req->tv_sec;
	r32.tv_nsec = (int32_t)req->tv_nsec;

	long r = syscall(__NR_nanosleep, (long)&r32, rem ? (long)&rm32 : 0L);
	if (rem) {
		rem->tv_sec = (int64_t)rm32.tv_sec;
		rem->tv_nsec = (long)rm32.tv_nsec;
	}
	return (int)r;
}

/* select — musl's time64 entry point */
int __select_time64(int n, fd_set *rfds, fd_set *wfds, fd_set *efds,
		    struct timeval *tv)
{
	struct tv32 t32;
	struct tv32 *pt32 = 0;
	if (tv) {
		t32.tv_sec = (tv->tv_sec > (int64_t)INT32_MAX) ? INT32_MAX
				: (int32_t)tv->tv_sec;
		t32.tv_usec = (int32_t)tv->tv_usec;
		pt32 = &t32;
	}
	long r = syscall(__NR__newselect, (long)n, (long)rfds, (long)wfds,
			 (long)efds, (long)pt32);
	if (tv) {
		tv->tv_sec = (int64_t)t32.tv_sec;
		tv->tv_usec = (long)t32.tv_usec;
	}
	return (int)r;
}

/* pthread_cond_timedwait — musl's time64 entry point. Forwards to the
 * device libc's plain pthread_cond_timedwait with a converted 32-bit
 * timespec. The __asm__ label on the declaration bypasses the musl
 * header's time64 redirect so we get a reference to the plain symbol
 * name, which the dynamic loader resolves against the device's libc
 * at load time.
 *
 * pthread_cond_t / pthread_mutex_t are opaque here (void *) because
 * including <pthread.h> would drag the redirect in and defeat the
 * asm-label trick. libimp itself owns these structs, so we only need
 * to pass pointers through. */
extern int __pthread_cond_timedwait_plain(void *cond, void *mutex,
                                          const struct ts32 *abstime)
        __asm__("pthread_cond_timedwait");

int __pthread_cond_timedwait_time64(void *cond, void *mutex,
                                    const struct timespec *abstime)
{
        struct ts32 t32;

        if (abstime->tv_sec > (int64_t)INT32_MAX)
                t32.tv_sec = INT32_MAX;
        else if (abstime->tv_sec < 0)
                t32.tv_sec = 0;
        else
                t32.tv_sec = (int32_t)abstime->tv_sec;
        t32.tv_nsec = (int32_t)abstime->tv_nsec;

        return __pthread_cond_timedwait_plain(cond, mutex, &t32);
}

/* sem_timedwait — musl's time64 entry point. Implemented as a poll loop
 * using sem_trywait + short sleeps. Not fast, but libimp uses it only
 * on IVS / encoder / fifo timeout paths which already expect ~ms-level
 * latency. The alternative would be reimplementing it against the
 * futex_time64 syscall, which isn't available on kernel 3.10 either. */
int __sem_timedwait_time64(sem_t *sem, const struct timespec *abs_timeout)
{
	/* Convert absolute timeout to a monotonic deadline we can compare.
	 * musl's sem_timedwait takes a CLOCK_REALTIME absolute deadline,
	 * so we use the same clock here. */
	struct ts32 now32;

	for (;;) {
		if (sem_trywait(sem) == 0)
			return 0;
		if (errno != EAGAIN)
			return -1;

		if (syscall(__NR_clock_gettime32, CLOCK_REALTIME,
			    (long)&now32) < 0)
			return -1;

		int64_t now_ns = (int64_t)now32.tv_sec * 1000000000LL
				 + (int64_t)now32.tv_nsec;
		int64_t end_ns = abs_timeout->tv_sec * 1000000000LL
				 + (int64_t)abs_timeout->tv_nsec;
		if (now_ns >= end_ns) {
			errno = ETIMEDOUT;
			return -1;
		}

		/* Sleep 1ms, then retry */
		struct ts32 nap = { 0, 1000000 };
		syscall(__NR_nanosleep, (long)&nap, 0L);
	}
}
