/**
 * IMP Encoder Module Implementation
 * Based on reverse engineering of libimp.so v1.1.6
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/eventfd.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdarg.h>
#include <fcntl.h>
#include <imp/imp_encoder.h>
#include <imp/imp_system.h>
#include "core/module.h"
#include "fifo.h"
#include "codec.h"
#include "kernel_interface.h"

/* Internal core module helpers used by the ported build. */
extern Module *AllocModule(char *name, int32_t arg2);
extern Module *g_modules[6][6];

#include "imp_log_int.h"
#define FIFO_SIZE 64  /* Size of Fifo structure */

static void enc_kmsg(const char *fmt, ...)
{
    int fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    if (fd < 0)
        return;

    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if (n < 0) {
        close(fd);
        return;
    }

    dprintf(fd, "libimp/ENCW: %s\n", msg);
    close(fd);
}

/* Encoder channel structure - 0x308 bytes per channel */
#define MAX_ENC_CHANNELS 9
#define ENC_CHANNEL_SIZE 0x308
#define MAX_ENC_GROUPS 3
#define MAX_CHANNELS_PER_GROUP MAX_ENC_CHANNELS

/* Internal stream buffer structure */
typedef struct {
    /* Legacy single-pack (kept for internal debug compatibility) */
    IMPEncoderPack pack;
    /* New: array of packs to mirror OEM libimp semantics */
    IMPEncoderPack *packs;      /* Dynamically allocated array of packs for this frame */
    uint32_t packCount;         /* Number of packs in 'packs' */
    uint32_t seq;               /* Sequence number */
    int streamEnd;              /* Stream end flag */
    void *codec_stream;         /* Pointer to codec stream data */
    void *codec_user_data;      /* Returned metadata associated with codec_stream */
    void *injected_buf;         /* If non-NULL, malloc'd buffer we must free */
    /* Base addresses for the full frame buffer (for IMPEncoderStream top-level) */
    uint32_t base_phy;          /* Physical base address */
    uint32_t base_vir;          /* Virtual base address */
    uint32_t base_size;         /* Total available bytes at base_vir */
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
    int resize_mode;               /* T31 resize-mode flag */
    int qp_ip_delta;               /* T31 IP delta cache */
    int last_qp;                   /* Last fixed QP applied */
    int bufshare_chn;              /* OEM SetbufshareChn(encChn, shareChn) cache */
    /* OpenIMP tracking to ensure early IDR without GetStream-time injection */
    int idr_requested_once;        /* 0=no request yet, 1=requested due to missing SPS/PPS */
    int param_sets_seen;           /* 1 once SPS and PPS observed in packs */
    int fisheye_enable;            /* Fisheye enable status (OEM offset 0x4c8) */
    void *frame_release_cb;        /* Frame release callback (OEM offset 0xFC) */
    void *frame_release_arg;       /* Frame release callback argument (OEM offset 0x100) */
    uint8_t enc_type;              /* Encoding type: 0=H264, 1=H265, 2=JPEG (OEM offset 0x2B) */
    void *pending_frame;           /* Async frame handed from encoder_update -> encoder_thread */
} EncChannel;

/* Encoder group structure */
typedef struct {
    int group_id;               /* 0x00: Group ID */
    uint32_t field_04;          /* 0x04: Field */
    uint32_t chn_count;         /* 0x08: Number of registered channels */
    EncChannel *channels[MAX_CHANNELS_PER_GROUP];
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

static void enc_group_clear_slots(EncGroup *grp)
{
    if (grp == NULL) return;
    for (int i = 0; i < MAX_CHANNELS_PER_GROUP; ++i) {
        grp->channels[i] = NULL;
    }
}

static uint32_t enc_group_count_slots(const EncGroup *grp)
{
    if (grp == NULL) return 0;

    uint32_t count = 0;
    for (int i = 0; i < MAX_CHANNELS_PER_GROUP; ++i) {
        if (grp->channels[i] != NULL) {
            count++;
        }
    }
    return count;
}

static int enc_group_has_channels(const EncGroup *grp)
{
    if (grp == NULL) return 0;
    for (int i = 0; i < MAX_CHANNELS_PER_GROUP; ++i) {
        if (grp->channels[i] != NULL) return 1;
    }
    return 0;
}

static uint32_t imp_encoder_profile_type(IMPEncoderProfile profile)
{
    return ((uint32_t)profile >> 24) & 0xffu;
}

static uint32_t imp_encoder_attr_width(const IMPEncoderCHNAttr *attr)
{
    return attr->encAttr.maxPicWidth;
}

static uint32_t imp_encoder_attr_height(const IMPEncoderCHNAttr *attr)
{
    return attr->encAttr.maxPicHeight;
}

static uint32_t imp_encoder_rc_target_bitrate_kbps(const IMPEncoderRcAttr *rc)
{
    switch (rc->attrRcMode.rcMode) {
    case IMP_ENC_RC_MODE_CBR:
        return rc->attrRcMode.attrCbr.uTargetBitRate;
    case IMP_ENC_RC_MODE_VBR:
        return rc->attrRcMode.attrVbr.uTargetBitRate;
    case IMP_ENC_RC_MODE_CAPPED_VBR:
        return rc->attrRcMode.attrCappedVbr.uTargetBitRate;
    case IMP_ENC_RC_MODE_CAPPED_QUALITY:
        return rc->attrRcMode.attrCappedQuality.uTargetBitRate;
    default:
        return 0;
    }
}

static uint32_t imp_encoder_rc_max_bitrate_kbps(const IMPEncoderRcAttr *rc)
{
    switch (rc->attrRcMode.rcMode) {
    case IMP_ENC_RC_MODE_CBR:
        return rc->attrRcMode.attrCbr.uTargetBitRate;
    case IMP_ENC_RC_MODE_VBR:
        return rc->attrRcMode.attrVbr.uMaxBitRate;
    case IMP_ENC_RC_MODE_CAPPED_VBR:
        return rc->attrRcMode.attrCappedVbr.uMaxBitRate;
    case IMP_ENC_RC_MODE_CAPPED_QUALITY:
        return rc->attrRcMode.attrCappedQuality.uMaxBitRate;
    default:
        return 0;
    }
}

static uint32_t imp_encoder_rc_min_qp(const IMPEncoderRcAttr *rc)
{
    switch (rc->attrRcMode.rcMode) {
    case IMP_ENC_RC_MODE_CBR:
        return (uint32_t)rc->attrRcMode.attrCbr.iMinQP;
    case IMP_ENC_RC_MODE_VBR:
        return (uint32_t)rc->attrRcMode.attrVbr.iMinQP;
    case IMP_ENC_RC_MODE_CAPPED_VBR:
        return (uint32_t)rc->attrRcMode.attrCappedVbr.iMinQP;
    case IMP_ENC_RC_MODE_CAPPED_QUALITY:
        return (uint32_t)rc->attrRcMode.attrCappedQuality.iMinQP;
    case IMP_ENC_RC_MODE_FIXQP:
    default:
        return (uint32_t)rc->attrRcMode.attrFixQp.iInitialQP;
    }
}

static uint32_t imp_encoder_rc_max_qp(const IMPEncoderRcAttr *rc)
{
    switch (rc->attrRcMode.rcMode) {
    case IMP_ENC_RC_MODE_CBR:
        return (uint32_t)rc->attrRcMode.attrCbr.iMaxQP;
    case IMP_ENC_RC_MODE_VBR:
        return (uint32_t)rc->attrRcMode.attrVbr.iMaxQP;
    case IMP_ENC_RC_MODE_CAPPED_VBR:
        return (uint32_t)rc->attrRcMode.attrCappedVbr.iMaxQP;
    case IMP_ENC_RC_MODE_CAPPED_QUALITY:
        return (uint32_t)rc->attrRcMode.attrCappedQuality.iMaxQP;
    case IMP_ENC_RC_MODE_FIXQP:
    default:
        return (uint32_t)rc->attrRcMode.attrFixQp.iInitialQP;
    }
}

static uint8_t imp_encoder_default_level(uint32_t codec_type)
{
    switch (codec_type) {
    case IMP_ENC_TYPE_AVC:
        return 0x33;
    case IMP_ENC_TYPE_HEVC:
    case IMP_ENC_TYPE_JPEG:
    default:
        return 0;
    }
}

static uint32_t imp_encoder_default_pic_format(uint32_t codec_type)
{
    (void)codec_type;
    return 0x188;
}

static uint32_t imp_encoder_default_enc_options(uint32_t codec_type)
{
    (void)codec_type;
    return 0x40028;
}

static uint32_t imp_encoder_default_enc_tools(uint32_t codec_type)
{
    (void)codec_type;
    return 0x9c;
}

static int imp_encoder_rc_mode_valid(IMPEncoderRcMode rc_mode)
{
    switch (rc_mode) {
    case IMP_ENC_RC_MODE_FIXQP:
    case IMP_ENC_RC_MODE_CBR:
    case IMP_ENC_RC_MODE_VBR:
    case IMP_ENC_RC_MODE_CAPPED_VBR:
    case IMP_ENC_RC_MODE_CAPPED_QUALITY:
        return 1;
    default:
        return 0;
    }
}

static uint32_t imp_encoder_codec_rc_mode(IMPEncoderRcMode rc_mode)
{
    switch (rc_mode) {
    case IMP_ENC_RC_MODE_FIXQP:
        return 0;
    case IMP_ENC_RC_MODE_CBR:
        return 1;
    case IMP_ENC_RC_MODE_VBR:
    case IMP_ENC_RC_MODE_CAPPED_VBR:
    case IMP_ENC_RC_MODE_CAPPED_QUALITY:
        return 2;
    default:
        return UINT32_MAX;
    }
}

static uint32_t imp_encoder_default_qp(uint32_t codec_type, int qp)
{
    if (qp > 0)
        return (uint32_t)qp;
    return (codec_type == IMP_ENC_TYPE_JPEG) ? 75u : 35u;
}

static uint32_t imp_encoder_default_bitrate_kbps(int bitrate)
{
    return (bitrate > 0) ? (uint32_t)bitrate : 2000u;
}

static void imp_encoder_apply_jpeg_codec_defaults(uint8_t *codec_params)
{
    if (codec_params == NULL)
        return;

    *(uint16_t*)(codec_params + 0x4e) = 4;
    *(uint16_t*)(codec_params + 0x82) = 1;
    *(uint16_t*)(codec_params + 0x84) = 0x64;
    *(uint32_t*)(codec_params + 0xa8) = 2;
    *(uint32_t*)(codec_params + 0xac) = 1;
    *(uint16_t*)(codec_params + 0xae) = 0;
    *(uint32_t*)(codec_params + 0xfc) = 4;
}

/* Forward declarations for helper functions */
static int channel_encoder_init(EncChannel *chn);
static int channel_encoder_exit(EncChannel *chn);
static int channel_encoder_set_rc_param(void *dst, IMPEncoderRcAttr *src);
static void *encoder_thread(void *arg);
static void *stream_thread(void *arg);
static int encoder_update(void *module, void *frame);

#define OEM_FRAME_CLONE_BYTES 0x30
#define OEM_FRAME_SLOT_BYTES  0x458

static void *encoder_acquire_frame_slot(EncChannel *chn)
{
    if (chn == NULL) return NULL;
    return Fifo_Dequeue(chn->fifo, 0);
}

static void encoder_release_frame_slot(EncChannel *chn, void *slot)
{
    if (chn == NULL || slot == NULL) return;
    Fifo_Queue(chn->fifo, slot, -1);
}

static void encoder_unlock_and_release_frame(EncChannel *chn, void *slot)
{
    uint32_t vaddr = 0;

    if (chn == NULL || slot == NULL)
        return;

    memcpy(&vaddr, (uint8_t *)slot + 0x1c, sizeof(vaddr));
    if (vaddr != 0)
        VBMUnlockFrameByVaddr(vaddr);
    encoder_release_frame_slot(chn, slot);
}

static int encoder_clone_source_frame(EncChannel *chn, void *src_frame, void **slot_out)
{
    if (slot_out == NULL) return -1;
    *slot_out = NULL;
    if (chn == NULL || src_frame == NULL) return -1;

    void *slot = encoder_acquire_frame_slot(chn);
    if (slot == NULL) {
        LOG_ENC("encoder_update: no free encoder frame slot for chn=%d", chn->chn_id);
        return -1;
    }

    memset(slot, 0, OEM_FRAME_SLOT_BYTES);
    memcpy(slot, src_frame, OEM_FRAME_CLONE_BYTES);

    uint32_t vaddr = 0;
    memcpy(&vaddr, (uint8_t*)slot + 0x1c, sizeof(vaddr));
    if (VBMLockFrameByVaddr(vaddr) < 0) {
        LOG_ENC("encoder_update: failed to lock source frame vaddr=0x%x", vaddr);
        encoder_release_frame_slot(chn, slot);
        return -1;
    }

    *slot_out = slot;
    return 0;
}
/* H.264 SPS/PPS caching and minimal Annex B parsing for prefix injection */
#define MAX_PARAM_SET_SIZE 256
static uint8_t g_last_sps[MAX_ENC_CHANNELS][MAX_PARAM_SET_SIZE];
static int g_last_sps_len[MAX_ENC_CHANNELS];
static uint8_t g_last_pps[MAX_ENC_CHANNELS][MAX_PARAM_SET_SIZE];
static int g_last_pps_len[MAX_ENC_CHANNELS];
/* Warmup: force AUD+SPS+PPS injection for first few AUs per channel */
static int g_inject_warmup[MAX_ENC_CHANNELS] = {0};


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
/* Minimal bit writer helpers for synthesized SPS/PPS */
static void wb_bit(uint8_t *b, int *bp, int v){ int byte=*bp/8, off=7-(*bp%8); if(v) b[byte]|=(1<<off); else b[byte]&=~(1<<off); (*bp)++; }
static void wb_bits(uint8_t *b, int *bp, uint32_t val, int n){ for(int i=n-1;i>=0;i--) wb_bit(b,bp,(val>>i)&1); }
static void wb_ue(uint8_t *b, int *bp, uint32_t v){ uint32_t code=v+1; int k=0; for(uint32_t t=code; t; t>>=1) k++; int leading_zero_bits = k-1; for(int i=0;i<leading_zero_bits;i++) wb_bit(b,bp,0); wb_bits(b,bp,code, k); }

/* Build very minimal Baseline SPS (Annex B, startcode included). Matches 42e01f */
static int synth_h264_sps(uint8_t *out, int width, int height){
    int pos=0; out[pos++]=0; out[pos++]=0; out[pos++]=0; out[pos++]=1; out[pos++]=0x67; /* SPS */
    memset(out+pos,0,128); int bp=pos*8;
    wb_bits(out,&bp,66,8);       /* profile_idc = 66 (Baseline) */
    wb_bits(out,&bp,0xE0,8);     /* constraint flags */
    wb_bits(out,&bp,0x1F,8);     /* level_idc = 31 */
    wb_ue(out,&bp,0);            /* seq_parameter_set_id */
    wb_ue(out,&bp,0);            /* log2_max_frame_num_minus4 */
    wb_ue(out,&bp,0);            /* pic_order_cnt_type */
    wb_ue(out,&bp,0);            /* log2_max_pic_order_cnt_lsb_minus4 */
    wb_ue(out,&bp,1);            /* max_num_ref_frames (1) */
    wb_bit(out,&bp,0);           /* gaps_in_frame_num_value_allowed_flag */
    uint32_t w_mbs = (width + 15) / 16; uint32_t h_mbs = (height + 15) / 16;
    wb_ue(out,&bp, w_mbs - 1);   /* pic_width_in_mbs_minus1 */
    wb_ue(out,&bp, h_mbs - 1);   /* pic_height_in_map_units_minus1 (frame-only) */
    wb_bit(out,&bp,1);           /* frame_mbs_only_flag */
    wb_bit(out,&bp,1);           /* direct_8x8_inference_flag */
    wb_bit(out,&bp,0);           /* frame_cropping_flag */
    wb_bit(out,&bp,0);           /* vui_parameters_present_flag (omit) */
    wb_bit(out,&bp,1);           /* rbsp_stop_one_bit */
    while (bp % 8) wb_bit(out,&bp,0);
    return bp/8;
}

/* Build minimal PPS (Annex B, startcode included) */
static int synth_h264_pps(uint8_t *out){
    int pos=0; out[pos++]=0; out[pos++]=0; out[pos++]=0; out[pos++]=1; out[pos++]=0x68; /* PPS */
    memset(out+pos,0,64); int bp=pos*8;
    wb_ue(out,&bp,0);            /* pic_parameter_set_id */
    wb_ue(out,&bp,0);            /* seq_parameter_set_id */
    wb_bit(out,&bp,0);           /* entropy_coding_mode_flag = CAVLC */
    wb_bit(out,&bp,0);           /* bottom_field_pic_order_in_frame_present_flag */
    wb_ue(out,&bp,0);            /* num_slice_groups_minus1 */
    wb_ue(out,&bp,0);            /* num_ref_idx_l0_default_active_minus1 */
    wb_ue(out,&bp,0);            /* num_ref_idx_l1_default_active_minus1 */
    wb_bit(out,&bp,0);           /* weighted_pred_flag */
    wb_bits(out,&bp,0,2);        /* weighted_bipred_idc */
    wb_ue(out,&bp,0);            /* pic_init_qp_minus26 */
    wb_ue(out,&bp,0);            /* pic_init_qs_minus26 */
    wb_ue(out,&bp,0);            /* chroma_qp_index_offset */
    wb_bit(out,&bp,0);           /* deblocking_filter_control_present_flag */
    wb_bit(out,&bp,0);           /* constrained_intra_pred_flag */
    wb_bit(out,&bp,0);           /* redundant_pic_cnt_present_flag */
    wb_bit(out,&bp,1);           /* rbsp_stop_one_bit */
    while (bp % 8) wb_bit(out,&bp,0);
    return bp/8;
}

static int first_vcl_type(const uint8_t *buf, size_t len){ size_t i=0; int sc=0; i=find_start_code(buf,0,len,&sc); while(i<len){ size_t ns=i+(sc?sc:0); int sc2=0; size_t nx=find_start_code(buf,ns,len,&sc2); if(ns<nx){ uint8_t t=buf[ns]&0x1F; if(t==1||t==5) return t; } if(nx>=len) break; i=nx; sc=sc2; } return 0; }

static int build_aud_simple(uint8_t *out, int is_idr){ int pos=0; out[pos++]=0; out[pos++]=0; out[pos++]=0; out[pos++]=1; out[pos++]=0x09; /* AUD */ uint8_t rbsp= (is_idr?0:1)<<5; out[pos++]=rbsp; out[pos++]=0x80; return pos; }


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
    /* If SPS/PPS already present before first VCL, do nothing (OEM-compatible). Otherwise inject unconditionally. */
    if (has_sps_pps_before_vcl(buf, len)) return NULL;

    /* Determine if first VCL is IDR to choose AUD primary_pic_type */
    int vcl_t = first_vcl_type(buf, len);
    int is_idr = (vcl_t == 5);

    /* Gather SPS/PPS: prefer cached, otherwise synthesize minimal from channel attrs */
    uint8_t sps_local[256], pps_local[128];
    const uint8_t *sps = NULL, *pps = NULL; int sps_len = 0, pps_len = 0;
    if (g_last_sps_len[ch] > 0 && g_last_pps_len[ch] > 0) {
        sps = g_last_sps[ch]; sps_len = g_last_sps_len[ch];
        pps = g_last_pps[ch]; pps_len = g_last_pps_len[ch];
    } else {
        /* Synthesize minimal SPS/PPS (Annex B) */
        int width = 0, height = 0;
        if (g_EncChannel[ch].chn_id >= 0) {
            width = (int)g_EncChannel[ch].attr.encAttr.maxPicWidth;
            height = (int)g_EncChannel[ch].attr.encAttr.maxPicHeight;
        }
        if (width <= 0 || height <= 0) {
            /* Fallback to common defaults to avoid crash */
            width = 640; height = 360;
        }
        sps_len = synth_h264_sps(sps_local, width, height);
        pps_len = synth_h264_pps(pps_local);
        sps = sps_local; pps = pps_local;
    }

    /* Optionally prepend AUD for better compatibility */
    uint8_t aud[8]; int aud_len = build_aud_simple(aud, is_idr);

    size_t add = (size_t)aud_len + (size_t)sps_len + (size_t)pps_len;
    uint8_t *out = (uint8_t*)malloc(add + len);
    if (!out) return NULL;
    size_t o = 0;
    memcpy(out + o, aud, aud_len); o += aud_len;
    memcpy(out + o, sps, sps_len); o += sps_len;
    memcpy(out + o, pps, pps_len); o += pps_len;
    if (g_inject_warmup[ch] > 0) { g_inject_warmup[ch]--; }

    memcpy(out + o, buf, len); o += len;
    *out_len = o;
    return out;
}


/* Initialize encoder module */
static void encoder_init(void) {
    if (encoder_initialized) return;

    /* Initialize all channels as unused */
    for (int i = 0; i < MAX_ENC_CHANNELS; i++) {
        memset(&g_EncChannel[i], 0, sizeof(g_EncChannel[i]));
        g_EncChannel[i].chn_id = -1;
        g_EncChannel[i].entropy_mode = -1;
        g_EncChannel[i].bufshare_chn = -1;

        /* Register module with system for each channel */
        Module *module = g_modules[DEV_ID_ENC][i];
        if (module != NULL) {
            module->update_fn = (int32_t (*)(struct Module *, void *))encoder_update;

            LOG_ENC("Registered update callback for ENC channel %d", i);
            enc_kmsg("EncoderInit registered callback chn=%d module=%p", i, module);
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
            enc_group_clear_slots(&gEncoder->groups[i]);
        }
    }
    pthread_mutex_unlock(&encoder_mutex);
    return 0;
}

/* EncoderExit - OEM at 0x825c4: tears down encoder subsystem */
int EncoderExit(void) {
    pthread_mutex_lock(&encoder_mutex);
    if (gEncoder != NULL) {
        free(gEncoder);
        gEncoder = NULL;
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

    /* OEM: gEncoder must already exist from EncoderInit; error if NULL */
    if (gEncoder == NULL) {
        LOG_ENC("CreateGroup: gEncoder is NULL, EncoderInit not called");
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    /* Check if group already exists */
    if (gEncoder->groups[encGroup].group_id >= 0) {
        LOG_ENC("CreateGroup: group %d already exists", encGroup);
        pthread_mutex_unlock(&encoder_mutex);
        return 0;
    }

    /* Initialize the group. Preserve channels that may have been registered
     * before CreateGroup(), but recompute the visible channel count from the
     * current slot table so stale counts do not leak across lifecycles. */
    gEncoder->groups[encGroup].group_id = encGroup;
    gEncoder->groups[encGroup].chn_count = enc_group_count_slots(&gEncoder->groups[encGroup]);

    /* Register Encoder module with system (DEV_ID_ENC = 1) */
    /* Allocate a proper Module structure for this encoder group */
    Module *enc_module = AllocModule("Encoder", 0);
    if (enc_module == NULL) {
        LOG_ENC("CreateGroup: Failed to allocate module");
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    enc_module->channel = encGroup;
    enc_module->update_fn = (int32_t (*)(struct Module *, void *))encoder_update;
    *((uint32_t *)((char *)enc_module + 0x134)) = 1;
    g_modules[DEV_ID_ENC][encGroup] = enc_module;
    LOG_ENC("CreateGroup: registered Encoder module [1,%d] with 1 output and update callback", encGroup);
    enc_kmsg("CreateGroup registered grp=%d module=%p", encGroup, enc_module);

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
    if (enc_group_has_channels(&gEncoder->groups[encGroup])) {
        LOG_ENC("DestroyGroup failed: group %d still has channels", encGroup);
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    /* Destroy the group */
    gEncoder->groups[encGroup].group_id = -1;
    gEncoder->groups[encGroup].chn_count = 0;
    enc_group_clear_slots(&gEncoder->groups[encGroup]);

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

    if (encChn == 0 || encChn == 1) {
        const uint32_t *w = (const uint32_t *)attr;
        enc_kmsg("CreateChn raw chn=%d attr=%p sizeof=%zu", encChn, (void *)attr, sizeof(*attr));
        enc_kmsg("CreateChn raw0 chn=%d w0=%08x w1=%08x w2=%08x w3=%08x w4=%08x w5=%08x w6=%08x",
                 encChn, w[0], w[1], w[2], w[3], w[4], w[5], w[6]);
        enc_kmsg("CreateChn raw1 chn=%d w7=%08x w8=%08x w9=%08x w10=%08x w11=%08x w12=%08x w13=%08x",
                 encChn, w[7], w[8], w[9], w[10], w[11], w[12], w[13]);
        enc_kmsg("CreateChn raw2 chn=%d prof=0x%x lvl=%u tier=%u width=%u height=%u picfmt=0x%x opts=0x%x tools=0x%x rc=%u fps=%u/%u gopMode=%u gopLen=%u sameScene=%u ivdc=%u",
                 encChn,
                 attr->encAttr.profile,
                 attr->encAttr.level,
                 attr->encAttr.tier,
                 attr->encAttr.maxPicWidth,
                 attr->encAttr.maxPicHeight,
                 attr->encAttr.picFormat,
                 attr->encAttr.encOptions,
                 attr->encAttr.encTools,
                 attr->rcAttr.attrRcMode.rcMode,
                 attr->rcAttr.outFrmRate.frmRateNum,
                 attr->rcAttr.outFrmRate.frmRateDen,
                 attr->gopAttr.uGopCtrlMode,
                 attr->gopAttr.uGopLength,
                 attr->gopAttr.uMaxSameSenceCnt,
                 attr->bEnableIvdc);
    }

    pthread_mutex_lock(&encoder_mutex);

    /* Ensure encoder globals are initialized (sets chn_id = -1 for all) */
    encoder_init();

    EncChannel *chn = &g_EncChannel[encChn];

    /* Check if channel already exists */
    if (chn->chn_id >= 0) {
        LOG_ENC("CreateChn: channel %d already exists", encChn);
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    uint32_t codec_type = imp_encoder_profile_type(attr->encAttr.profile);
    int saved_entropy_mode = chn->entropy_mode;
    int saved_max_stream_cnt = chn->max_stream_cnt;
    int saved_stream_buf_size = chn->stream_buf_size;
    int saved_resize_mode = chn->resize_mode;
    int saved_qp_ip_delta = chn->qp_ip_delta;
    int saved_last_qp = chn->last_qp;
    int saved_bufshare_chn = chn->bufshare_chn;

    if (codec_type == IMP_ENC_TYPE_JPEG) {
        if (saved_bufshare_chn < 0) {
            LOG_ENC("CreateChn: Jpeg channel will not share buff");
        } else if (saved_bufshare_chn >= MAX_ENC_CHANNELS ||
                   g_EncChannel[saved_bufshare_chn].chn_id < 0 ||
                   g_EncChannel[saved_bufshare_chn].codec == NULL) {
            LOG_ENC("CreateChn failed: Jpeg channel need create after channel 0");
            pthread_mutex_unlock(&encoder_mutex);
            return -1;
        }
    }

    /* Clear the full OpenIMP channel state so stale bookkeeping does not
     * survive across channel reuse. */
    memset(chn, 0, sizeof(*chn));

    chn->entropy_mode = saved_entropy_mode;
    chn->max_stream_cnt = saved_max_stream_cnt;
    chn->stream_buf_size = saved_stream_buf_size;
    chn->resize_mode = saved_resize_mode;
    chn->qp_ip_delta = saved_qp_ip_delta;
    chn->last_qp = saved_last_qp;
    chn->bufshare_chn = saved_bufshare_chn;

    /* Copy attributes */
    memcpy(&chn->attr, attr, sizeof(IMPEncoderCHNAttr));

    /* Set encoding type from profile (OEM stores at offset 0x2B) */
    chn->enc_type = (uint8_t)imp_encoder_profile_type(attr->encAttr.profile);

    /* Set channel ID and flags */
    chn->chn_id = encChn;
    chn->registered = 0;
    chn->enabled = 0;
    chn->started = 1;
    chn->recv_pic_enabled = 0;
    chn->recv_pic_started = 1;

    /* Raptor keeps the first encoded stream buffer live long enough that a
     * single-slot default starves the second frame before userspace can
     * recycle it. Keep the fallback at two slots: enough to avoid the
     * one-frame stall, but below the 3/4-slot wedge threshold seen on live
     * T31 smoke. */
    if (chn->max_stream_cnt <= 0) {
        chn->max_stream_cnt = 2;
        enc_kmsg("CreateChn default max_stream_cnt chn=%d max=2", encChn);
    }

    /* Initialize semaphores (from decompilation at 0x83d18) */
    /* sem_init at offset 0x418 with value 0 */
    sem_init(&chn->sem_418, 0, 0);

    /* OEM CreateChn seeds the stream-manager semaphore from the cached
     * max-stream count field at channel+0x4c0. This semaphore is distinct
     * from sem_418 (GetStream wait) and sem_428 (PollingStream wait). */
    {
        unsigned int stream_slots = (chn->max_stream_cnt > 0)
            ? (unsigned int)chn->max_stream_cnt
            : 0u;
        sem_init(&chn->sem_408, 0, stream_slots);
    }

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

    /* OEM CreateChn allocates the per-channel frame-stream metadata ring as:
     *   calloc(max_stream_cnt * 0x38 * 7, 1)
     * i.e. 0x188 bytes per entry. This is NOT the encoded payload buffer size;
     * stream_buf_size controls the hardware/software bitstream buffers
     * elsewhere in the pipeline. */
    int stream_cnt = (chn->max_stream_cnt > 0) ? chn->max_stream_cnt : 4;
    const int frame_stream_meta_size = 0x188;
    size_t total_size = (size_t)stream_cnt * (size_t)frame_stream_meta_size;

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

    LOG_ENC("CreateChn: chn=%d, profile=0x%x, share=%d created successfully (frame_stream_meta=%d x %d)",
            encChn, attr->encAttr.profile, chn->bufshare_chn,
            stream_cnt, frame_stream_meta_size);
    enc_kmsg("CreateChn ok chn=%d profile=0x%x share=%d stream_cnt=%d",
             encChn, attr->encAttr.profile, chn->bufshare_chn, stream_cnt);
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

    if (chn->pending_frame != NULL) {
        encoder_unlock_and_release_frame(chn, chn->pending_frame);
        chn->pending_frame = NULL;
    }

    pthread_cond_destroy(&chn->cond_1f0);
    pthread_mutex_destroy(&chn->mutex_1d8);

    /* Free current stream buffer if any */
    if (chn->current_stream != NULL) {
        if (chn->current_stream->codec_stream != NULL) {
            free(chn->current_stream->codec_stream);
        }
        free(chn->current_stream);
        chn->current_stream = NULL;
    }

    /* Clear the channel */
    memset(&g_EncChannel[encChn], 0, sizeof(g_EncChannel[encChn]));
    g_EncChannel[encChn].chn_id = -1;
    g_EncChannel[encChn].entropy_mode = -1;

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

    /* Ensure globals are initialized so chn_id is valid */
    encoder_init();

    /* Lazily allocate gEncoder/groups if needed (vendor allows RegisterChn before CreateGroup) */
    if (gEncoder == NULL) {
        gEncoder = (EncoderState*)calloc(1, sizeof(EncoderState));
        if (gEncoder == NULL) {
            pthread_mutex_unlock(&encoder_mutex);
            return -1;
        }
        for (int i = 0; i < 6; i++) {
            gEncoder->groups[i].group_id = -1;
            gEncoder->groups[i].chn_count = 0;
            enc_group_clear_slots(&gEncoder->groups[i]);
        }
    }

    /* Check if channel exists */
    if (g_EncChannel[encChn].chn_id < 0) {
        LOG_ENC("RegisterChn failed: channel %d not created", encChn);
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    /* OEM: RegisterChn does not require CreateGroup; proceed to use group slots */
    EncGroup *grp = &gEncoder->groups[encGroup];
    int slot = -1;
    for (int i = 0; i < MAX_CHANNELS_PER_GROUP; ++i) {
        if (grp->channels[i] == &g_EncChannel[encChn]) {
            slot = i;
            break;
        }
        if (slot < 0 && grp->channels[i] == NULL) {
            slot = i;
        }
    }

    if (slot < 0) {
        LOG_ENC("RegisterChn failed: group %d is full", encGroup);
        pthread_mutex_unlock(&encoder_mutex);
        return -1;
    }

    if (grp->channels[slot] != &g_EncChannel[encChn]) {
        grp->channels[slot] = &g_EncChannel[encChn];
        grp->chn_count++;
    }

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
    for (int i = 0; i < MAX_CHANNELS_PER_GROUP; ++i) {
        if (grp->channels[i] == &g_EncChannel[encChn]) {
            grp->channels[i] = NULL;
            if (grp->chn_count > 0) {
                grp->chn_count--;
            }
            break;
        }
    }

    g_EncChannel[encChn].registered = 0;
    g_EncChannel[encChn].group_ptr = NULL;

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

    /* Stock behavior: set recv/start flags only. */
    g_EncChannel[encChn].recv_pic_started = 1;
    g_EncChannel[encChn].recv_pic_enabled = 1;

    pthread_mutex_unlock(&encoder_mutex);

    LOG_ENC("StartRecvPic: chn=%d", encChn);
    enc_kmsg("StartRecvPic chn=%d recv_started=%u recv_enabled=%u",
             encChn, g_EncChannel[encChn].recv_pic_started, g_EncChannel[encChn].recv_pic_enabled);
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

    /* OEM: if recv_pic not started and non-blocking, return 2 */
    pthread_mutex_lock(&chn->mutex_450);
    if (!chn->recv_pic_started && !block) {
        pthread_mutex_unlock(&chn->mutex_450);
        return 2;
    }
    pthread_mutex_unlock(&chn->mutex_450);

    /* Wait for stream availability — use sem_418 (GetStream semaphore).
     * OEM GetStream_Impl acquires sem_418 to get the stream permit. */
    if (block) {
        /* Blocking: wait on sem_418 */
        if (sem_wait(&chn->sem_418) < 0) {
            return -1;
        }
    } else {
        /* Non-blocking: try to acquire */
        if (sem_trywait(&chn->sem_418) < 0) {
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

    /* Populate IMPEncoderStream structure (T31 layout) */
    stream->phyAddr = stream_buf->base_phy;
    stream->virAddr = stream_buf->base_vir;
    stream->streamSize = stream_buf->base_size;
    stream->pack = stream_buf->packs ? stream_buf->packs : &stream_buf->pack;
    stream->packCount = stream_buf->packs ? stream_buf->packCount : 1;
    stream->seq = stream_buf->seq;
    stream->isVI = 0;

    /* Log using internal buffer to avoid dereferencing app-provided pointer */
    if (stream_buf->seq % 50 == 0)
    LOG_ENC("GetStream: returning stream seq=%u, packs=%u, total_len=%u",
            stream_buf->seq, stream_buf->packs ? stream_buf->packCount : 1, stream_buf->base_size);

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

    if (stream->seq % 50 == 0)
    LOG_ENC("ReleaseStream: chn=%d, seq=%u", encChn, stream->seq);

    /* Lock mutex to access current_stream */
    pthread_mutex_lock(&chn->mutex_450);

    if (chn->current_stream != NULL) {
        StreamBuffer *stream_buf = chn->current_stream;

        if (stream_buf->codec_user_data != NULL) {
            uint32_t vaddr = 0;
            memcpy(&vaddr, (uint8_t*)stream_buf->codec_user_data + 0x1c, sizeof(vaddr));
            if (VBMUnlockFrameByVaddr(vaddr) < 0) {
                LOG_ENC("ReleaseStream: WARNING - failed to unlock source frame vaddr=0x%x", vaddr);
            }
        }

        /* Release codec stream back to codec */
        if (chn->codec != NULL && stream_buf->codec_stream != NULL) {
            AL_Codec_Encode_ReleaseStream(chn->codec, stream_buf->codec_stream,
                                          stream_buf->codec_user_data);
        }

        if (stream_buf->codec_user_data != NULL) {
            encoder_release_frame_slot(chn, stream_buf->codec_user_data);
            stream_buf->codec_user_data = NULL;
        }

        /* Free injected buffer if we allocated one */
        if (stream_buf->injected_buf) {
            free(stream_buf->injected_buf);
            stream_buf->injected_buf = NULL;
        }
        /* Free packs array if allocated */
        if (stream_buf->packs) {
            free(stream_buf->packs);
            stream_buf->packs = NULL;
            stream_buf->packCount = 0;
        }
        /* Free stream buffer */
        free(stream_buf);
        chn->current_stream = NULL;
        /* Signal producer (stream_thread) that a slot is now available.
         * stream_thread waits on sem_408 when the slot is occupied. */
        sem_post(&chn->sem_408);

        /* Throttled: per-frame freed log suppressed */
    }

    pthread_mutex_unlock(&chn->mutex_450);

    return 0;
}

int IMP_Encoder_PollingStream(int encChn, uint32_t timeoutMsec) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) {
        return -1;
    }

    EncChannel *chn = &g_EncChannel[encChn];

    /* OEM checks channel created AND registered flags */
    if (chn->chn_id < 0) {
        return -1;
    }
    if (!chn->registered) {
        return -1;
    }

    /* OEM pattern from HLIL at 0x85724:
     * If timeout == 0: sem_wait (block forever)
     * If timeout > 0:  video_sem_timedwait (sem_timedwait wrapper)
     * OEM does NOT re-post — PollingStream consumes the permit,
     * and GetStream does NOT acquire it again. */
    if (timeoutMsec == 0) {
        return sem_wait(&chn->sem_428);
    }

    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) < 0) {
        return -1;
    }
    ts.tv_sec += timeoutMsec / 1000;
    ts.tv_nsec += (timeoutMsec % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
    }

    return sem_timedwait(&chn->sem_428, &ts) < 0 ? -1 : 0;
}

int IMP_Encoder_Query(int encChn, IMPEncoderCHNStat *stat) {
    if (stat == NULL) return -1;
    LOG_ENC("Query: chn=%d", encChn);
    memset(stat, 0, sizeof(*stat));
    return 0;
}

int IMP_Encoder_RequestIDR(int encChn) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) {
        return -1;
    }

    EncChannel *chn = &g_EncChannel[encChn];
    { static int idr_log = 0; if (++idr_log <= 3 || (idr_log % 50) == 0)
        LOG_ENC("RequestIDR: chn=%d [#%d]", encChn, idr_log); }

    if (chn->chn_id < 0 || chn->codec == NULL) {
        LOG_ENC("RequestIDR: channel %d not ready", encChn);
        return -1;
    }

    return AL_Codec_Encode_RequestIDR(chn->codec);
}

int IMP_Encoder_FlushStream(int encChn) {
    LOG_ENC("FlushStream: chn=%d", encChn);
    return 0;
}

int IMP_Encoder_SetDefaultParam(IMPEncoderChnAttr *attr, IMPEncoderProfile profile,
                                 IMPEncoderRcMode rcMode, int width, int height,
                                 int fpsNum, int fpsDen, int gopLen, int maxSameSceneCnt,
                                 int quality, int bitrate) {
    if (attr == NULL) {
        return -1;
    }

    uint32_t codec_type = imp_encoder_profile_type(profile);
    if (codec_type != IMP_ENC_TYPE_AVC && codec_type != IMP_ENC_TYPE_HEVC &&
        codec_type != IMP_ENC_TYPE_JPEG) {
        LOG_ENC("SetDefaultParam failed: unsupported encode type %u", codec_type);
        return -1;
    }

    if (fpsDen == 0) {
        LOG_ENC("SetDefaultParam failed: invalid fps %d/%d", fpsNum, fpsDen);
        return -1;
    }

    if (codec_type != IMP_ENC_TYPE_JPEG && !imp_encoder_rc_mode_valid(rcMode)) {
        LOG_ENC("SetDefaultParam failed: unsupported rc mode %d", rcMode);
        return -1;
    }

    memset(attr, 0, sizeof(*attr));
    attr->encAttr.profile = profile;
    attr->encAttr.level = imp_encoder_default_level(codec_type);
    attr->encAttr.tier = 0;
    attr->encAttr.picFormat = imp_encoder_default_pic_format(codec_type);
    attr->encAttr.encOptions = imp_encoder_default_enc_options(codec_type);
    attr->encAttr.encTools = imp_encoder_default_enc_tools(codec_type);

    if (codec_type == IMP_ENC_TYPE_JPEG) {
        attr->encAttr.maxPicWidth = (uint16_t)width;
        attr->encAttr.maxPicHeight = (uint16_t)height;
        attr->rcAttr.attrRcMode.rcMode = IMP_ENC_RC_MODE_FIXQP;
        attr->rcAttr.attrRcMode.attrFixQp.iInitialQP = (int16_t)imp_encoder_default_qp(codec_type, quality);
    } else {
        attr->encAttr.maxPicWidth = (uint16_t)width;
        attr->encAttr.maxPicHeight = (uint16_t)height;
        attr->rcAttr.attrRcMode.rcMode = rcMode;

        switch (rcMode) {
        case IMP_ENC_RC_MODE_FIXQP:
            attr->rcAttr.attrRcMode.attrFixQp.iInitialQP = (int16_t)imp_encoder_default_qp(codec_type, quality);
            break;
        case IMP_ENC_RC_MODE_CBR:
            attr->rcAttr.attrRcMode.attrCbr.uTargetBitRate = imp_encoder_default_bitrate_kbps(bitrate);
            attr->rcAttr.attrRcMode.attrCbr.iInitialQP = (int16_t)imp_encoder_default_qp(codec_type, quality);
            attr->rcAttr.attrRcMode.attrCbr.iMaxQP = 45;
            attr->rcAttr.attrRcMode.attrCbr.iMinQP = 15;
            attr->rcAttr.attrRcMode.attrCbr.iIPDelta = 2;
            break;
        case IMP_ENC_RC_MODE_VBR:
        case IMP_ENC_RC_MODE_CAPPED_VBR:
        case IMP_ENC_RC_MODE_CAPPED_QUALITY:
            attr->rcAttr.attrRcMode.attrVbr.uTargetBitRate = imp_encoder_default_bitrate_kbps(bitrate);
            attr->rcAttr.attrRcMode.attrVbr.uMaxBitRate = imp_encoder_default_bitrate_kbps(bitrate);
            attr->rcAttr.attrRcMode.attrVbr.iInitialQP = (int16_t)imp_encoder_default_qp(codec_type, quality);
            attr->rcAttr.attrRcMode.attrVbr.iMaxQP = 45;
            attr->rcAttr.attrRcMode.attrVbr.iMinQP = 15;
            attr->rcAttr.attrRcMode.attrVbr.iIPDelta = 2;
            break;
        default:
            break;
        }
    }

    attr->rcAttr.outFrmRate.frmRateNum = (uint32_t)fpsNum;
    attr->rcAttr.outFrmRate.frmRateDen = (uint32_t)fpsDen;
    attr->gopAttr.gopLength = (uint16_t)gopLen;
    attr->gopAttr.gopMode = IMP_ENC_GOP_CTRL_MODE_DEFAULT;
    attr->gopAttr.uMaxSameSenceCnt = (uint32_t)maxSameSceneCnt;

    LOG_ENC("SetDefaultParam: profile=0x%x type=%u %dx%d fps=%d/%d gop=%d scene=%d rc=%d quality=%d bitrate=%d",
            profile, codec_type, width, height, fpsNum, fpsDen, gopLen, maxSameSceneCnt,
            attr->rcAttr.attrRcMode.rcMode, quality, bitrate);
    return 0;
}

int IMP_Encoder_GetChnAttr(int encChn, IMPEncoderChnAttr *attr) {
    if (attr == NULL) return -1;
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) return -1;
    EncChannel *chn = &g_EncChannel[encChn];
    if (chn->chn_id < 0) {
        LOG_ENC("GetChnAttr: channel %d not registered", encChn);
        memset(attr, 0, sizeof(*attr));
        return -1;
    }
    memcpy(attr, &chn->attr, sizeof(IMPEncoderCHNAttr));
    LOG_ENC("GetChnAttr: chn=%d, %ux%u", encChn,
            attr->encAttr.maxPicWidth, attr->encAttr.maxPicHeight);
    return 0;
}

int IMP_Encoder_SetJpegeQl(int encChn, IMPEncoderJpegeQl *attr) {
    if (attr == NULL) return -1;
    LOG_ENC("SetJpegeQl: chn=%d", encChn);
    return 0;
}

int IMP_Encoder_SetbufshareChn(int encChn, int shareChn) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) {
        LOG_ENC("SetbufshareChn failed: invalid encChn %d", encChn);
        return -1;
    }
    if (shareChn < 0 || shareChn >= MAX_ENC_CHANNELS) {
        LOG_ENC("SetbufshareChn failed: invalid shareChn %d", shareChn);
        return -1;
    }

    pthread_mutex_lock(&encoder_mutex);
    encoder_init();
    g_EncChannel[encChn].bufshare_chn = shareChn;
    pthread_mutex_unlock(&encoder_mutex);

    LOG_ENC("SetbufshareChn: encChn=%d, shareChn=%d", encChn, shareChn);
    return 0;
}

int IMP_Encoder_SetFisheyeEnableStatus(int encChn, int enable) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) {
        LOG_ENC("SetFisheyeEnableStatus: Invalid Channel Num: %d", encChn);
        return -1;
    }
    g_EncChannel[encChn].fisheye_enable = enable;
    LOG_ENC("SetFisheyeEnableStatus: chn=%d, enable=%d", encChn, enable);
    return 0;
}

int IMP_Encoder_GetFisheyeEnableStatus(int encChn, int *enable) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) {
        LOG_ENC("GetFisheyeEnableStatus: Invalid Channel Num: %d", encChn);
        return -1;
    }
    *enable = (g_EncChannel[encChn].fisheye_enable > 0) ? 1 : 0;
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

static int encoder_channel_has_pending_stream(int encChn) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) {
        return 0;
    }

    EncChannel *chn = &g_EncChannel[encChn];
    if (chn->chn_id < 0 || !chn->recv_pic_started) {
        return 0;
    }

    pthread_mutex_lock(&chn->mutex_450);
    int pending = (chn->current_stream != NULL);
    pthread_mutex_unlock(&chn->mutex_450);
    return pending;
}

int IMP_Encoder_SetChnQp(int encChn, int iQP) {
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

    if (iQP < 0) iQP = 0;
    if (iQP > 51) iQP = 51;

    IMPEncoderQp qp = {
        .qp_i = (uint32_t)iQP,
        .qp_p = (uint32_t)iQP,
        .qp_b = (uint32_t)iQP,
    };
    chn->last_qp = iQP;

    /* Call codec SetQp if codec is active */
    int ret = 0;
    if (chn->codec != NULL) {
        ret = AL_Codec_Encode_SetQp(chn->codec, &qp);
    }

    pthread_mutex_unlock(&chn->mutex_450);

    LOG_ENC("SetChnQp: chn=%d, qp=%d, ret=%d", encChn, iQP, ret);
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

int IMP_Encoder_SetChnEntropyMode(int encChn, IMPEncoderEntropyMode mode) {
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

int IMP_Encoder_PollingModuleStream(uint32_t *encChnBitmap, uint32_t timeoutMsec) {
    if (encChnBitmap == NULL) return -1;

    const uint32_t sleep_step_us = 1000;
    uint32_t waited_us = 0;

    do {
        uint32_t bitmap = 0;
        for (int encChn = 0; encChn < MAX_ENC_CHANNELS && encChn < 32; encChn++) {
            if (encoder_channel_has_pending_stream(encChn)) {
                bitmap |= (1u << encChn);
            }
        }

        *encChnBitmap = bitmap;
        if (bitmap != 0 || timeoutMsec == 0) {
            return 0;
        }

        usleep(sleep_step_us);
        waited_us += sleep_step_us;
    } while (waited_us < (timeoutMsec * 1000u));

    *encChnBitmap = 0;
    return 0;
}

int IMP_Encoder_SetChnResizeMode(int encChn, int en) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) return -1;

    EncChannel *chn = &g_EncChannel[encChn];
    if (chn->chn_id >= 0) {
        LOG_ENC("SetChnResizeMode failed: channel %d already created", encChn);
        return -1;
    }

    chn->resize_mode = en ? 1 : 0;
    LOG_ENC("SetChnResizeMode: chn=%d, en=%d", encChn, chn->resize_mode);
    return 0;
}

int IMP_Encoder_GetChnEvalInfo(int encChn, void *info) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS || info == NULL) return -1;

    EncChannel *chn = &g_EncChannel[encChn];
    uint32_t *words = (uint32_t *)info;
    words[0] = chn->stream_seq;
    words[1] = (uint32_t)chn->last_qp;
    words[2] = (uint32_t)chn->stream_buf_size;
    words[3] = encoder_channel_has_pending_stream(encChn) ? 1u : 0u;

    LOG_ENC("GetChnEvalInfo: chn=%d seq=%u qp=%d pending=%u",
            encChn, words[0], chn->last_qp, words[3]);
    return 0;
}

/* ========== Missing Encoder functions needed by raptor-hal ========== */

int IMP_Encoder_GetStreamBufSize(int encChn, int *size) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS || !size) return -1;
    *size = g_EncChannel[encChn].stream_buf_size;
    return 0;
}

int IMP_Encoder_GetMaxStreamCnt(int encChn, int *cnt) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS || !cnt) return -1;
    *cnt = g_EncChannel[encChn].max_stream_cnt > 0 ? g_EncChannel[encChn].max_stream_cnt : 1;
    return 0;
}

int IMP_Encoder_SetChnFrmRate(int encChn, IMPEncoderFrmRate *in, IMPEncoderFrmRate *out) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) return -1;
    EncChannel *chn = &g_EncChannel[encChn];
    if (in) {
        chn->attr.rcAttr.outFrmRate.frmRateNum = in->frmRateNum;
        chn->attr.rcAttr.outFrmRate.frmRateDen = in->frmRateDen;
    }
    if (out) {
        out->frmRateNum = chn->attr.rcAttr.outFrmRate.frmRateNum;
        out->frmRateDen = chn->attr.rcAttr.outFrmRate.frmRateDen;
    }
    LOG_ENC("SetChnFrmRate: chn=%d", encChn);
    return 0;
}

int IMP_Encoder_GetChnFrmRate(int encChn, IMPEncoderFrmRate *in, IMPEncoderFrmRate *out) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) return -1;
    EncChannel *chn = &g_EncChannel[encChn];
    if (in) {
        in->frmRateNum = chn->attr.rcAttr.outFrmRate.frmRateNum;
        in->frmRateDen = chn->attr.rcAttr.outFrmRate.frmRateDen;
    }
    if (out) {
        out->frmRateNum = chn->attr.rcAttr.outFrmRate.frmRateNum;
        out->frmRateDen = chn->attr.rcAttr.outFrmRate.frmRateDen;
    }
    return 0;
}

int IMP_Encoder_SetChnRcAttr(int encChn, void *rcAttr) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS || !rcAttr) return -1;
    memcpy(&g_EncChannel[encChn].attr.rcAttr, rcAttr, sizeof(IMPEncoderRcAttr));
    LOG_ENC("SetChnRcAttr: chn=%d", encChn);
    return 0;
}

int IMP_Encoder_GetChnRcAttr(int encChn, void *rcAttr) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS || !rcAttr) return -1;
    memcpy(rcAttr, &g_EncChannel[encChn].attr.rcAttr, sizeof(IMPEncoderRcAttr));
    return 0;
}

int IMP_Encoder_GetChnGopAttr(int encChn, IMPEncoderGopAttr *gopAttr) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS || !gopAttr) return -1;
    memcpy(gopAttr, &g_EncChannel[encChn].attr.gopAttr, sizeof(IMPEncoderGopAttr));
    return 0;
}

int IMP_Encoder_SetChnGopAttr(int encChn, IMPEncoderGopAttr *gopAttr) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS || !gopAttr) return -1;
    memcpy(&g_EncChannel[encChn].attr.gopAttr, gopAttr, sizeof(IMPEncoderGopAttr));
    LOG_ENC("SetChnGopAttr: chn=%d, gopLen=%u", encChn, gopAttr->gopLength);
    return 0;
}

/* OEM: g_pools is a lazily-allocated array of 32 ints (0x80 bytes), init to -1.
 * Each slot maps an encoder channel to a memory pool ID. */
static int *g_enc_pools = NULL;
#define MAX_ENC_POOL_SLOTS 32

int IMP_Encoder_SetPool(int encChn, int poolId) {
    if (encChn < 0 || encChn >= MAX_ENC_POOL_SLOTS) return -1;
    /* Lazily allocate g_enc_pools */
    if (g_enc_pools == NULL) {
        g_enc_pools = (int*)malloc(MAX_ENC_POOL_SLOTS * sizeof(int));
        if (g_enc_pools == NULL) return -1;
        memset(g_enc_pools, 0xff, MAX_ENC_POOL_SLOTS * sizeof(int)); /* all -1 */
    }
    /* OEM: only set if slot is currently unbound (-1) */
    if (g_enc_pools[encChn] != -1) {
        LOG_ENC("SetPool: chn=%d already bound to pool %d", encChn, g_enc_pools[encChn]);
        return -1;
    }
    g_enc_pools[encChn] = poolId;
    LOG_ENC("SetPool: chn=%d, pool=%d", encChn, poolId);
    return 0;
}

int IMP_Encoder_GetPool(int encChn) {
    if (encChn < 0 || encChn >= MAX_ENC_POOL_SLOTS) return -1;
    if (g_enc_pools == NULL) return -1;
    return g_enc_pools[encChn];
}

int IMP_Encoder_ClearPoolId(int encChn) {
    (void)encChn;
    if (g_enc_pools != NULL) {
        free(g_enc_pools);
        g_enc_pools = NULL;
    }
    return 0;
}

/* ========== Additional missing Encoder symbols (prudynt parity) ========== */

int IMP_Encoder_SetChnBitRate(int encChn, int iTargetBitRate, int iMaxBitRate) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) {
        LOG_ENC("SetChnBitRate: Invalid Channel Num:%d", encChn);
        return -1;
    }
    EncChannel *chn = &g_EncChannel[encChn];
    if (chn->chn_id < 0) {
        LOG_ENC("SetChnBitRate: Encoder Channel%d hasn't been created", encChn);
        return -1;
    }

    /* For CBR mode, OEM forces max bitrate = target bitrate */
    int maxBR = iMaxBitRate;
    IMPEncoderRcMode rcMode = chn->attr.rcAttr.attrRcMode.rcMode;
    if (rcMode == IMP_ENC_RC_MODE_CBR) {
        maxBR = iTargetBitRate;
    }

    pthread_mutex_lock(&chn->mutex_438);
    switch (rcMode) {
    case IMP_ENC_RC_MODE_CBR:
        chn->attr.rcAttr.attrRcMode.attrCbr.uTargetBitRate = (uint32_t)iTargetBitRate;
        break;
    case IMP_ENC_RC_MODE_VBR:
        chn->attr.rcAttr.attrRcMode.attrVbr.uTargetBitRate = (uint32_t)iTargetBitRate;
        chn->attr.rcAttr.attrRcMode.attrVbr.uMaxBitRate = (uint32_t)maxBR;
        break;
    case IMP_ENC_RC_MODE_CAPPED_VBR:
        chn->attr.rcAttr.attrRcMode.attrCappedVbr.uTargetBitRate = (uint32_t)iTargetBitRate;
        chn->attr.rcAttr.attrRcMode.attrCappedVbr.uMaxBitRate = (uint32_t)maxBR;
        break;
    case IMP_ENC_RC_MODE_CAPPED_QUALITY:
        chn->attr.rcAttr.attrRcMode.attrCappedQuality.uTargetBitRate = (uint32_t)iTargetBitRate;
        chn->attr.rcAttr.attrRcMode.attrCappedQuality.uMaxBitRate = (uint32_t)maxBR;
        break;
    default:
        break;
    }
    pthread_mutex_unlock(&chn->mutex_438);
    LOG_ENC("SetChnBitRate: chn=%d, target=%d, max=%d", encChn, iTargetBitRate, maxBR);
    return 0;
}

int IMP_Encoder_SetChnQpBounds(int encChn, int minQp, int maxQp) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) {
        LOG_ENC("SetChnQpBounds: Invalid Channel Num:%d", encChn);
        return -1;
    }
    EncChannel *chn = &g_EncChannel[encChn];
    if (chn->chn_id < 0) {
        LOG_ENC("SetChnQpBounds: Encoder Channel%d hasn't been created", encChn);
        return -1;
    }

    pthread_mutex_lock(&chn->mutex_438);
    IMPEncoderRcMode rcMode = chn->attr.rcAttr.attrRcMode.rcMode;

    /* Only meaningful for non-FixQP modes */
    if (rcMode == IMP_ENC_RC_MODE_FIXQP) {
        pthread_mutex_unlock(&chn->mutex_438);
        return 0;
    }

    /* Attempt codec-level QP bounds set */
    if (chn->codec != NULL) {
        int ret = AL_Codec_Encode_SetQpBounds(chn->codec, minQp, maxQp);
        if (ret < 0) {
            pthread_mutex_unlock(&chn->mutex_438);
            LOG_ENC("SetChnQpBounds: Encoder Channel%d Codec_Encode_SetQpBounds failed", encChn);
            return -1;
        }
    }

    /* Store QP bounds in rcAttr based on mode */
    switch (rcMode) {
    case IMP_ENC_RC_MODE_CBR:
        chn->attr.rcAttr.attrRcMode.attrCbr.iMinQP = (int16_t)minQp;
        chn->attr.rcAttr.attrRcMode.attrCbr.iMaxQP = (int16_t)maxQp;
        break;
    case IMP_ENC_RC_MODE_VBR:
        chn->attr.rcAttr.attrRcMode.attrVbr.iMinQP = (int16_t)minQp;
        chn->attr.rcAttr.attrRcMode.attrVbr.iMaxQP = (int16_t)maxQp;
        break;
    case IMP_ENC_RC_MODE_CAPPED_VBR:
        chn->attr.rcAttr.attrRcMode.attrCappedVbr.iMinQP = (int16_t)minQp;
        chn->attr.rcAttr.attrRcMode.attrCappedVbr.iMaxQP = (int16_t)maxQp;
        break;
    case IMP_ENC_RC_MODE_CAPPED_QUALITY:
        chn->attr.rcAttr.attrRcMode.attrCappedQuality.iMinQP = (int16_t)minQp;
        chn->attr.rcAttr.attrRcMode.attrCappedQuality.iMaxQP = (int16_t)maxQp;
        break;
    default:
        LOG_ENC("SetChnQpBounds: Encoder Channel%d unsupport to set iMinQP and iMaxQP", encChn);
        break;
    }

    pthread_mutex_unlock(&chn->mutex_438);
    return 0;
}

int IMP_Encoder_SetChnQpIPDelta(int encChn, int delta) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) return -1;
    g_EncChannel[encChn].qp_ip_delta = delta;
    LOG_ENC("SetChnQpIPDelta: chn=%d, delta=%d", encChn, delta);
    return 0;
}

int IMP_Encoder_GetChnAttrRcMode(int encChn, IMPEncoderAttrRcMode *rcMode) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS || !rcMode) return -1;
    memcpy(rcMode, &g_EncChannel[encChn].attr.rcAttr.attrRcMode, sizeof(*rcMode));
    return 0;
}

int IMP_Encoder_SetChnAttrRcMode(int encChn, IMPEncoderAttrRcMode *rcMode) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS || !rcMode) return -1;
    memcpy(&g_EncChannel[encChn].attr.rcAttr.attrRcMode, rcMode, sizeof(*rcMode));
    LOG_ENC("SetChnAttrRcMode: chn=%d", encChn);
    return 0;
}

/* ========== Helper Functions ========== */

/* channel_encoder_init - based on decompilation at 0x8098c */
static int channel_encoder_init(EncChannel *chn) {
    if (chn == NULL) {
        return -1;
    }

    /* Initialize codec parameters with defaults (heap-allocated to avoid
     * stack overflow on MIPS — 0x794 = 1940 bytes exceeds typical thread
     * stack budget when combined with call chain depth) */
    uint8_t *codec_params = (uint8_t *)malloc(0x794);
    if (codec_params == NULL) {
        LOG_ENC("channel_encoder_init: failed to allocate codec_params");
        return -1;
    }
    AL_Codec_Encode_SetDefaultParam(codec_params);

    /* Extract encoder attributes - note: width/height are in the union members */
    IMPEncoderProfile profile_enum = chn->attr.encAttr.profile;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t bitrate_kbps = 0;
    IMPEncoderRcMode rc_mode = chn->attr.rcAttr.attrRcMode.rcMode;

    uint32_t codec_type = imp_encoder_profile_type(profile_enum);
    width = imp_encoder_attr_width(&chn->attr);
    height = imp_encoder_attr_height(&chn->attr);

    /* Extract rate control attributes */
    uint32_t fps_num = chn->attr.rcAttr.outFrmRate.frmRateNum;
    uint32_t fps_den = chn->attr.rcAttr.outFrmRate.frmRateDen;
    uint32_t gop = chn->attr.gopAttr.gopLength;
    uint32_t initial_qp = imp_encoder_default_qp(codec_type, -1);
    uint8_t level = chn->attr.encAttr.level;
    uint8_t tier = chn->attr.encAttr.tier;
    uint32_t pic_format = chn->attr.encAttr.picFormat;
    uint32_t enc_options = chn->attr.encAttr.encOptions;
    uint32_t enc_tools = chn->attr.encAttr.encTools;
    uint32_t fps_clk = 0;

    switch (rc_mode) {
    case IMP_ENC_RC_MODE_FIXQP:
        initial_qp = imp_encoder_default_qp(codec_type,
                                            (int)chn->attr.rcAttr.attrRcMode.attrFixQp.iInitialQP);
        break;
    case IMP_ENC_RC_MODE_CBR:
    case IMP_ENC_RC_MODE_VBR:
    case IMP_ENC_RC_MODE_CAPPED_VBR:
    case IMP_ENC_RC_MODE_CAPPED_QUALITY:
        bitrate_kbps = imp_encoder_rc_target_bitrate_kbps(&chn->attr.rcAttr);
        break;
    default:
        break;
    }
    if (bitrate_kbps == 0)
        bitrate_kbps = imp_encoder_default_bitrate_kbps(0);
    if (fps_num == 0)
        fps_num = 25;
    if (fps_den == 0)
        fps_den = 1;

    fps_clk = fps_den * 1000U;
    if (fps_clk == 0)
        fps_clk = 1000U;
    if (fps_clk > 0xffffU)
        fps_clk = 0xffffU;
    if (level == 0)
        level = imp_encoder_default_level(codec_type);
    if (pic_format == 0)
        pic_format = imp_encoder_default_pic_format(codec_type);
    if (enc_options == 0)
        enc_options = imp_encoder_default_enc_options(codec_type);
    if (enc_tools == 0)
        enc_tools = imp_encoder_default_enc_tools(codec_type);

    /* OEM layout:
     *   0x08/0x0c width, 0x0a/0x0e height
     *   0x14 pic format
     *   0x20 profile enum, 0x24 level, 0x25 tier
     *   0x30 enc options, 0x34 enc tools
     *   0x6c.. RC params
     *   0x78 fps num, 0x7a fps denominator * 1000
     *   0xac.. GOP block
     */
    *(uint16_t *)(codec_params + 0x08) = (uint16_t)width;
    *(uint16_t *)(codec_params + 0x0a) = (uint16_t)height;
    *(uint16_t *)(codec_params + 0x0c) = (uint16_t)width;
    *(uint16_t *)(codec_params + 0x0e) = (uint16_t)height;
    *(uint32_t *)(codec_params + 0x14) = pic_format;
    *(uint32_t *)(codec_params + 0x20) = (uint32_t)profile_enum;
    *(uint8_t  *)(codec_params + 0x24) = level;
    *(uint8_t  *)(codec_params + 0x25) = tier;
    *(uint32_t *)(codec_params + 0x30) = enc_options;
    *(uint32_t *)(codec_params + 0x34) = enc_tools;
    *(uint16_t *)(codec_params + 0x78) = (uint16_t)fps_num;
    *(uint16_t *)(codec_params + 0x7a) = (uint16_t)fps_clk;
    memset(codec_params + 0xac, 0, 0x1c);
    memcpy(codec_params + 0xac, &chn->attr.gopAttr,
           sizeof(chn->attr.gopAttr) < 0x1c ? sizeof(chn->attr.gopAttr) : 0x1c);
    if (gop != 0)
        *(uint16_t *)(codec_params + 0xb0) = (uint16_t)gop;
    *(uint16_t *)(codec_params + 0x84) = (uint16_t)initial_qp;

    if (codec_type == IMP_ENC_TYPE_JPEG) {
        imp_encoder_apply_jpeg_codec_defaults(codec_params);
    }

    if (channel_encoder_set_rc_param(codec_params + 0x6c, &chn->attr.rcAttr) < 0) {
        LOG_ENC("channel_encoder_init: failed to map RC params");
        free(codec_params);
        return -1;
    }

    LOG_ENC("channel_encoder_init: %dx%d, profile=0x%x, lvl=%u, fmt=0x%x, fps=%u/%u, gop=%u, bitrate=%u kbps, share=%d",
            width, height, profile_enum, level, pic_format, fps_num, fps_den,
            gop, bitrate_kbps, chn->bufshare_chn);
    enc_kmsg("channel_encoder_init chn=%d %ux%u profile=0x%x lvl=%u fmt=0x%x fps=%u/%u clk=%u gop=%u bitrate=%u",
             chn->chn_id, width, height, profile_enum, level, pic_format,
             fps_num, fps_den, fps_clk, gop, bitrate_kbps);

    /* Create codec instance */
    enc_kmsg("channel_encoder_init pre-create chn=%d codec_params=%p", chn->chn_id, codec_params);
    if (AL_Codec_Encode_Create(&chn->codec, codec_params) < 0 || chn->codec == NULL) {
        LOG_ENC("channel_encoder_init: AL_Codec_Encode_Create failed");
        enc_kmsg("channel_encoder_init create-failed chn=%d codec=%p", chn->chn_id, chn->codec);
        free(codec_params);
        return -1;
    }
    enc_kmsg("channel_encoder_init post-create chn=%d codec=%p", chn->chn_id, chn->codec);

    /* codec_params no longer needed after Create copies it */
    free(codec_params);
    codec_params = NULL;

    if (chn->entropy_mode >= 0 && AL_Codec_Encode_SetEntropyMode(chn->codec, chn->entropy_mode) < 0) {
        LOG_ENC("channel_encoder_init: failed to set entropy mode");
        enc_kmsg("channel_encoder_init entropy-failed chn=%d codec=%p mode=%d",
                 chn->chn_id, chn->codec, chn->entropy_mode);
        AL_Codec_Encode_Destroy(chn->codec);
        chn->codec = NULL;
        return -1;
    }
    enc_kmsg("channel_encoder_init entropy-ok chn=%d mode=%d", chn->chn_id, chn->entropy_mode);

    /* Get source frame count and size */
    if (AL_Codec_Encode_GetSrcFrameCntAndSize(chn->codec, &chn->src_frame_cnt, &chn->src_frame_size) < 0) {
        LOG_ENC("channel_encoder_init: GetSrcFrameCntAndSize failed");
        enc_kmsg("channel_encoder_init src-info-failed chn=%d codec=%p", chn->chn_id, chn->codec);
        AL_Codec_Encode_Destroy(chn->codec);
        return -1;
    }

    LOG_ENC("channel_encoder_init: frame_cnt=%d, frame_size=%d",
            chn->src_frame_cnt, chn->src_frame_size);
    enc_kmsg("channel_encoder_init buffers chn=%d frame_cnt=%d frame_size=%d",
             chn->chn_id, chn->src_frame_cnt, chn->src_frame_size);

    /* Allocate frame buffers */
    size_t buf_size = chn->src_frame_cnt * 0x458; /* 0x458 bytes per frame buffer */
    chn->frame_buffers = calloc(buf_size, 1);
    if (chn->frame_buffers == NULL) {
        LOG_ENC("channel_encoder_init: failed to allocate frame buffers");
        enc_kmsg("channel_encoder_init framebuf-failed chn=%d size=%zu", chn->chn_id, buf_size);
        AL_Codec_Encode_Destroy(chn->codec);
        return -1;
    }
    enc_kmsg("channel_encoder_init framebuf-ok chn=%d ptr=%p size=%zu", chn->chn_id, chn->frame_buffers, buf_size);

    /* Initialize fifo */
    Fifo_Init(chn->fifo, chn->src_frame_cnt);

    /* Queue all buffers to fifo */
    for (int i = 0; i < chn->src_frame_cnt; i++) {
        void *buf = (uint8_t*)chn->frame_buffers + (i * 0x458);
        Fifo_Queue(chn->fifo, buf, -1);
    }
    enc_kmsg("channel_encoder_init fifo-primed chn=%d frame_cnt=%d", chn->chn_id, chn->src_frame_cnt);

    /* Create encoder thread (from decompilation at 0x80b40) */
    int ret = pthread_create(&chn->thread_encoder, NULL, encoder_thread, chn);
    if (ret != 0) {
        LOG_ENC("channel_encoder_init: failed to create encoder thread: %s (%d)",
                strerror(ret), ret);
        enc_kmsg("channel_encoder_init encoder-thread-failed chn=%d ret=%d", chn->chn_id, ret);
        Fifo_Deinit(chn->fifo);
        free(chn->frame_buffers);
        AL_Codec_Encode_Destroy(chn->codec);
        return -1;
    }
    enc_kmsg("channel_encoder_init encoder-thread-ok chn=%d tid=%p",
             chn->chn_id, (void *)chn->thread_encoder);

    /* Create stream thread (from decompilation at 0x80b60) */
    ret = pthread_create(&chn->thread_stream, NULL, stream_thread, chn);
    if (ret != 0) {
        LOG_ENC("channel_encoder_init: failed to create stream thread: %s (%d)",
                strerror(ret), ret);
        enc_kmsg("channel_encoder_init stream-thread-failed chn=%d ret=%d", chn->chn_id, ret);
        pthread_cancel(chn->thread_encoder);
        pthread_join(chn->thread_encoder, NULL);
        Fifo_Deinit(chn->fifo);
        free(chn->frame_buffers);
        AL_Codec_Encode_Destroy(chn->codec);
        return -1;
    }
    enc_kmsg("channel_encoder_init stream-thread-ok chn=%d tid=%p",
             chn->chn_id, (void *)chn->thread_stream);

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
        pthread_cond_broadcast(&chn->cond_1f0);
        pthread_join(chn->thread_encoder, NULL);
    }

    if (chn->thread_stream) {
        pthread_cancel(chn->thread_stream);
        pthread_join(chn->thread_stream, NULL);
    }

    if (chn->pending_frame != NULL) {
        encoder_unlock_and_release_frame(chn, chn->pending_frame);
        chn->pending_frame = NULL;
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

    uint8_t *dst_b = (uint8_t *)dst;
    int32_t *dst_w = (int32_t *)dst;
    int32_t mode = (int32_t)src->attrRcMode.rcMode;

    switch (mode) {
    case IMP_ENC_RC_MODE_FIXQP:
        dst_w[0] = 0;
        *(int16_t *)(dst_b + 0x18) = src->attrRcMode.attrFixQp.iInitialQP;
        break;
    case IMP_ENC_RC_MODE_CBR:
        dst_w[0] = mode;
        dst_w[4] = (int32_t)src->attrRcMode.attrCbr.uTargetBitRate * 1000;
        dst_w[5] = (int32_t)src->attrRcMode.attrCbr.uTargetBitRate * 1000;
        *(int16_t *)(dst_b + 0x18) = src->attrRcMode.attrCbr.iInitialQP;
        *(int8_t  *)(dst_b + 0x1a) = (int8_t)src->attrRcMode.attrCbr.iMinQP;
        *(int16_t *)(dst_b + 0x1c) = src->attrRcMode.attrCbr.iMaxQP;
        *(int8_t  *)(dst_b + 0x1e) = (int8_t)src->attrRcMode.attrCbr.iIPDelta;
        *(int16_t *)(dst_b + 0x20) = src->attrRcMode.attrCbr.iPBDelta;
        dst_w[10] = (int32_t)src->attrRcMode.attrCbr.eRcOptions;
        dst_w[15] = (int32_t)src->attrRcMode.attrCbr.uMaxPictureSize * 1000;
        break;
    case IMP_ENC_RC_MODE_VBR:
        dst_w[0] = mode;
        dst_w[4] = (int32_t)src->attrRcMode.attrVbr.uTargetBitRate * 1000;
        dst_w[5] = (int32_t)src->attrRcMode.attrVbr.uMaxBitRate * 1000;
        *(int16_t *)(dst_b + 0x18) = src->attrRcMode.attrVbr.iInitialQP;
        *(int8_t  *)(dst_b + 0x1a) = (int8_t)src->attrRcMode.attrVbr.iMinQP;
        *(int16_t *)(dst_b + 0x1c) = src->attrRcMode.attrVbr.iMaxQP;
        *(int8_t  *)(dst_b + 0x1e) = (int8_t)src->attrRcMode.attrVbr.iIPDelta;
        *(int16_t *)(dst_b + 0x20) = src->attrRcMode.attrVbr.iPBDelta;
        dst_w[10] = (int32_t)src->attrRcMode.attrVbr.eRcOptions;
        dst_w[15] = (int32_t)src->attrRcMode.attrVbr.uMaxPictureSize * 1000;
        break;
    case IMP_ENC_RC_MODE_CAPPED_VBR:
    case IMP_ENC_RC_MODE_CAPPED_QUALITY: {
        const IMPEncoderAttrVbr *vbr = (mode == IMP_ENC_RC_MODE_CAPPED_VBR)
            ? &src->attrRcMode.attrCappedVbr
            : &src->attrRcMode.attrCappedQuality;
        dst_w[0] = mode;
        dst_w[4] = (int32_t)vbr->uTargetBitRate * 1000;
        dst_w[5] = (int32_t)vbr->uMaxBitRate * 1000;
        *(int16_t *)(dst_b + 0x18) = vbr->iInitialQP;
        *(int8_t  *)(dst_b + 0x1a) = (int8_t)vbr->iMinQP;
        *(int16_t *)(dst_b + 0x1c) = vbr->iMaxQP;
        *(int8_t  *)(dst_b + 0x1e) = (int8_t)vbr->iIPDelta;
        *(int16_t *)(dst_b + 0x20) = vbr->iPBDelta;
        dst_w[10] = (int32_t)vbr->eRcOptions;
        dst_w[15] = (int32_t)vbr->uMaxPictureSize * 1000;
        *(int16_t *)(dst_b + 0x30) = (int16_t)(vbr->uMaxPSNR * 100);
        break;
    }
    default:
        return -1;
    }

    LOG_ENC("set_rc_param: mode=%d, target=%u, max=%u, qp_range=[%u,%u]",
            mode,
            imp_encoder_rc_target_bitrate_kbps(src),
            imp_encoder_rc_max_bitrate_kbps(src),
            imp_encoder_rc_min_qp(src),
            imp_encoder_rc_max_qp(src));

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
    enc_kmsg("encoder_thread start chn=%d codec=%p", chn->chn_id, chn->codec);

    while (1) {
        void *queued_frame = NULL;

        pthread_testcancel();
        pthread_mutex_lock(&chn->mutex_1d8);
        while (chn->pending_frame == NULL) {
            LOG_ENC("encoder_thread: waiting chn=%d codec=%p recv=%u started=%u", chn->chn_id, chn->codec,
                    chn->recv_pic_started, chn->started);
            pthread_cond_wait(&chn->cond_1f0, &chn->mutex_1d8);
        }
        queued_frame = chn->pending_frame;
        chn->pending_frame = NULL;
        pthread_mutex_unlock(&chn->mutex_1d8);

        if (queued_frame == NULL)
            continue;

        LOG_ENC("encoder_thread: dequeued frame chn=%d frame=%p codec=%p", chn->chn_id, queued_frame, chn->codec);
        enc_kmsg("encoder_thread dequeued chn=%d frame=%p codec=%p", chn->chn_id, queued_frame, chn->codec);

        if (AL_Codec_Encode_Process(chn->codec, queued_frame, queued_frame) != 0) {
            LOG_ENC("encoder_thread: AL_Codec_Encode_Process failed on chn=%d", chn->chn_id);
            enc_kmsg("encoder_thread process failed chn=%d frame=%p", chn->chn_id, queued_frame);
            encoder_unlock_and_release_frame(chn, queued_frame);
        } else {
            LOG_ENC("encoder_thread: AL_Codec_Encode_Process ok chn=%d frame=%p", chn->chn_id, queued_frame);
            enc_kmsg("encoder_thread process ok chn=%d frame=%p", chn->chn_id, queued_frame);
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
            void *codec_user_data = NULL;
            if (AL_Codec_Encode_GetStream(chn->codec, &codec_stream, &codec_user_data) == 0 && codec_stream != NULL) {
                /* Validate stream pointer - must be a reasonable heap address */
                uintptr_t stream_addr = (uintptr_t)codec_stream;
                if (stream_addr < 0x10000) {
                    LOG_ENC("stream_thread: invalid stream pointer %p (too small)", codec_stream);
                    continue;
                }

                if (chn->stream_seq % 50 == 0)
                LOG_ENC("stream_thread: got stream %p (seq=%u)", codec_stream, chn->stream_seq);

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
                        /* Determine codec type from profile's top byte (OEM format): 0=AVC,1=HEVC,4=JPEG */
                        uint32_t profile_enum = chn->attr.encAttr.profile;
                        uint32_t codec_type = (profile_enum >> 24) & 0xFF;
                        int is_h264 = (codec_type == IMP_ENC_TYPE_AVC);
                        /* Injection path removed: streaming uses encoder-provided AU only */
                        (void)is_h264; /* keep variable for potential future use */

                    /* Initialize stream buffer */
                    stream_buf->codec_stream = codec_stream;
                    stream_buf->codec_user_data = codec_user_data;
                    stream_buf->seq = chn->stream_seq++;
                    stream_buf->streamEnd = 0;

                    /* Populate base addresses and T31-style pack (possibly using injected buffer) */
                    stream_buf->base_phy = out_phy;
                    stream_buf->base_vir = out_vir;
                    stream_buf->base_size = out_len;

                    /* Split Annex B buffer into packs per NAL start code to mirror OEM libimp */
                    uint8_t *p_all = (uint8_t*)(uintptr_t)out_vir;
                    size_t L_all = (size_t)out_len;
                    size_t i = 0; int sc = 0; int count = 0;
                    i = find_start_code(p_all, 0, L_all, &sc);
                    while (i < L_all) {
                        size_t ns = i + (sc ? sc : 0);
                        int sc2 = 0; size_t nx = find_start_code(p_all, ns, L_all, &sc2);
                        count++;
                        if (nx >= L_all) break;
                        i = nx; sc = sc2;
                    }

                    if (count <= 0) {
                        /* Fallback: single pack covering whole buffer */
                        stream_buf->packs = NULL;
                        stream_buf->packCount = 0;
                        stream_buf->pack.offset = 0;
                        stream_buf->pack.length = out_len;
                        stream_buf->pack.timestamp = (int64_t)timestamp;
                        stream_buf->pack.frameEnd = 1;
                        memset(&stream_buf->pack.nalType, 0, sizeof(stream_buf->pack.nalType));
                        if (codec_type == IMP_ENC_TYPE_AVC) {
                            /* Heuristic based on frame_type */
                            stream_buf->pack.nalType.h264NalType = (frame_type == 0) ? 5 : 1;
                        }
                        stream_buf->pack.sliceType = (IMPEncoderSliceType)slice_type;
                    } else {
                        stream_buf->packs = (IMPEncoderPack*)calloc((size_t)count, sizeof(IMPEncoderPack));
                        stream_buf->packCount = (uint32_t)count;

                        /* Second pass: fill packs */
                        i = find_start_code(p_all, 0, L_all, &sc);
                        int idx = 0;
                        while (idx < count && i < L_all) {
                            size_t ns = i + (sc ? sc : 0);
                            int sc2 = 0; size_t nx = find_start_code(p_all, ns, L_all, &sc2);
                            size_t seg_len = (nx < L_all) ? (nx - i) : (L_all - i);

                            stream_buf->packs[idx].offset = (uint32_t)i;
                            stream_buf->packs[idx].length = (uint32_t)seg_len;
                            stream_buf->packs[idx].timestamp = (int64_t)timestamp;
                            stream_buf->packs[idx].frameEnd = (idx == (count - 1)) ? 1 : 0;
                            memset(&stream_buf->packs[idx].nalType, 0, sizeof(stream_buf->packs[idx].nalType));
                            if (ns < L_all) {
                                uint8_t t = p_all[ns] & 0x1F; /* H.264 */
                                stream_buf->packs[idx].nalType.h264NalType = t;
                            }
                            stream_buf->packs[idx].sliceType = (IMPEncoderSliceType)slice_type;

                            if (nx >= L_all) break;
                            i = nx; sc = sc2; idx++;
                        }

                        /* Populate legacy single-pack with first pack for debug compatibility */
                        stream_buf->pack = stream_buf->packs[0];
                    }

                    /* If no SPS/PPS observed yet on this channel, request an IDR once */
                    if (codec_type == IMP_ENC_TYPE_AVC && !chn->param_sets_seen) {
                        int saw_sps = 0, saw_pps = 0;
                        if (stream_buf->packs && stream_buf->packCount > 0) {
                            for (uint32_t ii = 0; ii < stream_buf->packCount; ++ii) {
                                uint8_t t = stream_buf->packs[ii].nalType.h264NalType;
                                if (t == 7) saw_sps = 1; else if (t == 8) saw_pps = 1;
                            }
                        } else {
                            uint8_t t = stream_buf->pack.nalType.h264NalType;
                            if (t == 7) saw_sps = 1; else if (t == 8) saw_pps = 1;
                        }
                        if (saw_sps && saw_pps) {
                            chn->param_sets_seen = 1;
                        } else if (!chn->idr_requested_once) {
                            chn->idr_requested_once = 1;
                            LOG_ENC("No SPS/PPS yet on chn=%d; requesting IDR", chn->chn_id);
                            IMP_Encoder_RequestIDR(chn->chn_id);
                        }
                    }

                    /* Store in channel for GetStream (only if slot is empty) */
                    int posted = 0;
                    pthread_mutex_lock(&chn->mutex_450);
                    if (chn->current_stream == NULL) {
                        chn->current_stream = stream_buf;
                        /* Signal sem_428 (PollingStream waits on this) */
                        sem_post(&chn->sem_428);
                        /* Signal sem_418 (GetStream waits on this) */
                        sem_post(&chn->sem_418);
                        /* Also notify via eventfd for apps using GetFd/poll */
                        if (chn->eventfd >= 0) {
                            uint64_t val = 1;
                            ssize_t n = write(chn->eventfd, &val, sizeof(val));
                            (void)n;
                        }
                        posted = 1;
                    }
                    pthread_mutex_unlock(&chn->mutex_450);

                    /* If consumer hasn't released previous stream yet, wait until it is released, then post */
                    if (!posted) {
                        { static unsigned int wait_count = 0; unsigned int c = __sync_add_and_fetch(&wait_count, 1);
                          if (c <= 5 || (c % 50) == 0)
                            LOG_ENC("stream_thread: waiting (not dropping) because previous stream not yet released [#%u]", c);
                        }
                        while (!posted) {
                            /* Wait for ReleaseStream to signal slot available */
                            sem_wait(&chn->sem_408);
                            /* Try again to post */
                            pthread_mutex_lock(&chn->mutex_450);
                            if (chn->current_stream == NULL) {
                                chn->current_stream = stream_buf;
                                sem_post(&chn->sem_428);
                                sem_post(&chn->sem_418);
                                if (chn->eventfd >= 0) {
                                    uint64_t val = 1;
                                    ssize_t n = write(chn->eventfd, &val, sizeof(val));
                                    (void)n;
                                }
                                posted = 1;
                            }
                            pthread_mutex_unlock(&chn->mutex_450);
                        }
                    }

                    /* Log final posted stream */
                    if (stream_buf->seq % 50 == 0)
                    LOG_ENC("stream_thread: stream seq=%u, packs=%u, total_len=%u, type=%s",
                            stream_buf->seq, stream_buf->packs ? stream_buf->packCount : 1, stream_buf->base_size,
                            frame_type == 0 ? "I" : (frame_type == 1 ? "P" : "B"));

                    /* Debug: print first few NAL types from outgoing buffer */
                    if (stream_buf->base_size >= 6) {
                        const uint8_t *p = (const uint8_t*)(uintptr_t)stream_buf->base_vir;
                        size_t L = stream_buf->base_size;
                        size_t i = 0; int sc = 0; int cnt = 0;
                        i = find_start_code(p, 0, L, &sc);
                        char nal_str[64]; int off = 0;
                        off += snprintf(nal_str+off, sizeof(nal_str)-off, "NALs:");
                        while (i < L && cnt < 4) {
                            size_t ns = i + (sc ? sc : 0);
                            int sc2 = 0; size_t nx = find_start_code(p, ns, L, &sc2);
                            if (ns < nx) {
                                uint8_t t = p[ns] & 0x1F; cnt++;
                                off += snprintf(nal_str+off, sizeof(nal_str)-off, " %u", t);
                            }
                            if (nx >= L) break;
                            i = nx;
                            sc = sc2;
                        }
                        if (stream_buf->seq % 50 == 0)
                        LOG_ENC("stream_thread: %s", nal_str);
                    }

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
    if (module == NULL || frame == NULL) {
        return -1;
    }

    /* OEM shape: encoder_update should stay lightweight and hand frames off to
     * the per-channel encoder thread. Running AL_Codec_Encode_Process inline on
     * the FS capture thread stalls capture and has been a reliable path to
     * main-stream starvation and eventual reboot. */

    static int update_log_count = 0;
    int should_log = (update_log_count < 5);
    update_log_count++;

    if (should_log)
        LOG_ENC("encoder_update: frame=%p (call #%d)", frame, update_log_count);
    if (should_log)
        enc_kmsg("encoder_update frame=%p call=%d", frame, update_log_count);

    /* Identify the encoder group from the module structure */
    int enc_group = -1;
    {
        uint32_t group_id = *((uint32_t*)((char*)module + 0x130));
        if (group_id < 6u)
            enc_group = (int)group_id;
    }

    if (enc_group < 0)
        return 0;

    /* FIX: Match 93de1a9 behavior — submit frame to ONE channel only (the
     * one matching enc_group, i.e. module->group_id). The old code used
     * dst_chn = group_id as a direct channel index, not as a group with
     * multiple sub-channels. Iterating ALL channels in the group causes
     * multiple VBMLockFrameByVaddr calls (ref_count > 1), and if any
     * channel's ReleaseStream path fails to call VBMUnlockFrameByVaddr,
     * the frame is never returned to the kernel → DQBUF blocks forever.
     *
     * Also: on Process failure, must VBMUnlock the frame (93de1a9 did this). */
    pthread_mutex_lock(&encoder_mutex);
    if (enc_group >= 0 && enc_group < MAX_ENC_CHANNELS) {
        EncChannel *chn = &g_EncChannel[enc_group];
        LOG_ENC("encoder_update: group=%d chn_id=%d recv=%u started=%u codec=%p frame=%p",
                enc_group, chn->chn_id, chn->recv_pic_started, chn->started, chn->codec, frame);
        enc_kmsg("encoder_update group=%d chn=%d recv=%u started=%u codec=%p frame=%p",
                 enc_group, chn->chn_id, chn->recv_pic_started, chn->started, chn->codec, frame);
        if (chn->chn_id >= 0 && chn->recv_pic_started && chn->codec != NULL) {
            void *queued_frame = NULL;
            int clone_ret = encoder_clone_source_frame(chn, frame, &queued_frame);
            LOG_ENC("encoder_update: clone ret=%d queued_frame=%p chn=%d", clone_ret, queued_frame, chn->chn_id);
            enc_kmsg("encoder_update clone ret=%d queued_frame=%p chn=%d", clone_ret, queued_frame, chn->chn_id);
            if (clone_ret == 0) {
                pthread_mutex_lock(&chn->mutex_1d8);
                if (chn->pending_frame != NULL) {
                    LOG_ENC("encoder_update: replacing pending frame on chn=%d", chn->chn_id);
                    enc_kmsg("encoder_update replacing pending chn=%d old=%p new=%p",
                             chn->chn_id, chn->pending_frame, queued_frame);
                    encoder_unlock_and_release_frame(chn, chn->pending_frame);
                }
                chn->pending_frame = queued_frame;
                pthread_cond_signal(&chn->cond_1f0);
                LOG_ENC("encoder_update: queued frame chn=%d frame=%p", chn->chn_id, queued_frame);
                enc_kmsg("encoder_update queued chn=%d frame=%p", chn->chn_id, queued_frame);
                pthread_mutex_unlock(&chn->mutex_1d8);
            }
        } else {
            LOG_ENC("encoder_update: skip group=%d chn_id=%d recv=%u codec=%p", enc_group, chn->chn_id,
                    chn->recv_pic_started, chn->codec);
            enc_kmsg("encoder_update skip group=%d chn=%d recv=%u codec=%p",
                     enc_group, chn->chn_id, chn->recv_pic_started, chn->codec);
        }
    }
    pthread_mutex_unlock(&encoder_mutex);

    return 0;
}

/* ========== Missing OEM encoder functions (from BN decompilation) ========== */

int IMP_Encoder_GetChnEncType(int encChn, IMPEncoderEncType *encType) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) {
        LOG_ENC("GetChnEncType: Invalid Channel Num:%d", encChn);
        return -1;
    }
    EncChannel *chn = &g_EncChannel[encChn];
    if (chn->chn_id < 0) {
        LOG_ENC("GetChnEncType: Encoder Channel%d hasn't been created", encChn);
        return -1;
    }
    *encType = (IMPEncoderEncType)chn->enc_type;
    return 0;
}

int IMP_Encoder_SetFrameRelease(int encChn, void *callback, void *arg) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) {
        LOG_ENC("SetFrameRelease: Invalid Channel Num:%d", encChn);
        return -1;
    }
    EncChannel *chn = &g_EncChannel[encChn];
    if (chn->chn_id < 0) {
        LOG_ENC("SetFrameRelease: Encoder Channel%d hasn't been created", encChn);
        return -1;
    }
    pthread_mutex_lock(&chn->mutex_438);
    chn->frame_release_cb = callback;
    chn->frame_release_arg = arg;
    pthread_mutex_unlock(&chn->mutex_438);
    return 0;
}

/* Per-channel bitrate tracking state */
static struct {
    uint64_t total_bits;
    uint64_t start_time_us;
    int frame_count;
    int initialized;
    double last_bitrate;
} g_bitrate_track[MAX_ENC_CHANNELS];

int IMP_Encoder_GetChnAveBitrate(int encChn, IMPEncoderStream *stream,
                                  int frameCnt, int *bitrate) {
    if (encChn < 0 || encChn >= MAX_ENC_CHANNELS) {
        LOG_ENC("GetChnAveBitrate: Invalid Channel Num:%d", encChn);
        return -1;
    }
    if (bitrate == NULL) {
        LOG_ENC("GetChnAveBitrate: NULL bitrate output");
        return -1;
    }
    if (stream == NULL) {
        LOG_ENC("GetChnAveBitrate: The API must used after IMP_Encoder_GetStream");
        return -1;
    }

    /* Sum stream pack sizes for this frame */
    uint32_t frame_bytes = 0;
    for (uint32_t i = 0; i < stream->packCount; i++) {
        frame_bytes += stream->pack[i].length;
    }

    g_bitrate_track[encChn].total_bits += (uint64_t)frame_bytes * 8;

    if (!g_bitrate_track[encChn].initialized) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        g_bitrate_track[encChn].start_time_us =
            (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
        g_bitrate_track[encChn].initialized = 1;
        g_bitrate_track[encChn].frame_count = 1;
        *bitrate = 0;
        return 0;
    }

    g_bitrate_track[encChn].frame_count++;

    if (g_bitrate_track[encChn].frame_count >= frameCnt) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t now_us = (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000;
        uint64_t elapsed_us = now_us - g_bitrate_track[encChn].start_time_us;
        if (elapsed_us > 0) {
            g_bitrate_track[encChn].last_bitrate =
                (double)g_bitrate_track[encChn].total_bits /
                ((double)elapsed_us / 1000000.0);
        }
        /* Reset for next window */
        g_bitrate_track[encChn].start_time_us = now_us;
        g_bitrate_track[encChn].total_bits = 0;
        g_bitrate_track[encChn].frame_count = 0;
    }

    *bitrate = (int)g_bitrate_track[encChn].last_bitrate;
    return 0;
}
