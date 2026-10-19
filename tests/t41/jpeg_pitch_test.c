/* Software JPEG of a frame with a line pitch wider than the width (the I2D
 * output, ALIGN32): the stream must equal that of the same picture packed
 * with pitch == width.  Synthetic picture only. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include "hw_encoder.h"

#define W 360u
#define H 640u
#define P 384u
#define PH ((H + 15u) & ~15u)

static uint8_t packed[W * PH + W * (H / 2u)];
static uint8_t padded[P * PH + P * (H / 2u)];

static int encode(uint8_t *buf, size_t size, uint32_t pitch, uint8_t **out,
                  uint32_t *len)
{
    HWFrameBuffer f;
    HWStreamBuffer s;

    memset(&f, 0, sizeof(f));
    memset(&s, 0, sizeof(s));
    f.virt_addr = (uint32_t)(uintptr_t)buf;
    f.size = (uint32_t)size;
    f.width = W;
    f.height = H;
    f.pixfmt = 0x3231564eu;
    f.pitch = pitch;
    if (HW_Encoder_Encode_NV12_JPEG(&f, &s, 75u) != 0)
        return -1;
    *out = (uint8_t *)(uintptr_t)s.virt_addr;
    *len = s.length;
    return 0;
}

int main(void)
{
    uint32_t x, y, la, lb;
    uint8_t *a, *b;

    mallopt(M_MMAP_MAX, 0); /* stream address is a 32-bit field */

    memset(padded, 0xA5, sizeof(padded));
    for (y = 0; y < PH; y++)
        for (x = 0; x < W; x++)
            packed[y * W + x] = padded[y * P + x] =
                (uint8_t)((x * 3u + y * 5u) ^ (x >> 2));
    for (y = 0; y < H / 2u; y++)
        for (x = 0; x < W; x++)
            packed[W * PH + y * W + x] = padded[P * PH + y * P + x] =
                (uint8_t)(100u + ((x + y) & 63u));
    assert(encode(packed, sizeof(packed), 0, &a, &la) == 0);
    assert(encode(padded, sizeof(padded), P, &b, &lb) == 0);
    assert(la == lb && la > 1000u && memcmp(a, b, la) == 0);
    /* wrong view of the padded frame (the bug) differs */
    assert(encode(padded, sizeof(padded), 0, &b, &lb) != 0 ||
           lb != la || memcmp(a, b, la) != 0);
    /* pitch below width and too small a buffer are refused */
    assert(encode(padded, sizeof(padded), W - 16u, &b, &lb) != 0);
    assert(encode(padded, P * PH, P, &b, &lb) != 0);
    puts("jpeg pitch test ok");
    return 0;
}
