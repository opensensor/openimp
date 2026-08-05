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
#include <unistd.h>

#ifndef AVPU_IOC_MAGIC
#define AVPU_IOC_MAGIC 'q'
#endif

struct avpu_reg {
    unsigned int id;
    unsigned int value;
};

#define AL_CMD_IP_WRITE_REG _IOWR(AVPU_IOC_MAGIC, 10, struct avpu_reg)
#define AL_CMD_IP_READ_REG  _IOWR(AVPU_IOC_MAGIC, 11, struct avpu_reg)

typedef int (*ioctl_fn)(int fd, unsigned long request, ...);

static ioctl_fn real_ioctl;
static unsigned int snapshot_count;

static void append_snapshot(int fd)
{
    static const unsigned int regs[] = {
        0x8400, 0x8404, 0x8408, 0x840c,
        0x8410, 0x8414, 0x8418, 0x841c,
        0x8420, 0x8424, 0x8428,
        0x85e4, 0x85f0, 0x85f4
    };
    const char *path = getenv("AVPU_REG_SNAPSHOT_LOG");
    char line[768];
    size_t used = 0;
    unsigned int i;
    int out;

    if (!path || !*path)
        path = "/tmp/avpu-reg-snapshot.log";

    used += (size_t)snprintf(line + used, sizeof(line) - used,
                            "pre-cl-push snapshot=%u", snapshot_count);
    for (i = 0; i < sizeof(regs) / sizeof(regs[0]); ++i) {
        struct avpu_reg reg;
        int ret;

        reg.id = regs[i];
        reg.value = 0;
        ret = real_ioctl(fd, AL_CMD_IP_READ_REG, &reg);
        used += (size_t)snprintf(line + used, sizeof(line) - used,
                                " %04x=%08x(%d)",
                                regs[i], reg.value, ret);
        if (used >= sizeof(line))
            break;
    }
    if (used < sizeof(line) - 1)
        line[used++] = '\n';

    out = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (out >= 0) {
        (void)write(out, line, used);
        (void)close(out);
    }
}

int ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    void *arg;

    if (!real_ioctl)
        real_ioctl = (ioctl_fn)dlsym(RTLD_NEXT, "ioctl");
    if (!real_ioctl) {
        errno = ENOSYS;
        return -1;
    }

    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);

    if (request == AL_CMD_IP_WRITE_REG && arg) {
        const struct avpu_reg *reg = (const struct avpu_reg *)arg;

        if (reg->id == 0x83e4 && reg->value == 2 &&
            snapshot_count < 4) {
            ++snapshot_count;
            append_snapshot(fd);
        }
    }

    return real_ioctl(fd, request, arg);
}
