/* T31 digital microphone array (IMP_DMIC_*).
 *
 * Follows libimp 1.1.6 dmic.c. The DMIC shares the oss2 /dev/dsp driver
 * (ingenic-sdk common/audio/t31/oss2) with the analog microphone, through a
 * second descriptor:
 *   - SNDCTL_DSP_SPEED / SNDCTL_DSP_CHANNELS (= number of microphones) /
 *     SNDCTL_DSP_SETFMT, then SNDCTL_EXT_ENABLE_DMIC_STREAM;
 *   - capture with DMIC_GET_AI_STREAM (0x300), struct {data, aec, size}: one
 *     IMP frame is numPerFrm samples of every microphone, interleaved; the
 *     driver fills "aec" with the playback reference (size / channels bytes)
 *     after SNDCTL_EXT_ENABLE_AEC;
 *   - gain through DMIC_SET_DMIC_GAIN (0x200, value 0..31).
 * The driver only builds the DMIC path with CONFIG_JZ_TS_DMIC=y. Without it
 * SNDCTL_EXT_ENABLE_DMIC_STREAM is "not supported" and IMP_DMIC_Enable fails
 * with -1, which is what libimp does on such a kernel too.
 *
 * As in libimp a record thread fills a ring of usrFrmDepth frames and drops
 * the oldest frame when the application falls behind. DMIC AEC runs
 * libaudioProcess' audio_process_aec_* on the microphone selected by
 * IMP_DMIC_SetUserInfo, with the driver's playback reference as far end, and
 * returns it as IMPDmicChnFrame.aecFrame. Volume is applied in software
 * (0.5 dB steps, 60 = unity), the same curve as the OpenIMP AI path.
 *
 * Differences from libimp: only device 0 / channel 0 exist (libimp accepts
 * channel 1 but stores it past its device table); PollingFrame honours its
 * timeout (libimp always waits up to 5 s and returns 0); GetFrame(NOBLOCK)
 * and GetFrameAndRef without reference frames fail instead of returning
 * stale data; SetPubAttr is refused while the device is enabled.
 */

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <imp/imp_dmic.h>

#define DMIC_DSP_SPEED            0xc0045002UL
#define DMIC_DSP_SETFMT           0xc0045005UL
#define DMIC_DSP_CHANNELS         0xc0045006UL
#define DMIC_EXT_DISABLE_AEC      0x40045064UL /* SNDCTL_EXT_DISABLE_AEC */
#define DMIC_EXT_ENABLE_AEC       0x40045065UL /* SNDCTL_EXT_ENABLE_AEC */
#define DMIC_EXT_ENABLE_STREAM    0x40045070UL /* ..._ENABLE_DMIC_STREAM */
#define DMIC_SET_GAIN             0x200UL      /* DMIC_SET_DMIC_GAIN */
#define DMIC_GET_STREAM           0x300UL      /* DMIC_GET_AI_STREAM */
#define DMIC_MAX_MICS             4
#define DMIC_DEFAULT_VOLUME       60
#define DMIC_READ_RETRY_US        20000

typedef struct {
    void *data;
    void *aec;
    uint32_t size;
} DmicStream;

#if UINTPTR_MAX == 0xffffffffu
_Static_assert(sizeof(DmicStream) == 12, "DMIC stream ABI mismatch");
_Static_assert(sizeof(IMPDmicFrame) == 32, "IMPDmicFrame ABI mismatch");
_Static_assert(sizeof(IMPDmicChnFrame) == 64, "IMPDmicChnFrame ABI mismatch");
#endif

typedef struct DmicNode {
    struct DmicNode *next;
    int index;
    int len;
    int64_t timestamp;
    int seq;
    int has_aec;
    int has_ref;
    int16_t *raw;          /* chnCnt * numPerFrm interleaved samples */
    int16_t *aec;          /* numPerFrm samples */
} DmicNode;

typedef struct {
    DmicNode *head;
    DmicNode *tail;
} DmicList;

typedef void *(*DmicAecCreate)(int, const char *);
typedef int (*DmicAecProcess)(void *, void *);
typedef int (*DmicAecFree)(void *);

typedef struct {
    const int16_t *far_end;
    int16_t *near_end;
    void *reserved;
    int num_bytes;
} DmicAecFrame;

static struct {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int enabled;
    int fd;
    IMPDmicAttr attr;
    int aec_mic;
    int need_aec;
    /* channel 0 */
    int chn_enabled;
    int depth;
    int volume;
    int gain;
    int seq;
    DmicNode *nodes;
    uint8_t *storage;
    DmicList free_list;
    DmicList ready_list;
    pthread_t thread;
    int thread_running;
    int stop;
    int reading;           /* the record thread uses ref buffer / aec */
    int16_t *ref;          /* depth * numPerFrm samples, indexed by node */
    int ref_enabled;
    int aec_enabled;
    void *aec;
    void *library;
    DmicAecCreate aec_create;
    DmicAecProcess aec_process;
    DmicAecFree aec_free;
} dmic = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .fd = -1,
    .volume = DMIC_DEFAULT_VOLUME,
};

static pthread_once_t dmic_once = PTHREAD_ONCE_INIT;

static void dmic_init_once(void)
{
    pthread_condattr_t attr;

    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&dmic.cond, &attr);
    pthread_condattr_destroy(&attr);
}

static void list_push(DmicList *list, DmicNode *node)
{
    node->next = NULL;
    if (list->tail)
        list->tail->next = node;
    else
        list->head = node;
    list->tail = node;
}

static DmicNode *list_pop(DmicList *list)
{
    DmicNode *node = list->head;

    if (node) {
        list->head = node->next;
        if (!list->head)
            list->tail = NULL;
        node->next = NULL;
    }
    return node;
}

static int dmic_valid(int device, int channel)
{
    return device == 0 && channel == 0;
}

static size_t dmic_mic_bytes(void)
{
    return (size_t)dmic.attr.numPerFrm * sizeof(int16_t);
}

static size_t dmic_frame_bytes(void)
{
    return dmic_mic_bytes() * (size_t)dmic.attr.chnCnt;
}

/* IMP volume in half-decibels, 60 = unity (same curve as IMP_AI_SetVol) */
static void dmic_apply_volume(int16_t *samples, int count, int volume)
{
    int steps = volume - 60;
    uint64_t gain = 65536;
    int i;

    if (!steps)
        return;
    if (volume <= -30) {
        memset(samples, 0, (size_t)count * sizeof(*samples));
        return;
    }
    if (steps > 60)
        steps = 60;
    if (steps > 0) {
        for (i = 0; i < steps; i++)
            gain = (gain * 69419U + 32768U) >> 16;
    } else {
        for (i = 0; i > steps; i--)
            gain = (gain * 61870U + 32768U) >> 16;
    }
    for (i = 0; i < count; i++) {
        int64_t value = ((int64_t)samples[i] * (int64_t)gain) >> 16;

        if (value > 32767)
            value = 32767;
        else if (value < -32768)
            value = -32768;
        samples[i] = (int16_t)value;
    }
}

static int64_t dmic_timestamp(void)
{
    extern int64_t IMP_System_GetTimeStamp(void);

    return IMP_System_GetTimeStamp();
}

static void *dmic_record_main(void *argument)
{
    (void)argument;
    pthread_mutex_lock(&dmic.lock);
    while (!dmic.stop) {
        DmicNode *node = list_pop(&dmic.free_list);
        DmicStream stream;
        int16_t *ref = NULL;
        int aec = 0;
        int volume;
        int ret;

        if (!node)
            node = list_pop(&dmic.ready_list); /* drop the oldest frame */
        if (!node) {
            /* every frame is held by the application */
            struct timespec deadline;

            clock_gettime(CLOCK_MONOTONIC, &deadline);
            deadline.tv_nsec += DMIC_READ_RETRY_US * 1000L;
            if (deadline.tv_nsec >= 1000000000L) {
                deadline.tv_sec++;
                deadline.tv_nsec -= 1000000000L;
            }
            pthread_cond_timedwait(&dmic.cond, &dmic.lock, &deadline);
            continue;
        }
        if ((dmic.ref_enabled || dmic.aec_enabled) && dmic.ref)
            ref = dmic.ref + (size_t)node->index * (size_t)dmic.attr.numPerFrm;
        aec = dmic.aec_enabled && dmic.aec && ref;
        volume = dmic.volume;
        dmic.reading = 1;
        pthread_mutex_unlock(&dmic.lock);

        memset(&stream, 0, sizeof(stream));
        stream.data = node->raw;
        stream.aec = ref;
        stream.size = (uint32_t)dmic_frame_bytes();
        ret = ioctl(dmic.fd, DMIC_GET_STREAM, &stream);
        if (ret == 0) {
            node->timestamp = dmic_timestamp();
            node->len = (int)dmic_frame_bytes();
            node->has_ref = ref != NULL;
            node->has_aec = 0;
            if (aec) {
                const int16_t *in = node->raw + dmic.aec_mic;
                DmicAecFrame frame;
                int i;

                for (i = 0; i < dmic.attr.numPerFrm; i++)
                    node->aec[i] = in[(size_t)i * (size_t)dmic.attr.chnCnt];
                frame.far_end = ref;
                frame.near_end = node->aec;
                frame.reserved = node->aec;
                frame.num_bytes = (int)dmic_mic_bytes();
                node->has_aec = dmic.aec_process(dmic.aec, &frame) == 0;
                if (node->has_aec)
                    dmic_apply_volume(node->aec, dmic.attr.numPerFrm, volume);
            }
            dmic_apply_volume(node->raw, node->len / 2, volume);
        }

        pthread_mutex_lock(&dmic.lock);
        dmic.reading = 0;
        if (ret == 0) {
            node->seq = dmic.seq++;
            list_push(&dmic.ready_list, node);
        } else {
            list_push(&dmic.free_list, node);
        }
        pthread_cond_broadcast(&dmic.cond);
        if (ret != 0 && !dmic.stop) {
            pthread_mutex_unlock(&dmic.lock);
            usleep(DMIC_READ_RETRY_US);
            pthread_mutex_lock(&dmic.lock);
        }
    }
    pthread_mutex_unlock(&dmic.lock);
    return NULL;
}

/* lock held; waits until the record thread is outside its ioctl/AEC step */
static void dmic_wait_idle(void)
{
    while (dmic.reading)
        pthread_cond_wait(&dmic.cond, &dmic.lock);
}

int IMP_DMIC_SetUserInfo(int dmicDevId, int aecDmicId, int need_aec)
{
    if (dmicDevId != 0 || aecDmicId < 0 || aecDmicId >= DMIC_MAX_MICS)
        return -1;
    pthread_mutex_lock(&dmic.lock);
    dmic.aec_mic = aecDmicId;
    dmic.need_aec = need_aec != 0;
    pthread_mutex_unlock(&dmic.lock);
    return 0;
}

int IMP_DMIC_SetPubAttr(int dmicDevId, IMPDmicAttr *attr)
{
    if (dmicDevId != 0 || !attr || attr->chnCnt <= 0 ||
        attr->chnCnt > DMIC_MAX_MICS || attr->samplerate <= 0 ||
        attr->numPerFrm <= 0)
        return -1;
    /* frames must be a whole number of 10 ms */
    if (((unsigned int)attr->numPerFrm * 1000u /
         (unsigned int)attr->samplerate) % 10u != 0)
        return -1;
    pthread_mutex_lock(&dmic.lock);
    if (dmic.enabled) {
        pthread_mutex_unlock(&dmic.lock);
        return -1;
    }
    dmic.attr = *attr;
    pthread_mutex_unlock(&dmic.lock);
    return 0;
}

int IMP_DMIC_GetPubAttr(int dmicDevId, IMPDmicAttr *attr)
{
    if (dmicDevId != 0 || !attr)
        return -1;
    pthread_mutex_lock(&dmic.lock);
    *attr = dmic.attr;
    pthread_mutex_unlock(&dmic.lock);
    return 0;
}

int IMP_DMIC_Enable(int dmicDevId)
{
    int fd;
    int rate;
    int channels;
    int format;

    if (dmicDevId != 0)
        return -1;
    pthread_once(&dmic_once, dmic_init_once);
    pthread_mutex_lock(&dmic.lock);
    if (dmic.enabled) {
        pthread_mutex_unlock(&dmic.lock);
        return 0;
    }
    if (dmic.attr.soundmode != DMIC_SOUND_MODE_MONO ||
        dmic.attr.bitwidth != DMIC_BIT_WIDTH_16 || dmic.attr.chnCnt <= 0 ||
        dmic.attr.numPerFrm <= 0) {
        pthread_mutex_unlock(&dmic.lock);
        return -1;
    }
    fd = open("/dev/dsp", O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        pthread_mutex_unlock(&dmic.lock);
        return -1;
    }
    rate = dmic.attr.samplerate;
    channels = dmic.attr.chnCnt;
    format = dmic.attr.bitwidth;
    if (ioctl(fd, DMIC_DSP_SPEED, &rate) != 0 ||
        ioctl(fd, DMIC_DSP_CHANNELS, &channels) != 0 ||
        ioctl(fd, DMIC_DSP_SETFMT, &format) != 0 ||
        ioctl(fd, DMIC_EXT_ENABLE_STREAM, 1) != 0) {
        /* no CONFIG_JZ_TS_DMIC in the audio driver, or no DMIC wired */
        close(fd);
        pthread_mutex_unlock(&dmic.lock);
        return -1;
    }
    dmic.fd = fd;
    dmic.enabled = 1;
    pthread_mutex_unlock(&dmic.lock);
    return 0;
}

static void dmic_stop_channel(void);

int IMP_DMIC_Disable(int dmicDevId)
{
    if (dmicDevId != 0)
        return -1;
    pthread_once(&dmic_once, dmic_init_once);
    pthread_mutex_lock(&dmic.lock);
    if (!dmic.enabled) {
        pthread_mutex_unlock(&dmic.lock);
        return 0;
    }
    pthread_mutex_unlock(&dmic.lock);
    dmic_stop_channel();
    pthread_mutex_lock(&dmic.lock);
    close(dmic.fd);
    dmic.fd = -1;
    dmic.enabled = 0;
    pthread_mutex_unlock(&dmic.lock);
    return 0;
}

int IMP_DMIC_SetChnParam(int dmicDevId, int dmicChnId,
                         IMPDmicChnParam *chnParam)
{
    if (!dmic_valid(dmicDevId, dmicChnId) || !chnParam)
        return -1;
    pthread_mutex_lock(&dmic.lock);
    if (chnParam->usrFrmDepth < 2 ||
        chnParam->usrFrmDepth > dmic.attr.frmNum || dmic.chn_enabled) {
        pthread_mutex_unlock(&dmic.lock);
        return -1;
    }
    dmic.depth = chnParam->usrFrmDepth;
    pthread_mutex_unlock(&dmic.lock);
    return 0;
}

int IMP_DMIC_GetChnParam(int dmicDevId, int dmicChnId,
                         IMPDmicChnParam *chnParam)
{
    if (!dmic_valid(dmicDevId, dmicChnId) || !chnParam)
        return -1;
    pthread_mutex_lock(&dmic.lock);
    if (!dmic.enabled) {
        pthread_mutex_unlock(&dmic.lock);
        return -1;
    }
    memset(chnParam, 0, sizeof(*chnParam));
    chnParam->usrFrmDepth = dmic.depth;
    pthread_mutex_unlock(&dmic.lock);
    return 0;
}

int IMP_DMIC_EnableChn(int dmicDevId, int dmicChnId)
{
    size_t raw_bytes;
    size_t node_bytes;
    int i;

    if (!dmic_valid(dmicDevId, dmicChnId))
        return -1;
    pthread_mutex_lock(&dmic.lock);
    if (!dmic.enabled) {
        pthread_mutex_unlock(&dmic.lock);
        return -1;
    }
    if (dmic.chn_enabled) {
        pthread_mutex_unlock(&dmic.lock);
        return 0;
    }
    if (dmic.depth < 2 || dmic.depth > dmic.attr.frmNum) {
        pthread_mutex_unlock(&dmic.lock);
        return -1;
    }
    raw_bytes = dmic_frame_bytes();
    node_bytes = raw_bytes + dmic_mic_bytes();
    dmic.nodes = calloc((size_t)dmic.depth, sizeof(*dmic.nodes));
    dmic.storage = calloc((size_t)dmic.depth, node_bytes);
    if (!dmic.nodes || !dmic.storage) {
        free(dmic.nodes);
        free(dmic.storage);
        dmic.nodes = NULL;
        dmic.storage = NULL;
        pthread_mutex_unlock(&dmic.lock);
        return -1;
    }
    memset(&dmic.free_list, 0, sizeof(dmic.free_list));
    memset(&dmic.ready_list, 0, sizeof(dmic.ready_list));
    for (i = 0; i < dmic.depth; i++) {
        uint8_t *base = dmic.storage + (size_t)i * node_bytes;

        dmic.nodes[i].index = i;
        dmic.nodes[i].raw = (int16_t *)(void *)base;
        dmic.nodes[i].aec = (int16_t *)(void *)(base + raw_bytes);
        list_push(&dmic.free_list, &dmic.nodes[i]);
    }
    dmic.volume = DMIC_DEFAULT_VOLUME;
    dmic.stop = 0;
    dmic.chn_enabled = 1;
    if (pthread_create(&dmic.thread, NULL, dmic_record_main, NULL) != 0) {
        dmic.chn_enabled = 0;
        free(dmic.nodes);
        free(dmic.storage);
        dmic.nodes = NULL;
        dmic.storage = NULL;
        pthread_mutex_unlock(&dmic.lock);
        return -1;
    }
    dmic.thread_running = 1;
    pthread_mutex_unlock(&dmic.lock);
    return 0;
}

static void dmic_release_aec(void)
{
    if (dmic.aec && dmic.aec_free)
        (void)dmic.aec_free(dmic.aec);
    dmic.aec = NULL;
    dmic.aec_enabled = 0;
}

static void dmic_stop_channel(void)
{
    pthread_t thread;
    int running;

    pthread_mutex_lock(&dmic.lock);
    running = dmic.thread_running;
    thread = dmic.thread;
    dmic.stop = 1;
    dmic.chn_enabled = 0;
    pthread_cond_broadcast(&dmic.cond);
    pthread_mutex_unlock(&dmic.lock);
    if (running)
        pthread_join(thread, NULL);
    pthread_mutex_lock(&dmic.lock);
    dmic.thread_running = 0;
    if (dmic.aec_enabled || dmic.ref_enabled) {
        if (dmic.fd >= 0)
            (void)ioctl(dmic.fd, DMIC_EXT_DISABLE_AEC, 0);
    }
    dmic_release_aec();
    dmic.ref_enabled = 0;
    free(dmic.ref);
    dmic.ref = NULL;
    free(dmic.nodes);
    free(dmic.storage);
    dmic.nodes = NULL;
    dmic.storage = NULL;
    memset(&dmic.free_list, 0, sizeof(dmic.free_list));
    memset(&dmic.ready_list, 0, sizeof(dmic.ready_list));
    pthread_cond_broadcast(&dmic.cond);
    pthread_mutex_unlock(&dmic.lock);
}

int IMP_DMIC_DisableChn(int dmicDevId, int dmicChnId)
{
    int enabled;

    if (!dmic_valid(dmicDevId, dmicChnId))
        return -1;
    pthread_once(&dmic_once, dmic_init_once);
    pthread_mutex_lock(&dmic.lock);
    enabled = dmic.enabled && dmic.chn_enabled;
    pthread_mutex_unlock(&dmic.lock);
    if (!enabled)
        return -1;
    dmic_stop_channel();
    return 0;
}

static void dmic_fill_frame(IMPDmicFrame *frame, void *data, int len,
                            int64_t timestamp, int seq)
{
    memset(frame, 0, sizeof(*frame));
    frame->bitwidth = dmic.attr.bitwidth;
    frame->soundmode = dmic.attr.soundmode;
    frame->virAddr = (uint32_t *)data;
    frame->timeStamp = timestamp;
    frame->seq = seq;
    frame->len = len;
}

static DmicNode *dmic_take(IMPBlock block)
{
    DmicNode *node = NULL;

    while (dmic.chn_enabled && !(node = list_pop(&dmic.ready_list))) {
        if (block != BLOCK)
            return NULL;
        pthread_cond_wait(&dmic.cond, &dmic.lock);
    }
    return dmic.chn_enabled ? node : NULL;
}

int IMP_DMIC_GetFrame(int dmicDevId, int dmicChnId, IMPDmicChnFrame *chnFrm,
                      IMPBlock block)
{
    DmicNode *node;

    if (!dmic_valid(dmicDevId, dmicChnId) || !chnFrm)
        return -1;
    pthread_mutex_lock(&dmic.lock);
    if (!dmic.enabled || !dmic.chn_enabled ||
        !(node = dmic_take(block))) {
        pthread_mutex_unlock(&dmic.lock);
        return -1;
    }
    dmic_fill_frame(&chnFrm->rawFrame, node->raw, node->len, node->timestamp,
                    node->seq);
    if (node->has_aec)
        dmic_fill_frame(&chnFrm->aecFrame, node->aec,
                        node->len / dmic.attr.chnCnt, node->timestamp,
                        node->seq);
    else
        memset(&chnFrm->aecFrame, 0, sizeof(chnFrm->aecFrame));
    pthread_mutex_unlock(&dmic.lock);
    return 0;
}

int IMP_DMIC_GetFrameAndRef(int dmicDevId, int dmicChnId,
                            IMPDmicChnFrame *chnFrm, IMPDmicFrame *ref,
                            IMPBlock block)
{
    DmicNode *node;

    if (!dmic_valid(dmicDevId, dmicChnId) || !chnFrm || !ref)
        return -1;
    pthread_mutex_lock(&dmic.lock);
    if (!dmic.enabled || !dmic.chn_enabled || !dmic.ref ||
        !(node = dmic_take(block))) {
        pthread_mutex_unlock(&dmic.lock);
        return -1;
    }
    dmic_fill_frame(&chnFrm->rawFrame, node->raw, node->len, node->timestamp,
                    node->seq);
    memset(&chnFrm->aecFrame, 0, sizeof(chnFrm->aecFrame));
    dmic_fill_frame(ref,
                    dmic.ref + (size_t)node->index *
                                   (size_t)dmic.attr.numPerFrm,
                    node->len / dmic.attr.chnCnt, 0, node->seq);
    if (!node->has_ref)
        memset(ref->virAddr, 0, (size_t)ref->len);
    pthread_mutex_unlock(&dmic.lock);
    return 0;
}

int IMP_DMIC_ReleaseFrame(int dmicDevId, int dmicChnId,
                          IMPDmicChnFrame *chnFrm)
{
    int i;

    if (!dmic_valid(dmicDevId, dmicChnId) || !chnFrm)
        return -1;
    pthread_mutex_lock(&dmic.lock);
    if (!dmic.enabled || !dmic.chn_enabled || !dmic.nodes) {
        pthread_mutex_unlock(&dmic.lock);
        return -1;
    }
    for (i = 0; i < dmic.depth; i++) {
        if ((void *)dmic.nodes[i].raw == (void *)chnFrm->rawFrame.virAddr) {
            list_push(&dmic.free_list, &dmic.nodes[i]);
            pthread_cond_broadcast(&dmic.cond);
            pthread_mutex_unlock(&dmic.lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&dmic.lock);
    return -1;
}

int IMP_DMIC_PollingFrame(int dmicDevId, int dmicChnId,
                          unsigned int timeout_ms)
{
    struct timespec deadline;
    int ready;

    if (!dmic_valid(dmicDevId, dmicChnId))
        return -1;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += timeout_ms / 1000u;
    deadline.tv_nsec += (long)(timeout_ms % 1000u) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }
    pthread_mutex_lock(&dmic.lock);
    if (!dmic.enabled || !dmic.chn_enabled) {
        pthread_mutex_unlock(&dmic.lock);
        return -1;
    }
    while (dmic.chn_enabled && !dmic.ready_list.head &&
           pthread_cond_timedwait(&dmic.cond, &dmic.lock, &deadline) == 0)
        ;
    ready = dmic.ready_list.head != NULL;
    pthread_mutex_unlock(&dmic.lock);
    return ready ? 0 : -1;
}

int IMP_DMIC_SetVol(int dmicDevId, int dmicChnId, int dmicVol)
{
    if (!dmic_valid(dmicDevId, dmicChnId))
        return -1;
    if (dmicVol < -30)
        dmicVol = -30;
    else if (dmicVol > 120)
        dmicVol = 120;
    pthread_mutex_lock(&dmic.lock);
    dmic.volume = dmicVol;
    pthread_mutex_unlock(&dmic.lock);
    return 0;
}

int IMP_DMIC_GetVol(int dmicDevId, int dmicChnId, int *dmicVol)
{
    if (!dmic_valid(dmicDevId, dmicChnId) || !dmicVol)
        return -1;
    pthread_mutex_lock(&dmic.lock);
    *dmicVol = dmic.volume;
    pthread_mutex_unlock(&dmic.lock);
    return 0;
}

int IMP_DMIC_SetGain(int dmicDevId, int dmicChnId, int dmicGain)
{
    int ret;

    if (!dmic_valid(dmicDevId, dmicChnId))
        return -1;
    if (dmicGain < 0)
        dmicGain = 0;
    else if (dmicGain > 31)
        dmicGain = 31;
    pthread_mutex_lock(&dmic.lock);
    ret = dmic.fd >= 0 ? ioctl(dmic.fd, DMIC_SET_GAIN, dmicGain) : -1;
    if (ret >= 0)
        dmic.gain = dmicGain;
    pthread_mutex_unlock(&dmic.lock);
    return ret >= 0 ? 0 : -1;
}

int IMP_DMIC_GetGain(int dmicDevId, int dmicChnId, int *dmicGain)
{
    if (!dmic_valid(dmicDevId, dmicChnId) || !dmicGain)
        return -1;
    pthread_mutex_lock(&dmic.lock);
    *dmicGain = dmic.gain;
    pthread_mutex_unlock(&dmic.lock);
    return 0;
}

/* lock held */
static int dmic_ref_enable(void)
{
    int one = 1;

    if (!dmic.ref) {
        dmic.ref = calloc((size_t)dmic.depth, dmic_mic_bytes());
        if (!dmic.ref)
            return -1;
    }
    if (ioctl(dmic.fd, DMIC_EXT_ENABLE_AEC, &one) != 0) {
        free(dmic.ref);
        dmic.ref = NULL;
        return -1;
    }
    return 0;
}

/* lock held */
static void dmic_ref_disable(void)
{
    int zero = 0;

    dmic_wait_idle();
    (void)ioctl(dmic.fd, DMIC_EXT_DISABLE_AEC, &zero);
    free(dmic.ref);
    dmic.ref = NULL;
}

static int dmic_load_aec(void)
{
    if (dmic.library)
        return 0;
    dmic.library = dlopen("libaudioProcess.so", RTLD_NOW | RTLD_LOCAL);
    if (!dmic.library)
        return -1;
    *(void **)&dmic.aec_create =
        dlsym(dmic.library, "audio_process_aec_create");
    *(void **)&dmic.aec_process =
        dlsym(dmic.library, "audio_process_aec_process");
    *(void **)&dmic.aec_free = dlsym(dmic.library, "audio_process_aec_free");
    if (!dmic.aec_create || !dmic.aec_process || !dmic.aec_free) {
        dlclose(dmic.library);
        dmic.library = NULL;
        return -1;
    }
    return 0;
}

int IMP_DMIC_EnableAec(int dmicDevId, int dmicChnId, int aoDevId,
                       int aoChId)
{
    void *handle;

    (void)aoDevId;
    (void)aoChId;
    if (!dmic_valid(dmicDevId, dmicChnId))
        return -1;
    pthread_mutex_lock(&dmic.lock);
    if (!dmic.enabled || !dmic.chn_enabled) {
        pthread_mutex_unlock(&dmic.lock);
        return -1;
    }
    if (dmic.ref_enabled || dmic.aec_enabled) {
        /* libimp: "AEC and Ref can not enable both", reported as success */
        pthread_mutex_unlock(&dmic.lock);
        return 0;
    }
    if (dmic_load_aec() != 0) {
        pthread_mutex_unlock(&dmic.lock);
        return -1;
    }
    handle = dmic.aec_create(dmic.attr.samplerate, NULL);
    if (!handle) {
        pthread_mutex_unlock(&dmic.lock);
        return -1;
    }
    if (dmic_ref_enable() != 0) {
        (void)dmic.aec_free(handle);
        pthread_mutex_unlock(&dmic.lock);
        return -1;
    }
    dmic.aec = handle;
    dmic.aec_enabled = 1;
    pthread_mutex_unlock(&dmic.lock);
    return 0;
}

int IMP_DMIC_DisableAec(int dmicDevId, int dmicChnId)
{
    if (!dmic_valid(dmicDevId, dmicChnId))
        return -1;
    pthread_mutex_lock(&dmic.lock);
    if (!dmic.enabled || !dmic.chn_enabled || !dmic.aec_enabled) {
        pthread_mutex_unlock(&dmic.lock);
        return -1;
    }
    dmic.aec_enabled = 0;
    dmic_wait_idle();
    dmic_release_aec();
    dmic_ref_disable();
    pthread_mutex_unlock(&dmic.lock);
    return 0;
}

int IMP_DMIC_EnableAecRefFrame(int dmicDevId, int dmicChnId, int audioAoDevId,
                               int aoChn)
{
    (void)audioAoDevId;
    (void)aoChn;
    if (!dmic_valid(dmicDevId, dmicChnId))
        return -1;
    pthread_mutex_lock(&dmic.lock);
    if (!dmic.enabled || !dmic.chn_enabled || dmic.aec_enabled) {
        pthread_mutex_unlock(&dmic.lock);
        return -1;
    }
    if (!dmic.ref_enabled) {
        if (dmic_ref_enable() != 0) {
            pthread_mutex_unlock(&dmic.lock);
            return -1;
        }
        dmic.ref_enabled = 1;
    }
    pthread_mutex_unlock(&dmic.lock);
    return 0;
}

int IMP_DMIC_DisableAecRefFrame(int dmicDevId, int dmicChnId,
                                int audioAoDevId, int aoChn)
{
    (void)audioAoDevId;
    (void)aoChn;
    if (!dmic_valid(dmicDevId, dmicChnId))
        return -1;
    pthread_mutex_lock(&dmic.lock);
    if (!dmic.enabled || !dmic.chn_enabled || !dmic.ref_enabled) {
        pthread_mutex_unlock(&dmic.lock);
        return -1;
    }
    dmic.ref_enabled = 0;
    dmic_ref_disable();
    pthread_mutex_unlock(&dmic.lock);
    return 0;
}
