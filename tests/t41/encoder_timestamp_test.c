/* Real P2 public GetStream and JPEG fanout, private already-completed frames. */
#include <assert.h>
#include "../../src/t40/openimp_p2_encoder.c"

/* No hardware is needed or allowed by this fixture. GetStream(block=0) must
 * not enter any of these paths for an already completed stream. */
int IMP_FrameSource_GetFrame(int channel, void **frame)
{ (void)channel; (void)frame; assert(0); return -1; }
int IMP_FrameSource_ReleaseFrame(int channel, void *frame)
{ (void)channel; (void)frame; assert(0); return -1; }
int AL_Codec_Encode_Process(void *codec, void *frame, void *user)
{ (void)codec; (void)frame; (void)user; assert(0); return -1; }
int AL_Codec_Encode_GetStream(void *codec, void **stream, void **user)
{ (void)codec; (void)stream; (void)user; assert(0); return -1; }
int AL_Codec_Encode_ReleaseStream(void *codec, void *stream, void *user)
{ (void)codec; (void)stream; (void)user; assert(0); return -1; }
int DMA_RmemFlushCache(void *address, uint32_t length, int direction)
{ (void)address; (void)length; (void)direction; assert(0); return -1; }

int main(void)
{
    static const uint64_t timestamps[] = {0, 40022, 80044, 200110, 9900000};
    P2HWStream raw = {.length = 1024, .frame_type = 0};
    IMPEncoderStream stream;
    unsigned int channel, i;
    uint8_t pixels[6] = {16, 17, 18, 19, 128, 128};
    P2SyntheticFrame source = {
        .width = 2, .height = 2, .size = sizeof(pixels), .timestamp = 123456,
        .virtual_address = (uintptr_t)pixels,
    };

    assert(EncoderInit() == 0);
    for (channel = 0; channel < 3; ++channel) {
        P2EncoderChannel *ch = &p2_channels[channel];

        ch->created = ch->registered = ch->receiving = 1;
        ch->codec_type = channel == 2 ? IMP_ENC_TYPE_JPEG : IMP_ENC_TYPE_AVC;
        ch->raw_stream = &raw;
        ch->attr.rcAttr.outFrmRate.frmRateNum = channel ? 15 : 25;
        ch->attr.rcAttr.outFrmRate.frmRateDen = 1;
        for (i = 0; i < sizeof(timestamps) / sizeof(timestamps[0]); ++i) {
            raw.timestamp = timestamps[i];
            assert(IMP_Encoder_GetStream(channel, &stream, 0) == 0);
            assert(stream.packCount == 1);
            assert(stream.pack[0].timestamp == (int64_t)timestamps[i]);
        }
    }
    p2_channels[2].source_channel = 0;
    p2_channels[2].jpeg_frame_requested = 1;
    assert(p2_copy_requested_jpeg_frames(0, &source) == 0);
    assert(p2_channels[2].synthetic_frame.timestamp == source.timestamp);
    assert(p2_channels[2].synthetic_frame.virtual_address != source.virtual_address);
    assert(!memcmp(p2_channels[2].jpeg_frame_buffer, pixels, sizeof(pixels)));
    free(p2_channels[2].jpeg_frame_buffer);
    puts("T41 P2 main/substream/JPEG capture timestamps: passed");
    return 0;
}
