// LD_PRELOAD ioctl tracer for /dev/avpu to capture OEM register sequences
// Build: see Makefile or build-for-device.sh
// Usage:
//   LOG_AVPU_TRACE=/tmp/avpu_trace.log LD_PRELOAD=./libioctl_trace.so <oem_app>
// Logs lines like:
//   AVPU WREG [0x83f0] <- 0x00000002
//   AVPU RREG [0x8014] -> 0xffffffff

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// Hardcode T31 avpu ioctl request numbers (from libimp + driver)
// _IOWR('q', 10, struct avpu_reg)
// _IOWR('q', 11, struct avpu_reg)
static const unsigned long IOCTL_AVPU_WRITE_REG = 0xc008710aUL;
static const unsigned long IOCTL_AVPU_READ_REG  = 0xc008710bUL;

struct avpu_reg { uint32_t id; uint32_t value; };

static int log_fd = -1;
static int (*real_ioctl_fn)(int, unsigned long, void*) = NULL;

static void open_log_once(void)
{
    if (log_fd != -1) return;
    const char *path = getenv("LOG_AVPU_TRACE");
    if (path && *path) {
        int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0) { log_fd = fd; return; }
    }
    log_fd = STDERR_FILENO; // fallback
}

static void write_line(const char *s)
{
    if (log_fd < 0) open_log_once();
    if (!s) return;
    size_t n = strlen(s);
    if (n == 0) return;
    (void)write(log_fd, s, n);
}

static void ts_prefix(char *buf, size_t sz)
{
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm; localtime_r(&ts.tv_sec, &tm);
    char tmbuf[64]; strftime(tmbuf, sizeof(tmbuf), "%Y-%m-%d %H:%M:%S", &tm);
    snprintf(buf, sz, "%s.%03ld ", tmbuf, ts.tv_nsec/1000000);
}

static int is_avpu_fd(int fd)
{
    // Resolve /proc/self/fd/<fd> -> path, compare against "/dev/avpu"
    char link[64]; char path[PATH_MAX];
    snprintf(link, sizeof(link), "/proc/self/fd/%d", fd);
    ssize_t n = readlink(link, path, sizeof(path)-1);
    if (n <= 0) return 0;
    path[n] = '\0';
    // Some systems show "(deleted)" suffix or device numbers; just check substring
    if (strstr(path, "/dev/avpu") != NULL) return 1;
    return 0;
}

static inline int is_target_request(unsigned long request)
{
    return (request == IOCTL_AVPU_WRITE_REG) || (request == IOCTL_AVPU_READ_REG);
}

__attribute__((constructor)) static void init_tracer(void)
{
    open_log_once();
    // Prefer resolving __ioctl (fixed 3-arg signature). Fall back to ioctl if needed.
    real_ioctl_fn = (int (*)(int, unsigned long, void*))dlsym(RTLD_NEXT, "__ioctl");
    if (!real_ioctl_fn) {
        real_ioctl_fn = (int (*)(int, unsigned long, void*))dlsym(RTLD_NEXT, "ioctl");
    }
    char line[128] = {0};
    ts_prefix(line, sizeof(line));
    write_line(line);
    write_line("[ioctl_trace] initialized\n");
}

__attribute__((destructor)) static void fini_tracer(void)
{
    if (log_fd >= 0 && log_fd != STDERR_FILENO) close(log_fd);
    log_fd = -1;
}

static inline unsigned long ioc_size(unsigned long request)
{
    // Generic decoder for Linux _IOC size field (14 bits starting at bit 16)
    return (request >> 16) & ((1UL << 14) - 1);
}

static int intercept_ioctl(int fd, unsigned long request, void *arg)
{
    // Log only matching AVPU ioctls on the AVPU device fd
    if (is_target_request(request) && is_avpu_fd(fd)) {
        char line[256]; line[0] = '\0';
        if (request == IOCTL_AVPU_WRITE_REG) {
            struct avpu_reg in = {0,0};
            if (arg) memcpy(&in, arg, sizeof(in));
            ts_prefix(line, sizeof(line));
            char buf[160]; snprintf(buf, sizeof(buf), "AVPU WREG [0x%04x] <- 0x%08x\n", in.id, in.value);
            strncat(line, buf, sizeof(line)-strlen(line)-1);
            write_line(line);
            return real_ioctl_fn ? real_ioctl_fn(fd, request, arg) : -1;
        } else {
            struct avpu_reg in = {0,0};
            if (arg) memcpy(&in, arg, sizeof(in));
            int ret = real_ioctl_fn ? real_ioctl_fn(fd, request, arg) : -1;
            struct avpu_reg out = {0,0};
            if (arg) memcpy(&out, arg, sizeof(out));
            ts_prefix(line, sizeof(line));
            char buf[160]; snprintf(buf, sizeof(buf), "AVPU RREG [0x%04x] -> 0x%08x (ret=%d)\n", out.id ? out.id : in.id, out.value, ret);
            strncat(line, buf, sizeof(line)-strlen(line)-1);
            write_line(line);
            return ret;
        }
    }
    // Non-target: forward safely. If the ioctl encodes no payload (size==0),
    // ensure we pass a NULL third argument to avoid propagating garbage when the
    // original call used the 2-arg form.
    void *safe_arg = (ioc_size(request) == 0) ? NULL : arg;
    return real_ioctl_fn ? real_ioctl_fn(fd, request, safe_arg) : -1;
}


// Some libcs use __ioctl as the underlying symbol; interpose it too
int __ioctl(int fd, unsigned long request, void *arg)
{
    if (!real_ioctl_fn) {
        real_ioctl_fn = (int (*)(int, unsigned long, void*))dlsym(RTLD_NEXT, "__ioctl");
        if (!real_ioctl_fn)
            real_ioctl_fn = (int (*)(int, unsigned long, void*))dlsym(RTLD_NEXT, "ioctl");
    }
    return intercept_ioctl(fd, request, arg);
}


// Interpose the public ioctl symbol as well (variadic), some libcs/apps call this directly
int ioctl(int fd, unsigned long request, ...)
{
    if (!real_ioctl_fn) {
        real_ioctl_fn = (int (*)(int, unsigned long, void*))dlsym(RTLD_NEXT, "__ioctl");
        if (!real_ioctl_fn)
            real_ioctl_fn = (int (*)(int, unsigned long, void*))dlsym(RTLD_NEXT, "ioctl");
    }
    void *arg = NULL;
    if (ioc_size(request) != 0) {
        va_list ap;
        va_start(ap, request);
        arg = va_arg(ap, void*);
        va_end(ap);
    }
    return intercept_ioctl(fd, request, arg);
}

