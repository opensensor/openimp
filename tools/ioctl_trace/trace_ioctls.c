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
#include <sys/mman.h>
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
static const unsigned long IOCTL_TISP_SET_FRAME_FORMAT = 0xc0705451UL;
static const unsigned long IOCTL_TISP_REQBUFS = 0xc0145453UL;
static const unsigned long IOCTL_TISP_QBUF = 0xc0445455UL;
static const unsigned long IOCTL_TISP_DQBUF = 0xc0445456UL;
static const unsigned long IOCTL_TISP_STREAMON = 0xc0045457UL;
static const unsigned long IOCTL_TISP_STREAMOFF = 0xc0045458UL;
static const unsigned long IOCTL_TISP_WAIT_FRAME = 0xc004545aUL;

struct avpu_reg { uint32_t id; uint32_t value; };

static int log_fd = -1;
static int (*real_ioctl_fn)(int, unsigned long, void*) = NULL;
static unsigned int cl_dump_count;
static unsigned int tisp_frame_trace_count;

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

static void dump_command_list(uint32_t physical_address)
{
    const char *limit_text = getenv("P2_CL_DUMP_LIMIT");
    const char *directory = getenv("P2_CL_DUMP_DIR");
    unsigned long limit;
    long page_size;
    off_t page_base;
    size_t page_offset;
    size_t map_length;
    void *mapping;
    char path[PATH_MAX];
    char line[PATH_MAX + 128];
    int memory_fd;
    int output_fd;
    ssize_t written;

    if (!limit_text || !*limit_text || physical_address == 0)
        return;
    limit = strtoul(limit_text, NULL, 0);
    if (limit == 0 || cl_dump_count >= limit)
        return;
    if (!directory || !*directory)
        directory = "/tmp";

    page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0)
        page_size = 4096;
    page_base = (off_t)(physical_address & ~((uint32_t)page_size - 1u));
    page_offset = (size_t)(physical_address - (uint32_t)page_base);
    map_length = page_offset + 512u;
    map_length = (map_length + (size_t)page_size - 1u) &
                 ~((size_t)page_size - 1u);

    memory_fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (memory_fd < 0)
        return;
    mapping = mmap(NULL, map_length, PROT_READ, MAP_SHARED,
                   memory_fd, page_base);
    if (mapping == MAP_FAILED) {
        close(memory_fd);
        return;
    }

    ++cl_dump_count;
    snprintf(path, sizeof(path), "%s/p2-cl-%05ld-%03u-%08x.bin",
             directory, (long)getpid(), cl_dump_count, physical_address);
    output_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (output_fd >= 0) {
        written = write(output_fd, (const uint8_t *)mapping + page_offset,
                        512u);
        close(output_fd);
        snprintf(line, sizeof(line),
                 "CL_DUMP seq=%u phys=0x%08x bytes=%ld path=%s\n",
                 cl_dump_count, physical_address, (long)written, path);
        write_line(line);
    }
    munmap(mapping, map_length);
    close(memory_fd);
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
    if ((request == IOCTL_TISP_REQBUFS ||
         request == IOCTL_TISP_QBUF ||
         request == IOCTL_TISP_DQBUF ||
         request == IOCTL_TISP_STREAMON ||
         request == IOCTL_TISP_STREAMOFF ||
         request == IOCTL_TISP_WAIT_FRAME) && arg) {
        const char *limit_text = getenv("TISP_FRAME_TRACE_LIMIT");
        unsigned long limit = limit_text && *limit_text
            ? strtoul(limit_text, NULL, 0) : 0u;

        if (limit && tisp_frame_trace_count < limit) {
            uint32_t before[17] = {0};
            uint32_t after[17] = {0};
            size_t byte_count = ioc_size(request);
            size_t word_count;
            char line[1024];
            const char *name;
            size_t used;
            int ret;
            unsigned int i;

            if (byte_count > sizeof(before))
                byte_count = sizeof(before);
            word_count = (byte_count + sizeof(uint32_t) - 1u) /
                         sizeof(uint32_t);
            memcpy(before, arg, byte_count);
            ret = real_ioctl_fn ? real_ioctl_fn(fd, request, arg) : -1;
            memcpy(after, arg, byte_count);
            ++tisp_frame_trace_count;
            name = request == IOCTL_TISP_REQBUFS ? "REQBUFS" :
                   request == IOCTL_TISP_QBUF ? "QBUF" :
                   request == IOCTL_TISP_DQBUF ? "DQBUF" :
                   request == IOCTL_TISP_STREAMON ? "STREAMON" :
                   request == IOCTL_TISP_STREAMOFF ? "STREAMOFF" :
                   "WAIT_FRAME";
            ts_prefix(line, sizeof(line));
            used = strlen(line);
            used += (size_t)snprintf(line + used, sizeof(line) - used,
                                     "TISP %s seq=%u fd=%d before=", name,
                                     tisp_frame_trace_count, fd);
            for (i = 0; i < word_count && used + 10u < sizeof(line); ++i)
                used += (size_t)snprintf(line + used, sizeof(line) - used,
                                         "%s%08x", i ? "," : "", before[i]);
            used += (size_t)snprintf(line + used, sizeof(line) - used,
                                     " after=");
            for (i = 0; i < word_count && used + 10u < sizeof(line); ++i)
                used += (size_t)snprintf(line + used, sizeof(line) - used,
                                         "%s%08x", i ? "," : "", after[i]);
            (void)snprintf(line + used, sizeof(line) - used,
                           " ret=%d\n", ret);
            write_line(line);
            return ret;
        }
    }

    if (request == IOCTL_TISP_SET_FRAME_FORMAT && arg) {
        uint32_t before[0x70u / sizeof(uint32_t)];
        uint32_t after[0x70u / sizeof(uint32_t)];
        char line[1024];
        size_t used;
        int ret;
        unsigned int i;

        memcpy(before, arg, sizeof(before));
        ret = real_ioctl_fn ? real_ioctl_fn(fd, request, arg) : -1;
        memcpy(after, arg, sizeof(after));
        ts_prefix(line, sizeof(line));
        used = strlen(line);
        used += (size_t)snprintf(line + used, sizeof(line) - used,
                                 "TISP SET_FRAME_FORMAT fd=%d before=", fd);
        for (i = 0; i < sizeof(before) / sizeof(before[0]) &&
                    used + 10u < sizeof(line); ++i)
            used += (size_t)snprintf(line + used, sizeof(line) - used,
                                     "%s%08x", i ? "," : "", before[i]);
        used += (size_t)snprintf(line + used, sizeof(line) - used,
                                 " after=");
        for (i = 0; i < sizeof(after) / sizeof(after[0]) &&
                    used + 10u < sizeof(line); ++i)
            used += (size_t)snprintf(line + used, sizeof(line) - used,
                                     "%s%08x", i ? "," : "", after[i]);
        (void)snprintf(line + used, sizeof(line) - used,
                       " ret=%d\n", ret);
        write_line(line);
        return ret;
    }

    // Log only matching AVPU ioctls on the AVPU device fd
    if (is_target_request(request) && is_avpu_fd(fd)) {
        char line[256]; line[0] = '\0';
        if (request == IOCTL_AVPU_WRITE_REG) {
            struct avpu_reg in = {0,0};
            if (arg) memcpy(&in, arg, sizeof(in));
            if (in.id == 0x83e0)
                dump_command_list(in.value);
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
