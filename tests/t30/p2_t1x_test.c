/*
 * p2_t1x_test - T20/T21 (T10) encoder channel extras through the P2 API
 * (src/t40/openimp_p2_encoder.c against a stub codec):
 *
 *  - Color2Grey and MbRC reach the codec (HWEncoderParams);
 *  - SuperFrameCfg: T20 hands NONE/REENCODE and the thresholds to the
 *    codec, refuses DISCARD and reports the OEM defaults before a Set;
 *    T21 accepts NONE only;
 *  - ROI: the OEM i264e table entry (sorted corners / 16) reaches the
 *    codec, GetChnROI returns it x16; H264TransCfg: the chroma QP offset
 *    reaches the codec (-12..12);
 *  - Denoise, QpgMode: settings the encoder cannot apply return -1, the
 *    ones it already follows are stored.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <imp/imp_encoder.h>
#include <imp/imp_system.h>
#include "dma_alloc.h"

int IMP_Encoder_SetChnColor2Grey(int, const IMPEncoderColor2GreyCfg *);
int IMP_Encoder_GetChnColor2Grey(int, IMPEncoderColor2GreyCfg *);
int IMP_Encoder_SetChnROI(int, const IMPEncoderROICfg *);
int IMP_Encoder_GetChnAttr(int, IMPEncoderCHNAttr *);
int IMP_Encoder_SetChnDenoise(int, const IMPEncoderAttrDenoise *);
int IMP_Encoder_GetChnDenoise(int, IMPEncoderAttrDenoise *);
int IMP_Encoder_SetMbRC(int, int);
int IMP_Encoder_SetSuperFrameCfg(int, const IMPEncoderSuperFrmCfg *);
#if defined(PLATFORM_T20)
int IMP_Encoder_SetChnRcTrigLevel(int, float, int);
#endif
int IMP_Encoder_GetSuperFrameCfg(int, IMPEncoderSuperFrmCfg *);
int IMP_Encoder_SetH264TransCfg(int, const IMPEncoderH264TransCfg *);
int IMP_Encoder_SetQpgMode(int, const IMPEncoderQpgMode *);
int IMP_Encoder_GetQpgMode(int, IMPEncoderQpgMode *);
int IMP_Encoder_SetH265TransCfg(int, const IMPEncoderH265TransCfg *);
int IMP_Encoder_GetH265TransCfg(int, IMPEncoderH265TransCfg *);
int IMP_Encoder_SetJpegeQl(int, IMPEncoderJpegeQl *);
int IMP_Encoder_GetJpegeQl(int, IMPEncoderJpegeQl *);

static int failures;

#define CHECK(cond) do { if (!(cond)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* ---- the rest of libimp ---- */
int IMP_FrameSource_GetFrame(int chn, void **frame) { (void)chn; (void)frame; return -1; }
int IMP_FrameSource_ReleaseFrame(int chn, void *frame) { (void)chn; (void)frame; return 0; }
int DMA_AllocDescriptor(IMPDMABufferInfo *info, int size, const char *tag)
{ (void)info; (void)size; (void)tag; return -1; }
int DMA_FreePhys(uint32_t phys) { (void)phys; return 0; }
int DMA_RmemFlushCache(void *virt, uint32_t size, int dir)
{ (void)virt; (void)size; (void)dir; return 0; }
int OpenIMP_HelixJpeg_Available(void) { return 0; }
int openimp_t23_persist_enabled(void) { return 0; }
int openimp_t23_persist_write(const char *a, const void *b, unsigned int c)
{ (void)a; (void)b; (void)c; return 0; }
int openimp_t31_osd_apply_ex(int group, void *frame, unsigned int flags)
{ (void)group; (void)frame; (void)flags; return 0; }

/* ---- stub codec ---- */
static int c2g_calls, c2g_value = -1;
static int mbrc_calls, mbrc_value = -1;
static int sf_calls;
static uint32_t sf_mode, sf_i, sf_p;

int AL_Codec_Encode_Create(void **codec, void *params)
{ (void)params; *codec = calloc(1, 64); return *codec ? 0 : -1; }
int AL_Codec_Encode_Destroy(void *codec) { free(codec); return 0; }
int AL_Codec_Encode_Process(void *c, void *f, void *u) { (void)c; (void)f; (void)u; return -1; }
int AL_Codec_Encode_GetStream(void *c, void **s, void **u) { (void)c; (void)s; (void)u; return 1; }
int AL_Codec_Encode_ReleaseStream(void *c, void *s, void *u) { (void)c; (void)s; (void)u; return 0; }
int AL_Codec_Encode_JpegSkipped(void *c) { (void)c; return 0; }
int VBMGetFrame(int chn, void **frame) { (void)chn; (void)frame; return -1; }
int AL_Codec_Encode_InsertUserData(void *c, const void *d, unsigned int s, unsigned int n, unsigned int m)
{ (void)c; (void)d; (void)s; (void)n; (void)m; return 0; }
int AL_Codec_Encode_SetJpegSkip(void *c, int a) { (void)c; (void)a; return 0; }
int AL_Codec_Encode_SetJpegSrcPitch(void *c, uint32_t p) { (void)c; (void)p; return 0; }
static int jql_calls, jql_en = -1;
static uint8_t jql_tab[128];
int AL_Codec_Encode_SetJpegQl(void *c, int a, const uint8_t *b)
{ (void)c; jql_calls++; jql_en = a; if (b) memcpy(jql_tab, b, sizeof(jql_tab)); return 0; }
#define STUB0(name) int name(void *codec) { (void)codec; return 0; }
#define STUB1(name, t) int name(void *codec, t a) { (void)codec; (void)a; return 0; }
STUB0(AL_Codec_Encode_RequestIDR)
STUB1(AL_Codec_Encode_SetEntropyMode, int)
STUB1(AL_Codec_Encode_SetGopLength, int)
STUB1(AL_Codec_Encode_SetQp, void *)
STUB1(AL_Codec_Encode_SetStreamBufferCount, int)
STUB1(AL_Codec_Encode_SetStreamBufferSize, int)
STUB1(AL_Codec_Encode_SetQpIPDelta, int)
STUB1(AL_Codec_Encode_SetRcParam, void *)
STUB1(AL_Codec_Encode_SetRcExtras, const void *)
STUB1(AL_Codec_Encode_SetFrameRate, void *)
STUB1(AL_Codec_Encode_SetGopParam, void *)
int AL_Codec_Encode_SetBitRate(void *c, int a, int b) { (void)c; (void)a; (void)b; return 0; }
int AL_Codec_Encode_SetQpBounds(void *c, int a, int b) { (void)c; (void)a; (void)b; return 0; }
int AL_Codec_Encode_SetDefaultParam(void *p) { (void)p; return 0; }
int AL_Codec_Encode_SetSameSceneGops(void *c, uint32_t g) { (void)c; (void)g; return 0; }
int AL_Codec_Encode_SetMbRC(void *c, int e) { (void)c; mbrc_calls++; mbrc_value = e; return 0; }
int AL_Codec_Encode_SetColor2Grey(void *c, int e) { (void)c; c2g_calls++; c2g_value = e; return 0; }
static uint8_t roi_entry[8][7];
static int roi_calls, chroma_calls, chroma_value;
int AL_Codec_Encode_SetRoi(void *c, uint32_t i, const uint8_t e[7])
{ (void)c; if (i >= 8u) return -1; roi_calls++; memcpy(roi_entry[i], e, 7); return 0; }
int AL_Codec_Encode_SetChromaQpOffset(void *c, int o)
{ (void)c; chroma_calls++; chroma_value = o; return 0; }
static int trig_calls, trig_mode;
static float trig_level;
int AL_Codec_Encode_SetRcTrig(void *c, float l, int m)
{ (void)c; trig_calls++; trig_level = l; trig_mode = m; return 0; }
int AL_Codec_Encode_SetSuperFrame(void *c, uint32_t m, uint32_t i, uint32_t p)
{ (void)c; sf_calls++; sf_mode = m; sf_i = i; sf_p = p; return 0; }

static void make_attr(IMPEncoderCHNAttr *attr, int denoise)
{
    memset(attr, 0, sizeof(*attr));
    attr->encAttr.enType = PT_H264;
    attr->encAttr.profile = 1;
    attr->encAttr.picWidth = 640;
    attr->encAttr.picHeight = 360;
    attr->encAttr.bufSize = 0;
    attr->rcAttr.outFrmRate.frmRateNum = 25;
    attr->rcAttr.outFrmRate.frmRateDen = 1;
    attr->rcAttr.maxGop = 50;
    attr->rcAttr.attrRcMode.rcMode = IMP_ENC_RC_MODE_CBR;
    attr->rcAttr.attrRcMode.attrH264Cbr.maxQp = 45;
    attr->rcAttr.attrRcMode.attrH264Cbr.minQp = 20;
    attr->rcAttr.attrRcMode.attrH264Cbr.outBitRate = 1000;
    attr->rcAttr.attrDenoise.enable = denoise;
}

int main(void)
{
    IMPEncoderCHNAttr attr;
    IMPEncoderColor2GreyCfg c2g;
    IMPEncoderROICfg roi;
    IMPEncoderAttrDenoise dn;
    IMPEncoderSuperFrmCfg sf;
    IMPEncoderH264TransCfg tr;
    IMPEncoderQpgMode qpg;
    IMPEncoderJpegeQl jql;

    CHECK(IMP_Encoder_CreateGroup(0) == 0);
    make_attr(&attr, 0);
    CHECK(IMP_Encoder_CreateChn(0, &attr) == 0);
    make_attr(&attr, 1);
    CHECK(IMP_Encoder_CreateChn(1, &attr) == 0);

    /* CreateChn denoise (vendor T21 1.0.33): a disabled denoise has its type
     * cleared in the channel attribute; an enabled one keeps the caller's
     * dnType in GetChnAttr while GetChnDenoise reads the encoder's 0; an
     * enabled type >= 3 fails validation, so the create fails */
#if defined(PLATFORM_T21) && !defined(PLATFORM_T20)
    {
        IMPEncoderCHNAttr got;
        IMPEncoderAttrDenoise gdn;

        make_attr(&attr, 0);
        attr.rcAttr.attrDenoise.dnType = 2;
        attr.rcAttr.attrDenoise.dnIQp = 21;
        CHECK(IMP_Encoder_CreateChn(2, &attr) == 0);
        memset(&got, 0x5a, sizeof(got));
        CHECK(IMP_Encoder_GetChnAttr(2, &got) == 0);
        CHECK(!got.rcAttr.attrDenoise.enable && got.rcAttr.attrDenoise.dnType == 0 &&
              got.rcAttr.attrDenoise.dnIQp == 21);
        CHECK(IMP_Encoder_DestroyChn(2) == 0);

        make_attr(&attr, 1);
        attr.rcAttr.attrDenoise.dnType = 1;
        attr.rcAttr.attrDenoise.dnIQp = 22;
        CHECK(IMP_Encoder_CreateChn(2, &attr) == 0);
        CHECK(IMP_Encoder_GetChnAttr(2, &got) == 0);
        CHECK(got.rcAttr.attrDenoise.enable && got.rcAttr.attrDenoise.dnType == 1 &&
              got.rcAttr.attrDenoise.dnIQp == 22);
        memset(&gdn, 0x5a, sizeof(gdn));
        CHECK(IMP_Encoder_GetChnDenoise(2, &gdn) == 0 && gdn.dnType == 0 &&
              gdn.dnIQp == 0);        /* T21 validation clears type and QPs */
        CHECK(IMP_Encoder_DestroyChn(2) == 0);

        attr.rcAttr.attrDenoise.dnType = 3;
        CHECK(IMP_Encoder_CreateChn(2, &attr) == -1);
        attr.rcAttr.attrDenoise.dnType = -1;
        CHECK(IMP_Encoder_CreateChn(2, &attr) == -1);
        attr.rcAttr.attrDenoise.enable = 0;     /* disabled: type forced to 0 */
        attr.rcAttr.attrDenoise.dnType = 7;
        CHECK(IMP_Encoder_CreateChn(2, &attr) == 0);
        CHECK(IMP_Encoder_GetChnAttr(2, &got) == 0 &&
              got.rcAttr.attrDenoise.dnType == 0);
        CHECK(IMP_Encoder_DestroyChn(2) == 0);
    }
#endif

    /* Color2Grey */
    c2g.enable = 1;
    CHECK(IMP_Encoder_SetChnColor2Grey(0, &c2g) == 0);
    CHECK(c2g_calls == 1 && c2g_value == 1);
    memset(&c2g, 0, sizeof(c2g));
    CHECK(IMP_Encoder_GetChnColor2Grey(0, &c2g) == 0 && c2g.enable);
    c2g.enable = 0;
    CHECK(IMP_Encoder_SetChnColor2Grey(0, &c2g) == 0 && c2g_value == 0);

    /* MbRC */
    CHECK(IMP_Encoder_SetMbRC(0, 1) == 0 && mbrc_value == 1);
    CHECK(IMP_Encoder_SetMbRC(0, 2) == -1);

    /* SuperFrameCfg */
    memset(&sf, 0, sizeof(sf));
    CHECK(IMP_Encoder_GetSuperFrameCfg(0, &sf) == 0);
#if defined(PLATFORM_T20)
    CHECK(sf.superFrmMode == IMP_RC_SUPERFRM_REENCODE &&
          sf.superIFrmBitsThr == 19660800u &&
          sf.superPFrmBitsThr == 14043429u);
    sf.superFrmMode = IMP_RC_SUPERFRM_REENCODE;
    sf.superIFrmBitsThr = 800000u;
    sf.superPFrmBitsThr = 400000u;
    CHECK(IMP_Encoder_SetSuperFrameCfg(0, &sf) == 0);
    CHECK(sf_calls == 1 && sf_mode == 2u && sf_i == 800000u && sf_p == 400000u);
    sf.superFrmMode = IMP_RC_SUPERFRM_NONE;
    CHECK(IMP_Encoder_SetSuperFrameCfg(0, &sf) == 0 && sf_mode == 1u);
    sf.superFrmMode = IMP_RC_SUPERFRM_DISCARD;
    CHECK(IMP_Encoder_SetSuperFrameCfg(0, &sf) == -1 && sf_calls == 2);
    memset(&sf, 0, sizeof(sf));
    CHECK(IMP_Encoder_GetSuperFrameCfg(0, &sf) == 0 &&
          sf.superFrmMode == IMP_RC_SUPERFRM_NONE);
#else
    sf.superFrmMode = IMP_RC_SUPERFRM_REENCODE;
    CHECK(IMP_Encoder_SetSuperFrameCfg(0, &sf) == -1);
    sf.superFrmMode = IMP_RC_SUPERFRM_NONE;
    CHECK(IMP_Encoder_SetSuperFrameCfg(0, &sf) == 0);
    CHECK(sf_calls == 0);
#endif

#if defined(PLATFORM_T20)
    /* RcTrigLevel: channel >= 6 is -1, a created H.264 channel hands the pair
     * to the controller (max QP as new max QP = a hard cap) */
    CHECK(IMP_Encoder_SetChnRcTrigLevel(6, 3.0f, 28) == -1);
    CHECK(IMP_Encoder_SetChnRcTrigLevel(-1, 3.0f, 28) == -1);
    CHECK(IMP_Encoder_SetChnRcTrigLevel(0, 2.5f, 28) == 0 && trig_calls == 1 &&
          trig_level == 2.5f && trig_mode == 28);
    CHECK(IMP_Encoder_SetChnRcTrigLevel(5, 2.5f, 28) == 0 && trig_calls == 1);
#endif

    /* ROI: the OEM table entry {en, rel, (s8) qp, x0, x1, y0, y1} with the
     * corners (+12/+20 x, +16/+24 y: the OEM IMPRect p0/p1) sorted and
     * divided by 16 */
    memset(&roi, 0, sizeof(roi));
    roi.u32Index = 1;
    CHECK(IMP_Encoder_SetChnROI(0, &roi) == 0 && roi_calls == 1);
    roi.bEnable = 1;
    roi.bRelatedQp = 1;
    roi.s32Qp = -4;
    roi.rect.x = 300;                   /* p0.x */
    roi.rect.y = 199;                   /* p0.y */
    roi.rect.width = 40;                /* p1.x: smaller, swapped */
    roi.rect.height = 359;              /* p1.y */
    CHECK(IMP_Encoder_SetChnROI(0, &roi) == 0 && roi_calls == 2);
    CHECK(roi_entry[1][0] == 1 && roi_entry[1][1] == 1 &&
          roi_entry[1][2] == 0xfc && roi_entry[1][3] == 2 &&
          roi_entry[1][4] == 18 && roi_entry[1][5] == 12 &&
          roi_entry[1][6] == 22);
    memset(&roi, 0x5a, sizeof(roi));
    roi.u32Index = 1;
    CHECK(IMP_Encoder_GetChnROI(0, &roi) == 0);
    CHECK(roi.u32Index == 1 && roi.bEnable && roi.bRelatedQp &&
          roi.s32Qp == -4 && roi.rect.x == 32 && roi.rect.width == 288 &&
          roi.rect.y == 192 && roi.rect.height == 352);
    roi.u32Index = 8;
    CHECK(IMP_Encoder_SetChnROI(0, &roi) == -1 && roi_calls == 2);
    CHECK(IMP_Encoder_GetChnROI(0, &roi) == -1);

    /* Denoise (vendor semantics, src/t40/p2_denoise.h): no effect on a
     * channel created without it; on a channel created with it dnType
     * 0..2 is accepted, dnType >= 3 is refused; the T21 library reads the
     * type back as 0, T20/T10 keep it */
    memset(&dn, 0, sizeof(dn));
    dn.enable = 1;
    dn.dnType = 1;
    dn.dnIQp = 30;
    CHECK(IMP_Encoder_SetChnDenoise(0, &dn) == 0);
    CHECK(IMP_Encoder_SetChnDenoise(1, &dn) == 0);
    dn.dnType = 3;
    CHECK(IMP_Encoder_SetChnDenoise(1, &dn) == -1);
    dn.dnType = 0;
    CHECK(IMP_Encoder_SetChnDenoise(1, &dn) == 0);
    memset(&dn, 0, sizeof(dn));
    CHECK(IMP_Encoder_GetChnDenoise(1, &dn) == 0 && dn.dnType == 0);
    /* a type on a channel created with denoise: T21 reads 0 back, T20/T10
     * (3.12.0 runs the two-pass denoise) keep it */
    memset(&dn, 0, sizeof(dn));
    dn.dnType = 2;
    dn.dnIQp = 31;
    dn.dnPQp = 33;
    CHECK(IMP_Encoder_SetChnDenoise(1, &dn) == 0);
    memset(&dn, 0, sizeof(dn));
    CHECK(IMP_Encoder_GetChnDenoise(1, &dn) == 0 && dn.dnIQp == 31 &&
          dn.dnPQp == 33);
#if defined(PLATFORM_T20)
    CHECK(dn.dnType == 2);
#else
    CHECK(dn.dnType == 0);
#endif

    /* H264TransCfg: the chroma QP offset (-12..12) reaches the codec */
    memset(&tr, 0, sizeof(tr));
    tr.chroma_qp_index_offset = 0;
    CHECK(IMP_Encoder_SetH264TransCfg(0, &tr) == 0 && chroma_value == 0);
    tr.chroma_qp_index_offset = -3;
    CHECK(IMP_Encoder_SetH264TransCfg(0, &tr) == 0 && chroma_value == -3);
    memset(&tr, 0, sizeof(tr));
    CHECK(IMP_Encoder_GetH264TransCfg(0, &tr) == 0 &&
          tr.chroma_qp_index_offset == -3);
    tr.chroma_qp_index_offset = 13;
    CHECK(IMP_Encoder_SetH264TransCfg(0, &tr) == -1 && chroma_calls == 2);

    /* QpgMode (vendor T21): any value is kept and read back, no range
     * check; a channel that was not created answers 0 and keeps nothing */
    qpg = ENC_QPG_CLOSE;
    CHECK(IMP_Encoder_SetQpgMode(0, &qpg) == 0);
    qpg = ENC_QPG_SAS;
    CHECK(IMP_Encoder_SetQpgMode(0, &qpg) == 0);
    qpg = (IMPEncoderQpgMode)77;
    CHECK(IMP_Encoder_SetQpgMode(1, &qpg) == 0);
    qpg = ENC_QPG_CLOSE;
    CHECK(IMP_Encoder_GetQpgMode(0, &qpg) == 0 && qpg == ENC_QPG_SAS);
    CHECK(IMP_Encoder_GetQpgMode(1, &qpg) == 0 && (int)qpg == 77);
    qpg = ENC_QPG_SAS;
    CHECK(IMP_Encoder_SetQpgMode(5, &qpg) == 0);
    qpg = ENC_QPG_CLOSE;
    CHECK(IMP_Encoder_GetQpgMode(5, &qpg) == 0 && qpg == ENC_QPG_CLOSE);
    CHECK(IMP_Encoder_SetQpgMode(0, NULL) == -1 &&
          IMP_Encoder_SetQpgMode(99, &qpg) == -1);

    /* H265TransCfg (vendor T21): checked and dropped, Get returns zeros */
    {
        IMPEncoderH265TransCfg h265 = { 99, -99 };

        CHECK(IMP_Encoder_SetH265TransCfg(0, &h265) == 0);
        CHECK(IMP_Encoder_SetH265TransCfg(5, &h265) == 0);
        CHECK(IMP_Encoder_SetH265TransCfg(0, NULL) == -1);
        CHECK(IMP_Encoder_SetH265TransCfg(99, &h265) == -1);
        CHECK(IMP_Encoder_GetH265TransCfg(0, &h265) == 0 &&
              h265.chroma_cr_qp_offset == 0 && h265.chroma_cb_qp_offset == 0);
        CHECK(IMP_Encoder_GetH265TransCfg(0, NULL) == -1);
    }

    /* JpegeQl: applied to the running codec at once (the vendor hands it to
     * its JPEG core), stored for Get; bad channel / NULL fail */
    memset(&jql, 0, sizeof(jql));
    jql.user_ql_en = 1;
    memset(jql.qmem_table, 7, 128);
    CHECK(IMP_Encoder_SetJpegeQl(0, &jql) == 0);
    CHECK(jql_calls == 1 && jql_en == 1 && jql_tab[0] == 7 && jql_tab[127] == 7);
    memset(&jql, 0xaa, sizeof(jql));
    CHECK(IMP_Encoder_GetJpegeQl(0, &jql) == 0 && jql.user_ql_en &&
          jql.qmem_table[5] == 7);
    jql.user_ql_en = 0;
    CHECK(IMP_Encoder_SetJpegeQl(0, &jql) == 0 && jql_calls == 2 && jql_en == 0);
    CHECK(IMP_Encoder_SetJpegeQl(0, NULL) == -1);
    CHECK(IMP_Encoder_GetJpegeQl(0, NULL) == -1);
    CHECK(IMP_Encoder_SetJpegeQl(99, &jql) == -1 && jql_calls == 2);

    CHECK(IMP_Encoder_DestroyChn(0) == 0);
    CHECK(IMP_Encoder_DestroyChn(1) == 0);
    if (failures) {
        fprintf(stderr, "p2_t1x_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("p2_t1x_test: ok\n");
    return 0;
}
