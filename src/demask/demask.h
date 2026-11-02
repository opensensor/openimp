/*
 * T10 / T20 IMP_Encoder_SetChnDemask: the vendor's "demask" strategy
 * (issues #3, #5).
 *
 * The vendor libimp 3.12.0 (one binary for T10 and T20; the 3.9.0 library
 * has the same code under the name IMP_Encoder_SetChnH264Demask) does not
 * run a de-mosaic filter in the encoder.  do_channel_process_h264e runs
 * encoder_channel_h264e_demask_strategy for every H.264 picture and steps
 * the ISP sharpness (IMP_ISP_Tuning_SetSharpness_internal) by 0x14 per
 * picture between 0x80 and 0: down while the strategy is active, up again
 * afterwards.  The measure the strategy looks at is the QP the encoder gave
 * the previous picture (i264e_encode writes the picture QP into word 0 of
 * the picture record; the channel keeps it at +0x618 after the picture).
 *
 * Pure C, no libimp dependencies: tests/demask checks it against
 * tools/demask_oracle.py (the vendor functions run under unicorn).
 */
#ifndef OPENIMP_DEMASK_H
#define OPENIMP_DEMASK_H

#include <stdint.h>

#define DEMASK_SHARP_MAX   0x80     /* the channel's sharpness word at rest */
#define DEMASK_SHARP_STEP  0x14     /* one picture's step */
#define DEMASK_QP_DEFAULT  30       /* i264e_param_default QP: the measure before the first picture of a
                                     * channel that is not FixQP (FixQP: its qp), channel_i264e_encoder_init */

typedef struct {
    /* IMP_Encoder_SetChnDemask / rcAttr.attrDemask (channel record +0x7c) */
    uint8_t enable;
    uint8_t auto_mode;              /* isAutoMode: the vendor only logs an error */
    int32_t cnt;                    /* demaskCnt: pictures the strategy stays active below the threshold */
    int32_t thr;                    /* demaskThresd: QP threshold */
    /* run-time state, zero at IMP_Encoder_CreateChn */
    int32_t measure;                /* +0x618: QP of the previous picture */
    int32_t hold;                   /* +0x628: hold counter */
    int32_t last;                   /* +0x7cc: measure the counter last ran on */
    int32_t sharp;                  /* +0x7c8: the channel's sharpness word */
} DemaskState;

/* encoder_channel_h264e_demask_strategy: 1 while the strategy is active.
 * Called twice per picture by the vendor: before the picture (only while
 * IMP_Encoder_SetChnFrmUsedMode is off; the result gates the frame re-use
 * path) and after the picture (demask_after_picture). */
int demask_strategy(DemaskState *s);

/* IMP_Encoder_SetChnDemask (no created check): stores the attribute; returns
 * 1 when the caller must restore the ISP sharpness with
 * IMP_ISP_Tuning_SetSharpness_internal(0x80, 0) (enable went 1 -> 0). */
int demask_set(DemaskState *s, int enable, int auto_mode, int32_t cnt,
               int32_t thr);

/* The post-picture block of do_channel_process_h264e while enable is set:
 * runs the strategy, steps the sharpness word and names the ISP call
 * IMP_ISP_Tuning_SetSharpness_internal(*value, *mode) the caller makes.
 * Returns the strategy result.  Not to be called with enable clear. */
int demask_after_picture(DemaskState *s, uint32_t *value, int *mode);

/* the QP of the picture just coded becomes the measure of the next one */
static inline void demask_picture_qp(DemaskState *s, int32_t qp)
{
    s->measure = qp;
}

#endif
