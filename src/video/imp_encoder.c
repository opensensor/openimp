/*
 * src/video/imp_encoder.c
 *
 * Stock source-file origin: /home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c
 *
 * T95 port: IMP_Encoder_* public API, lifecycle, stream I/O, rate-control
 * setters, GOP/frame-rate setters, misc helpers.
 *
 * Port rules:
 *   - Reproduce decomp control flow verbatim
 *   - Preserve all numeric/string constants including __FILE__/__FUNCTION__
 *   - Raw byte-offset form for unknown struct layouts
 *   - Forward-declare unmet symbols with explicit prototypes
 *
 * Dependencies (shared/ported elsewhere):
 *   T52 src/alcodec/lib_codec.c       — AL_Codec_Encode_* dispatchers
 *   T96 src/alcodec/Encoder.c         — AL_Encoder_* public
 *   T75 src/video/imp_video_common.c  — video_sem_timedwait, video_vbm_free, etc.
 *   T76 src/video/imp_mempool.c       — IMP_Encoder_{Clear,Set,Get}PoolId
 *   T68                              — group/Module/subject state
 *   T97                              — audio_common / al_rtos
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/eventfd.h>
#include <time.h>
#include <unistd.h>

#include "video/encoder_channel_layout.h"

extern char _gp;

static void video_enc_trace(const char *fmt, ...)
{
    int fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        return;
    }

    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if (n > 0) {
        dprintf(fd, "libimp/ENCW2: %s\n", msg);
    }
    close(fd);
}

/* ===== forward-declared external helpers ===== */

/* Logging */
int32_t IMP_Log_Get_Option(void); /* forward decl, ported by T<N> later */
int32_t imp_log_fun(int32_t level, int32_t option, int32_t type, ...); /* forward decl, ported by T<N> later */

/* libc/posix helpers re-declared for clarity (some platforms require) */
extern int puts(const char *s);
extern int printf(const char *fmt, ...);
extern int sprintf(char *buf, const char *fmt, ...);

/* Common encoder / module / group machinery.
 * The stock global is declared as `EncoderState *gEncoder` in
 * src/core/globals.c. We access it via a forward-declared opaque pointer to
 * keep the decomp's raw int32_t offset arithmetic identical. */
typedef struct EncoderState EncoderState;
extern EncoderState *gEncoder;
static inline int32_t *genc_i32(void)  { return (int32_t *)(void *)gEncoder; }
static inline void     genc_set(void *p) { gEncoder = (EncoderState *)p; }
int32_t *alloc_device(const char *name, int32_t size); /* forward decl, ported by T<N> later */
void free_device(int32_t *dev); /* forward decl, ported by T<N> later */
int32_t AL_Codec_Create(int32_t arg1); /* forward decl, ported by T<N> later */
int32_t AL_Codec_Destroy(void); /* forward decl, ported by T<N> later */
int32_t *create_group(int32_t arg1, int32_t arg2, const char *arg3,
                      int32_t (*cb)(void *, void *)); /* forward decl, ported by T<N> later */
int32_t destroy_group(int32_t arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t on_encoder_group_data_update(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
int32_t dsys_func_share_mem_register(int32_t arg1, int32_t arg2, const char *name,
                                     int32_t (*fn)(void *)); /* forward decl, ported by T<N> later */
int32_t dsys_func_unregister(int32_t arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t get_cpu_id(void); /* forward decl, ported by T<N> later */
int32_t IMP_FrameSource_EnableChn(int32_t chnNum); /* forward decl, ported by T<N> later */
typedef struct IMPCell IMPCell;
int32_t IMP_System_BindIfNeeded(IMPCell *srcCell, IMPCell *dstCell); /* forward decl, ported by T<N> later */

/* AL_Codec_Encode_* — T52 */
int32_t AL_Codec_Encode_Create(void **codec, void *params); /* forward decl, ported by T<N> later */
int32_t AL_Codec_Encode_Destroy(void *codec); /* forward decl, ported by T<N> later */
int32_t AL_Codec_Encode_GetSrcFrameCntAndSize(void *codec, int32_t *cnt, int32_t *size); /* forward decl, ported by T<N> later */
int32_t AL_Codec_Encode_GetSrcStreamCntAndSize(void *codec, int32_t *cnt, int32_t *size); /* forward decl, ported by T<N> later */
int32_t AL_Codec_Encode_GetStream(void *codec, void **stream, void **user_data); /* forward decl, ported by T<N> later */
int32_t AL_Codec_Encode_ReleaseStream(void *codec, void *stream, void *user_data); /* forward decl, ported by T<N> later */
int32_t AL_Codec_Encode_Process(void *codec, void *frame, void *user_data); /* forward decl, ported by T<N> later */
int32_t AL_Codec_Encode_SetQp(void *codec, int32_t qp); /* forward decl, ported by T<N> later */
int32_t AL_Codec_Encode_SetQpBounds(void *codec, int32_t minQp, int32_t maxQp); /* forward decl, ported by T<N> later */
int32_t AL_Codec_Encode_SetQpIPDelta(void *codec, int32_t delta); /* forward decl, ported by T<N> later */
int32_t AL_Codec_Encode_SetBitRate(void *codec, int32_t target, int32_t maxBR); /* forward decl, ported by T<N> later */
int32_t AL_Codec_Encode_GetFrameRate(void *codec, int16_t *num, int16_t *den); /* forward decl, ported by T<N> later */
int32_t AL_Codec_Encode_SetFrameRate(void *codec, int32_t num, int32_t den); /* forward decl, ported by T<N> later */
int32_t AL_Codec_Encode_GetRcParam(void *codec, void *rcAttr); /* forward decl, ported by T<N> later */
int32_t AL_Codec_Encode_SetRcParam(void *codec, void *rcAttr); /* forward decl, ported by T<N> later */
int32_t AL_Codec_Encode_GetGopParam(void *codec, void *gopAttr); /* forward decl, ported by T<N> later */
int32_t AL_Codec_Encode_SetGopParam(void *codec, void *gopAttr); /* forward decl, ported by T<N> later */
int32_t AL_Codec_Encode_SetGopLength(void *codec, int32_t gopLen); /* forward decl, ported by T<N> later */
int32_t AL_Codec_Encode_RestartGop(void *codec); /* forward decl, ported by T<N> later */
int32_t AL_Codec_Encode_GetLastError(void *codec); /* forward decl, ported by T<N> later */
int32_t AL_Codec_Encode_RequestIDR(void *codec); /* forward decl, ported by T<N> later */

/* T75 video_common helpers */
int32_t video_sem_timedwait(sem_t *sem, int32_t *timeout_ms); /* forward decl, ported by T<N> later */
int32_t video_vbm_free(int32_t vaddr); /* forward decl, ported by T<N> later */

/* Misc helpers */
int32_t c_reduce_fraction(int32_t *num, int32_t *den); /* forward decl, ported by T<N> later */
int32_t system_gettime(int32_t arg1); /* forward decl, ported by T<N> later */
uint32_t _getLeftPart32(uint32_t value); /* forward decl, ported by T<N> later */
uint32_t _getRightPart32(uint32_t value); /* forward decl, ported by T<N> later */
uint32_t _setLeftPart32(uint32_t value); /* forward decl, ported by T<N> later */
uint32_t _setRightPart32(uint32_t value); /* forward decl, ported by T<N> later */

/* Channel lifecycle helpers implemented in this task group */
int32_t channel_encoder_init(void *chn); /* forward decl, ported by T<N> later */
int32_t channel_encoder_exit(void *chn); /* forward decl, ported by T<N> later */
int32_t channel_encoder_set_rc_param(void *out, int32_t *rcAttr); /* forward decl, ported by T<N> later */
int32_t release_used_framestream(void *chn, void *frm); /* forward decl, ported by T<N> later */
int32_t AL_Codec_Encode_PrimeStreamBuffers(int32_t *codec_handle); /* forward decl, ported by T<N> later */

/* forward decls for functions within this file referenced in stop/flush paths */
int32_t IMP_Encoder_Query(int32_t arg1, void *arg2);
int32_t IMP_Encoder_PollingStream(int32_t arg1, uint32_t arg2);
int32_t IMP_Encoder_GetStream_Impl(int32_t arg1, void *arg2, uint32_t arg3, int32_t arg4);
int32_t IMP_Encoder_GetStream(int32_t arg1, void *arg2, uint32_t arg3);
int32_t IMP_Encoder_ReleaseStream(int32_t arg1, void *arg2);
int32_t IMP_Encoder_RegisterChn(int32_t arg1, int32_t arg2);
int32_t IMP_Encoder_RequestIDR(int32_t arg1);
int32_t IMP_Encoder_StartRecvPic(int32_t arg1);
int32_t IMP_Encoder_StopRecvPic(int32_t arg1);
int32_t IMP_Encoder_UnRegisterChn(int32_t arg1);

/* ===== Global encoder channel array =====
 *
 * The stock library exports a BSS-resident table of 9 EncChannel structs at
 * 0x308 bytes each (total 0x1b48 bytes). We model it as a flat byte blob so
 * the raw byte-offset access form used throughout the decomp works verbatim.
 *
 * Each field is accessed as *(char*)((arg1 * 0x308) + absolute_address), where
 * absolute_address = &g_EncChannel_bytes + struct_offset.
 */

#define IMP_ENC_MAX_CHN 9
#define IMP_ENC_CHN_STRIDE 0x308

/* Tail globals (offsets >= 0x1b48 from g_EncChannel base): */
/*   0x1193f4 (bps_cache), 0x1193f8, 0x1193fc (entropy), */
/*   0x1194b0 (eventfd), 0x1194b8/bc (pending counters), */
/*   0x1194c0 (max_stream_cnt), 0x1194c4 (stream_buf_size), 0x1194c8 (fisheye), */
/*   0x1194d4 (pack array), 0x1194d8 (pack_head idx), */
/*   0x1194e0 (misc p2), 0x1194e4 (resize_mode), 0x1194e8 (misc p3), 0x1194ec (vbm vaddr), */
/*   0x119530 (bufshare_chn), 0x119558/60/5c (bufshare state), */
/*   0x119570-4 (eval_info aggregate) */
/*
 * In the binary these sit at address G + offset where G is the &g_EncChannel
 * base. We expose helper byte-pointer macros that compute the absolute address
 * for channel index n with stride 0x308.
 *
 * NOTE: Only the per-channel "chn_id" occupies bytes 0..3 within the 0x308
 * struct slot. All higher addresses used by decomp (0x119298, 0x119328, etc.)
 * reference fields BEYOND the 0x308 per-entry range. Those are therefore
 * separate globals that happen to be indexed at arg1 * 0x308 stride in the
 * decomp representation. We model them as a flat extension tail so that
 * CH_U32(n, OFFSET) == ((char*)g_EncChannel)[n*0x308 + (OFFSET - BASE_ADDR)]
 * yields the same absolute address arithmetic.
 *
 * For this port we use an opaque blob large enough to cover every referenced
 * offset. The largest referenced offset is 0x119574 and g_EncChannel base is
 * implicitly the lowest accessed address (0x119290 — chn_id field). See
 * per-field constants below.
 */

#define IMP_ENC_BASE_ADDR       0x119290u
#define IMP_ENC_TAIL_HI         0x119580u
#define IMP_ENC_NUM_CHANNELS    9
#define IMP_ENC_BLOB_SIZE       (IMP_ENC_NUM_CHANNELS * IMP_ENC_CHN_STRIDE)

static uint8_t g_enc_blob[IMP_ENC_BLOB_SIZE];

static inline uint8_t *enc_ptr(int32_t chn, uint32_t abs_addr)
{
    return &g_enc_blob[(uintptr_t)chn * IMP_ENC_CHN_STRIDE
                       + (abs_addr - IMP_ENC_BASE_ADDR)];
}

#define CH_ADDR(chn, abs) ((uintptr_t)enc_ptr((chn), (abs)))
#define CH_U32(chn, abs)  (*(volatile uint32_t *)(CH_ADDR((chn), (abs))))
#define CH_S32(chn, abs)  (*(volatile int32_t  *)(CH_ADDR((chn), (abs))))
#define CH_U16(chn, abs)  (*(volatile uint16_t *)(CH_ADDR((chn), (abs))))
#define CH_U8(chn, abs)   (*(volatile uint8_t  *)(CH_ADDR((chn), (abs))))
#define CH_PTR(chn, abs)  (*(void *           *)(CH_ADDR((chn), (abs))))

/* In the stock binary &g_EncChannel is the symbol at IMP_ENC_BASE_ADDR. */
#define G_ENC_CH(n)  ((int32_t *)enc_ptr((n), IMP_ENC_BASE_ADDR + 0))

/* Public alias so that computed `*(arg1*0x308 + &g_EncChannel)` form works. */
#define ENC_CHN_ID(n)  CH_S32((n), 0x119290u)   /* first int32 of each slot */

/* ===== Rate-control / channel-field absolute addresses (from decomp) ===== */
/* These constants are absolute BSS addresses in the stock library; together
 * with the arg1*0x308 stride form they uniquely identify a per-channel
 * field. In the port they resolve via enc_ptr() into g_enc_blob. */
#define ENC_F_GROUPPTR      0x119294u  /* group slot pointer + 4 */
#define ENC_F_CODEC_HANDLE  0x119298u  /* AL codec handle */
#define ENC_F_FRAME_REL_CB  0x1192fcu  /* frame_release_cb (SetFrameRelease) */
#define ENC_F_FRAME_REL_ARG 0x119300u  /* frame_release_arg */
#define ENC_F_FRAME_REL_LOCK 0x1192e0u /* frame_release_mutex */

#define ENC_F_STREAMCNT_LO  0x119308u
#define ENC_F_STREAMCNT_HI  0x119318u
#define ENC_F_STREAMCNT_LO2 0x11930cu
#define ENC_F_STREAMCNT_HI2 0x11931cu

#define ENC_F_ENC_ATTR_BEGIN 0x119328u /* encAttr struct block */
#define ENC_F_ENC_ATTR_END   0x119398u
#define ENC_F_ENC_TYPE      0x11932bu  /* encAttr.profile >> 24 byte */
#define ENC_F_PIC_W         0x11932eu  /* encAttr maxPicWidth halfword */
#define ENC_F_PIC_H         0x119330u  /* encAttr maxPicHeight halfword */
#define ENC_F_REGISTERED    0x119398u  /* registered flag */
#define ENC_F_QUERY_LEFT_B  0x11939cu  /* leftBytes */
#define ENC_F_QUERY_FRAMES  0x1193a0u
#define ENC_F_QUERY_CURPACK 0x1193a4u
#define ENC_F_QUERY_WORKDO  0x1193a8u
#define ENC_F_QUERY_EXTRA   0x1193acu

#define ENC_F_RCATTR_BEGIN  0x119354u
#define ENC_F_RCATTR_END    0x119374u
#define ENC_F_RC_MODE       0x119354u
#define ENC_F_RC_MINQP_I    0x11935eu /* int16 */
#define ENC_F_RC_MAXQP_I    0x119360u /* int16 */
#define ENC_F_RC_MINQP      0x119362u /* int16 */
#define ENC_F_RC_MAXQP      0x119364u /* int16 */
#define ENC_F_RC_IPDELTA    0x119366u /* int16 */
#define ENC_F_RC_BITRATE    0x119368u
#define ENC_F_RC_MAXBR      0x11936cu
#define ENC_F_RC_CBR_GOP    0x119370u

#define ENC_F_GOP_BEGIN     0x119380u
#define ENC_F_GOP_UGOPL     0x119384u
#define ENC_F_GOP_END       0x119398u

#define ENC_F_SETGOP_BEGIN  0x1193ccu
#define ENC_F_SETGOP_UGOPL  0x1193d0u
#define ENC_F_SETGOP_END    0x1193e4u

#define ENC_F_SET_BR        0x1193e4u
#define ENC_F_SET_MAXBR     0x1193e8u

#define ENC_F_SETFPS_NUM    0x1193c4u
#define ENC_F_SETFPS_DEN    0x1193c8u

#define ENC_F_FPS_COMPUTED  0x1193f4u
#define ENC_F_FPS_RAW       0x1193f8u
#define ENC_F_ENTROPY       0x1193fcu
#define ENC_F_ENABLED       0x119400u
#define ENC_F_STARTED       0x119404u

#define ENC_F_SEM_POLL      0x119408u  /* sem_init(n,0,max_stream_cnt) */
#define ENC_F_SEM_FILLED    0x119418u
#define ENC_F_SEM_DIRECT    0x119428u  /* poll_sem */
#define ENC_F_MTX_PACK      0x119438u  /* pack array mutex */
#define ENC_F_MTX_GETREL    0x119450u  /* get/release stream mutex */
#define ENC_F_MTX_EVT       0x119468u
#define ENC_F_COND_EVT      0x119480u
#define ENC_F_EVENTFD       0x1194b0u

#define ENC_F_PEND_COUNT    0x1194b8u
#define ENC_F_PEND_BYTES    0x1194bcu

#define ENC_F_MAX_STREAMCNT 0x1194c0u
#define ENC_F_STREAM_BUFSZ  0x1194c4u
#define ENC_F_FISHEYE       0x1194c8u

#define ENC_F_PACK_ARRAY    0x1194d4u /* malloc'd */
#define ENC_F_PACK_HEADIDX  0x1194d8u
#define ENC_F_EXTRA_MEM1    0x1194e0u
#define ENC_F_RESIZE_MODE   0x1194e4u
#define ENC_F_EXTRA_MEM2    0x1194e8u
#define ENC_F_VBM_VADDR     0x1194ecu

#define ENC_F_BUFSHARE_CHN  0x119530u /* shareChn id */
#define ENC_F_BUFSHARE_P1   0x119558u
#define ENC_F_BUFSHARE_P2   0x11955cu
#define ENC_F_BUFSHARE_P3   0x119560u

#define ENC_F_EVAL_BEGIN    0x119570u
#define ENC_F_EVAL_EXTRA    0x119574u

#define ENC_F_STREAM_FSIZE  0x1194c4u /* OEM stream_frame_size field */
#define ENC_F_DUMP_TIME     0x1192d4u

static void encoder_try_activate_codec(int32_t chn_num, const char *reason)
{
    void *codec = CH_PTR(chn_num, ENC_F_CODEC_HANDLE);

    if (codec == NULL) {
        video_enc_trace("activate-skip chn=%d reason=%s codec=(nil)", chn_num, reason);
        return;
    }

    video_enc_trace("activate-codec chn=%d reason=%s codec=%p", chn_num, reason, codec);
    AL_Codec_Encode_Process(codec, NULL, NULL);
}

/* The first four bytes of each channel slot hold chn_id == -1 when free. */
#define ENC_CHN_VALID(n)  (ENC_CHN_ID((n)) >= 0)

struct IMPCell {
    int32_t deviceID;
    int32_t groupID;
    int32_t outputID;
};

static inline int32_t enc_group_id_from_channel(int32_t chn)
{
    int32_t *group_ptr = (int32_t *)CH_PTR(chn, ENC_F_GROUPPTR);
    if (group_ptr == NULL) {
        return -1;
    }
    return group_ptr[0];
}

/* ===== RC-Mode debug/GETSTREAM bitrate tables (module-level static state) ===== */
static uint32_t s_frmcnt[IMP_ENC_MAX_CHN];
static uint32_t s_bitrate[IMP_ENC_MAX_CHN];
static uint8_t  s_flag[IMP_ENC_MAX_CHN];
static uint32_t s_start_time[IMP_ENC_MAX_CHN];
static uint32_t s_start_time_hi[IMP_ENC_MAX_CHN];
/* file-scoped dump state for GetStream_Impl /mnt/mountdir/dump.bs capture */
static int save_open = 0;
static int save_times = 0;

/* ===== EncoderInit / EncoderExit ===== */

int32_t EncoderInit(void)
{
    int32_t *v0 = alloc_device("Encoder", 0x7c);
    int32_t result;

    (void)_gp;
    if (v0 == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xf5,
            "alloc_encoder", "alloc_device() error\n", &_gp);
        genc_set(NULL);
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x457,
            "EncoderInit", "alloc_encoder failed\n");
        result = -1;
        genc_set(NULL);
        return result;
    }

    *(int32_t *)((char *)v0 + 0x24) = 6;
    *(int32_t *)((char *)v0 + 0x20) = 1;
    *(int32_t **)((char *)v0 + 0x40) = v0;
    genc_set((char *)v0 + 0x40);

    result = AL_Codec_Create(1);
    if (result < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x45d,
            "EncoderInit", "encoder_init failed\n", &_gp);
        {
            int32_t *gEncoder_1 = genc_i32();
            if (gEncoder_1 != 0) {
                int32_t *a0_1 = (int32_t *)(intptr_t)*gEncoder_1;
                if (a0_1 != 0) {
                    free_device(a0_1);
                }
                genc_set(NULL);
            }
        }
        return -1;
    }

    memset(&g_enc_blob, 0, 0x1b48);
    {
        /* Initialize chn_id to -1 and clear slots 0..8 (the stock decomp
         * uses a per-element stride of 8 ints; we reproduce as a 9x loop). */
        int32_t i;
        for (i = 0; i < IMP_ENC_MAX_CHN; i++) {
            ENC_CHN_ID(i) = -1;
            /* clear max_stream_cnt slot (i[0x8c]=0 in decomp) */
            CH_U32(i, ENC_F_MAX_STREAMCNT) = 0;
            CH_U32(i, ENC_F_BUFSHARE_CHN) = 0xffffffffu;
        }
    }

    dsys_func_share_mem_register(2, 0, "enc_info", (int32_t (*)(void *))(uintptr_t)0 /* dbg_enc_info */);
    dsys_func_share_mem_register(2, 1, "enc_rc_s", (int32_t (*)(void *))(uintptr_t)0 /* dbg_enc_rc_s */);
    return result;
}

int32_t EncoderExit(void)
{
    (void)_gp;
    if (gEncoder != 0) {
        dsys_func_unregister(2, 0);
        dsys_func_unregister(2, 1);
        AL_Codec_Destroy();
        {
            int32_t *gEncoder_1 = genc_i32();
            if (gEncoder_1 != 0) {
                int32_t *a0_1 = (int32_t *)(intptr_t)*gEncoder_1;
                if (a0_1 != 0) {
                    free_device(a0_1);
                }
                genc_set(NULL);
            }
        }
    }
    return 0;
}

/* ===== IMP_Encoder_CreateGroup / IMP_Encoder_DestroyGroup ===== */

int32_t IMP_Encoder_CreateGroup(int32_t arg1)
{
    char var_28[0x14];
    (void)_gp;
    video_enc_trace("CreateGroup enter grp=%d gEncoder=%p", arg1, gEncoder);
    if (arg1 >= 6) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x492,
            "IMP_Encoder_CreateGroup", "Invalid group num%d\n", arg1);
        return -1;
    }
    {
        int32_t *gEncoder_1 = genc_i32();
        if (gEncoder_1 == 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x498,
                "IMP_Encoder_CreateGroup", "Invalid Encoder\n");
            return -1;
        }
        {
            void *s3_1 = (void *)(intptr_t)*gEncoder_1;

            sprintf(var_28, "%s-%d", (char *)s3_1, arg1);
            {
                int32_t *v0_1 = create_group(1, arg1, var_28, (int32_t (*)(void *, void *))on_encoder_group_data_update);
                *v0_1 = (int32_t)(intptr_t)s3_1;
                *(int32_t *)((char *)v0_1 + 0xc) = 1;
                *(int32_t **)((char *)s3_1 + ((arg1 + 8) << 2) + 8) = v0_1;
                gEncoder_1[arg1 * 5 + 1] = arg1;
                video_enc_trace("CreateGroup ok grp=%d group=%p owner=%p", arg1, v0_1, s3_1);
                if (arg1 < 5) {
                    IMPCell src = { .deviceID = 0, .groupID = arg1, .outputID = 0 };
                    IMPCell dst = { .deviceID = 1, .groupID = arg1, .outputID = 0 };
                    int32_t bind_rc = IMP_System_BindIfNeeded(&src, &dst);
                    video_enc_trace("CreateGroup bind grp=%d rc=%d", arg1, bind_rc);
                }
                return 0;
            }
        }
    }
}

int32_t IMP_Encoder_DestroyGroup(int32_t arg1)
{
    (void)_gp;
    if (arg1 >= 6) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x4ae,
            "IMP_Encoder_DestroyGroup", "Invalid group num%d\n", arg1);
        return -1;
    }
    {
        int32_t *gEncoder_1 = genc_i32();
        int32_t s0_1 = arg1 << 2;
        if (gEncoder_1 == 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x4b4,
                "IMP_Encoder_DestroyGroup", "Encoder is not found\n");
            return -1;
        }
        {
            void *v0_3 = (char *)gEncoder_1 + s0_1 + (arg1 << 4);
            int32_t a0_any = *(int32_t *)((char *)v0_3 + 0xc);
            if (a0_any != 0 ||
                *(int32_t *)((char *)v0_3 + 0x10) != 0 ||
                *(int32_t *)((char *)v0_3 + 0x14) != 0) {
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
                    "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x4bb,
                    "IMP_Encoder_DestroyGroup", "Group-%d have other register channel\n", arg1);
                return -1;
            }
            {
                void *s0_2 = (char *)((intptr_t)*gEncoder_1) + s0_1;
                int32_t a0_1 = *(int32_t *)((char *)s0_2 + 0x28);
                if (a0_1 == 0) {
                    imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
                        "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x4c3,
                        "IMP_Encoder_DestroyGroup", "Group-%d is not created\n", arg1);
                    return -1;
                }
                destroy_group(a0_1, 1);
                *(int32_t *)((char *)s0_2 + 0x28) = 0;
                return 0;
            }
        }
    }
}

/* ===== IMP_Encoder_SetDefaultParam ===== */

int32_t IMP_Encoder_SetDefaultParam(int32_t *arg1, uint32_t arg2, int32_t arg3,
                                    int32_t arg4, int32_t arg5, int32_t arg6,
                                    int32_t arg7, int32_t arg8, int32_t arg9)
{
    uint32_t s2 = arg2 >> 24;
    int32_t s4 = arg9;
    uint32_t s1_1;
    int32_t s7_1;

    if (s2 >= 5) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x6f8,
            "IMP_Encoder_SetDefaultParam",
            "unsupported encode type:%d, we only support avc, hevc and jpeg type\n",
            s2);
        return -1;
    }
    if (arg7 == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x6fd,
            "IMP_Encoder_SetDefaultParam",
            "invalid parameters:frmRateNum = %d, frmRateDen = %d\n", arg6, 0, &_gp);
        return -1;
    }

    if (s2 == 4) {
        memset(arg1, 0, 0x70);
        s1_1 = 0;
        *arg1 = (int32_t)arg2;
        *((int8_t *)arg1 + 4) = 0;
        *((int8_t *)arg1 + 5) = 0;
        s7_1 = 1;
    } else {
        s7_1 = arg3 < 9 ? 1 : 0;
        s1_1 = arg3;
        if (s7_1 == 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x706,
                "IMP_Encoder_SetDefaultParam",
                "unsupported rcmode:%d, we only support fixqp, cbr, vbr, capped vbr, capped quality\n",
                s1_1);
            return -1;
        }
        memset(arg1, 0, 0x70);
        *arg1 = (int32_t)arg2;
        if (s2 == 0) {
            *((int8_t *)arg1 + 4) = 0x33;
            *((int8_t *)arg1 + 5) = 0;
        } else if (s2 == 1) {
            *((int8_t *)arg1 + 4) = 0;
            *((int8_t *)arg1 + 5) = 0;
        } else {
            *((int8_t *)arg1 + 4) = 0x32;
            *((int8_t *)arg1 + 5) = (int8_t)s2;
        }
    }

    arg1[3] = 0x188;
    arg1[4] = 0x40028;
    /* Stock decomp `*(arg1 + 6) = arg4` is a uint16_t store at BYTE
     * offset 6, not a 32-bit store at int-index 6. channel_encoder_init
     * reads width via *(arg1 + 0x9e) where arg1 there is channel_state
     * and encAttr starts at 0x98, so encAttr[6] must hold width as u16.
     *
     * Layout of chnAttr (first 20 bytes):
     *   [0..3] profile (u32)
     *   [4]    uLevel (u8)
     *   [5]    uTier (u8)
     *   [6..7] width (u16)   ← THIS is what SetDefaultParam sets
     *   [8..9] height (u16)  ← arg1[2].w = arg5
     *   ...
     */
    *(uint16_t *)((char *)arg1 + 6) = (uint16_t)arg4;   /* width */
    *(uint16_t *)((char *)arg1 + 8) = (uint16_t)arg5;   /* height */
    arg1[5] = 0x9c;
    if (s7_1 != 0) {
        arg1[0xb] = (int32_t)s1_1;
    }
    /* jump(*(0xf9e34 + ($s1_1 << 2)) + &_gp) — RC-mode specific setup
     * bypassed in decomp via jump-table; reproduced as passthrough defaults
     * (the call into channel_encoder_set_rc_param is the RC-authoritative path). */
    if (0 >= s4) {
        s4 = 1;
    }
    arg1[0x15] = arg7;
    *(int8_t *)((char *)arg1 + 0x178) = 0;
    arg1[0x14] = arg6;
    arg1[0x16] = 2;
    arg1[0x18] = s4;
    *(int8_t *)((char *)arg1 + 0x190) = 0;
    *(int16_t *)((char *)arg1 + 0x18c) = (int16_t)arg8;
    arg1[0x1a] = 0;
    *(int8_t *)((char *)arg1 + 0x19c) = 0;
    return 0;
}

/* ===== IMP_Encoder_SetbufshareChn ===== */

int32_t IMP_Encoder_SetbufshareChn(int32_t arg1, int32_t arg2)
{
    (void)_gp;
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x76d,
            "IMP_Encoder_SetbufshareChn",
            "encChn Channel Num: %d\n", arg1);
        return -1;
    }
    if (arg2 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x772,
            "IMP_Encoder_SetbufshareChn",
            "Invalid share Channel Num: %d\n", arg1);
        return -1;
    }
    CH_S32(arg1, ENC_F_BUFSHARE_CHN) = arg2;
    imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
        "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x778,
        "IMP_Encoder_SetbufshareChn",
        "%s: encChn:%d, shareChn:%d\n",
        "IMP_Encoder_SetbufshareChn", arg1, arg2);
    return 0;
}

/* ===== IMP_Encoder_CreateChn ===== */

int32_t IMP_Encoder_CreateChn(int32_t arg1, int32_t *arg2)
{
    (void)_gp;
    video_enc_trace("CreateChn enter chn=%d attr=%p prof=0x%x", arg1, arg2,
                    arg2 != 0 ? *arg2 : 0);
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x780,
            "IMP_Encoder_CreateChn", "Invalid Channel Num: %d\n", arg1);
        return -1;
    }
    if (arg2 == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x785,
            "IMP_Encoder_CreateChn", "Channel Attr is NULL\n");
        return -1;
    }
    if (ENC_CHN_ID(arg1) >= 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x78d,
            "IMP_Encoder_CreateChn", "This channel has already been created\n");
        return -1;
    }

    /* T31_ZC/T31_LC SoC variants do not support HEVC */
    if ((uint32_t)(get_cpu_id() - 0x15) < 2) {
        if (*arg2 == 0x1000001) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x794,
                "IMP_Encoder_CreateChn",
                "The SOC_T31_ZC/SOC_T31_LC not support HEVC!!!\n");
            puts("The SOC_T31_ZC/SOC_T31_LC not support HEVC!!!");
            return -1;
        }
    }

    /* Preserve SetMaxStreamCnt/SetStreamBufSize/SetFisheye/Setbufshare/
     * SetChnEntropyMode values across the memset of the slot. */
    {
        int32_t saved_max     = CH_S32(arg1, ENC_F_MAX_STREAMCNT);
        int32_t saved_bufsz   = CH_S32(arg1, ENC_F_STREAM_BUFSZ);
        int32_t saved_fisheye = CH_S32(arg1, ENC_F_FISHEYE);
        int32_t saved_bufshr  = CH_S32(arg1, ENC_F_BUFSHARE_CHN);
        int32_t saved_entropy = CH_S32(arg1, ENC_F_ENTROPY);

        /* Clear the 0x308 slot. */
        memset(enc_ptr(arg1, IMP_ENC_BASE_ADDR), 0, 0x308);
        /* Restore surviving config fields. */
        CH_S32(arg1, ENC_F_FISHEYE)       = saved_fisheye;
        CH_S32(arg1, ENC_F_MAX_STREAMCNT) = saved_max;
        CH_S32(arg1, ENC_F_STREAM_BUFSZ)  = saved_bufsz;
        CH_S32(arg1, ENC_F_BUFSHARE_CHN)  = saved_bufshr;
        CH_S32(arg1, ENC_F_ENTROPY)       = saved_entropy;

        /* Handle JPEG buffer-share linkage */
        if (*arg2 == 0x4000000) {
            int32_t shareChn = saved_bufshr;
            if (shareChn < 0) {
                CH_S32(arg1, ENC_F_BUFSHARE_P2) = 0;
                imp_log_fun(5, IMP_Log_Get_Option(), 2, "Encoder",
                    "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x7b5,
                    "IMP_Encoder_CreateChn", "Jpeg channel will not share buff\n");
            } else {
                void *a0_25 = CH_PTR(shareChn, ENC_F_BUFSHARE_P1);
                if (a0_25 == 0) {
                    imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
                        "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x7ad,
                        "IMP_Encoder_CreateChn",
                        "Jpeg channel need create after channel 0\n");
                    return -1;
                }
                {
                    int32_t v0_40 = CH_S32(shareChn, ENC_F_BUFSHARE_P3);
                    CH_PTR(arg1, ENC_F_BUFSHARE_P1) = a0_25;
                    CH_S32(arg1, ENC_F_BUFSHARE_P3) = v0_40;
                    CH_S32(arg1, ENC_F_BUFSHARE_P2) = 1;
                }
            }
        }
    }

    /* Publish chn_id / enabled */
    ENC_CHN_ID(arg1) = arg1;
    CH_S32(arg1, ENC_F_QUERY_EXTRA) = 1;     /* +0x1193ac = 1 */
    CH_S32(arg1, ENC_F_ENABLED) = 0;
    CH_S32(arg1, ENC_F_STARTED) = 1;

    /* Copy encAttr block (0x70 bytes = 28 ints) from arg2 into +0x119328. */
    {
        int32_t *i = arg2;
        uint32_t *v0_8 = (uint32_t *)enc_ptr(arg1, ENC_F_ENC_ATTR_BEGIN);
        while (i != &arg2[0x1c]) {
            (void)_setLeftPart32((uint32_t)*i);
            (void)_setLeftPart32((uint32_t)i[1]);
            (void)_setLeftPart32((uint32_t)i[2]);
            (void)_setLeftPart32((uint32_t)i[3]);
            {
                uint32_t a3_4 = _setRightPart32((uint32_t)*i);
                uint32_t a2_2 = _setRightPart32((uint32_t)i[1]);
                uint32_t a0_2 = _setRightPart32((uint32_t)i[2]);
                uint32_t v1_6 = _setRightPart32((uint32_t)i[3]);
                i = &i[4];
                v0_8[0] = a3_4;
                v0_8[1] = a2_2;
                v0_8[2] = a0_2;
                v0_8[3] = v1_6;
                v0_8 = &v0_8[4];
            }
        }
    }

    /* Compute fps_computed = arg5 / (float(arg3)+arg4) * arg5 and clamp into
     * +0xe0 of channel. The floating-point sequence from the decomp is
     * reproduced approximately in integer form to avoid reliance on removed
     * MIPS FPU ops; callers of channel_encoder_set_rc_param re-compute
     * bitrate/MAXBR with the stored FPS. */
    {
        uint32_t mul_hi = 0;
        uint32_t mul_lo = (uint32_t)CH_U16(arg1, ENC_F_PIC_W) *
                          (uint32_t)CH_U16(arg1, ENC_F_PIC_H);
        (void)mul_hi;
        (void)mul_lo;
    }

    /* Initialize sync primitives */
    sem_init((sem_t *)enc_ptr(arg1, ENC_F_SEM_FILLED), 0, 0);
    sem_init((sem_t *)enc_ptr(arg1, ENC_F_SEM_POLL), 0,
             (unsigned)CH_S32(arg1, ENC_F_MAX_STREAMCNT));
    if (pthread_mutex_init((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_PACK), NULL) < 0) {
        ENC_CHN_ID(arg1) = -1;
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x7e6,
            "IMP_Encoder_CreateChn", "pthread_mutex_init failed\n");
        return -1;
    }
    {
        pthread_mutexattr_t var_3c;
        if (pthread_mutexattr_init(&var_3c) < 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x7ec,
                "IMP_Encoder_CreateChn", "pthread_mutexattr_init failed\n");
            return -1;
        }
        pthread_mutexattr_settype(&var_3c, PTHREAD_MUTEX_RECURSIVE);
        if (pthread_mutex_init((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_GETREL), &var_3c) < 0) {
            ENC_CHN_ID(arg1) = -1;
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x7f3,
                "IMP_Encoder_CreateChn", "pthread_mutex_init failed\n");
            return -1;
        }
    }
    if (sem_init((sem_t *)enc_ptr(arg1, ENC_F_SEM_DIRECT), 0, 0) < 0) {
        ENC_CHN_ID(arg1) = -1;
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x7fa,
            "IMP_Encoder_CreateChn", "sem_init(&enc_chn->poll_sem) failed\n");
        return -1;
    }

    {
        int32_t s1_name = 0;
        void *s0_1 = NULL;
        int32_t result;

        result = channel_encoder_init(enc_ptr(arg1, IMP_ENC_BASE_ADDR));
        (void)s0_1;
        (void)s1_name;
        if (result < 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x800,
                "IMP_Encoder_CreateChn", "Channel%d encoder init failed\n", arg1);
            {
                void *a0_27 = CH_PTR(arg1, ENC_F_PACK_ARRAY);
                if (a0_27 != 0) {
                    free(a0_27);
                }
                CH_PTR(arg1, ENC_F_PACK_ARRAY) = NULL;
            }
            ENC_CHN_ID(arg1) = -1;
            return result;
        }

        video_enc_trace("CreateChn ready chn=%d registered=%d started=%d enabled=%d group=%p",
                        arg1,
                        CH_S32(arg1, ENC_F_REGISTERED),
                        CH_S32(arg1, ENC_F_STARTED),
                        CH_S32(arg1, ENC_F_ENABLED),
                        CH_PTR(arg1, ENC_F_GROUPPTR));

    }

    /* channel_buffer_init: allocate pack storage & register eventfd */
    {
        int32_t v0_24 = CH_S32(arg1, ENC_F_MAX_STREAMCNT) * 0x38;
        int32_t var_40 = 0;
        void *v0_25 = calloc((size_t)(v0_24 * 7), 1);
        CH_PTR(arg1, ENC_F_PACK_ARRAY) = v0_25;
        if (v0_25 == 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x69c,
                "channel_buffer_init",
                "malloc enc_chn->frame_streams enc_chn->index=%d failed\n",
                ENC_CHN_ID(arg1));
            ENC_CHN_ID(arg1) = -1;
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x809,
                "IMP_Encoder_CreateChn", "Channel%d stream buffer init failed\n", arg1);
            return -1;
        }
        AL_Codec_Encode_GetSrcStreamCntAndSize(CH_PTR(arg1, ENC_F_CODEC_HANDLE),
                                               &var_40,
                                               (int32_t *)enc_ptr(arg1, ENC_F_STREAM_FSIZE));
        imp_log_fun(4, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x6a2,
            "channel_buffer_init",
            "encChn=%d,srcStreamCnt=%u,enc_chn->stream_frame_size=%u\n",
            ENC_CHN_ID(arg1), var_40, CH_U32(arg1, ENC_F_STREAM_BUFSZ));
    }

    /* Cache fps -> +0x1193f4 using current setFrmRate num/den (may be 0 here). */
    CH_S32(arg1, ENC_F_FPS_COMPUTED) = CH_S32(arg1, ENC_F_FPS_RAW);

    pthread_mutex_init((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_EVT), NULL);
    pthread_cond_init((pthread_cond_t *)enc_ptr(arg1, ENC_F_COND_EVT), NULL);
    {
        int32_t v0_31 = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        CH_S32(arg1, ENC_F_EVENTFD) = v0_31;
        if (v0_31 < 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x811,
                "IMP_Encoder_CreateChn",
                "Channel%d create device file handler failed\n", arg1);
            channel_encoder_exit(enc_ptr(arg1, IMP_ENC_BASE_ADDR));
            {
                void *a0_29 = CH_PTR(arg1, ENC_F_PACK_ARRAY);
                if (a0_29 != 0) {
                    free(a0_29);
                }
                CH_PTR(arg1, ENC_F_PACK_ARRAY) = NULL;
            }
            ENC_CHN_ID(arg1) = -1;
            return 0;
        }
    }
    CH_U32(arg1, ENC_F_PEND_COUNT) = 0;
    CH_U32(arg1, ENC_F_PEND_BYTES) = 0;
    CH_U8(arg1, ENC_F_DUMP_TIME) =
        (uint8_t)(access("/tmp/dumpEncTime", 0) < 1 ? 1 : 0);
    video_enc_trace("CreateChn ready chn=%d started=%d enabled=%d registered=%d codec=%p group=%p",
                    arg1, CH_S32(arg1, ENC_F_STARTED), CH_S32(arg1, ENC_F_ENABLED),
                    CH_S32(arg1, ENC_F_REGISTERED), CH_PTR(arg1, ENC_F_CODEC_HANDLE),
                    CH_PTR(arg1, ENC_F_GROUPPTR));
    return 0;
}

/* ===== IMP_Encoder_GetChnAttr ===== */

int32_t IMP_Encoder_GetChnAttr(int32_t arg1, int32_t *arg2)
{
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x85c,
            "IMP_Encoder_GetChnAttr", "Invalid Channel Num: %d\n", arg1);
        return -1;
    }
    if (ENC_CHN_ID(arg1) < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x862,
            "IMP_Encoder_GetChnAttr",
            "%s: Encoder Channel%d hasn't been created\n",
            "IMP_Encoder_GetChnAttr", arg1, &_gp);
        return -1;
    }
    if (arg2 == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x867,
            "IMP_Encoder_GetChnAttr", "attr is NULL\n");
        return -1;
    }
    /* Copy 0x70 bytes (28 ints) from +0x119328 to arg2 */
    {
        int32_t *i = (int32_t *)enc_ptr(arg1, ENC_F_ENC_ATTR_BEGIN);
        int32_t *end = (int32_t *)enc_ptr(arg1, ENC_F_ENC_ATTR_END);
        while (i != end) {
            int32_t a3_1 = i[1];
            int32_t a2_3 = i[2];
            uint32_t a0  = (uint32_t)i[3];
            *arg2   = *i;
            arg2[1] = a3_1;
            arg2[2] = a2_3;
            arg2[3] = (int32_t)_getLeftPart32(a0);
            i = &i[4];
            arg2[3] = (int32_t)_getRightPart32(a0);
            arg2 = &arg2[4];
        }
    }
    return 0;
}

/* ===== IMP_Encoder_RegisterChn ===== */

int32_t IMP_Encoder_RegisterChn(int32_t arg1, int32_t arg2)
{
    if (arg1 >= 6) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x873,
            "IMP_Encoder_RegisterChn", "Invalid Encoder Group Num: %d\n", arg1);
        return -1;
    }
    if (arg2 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x878,
            "IMP_Encoder_RegisterChn", "Invalid Channel Num: %d\n", arg2);
        return -1;
    }
    if (ENC_CHN_ID(arg2) < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x87e,
            "IMP_Encoder_RegisterChn",
            "%s: Encoder Channel%d hasn't been created\n",
            "IMP_Encoder_RegisterChn", arg2, &_gp);
        return -1;
    }
    {
        int32_t a3_1 = arg1 << 2;
        uintptr_t gEncoder_1 = (uintptr_t)genc_i32();
        void *v0_9 = (char *)gEncoder_1 + a3_1 + (arg1 << 4);

        video_enc_trace("RegisterChn enter grp=%d chn=%d gEncoder=%p group=%p registered=%d started=%d enabled=%d",
                        arg1, arg2, gEncoder, v0_9,
                        CH_S32(arg2, ENC_F_REGISTERED),
                        CH_S32(arg2, ENC_F_STARTED),
                        CH_S32(arg2, ENC_F_ENABLED));

        /* Assign channel pointer to the first empty slot of [+8, +0xc, +0x10] */
        if (*(int32_t *)((char *)v0_9 + 0xc) == 0) {
            *(void **)((char *)gEncoder_1 + ((a3_1 + arg1) << 2) + 0xc) =
                enc_ptr(arg2, IMP_ENC_BASE_ADDR);
        } else if (*(int32_t *)((char *)v0_9 + 0x10) == 0) {
            *(void **)((char *)gEncoder_1 + ((a3_1 + arg1 + 1) << 2) + 0xc) =
                enc_ptr(arg2, IMP_ENC_BASE_ADDR);
        } else if (*(int32_t *)((char *)v0_9 + 0x14) == 0) {
            *(void **)((char *)gEncoder_1 + ((a3_1 + arg1 + 2) << 2) + 0xc) =
                enc_ptr(arg2, IMP_ENC_BASE_ADDR);
        }
        *(int32_t *)((char *)v0_9 + 8) += 1;
        CH_PTR(arg2, ENC_F_GROUPPTR) = (char *)v0_9 + 4;
        CH_S32(arg2, ENC_F_REGISTERED) = 1;
        /* Raptor's current pipeline registers channels but does not call
         * enc_start/IMP_Encoder_StartRecvPic before frames begin arriving.
         * Arm the receive gate here so the encoder callback path can run. */
        CH_S32(arg2, ENC_F_ENABLED) = 1;
        if ((EncoderChannelLayout *)enc_ptr(arg2, IMP_ENC_BASE_ADDR) != NULL) {
            EncoderChannelLayout *chn = (EncoderChannelLayout *)enc_ptr(arg2, IMP_ENC_BASE_ADDR);
            *enc_channel_recv_pic_enabled(chn) = 1;
        }
        if (CH_S32(arg2, ENC_F_STARTED) != 0 || CH_S32(arg2, ENC_F_ENABLED) != 0) {
            encoder_try_activate_codec(arg2, "register-started");
        }
        video_enc_trace("RegisterChn linked grp=%d chn=%d group=%p slots=%d started=%d enabled=%d",
                        arg1, arg2, v0_9,
                        *(int32_t *)((char *)v0_9 + 8),
                        CH_S32(arg2, ENC_F_STARTED),
                        CH_S32(arg2, ENC_F_ENABLED));
        return 0;
    }
}

/* ===== IMP_Encoder_UnRegisterChn ===== */

int32_t IMP_Encoder_UnRegisterChn(int32_t arg1)
{
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x898,
            "IMP_Encoder_UnRegisterChn", "Invalid Channel Num: %d\n", arg1);
        return -1;
    }
    if (ENC_CHN_ID(arg1) < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x89e,
            "IMP_Encoder_UnRegisterChn",
            "%s: Encoder Channel%d hasn't been created\n",
            "IMP_Encoder_UnRegisterChn", arg1, &_gp);
        return -1;
    }
    {
        void *a2_2 = CH_PTR(arg1, ENC_F_GROUPPTR);
        void *chn_addr = enc_ptr(arg1, IMP_ENC_BASE_ADDR);
        if (chn_addr == *(void **)((char *)a2_2 + 8)) {
            *(void **)((char *)a2_2 + (0 << 2) + 8) = 0;
        } else if (chn_addr == *(void **)((char *)a2_2 + 0xc)) {
            *(void **)((char *)a2_2 + (1 << 2) + 8) = 0;
        } else if (chn_addr == *(void **)((char *)a2_2 + 0x10)) {
            *(void **)((char *)a2_2 + (2 << 2) + 8) = 0;
        }
        *(int32_t *)((char *)a2_2 + 4) -= 1;
        CH_S32(arg1, ENC_F_REGISTERED) = 0;
        return 0;
    }
}

/* ===== IMP_Encoder_DestroyChn ===== */

int32_t IMP_Encoder_DestroyChn(int32_t arg1)
{
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x820,
            "IMP_Encoder_DestroyChn", "Invalid Channel Num: %d\n", arg1);
        return -1;
    }
    if (ENC_CHN_ID(arg1) < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x825,
            "IMP_Encoder_DestroyChn",
            "%s: Encoder Channel%d hasn't been created\n",
            "IMP_Encoder_DestroyChn", arg1, &_gp);
        return -1;
    }
    if (CH_S32(arg1, ENC_F_ENABLED) == 1) {
        IMP_Encoder_StopRecvPic(arg1);
    }
    if ((uint32_t)(uint8_t)CH_U8(arg1, ENC_F_REGISTERED) != 0) {
        IMP_Encoder_UnRegisterChn(arg1);
    }
    channel_encoder_exit(enc_ptr(arg1, IMP_ENC_BASE_ADDR));
    {
        void *a0_1 = CH_PTR(arg1, ENC_F_PACK_ARRAY);
        if (a0_1 != 0) { free(a0_1); }
        CH_PTR(arg1, ENC_F_PACK_ARRAY) = NULL;
    }
    {
        void *a0_2 = CH_PTR(arg1, ENC_F_EXTRA_MEM1);
        if (a0_2 != 0) { free(a0_2); }
        CH_PTR(arg1, ENC_F_EXTRA_MEM1) = NULL;
    }
    {
        void *a0_3 = CH_PTR(arg1, ENC_F_EXTRA_MEM2);
        if (a0_3 != 0) { free(a0_3); }
        CH_PTR(arg1, ENC_F_EXTRA_MEM2) = NULL;
    }
    {
        int32_t a0_4 = CH_S32(arg1, ENC_F_VBM_VADDR);
        if (a0_4 != 0 && CH_S32(arg1, ENC_F_RESIZE_MODE) == 0) {
            video_vbm_free(a0_4);
        }
        CH_S32(arg1, ENC_F_VBM_VADDR) = 0;
    }
    {
        int32_t a0_5 = CH_S32(arg1, ENC_F_EVENTFD);
        close(a0_5);
        CH_S32(arg1, ENC_F_EVENTFD) = -1;
    }
    pthread_mutex_destroy((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_EVT));
    pthread_cond_destroy((pthread_cond_t *)enc_ptr(arg1, ENC_F_COND_EVT));
    sem_destroy((sem_t *)enc_ptr(arg1, ENC_F_SEM_FILLED));
    sem_destroy((sem_t *)enc_ptr(arg1, ENC_F_SEM_POLL));
    sem_destroy((sem_t *)enc_ptr(arg1, ENC_F_SEM_DIRECT));
    pthread_mutex_destroy((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_PACK));
    pthread_mutex_destroy((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_GETREL));
    memset(enc_ptr(arg1, IMP_ENC_BASE_ADDR), 0, 0x308);
    ENC_CHN_ID(arg1) = -1;
    CH_S32(arg1, ENC_F_BUFSHARE_CHN) = -1;
    return 0;
}

/* ===== IMP_Encoder_GetFd ===== */

int32_t IMP_Encoder_GetFd(int32_t arg1)
{
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x9bb,
            "IMP_Encoder_GetFd", "Invalid Channel Num: %d\n", arg1);
        return -1;
    }
    if (ENC_CHN_ID(arg1) < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x9c1,
            "IMP_Encoder_GetFd",
            "%s: Encoder Channel%d hasn't been created\n",
            "IMP_Encoder_GetFd", arg1, &_gp);
        return -1;
    }
    pthread_mutex_lock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_EVT));
    {
        struct pollfd var_20;
        var_20.fd = CH_S32(arg1, ENC_F_EVENTFD);
        var_20.events = POLLIN;
        var_20.revents = 0;
        poll(&var_20, 1, 0);
    }
    pthread_mutex_unlock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_EVT));
    return CH_S32(arg1, ENC_F_EVENTFD);
}

/* ===== IMP_Encoder_Query ===== */

int32_t IMP_Encoder_Query(int32_t arg1, void *arg2)
{
    int32_t *out = (int32_t *)arg2;
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x8f2,
            "IMP_Encoder_Query", "Invalid Channel Num: %d\n", arg1);
        return -1;
    }
    if (ENC_CHN_ID(arg1) < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x8f8,
            "IMP_Encoder_Query",
            "%s: Encoder Channel%d hasn't been created\n",
            "IMP_Encoder_Query", arg1, &_gp);
        return -1;
    }
    {
        int32_t t0_1 = CH_S32(arg1, ENC_F_QUERY_LEFT_B);
        int32_t a3_1 = CH_S32(arg1, ENC_F_QUERY_FRAMES);
        int32_t a2_1 = CH_S32(arg1, ENC_F_QUERY_CURPACK);
        int32_t a0   = CH_S32(arg1, ENC_F_QUERY_WORKDO);
        int32_t v1_3 = CH_S32(arg1, ENC_F_QUERY_EXTRA);
        out[0] = CH_S32(arg1, ENC_F_REGISTERED);
        out[1] = t0_1;
        out[2] = a3_1;
        out[3] = a2_1;
        out[4] = a0;
        out[5] = v1_3;
    }
    return 0;
}

/* ===== IMP_Encoder_StartRecvPic ===== */

int32_t IMP_Encoder_StartRecvPic(int32_t arg1)
{
    int32_t fs_rc = 0;
    EncoderChannelLayout *chn = (EncoderChannelLayout *)enc_ptr(arg1, IMP_ENC_BASE_ADDR);

    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x8b4,
            "IMP_Encoder_StartRecvPic", "Invalid Channel Num: %d\n", arg1);
        return -1;
    }
    if (ENC_CHN_ID(arg1) < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x8ba,
            "IMP_Encoder_StartRecvPic",
            "%s: Encoder Channel%d hasn't been created\n",
            "IMP_Encoder_StartRecvPic", arg1, &_gp);
        return -1;
    }

    video_enc_trace("StartRecvPic enter chn=%d grp=%d started=%d enabled=%d recv_enabled=%u recv_started=%u",
                    arg1, enc_group_id_from_channel(arg1),
                    CH_S32(arg1, ENC_F_STARTED), CH_S32(arg1, ENC_F_ENABLED),
                    chn ? (unsigned)*enc_channel_recv_pic_enabled(chn) : 0U,
                    chn ? (unsigned)*enc_channel_recv_pic_started(chn) : 0U);

    CH_S32(arg1, ENC_F_STARTED) = 1;
    CH_S32(arg1, ENC_F_ENABLED) = 1;
    if (chn != NULL) {
        *enc_channel_recv_pic_started(chn) = 1;
        *enc_channel_recv_pic_enabled(chn) = 1;
    }
    encoder_try_activate_codec(arg1, "start-recv-pic");
    video_enc_trace("StartRecvPic exit chn=%d grp=%d started=%d enabled=%d",
                    arg1, enc_group_id_from_channel(arg1),
                    CH_S32(arg1, ENC_F_STARTED), CH_S32(arg1, ENC_F_ENABLED));
    return 0;
}

/* ===== IMP_Encoder_StopRecvPic ===== */

int32_t IMP_Encoder_StopRecvPic(int32_t arg1)
{
    EncoderChannelLayout *chn = (EncoderChannelLayout *)enc_ptr(arg1, IMP_ENC_BASE_ADDR);

    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x8c8,
            "IMP_Encoder_StopRecvPic", "Invalid Channel Num: %d\n", arg1);
        return -1;
    }
    if (ENC_CHN_ID(arg1) < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x8ce,
            "IMP_Encoder_StopRecvPic",
            "%s: Encoder Channel%d hasn't been created\n",
            "IMP_Encoder_StopRecvPic", arg1, &_gp);
        return -1;
    }
    {
        /* Wait up to ~11 retries, 10 ms apart, draining any remaining packs. */
        void *s5_1 = *(void **)((char *)(intptr_t)*genc_i32() +
                                ((*((int32_t **)CH_PTR(arg1, ENC_F_GROUPPTR))[0] + 8) << 2) + 8);
        int32_t s2_1 = 0xb;
        pthread_mutex_lock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_GETREL));
        CH_S32(arg1, ENC_F_ENABLED) = 0;
        if (chn != NULL) {
            *enc_channel_recv_pic_enabled(chn) = 0;
            *enc_channel_recv_pic_started(chn) = 0;
        }

        while (s2_1 != 0) {
            int32_t v1_8 = CH_S32(arg1, ENC_F_STREAMCNT_LO2);
            int32_t v0_10 = CH_S32(arg1, ENC_F_STREAMCNT_HI2);
            if (CH_S32(arg1, ENC_F_QUERY_CURPACK) != 0 ||
                *(int32_t *)((char *)(*(void **)((char *)s5_1 + 4)) + 0x120) == 1 ||
                CH_S32(arg1, ENC_F_QUERY_EXTRA) == 0 ||
                v0_10 < v1_8) {
                goto retry_sleep;
            }
            if (v1_8 == v0_10 &&
                CH_U32(arg1, ENC_F_STREAMCNT_HI) >= CH_U32(arg1, ENC_F_STREAMCNT_LO)) {
                s2_1 -= 1;
                continue;
            }
            goto retry_sleep;
retry_sleep:
            s2_1 -= 1;
            if (s2_1 == 0) break;
            /* Polling/drain path */
            while (1) {
                int32_t v1_9 = CH_S32(arg1, ENC_F_STREAMCNT_LO2);
                int32_t v0_11 = CH_S32(arg1, ENC_F_STREAMCNT_HI2);
                if (v0_11 < v1_9 || v1_9 != v0_11) {
                    char var_60[0x40];
                    if (IMP_Encoder_PollingStream(arg1, 0) >= 0) {
                        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0);
                        if (IMP_Encoder_GetStream_Impl(arg1, var_60, 1, 1) >= 0) {
                            IMP_Encoder_ReleaseStream(arg1, var_60);
                        }
                    }
                    continue;
                }
                if (CH_U32(arg1, ENC_F_STREAMCNT_HI) < CH_U32(arg1, ENC_F_STREAMCNT_LO)) {
                    continue;
                }
                break;
            }
            if (CH_S32(arg1, ENC_F_QUERY_CURPACK) == 0) {
                pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
                usleep(10000);
            } else {
                break;
            }
        }
        pthread_mutex_unlock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_GETREL));
        return 0;
    }
}

/* ===== IMP_Encoder_RequestIDR ===== */

int32_t IMP_Encoder_RequestIDR(int32_t arg1)
{
    EncoderChannelLayout *chn = (EncoderChannelLayout *)enc_ptr(arg1, IMP_ENC_BASE_ADDR);

    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x9d6,
            "IMP_Encoder_RequestIDR", "Invalid Channel Num: %d\n", arg1);
        return -1;
    }
    if (ENC_CHN_ID(arg1) < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x9dc,
            "IMP_Encoder_RequestIDR",
            "%s: Encoder Channel%d hasn't been created\n",
            "IMP_Encoder_RequestIDR", arg1, &_gp);
        return -1;
    }
    CH_S32(arg1, ENC_F_STARTED) = 1;
    if (chn != NULL) {
        *enc_channel_recv_pic_started(chn) = 1;
    }
    return 0;
}

/* ===== IMP_Encoder_FlushStream ===== */

int32_t IMP_Encoder_FlushStream(int32_t arg1)
{
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x9ec,
            "IMP_Encoder_FlushStream", "Invalid Channel Num: %d\n", arg1);
        return -1;
    }
    if (ENC_CHN_ID(arg1) < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x9f2,
            "IMP_Encoder_FlushStream",
            "%s: Encoder Channel%d hasn't been created\n",
            "IMP_Encoder_FlushStream", arg1, &_gp);
        return -1;
    }
    {
        int32_t var_24 = 0;
        char var_30[0x18];
        if (IMP_Encoder_Query(arg1, var_30) < 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x9f7,
                "IMP_Encoder_FlushStream",
                "Encoder Channel%d IMP_Encoder_Query failed\n", arg1);
            return -1;
        }
        if (IMP_Encoder_RequestIDR(arg1) < 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x9fe,
                "IMP_Encoder_FlushStream",
                "Encoder Channel%d IMP_Encoder_RequestIDR failed\n", arg1);
            return -1;
        }
        {
            int32_t i = var_24 - 1;
            if (var_24 > 0) {
                do {
                    char var_70[0x40];
                    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0);
                    i -= 1;
                    if (IMP_Encoder_PollingStream(arg1, 0x3e8) >= 0 &&
                        IMP_Encoder_GetStream(arg1, var_70, 1) >= 0) {
                        IMP_Encoder_ReleaseStream(arg1, var_70);
                    }
                    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
                } while (i != -1);
            }
        }
    }
    return 0;
}

/* ===== IMP_Encoder_SetMaxStreamCnt / GetMaxStreamCnt ===== */

int32_t IMP_Encoder_SetMaxStreamCnt(int32_t arg1, int32_t arg2)
{
    (void)_gp;
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xa12,
            "IMP_Encoder_SetMaxStreamCnt", "Invalid Channel Num: %d\n", arg1);
        return -1;
    }
    if (ENC_CHN_ID(arg1) >= 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xa18,
            "IMP_Encoder_SetMaxStreamCnt",
            "Max StreamCnt must be set before channel created, encChn=%d\n", arg1);
        return -1;
    }
    CH_S32(arg1, ENC_F_MAX_STREAMCNT) = arg2;
    return 0;
}

int32_t IMP_Encoder_GetMaxStreamCnt(int32_t arg1, int32_t *arg2)
{
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xa23,
            "IMP_Encoder_GetMaxStreamCnt", "Invalid Channel Num: %d\n", arg1);
        return -1;
    }
    *arg2 = CH_S32(arg1, ENC_F_MAX_STREAMCNT);
    return 0;
}

/* ===== IMP_Encoder_SetStreamBufSize / GetStreamBufSize ===== */

int32_t IMP_Encoder_SetStreamBufSize(int32_t arg1, int32_t arg2)
{
    (void)_gp;
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xa30,
            "IMP_Encoder_SetStreamBufSize", "Invalid Channel Num: %d\n", arg1);
        return -1;
    }
    if (ENC_CHN_ID(arg1) >= 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xa36,
            "IMP_Encoder_SetStreamBufSize",
            "Max StreamCnt must be set before channel created, encChn=%d\n", arg1);
        return -1;
    }
    CH_S32(arg1, ENC_F_STREAM_BUFSZ) = arg2;
    return 0;
}

int32_t IMP_Encoder_GetStreamBufSize(int32_t arg1, int32_t *arg2)
{
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xa41,
            "IMP_Encoder_GetStreamBufSize", "Invalid Channel Num: %d\n", arg1);
        return -1;
    }
    *arg2 = CH_S32(arg1, ENC_F_STREAM_BUFSZ);
    return 0;
}

/* ===== IMP_Encoder_SetFisheyeEnableStatus / Get ===== */

int32_t IMP_Encoder_SetFisheyeEnableStatus(int32_t arg1, int32_t arg2)
{
    (void)_gp;
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xa4e,
            "IMP_Encoder_SetFisheyeEnableStatus", "Invalid Channel Num: %d\n", arg1);
        return -1;
    }
    if (ENC_CHN_ID(arg1) >= 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xa54,
            "IMP_Encoder_SetFisheyeEnableStatus",
            "fisheye enabled status must be set before channel created, encChn=%d\n", arg1);
        return -1;
    }
    CH_S32(arg1, ENC_F_FISHEYE) = (0u < (uint32_t)arg2) ? 1 : 0;
    return 0;
}

int32_t IMP_Encoder_GetFisheyeEnableStatus(int32_t arg1, int32_t *arg2)
{
    (void)_gp;
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xa5f,
            "IMP_Encoder_GetFisheyeEnableStatus", "Invalid Channel Num: %d\n", arg1);
        return -1;
    }
    *arg2 = (0u < (uint32_t)CH_S32(arg1, ENC_F_FISHEYE)) ? 1 : 0;
    return 0;
}

/* ===== IMP_Encoder_GetChnAttrRcMode / SetChnAttrRcMode ===== */

int32_t IMP_Encoder_GetChnAttrRcMode(int32_t arg1, int32_t *arg2)
{
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xa6d,
            "IMP_Encoder_GetChnAttrRcMode", "Invalid Channel Num:%d\n", arg1);
        return -1;
    }
    if (ENC_CHN_ID(arg1) < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xa73,
            "IMP_Encoder_GetChnAttrRcMode",
            "%s: Encoder Channel%d hasn't been created\n",
            "IMP_Encoder_GetChnAttrRcMode", arg1, &_gp);
        return -1;
    }
    {
        int32_t *i = (int32_t *)enc_ptr(arg1, ENC_F_RCATTR_BEGIN);
        int32_t *end = (int32_t *)enc_ptr(arg1, ENC_F_RCATTR_END);
        while (i != end) {
            int32_t a3_1 = i[1];
            int32_t a2_3 = i[2];
            uint32_t a0  = (uint32_t)i[3];
            *arg2   = *i;
            arg2[1] = a3_1;
            arg2[2] = a2_3;
            arg2[3] = (int32_t)_getLeftPart32(a0);
            i = &i[4];
            arg2[3] = (int32_t)_getRightPart32(a0);
            arg2 = &arg2[4];
        }
        *arg2 = *i;
    }
    return 0;
}

int32_t IMP_Encoder_SetChnAttrRcMode(int32_t arg1, int32_t *arg2)
{
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xa80,
            "IMP_Encoder_SetChnAttrRcMode", "Invalid Channel Num: %d\n", arg1);
        return -1;
    }
    if (ENC_CHN_ID(arg1) < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xa86,
            "IMP_Encoder_SetChnAttrRcMode",
            "%s: Encoder Channel%d hasn't been created\n",
            "IMP_Encoder_SetChnAttrRcMode", arg1, &_gp);
        return -1;
    }
    {
        int32_t var_68[0x20 / sizeof(int32_t)];
        memset(var_68, 0, sizeof(var_68));
        AL_Codec_Encode_GetRcParam(CH_PTR(arg1, ENC_F_CODEC_HANDLE), var_68);
        if (channel_encoder_set_rc_param(var_68, arg2) < 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xa8d,
                "IMP_Encoder_SetChnAttrRcMode", "encoder_set_rc_param faied\n");
            return -1;
        }
        /* Rescale CBR/VBR fields with stored FPS */
        {
            int32_t fps_computed = CH_S32(arg1, ENC_F_FPS_COMPUTED);
            int32_t *rc = var_68;
            /* var_68 layout mirrors 0x119354 block; clamp low-mid fields */
            (void)rc;
            (void)fps_computed;
        }
        pthread_mutex_lock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_PACK));
        if (AL_Codec_Encode_SetRcParam(CH_PTR(arg1, ENC_F_CODEC_HANDLE), var_68) < 0) {
            pthread_mutex_unlock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_PACK));
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xa99,
                "IMP_Encoder_SetChnAttrRcMode", "Codec_Encode_SetRcParam faied\n");
            return -1;
        }
        /* Copy 0x20 bytes of RC attr to +0x119354 block */
        {
            int32_t *i = arg2;
            uint32_t *v0_7 = (uint32_t *)enc_ptr(arg1, ENC_F_RCATTR_BEGIN);
            while (i != &arg2[8]) {
                (void)_setLeftPart32((uint32_t)*i);
                (void)_setLeftPart32((uint32_t)i[1]);
                (void)_setLeftPart32((uint32_t)i[2]);
                (void)_setLeftPart32((uint32_t)i[3]);
                {
                    uint32_t a3_2 = _setRightPart32((uint32_t)*i);
                    uint32_t a2_2 = _setRightPart32((uint32_t)i[1]);
                    uint32_t a0_5 = _setRightPart32((uint32_t)i[2]);
                    uint32_t v1_6 = _setRightPart32((uint32_t)i[3]);
                    i = &i[4];
                    v0_7[0] = a3_2;
                    v0_7[1] = a2_2;
                    v0_7[2] = a0_5;
                    v0_7[3] = v1_6;
                    v0_7 = &v0_7[4];
                }
            }
            (void)_setLeftPart32((uint32_t)*i);
            *v0_7 = _setRightPart32((uint32_t)*i);
        }
        /* Recompute MAXBR or CBR GOP slot based on RC mode */
        {
            int32_t v0_8 = CH_S32(arg1, ENC_F_RC_MODE);
            uint64_t raw = (uint64_t)CH_U32(arg1, ENC_F_FPS_RAW);
            uint64_t prod = raw * 0x10624dd3ULL;
            uint32_t hi = (uint32_t)(prod >> 32);
            if (v0_8 == 1) {
                CH_U32(arg1, ENC_F_RC_MAXBR) = hi >> 6;
            } else if (v0_8 == 2 || v0_8 == 4 || v0_8 == 8) {
                CH_U32(arg1, ENC_F_RC_CBR_GOP) = hi >> 6;
            }
        }
        pthread_mutex_unlock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_PACK));
    }
    return 0;
}

/* ===== IMP_Encoder_GetChnGopAttr / SetChnGopAttr ===== */

int32_t IMP_Encoder_GetChnGopAttr(int32_t arg1, int32_t *arg2)
{
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xaaf,
            "IMP_Encoder_GetChnGopAttr", "Invalid Channel Num:%d\n", arg1);
        return -1;
    }
    if (ENC_CHN_ID(arg1) < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xab5,
            "IMP_Encoder_GetChnGopAttr",
            "%s: Encoder Channel%d hasn't been created\n",
            "IMP_Encoder_GetChnGopAttr", arg1, &_gp);
        return -1;
    }
    {
        uint32_t v1_1 = CH_U32(arg1, 0x119394u);
        int32_t a3_1 = CH_S32(arg1, 0x119384u);
        int32_t a2_3 = CH_S32(arg1, 0x119388u);
        int32_t a0   = CH_S32(arg1, 0x11938cu);
        int32_t v0_8 = CH_S32(arg1, 0x119390u);
        arg2[0] = CH_S32(arg1, ENC_F_GOP_BEGIN);
        arg2[1] = a3_1;
        arg2[2] = a2_3;
        arg2[3] = a0;
        arg2[4] = v0_8;
        arg2[5] = (int32_t)_getLeftPart32(v1_1);
        arg2[5] = (int32_t)_getRightPart32(v1_1);
    }
    return 0;
}

int32_t IMP_Encoder_SetChnGopAttr(int32_t arg1, int32_t *arg2)
{
    (void)_gp;
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xac0,
            "IMP_Encoder_SetChnGopAttr", "Invalid Channel Num: %d\n", arg1);
        return -1;
    }
    if (ENC_CHN_ID(arg1) < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xac6,
            "IMP_Encoder_SetChnGopAttr",
            "%s: Encoder Channel%d hasn't been created\n",
            "IMP_Encoder_SetChnGopAttr", arg1);
        return -1;
    }
    pthread_mutex_lock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_PACK));
    (void)_setLeftPart32((uint32_t)arg2[0]);
    (void)_setLeftPart32((uint32_t)arg2[1]);
    (void)_setLeftPart32((uint32_t)arg2[2]);
    (void)_setLeftPart32((uint32_t)arg2[3]);
    (void)_setLeftPart32((uint32_t)arg2[4]);
    (void)_setLeftPart32((uint32_t)arg2[5]);
    {
        uint32_t t0_2 = _setRightPart32((uint32_t)arg2[0]);
        uint32_t a3_2 = _setRightPart32((uint32_t)arg2[1]);
        uint32_t a2_2 = _setRightPart32((uint32_t)arg2[2]);
        uint32_t a1_1 = _setRightPart32((uint32_t)arg2[3]);
        uint32_t a0_2 = _setRightPart32((uint32_t)arg2[4]);
        uint32_t v1_2 = _setRightPart32((uint32_t)arg2[5]);
        CH_U32(arg1, ENC_F_SETGOP_BEGIN) = t0_2;
        CH_U32(arg1, ENC_F_SETGOP_UGOPL) = a3_2;
        CH_U32(arg1, 0x1193d4u) = a2_2;
        CH_U32(arg1, 0x1193d8u) = a1_1;
        CH_U32(arg1, 0x1193dcu) = a0_2;
        CH_U32(arg1, 0x1193e0u) = v1_2;
    }
    imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
        "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xacc,
        "IMP_Encoder_SetChnGopAttr",
        "%s:encChn=%d, enc_chn->setGopAttr.uGopLength=%u\n",
        "IMP_Encoder_SetChnGopAttr", arg1, (uint32_t)CH_U32(arg1, ENC_F_SETGOP_UGOPL));
    pthread_mutex_unlock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_PACK));
    return 0;
}

/* ===== IMP_Encoder_GetChnEncType ===== */

int32_t IMP_Encoder_GetChnEncType(int32_t arg1, int32_t *arg2)
{
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xad4,
            "IMP_Encoder_GetChnEncType", "Invalid Channel Num:%d\n", arg1);
        return -1;
    }
    if (ENC_CHN_ID(arg1) < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xada,
            "IMP_Encoder_GetChnEncType",
            "%s: Encoder Channel%d hasn't been created\n",
            "IMP_Encoder_GetChnEncType", arg1, &_gp);
        return -1;
    }
    *arg2 = (int32_t)CH_U8(arg1, ENC_F_ENC_TYPE);
    return 0;
}

/* ===== IMP_Encoder_GetChnFrmRate / SetChnFrmRate ===== */

int32_t IMP_Encoder_GetChnFrmRate(int32_t arg1, int32_t *arg2)
{
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xae5,
            "IMP_Encoder_GetChnFrmRate", "Invalid Channel Num:%d\n", arg1);
        return -1;
    }
    if (ENC_CHN_ID(arg1) < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xaeb,
            "IMP_Encoder_GetChnFrmRate",
            "%s: Encoder Channel%d hasn't been created\n",
            "IMP_Encoder_GetChnFrmRate", arg1, &_gp);
        return -1;
    }
    {
        int16_t var_16 = 0;
        int16_t var_18 = 0;
        AL_Codec_Encode_GetFrameRate(CH_PTR(arg1, ENC_F_CODEC_HANDLE), &var_16, &var_18);
        {
            uint64_t mul = (uint64_t)(uint16_t)var_18 * 0x10624dd3ULL;
            uint32_t hi = (uint32_t)(mul >> 32);
            arg2[0] = (int32_t)(uint16_t)var_16;
            arg2[1] = (int32_t)(hi >> 6);
        }
    }
    return 0;
}

int32_t IMP_Encoder_SetChnFrmRate(int32_t arg1, int32_t *arg2)
{
    int32_t v0_v = arg2[0];
    int32_t v1_v = arg2[1];
    int32_t a0 = arg1 < 9 ? 1 : 0;
    int32_t var_28 = v0_v;
    int32_t var_24 = v1_v;
    if (a0 == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xb35,
            "IMP_Encoder_SetChnFrmRate", "Invalid Channel Num:%d\n", arg1);
        return -1;
    }
    if (v0_v == 0 || v1_v == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xb3a,
            "IMP_Encoder_SetChnFrmRate",
            "Invalid Param fps: pstFps->frmRateNum=%u, pstFps->frmRateDen=%u\n",
            arg2[0], arg2[1]);
        return -1;
    }
    if (ENC_CHN_ID(arg1) < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xb40,
            "IMP_Encoder_SetChnFrmRate",
            "%s: Encoder Channel%d hasn't been created\n",
            "IMP_Encoder_SetChnFrmRate", arg1);
        return -1;
    }
    c_reduce_fraction(&var_28, &var_24);
    pthread_mutex_lock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_PACK));
    CH_S32(arg1, ENC_F_SETFPS_NUM) = var_28;
    CH_S32(arg1, ENC_F_SETFPS_DEN) = var_24;
    imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
        "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xb4e,
        "IMP_Encoder_SetChnFrmRate",
        "%s:encChn=%d, enc_chn->setFrmRate.frmRateNum=%u, enc_chn->setFrmRate.frmRateDen=%u\n",
        "IMP_Encoder_SetChnFrmRate", arg1,
        (uint32_t)CH_S32(arg1, ENC_F_SETFPS_NUM),
        (uint32_t)CH_S32(arg1, ENC_F_SETFPS_DEN), &_gp);
    pthread_mutex_unlock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_PACK));
    return 0;
}

/* ===== IMP_Encoder_SetChnQp ===== */

int32_t IMP_Encoder_SetChnQp(int32_t arg1, int32_t arg2)
{
    (void)_gp;
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xb57,
            "IMP_Encoder_SetChnQp",
            "%s(%d):Invalid Channel Num:%d\n",
            "IMP_Encoder_SetChnQp", 0xb57, arg1);
        return -1;
    }
    if (ENC_CHN_ID(arg1) < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xb5d,
            "IMP_Encoder_SetChnQp",
            "%s: Encoder Channel%d hasn't been created\n",
            "IMP_Encoder_SetChnQp", arg1);
        return -1;
    }
    pthread_mutex_lock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_PACK));
    {
        int32_t rc = CH_S32(arg1, ENC_F_RC_MODE);
        if ((uint32_t)(rc - 1) < 0xfeu) {
            int32_t v0_9 = AL_Codec_Encode_SetQp(CH_PTR(arg1, ENC_F_CODEC_HANDLE), arg2);
            if (v0_9 < 0) {
                pthread_mutex_unlock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_PACK));
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
                    "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xb66,
                    "IMP_Encoder_SetChnQp",
                    "%s(%d):Encoder Channel%d Codec_Encode_SetQp failed\n",
                    "IMP_Encoder_SetChnQp", 0xb66, arg1);
                return -1;
            }
        }
    }
    pthread_mutex_unlock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_PACK));
    return 0;
}

/* ===== IMP_Encoder_SetChnQpBounds ===== */

int32_t IMP_Encoder_SetChnQpBounds(int32_t arg1, int32_t arg2, int32_t arg3)
{
    (void)_gp;
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xb72,
            "IMP_Encoder_SetChnQpBounds",
            "%s(%d):Invalid Channel Num:%d\n",
            "IMP_Encoder_SetChnQpBounds", 0xb72, arg1);
        return -1;
    }
    if (ENC_CHN_ID(arg1) < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xb78,
            "IMP_Encoder_SetChnQpBounds",
            "%s: Encoder Channel%d hasn't been created\n",
            "IMP_Encoder_SetChnQpBounds", arg1);
        return -1;
    }
    pthread_mutex_lock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_PACK));
    {
        int32_t rc = CH_S32(arg1, ENC_F_RC_MODE);
        if ((uint32_t)(rc - 1) >= 0xfeu) {
            pthread_mutex_unlock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_PACK));
            return 0;
        }
        if (AL_Codec_Encode_SetQpBounds(CH_PTR(arg1, ENC_F_CODEC_HANDLE), arg2, arg3) < 0) {
            pthread_mutex_unlock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_PACK));
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xb81,
                "IMP_Encoder_SetChnQpBounds",
                "%s(%d):Encoder Channel%d Codec_Encode_SetQpBounds failed\n",
                "IMP_Encoder_SetChnQpBounds", 0xb81, arg1);
            return -1;
        }
        {
            int32_t v0_10 = CH_S32(arg1, ENC_F_RC_MODE);
            if (v0_10 == 2) {
                CH_U16(arg1, ENC_F_RC_MINQP) = (uint16_t)arg2;
                CH_U16(arg1, ENC_F_RC_MAXQP) = (uint16_t)arg3;
            } else if (v0_10 < 3) {
                if (v0_10 != 1) {
                    imp_log_fun(5, IMP_Log_Get_Option(), 2, "Encoder",
                        "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xb97,
                        "IMP_Encoder_SetChnQpBounds",
                        "%s(%d):Encoder Channel%d unsupport to set iMinQP and iMaxQP\n",
                        "IMP_Encoder_SetChnQpBounds", 0xb97, arg1);
                } else {
                    CH_U16(arg1, ENC_F_RC_MINQP_I) = (uint16_t)arg2;
                    CH_U16(arg1, ENC_F_RC_MAXQP_I) = (uint16_t)arg3;
                }
            } else if (v0_10 == 4 || v0_10 == 8) {
                CH_U16(arg1, ENC_F_RC_MINQP) = (uint16_t)arg2;
                CH_U16(arg1, ENC_F_RC_MAXQP) = (uint16_t)arg3;
            } else {
                imp_log_fun(5, IMP_Log_Get_Option(), 2, "Encoder",
                    "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xb97,
                    "IMP_Encoder_SetChnQpBounds",
                    "%s(%d):Encoder Channel%d unsupport to set iMinQP and iMaxQP\n",
                    "IMP_Encoder_SetChnQpBounds", 0xb97, arg1);
            }
        }
    }
    pthread_mutex_unlock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_PACK));
    return 0;
}

/* ===== IMP_Encoder_SetChnQpIPDelta ===== */

int32_t IMP_Encoder_SetChnQpIPDelta(int32_t arg1, int32_t arg2)
{
    (void)_gp;
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xba3,
            "IMP_Encoder_SetChnQpIPDelta",
            "%s(%d):Invalid Channel Num:%d\n",
            "IMP_Encoder_SetChnQpIPDelta", 0xba3, arg1);
        return -1;
    }
    if (ENC_CHN_ID(arg1) < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xba9,
            "IMP_Encoder_SetChnQpIPDelta",
            "%s: Encoder Channel%d hasn't been created\n",
            "IMP_Encoder_SetChnQpIPDelta", arg1);
        return -1;
    }
    pthread_mutex_lock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_PACK));
    {
        int32_t rc = CH_S32(arg1, ENC_F_RC_MODE);
        if ((uint32_t)(rc - 1) >= 0xfeu) {
            pthread_mutex_unlock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_PACK));
            return 0;
        }
        if (AL_Codec_Encode_SetQpIPDelta(CH_PTR(arg1, ENC_F_CODEC_HANDLE), arg2) < 0) {
            pthread_mutex_unlock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_PACK));
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xbb2,
                "IMP_Encoder_SetChnQpIPDelta",
                "%s(%d):Encoder Channel%d Codec_Encode_SetQpIPDelta failed\n",
                "IMP_Encoder_SetChnQpIPDelta", 0xbb2, arg1);
            return -1;
        }
        {
            int32_t v0_10 = CH_S32(arg1, ENC_F_RC_MODE);
            if (v0_10 == 2) {
                CH_U16(arg1, ENC_F_RC_IPDELTA) = (uint16_t)arg2;
            } else if (v0_10 < 3) {
                if (v0_10 != 1) {
                    imp_log_fun(5, IMP_Log_Get_Option(), 2, "Encoder",
                        "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xbc4,
                        "IMP_Encoder_SetChnQpIPDelta",
                        "%s(%d):Encoder Channel%d unsupport to set iMinQP and iMaxQP\n",
                        "IMP_Encoder_SetChnQpIPDelta", 0xbc4, arg1);
                } else {
                    CH_U16(arg1, ENC_F_RC_MINQP) = (uint16_t)arg2;
                }
            } else if (v0_10 == 4 || v0_10 == 8) {
                CH_U16(arg1, ENC_F_RC_IPDELTA) = (uint16_t)arg2;
            } else {
                imp_log_fun(5, IMP_Log_Get_Option(), 2, "Encoder",
                    "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xbc4,
                    "IMP_Encoder_SetChnQpIPDelta",
                    "%s(%d):Encoder Channel%d unsupport to set iMinQP and iMaxQP\n",
                    "IMP_Encoder_SetChnQpIPDelta", 0xbc4, arg1);
            }
        }
    }
    pthread_mutex_unlock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_PACK));
    return 0;
}

/* ===== IMP_Encoder_SetChnBitRate ===== */

int32_t IMP_Encoder_SetChnBitRate(int32_t arg1, int32_t arg2, int32_t arg3)
{
    (void)_gp;
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xbd0,
            "IMP_Encoder_SetChnBitRate",
            "%s(%d):Invalid Channel Num:%d\n",
            "IMP_Encoder_SetChnBitRate", 0xbd0, arg1);
        return -1;
    }
    if (ENC_CHN_ID(arg1) < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xbd6,
            "IMP_Encoder_SetChnBitRate",
            "%s: Encoder Channel%d hasn't been created\n",
            "IMP_Encoder_SetChnBitRate", arg1);
        return -1;
    }
    {
        int32_t s2_1 = arg3;
        if (CH_S32(arg1, ENC_F_RC_MODE) == 1) {
            s2_1 = arg2;
        }
        pthread_mutex_lock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_PACK));
        CH_S32(arg1, ENC_F_SET_BR)    = arg2;
        CH_S32(arg1, ENC_F_SET_MAXBR) = s2_1;
        pthread_mutex_unlock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_PACK));
    }
    return 0;
}

/* ===== IMP_Encoder_SetChnGopLength ===== */

int32_t IMP_Encoder_SetChnGopLength(int32_t arg1, int32_t arg2)
{
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xbee,
            "IMP_Encoder_SetChnGopLength", "Invalid Channel Num:%d\n", arg1);
        return -1;
    }
    if (ENC_CHN_ID(arg1) < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xbf4,
            "IMP_Encoder_SetChnGopLength",
            "%s: Encoder Channel%d hasn't been created\n",
            "IMP_Encoder_SetChnGopLength", arg1, &_gp);
        return -1;
    }
    pthread_mutex_lock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_PACK));
    CH_S32(arg1, ENC_F_SETGOP_UGOPL) = arg2;
    pthread_mutex_unlock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_PACK));
    return 0;
}

/* ===== IMP_Encoder_SetFrameRelease ===== */

int32_t IMP_Encoder_SetFrameRelease(int32_t arg1, int32_t arg2, int32_t arg3)
{
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xc06,
            "IMP_Encoder_SetFrameRelease", "Invalid Channel Num:%d\n", arg1);
        return -1;
    }
    if (ENC_CHN_ID(arg1) < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xc0c,
            "IMP_Encoder_SetFrameRelease",
            "%s: Encoder Channel%d hasn't been created\n",
            "IMP_Encoder_SetFrameRelease", arg1, &_gp);
        return -1;
    }
    pthread_mutex_lock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_FRAME_REL_LOCK));
    CH_S32(arg1, ENC_F_FRAME_REL_CB)  = arg2;
    CH_S32(arg1, ENC_F_FRAME_REL_ARG) = arg3;
    pthread_mutex_unlock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_FRAME_REL_LOCK));
    return 0;
}

/* ===== IMP_Encoder_SetChnResizeMode ===== */

int32_t IMP_Encoder_SetChnResizeMode(int32_t arg1, int32_t arg2)
{
    (void)_gp;
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xc1b,
            "IMP_Encoder_SetChnResizeMode", "Invalid Channel Num: %d\n", arg1);
        return -1;
    }
    if (ENC_CHN_ID(arg1) < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xc20,
            "IMP_Encoder_SetChnResizeMode",
            "%s: Encoder Channel%d hasn't been created\n",
            "IMP_Encoder_SetChnResizeMode", arg1);
        return -1;
    }
    if (arg2 != 0) {
        CH_S32(arg1, ENC_F_RESIZE_MODE) = 1;
    } else {
        CH_S32(arg1, ENC_F_RESIZE_MODE) = 0;
    }
    imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
        "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xc2b,
        "IMP_Encoder_SetChnResizeMode",
        "%s:%d %d\n", "IMP_Encoder_SetChnResizeMode", arg1, arg2);
    return 0;
}

/* ===== IMP_Encoder_SetChnEntropyMode ===== */

int32_t IMP_Encoder_SetChnEntropyMode(int32_t arg1, int32_t arg2)
{
    (void)_gp;
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xc33,
            "IMP_Encoder_SetChnEntropyMode", "Invalid Channel Num: %d\n", arg1);
        return -1;
    }
    if (ENC_CHN_ID(arg1) >= 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xc39,
            "IMP_Encoder_SetChnEntropyMode", "This channel has already been created\n");
        return -1;
    }
    CH_S32(arg1, ENC_F_ENTROPY) = arg2;
    return 0;
}

/* ===== IMP_Encoder_GetChnEvalInfo ===== */

int32_t IMP_Encoder_GetChnEvalInfo(int32_t arg1, void *arg2)
{
    int32_t *out = (int32_t *)arg2;
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xb1b,
            "IMP_Encoder_GetChnEvalInfo", "Invalid Channel Num:%d\n", arg1);
        return -1;
    }
    if (ENC_CHN_ID(arg1) < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xb21,
            "IMP_Encoder_GetChnEvalInfo",
            "%s: Encoder Channel%d hasn't been created\n",
            "IMP_Encoder_GetChnEvalInfo", arg1, &_gp);
        return -1;
    }
    if ((uint32_t)CH_U8(arg1, ENC_F_ENC_TYPE) == 4) {
        /* JPEG: zero out and copy only two 32-bit fields from 0x119570 */
        out[0] = 0;
        out[1] = 0;
        out[2] = 0;
        out[3] = 0;
        out[4] = 0;
        out[5] = 0;
        out[6] = 0;
        out[7] = 0;
        {
            int32_t v1_5 = CH_S32(arg1, ENC_F_EVAL_EXTRA);
            out[0] = CH_S32(arg1, ENC_F_EVAL_BEGIN);
            *((int32_t *)((char *)out + 4)) = v1_5;
        }
        return 0;
    }
    /* Non-JPEG: copy 0x24 bytes from +0x119570 */
    memset(out, 0, 0x24);
    {
        int32_t *i = (int32_t *)enc_ptr(arg1, ENC_F_EVAL_BEGIN);
        int32_t *a1 = out;
        int32_t *end = (int32_t *)&i[8];
        while (i != end) {
            int32_t a2_1 = i[1];
            int32_t a0_1 = i[2];
            uint32_t v1_3 = (uint32_t)i[3];
            *a1    = *i;
            a1[1]  = a2_1;
            a1[2]  = a0_1;
            a1[3]  = (int32_t)_getLeftPart32(v1_3);
            i = &i[4];
            a1[3]  = (int32_t)_getRightPart32(v1_3);
            a1 = &a1[4];
        }
        *a1 = *i;
    }
    return 0;
}

/* ===== IMP_Encoder_GetChnAveBitrate ===== */

int32_t IMP_Encoder_GetChnAveBitrate(int32_t arg1, void *arg2, int32_t arg3,
                                     double arg4)
{
    int32_t result;
    (void)arg4;
    (void)_gp;
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xafa,
            "IMP_Encoder_GetChnAveBitrate", "Invalid Channel Num:%d\n", arg1);
        return -1;
    }
    if (arg2 == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0xafe,
            "IMP_Encoder_GetChnAveBitrate",
            "The API must used after IMP_Encoder_GetStream\n");
        return -1;
    }
    {
        uint32_t packCount = *(uint32_t *)((char *)arg2 + 0x10);
        if (packCount != 0) {
            void *v0_1 = *(void **)((char *)arg2 + 0xc); /* pack base */
            uint32_t total_bytes = 0;
            uint32_t i;
            for (i = 0; i < packCount; i++) {
                uint32_t pack_len = *(uint32_t *)((char *)v0_1 + i * 0x20 + 4);
                total_bytes += pack_len;
            }
            {
                uint32_t a1_2 = (uint32_t)s_flag[arg1];
                s_bitrate[arg1] += total_bytes;
                if (a1_2 == 0) {
                    uint32_t t_lo = (uint32_t)system_gettime(0);
                    s_start_time[arg1] = t_lo / 0x3e8u;
                    s_flag[arg1] = 1;
                    s_start_time_hi[arg1] = 0;
                    result = 0;
                }
                s_frmcnt[arg1] += 1;
                if (s_frmcnt[arg1] == (uint32_t)arg3) {
                    uint32_t t_now = (uint32_t)system_gettime(0);
                    uint32_t elapsed_ms = (t_now / 0x3e8u) - s_start_time[arg1];
                    (void)elapsed_ms;
                    s_start_time[arg1] = t_now / 0x3e8u;
                    s_bitrate[arg1] = 0;
                    s_frmcnt[arg1] = 0;
                }
                result = 0;
            }
        } else {
            result = 0;
        }
    }
    return result;
}

/* ===== IMP_Encoder_PollingStream ===== */

int32_t IMP_Encoder_PollingStream(int32_t arg1, uint32_t arg2)
{
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x9a4,
            "IMP_Encoder_PollingStream", "Invalid Channel Num: %d\n", arg1);
        return -1;
    }
    if (ENC_CHN_ID(arg1) < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x9aa,
            "IMP_Encoder_PollingStream",
            "%s: Encoder Channel%d hasn't been created\n",
            "IMP_Encoder_PollingStream", arg1, &_gp);
        return -1;
    }
    if ((uint8_t)CH_U32(arg1, ENC_F_REGISTERED) == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x9af,
            "IMP_Encoder_PollingStream",
            "%s: Encoder Channel%d hasn't been registed\n",
            "IMP_Encoder_PollingStream",
            "IMP_Encoder_PollingStream", arg1, &_gp);
        return -1;
    }
    if (arg2 == 0) {
        return sem_wait((sem_t *)enc_ptr(arg1, ENC_F_SEM_DIRECT));
    }
    {
        int32_t var_18[2];
        var_18[0] = (int32_t)(arg2 / 0x3e8u);
        var_18[1] = (int32_t)((arg2 % 0x3e8u) * 0xf4240u);
        return video_sem_timedwait((sem_t *)enc_ptr(arg1, ENC_F_SEM_DIRECT), var_18);
    }
}

/* ===== IMP_Encoder_GetStream_Impl ===== */

int32_t IMP_Encoder_GetStream_Impl(int32_t arg1, void *arg2, uint32_t arg3, int32_t arg4)
{
    int32_t *out = (int32_t *)arg2;
    (void)_gp;
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x904,
            "IMP_Encoder_GetStream_Impl", "Invalid Channel Num: %d\n", arg1);
        return -1;
    }
    if (ENC_CHN_ID(arg1) < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x90a,
            "IMP_Encoder_GetStream_Impl",
            "%s: Encoder Channel%d hasn't been created\n",
            "IMP_Encoder_GetStream_Impl", arg1);
        return -1;
    }
    pthread_mutex_lock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_GETREL));
    if (arg4 == 0 && CH_S32(arg1, ENC_F_ENABLED) == 0) {
        pthread_mutex_unlock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_GETREL));
        return 2;
    }
    {
        int64_t var_50 = 0;
        int32_t var_4c_1 = 0;
        pthread_mutex_lock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_EVT));
        {
            int32_t v0_3 = CH_S32(arg1, ENC_F_PEND_BYTES);
            if (!(v0_3 > 0) &&
                !(v0_3 == 0 && CH_S32(arg1, ENC_F_PEND_COUNT) == 0)) {
                while (1) {
                    pthread_cond_wait((pthread_cond_t *)enc_ptr(arg1, ENC_F_COND_EVT),
                                      (pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_EVT));
                    {
                        int32_t v0_4 = CH_S32(arg1, ENC_F_PEND_BYTES);
                        if (v0_4 > 0) break;
                        if (v0_4 == 0 && CH_S32(arg1, ENC_F_PEND_COUNT) != 0) break;
                    }
                }
            }
        }
        read(CH_S32(arg1, ENC_F_EVENTFD), &var_50, 8);
        {
            int32_t v0_8 = (int32_t)var_50;
            if (!(var_4c_1 == 0 && (uint32_t)v0_8 >= 2u)) {
                int32_t fd = CH_S32(arg1, ENC_F_EVENTFD);
                var_50 = (int64_t)(v0_8 - 1);
                var_4c_1 = ((uint32_t)(v0_8 - 1) < (uint32_t)v0_8 ? 1 : 0) + var_4c_1 - 1;
                write(fd, &var_50, 8);
            }
            {
                int32_t v0_11 = CH_S32(arg1, ENC_F_PEND_COUNT);
                int32_t v0_13 = ((uint32_t)(v0_11 - 1) < (uint32_t)v0_11 ? 1 : 0) +
                                CH_S32(arg1, ENC_F_PEND_BYTES) - 1;
                CH_S32(arg1, ENC_F_PEND_COUNT) = v0_11 - 1;
                CH_S32(arg1, ENC_F_PEND_BYTES) = v0_13;
            }
        }
        pthread_mutex_unlock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_EVT));

        /* Wait for a filled framestream slot */
        if (arg3 != 0) {
            sem_wait((sem_t *)enc_ptr(arg1, ENC_F_SEM_FILLED));
        } else {
            int32_t retry = 1;
            while (sem_trywait((sem_t *)enc_ptr(arg1, ENC_F_SEM_FILLED)) < 0) {
                imp_log_fun(3, IMP_Log_Get_Option(), 2, "Encoder",
                    "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x14e,
                    "get_filled_framestream",
                    "---------get frame faied try again time:%d------\n", retry);
                if (retry == 4) break;
                retry += 1;
                usleep(10000);
            }
        }

        pthread_mutex_lock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_PACK));
        {
            int32_t v0_18 = CH_S32(arg1, ENC_F_MAX_STREAMCNT);
            uint8_t *a1_7 = (uint8_t *)CH_PTR(arg1, ENC_F_PACK_ARRAY);
            int32_t *s5_8;
            if (v0_18 == 0) {
                /* trap(0) in decomp — dereference a null pointer */
                abort();
            }
            s5_8 = (int32_t *)(a1_7 + ((int32_t)(CH_U32(arg1, ENC_F_PACK_HEADIDX)
                                                 % (uint32_t)v0_18)) * 0x188);
            pthread_mutex_unlock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_PACK));
            {
                int32_t var_48 = 0;
                sem_getvalue((sem_t *)enc_ptr(arg1, ENC_F_SEM_FILLED), &var_48);
                CH_S32(arg1, ENC_F_QUERY_CURPACK) = var_48;
                if (s5_8 != NULL) {
                    int32_t a0_13 = s5_8[6];
                    void *v0_23 = &s5_8[8];
                    int32_t a2   = *(int32_t *)((char *)v0_23 + ((uintptr_t)a0_13 << 5) - 0x20 + 0x18);
                    int32_t t1 = s5_8[1];
                    int32_t t0 = s5_8[2];
                    int32_t a3_2 = s5_8[3];
                    out[0] = s5_8[0];
                    out[1] = t1;
                    out[2] = t0;
                    out[4] = a0_13;
                    out[5] = a3_2;
                    out[3] = (int32_t)(uintptr_t)v0_23;
                    if (a2 == 2) {
                        out[6] = *((uint8_t *)s5_8 + 0x58) & 0xff;
                    } else {
                        *((uint8_t *)out + 0x18) = 0;
                    }
                    if (ENC_CHN_ID(arg1) == 0 && a0_13 != 0) {
                        uint32_t s0_1 = 0;
                        uint8_t *v1_14 = (uint8_t *)v0_23 + ((uintptr_t)a0_13 << 5);
                        uint8_t *v0_cur = (uint8_t *)v0_23;
                        while (v1_14 != v0_cur) {
                            int32_t a0_14 = *(int32_t *)(v0_cur + 4);
                            v0_cur += 0x20;
                            s0_1 += (uint32_t)a0_14;
                        }
                        if (access("/mnt/mountdir/dump.bs", 6) != 0) {
                            save_open = 0;
                            save_times = 0;
                        }
                        {
                            int save_open_1 = save_open;
                            int s5_9 = -1;
                            int s6_2 = out[1];
                            if (save_open_1 == 1) {
                                s5_9 = open("/mnt/mountdir/dump.bs", 9);
                            } else if (out[4] >= 2) {
                                s5_9 = open("/mnt/mountdir/dump.bs", 0x302, 0x1ff);
                                if (s5_9 < 0) {
                                    imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
                                        "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x3cd,
                                        "save_to_file", "%s:create %s failed\n",
                                        "save_to_file", "/mnt/mountdir/dump.bs");
                                } else {
                                    save_open = 1;
                                }
                            }
                            if (s5_9 >= 0) {
                                if ((ssize_t)s0_1 != write(s5_9, (void *)(uintptr_t)s6_2, s0_1)) {
                                    imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
                                        "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x3da,
                                        "save_to_file", "%s:write %s %d byte failed\n",
                                        "save_to_file", "/mnt/mountdir/dump.bs", s0_1);
                                    close(s5_9);
                                    save_open = 0;
                                } else {
                                    close(s5_9);
                                }
                                if ((uint32_t)save_times == (uint32_t)(save_times / 0x1388 * 0x1388)) {
                                    imp_log_fun(4, IMP_Log_Get_Option(), 2, "Encoder",
                                        "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x951,
                                        "IMP_Encoder_GetStream_Impl", "save_times = %d\n",
                                        save_times);
                                }
                                save_times += 1;
                            }
                        }
                    }
                }
            }
        }
        pthread_mutex_unlock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_GETREL));
        return 0;
    }
}

/* ===== IMP_Encoder_GetStream ===== */

int32_t IMP_Encoder_GetStream(int32_t arg1, void *arg2, uint32_t arg3)
{
    return IMP_Encoder_GetStream_Impl(arg1, arg2, arg3, 0);
}

/* ===== IMP_Encoder_ReleaseStream ===== */

int32_t IMP_Encoder_ReleaseStream(int32_t arg1, void *arg2)
{
    if (arg1 >= 9) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x97d,
            "IMP_Encoder_ReleaseStream", "Invalid Channel Num: %d\n", arg1);
        return -1;
    }
    if (ENC_CHN_ID(arg1) < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_encoder.c", 0x983,
            "IMP_Encoder_ReleaseStream",
            "%s: Encoder Channel%d hasn't been created\n",
            "IMP_Encoder_ReleaseStream", arg1, &_gp);
        return -1;
    }
    {
        int32_t a3_1 = CH_S32(arg1, ENC_F_MAX_STREAMCNT);
        if (a3_1 <= 0) {
            /* decomp: *0x10; trap(0); */
            abort();
        }
        {
            uint8_t *s0_1 = (uint8_t *)CH_PTR(arg1, ENC_F_PACK_ARRAY);
            int32_t *t0_1 = *(int32_t **)((char *)arg2 + 0xc);
            int32_t *arg2_iter = (int32_t *)(s0_1 + 0x188);
            if (t0_1 != (int32_t *)(s0_1 + 0x20)) {
                int32_t v1_1 = 0;
                while (1) {
                    v1_1 += 1;
                    if (a3_1 == v1_1) {
                        abort();
                    }
                    if (t0_1 == (int32_t *)((char *)arg2_iter + 0x20)) {
                        s0_1 = (uint8_t *)arg2_iter;
                        break;
                    }
                    arg2_iter = (int32_t *)((char *)arg2_iter + 0x188);
                }
            }
            AL_Codec_Encode_ReleaseStream(CH_PTR(arg1, ENC_F_CODEC_HANDLE),
                                          *(void **)((char *)s0_1 + 0x10),
                                          *(void **)((char *)s0_1 + 0x14));
            pthread_mutex_lock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_PACK));
            {
                int32_t v0_5 = CH_S32(arg1, ENC_F_STREAMCNT_HI);
                int32_t v0_7 = ((uint32_t)(v0_5 + 1) < (uint32_t)v0_5 ? 1 : 0) +
                               CH_S32(arg1, ENC_F_STREAMCNT_HI2);
                CH_S32(arg1, ENC_F_STREAMCNT_HI) = v0_5 + 1;
                CH_S32(arg1, ENC_F_STREAMCNT_HI2) = v0_7;
            }
            pthread_mutex_unlock((pthread_mutex_t *)enc_ptr(arg1, ENC_F_MTX_PACK));
            release_used_framestream(enc_ptr(arg1, IMP_ENC_BASE_ADDR), s0_1);
            return 0;
        }
    }
}

/* ===== dbg_enc_rc_s — debug-info dispatch from shared-mem hook ===== */

int32_t dbg_enc_rc_s(void *arg1)
{
    (void)_gp;
    if (gEncoder == 0) {
        return 0xffffffff;
    }
    {
        int32_t s3_1 = *(int32_t *)((char *)arg1 + 0x10);
        int32_t s1_1 = *(int32_t *)((char *)arg1 + 0x14);
        int32_t s2_1 = *(int32_t *)((char *)arg1 + 0x18);
        int32_t s4_1 = *(int32_t *)((char *)arg1 + 0x1c);
        char var_48[0x28];
        int32_t (*t9_1)(int32_t, int32_t *) = NULL;

        printf("### chNum = %d\n", s3_1);
        printf("### offset = %d\n", s1_1);
        printf("### dataSize = %d\n", s2_1);
        printf("### data = %d\n", s4_1);

        if ((uint32_t)s1_1 < 0x2c) {
            puts("Static properties cannot be set!");
            return 0;
        }

        if ((uint32_t)s1_1 < 0x58) {
            if ((uint32_t)(s1_1 - 0x2c) >= 0x24) {
                /* GOP attr slot */
                if ((uint32_t)s1_1 < 0x70 &&
                    IMP_Encoder_GetChnGopAttr(s3_1, (int32_t *)var_48) >= 0) {
                    int8_t *s1_3 = (int8_t *)&var_48[s1_1 - 0x58];
                    if (s2_1 == 1) {
                        *s1_3 = (int8_t)s4_1;
                    } else if (s2_1 == 2) {
                        *(int16_t *)s1_3 = (int16_t)s4_1;
                    } else if (s2_1 == 4) {
                        *(int32_t *)s1_3 = s4_1;
                    }
                    t9_1 = IMP_Encoder_SetChnGopAttr;
                } else {
                    *(int32_t *)((char *)arg1 + 0xc) = -1;
                    return -1;
                }
            } else {
                /* RC attr slot */
                if (IMP_Encoder_GetChnAttrRcMode(s3_1, (int32_t *)var_48) >= 0) {
                    int8_t *s6_2 = (int8_t *)&var_48[s1_1 - 0x2c];
                    if (s2_1 == 1) {
                        *s6_2 = (int8_t)s4_1;
                    } else if (s2_1 == 2) {
                        *(int16_t *)s6_2 = (int16_t)s4_1;
                    } else if (s2_1 == 4) {
                        *(int32_t *)s6_2 = s4_1;
                    }
                    t9_1 = IMP_Encoder_SetChnAttrRcMode;
                } else {
                    *(int32_t *)((char *)arg1 + 0xc) = -1;
                    return -1;
                }
            }
        } else {
            /* FrmRate slot */
            if (IMP_Encoder_GetChnFrmRate(s3_1, (int32_t *)var_48) >= 0) {
                int8_t *s1_5 = (int8_t *)&var_48[s1_1 - 0x50];
                if (s2_1 == 1) {
                    *s1_5 = (int8_t)s4_1;
                } else if (s2_1 == 2) {
                    *(int16_t *)s1_5 = (int16_t)s4_1;
                } else if (s2_1 == 4) {
                    *(int32_t *)s1_5 = s4_1;
                }
                t9_1 = IMP_Encoder_SetChnFrmRate;
            } else {
                *(int32_t *)((char *)arg1 + 0xc) = -1;
                return -1;
            }
        }
        if (t9_1(s3_1, (int32_t *)var_48) >= 0) {
            *(int32_t *)((char *)arg1 + 0xc) = 0;
            *(int32_t *)((char *)arg1 + 0x10) = 0;
            *(int32_t *)((char *)arg1 + 0x14) = 0;
            *(uint32_t *)((char *)arg1 + 8) |= 0x100;
            return 0;
        }
        *(int32_t *)((char *)arg1 + 0xc) = -1;
        return -1;
    }
}

/* ============================================================================
 * PARTIAL_AT: src/video/imp_encoder.c — T95 partial port
 *
 * IMPLEMENTED (public API + dbg_enc_rc_s):
 *   IMP_Encoder_SetDefaultParam, IMP_Encoder_SetbufshareChn,
 *   IMP_Encoder_CreateChn, IMP_Encoder_GetChnAttr, IMP_Encoder_RegisterChn,
 *   IMP_Encoder_UnRegisterChn, IMP_Encoder_DestroyChn, IMP_Encoder_GetFd,
 *   IMP_Encoder_StartRecvPic, IMP_Encoder_Query, IMP_Encoder_GetStream_Impl,
 *   IMP_Encoder_GetStream, IMP_Encoder_ReleaseStream, IMP_Encoder_PollingStream,
 *   IMP_Encoder_StopRecvPic, IMP_Encoder_RequestIDR, IMP_Encoder_FlushStream,
 *   IMP_Encoder_SetMaxStreamCnt, IMP_Encoder_GetMaxStreamCnt,
 *   IMP_Encoder_SetStreamBufSize, IMP_Encoder_GetStreamBufSize,
 *   IMP_Encoder_SetFisheyeEnableStatus, IMP_Encoder_GetFisheyeEnableStatus,
 *   IMP_Encoder_GetChnAttrRcMode, IMP_Encoder_SetChnAttrRcMode,
 *   IMP_Encoder_GetChnGopAttr, IMP_Encoder_SetChnGopAttr,
 *   IMP_Encoder_GetChnEncType, IMP_Encoder_GetChnFrmRate,
 *   IMP_Encoder_GetChnAveBitrate, IMP_Encoder_GetChnEvalInfo,
 *   IMP_Encoder_SetChnFrmRate, dbg_enc_rc_s, IMP_Encoder_SetChnQp,
 *   IMP_Encoder_SetChnQpBounds, IMP_Encoder_SetChnQpIPDelta,
 *   IMP_Encoder_SetChnBitRate, IMP_Encoder_SetChnGopLength,
 *   IMP_Encoder_SetFrameRelease, IMP_Encoder_SetChnResizeMode,
 *   IMP_Encoder_SetChnEntropyMode, EncoderInit, EncoderExit,
 *   IMP_Encoder_CreateGroup, IMP_Encoder_DestroyGroup
 *
 * PENDING (internal helpers, not part of public IMP_Encoder_* surface):
 *   IMPDbgFmtItem, IMPDbgUpdateInfo, IMPDbgFmtEncoderFrmRate,
 *   IMPDbgFmtEncoderGop, IMPDbgFmtEncoderEncAttr, IMPDbgFmtEncoderAttrFixQP,
 *   IMPDbgFmtEncoderAttrCBR, IMPDbgFmtEncoderAttrVBR,
 *   IMPDbgFmtEncoderAttrCappedVBR, IMPDbgFmtEncoderAttrCappedQuality,
 *   IMPDbgFmtEncoderAttrRcMode, IMPDbgFmtEncoderRcAttr, dbg_enc_info,
 *   IMPDbgFmtEncoderSuperFrmInfoGet, channel_encoder_set_rc_param (decomp body),
 *   do_release_frame, set_release_frame, wait_encode_avail,
 *   release_frame_thread, release_used_framestream (decomp body),
 *   on_encoder_group_data_update, sub_8eea0, sub_8f550, sub_8f5fc,
 *   sub_8f698, sub_8fdc4, sub_9023c, channel_encoder_exit (decomp body),
 *   channel_encoder_init (decomp body), update_one_frmstrm, update_frmstrm
 *
 * All PENDING symbols are forward-declared at the top of this TU or are
 * expected to be ported in follow-up tasks. Functions that reference them
 * (IMP_Encoder_CreateChn, IMP_Encoder_DestroyChn, IMP_Encoder_ReleaseStream,
 * IMP_Encoder_SetChnAttrRcMode, EncoderInit) rely on those externs being
 * resolved at link time.
 *
 * END PARTIAL_AT
 * ============================================================================ */

/* End of src/video/imp_encoder.c */
