/*
 * p2_bufshare_test - IMP_Encoder_SetbufshareChn: the vendor's argument
 * checks and return codes (T31/T40: -1, T41: 0x80040001; a negative share
 * channel is "no sharing" and valid), the stored value surviving CreateChn
 * and being cleared by DestroyChn, and CreateChn of a JPEG channel next to
 * a missing / JPEG share channel (the vendor fails there; OpenIMP warns and
 * creates the channel, since nothing is shared).  Values and codes are read
 * from the disassembly of the vendor libimp (T31 1.1.6, T40 1.3.1, T41
 * 1.2.6).
 *
 * Builds src/t40/openimp_p2_encoder.c against a stub codec and a fake
 * FrameSource.  Build with -DPLATFORM_T31, -DPLATFORM_T40 or -DPLATFORM_T41.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <imp/imp_encoder.h>
#include <imp/imp_system.h>
#include "../../src/t40/openimp_p2_encoder.c"

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

int IMP_FrameSource_GetFrame(int chn, void **frame);
int VBMGetFrame(int chn, void **frame) { return IMP_FrameSource_GetFrame(chn, frame); }
int OpenIMP_T40_FrameSource_TryGetFrame(int chn, void **frame) { return IMP_FrameSource_GetFrame(chn, frame); }

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
int openimp_t31_osd_apply(int group, void *frame) { (void)group; (void)frame; return 0; }
int openimp_t31_osd_apply_ex(int group, void *frame, unsigned int flags)
{ (void)group; (void)frame; (void)flags; return 0; }

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
int AL_Codec_Encode_SetBitRate(void *c, int a, int b) { (void)c; (void)a; (void)b; return 0; }
STUB1(AL_Codec_Encode_SetEntropyMode, int)
STUB1(AL_Codec_Encode_SetGopLength, int)
STUB1(AL_Codec_Encode_SetJpegQuality, int)
int AL_Codec_Encode_SetQp(void *c, void *a) { (void)c; (void)a; return 0; }
STUB1(AL_Codec_Encode_SetStreamBufferCount, int)
STUB1(AL_Codec_Encode_SetStreamBufferSize, int)
int AL_Codec_Encode_SetDefaultParam(void *params) { (void)params; return 0; }
int AL_Codec_Encode_SetFrameRate(void *c, void *a) { (void)c; (void)a; return 0; }
int AL_Codec_Encode_SetGopParam(void *c, void *p) { (void)c; (void)p; return 0; }
int AL_Codec_Encode_SetQpBounds(void *c, int a, int b) { (void)c; (void)a; (void)b; return 0; }
int AL_Codec_Encode_SetQpIPDelta(void *c, int a) { (void)c; (void)a; return 0; }
int AL_Codec_Encode_SetRcParam(void *c, void *p) { (void)c; (void)p; return 0; }
int AL_Codec_Encode_SetRcQualityCap(void *c, int m, unsigned int p) { (void)c; (void)m; (void)p; return 0; }

int AL_Codec_Encode_SetRoiAttr(void *c, const void *a) { (void)c; (void)a; return 0; }
#if defined(PLATFORM_T40)
int AL_Codec_Encode_SetSei(void *c, int m, const uint8_t *u, const uint8_t *d, uint32_t n);
int AL_Codec_Encode_SetSei(void *c, int m, const uint8_t *u, const uint8_t *d, uint32_t n) { (void)c; (void)m; (void)u; (void)d; (void)n; return 0; }
#endif
int AL_Codec_Encode_SetMapRoi(void *c, const void *a) { (void)c; (void)a; return 0; }
int AL_Codec_Encode_SetJpegQl(void *c, int e, const uint8_t t[128])
{ (void)c; (void)e; (void)t; return 0; }
void openimp_t23_osd_apply(int group, void *frame) { (void)group; (void)frame; }

/* ---- test ---- */

extern int IMP_System_Bind(IMPCell *source, IMPCell *destination);

#if defined(PLATFORM_T41)
#define VENDOR_ERR ((int)0x80040001u)
#else
#define VENDOR_ERR (-1)
#endif

static void make_attr(IMPEncoderCHNAttr *attr, int jpeg)
{
    memset(attr, 0, sizeof(*attr));
    attr->encAttr.profile = jpeg ? IMP_ENC_PROFILE_JPEG
                                 : IMP_ENC_PROFILE_AVC_MAIN;
    attr->encAttr.uWidth = 640;
    attr->encAttr.uHeight = 360;
    attr->rcAttr.attrRcMode.rcMode = IMP_ENC_RC_MODE_FIXQP;
    attr->rcAttr.attrRcMode.attrFixQp.iInitialQP = 30;
    attr->rcAttr.outFrmRate.frmRateNum = 25;
    attr->rcAttr.outFrmRate.frmRateDen = 1;
}

int main(void)
{
    IMPEncoderCHNAttr video, jpeg;
    int last = P2_MAX_CHANNELS - 1;

    make_attr(&video, 0);
    make_attr(&jpeg, 1);

    /* argument checks: both numbers below the limit, share may be < 0 */
    CHECK(IMP_Encoder_SetbufshareChn(2, 0) == 0, "JPEG 2 shares 0");
    CHECK(p2_channels[2].bufshare_chn == 0, "stored %d",
          p2_channels[2].bufshare_chn);
    CHECK(IMP_Encoder_SetbufshareChn(2, -1) == 0, "share -1 is valid");
    CHECK(p2_channels[2].bufshare_chn == -1, "stored %d",
          p2_channels[2].bufshare_chn);
    CHECK(IMP_Encoder_SetbufshareChn(2, -7) == 0, "any negative share passes");
    CHECK(IMP_Encoder_SetbufshareChn(last, last) == 0, "highest numbers");
    CHECK(IMP_Encoder_SetbufshareChn(P2_MAX_CHANNELS, 0) == VENDOR_ERR,
          "encChn at the limit");
    CHECK(IMP_Encoder_SetbufshareChn(-1, 0) == VENDOR_ERR, "encChn -1");
    CHECK(IMP_Encoder_SetbufshareChn(0, P2_MAX_CHANNELS) == VENDOR_ERR,
          "share at the limit");
    CHECK(IMP_Encoder_SetbufshareChn(P2_MAX_CHANNELS, P2_MAX_CHANNELS) ==
          VENDOR_ERR, "both at the limit");
    CHECK(p2_channels[0].bufshare_chn == -1,
          "a rejected call stored a value (%d)", p2_channels[0].bufshare_chn);
    CHECK(p2_channels[last].bufshare_chn == last, "highest stored");
    CHECK(IMP_Encoder_SetbufshareChn(last, -1) == 0, "reset");
    /* default of every channel */
    {
        int i, bad = 0;

        for (i = 0; i < P2_MAX_CHANNELS; i++)
            bad += p2_channels[i].bufshare_chn != (i == 2 ? -7 : -1);
        CHECK(!bad, "%d channels with a wrong default / stored value", bad);
    }
    CHECK(IMP_Encoder_SetbufshareChn(2, -1) == 0, "back to the default");

    /* the value survives CreateChn, set before and after, and is used only
     * for a JPEG channel: video 0, JPEG 2 shares 0 */
    CHECK(IMP_Encoder_CreateGroup(0) == 0 && IMP_Encoder_CreateGroup(2) == 0,
          "groups");
    CHECK(IMP_Encoder_SetbufshareChn(2, 0) == 0, "JPEG 2 shares 0 (again)");
    CHECK(IMP_Encoder_SetbufshareChn(0, 1) == 0, "a video channel's value");
    CHECK(IMP_Encoder_CreateChn(0, &video) == 0, "video CreateChn");
    CHECK(p2_channels[0].bufshare_chn == 1, "video channel kept %d",
          p2_channels[0].bufshare_chn);
    CHECK(IMP_Encoder_CreateChn(2, &jpeg) == 0, "JPEG CreateChn");
    CHECK(p2_channels[2].bufshare_chn == 0 && p2_channels[2].created,
          "JPEG channel kept %d", p2_channels[2].bufshare_chn);
    CHECK(p2_channels[0].created && p2_channels[0].codec_type !=
          IMP_ENC_TYPE_JPEG && p2_channels[2].codec_type ==
          IMP_ENC_TYPE_JPEG, "channel types");
    /* allowed on a created channel (the vendor does not look) */
    CHECK(IMP_Encoder_SetbufshareChn(2, 3) == 0 &&
          p2_channels[2].bufshare_chn == 3, "set on a created channel");
    /* DestroyChn clears it, for both channels */
    CHECK(IMP_Encoder_DestroyChn(2) == 0, "JPEG DestroyChn");
    CHECK(p2_channels[2].bufshare_chn == -1, "JPEG after Destroy: %d",
          p2_channels[2].bufshare_chn);
    CHECK(IMP_Encoder_DestroyChn(0) == 0, "video DestroyChn");
    CHECK(p2_channels[0].bufshare_chn == -1, "video after Destroy: %d",
          p2_channels[0].bufshare_chn);

    /* Where the vendor fails (share channel not created yet, share channel
     * is a JPEG channel, share channel is the channel itself) CreateChn
     * still succeeds here and shares nothing */
    CHECK(IMP_Encoder_SetbufshareChn(2, 0) == 0, "share a missing channel");
    CHECK(IMP_Encoder_CreateChn(2, &jpeg) == 0 && p2_channels[2].created,
          "JPEG before its video channel");
    CHECK(IMP_Encoder_CreateChn(1, &jpeg) == 0, "second JPEG channel");
    CHECK(IMP_Encoder_SetbufshareChn(1, 2) == 0 &&
          IMP_Encoder_SetbufshareChn(3, 3) == 0, "share a JPEG / itself");
    CHECK(IMP_Encoder_DestroyChn(1) == 0, "destroy 1");
    CHECK(IMP_Encoder_SetbufshareChn(1, 2) == 0, "share a JPEG channel");
    CHECK(IMP_Encoder_CreateChn(1, &jpeg) == 0, "JPEG sharing a JPEG");
    CHECK(IMP_Encoder_CreateChn(3, &jpeg) == 0, "JPEG sharing itself");
    CHECK(IMP_Encoder_DestroyChn(3) == 0 && IMP_Encoder_DestroyChn(1) == 0 &&
          IMP_Encoder_DestroyChn(2) == 0, "cleanup");

#if defined(PLATFORM_T40)
    /* T40 1.3.1 Set/GetFisheyeEnableStatus: a flag, settable only while the
     * channel is not created */
    {
        int v = 7;

        CHECK(IMP_Encoder_GetFisheyeEnableStatus(0, &v) == 0 && v == 0,
              "fisheye default");
        CHECK(IMP_Encoder_SetFisheyeEnableStatus(0, 5) == 0 &&
              IMP_Encoder_GetFisheyeEnableStatus(0, &v) == 0 && v == 1,
              "fisheye on");
        CHECK(IMP_Encoder_CreateChn(0, &video) == 0, "video CreateChn again");
        CHECK(IMP_Encoder_SetFisheyeEnableStatus(0, 0) == -1 &&
              IMP_Encoder_GetFisheyeEnableStatus(0, &v) == 0 && v == 1,
              "fisheye refused once created");
        CHECK(IMP_Encoder_DestroyChn(0) == 0, "video DestroyChn again");
        CHECK(IMP_Encoder_SetFisheyeEnableStatus(0, 0) == 0 &&
              IMP_Encoder_GetFisheyeEnableStatus(0, &v) == 0 && v == 0,
              "fisheye off");
        CHECK(IMP_Encoder_SetFisheyeEnableStatus(-1, 1) == -1 &&
              IMP_Encoder_SetFisheyeEnableStatus(P2_MAX_CHANNELS, 1) == -1 &&
              IMP_Encoder_GetFisheyeEnableStatus(0, NULL) == -1,
              "fisheye argument checks");
    }
#endif

    if (failures) {
        fprintf(stderr, "p2_bufshare_test: %d FAILED\n", failures);
        return 1;
    }
    printf("p2_bufshare_test OK\n");
    return 0;
}
