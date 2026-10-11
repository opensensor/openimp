/*
 * p2_roi_test - T23 IMP_Encoder_SetChnROI / GetChnROI (src/t40/
 * openimp_p2_encoder.c against a stub codec), the vendor T23 1.3.0
 * behaviour (disassembly of 0x53ea0 / 0x54164):
 *  - a channel that does not exist (>= 9) or no config: -1;
 *  - a channel that is not created: 0, nothing stored;
 *  - an H.264 channel: the i264e table entry (sorted corners / 16) goes to
 *    the encoder and GetChnROI returns it x16, as on T21;
 *  - a failing encoder: -1.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <imp/imp_encoder.h>
#include <imp/imp_system.h>
#include "dma_alloc.h"

int IMP_Encoder_SetChnROI(int, const IMPEncoderROICfg *);
int IMP_Encoder_GetChnROI(int, IMPEncoderROICfg *);

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


uint64_t IMP_System_GetTimeStamp(void) { return 0; }
int openimp_t23_osd_apply(int g, void *f) { (void)g; (void)f; return 0; }
#define T23STUB(name) int name() { return 0; }
T23STUB(openimp_t23_enc_push_fps)
T23STUB(openimp_t23_enc_push_rc)
T23STUB(openimp_t23_enc_push_gop)
T23STUB(openimp_t23_enc_set_lossthd)
T23STUB(openimp_t23_enc_push_color2grey)
T23STUB(openimp_t23_enc_push_denoise)
T23STUB(openimp_t23_enc_push_mbrc)
T23STUB(openimp_t23_enc_push_superframe)
T23STUB(openimp_t23_enc_push_h264trans)
T23STUB(openimp_t23_enc_push_qpgmode)
static int push_roi_calls;
int openimp_t23_enc_push_roi(void *codec, int type, const IMPEncoderROICfg *cfg)
{ (void)codec; (void)type; (void)cfg; push_roi_calls++; return 0; }

int main(void)
{
    IMPEncoderCHNAttr attr;
    IMPEncoderROICfg roi;

    memset(&roi, 0, sizeof(roi));
    roi.u32Index = 1;
    /* not created: vendor answers 0 and keeps nothing */
    CHECK(IMP_Encoder_SetChnROI(0, &roi) == 0 && roi_calls == 0 &&
          push_roi_calls == 0);
    CHECK(IMP_Encoder_SetChnROI(0, NULL) == -1);
    CHECK(IMP_Encoder_SetChnROI(99, &roi) == -1);

    CHECK(IMP_Encoder_CreateGroup(0) == 0);
    make_attr(&attr, 0);
    CHECK(IMP_Encoder_CreateChn(0, &attr) == 0);

    roi.bEnable = 1;
    roi.bRelatedQp = 1;
    roi.s32Qp = -4;
    roi.rect.x = 300;                   /* p0.x */
    roi.rect.y = 199;                   /* p0.y */
    roi.rect.width = 40;                /* p1.x: smaller, swapped */
    roi.rect.height = 359;              /* p1.y */
    CHECK(IMP_Encoder_SetChnROI(0, &roi) == 0 && roi_calls == 1 &&
          push_roi_calls == 1);
    CHECK(roi_entry[1][0] == 1 && roi_entry[1][1] == 1 &&
          roi_entry[1][2] == 0xfc && roi_entry[1][3] == 2 &&
          roi_entry[1][4] == 18 && roi_entry[1][5] == 12 &&
          roi_entry[1][6] == 22);
    /* same representation as T21: the table entry, x16 */
    memset(&roi, 0x5a, sizeof(roi));
    roi.u32Index = 1;
    CHECK(IMP_Encoder_GetChnROI(0, &roi) == 0);
    CHECK(roi.u32Index == 1 && roi.bEnable && roi.bRelatedQp &&
          roi.s32Qp == -4 && roi.rect.x == 32 && roi.rect.width == 288 &&
          roi.rect.y == 192 && roi.rect.height == 352);
    /* out-of-range QP: kept in the entry (clamped per picture), warned once */
    roi.s32Qp = -40;
    CHECK(IMP_Encoder_SetChnROI(0, &roi) == 0 && roi_entry[1][2] == 0xd8);
    roi.u32Index = 8;
    CHECK(IMP_Encoder_SetChnROI(0, &roi) == -1 && roi_calls == 2);
    CHECK(IMP_Encoder_GetChnROI(0, &roi) == -1);

    CHECK(IMP_Encoder_DestroyChn(0) == 0);
    /* destroyed again: like never created */
    roi.u32Index = 1;
    CHECK(IMP_Encoder_SetChnROI(0, &roi) == 0 && roi_calls == 2);
    if (failures) {
        fprintf(stderr, "p2_roi_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("p2_roi_test: ok\n");
    return 0;
}
