#ifndef OPENIMP_T23_JPEGDEC_H
#define OPENIMP_T23_JPEGDEC_H

#include <stddef.h>
#include <stdint.h>

/* Output layouts of t23_jpeg_decode(), the values of the vendor decoder's
 * pixel formats (PIX_FMT_YUV420P = 0, NV12 = 10, NV21 = 11). */
#define T23_JPEGDEC_YUV420P 0
#define T23_JPEGDEC_NV12    10
#define T23_JPEGDEC_NV21    11

/* Baseline (sequential, Huffman, 8 bit) JPEG decoder: gray or YCbCr with any
 * 1x1 / 2x1 / 1x2 / 2x2 luma sampling, restart intervals, 8 and 16 bit
 * quantisation tables.  Progressive, arithmetic-coded and 12 bit files are
 * refused.  The picture comes out as 4:2:0 in the requested layout, chroma
 * straight from the file's Cb / Cr (no colour conversion; gray gets 128),
 * luma stride = width, chroma planes (YUV420P) or the interleaved plane
 * (NV12 / NV21) directly after the luma plane.
 *
 * max_w / max_h: refuse pictures larger than that (0 = no limit).
 * Returns 0 and a malloc()ed picture of w * h * 3 / 2 rounded up (see
 * t23_jpeg_size()), or -1 (nothing allocated). */
int t23_jpeg_decode(const uint8_t *jpeg, size_t len, int pixfmt,
                    uint32_t max_w, uint32_t max_h,
                    uint8_t **out, uint32_t *w, uint32_t *h);

/* Bytes of a decoded picture: w * h + 2 * ((w + 1) / 2) * ((h + 1) / 2). */
size_t t23_jpeg_size(uint32_t w, uint32_t h);

#endif
