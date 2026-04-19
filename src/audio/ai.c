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
