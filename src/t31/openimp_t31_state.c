/*
 * T31 platform state required by the stock ISP/FrameSource adapter.
 *
 * Encoding, rate control, and stream ownership live in the shared T40-derived
 * implementation. Keep this file limited to the small ABI seam that differs
 * on the stock T31 kernel.
 */

#include <stdint.h>
#include <time.h>
#if defined(PLATFORM_T20)
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "core/globals.h"

FrameSourceState *gFrameSource;
ISPDevice *gISP;
Module *g_modules[6][IMP_MAX_GROUPS];

/* group.c uses this symbol only as the historical end marker in a legacy
 * clear helper. The shared System lifecycle does not call that helper. */
uint32_t g_block_info_addr;

uint64_t system_gettime(int clock_type)
{
    struct timespec now;
    clockid_t clock_id = clock_type == 0 ? CLOCK_REALTIME : CLOCK_MONOTONIC;

    if (clock_gettime(clock_id, &now) != 0)
        return 0;
    return (uint64_t)now.tv_sec * 1000000u +
           (uint64_t)now.tv_nsec / 1000u;
}

#if defined(PLATFORM_T20)
/* Read one 32-bit SoC register through /dev/mem; -1 when not readable. */
static int t20_read_soc_reg(uint32_t addr, uint32_t *value)
{
    long page = sysconf(_SC_PAGESIZE);
    volatile uint32_t *regs;
    int fd;

    if (page <= 0)
        page = 4096;
    fd = open("/dev/mem", O_RDONLY | O_SYNC | O_CLOEXEC);
    if (fd < 0)
        return -1;
    regs = mmap(NULL, (size_t)page, PROT_READ, MAP_SHARED, fd,
                (off_t)(addr & ~((uint32_t)page - 1u)));
    close(fd);
    if (regs == MAP_FAILED)
        return -1;
    *value = regs[(addr & ((uint32_t)page - 1u)) / 4u];
    munmap((void *)regs, (size_t)page);
    return 0;
}
#endif

int32_t get_cpu_id(void)
{
#if defined(PLATFORM_T20)
    /* The stock T10/T20 libimp is one binary: a T10 (soc id 0x1300002c
     * family 1, id 5) reports 0 (T10) or 1/2 (T10-Lite) by the CPPSR byte
     * at 0x10000034, as the vendor get_cpu_id. Everything else keeps the
     * T20 value this build has always reported. Read once, like the
     * vendor's cached registers. */
    static int32_t cached = -2;

    if (cached == -2) {
        uint32_t soc_id;
        uint32_t cppsr;

        cached = 0x0c; /* T20-X */
        if (t20_read_soc_reg(0x1300002cu, &soc_id) == 0 &&
            (soc_id >> 28) == 1u && ((soc_id >> 12) & 0xffffu) == 5u &&
            t20_read_soc_reg(0x10000034u, &cppsr) == 0) {
            switch (cppsr & 0xffu) {
            case 0x01: cached = 0; break;  /* T10 */
            case 0x00: cached = 1; break;  /* T10-Lite */
            case 0x10: cached = 2; break;  /* T10-Lite */
            default:   cached = -1; break; /* vendor: cppsr error */
            }
        }
    }
    return cached;
#elif defined(PLATFORM_T21)
    return 0x11; /* T21-N */
#elif defined(PLATFORM_T23)
    return 0x0f; /* T23-N */
#else
    return 0x15; /* T31X */
#endif
}

int32_t is_has_simd128(void)
{
    return 0;
}
