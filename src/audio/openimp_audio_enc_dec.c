/* Audio encoder (AENC) and decoder (ADEC) channels, shared by T31 and T23.
 *
 * Mirrors libimp 1.1.6 aenc.c/adec.c (the T23 SDK 1.3.0 objects behave the
 * same, with the two platform differences under OPENIMP_ACODEC_* below):
 *   - six channels per direction; a channel owns bufSize nodes that cycle
 *     between a free list and a ready list;
 *   - the work is synchronous: SendFrame/SendStream run the codec in the
 *     caller's thread (prudynt keeps its AAC/Opus codec state in thread-local
 *     storage and relies on this) and queue the result, GetStream dequeues it
 *     and ReleaseStream returns the node;
 *   - a node holds one AI frame (AENC: numPerFrm * 2 bytes of the AI
 *     device) or one AO frame (ADEC: numPerFrm * 2 bytes of the AO device);
 *     800 bytes if the device attributes are not set yet;
 *   - method slots 0..5 are the PT_* payload types (G.711A, G.711U, G.726 and
 *     IMA ADPCM are built in, PT_PCM and PT_AEC are not), slots
 *     6..10 take user codecs; IMP_A*_Register* returns the slot as handle and
 *     the caller passes it as IMPAudio*ChnAttr.type;
 *   - user callbacks receive NULL as their attribute/instance pointers, like
 *     the vendor, and the codec's encode/decode return value is ignored.
 *
 * Differences, all on paths where libimp misbehaves: built-in codec state is
 * per channel instead of one static state per codec; GetStream(NOBLOCK)
 * with nothing queued returns -1 instead of 0 with an untouched stream;
 * negative channel numbers are rejected; the built-in decoders never write
 * past the node and user codecs get at least 8 KiB of output room.
 */

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

#include <imp/imp_audio.h>

#include "audio/openimp_audio_codec.h"

#define ACODEC_CHANNELS        6
#define ACODEC_BUILTIN_SLOTS   6
#define ACODEC_METHOD_SLOTS    11
#define ACODEC_DEFAULT_BYTES   800
#define ACODEC_USER_MIN_BYTES  8192

/* Platform differences of the vendor objects:
 * - PT_G726 rate: T31 libimp 1.1.6 opens g726_init(16000), the T23 OEM
 *   libimp g726_init(32000) (both in _aenc_pcm2g726/_adec_g726_2pcm);
 * - stream time stamp: the T23 OEM GetStream stamps the stream with
 *   gettimeofday() in microseconds; T31 libimp leaves it untouched, here it
 *   carries the frame's (AENC) or input stream's (ADEC) time stamp. */
#if defined(PLATFORM_T23)
#define OPENIMP_ACODEC_G726_32K       1
#define OPENIMP_ACODEC_WALLCLOCK_TS   1
#else
#define OPENIMP_ACODEC_G726_32K       0
#define OPENIMP_ACODEC_WALLCLOCK_TS   0
#endif

#if UINTPTR_MAX == 0xffffffffu /* MIPS32 target; host tests are 64-bit */
_Static_assert(sizeof(IMPAudioEncEncoder) == 36,
               "IMPAudioEncEncoder vendor ABI mismatch");
_Static_assert(sizeof(IMPAudioDecDecoder) == 36,
               "IMPAudioDecDecoder vendor ABI mismatch");
_Static_assert(sizeof(IMPAudioDecChnAttr) == 16,
               "IMPAudioDecChnAttr vendor ABI mismatch");
_Static_assert(sizeof(IMPAudioEncChnAttr) == 12,
               "IMPAudioEncChnAttr vendor ABI mismatch");
_Static_assert(offsetof(IMPAudioStream, timeStamp) == 16 &&
                   sizeof(IMPAudioStream) == 32,
               "IMPAudioStream vendor ABI mismatch");
#endif

typedef struct AcodecNode {
    struct AcodecNode *next;
    int len;
    int64_t timestamp;
    int seq;
    uint8_t *data;
} AcodecNode;

typedef struct {
    AcodecNode *head;
    AcodecNode *tail;
    int count;
} AcodecList;

typedef struct {
    int enabled;
    int type;                   /* method slot */
    int depth;
    int capacity;               /* bytes per node */
    int reported;               /* *outLen preset handed to the codec */
    AcodecNode *nodes;
    uint8_t *storage;
    AcodecList free_list;
    AcodecList ready_list;
    int busy;                   /* codec calls running outside lock */
    pthread_mutex_t lock;
    pthread_cond_t cond;
    union {
        OpenIMPAdpcmState adpcm;
        OpenIMPG726State g726;
    } state;
} AcodecChannel;

typedef struct {
    int registered;
    IMPAudioEncEncoder encoder;
} AencMethod;

typedef struct {
    int registered;
    IMPAudioDecDecoder decoder;
} AdecMethod;

static pthread_mutex_t acodec_lock = PTHREAD_MUTEX_INITIALIZER;
static AencMethod aenc_methods[ACODEC_METHOD_SLOTS];
static AdecMethod adec_methods[ACODEC_METHOD_SLOTS];
static AcodecChannel aenc_channels[ACODEC_CHANNELS];
static AcodecChannel adec_channels[ACODEC_CHANNELS];

static pthread_once_t acodec_once = PTHREAD_ONCE_INIT;

static void channel_init_sync(AcodecChannel *ch)
{
    pthread_condattr_t attr;

    pthread_mutex_init(&ch->lock, NULL);
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&ch->cond, &attr);
    pthread_condattr_destroy(&attr);
}

static void acodec_init_once(void)
{
    int i;

    for (i = 0; i < ACODEC_CHANNELS; i++) {
        channel_init_sync(&aenc_channels[i]);
        channel_init_sync(&adec_channels[i]);
    }
}

static int acodec_builtin(int type)
{
    return type == PT_G711A || type == PT_G711U || type == PT_G726 ||
           type == PT_ADPCM;
}

static void list_push(AcodecList *list, AcodecNode *node)
{
    node->next = NULL;
    if (list->tail)
        list->tail->next = node;
    else
        list->head = node;
    list->tail = node;
    list->count++;
}

static AcodecNode *list_pop(AcodecList *list)
{
    AcodecNode *node = list->head;

    if (!node)
        return NULL;
    list->head = node->next;
    if (!list->head)
        list->tail = NULL;
    list->count--;
    node->next = NULL;
    return node;
}

static void channel_free_buffers(AcodecChannel *ch)
{
    free(ch->nodes);
    free(ch->storage);
    ch->nodes = NULL;
    ch->storage = NULL;
    memset(&ch->free_list, 0, sizeof(ch->free_list));
    memset(&ch->ready_list, 0, sizeof(ch->ready_list));
}

static int channel_alloc_buffers(AcodecChannel *ch, int depth, int capacity)
{
    int i;

    if (depth <= 0 || capacity <= 0)
        return -1;
    ch->nodes = calloc((size_t)depth, sizeof(*ch->nodes));
    ch->storage = calloc((size_t)depth, (size_t)capacity);
    if (!ch->nodes || !ch->storage) {
        channel_free_buffers(ch);
        return -1;
    }
    ch->depth = depth;
    ch->capacity = capacity;
    for (i = 0; i < depth; i++) {
        ch->nodes[i].data = ch->storage + (size_t)i * (size_t)capacity;
        list_push(&ch->free_list, &ch->nodes[i]);
    }
    return 0;
}

static void acodec_g726_init(OpenIMPG726State *state)
{
#if OPENIMP_ACODEC_G726_32K
    openimp_g726_32_init(state);
#else
    openimp_g726_16_init(state);
#endif
}

/* PCM samples per G.726 code byte: 4 at 16 kbit/s, 2 at 32 kbit/s */
#define ACODEC_G726_SAMPLES_PER_BYTE (OPENIMP_ACODEC_G726_32K ? 2 : 4)

static void deadline_after_ms(struct timespec *deadline, unsigned int ms)
{
    clock_gettime(CLOCK_MONOTONIC, deadline);
    deadline->tv_sec += ms / 1000u;
    deadline->tv_nsec += (long)(ms % 1000u) * 1000000L;
    if (deadline->tv_nsec >= 1000000000L) {
        deadline->tv_sec++;
        deadline->tv_nsec -= 1000000000L;
    }
}

/* A node for new output; blocks like libimp (1 ms polling there) until the
 * application releases one. The caller runs the codec on it outside the
 * lock and then hands it to channel_put_ready(). */
static AcodecNode *channel_get_free(AcodecChannel *ch)
{
    AcodecNode *node = NULL;

    pthread_mutex_lock(&ch->lock);
    while (ch->enabled && !(node = list_pop(&ch->free_list)))
        pthread_cond_wait(&ch->cond, &ch->lock);
    if (!ch->enabled) {
        pthread_mutex_unlock(&ch->lock);
        return NULL;
    }
    ch->busy++;
    pthread_mutex_unlock(&ch->lock);
    return node;
}

static void channel_put_ready(AcodecChannel *ch, AcodecNode *node)
{
    pthread_mutex_lock(&ch->lock);
    if (ch->enabled)
        list_push(&ch->ready_list, node);
    ch->busy--;
    pthread_cond_broadcast(&ch->cond);
    pthread_mutex_unlock(&ch->lock);
}

static int channel_polling(AcodecChannel *ch, unsigned int timeout_ms)
{
    struct timespec deadline;
    int ret = 0;

    deadline_after_ms(&deadline, timeout_ms);
    pthread_mutex_lock(&ch->lock);
    while (ch->enabled && ch->ready_list.count == 0) {
        if (pthread_cond_timedwait(&ch->cond, &ch->lock, &deadline) != 0)
            break;
    }
    if (!ch->enabled || ch->ready_list.count == 0)
        ret = -1;
    pthread_mutex_unlock(&ch->lock);
    return ret;
}

static int channel_get(AcodecChannel *ch, IMPAudioStream *stream,
                       IMPBlock block)
{
    AcodecNode *node = NULL;

    pthread_mutex_lock(&ch->lock);
    while (ch->enabled && !(node = list_pop(&ch->ready_list))) {
        if (block != BLOCK) {
            pthread_mutex_unlock(&ch->lock);
            return -1;
        }
        pthread_cond_wait(&ch->cond, &ch->lock);
    }
    if (!ch->enabled) {
        pthread_mutex_unlock(&ch->lock);
        return -1;
    }
    stream->stream = node->data;
    stream->phyAddr = 0;
    stream->len = node->len;
#if OPENIMP_ACODEC_WALLCLOCK_TS
    {
        struct timeval tv;

        gettimeofday(&tv, NULL);
        stream->timeStamp = (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
    }
#else
    stream->timeStamp = node->timestamp;
#endif
    stream->seq = node->seq;
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

static int channel_release(AcodecChannel *ch, IMPAudioStream *stream)
{
    uintptr_t base;
    uintptr_t address;
    size_t index;

    if (!stream || !stream->stream)
        return -1;
    pthread_mutex_lock(&ch->lock);
    if (!ch->enabled || !ch->storage) {
        pthread_mutex_unlock(&ch->lock);
        return -1;
    }
    base = (uintptr_t)ch->storage;
    address = (uintptr_t)stream->stream;
    if (address < base ||
        address >= base + (size_t)ch->depth * (size_t)ch->capacity ||
        (address - base) % (size_t)ch->capacity != 0) {
        pthread_mutex_unlock(&ch->lock);
        return -1;
    }
    index = (address - base) / (size_t)ch->capacity;
    list_push(&ch->free_list, &ch->nodes[index]);
    pthread_cond_broadcast(&ch->cond);
    pthread_mutex_unlock(&ch->lock);
    return 0;
}

static void channel_destroy(AcodecChannel *ch)
{
    pthread_mutex_lock(&ch->lock);
    ch->enabled = 0;
    pthread_cond_broadcast(&ch->cond);
    /* blocked callers drop out on !enabled; a codec call in flight still
     * writes into its node */
    while (ch->busy > 0)
        pthread_cond_wait(&ch->cond, &ch->lock);
    channel_free_buffers(ch);
    ch->depth = 0;
    ch->capacity = 0;
    pthread_mutex_unlock(&ch->lock);
}

static int frame_bytes(int (*get_attr)(int, IMPAudioIOAttr *))
{
    IMPAudioIOAttr attr;

    memset(&attr, 0, sizeof(attr));
    if (get_attr(0, &attr) != 0 || attr.numPerFrm <= 0)
        return ACODEC_DEFAULT_BYTES;
    return attr.numPerFrm * 2;
}

/* ------------------------------------------------------------------ AENC */

int IMP_AENC_RegisterEncoder(int *handle, IMPAudioEncEncoder *encoder)
{
    int slot;

    if (!handle || !encoder)
        return -1;
    pthread_mutex_lock(&acodec_lock);
    for (slot = ACODEC_BUILTIN_SLOTS; slot < ACODEC_METHOD_SLOTS; slot++) {
        if (!aenc_methods[slot].registered) {
            aenc_methods[slot].encoder = *encoder;
            aenc_methods[slot].registered = 1;
            *handle = slot;
            pthread_mutex_unlock(&acodec_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&acodec_lock);
    return -1;
}

int IMP_AENC_UnRegisterEncoder(int *handle)
{
    if (!handle || *handle < ACODEC_BUILTIN_SLOTS ||
        *handle >= ACODEC_METHOD_SLOTS)
        return -1;
    pthread_mutex_lock(&acodec_lock);
    aenc_methods[*handle].registered = 0;
    pthread_mutex_unlock(&acodec_lock);
    return 0;
}

static int aenc_valid(int channel)
{
    return channel >= 0 && channel < ACODEC_CHANNELS;
}

int IMP_AENC_CreateChn(int aeChn, IMPAudioEncChnAttr *attr)
{
    AcodecChannel *ch;
    IMPAudioEncEncoder encoder;
    int type;
    int reported;
    int capacity;

    if (!aenc_valid(aeChn) || !attr)
        return -1;
    type = (int)attr->type;
    if (type < 0 || type >= ACODEC_METHOD_SLOTS)
        return -1;
    pthread_once(&acodec_once, acodec_init_once);
    pthread_mutex_lock(&acodec_lock);
    if (!acodec_builtin(type) && !aenc_methods[type].registered) {
        pthread_mutex_unlock(&acodec_lock);
        return -1;
    }
    encoder = aenc_methods[type].encoder;
    ch = &aenc_channels[aeChn];
    if (ch->enabled) {
        pthread_mutex_unlock(&acodec_lock);
        return -1;
    }
    reported = frame_bytes(IMP_AI_GetPubAttr);
    capacity = reported;
    if (!acodec_builtin(type)) {
        if (encoder.maxFrmLen > capacity)
            capacity = encoder.maxFrmLen;
        if (capacity < ACODEC_USER_MIN_BYTES)
            capacity = ACODEC_USER_MIN_BYTES;
        reported = capacity;
    }
    if (channel_alloc_buffers(ch, attr->bufSize, capacity) != 0) {
        pthread_mutex_unlock(&acodec_lock);
        return -1;
    }
    ch->type = type;
    ch->reported = reported;
    if (type == PT_ADPCM)
        openimp_adpcm_init(&ch->state.adpcm);
    else if (type == PT_G726)
        acodec_g726_init(&ch->state.g726);
    else if (encoder.openEncoder)
        (void)encoder.openEncoder(NULL, NULL);
    ch->enabled = 1;
    pthread_mutex_unlock(&acodec_lock);
    return 0;
}

int IMP_AENC_DestroyChn(int aeChn)
{
    AcodecChannel *ch;
    int (*close_encoder)(void *) = NULL;

    if (!aenc_valid(aeChn))
        return -1;
    ch = &aenc_channels[aeChn];
    pthread_mutex_lock(&acodec_lock);
    if (!ch->enabled) {
        pthread_mutex_unlock(&acodec_lock);
        return 0;
    }
    if (!acodec_builtin(ch->type))
        close_encoder = aenc_methods[ch->type].encoder.closeEncoder;
    channel_destroy(ch);
    pthread_mutex_unlock(&acodec_lock);
    if (close_encoder)
        (void)close_encoder(NULL);
    return 0;
}

static int aenc_builtin_encode(AcodecChannel *ch, IMPAudioFrame *frame,
                               uint8_t *out, int capacity)
{
    const int16_t *pcm = (const int16_t *)(const void *)frame->virAddr;
    int samples = frame->len / 2;

    if (!pcm || samples <= 0)
        return 0;
    switch (ch->type) {
    case PT_G711A:
        if (samples > capacity)
            samples = capacity;
        return openimp_g711a_encode(out, pcm, samples);
    case PT_G711U:
        if (samples > capacity)
            samples = capacity;
        return openimp_g711u_encode(out, pcm, samples);
    case PT_ADPCM:
        if (samples > capacity * 2)
            samples = capacity * 2;
        return openimp_adpcm_encode(&ch->state.adpcm, out, pcm, samples);
    case PT_G726:
        if (samples > capacity * ACODEC_G726_SAMPLES_PER_BYTE)
            samples = capacity * ACODEC_G726_SAMPLES_PER_BYTE;
        return openimp_g726_encode(&ch->state.g726, out, pcm, samples);
    default:
        return 0;
    }
}

int IMP_AENC_SendFrame(int aeChn, IMPAudioFrame *frm)
{
    AcodecChannel *ch;
    AcodecNode *node;
    int (*encode)(void *, IMPAudioFrame *, unsigned char *, int *) = NULL;
    int len;

    if (!aenc_valid(aeChn) || !frm)
        return -1;
    ch = &aenc_channels[aeChn];
    if (!ch->enabled)
        return -1;
    if (!acodec_builtin(ch->type)) {
        pthread_mutex_lock(&acodec_lock);
        encode = aenc_methods[ch->type].encoder.encoderFrm;
        pthread_mutex_unlock(&acodec_lock);
    }
    node = channel_get_free(ch);
    if (!node)
        return -1;

    if (acodec_builtin(ch->type)) {
        len = aenc_builtin_encode(ch, frm, node->data, ch->capacity);
    } else {
        len = ch->reported;
        if (encode)
            (void)encode(NULL, frm, node->data, &len);
        if (len < 0)
            len = 0;
        else if (len > ch->capacity)
            len = ch->capacity;
    }
    node->len = len;
    node->timestamp = frm->timeStamp;
    node->seq = frm->seq;
    channel_put_ready(ch, node);
    return 0;
}

int IMP_AENC_PollingStream(int AeChn, unsigned int timeout_ms)
{
    if (!aenc_valid(AeChn) || !aenc_channels[AeChn].enabled)
        return -1;
    return channel_polling(&aenc_channels[AeChn], timeout_ms);
}

int IMP_AENC_GetStream(int aeChn, IMPAudioStream *stream, IMPBlock block)
{
    if (!aenc_valid(aeChn) || !stream || !aenc_channels[aeChn].enabled)
        return -1;
    return channel_get(&aenc_channels[aeChn], stream, block);
}

int IMP_AENC_ReleaseStream(int aeChn, IMPAudioStream *stream)
{
    if (!aenc_valid(aeChn) || !aenc_channels[aeChn].enabled)
        return -1;
    return channel_release(&aenc_channels[aeChn], stream);
}

/* ------------------------------------------------------------------ ADEC */

int IMP_ADEC_RegisterDecoder(int *handle, IMPAudioDecDecoder *decoder)
{
    int slot;

    if (!handle || !decoder)
        return -1;
    pthread_mutex_lock(&acodec_lock);
    for (slot = ACODEC_BUILTIN_SLOTS; slot < ACODEC_METHOD_SLOTS; slot++) {
        if (!adec_methods[slot].registered) {
            adec_methods[slot].decoder = *decoder;
            adec_methods[slot].registered = 1;
            *handle = slot;
            pthread_mutex_unlock(&acodec_lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&acodec_lock);
    return -1;
}

int IMP_ADEC_UnRegisterDecoder(int *handle)
{
    if (!handle || *handle < ACODEC_BUILTIN_SLOTS ||
        *handle >= ACODEC_METHOD_SLOTS)
        return -1;
    pthread_mutex_lock(&acodec_lock);
    adec_methods[*handle].registered = 0;
    pthread_mutex_unlock(&acodec_lock);
    return 0;
}

static int adec_valid(int channel)
{
    return channel >= 0 && channel < ACODEC_CHANNELS;
}

int IMP_ADEC_CreateChn(int adChn, IMPAudioDecChnAttr *attr)
{
    AcodecChannel *ch;
    IMPAudioDecDecoder decoder;
    int type;
    int reported;
    int capacity;

    if (!adec_valid(adChn) || !attr)
        return -1;
    type = (int)attr->type;
    if (type < 0 || type >= ACODEC_METHOD_SLOTS)
        return -1;
    pthread_once(&acodec_once, acodec_init_once);
    pthread_mutex_lock(&acodec_lock);
    if (!acodec_builtin(type) && !adec_methods[type].registered) {
        pthread_mutex_unlock(&acodec_lock);
        return -1;
    }
    decoder = adec_methods[type].decoder;
    ch = &adec_channels[adChn];
    if (ch->enabled) {
        pthread_mutex_unlock(&acodec_lock);
        return -1;
    }
    reported = frame_bytes(IMP_AO_GetPubAttr);
    capacity = reported;
    if (!acodec_builtin(type) && capacity < ACODEC_USER_MIN_BYTES) {
        capacity = ACODEC_USER_MIN_BYTES;
        reported = capacity;
    }
    if (channel_alloc_buffers(ch, attr->bufSize, capacity) != 0) {
        pthread_mutex_unlock(&acodec_lock);
        return -1;
    }
    ch->type = type;
    ch->reported = reported;
    if (type == PT_ADPCM)
        openimp_adpcm_init(&ch->state.adpcm);
    else if (type == PT_G726)
        acodec_g726_init(&ch->state.g726);
    else if (decoder.openDecoder)
        (void)decoder.openDecoder(NULL, NULL);
    ch->enabled = 1;
    pthread_mutex_unlock(&acodec_lock);
    return 0;
}

int IMP_ADEC_DestroyChn(int adChn)
{
    AcodecChannel *ch;
    int (*close_decoder)(void *) = NULL;

    if (!adec_valid(adChn))
        return -1;
    ch = &adec_channels[adChn];
    pthread_mutex_lock(&acodec_lock);
    if (!ch->enabled) {
        pthread_mutex_unlock(&acodec_lock);
        return 0;
    }
    if (!acodec_builtin(ch->type))
        close_decoder = adec_methods[ch->type].decoder.closeDecoder;
    channel_destroy(ch);
    pthread_mutex_unlock(&acodec_lock);
    if (close_decoder)
        (void)close_decoder(NULL);
    return 0;
}

static int adec_builtin_decode(AcodecChannel *ch, const uint8_t *in, int len,
                               uint8_t *out, int capacity)
{
    int16_t *pcm = (int16_t *)(void *)out;

    if (!in || len <= 0)
        return 0;
    switch (ch->type) {
    case PT_G711A:
        if (len > capacity / 2)
            len = capacity / 2;
        return openimp_g711a_decode(pcm, in, len);
    case PT_G711U:
        if (len > capacity / 2)
            len = capacity / 2;
        return openimp_g711u_decode(pcm, in, len);
    case PT_ADPCM:
        if (len > capacity / 4)
            len = capacity / 4;
        return openimp_adpcm_decode(&ch->state.adpcm, pcm, in, len);
    case PT_G726:
        if (len > capacity / (2 * ACODEC_G726_SAMPLES_PER_BYTE))
            len = capacity / (2 * ACODEC_G726_SAMPLES_PER_BYTE);
        return openimp_g726_decode(&ch->state.g726, pcm, in, len) * 2;
    default:
        return 0;
    }
}

int IMP_ADEC_SendStream(int adChn, IMPAudioStream *stream, IMPBlock block)
{
    AcodecChannel *ch;
    AcodecNode *node;
    int (*decode)(void *, unsigned char *, int, unsigned short *, int *,
                  int *) = NULL;
    int len;

    (void)block; /* libimp always waits for a free node */
    if (!adec_valid(adChn) || !stream)
        return -1;
    ch = &adec_channels[adChn];
    if (!ch->enabled)
        return -1;
    if (!acodec_builtin(ch->type)) {
        pthread_mutex_lock(&acodec_lock);
        decode = adec_methods[ch->type].decoder.decodeFrm;
        pthread_mutex_unlock(&acodec_lock);
    }
    node = channel_get_free(ch);
    if (!node)
        return -1;

    if (acodec_builtin(ch->type)) {
        len = adec_builtin_decode(ch, stream->stream, stream->len, node->data,
                                  ch->capacity);
    } else {
        len = ch->reported;
        if (decode)
            (void)decode(NULL, stream->stream, stream->len,
                         (unsigned short *)(void *)node->data, &len, NULL);
        if (len < 0)
            len = 0;
        else if (len > ch->capacity)
            len = ch->capacity;
    }
    node->len = len;
    node->timestamp = stream->timeStamp;
    node->seq = stream->seq;
    channel_put_ready(ch, node);
    return 0;
}

int IMP_ADEC_PollingStream(int AdChn, unsigned int timeout_ms)
{
    if (!adec_valid(AdChn) || !adec_channels[AdChn].enabled)
        return -1;
    return channel_polling(&adec_channels[AdChn], timeout_ms);
}

int IMP_ADEC_GetStream(int adChn, IMPAudioStream *stream, IMPBlock block)
{
    if (!adec_valid(adChn) || !stream || !adec_channels[adChn].enabled)
        return -1;
    return channel_get(&adec_channels[adChn], stream, block);
}

int IMP_ADEC_ReleaseStream(int adChn, IMPAudioStream *stream)
{
    if (!adec_valid(adChn) || !adec_channels[adChn].enabled)
        return -1;
    return channel_release(&adec_channels[adChn], stream);
}

int IMP_ADEC_ClearChnBuf(int adChn)
{
    AcodecChannel *ch;
    AcodecNode *node;

    if (!adec_valid(adChn) || !adec_channels[adChn].enabled)
        return -1;
    ch = &adec_channels[adChn];
    pthread_mutex_lock(&ch->lock);
    while ((node = list_pop(&ch->ready_list)))
        list_push(&ch->free_list, node);
    pthread_cond_broadcast(&ch->cond);
    pthread_mutex_unlock(&ch->lock);
    return 0;
}
