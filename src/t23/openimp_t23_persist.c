/*
 * Short-lived T23 diagnostic trace which survives a watchdog reset.
 *
 * The T23 bootloader reserves an rmem arena for media buffers.  The OpenIMP
 * allocator grows upward from its base, so diagnostic builds may use the
 * final 64 KiB while running the deliberately small, encoder-disabled smoke
 * profile.  Every record and the ring header are written back through the
 * OEM rmem cache-maintenance ioctl before returning.
 *
 * This is opt-in because the tail is not reserved from normal media-buffer
 * allocation.  Never enable OPENIMP_RMEM_TRACE in a production profile.
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "openimp_t23_persist.h"

#define T23_TRACE_REGION_SIZE 0x10000u
#define T23_TRACE_VERSION 1u
#define RMEM_IOCTL_FLUSH_CACHE 0xc00c7200u
#define T23_TRACE_FILE_LIMIT 0x20000u

typedef struct {
    char magic[8];
    uint32_t version;
    uint32_t header_size;
    uint32_t capacity;
    uint32_t write_pos;
    uint32_t valid;
    uint32_t records;
    uint32_t wraps;
    uint32_t rmem_base;
    uint32_t rmem_size;
    uint32_t reserved[5];
    unsigned char data[];
} T23PersistHeader;

typedef struct {
    uint32_t addr;
    uint32_t size;
    uint32_t dir;
} T23RmemFlush;

_Static_assert(sizeof(T23PersistHeader) == 64u,
               "T23 persistent trace header must remain stable");

static pthread_mutex_t t23_trace_lock = PTHREAD_MUTEX_INITIALIZER;
static int t23_trace_checked;
static int t23_trace_enabled_value;
static int t23_trace_fd = -1;
static int t23_trace_file_fd = -1;
static uint32_t t23_trace_file_bytes;
static uint32_t t23_trace_file_records;
static void *t23_trace_mapping;
static T23PersistHeader *t23_trace_header;

static int t23_parse_rmem(uint32_t *base_out, uint32_t *size_out)
{
    char cmdline[1024];
    char *field;
    char *endp;
    unsigned long amount;
    unsigned long base;
    uint64_t bytes;
    FILE *file;

    file = fopen("/proc/cmdline", "r");
    if (file == NULL)
        return -1;
    if (fgets(cmdline, sizeof(cmdline), file) == NULL) {
        fclose(file);
        return -1;
    }
    fclose(file);

    field = strstr(cmdline, "rmem=");
    if (field == NULL)
        return -1;
    field += strlen("rmem=");
    amount = strtoul(field, &endp, 0);
    if (endp == field || amount == 0)
        return -1;

    bytes = amount;
    if (*endp == 'M' || *endp == 'm') {
        bytes *= 1024u * 1024u;
        endp++;
    } else if (*endp == 'K' || *endp == 'k') {
        bytes *= 1024u;
        endp++;
    }
    if (*endp != '@' || bytes < T23_TRACE_REGION_SIZE ||
        bytes > UINT32_MAX)
        return -1;

    field = endp + 1;
    base = strtoul(field, &endp, 0);
    if (endp == field || base > UINT32_MAX ||
        (uint64_t)base + bytes > UINT32_MAX)
        return -1;

    *base_out = (uint32_t)base;
    *size_out = (uint32_t)bytes;
    return 0;
}

static void t23_flush(void *address, uint32_t size, uint32_t direction)
{
    T23RmemFlush flush;

    if (t23_trace_fd < 0 || address == NULL || size == 0)
        return;
    flush.addr = (uint32_t)(uintptr_t)address;
    flush.size = size;
    flush.dir = direction;
    (void)ioctl(t23_trace_fd, RMEM_IOCTL_FLUSH_CACHE, &flush);
}

static int t23_trace_init(void)
{
    uint32_t rmem_base;
    uint32_t rmem_size;
    uint32_t trace_phys;

    if (t23_trace_header != NULL)
        return 0;
    if (t23_parse_rmem(&rmem_base, &rmem_size) != 0)
        return -1;

    trace_phys = rmem_base + rmem_size - T23_TRACE_REGION_SIZE;
    t23_trace_fd = open("/dev/rmem", O_RDWR | O_CLOEXEC);
    if (t23_trace_fd < 0)
        return -1;
    t23_trace_mapping = mmap(NULL, T23_TRACE_REGION_SIZE,
                             PROT_READ | PROT_WRITE, MAP_SHARED,
                             t23_trace_fd, (off_t)trace_phys);
    if (t23_trace_mapping == MAP_FAILED) {
        t23_trace_mapping = NULL;
        close(t23_trace_fd);
        t23_trace_fd = -1;
        return -1;
    }

    t23_trace_header = (T23PersistHeader *)t23_trace_mapping;
    memset(t23_trace_header, 0, T23_TRACE_REGION_SIZE);
    memcpy(t23_trace_header->magic, "OIT23TRC", 8u);
    t23_trace_header->version = T23_TRACE_VERSION;
    t23_trace_header->header_size = sizeof(*t23_trace_header);
    t23_trace_header->capacity =
        T23_TRACE_REGION_SIZE - sizeof(*t23_trace_header);
    t23_trace_header->rmem_base = rmem_base;
    t23_trace_header->rmem_size = rmem_size;
    t23_flush(t23_trace_header, T23_TRACE_REGION_SIZE, 1u);
    return 0;
}

int openimp_t23_persist_enabled(void)
{
    if (!t23_trace_checked) {
        const char *value = getenv("OPENIMP_RMEM_TRACE");
        const char *file = getenv("OPENIMP_PERSIST_TRACE_FILE");

        t23_trace_enabled_value =
            (value != NULL && value[0] != '\0' && value[0] != '0') ||
            (file != NULL && file[0] == '/');
        t23_trace_checked = 1;
    }
    return t23_trace_enabled_value;
}

static int t23_trace_file_init(void)
{
    const char *path;
    struct stat status;

    if (t23_trace_file_fd >= 0)
        return 0;
    path = getenv("OPENIMP_PERSIST_TRACE_FILE");
    if (path == NULL || path[0] != '/')
        return -1;
    t23_trace_file_fd = open(path, O_WRONLY | O_APPEND | O_CREAT |
                             O_SYNC | O_CLOEXEC, 0600);
    if (t23_trace_file_fd < 0)
        return -1;
    if (fstat(t23_trace_file_fd, &status) == 0 && status.st_size > 0)
        t23_trace_file_bytes = (uint64_t)status.st_size > UINT32_MAX
            ? UINT32_MAX : (uint32_t)status.st_size;
    return 0;
}

static void t23_ring_copy(T23PersistHeader *header, const char *data,
                          uint32_t size)
{
    uint32_t first;

    if (size >= header->capacity) {
        data += size - header->capacity;
        size = header->capacity;
        header->write_pos = 0;
        header->valid = 0;
        header->wraps++;
    }

    first = header->capacity - header->write_pos;
    if (first > size)
        first = size;
    memcpy(header->data + header->write_pos, data, first);
    t23_flush(header->data + header->write_pos, first, 1u);
    if (size > first) {
        memcpy(header->data, data + first, size - first);
        t23_flush(header->data, size - first, 1u);
        header->wraps++;
    }
    header->write_pos = (header->write_pos + size) % header->capacity;
    if (header->valid < header->capacity) {
        uint32_t available = header->capacity - header->valid;

        header->valid += size < available ? size : available;
    }
}

void openimp_t23_persist_write(const char *message, size_t size)
{
    char record[512];
    const char *file_path;
    int prefix_size;
    uint32_t record_size;
    uint32_t record_number;

    if (!openimp_t23_persist_enabled() || message == NULL || size == 0)
        return;

    pthread_mutex_lock(&t23_trace_lock);
    file_path = getenv("OPENIMP_PERSIST_TRACE_FILE");
    if (file_path != NULL && file_path[0] == '/') {
        if (t23_trace_file_init() != 0) {
            pthread_mutex_unlock(&t23_trace_lock);
            return;
        }
        record_number = ++t23_trace_file_records;
    } else {
        if (t23_trace_init() != 0) {
            pthread_mutex_unlock(&t23_trace_lock);
            return;
        }
        record_number = t23_trace_header->records + 1u;
    }

    prefix_size = snprintf(record, sizeof(record), "%06u ",
                           record_number);
    if (prefix_size < 0) {
        pthread_mutex_unlock(&t23_trace_lock);
        return;
    }
    if (size > sizeof(record) - (size_t)prefix_size - 1u)
        size = sizeof(record) - (size_t)prefix_size - 1u;
    memcpy(record + prefix_size, message, size);
    record_size = (uint32_t)prefix_size + (uint32_t)size;
    if (record_size == 0 || record[record_size - 1u] != '\n')
        record[record_size++] = '\n';

    if (file_path != NULL && file_path[0] == '/') {
        if (t23_trace_file_bytes + record_size <= T23_TRACE_FILE_LIMIT) {
            ssize_t written = write(t23_trace_file_fd, record, record_size);

            if (written > 0)
                t23_trace_file_bytes += (uint32_t)written;
            (void)fsync(t23_trace_file_fd);
        }
        pthread_mutex_unlock(&t23_trace_lock);
        return;
    }

    t23_ring_copy(t23_trace_header, record, record_size);
    t23_trace_header->records++;
    t23_flush(t23_trace_header, sizeof(*t23_trace_header), 1u);
    pthread_mutex_unlock(&t23_trace_lock);
}

void openimp_t23_persist_trace(const char *format, ...)
{
    char message[448];
    va_list arguments;
    int length;
    size_t size;

    if (!openimp_t23_persist_enabled())
        return;
    va_start(arguments, format);
    length = vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    if (length <= 0)
        return;
    size = (size_t)length;
    if (size >= sizeof(message))
        size = sizeof(message) - 1u;
    openimp_t23_persist_write(message, size);
}
