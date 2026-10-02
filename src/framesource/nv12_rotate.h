/*
 * NV12 rotation for the T31 FrameSource (IMP_FrameSource_SetChnRotate).
 *
 * The vendor libimp rotates in software (nv12_left_rotate_90 /
 * nv12_right_rotate_90, 32x32 blocks); see docs/T31_ROTATE.md. This is a
 * portable plain-C version: 32x32 tiles, 4x4 byte (luma) and 2x2 pair
 * (chroma) transposes in 32-bit registers.
 *
 * Buffers use the OpenIMP T31 NV12 layout, the one the AVPU source words
 * are built from: luma pitch = ALIGN16(width), ALIGN16(height) luma lines,
 * the interleaved CbCr plane starts at pitch * ALIGN16(height).
 */
#ifndef OPENIMP_NV12_ROTATE_H
#define OPENIMP_NV12_ROTATE_H

#include <stddef.h>
#include <stdint.h>

/* Values of the IMP_FrameSource_SetChnRotate rotTo90 argument. 1 and 2 are
 * the vendor values; 3 (180 degrees) is an OpenIMP extension that the
 * vendor library ignores (it then delivers the frame unrotated). */
enum {
    NV12_ROT_NONE    = 0,
    NV12_ROT_90_CCW  = 1,   /* vendor nv12_left_rotate_90 */
    NV12_ROT_90_CW   = 2,   /* vendor nv12_right_rotate_90 */
    NV12_ROT_180     = 3,
};

static inline uint32_t nv12_rotate_pitch(uint32_t width)
{
    return (width + 15u) & ~15u;
}

static inline uint32_t nv12_rotate_lines(uint32_t height)
{
    return (height + 15u) & ~15u;
}

/* Bytes of a width x height frame in the layout above. */
static inline size_t nv12_rotate_frame_size(uint32_t width, uint32_t height)
{
    return (size_t)nv12_rotate_pitch(width) * nv12_rotate_lines(height) * 3u / 2u;
}

/* Output dimensions of a mode; returns -1 for an unknown mode. */
int nv12_rotate_out_dims(int mode, uint32_t width, uint32_t height,
                         uint32_t *out_width, uint32_t *out_height);

/* Rotates one plane of 1-byte (bpp 1, luma) or 2-byte (bpp 2, CbCr pair)
 * samples. width/height are in samples of the source plane, strides in
 * bytes. src and dst must not overlap. */
void nv12_rotate_plane(const uint8_t *src, uint32_t src_stride,
                       uint8_t *dst, uint32_t dst_stride,
                       uint32_t width, uint32_t height, int bpp, int mode);

/* Rotates a whole width x height NV12 frame from src into dst (both in the
 * layout above, dst sized nv12_rotate_frame_size of the output dims).
 * width and height must be even. Returns 0, or -1 for bad arguments. */
int nv12_rotate(const uint8_t *src, uint8_t *dst, uint32_t width,
                uint32_t height, int mode);

#endif
