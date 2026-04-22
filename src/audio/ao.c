#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

#include "imp/imp_common.h"

typedef enum {
    AUDIO_SAMPLE_RATE_8000 = 8000,
    AUDIO_SAMPLE_RATE_16000 = 16000,
    AUDIO_SAMPLE_RATE_24000 = 24000,
    AUDIO_SAMPLE_RATE_44100 = 44100,
    AUDIO_SAMPLE_RATE_48000 = 48000
} IMPAudioSampleRate;

typedef enum {
    AUDIO_BIT_WIDTH_16 = 16
} IMPAudioBitWidth;

typedef enum {
    AUDIO_SOUND_MODE_MONO = 1,
    AUDIO_SOUND_MODE_STEREO = 2
} IMPAudioSoundMode;

typedef struct {
    IMPAudioSampleRate samplerate;
    IMPAudioBitWidth bitwidth;
    IMPAudioSoundMode soundmode;
    int frmNum;
    int numPerFrm;
    int chnCnt;
} IMPAudioIOAttr;

typedef struct {
    IMPAudioBitWidth bitwidth;
    IMPAudioSoundMode soundmode;
    uint32_t *virAddr;
    uint32_t phyAddr;
    int64_t timeStamp;
    int seq;
    int len;
} IMPAudioFrame;

typedef struct {
    int32_t dev_id;
    int32_t running;
} AoDevInfo;

int32_t IMP_Log_Get_Option(); /* forward decl, ported by T<N> later */
int32_t imp_log_fun(int32_t level, int32_t option, int32_t type, ...); /* forward decl, ported by T<N> later */
void _setLeftPart32(uint32_t leftpart); /* forward decl, ported by T<N> later */
uint32_t _setRightPart32(uint32_t rightpart); /* forward decl, ported by T<N> later */
uint32_t _getLeftPart32(uint32_t value); /* forward decl, ported by T<N> later */
uint32_t _getRightPart32(uint32_t value); /* forward decl, ported by T<N> later */

uint32_t _audio_set_volume(int16_t *arg1, uint8_t *arg2, int32_t arg3,
    int32_t arg4, double arg5, double arg6, double arg7);
int32_t init_audioProcess_library(char *arg1);
int32_t deinit_audioProcess_library(void);
int32_t Hpf_Version(char *arg1, char arg2);
int32_t Hpf_gen_filter_coefficients(int16_t *arg1, uint32_t arg2, uint32_t arg3,
    int32_t arg4, int32_t arg5, double arg6, double arg7);
int32_t get_fun_address(void *arg1, void **arg2, int32_t arg3);
void *load_audioProcess_library(char **arg1, int32_t arg2, char *arg3, char *arg4);
int32_t free_audioProcess_library(void *arg1);
int32_t *audio_buf_alloc(int32_t arg1, int32_t arg2, int32_t arg3);
int32_t audio_buf_free(int32_t *arg1);
int32_t audio_buf_get_node(int32_t *arg1, int32_t arg2);
int32_t audio_buf_put_node(int32_t *arg1, int32_t arg2, int32_t arg3);
int32_t audio_buf_clear(int32_t *arg1);
int32_t audio_buf_node_get_info(void *arg1);
int32_t audio_buf_node_get_data(void *arg1);
int32_t func_init(void); /* forward decl, ported by T<N> later */
int32_t dsys_func_share_mem_register(int32_t arg1, int32_t arg2, const char *arg3, void *arg4);
int usleep(unsigned int);

int32_t IMP_AO_IMPDBG_Init(void);
static int32_t impdbg_ao_dev_info(char *arg1);
static void *impdbg_ao_get_frm(void *arg1);
static void *dbg_ao_get_frm(void *arg1, int32_t arg2, int32_t arg3);
static void * _ao_play_thread(void *arg1);
static void * _ao_play_mute_thread(void *arg1);
static void * _ao_play_unmute_thread(void *arg1);
static int32_t __ao_dev_set_gain(int32_t arg1, int32_t arg2, int32_t *arg3);
static int32_t __ao_dev_init(int32_t arg1);
static int32_t __ao_dev_deinit(int32_t arg1);
static int32_t _ao_chn_enable(int32_t arg1, int32_t arg2);
static int32_t _ao_chn_disable(int32_t arg1, int32_t arg2);
static int32_t _ao_get_buf_size(void);
static int32_t _ao_InitializeFilter(void *arg1, int32_t arg2, int32_t arg3);
static int32_t _ao_HPF_Filter(int32_t arg1, int32_t arg2, int32_t arg3);
static int32_t _ao_Agc_Process(int32_t arg1, int32_t arg2, int16_t arg3,
    int32_t arg4, int32_t arg5, int32_t arg6, int16_t arg7, int32_t arg8, int32_t arg9);
static int32_t _ao_get_emptybuf(int32_t arg1, int32_t arg2, int32_t arg3);

static AoDevInfo aoDev;
static pthread_mutex_t mutex_dev = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond_dev = PTHREAD_COND_INITIALIZER;
static pthread_cond_t cond_ao_send = PTHREAD_COND_INITIALIZER;
static char data_100544;
static char _gp;

static int32_t data_11b7ac;
static int32_t data_11b7b0 = -1;
static pthread_t data_11b7b4;
static int32_t data_11b7b8;
static int32_t data_11b7bc;
static int32_t data_11b7c0;
static int32_t data_11b7c4;
static int32_t data_11b7c8;
static int32_t data_11b7cc;
static int32_t data_11b7d0;
static int32_t data_11b7d4;
static struct {
    char pad0[0x18];
    double linear;
    char pad1[0x60];
    int32_t volume;
} data_11b7d8;
static int32_t data_11b7dc;
static int32_t data_11b7e0;
static int32_t data_11b7e8 = -30;
static int32_t data_11b7ec = 120;
static double data_11b7f0 = 1.0;
static int32_t data_11b7f8;
static int32_t data_11b828;
static int32_t data_11b82c;
static uint32_t data_11b830;
static pthread_mutex_t data_11b834;
static int32_t data_11b84c;
static int32_t data_11b850;
static int32_t *data_11b854;
static int32_t data_11b858 = 60;
static int32_t data_11b868;
static int32_t data_11b86c;
static int32_t data_11b870;
static pthread_mutex_t data_11b874;
static pthread_cond_t data_11b890;
static int32_t data_11b8c0;
static int32_t data_11b8c4;
static int32_t data_11b8c8;
static pthread_mutex_t data_11b8cc;
static pthread_mutex_t data_11b8e4;
static int32_t data_11b8fc;
static int16_t *data_11b900;
static int32_t data_11b904;
static uint8_t *data_11b908;
static void *ao_source_file;
static void *ao_target_file;
static uint32_t ao_frm_num;
static uint32_t handle_ao_agc;
static void *handle_ao_hpf;
static int16_t AoCoefficients[6];
static const int16_t kFilterCoefficients[6] = {0x0e51, (int16_t)0xe35e, 0x0e51, 0x1c75, (int16_t)0xf330, 0};
static const int16_t kFilterCoefficients8kHz[6] = {0x0ed6, (int16_t)0xe254, 0x0ed6, 0x1e7f, (int16_t)0xf16b, 0};

static void *ao_audio_process_handle;
static uint32_t (*fun_ao_agc_create)(void);
static void (*fun_ao_agc_free)(uint32_t);
static int32_t (*fun_ao_agc_set_config)(uint32_t, int32_t, int32_t, int32_t, int32_t, int16_t, int32_t);
static int32_t (*fun_ao_agc_get_config)(void);
static int32_t (*fun_ao_agc_process)(int32_t, int32_t *, int32_t, int32_t, int32_t *, int32_t, int32_t, int32_t, int32_t);
static int32_t (*fun_ao_hpf_create)(void *, void *, int32_t, int32_t, int32_t, int32_t);
static int32_t (*fun_ao_hpf_process)(int32_t, int32_t, int32_t);
static int32_t (*fun_ao_hpf_free)(void *);
static int32_t agc_sample_o;

static int32_t ensure_ao_audio_process_handle(void)
{
    char *default_lib_path[2];

    if (ao_audio_process_handle != NULL) {
        return 0;
    }

    default_lib_path[0] = "/usr/lib";
    default_lib_path[1] = "/lib";
    ao_audio_process_handle = load_audioProcess_library(default_lib_path, 2,
        "libaudioProcess.so", getenv("LD_LIBRARY_PATH"));
    return ao_audio_process_handle == NULL ? -1 : 0;
}

static int32_t impdbg_ao_dev_info(char *arg1)
{
    int32_t v0_1;

    if (arg1 == NULL) {
        return -1;
    }

    v0_1 = sprintf(arg1 + 0x18,
        "\tDEV(ID:%d)\nAudioOutAttr:\n\tSamplerate:%5d\n\tbitwidth  :%5d\n\tsoundmode :%5d\n\tfrmNum    :%5d\n\tnumPerFrm :%5d\n\tchnCnt    :%5d\n",
        0, data_11b7b8, data_11b7bc, data_11b7c0, data_11b7c4, data_11b7c8, data_11b7cc);
    *(int32_t *)(arg1 + 0x14) = v0_1 + sprintf(arg1 + 0x18 + v0_1,
        "ChannalAttr:\n\tminVol:%5d\n\tmaxVol:%5d\n\tsetVol:%5d\n\tGain  :%5d\n",
        data_11b7e8, data_11b7ec, data_11b858, data_11b868);
    *(int32_t *)(arg1 + 0xc) = 0;
    *(int32_t *)(arg1 + 0x10) = 0;
    *(int32_t *)(arg1 + 8) |= 0x100;
    return 0;
}

static void *impdbg_ao_get_frm(void *arg1)
{
    uint32_t i;

    if (arg1 == NULL) {
        return (void *)(intptr_t)-1;
    }

    remove("ao_source_record.pcm");
    remove("ao_target_record.pcm");
    ao_source_file = fopen("ao_source_record.pcm", "wb");
    if (ao_source_file == NULL) {
        i = 0xffffffff;
        puts("open source file fail");
        return (void *)(intptr_t)i;
    }

    ao_target_file = fopen("ao_target_record.pcm", "wb");
    if (ao_target_file == NULL) {
        i = 0xffffffff;
        puts("open target file fail");
        return (void *)(intptr_t)i;
    }

    ao_frm_num = *(uint32_t *)((char *)arg1 + 0x10);
    if ((int32_t)ao_frm_num <= 0) {
        i = 0xffffffff;
        printf("input frm(%d) err,clear file!!!\n", ao_frm_num);
        fclose((FILE *)ao_source_file);
        fclose((FILE *)ao_target_file);
        ao_source_file = NULL;
        ao_target_file = NULL;
        ao_frm_num = 0;
        remove("ao_source_record.pcm");
        remove("ao_target_record.pcm");
        return (void *)(intptr_t)i;
    }

    do {
        i = ao_frm_num;
    } while (i != 0);

    fclose((FILE *)ao_source_file);
    fclose((FILE *)ao_target_file);
    ao_source_file = NULL;
    ao_target_file = NULL;
    return (void *)(intptr_t)i;
}

static void *dbg_ao_get_frm(void *arg1, int32_t arg2, int32_t arg3)
{
    FILE *stream = (FILE *)ao_source_file;

    if (stream != NULL) {
        FILE *stream_1 = (FILE *)ao_target_file;

        if (stream_1 != NULL) {
            if (arg2 == 0) {
                return (void *)(intptr_t)puts("cannot write 0 byte to fail");
            }
            if (arg3 == 0) {
                return (void *)(intptr_t)fwrite(arg1, 1, (size_t)arg2, stream);
            }
            return (void *)(intptr_t)fwrite(arg1, 1, (size_t)arg2, stream_1);
        }
    }

    return (void *)(intptr_t)puts("collect file no exit");
}

static int32_t __ao_dev_set_gain(int32_t arg1, int32_t arg2, int32_t *arg3)
{
    int32_t result;

    (void)arg2;
    (void)arg3;

    result = ioctl((&data_11b7b0)[arg1 * 0x5a], 0x4004505a);
    if (result != 0) {
        printf("err: %s\n", strerror(*__errno_location()));
    }
    return result;
}

static void * _ao_play_thread(void *arg1)
{
    char var_88[0x58];
    AoDevInfo *dev = (AoDevInfo *)arg1;
    const char *var_2c = "AI_DisableAec";
    const char *var_30 = "AI_DisableAec";
    const char *var_34 = "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c";
    const char *var_a0 = NULL;
    int32_t var_9c = 0;
    const char *var_98 = NULL;
    const char *var_94 = NULL;
    int32_t a2_2 = 0;
    int32_t a3_2 = 0;

    sprintf(var_88, "ao-%s", "_ao_play_thread");
    prctl(0xf, var_88);

    while (1) {
        int32_t v0_3 = pthread_mutex_lock(&mutex_dev);
        int32_t fp_1 = 0;

        if (v0_3 != 0) {
            a3_2 = v0_3;
            a2_2 = 0x26a;
            goto exit_err;
        }

        while (1) {
            int32_t v0_4 = dev->running;

            if (v0_4 == 0) {
                fp_1 = 0;
                break;
            }

            if (data_11b7dc == 0) {
                data_11b850 = 1;
                gettimeofday((struct timeval *)&var_9c, NULL);
                {
                    struct timespec ts;
                    ts.tv_sec = var_9c + 5;
                    ts.tv_nsec = 0;
                    pthread_cond_timedwait(&cond_dev, &mutex_dev, &ts);
                }
                continue;
            }

            if (data_11b870 == 0) {
                data_11b86c = 0;
                if (data_11b7e0 == data_11b7dc) {
                    data_11b84c = 1;
                }
                gettimeofday((struct timeval *)&var_9c, NULL);
                {
                    struct timespec ts;
                    ts.tv_sec = var_9c + 5;
                    ts.tv_nsec = 0;
                    pthread_cond_timedwait(&cond_dev, &mutex_dev, &ts);
                }
                continue;
            }

            fp_1 = audio_buf_get_node(data_11b854, 1);
            if (fp_1 == 0) {
                data_11b86c = 1;
                gettimeofday((struct timeval *)&var_9c, NULL);
                {
                    struct timespec ts;
                    ts.tv_sec = var_9c + 5;
                    ts.tv_nsec = 0;
                    pthread_cond_timedwait(&cond_dev, &mutex_dev, &ts);
                }
                continue;
            }

            if (data_11b7e0 == data_11b7dc) {
                data_11b84c = 1;
            }

            if (data_11b86c != data_11b7dc) {
                if (data_11b830 >= (uint32_t)data_11b82c) {
                    fp_1 = audio_buf_get_node(data_11b854, 1);
                    if (fp_1 == 0) {
                        data_11b86c = 1;
                        gettimeofday((struct timeval *)&var_9c, NULL);
                        {
                            struct timespec ts;
                            ts.tv_sec = var_9c + 5;
                            ts.tv_nsec = 0;
                            pthread_cond_timedwait(&cond_dev, &mutex_dev, &ts);
                        }
                        continue;
                    }
                }
                if (data_11b86c == 1) {
                    data_11b86c = 0;
                }
            }
            break;
        }

        {
            int32_t v0_28 = pthread_mutex_unlock(&mutex_dev);

            if (v0_28 != 0) {
                a3_2 = v0_28;
                a2_2 = 0x297;
                goto exit_err;
            }
        }

        if (dev->running == 0) {
            break;
        }

        if (fp_1 == 0) {
            printf("err: %s,%d aobuf == NULL\n", &var_2c[0x100c], 0x2a2);
            v0_3 = pthread_mutex_lock(&mutex_dev);
            if (v0_3 != 0) {
                a3_2 = v0_3;
                a2_2 = 0x26a;
                goto exit_err;
            }
            continue;
        }

        {
            int32_t *v0_6 = (int32_t *)(intptr_t)audio_buf_node_get_info((void *)(intptr_t)fp_1);
            int32_t i = *v0_6;
            uint8_t *s3_1 = (uint8_t *)(intptr_t)*(v0_6 + 1);

            while (i >= 4) {
                int32_t var_48 = (int32_t)(intptr_t)s3_1;
                int32_t i_1 = ioctl((&data_11b7b0)[dev->dev_id * 0x5a], 0x40085069, &var_48);

                if (i_1 < 0) {
                    printf("err: %s\n", strerror(*__errno_location()));
                    var_94 = "write aofd error\n";
                    var_98 = "_ao_play";
                    var_9c = 0x24e;
                    var_a0 = var_34;
                    imp_log_fun(6, IMP_Log_Get_Option(), 2, &var_30[0x544], var_a0,
                        0x24e, "_ao_play", "write aofd error\n");
                    break;
                }

                s3_1 += i_1;
                i -= i_1;
            }

            {
                int32_t v0_12 = pthread_mutex_lock(&mutex_dev);

                if (v0_12 != 0) {
                    a3_2 = v0_12;
                    a2_2 = 0x2aa;
                    goto exit_err;
                }
            }

            audio_buf_put_node(data_11b854, fp_1, 0);
            pthread_mutex_lock(&data_11b874);
            data_11b830 -= 1;
            data_11b82c += 1;
            if (data_11b830 == 0) {
                pthread_cond_signal(&data_11b890);
            }
            pthread_mutex_unlock(&data_11b874);
            pthread_cond_signal(&cond_ao_send);

            {
                int32_t v0_16 = pthread_mutex_unlock(&mutex_dev);

                if (v0_16 != 0) {
                    a3_2 = v0_16;
                    a2_2 = 0x2be;
                    goto exit_err;
                }
            }
        }
    }

    imp_log_fun(4, IMP_Log_Get_Option(), 2, &var_30[0x544], var_34, 0x2c2,
        &var_2c[0x100c], "ao record thread exit\n", &_gp);
    pthread_exit(NULL);

exit_err:
    printf("err: %s,%d ret= %d\n", &var_2c[0x100c], a2_2, a3_2,
        var_a0, var_9c, var_98, var_94);
    imp_log_fun(4, IMP_Log_Get_Option(), 2, &var_30[0x544], var_34, 0x2c2,
        &var_2c[0x100c], "ao record thread exit\n", &_gp);
    pthread_exit(NULL);
}

static void * _ao_play_mute_thread(void *arg1)
{
    char var_70[0x70];
    uint32_t i;

    sprintf(var_70, "ao-%s", "_ao_play_mute_thread");
    prctl(0xf, var_70);

    i = *(uint32_t *)((char *)arg1 + 0x80);
    if ((int32_t)i >= -29) {
        do {
            data_11b7f0 = pow(10.0, (double)(int32_t)i / 20.0);
            data_11b7d8.linear = data_11b7f0;
            i -= 3;
            usleep(0x249f0);
        } while ((int32_t)i >= -29);
    }

    *(double *)((char *)arg1 + 0x18) = 0.0;
    data_11b7d8.linear = 0.0;
    return NULL;
}

static void * _ao_play_unmute_thread(void *arg1)
{
    char var_80[0x80];
    uint32_t v0;
    int32_t s6_1;

    sprintf(var_80, "ao-%s", "_ao_play_unmute_thread");
    prctl(0xf, var_80);

    v0 = *(uint32_t *)((char *)arg1 + 0x80);
    if ((int32_t)v0 >= -29) {
        s6_1 = -30;
        do {
            data_11b7f0 = pow(10.0, (double)s6_1 / 20.0);
            data_11b7d8.linear = data_11b7f0;
            s6_1 += 3;
            usleep(0x249f0);
            v0 = *(uint32_t *)((char *)arg1 + 0x80);
        } while (s6_1 < (int32_t)v0);
    }

    data_11b7f0 = pow(10.0, (double)(int32_t)v0 / 20.0);
    data_11b7d8.linear = data_11b7f0;
    return NULL;
}

static int32_t __ao_dev_init(int32_t arg1)
{
    int32_t var_1c;
    int32_t var_20;
    int32_t result;

    aoDev.dev_id = arg1;
    var_1c = 0x10;
    var_20 = data_11b7b8;
    result = open("/dev/dsp", 0x80001);
    data_11b7b0 = result;
    if (result < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0xf1,
            "__ao_dev_init", "open %s error\n", "/dev/dsp");
        return -1;
    }

    if (var_20 != 0) {
        int32_t v0_4 = 0;

        if (result != 0) {
            v0_4 = ioctl(result, 0xc0045002, &var_20);
        }
        if (result == 0 || v0_4 != 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
                "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0xf8,
                "__ao_dev_init", "ao set play samplerate error!!\n");
            return -1;
        }
    }

    if (ioctl(data_11b7b0, 0xc0045006, &data_11b7c0) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0xff,
            "__ao_dev_init", "Ao Device set channel count error.\n");
    }

    if (ioctl(data_11b7b0, 0xc0045005, &var_1c) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x105,
            "__ao_dev_init", "Ao Device set SNDCTL_DSP_SETFMT error.\n");
        return -1;
    }

    if (ioctl(data_11b7b0, 0x40045066, 1) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x10a,
            "__ao_dev_init", "Ao Device set SNDCTL_EXT_ENABLE_STREAM error.\n");
        return -1;
    }

    result = pthread_create(&data_11b7b4, NULL, _ao_play_thread, &aoDev);
    if (result == 0) {
        return result;
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
        "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x10f,
        "__ao_dev_init", "start audio device thread error\n");
    return -1;
}

static int32_t __ao_dev_deinit(int32_t arg1)
{
    (void)arg1;

    data_11b7ac = 0;
    pthread_cond_signal(&cond_dev);
    pthread_join(data_11b7b4, NULL);
    if (ioctl(data_11b7b0, 0x40045067, 1) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x120,
            "__ao_dev_deinit", "Ao Device set SNDCTL_EXT_DISABLE_STREAM error.\n", &_gp);
    }
    if (data_11b7b0 > 0) {
        close(data_11b7b0);
        data_11b7b0 = -1;
    }
    return 0;
}

static int32_t _ao_chn_enable(int32_t arg1, int32_t arg2)
{
    int32_t v0_2;
    int32_t v0_3;
    int32_t v0_4;

    (void)arg1;
    (void)arg2;

    pthread_mutex_init(&data_11b834, NULL);
    pthread_mutex_init(&data_11b874, NULL);
    pthread_cond_init(&data_11b890, NULL);
    v0_2 = pthread_mutex_lock(&mutex_dev);
    if (v0_2 != 0) {
        printf("err: %s,%d ret= %d\n", "_ao_chn_enable", 0x1de, v0_2);
        return -1;
    }

    v0_3 = (int32_t)(intptr_t)audio_buf_alloc(data_11b7c4, data_11b7d4, 8);
    data_11b854 = (int32_t *)(intptr_t)v0_3;
    if (v0_3 == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x1e4,
            "_ao_chn_enable", "ao_buf alloc error\n", &_gp);
        pthread_mutex_unlock(&data_11b834);
        return -1;
    }

    data_11b828 = data_11b7c4;
    data_11b82c = data_11b7c4;
    data_11b858 = 0x3c;
    data_11b830 = 0;
    data_11b7dc = 1;
    data_11b84c = 0;
    data_11b86c = 1;
    data_11b870 = 1;
    v0_4 = pthread_mutex_unlock(&mutex_dev);
    if (v0_4 == 0) {
        return 0;
    }

    printf("err: %s,%d ret= %d\n", "_ao_chn_enable", 0x1f6, v0_4);
    return -1;
}

static int32_t _ao_chn_disable(int32_t arg1, int32_t arg2)
{
    int32_t v0;
    int32_t a2_1;
    int32_t a3_1;
    int32_t i;
    int32_t v0_4;

    v0 = pthread_mutex_lock(&mutex_dev);
    if (v0 != 0) {
        a3_1 = v0;
        a2_1 = 0x205;
        printf("err: %s,%d ret= %d\n", "_ao_chn_disable", a2_1, a3_1, &_gp);
        return -1;
    }

    (void)arg1;
    (void)arg2;
    data_11b7dc = 0;
    pthread_cond_signal(&cond_dev);
    i = 0x14;
    v0_4 = pthread_mutex_unlock(&mutex_dev);
    if (v0_4 != 0) {
        a3_1 = v0_4;
        a2_1 = 0x20c;
        printf("err: %s,%d ret= %d\n", "_ao_chn_disable", a2_1, a3_1, &_gp);
        return -1;
    }

    do {
        i -= 1;
        if (data_11b850 == 1) {
            break;
        }
        usleep(0x186a0);
    } while (i != 0);

    if (data_11b854 != NULL) {
        audio_buf_free(data_11b854);
    }
    pthread_mutex_destroy(&data_11b874);
    data_11b850 = 0;
    pthread_cond_destroy(&data_11b890);
    return 0;
}

static int32_t _ao_get_buf_size(void)
{
    return data_11b7c8 << 1;
}

static int32_t _ao_InitializeFilter(void *arg1, int32_t arg2, int32_t arg3)
{
    if (arg1 == NULL) {
        puts("hpf is NULL");
        return -1;
    }

    if (arg3 != 0) {
        Hpf_gen_filter_coefficients(AoCoefficients, (uint32_t)arg2, (uint32_t)arg3, 0, 0, 0.0, 0.0);
        *(int16_t **)((char *)arg1 + 0xc) = AoCoefficients;
    } else if (arg2 == 0x1f40) {
        *(const int16_t **)((char *)arg1 + 0xc) = kFilterCoefficients8kHz;
    } else {
        *(const int16_t **)((char *)arg1 + 0xc) = kFilterCoefficients;
    }

    fun_ao_hpf_create((char *)arg1 + 8, arg1, 0, 0, 2, 4);
    return 0;
}

static int32_t _ao_HPF_Filter(int32_t arg1, int32_t arg2, int32_t arg3)
{
    int32_t v0_1;
    int32_t i;

    v0_1 = arg3 / 0xa0;
    if (v0_1 == 0) {
        abort();
    }

    if (v0_1 > 0) {
        i = arg2;
        do {
            int32_t v0_4 = fun_ao_hpf_process(arg1, i, (((uint32_t)arg3 >> 0x1f) + arg3) / 2 / v0_1);
            i += 0xa0;
            if (v0_4 != 0) {
                printf("%s: Filter error\n", "_ao_HPF_Filter");
                return -1;
            }
        } while (i != arg2 + v0_1 * 0xa0);
    }

    return 0;
}

static int32_t _ao_Agc_Process(int32_t arg1, int32_t arg2, int16_t arg3,
    int32_t arg4, int32_t arg5, int32_t arg6, int16_t arg7, int32_t arg8, int32_t arg9)
{
    int32_t s1;
    int32_t s7;
    int32_t lo;

    s1 = (int32_t)arg3;
    s7 = s1 << 1;
    lo = arg9 / s7;
    if (s7 == 0) {
        abort();
    }

    if (lo > 0) {
        int32_t s6_1 = arg2;
        int32_t s0_1 = arg4;
        int32_t s4_1 = 0;
        int32_t var_38;
        int32_t *var_30_1 = &var_38;

        do {
            int32_t var_34 = s6_1;
            uint32_t fun_ao_agc_process_1 = (uint32_t)(uintptr_t)fun_ao_agc_process;
            var_38 = s0_1;
            s4_1 += 1;
            if (((int32_t (*)(int32_t, int32_t *, int32_t, int32_t, int32_t *, int32_t, int32_t, int32_t, int32_t))(uintptr_t)fun_ao_agc_process_1)(
                    arg1, &var_34, 1, s1, var_30_1, arg5, arg6, (int32_t)arg7, arg8) != 0) {
                printf("%s, line %d : agc proccess error\n", "_ao_Agc_Process", 0x30d);
                return -1;
            }
            s6_1 += s7;
            s0_1 += s7;
        } while (lo != s4_1);
    }

    return 0;
}

int32_t IMP_AO_EnableAgc(int32_t *arg1, int16_t arg2, int16_t arg3)
{
    int32_t s4;
    int32_t s3;
    int32_t var_74_1;
    const char *var_6c;
    const char *var_68_1;
    int32_t v0_10;
    void *var_50[5];

    imp_log_fun(4, IMP_Log_Get_Option(), 2, &data_100544,
        "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x3c9,
        "IMP_AO_EnableAgc", "AO Enable AGC\n");
    s4 = (int32_t)arg2;
    s3 = (int32_t)arg3;
    imp_log_fun(4, IMP_Log_Get_Option(), 2, &data_100544,
        "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x3d1,
        "IMP_AO_EnableAgc",
        "AO AGC ENABLE: targetLevelDbfs = %d, compressionGaindB = %d, limiterEnable = %d\n",
        s4, s3, 1);

    if (init_audioProcess_library("libaudioProcess.so") != 0 || ensure_ao_audio_process_handle() != 0) {
        v0_10 = IMP_Log_Get_Option();
        var_68_1 = "IMP_AO_EnableAgc";
        var_6c = "fun:%s,init audioProcess library failed\n";
        var_74_1 = 0x3de;
        imp_log_fun(6, v0_10, 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", var_74_1,
            "IMP_AO_EnableAgc", var_6c, var_68_1);
        return -1;
    }

    var_50[0] = "audio_process_agc_create";
    var_50[1] = &fun_ao_agc_create;
    var_50[2] = "audio_process_agc_free";
    var_50[3] = &fun_ao_agc_free;
    var_50[4] = "audio_process_agc_set_config";
    if (get_fun_address(ao_audio_process_handle, var_50, 5) != 0) {
        v0_10 = IMP_Log_Get_Option();
        var_68_1 = (const char *)(intptr_t)0x3ea;
        var_6c = "line:%d get fun address failed\n";
        var_74_1 = 0x3ea;
        imp_log_fun(6, v0_10, 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", var_74_1,
            "IMP_AO_EnableAgc", var_6c, var_68_1);
        return -1;
    }

    handle_ao_agc = fun_ao_agc_create();
    if (handle_ao_agc == 0) {
        v0_10 = IMP_Log_Get_Option();
        var_68_1 = "IMP_AO_EnableAgc";
        var_6c = "fun:%s,agc create error.\n";
        var_74_1 = 0x3f0;
        imp_log_fun(6, v0_10, 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", var_74_1,
            "IMP_AO_EnableAgc", var_6c, var_68_1);
        return -1;
    }

    if (*arg1 == 0x1f40) {
        agc_sample_o = 0x50;
    } else if (*arg1 == 0x3e80 || *arg1 == 0x7d00 || *arg1 == 0xbb80) {
        agc_sample_o = 0xa0;
    } else {
        v0_10 = IMP_Log_Get_Option();
        var_68_1 = (const char *)(intptr_t)(*arg1);
        var_6c = "WebRtcAgc do not supoort samplerate: %d\n";
        var_74_1 = 0x3f8;
        imp_log_fun(6, v0_10, 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", var_74_1,
            "IMP_AO_EnableAgc", var_6c, var_68_1);
        return -1;
    }

    {
        int32_t var_70 = 1;

        if (fun_ao_agc_set_config(handle_ao_agc, 0, 0xff, 2, *arg1, (int16_t)s4, var_70) == 0) {
            int32_t result = data_11b7ac;

            if (result == 0) {
                imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
                    "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x31e,
                    "_set_aoAgcEnableFlag", "[%s] [%d] Ao device is no enabled.\n",
                    "_set_aoAgcEnableFlag", 0x31e);
                return result;
            }
            data_11b8c4 = 1;
            return 0;
        }
    }

    v0_10 = IMP_Log_Get_Option();
    var_68_1 = "IMP_AO_EnableAgc";
    var_6c = "fun:%s agc set config error\n";
    var_74_1 = 0x3fe;
    imp_log_fun(6, v0_10, 2, &data_100544,
        "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", var_74_1,
        "IMP_AO_EnableAgc", var_6c, var_68_1);
    return -1;
}

int32_t IMP_AO_DisableAgc(void)
{
    uint32_t a0 = handle_ao_agc;

    imp_log_fun(4, IMP_Log_Get_Option(), 2, &data_100544,
        "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x40d,
        "IMP_AO_DisableAgc", "AO AGC DISABLE\n");
    if (a0 == 0) {
        imp_log_fun(5, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x410,
            "IMP_AO_DisableAgc", "AO AGC NOT ENABLED\n");
        return 0;
    }

    if (data_11b7ac == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x337,
            "_reset_aoAgcEnableFlag", "[%s] [%d] Ao device is no enabled.\n",
            "_reset_aoAgcEnableFlag", 0x337, &_gp);
        a0 = handle_ao_agc;
    }

    data_11b8c4 = 0;
    fun_ao_agc_free(a0);
    handle_ao_agc = 0;
    fun_ao_agc_create = NULL;
    fun_ao_agc_free = NULL;
    fun_ao_agc_set_config = NULL;
    fun_ao_agc_get_config = NULL;
    fun_ao_agc_process = NULL;
    return 0;
}

int32_t IMP_AO_EnableHpf(int32_t *arg1)
{
    int32_t s5 = data_11b8c0;
    const char *var_4c;
    void *var_48 = NULL;
    int32_t v0_11;
    int32_t v1;
    void *fun_names[3];

    imp_log_fun(4, IMP_Log_Get_Option(), 2, &data_100544,
        "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x424,
        "IMP_AO_EnableHpf", "AO HPF ENABLE\n");
    if (s5 < 0 || (uint32_t)(*arg1) < (uint32_t)s5) {
        v0_11 = IMP_Log_Get_Option();
        var_4c = "HPF cut-off frequency is illegal.\n";
        v1 = 0x428;
        imp_log_fun(6, v0_11, 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1,
            "IMP_AO_EnableHpf", var_4c, var_48);
        return -1;
    }

    if (init_audioProcess_library("libaudioProcess.so") != 0 || ensure_ao_audio_process_handle() != 0) {
        v0_11 = IMP_Log_Get_Option();
        var_4c = "fun:%s,init audioProcess library failed\n";
        v1 = 0x42e;
        imp_log_fun(6, v0_11, 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1,
            "IMP_AO_EnableHpf", var_4c, "IMP_AO_EnableHpf");
        return -1;
    }

    fun_names[0] = "audio_process_hpf_create";
    fun_names[1] = &fun_ao_hpf_create;
    fun_names[2] = "audio_process_hpf_process";
    if (get_fun_address(ao_audio_process_handle, fun_names, 3) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x438,
            "IMP_AO_EnableHpf", "line:%d get fun address failed\n", 0x438);
        return -1;
    }

    var_48 = malloc(0x20);
    if (var_48 != NULL) {
        Hpf_Version((char *)var_48, 0x20);
        imp_log_fun(4, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x43f,
            "IMP_AO_EnableHpf", "HPF version is: %s\n", var_48);
        free(var_48);
    }

    if (_ao_InitializeFilter(handle_ao_hpf, *arg1, s5) == 0) {
        int32_t result = data_11b7ac;

        if (result == 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
                "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x351,
                "_set_aoHpfEnableFlag", "[%s] [%d] Ao device is no enabled.\n",
                "_set_aoHpfEnableFlag", 0x351, &_gp);
            return result;
        }
        data_11b8c8 = 1;
        return 0;
    }

    v0_11 = IMP_Log_Get_Option();
    var_4c = "HPF InitializeFilter error.\n";
    v1 = 0x444;
    imp_log_fun(6, v0_11, 2, &data_100544,
        "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1,
        "IMP_AO_EnableHpf", var_4c, var_48);
    return -1;
}

int32_t IMP_AO_SetHpfCoFrequency(int32_t arg1)
{
    if (data_11b7ac == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x36a,
            "_set_aoHpfCutoffFrequency", "[%s] [%d] Ao device is no enabled.\n",
            "_set_aoHpfCutoffFrequency", 0x36a, &_gp);
        return 0;
    }

    data_11b8c0 = arg1;
    return 0;
}

int32_t IMP_AO_DisableHpf(void)
{
    imp_log_fun(4, IMP_Log_Get_Option(), 2, &data_100544,
        "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x45e,
        "IMP_AO_DisableHpf", "AO HPF DISABLE\n");
    if (data_11b7ac == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x389,
            "_reset_aoHpfEnableFlag", "[%s] [%d] Ao device is no enabled.\n",
            "_reset_aoHpfEnableFlag", 0x389, &_gp);
    }
    data_11b8c8 = 0;
    fun_ao_hpf_create = NULL;
    fun_ao_hpf_process = NULL;
    fun_ao_hpf_free = NULL;
    return 0;
}

int32_t IMP_AO_SetPubAttr(int32_t arg1, int32_t *arg2)
{
    int32_t a0 = arg2[4];
    int32_t a2_1 = *arg2;
    uint32_t lo_1;

    if (arg1 != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x46d,
            "IMP_AO_SetPubAttr", "Invalid ao device ID.\n", &_gp);
        return -1;
    }

    lo_1 = (uint32_t)(a0 * 0x3e8) / (uint32_t)a2_1;
    if (a2_1 == 0) {
        abort();
    }
    if (lo_1 != (lo_1 / 0xa) * 0xa) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x474,
            "IMP_AO_SetPubAttr",
            "ao  samples(numPerFrm) must be an integer number of 10ms (10ms * n).\n",
            &_gp);
        return -1;
    }

    data_11b7d0 = a0;
    if ((uint32_t)a0 < (((uint32_t)(((uint64_t)(uint32_t)(*arg2) * 0x51eb851fULL) >> 32) >> 5) << 2)) {
        arg2[4] = (((uint32_t)(((uint64_t)(uint32_t)(*arg2) * 0x51eb851fULL) >> 32) >> 5) << 2);
    }

    _setLeftPart32((uint32_t)*arg2);
    _setLeftPart32((uint32_t)arg2[1]);
    _setLeftPart32((uint32_t)arg2[2]);
    _setLeftPart32((uint32_t)arg2[3]);
    _setLeftPart32((uint32_t)arg2[4]);
    _setLeftPart32((uint32_t)arg2[5]);
    data_11b7b8 = (int32_t)_setRightPart32((uint32_t)*arg2);
    data_11b7cc = (int32_t)_setRightPart32((uint32_t)arg2[5]);
    data_11b7bc = (int32_t)_setRightPart32((uint32_t)arg2[1]);
    data_11b7c0 = (int32_t)_setRightPart32((uint32_t)arg2[2]);
    data_11b7c4 = (int32_t)_setRightPart32((uint32_t)arg2[3]);
    data_11b7c8 = (int32_t)_setRightPart32((uint32_t)arg2[4]);
    data_11b7d4 = arg2[4] << 1;
    return 0;
}

int32_t IMP_AO_GetPubAttr(int32_t arg1, int32_t *arg2)
{
    if (arg1 != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x486,
            "IMP_AO_GetPubAttr", "Invalid ao device ID.\n", &_gp);
        return -1;
    }

    *arg2 = data_11b7b8;
    arg2[1] = data_11b7bc;
    arg2[2] = data_11b7c0;
    arg2[3] = data_11b7c4;
    arg2[4] = data_11b7c8;
    arg2[5] = data_11b7cc;
    arg2[4] = data_11b7d0;
    return 0;
}

int32_t IMP_AO_Enable(int32_t arg1)
{
    int32_t a0;
    int32_t result;

    imp_log_fun(4, IMP_Log_Get_Option(), 2, &data_100544,
        "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x494,
        "IMP_AO_Enable", "AO Enable: %d\n", arg1);
    if (arg1 != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x496,
            "IMP_AO_Enable", "Invalid audio device ID.\n");
        return -1;
    }

    a0 = data_11b7ac;
    if (a0 == 1) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x49a,
            "IMP_AO_Enable", "Audio Device is already enabled.\n");
        return 0;
    }

    data_11b7ac = 1;
    aoDev.running = 1;
    result = __ao_dev_init(0);
    if (result == 0) {
        return 0;
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
        "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x4a2,
        "IMP_AO_Enable", "__ao_dev_init failed.\n");
    return result;
}

int32_t IMP_AO_Disable(int32_t arg1)
{
    int32_t result;
    const char *var_2c;
    int32_t v0_3;
    int32_t v1;

    imp_log_fun(4, IMP_Log_Get_Option(), 2, &data_100544,
        "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x4ab,
        "IMP_AO_Disable", "AO Disable: %d\n", arg1);
    if (arg1 != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x4ad,
            "IMP_AO_Disable", "Invalid ao device ID.\n");
        return -1;
    }

    result = data_11b7ac;
    if (result == 0) {
        v0_3 = IMP_Log_Get_Option();
        var_2c = "ao Device is already Disabled.\n";
        v1 = 0x4b2;
        imp_log_fun(6, v0_3, 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1,
            "IMP_AO_Disable", var_2c);
        return result;
    }

    result = __ao_dev_deinit(0);
    if (result == 0) {
        if (aoDev.dev_id == 0) {
            deinit_audioProcess_library();
            if (ao_audio_process_handle != NULL) {
                free_audioProcess_library(ao_audio_process_handle);
                ao_audio_process_handle = NULL;
            }
            aoDev.running = 0;
        }
        aoDev.running = 0;
        return 0;
    }

    v0_3 = IMP_Log_Get_Option();
    var_2c = "__ao_dev_deinit failed.\n";
    v1 = 0x4b7;
    imp_log_fun(6, v0_3, 2, &data_100544,
        "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1,
        "IMP_AO_Disable", var_2c);
    return result;
}

int32_t IMP_AO_EnableChn(int32_t arg1, int32_t arg2)
{
    const char *var_34_1;
    int32_t v0_10;
    int32_t v1_1;
    int32_t s5_2;
    int32_t nitems;
    void *v0_1;

    imp_log_fun(4, IMP_Log_Get_Option(), 2, &data_100544,
        "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x4c8,
        "IMP_AO_EnableChn", "AO Ch Enable: %d:%d\n", arg1, arg2, &_gp);
    if (arg1 != 0) {
        v0_10 = IMP_Log_Get_Option();
        var_34_1 = "Invalid ao device ID.\n";
        v1_1 = 0x4ca;
        imp_log_fun(6, v0_10, 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_1,
            "IMP_AO_EnableChn", var_34_1);
        return -1;
    }

    if (data_11b7ac == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x4cf,
            "IMP_AO_EnableChn", "[%s] [%d] Ao device is no enabled.\n",
            "IMP_AO_EnableChn", 0x4cf);
        return data_11b7ac;
    }

    if (arg2 != 0) {
        v0_10 = IMP_Log_Get_Option();
        var_34_1 = "Invalid ao channel.\n";
        v1_1 = 0x4d4;
        imp_log_fun(6, v0_10, 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_1,
            "IMP_AO_EnableChn", var_34_1);
        return -1;
    }

    nitems = data_11b7c8 << 1;
    data_11b8fc = nitems;
    if (nitems <= 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x3ac,
            "_ao_alloc_buf", "fun:%s,parameter size is smaller than zero.\n",
            "_ao_alloc_buf");
        data_11b900 = NULL;
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x4dd,
            "IMP_AO_EnableChn", "line:%d,ao play alloc buf failed.\n", 0x4dd);
        return -1;
    }

    v0_1 = calloc((size_t)nitems, 1);
    if (v0_1 == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x3b1,
            "_ao_alloc_buf", "fun:%s,malloc buf failed\n", "_ao_alloc_buf");
        data_11b900 = NULL;
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x4dd,
            "IMP_AO_EnableChn", "line:%d,ao play alloc buf failed.\n", 0x4dd);
        return -1;
    }

    data_11b900 = (int16_t *)v0_1;
    data_11b908 = (uint8_t *)v0_1;
    data_11b904 = nitems;
    data_11b8c0 = 0;
    s5_2 = pthread_mutex_init(&data_11b8cc, NULL);
    if (pthread_mutex_init(&data_11b8e4, NULL) != 0 || s5_2 != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x4e6,
            "IMP_AO_EnableChn", "fun:%s\n,init audioProcess mutex failded\n",
            "IMP_AO_EnableChn");
        return -1;
    }

    if (IMP_AO_IMPDBG_Init() != 0) {
        puts("ao impdbg init fail!!!");
    }
    if (_ao_chn_enable(0, 0) == 0) {
        pthread_cond_signal(&cond_dev);
        imp_log_fun(4, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x4f4,
            "IMP_AO_EnableChn", "EXIT AO Ch Enable: %d:%d\n", 0, 0);
        return s5_2;
    }

    v0_10 = IMP_Log_Get_Option();
    var_34_1 = "_ao_chn_enable failed\n";
    v1_1 = 0x4ef;
    imp_log_fun(6, v0_10, 2, &data_100544,
        "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_1,
        "IMP_AO_EnableChn", var_34_1);
    return -1;
}

int32_t IMP_AO_DisableChn(int32_t arg1, int32_t arg2)
{
    const char *var_2c;
    int32_t v0_7;
    int32_t v1;
    int32_t result;

    imp_log_fun(4, IMP_Log_Get_Option(), 2, &data_100544,
        "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x4fb,
        "IMP_AO_DisableChn", "AO Ch Disable: %d:%d\n", arg1, arg2, &_gp);
    if (arg1 != 0) {
        v0_7 = IMP_Log_Get_Option();
        var_2c = "Invalid ao device ID.\n";
        v1 = 0x4fd;
        imp_log_fun(6, v0_7, 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1,
            "IMP_AO_DisableChn", var_2c);
        return -1;
    }

    if (data_11b7ac == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x502,
            "IMP_AO_DisableChn", "[%s] [%d] Ao device is no enabled.\n",
            "IMP_AO_DisableChn", 0x502);
        return -1;
    }

    if (arg2 != 0) {
        v0_7 = IMP_Log_Get_Option();
        var_2c = "Invalid ao channel.\n";
        v1 = 0x507;
        imp_log_fun(6, v0_7, 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1,
            "IMP_AO_DisableChn", var_2c);
        return -1;
    }

    if (_ao_chn_disable(0, 0) != 0) {
        v0_7 = IMP_Log_Get_Option();
        var_2c = "_ao_chn_disable failed\n";
        v1 = 0x50d;
        imp_log_fun(6, v0_7, 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1,
            "IMP_AO_DisableChn", var_2c);
        return -1;
    }

    result = pthread_mutex_destroy(&data_11b8cc);
    if (pthread_mutex_destroy(&data_11b8e4) == 0 && result == 0) {
        if (data_11b900 != NULL) {
            free(data_11b900);
        }
        data_11b900 = NULL;
        imp_log_fun(4, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x51a,
            "IMP_AO_DisableChn", "EXIT AO Ch Disable: %d:%d\n", 0, 0);
        return result;
    }

    v0_7 = IMP_Log_Get_Option();
    var_2c = "fun:%s,ao deinit audioProcess mutex failded\n";
    v1 = 0x514;
    imp_log_fun(6, v0_7, 2, &data_100544,
        "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1,
        "IMP_AO_DisableChn", var_2c);
    return -1;
}

static int32_t _ao_get_emptybuf(int32_t arg1, int32_t arg2, int32_t arg3)
{
    const char *var_48 = NULL;
    int32_t var_44 = 0;
    const char *var_40 = NULL;
    const char *var_3c = NULL;
    int32_t v0_2 = 0;

    while (1) {
        int32_t v0_3 = pthread_mutex_lock(&mutex_dev);
        int32_t v0_2;

        if (v0_3 != 0) {
            printf("err: %s,%d ret= %d\n", "_ao_get_emptybuf", 0x528, v0_3,
                var_48, var_44, var_40, var_3c);
            return 0;
        }

        {
            int32_t result = audio_buf_get_node(*(&data_11b854 + arg2 * 0x138 + arg1 * 0x168), 0);

            if (arg3 != 0 || result != 0) {
                int32_t v0_11 = pthread_mutex_unlock(&mutex_dev);

                if (v0_11 == 0) {
                    return result;
                }
                printf("err: %s,%d ret= %d\n", "_ao_get_emptybuf", 0x52f, v0_11,
                    var_48, var_44, var_40, var_3c, &_gp);
                return 0;
            }
        }

        {
            struct timeval tv;
            struct timespec ts;

            gettimeofday(&tv, NULL);
            ts.tv_sec = tv.tv_sec + 5;
            ts.tv_nsec = tv.tv_usec * 1000;
            if (pthread_cond_timedwait(&cond_ao_send, &mutex_dev, &ts) != 0x91) {
                v0_2 = pthread_mutex_unlock(&mutex_dev);
                if (v0_2 == 0) {
                    continue;
                }
                break;
            }

            var_3c = "Audio Out Send Wait timeout\n";
            var_40 = "_ao_send_wait";
            var_44 = 0x196;
            var_48 = "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c";
            imp_log_fun(5, IMP_Log_Get_Option(), 2, &data_100544,
                "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x196,
                "_ao_send_wait", "Audio Out Send Wait timeout\n");
            v0_2 = pthread_mutex_unlock(&mutex_dev);
            if (v0_2 == 0) {
                continue;
            }
            break;
        }
    }

    printf("err: %s,%d ret= %d\n", "_ao_get_emptybuf", 0x537, v0_2,
        var_48, var_44, var_40, var_3c);
    return 0;
}

int32_t IMP_AO_PauseChn(int32_t arg1, int32_t arg2)
{
    const char *var_2c_1;
    int32_t v0_5;
    int32_t v1_1;

    if (arg1 != 0) {
        v0_5 = IMP_Log_Get_Option();
        var_2c_1 = "Invalid ao device ID.\n";
        v1_1 = 0x5c1;
        imp_log_fun(6, v0_5, 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_1,
            "IMP_AO_PauseChn", var_2c_1);
        return -1;
    }
    if (data_11b7ac == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x5c6,
            "IMP_AO_PauseChn", "[%s] [%d] Ao device is no enabled.\n",
            "IMP_AO_PauseChn", 0x5c6, &_gp);
        return -1;
    }
    if (arg2 == 0) {
        int32_t v0_1 = pthread_mutex_lock(&mutex_dev);
        int32_t a2_1;
        int32_t a3_1;
        int32_t s0_1;

        if (v0_1 != 0) {
            a3_1 = v0_1;
            a2_1 = 0x5d1;
            printf("err: %s,%d ret= %d\n", "IMP_AO_PauseChn", a2_1, a3_1);
            return -1;
        }
        data_11b7e0 = 1;
        pthread_cond_signal(&cond_dev);
        s0_1 = 0x1e;
        {
            int32_t v0_2 = pthread_mutex_unlock(&mutex_dev);
            if (v0_2 != 0) {
                a3_1 = v0_2;
                a2_1 = 0x5d9;
                printf("err: %s,%d ret= %d\n", "IMP_AO_PauseChn", a2_1, a3_1);
                return -1;
            }
        }

        while (1) {
            s0_1 -= 1;
            if (data_11b84c == 1) {
                data_11b84c = 0;
                break;
            }
            usleep(0x186a0);
            if (s0_1 == 0) {
                puts("warn: wait pause_complete too long time");
                data_11b84c = 0;
                break;
            }
        }
        return 0;
    }

    v0_5 = IMP_Log_Get_Option();
    var_2c_1 = "Invalid ao channel.\n";
    v1_1 = 0x5cb;
    imp_log_fun(6, v0_5, 2, &data_100544,
        "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_1,
        "IMP_AO_PauseChn", var_2c_1);
    return -1;
}

int32_t IMP_AO_ResumeChn(int32_t arg1, int32_t arg2)
{
    const char *var_24_1;
    int32_t v0_4;
    int32_t v1_1;

    if (arg1 != 0) {
        v0_4 = IMP_Log_Get_Option();
        var_24_1 = "Invalid ao device ID.\n";
        v1_1 = 0x5eb;
        imp_log_fun(6, v0_4, 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_1,
            "IMP_AO_ResumeChn", var_24_1);
        return -1;
    }
    if (data_11b7ac == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x5f0,
            "IMP_AO_ResumeChn", "[%s] [%d] Ao device is no enabled.\n",
            "IMP_AO_ResumeChn", 0x5f0, &_gp);
        return -1;
    }
    if (arg2 == 0) {
        int32_t v0_1 = pthread_mutex_lock(&mutex_dev);
        if (v0_1 != 0) {
            printf("err: %s,%d ret= %d\n", "IMP_AO_ResumeChn", 0x5fb, v0_1);
            return -1;
        }
        data_11b7e0 = 0;
        pthread_cond_signal(&cond_dev);
        {
            int32_t v0_2 = pthread_mutex_unlock(&mutex_dev);
            if (v0_2 == 0) {
                return 0;
            }
            printf("err: %s,%d ret= %d\n", "IMP_AO_ResumeChn", 0x602, v0_2);
            return -1;
        }
    }

    v0_4 = IMP_Log_Get_Option();
    var_24_1 = "Invalid ao channel.\n";
    v1_1 = 0x5f5;
    imp_log_fun(6, v0_4, 2, &data_100544,
        "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_1,
        "IMP_AO_ResumeChn", var_24_1);
    return -1;
}

int32_t IMP_AO_ClearChnBuf(int32_t arg1, int32_t arg2)
{
    const char *var_24_1;
    int32_t v0_10;
    int32_t v1_1;

    if (arg1 != 0) {
        v0_10 = IMP_Log_Get_Option();
        var_24_1 = "Invalid ao device ID.\n";
        v1_1 = 0x60e;
        imp_log_fun(6, v0_10, 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_1,
            "IMP_AO_ClearChnBuf", var_24_1);
        return -1;
    }
    if (data_11b7ac == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x613,
            "IMP_AO_ClearChnBuf", "[%s] [%d] Ao device is no enabled.\n",
            "IMP_AO_ClearChnBuf", 0x613, &_gp);
        return -1;
    }
    if (arg2 == 0) {
        int32_t v0_1 = pthread_mutex_lock(&mutex_dev);

        if (v0_1 != 0) {
            printf("err: %s,%d ret= %d\n", "IMP_AO_ClearChnBuf", 0x61f, v0_1);
            return -1;
        }
        audio_buf_clear(data_11b854);
        data_11b82c = data_11b828;
        data_11b830 = 0;
        if (ioctl(data_11b7b0, 0x4004506a, 0) != 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
                "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x130,
                "__ao_dev_clearbuf", "Ao Device set SNDCTL_EXT_FLUSH_STREAM error.\n");
            printf("err: %s\n", strerror(*__errno_location()));
            imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
                "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x629,
                "IMP_AO_ClearChnBuf", "ao dev clear buf error!!.\n");
        }
        {
            int32_t v0_4 = pthread_mutex_unlock(&mutex_dev);

            if (v0_4 == 0) {
                return 0;
            }
            printf("err: %s,%d ret= %d\n", "IMP_AO_ClearChnBuf", 0x62d, v0_4);
            return -1;
        }
    }

    v0_10 = IMP_Log_Get_Option();
    var_24_1 = "Invalid ao channel.\n";
    v1_1 = 0x618;
    imp_log_fun(6, v0_10, 2, &data_100544,
        "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_1,
        "IMP_AO_ClearChnBuf", var_24_1);
    return -1;
}

int32_t IMP_AO_FlushChnBuf(int32_t arg1, int32_t arg2)
{
    const char *var_34_1;
    int32_t v0_16;
    int32_t v1_7;

    if (arg1 != 0) {
        v0_16 = IMP_Log_Get_Option();
        var_34_1 = "Invalid ao device ID.\n";
        v1_7 = 0x63d;
        imp_log_fun(6, v0_16, 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_7,
            "IMP_AO_FlushChnBuf", var_34_1);
        return -1;
    }
    if (data_11b7ac == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x642,
            "IMP_AO_FlushChnBuf", "[%s] [%d] Ao device is no enabled.\n",
            "IMP_AO_FlushChnBuf", 0x642, &_gp);
        return -1;
    }
    if (arg2 == 0) {
        size_t n = (size_t)data_11b904;
        if (data_11b904 > 0) {
            memset(data_11b908, arg2, n);
        }
        {
            int32_t v0 = _ao_get_emptybuf(0, 0, 0);

            if (v0 != 0) {
                int32_t *v0_1 = (int32_t *)(intptr_t)audio_buf_node_get_info((void *)(intptr_t)v0);
                int32_t v0_2 = audio_buf_node_get_data((void *)(intptr_t)v0);

                *(v0_1 + 1) = v0_2;
                *v0_1 = data_11b8fc;
                _audio_set_volume(data_11b900, (uint8_t *)(intptr_t)v0_2, data_11b8fc, 0x10, 0.0, 0.0, 0.0);
                {
                    int32_t v0_3 = pthread_mutex_lock(&mutex_dev);
                    int32_t a2_3;
                    int32_t a3_1;

                    if (v0_3 != 0) {
                        a3_1 = v0_3;
                        a2_3 = 0x65b;
                        printf("err: %s,%d ret= %d\n", "IMP_AO_FlushChnBuf", a2_3, a3_1);
                        return -1;
                    }
                    data_11b86c = 0;
                    audio_buf_put_node(data_11b854, v0, 1);
                    data_11b830 += 1;
                    data_11b82c -= 1;
                    pthread_cond_signal(&cond_dev);
                    {
                        int32_t v0_6 = pthread_mutex_unlock(&mutex_dev);

                        if (v0_6 != 0) {
                            a3_1 = v0_6;
                            a2_3 = 0x665;
                            printf("err: %s,%d ret= %d\n", "IMP_AO_FlushChnBuf", a2_3, a3_1);
                            return -1;
                        }
                    }
                }

                pthread_mutex_lock(&data_11b874);
                if (data_11b830 != 0) {
                    struct timeval var_18;
                    struct timespec var_20;

                    gettimeofday(&var_18, NULL);
                    var_20.tv_sec = var_18.tv_sec + 5;
                    var_20.tv_nsec = var_18.tv_usec * 1000;
                    if (pthread_cond_timedwait(&data_11b890, &data_11b874, &var_20) == 0x91) {
                        imp_log_fun(5, IMP_Log_Get_Option(), 2, &data_100544,
                            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x1a5,
                            "_ao_flush_wait", "_ao_flush_wait timeout\n");
                    }
                }
                pthread_mutex_unlock(&data_11b874);
                if (ioctl(data_11b7b0, 0x4004506b, 0) == 0) {
                    return 0;
                }
                imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
                    "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x13f,
                    "__ao_dev_flushchnbuf", "Ao Device set SNDCTL_EXT_SYNC_STREAM error.\n");
                printf("err: %s\n", strerror(*__errno_location()));
                imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
                    "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x670,
                    "IMP_AO_FlushChnBuf", "ao dev flushchn buf error!!.\n");
                return -1;
            }
        }
        return -1;
    }

    v0_16 = IMP_Log_Get_Option();
    var_34_1 = "Invalid ao channel.\n";
    v1_7 = 0x647;
    imp_log_fun(6, v0_16, 2, &data_100544,
        "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_7,
        "IMP_AO_FlushChnBuf", var_34_1);
    return -1;
}

int32_t IMP_AO_QueryChnStat(int32_t arg1, int32_t arg2, int32_t *arg3)
{
    const char *var_24_1;
    int32_t v0_5;
    int32_t v1_2;

    if (arg1 != 0) {
        v0_5 = IMP_Log_Get_Option();
        var_24_1 = "Invalid ao device ID.\n";
        v1_2 = 0x67b;
        imp_log_fun(6, v0_5, 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_2,
            "IMP_AO_QueryChnStat", var_24_1);
        return -1;
    }
    if (data_11b7ac == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x680,
            "IMP_AO_QueryChnStat", "[%s] [%d] Ao device is no enabled.\n",
            "IMP_AO_QueryChnStat", 0x680, &_gp);
        return -1;
    }
    if (arg2 == 0) {
        int32_t v0_1 = pthread_mutex_lock(&mutex_dev);
        if (v0_1 != 0) {
            printf("err: %s,%d ret= %d\n", "IMP_AO_QueryChnStat", 0x68b, v0_1);
            return -1;
        }
        *arg3 = data_11b828;
        arg3[1] = data_11b82c;
        arg3[2] = (int32_t)_getLeftPart32(data_11b830);
        arg3[2] = (int32_t)_getRightPart32(data_11b830);
        {
            int32_t v0_3 = pthread_mutex_unlock(&mutex_dev);
            if (v0_3 == 0) {
                return 0;
            }
            printf("err: %s,%d ret= %d\n", "IMP_AO_QueryChnStat", 0x691, v0_3);
            return -1;
        }
    }

    v0_5 = IMP_Log_Get_Option();
    var_24_1 = "Invalid ao channel.\n";
    v1_2 = 0x685;
    imp_log_fun(6, v0_5, 2, &data_100544,
        "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_2,
        "IMP_AO_QueryChnStat", var_24_1);
    return -1;
}

int32_t IMP_AO_Soft_Mute(int32_t arg1, int32_t arg2, int32_t arg3)
{
    const char *var_24_1;
    int32_t v0_3;
    int32_t v1_1;
    pthread_t var_10;

    (void)arg3;

    if (arg1 != 0) {
        v0_3 = IMP_Log_Get_Option();
        var_24_1 = "Invalid audio device ID.\n";
        v1_1 = 0x69c;
        imp_log_fun(6, v0_3, 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_1,
            "IMP_AO_Soft_Mute", var_24_1);
        return -1;
    }
    if (data_11b7ac == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x6a1,
            "IMP_AO_Soft_Mute", "[%s] [%d] Ao device is no enabled.\n",
            "IMP_AO_Soft_Mute", 0x6a1, &_gp);
        return -1;
    }
    if (arg2 == 0) {
        if (pthread_create(&var_10, NULL, _ao_play_mute_thread, (char *)&data_11b7d8) != 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
                "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x6ae,
                "IMP_AO_Soft_Mute", "[ERROR] %s: pthread_create Ao Play failed\n",
                "IMP_AO_Soft_Mute");
        }
        pthread_detach(var_10);
        return 0;
    }

    (void)arg1; (void)arg2; (void)arg3;
    v0_3 = IMP_Log_Get_Option();
    var_24_1 = "Invalid ao channel.\n";
    v1_1 = 0x6a6;
    imp_log_fun(6, v0_3, 2, &data_100544,
        "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_1,
        "IMP_AO_Soft_Mute", var_24_1);
    return -1;
}

int32_t IMP_AO_Soft_UNMute(int32_t arg1, int32_t arg2, int32_t arg3)
{
    const char *var_24_1;
    int32_t v0_3;
    int32_t v1_1;
    pthread_t var_10;

    (void)arg3;

    if (arg1 != 0) {
        v0_3 = IMP_Log_Get_Option();
        var_24_1 = "Invalid ao device ID.\n";
        v1_1 = 0x6b9;
        imp_log_fun(6, v0_3, 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_1,
            "IMP_AO_Soft_UNMute", var_24_1);
        return -1;
    }
    if (data_11b7ac == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x6be,
            "IMP_AO_Soft_UNMute", "[%s] [%d] Ao device is no enabled.\n",
            "IMP_AO_Soft_UNMute", 0x6be, &_gp);
        return -1;
    }
    if (arg2 == 0) {
        if (pthread_create(&var_10, NULL, _ao_play_unmute_thread, (char *)&data_11b7d8) != 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
                "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x6cb,
                "IMP_AO_Soft_UNMute", "[ERROR] %s: pthread_create Ao Play failed\n",
                "IMP_AO_Soft_UNMute");
        }
        pthread_detach(var_10);
        return 0;
    }

    (void)arg1; (void)arg2;
    v0_3 = IMP_Log_Get_Option();
    var_24_1 = "Invalid ao channel.\n";
    v1_1 = 0x6c3;
    imp_log_fun(6, v0_3, 2, &data_100544,
        "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_1,
        "IMP_AO_Soft_UNMute", var_24_1);
    return -1;
}

int32_t IMP_AO_SetVolMute(int32_t arg1, int32_t arg2, int32_t arg3)
{
    const char *var_1c_1;
    int32_t v0_1;
    int32_t v1_1;

    if (arg1 != 0) {
        v0_1 = IMP_Log_Get_Option();
        var_1c_1 = "Invalid ao device ID.\n";
        v1_1 = 0x6d5;
        imp_log_fun(6, v0_1, 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_1,
            "IMP_AO_SetVolMute", var_1c_1);
        return -1;
    }
    if (data_11b7ac == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x6da,
            "IMP_AO_SetVolMute", "[%s] [%d] Ao device is no enabled.\n",
            "IMP_AO_SetVolMute", 0x6da, &_gp);
        return -1;
    }
    if (arg2 != 0) {
        v0_1 = IMP_Log_Get_Option();
        var_1c_1 = "Invalid ao channel.\n";
        v1_1 = 0x6df;
        imp_log_fun(6, v0_1, 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_1,
            "IMP_AO_SetVolMute", var_1c_1);
        return -1;
    }
    if (arg3 == 1) {
        data_11b7f0 = 0.0;
        data_11b7f8 = arg3;
        return 0;
    }
    if (arg3 == 0) {
        data_11b7f0 = pow(10.0, (double)data_11b858 / 20.0);
        data_11b7f8 = arg3;
        return 0;
    }

    v0_1 = IMP_Log_Get_Option();
    var_1c_1 = "Invalid AO Mute Value.\n";
    v1_1 = 0x6e8;
    imp_log_fun(6, v0_1, 2, &data_100544,
        "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_1,
        "IMP_AO_SetVolMute", var_1c_1);
    return -1;
}

int32_t IMP_AO_SetVol(int32_t arg1, int32_t arg2, uint32_t arg3)
{
    const char *var_2c_1;
    int32_t v0_7;
    int32_t v1_1;
    uint32_t s1_1;

    if (arg1 != 0) {
        v0_7 = IMP_Log_Get_Option();
        var_2c_1 = "Invalid ao device ID.\n";
        v1_1 = 0x6f4;
        imp_log_fun(6, v0_7, 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_1,
            "IMP_AO_SetVol", var_2c_1);
        return -1;
    }
    if (data_11b7ac == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x6f9,
            "IMP_AO_SetVol", "[%s] [%d] Ao device is no enabled.\n",
            "IMP_AO_SetVol", 0x6f9, &_gp);
        return -1;
    }
    if (arg2 == 0) {
        s1_1 = arg3;
        imp_log_fun(4, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x702,
            "IMP_AO_SetVol", "AO Set Vol: %d\n", s1_1);
        if ((int32_t)s1_1 < -30) {
            s1_1 = (uint32_t)-30;
        }
        if ((int32_t)s1_1 >= 0x79) {
            s1_1 = 0x78;
        }
        pthread_mutex_lock(&data_11b834);
        data_11b7e8 = -30;
        data_11b7ec = 0x78;
        pthread_mutex_unlock(&data_11b834);
        data_11b858 = (int32_t)s1_1;
        data_11b7f0 = pow(10.0, (double)(int32_t)s1_1 / 20.0);
        return 0;
    }

    v0_7 = IMP_Log_Get_Option();
    var_2c_1 = "Invalid ao channel.\n";
    v1_1 = 0x6fe;
    imp_log_fun(6, v0_7, 2, &data_100544,
        "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_1,
        "IMP_AO_SetVol", var_2c_1);
    return -1;
}

int32_t IMP_AO_GetVol(int32_t arg1, int32_t arg2, int32_t *arg3)
{
    const char *var_24_1;
    int32_t v0_4;
    int32_t v1_2;

    if (arg1 != 0) {
        v0_4 = IMP_Log_Get_Option();
        var_24_1 = "Invalid ao device ID.\n";
        v1_2 = 0x71f;
        imp_log_fun(6, v0_4, 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_2,
            "IMP_AO_GetVol", var_24_1);
        return -1;
    }
    if (data_11b7ac == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x724,
            "IMP_AO_GetVol", "[%s] [%d] Ao device is no enabled.\n",
            "IMP_AO_GetVol", 0x724, &_gp);
        return -1;
    }
    if (arg2 == 0) {
        pthread_mutex_lock(&data_11b834);
        *arg3 = data_11b858;
        pthread_mutex_unlock(&data_11b834);
        imp_log_fun(4, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x730,
            "IMP_AO_GetVol", "AO Get Vol: %d\n", *arg3);
        return 0;
    }

    v0_4 = IMP_Log_Get_Option();
    var_24_1 = "Invalid ao channel.\n";
    v1_2 = 0x729;
    imp_log_fun(6, v0_4, 2, &data_100544,
        "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_2,
        "IMP_AO_GetVol", var_24_1);
    return -1;
}

int32_t IMP_AO_SetGain(int32_t arg1, int32_t arg2, int32_t arg3)
{
    int32_t arg_8 = arg3;
    const char *var_34_1;
    int32_t v0_12;
    int32_t v1_2;
    int32_t v0_8;

    if (arg1 > 0) {
        v0_12 = IMP_Log_Get_Option();
        var_34_1 = "Invalid ao device ID.\n";
        v1_2 = 0x738;
        imp_log_fun(6, v0_12, 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_2,
            "IMP_AO_SetGain", var_34_1);
        return -1;
    }
    if (data_11b7ac == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x73d,
            "IMP_AO_SetGain", "[%s] [%d] Ao device is no enabled.\n",
            "IMP_AO_SetGain", 0x73d, &_gp);
        return -1;
    }
    if (arg2 == 0) {
        imp_log_fun(4, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x745,
            "IMP_AO_SetGain", "AO Get Gain: %d\n", arg_8);
        if (arg_8 < 0) {
            arg_8 = 0;
        }
        if (arg_8 >= 0x20) {
            arg_8 = 0x1f;
        }
        if (__ao_dev_set_gain(arg1, 0, &arg_8) != 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
                "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x74b,
                "IMP_AO_SetGain", "Set ao Play Gain Error.\n");
            v0_8 = 0;
            (void)v0_8;
        }
        data_11b868 = arg_8;
        return 0;
    }

    v0_12 = IMP_Log_Get_Option();
    var_34_1 = "Invalid ao channel.\n";
    v1_2 = 0x742;
    imp_log_fun(6, v0_12, 2, &data_100544,
        "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_2,
        "IMP_AO_SetGain", var_34_1);
    return -1;
}

int32_t IMP_AO_GetGain(int32_t arg1, int32_t arg2, int32_t *arg3)
{
    const char *var_1c_1;
    int32_t v0_3;
    int32_t v1_2;

    if (arg1 != 0) {
        v0_3 = IMP_Log_Get_Option();
        var_1c_1 = "Invalid ao device ID.\n";
        v1_2 = 0x755;
        imp_log_fun(6, v0_3, 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_2,
            "IMP_AO_GetGain", var_1c_1);
        return -1;
    }
    if (data_11b7ac == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x75a,
            "IMP_AO_GetGain", "[%s] [%d] Ao device is no enabled.\n",
            "IMP_AO_GetGain", 0x75a, &_gp);
        return -1;
    }
    if (arg2 == 0) {
        *arg3 = data_11b868;
        imp_log_fun(4, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x764,
            "IMP_AO_GetGain", "AO Get Gain: %d\n", *arg3);
        return 0;
    }

    v0_3 = IMP_Log_Get_Option();
    var_1c_1 = "Invalid ao channel.\n";
    v1_2 = 0x75f;
    imp_log_fun(6, v0_3, 2, &data_100544,
        "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_2,
        "IMP_AO_GetGain", var_1c_1);
    return -1;
}

int32_t IMP_AO_CacheSwitch(int32_t arg1, int32_t arg2, int32_t arg3)
{
    const char *var_34_1;
    int32_t v0_4;
    int32_t v1_1;
    int32_t result;

    imp_log_fun(4, IMP_Log_Get_Option(), 2, &data_100544,
        "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x76c,
        "IMP_AO_CacheSwitch", "AO Ch Enable: %d:%d\n", arg1, arg2, &_gp);
    if (arg1 != 0) {
        v0_4 = IMP_Log_Get_Option();
        var_34_1 = "Invalid ao device ID.\n";
        v1_1 = 0x76e;
        imp_log_fun(6, v0_4, 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_1,
            "IMP_AO_CacheSwitch", var_34_1);
        return -1;
    }

    result = data_11b7ac;
    if (result == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x773,
            "IMP_AO_CacheSwitch", "[%s] [%d] Ao device is no enabled.\n",
            "IMP_AO_CacheSwitch", 0x773);
        return result;
    }
    if (arg2 == 0) {
        data_11b870 = arg3;
        return 0;
    }

    v0_4 = IMP_Log_Get_Option();
    var_34_1 = "Invalid ao channel.\n";
    v1_1 = 0x778;
    imp_log_fun(6, v0_4, 2, &data_100544,
        "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_1,
        "IMP_AO_CacheSwitch", var_34_1);
    return -1;
}

int32_t IMP_AO_IMPDBG_Init(void)
{
    return func_init()
        + dsys_func_share_mem_register(5, 0, "mpdbg_ao_dev_info", impdbg_ao_dev_info)
        + dsys_func_share_mem_register(5, 1, "impdbg_ao_get_frm", impdbg_ao_get_frm);
}

int32_t IMP_AO_SendFrame(int32_t arg1, int32_t arg2, void *arg3, int32_t arg4)
{
    const char *var_3c_1;
    int32_t v0_31;
    int32_t v1_9;

    if (arg3 == NULL) {
        v0_31 = IMP_Log_Get_Option();
        var_3c_1 = "Invalid param data.\n";
        v1_9 = 0x548;
        imp_log_fun(6, v0_31, 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_9,
            "IMP_AO_SendFrame", var_3c_1);
        return -1;
    }
    if (arg1 != 0) {
        v0_31 = IMP_Log_Get_Option();
        var_3c_1 = "Invalid ao device ID.\n";
        v1_9 = 0x54c;
        imp_log_fun(6, v0_31, 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_9,
            "IMP_AO_SendFrame", var_3c_1);
        return -1;
    }
    if (arg2 != 0) {
        v0_31 = IMP_Log_Get_Option();
        var_3c_1 = "Invalid ao channel.\n";
        v1_9 = 0x551;
        imp_log_fun(6, v0_31, 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_9,
            "IMP_AO_SendFrame", var_3c_1);
        return -1;
    }
    if (data_11b7ac == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x556,
            "IMP_AO_SendFrame", "[%s] [%d] Ao device is no enabled.\n",
            "IMP_AO_SendFrame", 0x556, &_gp);
        return -1;
    }
    if (data_11b7dc == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x55b,
            "IMP_AO_SendFrame", "%s %d Ao Channel is no enabled.\n",
            "IMP_AO_SendFrame", 0x55b, &_gp);
        return -1;
    }

    {
        size_t n = (size_t)*(int32_t *)((char *)arg3 + 0x1c);

        if (*(int32_t *)((char *)arg3 + 0x1c) <= 0) {
            return 0;
        }
        if ((uint32_t)(data_11b7d0 << 1) < n) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
                "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x564,
                "IMP_AO_SendFrame", "ao frame size is too big, max size is %d\n",
                data_11b7d0 << 1);
            return -1;
        }
        if ((int32_t)n < data_11b904) {
            memcpy(data_11b908, *(void **)((char *)arg3 + 8), n);
            data_11b908 += *(int32_t *)((char *)arg3 + 0x1c);
            data_11b904 -= *(int32_t *)((char *)arg3 + 0x1c);
            return 0;
        }

        {
            int32_t v0_8 = _ao_get_emptybuf(arg1, arg2, arg4);

            if (v0_8 == 0) {
                return -1;
            }

            memcpy(data_11b908, *(void **)((char *)arg3 + 8), (size_t)data_11b904);
            {
                int32_t *v0_9 = (int32_t *)(intptr_t)audio_buf_node_get_info((void *)(intptr_t)v0_8);
                int32_t var_28 = 0;
                char var_24 = 0;
                int32_t var_40 = 0;
                int32_t var_38;
                char *var_3c;
                int32_t *var_44;
                int32_t var_48;

                *(v0_9 + 1) = audio_buf_node_get_data((void *)(intptr_t)v0_8);
                *v0_9 = data_11b8fc;
                if ((int32_t)ao_frm_num > 0) {
                    dbg_ao_get_frm(data_11b900, data_11b8fc, 0);
                }
                pthread_mutex_lock(&data_11b8e4);
                if (data_11b8c8 != 0) {
                    int32_t v0_13 = _ao_HPF_Filter((int32_t)(intptr_t)handle_ao_hpf,
                        (int32_t)(intptr_t)data_11b900, data_11b8fc);

                    if (v0_13 != 0) {
                        v0_31 = IMP_Log_Get_Option();
                        var_3c_1 = "_ao_Hpf_Filter error.\n";
                        v1_9 = 0x57f;
                        imp_log_fun(6, v0_31, 2, &data_100544,
                            "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", v1_9,
                            "IMP_AO_SendFrame", var_3c_1);
                        return -1;
                    }
                }
                pthread_mutex_unlock(&data_11b8e4);
                pthread_mutex_lock(&data_11b8cc);
                if (data_11b8c4 != 0) {
                    uint32_t a0_5 = handle_ao_agc;

                    if (a0_5 != 0) {
                        var_38 = data_11b8fc;
                        var_3c = &var_24;
                        var_44 = &var_28;
                        var_48 = 0x7f;
                        if (_ao_Agc_Process((int32_t)a0_5, (int32_t)(intptr_t)data_11b900,
                                (int16_t)agc_sample_o, (int32_t)(intptr_t)data_11b900,
                                0x7f, (int32_t)(intptr_t)var_44, 0, (int32_t)(intptr_t)var_3c, var_38) != 0) {
                            imp_log_fun(6, IMP_Log_Get_Option(), 2, &data_100544,
                                "/home/user/git/proj/sdk-lv3/src/imp/audio/ao.c", 0x58d,
                                "IMP_AO_SendFrame", "IngenicAgc_Process error.\n");
                            return -1;
                        }
                    }
                }
                pthread_mutex_unlock(&data_11b8cc);
                _audio_set_volume(data_11b900, (uint8_t *)(intptr_t)*((v0_9 + 1)), *v0_9,
                    0x10, (double)var_48, (double)(intptr_t)var_44, (double)var_40);
                if (*(int32_t *)((char *)arg3 + 0x1c) == data_11b904) {
                    data_11b908 = (uint8_t *)data_11b900;
                    data_11b904 = data_11b8fc;
                } else {
                    void *a0_7 = data_11b900;
                    data_11b908 = (uint8_t *)a0_7;
                    memcpy(a0_7, (char *)*(void **)((char *)arg3 + 8) + data_11b904,
                        (size_t)(*(int32_t *)((char *)arg3 + 0x1c) - data_11b904));
                    data_11b908 += *(int32_t *)((char *)arg3 + 0x1c) - data_11b904;
                    data_11b904 = data_11b8fc - (*(int32_t *)((char *)arg3 + 0x1c) - data_11b904);
                }
                {
                    int32_t v0_20 = pthread_mutex_lock(&mutex_dev);
                    int32_t a2_8;
                    int32_t a3_1;

                    if (v0_20 != 0) {
                        a3_1 = v0_20;
                        a2_8 = 0x5a3;
                        printf("err: %s,%d ret= %d\n", "IMP_AO_SendFrame", a2_8, a3_1);
                        return -1;
                    }
                    if ((int32_t)ao_frm_num > 0) {
                        dbg_ao_get_frm((void *)(intptr_t)*(v0_9 + 1), *v0_9, 1);
                    }
                    ao_frm_num -= 1;
                    if ((int32_t)ao_frm_num <= 0) {
                        ao_frm_num = 0;
                    }
                    audio_buf_put_node(data_11b854, v0_8, 1);
                    data_11b82c -= 1;
                    data_11b830 += 1;
                    pthread_cond_signal(&cond_dev);
                    {
                        int32_t v0_27 = pthread_mutex_unlock(&mutex_dev);
                        if (v0_27 == 0) {
                            return 0;
                        }
                        a3_1 = v0_27;
                        a2_8 = 0x5b3;
                        printf("err: %s,%d ret= %d\n", "IMP_AO_SendFrame", a2_8, a3_1);
                        return -1;
                    }
                }
            }
        }
    }
}
