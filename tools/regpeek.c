/*
 * regpeek -- read 32-bit registers through /dev/mem (read only).
 *
 *   regpeek BASE OFF[-END] ...     e.g. regpeek 0x13300000 0x18 0x40 0x540-0x570
 *
 * Prints "OFF=VALUE" per word on one line, prefixed with $IMGFX_CASE when set,
 * so a picture run (IMGFX_HOOK) records the ISP state of every case on the
 * vendor and the open stack alike.
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define MAP_LEN 0x10000

int main(int argc, char **argv)
{
    const char *tag = getenv("IMGFX_CASE");
    unsigned long base;
    volatile uint32_t *m;
    int fd, i;

    if (argc < 3) {
        fprintf(stderr, "usage: %s BASE OFF[-END] ...\n", argv[0]);
        return 2;
    }
    base = strtoul(argv[1], NULL, 0);
    fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (fd < 0) {
        perror("/dev/mem");
        return 1;
    }
    m = mmap(NULL, MAP_LEN, PROT_READ, MAP_SHARED, fd, base);
    if (m == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    printf("[REG] %s", tag ? tag : "-");
    for (i = 2; i < argc; i++) {
        char *dash;
        unsigned long lo = strtoul(argv[i], &dash, 0), hi = lo, o;

        if (*dash == '-')
            hi = strtoul(dash + 1, NULL, 0);
        for (o = lo & ~3UL; o <= hi && o < MAP_LEN; o += 4)
            printf(" %lx=%08x", o, m[o / 4]);
    }
    printf("\n");
    return 0;
}
