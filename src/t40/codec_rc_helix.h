#ifndef OPENIMP_CODEC_RC_HELIX_H
#define OPENIMP_CODEC_RC_HELIX_H

/*
 * T23/T30 IMP_Encoder_SetChnAttrRcMode -> HWEncoderParams (codec-t40.c
 * AL_Codec_Encode_SetRcParam and the CreateChn extras).
 *
 * The H.264 and H.265 rate-control attributes share the union in
 * IMPEncoderAttrRcMode but not their layout:
 *
 *   H.264 CBR  maxQp minQp outBitRate iBiasLvl frmQPStep gopQPStep
 *              adaptiveMode gopRelation (bools)
 *   H.265 CBR  maxQp minQp staticTime outBitRate iBiasLvl frmQPStep
 *              gopQPStep flucLvl
 *   H.264 VBR  maxQp minQp staticTime maxBitRate iBiasLvl changePos
 *              qualityLvl frmQPStep gopQPStep gopRelation (bool)
 *   H.265 VBR  the same nine words, then flucLvl (no gopRelation)
 *
 * The vendor T30 libimp 1.0.5 IMP_Encoder_SetChnAttrRcMode (0xc102c) has
 * one branch per codec: for H.265 it reads words 1..8 of the CBR attribute
 * (maxQp, minQp, staticTime, outBitRate, iBiasLvl, frmQPStep, gopQPStep,
 * flucLvl) and words 1..10 of VBR/SMART into the i265e_set_param 3 block.
 * Reading an H.265 CBR attribute in the H.264 layout takes staticTime as
 * the bit rate (#228: 2 kbit/s, the controller sits at max QP, 361 kbit/s
 * at 1080p25 whatever the target).
 */
#include <stdint.h>

#include "hw_encoder.h"
#include "imp/imp_encoder.h"

static inline uint32_t codec_rc_helix_qp(uint32_t qp)
{
    return qp > 51u ? 51u : qp;
}

/* The rate-control fields of mode into hw.  hevc: the channel is H.265
 * (T30 Radix), mode holds attrH265*.  The extras (and HW_RC_FLAG_APP) are
 * written for CBR/VBR/SMART; FIXQP leaves them zero. */
static inline void codec_rc_helix_from_attr(HWEncoderParams *hw,
                                            const IMPEncoderAttrRcMode *mode,
                                            int hevc)
{
    hw->static_time = 0;
    hw->change_pos = 0;
    hw->quality_level = 0;
    hw->frm_qp_step = 0;
    hw->gop_qp_step = 0;
    hw->bias_level = 0;
    hw->fluct_level = 0;
    hw->rc_flags = 0;
    switch (mode->rcMode) {
    case IMP_ENC_RC_MODE_CBR:
        hw->rc_mode = HW_RC_MODE_CBR;
        if (hevc) {
            const IMPEncoderAttrH265CBR *c = &mode->attrH265Cbr;

            hw->bitrate = c->outBitRate * 1000u;
            hw->min_qp = codec_rc_helix_qp(c->minQp);
            hw->max_qp = codec_rc_helix_qp(c->maxQp);
            hw->static_time = c->staticTime;
            hw->bias_level = c->iBiasLvl;
            hw->frm_qp_step = c->frmQPStep;
            hw->gop_qp_step = c->gopQPStep;
            hw->fluct_level = c->flucLvl;
            hw->rc_flags = HW_RC_FLAG_APP;
        } else {
            const IMPEncoderAttrH264CBR *c = &mode->attrH264Cbr;

            hw->bitrate = c->outBitRate * 1000u;
            hw->min_qp = codec_rc_helix_qp(c->minQp);
            hw->max_qp = codec_rc_helix_qp(c->maxQp);
            hw->bias_level = c->iBiasLvl;
            hw->frm_qp_step = c->frmQPStep;
            hw->gop_qp_step = c->gopQPStep;
            hw->rc_flags = HW_RC_FLAG_APP |
                (c->adaptiveMode ? HW_RC_FLAG_ADAPTIVE : 0u) |
                (c->gopRelation ? HW_RC_FLAG_GOP_RELATION : 0u);
        }
        hw->qp = (hw->min_qp + hw->max_qp) / 2u;
        break;
    case IMP_ENC_RC_MODE_VBR:
    case IMP_ENC_RC_MODE_SMART: {
        /* IMPEncoderAttrH264Smart is IMPEncoderAttrH264VBR; the first nine
         * words are the same for H.265 */
        const IMPEncoderAttrH264VBR *v = &mode->attrH264Vbr;

        hw->rc_mode = HW_RC_MODE_VBR;
        hw->bitrate = v->maxBitRate * 1000u;
        hw->min_qp = codec_rc_helix_qp(v->minQp);
        hw->max_qp = codec_rc_helix_qp(v->maxQp);
        hw->qp = (hw->min_qp + hw->max_qp) / 2u;
        hw->static_time = v->staticTime;
        hw->change_pos = v->changePos;
        hw->quality_level = v->qualityLvl;
        hw->frm_qp_step = v->frmQPStep;
        hw->gop_qp_step = v->gopQPStep;
        hw->bias_level = v->iBiasLvl;
        hw->rc_flags = HW_RC_FLAG_APP |
            (mode->rcMode == IMP_ENC_RC_MODE_SMART ? HW_RC_FLAG_SMART : 0u);
        if (hevc)
            hw->fluct_level = mode->attrH265Vbr.flucLvl;
        else if (v->gopRelation)
            hw->rc_flags |= HW_RC_FLAG_GOP_RELATION;
        break;
    }
    case IMP_ENC_RC_MODE_FIXQP:
    default:
        hw->rc_mode = HW_RC_MODE_FIXQP;
        hw->qp = codec_rc_helix_qp(mode->attrH264FixQp.qp);
        hw->min_qp = hw->qp;
        hw->max_qp = hw->qp;
        break;
    }
}

#endif
