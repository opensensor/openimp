/*
 * T10 / T20 IMP_Encoder_SetFisheyeEnableStatus: the vendor's per-picture
 * region of interest (issues #3, #5).
 *
 * The flag (channel record +0x140, settable only before IMP_Encoder_CreateChn)
 * is handed to the vendor encoder as i264e parameter word 0x72.  Its only
 * reader in libimp 3.12.0 (one binary for T10 and T20) is i264e_icodec_enc,
 * which i264e_slice_write calls for every coding try of every picture: with
 * the flag set it
 *   - clears the 56-byte ROI table of the picture (8 regions x 7 bytes
 *     {enable, relative, qp, x0, x1, y0, y1}); an application ROI
 *     (IMP_Encoder_SetChnROI) is lost;
 *   - for an I slice, and for any picture at a whole second of the frame rate
 *     (counter * fps_den % fps_num == 0), sets region 0 to
 *     {1, 0, min(slice QP, 15), 0, 3, 0, 1}: the top-left 64x32 pixels
 *     (macroblocks x 0..3, y 0..1) at an absolute QP of 15 or better.
 * With the flag clear the table is left alone.
 *
 * T21 (libimp 1.0.33, issue #7): i264e_icodec_enc is the same function (tools/fisheye_oracle.py
 * gives byte-identical vectors for both gcc builds).  But the T21 hardware path
 * (hwicodec_pf_h264e_t21_enc, H264E_T21_SliceInit) never reads that table: a
 * stream coded with the flag set shows no region and no application ROI on a T21
 * camera (docs/T21_FISHEYE.md).  fisheye_roi_hw() is the table the hardware
 * gets: T10/T20 the table of fisheye_roi(), T21 nothing.
 *
 * Pure C: tests/fisheye checks it against tools/fisheye_oracle.py (the vendor
 * function run under unicorn).
 */
#ifndef OPENIMP_FISHEYE_H
#define OPENIMP_FISHEYE_H

#include <stdint.h>

#define FISHEYE_QP_MAX 15       /* QP of the top-left region is min(slice QP, 15) */

/* i264e_icodec_enc, ROI part: roi is the table of the slice about to be coded.
 * counter is the picture counter of the vendor context (word 0), i_slice 1 for
 * an I slice (IDR), qp the slice QP of this try. */
void fisheye_roi(int enable, uint32_t counter, int i_slice, uint32_t fps_num,
                 uint32_t fps_den, int qp, uint8_t roi[8][7]);

/* The regions the encoder hardware is programmed with.  t21 selects the T21
 * hardware path: no region of the i264e table reaches it.  With the flag set
 * the application's table is dropped, like the vendor's, and no region 0 is
 * coded; with the flag clear the table is left alone. */
void fisheye_roi_hw(int t21, int enable, uint32_t counter, int i_slice,
                    uint32_t fps_num, uint32_t fps_den, int qp,
                    uint8_t roi[8][7]);

#endif
