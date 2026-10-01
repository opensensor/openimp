/*
 * jpeg_ql_test - IMP_Encoder_SetJpegeQl tables in the T23 software JPEG.
 *
 * Builds src/hw_encoder.c with PLATFORM_T23 and checks
 * HW_Encoder_Encode_NV12_JPEG_Tables against the quality path:
 *   - user tables equal to the quality-75 tables (in DQT/zigzag order, as
 *     IMPEncoderJpegeQl.qmem_table holds them) give a byte-identical JPEG,
 *     so the table order and the quantizer set-up match;
 *   - other user tables appear unchanged in the two DQT segments (luma
 *     first, chroma second), and finer steps give a larger image;
 *   - a zero step is used as 1 instead of dividing by zero.
 *
 * The frame record carries 32-bit addresses, so the test is linked
 * without PIE (heap below 4 GiB) and the frame is mapped there too.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "hw_encoder.h"

#define W 128
#define H 96

static int failures;

#define CHECK(cond, ...) do {                                         \
        if (!(cond)) {                                                \
            failures++;                                               \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);      \
            fprintf(stderr, __VA_ARGS__);                             \
            fputc('\n', stderr);                                      \
        }                                                             \
    } while (0)

/* The quality scaling of hw_encoder.c (IJG), base tables in natural order,
 * written out in zigzag order. */
static const uint8_t zigzag[64] = {
    0, 1, 5, 6, 14, 15, 27, 28, 2, 4, 7, 13, 16, 26, 29, 42,
    3, 8, 12, 17, 25, 30, 41, 43, 9, 11, 18, 24, 31, 40, 44, 53,
    10, 19, 23, 32, 39, 45, 52, 54, 20, 22, 33, 38, 46, 51, 55, 60,
    21, 34, 37, 47, 50, 56, 59, 61, 35, 36, 48, 49, 57, 58, 62, 63
};
static const uint8_t y_base[64] = {
    16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55,
    14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62,
    18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92,
    49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99
};
static const uint8_t uv_base[64] = {
    17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99
};

static void quality_tables(uint8_t out[128], int quality)
{
    int scale = quality < 50 ? 5000 / quality : 200 - quality * 2;
    int i;

    for (i = 0; i < 64; i++) {
        int y = (y_base[i] * scale + 50) / 100;
        int c = (uv_base[i] * scale + 50) / 100;

        out[zigzag[i]] = (uint8_t)(y < 1 ? 1 : y > 255 ? 255 : y);
        out[64 + zigzag[i]] = (uint8_t)(c < 1 ? 1 : c > 255 ? 255 : c);
    }
}

static uint8_t *frame_data;
static size_t frame_size;

static void make_frame(HWFrameBuffer *frame)
{
    int x, y;

    frame_size = (size_t)W * ((H + 15) & ~15) * 3 / 2;
    frame_data = mmap(NULL, frame_size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (frame_data == MAP_FAILED) {
        fprintf(stderr, "cannot map the frame below 4 GiB\n");
        exit(2);
    }
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            frame_data[y * W + x] = (uint8_t)((x * 7 + y * 3 + (x * y) % 13) & 0xff);
    for (x = 0; x < (int)(frame_size - (size_t)W * ((H + 15) & ~15)); x++)
        frame_data[(size_t)W * ((H + 15) & ~15) + x] = (uint8_t)(100 + x % 50);
    memset(frame, 0, sizeof(*frame));
    frame->virt_addr = (uint32_t)(uintptr_t)frame_data;
    frame->size = (uint32_t)frame_size;
    frame->width = W;
    frame->height = H;
    frame->pixfmt = 0x3231564eu;
}

static const uint8_t *stream_bytes(const HWStreamBuffer *stream)
{
    return (const uint8_t *)(uintptr_t)stream->virt_addr;
}

static int encode(HWFrameBuffer *frame, HWStreamBuffer *stream,
                  const uint8_t *tables)
{
    memset(stream, 0, sizeof(*stream));
    if ((tables ? HW_Encoder_Encode_NV12_JPEG_Tables(frame, stream, 75u, tables)
                : HW_Encoder_Encode_NV12_JPEG(frame, stream, 75u)) != 0)
        return -1;
    /* the encoder returns a heap pointer in a 32-bit field */
    if (!stream->virt_addr || !stream->length)
        return -1;
    return 0;
}

/* Returns the 64 table bytes of DQT segment `id`, or NULL. */
static const uint8_t *find_dqt(const uint8_t *jpeg, uint32_t length, int id)
{
    uint32_t i;

    for (i = 0; i + 69 <= length; i++)
        if (jpeg[i] == 0xff && jpeg[i + 1] == 0xdb && jpeg[i + 2] == 0x00 &&
            jpeg[i + 3] == 0x43 && jpeg[i + 4] == id)
            return jpeg + i + 5;
    return NULL;
}

int main(void)
{
    HWFrameBuffer frame;
    HWStreamBuffer reference, custom;
    uint8_t tables[128];
    const uint8_t *dqt;
    uint32_t coarse_length;
    int i;

    if ((uintptr_t)malloc(16) > 0xffffffffu) {
        fprintf(stderr, "heap above 4 GiB: link this test with -no-pie\n");
        return 2;
    }
    make_frame(&frame);

    CHECK(encode(&frame, &reference, NULL) == 0, "quality 75 encode");

    /* the quality-75 tables passed as user tables */
    quality_tables(tables, 75);
    CHECK(encode(&frame, &custom, tables) == 0, "q75 tables encode");
    CHECK(custom.length == reference.length &&
          !memcmp(stream_bytes(&custom), stream_bytes(&reference),
                  reference.length),
          "q75 user tables differ from quality 75 (%u vs %u bytes)",
          custom.length, reference.length);
    free((void *)(uintptr_t)custom.virt_addr);

    /* coarse tables reach the DQT unchanged and shrink the image */
    for (i = 0; i < 128; i++)
        tables[i] = (uint8_t)(i < 64 ? 40 + i : 90 + (i - 64));
    CHECK(encode(&frame, &custom, tables) == 0, "coarse encode");
    dqt = find_dqt(stream_bytes(&custom), custom.length, 0);
    CHECK(dqt && !memcmp(dqt, tables, 64), "luma DQT is not the user table");
    dqt = find_dqt(stream_bytes(&custom), custom.length, 1);
    CHECK(dqt && !memcmp(dqt, tables + 64, 64),
          "chroma DQT is not the user table");
    CHECK(custom.length < reference.length,
          "coarse tables did not shrink the image (%u vs %u)",
          custom.length, reference.length);
    coarse_length = custom.length;
    free((void *)(uintptr_t)custom.virt_addr);

    /* all ones (finest) with a zero step: no crash, step 1 in the DQT */
    memset(tables, 1, sizeof(tables));
    tables[0] = 0;
    tables[64] = 0;
    CHECK(encode(&frame, &custom, tables) == 0, "fine encode");
    dqt = find_dqt(stream_bytes(&custom), custom.length, 0);
    CHECK(dqt && dqt[0] == 1, "zero luma step not written as 1");
    CHECK(custom.length > reference.length && custom.length > coarse_length,
          "finest tables did not grow the image");
    free((void *)(uintptr_t)custom.virt_addr);
    free((void *)(uintptr_t)reference.virt_addr);

    munmap(frame_data, frame_size);
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("T23 JPEG user tables: all checks passed (q75 %u bytes, coarse "
           "%u bytes)\n", reference.length, coarse_length);
    return 0;
}
