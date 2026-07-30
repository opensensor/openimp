#include <errno.h>
#include <fcntl.h>
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

#define AL_CMD_IP_READ_REG _IOWR(AVPU_IOC_MAGIC, 11, struct avpu_reg)

static int read_reg(int fd, unsigned int reg, unsigned int *value)
{
    struct avpu_reg io;

    io.id = reg;
    io.value = 0;
    if (ioctl(fd, AL_CMD_IP_READ_REG, &io) < 0)
        return -1;

    *value = io.value;
    return 0;
}

static int parse_reg(const char *text, unsigned int *out)
{
    char *end = NULL;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || value > 0xffffffffUL)
        return -1;

    *out = (unsigned int)value;
    return 0;
}

int main(int argc, char **argv)
{
    int fd;
    int i;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <reg> [<reg> ...]\n", argv[0]);
        return 2;
    }

    fd = open("/dev/avpu", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "open /dev/avpu failed: %s\n", strerror(errno));
        return 1;
    }

    for (i = 1; i < argc; ++i) {
        unsigned int reg;
        unsigned int value;

        if (parse_reg(argv[i], &reg) != 0) {
            fprintf(stderr, "invalid reg: %s\n", argv[i]);
            close(fd);
            return 2;
        }

        if (read_reg(fd, reg, &value) != 0) {
            fprintf(stderr, "read 0x%04x failed: %s\n", reg, strerror(errno));
            close(fd);
            return 1;
        }

        printf("0x%04x=0x%08x\n", reg, value);
    }

    close(fd);
    return 0;
}
