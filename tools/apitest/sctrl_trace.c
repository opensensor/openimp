/*
 * LD_PRELOAD shim: records the ISP tuning controls a libimp issues
 * (VIDIOC_S_CTRL 0xc008561c / VIDIOC_G_CTRL 0xc008561b on the tuning node)
 * to the file named by SCTRL_LOG, one line per call:
 *   <monotonic us> S|G <id hex> <value> <return>
 * Used by the apitest case `demask` to compare the per-picture sharpness
 * calls of the vendor libimp and OpenIMP on the same camera (issues #3 #5).
 * Build: <toolchain>-gcc -O2 -shared -fPIC -o libsctrl_trace.so sctrl_trace.c -ldl
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static int (*real_ioctl)(int, unsigned long, ...);
static FILE *out;

static void setup(void)
{
    const char *path;

    if (real_ioctl)
        return;
    real_ioctl = dlsym(RTLD_NEXT, "ioctl");
    path = getenv("SCTRL_LOG");
    if (path)
        out = fopen(path, "a");
}

int ioctl(int fd, unsigned long req, ...)
{
    va_list ap;
    void *arg;
    int r;

    setup();
    va_start(ap, req);
    arg = va_arg(ap, void *);
    va_end(ap);
    r = real_ioctl(fd, req, arg);
    if (out && (req == 0xc008561cUL || req == 0xc008561bUL) && arg) {
        const unsigned int *c = (const unsigned int *)arg;
        struct timespec ts;

        clock_gettime(CLOCK_MONOTONIC, &ts);
        fprintf(out, "%llu %c %#x %u %d\n",
                (unsigned long long)ts.tv_sec * 1000000ull + (unsigned long long)ts.tv_nsec / 1000ull,
                req == 0xc008561cUL ? 'S' : 'G', c[0], c[1], r);
        fflush(out);
    }
    return r;
}
