#ifndef OPENIMP_ISP_MASK_RGB2YUV_H
#define OPENIMP_ISP_MASK_RGB2YUV_H

#include <stdint.h>

/*
 * RGB -> YUV step of the stock T31 IMP_ISP_Tuning_SetMask (libimp 1.1.6,
 * 0x96668): with mask_type 0 (RGB) every block colour of the 0xac-byte
 * IMPISPMASKAttr (3 channels x 4 blocks of 14 bytes, colour at +10..+12,
 * mask_type at +0xa8) is turned into YUV in the caller's struct before the
 * ioctl.  BT.601 full range in doubles, same operation order as the stock
 * (mul.d/add.d/sub.d, no fused ops), then trunc.w.d with the compiler's
 * unsigned fix-up and a byte store (no clamping).
 */
#define ISP_MASK_ATTR_SIZE      0xac
#define ISP_MASK_BLOCK_SIZE     14
#define ISP_MASK_VALUE_OFFSET   10
#define ISP_MASK_TYPE_OFFSET    0xa8

static inline uint8_t isp_mask_to_u8(double v)
{
    if (v >= 2147483648.0)
        return (uint8_t)((uint32_t)(int32_t)(v - 2147483648.0) | 0x80000000u);
    return (uint8_t)(int32_t)v;
}

/* Every product goes through a volatile so the compiler cannot contract it
 * into madd.d/msub.d; the rounding then is the stock one on any FPU. */
static inline void isp_mask_rgb_to_yuv(uint8_t *c)
{
    double r = c[0], g = c[1], b = c[2];
    volatile double p0, p1, p2;
    double y, u, v;

    p0 = r * 0.299; p1 = g * 0.587; p2 = b * 0.114;
    y = p0 + p1;
    y = y + p2;
    p0 = r * -0.168736; p1 = g * 0.331264; p2 = b * 0.5;
    u = p0 - p1;
    u = u + p2;
    u = u + 128.0;
    p0 = r * 0.5; p1 = g * 0.418688; p2 = b * 0.081312;
    v = p0 - p1;
    v = v - p2;
    v = v + 128.0;
    c[0] = isp_mask_to_u8(y);
    c[1] = isp_mask_to_u8(u);
    c[2] = isp_mask_to_u8(v);
}

/* In place, only when mask_type == 0, all 12 blocks (enabled or not). */
static inline void isp_mask_attr_rgb_to_yuv(void *attr)
{
    uint8_t *p = (uint8_t *)attr;
    int32_t type;
    unsigned i;

    __builtin_memcpy(&type, p + ISP_MASK_TYPE_OFFSET, sizeof(type));
    if (type != 0)
        return;
    for (i = 0; i < 12; i++)
        isp_mask_rgb_to_yuv(p + i * ISP_MASK_BLOCK_SIZE + ISP_MASK_VALUE_OFFSET);
}

#endif
