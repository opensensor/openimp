/* IMP_AENC / IMP_ADEC: the software audio encoder and decoder channels of the
 * Ingenic IMP audio API.  Platform neutral - the T23 SDK 1.3.0 and T31 SDK
 * 1.1.x headers declare the same ABI for them, which differs from OpenIMP's
 * generic include/imp/imp_audio.h (16-byte codec names, no maxFrmLen in the
 * decoder), so the vendor layout is declared here.
 *
 * Behaviour follows the OEM aenc.c/adec.c:
 *   - built-in codecs for PT_G711A (1), PT_G711U (2), PT_G726 (3, 32 kbit/s)
 *     and PT_ADPCM (5); IMP_*_Register* adds up to five custom codecs and
 *     returns their payload type in slots 6..10;
 *   - codec callbacks get a NULL codec handle (open at CreateChn, close at
 *     DestroyChn), the output capacity in *outLen, and run synchronously in
 *     SendFrame/SendStream; the codec state of the built-ins is global, as
 *     in the OEM;
 *   - every channel has `bufSize` stream nodes; Send* waits for a free node,
 *     Polling* waits up to the timeout for an encoded/decoded one, Get*
 *     blocks (IMPBlock BLOCK = 0) or not, Release* returns the node;
 *   - GetStream time-stamps the stream with the wall clock in microseconds.
 * Node buffers are allocated on first use, so an idle channel costs no
 * stream memory. */

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "audio/openimp_audio_codec.h"

#define AUDIO_CHANNELS      6
#define AUDIO_METHODS       11
#define AUDIO_CUSTOM_FIRST  6
#define AUDIO_DEFAULT_NODES 8
#define AUDIO_MAX_NODES     64

enum {
    AUDIO_PT_G711A = 1,
    AUDIO_PT_G711U = 2,
    AUDIO_PT_G726 = 3,
    AUDIO_PT_ADPCM = 5,
};

/* ---- vendor ABI ---- */

typedef struct {
    int bitwidth;
    int soundmode;
    uint32_t *virAddr;
    uint32_t phyAddr;
    int64_t timeStamp;
    int seq;
    int len;
} AudioFrameABI;

typedef struct {
    uint8_t *stream;
    uint32_t phyAddr;
    int len;
    int64_t timeStamp;
    int seq;
} AudioStreamABI;

typedef struct {
    int type;
    int bufSize;
    uint32_t *value;
} AencChnAttrABI;

typedef struct {
    int type;
    int bufSize;
    int mode;
    void *value;
} AdecChnAttrABI;

typedef struct {
    int type;
    int maxFrmLen;
    char name[16];
    int (*openEncoder)(void *attr, void *encoder);
    int (*encoderFrm)(void *encoder, AudioFrameABI *data, unsigned char *outbuf,
                      int *outLen);
    int (*closeEncoder)(void *encoder);
} AencEncoderABI;

typedef struct {
    int type;
    char name[16];
    int (*openDecoder)(void *attr, void *decoder);
    int (*decodeFrm)(void *decoder, unsigned char *inbuf, int inLen,
                     unsigned short *outbuf, int *outLen, int *chns);
    int (*getFrmInfo)(void *decoder, void *info);
    int (*closeDecoder)(void *decoder);
} AdecDecoderABI;

#if UINTPTR_MAX == 0xffffffffu  /* ILP32 targets: the vendor layout */
_Static_assert(sizeof(AudioFrameABI) == 32, "IMPAudioFrame");
_Static_assert(sizeof(AudioStreamABI) == 32, "IMPAudioStream");
_Static_assert(sizeof(AencChnAttrABI) == 12, "IMPAudioEncChnAttr");
_Static_assert(sizeof(AdecChnAttrABI) == 16, "IMPAudioDecChnAttr");
_Static_assert(sizeof(AencEncoderABI) == 36, "IMPAudioEncEncoder");
_Static_assert(sizeof(AdecDecoderABI) == 36, "IMPAudioDecDecoder");
#endif

/* ---- stream node queue ---- */

typedef struct AudioNode {
    struct AudioNode *next;
    uint8_t *data;
    int cap;
    int len;
} AudioNode;

typedef struct {
    int created;
    int closing;
    int type;
    int count;
    AudioNode *nodes;
    AudioNode *free_list;           /* LIFO: reuse warm buffers first */
    AudioNode *head, *tail;         /* filled, FIFO */
    int seq;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} AudioChannel;

static int64_t wall_us(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}

static void deadline_after(struct timespec *ts, uint32_t ms)
{
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += ms / 1000u;
    ts->tv_nsec += (long)(ms % 1000u) * 1000000l;
    if (ts->tv_nsec >= 1000000000l) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000l;
    }
}

static int channel_open(AudioChannel *c, int type, int nodes)
{
    if (nodes <= 0)
        nodes = AUDIO_DEFAULT_NODES;
    if (nodes > AUDIO_MAX_NODES)
        nodes = AUDIO_MAX_NODES;
    c->nodes = calloc((size_t)nodes, sizeof(*c->nodes));
    if (!c->nodes)
        return -1;
    c->count = nodes;
    c->free_list = NULL;
    for (int i = nodes - 1; i >= 0; i--) {
        c->nodes[i].next = c->free_list;
        c->free_list = &c->nodes[i];
    }
    c->head = c->tail = NULL;
    c->type = type;
    c->seq = 0;
    c->closing = 0;
    c->created = 1;
    return 0;
}

static void channel_close(AudioChannel *c)
{
    for (int i = 0; i < c->count; i++)
        free(c->nodes[i].data);
    free(c->nodes);
    c->nodes = NULL;
    c->count = 0;
    c->free_list = c->head = c->tail = NULL;
    c->created = 0;
}

/* Called with c->lock held; waits for a free node like the OEM (which
 * retries every millisecond for as long as it takes). */
static AudioNode *take_free_node(AudioChannel *c)
{
    while (c->created && !c->closing && !c->free_list)
        pthread_cond_wait(&c->cond, &c->lock);
    if (!c->created || c->closing || !c->free_list)
        return NULL;
    AudioNode *n = c->free_list;

    c->free_list = n->next;
    n->next = NULL;
    return n;
}

static int ensure_capacity(AudioNode *n, int cap)
{
    if (n->cap >= cap)
        return 0;
    uint8_t *p = realloc(n->data, (size_t)cap);

    if (!p)
        return -1;
    n->data = p;
    n->cap = cap;
    return 0;
}

static void push_filled(AudioChannel *c, AudioNode *n)
{
    n->next = NULL;
    if (c->tail)
        c->tail->next = n;
    else
        c->head = n;
    c->tail = n;
    pthread_cond_broadcast(&c->cond);
}

static void push_free(AudioChannel *c, AudioNode *n)
{
    n->next = c->free_list;
    c->free_list = n;
    pthread_cond_broadcast(&c->cond);
}

static int poll_filled(AudioChannel *c, uint32_t timeout_ms)
{
    struct timespec deadline;
    int ret = 0;

    deadline_after(&deadline, timeout_ms);
    pthread_mutex_lock(&c->lock);
    while (c->created && !c->closing && !c->head && ret == 0)
        ret = pthread_cond_timedwait(&c->cond, &c->lock, &deadline);
    ret = c->created && c->head ? 0 : -1;
    pthread_mutex_unlock(&c->lock);
    return ret;
}

/* IMPBlock: BLOCK = 0 waits for a stream, NOBLOCK = 1 does not (the OEM
 * then returns 0 with the stream untouched; here it fails instead). */
#define AUDIO_BLOCK 0

static int get_filled(AudioChannel *c, AudioStreamABI *stream, int block)
{
    AudioNode *n;

    pthread_mutex_lock(&c->lock);
    while (block == AUDIO_BLOCK && c->created && !c->closing && !c->head)
        pthread_cond_wait(&c->cond, &c->lock);
    n = c->created ? c->head : NULL;
    if (!n) {
        pthread_mutex_unlock(&c->lock);
        return -1;
    }
    c->head = n->next;
    if (!c->head)
        c->tail = NULL;
    n->next = NULL;
    stream->stream = n->data;
    stream->phyAddr = 0;
    stream->len = n->len;
    stream->timeStamp = wall_us();
    stream->seq = c->seq++;
    pthread_mutex_unlock(&c->lock);
    return 0;
}

static int release_filled(AudioChannel *c, const AudioStreamABI *stream)
{
    int ret = -1;

    pthread_mutex_lock(&c->lock);
    if (c->created) {
        for (int i = 0; i < c->count; i++) {
            AudioNode *n = &c->nodes[i];

            if (n->data && n->data == stream->stream) {
                push_free(c, n);
                ret = 0;
                break;
            }
        }
    }
    pthread_mutex_unlock(&c->lock);
    return ret;
}

static void clear_filled(AudioChannel *c)
{
    pthread_mutex_lock(&c->lock);
    while (c->head) {
        AudioNode *n = c->head;

        c->head = n->next;
        push_free(c, n);
    }
    c->tail = NULL;
    pthread_mutex_unlock(&c->lock);
}

static void destroy_channel(AudioChannel *c)
{
    pthread_mutex_lock(&c->lock);
    c->closing = 1;
    pthread_cond_broadcast(&c->cond);
    channel_close(c);
    pthread_mutex_unlock(&c->lock);
}

/* ---- built-in codecs (global state, as in the OEM) ---- */

static pthread_mutex_t codec_lock = PTHREAD_MUTEX_INITIALIZER;
static OpenIMPG726State g726_enc_state, g726_dec_state;
static int g726_enc_ready, g726_dec_ready;
static OpenIMPAdpcmState adpcm_enc_state, adpcm_dec_state;

static int builtin_type(int type)
{
    return type == AUDIO_PT_G711A || type == AUDIO_PT_G711U ||
           type == AUDIO_PT_G726 || type == AUDIO_PT_ADPCM;
}

static int builtin_encode(int type, const AudioFrameABI *frame, uint8_t *out)
{
    const int16_t *pcm = (const int16_t *)frame->virAddr;
    int samples = frame->len / 2;
    int len;

    switch (type) {
    case AUDIO_PT_G711A:
        return openimp_g711a_encode(out, pcm, samples);
    case AUDIO_PT_G711U:
        return openimp_g711u_encode(out, pcm, samples);
    case AUDIO_PT_G726:
        pthread_mutex_lock(&codec_lock);
        if (!g726_enc_ready) {
            openimp_g726_init(&g726_enc_state);
            g726_enc_ready = 1;
        }
        len = openimp_g726_encode(&g726_enc_state, out, pcm, samples);
        pthread_mutex_unlock(&codec_lock);
        return len;
    case AUDIO_PT_ADPCM:
        pthread_mutex_lock(&codec_lock);
        len = openimp_adpcm_encode(&adpcm_enc_state, out, pcm, samples);
        pthread_mutex_unlock(&codec_lock);
        return len;
    default:
        return -1;
    }
}

static int builtin_decode(int type, const uint8_t *in, int in_len,
                          int16_t *out)
{
    int len;

    switch (type) {
    case AUDIO_PT_G711A:
        return openimp_g711a_decode(out, in, in_len);
    case AUDIO_PT_G711U:
        return openimp_g711u_decode(out, in, in_len);
    case AUDIO_PT_G726:
        pthread_mutex_lock(&codec_lock);
        if (!g726_dec_ready) {
            openimp_g726_init(&g726_dec_state);
            g726_dec_ready = 1;
        }
        len = openimp_g726_decode(&g726_dec_state, out, in, in_len) * 2;
        pthread_mutex_unlock(&codec_lock);
        return len;
    case AUDIO_PT_ADPCM:
        pthread_mutex_lock(&codec_lock);
        len = openimp_adpcm_decode(&adpcm_dec_state, out, in, in_len * 2) * 2;
        pthread_mutex_unlock(&codec_lock);
        return len;
    default:
        return -1;
    }
}

/* ---- AENC ---- */

static pthread_mutex_t aenc_lock = PTHREAD_MUTEX_INITIALIZER;
static AencEncoderABI aenc_methods[AUDIO_METHODS];
static int aenc_method_used[AUDIO_METHODS];
static AudioChannel aenc_channels[AUDIO_CHANNELS] = {
    [0 ... AUDIO_CHANNELS - 1] = {
        .lock = PTHREAD_MUTEX_INITIALIZER,
        .cond = PTHREAD_COND_INITIALIZER,
    },
};

int IMP_AENC_RegisterEncoder(int *handle, AencEncoderABI *encoder)
{
    int slot;

    if (!handle || !encoder)
        return -1;
    pthread_mutex_lock(&aenc_lock);
    for (slot = AUDIO_CUSTOM_FIRST; slot < AUDIO_METHODS; slot++) {
        if (!aenc_method_used[slot])
            break;
    }
    if (slot == AUDIO_METHODS) {
        pthread_mutex_unlock(&aenc_lock);
        return -1;
    }
    aenc_methods[slot] = *encoder;
    aenc_method_used[slot] = 1;
    pthread_mutex_unlock(&aenc_lock);
    *handle = slot;
    return 0;
}

int IMP_AENC_UnRegisterEncoder(int *handle)
{
    if (!handle || *handle < AUDIO_CUSTOM_FIRST ||
        *handle >= AUDIO_CUSTOM_FIRST + 6)
        return -1;
    pthread_mutex_lock(&aenc_lock);
    if (*handle < AUDIO_METHODS)
        aenc_method_used[*handle] = 0;
    pthread_mutex_unlock(&aenc_lock);
    return 0;
}

static int aenc_valid(int chn)
{
    return chn >= 0 && chn < AUDIO_CHANNELS;
}

int IMP_AENC_CreateChn(int chn, AencChnAttrABI *attr)
{
    AudioChannel *c;
    AencEncoderABI method;
    int custom;

    if (!aenc_valid(chn) || !attr || attr->type < 0 ||
        attr->type >= AUDIO_METHODS)
        return -1;
    c = &aenc_channels[chn];
    pthread_mutex_lock(&aenc_lock);
    custom = aenc_method_used[attr->type];
    method = aenc_methods[attr->type];
    pthread_mutex_unlock(&aenc_lock);
    if (!custom && !builtin_type(attr->type))
        return -1;
    pthread_mutex_lock(&c->lock);
    if (c->created || channel_open(c, attr->type, attr->bufSize) != 0) {
        pthread_mutex_unlock(&c->lock);
        return -1;
    }
    pthread_mutex_unlock(&c->lock);
    if (custom && method.openEncoder)
        (void)method.openEncoder(NULL, NULL);
    return 0;
}

int IMP_AENC_DestroyChn(int chn)
{
    AudioChannel *c;
    AencEncoderABI method;
    int custom = 0, created;

    if (!aenc_valid(chn))
        return -1;
    c = &aenc_channels[chn];
    pthread_mutex_lock(&c->lock);
    created = c->created;
    if (created && c->type >= AUDIO_CUSTOM_FIRST) {
        pthread_mutex_lock(&aenc_lock);
        custom = aenc_method_used[c->type];
        method = aenc_methods[c->type];
        pthread_mutex_unlock(&aenc_lock);
    }
    pthread_mutex_unlock(&c->lock);
    if (custom && method.closeEncoder)
        (void)method.closeEncoder(NULL);
    if (created)
        destroy_channel(c);
    return 0;
}

int IMP_AENC_SendFrame(int chn, AudioFrameABI *frame)
{
    AudioChannel *c;
    AudioNode *n;
    AencEncoderABI method;
    int custom, type, cap, out_len;

    if (!aenc_valid(chn) || !frame || !frame->virAddr || frame->len < 0)
        return -1;
    c = &aenc_channels[chn];
    pthread_mutex_lock(&c->lock);
    if (!c->created) {
        pthread_mutex_unlock(&c->lock);
        return -1;
    }
    type = c->type;
    n = take_free_node(c);
    pthread_mutex_unlock(&c->lock);
    if (!n)
        return -1;

    pthread_mutex_lock(&aenc_lock);
    custom = aenc_method_used[type];
    method = aenc_methods[type];
    pthread_mutex_unlock(&aenc_lock);

    cap = frame->len > 1024 ? frame->len : 1024;
    if (custom && method.maxFrmLen > cap)
        cap = method.maxFrmLen;
    out_len = -1;
    if (ensure_capacity(n, cap) == 0) {
        if (custom) {
            out_len = cap;
            if (!method.encoderFrm ||
                method.encoderFrm(NULL, frame, n->data, &out_len) != 0)
                out_len = -1;
        } else {
            out_len = builtin_encode(type, frame, n->data);
        }
    }

    pthread_mutex_lock(&c->lock);
    if (!c->created) {
        pthread_mutex_unlock(&c->lock);
        return -1;
    }
    if (out_len < 0 || out_len > n->cap) {
        push_free(c, n);
        pthread_mutex_unlock(&c->lock);
        return -1;
    }
    n->len = out_len;
    push_filled(c, n);
    pthread_mutex_unlock(&c->lock);
    return 0;
}

int IMP_AENC_PollingStream(int chn, unsigned int timeout_ms)
{
    if (!aenc_valid(chn))
        return -1;
    return poll_filled(&aenc_channels[chn], timeout_ms);
}

int IMP_AENC_GetStream(int chn, AudioStreamABI *stream, int block)
{
    if (!aenc_valid(chn) || !stream)
        return -1;
    return get_filled(&aenc_channels[chn], stream, block);
}

int IMP_AENC_ReleaseStream(int chn, AudioStreamABI *stream)
{
    if (!aenc_valid(chn) || !stream)
        return -1;
    return release_filled(&aenc_channels[chn], stream);
}

/* ---- ADEC ---- */

static pthread_mutex_t adec_lock = PTHREAD_MUTEX_INITIALIZER;
static AdecDecoderABI adec_methods[AUDIO_METHODS];
static int adec_method_used[AUDIO_METHODS];
static AudioChannel adec_channels[AUDIO_CHANNELS] = {
    [0 ... AUDIO_CHANNELS - 1] = {
        .lock = PTHREAD_MUTEX_INITIALIZER,
        .cond = PTHREAD_COND_INITIALIZER,
    },
};

int IMP_ADEC_RegisterDecoder(int *handle, AdecDecoderABI *decoder)
{
    int slot;

    if (!handle || !decoder)
        return -1;
    pthread_mutex_lock(&adec_lock);
    for (slot = AUDIO_CUSTOM_FIRST; slot < AUDIO_METHODS; slot++) {
        if (!adec_method_used[slot])
            break;
    }
    if (slot == AUDIO_METHODS) {
        pthread_mutex_unlock(&adec_lock);
        return -1;
    }
    adec_methods[slot] = *decoder;
    adec_method_used[slot] = 1;
    pthread_mutex_unlock(&adec_lock);
    *handle = slot;
    return 0;
}

int IMP_ADEC_UnRegisterDecoder(int *handle)
{
    if (!handle || *handle < AUDIO_CUSTOM_FIRST ||
        *handle >= AUDIO_CUSTOM_FIRST + 6)
        return -1;
    pthread_mutex_lock(&adec_lock);
    if (*handle < AUDIO_METHODS)
        adec_method_used[*handle] = 0;
    pthread_mutex_unlock(&adec_lock);
    return 0;
}

static int adec_valid(int chn)
{
    return chn >= 0 && chn < AUDIO_CHANNELS;
}

int IMP_ADEC_CreateChn(int chn, AdecChnAttrABI *attr)
{
    AudioChannel *c;
    AdecDecoderABI method;
    int custom;

    if (!adec_valid(chn) || !attr || attr->type < 0 ||
        attr->type >= AUDIO_METHODS)
        return -1;
    c = &adec_channels[chn];
    pthread_mutex_lock(&adec_lock);
    custom = adec_method_used[attr->type];
    method = adec_methods[attr->type];
    pthread_mutex_unlock(&adec_lock);
    if (!custom && !builtin_type(attr->type))
        return -1;
    pthread_mutex_lock(&c->lock);
    if (c->created || channel_open(c, attr->type, attr->bufSize) != 0) {
        pthread_mutex_unlock(&c->lock);
        return -1;
    }
    pthread_mutex_unlock(&c->lock);
    if (custom && method.openDecoder)
        (void)method.openDecoder(NULL, NULL);
    return 0;
}

int IMP_ADEC_DestroyChn(int chn)
{
    AudioChannel *c;
    AdecDecoderABI method;
    int custom = 0, created;

    if (!adec_valid(chn))
        return -1;
    c = &adec_channels[chn];
    pthread_mutex_lock(&c->lock);
    created = c->created;
    if (created && c->type >= AUDIO_CUSTOM_FIRST) {
        pthread_mutex_lock(&adec_lock);
        custom = adec_method_used[c->type];
        method = adec_methods[c->type];
        pthread_mutex_unlock(&adec_lock);
    }
    pthread_mutex_unlock(&c->lock);
    if (custom && method.closeDecoder)
        (void)method.closeDecoder(NULL);
    if (created)
        destroy_channel(c);
    return 0;
}

int IMP_ADEC_SendStream(int chn, AudioStreamABI *stream, int block)
{
    AudioChannel *c;
    AudioNode *n;
    AdecDecoderABI method;
    int custom, type, cap, out_len, chns = 1;

    (void)block;                    /* the OEM waits for a node either way */
    if (!adec_valid(chn) || !stream || !stream->stream || stream->len < 0)
        return -1;
    c = &adec_channels[chn];
    pthread_mutex_lock(&c->lock);
    if (!c->created) {
        pthread_mutex_unlock(&c->lock);
        return -1;
    }
    type = c->type;
    n = take_free_node(c);
    pthread_mutex_unlock(&c->lock);
    if (!n)
        return -1;

    pthread_mutex_lock(&adec_lock);
    custom = adec_method_used[type];
    method = adec_methods[type];
    pthread_mutex_unlock(&adec_lock);

    /* built-ins expand at most 4x (G.726/ADPCM); custom decoders get room
     * for a stereo 2048-sample frame */
    cap = custom ? 8192 : 4 * stream->len + 4;
    if (cap < 1024)
        cap = 1024;
    out_len = -1;
    if (ensure_capacity(n, cap) == 0) {
        if (custom) {
            out_len = cap;
            if (!method.decodeFrm ||
                method.decodeFrm(NULL, stream->stream, stream->len,
                                 (unsigned short *)n->data, &out_len,
                                 &chns) != 0)
                out_len = -1;
        } else {
            out_len = builtin_decode(type, stream->stream, stream->len,
                                     (int16_t *)n->data);
        }
    }

    pthread_mutex_lock(&c->lock);
    if (!c->created) {
        pthread_mutex_unlock(&c->lock);
        return -1;
    }
    if (out_len < 0 || out_len > n->cap) {
        push_free(c, n);
        pthread_mutex_unlock(&c->lock);
        return -1;
    }
    n->len = out_len;
    push_filled(c, n);
    pthread_mutex_unlock(&c->lock);
    return 0;
}

int IMP_ADEC_PollingStream(int chn, unsigned int timeout_ms)
{
    if (!adec_valid(chn))
        return -1;
    return poll_filled(&adec_channels[chn], timeout_ms);
}

int IMP_ADEC_GetStream(int chn, AudioStreamABI *stream, int block)
{
    if (!adec_valid(chn) || !stream)
        return -1;
    return get_filled(&adec_channels[chn], stream, block);
}

int IMP_ADEC_ReleaseStream(int chn, AudioStreamABI *stream)
{
    if (!adec_valid(chn) || !stream)
        return -1;
    return release_filled(&adec_channels[chn], stream);
}

int IMP_ADEC_ClearChnBuf(int chn)
{
    if (!adec_valid(chn) || !adec_channels[chn].created)
        return -1;
    clear_filled(&adec_channels[chn]);
    return 0;
}
