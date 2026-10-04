/*
 * p2_rc_mode_test - T31 rate-control modes through the P2 encoder API.
 *
 * CappedVBR (4) and CappedQuality (8) run the codec's VBR with the OEM PSNR
 * cap: CreateChn and SetChnAttrRcMode hand the mode and uMaxPSNR to
 * AL_Codec_Encode_SetRcQualityCap, iIPDelta reaches the codec as for VBR,
 * GetChnAttrRcMode returns the attribute as given (the OEM returns its
 * stored copy).  Other modes clear the cap.
 *
 * Builds src/t40/openimp_p2_encoder.c (T31) against a stub codec.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <imp/imp_encoder.h>
#include <imp/imp_system.h>
#include "dma_alloc.h"

static int failures;

#define CHECK(cond, ...) do {                                           \
        if (!(cond)) {                                                  \
            failures++;                                                 \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);        \
            fprintf(stderr, __VA_ARGS__);                               \
            fputc('\n', stderr);                                        \
        }                                                               \
    } while (0)

/* ---- fake FrameSource: one frame record per channel, always ready ---- */

typedef struct {
    int32_t index;
    int32_t pool_index;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    uint32_t size;
    uint32_t physical_address;
    uint32_t virtual_address;
    int64_t timestamp;
} FakeFrame;

static FakeFrame fs_frames[4];
static uint8_t fs_pixels[4][640 * 368 * 3 / 2];
static int fs_gets[4];
static int fs_outstanding[4];

int IMP_FrameSource_GetFrame(int chn, void **frame)
{
    if (chn < 0 || chn >= 4 || fs_outstanding[chn])
        return -1;
    fs_frames[chn].width = 640;
    fs_frames[chn].height = 360;
    fs_frames[chn].pixel_format = 0x3231564e;
    fs_frames[chn].size = sizeof(fs_pixels[chn]);
    fs_frames[chn].virtual_address = (uint32_t)(uintptr_t)fs_pixels[chn];
    fs_frames[chn].timestamp++;
    fs_gets[chn]++;
    fs_outstanding[chn] = 1;
    *frame = &fs_frames[chn];
    return 0;
}

int IMP_FrameSource_ReleaseFrame(int chn, void *frame)
{
    if (chn < 0 || chn >= 4 || frame != &fs_frames[chn])
        return -1;
    fs_outstanding[chn] = 0;
    return 0;
}

unsigned int VBMReadySequence(int chn) { (void)chn; return 0; }
int VBMWaitReady(int chn, unsigned int seq, uint32_t us)
{
    (void)chn; (void)seq; (void)us;
    return 0;
}
void VBMWakeReaders(int chn) { (void)chn; }
int OpenIMP_T31_HwJpegActive(void) { return 0; }
void openimp_t31_osd_apply(int group, void *frame) { (void)group; (void)frame; }
void openimp_t31_osd_apply_ex(int group, void *frame, unsigned int flags)
{ (void)group; (void)frame; (void)flags; }

int DMA_AllocDescriptor(IMPDMABufferInfo *info, int size, const char *tag)
{
    (void)info; (void)size; (void)tag;
    return -1;                  /* JPEG copies stay in normal memory */
}
int DMA_FreePhys(uint32_t phys) { (void)phys; return 0; }
int DMA_RmemFlushCache(void *virt, uint32_t size, int dir)
{
    (void)virt; (void)size; (void)dir;
    return 0;
}

/* ---- stub codec: Process records the frame, GetStream returns one ---- */

/* the codec's stream record (P2HWStream) */
typedef struct {
    uint32_t phys_addr;
    uint32_t virt_addr;
    uint32_t length;
    uint64_t timestamp;
    uint32_t frame_type;
    uint32_t slice_type;
    uint32_t reserved[8];
} StubStream;

typedef struct {
    int pending;
    int encoded;
    int skip_allowed;
    int skipped;
    StubStream hw;
    char jpeg[32];
} StubCodec;

/* stub_streams: GetStream hands out jpeg[] (non-PIE build: the 32-bit
 * stream addresses hold the pointers); stub_busy: a JPEG that may be
 * skipped is (AL_Codec_Encode_SetJpegSkip) */
static int stub_streams;
static int stub_busy;
static int stub_released;       /* streams given back to the codec */

int AL_Codec_Encode_Create(void **codec, void *params)
{
    (void)params;
    *codec = calloc(1, sizeof(StubCodec));
    return *codec ? 0 : -1;
}
int AL_Codec_Encode_Destroy(void *codec) { free(codec); return 0; }
int AL_Codec_Encode_Process(void *codec, void *frame, void *user)
{
    StubCodec *c = codec;

    (void)user;
    if (!frame)
        return -1;
    if (stub_busy && c->skip_allowed) {
        c->skipped = 1;
        return -1;
    }
    c->pending = 1;
    c->encoded++;
    return 0;
}
int AL_Codec_Encode_GetStream(void *codec, void **stream, void **user)
{
    StubCodec *c = codec;

    if (!c->pending)
        return 1;
    c->pending = 0;
    *user = NULL;
    if (!stub_streams) {
        *stream = NULL;         /* nothing to hand out: PollingStream's
                                   frame reached the codec, that is all
                                   the source checks need */
        return 0;
    }
    snprintf(c->jpeg, sizeof(c->jpeg), "JPEG#%d", c->encoded);
    memset(&c->hw, 0, sizeof(c->hw));
    c->hw.virt_addr = (uint32_t)(uintptr_t)c->jpeg;
    c->hw.length = (uint32_t)strlen(c->jpeg) + 1u;
    *stream = &c->hw;
    return 0;
}
int AL_Codec_Encode_ReleaseStream(void *codec, void *stream, void *user)
{
    StubCodec *c = codec;

    (void)user;
    if (stream != &c->hw)
        return -1;
    stub_released++;
    return 0;
}
int AL_Codec_Encode_SetJpegSkip(void *codec, int allow)
{
    StubCodec *c = codec;

    c->skip_allowed = allow;
    c->skipped = 0;
    return 0;
}
int AL_Codec_Encode_JpegSkipped(void *codec)
{
    return ((StubCodec *)codec)->skipped;
}
#define STUB0(name) int name(void *codec) { (void)codec; return 0; }
#define STUB1(name, t) int name(void *codec, t a) { (void)codec; (void)a; return 0; }
STUB0(AL_Codec_Encode_RequestIDR)
STUB1(AL_Codec_Encode_SetBitRate, int)
STUB1(AL_Codec_Encode_SetEntropyMode, int)
STUB1(AL_Codec_Encode_SetGopLength, int)
STUB1(AL_Codec_Encode_SetJpegQuality, int)
STUB1(AL_Codec_Encode_SetQp, int)
STUB1(AL_Codec_Encode_SetStreamBufferCount, int)
STUB1(AL_Codec_Encode_SetStreamBufferSize, int)
int AL_Codec_Encode_SetDefaultParam(void *params) { (void)params; return 0; }
int AL_Codec_Encode_SetFrameRate(void *c, int a, int b) { (void)c; (void)a; (void)b; return 0; }
int AL_Codec_Encode_SetGopParam(void *c, void *p) { (void)c; (void)p; return 0; }
int AL_Codec_Encode_SetQpBounds(void *c, int a, int b) { (void)c; (void)a; (void)b; return 0; }


/* ---- recorded rate-control calls ---- */
static int cap_calls;
static int cap_mode = -1;
static unsigned int cap_psnr;
static int ip_delta_calls;
static int ip_delta;
static int rc_param_calls;
static unsigned int rc_param_mode;
static unsigned int rc_param_target;
static unsigned int rc_param_max;

int AL_Codec_Encode_SetRcQualityCap(void *c, int mode, unsigned int psnr)
{
    (void)c;
    cap_calls++;
    cap_mode = mode;
    cap_psnr = psnr;
    return 0;
}
int AL_Codec_Encode_SetQpIPDelta(void *c, int a)
{
    (void)c;
    ip_delta_calls++;
    ip_delta = a;
    return 0;
}
int AL_Codec_Encode_SetRcParam(void *c, void *p)
{
    IMPEncoderRcAttr *rc = p;

    (void)c;
    rc_param_calls++;
    rc_param_mode = rc->attrRcMode.rcMode;
    rc_param_target = rc->attrRcMode.attrCappedVbr.uTargetBitRate;
    rc_param_max = rc->attrRcMode.attrCappedVbr.uMaxBitRate;
    return 0;
}

/* ---- test ---- */

static void make_attr(IMPEncoderCHNAttr *attr, IMPEncoderRcMode mode)
{
    memset(attr, 0, sizeof(*attr));
    attr->encAttr.profile = IMP_ENC_PROFILE_AVC_HIGH;
    attr->encAttr.uWidth = 640;
    attr->encAttr.uHeight = 360;
    attr->rcAttr.outFrmRate.frmRateNum = 25;
    attr->rcAttr.outFrmRate.frmRateDen = 1;
    attr->gopAttr.uGopLength = 50;
    attr->rcAttr.attrRcMode.rcMode = mode;
    attr->rcAttr.attrRcMode.attrCappedVbr.uTargetBitRate = 1500;
    attr->rcAttr.attrRcMode.attrCappedVbr.uMaxBitRate = 2500;
    attr->rcAttr.attrRcMode.attrCappedVbr.iInitialQP = -1;
    attr->rcAttr.attrRcMode.attrCappedVbr.iMinQP = 22;
    attr->rcAttr.attrRcMode.attrCappedVbr.iMaxQP = 48;
    attr->rcAttr.attrRcMode.attrCappedVbr.iIPDelta = -2;
    attr->rcAttr.attrRcMode.attrCappedVbr.uMaxPictureSize = 2500;
    if (mode == IMP_ENC_RC_MODE_CAPPED_VBR ||
        mode == IMP_ENC_RC_MODE_CAPPED_QUALITY)
        attr->rcAttr.attrRcMode.attrCappedVbr.uMaxPSNR = 42;
}

int main(void)
{
    IMPEncoderCHNAttr attr;
    IMPEncoderAttrRcMode mode;
    static const IMPEncoderRcMode capped[] = {
        IMP_ENC_RC_MODE_CAPPED_VBR, IMP_ENC_RC_MODE_CAPPED_QUALITY
    };
    unsigned int i;

    CHECK(IMP_Encoder_CreateGroup(0) == 0, "group");
    for (i = 0; i < 2; i++) {
        int chn = (int)i;

        cap_calls = ip_delta_calls = 0;
        make_attr(&attr, capped[i]);
        CHECK(IMP_Encoder_CreateChn(chn, &attr) == 0, "CreateChn mode %d",
              capped[i]);
        CHECK(cap_calls == 1 && cap_mode == (int)capped[i] &&
              cap_psnr == 42u, "mode %d: cap calls %d mode %d psnr %u",
              capped[i], cap_calls, cap_mode, cap_psnr);
        CHECK(ip_delta_calls == 1 && ip_delta == -2,
              "mode %d: iIPDelta not passed (%d calls, %d)", capped[i],
              ip_delta_calls, ip_delta);
        memset(&mode, 0, sizeof(mode));
        CHECK(IMP_Encoder_GetChnAttrRcMode(chn, &mode) == 0 &&
              mode.rcMode == capped[i] &&
              mode.attrCappedVbr.uMaxPSNR == 42 &&
              mode.attrCappedVbr.uTargetBitRate == 1500 &&
              mode.attrCappedVbr.uMaxBitRate == 2500 &&
              mode.attrCappedVbr.iMinQP == 22 &&
              mode.attrCappedVbr.iMaxQP == 48,
              "mode %d read back as %d psnr %u", capped[i], mode.rcMode,
              mode.attrCappedVbr.uMaxPSNR);
    }

    /* run time: a new cap, then CBR clears it */
    mode.rcMode = IMP_ENC_RC_MODE_CAPPED_QUALITY;
    mode.attrCappedQuality.uMaxPSNR = 38;
    cap_calls = rc_param_calls = 0;
    CHECK(IMP_Encoder_SetChnAttrRcMode(1, &mode) == 0, "SetChnAttrRcMode");
    CHECK(rc_param_calls == 1 && rc_param_mode == IMP_ENC_RC_MODE_CAPPED_QUALITY
          && rc_param_target == 1500000u && rc_param_max == 2500000u,
          "codec rc %u target %u max %u", rc_param_mode, rc_param_target,
          rc_param_max);
    CHECK(cap_calls == 1 && cap_mode == IMP_ENC_RC_MODE_CAPPED_QUALITY &&
          cap_psnr == 38u, "run-time cap %d/%u", cap_mode, cap_psnr);
    memset(&mode, 0, sizeof(mode));
    CHECK(IMP_Encoder_GetChnAttrRcMode(1, &mode) == 0 &&
          mode.attrCappedQuality.uMaxPSNR == 38, "run-time read-back");

    memset(&mode, 0, sizeof(mode));
    mode.rcMode = IMP_ENC_RC_MODE_CBR;
    mode.attrCbr.uTargetBitRate = 1000;
    mode.attrCbr.iMinQP = 20;
    mode.attrCbr.iMaxQP = 45;
    cap_calls = 0;
    CHECK(IMP_Encoder_SetChnAttrRcMode(1, &mode) == 0 && cap_calls == 1 &&
          cap_mode == IMP_ENC_RC_MODE_CBR, "CBR does not clear the cap");

    /* a VBR channel hands its mode over too (the codec clears the cap) */
    cap_calls = 0;
    make_attr(&attr, IMP_ENC_RC_MODE_VBR);
    CHECK(IMP_Encoder_CreateChn(2, &attr) == 0 && cap_calls == 1 &&
          cap_mode == IMP_ENC_RC_MODE_VBR, "VBR CreateChn cap call");

    /* SetDefaultParam: the OEM default cap is 42 dB */
    CHECK(IMP_Encoder_SetDefaultParam(&attr, IMP_ENC_PROFILE_AVC_HIGH,
                                      IMP_ENC_RC_MODE_CAPPED_QUALITY, 1920,
                                      1080, 25, 1, 50, 1, -1, 3000) == 0 &&
          attr.rcAttr.attrRcMode.rcMode == IMP_ENC_RC_MODE_CAPPED_QUALITY &&
          attr.rcAttr.attrRcMode.attrCappedQuality.uMaxPSNR == 42 &&
          attr.rcAttr.attrRcMode.attrCappedQuality.uTargetBitRate == 3000,
          "SetDefaultParam CappedQuality psnr %u",
          attr.rcAttr.attrRcMode.attrCappedQuality.uMaxPSNR);
    CHECK(IMP_Encoder_SetDefaultParam(&attr, IMP_ENC_PROFILE_AVC_HIGH,
                                      IMP_ENC_RC_MODE_VBR, 1920, 1080, 25, 1,
                                      50, 1, -1, 3000) == 0 &&
          attr.rcAttr.attrRcMode.attrCappedVbr.uMaxPSNR == 0,
          "SetDefaultParam VBR sets a cap");

    /* SetDefaultParam rc fields as the OEM T31 1.1.6 (0x831a0) */
    {
        const IMPEncoderAttrVbr *v = &attr.rcAttr.attrRcMode.attrVbr;
        const IMPEncoderAttrCbr *c = &attr.rcAttr.attrRcMode.attrCbr;

        CHECK(IMP_Encoder_SetDefaultParam(&attr, IMP_ENC_PROFILE_AVC_HIGH,
                                          IMP_ENC_RC_MODE_VBR, 1920, 1080,
                                          25, 1, 50, 1, 30, 3000) == 0 &&
              v->uTargetBitRate == 3000 && v->uMaxBitRate == 4000 &&
              v->iInitialQP == 30 && v->iMinQP == 15 && v->iMaxQP == 48 &&
              v->iIPDelta == -1 && v->iPBDelta == -1 &&
              v->eRcOptions == 1 && v->uMaxPictureSize == 6000 &&
              v->uMaxPSNR == 0,
              "SetDefaultParam VBR max %u qp %d/%d/%d pb %d opt %u pic %u",
              v->uMaxBitRate, v->iInitialQP, v->iMinQP, v->iMaxQP,
              v->iPBDelta, v->eRcOptions, v->uMaxPictureSize);
        CHECK(IMP_Encoder_SetDefaultParam(&attr, IMP_ENC_PROFILE_AVC_HIGH,
                                          IMP_ENC_RC_MODE_CAPPED_VBR, 1920,
                                          1080, 25, 1, 50, 1, -1, 1000) == 0 &&
              v->uMaxBitRate == 1333 && v->iInitialQP == -1 &&
              v->uMaxPSNR == 42 && v->uMaxPictureSize == 2000,
              "SetDefaultParam CappedVBR max %u qp %d", v->uMaxBitRate,
              v->iInitialQP);
        CHECK(IMP_Encoder_SetDefaultParam(&attr, IMP_ENC_PROFILE_HEVC_MAIN,
                                          IMP_ENC_RC_MODE_CBR, 1920, 1080,
                                          25, 1, 50, 1, -1, 2000) == 0 &&
              c->uTargetBitRate == 2000 && c->iInitialQP == -1 &&
              c->iMinQP == 15 && c->iMaxQP == 48 && c->iIPDelta == -1 &&
              c->iPBDelta == -1 && c->eRcOptions == 1 &&
              c->uMaxPictureSize == 4000,
              "SetDefaultParam CBR qp %d/%d/%d pb %d opt %u pic %u",
              c->iInitialQP, c->iMinQP, c->iMaxQP, c->iPBDelta,
              c->eRcOptions, c->uMaxPictureSize);
    }

    /* API boundary: a bad handle or NULL pointer is -1 (one limited log
     * line), never a crash, and does not disturb the channel table. */
    {
        IMPEncoderCHNAttr bad_attr;
        IMPEncoderStream bad_stream;
        int bad_value = 0;

        memset(&bad_attr, 0, sizeof(bad_attr));
        memset(&bad_stream, 0, sizeof(bad_stream));
        CHECK(IMP_Encoder_CreateChn(-1, &bad_attr) == -1, "CreateChn(-1)");
        CHECK(IMP_Encoder_CreateChn(4096, &bad_attr) == -1, "CreateChn(4096)");
        CHECK(IMP_Encoder_CreateChn(0, NULL) == -1, "CreateChn(NULL)");
        CHECK(IMP_Encoder_GetStream(-5, &bad_stream, 0) == -1, "GetStream(-5)");
        CHECK(IMP_Encoder_GetStream(0, NULL, 0) == -1, "GetStream(NULL)");
        CHECK(IMP_Encoder_ReleaseStream(0, NULL) == -1, "ReleaseStream(NULL)");
        CHECK(IMP_Encoder_ReleaseStream(99999, &bad_stream) == -1,
              "ReleaseStream(99999)");
        CHECK(IMP_Encoder_StartRecvPic(-1) == -1, "StartRecvPic(-1)");
        CHECK(IMP_Encoder_StopRecvPic(1 << 20) == -1, "StopRecvPic(huge)");
        CHECK(IMP_Encoder_PollingStream(-1, 10) == -1, "PollingStream(-1)");
        CHECK(IMP_Encoder_DestroyChn(-1) == -1, "DestroyChn(-1)");
        CHECK(IMP_Encoder_CreateGroup(-1) == -1, "CreateGroup(-1)");
        CHECK(IMP_Encoder_RegisterChn(-1, 0) == -1, "RegisterChn(-1,0)");
        CHECK(IMP_Encoder_GetMaxStreamCnt(0, NULL) == -1, "GetMaxStreamCnt(NULL)");
        CHECK(IMP_Encoder_GetMaxStreamCnt(-1, &bad_value) == -1,
              "GetMaxStreamCnt(-1)");
    }

    if (failures) {
        fprintf(stderr, "p2 rc mode: %d check(s) failed\n", failures);
        return 1;
    }
    printf("p2 rc mode tests passed\n");
    return 0;
}
