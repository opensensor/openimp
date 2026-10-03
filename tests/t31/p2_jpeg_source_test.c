/*
 * p2_jpeg_source_test - a JPEG channel that is the only channel on its
 * framesource (timps' dedicated jpeg.* channel: own framesource, own
 * group, no video channel) must get frames from IMP_Encoder_PollingStream,
 * as with the vendor libimp, where every bound encoder group receives the
 * framesource's frames.
 *
 * Builds src/t40/openimp_p2_encoder.c (T31) against a stub codec and a
 * fake FrameSource that always has a frame. Also checks that a JPEG
 * channel next to a receiving video channel still works (fan-out), and
 * that a picture the codec skips (core busy, rmem short) is replaced by
 * the channel's last JPEG.
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
int AL_Codec_Encode_SetQpIPDelta(void *c, int a) { (void)c; (void)a; return 0; }
int AL_Codec_Encode_SetRcParam(void *c, void *p) { (void)c; (void)p; return 0; }
int AL_Codec_Encode_SetRcQualityCap(void *c, int m, unsigned int p) { (void)c; (void)m; (void)p; return 0; }

/* ---- test ---- */

extern int IMP_System_Bind(IMPCell *source, IMPCell *destination);

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

static int setup(int group, int channel, int fs, int jpeg)
{
    IMPEncoderCHNAttr attr;
    IMPCell src = { DEV_ID_FS, fs, 0 };
    IMPCell dst = { DEV_ID_ENC, group, 0 };

    make_attr(&attr, jpeg);
    if (IMP_Encoder_CreateGroup(group) != 0 ||
        IMP_Encoder_CreateChn(channel, &attr) != 0 ||
        IMP_Encoder_RegisterChn(group, channel) != 0 ||
        IMP_System_Bind(&src, &dst) != 0 ||
        IMP_Encoder_StartRecvPic(channel) != 0)
        return -1;
    return 0;
}

int main(void)
{
    int got = 0, i;

    /* timps' dedicated JPEG channel: framesource 3, group 3, channel 3,
     * nothing else on framesource 3 */
    CHECK(setup(3, 3, 3, 1) == 0, "JPEG channel setup");
    for (i = 0; i < 5; i++)
        got += IMP_Encoder_PollingStream(3, 200) == 0;
    CHECK(fs_gets[3] > 0, "the lone JPEG channel never took a frame from "
          "its framesource (%d PollingStream successes)", got);
    /* one capture frame per PollingStream, each handed back (the stub
     * codec returns no stream, so PollingStream itself reports -1) */
    CHECK(fs_gets[3] == 5 && !fs_outstanding[3],
          "%d frames taken in 5 polls, frame still held %d", fs_gets[3],
          fs_outstanding[3]);

    /* timps' JPEG-on-video channel (jpeg_attach): registered into the main
     * stream's group 0 on framesource 0, next to an H.264 channel that is
     * created, registered and bound but idle (no RTSP client, no
     * StartRecvPic, never polled).  The JPEG channel must read framesource
     * 0 itself instead of waiting for a fan-out that never comes. */
    {
        IMPEncoderCHNAttr attr;
        IMPCell src = { DEV_ID_FS, 0, 0 };
        IMPCell dst = { DEV_ID_ENC, 0, 0 };

        make_attr(&attr, 0);
        CHECK(IMP_Encoder_CreateGroup(0) == 0 &&
              IMP_Encoder_CreateChn(0, &attr) == 0 &&
              IMP_Encoder_RegisterChn(0, 0) == 0 &&
              IMP_System_Bind(&src, &dst) == 0, "idle video channel setup");
        make_attr(&attr, 1);
        CHECK(IMP_Encoder_CreateChn(1, &attr) == 0 &&
              IMP_Encoder_RegisterChn(0, 1) == 0 &&
              IMP_Encoder_StartRecvPic(1) == 0, "JPEG-on-video setup");
        for (i = 0; i < 5; i++)
            (void)IMP_Encoder_PollingStream(1, 200);
        CHECK(fs_gets[0] == 5 && !fs_outstanding[0],
              "JPEG next to an idle video channel took %d frames in 5 "
              "polls (waited for a fan-out that never comes)", fs_gets[0]);
    }

    /* A skipped JPEG: the channel delivers its last picture again, which
     * is no codec stream (ReleaseStream must not hand it to the codec).
     * Lone JPEG channel 2 on framesource 2. */
    {
        IMPEncoderStream stream;

        stub_streams = 1;
        stub_busy = 1;
        CHECK(setup(2, 2, 2, 1) == 0, "reuse channel setup");
        /* nothing to reuse yet: the codec may not skip */
        CHECK(IMP_Encoder_PollingStream(2, 200) == 0, "first JPEG");
        CHECK(IMP_Encoder_GetStream(2, &stream, 0) == 0 &&
              stream.packCount == 1 &&
              !strcmp((const char *)(uintptr_t)stream.virAddr, "JPEG#1"),
              "first JPEG content");
        CHECK(IMP_Encoder_ReleaseStream(2, &stream) == 0,
              "first JPEG release");
        CHECK(!fs_outstanding[2], "capture frame of the first JPEG held");
        /* the codec skips the next one: JPEG#1 again, frame returned */
        CHECK(IMP_Encoder_PollingStream(2, 200) == 0,
              "skipped JPEG not replaced by the last one");
        memset(&stream, 0, sizeof(stream));
        CHECK(IMP_Encoder_GetStream(2, &stream, 0) == 0 &&
              stream.packCount == 1 && stream.streamSize == 7 &&
              !strcmp((const char *)(uintptr_t)stream.virAddr, "JPEG#1"),
              "reused JPEG content");
        CHECK(!fs_outstanding[2], "capture frame of the skipped JPEG held");
        CHECK(IMP_Encoder_ReleaseStream(2, &stream) == 0 &&
              stub_released == 1, "reused JPEG release (%d codec "
              "releases, want 1)", stub_released);
        /* the next encodes normally again */
        stub_busy = 0;
        CHECK(IMP_Encoder_PollingStream(2, 200) == 0 &&
              IMP_Encoder_GetStream(2, &stream, 0) == 0 &&
              !strcmp((const char *)(uintptr_t)stream.virAddr, "JPEG#2"),
              "JPEG after the skip");
        CHECK(IMP_Encoder_ReleaseStream(2, &stream) == 0 &&
              stub_released == 2, "release (%d codec releases)",
              stub_released);
    }

    if (failures) {
        fprintf(stderr, "p2 JPEG source: %d check(s) failed\n", failures);
        return 1;
    }
    printf("p2 JPEG source tests passed\n");
    return 0;
}
