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
    /* Quality cap of the OEM CappedVBR/CappedQuality modes (AL_RC_CAPPED_VBR
     * 4, IMP CAPPED_QUALITY 8): while the PSNR of the last completed picture
     * is above max_psnr_x100 (dB * 100), a QP decrease is not taken.  0 = no
     * cap.  last_psnr_x100 0 = no measurement. */
    uint32_t max_psnr_x100;
    uint32_t last_psnr_x100;
    uint32_t quality_cap_holds; /* QP decreases suppressed by the cap */
#if defined(PLATFORM_T23)
    /* Optional decision band (the T23 native encoder's VBR/SMART
     * parameters); 0 keeps the built-in constant.  Other SoCs build the
     * controller without it, unchanged. */
    uint32_t lower_qp_percent;  /* lower QP below this % of the target */
    uint32_t raise_qp_percent;  /* raise QP above this % of the target */
    uint32_t over_target_limit; /* GOPs over before a raise */
    uint32_t under_target_limit; /* GOPs under before a lower */
#endif
} OpenIMPT31RateController;

int openimp_t31_rate_controller_init(OpenIMPT31RateController *controller,
                                     uint32_t bitrate, uint32_t fps_num,
                                     uint32_t fps_den, uint32_t gop_length,
                                     uint32_t min_qp, uint32_t max_qp,
                                     uint32_t initial_qp);

/* Retarget an active controller without throwing away its normalized scene
 * model.  This is used by the T21/T30 Helix path as well as the direct T31
 * encoder when a consumer changes bitrate between frames. */
int openimp_t31_rate_controller_set_bitrate(
    OpenIMPT31RateController *controller, uint32_t bitrate);

/* Complete one access unit.  QP changes only at a completed GOP boundary. */
int openimp_t31_rate_controller_complete(
    OpenIMPT31RateController *controller, uint32_t completed_bits,
    uint32_t used_qp, int is_idr);

uint32_t openimp_t31_rate_controller_qp(
    const OpenIMPT31RateController *controller);

/* Set the quality cap (dB * 100, 0 = off) of an initialized controller;
 * init() clears it. */
int openimp_t31_rate_controller_set_quality_cap(
    OpenIMPT31RateController *controller, uint32_t max_psnr_x100);

/* PSNR (dB * 100) of the picture about to be passed to complete(); 0 when
 * the hardware gave no measurement. */
void openimp_t31_rate_controller_note_psnr(
    OpenIMPT31RateController *controller, uint32_t psnr_x100);

/* PSNR in dB * 100 from the picture's sum of squared errors, computed as the
 * OEM T31 rate control does (libimp 1.1.6, CappedVBR update 0x55540):
 * mse1000 = max(1, sse * 1000 / num_pel), psnr = 1000 * log10(max_pel^2 *
 * 1000 / mse1000), truncated.  Integer only (no libm).  0 for num_pel 0 or
 * max_pel 0. */
uint32_t openimp_t31_psnr_x100(uint64_t sse, uint32_t num_pel,
                               uint32_t max_pel);

#if defined(PLATFORM_T23)
/* Set the decision band of an initialized controller; 0 for any value
 * keeps its built-in constant.  init() resets the band. */
int openimp_t31_rate_controller_set_band(
    OpenIMPT31RateController *controller, uint32_t lower_percent,
    uint32_t raise_percent, uint32_t over_gops, uint32_t under_gops);
#endif

#endif
