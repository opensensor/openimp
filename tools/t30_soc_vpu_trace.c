/*
 * Capture T30 Helix command descriptors submitted through /dev/soc_vpu.
 *
 * Build as a shared object and preload it into an OEM encoder process.  The
 * tracer is deliberately read-only: it records channel_node arguments and
 * maps the descriptor through /dev/mem before forwarding the ioctl unchanged.
 *
 * Environment:
 *   T30_VPU_TRACE_LOG    log path (default /tmp/t30-vpu-trace.log)
 *   T30_VPU_TRACE_DIR    descriptor directory (default /tmp)
 *   T30_VPU_TRACE_LIMIT  number of RUN/START calls to capture (default 8)
 */

#define _GNU_SOURCE

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define SOC_VPU_MAGIC 'c'
#define SOC_VPU_CHANNEL_WORDS 14u
#define SOC_VPU_DESCRIPTOR_SIZE (64u * 1024u)

enum soc_vpu_word {
    SOC_VPU_CLIST = 0,
    SOC_VPU_VLIST,
    SOC_VPU_MDELAY,
    SOC_VPU_CHANNEL_ID,
    SOC_VPU_VPU_ID,
    SOC_VPU_CODECDIR,
    SOC_VPU_WORKPHASE,
    SOC_VPU_STATUS,
    SOC_VPU_OUTPUT_LEN,
    SOC_VPU_DMA_ADDR,
    SOC_VPU_THREAD_ID,
    SOC_VPU_CMPX,
    SOC_VPU_N_FLAG,
    SOC_VPU_NCU_ADDR,
};

typedef int (*ioctl_fn)(int fd, unsigned long request, void *arg);

static ioctl_fn real_ioctl;
static int log_fd = -1;
static unsigned int capture_count;
static unsigned int slice_capture_count;

typedef void (*t30_slice_init_fn)(void *slice);
static t30_slice_init_fn real_t30_slice_init;

static unsigned long ioc_size(unsigned long request)
{
    return (request >> 16) & ((1UL << 14) - 1UL);
}

static unsigned int trace_limit(void)
{
    const char *value = getenv("T30_VPU_TRACE_LIMIT");

    if (!value || !*value)
        return 8u;
    return (unsigned int)strtoul(value, NULL, 0);
}

static void open_log(void)
{
    const char *path;

    if (log_fd >= 0)
        return;
    path = getenv("T30_VPU_TRACE_LOG");
    if (!path || !*path)
        path = "/tmp/t30-vpu-trace.log";
    log_fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (log_fd < 0)
        log_fd = STDERR_FILENO;
}

static void log_line(const char *line)
{
    size_t length;

    open_log();
    if (!line)
        return;
    length = strlen(line);
    if (length)
        (void)write(log_fd, line, length);
}

static void timestamp(char *buffer, size_t size)
{
    struct timespec now;
    struct tm local;
    char wall[48];

    clock_gettime(CLOCK_REALTIME, &now);
    localtime_r(&now.tv_sec, &local);
    strftime(wall, sizeof(wall), "%Y-%m-%d %H:%M:%S", &local);
    snprintf(buffer, size, "%s.%03ld", wall, now.tv_nsec / 1000000L);
}

static int is_soc_vpu_fd(int fd)
{
    char link[64];
    char path[PATH_MAX];
    ssize_t length;

    snprintf(link, sizeof(link), "/proc/self/fd/%d", fd);
    length = readlink(link, path, sizeof(path) - 1u);
    if (length <= 0)
        return 0;
    path[length] = '\0';
    return strstr(path, "/dev/soc_vpu") != NULL;
}

static void format_node(char *buffer, size_t size, const uint32_t *node)
{
    snprintf(buffer, size,
             "clist=%08x vlist=%08x delay=%u ch=%u vpu=%08x codec=%u "
             "phase=%u status=%08x out=%u dma=%08x tid=%d cmpx=%u "
             "nflag=%u ncu=%08x",
             node[SOC_VPU_CLIST], node[SOC_VPU_VLIST],
             node[SOC_VPU_MDELAY], node[SOC_VPU_CHANNEL_ID],
             node[SOC_VPU_VPU_ID], node[SOC_VPU_CODECDIR],
             node[SOC_VPU_WORKPHASE], node[SOC_VPU_STATUS],
             node[SOC_VPU_OUTPUT_LEN], node[SOC_VPU_DMA_ADDR],
             (int32_t)node[SOC_VPU_THREAD_ID], node[SOC_VPU_CMPX],
             node[SOC_VPU_N_FLAG], node[SOC_VPU_NCU_ADDR]);
}

static void dump_descriptor(unsigned int sequence, uint32_t physical_address)
{
    const char *directory = getenv("T30_VPU_TRACE_DIR");
    long page_size;
    off_t page_base;
    size_t page_offset;
    size_t map_length;
    void *mapping;
    char path[PATH_MAX];
    char line[PATH_MAX + 128];
    int memory_fd;
    int output_fd;
    ssize_t written = -1;

    if (!directory || !*directory)
        directory = "/tmp";
    if (!physical_address)
        return;

    page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0)
        page_size = 4096;
    page_base = (off_t)(physical_address & ~((uint32_t)page_size - 1u));
    page_offset = (size_t)(physical_address - (uint32_t)page_base);
    map_length = page_offset + SOC_VPU_DESCRIPTOR_SIZE;
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

    snprintf(path, sizeof(path), "%s/t30-vpu-%05ld-%03u-%08x.bin",
             directory, (long)getpid(), sequence, physical_address);
    output_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (output_fd >= 0) {
        written = write(output_fd,
                        (const uint8_t *)mapping + page_offset,
                        SOC_VPU_DESCRIPTOR_SIZE);
        close(output_fd);
    }
    snprintf(line, sizeof(line),
             "DUMP seq=%u phys=%08x bytes=%ld path=%s\n",
             sequence, physical_address, (long)written, path);
    log_line(line);

    munmap(mapping, map_length);
    close(memory_fd);
}

static void dump_slice_info(unsigned int sequence, const void *slice)
{
    const char *directory = getenv("T30_VPU_TRACE_DIR");
    char path[PATH_MAX];
    int output_fd;

    if (!directory || !*directory)
        directory = "/tmp";
    if (!slice)
        return;

    snprintf(path, sizeof(path), "%s/t30-slice-%05ld-%03u.bin",
             directory, (long)getpid(), sequence);
    output_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (output_fd >= 0) {
        (void)write(output_fd, slice, 4096u);
        close(output_fd);
    }
}

static int intercept_ioctl(int fd, unsigned long request, void *arg)
{
    unsigned int type = _IOC_TYPE(request);
    unsigned int number = _IOC_NR(request);

    /*
     * Ingenic declares every soc_vpu ioctl with sizeof(channel_node), even
     * when the implementation actually consumes another payload.  In
     * particular nr 7 takes struct reg_info and is called dozens of times per
     * encoded frame.  Treating it as channel_node both reads beyond the real
     * argument and turns a short trace into tens of megabytes.  Only nr 0..4
     * have the channel_node ABI described above.
     */
    if (type == SOC_VPU_MAGIC && number <= 4u && is_soc_vpu_fd(fd) && arg &&
        ioc_size(request) >= SOC_VPU_CHANNEL_WORDS * sizeof(uint32_t)) {
        uint32_t before[SOC_VPU_CHANNEL_WORDS];
        uint32_t after[SOC_VPU_CHANNEL_WORDS];
        char stamp[64];
        char node_text[512];
        char line[768];
        unsigned int sequence = 0;
        int capture = (number == 2u || number == 3u);
        int ret;

        memcpy(before, arg, sizeof(before));
        if (capture) {
            sequence = __sync_add_and_fetch(&capture_count, 1u);
        }

        ret = real_ioctl ? real_ioctl(fd, request, arg) : -1;
        memcpy(after, arg, sizeof(after));
        /* The kernel has flushed the CPU cache by this point, so the physical
         * mapping now contains the exact descriptor consumed by Helix. */
        if (capture && sequence <= trace_limit())
            dump_descriptor(sequence, before[SOC_VPU_DMA_ADDR]);
        timestamp(stamp, sizeof(stamp));
        format_node(node_text, sizeof(node_text), before);
        snprintf(line, sizeof(line),
                 "%s SOC_VPU nr=%u request=%08lx seq=%u before %s\n",
                 stamp, number, request, sequence, node_text);
        log_line(line);
        format_node(node_text, sizeof(node_text), after);
        snprintf(line, sizeof(line),
                 "%s SOC_VPU nr=%u request=%08lx seq=%u ret=%d after %s\n",
                 stamp, number, request, sequence, ret, node_text);
        log_line(line);
        return ret;
    }

    return real_ioctl ? real_ioctl(fd, request,
                                   ioc_size(request) ? arg : NULL) : -1;
}

__attribute__((constructor)) static void trace_init(void)
{
    real_ioctl = (ioctl_fn)dlsym(RTLD_NEXT, "__ioctl");
    if (!real_ioctl)
        real_ioctl = (ioctl_fn)dlsym(RTLD_NEXT, "ioctl");
    log_line("t30-soc-vpu-trace initialized\n");
}

__attribute__((destructor)) static void trace_fini(void)
{
    if (log_fd >= 0 && log_fd != STDERR_FILENO)
        close(log_fd);
    log_fd = -1;
}

int __ioctl(int fd, unsigned long request, void *arg)
{
    if (!real_ioctl)
        trace_init();
    return intercept_ioctl(fd, request, arg);
}

int ioctl(int fd, unsigned long request, ...)
{
    void *arg = NULL;

    if (!real_ioctl)
        trace_init();
    if (ioc_size(request)) {
        va_list ap;

        va_start(ap, request);
        arg = va_arg(ap, void *);
        va_end(ap);
    }
    return intercept_ioctl(fd, request, arg);
}

/* Capture the compact, private T30 slice ABI before the stock descriptor
 * builder consumes it.  The symbol is preemptible in SDK 1.0.5's libimp. */
void H264E_T30_SliceInit(void *slice)
{
    unsigned int sequence;

    if (!real_t30_slice_init)
        real_t30_slice_init = (t30_slice_init_fn)dlsym(RTLD_NEXT,
                                                       "H264E_T30_SliceInit");
    sequence = __sync_add_and_fetch(&slice_capture_count, 1u);
    if (sequence <= trace_limit())
        dump_slice_info(sequence, slice);
    if (real_t30_slice_init)
        real_t30_slice_init(slice);
}
