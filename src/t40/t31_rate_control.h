#ifndef OPENIMP_T31_RATE_CONTROL_H
#define OPENIMP_T31_RATE_CONTROL_H

#include <stdint.h>

/*
 * T31 exposes the completed entropy byte count but no usable OEM software
 * rate-controller object.  Keep the missing closed loop small and explicit:
 * normalize completed-picture sizes to a common QP, average whole GOPs, and
 * move the encoder QP slowly enough that transient motion spends bits instead
 * of becoming a visible quality pump.
 */
typedef struct OpenIMPT31RateController {
    uint32_t bitrate;
    uint32_t fps_num;
    uint32_t fps_den;
    uint32_t gop_length;
    uint32_t target_bits;
    uint32_t min_qp;
    uint32_t max_qp;
    uint32_t current_qp;
    uint32_t model_p_bits;
    uint32_t model_p_qp;
    uint32_t smoothed_p_bits;
    uint32_t smoothed_idr_bits;
    uint32_t picture_target_bits;
    uint32_t completed_pictures;
    uint32_t completed_p_pictures;
    uint64_t gop_model_bits;
    uint32_t gop_pictures;
    uint32_t smoothed_gop_model_bits;
    uint32_t completed_gops;
    uint32_t over_target_gops;
    uint32_t under_target_gops;
    int64_t virtual_buffer_bits;
    int initialized;
} OpenIMPT31RateController;

int openimp_t31_rate_controller_init(OpenIMPT31RateController *controller,
                                     uint32_t bitrate, uint32_t fps_num,
                                     uint32_t fps_den, uint32_t gop_length,
                                     uint32_t min_qp, uint32_t max_qp,
                                     uint32_t initial_qp);

/* Complete one access unit.  QP changes only at a completed GOP boundary. */
int openimp_t31_rate_controller_complete(
    OpenIMPT31RateController *controller, uint32_t completed_bits,
    uint32_t used_qp, int is_idr);

uint32_t openimp_t31_rate_controller_qp(
    const OpenIMPT31RateController *controller);

#endif
