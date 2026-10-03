/* RGB -> YUV step of the T31 IMP_ISP_Tuning_SetMask (stock libimp 1.1.6).
 * Expected values: the stock sequence of double operations (constants read
 * from libimp.so .rodata) evaluated outside of this code. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "isp/isp_mask_rgb2yuv.h"

static const struct { uint8_t rgb[3], yuv[3]; } vec[] = {
    { {0, 0, 0}, {0, 128, 128} },
    { {255, 255, 255}, {255, 128, 128} },
    { {255, 0, 0}, {76, 84, 255} },
    { {0, 255, 0}, {149, 43, 21} },
    { {0, 0, 255}, {29, 255, 107} },
    { {255, 255, 0}, {225, 0, 148} },
    { {0, 255, 255}, {178, 171, 0} },
    { {255, 0, 255}, {105, 212, 234} },
    { {128, 128, 128}, {127, 128, 128} },
    { {1, 2, 3}, {1, 128, 127} },
    { {200, 100, 50}, {124, 86, 182} },
    { {17, 99, 240}, {90, 212, 75} },
    { {254, 1, 127}, {91, 148, 244} },
};

static int fails;

#define CHECK(c, ...) do { if (!(c)) { fails++; printf(__VA_ARGS__); } } while (0)

int main(void)
{
    uint8_t attr[ISP_MASK_ATTR_SIZE], ref[ISP_MASK_ATTR_SIZE];
    unsigned i, k;
    int32_t type;

    for (i = 0; i < sizeof(vec) / sizeof(vec[0]); i++) {
        uint8_t c[3];

        memcpy(c, vec[i].rgb, 3);
        isp_mask_rgb_to_yuv(c);
        CHECK(!memcmp(c, vec[i].yuv, 3), "rgb %u/%u/%u -> %u/%u/%u, want %u/%u/%u\n",
              vec[i].rgb[0], vec[i].rgb[1], vec[i].rgb[2], c[0], c[1], c[2],
              vec[i].yuv[0], vec[i].yuv[1], vec[i].yuv[2]);
    }

    /* mask_type 0: every block colour converted, nothing else touched */
    for (i = 0; i < sizeof(attr); i++)
        attr[i] = (uint8_t)(i * 37 + 11);
    type = 0;
    memcpy(attr + ISP_MASK_TYPE_OFFSET, &type, 4);
    memcpy(ref, attr, sizeof(attr));
    isp_mask_attr_rgb_to_yuv(attr);
    for (k = 0; k < 12; k++) {
        uint8_t *v = ref + k * ISP_MASK_BLOCK_SIZE + ISP_MASK_VALUE_OFFSET;
        isp_mask_rgb_to_yuv(v);
    }
    CHECK(!memcmp(attr, ref, sizeof(attr)), "type 0 attr mismatch\n");

    /* mask_type 1 (YUV) and others: untouched */
    for (type = 1; type <= 2; type++) {
        memcpy(attr + ISP_MASK_TYPE_OFFSET, &type, 4);
        memcpy(ref, attr, sizeof(attr));
        isp_mask_attr_rgb_to_yuv(attr);
        CHECK(!memcmp(attr, ref, sizeof(attr)), "type %d changed\n", (int)type);
    }

    if (fails)
        return 1;
    printf("t31 mask rgb2yuv: OK\n");
    return 0;
}
