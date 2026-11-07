/*
 * codec_rc_helix_test - T30 IMP_Encoder_SetChnAttrRcMode on an H.265 channel
 * (#228): the attribute is read in the H.265 layout (src/t40/codec_rc_helix.h),
 * as the vendor libimp 1.0.5 SetChnAttrRcMode (0xc102c) does.  Before the fix
 * the H.264 CBR layout took staticTime as the bit rate: 1000 and 3000 kbit/s
 * both became 2 kbit/s.
 */
#include <stdio.h>
#include <string.h>

#include "t40/codec_rc_helix.h"

#define EXPECT(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "failed at line %d: %s\n", __LINE__, #condition); \
        return 1; \
    } \
} while (0)

/* timps' H.265 CBR attribute (audit/t30/b47: qp 20..45, staticTime 2) */
static void h265_cbr(IMPEncoderAttrRcMode *m, uint32_t kbps)
{
    memset(m, 0, sizeof(*m));
    m->rcMode = IMP_ENC_RC_MODE_CBR;
    m->attrH265Cbr.maxQp = 45;
    m->attrH265Cbr.minQp = 20;
    m->attrH265Cbr.staticTime = 2;
    m->attrH265Cbr.outBitRate = kbps;
    m->attrH265Cbr.iBiasLvl = 0;
    m->attrH265Cbr.frmQPStep = 3;
    m->attrH265Cbr.gopQPStep = 15;
    m->attrH265Cbr.flucLvl = 2;
}

int main(void)
{
    IMPEncoderAttrRcMode m;
    HWEncoderParams hw;

    /* H.265 CBR live 1000 then 3000 kbit/s: the targets reach the encoder */
    memset(&hw, 0, sizeof(hw));
    h265_cbr(&m, 1000);
    codec_rc_helix_from_attr(&hw, &m, 1);
    EXPECT(hw.rc_mode == HW_RC_MODE_CBR);
    EXPECT(hw.bitrate == 1000000u);
    EXPECT(hw.min_qp == 20 && hw.max_qp == 45 && hw.qp == 32);
    EXPECT(hw.static_time == 2);
    EXPECT(hw.frm_qp_step == 3 && hw.gop_qp_step == 15 && hw.bias_level == 0);
    EXPECT(hw.fluct_level == 2);
    EXPECT(hw.rc_flags == HW_RC_FLAG_APP);

    h265_cbr(&m, 3000);
    codec_rc_helix_from_attr(&hw, &m, 1);
    EXPECT(hw.bitrate == 3000000u);

    /* H.265 VBR -> CBR (b47 post3/post4) */
    memset(&m, 0, sizeof(m));
    m.rcMode = IMP_ENC_RC_MODE_VBR;
    m.attrH265Vbr.maxQp = 45;
    m.attrH265Vbr.minQp = 20;
    m.attrH265Vbr.staticTime = 2;
    m.attrH265Vbr.maxBitRate = 2000;
    m.attrH265Vbr.changePos = 80;
    m.attrH265Vbr.qualityLvl = 2;
    m.attrH265Vbr.frmQPStep = 3;
    m.attrH265Vbr.gopQPStep = 15;
    m.attrH265Vbr.flucLvl = 1;      /* word 10: H.264's gopRelation byte */
    codec_rc_helix_from_attr(&hw, &m, 1);
    EXPECT(hw.rc_mode == HW_RC_MODE_VBR && hw.bitrate == 2000000u);
    EXPECT(hw.static_time == 2 && hw.change_pos == 80 && hw.quality_level == 2);
    EXPECT(hw.fluct_level == 1);
    EXPECT(hw.rc_flags == HW_RC_FLAG_APP);  /* no gopRelation in H.265 */
    h265_cbr(&m, 2000);
    codec_rc_helix_from_attr(&hw, &m, 1);
    EXPECT(hw.rc_mode == HW_RC_MODE_CBR && hw.bitrate == 2000000u);
    EXPECT(hw.change_pos == 0 && hw.quality_level == 0);

    /* H.265 SMART */
    m.rcMode = IMP_ENC_RC_MODE_SMART;
    m.attrH265Smart.maxBitRate = 1500;
    codec_rc_helix_from_attr(&hw, &m, 1);
    EXPECT(hw.rc_mode == HW_RC_MODE_VBR && hw.bitrate == 1500000u);
    EXPECT(hw.rc_flags == (HW_RC_FLAG_APP | HW_RC_FLAG_SMART));

    /* H.264 unchanged: CBR outBitRate is word 3 of the union */
    memset(&m, 0, sizeof(m));
    m.rcMode = IMP_ENC_RC_MODE_CBR;
    m.attrH264Cbr.maxQp = 60;
    m.attrH264Cbr.minQp = 15;
    m.attrH264Cbr.outBitRate = 1000;
    m.attrH264Cbr.iBiasLvl = -2;
    m.attrH264Cbr.frmQPStep = 3;
    m.attrH264Cbr.gopQPStep = 15;
    m.attrH264Cbr.adaptiveMode = true;
    m.attrH264Cbr.gopRelation = true;
    codec_rc_helix_from_attr(&hw, &m, 0);
    EXPECT(hw.bitrate == 1000000u && hw.max_qp == 51 && hw.min_qp == 15);
    EXPECT(hw.static_time == 0 && hw.fluct_level == 0 && hw.bias_level == -2);
    EXPECT(hw.rc_flags == (HW_RC_FLAG_APP | HW_RC_FLAG_ADAPTIVE |
                           HW_RC_FLAG_GOP_RELATION));

    memset(&m, 0, sizeof(m));
    m.rcMode = IMP_ENC_RC_MODE_VBR;
    m.attrH264Vbr.maxBitRate = 3000;
    m.attrH264Vbr.gopRelation = true;
    codec_rc_helix_from_attr(&hw, &m, 0);
    EXPECT(hw.bitrate == 3000000u && hw.fluct_level == 0);
    EXPECT(hw.rc_flags == (HW_RC_FLAG_APP | HW_RC_FLAG_GOP_RELATION));

    /* FIXQP: no extras */
    memset(&m, 0, sizeof(m));
    m.rcMode = IMP_ENC_RC_MODE_FIXQP;
    m.attrH265FixQp.qp = 30;
    codec_rc_helix_from_attr(&hw, &m, 1);
    EXPECT(hw.rc_mode == HW_RC_MODE_FIXQP && hw.qp == 30 &&
           hw.min_qp == 30 && hw.max_qp == 30 && hw.rc_flags == 0);

    printf("codec_rc_helix_test: ok\n");
    return 0;
}
