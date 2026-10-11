/*
 * p2_gap_test - T21/T20 vendor bookkeeping calls of the P2 encoder
 * (gap work 2026-10-10): Get/SetChnFrmUsedMode, Get/SetFisheyeEnableStatus,
 * GetGOPSize, Get/SetChangeRef, SetChnHSkipBlackEnhance.  The expected
 * results are the return codes and stores of the vendor libimp (T21 1.0.33,
 * T20/T10 3.12.0) read from its disassembly.
 *
 * Includes src/t40/openimp_p2_encoder.c so the test can mark a channel
 * created without a codec; the codec calls are stubbed.
 * Build with -DPLATFORM_T21, or -DPLATFORM_T20 -DPLATFORM_T21 for T20/T10.
 * -DPLATFORM_T30 (T30 1.0.5 libimp, same code as T21 apart from the HSkip
 * idle channel and GetChangeRef of a created channel) also covers
 * SetPoolSize and SetChnRcTrigLevel.
 */
#include "../../src/t40/openimp_p2_encoder.c"

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(void)
{
    IMPEncoderAttrFrmUsed fu = { true, ENC_FRM_REUSED, 7 }, got;
    IMPEncoderGOPSizeCfg gop = { 1234 };
    int v = 99;

    EncoderInit();
    /* --- FrmUsedMode: three words stored as given, before create too --- */
    CHECK(IMP_Encoder_SetChnFrmUsedMode(0, &fu) == 0);
    memset(&got, 0, sizeof(got));
    CHECK(IMP_Encoder_GetChnFrmUsedMode(0, &got) == 0);
    CHECK(got.enable && got.frmUsedMode == ENC_FRM_REUSED &&
          got.frmUsedTimes == 7);
    CHECK(IMP_Encoder_SetChnFrmUsedMode(0, NULL) == -1);
    CHECK(IMP_Encoder_GetChnFrmUsedMode(0, NULL) == -1);
    CHECK(IMP_Encoder_SetChnFrmUsedMode(-1, &fu) == -1);
    CHECK(IMP_Encoder_GetChnFrmUsedMode(P2_MAX_CHANNELS, &got) == -1);
    memset(&got, 0xff, sizeof(got));
    CHECK(IMP_Encoder_GetChnFrmUsedMode(1, &got) == 0);   /* untouched = 0 */
    CHECK(!got.enable && got.frmUsedMode == 0 && got.frmUsedTimes == 0);

    /* --- idle channel ---------------------------------------------------- */
    CHECK(IMP_Encoder_SetFisheyeEnableStatus(0, 5) == 0);
    CHECK(IMP_Encoder_GetFisheyeEnableStatus(0, &v) == 0 && v == 1);
    CHECK(IMP_Encoder_SetFisheyeEnableStatus(0, 0) == 0);
    CHECK(IMP_Encoder_GetFisheyeEnableStatus(0, &v) == 0 && v == 0);
    CHECK(IMP_Encoder_GetFisheyeEnableStatus(0, NULL) == -1);
    CHECK(IMP_Encoder_SetChangeRef(1, 1) == -1);
    v = 99;
#if defined(PLATFORM_T20)
    CHECK(IMP_Encoder_GetChangeRef(1, &v) == -1);
    CHECK(IMP_Encoder_GetGOPSize(1, &gop) == -1 && gop.gopsize == 1234);
#else
    CHECK(IMP_Encoder_GetChangeRef(1, &v) == 0 && v == 0);
    CHECK(IMP_Encoder_GetGOPSize(1, &gop) == 0 && gop.gopsize == 1234);
#endif
    CHECK(IMP_Encoder_SetChnHSkipBlackEnhance(1, 1) == 0);
    CHECK(IMP_Encoder_SetChnHSkipBlackEnhance(P2_MAX_CHANNELS, 1) == -1);

    /* --- created channel ------------------------------------------------- */
    p2_channels[2].created = 1;
    p2_channels[2].attr.rcAttr.maxGop = 50;
    CHECK(IMP_Encoder_SetFisheyeEnableStatus(2, 1) == -1);    /* too late */
    CHECK(IMP_Encoder_GetFisheyeEnableStatus(2, &v) == 0 && v == 0);
    CHECK(IMP_Encoder_SetChangeRef(2, 3) == 0);
#if defined(PLATFORM_T21)
    CHECK(IMP_Encoder_GetChangeRef(2, &v) == 0 && v == 1);
    CHECK(IMP_Encoder_SetChangeRef(2, 0) == 0);
    CHECK(IMP_Encoder_GetChangeRef(2, &v) == 0 && v == 0);
#else
    /* T30 1.0.5: "unsupport to change ref channel" for a created channel */
    v = 99;
    CHECK(IMP_Encoder_GetChangeRef(2, &v) == -1 && v == 99);
    CHECK(p2_channels[2].change_ref == 1);
    CHECK(IMP_Encoder_SetChangeRef(2, 0) == 0);
    CHECK(p2_channels[2].change_ref == 0);
#endif
    CHECK(IMP_Encoder_GetChangeRef(2, NULL) == -1);
    CHECK(IMP_Encoder_GetGOPSize(2, &gop) == 0 && gop.gopsize == 50);
    CHECK(IMP_Encoder_GetGOPSize(2, NULL) == -1);
    CHECK(IMP_Encoder_SetChnHSkipBlackEnhance(2, 9) == 0);
    CHECK(p2_channels[2].attr.rcAttr.attrHSkip.hSkipAttr.bBlackEnhance == 1);
    CHECK(IMP_Encoder_SetChnHSkipBlackEnhance(2, 0) == 0);
    CHECK(p2_channels[2].attr.rcAttr.attrHSkip.hSkipAttr.bBlackEnhance == 0);

    /* --- ChnHSkip (T20 and T21): created channel, skipType <= max ------- */
    {
        IMPEncoderAttrHSkip hs = { IMP_Encoder_STYPE_N2X, 4, 5, 6, 1, 1 }, gh;

#if defined(PLATFORM_T21)
        CHECK(IMP_Encoder_SetChnHSkip(1, &hs) == -1);        /* idle */
        CHECK(IMP_Encoder_GetChnHSkip(1, &gh) == -1);
#else
        /* T30 1.0.5: an idle channel reads as zeros and takes the value
         * (within its maxHSkipType, 0 until CreateChn) */
        memset(&gh, 0xff, sizeof(gh));
        CHECK(IMP_Encoder_GetChnHSkip(1, &gh) == 0 && gh.skipType == 0 &&
              gh.m == 0 && gh.n == 0 && gh.maxSameSceneCnt == 0);
        CHECK(IMP_Encoder_GetChnHSkip(1, NULL) == -1);
        CHECK(IMP_Encoder_GetChnHSkip(P2_MAX_CHANNELS, &gh) == -1);
        CHECK(IMP_Encoder_SetChnHSkip(1, &hs) == -1);        /* above max 0 */
        hs.skipType = 0;
        CHECK(IMP_Encoder_SetChnHSkip(1, &hs) == 0);
        CHECK(p2_channels[1].attr.rcAttr.attrHSkip.hSkipAttr.m == 4 &&
              p2_channels[1].attr.rcAttr.attrHSkip.hSkipAttr.n == 5);
        memset(&gh, 0xff, sizeof(gh));                 /* idle still reads 0 */
        CHECK(IMP_Encoder_GetChnHSkip(1, &gh) == 0 && gh.m == 0 && gh.n == 0);
        hs.skipType = IMP_Encoder_STYPE_N2X;
#endif
        p2_channels[2].attr.rcAttr.attrHSkip.maxHSkipType = IMP_Encoder_STYPE_N2X;
        CHECK(IMP_Encoder_SetChnHSkip(2, &hs) == 0);
        memset(&gh, 0, sizeof(gh));
        CHECK(IMP_Encoder_GetChnHSkip(2, &gh) == 0 && gh.skipType == hs.skipType &&
              gh.m == 4 && gh.n == 5 && gh.maxSameSceneCnt == 6 &&
              gh.bEnableScenecut == 1 && gh.bBlackEnhance == 1);
        hs.skipType = IMP_Encoder_STYPE_N4X;
        CHECK(IMP_Encoder_SetChnHSkip(2, &hs) == -1);        /* above the maximum */
        CHECK(IMP_Encoder_SetChnHSkip(2, NULL) == -1);
    }
#if defined(PLATFORM_T30) && !defined(PLATFORM_T21)
    /* --- SetPoolSize, SetChnRcTrigLevel (T30 only) ----------------------- */
    CHECK(IMP_Encoder_SetPoolSize(0) == -1);
    CHECK(IMP_Encoder_SetPoolSize(-5) == -1);
    CHECK(IMP_Encoder_SetPoolSize(4 << 20) == 0 && p2_pool_size == (4 << 20));
    CHECK(IMP_Encoder_SetChnRcTrigLevel(P2_MAX_CHANNELS, 0.5f, 1) == -1);
    CHECK(IMP_Encoder_SetChnRcTrigLevel(4, 0.5f, 1) == 0);   /* idle: ignored */
    CHECK(p2_rc_trig[4].mode == 0);
    p2_channels[4].created = 1;
    CHECK(IMP_Encoder_SetChnRcTrigLevel(4, 0.5f, 3) == 0);
    CHECK(p2_rc_trig[4].level == 0.5f && p2_rc_trig[4].mode == 3);
#endif
#if defined(PLATFORM_T20)
    /* --- ChnDemask (T20/T10): stored as given, no created check ---------- */
    {
        IMPEncoderAttrDemask dm = { true, false, 30, 70 }, gd;

        memset(&gd, 0xff, sizeof(gd));
        CHECK(IMP_Encoder_GetChnDemask(3, &gd) == 0 && !gd.enable &&
              gd.demaskCnt == 0 && gd.demaskThresd == 0);
        CHECK(IMP_Encoder_SetChnDemask(3, &dm) == 0);
        CHECK(IMP_Encoder_GetChnDemask(3, &gd) == 0 && gd.enable && !gd.isAutoMode &&
              gd.demaskCnt == 30 && gd.demaskThresd == 70);
        CHECK(IMP_Encoder_SetChnDemask(3, NULL) == -1);
        CHECK(IMP_Encoder_GetChnDemask(3, NULL) == -1);
        CHECK(IMP_Encoder_SetChnDemask(-1, &dm) == -1);
    }
#endif

    if (failures) {
        fprintf(stderr, "p2_gap_test: %d failure(s)\n", failures);
        return 1;
    }
    puts("p2_gap_test: PASS");
    return 0;
}
