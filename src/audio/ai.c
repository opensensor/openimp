#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <unistd.h>

#include "imp/imp_audio.h"

#define READ_I32(base, off) (*(int32_t *)((char *)(base) + (off)))
#define WRITE_I32(base, off, val) (*(int32_t *)((char *)(base) + (off)) = (val))
#define WRITE_PTR(base, off, val) (*(void **)((char *)(base) + (off)) = (val))

int32_t IMP_Log_Get_Option(void); /* forward decl, ported by T<N> later */
int32_t imp_log_fun(int32_t level, int32_t option, int32_t type, ...); /* forward decl, ported by T<N> later */
int32_t audio_buf_try_get_node(int32_t *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t Hpf_gen_filter_coefficients(int16_t *arg1, uint32_t arg2, uint32_t arg3,
    int32_t arg4, int32_t arg5, double arg6, double arg7); /* forward decl, ported by T<N> later */
int pthread_kill(pthread_t thread, int sig); /* libc decl on some host libcs needs feature macros */

static int32_t data_11b498;
static int32_t data_11b49c;
static int32_t data_11b4a0;
static int32_t data_11b4a4;
static int32_t data_11b4a8;
static int32_t data_11b4ac;
static int32_t data_11b4c8;
static int32_t data_11b4cc;
static int32_t data_11b5e0;
static int32_t data_11b5e4;

static int16_t kFilterCoefficients[] = {0x0e51, (int16_t)0xe35e, 0x0e51, 0x1c75, (int16_t)0xf330, 0};
static int16_t kFilterCoefficients8kHz[] = {0x0ed6, (int16_t)0xe254, 0x0ed6, 0x1e7f, (int16_t)0xf16b, 0};
static int16_t AiCoefficients[6];

static FILE *ai_target_file;
static FILE *ai_source_file;
static int32_t ai_frm_num;

static int32_t (*fun_ai_hpf_process)(void *arg1, void *arg2, int32_t arg3);
static int32_t (*fun_ai_hpf_create)(void *arg1, ...);

int32_t impdbg_ai_dev_info(void *arg1)
{
    int32_t v0_1;

    if (arg1 == NULL) {
        return -1;
    }

    v0_1 = sprintf((char *)arg1 + 0x18,
        "\tDEV(ID:%d)\nAudioIoAttr:\n\tSamplerate:%5d\n\tbitwidth  :%5d\n\tsoundmode :%5d\n\tfrmNum    :%5d\n\tnumPerFrm :%5d\n\tchnCnt    :%5d\n",
        1, data_11b498, data_11b49c, data_11b4a0, data_11b4a4, data_11b4a8, data_11b4ac);
    WRITE_I32(arg1, 0x14, v0_1 + sprintf((char *)arg1 + 0x18 + v0_1,
        "ChannalAttr:\n\tminVol:%5d\n\tmaxVol:%5d\n\tsetVol:%5d\n\tGain  :%5d\n",
        data_11b4c8, data_11b4cc, data_11b5e0, data_11b5e4));
    WRITE_I32(arg1, 0xc, 0);
    WRITE_I32(arg1, 0x10, 0);
    WRITE_I32(arg1, 8, READ_I32(arg1, 8) | 0x100);
    return 0;
}

int32_t _ai_chn_disable_ref_wait(void *arg1, void *arg2)
{
    struct timeval var_30;
    struct timespec var_38;
    int32_t i;

    gettimeofday(&var_30, 0);
    var_38.tv_sec = var_30.tv_sec + 5;
    var_38.tv_nsec = var_30.tv_usec * 0x3e8;
    if (pthread_kill(*(pthread_t *)((char *)arg1 + 0xc), 0) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x1d3,
            "_ai_chn_disable_ref_wait", "disable ref ai thread has exit!\n");
        WRITE_I32(arg2, 4, 0);
        return 0;
    }

    WRITE_I32(arg2, 0x100, 0);
    WRITE_I32(arg2, 0x6c, 0);
    i = 0;
    while (i == 0) {
        if (pthread_cond_timedwait((pthread_cond_t *)((char *)arg2 + 0xa0),
                (pthread_mutex_t *)((char *)arg1 + 0x208), &var_38) == 0x91) {
            imp_log_fun(5, IMP_Log_Get_Option(), 2,
                "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x1dc,
                "_ai_chn_disable_ref_wait", "_ai_chn_disable_wait timeout\n");
        }
        i = READ_I32(arg2, 0x6c);
        WRITE_I32(arg2, 0x6c, 0);
    }

    return 0;
}

uint32_t impdbg_ai_get_frm(void *arg1)
{
    uint32_t i;

    if (arg1 == NULL) {
        return 0xffffffff;
    }

    remove("ai_source_record.pcm");
    remove("ai_target_record.pcm");
    ai_source_file = fopen("ai_source_record.pcm", "wb");
    if (ai_source_file == NULL) {
        i = 0xffffffff;
        puts("open source file fail");
        return i;
    }

    ai_target_file = fopen("ai_target_record.pcm", "wb");
    if (ai_target_file == NULL) {
        i = 0xffffffff;
        puts("open target file fail");
        return i;
    }

    ai_frm_num = READ_I32(arg1, 0x10);
    if (ai_frm_num <= 0) {
        i = 0xffffffff;
        printf("input frm(%d) err,clear file!!!\n", ai_frm_num);
        fclose(ai_source_file);
        fclose(ai_target_file);
        ai_source_file = NULL;
        ai_target_file = NULL;
        ai_frm_num = 0;
        remove("ai_source_record.pcm");
        remove("ai_target_record.pcm");
        return i;
    }

    i = (uint32_t)ai_frm_num;
    do {
    } while (i != 0);
    fclose(ai_source_file);
    fclose(ai_target_file);
    ai_source_file = NULL;
    ai_target_file = NULL;
    return i;
}

int32_t _ai_set_tmpbuf_size(IMPAudioIOAttr *arg1, void *arg2)
{
    uint64_t product;
    uint32_t v0_1;
    uint32_t lo_2;

    if (arg1 == NULL || arg2 == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x423,
            "_ai_set_tmpbuf_size", "fun:%s,parameters is invalid.\n", "_ai_set_tmpbuf_size");
    }

    product = (uint64_t)(uint32_t)arg1->samplerate * 0x51eb851fULL;
    v0_1 = (uint32_t)(product >> 0x25);
    lo_2 = (uint32_t)arg1->numPerFrm / v0_1;
    if (v0_1 == 0) {
        __builtin_trap();
    }
    if (lo_2 == 2) {
        WRITE_I32(arg2, 0x1b0, (int32_t)(v0_1 << 2));
        return 0;
    }
    if (lo_2 == 3) {
        WRITE_I32(arg2, 0x1b0, (int32_t)(v0_1 * 0xc));
        return 0;
    }
    if (lo_2 == 1) {
        WRITE_I32(arg2, 0x1b0, (int32_t)(v0_1 * 6));
        return 0;
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2,
        "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x433,
        "_ai_set_tmpbuf_size", "fun:%s,error case.\n", "_ai_set_tmpbuf_size");
    return -1;
}

void *_ai_alloc_tmpbuf(size_t arg1)
{
    void *result;

    if ((ssize_t)arg1 <= 0) {
        result = NULL;
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x43f,
            "_ai_alloc_tmpbuf", "fun:%s,parameter size is smaller than zero.\n", "_ai_alloc_tmpbuf");
    } else {
        void *result_1 = calloc(arg1, 1);

        result = result_1;
        if (result_1 == NULL) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2,
                "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x445,
                "_ai_alloc_tmpbuf", "fun:%s,malloc buf failed\n", "_ai_alloc_tmpbuf");
        }
    }

    return result;
}

int32_t __ai_dev_init(void *arg1)
{
    int32_t var_14 = 0x10;
    int32_t var_18 = 1;
    int32_t v0;
    char *var_28_2;
    const char *var_2c_1;
    int32_t v0_12;

    v0 = open("/dev/dsp", 0x80000);
    WRITE_I32(arg1, 8, v0);
    if (v0 < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0xb7,
            "__ai_dev_init", "open ai dev %s error %s\n", "/dev/dsp",
            strerror(*__errno_location()));
        return -1;
    }
    if (ioctl(v0, 0xc0045002, (char *)arg1 + 0x10) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0xbc,
            "__ai_dev_init", "ioctl mic set samplerate %d error %s\n",
            READ_I32(arg1, 0x10), strerror(*__errno_location()));
        return -1;
    }
    if (ioctl(READ_I32(arg1, 8), 0xc0045006, &var_18) != 0) {
        var_28_2 = strerror(*__errno_location());
        var_2c_1 = "ai dev SNDCTL_DSP_CHANNELS error %s\n";
        v0_12 = 0xc1;
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", v0_12,
            "__ai_dev_init", var_2c_1, var_28_2);
        return -1;
    }
    if (ioctl(READ_I32(arg1, 8), 0xc0045005, &var_14) == 0) {
        if (ioctl(READ_I32(arg1, 8), 0x40045066, 1) == 0) {
            return 0;
        }
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0xcb,
            "__ai_dev_init", "ai dev SNDCTL_EXT_ENABLE_STREAM error %s\n",
            strerror(*__errno_location()));
        return -1;
    }
    var_28_2 = strerror(*__errno_location());
    var_2c_1 = "ai dev SNDCTL_DSP_SETFMT error %s\n";
    v0_12 = 0xc6;
    imp_log_fun(6, IMP_Log_Get_Option(), 2,
        "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", v0_12,
        "__ai_dev_init", var_2c_1, var_28_2);
    return -1;
}

int32_t __ai_dev_deinit(void *arg1)
{
    int32_t v0 = READ_I32(arg1, 8);

    if (v0 <= 0) {
        return 0;
    }

    close(v0);
    WRITE_I32(arg1, 8, -1);
    return 0;
}

int32_t __ai_dev_set_gain(void *arg1, int32_t arg2)
{
    int32_t var_18 = arg2;
    int32_t result = ioctl(READ_I32(arg1, 8), 0x4004505b, &var_18);

    if (result != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0xdf,
            "__ai_dev_set_gain", "err: set ai gain %s\n", strerror(*__errno_location()));
    }
    return result;
}

int32_t __ai_dev_enable_aec(void *arg1)
{
    int32_t var_18 = 1;
    int32_t result = ioctl(READ_I32(arg1, 8), 0x40045065, &var_18);

    if (result != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0xe9,
            "__ai_dev_enable_aec", "err: SNDCTL_EXT_ENABLE_AEC %s\n", strerror(*__errno_location()));
    }
    return result;
}

int32_t __ai_dev_disable_aec(void *arg1)
{
    int32_t var_18 = 0;
    int32_t result = ioctl(READ_I32(arg1, 8), 0x40045064, &var_18);

    if (result != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0xf3,
            "__ai_dev_disable_aec", "err: SNDCTL_EXT_DISABLE_AEC %s\n", strerror(*__errno_location()));
    }
    return result;
}

int32_t __ai_dev_read(void *arg1, void *arg2, int32_t arg3, int32_t arg4, int32_t arg5)
{
    int32_t var_18 = arg3;
    int32_t var_14_1;

    if (READ_I32(arg2, 0x100) != 0 || READ_I32(arg2, 0x10c) != 0) {
        if (arg4 == 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2,
                "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x107,
                "__ai_dev_read", "ai ref enabled but ref buf is NULL\n");
            return -1;
        }
        var_14_1 = arg4;
    } else {
        var_14_1 = 0;
    }

    (void)var_14_1;
    if (ioctl(READ_I32(arg1, 8), 0x400c5068, &var_18) == 0) {
        return arg5;
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2,
        "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x118,
        "__ai_dev_read", "ai SNDCTL_EXT_GET_AI_STREAM error %s\n", strerror(*__errno_location()));
    return -1;
}

int32_t _ai_thread_post(void *arg1)
{
    int32_t result = pthread_cond_signal((pthread_cond_t *)((char *)arg1 + 0x220));

    if (result != 0) {
        printf("err: %s,%d ret= %d\n", "_ai_thread_post", 0x193,
            pthread_cond_signal((pthread_cond_t *)((char *)arg1 + 0x220)));
    }
    return result;
}

int32_t _ai_thread_wait(void *arg1)
{
    struct timeval var_10;
    struct timespec var_18;

    gettimeofday(&var_10, 0);
    var_18.tv_sec = var_10.tv_sec + 5;
    var_18.tv_nsec = var_10.tv_usec * 0x3e8;
    pthread_cond_timedwait((pthread_cond_t *)((char *)arg1 + 0x220),
        (pthread_mutex_t *)((char *)arg1 + 0x208), &var_18);
    return 0;
}

int32_t _ai_chn_lock(void *arg1)
{
    int32_t result = pthread_mutex_lock((pthread_mutex_t *)((char *)arg1 + 0x50));

    if (result != 0) {
        printf("err: %s,%d ret= %d\n", "_ai_chn_lock", 0x1eb,
            pthread_mutex_lock((pthread_mutex_t *)((char *)arg1 + 0x50)));
    }
    return result;
}

int32_t _ai_chn_unlock(void *arg1)
{
    int32_t result = pthread_mutex_unlock((pthread_mutex_t *)((char *)arg1 + 0x50));

    if (result != 0) {
        printf("err: %s,%d ret= %d\n", "_ai_chn_unlock", 0x1f1,
            pthread_mutex_unlock((pthread_mutex_t *)((char *)arg1 + 0x50)));
    }
    return result;
}

int32_t _ai_polling_frame_wait(void *arg1)
{
    struct timeval var_30;
    struct timespec var_38;
    pthread_cond_t *s1;
    int32_t *a2_1;

    _ai_chn_lock(arg1);
    gettimeofday(&var_30, 0);
    var_38.tv_sec = var_30.tv_sec + 5;
    var_38.tv_nsec = var_30.tv_usec * 0x3e8;
    s1 = (pthread_cond_t *)((char *)arg1 + 0xd0);
    while (1) {
        a2_1 = (int32_t *)&var_38;
        if (audio_buf_try_get_node((int32_t *)(intptr_t)READ_I32(arg1, 0x110), 1) != 0) {
            break;
        }
        if (pthread_cond_timedwait(s1, (pthread_mutex_t *)((char *)arg1 + 0x50), &var_38) == 0x91) {
            s1 = (pthread_cond_t *)((char *)arg1 + 0xd0);
            imp_log_fun(5, IMP_Log_Get_Option(), 2,
                "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x200,
                "_ai_polling_frame_wait", "_ai_polling_frame_wait timeout\n");
        }
    }
    _ai_chn_unlock(arg1);
    return 0;
}

int32_t _ai_InitializeFilter(void *arg1, int32_t arg2, int32_t arg3)
{
    if (arg1 == NULL) {
        puts("hpf is NULL");
        return -1;
    }
    if (arg3 != 0) {
        Hpf_gen_filter_coefficients(AiCoefficients, (uint32_t)arg2, (uint32_t)arg3, 0, 0, 0.0, 0.0);
        WRITE_PTR(arg1, 0xc, AiCoefficients);
    } else if (arg2 == 0x1f40) {
        WRITE_PTR(arg1, 0xc, kFilterCoefficients8kHz);
    } else {
        WRITE_PTR(arg1, 0xc, kFilterCoefficients);
    }
    fun_ai_hpf_create((char *)arg1 + 8, arg1, 0, 0, 2, 4);
    return 0;
}

int32_t _ai_HPF_Filter(void *arg1, int32_t arg2, int32_t arg3)
{
    int32_t v0_1 = arg3 / 0xa0;
    int32_t i;

    if (v0_1 == 0) {
        __builtin_trap();
    }
    if (v0_1 > 0) {
        i = arg2;
        do {
            int32_t v0_4 = fun_ai_hpf_process(arg1, (void *)(intptr_t)i,
                ((((uint32_t)arg3 >> 0x1f) + arg3) >> 1) / v0_1);

            i += 0xa0;
            if (v0_4 != 0) {
                printf("%s: Filter error\n", "_ai_HPF_Filter");
                return -1;
            }
        } while (i != arg2 + v0_1 * 0xa0);
    }

    return 0;
}

int32_t _ai_get_buf_size(void)
{
    return data_11b4a8 << 1;
}

int32_t _ai_ref_enable(void *arg1, void *arg2)
{
    int32_t result = __ai_dev_enable_aec(arg1);

    if (result != 0) {
        puts("err: __ai_dev_enable_aec");
        return result;
    }

    _ai_chn_lock(arg2);
    WRITE_PTR(arg2, 0x104, calloc((size_t)READ_I32(arg2, 0x20) * (size_t)(READ_I32(arg1, 0x20) << 1), 1));
    if (*(void **)((char *)arg2 + 0x104) != NULL) {
        _ai_chn_unlock(arg2);
        return 0;
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2,
        "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x3f9,
        "_ai_ref_enable", "%s %d Ai ref buf malloc error.\n", "_ai_ref_enable", 0x3f9);
    _ai_chn_unlock(arg2);
    return -1;
}

int32_t _ai_ref_disable(void *arg1)
{
    int32_t result = __ai_dev_disable_aec(arg1);

    if (result != 0) {
        puts("err: __ai_dev_disable_aec");
    }
    return result;
}

/* ==============================================================
 * T80 round 3: public IMP_AI_* API surface (33 functions).
 *
 * The stock libimp keeps a giant flat per-device / per-channel state
 * table ("aiDev") at offset 0x11b22c in .bss.  Modelling that array
 * byte-for-byte is out of scope for this round — the goal is a clean
 * compile that matches the public header signatures while preserving
 * the stock control-flow shape (argument validation, log strings,
 * and return codes).  The channel/device state is tracked here in
 * named structs that mirror the binary fields actually referenced.
 * ============================================================== */

#define AI_MAX_DEV 2
#define AI_MAX_CHN 3

typedef struct {
    int32_t enabled;                 /* 0x11b22c */
    pthread_t record_thread;         /* 0x11b234 */
    IMPAudioIOAttr attr;             /* 0x11b238 .. 0x11b24c */
    int32_t num_per_frm_aligned;     /* 0x11b250 */
    int32_t frmnum_multiple_of_10ms; /* 0x11b254 */
    pthread_mutex_t dev_mutex;       /* 0x11b430 */
    pthread_cond_t  dev_cond;        /* 0x11b448 */
    int32_t sound_mode;              /* cached */
    int32_t dev_fd;                  /* AiDevInfo.ai_fd (for __ai_dev_*) */
} AiDevState;

typedef struct {
    int32_t enabled;                 /* 0x11b264 */
    int32_t min_vol;                 /* 0x11b268 */
    int32_t max_vol;                 /* 0x11b26c */
    int32_t cur_vol;                 /* 0x11b270 */
    int32_t mute_pad;                /* 0x11b274 */
    int32_t mute;                    /* 0x11b278 */
    int32_t usr_frm_depth;           /* 0x11b280 */
    int32_t aec_enabled;             /* 0x11b36c */
    int32_t aec_ref_enabled;         /* 0x11b360 */
    int32_t vol;                     /* 0x11b380 (echoes volume) */
    int32_t gain;                    /* 0x11b384 */
    int32_t alc_gain;                /* 0x11b388 */
} AiChnState;

static AiDevState ai_dev_state[AI_MAX_DEV];
static AiChnState ai_chn_state[AI_MAX_DEV][AI_MAX_CHN];

/* AGC / NS / HPF enable flags (binary globals). */
static int32_t ai_agc_enabled;       /* data_11b5f0 */
static int32_t ai_ns_enabled;        /* data_11b5fc */
static int32_t ai_hpf_enabled;       /* data_11b5f4 */
static int32_t ai_hpf_cutoff;        /* data_11b5ec */
static int32_t ai_any_dev_enabled;   /* data_11b48c */

/* WebRTC profile path buffer (referenced in IMP_AI_Set_WebrtcProfileIni_Path). */
static char ai_webrtc_pathbuff[256];

/* Forward declarations for private helpers / foreign symbols. */
extern int32_t func_init(void); /* forward decl, ported by T<N> later */
extern int32_t dsys_func_share_mem_register(int32_t arg1, int32_t arg2,
    const char *arg3, void *arg4); /* forward decl, ported by T<N> later */
extern int32_t init_audioProcess_library(char *arg1); /* forward decl, ported by T<N> later */
extern int32_t deinit_audioProcess_library(void); /* forward decl, ported by T<N> later */

static int32_t _ai_validate_dev(int dev_id, const char *fn, int line)
{
    if (dev_id < 0 || dev_id >= AI_MAX_DEV) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c",
            line, fn, "Invalid ai device ID.\n");
        return -1;
    }
    return 0;
}

static int32_t _ai_validate_chn(int chn, const char *fn, int line)
{
    if (chn <= 0 || chn >= AI_MAX_CHN) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c",
            line, fn, "Invalid ai channel.\n");
        return -1;
    }
    return 0;
}

int IMP_AI_IMPDBG_Init(void)
{
    /* Decompile: return func_init() + dsys_func_share_mem_register(...)
     * Forward-declared; safe to call because the other audio modules share
     * these helpers and our ported tree resolves them at link time. */
    return dsys_func_share_mem_register(4, 0, "mpdbg_ai_dev_info", (void *)impdbg_ai_dev_info)
         + dsys_func_share_mem_register(4, 1, "impdbg_ai_get_frm", (void *)impdbg_ai_get_frm);
}

int IMP_AI_SetPubAttr(int audioDevId, IMPAudioIOAttr *attr)
{
    uint32_t samples_per_10ms;

    if (_ai_validate_dev(audioDevId, "IMP_AI_SetPubAttr", 0x5d8) != 0) {
        return -1;
    }
    if (attr == NULL || attr->chnCnt >= 2) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x5dc,
            "IMP_AI_SetPubAttr", "set ai pub attr error, invalid chnCnt.\n");
        return -1;
    }
    if (attr->samplerate == 0) {
        __builtin_trap();
    }
    /* numPerFrm must be a multiple of 10ms: (numPerFrm * 1000) / samplerate % 10 == 0 */
    samples_per_10ms = (uint32_t)attr->samplerate / 100;
    if (samples_per_10ms == 0 ||
        ((uint32_t)(attr->numPerFrm * 1000) / (uint32_t)attr->samplerate) % 10 != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x5e1,
            "IMP_AI_SetPubAttr",
            "ai samples(numPerFrm) must be an integer number of 10ms (10ms * n).\n");
        return -1;
    }

    ai_dev_state[audioDevId].num_per_frm_aligned = attr->numPerFrm;
    ai_dev_state[audioDevId].frmnum_multiple_of_10ms = 1;
    ai_dev_state[audioDevId].attr = *attr;
    ai_dev_state[audioDevId].sound_mode = attr->soundmode;

    /* Mirror stock stats globals used by impdbg_ai_dev_info. */
    data_11b498 = attr->samplerate;
    data_11b49c = attr->bitwidth;
    data_11b4a0 = attr->soundmode;
    data_11b4a4 = attr->frmNum;
    data_11b4a8 = attr->numPerFrm;
    data_11b4ac = attr->chnCnt;
    return 0;
}

int IMP_AI_GetPubAttr(int audioDevId, IMPAudioIOAttr *attr)
{
    if (_ai_validate_dev(audioDevId, "IMP_AI_GetPubAttr", 0x5f2) != 0 || attr == NULL) {
        return -1;
    }
    *attr = ai_dev_state[audioDevId].attr;
    attr->numPerFrm = ai_dev_state[audioDevId].num_per_frm_aligned;
    return 0;
}

int IMP_AI_Enable(int audioDevId)
{
    AiDevState *dev;

    imp_log_fun(4, IMP_Log_Get_Option(), 2,
        "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x601,
        "IMP_AI_Enable", "AI Enable: %d\n", audioDevId);

    if (_ai_validate_dev(audioDevId, "IMP_AI_Enable", 0x603) != 0) {
        return -1;
    }

    dev = &ai_dev_state[audioDevId];
    if (dev->enabled == 1) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x608,
            "IMP_AI_Enable", "ai device is already enabled.\n");
        return 0;
    }

    if (dev->attr.soundmode != AUDIO_SOUND_MODE_MONO) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x60d,
            "IMP_AI_Enable", "ai sound mode error, only support AUDIO_SOUND_MODE_MONO\n");
        return -1;
    }
    if (dev->attr.bitwidth != AUDIO_BIT_WIDTH_16) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x611,
            "IMP_AI_Enable", "ai in bit width error, only support AUDIO_BIT_WIDTH_16\n");
        return -1;
    }

    if (__ai_dev_init(dev) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x616,
            "IMP_AI_Enable", "__ai_dev_init error %s\n", strerror(errno));
        return -1;
    }

    pthread_mutex_init(&dev->dev_mutex, NULL);
    pthread_cond_init(&dev->dev_cond, NULL);
    dev->enabled = 1;
    ai_any_dev_enabled = 1;
    return 0;
}

int IMP_AI_Disable(int audioDevId)
{
    AiDevState *dev;

    imp_log_fun(4, IMP_Log_Get_Option(), 2,
        "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x629,
        "IMP_AI_Disable", "AI Disable: %d\n", audioDevId);

    if (_ai_validate_dev(audioDevId, "IMP_AI_Disable", 0x62b) != 0) {
        return -1;
    }

    dev = &ai_dev_state[audioDevId];
    if (dev->enabled == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x62f,
            "IMP_AI_Disable", "ai device is already Disabled.\n");
        return 0;
    }

    pthread_mutex_lock(&dev->dev_mutex);
    dev->enabled = 0;
    _ai_thread_post(dev);
    pthread_mutex_unlock(&dev->dev_mutex);

    __ai_dev_deinit(dev);
    pthread_mutex_destroy(&dev->dev_mutex);
    pthread_cond_destroy(&dev->dev_cond);
    if (ai_any_dev_enabled != 0) {
        deinit_audioProcess_library();
        ai_any_dev_enabled = 0;
    }
    return 0;
}

int IMP_AI_EnableChn(int audioDevId, int aiChn)
{
    AiDevState *dev;
    AiChnState *chn;

    imp_log_fun(4, IMP_Log_Get_Option(), 2,
        "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x651,
        "IMP_AI_EnableChn", "AI Enable Chn: %d-%d\n", audioDevId, aiChn);

    if (_ai_validate_dev(audioDevId, "IMP_AI_EnableChn", 0x653) != 0) {
        return -1;
    }
    if (_ai_validate_chn(aiChn, "IMP_AI_EnableChn", 0x657) != 0) {
        return -1;
    }

    dev = &ai_dev_state[audioDevId];
    if (dev->enabled == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x65d,
            "IMP_AI_EnableChn", "[%s] [%d] ai device is no enabled.\n",
            "IMP_AI_EnableChn", 0x65d);
        return -1;
    }

    chn = &ai_chn_state[audioDevId][aiChn];
    if (chn->enabled == 1) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x661,
            "IMP_AI_EnableChn", "[%s] [%d] ai channel is already enabled.\n",
            "IMP_AI_EnableChn", 0x661);
        return 0;
    }

    if (IMP_AI_IMPDBG_Init() != 0) {
        puts("ai impdbg init fail!!!");
    }

    chn->enabled = 1;
    return 0;
}

int IMP_AI_DisableChn(int audioDevId, int aiChn)
{
    AiChnState *chn;

    imp_log_fun(4, IMP_Log_Get_Option(), 2,
        "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x6a8,
        "IMP_AI_DisableChn", "AI Disable Chn: %d-%d\n", audioDevId, aiChn);

    if (_ai_validate_dev(audioDevId, "IMP_AI_DisableChn", 0x6aa) != 0) {
        return -1;
    }
    if (_ai_validate_chn(aiChn, "IMP_AI_DisableChn", 0x6ae) != 0) {
        return -1;
    }

    if (ai_dev_state[audioDevId].enabled == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x6b2,
            "IMP_AI_DisableChn", "[%s] [%d] ai device is no enabled.\n",
            "IMP_AI_DisableChn", 0x6b2);
        return -1;
    }

    chn = &ai_chn_state[audioDevId][aiChn];
    if (chn->enabled == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x6b6,
            "IMP_AI_DisableChn", "[%s] [%d] ai channel is already disabled.\n",
            "IMP_AI_DisableChn", 0x6b6);
        return 0;
    }

    chn->enabled = 0;
    return 0;
}

int IMP_AI_SetChnParam(int audioDevId, int aiChn, IMPAudioIChnParam *attr)
{
    if (_ai_validate_dev(audioDevId, "IMP_AI_SetChnParam", 0x8a9) != 0) {
        return -1;
    }
    if (_ai_validate_chn(aiChn, "IMP_AI_SetChnParam", 0x8ad) != 0) {
        return -1;
    }
    if (ai_dev_state[audioDevId].enabled == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x8b4,
            "IMP_AI_SetChnParam", "[%s] [%d] ai device is no enabled.\n",
            "IMP_AI_SetChnParam", 0x8b4);
        return -1;
    }
    if (attr == NULL || attr->usrFrmDepth < 2 ||
        attr->usrFrmDepth > ai_dev_state[audioDevId].attr.frmNum) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x8b9,
            "IMP_AI_SetChnParam", "Invalid ai channel usrFrmDepth.\n");
        return -1;
    }
    ai_chn_state[audioDevId][aiChn].usr_frm_depth = attr->usrFrmDepth;
    return 0;
}

int IMP_AI_GetChnParam(int audioDevId, int aiChn, IMPAudioIChnParam *attr)
{
    if (_ai_validate_dev(audioDevId, "IMP_AI_GetChnParam", 0x8c4) != 0) {
        return -1;
    }
    if (_ai_validate_chn(aiChn, "IMP_AI_GetChnParam", 0x8c8) != 0) {
        return -1;
    }
    if (ai_dev_state[audioDevId].enabled == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x8cd,
            "IMP_AI_GetChnParam", "[%s] [%d] ai device is no enabled.\n",
            "IMP_AI_GetChnParam", 0x8cd);
        return -1;
    }
    if (attr == NULL) {
        return -1;
    }
    attr->usrFrmDepth = ai_chn_state[audioDevId][aiChn].usr_frm_depth;
    return 0;
}

int IMP_AI_SetVol(int audioDevId, int aiChn, int vol)
{
    int clamped;
    AiChnState *chn;

    if (_ai_validate_dev(audioDevId, "IMP_AI_SetVol", 0x989) != 0) {
        return -1;
    }
    if (_ai_validate_chn(aiChn, "IMP_AI_SetVol", 0x98d) != 0) {
        return -1;
    }
    imp_log_fun(4, IMP_Log_Get_Option(), 2,
        "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x990,
        "IMP_AI_SetVol", "AI Set Vol: %d\n", vol);

    /* Binary clamps to [-30, 120]. */
    clamped = vol;
    if (clamped < -30) clamped = -30;
    if (clamped >= 121) clamped = 120;

    chn = &ai_chn_state[audioDevId][aiChn];
    chn->min_vol = -30;
    chn->max_vol = 120;
    chn->mute = 0;
    chn->vol = clamped;
    data_11b5e0 = clamped; /* impdbg set-vol stat */
    return 0;
}

int IMP_AI_GetVol(int audioDevId, int aiChn, int *vol)
{
    if (_ai_validate_dev(audioDevId, "IMP_AI_GetVol", 0x9aa) != 0) {
        return -1;
    }
    if (_ai_validate_chn(aiChn, "IMP_AI_GetVol", 0x9ae) != 0) {
        return -1;
    }
    if (vol == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x9b3,
            "IMP_AI_GetVol", "vol is NULL.\n");
        return -1;
    }
    *vol = ai_chn_state[audioDevId][aiChn].vol;
    imp_log_fun(4, IMP_Log_Get_Option(), 2,
        "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x9b7,
        "IMP_AI_GetVol", "AI Get Vol: %d\n", *vol);
    return 0;
}

int IMP_AI_SetGain(int audioDevId, int aiChn, int gain)
{
    int clamped = gain;

    if (_ai_validate_dev(audioDevId, "IMP_AI_SetGain", 0x9c1) != 0) {
        return -1;
    }
    if (_ai_validate_chn(aiChn, "IMP_AI_SetGain", 0x9c5) != 0) {
        return -1;
    }
    imp_log_fun(4, IMP_Log_Get_Option(), 2,
        "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x9c8,
        "IMP_AI_SetGain", "AI Set Gain: %d\n", gain);
    if (clamped < 0) clamped = 0;
    if (clamped > 31) clamped = 31;

    if (__ai_dev_set_gain(&ai_dev_state[audioDevId], clamped) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x9d5,
            "IMP_AI_SetGain", "err: __ai_dev_set_gain\n");
    }
    ai_chn_state[audioDevId][aiChn].gain = clamped;
    data_11b5e4 = clamped; /* impdbg gain stat */
    return 0;
}

int IMP_AI_GetGain(int audioDevId, int aiChn, int *gain)
{
    if (_ai_validate_dev(audioDevId, "IMP_AI_GetGain", 0x9df) != 0) {
        return -1;
    }
    if (_ai_validate_chn(aiChn, "IMP_AI_GetGain", 0x9e3) != 0) {
        return -1;
    }
    if (gain == NULL) {
        return -1;
    }
    *gain = ai_chn_state[audioDevId][aiChn].gain;
    imp_log_fun(4, IMP_Log_Get_Option(), 2,
        "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x9e8,
        "IMP_AI_GetGain", "AI Get Gain: %d\n", *gain);
    return 0;
}

int IMP_AI_SetAlcGain(int audioDevId, int aiChn, int gain)
{
    int clamped = gain;

    if (_ai_validate_dev(audioDevId, "IMP_AI_SetAlcGain", 0x9f2) != 0) {
        return -1;
    }
    if (_ai_validate_chn(aiChn, "IMP_AI_SetAlcGain", 0x9f6) != 0) {
        return -1;
    }
    imp_log_fun(4, IMP_Log_Get_Option(), 2,
        "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x9f9,
        "IMP_AI_SetAlcGain", "AI Set Pga Gain: %d\n", gain);
    if (clamped < 0) clamped = 0;
    if (clamped > 7) clamped = 7;

    if (__ai_dev_set_gain(&ai_dev_state[audioDevId], clamped) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0xa06,
            "IMP_AI_SetAlcGain", "err: __ai_dev_set_pga_gain\n");
    }
    ai_chn_state[audioDevId][aiChn].alc_gain = clamped;
    return 0;
}

int IMP_AI_GetAlcGain(int audioDevId, int aiChn, int *gain)
{
    if (_ai_validate_dev(audioDevId, "IMP_AI_GetAlcGain", 0xa10) != 0) {
        return -1;
    }
    if (_ai_validate_chn(aiChn, "IMP_AI_GetAlcGain", 0xa14) != 0) {
        return -1;
    }
    if (gain == NULL) {
        return -1;
    }
    *gain = ai_chn_state[audioDevId][aiChn].alc_gain;
    imp_log_fun(4, IMP_Log_Get_Option(), 2,
        "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0xa19,
        "IMP_AI_GetAlcGain", "AI Get Pga Gain: %d\n", *gain);
    return 0;
}

int IMP_AI_PollingFrame(int audioDevId, int aiChn, uint32_t timeoutMs)
{
    (void)timeoutMs;
    if (_ai_validate_dev(audioDevId, "IMP_AI_PollingFrame", 0x859) != 0) {
        return -1;
    }
    if (_ai_validate_chn(aiChn, "IMP_AI_PollingFrame", 0x85d) != 0) {
        return -1;
    }
    if (ai_dev_state[audioDevId].enabled == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x862,
            "IMP_AI_PollingFrame", "[%s] [%d] ai device is no enabled.\n",
            "IMP_AI_PollingFrame", 0x862);
        return -1;
    }
    if (ai_chn_state[audioDevId][aiChn].enabled == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x867,
            "IMP_AI_PollingFrame", "%s %d ai Channel is no enabled.\n",
            "IMP_AI_PollingFrame", 0x867);
        return -1;
    }
    /* Full implementation calls _ai_polling_frame_wait — stub out for now. */
    return 0;
}

int IMP_AI_GetFrame(int audioDevId, int aiChn, IMPAudioFrame *frame, IMPBlock block)
{
    (void)block;
    if (_ai_validate_dev(audioDevId, "IMP_AI_GetFrame", 0x6e6) != 0) {
        return -1;
    }
    if (_ai_validate_chn(aiChn, "IMP_AI_GetFrame", 0x6ea) != 0) {
        return -1;
    }
    if (ai_dev_state[audioDevId].enabled == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x6f0,
            "IMP_AI_GetFrame", "[%s] [%d] ai device is no enabled.\n",
            "IMP_AI_GetFrame", 0x6f0);
        return -1;
    }
    if (ai_chn_state[audioDevId][aiChn].enabled == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x6f5,
            "IMP_AI_GetFrame", "%s %d ai Channel is no enabled.\n",
            "IMP_AI_GetFrame", 0x6f5);
        return -1;
    }
    if (frame == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x459,
            "_default_way_get_frame", "fun:%s,parameters is invalid.\n",
            "_default_way_get_frame");
        return -1;
    }
    /* Full implementation walks audio_buf_get_node and populates frame — stub. */
    memset(frame, 0, sizeof(*frame));
    frame->bitwidth = ai_dev_state[audioDevId].attr.bitwidth;
    frame->soundmode = ai_dev_state[audioDevId].attr.soundmode;
    return 0;
}

int IMP_AI_ReleaseFrame(int audioDevId, int aiChn, IMPAudioFrame *frame)
{
    if (_ai_validate_dev(audioDevId, "IMP_AI_ReleaseFrame", 0x875) != 0) {
        return -1;
    }
    if (_ai_validate_chn(aiChn, "IMP_AI_ReleaseFrame", 0x879) != 0) {
        return -1;
    }
    if (ai_dev_state[audioDevId].enabled == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x881,
            "IMP_AI_ReleaseFrame", "[%s] [%d] aio device is no enabled.\n",
            "IMP_AI_ReleaseFrame", 0x881);
        return -1;
    }
    if (ai_chn_state[audioDevId][aiChn].enabled == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x886,
            "IMP_AI_ReleaseFrame", "%s %d ai Channel is no enabled.\n",
            "IMP_AI_ReleaseFrame", 0x886);
        return -1;
    }
    if (frame == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x478,
            "_default_way_release_frame", "fun:%s,parameters is invalid.\n",
            "_default_way_release_frame");
        return -1;
    }
    return 0;
}

int IMP_AI_EnableNs(IMPAudioIOAttr *attr, int level)
{
    int clamped = level;

    if (attr == NULL) {
        return -1;
    }
    if ((uint32_t)clamped >= 4) {
        clamped = 3;
        imp_log_fun(5, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x516,
            "IMP_AI_EnableNs",
            "mode too big, should be 0 <= mode <= 3. use default mode 3.\n");
    }
    imp_log_fun(4, IMP_Log_Get_Option(), 2,
        "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x519,
        "IMP_AI_EnableNs", "AI NS ENABLE: mode = %d\n", clamped);

    if (ai_ns_enabled == 1) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x51e,
            "IMP_AI_EnableNs", "AI NS already enable\n");
        return 0;
    }
    if (init_audioProcess_library("libaudioProcess.so") != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x52c,
            "IMP_AI_EnableNs", "fun:%s,init audioProcess library failed\n",
            "IMP_AI_EnableNs");
        return -1;
    }
    ai_ns_enabled = 1;
    return 0;
}

int IMP_AI_DisableNs(void)
{
    imp_log_fun(4, IMP_Log_Get_Option(), 2,
        "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x56e,
        "IMP_AI_DisableNs", "AI NS DISABLE\n");
    if (ai_ns_enabled == 0) {
        imp_log_fun(5, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x575,
            "IMP_AI_DisableNs", "AI NS is not enabled\n");
        return 0;
    }
    ai_ns_enabled = 0;
    return 0;
}

int IMP_AI_EnableHpf(void)
{
    /* Header declares zero-arg; binary reads sample rate from *arg1.
     * Use a sensible default (16kHz) when called without context. */
    int32_t default_sr = 16000;

    imp_log_fun(4, IMP_Log_Get_Option(), 2,
        "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x592,
        "IMP_AI_EnableHpf", "AI HPF Enable\n");

    if (ai_hpf_cutoff < 0 || (uint32_t)default_sr < (uint32_t)ai_hpf_cutoff) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x596,
            "IMP_AI_EnableHpf", "HPF cut-off frequency is illegal.\n");
        return -1;
    }
    if (init_audioProcess_library("libaudioProcess.so") != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x59b,
            "IMP_AI_EnableHpf", "fun:%s,init audioProcess library failed\n",
            "IMP_AI_EnableHpf");
        return -1;
    }
    ai_hpf_enabled = 1;
    return 0;
}

int IMP_AI_DisableHpf(void)
{
    imp_log_fun(4, IMP_Log_Get_Option(), 2,
        "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x5c8,
        "IMP_AI_DisableHpf", "AI HPF DISABLE\n");
    ai_hpf_enabled = 0;
    fun_ai_hpf_create = NULL;
    fun_ai_hpf_process = NULL;
    return 0;
}

int IMP_AI_SetHpfCoFrequency(int freq)
{
    ai_hpf_cutoff = freq;
    return 0;
}

/* raptor-hal parity alias: some callers use the shorter name. */
int IMP_AI_SetHpfCoFreq(int freq)
{
    return IMP_AI_SetHpfCoFrequency(freq);
}

int IMP_AI_EnableAgc(IMPAudioIOAttr *attr, IMPAudioAgcConfig config)
{
    int32_t target = config.TargetLevelDbfs;
    int32_t comp   = config.CompressionGaindB;

    if (attr == NULL) {
        return -1;
    }
    imp_log_fun(4, IMP_Log_Get_Option(), 2,
        "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x4cb,
        "IMP_AI_EnableAgc",
        "AI AGC ENABLE: targetLevelDbfs = %d, compressionGaindB = %d, limiterEnable =%d\n",
        target, comp, 1);

    if (init_audioProcess_library("libaudioProcess.so") != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x4d4,
            "IMP_AI_EnableAgc", "fun:%s,init audioProcess library failed\n",
            "IMP_AI_EnableAgc");
        return -1;
    }

    if (attr->samplerate != AUDIO_SAMPLE_RATE_8000 &&
        attr->samplerate != AUDIO_SAMPLE_RATE_16000 &&
        attr->samplerate != AUDIO_SAMPLE_RATE_24000 &&
        attr->samplerate != AUDIO_SAMPLE_RATE_48000) {
        /* Stock allows multiple supported rates (8k/16k/24k/48k). */
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x4f0,
            "IMP_AI_EnableAgc", "Agc do not supoort samplerate: %d\n",
            attr->samplerate);
        return -1;
    }

    ai_agc_enabled = 1;
    return 0;
}

int IMP_AI_DisableAgc(void)
{
    imp_log_fun(4, IMP_Log_Get_Option(), 2,
        "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x504,
        "IMP_AI_DisableAgc", "AI AGC DISABLE\n");
    ai_agc_enabled = 0;
    return 0;
}

int IMP_AI_SetAgcMode(int mode)
{
    if (ai_agc_enabled == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x4bb,
            "IMP_AI_SetAgcMode", "Set agcmode error, ret = %d\n", -1);
        return -1;
    }
    (void)mode; /* stock stores in *agcMode static global — stubbed */
    return 0;
}

int IMP_AI_EnableAec(int aiDevId, int aiChn, int aoDevId, int aoChn)
{
    (void)aoDevId;
    (void)aoChn;
    imp_log_fun(4, IMP_Log_Get_Option(), 2,
        "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x8ea,
        "IMP_AI_EnableAec",
        "AI_EnableAec:AiDevId = %d,aiChn = %d,AoDevId = %d,aoChn = %d\n",
        aiDevId, aiChn, aoDevId, aoChn);

    if (_ai_validate_dev(aiDevId, "IMP_AI_EnableAec", 0x8ed) != 0) {
        return -1;
    }
    if (_ai_validate_chn(aiChn, "IMP_AI_EnableAec", 0x8f2) != 0) {
        return -1;
    }
    if (ai_dev_state[aiDevId].enabled == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x8f7,
            "IMP_AI_EnableAec", "[%s] [%d] Ai device is no enabled.\n",
            "IMP_AI_EnableAec", 0x8f7);
        return -1;
    }
    if (ai_chn_state[aiDevId][aiChn].enabled == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x8fc,
            "IMP_AI_EnableAec", "%s %d Ai Channel is no enabled.\n",
            "IMP_AI_EnableAec", 0x8fc);
        return -1;
    }
    imp_log_fun(4, IMP_Log_Get_Option(), 2,
        "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x902,
        "IMP_AI_EnableAec", "AI AEC ENABLE\n");
    if (ai_chn_state[aiDevId][aiChn].aec_ref_enabled == 1) {
        imp_log_fun(4, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x905,
            "IMP_AI_EnableAec",
            "%s %d ai Ref has enabled. AEC and Ref can not enable both.\n",
            "IMP_AI_EnableAec", 0x905);
        return 0;
    }
    ai_chn_state[aiDevId][aiChn].aec_enabled = 1;
    return 0;
}

int IMP_AI_DisableAec(int aiDevId, int aiChn)
{
    imp_log_fun(4, IMP_Log_Get_Option(), 2,
        "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x92e,
        "IMP_AI_DisableAec", "AI_DisableAec:AiDevId = %d,aiChn = %d\n",
        aiDevId, aiChn);
    if (_ai_validate_dev(aiDevId, "IMP_AI_DisableAec", 0x931) != 0) {
        return -1;
    }
    if (_ai_validate_chn(aiChn, "IMP_AI_DisableAec", 0x936) != 0) {
        return -1;
    }
    if (ai_dev_state[aiDevId].enabled == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x93b,
            "IMP_AI_DisableAec", "[%s] [%d] Ai device is no enabled.\n",
            "IMP_AI_DisableAec", 0x93b);
        return -1;
    }
    if (ai_chn_state[aiDevId][aiChn].enabled == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x940,
            "IMP_AI_DisableAec", "%s %d Ai Channel is no enabled.\n",
            "IMP_AI_DisableAec", 0x940);
        return -1;
    }
    imp_log_fun(4, IMP_Log_Get_Option(), 2,
        "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x946,
        "IMP_AI_DisableAec", "AI AEC DISABLE\n");
    if (ai_chn_state[aiDevId][aiChn].aec_enabled == 0) {
        imp_log_fun(4, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x949,
            "IMP_AI_DisableAec", "%s %d ai AEC is not enabled. \n",
            "IMP_AI_DisableAec", 0x949);
        return 0;
    }
    ai_chn_state[aiDevId][aiChn].aec_enabled = 0;
    return 0;
}

int IMP_AI_EnableAecRefFrame(int aiDevId, int aiChn, int aoDevId, int aoChn)
{
    (void)aoDevId;
    (void)aoChn;
    imp_log_fun(4, IMP_Log_Get_Option(), 2,
        "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x7f0,
        "IMP_AI_EnableAecRefFrame",
        "AI_EnableAecRefFrame:audioDevId = %d,aiChn = %d,audioAoDevId = %d,aoChn = %d\n",
        aiDevId, aiChn, aiDevId, aoChn);
    if (_ai_validate_dev(aiDevId, "IMP_AI_EnableAecRefFrame", 0x7f3) != 0) {
        return -1;
    }
    if (_ai_validate_chn(aiChn, "IMP_AI_EnableAecRefFrame", 0x7f7) != 0) {
        return -1;
    }
    if (ai_dev_state[aiDevId].enabled == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x7fd,
            "IMP_AI_EnableAecRefFrame", "[%s] [%d] Ai device is no enabled.\n",
            "IMP_AI_EnableAecRefFrame", 0x7fd);
        return -1;
    }
    if (ai_chn_state[aiDevId][aiChn].enabled == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x802,
            "IMP_AI_EnableAecRefFrame", "%s %d Ai Channel is no enabled.\n",
            "IMP_AI_EnableAecRefFrame", 0x802);
        return -1;
    }
    if (ai_chn_state[aiDevId][aiChn].aec_enabled == 1) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x806,
            "IMP_AI_EnableAecRefFrame",
            "%s %d Internal AEC has enabled. . AEC and Ref can not enable both.\n",
            "IMP_AI_EnableAecRefFrame", 0x806);
        return -1;
    }
    ai_chn_state[aiDevId][aiChn].aec_ref_enabled = 1;
    return 0;
}

int IMP_AI_DisableAecRefFrame(int aiDevId, int aiChn)
{
    /* Header declares 2-arg; binary takes 4 (ao ids ignored here). */
    imp_log_fun(4, IMP_Log_Get_Option(), 2,
        "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x82a,
        "IMP_AI_DisableAecRefFrame",
        "AI_DisableAecRefFrame:audioDevId = %d,aiChn = %d,audioAoDevId = %d,aoChn = %d\n",
        aiDevId, aiChn, aiDevId, 0);
    if (_ai_validate_dev(aiDevId, "IMP_AI_DisableAecRefFrame", 0x82d) != 0) {
        return -1;
    }
    if (_ai_validate_chn(aiChn, "IMP_AI_DisableAecRefFrame", 0x831) != 0) {
        return -1;
    }
    if (ai_dev_state[aiDevId].enabled == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x837,
            "IMP_AI_DisableAecRefFrame", "[%s] [%d] Ai device is no enabled.\n",
            "IMP_AI_DisableAecRefFrame", 0x837);
        return -1;
    }
    if (ai_chn_state[aiDevId][aiChn].enabled == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x83c,
            "IMP_AI_DisableAecRefFrame", "%s %d Ai Channel is no enabled.\n",
            "IMP_AI_DisableAecRefFrame", 0x83c);
        return -1;
    }
    if (ai_chn_state[aiDevId][aiChn].aec_ref_enabled == 0) {
        imp_log_fun(4, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x840,
            "IMP_AI_DisableAecRefFrame", "%s %d ai Ref is not enabled.\n",
            "IMP_AI_DisableAecRefFrame", 0x840);
        return 0;
    }
    ai_chn_state[aiDevId][aiChn].aec_ref_enabled = 0;
    return 0;
}

int IMP_AI_GetFrameAndRef(int audioDevId, int aiChn, IMPAudioFrame *frame,
                          IMPAudioFrame *ref, IMPBlock block)
{
    (void)block;
    if (_ai_validate_dev(audioDevId, "IMP_AI_GetFrameAndRef", 0x759) != 0) {
        return -1;
    }
    if (_ai_validate_chn(aiChn, "IMP_AI_GetFrameAndRef", 0x75d) != 0) {
        return -1;
    }
    if (ai_dev_state[audioDevId].enabled == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x763,
            "IMP_AI_GetFrameAndRef", "[%s] [%d] ai device is no enabled.\n",
            "IMP_AI_GetFrameAndRef", 0x763);
        return -1;
    }
    if (ai_chn_state[audioDevId][aiChn].enabled == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x768,
            "IMP_AI_GetFrameAndRef", "%s %d ai Channel is no enabled.\n",
            "IMP_AI_GetFrameAndRef", 0x768);
        return -1;
    }
    if (frame == NULL || ref == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x48d,
            "_default_way_get_frameAndref", "fun:%s,parameters is invalid.\n",
            "_default_way_get_frameAndref");
        return -1;
    }
    memset(frame, 0, sizeof(*frame));
    memset(ref, 0, sizeof(*ref));
    frame->bitwidth = ai_dev_state[audioDevId].attr.bitwidth;
    frame->soundmode = ai_dev_state[audioDevId].attr.soundmode;
    ref->bitwidth   = ai_dev_state[audioDevId].attr.bitwidth;
    ref->soundmode  = ai_dev_state[audioDevId].attr.soundmode;
    return 0;
}

int IMP_AI_SetVolMute(int audioDevId, int aiChn, int mute)
{
    AiChnState *chn;

    if (_ai_validate_dev(audioDevId, "IMP_AI_SetVolMute", 0x96e) != 0) {
        return -1;
    }
    if (_ai_validate_chn(aiChn, "IMP_AI_SetVolMute", 0x972) != 0) {
        return -1;
    }
    chn = &ai_chn_state[audioDevId][aiChn];
    if (mute == 1) {
        chn->cur_vol = 0;
        chn->mute_pad = 0;
        chn->mute = 1;
        return 0;
    }
    if (mute == 0) {
        chn->mute = 0;
        return 0;
    }
    imp_log_fun(6, IMP_Log_Get_Option(), 2,
        "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x97d,
        "IMP_AI_SetVolMute", "Invalid AI Mute Value.\n");
    return -1;
}

int IMP_AI_Set_WebrtcProfileIni_Path(const char *path)
{
    int ret;

    if (path == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x8d7,
            "IMP_AI_Set_WebrtcProfileIni_Path",
            "Invalid path set,The default path will be used.\n");
        return 0;
    }
    ret = access(path, 0);
    if (ret != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2,
            "Audio", "/home/user/git/proj/sdk-lv3/src/imp/audio/ai.c", 0x8dc,
            "IMP_AI_Set_WebrtcProfileIni_Path", "Invalid path set.\n");
        return -1;
    }
    snprintf(ai_webrtc_pathbuff, sizeof(ai_webrtc_pathbuff),
        "%s/%s", path, "webrtc_profile.ini");
    return ret;
}

