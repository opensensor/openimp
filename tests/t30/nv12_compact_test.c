/*
 * Compact NV12 copy of IMP_FrameSource_GetFrame (src/framesource/nv12_compact.h, #202):
 * a capture frame of 640x360 (chroma after ALIGN16(360) = 368 luma lines)
 * becomes luma w*h, chroma at w*h, size w*h*3/2. Checks the layout for a
 * set of sizes, that the source stays untouched and that nothing is written
 * outside the destination, and prints the copy time for 640x360 (host CPU:
 * the figure for a T30 core is larger, see the issue comment).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "framesource/nv12_compact.h"

static int failures;

#define CHECK(cond, ...) do {                                   \
        if (!(cond)) {                                          \
            failures++;                                         \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);         \
            printf(__VA_ARGS__);                                \
            putchar('\n');                                      \
        }                                                       \
    } while (0)

static uint8_t pat(size_t o)
{
    return (uint8_t)((o * 31u) ^ (o >> 7) ^ (o >> 15));
}

static void check_size(uint32_t w, uint32_t h)
{
    size_t src_size = nv12_compact_src_size(w, h), size = nv12_compact_size(w, h);
    size_t luma = (size_t)w * h, gap = (size_t)w * nv12_compact_aligned_height(h);
    uint8_t *src = malloc(src_size), *copy = malloc(src_size), *dst = malloc(size + 64);
    size_t i, ret;

    for (i = 0; i < src_size; i++)
        src[i] = pat(i);
    memcpy(copy, src, src_size);
    memset(dst, 0xa5, size + 64);

    CHECK(size == (size_t)w * h * 3 / 2, "%ux%u size", w, h);
    CHECK(nv12_compact_needed(w, h) == ((h & 15u) != 0), "%ux%u needed", w, h);
    ret = nv12_compact_copy(dst, src, w, h);
    CHECK(ret == size, "%ux%u returned %zu, want %zu", w, h, ret, size);
    CHECK(memcmp(dst, src, luma) == 0, "%ux%u luma differs", w, h);
    CHECK(memcmp(dst + luma, src + gap, luma / 2) == 0,
          "%ux%u chroma is not the capture chroma", w, h);
    for (i = 0; i < luma / 2; i++)
        if (dst[luma + i] != pat(gap + i)) {
            CHECK(0, "%ux%u chroma byte %zu", w, h, i);
            break;
        }
    for (i = size; i < size + 64; i++)
        if (dst[i] != 0xa5) {
            CHECK(0, "%ux%u wrote past the end at %zu", w, h, i);
            break;
        }
    CHECK(memcmp(src, copy, src_size) == 0, "%ux%u source changed", w, h);
    if (nv12_compact_needed(w, h))
        CHECK(memcmp(dst + luma, src + luma, luma / 2) != 0,
              "%ux%u: chroma at w*h would equal the capture bytes (test blind)", w, h);
    free(src);
    free(copy);
    free(dst);
}

int main(void)
{
    static const uint32_t sizes[][2] = {
        { 640, 360 }, { 320, 180 }, { 1920, 1080 }, { 1280, 720 },
        { 640, 368 }, { 2304, 1296 }, { 16, 2 }, { 640, 480 },
    };
    size_t k;
    uint32_t w = 640, h = 360;
    size_t src_size = nv12_compact_src_size(w, h), size = nv12_compact_size(w, h);
    uint8_t *src = malloc(src_size), *dst = malloc(size);
    struct timespec t0, t1;
    const int loops = 2000;
    volatile uint8_t sink = 0;
    double us;
    int n;

    for (k = 0; k < sizeof(sizes) / sizeof(sizes[0]); k++)
        check_size(sizes[k][0], sizes[k][1]);

    memset(src, 0x55, src_size);
    nv12_compact_copy(dst, src, w, h);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (n = 0; n < loops; n++) {
        src[n & 1023] = (uint8_t)n;             /* keep the copy from being hoisted */
        nv12_compact_copy(dst, src, w, h);
        __asm__ volatile("" : : "r"(dst) : "memory");
        sink += dst[(size_t)n * 211u % size];
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    us = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / loops;
    printf("nv12_compact_copy 640x360: %zu bytes, %.1f us per frame (host, warm cache)\n",
           size, us);
    (void)sink;
    free(src);
    free(dst);
    if (failures) {
        printf("nv12_compact_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("nv12_compact_test: all passed\n");
    return 0;
}
