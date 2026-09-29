/* T31 IVS motion algorithms ("move" and "base move"), clean-room C.
 *
 * Both algorithms are split into two steps so that the capture path only
 * copies what is needed and the heavy work runs in the IVS channel thread:
 *
 *   *_needs_luma()  does the next input frame have to be read at all?
 *   *_feed()        advance the per-frame state by one input frame and copy
 *                   the luma it needs into private buffers (capture thread)
 *   *_run()         compute the result for the frame fed last (IVS thread)
 *
 * feed() and run() must alternate and must not run concurrently for the
 * same instance; the IVS framework guarantees that with its semaphores.
 * No function here touches the frame after feed() returns. */
#ifndef OPENIMP_T31_IVS_MOVE_H
#define OPENIMP_T31_IVS_MOVE_H

#include <stdint.h>

#include "openimp_t31_ivs_abi.h"

typedef struct T31IvsMove T31IvsMove;
typedef struct T31IvsBaseMove T31IvsBaseMove;

/* ---- move: one yes/no per ROI ---- */
T31IvsMove *t31_ivs_move_create(const IMP_IVS_MoveParam *param);
void t31_ivs_move_destroy(T31IvsMove *move);
/* Width and height the instance expects (param frameInfo). */
void t31_ivs_move_frame_size(const T31IvsMove *move, uint32_t *width,
                             uint32_t *height);
/* Copies sense[], roiRect[] and roiRectCnt (not skipFrameCnt/frameInfo).
 * Returns -1 and changes nothing when a sense value is outside 0..8. */
int t31_ivs_move_set_param(T31IvsMove *move, const IMP_IVS_MoveParam *param);
void t31_ivs_move_get_param(const T31IvsMove *move, IMP_IVS_MoveParam *param);
int t31_ivs_move_needs_luma(const T31IvsMove *move);
/* luma may be NULL when needs_luma() returned 0. */
void t31_ivs_move_feed(T31IvsMove *move, const uint8_t *luma, uint32_t stride);
/* 0: retRoi[0..roiCnt) written, 1: no result for this frame. */
int t31_ivs_move_run(T31IvsMove *move, int *retRoi);

/* ---- base move: one byte per 8x8 block ---- */
T31IvsBaseMove *t31_ivs_base_move_create(const IMP_IVS_BaseMoveParam *param);
void t31_ivs_base_move_destroy(T31IvsBaseMove *base);
void t31_ivs_base_move_frame_size(const T31IvsBaseMove *base, uint32_t *width,
                                  uint32_t *height);
/* (width / 8) * (height / 8) */
int t31_ivs_base_move_datalen(const T31IvsBaseMove *base);
/* Only sense changes take effect. */
int t31_ivs_base_move_set_param(T31IvsBaseMove *base,
                                const IMP_IVS_BaseMoveParam *param);
void t31_ivs_base_move_get_param(const T31IvsBaseMove *base,
                                 IMP_IVS_BaseMoveParam *param);
int t31_ivs_base_move_needs_luma(const T31IvsBaseMove *base);
void t31_ivs_base_move_feed(T31IvsBaseMove *base, const uint8_t *luma,
                            uint32_t stride);
/* Always publishes: *ret = 1 and data filled on detection frames, *ret = 0
 * and data untouched otherwise. */
void t31_ivs_base_move_run(T31IvsBaseMove *base, uint8_t *data, int *ret);

#endif
