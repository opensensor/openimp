#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    unsigned long address;
    unsigned long byte_count;
    long page_size;
    off_t page_address;
    size_t page_offset;
    size_t map_size;
    uint8_t *mapping;
    ssize_t written;
    int fd;

    if (argc != 3) {
        fprintf(stderr, "usage: %s <physical-address> <byte-count>\n",
                argv[0]);
        return 2;
    }

    errno = 0;
    address = strtoul(argv[1], NULL, 0);
    byte_count = strtoul(argv[2], NULL, 0);
    if (errno != 0 || address > UINT32_MAX || byte_count == 0u) {
        fprintf(stderr, "invalid address or byte count\n");
        return 2;
    }

    page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        fprintf(stderr, "sysconf(_SC_PAGESIZE) failed\n");
        return 1;
    }

    page_address = (off_t)(address & ~((unsigned long)page_size - 1u));
    page_offset = (size_t)(address - (unsigned long)page_address);
    map_size = page_offset + (size_t)byte_count;

    fd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (fd < 0) {
        fprintf(stderr, "open /dev/mem: %s\n", strerror(errno));
        return 1;
    }

    mapping = mmap(NULL, map_size, PROT_READ, MAP_SHARED, fd, page_address);
    if (mapping == MAP_FAILED) {
        fprintf(stderr, "mmap: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    written = write(STDOUT_FILENO, mapping + page_offset,
                    (size_t)byte_count);
    if (written < 0 || (unsigned long)written != byte_count) {
        fprintf(stderr, "write: %s\n",
                written < 0 ? strerror(errno) : "short write");
        munmap(mapping, map_size);
        close(fd);
        return 1;
    }

    munmap(mapping, map_size);
    close(fd);
    return 0;
}
