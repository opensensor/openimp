/*
 * rc_readback_test - T20/T21/T23 IMP_Encoder_GetChnAttrRcMode read-back as the
 * OEM library gives it: the live i264e parameters after
 * i264e_validate_parameters (CreateChn) or i264e_reconfig_rc_set
 * (SetChnAttrRcMode) (src/t40/p2_rc_readback.h).
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "t40/p2_rc_readback.h"

#define EXPECT(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "failed at line %d: %s\n", __LINE__, #condition); \
        return 1; \
    } \
} while (0)

#if defined(PLATFORM_T20)
#define BIAS 3
#else
#define BIAS 10
#endif

int main(void)
{
    IMPEncoderAttrRcMode mode;

    /* an application leaving the extras 0 (timps' VBR: staticTime 2,
     * the rest from its config) */
    memset(&mode, 0, sizeof(mode));
    mode.rcMode = IMP_ENC_RC_MODE_VBR;
    mode.attrH264Vbr.maxQp = 45;
    mode.attrH264Vbr.minQp = 20;
    mode.attrH264Vbr.maxBitRate = 3000;
    p2_t21_rc_effective(&mode, 0);
    EXPECT(mode.rcMode == IMP_ENC_RC_MODE_VBR);
    EXPECT(mode.attrH264Vbr.maxQp == 45 && mode.attrH264Vbr.minQp == 20);
    EXPECT(mode.attrH264Vbr.maxBitRate == 3000);
    EXPECT(mode.attrH264Vbr.staticTime == 1);
    EXPECT(mode.attrH264Vbr.changePos == 50);
    EXPECT(mode.attrH264Vbr.qualityLvl == 0);
    EXPECT(mode.attrH264Vbr.frmQPStep == 2);
    EXPECT(mode.attrH264Vbr.gopQPStep == 2);
    EXPECT(mode.attrH264Vbr.iBiasLvl == 0);

    /* valid values stay, out-of-range ones are clamped (SMART alike) */
    memset(&mode, 0, sizeof(mode));
    mode.rcMode = IMP_ENC_RC_MODE_SMART;
    mode.attrH264Smart.maxQp = 60;
    mode.attrH264Smart.minQp = 55;
    mode.attrH264Smart.maxBitRate = 64;
    mode.attrH264Smart.staticTime = 5;
    mode.attrH264Smart.changePos = 120;
    mode.attrH264Smart.qualityLvl = 7;
    mode.attrH264Smart.frmQPStep = 3;
    mode.attrH264Smart.gopQPStep = 15;
    mode.attrH264Smart.iBiasLvl = -12;
    mode.attrH264Smart.gopRelation = true;
    p2_t21_rc_effective(&mode, 0);
    EXPECT(mode.rcMode == IMP_ENC_RC_MODE_SMART);
    EXPECT(mode.attrH264Smart.maxQp == 51 && mode.attrH264Smart.minQp == 51);
    EXPECT(mode.attrH264Smart.maxBitRate == 128);
    EXPECT(mode.attrH264Smart.staticTime == 5);
    EXPECT(mode.attrH264Smart.changePos == 100);
    EXPECT(mode.attrH264Smart.qualityLvl == 6);
    EXPECT(mode.attrH264Smart.frmQPStep == 3);
    EXPECT(mode.attrH264Smart.gopQPStep == 15);
    EXPECT(mode.attrH264Smart.iBiasLvl == -BIAS);
    EXPECT(mode.attrH264Smart.gopRelation);

    /* CBR: QP steps and bias */
    memset(&mode, 0, sizeof(mode));
    mode.rcMode = IMP_ENC_RC_MODE_CBR;
    mode.attrH264Cbr.maxQp = 45;
    mode.attrH264Cbr.minQp = 20;
    mode.attrH264Cbr.outBitRate = 1000;
    mode.attrH264Cbr.iBiasLvl = 4;
    p2_t21_rc_effective(&mode, 0);
    EXPECT(mode.attrH264Cbr.outBitRate == 1000);
    EXPECT(mode.attrH264Cbr.frmQPStep == 2 && mode.attrH264Cbr.gopQPStep == 2);
    EXPECT(mode.attrH264Cbr.iBiasLvl == (BIAS < 4 ? BIAS : 4));

    /* FIXQP: qp 0..51 */
    memset(&mode, 0, sizeof(mode));
    mode.rcMode = IMP_ENC_RC_MODE_FIXQP;
    mode.attrH264FixQp.qp = 30;
    p2_t21_rc_effective(&mode, 0);
    EXPECT(mode.attrH264FixQp.qp == 30);
    mode.attrH264FixQp.qp = 60;
    p2_t21_rc_effective(&mode, 1);
    EXPECT(mode.attrH264FixQp.qp == 51);

    /* run time (SetChnAttrRcMode -> i264e_reconfig_rc_set): QPs 1..51 each,
     * steps only lose a negative value, changePos 0..100 */
    memset(&mode, 0, sizeof(mode));
    mode.rcMode = IMP_ENC_RC_MODE_VBR;
    mode.attrH264Vbr.maxQp = 30;
    mode.attrH264Vbr.minQp = 40;
    mode.attrH264Vbr.maxBitRate = 100;
    mode.attrH264Vbr.changePos = 20;
    mode.attrH264Vbr.qualityLvl = 9;
    mode.attrH264Vbr.frmQPStep = 0;
    mode.attrH264Vbr.gopQPStep = 60;
    mode.attrH264Vbr.iBiasLvl = 11;
    p2_t21_rc_effective(&mode, 1);
    EXPECT(mode.attrH264Vbr.maxQp == 30 && mode.attrH264Vbr.minQp == 40);
    EXPECT(mode.attrH264Vbr.maxBitRate == 128);
    EXPECT(mode.attrH264Vbr.staticTime == 1);
    EXPECT(mode.attrH264Vbr.changePos == 20);
    EXPECT(mode.attrH264Vbr.qualityLvl == 6);
    EXPECT(mode.attrH264Vbr.frmQPStep == 0);
    EXPECT(mode.attrH264Vbr.gopQPStep == 60);
    EXPECT(mode.attrH264Vbr.iBiasLvl == BIAS);

    memset(&mode, 0, sizeof(mode));
    mode.rcMode = IMP_ENC_RC_MODE_CBR;
    mode.attrH264Cbr.maxQp = 0;
    mode.attrH264Cbr.minQp = 0;
    mode.attrH264Cbr.frmQPStep = (uint32_t)-1;
    mode.attrH264Cbr.outBitRate = 50;
    p2_t21_rc_effective(&mode, 1);
    EXPECT(mode.attrH264Cbr.maxQp == 1 && mode.attrH264Cbr.minQp == 1);
    EXPECT(mode.attrH264Cbr.frmQPStep == 0 && mode.attrH264Cbr.gopQPStep == 0);
    EXPECT(mode.attrH264Cbr.outBitRate == 50);

    puts("rc read-back tests passed");
    return 0;
}
