#ifndef OPENIMP_P2_RC_READBACK_H
#define OPENIMP_P2_RC_READBACK_H

/*
 * T20/T21/T23 rate-control read-back as the OEM library gives it.
 *
 * The OEM IMP_Encoder_GetChnAttrRcMode (T21 1.0.33, T20 3.12.0, T23 1.3.0
 * 0x5369c) does not
 * return the stored attribute: it reads the encoder's live parameters
 * (i264e_get_param 3 -> i264e_reconfig_rc_get).  IMP_Encoder_CreateChn copies
 * the application's H.264 CBR/VBR/SMART fields into those parameters as given
 * and i264e_validate_parameters then clamps them:
 *
 *   qp (FIXQP)  0..51
 *   maxQp       0..51            minQp     0..maxQp
 *   iBiasLvl    -10..10 (T21, T23)  -3..3 (T20)
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
 *
 * T23 1.3.0 is the same: IMP_Encoder_CreateChn (0x4e164) copies the fields
 * unconditionally into the i264e parameters (VBR/SMART +0x48..+0x6c, CBR
 * +0x48..+0x61), i264e_validate_parameters (0x33780) clamps them as above
 * (iBiasLvl -10..10), i264e_reconfig_init (0x35b50) copies the result into
 * the live rc block that i264e_reconfig_rc_get (0x38070) returns.  (The
 * keep-the-default rule for 0 belongs to IMP_Encoder_YuvInit, not to
 * CreateChn.)
 */
#if defined(PLATFORM_T21) || defined(PLATFORM_T23)
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

/* QP step after the run-time set: a negative value (as the OEM's signed
 * int) becomes 0, anything else stays. */
static inline uint32_t p2_rc_step_runtime(uint32_t value)
{
    return (int32_t)value < 0 ? 0u : value;
}

/* runtime 0: the CreateChn values (i264e_validate_parameters).  runtime 1:
 * after IMP_Encoder_SetChnAttrRcMode, which goes through i264e_set_param 3
 * -> i264e_reconfig_rc_set (T23 1.3.0 0x37cf8; T21/T20 the same code):
 * minQp and maxQp each 1..51 (no minQp <= maxQp), iBiasLvl as above,
 * frm/gopQPStep negative -> 0 (no 2..51), staticTime <= 0 -> 1, maxBitRate
 * < 128 -> 128, changePos 0..100 (not 50..100), qualityLvl 0..6, FIXQP qp
 * 0..51. */
static inline void p2_t21_rc_effective(IMPEncoderAttrRcMode *mode,
                                       int runtime)
{
#if defined(PLATFORM_T20)
    const int32_t bias = 3;
#else
    const int32_t bias = 10;
#endif
    const uint32_t qp_lo = runtime ? 1u : 0u;

    switch (mode->rcMode) {
    case IMP_ENC_RC_MODE_FIXQP:
        mode->attrH264FixQp.qp = p2_rc_clip_u(mode->attrH264FixQp.qp, 0u, 51u);
        break;
    case IMP_ENC_RC_MODE_CBR: {
        IMPEncoderAttrH264CBR *cbr = &mode->attrH264Cbr;

        cbr->maxQp = p2_rc_clip_u(cbr->maxQp, qp_lo, 51u);
        cbr->minQp = p2_rc_clip_u(cbr->minQp, qp_lo,
                                  runtime ? 51u : cbr->maxQp);
        cbr->iBiasLvl = p2_rc_clip(cbr->iBiasLvl, -bias, bias);
        if (runtime) {
            cbr->frmQPStep = p2_rc_step_runtime(cbr->frmQPStep);
            cbr->gopQPStep = p2_rc_step_runtime(cbr->gopQPStep);
        } else {
            cbr->frmQPStep = p2_rc_clip_u(cbr->frmQPStep, 2u, 51u);
            cbr->gopQPStep = p2_rc_clip_u(cbr->gopQPStep, 2u, 51u);
        }
        break;
    }
    case IMP_ENC_RC_MODE_VBR:
    case IMP_ENC_RC_MODE_SMART: {
        IMPEncoderAttrH264VBR *vbr = &mode->attrH264Vbr;

        vbr->maxQp = p2_rc_clip_u(vbr->maxQp, qp_lo, 51u);
        vbr->minQp = p2_rc_clip_u(vbr->minQp, qp_lo,
                                  runtime ? 51u : vbr->maxQp);
        vbr->iBiasLvl = p2_rc_clip(vbr->iBiasLvl, -bias, bias);
        if (runtime) {
            vbr->frmQPStep = p2_rc_step_runtime(vbr->frmQPStep);
            vbr->gopQPStep = p2_rc_step_runtime(vbr->gopQPStep);
        } else {
            vbr->frmQPStep = p2_rc_clip_u(vbr->frmQPStep, 2u, 51u);
            vbr->gopQPStep = p2_rc_clip_u(vbr->gopQPStep, 2u, 51u);
        }
        if ((int32_t)vbr->staticTime <= 0)
            vbr->staticTime = 1u;
        if ((int32_t)vbr->maxBitRate < 128)
            vbr->maxBitRate = 128u;
        vbr->changePos = p2_rc_clip_u(vbr->changePos, runtime ? 0u : 50u,
                                      100u);
        vbr->qualityLvl = p2_rc_clip_u(vbr->qualityLvl, 0u, 6u);
        break;
    }
    default:
        break;
    }
}
#endif

#endif
