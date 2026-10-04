/* Motion v2: opt-in background-model motion analysis for the IVS move
 * interface (beyond vendor), see include/imp/openimp_ivs_move_ex.h.
 *
 * Pure C, integer only (the T10..T23 cores have no FPU), no dependencies.
 * Same threading contract as openimp_t31_ivs_move.h:
 *   ivs_move_v2_next_frame() + ivs_move_v2_feed()   capture thread
 *   ivs_move_v2_run() + ivs_move_v2_result()        IVS channel thread
 * feed and run alternate and never overlap for one instance. */
#ifndef OPENIMP_IVS_MOVE_V2_H
#define OPENIMP_IVS_MOVE_V2_H

#include <stdint.h>

#include "imp/openimp_ivs_move_ex.h"

typedef struct IvsMoveV2 IvsMoveV2;

/* Defaults, then OPENIMP_MOTION_V2* from the environment. */
void ivs_move_v2_config_env(OpenIMP_IVS_MoveConfigEx *cfg);
/* Copy at most cfg->size bytes over the defaults and clamp. */
void ivs_move_v2_config_sanitize(OpenIMP_IVS_MoveConfigEx *dst,
                                 const OpenIMP_IVS_MoveConfigEx *src);

/* interval: analyse every interval-th input frame (skipFrameCnt + 1). */
IvsMoveV2 *ivs_move_v2_create(uint32_t width, uint32_t height,
                              uint32_t interval,
                              const OpenIMP_IVS_MoveConfigEx *cfg);
void ivs_move_v2_destroy(IvsMoveV2 *v2);
/* Takes effect with the next run(); a change of the features restarts the
 * warm-up. */
void ivs_move_v2_set_config(IvsMoveV2 *v2, const OpenIMP_IVS_MoveConfigEx *cfg);

/* Capture thread, once per input frame: 1 when this frame is to be fed. */
int ivs_move_v2_next_frame(IvsMoveV2 *v2);
void ivs_move_v2_feed(IvsMoveV2 *v2, const uint8_t *luma, uint32_t stride,
                      int64_t timestamp);

/* IVS thread: analyse the frame fed last (no-op when none was fed).
 * isp_mode: ISP running mode or -1 if unknown; total_gain: sensor total
 * gain (any linear unit) or 0 if unknown; now_ms: monotonic clock;
 * sense: vendor sense 0..8 of the most sensitive ROI (-1: as 2), scales the
 * thresholds (2 = as configured, 0 = x2, 4 = x0.5). */
void ivs_move_v2_run(IvsMoveV2 *v2, int isp_mode, uint32_t total_gain,
                     int64_t now_ms, int sense);

/* Current analysis state. roi_x0..roi_y1: inclusive ROI rectangles in frame
 * pixels; ret_roi[i] (may be NULL) gets 1 when an object covers ROI i. */
void ivs_move_v2_result(const IvsMoveV2 *v2, OpenIMP_IVS_MoveOutputEx *ex,
                        int roi_cnt, const int *roi_x0, const int *roi_y0,
                        const int *roi_x1, const int *roi_y1, int *ret_roi);

#endif
