/**
 * IMP Encoder Module Implementation
 * Based on reverse engineering of libimp.so v1.1.6
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/select.h>
#include <sys/eventfd.h>
#include <pthread.h>
#include <semaphore.h>
#include <imp/imp_encoder.h>
#include <imp/imp_system.h>
#include "fifo.h"
#include "codec.h"

/* External system functions */
extern void* IMP_System_GetModule(int deviceID, int groupID);

#define LOG_ENC(fmt, ...) fprintf(stderr, "[Encoder] " fmt "\n", ##__VA_ARGS__)
#define FIFO_SIZE 64  /* Size of Fifo structure */

/* Encoder channel structure - 0x308 bytes per channel */
#define MAX_ENC_CHANNELS 9
#define ENC_CHANNEL_SIZE 0x308
#define MAX_ENC_GROUPS 3

/* Internal stream buffer structure */
typedef struct {
    IMPEncoderPack pack;        /* Stream pack data */
    uint32_t seq;               /* Sequence number */
    int streamEnd;              /* Stream end flag */
    void *codec_stream;         /* Pointer to codec stream data */
    void *injected_buf;         /* If non-NULL, malloc'd buffer we must free */
} StreamBuffer;

typedef struct {
    int chn_id;                 /* 0x00: Channel ID (-1 = unused) */
    uint8_t data_04[0x4];       /* 0x04-0x07: Padding */
    void *codec;                /* 0x08: AL_Codec handle (offset arg1[2]) */
    int src_frame_cnt;          /* 0x0c: Source frame count (offset arg1[3]) */
    int src_frame_size;         /* 0x10: Source frame size (offset arg1[4]) */
    void *frame_buffers;        /* 0x14: Frame buffer array (offset arg1[5]) */
    uint8_t fifo[FIFO_SIZE];    /* 0x18: Fifo structure (offset arg1[6]) */
    uint8_t data_58[0x40];      /* 0x58-0x97: Padding to 0x98 */
    IMPEncoderCHNAttr attr;     /* 0x98: Channel attributes (0x70 bytes) */
    uint8_t data_108[0x8c];     /* 0x108-0x193: Padding */
    uint8_t data_194[0x44];     /* 0x194-0x1d7: Padding */
    pthread_mutex_t mutex_1d8;  /* 0x1d8: Mutex (offset arg1[0x6a]) */
    pthread_cond_t cond_1f0;    /* 0x1f0: Condition variable */
    uint8_t data_208[0x18];     /* 0x208-0x21f: Padding */
    int eventfd;                /* 0x220: Event FD */
    uint8_t data_224[0x70];     /* 0x224-0x293: Padding */
    void *group_ptr;            /* 0x294: Pointer to group */
    uint8_t data_298[0x100];    /* 0x298-0x397: More data */
    uint8_t registered;         /* 0x398: Registration flag */
    uint8_t data_399[0x7];      /* 0x399-0x39f: Padding */
    uint8_t data_3a0[0x4];      /* 0x3a0-0x3a3: More data */
    uint8_t enabled;            /* 0x3a4: Enable flag */
    uint8_t data_3a5[0x3];      /* 0x3a5-0x3a7: Padding */
    uint8_t data_3a8[0x4];      /* 0x3a8-0x3ab: More data */
    uint8_t started;            /* 0x3ac: Started flag */
    uint8_t data_3ad[0x53];     /* 0x3ad-0x3ff: More data */
    uint8_t recv_pic_enabled;   /* 0x400: Receive picture enabled */
    uint8_t data_401[0x3];      /* 0x401-0x403: Padding */
    uint8_t recv_pic_started;   /* 0x404: Receive picture started */
    uint8_t data_405[0x3];      /* 0x405-0x407: Padding */
    sem_t sem_408;              /* 0x408: Semaphore (offset arg1[0x102]) */
    sem_t sem_418;              /* 0x418: Semaphore (offset arg1[0x106]) */
    sem_t sem_428;              /* 0x428: Semaphore (offset arg1[0x10a]) */
    pthread_mutex_t mutex_438;  /* 0x438: Mutex (offset arg1[0x10e]) */
    pthread_mutex_t mutex_450;  /* 0x450: Mutex (offset arg1[0x114]) */
    pthread_t thread_encoder;   /* 0x468: Encoder thread (offset arg1[0x1a]) */
    pthread_t thread_stream;    /* 0x46c: Stream thread (offset arg1[0xf]) */
    uint8_t data_470[0x98];     /* 0x470-0x507: Padding */
    /* Total: 0x308 bytes (776 bytes) */

    /* Extended fields (not part of binary structure) */
    StreamBuffer *current_stream;  /* Current stream buffer */
    uint32_t stream_seq;           /* Stream sequence counter */
    int gop_length;                /* GOP length (offset 0x3d0) */
    int entropy_mode;              /* Entropy mode (offset 0x3fc) */
    int max_stream_cnt;            /* Max stream count (offset 0x4c0) */
    int stream_buf_size;           /* Stream buffer size (offset 0x4c4) */
} EncChannel;

/* Encoder group structure */
typedef struct {
    int group_id;               /* 0x00: Group ID */
    uint32_t field_04;          /* 0x04: Field */
    uint32_t chn_count;         /* 0x08: Number of registered channels */
    EncChannel *channels[3];    /* 0x0c-0x14: Up to 3 channels per group */
} EncGroup;

/* Global encoder state */
typedef struct {
    void *module_ptr;           /* 0x00: Module pointer */
    EncGroup groups[6];         /* 0x04: Up to 6 groups */
} EncoderState;

/* Global variables */
static EncoderState *gEncoder = NULL;
static EncChannel g_EncChannel[MAX_ENC_CHANNELS];
static pthread_mutex_t encoder_mutex = PTHREAD_MUTEX_INITIALIZER;
static int encoder_initialized = 0;

/* Forward declarations for helper functions */
static int channel_encoder_init(EncChannel *chn);
static int channel_encoder_exit(EncChannel *chn);
static int channel_encoder_set_rc_param(void *dst, IMPEncoderRcAttr *src);
static void *encoder_thread(void *arg);
static void *stream_thread(void *arg);
static int encoder_update(void *module, void *frame);
/* H.264 SPS/PPS caching and minimal Annex B parsing for prefix injection */
#define MAX_PARAM_SET_SIZE 256
static uint8_t g_last_sps[MAX_ENC_CHANNELS][MAX_PARAM_SET_SIZE];
static int g_last_sps_len[MAX_ENC_CHANNELS];
static uint8_t g_last_pps[MAX_ENC_CHANNELS][MAX_PARAM_SET_SIZE];
static int g_last_pps_len[MAX_ENC_CHANNELS];

/* Find next start code position (returns index or len if none). Sets *sc to 3 or 4 */
static size_t find_start_code(const uint8_t *b, size_t off, size_t len, int *sc)
{
    for (size_t i = off; i + 3 < len; ++i) {
        if (b[i] == 0x00 && b[i+1] == 0x00) {
            if (b[i+2] == 0x01) { if (sc) *sc = 3; return i; }
            if (i + 4 < len && b[i+2] == 0x00 && b[i+3] == 0x01) { if (sc) *sc = 4; return i; }
        }
    }
    if (sc) *sc = 0; return len;
}

static void cache_sps_pps_from_annexb(int ch, const uint8_t *buf, size_t len)
{
    if (ch < 0 || ch >= MAX_ENC_CHANNELS || !buf || len < 4) return;
    size_t i = 0; int sc = 0;
    i = find_start_code(buf, 0, len, &sc);
    while (i < len) {
        size_t nal_start = i + (sc ? sc : 0);
        int sc2 = 0; size_t next = find_start_code(buf, nal_start, len, &sc2);
        if (nal_start < next) {
            uint8_t nalh = buf[nal_start];
            uint8_t nalt = nalh & 0x1F;
            if (nalt == 7) { /* SPS */
                size_t sz = next - nal_start;
                if ((int)sz > MAX_PARAM_SET_SIZE) sz = MAX_PARAM_SET_SIZE;
                memcpy(g_last_sps[ch], buf + nal_start, sz);
                g_last_sps_len[ch] = (int)sz;
            } else if (nalt == 8) { /* PPS */
                size_t sz = next - nal_start;
                if ((int)sz > MAX_PARAM_SET_SIZE) sz = MAX_PARAM_SET_SIZE;
                memcpy(g_last_pps[ch], buf + nal_start, sz);
                g_last_pps_len[ch] = (int)sz;
            }
        }
        if (next >= len) break;
        i = next; sc = sc2;
    }
}

static int has_sps_pps_before_vcl(const uint8_t *buf, size_t len)
{
    size_t i = 0; int sc = 0; int seen_sps = 0, seen_pps = 0;
    i = find_start_code(buf, 0, len, &sc);
    while (i < len) {
        size_t nal_start = i + (sc ? sc : 0);
        int sc2 = 0; size_t next = find_start_code(buf, nal_start, len, &sc2);
        if (nal_start < next) {
            uint8_t nalt = buf[nal_start] & 0x1F;
            if (nalt == 7) seen_sps = 1;
            else if (nalt == 8) seen_pps = 1;
            else if (nalt == 1 || nalt == 5) return (seen_sps && seen_pps);
        }
        if (next >= len) break;
        i = next; sc = sc2;
    }
    return (seen_sps && seen_pps);
}

static uint8_t *inject_prefix_if_needed(int ch, int is_h264, const uint8_t *buf, size_t len, size_t *out_len)
{
    if (!is_h264 || !buf || len == 0 || ch < 0 || ch >= MAX_ENC_CHANNELS) return NULL;
    /* Update caches from current buffer */
    cache_sps_pps_from_annexb(ch, buf, len);
    /* If SPS/PPS already present before first VCL, do nothing */
    if (has_sps_pps_before_vcl(buf, len)) return NULL;
    /* Need cached SPS & PPS to inject */
    if (g_last_sps_len[ch] <= 0 || g_last_pps_len[ch] <= 0) return NULL;
    size_t add = 4 + (size_t)g_last_sps_len[ch] + 4 + (size_t)g_last_pps_len[ch];
    uint8_t *out = (uint8_t*)malloc(add + len);
    if (!out) return NULL;
    size_t o = 0;
    out[o++] = 0; out[o++] = 0; out[o++] = 0; out[o++] = 1;
    memcpy(out + o, g_last_sps[ch], g_last_sps_len[ch]); o += g_last_sps_len[ch];
    out[o++] = 0; out[o++] = 0; out[o++] = 0; out[o++] = 1;
    memcpy(out + o, g_last_pps[ch], g_last_pps_len[ch]); o += g_last_pps_len[ch];
    memcpy(out + o, buf, len); o += len;
    *out_len = o;
    return out;
}


/* Initialize encoder module */
static void encoder_init(void) {
    if (encoder_initialized) return;

    /* Initialize all channels as unused */
    for (int i = 0; i < MAX_ENC_CHANNELS; i++) {
        memset(&g_EncChannel[i], 0, ENC_CHANNEL_SIZE);
        g_EncChannel[i].chn_id = -1;

        /* Register module with system for each channel */
        void *module = IMP_System_GetModule(DEV_ID_ENC, i);
        if (module != NULL) {
            /* Set update function pointer at offset 0x4c */
            uint8_t *mod_bytes = (uint8_t*)module;
            void **update_ptr = (void**)(mod_bytes + 0x4c);

            *update_ptr = (void*)encoder_update;

            LOG_ENC("Registered update callback for ENC channel %d", i);
        }
    }

    encoder_initialized = 1;
}

/* Public init to match OEM SystemInit calling pattern */
int EncoderInit(void) {
    pthread_mutex_lock(&encoder_mutex);
    encoder_init();
    if (gEncoder == NULL) {
        gEncoder = (EncoderState*)calloc(1, sizeof(EncoderState));
        if (gEncoder == NULL) {
            pthread_mutex_unlock(&encoder_mutex);
            return -1;
        }
        for (int i = 0; i < 6; i++) {
            gEncoder->groups[i].group_id = -1;
            gEncoder->groups[i].chn_count = 0;
            gEncoder->groups[i].channels[0] = NULL;
            gEncoder->groups[i].channels[1] = NULL;
            gEncoder->groups[i].channels[2] = NULL;
        }
    }
    pthread_mutex_unlock(&encoder_mutex);
    return 0;
}


/* IMP_Encoder_CreateGroup - based on decompilation at 0x82658 */
int IMP_Encoder_CreateGroup(int encGroup) {
    if (encGroup < 0 || encGroup >= 6) {
        LOG_ENC("CreateGroup failed: invalid group %d", encGroup);
        return -1;
    }

    pthread_mutex_lock(&encoder_mutex);


    /* Initialize encoder module */
    encoder_init();

    /* Initialize gEncoder if needed */
    if (gEncoder == NULL) {
        gEncoder = (EncoderState*)calloc(1, sizeof(EncoderState));
        if (gEncoder == NULL) {
            pthread_mutex_unlock(&encoder_mutex);
            return -1;
        }

        /* Initialize all groups */
        for (int i = 0; i < 6; i++) {
            gEncoder->groups[i].group_id = -1;
            gEncoder->groups[i].chn_count = 0;
            gEncoder->groups[i].channels[0] = NULL;
            gEncoder->groups[i].channels[1] = NULL;
            gEncoder->groups[i].channels[2] = NULL;
        }
    }

    /* Check if group already exists */
    if (gEncoder->groups[encGroup].group_id >= 0) {
        LOG_ENC("CreateGroup: group %d already exists", encGroup);
        pthread_mutex_unlock(&encoder_mutex);
        return 0;
    }

    /* Initialize the group */
    gEncoder->groups[encGroup].group_id = encGroup;
    gEncoder->groups[encGroup].chn_count = 0;

    /* Register Encoder module with system (DEV_ID_ENC = 1) */
    /* Allocate a proper Module structure for this encoder group */
    extern void* IMP_System_AllocModule(const char *name, int groupID);
    void *enc_module = IMP_System_AllocModule("Encoder", encGroup);
    if (enc_module == NULL) {
        LOG_ENC("CreateGroup: Failed to allocate module");
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    /* Set output_count to 1 (Encoder has 1 output per group) */
    /* Module structure has output_count at offset 0x134 */
    uint32_t *output_count_ptr = (uint32_t*)((char*)enc_module + 0x134);
    *output_count_ptr = 1;

    /* Set update callback (func_4c at offset 0x4c) */
    void **update_ptr = (void**)((char*)enc_module + 0x4c);
    *update_ptr = (void*)encoder_update;

    extern int IMP_System_RegisterModule(int deviceID, int groupID, void *module);
    IMP_System_RegisterModule(1, encGroup, enc_module);  /* DEV_ID_ENC = 1 */
    LOG_ENC("CreateGroup: registered Encoder module [1,%d] with 1 output and update callback", encGroup);

    pthread_mutex_unlock(&encoder_mutex);

    LOG_ENC("CreateGroup: grp=%d", encGroup);
    return 0;
}

/* IMP_Encoder_DestroyGroup - based on decompilation at 0x827f4 */
int IMP_Encoder_DestroyGroup(int encGroup) {
    if (encGroup < 0 || encGroup >= 6) {
        LOG_ENC("DestroyGroup failed: invalid group %d", encGroup);
        return -1;
    }

    pthread_mutex_lock(&encoder_mutex);

    if (gEncoder == NULL || gEncoder->groups[encGroup].group_id < 0) {
        LOG_ENC("DestroyGroup: group %d doesn't exist", encGroup);
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    /* Check if any channels are still registered */
    if (gEncoder->groups[encGroup].channels[0] != NULL ||
        gEncoder->groups[encGroup].channels[1] != NULL ||
        gEncoder->groups[encGroup].channels[2] != NULL) {
        LOG_ENC("DestroyGroup failed: group %d still has channels", encGroup);
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    /* Destroy the group */
    gEncoder->groups[encGroup].group_id = -1;
    gEncoder->groups[encGroup].chn_count = 0;

    pthread_mutex_unlock(&encoder_mutex);

    LOG_ENC("DestroyGroup: grp=%d", encGroup);
    return 0;
}

/* IMP_Encoder_CreateChn - based on decompilation at 0x836e0 */
int IMP_Encoder_CreateChn(int encChn, IMPEncoderCHNAttr *attr) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) {
        LOG_ENC("CreateChn failed: invalid channel %d", encChn);
        return -1;
    }

    if (attr == NULL) {
        LOG_ENC("CreateChn failed: NULL attr");
        return -1;
    }

    pthread_mutex_lock(&encoder_mutex);

    EncChannel *chn = &g_EncChannel[encChn];

    /* Check if channel already exists */
    if (chn->chn_id >= 0) {
        LOG_ENC("CreateChn: channel %d already exists", encChn);
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    /* Clear channel structure (from decompilation: memset 0x308 bytes) */
    memset(chn, 0, ENC_CHANNEL_SIZE);

    /* Copy attributes */
    memcpy(&chn->attr, attr, sizeof(IMPEncoderCHNAttr));

    /* Set channel ID and flags */
    chn->chn_id = encChn;
    chn->registered = 1;  /* Mark as created (offset 0x3ac in decompilation) */
    chn->enabled = 0;
    chn->started = 1;     /* Mark as initialized (offset 0x404 in decompilation) */

    /* Initialize semaphores (from decompilation at 0x83d18) */
    /* sem_init at offset 0x418 with value 0 */
    sem_init(&chn->sem_418, 0, 0);

    /* sem_init at offset 0x408 with buffer count value */
    int buf_count = 4; /* Default buffer count */
    sem_init(&chn->sem_408, 0, buf_count);

    /* Initialize mutexes (from decompilation at 0x83d30) */
    if (pthread_mutex_init(&chn->mutex_438, NULL) < 0) {
        LOG_ENC("CreateChn: failed to init mutex_438");
        chn->chn_id = -1;
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    /* Initialize recursive mutex (from decompilation at 0x83d50) */
    pthread_mutexattr_t attr_recursive;
    if (pthread_mutexattr_init(&attr_recursive) < 0) {
        LOG_ENC("CreateChn: failed to init mutexattr");
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    pthread_mutexattr_settype(&attr_recursive, PTHREAD_MUTEX_RECURSIVE);

    if (pthread_mutex_init(&chn->mutex_450, &attr_recursive) < 0) {
        LOG_ENC("CreateChn: failed to init mutex_450");
        chn->chn_id = -1;
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    pthread_mutexattr_destroy(&attr_recursive);

    /* Initialize semaphore at 0x428 (from decompilation at 0x83d70) */
    if (sem_init(&chn->sem_428, 0, 0) < 0) {
        LOG_ENC("CreateChn: failed to init sem_428");
        chn->chn_id = -1;
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    /* Initialize encoder (from decompilation: channel_encoder_init) */
    if (channel_encoder_init(chn) < 0) {
        LOG_ENC("CreateChn: channel_encoder_init failed");

        /* Cleanup on failure */
        if (chn->frame_buffers != NULL) {
            free(chn->frame_buffers);
            chn->frame_buffers = NULL;
        }

        chn->chn_id = -1;
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    /* Allocate stream buffers (from decompilation at 0x83e10) */
    int stream_cnt = buf_count;
    int stream_size = 0x38; /* Size per stream buffer */
    size_t total_size = stream_cnt * stream_size * 7;

    void **buf_ptr = (void**)&chn->data_298[0];
    *buf_ptr = calloc(total_size, 1);
    if (*buf_ptr == NULL) {
        LOG_ENC("CreateChn: failed to allocate stream buffers");
        channel_encoder_exit(chn);
        chn->chn_id = -1;
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    /* Initialize mutex and condition for frame handling */
    pthread_mutex_init(&chn->mutex_1d8, NULL);
    pthread_cond_init(&chn->cond_1f0, NULL);

    /* Create eventfd (from decompilation at 0x840a0) */
    chn->eventfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (chn->eventfd < 0) {
        LOG_ENC("CreateChn: failed to create eventfd");
        channel_encoder_exit(chn);
        if (chn->frame_buffers != NULL) {
            free(chn->frame_buffers);
        }
        chn->chn_id = -1;
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    pthread_mutex_unlock(&encoder_mutex);

    LOG_ENC("CreateChn: chn=%d, profile=%d created successfully", encChn, attr->encAttr.profile);
    return 0;
}

/* IMP_Encoder_DestroyChn - based on decompilation at 0x85c30 */
int IMP_Encoder_DestroyChn(int encChn) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) {
        LOG_ENC("DestroyChn failed: invalid channel %d", encChn);
        return -1;
    }

    pthread_mutex_lock(&encoder_mutex);

    if (g_EncChannel[encChn].chn_id < 0) {
        LOG_ENC("DestroyChn: channel %d doesn't exist", encChn);
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    /* Stop receiving pictures if started */
    if (g_EncChannel[encChn].recv_pic_enabled) {
        pthread_mutex_unlock(&encoder_mutex);
        IMP_Encoder_StopRecvPic(encChn);
        pthread_mutex_lock(&encoder_mutex);
    }

    /* Unregister if registered */
    if (g_EncChannel[encChn].registered) {
        pthread_mutex_unlock(&encoder_mutex);
        IMP_Encoder_UnRegisterChn(encChn);
        pthread_mutex_lock(&encoder_mutex);
    }

    EncChannel *chn = &g_EncChannel[encChn];

    /* Destroy codec if it exists */
    if (chn->codec != NULL) {
        AL_Codec_Encode_Destroy(chn->codec);
        chn->codec = NULL;
    }

    /* Destroy semaphores */
    sem_destroy(&chn->sem_408);
    sem_destroy(&chn->sem_418);
    sem_destroy(&chn->sem_428);

    /* Destroy mutexes */
    pthread_mutex_destroy(&chn->mutex_450);
    pthread_mutex_destroy(&chn->mutex_438);

    /* Close event file descriptor */
    if (chn->eventfd >= 0) {
        close(chn->eventfd);
        chn->eventfd = -1;
    }

    /* Free current stream buffer if any */
    if (chn->current_stream != NULL) {
        if (chn->current_stream->codec_stream != NULL) {
            free(chn->current_stream->codec_stream);
        }
        free(chn->current_stream);
        chn->current_stream = NULL;
    }

    /* Clear the channel */
    memset(&g_EncChannel[encChn], 0, ENC_CHANNEL_SIZE);
    g_EncChannel[encChn].chn_id = -1;

    pthread_mutex_unlock(&encoder_mutex);

    LOG_ENC("DestroyChn: chn=%d", encChn);
    return 0;
}

/* IMP_Encoder_RegisterChn - based on decompilation at 0x84634 */
int IMP_Encoder_RegisterChn(int encGroup, int encChn) {
    if (encGroup < 0 || encGroup >= 6) {
        LOG_ENC("RegisterChn failed: invalid group %d", encGroup);
        return -1;
    }

    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) {
        LOG_ENC("RegisterChn failed: invalid channel %d", encChn);
        return -1;
    }

    pthread_mutex_lock(&encoder_mutex);

    /* Check if channel exists */
    if (g_EncChannel[encChn].chn_id < 0) {
        LOG_ENC("RegisterChn failed: channel %d not created", encChn);
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    /* OEM: RegisterChn does not require CreateGroup; proceed to use group slots */
    /* Find empty slot in group (max 3 channels per group) */
    EncGroup *grp = &gEncoder->groups[encGroup];

    if (grp->channels[0] == NULL) {
        grp->channels[0] = &g_EncChannel[encChn];
    } else if (grp->channels[1] == NULL) {
        grp->channels[1] = &g_EncChannel[encChn];
    } else if (grp->channels[2] == NULL) {
        grp->channels[2] = &g_EncChannel[encChn];
    } else {
        LOG_ENC("RegisterChn failed: group %d is full", encGroup);
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    grp->chn_count++;
    g_EncChannel[encChn].group_ptr = grp;
    g_EncChannel[encChn].registered = 1;

    pthread_mutex_unlock(&encoder_mutex);

    LOG_ENC("RegisterChn: grp=%d, chn=%d", encGroup, encChn);
    return 0;
}

/* IMP_Encoder_UnRegisterChn - based on decompilation at 0x84838 */
int IMP_Encoder_UnRegisterChn(int encChn) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) {
        LOG_ENC("UnRegisterChn failed: invalid channel %d", encChn);
        return -1;
    }

    pthread_mutex_lock(&encoder_mutex);

    if (g_EncChannel[encChn].chn_id < 0) {
        LOG_ENC("UnRegisterChn failed: channel %d not created", encChn);
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    EncGroup *grp = (EncGroup*)g_EncChannel[encChn].group_ptr;

    if (grp == NULL) {
        LOG_ENC("UnRegisterChn: channel %d not registered", encChn);
        pthread_mutex_unlock(&encoder_mutex);
        return 0;
    }

    /* Remove from group */
    if (grp->channels[0] == &g_EncChannel[encChn]) {
        grp->channels[0] = NULL;
    } else if (grp->channels[1] == &g_EncChannel[encChn]) {
        grp->channels[1] = NULL;
    } else if (grp->channels[2] == &g_EncChannel[encChn]) {
        grp->channels[2] = NULL;
    }

    grp->chn_count--;
    g_EncChannel[encChn].registered = 0;

    pthread_mutex_unlock(&encoder_mutex);

    LOG_ENC("UnRegisterChn: chn=%d", encChn);
    return 0;
}

/* IMP_Encoder_StartRecvPic - based on decompilation at 0x849c8 */
int IMP_Encoder_StartRecvPic(int encChn) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) {
        LOG_ENC("StartRecvPic failed: invalid channel %d", encChn);
        return -1;
    }

    pthread_mutex_lock(&encoder_mutex);

    if (g_EncChannel[encChn].chn_id < 0) {
        LOG_ENC("StartRecvPic failed: channel %d not created", encChn);
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    /* Set flags at offsets 0x404 and 0x400 */
    g_EncChannel[encChn].recv_pic_started = 1;
    g_EncChannel[encChn].recv_pic_enabled = 1;

    pthread_mutex_unlock(&encoder_mutex);

    LOG_ENC("StartRecvPic: chn=%d", encChn);
    return 0;
}

/* IMP_Encoder_StopRecvPic - based on decompilation at 0x8591c */
int IMP_Encoder_StopRecvPic(int encChn) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) {
        LOG_ENC("StopRecvPic failed: invalid channel %d", encChn);
        return -1;
    }

    pthread_mutex_lock(&encoder_mutex);

    if (g_EncChannel[encChn].chn_id < 0) {
        LOG_ENC("StopRecvPic failed: channel %d not created", encChn);
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    /* Clear flag at offset 0x400 */
    g_EncChannel[encChn].recv_pic_enabled = 0;

    /* Drain the pipeline - wait for pending frames to complete */
    /* Give threads time to finish processing current frames */
    pthread_mutex_unlock(&encoder_mutex);

    /* Wait up to 100ms for pipeline to drain */
    for (int i = 0; i < 10; i++) {
        usleep(10000); /* 10ms */

        /* Check if threads have stopped processing */
        pthread_mutex_lock(&encoder_mutex);
        int still_processing = g_EncChannel[encChn].recv_pic_started;
        pthread_mutex_unlock(&encoder_mutex);

        if (!still_processing) {
            break;
        }
    }

    pthread_mutex_lock(&encoder_mutex);

    LOG_ENC("StopRecvPic: chn=%d", encChn);
    return 0;
}

int IMP_Encoder_GetStream(int encChn, IMPEncoderStream *stream, int block) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS || stream == NULL) {
        return -1;
    }

    EncChannel *chn = &g_EncChannel[encChn];

    /* Check if channel is registered */
    if (chn->chn_id < 0) {
        LOG_ENC("GetStream: channel %d not registered", encChn);
        return -1;
    }

    /* Check if recv_pic is started */
    if (!chn->recv_pic_started) {
        return 2; /* No stream available */
    }

    /* Wait for stream availability if blocking */
    if (block) {
        /* Wait on semaphore with timeout */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1; /* 1 second timeout */

        if (sem_timedwait(&chn->sem_408, &ts) < 0) {
            return -1; /* Timeout or error */
        }
    } else {
        /* Try to get semaphore without blocking */
        if (sem_trywait(&chn->sem_408) < 0) {
            return -1; /* No stream available */
        }
    }

    /* Lock mutex to access current_stream */
    pthread_mutex_lock(&chn->mutex_450);

    if (chn->current_stream == NULL) {
        pthread_mutex_unlock(&chn->mutex_450);
        return -1;
    }

    StreamBuffer *stream_buf = chn->current_stream;

    /* Populate IMPEncoderStream structure */
    stream->pack = &stream_buf->pack;
    stream->packCount = 1;
    stream->seq = stream_buf->seq;
    stream->streamEnd = stream_buf->streamEnd;

    /* Log using internal buffer to avoid dereferencing app-provided pointer */
    LOG_ENC("GetStream: returning stream seq=%u, length=%u",
            stream_buf->seq, stream_buf->pack.length);

    /* Keep stream_buf in current_stream until ReleaseStream is called */
    /* Don't set current_stream to NULL here */

    pthread_mutex_unlock(&chn->mutex_450);

    return 0;
}

int IMP_Encoder_ReleaseStream(int encChn, IMPEncoderStream *stream) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS || stream == NULL) {
        return -1;
    }

    EncChannel *chn = &g_EncChannel[encChn];

    /* Check if channel is registered */
    if (chn->chn_id < 0) {
        return -1;
    }

    LOG_ENC("ReleaseStream: chn=%d, seq=%u", encChn, stream->seq);

    /* Lock mutex to access current_stream */
    pthread_mutex_lock(&chn->mutex_450);

    if (chn->current_stream != NULL) {
        StreamBuffer *stream_buf = chn->current_stream;

        /* Release codec stream back to codec */
        if (chn->codec != NULL && stream_buf->codec_stream != NULL) {
            AL_Codec_Encode_ReleaseStream(chn->codec, stream_buf->codec_stream, NULL);
        }

        /* Free injected buffer if we allocated one */
        if (stream_buf->injected_buf) {
            free(stream_buf->injected_buf);
            stream_buf->injected_buf = NULL;
        }
        /* Free stream buffer */
        free(stream_buf);
        chn->current_stream = NULL;

        LOG_ENC("ReleaseStream: freed stream buffer");
    }

    pthread_mutex_unlock(&chn->mutex_450);

    return 0;
}

int IMP_Encoder_PollingStream(int encChn, uint32_t timeoutMsec) {
    /* Disabled noisy log - always returns timeout */
    (void)encChn;
    (void)timeoutMsec;
    return -1;
}

int IMP_Encoder_Query(int encChn, IMPEncoderCHNStat *stat) {
    if (stat == NULL) return -1;
    LOG_ENC("Query: chn=%d", encChn);
    memset(stat, 0, sizeof(*stat));
    return 0;
}

int IMP_Encoder_RequestIDR(int encChn) {
    LOG_ENC("RequestIDR: chn=%d", encChn);

    /* Request IDR frame from software encoder */
    extern void HW_Encoder_RequestIDR(void);
    HW_Encoder_RequestIDR();

    return 0;
}

int IMP_Encoder_FlushStream(int encChn) {
    LOG_ENC("FlushStream: chn=%d", encChn);
    return 0;
}

int IMP_Encoder_SetDefaultParam(IMPEncoderChnAttr *attr, IMPEncoderProfile profile,
                                 IMPEncoderRcMode rcMode, int width, int height,
                                 int fpsNum, int fpsDen, int gopLen, int gopMode,
                                 int quality, int bitrate) {
    if (attr == NULL) return -1;
    LOG_ENC("SetDefaultParam: %dx%d, %d/%d fps, profile=%d, rc=%d",
            width, height, fpsNum, fpsDen, profile, rcMode);

    memset(attr, 0, sizeof(*attr));
    attr->encAttr.profile = profile;

    /* Set basic parameters based on profile */
    if (profile == IMP_ENC_PROFILE_JPEG) {
        attr->encAttr.attrJpeg.maxPicWidth = width;
        attr->encAttr.attrJpeg.maxPicHeight = height;
        attr->encAttr.attrJpeg.bufSize = width * height * 2;
    } else {
        attr->encAttr.attrH264.maxPicWidth = width;
        attr->encAttr.attrH264.maxPicHeight = height;
        attr->encAttr.attrH264.bufSize = width * height * 2;
        attr->encAttr.attrH264.profile = profile;
    }

    attr->rcAttr.attrRcMode.rcMode = rcMode;
    attr->rcAttr.outFrmRate.frmRateNum = fpsNum;
    attr->rcAttr.outFrmRate.frmRateDen = fpsDen;
    attr->rcAttr.attrGop.gopLength = gopLen;

    return 0;
}

int IMP_Encoder_GetChnAttr(int encChn, IMPEncoderChnAttr *attr) {
    if (attr == NULL) return -1;
    LOG_ENC("GetChnAttr: chn=%d", encChn);
    memset(attr, 0, sizeof(*attr));
    return 0;
}

int IMP_Encoder_SetJpegeQl(int encChn, IMPEncoderJpegeQl *attr) {
    if (attr == NULL) return -1;
    LOG_ENC("SetJpegeQl: chn=%d", encChn);
    return 0;
}

int IMP_Encoder_SetbufshareChn(int srcChn, int dstChn) {
    LOG_ENC("SetbufshareChn: src=%d, dst=%d", srcChn, dstChn);
    return 0;
}

int IMP_Encoder_SetFisheyeEnableStatus(int encChn, int enable) {
    LOG_ENC("SetFisheyeEnableStatus: chn=%d, enable=%d", encChn, enable);
    return 0;
}

int IMP_Encoder_GetFd(int encChn) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) {
        return -1;
    }

    EncChannel *chn = &g_EncChannel[encChn];
    if (chn->chn_id < 0) {
        return -1;
    }

    LOG_ENC("GetFd: chn=%d, fd=%d", encChn, chn->eventfd);
    return chn->eventfd;
}

/* Additional Encoder Functions - based on Binary Ninja decompilations */

int IMP_Encoder_SetChnQp(int encChn, IMPEncoderQp *qp) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) {
        LOG_ENC("SetChnQp failed: invalid channel %d", encChn);
        return -1;
    }

    EncChannel *chn = &g_EncChannel[encChn];
    if (chn->chn_id < 0) {
        LOG_ENC("SetChnQp failed: channel %d not created", encChn);
        return -1;
    }

    pthread_mutex_lock(&chn->mutex_450);

    /* Call codec SetQp if codec is active */
    int ret = 0;
    if (chn->codec != NULL) {
        ret = AL_Codec_Encode_SetQp(chn->codec, qp);
    }

    pthread_mutex_unlock(&chn->mutex_450);

    LOG_ENC("SetChnQp: chn=%d, ret=%d", encChn, ret);
    return ret;
}

int IMP_Encoder_SetChnGopLength(int encChn, int gopLength) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) {
        LOG_ENC("SetChnGopLength failed: invalid channel %d", encChn);
        return -1;
    }

    EncChannel *chn = &g_EncChannel[encChn];
    if (chn->chn_id < 0) {
        LOG_ENC("SetChnGopLength failed: channel %d not created", encChn);
        return -1;
    }

    pthread_mutex_lock(&chn->mutex_450);
    /* Store GOP length at offset 0x3d0 from channel base */
    chn->gop_length = gopLength;
    pthread_mutex_unlock(&chn->mutex_450);

    LOG_ENC("SetChnGopLength: chn=%d, gop=%d", encChn, gopLength);
    return 0;
}

int IMP_Encoder_SetChnEntropyMode(int encChn, int mode) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) {
        LOG_ENC("SetChnEntropyMode failed: invalid channel %d", encChn);
        return -1;
    }

    EncChannel *chn = &g_EncChannel[encChn];
    if (chn->chn_id >= 0) {
        LOG_ENC("SetChnEntropyMode failed: channel %d already created", encChn);
        return -1;
    }

    /* Store entropy mode at offset 0x3fc from channel base */
    chn->entropy_mode = mode;

    LOG_ENC("SetChnEntropyMode: chn=%d, mode=%d", encChn, mode);
    return 0;
}

int IMP_Encoder_SetMaxStreamCnt(int encChn, int cnt) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) {
        LOG_ENC("SetMaxStreamCnt failed: invalid channel %d", encChn);
        return -1;
    }

    EncChannel *chn = &g_EncChannel[encChn];
    if (chn->chn_id >= 0) {
        LOG_ENC("SetMaxStreamCnt failed: channel %d already created", encChn);
        return -1;
    }

    /* Store max stream count at offset 0x4c0 from channel base */
    chn->max_stream_cnt = cnt;

    LOG_ENC("SetMaxStreamCnt: chn=%d, cnt=%d", encChn, cnt);
    return 0;
}

int IMP_Encoder_SetStreamBufSize(int encChn, int size) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) {
        LOG_ENC("SetStreamBufSize failed: invalid channel %d", encChn);
        return -1;
    }

    EncChannel *chn = &g_EncChannel[encChn];
    if (chn->chn_id >= 0) {
        LOG_ENC("SetStreamBufSize failed: channel %d already created", encChn);
        return -1;
    }

    /* Store stream buffer size at offset 0x4c4 from channel base */
    chn->stream_buf_size = size;

    LOG_ENC("SetStreamBufSize: chn=%d, size=%d", encChn, size);
    return 0;
}

/* ========== Helper Functions ========== */

/* channel_encoder_init - based on decompilation at 0x8098c */
static int channel_encoder_init(EncChannel *chn) {
    if (chn == NULL) {
        return -1;
    }

    /* Set default codec parameters */
    uint8_t codec_params[0x7c0];
    memset(codec_params, 0, sizeof(codec_params));

    /* Create codec instance */
    if (AL_Codec_Encode_Create(&chn->codec, codec_params) < 0 || chn->codec == NULL) {
        LOG_ENC("channel_encoder_init: AL_Codec_Encode_Create failed");
        return -1;
    }

    /* Get source frame count and size */
    if (AL_Codec_Encode_GetSrcFrameCntAndSize(chn->codec, &chn->src_frame_cnt, &chn->src_frame_size) < 0) {
        LOG_ENC("channel_encoder_init: GetSrcFrameCntAndSize failed");
        AL_Codec_Encode_Destroy(chn->codec);
        return -1;
    }

    LOG_ENC("channel_encoder_init: frame_cnt=%d, frame_size=%d",
            chn->src_frame_cnt, chn->src_frame_size);

    /* Allocate frame buffers */
    size_t buf_size = chn->src_frame_cnt * 0x458; /* 0x458 bytes per frame buffer */
    chn->frame_buffers = calloc(buf_size, 1);
    if (chn->frame_buffers == NULL) {
        LOG_ENC("channel_encoder_init: failed to allocate frame buffers");
        AL_Codec_Encode_Destroy(chn->codec);
        return -1;
    }

    /* Initialize fifo */
    Fifo_Init(chn->fifo, chn->src_frame_cnt);

    /* Queue all buffers to fifo */
    for (int i = 0; i < chn->src_frame_cnt; i++) {
        void *buf = (uint8_t*)chn->frame_buffers + (i * 0x458);
        Fifo_Queue(chn->fifo, buf, -1);
    }

    /* Create encoder thread (from decompilation at 0x80b40) */
    if (pthread_create(&chn->thread_encoder, NULL, encoder_thread, chn) < 0) {
        LOG_ENC("channel_encoder_init: failed to create encoder thread");
        Fifo_Deinit(chn->fifo);
        free(chn->frame_buffers);
        AL_Codec_Encode_Destroy(chn->codec);
        return -1;
    }

    /* Create stream thread (from decompilation at 0x80b60) */
    if (pthread_create(&chn->thread_stream, NULL, stream_thread, chn) < 0) {
        LOG_ENC("channel_encoder_init: failed to create stream thread");
        pthread_cancel(chn->thread_encoder);
        pthread_join(chn->thread_encoder, NULL);
        Fifo_Deinit(chn->fifo);
        free(chn->frame_buffers);
        AL_Codec_Encode_Destroy(chn->codec);
        return -1;
    }

    return 0;
}

/* channel_encoder_exit - based on decompilation at 0x806e8 */
static int channel_encoder_exit(EncChannel *chn) {
    if (chn == NULL || chn->codec == NULL) {
        return -1;
    }

    /* Cancel threads */
    if (chn->thread_encoder) {
        pthread_cancel(chn->thread_encoder);
        pthread_join(chn->thread_encoder, NULL);
    }

    if (chn->thread_stream) {
        pthread_cancel(chn->thread_stream);
        pthread_join(chn->thread_stream, NULL);
    }

    /* Cleanup fifo */
    Fifo_Deinit(chn->fifo);

    /* Free buffers */
    if (chn->frame_buffers) {
        free(chn->frame_buffers);
        chn->frame_buffers = NULL;
    }

    /* Destroy codec */
    AL_Codec_Encode_Destroy(chn->codec);
    chn->codec = NULL;

    return 0;
}

/* channel_encoder_set_rc_param - Convert rate control parameters */
static int channel_encoder_set_rc_param(void *dst, IMPEncoderRcAttr *src) {
    if (dst == NULL || src == NULL) {
        return -1;
    }

    /* Convert IMPEncoderRcAttr to internal codec format
     * The dst pointer points to codec_param structure
     * We need to map the rate control parameters to the correct offsets */

    uint8_t *codec_param = (uint8_t*)dst;

    /* Set rate control mode at offset 0x2c */
    uint32_t rc_mode = 0;
    switch (src->attrRcMode.rcMode) {
        case IMP_ENC_RC_MODE_FIXQP:
            rc_mode = 0;
            break;
        case IMP_ENC_RC_MODE_CBR:
            rc_mode = 1;
            break;
        case IMP_ENC_RC_MODE_VBR:
            rc_mode = 2;
            break;
        case IMP_ENC_RC_MODE_CAPPED_VBR:
            rc_mode = 3;
            break;
        case IMP_ENC_RC_MODE_CAPPED_QUALITY:
            rc_mode = 4;
            break;
        default:
            rc_mode = 1; /* Default to CBR */
            break;
    }
    memcpy(codec_param + 0x2c, &rc_mode, sizeof(uint32_t));

    /* Set QP values */
    uint32_t max_qp = 51, min_qp = 0;
    if (src->attrRcMode.rcMode == IMP_ENC_RC_MODE_CBR) {
        max_qp = src->attrRcMode.attrH264Cbr.maxQp;
        min_qp = src->attrRcMode.attrH264Cbr.minQp;
    } else if (src->attrRcMode.rcMode == IMP_ENC_RC_MODE_VBR) {
        max_qp = src->attrRcMode.attrH264Vbr.maxQp;
        min_qp = src->attrRcMode.attrH264Vbr.minQp;
    } else if (src->attrRcMode.rcMode == IMP_ENC_RC_MODE_FIXQP) {
        uint32_t qp = src->attrRcMode.attrH264FixQp.qp;
        memcpy(codec_param + 0x38, &qp, sizeof(uint32_t));
        max_qp = qp;
        min_qp = qp;
    }

    memcpy(codec_param + 0x3c, &max_qp, sizeof(uint32_t));
    memcpy(codec_param + 0x40, &min_qp, sizeof(uint32_t));

    /* Set GOP length */
    uint32_t gop_len = src->attrGop.gopLength;
    memcpy(codec_param + 0x44, &gop_len, sizeof(uint32_t));

    LOG_ENC("set_rc_param: mode=%d, qp_range=[%u,%u], gop=%u",
            rc_mode, min_qp, max_qp, gop_len);

    return 0;
}

/* Encoder thread - processes frames */
static void *encoder_thread(void *arg) {
    EncChannel *chn = (EncChannel*)arg;

    if (chn == NULL) {
        LOG_ENC("encoder_thread: NULL channel pointer!");
        return NULL;
    }

    LOG_ENC("encoder_thread: started for channel %d", chn->chn_id);

    /* Main encoding loop */
    while (1) {
        /* Check for thread cancellation */
        pthread_testcancel();

        /* Wait for recv_pic to be enabled */
        if (!chn->recv_pic_enabled) {
            usleep(10000); /* 10ms */
            continue;
        }

        /* Wait for recv_pic to be started */
        if (!chn->recv_pic_started) {
            usleep(10000); /* 10ms */
            continue;
        }

        /* Frames are received via observer pattern in encoder_update()
         * The update callback queues frames to the input FIFO
         * This thread processes frames from the FIFO */

        /* Wait for frame arrival (~30fps) */
        usleep(33000);

        /* Frames come from FrameSource via observer pattern (encoder_update)
         * They are queued to enc->fifo_input and processed here
         * The actual frame processing happens in encoder_update() which calls
         * AL_Codec_Encode_Process() with the real frame data */

        /* This thread primarily signals the stream thread via eventfd */

        /* Signal eventfd to wake up stream thread */
        if (chn->eventfd >= 0) {
            uint64_t val = 1;
            ssize_t n = write(chn->eventfd, &val, sizeof(val));
            (void)n; /* Suppress unused warning */
        }
    }

    return NULL;
}

/* Stream thread - handles stream output */
static void *stream_thread(void *arg) {
    EncChannel *chn = (EncChannel*)arg;

    if (chn == NULL) {
        LOG_ENC("stream_thread: NULL channel pointer!");
        return NULL;
    }

    LOG_ENC("stream_thread: started for channel %d", chn->chn_id);

    /* Main stream handling loop */
    while (1) {
        /* Check for thread cancellation */
        pthread_testcancel();

        /* Wait for eventfd signal from encoder thread */
        if (chn->eventfd >= 0) {
            uint64_t val;
            fd_set readfds;
            struct timeval tv;

            FD_ZERO(&readfds);
            FD_SET(chn->eventfd, &readfds);
            tv.tv_sec = 0;
            tv.tv_usec = 100000; /* 100ms timeout */

            int ret = select(chn->eventfd + 1, &readfds, NULL, NULL, &tv);
            if (ret > 0 && FD_ISSET(chn->eventfd, &readfds)) {
                ssize_t n = read(chn->eventfd, &val, sizeof(val));
                (void)n; /* Suppress unused warning */
            }
        }

        /* Get encoded stream from codec */
        if (chn->codec != NULL) {
            void *codec_stream = NULL;
            if (AL_Codec_Encode_GetStream(chn->codec, &codec_stream) == 0 && codec_stream != NULL) {
                /* Validate stream pointer - must be a reasonable heap address */
                uintptr_t stream_addr = (uintptr_t)codec_stream;
                if (stream_addr < 0x10000) {
                    LOG_ENC("stream_thread: invalid stream pointer %p (too small)", codec_stream);
                    continue;
                }

                LOG_ENC("stream_thread: got stream %p", codec_stream);

                /* Create StreamBuffer structure */
                StreamBuffer *stream_buf = (StreamBuffer*)calloc(1, sizeof(StreamBuffer));
                if (stream_buf != NULL) {
                    /* codec_stream is actually a HWStreamBuffer pointer */
                    /* Extract metadata from HWStreamBuffer structure:
                     * 0x00: phys_addr
                     * 0x04: virt_addr
                     * 0x08: length
                     * 0x0c: timestamp (64-bit)
                     * 0x14: frame_type
                     * 0x18: slice_type
                     */
                    uint8_t *hw_stream = (uint8_t*)codec_stream;
                    uint32_t phys_addr, virt_addr, length, frame_type, slice_type;
                    uint64_t timestamp;

                    memcpy(&phys_addr, hw_stream + 0x00, sizeof(uint32_t));
                    memcpy(&virt_addr, hw_stream + 0x04, sizeof(uint32_t));
                    memcpy(&length, hw_stream + 0x08, sizeof(uint32_t));
                    memcpy(&timestamp, hw_stream + 0x0c, sizeof(uint64_t));
                    memcpy(&frame_type, hw_stream + 0x14, sizeof(uint32_t));
                    memcpy(&slice_type, hw_stream + 0x18, sizeof(uint32_t));


                        /* Prepare for optional H.264 SPS/PPS prefix injection */
                        uint32_t out_phy = phys_addr;
                        uint32_t out_vir = virt_addr;
                        uint32_t out_len = length;
                        stream_buf->injected_buf = NULL;
                        int is_h264 = (chn->attr.encAttr.profile <= IMP_ENC_PROFILE_AVC_HIGH);
                        if (is_h264) {
                            const uint8_t *orig_ptr = (const uint8_t*)(uintptr_t)virt_addr;
                            size_t inj_len_sz = 0;
                            uint8_t *inj = inject_prefix_if_needed(chn->chn_id, 1, orig_ptr, (size_t)length, &inj_len_sz);
                            if (inj) {
                                out_phy = 0;
                                out_vir = (uint32_t)(uintptr_t)inj;
                                out_len = (uint32_t)inj_len_sz;
                                stream_buf->injected_buf = inj;
                            }
                        }

                    /* Initialize stream buffer */
                    stream_buf->codec_stream = codec_stream;
                    stream_buf->seq = chn->stream_seq++;
                    stream_buf->streamEnd = 0;

                    /* Populate pack data (possibly using injected buffer) */
                    stream_buf->pack.phyAddr = out_phy;
                    stream_buf->pack.virAddr = out_vir;
                    stream_buf->pack.length = out_len;
                    stream_buf->pack.timestamp = timestamp;
                    stream_buf->pack.h264RefType = (frame_type == 0) ? 1 : 0; /* I-frame = ref */
                    stream_buf->pack.sliceType = slice_type;

                    /* Store in channel for GetStream */
                    pthread_mutex_lock(&chn->mutex_450);
                    if (chn->current_stream != NULL) {
                        /* Free old stream if not retrieved */
                        free(chn->current_stream);
                    }
                    chn->current_stream = stream_buf;
                    pthread_mutex_unlock(&chn->mutex_450);

                    /* Signal semaphore that stream is available */
                    sem_post(&chn->sem_408);

                    LOG_ENC("stream_thread: stream seq=%u, length=%u, type=%s",
                            stream_buf->seq, length,
                            frame_type == 0 ? "I" : (frame_type == 1 ? "P" : "B"));
                }
            }
        } else {
            /* No codec yet, just wait */
            usleep(10000); /* 10ms */
        }
    }

    return NULL;
}

/* ========== Module Binding Functions ========== */

/**
 * encoder_update - Called by observer pattern when a frame is available
 * This is the callback at offset 0x4c in the Module structure
 *
 * Called by notify_observers() when FrameSource has a new frame
 * The frame pointer is passed via the observer structure
 */
static int encoder_update(void *module, void *frame) {
    if (module == NULL) {
        LOG_ENC("encoder_update: NULL module pointer!");
        return -1;
    }

    if (frame == NULL) {
        LOG_ENC("encoder_update: NULL frame pointer!");
        return -1;
    }

    LOG_ENC("encoder_update: Frame available from FrameSource, frame=%p", frame);

    /* Find the encoder channel associated with this module */
    /* In a full implementation, we would:
     * 1. Extract channel info from module structure
     * 2. Queue frame to encoder thread via codec FIFO
     * 3. Signal encoder thread that frame is available
     */

    int frame_processed = 0;

    /* For now, we'll process the frame directly in the first active channel */
    for (int i = 0; i < MAX_ENC_CHANNELS; i++) {
        EncChannel *chn = &g_EncChannel[i];

        if (chn->chn_id >= 0 && chn->recv_pic_started && chn->codec != NULL) {
            /* Queue frame to codec for encoding */
            if (AL_Codec_Encode_Process(chn->codec, frame, NULL) == 0) {
                LOG_ENC("encoder_update: Queued frame to channel %d", i);

                /* Signal eventfd to wake up stream thread */
                if (chn->eventfd >= 0) {
                    uint64_t val = 1;
                    ssize_t n = write(chn->eventfd, &val, sizeof(val));
                    (void)n;
                }

                frame_processed = 1;
                break;  /* Only process frame in one channel */
            }
        }
    }

    /* Release the frame back to VBM pool so it can be reused
     * This is critical for software frame generation mode where frames
     * are allocated from a limited pool and must be recycled. */
    extern int IMP_FrameSource_ReleaseFrame(int chnNum, void *frame);

    /* Extract channel number from module - for now we'll try both channels */
    for (int chn = 0; chn < 2; chn++) {
        if (IMP_FrameSource_ReleaseFrame(chn, frame) == 0) {
            LOG_ENC("encoder_update: Released frame %p back to channel %d", frame, chn);
            break;
        }
    }

    return frame_processed ? 0 : -1;
}

/* ========== Stub Implementations for External Functions ========== */

