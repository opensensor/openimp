/* Real P2 PollingStream; private capture and a deliberately blocked JPEG. */
#include <assert.h>
#include "../../src/t40/openimp_p2_encoder.c"

static pthread_mutex_t fixture_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t fixture_ready = PTHREAD_COND_INITIALIZER;
static int jpeg_entered, release_jpeg, video_done;
static int sync_calls, sync_result;
static unsigned char pixels[384];
static P2SyntheticFrame captured = {
    .width = 16, .height = 16, .size = sizeof(pixels),
    .physical_address = 0x6000000, .timestamp = 40022,
};
static P2HWStream streams[3];

int DMA_RmemFlushCache(void *address, uint32_t length, int direction)
{
    assert(address == pixels && length == 0x100000 && direction == 2);
    ++sync_calls;
    if (!sync_result)
        memset(pixels, 0x5a, sizeof(pixels));
    return sync_result;
}
int IMP_FrameSource_GetFrame(int channel, void **frame)
{
    assert(channel == 0 || channel == 1);
    *frame = &captured;
    return 0;
}
int IMP_FrameSource_ReleaseFrame(int channel, void *frame)
{
    assert((channel == 0 || channel == 1) && frame == &captured);
    return 0;
}
int AL_Codec_Encode_Process(void *codec, void *frame, void *user)
{
    P2EncoderChannel *ch = codec;
    (void)user;
    if (ch->codec_type == IMP_ENC_TYPE_JPEG) {
        const P2SyntheticFrame *copy = frame;
        unsigned int i;
        assert(!copy->physical_address && copy->timestamp == captured.timestamp);
        assert(copy->virtual_address != captured.virtual_address);
        for (i = 0; i < copy->size; ++i)
            assert(((unsigned char *)(uintptr_t)copy->virtual_address)[i] == 0x5a);
        pthread_mutex_lock(&fixture_lock);
        jpeg_entered = 1;
        pthread_cond_broadcast(&fixture_ready);
        while (!release_jpeg)
            pthread_cond_wait(&fixture_ready, &fixture_lock);
        pthread_mutex_unlock(&fixture_lock);
    }
    return 0;
}
int AL_Codec_Encode_GetStream(void *codec, void **stream, void **user)
{
    *stream = &streams[(P2EncoderChannel *)codec - p2_channels];
    *user = NULL;
    return 0;
}
int AL_Codec_Encode_ReleaseStream(void *codec, void *stream, void *user)
{ (void)codec; (void)stream; (void)user; return 0; }

static void *poll_channel(void *argument)
{
    int channel = (int)(intptr_t)argument;
    assert(!IMP_Encoder_PollingStream(channel, 1000));
    if (channel == 1) {
        pthread_mutex_lock(&fixture_lock);
        video_done = 1;
        pthread_cond_broadcast(&fixture_ready);
        pthread_mutex_unlock(&fixture_lock);
    }
    return NULL;
}

static int wait_flag(int *flag)
{
    struct timespec deadline;
    int result = 0;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += 3;
    while (!*flag && !result)
        result = pthread_cond_timedwait(&fixture_ready, &fixture_lock, &deadline);
    return *flag;
}

int main(void)
{
    pthread_t jpeg, video;
    unsigned int i;
    int independent;
    captured.virtual_address = (uintptr_t)pixels;
    assert(!EncoderInit());
    for (i = 0; i < 4; ++i) {
        P2EncoderChannel *ch = &p2_channels[i];
        ch->created = ch->registered = ch->receiving = 1;
        ch->codec_type = i >= 2 ? IMP_ENC_TYPE_JPEG : IMP_ENC_TYPE_AVC;
        ch->codec = ch;
        ch->source_channel = i == 1 ? 1 : 0;
        ch->attr.rcAttr.outFrmRate.frmRateNum = 25;
        ch->attr.rcAttr.outFrmRate.frmRateDen = 1;
    }
    /* Failed coherence must not publish stale pixels to either JPEG owner. */
    sync_result = -1;
    p2_channels[2].jpeg_frame_requested = p2_channels[3].jpeg_frame_requested = 1;
    assert(!p2_copy_requested_jpeg_frames(0, &captured));
    assert(sync_calls == 1 && !p2_channels[2].jpeg_frame_generation &&
           !p2_channels[3].jpeg_frame_generation);
    assert(!p2_channels[2].jpeg_frame_requested && !p2_channels[3].jpeg_frame_requested);
    sync_result = 0;
    sync_calls = 0;
    p2_channels[2].jpeg_frame_requested = p2_channels[3].jpeg_frame_requested = 1;
    assert(!p2_copy_requested_jpeg_frames(0, &captured));
    assert(sync_calls == 1 && p2_channels[2].jpeg_frame_generation == 1 &&
           p2_channels[3].jpeg_frame_generation == 1);

    assert(!pthread_create(&jpeg, NULL, poll_channel, (void *)2));
    for (i = 0; i < 3000; ++i) {
        int requested;
        pthread_mutex_lock(&p2_channels[2].lock);
        requested = p2_channels[2].jpeg_frame_requested;
        pthread_mutex_unlock(&p2_channels[2].lock);
        if (requested)
            break;
        usleep(1000);
    }
    assert(i < 3000);
    /* Main produces the owned snapshot, then sub must encode while its
     * software JPEG consumer is still blocked inside Process. */
    assert(!IMP_Encoder_PollingStream(0, 1000));
    pthread_mutex_lock(&fixture_lock);
    assert(wait_flag(&jpeg_entered));
    pthread_mutex_unlock(&fixture_lock);
    assert(!pthread_create(&video, NULL, poll_channel, (void *)1));
    pthread_mutex_lock(&fixture_lock);
    independent = wait_flag(&video_done);
    release_jpeg = 1;
    pthread_cond_broadcast(&fixture_ready);
    pthread_mutex_unlock(&fixture_lock);
    pthread_join(video, NULL);
    pthread_join(jpeg, NULL);
    assert(independent);
    free(p2_channels[2].jpeg_frame_buffer);
    free(p2_channels[3].jpeg_frame_buffer);
    puts("T41 JPEG: coherent fanout and independent video progress passed");
    return 0;
}
