#ifndef OPENIMP_P2_RC_READBACK_H
#define OPENIMP_P2_RC_READBACK_H

/*
 * T20/T21 rate-control read-back as the OEM library gives it.
 *
 * The OEM IMP_Encoder_GetChnAttrRcMode (T21 1.0.33, T20 3.12.0) does not
 * return the stored attribute: it reads the encoder's live parameters
 * (i264e_get_param 3 -> i264e_reconfig_rc_get).  IMP_Encoder_CreateChn copies
 * the application's H.264 CBR/VBR/SMART fields into those parameters as given
 * and i264e_validate_parameters then clamps them:
 *
 *   maxQp       0..51            minQp     0..maxQp
 *   iBiasLvl    -10..10 (T21)    -3..3 (T20)
 *   frmQPStep   2..51            gopQPStep 2..51
 *   staticTime  <= 0 -> 1        (VBR/SMART; upper bound from a field not
 *                                 recovered, left as given)
 *   maxBitRate  < 128 -> 128     (VBR/SMART, kbit/s)
 *   changePos   50..100          (VBR/SMART)
 *   qualityLvl  0..6             (VBR/SMART)
 *
 * So an application that leaves these 0 reads back staticTime 1,
 * changePos 50, qualityLvl 0, frmQPStep 2, gopQPStep 2.  The i264e defaults
 * (i264e_param_default: staticTime 2, changePos 80, qualityLvl 4, frmQPStep 3,
 * gopQPStep 15) never reach the read-back, as CreateChn overwrites every one
 * of them.  GetChnAttr returns the stored attribute unchanged, as the OEM.
 */
#if defined(PLATFORM_T21)
#include <imp/imp_encoder.h>

static inline int32_t p2_rc_clip(int32_t value, int32_t lo, int32_t hi)
{
    return value < lo ? lo : value > hi ? hi : value;
}

static inline uint32_t p2_rc_clip_u(uint32_t value, uint32_t lo, uint32_t hi)
{
    /* the OEM clamps signed ints: a huge unsigned value is negative there */
    return (uint32_t)p2_rc_clip((int32_t)value, (int32_t)lo, (int32_t)hi);
}

static inline void p2_t21_rc_effective(IMPEncoderAttrRcMode *mode)
{
#if defined(PLATFORM_T20)
    const int32_t bias = 3;
#else
    const int32_t bias = 10;
#endif

    switch (mode->rcMode) {
    case IMP_ENC_RC_MODE_CBR: {
        IMPEncoderAttrH264CBR *cbr = &mode->attrH264Cbr;

        cbr->maxQp = p2_rc_clip_u(cbr->maxQp, 0u, 51u);
        cbr->minQp = p2_rc_clip_u(cbr->minQp, 0u, cbr->maxQp);
        cbr->iBiasLvl = p2_rc_clip(cbr->iBiasLvl, -bias, bias);
        cbr->frmQPStep = p2_rc_clip_u(cbr->frmQPStep, 2u, 51u);
        cbr->gopQPStep = p2_rc_clip_u(cbr->gopQPStep, 2u, 51u);
        break;
    }
    case IMP_ENC_RC_MODE_VBR:
    case IMP_ENC_RC_MODE_SMART: {
        IMPEncoderAttrH264VBR *vbr = &mode->attrH264Vbr;

        vbr->maxQp = p2_rc_clip_u(vbr->maxQp, 0u, 51u);
        vbr->minQp = p2_rc_clip_u(vbr->minQp, 0u, vbr->maxQp);
        vbr->iBiasLvl = p2_rc_clip(vbr->iBiasLvl, -bias, bias);
        vbr->frmQPStep = p2_rc_clip_u(vbr->frmQPStep, 2u, 51u);
        vbr->gopQPStep = p2_rc_clip_u(vbr->gopQPStep, 2u, 51u);
        if ((int32_t)vbr->staticTime <= 0)
            vbr->staticTime = 1u;
        if ((int32_t)vbr->maxBitRate < 128)
            vbr->maxBitRate = 128u;
        vbr->changePos = p2_rc_clip_u(vbr->changePos, 50u, 100u);
        vbr->qualityLvl = p2_rc_clip_u(vbr->qualityLvl, 0u, 6u);
        break;
    }
    default:
        break;
    }
}
#endif

#endif
