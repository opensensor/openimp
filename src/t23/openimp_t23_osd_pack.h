/* T23 OSD picture packing: source formats to the BGRA8888 layout the IPU
 * blends.  Kept in a header so the host test can check it. */
#ifndef OPENIMP_T23_OSD_PACK_H
#define OPENIMP_T23_OSD_PACK_H

#include <stdint.h>
#include <string.h>

#include "t23/openimp_t23_osd_abi.h"

/* The T23 IPU fetches an OSD picture line by line at a pitch of src_w
 * rounded up to whole 64-byte bursts (src_w a multiple of 16 pixels), not
 * src_w * 4 as the T31 driver: any other width shears the picture by
 * (pitch - w) pixels per line (cam-B: a 332-pixel date text came out as
 * diagonal stripes, 4 pixels per line).  Pictures are therefore stored
 * with a 16-pixel pitch, the padding transparent, and handed to the IPU
 * with src_w = pitch. */
#define T23_OSD_PIC_ALIGN 16u

static inline uint32_t t23_osd_pic_pitch(uint32_t w)
{
    return (w + T23_OSD_PIC_ALIGN - 1u) & ~(T23_OSD_PIC_ALIGN - 1u);
}

/* Convert one picture row to BGRA8888 (little-endian B,G,R,A bytes). */
static inline void row_to_bgra(uint8_t *dst, const uint8_t *src, uint32_t w, int fmt,
                        uint32_t x_bit)
{
    uint32_t i;

    switch (fmt) {
    case T23_OSD_PIX_BGRA:
        memcpy(dst, src, w * 4u);
        return;
    case T23_OSD_PIX_ARGB:          /* bytes A,R,G,B */
        for (i = 0; i < w; i++, src += 4, dst += 4) {
            dst[0] = src[3]; dst[1] = src[2]; dst[2] = src[1]; dst[3] = src[0];
        }
        return;
    case T23_OSD_PIX_RGBA:          /* bytes R,G,B,A */
        for (i = 0; i < w; i++, src += 4, dst += 4) {
            dst[0] = src[2]; dst[1] = src[1]; dst[2] = src[0]; dst[3] = src[3];
        }
        return;
    case T23_OSD_PIX_ABGR:          /* bytes A,B,G,R */
        for (i = 0; i < w; i++, src += 4, dst += 4) {
            dst[0] = src[1]; dst[1] = src[2]; dst[2] = src[3]; dst[3] = src[0];
        }
        return;
    case T23_OSD_PIX_RGB555LE:      /* 1555: A in bit 15, R in 14..10 */
    case T23_OSD_PIX_BGR555LE:
        for (i = 0; i < w; i++, src += 2, dst += 4) {
            uint32_t p = (uint32_t)src[0] | (uint32_t)src[1] << 8;
            uint32_t hi = (p >> 10) & 31u, mid = (p >> 5) & 31u, lo = p & 31u;
            uint8_t c_hi = (uint8_t)(hi << 3 | hi >> 2);
            uint8_t c_mid = (uint8_t)(mid << 3 | mid >> 2);
            uint8_t c_lo = (uint8_t)(lo << 3 | lo >> 2);

            if (fmt == T23_OSD_PIX_RGB555LE) {
                dst[0] = c_lo; dst[1] = c_mid; dst[2] = c_hi;
            } else {
                dst[0] = c_hi; dst[1] = c_mid; dst[2] = c_lo;
            }
            dst[3] = (p & 0x8000u) ? 0xff : 0x00;
        }
        return;
    case T23_OSD_PIX_MONOWHITE:     /* 1 bpp, msb first, 1 = white */
    default:
        for (i = 0; i < w; i++, dst += 4) {
            uint32_t bit = x_bit + i;
            int on = (src[bit >> 3] >> (7u - (bit & 7u))) & 1;

            dst[0] = dst[1] = dst[2] = on ? 0xff : 0x00;
            dst[3] = on ? 0xff : 0x00;
        }
        return;
    }
}

static inline uint32_t src_stride(int fmt, uint32_t w)
{
    switch (fmt) {
    case T23_OSD_PIX_BGRA:
    case T23_OSD_PIX_ARGB:
    case T23_OSD_PIX_RGBA:
    case T23_OSD_PIX_ABGR:
        return w * 4u;
    case T23_OSD_PIX_RGB555LE:
    case T23_OSD_PIX_BGR555LE:
        return w * 2u;
    default:
        return (w + 7u) / 8u;
    }
}

/* Convert a w x h picture of `fmt` into BGRA with a pitch of `pitch`
 * pixels (>= w); the pixels right of w are transparent black. */
static inline void t23_osd_pack(uint8_t *dst, uint32_t pitch,
                                const uint8_t *src, uint32_t w, uint32_t h,
                                int fmt)
{
    uint32_t stride = src_stride(fmt, w), y;

    for (y = 0; y < h; y++) {
        uint8_t *row = dst + y * pitch * 4u;

        if (fmt == T23_OSD_PIX_MONOWHITE)
            row_to_bgra(row, src, w, fmt, y * w);
        else
            row_to_bgra(row, src + y * stride, w, fmt, 0);
        if (pitch > w)
            memset(row + w * 4u, 0, (pitch - w) * 4u);
    }
}

#endif
