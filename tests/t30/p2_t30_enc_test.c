/*
 * p2_t30_enc_test - T30 IMP_Encoder_Get/SetChnROI and Get/SetSuperFrameCfg
 * through the P2 API (src/t40/openimp_p2_encoder.c against a stub codec),
 * the values of the vendor T30 1.0.5 libimp (camera A/B audit/t30/b1-all,
 * disassembly: IMP_Encoder_SetChnROI 0xc1ec0, GetChnROI 0xc22e8,
 * SetSuperFrameCfg 0xc4c60, GetSuperFrameCfg 0xc4eb8).
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <unistd.h>
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
int IMP_Encoder_SetFisheyeEnableStatus(int, int);
int IMP_Encoder_GetFisheyeEnableStatus(int, int *);
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
#if defined(PLATFORM_T20) || defined(PLATFORM_T21)
/* T10/T20/T21 encoder waits on the VBM ready event (#151); no frame ever comes */
unsigned int VBMReadySequence(int chn) { (void)chn; return 0; }
int VBMWaitReady(int chn, unsigned int seq, uint32_t us)
{
    (void)chn; (void)seq; (void)us;
    usleep(1000);
    return -1;
}
void VBMWakeReaders(int chn) { (void)chn; }
#endif
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
static int hs_calls, hs_type = -1, hs_m, hs_n, hs_scene, hs_black;
int AL_Codec_Encode_SetHSkip(void *c, uint32_t t, uint32_t m, uint32_t n, int sc, int bl)
{ (void)c; hs_calls++; hs_type = (int)t; hs_m = (int)m; hs_n = (int)n; hs_scene = sc; hs_black = bl; return 0; }
static int fe_flag = -1;
int AL_Codec_Encode_SetFisheye(void *c, int en)
{ (void)c; fe_flag = en; return 0; }
static int cr_calls = -1;
int AL_Codec_Encode_SetChangeRef(void *c, int en)
{ (void)c; cr_calls = en; return 0; }
#if defined(PLATFORM_T20)
static int sharp_calls, sharp_value, sharp_mode;
int AL_Codec_Encode_GetPictureQp(void *c, int *qp) { (void)c; *qp = 0; return 0; }
int IMP_ISP_Tuning_SetSharpness_internal(uint32_t v, int32_t m) { sharp_calls++; sharp_value = (int)v; sharp_mode = m; return 0; }
#endif
int AL_Codec_Encode_SetMbRC(void *c, int e) { (void)c; mbrc_calls++; mbrc_value = e; return 0; }
int AL_Codec_Encode_SetColor2Grey(void *c, int e) { (void)c; c2g_calls++; c2g_value = e; return 0; }
static uint8_t roi_entry[8][7];
static int roi_calls, chroma_calls, chroma_value;
int AL_Codec_Encode_SetRoi(void *c, uint32_t i, const uint8_t e[7])
{ (void)c; if (i >= 8u) return -1; roi_calls++; memcpy(roi_entry[i], e, 7); return 0; }
int AL_Codec_Encode_SetChromaQpOffset(void *c, int o)
{ (void)c; chroma_calls++; chroma_value = o; return 0; }
#if defined(PLATFORM_T21) && !defined(PLATFORM_T20)
static int chroma_applied;      /* what the encoder codes with now (the IDR picture takes chroma_value over) */
int AL_Codec_Encode_GetChromaQpOffset(void *c, int *o)
{ (void)c; *o = chroma_applied; return 0; }
#endif
int IMP_Encoder_SetChnCrop(int encChn, const IMPEncoderCropCfg *cfg);
int IMP_Encoder_GetChnCrop(int encChn, IMPEncoderCropCfg *cfg);
static int crop_calls, crop_en;
static uint32_t crop_v[4];
int AL_Codec_Encode_SetCrop(void *c, int e, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{ (void)c; crop_calls++; crop_en = e; crop_v[0] = x; crop_v[1] = y; crop_v[2] = w; crop_v[3] = h; return 0; }
int AL_Codec_Encode_GetCrop(void *c, int *e, uint32_t *x, uint32_t *y, uint32_t *w, uint32_t *h)
{ (void)c; *e = crop_en; *x = crop_v[0]; *y = crop_v[1]; *w = crop_v[2]; *h = crop_v[3]; return 0; }
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
    IMPEncoderROICfg roi, got;
    IMPEncoderSuperFrmCfg sf;
    IMPEncoderCHNAttr jattr;

    CHECK(IMP_Encoder_CreateGroup(0) == 0);
    make_attr(&attr, 0);
    CHECK(IMP_Encoder_CreateChn(0, &attr) == 0);

    /* ---- ROI: an enabled region is stored and read back, corners / 16 x 16 ---- */
    memset(&roi, 0, sizeof(roi));
    roi.u32Index = 0; roi.bEnable = true; roi.bRelatedQp = true; roi.s32Qp = -4;
    roi.rect.x = 64; roi.rect.y = 64; roi.rect.width = 319; roi.rect.height = 255;   /* the vendor p0/p1 corners */
    CHECK(IMP_Encoder_SetChnROI(0, &roi) == 0 && roi_calls == 1);
    CHECK(roi_entry[0][0] == 1 && roi_entry[0][1] == 1 && (int8_t)roi_entry[0][2] == -4 &&
          roi_entry[0][3] == 4 && roi_entry[0][4] == 19 && roi_entry[0][5] == 4 && roi_entry[0][6] == 15);
    memset(&got, 0, sizeof(got));
    got.u32Index = 0;
    CHECK(IMP_Encoder_GetChnROI(0, &got) == 0);
    /* the camera read-back: en 1 (64,64)-(304,240) qp -4 */
    CHECK(got.bEnable && got.bRelatedQp && got.s32Qp == -4 && got.rect.x == 64 &&
          got.rect.y == 64 && got.rect.width == 304 && got.rect.height == 240);
    roi.bEnable = false;
    CHECK(IMP_Encoder_SetChnROI(0, &roi) == 0);
    got.u32Index = 0;
    CHECK(IMP_Encoder_GetChnROI(0, &got) == 0 && !got.bEnable);
    roi.u32Index = 8; got.u32Index = 8;                 /* only 0..7 */
    CHECK(IMP_Encoder_SetChnROI(0, &roi) == -1 && IMP_Encoder_GetChnROI(0, &got) == -1);
    CHECK(IMP_Encoder_SetChnROI(0, NULL) == -1);
    CHECK(IMP_Encoder_SetChnROI(99, &roi) == -1);

    /* ---- SuperFrameCfg: the camera values before any Set (640x360 H.264) ---- */
    memset(&sf, 0x5a, sizeof(sf));
    CHECK(IMP_Encoder_GetSuperFrameCfg(0, &sf) == 0);
    CHECK(sf.superFrmMode == IMP_RC_SUPERFRM_REENCODE && sf.superIFrmBitsThr == 942080u &&
          sf.superPFrmBitsThr == 672914u && sf.rcPriority == IMP_RC_PRIORITY_BITRATE_FIRST);
    CHECK(sf.superBFrmBitsThr == 517626u || sf.superBFrmBitsThr == 517627u);
    /* Set takes every mode (clipped to 0..2), limits the I threshold to the stream buffer */
    memset(&sf, 0, sizeof(sf));
    sf.superFrmMode = IMP_RC_SUPERFRM_DISCARD; sf.superIFrmBitsThr = 400000; sf.superPFrmBitsThr = 200000;
    sf.superBFrmBitsThr = 100000; sf.rcPriority = IMP_RC_PRIORITY_FRAMEBITS_FIRST;
    CHECK(IMP_Encoder_SetSuperFrameCfg(0, &sf) == 0);
    memset(&sf, 0x5a, sizeof(sf));
    CHECK(IMP_Encoder_GetSuperFrameCfg(0, &sf) == 0 && sf.superFrmMode == IMP_RC_SUPERFRM_DISCARD &&
          sf.superIFrmBitsThr == 400000u && sf.superPFrmBitsThr == 200000u &&
          sf.superBFrmBitsThr == 100000u && sf.rcPriority == IMP_RC_PRIORITY_FRAMEBITS_FIRST);
    sf.superFrmMode = (IMPEncoderSuperFrmMode)9; sf.superIFrmBitsThr = 0xffffffffu; sf.rcPriority = (IMPEncoderRcPriority)7;
    CHECK(IMP_Encoder_SetSuperFrameCfg(0, &sf) == 0);
    memset(&sf, 0, sizeof(sf));
    CHECK(IMP_Encoder_GetSuperFrameCfg(0, &sf) == 0 && sf.superFrmMode == IMP_RC_SUPERFRM_REENCODE &&
          sf.rcPriority == IMP_RC_PRIORITY_FRAMEBITS_FIRST &&
          sf.superIFrmBitsThr == (640u * 360u * 3u / 2u / 2u) * 8u);   /* (buffer - 0x1080) * 8 */
    CHECK(IMP_Encoder_SetSuperFrameCfg(0, NULL) == -1 && IMP_Encoder_GetSuperFrameCfg(0, NULL) == -1);
    CHECK(IMP_Encoder_SetSuperFrameCfg(6, &sf) == -1 && IMP_Encoder_GetSuperFrameCfg(6, &sf) == -1);
    /* a channel that does not exist: 0, the structure is left alone */
    memset(&sf, 0x5a, sizeof(sf));
    CHECK(IMP_Encoder_GetSuperFrameCfg(4, &sf) == 0 && sf.superIFrmBitsThr == 0x5a5a5a5au);
    CHECK(IMP_Encoder_SetSuperFrameCfg(4, &sf) == 0);
    /* 1920x1080: I = area * 12 / 6, P = I / 1.4, B = P / 1.3 */
    make_attr(&attr, 0);
    attr.encAttr.picWidth = 1920; attr.encAttr.picHeight = 1080;
    CHECK(IMP_Encoder_CreateChn(1, &attr) == 0);
    CHECK(IMP_Encoder_GetSuperFrameCfg(1, &sf) == 0 &&
          sf.superIFrmBitsThr == 1920u * 1088u * 2u && sf.superPFrmBitsThr == (uint32_t)((float)(1920 * 1088 * 2) / 1.4f));
    /* a JPEG channel: Set 0 and nothing, Get 0 and nothing */
    memset(&jattr, 0, sizeof(jattr));
    jattr.encAttr.enType = PT_JPEG;
    jattr.encAttr.picWidth = 640; jattr.encAttr.picHeight = 360;
    jattr.rcAttr.attrRcMode.rcMode = IMP_ENC_RC_MODE_FIXQP;
    if (IMP_Encoder_CreateChn(2, &jattr) == 0) {
        memset(&sf, 0x5a, sizeof(sf));
        CHECK(IMP_Encoder_GetSuperFrameCfg(2, &sf) == 0 && sf.superIFrmBitsThr == 0x5a5a5a5au);
        CHECK(IMP_Encoder_SetSuperFrameCfg(2, &sf) == 0);
    }

    if (failures) {
        fprintf(stderr, "p2_t30_enc_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("p2_t30_enc_test: PASS\n");
    return 0;
}
