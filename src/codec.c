/**
 * AL_Codec Implementation
 * Based on reverse engineering of libimp.so v1.1.6
 * Decompiled from addresses 0x7950c (Create), 0x7a180 (Destroy), etc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <imp/imp_encoder.h>
#include <imp/imp_system.h>
#include "fifo.h"
#include "hw_encoder.h"

#include "al_avpu.h"
#include "device_pool.h"
#include "dma_alloc.h"
#define LOG_CODEC(fmt, ...) fprintf(stderr, "[Codec] " fmt "\n", ##__VA_ARGS__)
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <errno.h>

/* AVPU ioctl definitions (OEM parity - direct driver access) */
#ifndef AVPU_IOC_MAGIC
#define AVPU_IOC_MAGIC 'q'
#endif
struct avpu_reg { unsigned int id; unsigned int value; };
#define AL_CMD_IP_WRITE_REG    _IOWR(AVPU_IOC_MAGIC, 10, struct avpu_reg)
#define AL_CMD_IP_READ_REG     _IOWR(AVPU_IOC_MAGIC, 11, struct avpu_reg)

/* AVPU register offsets (from driver and BN decompilation) */
#define AVPU_BASE_OFFSET       0x8000
#define AVPU_INTERRUPT_MASK    (AVPU_BASE_OFFSET + 0x14)
#define AVPU_INTERRUPT         (AVPU_BASE_OFFSET + 0x18)
#define AVPU_REG_TOP_CTRL      (AVPU_BASE_OFFSET + 0x54)
#define AVPU_REG_MISC_CTRL     (AVPU_BASE_OFFSET + 0x10)
#define AVPU_REG_SRC_PUSH      (AVPU_BASE_OFFSET + 0x84)
#define AVPU_REG_STRM_PUSH     (AVPU_BASE_OFFSET + 0x94)
#define AVPU_REG_CL_ADDR       (AVPU_BASE_OFFSET + 0x3E0)
#define AVPU_REG_CL_PUSH       (AVPU_BASE_OFFSET + 0x3E4)
#define AVPU_REG_ENC_EN_A      (AVPU_BASE_OFFSET + 0x5F0)
#define AVPU_REG_ENC_EN_B      (AVPU_BASE_OFFSET + 0x5F4)
#define AVPU_REG_ENC_EN_C      (AVPU_BASE_OFFSET + 0x5E4)
#define AVPU_REG_AXI_ADDR_OFFSET_IP (AVPU_BASE_OFFSET + 0x1208)
static inline unsigned AVPU_CORE_BASE(int core) { return (AVPU_BASE_OFFSET + 0x3F0) + ((unsigned)core << 9); }
#define AVPU_REG_CORE_RESET(c)   (AVPU_CORE_BASE(c) + 0x00)
#define AVPU_REG_CORE_CLKCMD(c)  (AVPU_CORE_BASE(c) + 0x04)

/* Direct ioctl helpers (OEM parity - no wrapper functions) */
static int avpu_write_reg(int fd, unsigned int off, unsigned int val)
{
    if (fd < 0 || (off & 3) != 0) return -1;
    struct avpu_reg r = { .id = off, .value = val };
    return ioctl(fd, AL_CMD_IP_WRITE_REG, &r);
}

static int avpu_read_reg(int fd, unsigned int off, unsigned int *out)
{
    if (fd < 0 || (off & 3) != 0) return -1;
    struct avpu_reg r = { .id = off, .value = 0 };
    int ret = ioctl(fd, AL_CMD_IP_READ_REG, &r);
    if (ret == 0 && out) *out = r.value;
    return ret;
}

/* Fill Enc1 command registers (OEM parity: from SliceParamToCmdRegsEnc1) */
static void fill_cmd_regs_enc1(const ALAvpuContext* ctx, uint32_t* cmd)
{
    if (!ctx || !cmd) return;

    /* cmd[0]: base flags and formats */
    uint32_t c0 = 0x11 | (1u << 8) | (1u << 31); /* 4:2:0, Baseline, entry valid */
    cmd[0] = c0;

    /* cmd[1]: picture dimensions: ((h-1)<<12 | (w-1)) */
    if (ctx->enc_w && ctx->enc_h) {
        cmd[1] = (((ctx->enc_h - 1) & 0x7FF) << 12) | ((ctx->enc_w - 1) & 0x7FF);
    }

    /* cmd[2]: set fixed 0x2000 bit per OEM */
    cmd[2] = 0x2000u;

    /* cmd[3]: NAL/slice flags: IDR for first frame, else non-IDR */
    uint32_t nalu = (ctx->cl_idx == 0) ? 5u : 1u;
    cmd[3] = (nalu & 0x1Fu) | (1u << 31) | (1u << 30);

    /* cmd[4]: QP in low 5 bits */
    cmd[4] = (ctx->qp ? ctx->qp : 26) & 0x1F;

    /* cmd[7]: macroblock grid ((mb_h-1)<<12 | (mb_w-1)) */
    if (ctx->enc_w && ctx->enc_h) {
        uint32_t mb_w = (ctx->enc_w + 15) / 16;
        uint32_t mb_h = (ctx->enc_h + 15) / 16;
        cmd[7] = (((mb_h - 1) & 0x3FF) << 12) | ((mb_w - 1) & 0x3FF);
        cmd[5] = (1u << 12) | 0u; /* single slice */
        cmd[6] = (1u << 12) | 0u;
    }
}

/* Enable interrupts for core (OEM parity: AL_EncCore_EnableInterrupts) */
static void avpu_enable_interrupts(int fd, int core)
{
    unsigned m = 0;
    if (avpu_read_reg(fd, AVPU_INTERRUPT_MASK, &m) < 0) return;
    unsigned b0 = 1u << (((unsigned)core << 2) & 0x1F);
    unsigned b2 = 1u << ((((unsigned)core << 2) + 2) & 0x1F);
    unsigned new_m = m | (b0 | b2);
    if (new_m != m) avpu_write_reg(fd, AVPU_INTERRUPT_MASK, new_m);
}

/* Compute effective AnnexB stream size (trim trailing zeros) */
static size_t annexb_effective_size(const uint8_t *buf, size_t maxlen)
{
    if (!buf || maxlen < 4) return 0;
    size_t first = (size_t)-1;
    size_t last = 0;
    for (size_t i = 0; i + 3 < maxlen; ++i) {
        if ((buf[i] == 0 && buf[i+1] == 0 && buf[i+2] == 1) ||
            (i + 4 < maxlen && buf[i] == 0 && buf[i+1] == 0 && buf[i+2] == 0 && buf[i+3] == 1)) {
            if (first == (size_t)-1) first = i;
            last = i;
        }
    }
    if (first == (size_t)-1) return 0;
    size_t end = maxlen;
    for (size_t i = last + 3; i + 3 < maxlen; ++i) {
        if ((buf[i] == 0 && buf[i+1] == 0 && buf[i+2] == 1) ||
            (i + 4 < maxlen && buf[i] == 0 && buf[i+1] == 0 && buf[i+2] == 0 && buf[i+3] == 1)) {
            end = i; break;
        }
    }
    if (end == maxlen) {
        while (end > first && buf[end-1] == 0) end--;
    }
    return (end > first) ? (end - first) : 0;
}


/* Codec structure - based on decompilation at 0x7950c */
/* Size: 0x924 bytes */
typedef struct {
    void *g_pCodec;                 /* 0x000: Global codec pointer */
    uint8_t codec_param[0x794];     /* 0x004: Codec parameters */
    void *encoder;                  /* 0x798: AL_Encoder handle */
    void *event;                    /* 0x79c: Event handle */
    void *callback;                 /* 0x7a0: Callback function */
    void *callback_arg;             /* 0x7a4: Callback argument */
    int channel_id;                 /* 0x7a8: Channel ID + 1 */

    /* Stream buffer pool config - offsets from decompilation */
    int stream_buf_count;           /* 0x7ac: Stream buffer count */
    int stream_buf_size;            /* 0x7b0: Stream buffer size */
    uint8_t stream_pool[0x44];      /* 0x7b4-0x7f7: Stream buffer pool */

    /* FIFOs (control structures allocated dynamically to ensure proper size) */
    void *fifo_frames;              /* Frame FIFO control block */
    void *fifo_streams;             /* Stream FIFO control block */

    /* Frame buffer pool config */
    uint8_t frame_pool_config[0x60]; /* 0x840-0x89f: Frame pool config */
    int frame_buf_count;            /* 0x840: Frame buffer count (from GetSrcFrameCntAndSize) */
    uint8_t frame_pool_data[0x9c];  /* 0x8a0-0x8db: Frame pool data */
    int frame_buf_size;             /* 0x8dc: Frame buffer size (from GetSrcFrameCntAndSize) */

    /* Pixel map buffer pool */
    uint8_t pixmap_pool[0x3c];      /* 0x8e0-0x91b: PixMap buffer pool */
    int frame_count;                /* 0x91c: Frame count */
    int src_fourcc;                 /* 0x918: Source FourCC */
    int metadata_type;              /* 0x920: Metadata type */

    /* Extended fields (not part of binary structure) */
    int hw_encoder_fd;              /* Hardware encoder file descriptor */
    HWEncoderParams hw_params;      /* Hardware encoder parameters */
    int use_hardware;               /* Flag: 1=hardware, 0=software */
    ALAvpuContext avpu;            /* Vendor-like AL over /dev/avpu (scaffolding) */
} AL_CodecEncode;

/* Global codec state */
static void *g_pCodec = NULL;
static pthread_mutex_t g_codec_mutex = PTHREAD_MUTEX_INITIALIZER;
static AL_CodecEncode *g_codec_instances[6] = {NULL};

/* Single-owner gate for AVPU to avoid noisy second opens */
static pthread_mutex_t g_avpu_owner_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_avpu_owner_channel = 0;

/**
 * AL_Codec_Encode_SetDefaultParam - based on decompilation at 0x790b8
 * Sets default encoding parameters
 */
int AL_Codec_Encode_SetDefaultParam(void *param) {
    if (param == NULL) {
        LOG_CODEC("SetDefaultParam: NULL param");
        return -1;
    }

    /* Clear entire structure */
    memset(param, 0, 0x794);

    /* Set default values from decompilation */
    uint8_t *p = (uint8_t*)param;

    /* Basic settings */
    *(int32_t*)(p + 0x00) = 0;          /* codec type */
    *(int32_t*)(p + 0x04) = 0;          /* reserved */
    *(int32_t*)(p + 0x14) = 0x188;      /* width default */
    *(int32_t*)(p + 0x1c) = 8;          /* bit depth */
    *(int32_t*)(p + 0x20) = 0x1000001;  /* H264 codec */
    *(int32_t*)(p + 0x24) = 0x32;       /* profile (50 = high) */
    *(int32_t*)(p + 0x34) = 0x1c;       /* level */
    *(int32_t*)(p + 0x30) = 0x40000;    /* bitrate */

    /* QP settings */
    *(uint8_t*)(p + 0x38) = 0xff;       /* initial QP */
    *(uint8_t*)(p + 0x39) = 0xff;       /* min QP */
    *(uint8_t*)(p + 0x3f) = 1;          /* enable QP */
    *(uint8_t*)(p + 0x44) = 1;          /* enable rate control */

    /* GOP settings */
    *(uint16_t*)(p + 0x4e) = 0xffff;
    *(uint16_t*)(p + 0x50) = 0xffff;
    *(uint16_t*)(p + 0x4a) = 0xffff;
    *(uint16_t*)(p + 0x4c) = 0xffff;

    /* Rate control */
    *(uint8_t*)(p + 0x53) = 3;          /* RC mode */
    *(uint16_t*)(p + 0x8a) = 0xffff;
    *(uint16_t*)(p + 0x8c) = 0xffff;
    *(uint8_t*)(p + 0x55) = 2;
    *(int32_t*)(p + 0x90) = 2;
    *(uint8_t*)(p + 0x6a) = 0xf;
    *(uint16_t*)(p + 0x92) = 0xa;
    *(uint16_t*)(p + 0x94) = 0x11;

    /* Timing */
    *(int32_t*)(p + 0x7c) = 0xaae60;    /* framerate num */
    *(int32_t*)(p + 0x80) = 0xaae60;    /* framerate den */
    *(int32_t*)(p + 0x9c) = 0x1068;

    /* Slice settings */
    *(uint8_t*)(p + 0x52) = 5;
    *(uint8_t*)(p + 0x54) = 5;
    *(int32_t*)(p + 0x74) = 0x41eb0;
    *(uint16_t*)(p + 0x78) = 0x19;
    *(uint16_t*)(p + 0x7a) = 0x3e8;
    *(uint16_t*)(p + 0x84) = 0x19;
    *(uint16_t*)(p + 0x88) = 0x33;

    /* Enable flags */
    *(uint8_t*)(p + 0x56) = 1;
    *(uint8_t*)(p + 0x57) = 1;
    *(uint8_t*)(p + 0x58) = 1;
    *(uint8_t*)(p + 0x6c) = 1;

    /* GOP parameters */
    *(int32_t*)(p + 0xac) = 2;
    *(int32_t*)(p + 0xb4) = 0x7fffffff;
    *(int32_t*)(p + 0xcc) = 3;          /* buffer count */
    *(int32_t*)(p + 0x100) = 4;
    *(int32_t*)(p + 0xb0) = 0x19;       /* GOP length */
    *(int32_t*)(p + 0xe8) = 5;
    *(int32_t*)(p + 0x104) = 5;
    *(uint8_t*)(p + 0x108) = 1;
    *(uint8_t*)(p + 0x10c) = 1;
    *(uint8_t*)(p + 0x110) = 1;
    *(uint8_t*)(p + 0x116) = 1;
    *(uint8_t*)(p + 0x11c) = 1;
    *(uint8_t*)(p + 0x124) = 1;
    *(uint8_t*)(p + 0x128) = 1;

    /* Pixel format */
    strncpy((char*)(p + 0x764), "NV12", 4);
    *(uint8_t*)(p + 0x758) = 1;
    *(uint8_t*)(p + 0x760) = 1;
    *(uint8_t*)(p + 0x768) = 1;
    *(uint8_t*)(p + 0x76c) = 0x10;      /* alignment */

    /* OEM-aligned zeroing of subregions */
    memset(p + 0x12c, 0, 0x600);
    memset(p + 0x72c, 0, 0x18);
    memset(p + 0x744, 0, 8);
    memset(p + 0x74c, 0, 8);
    *(uint32_t*)(p + 0x754) = 0;
    *(uint8_t*)(p + 0x769) = 0; /* per OEM defaults */
    *(uint32_t*)(p + 0x770) = 0;
    *(uint32_t*)(p + 0x774) = 0;
    *(uint32_t*)(p + 0x778) = 0;
    *(uint32_t*)(p + 0x77c) = 0;
    *(uint32_t*)(p + 0x780) = 0;
    *(uint32_t*)(p + 0x784) = 0;

    LOG_CODEC("SetDefaultParam: initialized (OEM-aligned)");
    return 0;
}

/**
 * AL_Codec_Encode_GetSrcFrameCntAndSize - based on decompilation at 0x7a694
 * Returns frame buffer count and size
 */
int AL_Codec_Encode_GetSrcFrameCntAndSize(void *codec, int *cnt, int *size) {
    if (codec == NULL || cnt == NULL || size == NULL) {
        return -1;
    }

    AL_CodecEncode *enc = (AL_CodecEncode*)codec;

    /* From decompilation: offsets 0x840 and 0x8dc */
    *cnt = enc->frame_buf_count;
    *size = enc->frame_buf_size;

    return 0;
}

/**
 * AL_Codec_Encode_GetSrcStreamCntAndSize - based on decompilation at 0x7a6ac
 * Returns stream buffer count and size
 */
int AL_Codec_Encode_GetSrcStreamCntAndSize(void *codec, int *cnt, int *size) {
    if (codec == NULL || cnt == NULL || size == NULL) {
        return -1;
    }

    AL_CodecEncode *enc = (AL_CodecEncode*)codec;

    /* From decompilation: offsets 0x7ac and 0x7b0 */
    *cnt = enc->stream_buf_count;
    *size = enc->stream_buf_size;

    return 0;
}

/**
 * AL_Codec_Encode_Create - based on decompilation at 0x7950c
 * Creates a codec encoder instance
 */
int AL_Codec_Encode_Create(void **codec, void *params) {
    if (codec == NULL || params == NULL) {
        LOG_CODEC("Create: NULL parameters");
        return -1;
    }

    /* Allocate codec structure using real size */
    AL_CodecEncode *enc = (AL_CodecEncode*)malloc(sizeof(AL_CodecEncode));
    if (enc == NULL) {
        LOG_CODEC("Create: malloc failed");
        return -1;
    }

    memset(enc, 0, sizeof(AL_CodecEncode));

    /* Initialize from parameters */
    enc->g_pCodec = g_pCodec;
    memcpy(enc->codec_param, params, 0x794);

    /* OEM-like callback placeholders and event */
    enc->callback = NULL;
    enc->callback_arg = enc;
    int efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (efd >= 0) enc->event = (void*)(uintptr_t)efd;

    /* Set default buffer counts and sizes */
    enc->frame_buf_count = 4;           /* Default frame buffer count */
    enc->frame_buf_size = 0x100000;     /* 1MB per frame */
    enc->stream_buf_count = 7;          /* Default stream buffer count */
    enc->stream_buf_size = 0x20000;     /* 128KB stream buffer size (for encoded H.264 data) */

    /* Allocate and initialize FIFO control structures safely */
    int fifo_size = Fifo_SizeOf();
    enc->fifo_frames = malloc(fifo_size);
    enc->fifo_streams = malloc(fifo_size);
    if (enc->fifo_frames == NULL || enc->fifo_streams == NULL) {
        LOG_CODEC("Create: FIFO alloc failed");
        if (enc->fifo_frames) free(enc->fifo_frames);
        if (enc->fifo_streams) free(enc->fifo_streams);
        free(enc);
        return -1;
    }
    Fifo_Init(enc->fifo_frames, enc->frame_buf_count);
    Fifo_Init(enc->fifo_streams, enc->stream_buf_count);

    /* Set source FourCC to NV12 */
    enc->src_fourcc = 0x3231564e;  /* 'NV12' */
    enc->metadata_type = -1;

    /* Attempt to use hardware encoder via /dev/avpu (lazy-init on first frame) */
    enc->hw_encoder_fd = -1;
    enc->use_hardware = 1;

    LOG_CODEC("Create: hardware encoder will be attempted via /dev/avpu (lazy init)");

    /* Register in global instances */
    pthread_mutex_lock(&g_codec_mutex);
    for (int i = 0; i < 6; i++) {
        if (g_codec_instances[i] == NULL) {
            g_codec_instances[i] = enc;
            enc->channel_id = i + 1;
            pthread_mutex_unlock(&g_codec_mutex);

            *codec = enc;
            LOG_CODEC("Create: codec=%p, channel=%d", enc, i);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_codec_mutex);

    /* No free slots */
    Fifo_Deinit(enc->fifo_frames);
    Fifo_Deinit(enc->fifo_streams);
    free(enc->fifo_frames);
    free(enc->fifo_streams);
    free(enc);
    LOG_CODEC("Create: no free slots");
    return -1;
}

/**
 * AL_Codec_Encode_Destroy - based on decompilation at 0x7a180
 * Destroys a codec encoder instance
 */
int AL_Codec_Encode_Destroy(void *codec) {
    if (codec == NULL) {
        return -1;
    }

    AL_CodecEncode *enc = (AL_CodecEncode*)codec;

    LOG_CODEC("Destroy: codec=%p, channel=%d", codec, enc->channel_id - 1);

    /* Deinitialize hardware encoder(s) - OEM parity: no separate deinit function */
    if (enc->use_hardware == 2 && enc->avpu.fd >= 0) {
        /* Clean up stream buffers (OEM parity: direct cleanup, no wrapper) */
        for (int i = 0; i < enc->avpu.stream_bufs_used; ++i) {
            /* RMEM buffers are managed by IMP_Free, not munmap */
            enc->avpu.stream_bufs[i].map = NULL;
        }

        /* Close device via device pool (OEM parity) */
        AL_DevicePool_Close(enc->avpu.fd);
        enc->avpu.fd = -1;

        pthread_mutex_lock(&g_avpu_owner_mutex);
        if (g_avpu_owner_channel == enc->channel_id) {
            g_avpu_owner_channel = 0;
            LOG_CODEC("AVPU: released ownership by channel=%d", enc->channel_id - 1);
        }
        pthread_mutex_unlock(&g_avpu_owner_mutex);
    }
    if (enc->hw_encoder_fd >= 0) {
        HW_Encoder_Deinit(enc->hw_encoder_fd);
        enc->hw_encoder_fd = -1;
    }

    /* Unregister from global instances */
    pthread_mutex_lock(&g_codec_mutex);
    for (int i = 0; i < 6; i++) {
        if (g_codec_instances[i] == enc) {
            g_codec_instances[i] = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&g_codec_mutex);

    /* Deinitialize FIFOs */
    Fifo_Deinit(enc->fifo_frames);
    Fifo_Deinit(enc->fifo_streams);

    /* Free FIFO control blocks */
    free(enc->fifo_frames);
    free(enc->fifo_streams);

    /* Close OEM-like event if created */
    if (enc->event) {
        int efd = (int)(uintptr_t)enc->event;
        close(efd);
        enc->event = NULL;
    }

    /* Free codec structure */
    free(enc);

    return 0;
}

/**
 * AL_Codec_Encode_Process - based on decompilation at 0x7a334
 * Process a frame for encoding
 */
int AL_Codec_Encode_Process(void *codec, void *frame, void *user_data) {
    if (codec == NULL) {
        LOG_CODEC("Process: NULL codec pointer");
        return -1;
    }

    AL_CodecEncode *enc = (AL_CodecEncode*)codec;

    (void)user_data;

    if (frame == NULL) {
        /* NULL frame means flush */
        LOG_CODEC("Process: flush requested (NULL frame)");
        return 0;
    }

    /* Validate frame pointer - must be a reasonable address */
    uintptr_t frame_addr = (uintptr_t)frame;
    if (frame_addr < 0x10000) {
        LOG_CODEC("Process: invalid frame pointer %p (too small, likely corrupted)", frame);
        return -1;
    }

    /* Encode frame using hardware or software */
    HWStreamBuffer *hw_stream = (HWStreamBuffer*)malloc(sizeof(HWStreamBuffer));
    if (hw_stream == NULL) {
        LOG_CODEC("Process: failed to allocate stream buffer");
        return -1;
    }

    /* Extract frame data from VBM frame structure */
    /* VBMFrame structure layout (0x428 bytes):
     * 0x00: index
     * 0x04: chn
     * 0x08: width
     * 0x0c: height
     * 0x10: pixfmt
     * 0x14: size
     * 0x18: phys_addr
     * 0x1c: virt_addr
     * 0x20-0x427: data
     */
    uint8_t *frame_bytes = (uint8_t*)frame;
    uint32_t width, height, pixfmt, size, phys_addr, virt_addr;

    LOG_CODEC("Process: frame=%p, extracting metadata", frame);

    memcpy(&width, frame_bytes + 0x08, sizeof(uint32_t));
    memcpy(&height, frame_bytes + 0x0c, sizeof(uint32_t));
    memcpy(&pixfmt, frame_bytes + 0x10, sizeof(uint32_t));
    memcpy(&size, frame_bytes + 0x14, sizeof(uint32_t));
    memcpy(&phys_addr, frame_bytes + 0x18, sizeof(uint32_t));
    memcpy(&virt_addr, frame_bytes + 0x1c, sizeof(uint32_t));

    /* Get current timestamp */
    uint64_t timestamp = IMP_System_GetTimeStamp();

    if (enc->use_hardware) {
        /* Lazy-init hardware encoder on first frame */
        if (enc->hw_encoder_fd < 0) {
            /* Build parameters from codec_param (written by channel_encoder_init) */
            uint32_t bitrate = *(uint32_t*)(enc->codec_param + 0x30);
            uint32_t fps_num = *(uint32_t*)(enc->codec_param + 0x7c);
            uint32_t fps_den = *(uint32_t*)(enc->codec_param + 0x80);
            uint32_t gop = *(uint32_t*)(enc->codec_param + 0xb0);
            uint32_t profile_idc = *(uint32_t*)(enc->codec_param + 0x24);

            memset(&enc->hw_params, 0, sizeof(enc->hw_params));
            enc->hw_params.codec_type = HW_CODEC_H264; /* prudynt-t default */
            enc->hw_params.width = width;
            enc->hw_params.height = height;
            enc->hw_params.fps_num = fps_num ? fps_num : 25;
            enc->hw_params.fps_den = fps_den ? fps_den : 1;
            enc->hw_params.gop_length = gop ? gop : 25;
            enc->hw_params.rc_mode = HW_RC_MODE_CBR;
            enc->hw_params.bitrate = bitrate ? bitrate : 2*1000*1000;
            /* Map profile_idc to HW profile */
            switch (profile_idc) {
                case 66: enc->hw_params.profile = HW_PROFILE_BASELINE; break; /* Baseline */
                case 77: enc->hw_params.profile = HW_PROFILE_MAIN; break;     /* Main */
                case 100: enc->hw_params.profile = HW_PROFILE_HIGH; break;    /* High */
                default: enc->hw_params.profile = HW_PROFILE_MAIN; break;
            }

            /* Prefer vendor-like AL over /dev/avpu, but gate to a single owner */
            int skip_avpu = 0, current_owner = 0;
            pthread_mutex_lock(&g_avpu_owner_mutex);
            current_owner = g_avpu_owner_channel;
            if (current_owner != 0 && current_owner != enc->channel_id) {
                skip_avpu = 1;
            }
            pthread_mutex_unlock(&g_avpu_owner_mutex);

            if (!skip_avpu) {
                if (enc->avpu.fd > 2) {
                    /* Already open for this channel; do not re-open */
                    pthread_mutex_lock(&g_avpu_owner_mutex);
                    if (g_avpu_owner_channel == 0) {
                        g_avpu_owner_channel = enc->channel_id;
                    }
                    pthread_mutex_unlock(&g_avpu_owner_mutex);

                    enc->use_hardware = 2; /* 2 = AL/AVPU path */
                    /* OEM parity: no ALAvpu_SetEvent - event_fd stored directly */
                    if (enc->event) {
                        enc->avpu.event_fd = (int)(uintptr_t)enc->event;
                    }
                    LOG_CODEC("AVPU: channel=%d already open (fd=%d); skipping re-open", enc->channel_id - 1, enc->avpu.fd);
                } else {
                    /* Open device via device pool (OEM parity: AL_DevicePool_Open at 0x362dc) */
                    int fd = AL_DevicePool_Open("/dev/avpu");
                    if (fd >= 0) {
                        /* Initialize AVPU context directly (OEM parity: no ALAvpu_Init wrapper) */
                        memset(&enc->avpu, 0, sizeof(enc->avpu));
                        enc->avpu.fd = fd;
                        enc->avpu.event_fd = enc->event ? (int)(uintptr_t)enc->event : -1;

                        /* Cache encoding parameters for command-list population */
                        enc->avpu.enc_w = width;
                        enc->avpu.enc_h = height;
                        enc->avpu.fps_num = enc->hw_params.fps_num;
                        enc->avpu.fps_den = enc->hw_params.fps_den;
                        enc->avpu.profile = enc->hw_params.profile;
                        enc->avpu.rc_mode = enc->hw_params.rc_mode;
                        enc->avpu.qp = 26; /* default QP */
                        enc->avpu.gop_length = enc->hw_params.gop_length;

                        /* Allocate stream buffers via IMP_Alloc (OEM parity) */
                        enc->avpu.stream_buf_count = 4;
                        enc->avpu.stream_buf_size = 128 * 1024;
                        enc->avpu.stream_bufs_used = 0;
                        for (int i = 0; i < enc->avpu.stream_buf_count; ++i) {
                            unsigned char info[0x94];
                            memset(info, 0, sizeof(info));
                            if (IMP_Alloc((char*)info, enc->avpu.stream_buf_size, (char*)"avpu_stream") == 0) {
                                void *virt = *(void**)(info + 0x80);
                                uint32_t phys = *(uint32_t*)(info + 0x84);
                                if (virt && phys) {
                                    enc->avpu.stream_bufs[i].phy_addr = phys;
                                    enc->avpu.stream_bufs[i].map = virt;
                                    enc->avpu.stream_bufs[i].size = enc->avpu.stream_buf_size;
                                    enc->avpu.stream_bufs[i].from_rmem = 1;
                                    enc->avpu.stream_in_hw[i] = 0;
                                    memset(virt, 0, enc->avpu.stream_buf_size);
                                    ++enc->avpu.stream_bufs_used;
                                    LOG_CODEC("AVPU: stream buf[%d] phys=0x%08x size=%d", i, phys, enc->avpu.stream_buf_size);
                                }
                            }
                        }

                        /* Allocate command-list ring via IMP_Alloc (OEM parity: 0x13 entries x 512B) */
                        enc->avpu.cl_entry_size = 0x200;
                        enc->avpu.cl_count = 0x13;
                        size_t cl_bytes = enc->avpu.cl_entry_size * enc->avpu.cl_count;
                        unsigned char cl_info[0x94];
                        memset(cl_info, 0, sizeof(cl_info));
                        if (IMP_Alloc((char*)cl_info, (int)cl_bytes, (char*)"avpu_cmdlist") == 0) {
                            void *virt = *(void**)(cl_info + 0x80);
                            uint32_t phys = *(uint32_t*)(cl_info + 0x84);
                            if (virt && phys) {
                                enc->avpu.cl_ring.phy_addr = phys;
                                enc->avpu.cl_ring.map = virt;
                                enc->avpu.cl_ring.size = cl_bytes;
                                enc->avpu.cl_ring.from_rmem = 1;
                                enc->avpu.cl_idx = 0;
                                memset(virt, 0, cl_bytes);
                                LOG_CODEC("AVPU: cmdlist ring phys=0x%08x size=%zu entries=%u", phys, cl_bytes, enc->avpu.cl_count);
                            }
                        }

                        /* T31 uses absolute addressing (offset mode causes kernel crashes) */
                        enc->avpu.axi_base = 0;
                        enc->avpu.use_offsets = 0;
                        enc->avpu.session_ready = 0;
                        enc->avpu.hw_prepared = 0;

                        pthread_mutex_lock(&g_avpu_owner_mutex);
                        if (g_avpu_owner_channel == 0) {
                            g_avpu_owner_channel = enc->channel_id;
                            LOG_CODEC("AVPU: channel=%d acquired ownership", enc->channel_id - 1);
                        }
                        pthread_mutex_unlock(&g_avpu_owner_mutex);

                        enc->use_hardware = 2; /* 2 = AL/AVPU path */
                        LOG_CODEC("Process: AVPU opened fd=%d channel=%d", fd, enc->channel_id - 1);
                    } else {
                        int e = errno;
                        LOG_CODEC("Process: channel=%d AL_DevicePool_Open failed: %s", enc->channel_id - 1, strerror(e));
                        int init_fd = -1;
                        if (HW_Encoder_Init(&init_fd, &enc->hw_params) == 0 && init_fd >= 0) {
                            enc->hw_encoder_fd = init_fd;
                            enc->use_hardware = 1; /* legacy path */
                            LOG_CODEC("Process: legacy HW encoder initialized (fd=%d)", init_fd);
                        } else {
                            LOG_CODEC("Process: no hardware path available; falling back to software");
                            enc->use_hardware = 0;
                        }
                    }
                }
            } else {
                LOG_CODEC("Process: channel=%d skipping AVPU open; already owned by channel=%d", enc->channel_id - 1, current_owner - 1);
                /* Fallback: try legacy non-avpu devices via HW_Encoder_Init */
                int init_fd = -1;
                if (HW_Encoder_Init(&init_fd, &enc->hw_params) == 0 && init_fd >= 0) {
                    enc->hw_encoder_fd = init_fd;
                    enc->use_hardware = 1; /* legacy path */
                    LOG_CODEC("Process: legacy HW encoder initialized (fd=%d)", init_fd);
                } else {
                    LOG_CODEC("Process: no hardware path available; falling back to software");
                    enc->use_hardware = 0;
                }
            }
        }
    }

    if (enc->use_hardware == 1 && enc->hw_encoder_fd >= 0) {
        /* Legacy hardware path (/dev/venc, etc.) */
        HWFrameBuffer hw_frame;
        memset(&hw_frame, 0, sizeof(HWFrameBuffer));
        hw_frame.phys_addr = phys_addr;
        hw_frame.virt_addr = virt_addr;
        hw_frame.size = size;
        hw_frame.width = width;
        hw_frame.height = height;
        hw_frame.pixfmt = pixfmt;
        hw_frame.timestamp = timestamp;
        LOG_CODEC("Process: HW(lgcy) encode frame %ux%u, phys=0x%x, virt=0x%x, size=%u",
                  width, height, phys_addr, virt_addr, size);
        if (HW_Encoder_Encode(enc->hw_encoder_fd, &hw_frame) < 0) {
            LOG_CODEC("Process: legacy hardware encoding failed");
            free(hw_stream);
            return -1;
        }
        if (HW_Encoder_GetStream(enc->hw_encoder_fd, hw_stream, 100) < 0) {
            LOG_CODEC("Process: legacy HW get stream timed out");
            free(hw_stream);
            return 0; /* no stream yet */
        }
    } else if (enc->use_hardware == 2 && enc->avpu.fd >= 0) {
        /* OEM parity: Direct ioctl calls (AL_Common_Encoder_Process) - no ALAvpu_QueueFrame wrapper */
        ALAvpuContext *ctx = &enc->avpu;
        int fd = ctx->fd;

        /* Lazy-start: configure HW on first frame (OEM parity: AL_Common_Encoder_CreateChannel) */
        if (!ctx->session_ready) {
            /* Core reset FIRST */
            avpu_write_reg(fd, AVPU_REG_CORE_RESET(0), 2);
            usleep(1000);
            avpu_write_reg(fd, AVPU_REG_CORE_RESET(0), 4);

            /* Program AXI offset (write 0 for absolute mode) */
            avpu_write_reg(fd, AVPU_REG_AXI_ADDR_OFFSET_IP, 0);

            /* TOP_CTRL */
            avpu_write_reg(fd, AVPU_REG_TOP_CTRL, 0x00000080);

            /* Mask all IRQs and clear pending */
            avpu_write_reg(fd, AVPU_INTERRUPT_MASK, 0x00000000);
            unsigned pend = 0;
            if (avpu_read_reg(fd, AVPU_INTERRUPT, &pend) == 0 && pend) {
                avpu_write_reg(fd, AVPU_INTERRUPT, pend);
            }

            /* Enable encoder blocks */
            avpu_write_reg(fd, AVPU_REG_ENC_EN_A, 0x00000001);
            avpu_write_reg(fd, AVPU_REG_ENC_EN_B, 0x00000001);
            avpu_write_reg(fd, AVPU_REG_ENC_EN_C, 0x00000001);

            ctx->session_ready = 1;
            LOG_CODEC("AVPU: HW initialized for channel=%d", enc->channel_id - 1);
        }

        /* Prepare command-list entry (OEM parity: SetCommandListBuffer) */
        if (ctx->cl_ring.phy_addr && ctx->cl_ring.map && ctx->cl_entry_size) {
            uint32_t idx = ctx->cl_idx % ctx->cl_count;
            uint8_t* entry = (uint8_t*)ctx->cl_ring.map + (size_t)idx * ctx->cl_entry_size;
            memset(entry, 0, ctx->cl_entry_size);
            uint32_t* cmd = (uint32_t*)entry;

            /* Fill Enc1 command registers */
            fill_cmd_regs_enc1(ctx, cmd);

            /* SetClockCommand: read-modify-write on CORE_CLKCMD */
            unsigned clk = 0;
            avpu_read_reg(fd, AVPU_REG_CORE_CLKCMD(0), &clk);
            avpu_write_reg(fd, AVPU_REG_CORE_CLKCMD(0), clk ^ 0x3u);

            /* Program CL start and push (OEM parity: direct ioctl) */
            uint32_t cl_phys = ctx->cl_ring.phy_addr + (idx * ctx->cl_entry_size);
            avpu_write_reg(fd, AVPU_REG_CL_ADDR, cl_phys);
            avpu_write_reg(fd, AVPU_REG_CL_PUSH, 0x00000002);

            /* Push source frame address (OEM parity: direct ioctl) */
            avpu_write_reg(fd, AVPU_REG_SRC_PUSH, phys_addr);

            /* Unmask interrupts */
            avpu_enable_interrupts(fd, 0);

            /* Ensure at least one STRM buffer is queued */
            int any_in_hw = 0;
            for (int i = 0; i < ctx->stream_bufs_used; ++i) {
                if (ctx->stream_in_hw[i]) { any_in_hw = 1; break; }
            }
            if (!any_in_hw && ctx->stream_bufs_used > 0) {
                avpu_write_reg(fd, AVPU_REG_STRM_PUSH, ctx->stream_bufs[0].phy_addr);
                ctx->stream_in_hw[0] = 1;
            }

            /* Advance to next CL slot */
            ctx->cl_idx = (idx + 1) % ctx->cl_count;

            LOG_CODEC("Process: AVPU queued frame %ux%u phys=0x%x CL[%u]", width, height, phys_addr, idx);
        }

        /* Do not dequeue here; GetStream() will handle stream retrieval */
        free(hw_stream);
        return 0;
    } else {
        /* Software fallback */
        HWFrameBuffer hw_frame;
        memset(&hw_frame, 0, sizeof(HWFrameBuffer));
        hw_frame.phys_addr = phys_addr;
        hw_frame.virt_addr = virt_addr;
        hw_frame.width = width;
        hw_frame.height = height;
        hw_frame.timestamp = timestamp;
        LOG_CODEC("Process: SW encode frame %ux%u", width, height);
        if (HW_Encoder_Encode_Software(&hw_frame, hw_stream) < 0) {
            LOG_CODEC("Process: software encoding failed");
            free(hw_stream);
            return -1;
        }
    }

    /* Queue encoded stream to FIFO */
    if (Fifo_Queue(enc->fifo_streams, hw_stream, -1) == 0) {
        LOG_CODEC("Process: failed to queue stream");
        free(hw_stream);
        return -1;
    }

    LOG_CODEC("Process: encoded and queued stream, length=%u", hw_stream->length);
    return 0;
}

/**
 * AL_Codec_Encode_GetStream - based on decompilation at 0x7a548
 * Get an encoded stream
 */
int AL_Codec_Encode_GetStream(void *codec, void **stream, void **user_data) {
    if (codec == NULL || stream == NULL || user_data == NULL) {
        return -1;
    }

    AL_CodecEncode *enc = (AL_CodecEncode*)codec;

    /* No metadata in openimp path; match libimp by providing a separate pointer */
    *user_data = NULL;

    if (enc->use_hardware == 2 && enc->avpu.fd >= 0) {
        /* OEM parity: Direct stream buffer checking (no ALAvpu_DequeueStream wrapper) */
        ALAvpuContext *ctx = &enc->avpu;

        if (!ctx->session_ready) {
            errno = EAGAIN;
            return -1;
        }

        /* Poll for encoded stream with short sleep (OEM parity: no userspace IRQ wait) */
        struct timespec nap = {0, 5000000L}; /* 5ms */
        for (int retry = 0; retry < 20; ++retry) {
            nanosleep(&nap, NULL);

            /* Check stream buffers for valid AnnexB data */
            for (int i = 0; i < ctx->stream_bufs_used; ++i) {
                if (ctx->stream_in_hw[i]) {
                    const uint8_t *virt = (const uint8_t*)ctx->stream_bufs[i].map;
                    size_t eff = annexb_effective_size(virt, ctx->stream_buf_size);
                    if (eff > 0) {
                        /* Found valid stream */
                        HWStreamBuffer *s = (HWStreamBuffer*)malloc(sizeof(HWStreamBuffer));
                        if (!s) return -1;
                        s->phys_addr = ctx->stream_bufs[i].phy_addr;
                        s->virt_addr = (uint32_t)(uintptr_t)virt;
                        s->length = (uint32_t)eff;
                        s->timestamp = 0;
                        s->frame_type = 0;
                        s->slice_type = 0;
                        ctx->stream_in_hw[i] = 0; /* mark as dequeued */
                        *stream = s;
                        LOG_CODEC("GetStream[AVPU]: got stream phys=0x%x len=%u", s->phys_addr, s->length);
                        return 0;
                    }
                }
            }
        }

        /* Timeout */
        errno = EAGAIN;
        return -1;
    }

    /* Legacy/SW path: dequeue from our FIFO (wait indefinitely) */
    void *s = Fifo_Dequeue(enc->fifo_streams, -1);
    if (s == NULL) {
        return -1;
    }

    *stream = s;
    LOG_CODEC("GetStream: got stream %p", s);
    return 0;
}

/**
 * AL_Codec_Encode_ReleaseStream - based on decompilation at 0x7a624
 * Release an encoded stream
 */
int AL_Codec_Encode_ReleaseStream(void *codec, void *stream, void *user_data) {
    if (codec == NULL || stream == NULL) {
        return -1;
    }

    AL_CodecEncode *enc = (AL_CodecEncode*)codec;

    if (enc->use_hardware == 2 && enc->avpu.fd >= 0) {
        /* OEM parity: Direct ioctl to return buffer (no ALAvpu_ReleaseStream wrapper) */
        HWStreamBuffer *hw_stream = (HWStreamBuffer*)stream;
        ALAvpuContext *ctx = &enc->avpu;
        (void)user_data;

        if (ctx->session_ready) {
            /* Return buffer to hardware via direct ioctl */
            for (int i = 0; i < ctx->stream_bufs_used; ++i) {
                if (ctx->stream_bufs[i].phy_addr == hw_stream->phys_addr) {
                    avpu_write_reg(ctx->fd, AVPU_REG_STRM_PUSH, hw_stream->phys_addr);
                    ctx->stream_in_hw[i] = 1;
                    LOG_CODEC("ReleaseStream[AVPU]: requeued stream buf[%d] phys=0x%x", i, hw_stream->phys_addr);
                    break;
                }
            }
        }

        free(hw_stream);
        return 0;
    }

    /* Legacy/SW path follows libimp semantics (no refcounts) */
    if (user_data != NULL) {
        (void)user_data;
    }

    HWStreamBuffer *hw_stream = (HWStreamBuffer*)stream;
    LOG_CODEC("ReleaseStream: freed stream %p", stream);
    if (hw_stream->virt_addr != 0 && hw_stream->phys_addr == 0) {
        /* Software-encoded stream - free the allocated buffer */
        void *data_ptr = (void*)(uintptr_t)hw_stream->virt_addr;
        free(data_ptr);
        LOG_CODEC("ReleaseStream: freed software-encoded data at %p", data_ptr);
    }
    free(hw_stream);
    return 0;
}

/**
 * AL_Codec_Encode_SetQp - Set QP (Quantization Parameter)
 * Based on decompilation pattern
 */
int AL_Codec_Encode_SetQp(void *codec, void *qp) {
    if (codec == NULL || qp == NULL) {
        LOG_CODEC("SetQp: NULL parameter");
        return -1;
    }

    AL_CodecEncode *enc = (AL_CodecEncode*)codec;

    /* In a real implementation, this would configure the encoder's QP settings */
    /* For now, just log and return success */
    LOG_CODEC("SetQp: codec=%p, qp=%p", codec, qp);

    return 0;
}

