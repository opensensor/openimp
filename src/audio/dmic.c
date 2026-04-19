#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <signal.h>
#include <dlfcn.h>
#include <math.h>

#include <imp/imp_audio.h>

typedef struct DmicState {
    uint8_t opaque[0x800];
} DmicState;

static DmicState g_dmic_storage;
static int g_dmic_initialized;
static void *g_dmic_audio_process_handle;
static int32_t (*fun_ai_aec_create)(int32_t, int32_t);
static int32_t (*fun_ai_aec_process)(void *, void *);
static int32_t (*fun_ai_aec_free)(int32_t);

extern char _gp; /* forward decl, ported by T<N> later */
extern DmicState *gDmic; /* forward decl, owned by T0 */

int32_t IMP_Log_Get_Option(...); /* forward decl, ported by T<N> later */
int32_t imp_log_fun(int32_t level, int32_t option, int32_t type, ...); /* forward decl, ported by T<N> later */
uint64_t IMP_System_GetTimeStamp(void); /* forward decl, ported by T<N> later */

int32_t *audio_buf_alloc(int32_t arg1, int32_t arg2, int32_t arg3);
int32_t audio_buf_free(int32_t *arg1);
int32_t audio_buf_get_node(int32_t *arg1, int32_t arg2);
int32_t audio_buf_try_get_node(int32_t *arg1, int32_t arg2);
int32_t audio_buf_put_node(int32_t *arg1, int32_t arg2, int32_t arg3);
int32_t audio_buf_node_get_data(void *arg1);
int32_t audio_buf_node_index(int32_t *arg1);
int32_t init_audioProcess_library(char *arg1);
int32_t get_fun_address(void *arg1, void **arg2, int32_t arg3);
uint32_t _audio_set_volume(int16_t *arg1, uint8_t *arg2, int32_t arg3,
    int32_t arg4, double arg5, double arg6, double arg7);

static int32_t _dmic_dev_lock(void *arg1);
static int32_t _dmic_dev_unlock(void *arg1);
static int32_t _dmic_chn_lock(void *arg1);
static int32_t _dmic_chn_unlock(void *arg1);
static int32_t _dmic_thread_post(void *arg1);
static int32_t _dmic_thread_wait(void *arg1);
static int32_t _dmic_polling_frame_wait(void *arg1);
static int32_t __dmic_dev_init(void *arg1);
static int32_t __dmic_dev_deinit(void *arg1);
static int32_t _dmic_dev_read(void *arg1, void *arg2, int16_t *arg3, int32_t arg4, int32_t arg5);
static void *_dmic_record_thread(void *arg1);
static int32_t _dmic_dev_enable_aec(void *arg1);
static int32_t _dmic_dev_disable_aec(void *arg1);
static int32_t _dmic_ref_enable(void *arg1, void *arg2);
static int32_t _dmic_ref_disable(void *arg1, void *arg2);

#define READ_I32(base, off) (*(int32_t *)((uint8_t *)(base) + (off)))
#define WRITE_I32(base, off, val) (*(int32_t *)((uint8_t *)(base) + (off)) = (val))
#define READ_PTR(base, off) (*(void **)((uint8_t *)(base) + (off)))
#define WRITE_PTR(base, off, val) (*(void **)((uint8_t *)(base) + (off)) = (void *)(val))
#define READ_DBL(base, off) (*(double *)((uint8_t *)(base) + (off)))
#define WRITE_DBL(base, off, val) (*(double *)((uint8_t *)(base) + (off)) = (val))
#define MUTEX_AT(base, off) ((pthread_mutex_t *)((uint8_t *)(base) + (off)))
#define COND_AT(base, off) ((pthread_cond_t *)((uint8_t *)(base) + (off)))

static void *dmic_base(void)
{
    if (!g_dmic_initialized) {
        memset(&g_dmic_storage, 0, sizeof(g_dmic_storage));
        gDmic = &g_dmic_storage;
        WRITE_I32(gDmic, 0x8, -1);
        g_dmic_initialized = 1;
    }

    return gDmic;
}

static int32_t _dmic_dev_lock(void *arg1)
{
    int32_t result = pthread_mutex_lock((pthread_mutex_t *)((uint8_t *)arg1 + 0x34));

    if (result != 0) {
        printf("err: %s,%d ret= %d\n", "_dmic_dev_lock", 0x39,
            pthread_mutex_lock((pthread_mutex_t *)((uint8_t *)arg1 + 0x34)), &_gp);
    }

    return result;
}

static int32_t _dmic_dev_unlock(void *arg1)
{
    int32_t result = pthread_mutex_unlock((pthread_mutex_t *)((uint8_t *)arg1 + 0x34));

    if (result != 0) {
        printf("err: %s,%d ret= %d\n", "_dmic_dev_unlock", 0x40,
            pthread_mutex_unlock((pthread_mutex_t *)((uint8_t *)arg1 + 0x34)), &_gp);
    }

    return result;
}

static int32_t _dmic_chn_lock(void *arg1)
{
    int32_t result = pthread_mutex_lock((pthread_mutex_t *)((uint8_t *)arg1 + 0x7c));

    if (result != 0) {
        printf("err: %s,%d ret= %d\n", "_dmic_chn_lock", 0x47,
            pthread_mutex_lock((pthread_mutex_t *)((uint8_t *)arg1 + 0x7c)), &_gp);
    }

    return result;
}

static int32_t _dmic_chn_unlock(void *arg1)
{
    int32_t result = pthread_mutex_unlock((pthread_mutex_t *)((uint8_t *)arg1 + 0x7c));

    if (result != 0) {
        printf("err: %s,%d ret= %d\n", "_dmic_chn_unlock", 0x4e,
            pthread_mutex_unlock((pthread_mutex_t *)((uint8_t *)arg1 + 0x7c)), &_gp);
    }

    return result;
}

static int32_t _dmic_thread_post(void *arg1)
{
    int32_t result = pthread_cond_signal((pthread_cond_t *)((uint8_t *)arg1 + 0x4c));

    if (result != 0) {
        printf("err: %s,%d ret= %d\n", "_dmic_thread_post", 0x7e,
            pthread_cond_signal((pthread_cond_t *)((uint8_t *)arg1 + 0x4c)), &_gp);
    }

    return result;
}

static int32_t _dmic_thread_wait(void *arg1)
{
    struct timeval tv;
    struct timespec ts;

    gettimeofday(&tv, 0);
    ts.tv_sec = tv.tv_sec + 5;
    ts.tv_nsec = tv.tv_usec * 1000;
    if (pthread_cond_timedwait((pthread_cond_t *)((uint8_t *)arg1 + 0x4c), (pthread_mutex_t *)((uint8_t *)arg1 + 0x34), &ts) == 0x91) {
        printf("info:(%s,%d), dmic_thread_wait timeout!!!.\n", "_dmic_thread_wait", 0x8b);
    }

    return 0;
}

static int32_t _dmic_polling_frame_wait(void *arg1)
{
    struct timeval tv;
    struct timespec ts;
    void *s1 = (uint8_t *)arg1 + 0x118;

    gettimeofday(&tv, 0);
    ts.tv_sec = tv.tv_sec + 5;
    ts.tv_nsec = tv.tv_usec * 1000;

    while (audio_buf_try_get_node((int32_t *)READ_PTR(arg1, 0x158), 1) == 0) {
        if (pthread_cond_timedwait(s1, (pthread_mutex_t *)((uint8_t *)arg1 + 0x7c), &ts) == 0x91) {
            s1 = (uint8_t *)arg1 + 0x118;
            imp_log_fun(5, IMP_Log_Get_Option(), 2, "dmic",
                "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0xd9,
                "_dmic_polling_frame_wait", "fun:%s,polling frame wait timeout.\n",
                "_dmic_polling_frame_wait");
        }
    }

    _dmic_chn_unlock(arg1);
    return 0;
}

static int32_t __dmic_dev_init(void *arg1)
{
    int32_t v0 = open("/dev/dsp", 0x80000);

    WRITE_I32(arg1, 0x8, v0);
    if (v0 < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0xeb,
            "__dmic_dev_init", "open dev %s error %s\n", "/dev/dsp",
            strerror(*__errno_location()), &_gp);
        return -1;
    }

    if (ioctl(v0, 0xc0045002, (uint8_t *)arg1 + 0x18) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0xf1,
            "__dmic_dev_init", "ioctl mic set samplerate %d error %s\n",
            READ_I32(arg1, 0x18), strerror(*__errno_location()), &_gp);
        return -1;
    }

    if (ioctl(READ_I32(arg1, 0x8), 0xc0045006, (uint8_t *)arg1 + 0x2c) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0xf6,
            "__dmic_dev_init", "dmic dev SNDCTL_DSP_CHANNELS error %s\n",
            strerror(*__errno_location()));
        return -1;
    }

    if (ioctl(READ_I32(arg1, 0x8), 0xc0045005, (uint8_t *)arg1 + 0x1c) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0xfb,
            "__dmic_dev_init", "dmic dev SNDCTL_DSP_SETFMT error %s\n",
            strerror(*__errno_location()));
        return -1;
    }

    if (ioctl(READ_I32(arg1, 0x8), 0x40045070, 1) == 0) {
        return 0;
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x100,
        "__dmic_dev_init", "dmic dev SNDCTL_EXT_ENABLE_STREAM error %s\n",
        strerror(*__errno_location()));
    return -1;
}

static int32_t __dmic_dev_deinit(void *arg1)
{
    int32_t v0 = READ_I32(arg1, 0x8);

    if (v0 > 0) {
        close(v0);
        WRITE_I32(arg1, 0x8, -1);
    }

    return 0;
}

static int32_t _dmic_dev_read(void *arg1, void *arg2, int16_t *arg3, int32_t arg4, int32_t arg5)
{
    int32_t v0 = READ_I32(arg2, 0xe0);
    int32_t var_18 = (int32_t)(uintptr_t)arg3;
    int32_t var_14;

    if (v0 != 0 || READ_I32(arg2, 0xec) != 0) {
        if (arg4 == 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic",
                "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x11e,
                "_dmic_dev_read", "dmic ref enabled but ref buf is NULL.\n");
            return -1;
        }
        var_14 = arg4;
    } else {
        var_14 = 0;
    }

    if (ioctl(READ_I32(arg1, 0x8), 0x300, &var_18) == 0) {
        return arg5;
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x128,
        "_dmic_dev_read", "dmic DMIC_GET_AI_STREAM error,%s\n",
        strerror(*__errno_location()));
    return -1;
}

static void *_dmic_record_thread(void *arg1)
{
    const char *var_34 = "AI_DisableAec";
    const char *var_40 = "_dmic_record_thread";
    const char *var_38 = "AI_DisableAec";
    const char *var_3c = "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c";
    const char *var_44 = "AI_DisableAec";
    const char *var_48 = "_dmic_chn_disable_post";
    int32_t var_30 = 0x110000;
    const char *var_2c = "dmic aec process error.\n";
    int32_t s4 = 0;
    int32_t var_70 = 0;
    int32_t var_6c = 0;
    const char *var_68 = NULL;
    const char *var_64 = NULL;
    int32_t a2_4 = 0;
    int32_t a3_6 = 0;
    int32_t v0;

    if (arg1 == 0) {
        printf("error:(%s,%d), argv == NULL.\n", "_dmic_record_thread", 0x142);
        pthread_exit(NULL);
    }

    v0 = pthread_mutex_lock((pthread_mutex_t *)((uint8_t *)arg1 + 0x34));
    if (v0 != 0) {
        a3_6 = v0;
        a2_4 = 0x14e;
        goto label_exit_printf;
    }

    while (1) {
        int32_t s3_1 = READ_I32(arg1, 0x6c);
        int32_t i = READ_I32(arg1, 0x4);

        if (s3_1 == 1) {
            goto label_c8560;
        }

        while (i == 1) {
            if (s3_1 != 0) {
                goto label_c82b0;
            }

            WRITE_I32(arg1, 0x114, i);
            if (pthread_cond_signal((pthread_cond_t *)((uint8_t *)arg1 + 0x120)) != 0) {
                printf("err: %s,%d ret= %d\n", var_48, 0xaf,
                    pthread_cond_signal((pthread_cond_t *)((uint8_t *)arg1 + 0x120)),
                    var_70, var_6c, var_68, var_64, &_gp);
            }

            _dmic_thread_wait(arg1);
            s3_1 = READ_I32(arg1, 0x6c);
            i = READ_I32(arg1, 0x4);
            if (s3_1 == 1) {
label_c8560:
                if (READ_I32(arg1, 0x1b0) == 0) {
                    WRITE_I32(arg1, 0x118, s3_1);
                    if (pthread_cond_signal((pthread_cond_t *)((uint8_t *)arg1 + 0x150)) != 0) {
                        printf("err: %s,%d ret= %d\n", &var_44[0x2508], 0xe2,
                            pthread_cond_signal((pthread_cond_t *)((uint8_t *)arg1 + 0x150)),
                            var_70, var_6c, var_68, var_64, &_gp);
                    }

                    if (i == s3_1) {
                        goto label_c82b0;
                    }

                    if (i != 0) {
                        goto label_c82b0;
                    }

                    goto label_c85bc;
                }
            }
        }

        if (i == 0) {
label_c85bc:
            v0 = pthread_mutex_unlock((pthread_mutex_t *)((uint8_t *)arg1 + 0x34));
            if (v0 == 0) {
                goto label_thread_exit;
            }

            a3_6 = v0;
            a2_4 = 0x162;
            break;
        }

label_c82b0:
        v0 = pthread_mutex_unlock((pthread_mutex_t *)((uint8_t *)arg1 + 0x34));
        if (v0 != 0) {
            a3_6 = v0;
            a2_4 = 0x16a;
            break;
        }

        _dmic_chn_lock((uint8_t *)arg1 + 0x68);
        {
            int32_t s1_2 = audio_buf_get_node((int32_t *)READ_PTR(arg1, 0x1c0), 0);

            if (s1_2 == 0) {
                s1_2 = audio_buf_get_node((int32_t *)READ_PTR(arg1, 0x1c0), 1);
                if (s1_2 == 0) {
                    printf("error:(%s,%d),dmic thread cat not get full buf.\n",
                        &var_34[0x2524], 0x173);
                    goto label_thread_exit;
                }
            }

            {
                int32_t *s2_1 = (int32_t *)(intptr_t)audio_buf_node_get_data((void *)(intptr_t)s1_2);

                if (READ_I32(arg1, 0x1b0) == 1 || READ_I32(arg1, 0x1bc) == 1) {
                    int32_t s3_4 = READ_I32(arg1, 0x1b4);

                    if (s3_4 != 0) {
                        s4 = ((READ_I32(arg1, 0x28) << 1) * audio_buf_node_index((int32_t *)(intptr_t)s1_2)) + s3_4;
                    }
                }

                _dmic_chn_unlock((uint8_t *)arg1 + 0x68);
                var_70 = (READ_I32(arg1, 0x2c) * READ_I32(arg1, 0x28)) << 1;
                v0 = _dmic_dev_read(arg1, (uint8_t *)arg1 + 0x68, (int16_t *)&s2_1[5], s4, var_70);
                if (v0 < 0) {
                    printf("error:(%s,%d),_dmic_dev_read ret = %d.\n",
                        &var_34[0x2524], 0x183, v0);
                    goto label_thread_exit;
                }

                {
                    uint64_t ts = IMP_System_GetTimeStamp();
                    int32_t a0_8 = READ_I32(arg1, 0x1bc);
                    int32_t a2_2 = v0;

                    s2_1[2] = (int32_t)ts;
                    s2_1[3] = (int32_t)(ts >> 32);
                    s2_1[0] = s1_2;
                    s2_1[4] = v0;

                    if (a0_8 != 0 && s4 != 0) {
                        struct {
                            int32_t ref;
                            int32_t in;
                            int32_t out;
                            int32_t bytes;
                        } aec_args;

                        pthread_mutex_lock((pthread_mutex_t *)((uint8_t *)arg1 + 0xfc));
                        {
                            int32_t a1_2 = READ_I32(arg1, 0x2c);
                            int32_t t6_1 = READ_I32(arg1, 0x28);
                            int32_t v1_3 = READ_I32(arg1, 0x10);
                            uint8_t *t5_3 = (uint8_t *)&s2_1[5] + ((a1_2 * t6_1) << 1);

                            if (t6_1 > 0) {
                                int16_t *v1_5 = (int16_t *)((uint8_t *)&s2_1[5] + (v1_3 << 1));
                                int16_t *a0_12 = (int16_t *)((uint8_t *)&s2_1[5] + ((v1_3 + a1_2) << 1));
                                uint8_t *i_1 = t5_3 + 2;

                                do {
                                    i_1 += 8;
                                    *(int16_t *)(i_1 - 0xa) = *v1_5;
                                    v1_5 = &v1_5[a1_2 * 4];
                                    *(int16_t *)(i_1 - 8) = *a0_12;
                                    a0_12 = &a0_12[a1_2 * 4];
                                    *(int16_t *)(i_1 - 6) = v1_5[a1_2 * 2];
                                    *(int16_t *)(i_1 - 4) = a0_12[a1_2 * 2];
                                } while (i_1 != t5_3 + (((uint32_t)(t6_1 - 1) >> 2) << 3) + 0xa);
                            }

                            aec_args.ref = s4;
                            aec_args.in = (int32_t)(intptr_t)t5_3;
                            aec_args.out = (int32_t)(intptr_t)t5_3;
                            aec_args.bytes = t6_1 << 1;

                            if (READ_I32(arg1, 0x1bc) == 1 &&
                                fun_ai_aec_process != NULL &&
                                fun_ai_aec_process((void *)(intptr_t)READ_I32(arg1, 0x1b8), &aec_args) != 0) {
                                var_64 = var_2c;
                                var_68 = var_40;
                                var_6c = 0x1a2;
                                var_70 = (int32_t)(intptr_t)var_3c;
                                imp_log_fun(6, IMP_Log_Get_Option(), 2, &var_38[0x171c],
                                    (const char *)(intptr_t)var_70, 0x1a2, var_68, var_64);
                            }
                        }
                        pthread_mutex_unlock((pthread_mutex_t *)((uint8_t *)arg1 + 0xfc));
                        a2_2 = s2_1[4];
                    }

                    _audio_set_volume((int16_t *)&s2_1[5], (uint8_t *)&s2_1[5], a2_2, 0x10,
                        0.0, READ_DBL(arg1, 0x88), 32767.0);
                }

                _dmic_chn_lock((uint8_t *)arg1 + 0x68);
                audio_buf_put_node((int32_t *)READ_PTR(arg1, 0x1c0), s1_2, 1);
                pthread_cond_signal((pthread_cond_t *)((uint8_t *)arg1 + 0x180));
                _dmic_chn_unlock((uint8_t *)arg1 + 0x68);
            }
        }

        v0 = pthread_mutex_lock((pthread_mutex_t *)((uint8_t *)arg1 + 0x34));
        if (v0 != 0) {
            a3_6 = v0;
            a2_4 = 0x14e;
            break;
        }
    }

label_exit_printf:
    printf("err: %s,%d ret= %d\n", &var_34[0x2524], a2_4, a3_6,
        var_70, var_6c, var_68, var_64, &_gp);

label_thread_exit:
    imp_log_fun(4, IMP_Log_Get_Option(), 2, &var_38[0x171c], var_3c, 0x1b1,
        var_40, "dmic record thread exit.\n", &_gp);
    pthread_exit(NULL);
}

static int32_t _dmic_dev_enable_aec(void *arg1)
{
    int32_t a0 = READ_I32(arg1, 0x8);
    int32_t var_10 = 1;
    int32_t result = ioctl(a0, 0x40045065, &var_10);

    if (result != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x21d,
            "_dmic_dev_enable_aec", "error:(%s,%d), SNDCTL_EXT_ENABLE_AEC failed.\n",
            "_dmic_dev_enable_aec", 0x21d, &_gp);
    }

    return result;
}

static int32_t _dmic_dev_disable_aec(void *arg1)
{
    int32_t a0 = READ_I32(arg1, 0x8);
    int32_t var_10 = 0;
    int32_t result = ioctl(a0, 0x40045064, &var_10);

    if (result != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x227,
            "_dmic_dev_disable_aec", "error:(%s,%d), SNDCTL_EXT_Disable_AEC failed.\n",
            "_dmic_dev_disable_aec", 0x227, &_gp);
    }

    return result;
}

static int32_t _dmic_ref_enable(void *arg1, void *arg2)
{
    int32_t result = _dmic_dev_enable_aec(arg1);

    if (result != 0) {
        puts("err: __ai_dev_enable_aec");
        return result;
    }

    {
        int32_t s2_1 = READ_I32(arg2, 0x30);
        int32_t s0_1 = READ_I32(arg1, 0x28) << 1;

        _dmic_chn_lock(arg2);
        {
            void *v0 = calloc((size_t)s2_1 * (size_t)s0_1, 1);

            WRITE_PTR(arg2, 0x14c, v0);
            if (v0 != 0) {
                _dmic_chn_unlock(arg2);
                return 0;
            }
        }

        imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x23b,
            "_dmic_ref_enable", "error:(%s,%d),dmic ref malloc error.\n",
            "_dmic_ref_enable", 0x23b, &_gp);
        _dmic_chn_unlock(arg2);
    }

    return -1;
}

static int32_t _dmic_ref_disable(void *arg1, void *arg2)
{
    int32_t result = _dmic_dev_disable_aec(arg1);

    if (result != 0) {
        puts("error:dmic dev disable.");
    }

    (void)arg2;
    return result;
}

int IMP_DMIC_SetUserInfo(int devNum, int chnNum, void *info)
{
    void *dmicDev = dmic_base();
    int32_t a3_1;
    int32_t v0_1;

    if (devNum > 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x255,
            "IMP_DMIC_SetUserInfo", "fun:%s, Invalid dmic device ID.\n",
            "IMP_DMIC_SetUserInfo");
        return -1;
    }

    a3_1 = devNum << 2;
    if ((uint32_t)chnNum >= 4U) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x25a,
            "IMP_DMIC_SetUserInfo",
            "fun:%s, Invalid aecDmicId: the range of aecDmicId is 0 to 3.\n");
        return -1;
    }

    v0_1 = devNum << 5;
    if (info != 0) {
        WRITE_I32(dmicDev, ((v0_1 - a3_1 + devNum) << 4) + 0xc, 1);
    } else {
        WRITE_I32(dmicDev, ((v0_1 - a3_1 + devNum) << 4) + 0xc, 0);
    }

    WRITE_I32(dmicDev, ((v0_1 - a3_1 + devNum) << 4) + 0x10, chnNum);
    return 0;
}

int IMP_DMIC_SetPubAttr(int devNum, IMPAudioIOAttr *attr)
{
    void *dmicDev = dmic_base();
    const char *var_1c_1;
    int32_t v0_10;
    int32_t v1_9;
    uint32_t lo_1;
    void *a0_3;

    if (devNum > 0) {
        imp_log_fun(4, IMP_Log_Get_Option(), 2, "dmic",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x271,
            "IMP_DMIC_SetPubAttr", "fun:%s, Invalid Dmic device ID.\n",
            "IMP_DMIC_SetPubAttr");
        return -1;
    }

    if (((int32_t *)attr)[5] >= 5) {
        v0_10 = IMP_Log_Get_Option();
        var_1c_1 = "fun:%s,set dmic pub attr error, Invalid Chn cnt.\n";
        v1_9 = 0x275;
        goto label_log;
    }

    if (*(int32_t *)attr == 0) {
        __builtin_trap();
    }

    lo_1 = (uint32_t)(((int32_t *)attr)[4] * 1000U) / (uint32_t)(*(int32_t *)attr);
    if (lo_1 != ((lo_1 / 10U) * 10U)) {
        v0_10 = IMP_Log_Get_Option();
        var_1c_1 = "fun:%s,dmic samples(numPerFrm) must be an integer number of 10ms (10ms * n).\n";
        v1_9 = 0x27c;
        goto label_log;
    }

    a0_3 = (uint8_t *)dmicDev + devNum * 0x1d0 + 0x10;
    WRITE_I32(a0_3, 0x8, ((int32_t *)attr)[0]);
    WRITE_I32(a0_3, 0xc, ((int32_t *)attr)[1]);
    WRITE_I32(a0_3, 0x10, ((int32_t *)attr)[2]);
    WRITE_I32(a0_3, 0x14, ((int32_t *)attr)[3]);
    WRITE_I32(a0_3, 0x18, ((int32_t *)attr)[4]);
    WRITE_I32(a0_3, 0x1c, ((int32_t *)attr)[5]);
    return 0;

label_log:
    imp_log_fun(6, v0_10, 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", v1_9,
        "IMP_DMIC_SetPubAttr", var_1c_1, "IMP_DMIC_SetPubAttr");
    return -1;
}

int IMP_DMIC_GetPubAttr(int devNum, IMPAudioIOAttr *attr)
{
    void *dmicDev = dmic_base();
    void *v0_5;

    if (devNum > 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x289,
            "IMP_DMIC_GetPubAttr", "fun:%s, Invalid dmic device ID.\n",
            "IMP_DMIC_GetPubAttr");
        return -1;
    }

    v0_5 = (uint8_t *)dmicDev + devNum * 0x1d0 + 0x10;
    ((int32_t *)attr)[0] = READ_I32(v0_5, 0x8);
    ((int32_t *)attr)[1] = READ_I32(v0_5, 0xc);
    ((int32_t *)attr)[2] = READ_I32(v0_5, 0x10);
    ((int32_t *)attr)[3] = READ_I32(v0_5, 0x14);
    ((int32_t *)attr)[4] = READ_I32(v0_5, 0x18);
    ((int32_t *)attr)[5] = READ_I32(v0_5, 0x1c);
    return 0;
}

int IMP_DMIC_Enable(int devNum)
{
    void *dmicDev = dmic_base();
    int32_t s0_4;
    void *s1_1;
    int32_t s7_1;
    const char *var_3c;
    int32_t v0_13;
    int32_t v1_3;
    int32_t v0_2;
    int32_t v0_3;

    imp_log_fun(4, IMP_Log_Get_Option(), 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x29b,
        "IMP_DMIC_Enable", "DMIC Enable:%d.\n", devNum);
    if (devNum > 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x29d,
            "IMP_DMIC_Enable", "Invalid dmic device ID.\n");
        return -1;
    }

    s0_4 = devNum * 0x1d0;
    s1_1 = (uint8_t *)dmicDev + s0_4;
    if (READ_I32(s1_1, 0x4) == 1) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x2a2,
            "IMP_DMIC_Enable", "fun:%s,dmic device is already enabled.\n",
            "IMP_DMIC_Enable");
        return 0;
    }

    s7_1 = READ_I32(s1_1, 0x20);
    if (s7_1 != 1) {
        v0_13 = IMP_Log_Get_Option();
        var_3c = "fun:%s,sound mode error, only support AUDIO_SOUND_MODE_MONO\n";
        v1_3 = 0x2a9;
        goto label_enable_log;
    }

    if (READ_I32(s1_1, 0x1c) != 0x10) {
        v0_13 = IMP_Log_Get_Option();
        var_3c = "fun:%s, fun:%s,dmic in bit width error, onlysupport AUDIO_BIT_WIDTH_16.\n";
        v1_3 = 0x2ad;
        goto label_enable_log;
    }

    v0_2 = __dmic_dev_init(s1_1);
    if (v0_2 != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x2b3,
            "IMP_DMIC_Enable", "fun:%s,__dmic_dev_init error.\n",
            "IMP_DMIC_Enable", strerror(*__errno_location()), &_gp);
        return v0_2;
    }

    pthread_mutex_init((pthread_mutex_t *)((uint8_t *)dmicDev + s0_4 + 0x34), 0);
    pthread_cond_init((pthread_cond_t *)((uint8_t *)dmicDev + s0_4 + 0x4c), 0);
    WRITE_I32(s1_1, 0x4, s7_1);
    v0_3 = pthread_create((pthread_t *)((uint8_t *)dmicDev + s0_4 + 0x14), 0,
        _dmic_record_thread, s1_1);
    if (v0_3 == 0) {
        return 0;
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x2bd,
        "IMP_DMIC_Enable", "fun:%s, record thread create error %s.\n",
        "IMP_DMIC_Enable", strerror(*__errno_location()), &_gp);
    return v0_3;

label_enable_log:
    imp_log_fun(6, v0_13, 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", v1_3,
        "IMP_DMIC_Enable", var_3c, "IMP_DMIC_Enable");
    return -1;
}

int IMP_DMIC_Disable(int devNum)
{
    void *dmicDev = dmic_base();
    int32_t s1_1;
    int32_t s0_2;
    void *s5_1;
    int32_t s1;
    int32_t v0_2;
    int32_t v0_3;
    int32_t v0_4;
    int32_t v0_5;

    imp_log_fun(4, IMP_Log_Get_Option(), 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x2c9,
        "IMP_DMIC_Disable", "DMIC Disable:%d.\n", devNum);
    if (devNum > 0) {
        s1_1 = -1;
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x2cc,
            "IMP_DMIC_Disable", "fun:%s,Invalid dmic device ID.\n",
            "IMP_DMIC_Disable");
        return s1_1;
    }

    s0_2 = devNum * 0x1d0;
    s5_1 = (uint8_t *)dmicDev + s0_2;
    s1 = READ_I32(s5_1, 0x4);
    if (s1 == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x2d1,
            "IMP_DMIC_Disable", "fun:%s,dmic device is already Disabled.\n",
            "IMP_DMIC_Disable");
        return s1;
    }

    v0_2 = pthread_mutex_lock((pthread_mutex_t *)((uint8_t *)dmicDev + s0_2 + 0x34));
    s1_1 = v0_2;
    if (v0_2 != 0) {
        printf("err: %s,%d ret= %d\n", "IMP_DMIC_Disable", 0x2d6,
            pthread_mutex_lock((pthread_mutex_t *)((uint8_t *)dmicDev + s0_2 + 0x34)));
    }

    _dmic_thread_post(s5_1);
    WRITE_I32(s5_1, 0x4, 0);
    v0_3 = pthread_mutex_unlock((pthread_mutex_t *)((uint8_t *)dmicDev + s0_2 + 0x34));
    s1_1 = v0_3;
    if (v0_3 != 0) {
        printf("err: %s,%d ret= %d\n", "IMP_DMIC_Disable", 0x2d9,
            pthread_mutex_unlock((pthread_mutex_t *)((uint8_t *)dmicDev + s0_2 + 0x34)));
    }

    pthread_join(*(pthread_t *)((uint8_t *)s5_1 + 0x14), 0);
    __dmic_dev_deinit(s5_1);
    v0_4 = pthread_mutex_destroy((pthread_mutex_t *)((uint8_t *)dmicDev + s0_2 + 0x34));
    s1_1 = v0_4;
    if (v0_4 != 0) {
        printf("error:(%s,%d),destroy mutex_dev.\n");
    }

    v0_5 = pthread_cond_destroy((pthread_cond_t *)((uint8_t *)dmicDev + s0_2 + 0x4c));
    s1_1 = 0;
    if (v0_5 != 0) {
        s1_1 = v0_5;
        printf("error:(%s,%d),destrory cond_dev thread.\n");
    }

    return s1_1;
}

int IMP_DMIC_SetChnParam(int devNum, int chnNum, IMPAudioIChnParam *param)
{
    void *dmicDev = dmic_base();
    const char *var_1c;
    int32_t v0_6;
    int32_t v1_2;
    int32_t a3_1;

    if (devNum > 0) {
        v0_6 = IMP_Log_Get_Option();
        var_1c = "fun:%s, Invalid dmic device ID.\n";
        v1_2 = 0x2f5;
        goto label_setchn_log;
    }

    if (chnNum > 0) {
        v0_6 = IMP_Log_Get_Option();
        var_1c = "fun:%s, Invalid dmic channel ID.\n";
        v1_2 = 0x2fa;
        goto label_setchn_log;
    }

    a3_1 = param->usrFrmDepth;
    if (a3_1 >= 2 && READ_I32(dmicDev, 0x24) >= a3_1) {
        WRITE_I32(dmicDev, chnNum * 0x160 + devNum * 0x1d0 + 0x98, a3_1);
        return 0;
    }

    v0_6 = IMP_Log_Get_Option(devNum, chnNum, dmicDev);
    var_1c = "error:(%s),dmic channel usrFrmDepth.\n";
    v1_2 = 0x301;

label_setchn_log:
    imp_log_fun(6, v0_6, 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", v1_2,
        "IMP_DMIC_SetChnParam", var_1c, "IMP_DMIC_SetChnParam");
    return -1;
}

int IMP_DMIC_GetChnParam(int devNum, int chnNum, IMPAudioIChnParam *param)
{
    void *dmicDev = dmic_base();
    const char *var_1c_1;
    int32_t v0_7;
    int32_t v1_2;
    int32_t a0_1;

    if (devNum > 0) {
        v0_7 = IMP_Log_Get_Option();
        var_1c_1 = "fun:%s,Invalid dmic device ID.\n";
        v1_2 = 0x30e;
        goto label_getchn_log;
    }

    if (chnNum <= 0) {
        a0_1 = devNum * 0x1d0;
        if (READ_I32(dmicDev, a0_1 + 4) == 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic",
                "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x317,
                "IMP_DMIC_GetChnParam", "error:fun:%s,dmic device is no enabled.\n",
                "IMP_DMIC_GetChnParam");
            return -1;
        }

        param->usrFrmDepth = READ_I32(dmicDev, chnNum * 0x160 + a0_1 + 0x98);
        return 0;
    }

    v0_7 = IMP_Log_Get_Option();
    var_1c_1 = "fun:%s,Invalid dmic chnnel ID.\n";
    v1_2 = 0x312;

label_getchn_log:
    imp_log_fun(6, v0_7, 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", v1_2,
        "IMP_DMIC_GetChnParam", var_1c_1);
    return -1;
}

int IMP_DMIC_EnableAec(int devNum, int chnNum)
{
    void *dmicDev = dmic_base();
    const char *var_4c_1;
    int32_t v0_14;
    int32_t v1;
    int32_t a0_1;
    void *s2_1;
    int32_t s0_5;
    void *s3_1;
    void *names[3];
    int32_t v0_16;
    const char *a0_7;
    int32_t a1_5;
    int32_t var_44_1;

    if (devNum > 0) {
        v0_14 = IMP_Log_Get_Option();
        var_4c_1 = "fun:%s,Invalid dmic device ID.\n";
        v1 = 0x324;
        goto label_enableaec_log;
    }

    if (chnNum > 0) {
        v0_14 = IMP_Log_Get_Option();
        var_4c_1 = "fun:%s,Invalid dmic channel ID.\n";
        v1 = 0x328;
        goto label_enableaec_log;
    }

    a0_1 = devNum * 0x1d0;
    s2_1 = (uint8_t *)dmicDev + a0_1;
    if (READ_I32(s2_1, 0x4) == 0) {
        v0_14 = IMP_Log_Get_Option();
        var_4c_1 = "fun:%s, dmic device is no enabled.\n";
        v1 = 0x32d;
        goto label_enableaec_log;
    }

    s0_5 = chnNum * 0x160 + a0_1;
    s3_1 = (uint8_t *)dmicDev + s0_5;
    if (READ_I32(s3_1, 0x6c) == 0) {
        v0_14 = IMP_Log_Get_Option();
        var_4c_1 = "fun:%s, dmic channel is no enabled.\n";
        v1 = 0x331;
        goto label_enableaec_log;
    }

    if (READ_I32(s3_1, 0x1b0) == 1) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x337,
            "IMP_DMIC_EnableAec",
            "error:(%s,%d),dmic ref has enabled. . AEC and Ref can not enable both.\n",
            "IMP_DMIC_EnableAec", 0x337, &_gp);
        return 0;
    }

    if (init_audioProcess_library("libaudioProcess.so") != 0) {
        v0_14 = IMP_Log_Get_Option();
        var_4c_1 = "fun:%s,init audioProcess library failed\n";
        v1 = 0x33d;
        goto label_enableaec_log;
    }

    if (g_dmic_audio_process_handle == NULL) {
        g_dmic_audio_process_handle = dlopen("libaudioProcess.so", RTLD_LAZY);
    }

    names[0] = "audio_process_aec_create";
    names[1] = "audio_process_aec_process";
    names[2] = "audio_process_aec_free";
    if (get_fun_address(g_dmic_audio_process_handle, names, 3) != 0) {
        v0_16 = IMP_Log_Get_Option();
        a1_5 = 0x347;
        var_44_1 = 0x347;
        a0_7 = "error:(%s,%d), get fun address failed.\n";
        goto label_enableaec_log2;
    }

    fun_ai_aec_create = (int32_t (*)(int32_t, int32_t))names[0];
    fun_ai_aec_process = (int32_t (*)(void *, void *))names[1];
    fun_ai_aec_free = (int32_t (*)(int32_t))names[2];

    pthread_mutex_lock((pthread_mutex_t *)((uint8_t *)dmicDev + s0_5 + 0xfc));
    v0_16 = fun_ai_aec_create(READ_I32(s2_1, 0x18), 0);
    WRITE_I32(s3_1, 0x1b8, v0_16);
    if (v0_16 == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x34e,
            "IMP_DMIC_EnableAec", "fun:%s,aec create error.\n",
            "IMP_DMIC_EnableAec");
        pthread_mutex_unlock((pthread_mutex_t *)((uint8_t *)dmicDev + s0_5 + 0xfc));
        return -1;
    }

    if (_dmic_ref_enable(s2_1, (uint8_t *)dmicDev + s0_5 + 0x68) == 0) {
        WRITE_I32(s3_1, 0x1bc, 1);
        pthread_mutex_unlock((pthread_mutex_t *)((uint8_t *)dmicDev + s0_5 + 0xfc));
        return 0;
    }

    v0_16 = IMP_Log_Get_Option();
    a1_5 = 0x354;
    var_44_1 = 0x354;
    a0_7 = "error:(%s,%d), dmic ref enable error.\n";

label_enableaec_log2:
    imp_log_fun(6, v0_16, 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", a1_5,
        "IMP_DMIC_EnableAec", a0_7, "IMP_DMIC_EnableAec", var_44_1, &_gp);
    return -1;

label_enableaec_log:
    imp_log_fun(6, v0_14, 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", v1,
        "IMP_DMIC_EnableAec", var_4c_1, "IMP_DMIC_EnableAec");
    return -1;
}

int IMP_DMIC_DisableAec(int devNum, int chnNum)
{
    void *dmicDev = dmic_base();
    const char *var_3c;
    int32_t v0_12;
    int32_t v1_2;
    int32_t s0_3;
    void *s2_1;
    int32_t s0_4;
    void *s7_1;

    if (devNum > 0) {
        v0_12 = IMP_Log_Get_Option();
        var_3c = "fun:%s,Invalid dmic device ID.\n";
        v1_2 = 0x364;
        goto label_disableaec_log;
    }

    if (chnNum > 0) {
        v0_12 = IMP_Log_Get_Option();
        var_3c = "fun:%s,Invalid dmic channel ID.\n";
        v1_2 = 0x368;
        goto label_disableaec_log;
    }

    s0_3 = devNum * 0x1d0;
    s2_1 = (uint8_t *)dmicDev + s0_3;
    if (READ_I32(s2_1, 0x4) == 0) {
        v0_12 = IMP_Log_Get_Option();
        var_3c = "fun:%s, dmic device is no enabled.\n";
        v1_2 = 0x36c;
        goto label_disableaec_log;
    }

    s0_4 = chnNum * 0x160 + s0_3;
    s7_1 = (uint8_t *)dmicDev + s0_4;
    if (READ_I32(s7_1, 0x6c) == 0) {
        v0_12 = IMP_Log_Get_Option();
        var_3c = "fun:%s, dmic channel is no enabled.\n";
        v1_2 = 0x370;
        goto label_disableaec_log;
    }

    imp_log_fun(4, IMP_Log_Get_Option(), 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x377,
        "IMP_DMIC_DisableAec", "DMIC AEC Disable.\n");
    pthread_mutex_lock((pthread_mutex_t *)((uint8_t *)dmicDev + s0_4 + 0xfc));
    if (READ_I32(s7_1, 0x1bc) == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x37a,
            "IMP_DMIC_DisableAec", "error:(%s,%d),dmic aec is not enabled.\n",
            "IMP_DMIC_DisableAec", 0x37a, &_gp);
        pthread_mutex_unlock((pthread_mutex_t *)((uint8_t *)dmicDev + s0_4 + 0xfc));
        return -1;
    }

    pthread_mutex_unlock((pthread_mutex_t *)((uint8_t *)dmicDev + s0_4 + 0xfc));
    pthread_mutex_lock((pthread_mutex_t *)((uint8_t *)dmicDev + s0_4 + 0xfc));
    WRITE_I32(s7_1, 0x1bc, 0);
    pthread_mutex_unlock((pthread_mutex_t *)((uint8_t *)dmicDev + s0_4 + 0xfc));
    pthread_mutex_lock((pthread_mutex_t *)((uint8_t *)dmicDev + s0_4 + 0xfc));
    if (fun_ai_aec_free(READ_I32(s7_1, 0x1b8)) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x387,
            "IMP_DMIC_DisableAec", "erro:(%s,%d),aec free error.\n",
            "IMP_DMIC_DisableAec", 0x387, &_gp);
        pthread_mutex_unlock((pthread_mutex_t *)((uint8_t *)dmicDev + s0_4 + 0xfc));
    }
    pthread_mutex_unlock((pthread_mutex_t *)((uint8_t *)dmicDev + s0_4 + 0xfc));
    fun_ai_aec_free = 0;
    fun_ai_aec_create = 0;
    fun_ai_aec_process = 0;
    if (_dmic_ref_disable(s2_1, (uint8_t *)dmicDev + s0_4 + 0x68) == 0) {
        return 0;
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x391,
        "IMP_DMIC_DisableAec", "error:(%s,%d),_dmic_ref_disable error.\n",
        "IMP_DMIC_DisableAec", 0x391, &_gp);
    return -1;

label_disableaec_log:
    imp_log_fun(6, v0_12, 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", v1_2,
        "IMP_DMIC_DisableAec", var_3c, "IMP_DMIC_DisableAec");
    return -1;
}

int IMP_DMIC_EnableChn(int devNum, int chnNum)
{
    void *dmicDev = dmic_base();
    const char *var_54;
    int32_t v0_29;
    int32_t v1_3;
    int32_t s2_1;
    int32_t v1;
    int32_t s0_1;
    int32_t a1_1;
    int32_t a0;
    void *a2_1;
    int32_t v0_7;
    void *v0_8;
    int32_t t1_1;
    int32_t t0_1;
    int32_t v0_9;
    int32_t t0_4;
    int32_t s6_2;
    void *s5_1;
    uint64_t prod;
    int32_t hi_1;
    int32_t s0_5;
    void *s1_1;
    int32_t a0_4;
    int32_t v0_15;
    int32_t v0_16;
    int32_t v0_20;
    int32_t v0_21;

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x3a0,
        "IMP_DMIC_EnableChn", "DMIC Enable Chn:%d-%d.\n", devNum, chnNum, &_gp);
    if (devNum > 0) {
        v0_29 = IMP_Log_Get_Option();
        var_54 = "fun:%s, Invalid dmic device Id.\n";
        v1_3 = 0x3a3;
        goto label_enablechn_log;
    }

    s2_1 = devNum << 2;
    if (chnNum >= 2) {
        v0_29 = IMP_Log_Get_Option();
        var_54 = "fun:%s, Invalid dmic channel.\n";
        v1_3 = 0x3a8;
        goto label_enablechn_log;
    }

    v1 = devNum << 5;
    s0_1 = chnNum << 2;
    a1_1 = chnNum << 4;
    a0 = (v1 - s2_1 + devNum) << 4;
    a2_1 = (uint8_t *)dmicDev + a0;
    v0_7 = (((a1_1 - s0_1 - chnNum) << 5) + a0);
    if (READ_I32(a2_1, 0x4) == 0) {
        v0_29 = IMP_Log_Get_Option((uint8_t *)dmicDev + v0_7 + 0x68);
        var_54 = "fun:%s, dmic device is no enabled.\n";
        v1_3 = 0x3af;
        goto label_enablechn_log;
    }

    v0_8 = (uint8_t *)dmicDev + v0_7;
    if (READ_I32(v0_8, 0x6c) == 1) {
        imp_log_fun(6, IMP_Log_Get_Option(1), 2, "dmic",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x3b4,
            "IMP_DMIC_EnableChn", "fun:%s,dmic channel is already enabled.\n",
            "IMP_DMIC_EnableChn");
        return 0;
    }

    t1_1 = READ_I32(v0_8, 0x98);
    t0_1 = READ_I32(a2_1, 0x28);
    v0_9 = READ_I32(a2_1, 0x2c);
    if (READ_I32(a2_1, 0xc) != 0) {
        t0_4 = (((v0_9 + 1) * t0_1) + 0xc) << 1;
    } else {
        t0_4 = ((v0_9 * t0_1) + 0xc) << 1;
    }

    s6_2 = (v1 - s2_1 + devNum) << 4;
    s5_1 = (uint8_t *)dmicDev + s6_2;
    prod = (uint64_t)(uint32_t)READ_I32(s5_1, 0x18) * 0x51eb851fULL;
    hi_1 = (int32_t)(prod >> 32);
    s0_5 = (((a1_1 - s0_1 - chnNum) << 5) + s6_2);
    s1_1 = (uint8_t *)dmicDev + s0_5;
    a0_4 = t1_1 < 2 ? 1 : 0;
    WRITE_I32(s1_1, 0xe0, ((uint32_t)hi_1 >> 5) << 3);
    if (a0_4 != 0 || READ_I32(s5_1, 0x24) < t1_1) {
        v0_29 = IMP_Log_Get_Option(a0_4);
        var_54 = "fun:%s,The channel prameter are not set.\n";
        v1_3 = 0x3c1;
        goto label_enablechn_log;
    }

    pthread_mutex_init((pthread_mutex_t *)((uint8_t *)dmicDev + s0_5 + 0xe4), 0);
    pthread_cond_init((pthread_cond_t *)((uint8_t *)dmicDev + s0_5 + 0x120), 0);
    pthread_cond_init((pthread_cond_t *)((uint8_t *)dmicDev + s0_5 + 0x150), 0);
    _dmic_chn_lock((uint8_t *)dmicDev + v0_7 + 0x68);
    v0_15 = (int32_t)(intptr_t)audio_buf_alloc(t1_1, t0_4, 0);
    WRITE_I32(s1_1, 0x1c0, v0_15);
    if (v0_15 == 0) {
        v0_29 = IMP_Log_Get_Option();
        var_54 = "error:(%s),dmic buf alloc failed.\n";
        v1_3 = 0x3cc;
        goto label_enablechn_log;
    }

    _dmic_chn_unlock((uint8_t *)dmicDev + v0_7 + 0x68);
    WRITE_I32(s1_1, 0x90, 0x3c);
    WRITE_DBL(s1_1, 0x88, pow(10.0, (double)0x3c / 20.0));
    WRITE_I32(s1_1, 0x80, -30);
    WRITE_I32(s1_1, 0x84, 120);
    v0_16 = pthread_mutex_init((pthread_mutex_t *)((uint8_t *)dmicDev + s0_5 + 0xfc), 0);
    if (v0_16 != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x3d8,
            "IMP_DMIC_EnableChn",
            "error:(%s,%d),dmi init audioprocess mutex failed.\n",
            "IMP_DMIC_EnableChn", 0x3d8);
        return -1;
    }

    v0_20 = pthread_mutex_lock((pthread_mutex_t *)((uint8_t *)dmicDev + s6_2 + 0x34));
    if (v0_20 != 0) {
        printf("err: %s,%d ret= %d\n", "IMP_DMIC_EnableChn", 0x3dc,
            pthread_mutex_lock((pthread_mutex_t *)((uint8_t *)dmicDev + s6_2 + 0x34)));
        return v0_20;
    }

    _dmic_thread_post(s5_1);
    WRITE_I32(s1_1, 0x6c, 1);
    v0_21 = pthread_mutex_unlock((pthread_mutex_t *)((uint8_t *)dmicDev + s6_2 + 0x34));
    if (v0_21 != 0) {
        printf("err: %s,%d ret= %d\n", "IMP_DMIC_EnableChn", 0x3df,
            pthread_mutex_unlock((pthread_mutex_t *)((uint8_t *)dmicDev + s6_2 + 0x34)));
        return v0_21;
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x3e0,
        "IMP_DMIC_EnableChn", "EXIT DMIC Enable Chn:%d-%d.\n", devNum, chnNum);
    return v0_16;

label_enablechn_log:
    imp_log_fun(6, v0_29, 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", v1_3,
        "IMP_DMIC_EnableChn", var_54, "IMP_DMIC_EnableChn");
    return -1;
}

int IMP_DMIC_DisableChn(int devNum, int chnNum)
{
    void *dmicDev = dmic_base();
    const char *var_2c = "AI_DisableAec";
    const char *var_30 = "IMP_DMIC_DisableChn";
    const char *var_64 = "DMIC Disable Chn.\n";
    const char *var_68 = "IMP_DMIC_DisableChn";
    int32_t var_6c = 0x3ea;
    const char *var_68_2;
    const char *var_64_1;
    const char *var_60;
    int32_t v0_34;
    int32_t v1_14;
    int32_t v0_1;
    int32_t v1;
    int32_t s0_3;
    void *fp_1;
    int32_t v0_3;
    int32_t v1_1;
    int32_t s2_4;
    void *s3_1;
    int32_t v0_5;
    struct timeval tv;
    struct timespec ts;
    int32_t i;
    int32_t s2_8;
    int32_t v0_21;
    int32_t v0_22;

    imp_log_fun(4, IMP_Log_Get_Option(), 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x3ea,
        "IMP_DMIC_DisableChn", "DMIC Disable Chn.\n");
    if (devNum > 0) {
        v0_34 = IMP_Log_Get_Option();
        var_60 = var_30;
        var_64_1 = "fun:%s, Invalid dmic device Id.\n";
        var_68_2 = var_30;
        v1_14 = 0x3ed;
        goto label_disablechn_log;
    }

    if (chnNum > 0) {
        v0_34 = IMP_Log_Get_Option();
        var_60 = var_30;
        var_64_1 = "fun:%s,Invalid dmic device Id.\n";
        var_68_2 = var_30;
        v1_14 = 0x3f1;
        goto label_disablechn_log;
    }

    v0_1 = devNum << 2;
    v1 = devNum << 5;
    s0_3 = (v1 - v0_1 + devNum) << 4;
    fp_1 = (uint8_t *)dmicDev + s0_3;
    if (READ_I32(fp_1, 0x4) == 0) {
        v0_34 = IMP_Log_Get_Option();
        var_60 = var_30;
        var_64_1 = "fun:%s, dmic device is no enabled.\n";
        var_68_2 = var_30;
        v1_14 = 0x3f6;
        goto label_disablechn_log;
    }

    v0_3 = chnNum << 2;
    v1_1 = chnNum << 4;
    s2_4 = (((v1_1 - v0_3 - chnNum) << 5) + s0_3);
    s3_1 = (uint8_t *)dmicDev + s2_4;
    if (READ_I32(s3_1, 0x6c) == 0) {
        v0_34 = IMP_Log_Get_Option();
        var_60 = var_30;
        var_64_1 = "fun:%s, dmic device is already enable.\n";
        var_68_2 = var_30;
        v1_14 = 0x3fb;
        goto label_disablechn_log;
    }

    v0_5 = pthread_mutex_lock((pthread_mutex_t *)((uint8_t *)dmicDev + s0_3 + 0x34));
    if (v0_5 != 0) {
        printf("err: %s,%d ret= %d\n", var_30, 0x400,
            pthread_mutex_lock((pthread_mutex_t *)((uint8_t *)dmicDev + s0_3 + 0x34)));
        return v0_5;
    }

    gettimeofday(&tv, 0);
    ts.tv_sec = tv.tv_sec + 5;
    ts.tv_nsec = tv.tv_usec * 1000;
    if (pthread_kill(*(pthread_t *)((uint8_t *)fp_1 + 0x14), 0) != 0) {
        var_64 = "disable chn dmic thread has exit!.\n";
        var_68 = "_dmic_chn_disable_wait";
        var_6c = 0x9c;
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x9c,
            "_dmic_chn_disable_wait", "disable chn dmic thread has exit!.\n");
        WRITE_I32(s3_1, 0x6c, 0);
    }

    WRITE_I32(s3_1, 0x6c, 0);
    WRITE_I32(s3_1, 0x114, 0);
    i = 0;
    while (i == 0) {
        if (pthread_cond_timedwait((pthread_cond_t *)((uint8_t *)dmicDev + s2_4 + 0x120),
                (pthread_mutex_t *)((uint8_t *)dmicDev + s0_3 + 0x34), &ts) == 0x91) {
            var_64 = "dmic_chn_disable_wait timeout\n";
            var_68 = "_dmic_chn_disable_wait";
            var_6c = 0xa5;
            imp_log_fun(5, IMP_Log_Get_Option(0x91), 2, "dmic",
                "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0xa5,
                "_dmic_chn_disable_wait", "dmic_chn_disable_wait timeout\n");
            i = READ_I32((uint8_t *)dmicDev + s2_4, 0x114);
        } else {
            i = READ_I32((uint8_t *)dmicDev + s2_4, 0x114);
        }
    }

    WRITE_I32((uint8_t *)dmicDev + (((v1_1 - v0_3 - chnNum) << 5) + ((v1 - v0_1 + devNum) << 4)), 0x114, 0);
    s2_8 = (((v1_1 - v0_3 - chnNum) << 5) + ((v1 - v0_1 + devNum) << 4));
    audio_buf_free((int32_t *)READ_PTR(dmicDev, s2_8 + 0x1c0));
    v0_21 = pthread_mutex_unlock((pthread_mutex_t *)((uint8_t *)dmicDev + s0_3 + 0x34));
    if (v0_21 != 0) {
        printf("err: %s,%d ret= %d\n", &var_2c[0x245c], 0x403,
            pthread_mutex_unlock((pthread_mutex_t *)((uint8_t *)dmicDev + s0_3 + 0x34)),
            "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", var_6c, var_68, var_64);
        return v0_21;
    }

    v0_22 = pthread_mutex_destroy((pthread_mutex_t *)((uint8_t *)dmicDev + s2_8 + 0xfc));
    if (v0_22 != 0) {
        printf("err:(%s,%d):deinit_audioProcess-mutex.\n", &var_2c[0x245c], 0x407);
        return -1;
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x40a,
        var_30, "EXIT DMIC Disable Chn.\n");
    return v0_22;

label_disablechn_log:
    imp_log_fun(6, v0_34, 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", v1_14,
        var_68_2, var_64_1, var_60);
    return -1;
}

int IMP_DMIC_GetFrame(int devNum, int chnNum, IMPAudioFrame *frame, IMPBlock block)
{
    void *dmicDev = dmic_base();
    const char *var_3c;
    int32_t v0_16;
    int32_t v1_6;
    int32_t s7_2;
    int32_t s4_1;
    int32_t fp_2;
    int32_t s4_2;
    void *s3_1;
    int32_t v0_8;
    int32_t s7_3;
    int32_t v0_9;
    void *a1;
    int32_t result;

    if (devNum > 0) {
        v0_16 = IMP_Log_Get_Option();
        var_3c = "fun:%s, Invalid dmic device ID.\n";
        v1_6 = 0x418;
        goto label_getframe_log;
    }

    if (chnNum > 0) {
        v0_16 = IMP_Log_Get_Option();
        var_3c = "fun:%s, Invalid dmic channel ID.\n";
        v1_6 = 0x41c;
        goto label_getframe_log;
    }

    s7_2 = devNum * 0x1d;
    s4_1 = s7_2 << 4;
    if (READ_I32(dmicDev, s4_1 + 4) == 0) {
        v0_16 = IMP_Log_Get_Option();
        var_3c = "fun:%s, dmic device is no enabled.\n";
        v1_6 = 0x420;
        goto label_getframe_log;
    }

    fp_2 = chnNum * 0xb;
    s4_2 = (fp_2 << 5) + s4_1;
    s3_1 = (uint8_t *)dmicDev + s4_2;
    if (READ_I32(s3_1, 0x6c) == 0) {
        v0_16 = IMP_Log_Get_Option();
        var_3c = "fun:%s, dmic channel is no enabled.\n";
        v1_6 = 0x424;
        goto label_getframe_log;
    }

    if (frame == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x1bf,
            "_dmic_get_frame", "fun:%s,prameter is invalid.\n",
            "_dmic_get_frame", 0x1bf, &_gp);
        return -1;
    }

    while (1) {
        _dmic_chn_lock((uint8_t *)dmicDev + s4_2 + 0x68);
        v0_8 = audio_buf_get_node((int32_t *)READ_PTR(s3_1, 0x1c0), 1);
        _dmic_chn_unlock((uint8_t *)dmicDev + s4_2 + 0x68);
        if (v0_8 != 0) {
            break;
        }

        if (block != 0) {
            _dmic_polling_frame_wait((uint8_t *)dmicDev + s4_2 + 0x68);
        } else {
            return -1;
        }
    }

    s7_3 = s7_2 << 4;
    v0_9 = audio_buf_node_get_data((void *)(intptr_t)v0_8);
    WRITE_I32(s3_1, 0xa8, v0_9 + 0x14);
    a1 = (uint8_t *)dmicDev + s7_3;
    WRITE_I32(s3_1, 0xbc, READ_I32((void *)(intptr_t)v0_9, 0x10));
    WRITE_I32(s3_1, 0xb0, READ_I32((void *)(intptr_t)v0_9, 0x8));
    WRITE_I32(s3_1, 0xb4, READ_I32((void *)(intptr_t)v0_9, 0xc));
    WRITE_I32(s3_1, 0xa4, READ_I32(a1, 0x20));
    WRITE_I32(s3_1, 0xa0, READ_I32(a1, 0x1c));
    WRITE_I32(s3_1, 0xac, 0);
    frame->bitwidth = (IMPAudioBitWidth)READ_I32(s3_1, 0xa0);
    frame->soundmode = (IMPAudioSoundMode)READ_I32(s3_1, 0xa4);
    frame->virAddr = (uint32_t *)(uintptr_t)READ_I32(s3_1, 0xa8);
    frame->phyAddr = (uint32_t)READ_I32(s3_1, 0xac);
    frame->timeStamp = (uint64_t)(uint32_t)READ_I32(s3_1, 0xb0) |
                       ((uint64_t)(uint32_t)READ_I32(s3_1, 0xb4) << 32);
    frame->seq = READ_I32(s3_1, 0xb8);
    frame->len = READ_I32(s3_1, 0xbc);
    result = READ_I32(s3_1, 0x1bc);
    if (result == 0) {
        return result;
    }

    WRITE_I32(s3_1, 0xc8, v0_9 + 0x14 + ((READ_I32(a1, 0x2c) * READ_I32(a1, 0x28)) << 1));
    if (READ_I32(a1, 0x2c) == 0) {
        __builtin_trap();
    }

    WRITE_I32(s3_1, 0xdc, READ_I32((void *)(intptr_t)v0_9, 0x10) / READ_I32(a1, 0x2c));
    WRITE_I32(s3_1, 0xc0, READ_I32(a1, 0x1c));
    WRITE_I32(s3_1, 0xc4, READ_I32(a1, 0x20));
    WRITE_I32(s3_1, 0xd0, READ_I32((void *)(intptr_t)v0_9, 0x8));
    WRITE_I32(s3_1, 0xd4, READ_I32((void *)(intptr_t)v0_9, 0xc));
    return 0;

label_getframe_log:
    imp_log_fun(6, v0_16, 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", v1_6,
        "IMP_DMIC_GetFrame", var_3c, "IMP_DMIC_GetFrame");
    return -1;
}

int IMP_DMIC_EnableAecRefFrame(int devNum, int chnNum, int audioDevId, int aiChn)
{
    void *dmicDev = dmic_base();
    const char *var_2c_1;
    int32_t v0_8;
    int32_t v1_7;
    int32_t a0;
    int32_t v0_4;
    void *a0_1;
    int32_t a2_1;
    int32_t v0_5;
    void *s3_1;
    int32_t var_24_1;
    int32_t v0_10;
    const char *v1_8;
    int32_t a0_2;

    (void)audioDevId;
    (void)aiChn;

    imp_log_fun(4, IMP_Log_Get_Option(), 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x43d,
        "IMP_DMIC_EnableAecRefFrame", "DMIC_EnableAecRefFrame.\n");
    if (devNum > 0) {
        v0_8 = IMP_Log_Get_Option();
        var_2c_1 = "fun:%s, Invalid dmic device ID.\n";
        v1_7 = 0x440;
        goto label_enableref_log;
    }

    a0 = devNum << 5;
    if (chnNum > 0) {
        v0_8 = IMP_Log_Get_Option(a0);
        var_2c_1 = "fun:%s, Invalid dmic channel ID.\n";
        v1_7 = 0x444;
        goto label_enableref_log;
    }

    v0_4 = (a0 - devNum * 3) << 4;
    a0_1 = (uint8_t *)dmicDev + v0_4;
    a2_1 = chnNum << 4;
    if (READ_I32(a0_1, 0x4) == 0) {
        v0_8 = IMP_Log_Get_Option(a0_1, dmicDev, a2_1);
        var_2c_1 = "fun:%s, dmic device is no enabled.\n";
        v1_7 = 0x449;
        goto label_enableref_log;
    }

    v0_5 = (((a2_1 - chnNum * 5) << 5) + v0_4);
    s3_1 = (uint8_t *)dmicDev + v0_5;
    if (READ_I32(s3_1, 0x6c) == 0) {
        v0_8 = IMP_Log_Get_Option();
        var_2c_1 = "fun:%s, dmic channel is no enabled.\n";
        v1_7 = 0x44d;
        goto label_enableref_log;
    }

    if (READ_I32(s3_1, 0x1bc) == 1) {
        v0_10 = IMP_Log_Get_Option();
        a0_2 = 0x455;
        var_24_1 = 0x455;
        v1_8 = "error:(%s,%d),internal AEC has enabled. . AEC and Ref can not enable both.\n";
        goto label_enableref_log2;
    }

    if (_dmic_ref_enable(a0_1, (uint8_t *)dmicDev + v0_5 + 0x68) == 0) {
        WRITE_I32(s3_1, 0x1b0, 1);
        return 0;
    }

    v0_10 = IMP_Log_Get_Option();
    a0_2 = 0x45b;
    var_24_1 = 0x45b;
    v1_8 = "error:(%s,%d),dmic_ref_enable error.\n";

label_enableref_log2:
    imp_log_fun(6, v0_10, 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", a0_2,
        "IMP_DMIC_EnableAecRefFrame", v1_8,
        "IMP_DMIC_EnableAecRefFrame", var_24_1, &_gp);
    return -1;

label_enableref_log:
    imp_log_fun(6, v0_8, 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", v1_7,
        "IMP_DMIC_EnableAecRefFrame", var_2c_1, "IMP_DMIC_EnableAecRefFrame");
    return -1;
}

int IMP_DMIC_DisableAecRefFrame(int devNum, int chnNum, int audioDevId, int aiChn)
{
    void *dmicDev = dmic_base();
    const char *var_2c = "AI_DisableAec";
    const char *var_68 = "IMP_DMIC_DisableAecRefFrame";
    const char *var_40 = "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c";
    const char *var_70 = "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c";
    const char *var_64 = "DMIC_DisableAecRefFrame.\n";
    int32_t var_6c = 0x469;
    const char *var_64_1;
    int32_t v0_40;
    int32_t v1_18;
    int32_t v0_1;
    int32_t v1;
    int32_t s0_3;
    void *s6_1;
    int32_t v0_3;
    int32_t v1_1;
    int32_t s2_4;
    void *s7_1;
    int32_t v0_6;
    const char *var_60;
    int32_t var_5c;
    const char *a1_4;
    int32_t a2_2;
    int32_t a3_1;
    int32_t s1_1;
    struct timeval tv;
    struct timespec ts;
    int32_t i;
    void *a0_12;
    void *s1;

    (void)audioDevId;
    (void)aiChn;

    imp_log_fun(4, IMP_Log_Get_Option(), 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x469,
        "IMP_DMIC_DisableAecRefFrame", "DMIC_DisableAecRefFrame.\n");
    if (devNum > 0) {
        v0_40 = IMP_Log_Get_Option();
        var_64_1 = "fun:%s, Invalid dmic device ID.\n";
        v1_18 = 0x46c;
        goto label_disableref_log;
    }

    if (chnNum > 0) {
        v0_40 = IMP_Log_Get_Option();
        var_64_1 = "fun:%s, Invalid dmic channel ID.\n";
        v1_18 = 0x470;
        goto label_disableref_log;
    }

    v0_1 = devNum << 2;
    v1 = devNum << 5;
    s0_3 = (v1 - v0_1 + devNum) << 4;
    s6_1 = (uint8_t *)dmicDev + s0_3;
    if (READ_I32(s6_1, 0x4) == 0) {
        v0_40 = IMP_Log_Get_Option();
        var_64_1 = "fun:%s, dmic device is no enabled.\n";
        v1_18 = 0x475;
        goto label_disableref_log;
    }

    v0_3 = chnNum << 2;
    v1_1 = chnNum << 4;
    s2_4 = (((v1_1 - v0_3 - chnNum) << 5) + s0_3);
    s7_1 = (uint8_t *)dmicDev + s2_4;
    if (READ_I32(s7_1, 0x6c) == 0) {
        v0_40 = IMP_Log_Get_Option();
        var_64_1 = "fun:%s, dmic channel is no enabled.\n";
        v1_18 = 0x479;
        goto label_disableref_log;
    }

    if (READ_I32(s7_1, 0x1b0) == 0) {
        v0_40 = IMP_Log_Get_Option();
        var_64_1 = "fun:%s, dmic ref is not enaled.\n";
        v1_18 = 0x480;
        goto label_disableref_log;
    }

    v0_6 = pthread_mutex_lock((pthread_mutex_t *)((uint8_t *)dmicDev + s0_3 + 0x34));
    if (v0_6 != 0) {
        printf("err: %s,%d ret= %d\n", "IMP_DMIC_DisableAecRefFrame", 0x484,
            pthread_mutex_lock((pthread_mutex_t *)((uint8_t *)dmicDev + s0_3 + 0x34)));
        return v0_6;
    }

    if (_dmic_ref_disable(s6_1, (uint8_t *)dmicDev + s2_4 + 0x68) != 0) {
        var_5c = 0x487;
        var_6c = 0x487;
        var_64 = "%s,%d, _dmic_ref disable error.\n";
        var_70 = var_40;
        var_60 = "IMP_DMIC_DisableAecRefFrame";
        var_68 = "IMP_DMIC_DisableAecRefFrame";
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic", var_70, 0x487,
            "IMP_DMIC_DisableAecRefFrame", "%s,%d, _dmic_ref disable error.\n",
            "IMP_DMIC_DisableAecRefFrame", 0x487, &_gp);
        s1_1 = pthread_mutex_unlock((pthread_mutex_t *)((uint8_t *)dmicDev + s0_3 + 0x34));
        if (s1_1 == 0) {
            return -1;
        }

        a3_1 = pthread_mutex_unlock((pthread_mutex_t *)((uint8_t *)dmicDev + s0_3 + 0x34));
        a2_2 = 0x488;
        a1_4 = "IMP_DMIC_DisableAecRefFrame";
        printf("err: %s,%d ret= %d\n", a1_4, a2_2, a3_1, var_70, var_6c, var_68,
            var_64, var_60, var_5c, &_gp);
        return s1_1;
    }

    gettimeofday(&tv, 0);
    ts.tv_sec = tv.tv_sec + 5;
    ts.tv_nsec = tv.tv_usec * 1000;
    if (pthread_kill(*(pthread_t *)((uint8_t *)s6_1 + 0x14), 0) != 0) {
        var_60 = "_dmic_chn_disable_ref_wait";
        var_68 = "_dmic_chn_disable_ref_wait";
        var_6c = 0xc0;
        var_64 = "fun:%s,disable ref dmic thread has exit!.\n";
        var_70 = var_40;
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "dmic", var_70, 0xc0,
            "_dmic_chn_disable_ref_wait", "fun:%s,disable ref dmic thread has exit!.\n",
            "_dmic_chn_disable_ref_wait");
        WRITE_I32(s7_1, 0x6c, 0);
    }

    WRITE_I32(s7_1, 0x1b0, 0);
    WRITE_I32(s7_1, 0x118, 0);
    i = 0;
    while (i == 0) {
        if (pthread_cond_timedwait((pthread_cond_t *)((uint8_t *)dmicDev + s2_4 + 0x150),
                (pthread_mutex_t *)((uint8_t *)dmicDev + s0_3 + 0x34), &ts) == 0x91) {
            var_70 = var_40;
            var_64 = "dmic_chn_disable_ref_wait timeout\n";
            var_68 = "_dmic_chn_disable_ref_wait";
            var_6c = 0xc9;
            imp_log_fun(5, IMP_Log_Get_Option(0x91), 2, "dmic", var_70, 0xc9,
                "_dmic_chn_disable_ref_wait", "dmic_chn_disable_ref_wait timeout\n");
            i = READ_I32((uint8_t *)dmicDev + s2_4, 0x118);
        } else {
            i = READ_I32((uint8_t *)dmicDev + s2_4, 0x118);
        }
    }

    WRITE_I32((uint8_t *)dmicDev + (((v1_1 - v0_3 - chnNum) << 5) + ((v1 - v0_1 + devNum) << 4)), 0x118, 0);
    a0_12 = READ_PTR(dmicDev, (((v1_1 - v0_3 - chnNum) << 5) + ((v1 - v0_1 + devNum) << 4) + 0x1b4));
    if (a0_12 != 0) {
        free(a0_12);
    }

    s1 = (uint8_t *)dmicDev + (((v1_1 - v0_3 - chnNum) << 5) + ((v1 - v0_1 + devNum) << 4));
    WRITE_PTR(s1, 0x1b4, 0);
    WRITE_I32(s1, 0x1b0, 0);
    s1_1 = pthread_mutex_unlock((pthread_mutex_t *)((uint8_t *)dmicDev + s0_3 + 0x34));
    if (s1_1 == 0) {
        return 0;
    }

    a3_1 = pthread_mutex_unlock((pthread_mutex_t *)((uint8_t *)dmicDev + s0_3 + 0x34));
    a2_2 = 0x490;
    a1_4 = &var_2c[0x23e8];
    printf("err: %s,%d ret= %d\n", a1_4, a2_2, a3_1, var_70, var_6c, var_68,
        var_64, var_60, var_5c, &_gp);
    return s1_1;

label_disableref_log:
    imp_log_fun(6, v0_40, 2, "dmic", var_40, v1_18,
        "IMP_DMIC_DisableAecRefFrame", var_64_1, "IMP_DMIC_DisableAecRefFrame");
    return -1;
}

int IMP_DMIC_GetFrameAndRef(int devNum, int chnNum, IMPAudioFrame *frame,
    IMPAudioFrame *ref, IMPBlock block)
{
    void *dmicDev = dmic_base();
    const char *var_50;
    const char *var_4c;
    const char *var_48;
    int32_t v0_26;
    int32_t v1_3;
    int32_t a0;
    int32_t s5_1;
    int32_t s3_1;
    int32_t a1_1;
    int32_t a1_3;
    int32_t s3_2;
    void *s2_1;
    int32_t v0_8;
    int32_t s5_2;
    void *s0_2;
    int32_t s6_1;
    int32_t v0_10;
    int32_t v0_21;
    int32_t v1_1;
    int32_t v0_22;

    imp_log_fun(4, IMP_Log_Get_Option(), 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x49e,
        "IMP_DMIC_GetFrameAndRef", "DMIC_GetFrameAndRef\n");
    if (devNum > 0) {
        v0_26 = IMP_Log_Get_Option();
        var_48 = "IMP_DMIC_GetFrameAndRef";
        var_50 = "IMP_DMIC_GetFrameAndRef";
        var_4c = "fun:%s, Invalid dmic device ID.\n";
        v1_3 = 0x4a1;
        goto label_getframeandref_log;
    }

    a0 = devNum << 2;
    if (chnNum > 0) {
        v0_26 = IMP_Log_Get_Option(a0);
        var_48 = "IMP_DMIC_GetFrameAndRef";
        var_50 = "IMP_DMIC_GetFrameAndRef";
        var_4c = "fun:%s, Invalid dmic channel ID.\n";
        v1_3 = 0x4a5;
        goto label_getframeandref_log;
    }

    s5_1 = devNum * 0x21 - a0;
    s3_1 = s5_1 << 4;
    a1_1 = chnNum << 2;
    if (READ_I32(dmicDev, s3_1 + 4) == 0) {
        v0_26 = IMP_Log_Get_Option(a0, a1_1);
        var_48 = "IMP_DMIC_GetFrameAndRef";
        var_50 = "IMP_DMIC_GetFrameAndRef";
        var_4c = "fun:%s, dmic device is no enabled.\n";
        v1_3 = 0x4aa;
        goto label_getframeandref_log;
    }

    a1_3 = chnNum * 0xf - a1_1;
    s3_2 = (a1_3 << 5) + s3_1;
    s2_1 = (uint8_t *)dmicDev + s3_2;
    if (READ_I32(s2_1, 0x6c) == 0) {
        v0_26 = IMP_Log_Get_Option();
        var_48 = "IMP_DMIC_GetFrameAndRef";
        var_50 = "IMP_DMIC_GetFrameAndRef";
        var_4c = "fun:%s, dmic channel is no enabled.\n";
        v1_3 = 0x4ae;
        goto label_getframeandref_log;
    }

    if (frame == 0 || ref == 0) {
        v0_26 = IMP_Log_Get_Option();
        var_48 = "_dmic_get_frameAndref";
        var_50 = "_dmic_get_frameAndref";
        var_4c = "fun:%s,parameters is invalid.\n";
        v1_3 = 0x1e8;
        goto label_getframeandref_log;
    }

    while (1) {
        _dmic_chn_lock((uint8_t *)dmicDev + s3_2 + 0x68);
        v0_8 = audio_buf_get_node((int32_t *)READ_PTR(s2_1, 0x1c0), 1);
        _dmic_chn_unlock((uint8_t *)dmicDev + s3_2 + 0x68);
        if (v0_8 != 0) {
            break;
        }

        if (block != 0) {
            _dmic_polling_frame_wait((uint8_t *)dmicDev + s3_2 + 0x68);
        } else {
            return -1;
        }
    }

    s5_2 = s5_1 << 4;
    s0_2 = (uint8_t *)dmicDev + s5_2;
    s6_1 = READ_I32(s0_2, 0x28);
    v0_10 = audio_buf_node_get_data((void *)(intptr_t)v0_8);
    WRITE_I32(s2_1, 0xa8, v0_10 + 0x14);
    WRITE_I32(s2_1, 0xbc, READ_I32((void *)(intptr_t)v0_10, 0x10));
    WRITE_I32(s2_1, 0xa0, READ_I32(s0_2, 0x1c));
    WRITE_I32(s2_1, 0xb0, READ_I32((void *)(intptr_t)v0_10, 0x8));
    WRITE_I32(s2_1, 0xb4, READ_I32((void *)(intptr_t)v0_10, 0xc));
    WRITE_I32(s2_1, 0xa4, READ_I32(s0_2, 0x20));
    WRITE_I32(s2_1, 0xac, 0);

    frame->bitwidth = (IMPAudioBitWidth)READ_I32(s2_1, 0xa0);
    frame->soundmode = (IMPAudioSoundMode)READ_I32(s2_1, 0xa4);
    frame->virAddr = (uint32_t *)(uintptr_t)READ_I32(s2_1, 0xa8);
    frame->phyAddr = 0;
    frame->timeStamp = (uint64_t)(uint32_t)READ_I32(s2_1, 0xb0) |
                       ((uint64_t)(uint32_t)READ_I32(s2_1, 0xb4) << 32);
    frame->seq = READ_I32(s2_1, 0xb8);
    frame->len = READ_I32(s2_1, 0xbc);

    v0_21 = ((s6_1 << 1) * audio_buf_node_index((int32_t *)(intptr_t)v0_8)) +
        READ_I32(dmicDev, (a1_3 << 5) + s5_2 + 0x1b4);
    v1_1 = READ_I32((void *)(intptr_t)v0_10, 0x10);
    v0_22 = READ_I32(s0_2, 0x2c);
    if (v0_22 == 0) {
        __builtin_trap();
    }

    ref->bitwidth = (IMPAudioBitWidth)READ_I32(s0_2, 0x1c);
    ref->soundmode = (IMPAudioSoundMode)READ_I32(s0_2, 0x20);
    ref->virAddr = (uint32_t *)(uintptr_t)v0_21;
    ref->phyAddr = 0;
    ref->timeStamp = 0;
    ref->seq = 0;
    ref->len = v1_1 / v0_22;
    return 0;

label_getframeandref_log:
    imp_log_fun(6, v0_26, 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", v1_3,
        var_50, var_4c, var_48);
    return -1;
}

int IMP_DMIC_PollingFrame(int devNum, int chnNum, unsigned int timeout_ms)
{
    void *dmicDev = dmic_base();
    const char *var_1c;
    int32_t v0_6;
    int32_t v1_4;
    int32_t a0_1;
    int32_t a1;

    (void)timeout_ms;

    if (devNum > 0) {
        v0_6 = IMP_Log_Get_Option();
        var_1c = "fun:%s, Invalid dmic device ID.\n";
        v1_4 = 0x4be;
        goto label_poll_log;
    }

    if (chnNum > 0) {
        v0_6 = IMP_Log_Get_Option();
        var_1c = "fun:%s, Invalid dmic channel.\n";
        v1_4 = 0x4c3;
        goto label_poll_log;
    }

    a0_1 = devNum * 0x1d0;
    a1 = chnNum * 0x160 + a0_1;
    if (READ_I32(dmicDev, a0_1 + 4) == 0) {
        v0_6 = IMP_Log_Get_Option((uint8_t *)dmicDev + a1 + 0x68);
        var_1c = "fun:%s, dmic device is no enabled.\n";
        v1_4 = 0x4c8;
        goto label_poll_log;
    }

    if (READ_I32(dmicDev, a1 + 0x6c) != 0) {
        _dmic_polling_frame_wait((uint8_t *)dmicDev + a1 + 0x68);
        return 0;
    }

    v0_6 = IMP_Log_Get_Option((uint8_t *)dmicDev + a1 + 0x68);
    var_1c = "fun:%s, dmic channel is no enabled.\n";
    v1_4 = 0x4cc;

label_poll_log:
    imp_log_fun(6, v0_6, 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", v1_4,
        "IMP_DMIC_PollingFrame", var_1c, "IMP_DMIC_PollingFrame");
    return -1;
}

int IMP_DMIC_ReleaseFrame(int devNum, int chnNum, IMPAudioFrame *frame)
{
    void *dmicDev = dmic_base();
    const char *var_28;
    const char *var_24;
    const char *var_20;
    int32_t v0_7;
    int32_t v1_4;
    int32_t a0_1;
    int32_t a1;
    void *s0_1;
    int32_t node;

    if (devNum > 0) {
        v0_7 = IMP_Log_Get_Option();
        var_20 = "IMP_DMIC_ReleaseFrame";
        var_28 = "IMP_DMIC_ReleaseFrame";
        var_24 = "fun:%s, Invalid dmic device ID.\n";
        v1_4 = 0x4db;
        goto label_release_log;
    }

    if (chnNum > 0) {
        v0_7 = IMP_Log_Get_Option();
        var_20 = "IMP_DMIC_ReleaseFrame";
        var_28 = "IMP_DMIC_ReleaseFrame";
        var_24 = "fun:%s, Invalid dmic channel.\n";
        v1_4 = 0x4e0;
        goto label_release_log;
    }

    a0_1 = devNum * 0x1d0;
    a1 = chnNum * 0x160 + a0_1;
    if (READ_I32(dmicDev, a0_1 + 4) == 0) {
        v0_7 = IMP_Log_Get_Option();
        var_20 = "IMP_DMIC_ReleaseFrame";
        var_28 = "IMP_DMIC_ReleaseFrame";
        var_24 = "fun:%s, dmic device is no enabled.\n";
        v1_4 = 0x4e7;
        goto label_release_log;
    }

    s0_1 = (uint8_t *)dmicDev + a1;
    if (READ_I32(s0_1, 0x6c) == 0) {
        v0_7 = IMP_Log_Get_Option();
        var_20 = "IMP_DMIC_ReleaseFrame";
        var_28 = "IMP_DMIC_ReleaseFrame";
        var_24 = "fun:%s, dmic channel is no enabled.\n";
        v1_4 = 0x4eb;
        goto label_release_log;
    }

    if (frame == 0) {
        v0_7 = IMP_Log_Get_Option();
        var_20 = "_dmic_release_frame";
        var_28 = "_dmic_release_frame";
        var_24 = "fun:%s, parameter is empty.\n";
        v1_4 = 0x20d;
        goto label_release_log;
    }

    _dmic_chn_lock((uint8_t *)dmicDev + a1 + 0x68);
    node = *(int32_t *)((uint8_t *)(uintptr_t)frame->virAddr - 0x14);
    audio_buf_put_node((int32_t *)READ_PTR(s0_1, 0x1c0), node, 0);
    _dmic_chn_unlock((uint8_t *)dmicDev + a1 + 0x68);
    return 0;

label_release_log:
    imp_log_fun(6, v0_7, 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", v1_4,
        var_28, var_24, var_20);
    return -1;
}

int IMP_DMIC_SetVol(int devNum, int chnNum, int vol)
{
    void *dmicDev = dmic_base();
    const char *var_24;
    int32_t v0_10;
    int32_t v1_1;
    int32_t s0;
    void *a0_5;

    if (devNum > 0) {
        v0_10 = IMP_Log_Get_Option();
        var_24 = "fun:%s, Invalid dmic device ID.\n";
        v1_1 = 0x4fb;
        goto label_setvol_log;
    }

    if (chnNum > 0) {
        v0_10 = IMP_Log_Get_Option();
        var_24 = "fun:%s, Invalid dmic channel.\n";
        v1_1 = 0x500;
        goto label_setvol_log;
    }

    s0 = vol;
    imp_log_fun(4, IMP_Log_Get_Option(), 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x503,
        "IMP_DMIC_SetVol", "DMIC Set Vol:%d.\n", s0);
    if (s0 < -30) {
        s0 = -30;
    }
    if (s0 >= 0x79) {
        s0 = 0x78;
    }

    a0_5 = (uint8_t *)dmicDev + chnNum * 0x160 + devNum * 0x1d0;
    WRITE_I32(a0_5, 0x80, -30);
    WRITE_I32(a0_5, 0x84, 0x78);
    WRITE_DBL(a0_5, 0x88, pow(10.0, (double)s0 / 20.0));
    WRITE_I32(a0_5, 0x90, s0);
    return 0;

label_setvol_log:
    imp_log_fun(6, v0_10, 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", v1_1,
        "IMP_DMIC_SetVol", var_24, "IMP_DMIC_SetVol");
    return -1;
}

int IMP_DMIC_GetVol(int devNum, int chnNum, int *vol)
{
    void *dmicDev = dmic_base();
    const char *var_1c_1;
    int32_t v0_5;
    int32_t v1_3;

    if (devNum > 0) {
        v0_5 = IMP_Log_Get_Option();
        var_1c_1 = "fun:%s, Invalid dmic device ID.\n";
        v1_3 = 0x520;
        goto label_getvol_log;
    }

    if (chnNum > 0) {
        v0_5 = IMP_Log_Get_Option();
        var_1c_1 = "fun:%s, Invalid dmic channel.\n";
        v1_3 = 0x525;
        goto label_getvol_log;
    }

    if (vol != 0) {
        *vol = READ_I32(dmicDev, chnNum * 0x160 + devNum * 0x1d0 + 0x90);
        imp_log_fun(4, IMP_Log_Get_Option(), 2, "dmic",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x52f,
            "IMP_DMIC_GetVol", "DMIC Get Vol:%d.\n", *vol);
        return 0;
    }

    v0_5 = IMP_Log_Get_Option();
    var_1c_1 = "fun:%s, dmicVol is NULL.\n";
    v1_3 = 0x52a;

label_getvol_log:
    imp_log_fun(6, v0_5, 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", v1_3,
        "IMP_DMIC_GetVol", var_1c_1, "IMP_DMIC_GetVol");
    return -1;
}

int IMP_DMIC_SetGain(int devNum, int chnNum, int gain)
{
    void *dmicDev = dmic_base();
    int32_t var_3c;
    const char *var_34;
    int32_t v0_6;
    int32_t s1_1;
    int32_t s0_2;

    if (devNum > 0) {
        v0_6 = IMP_Log_Get_Option();
        var_34 = "fun:%s, Invalid dmic device ID.\n";
        var_3c = 0x53b;
        goto label_setgain_log;
    }

    if (chnNum > 0) {
        v0_6 = IMP_Log_Get_Option();
        var_34 = "fun:%s, Invalid dmic channel.\n";
        var_3c = 0x540;
        goto label_setgain_log;
    }

    s1_1 = gain;
    imp_log_fun(4, IMP_Log_Get_Option(), 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x544,
        "IMP_DMIC_SetGain", "DMIC Set Gain:%d.\n", s1_1);
    if (s1_1 < 0) {
        s1_1 = 0;
    }
    if (s1_1 >= 0x20) {
        s1_1 = 0x1f;
    }

    s0_2 = devNum * 0x1d0;
    if (ioctl(READ_I32(dmicDev, s0_2 + 8), 0x200, s1_1) >= 0) {
        WRITE_I32(dmicDev, chnNum * 0x160 + s0_2 + 0x94, s1_1);
        return 0;
    }

    v0_6 = IMP_Log_Get_Option();
    var_34 = "fun:%s, set dmic gain error.\n";
    var_3c = 0x54e;

label_setgain_log:
    imp_log_fun(6, v0_6, 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", var_3c,
        "IMP_DMIC_SetGain", var_34, "IMP_DMIC_SetGain");
    return -1;
}

int IMP_DMIC_GetGain(int devNum, int chnNum, int *gain)
{
    void *dmicDev = dmic_base();
    const char *var_1c_1;
    int32_t v0_5;
    int32_t v1_3;

    if (devNum > 0) {
        v0_5 = IMP_Log_Get_Option();
        var_1c_1 = "fun:%s, Invalid dmic device ID.\n";
        v1_3 = 0x55b;
        goto label_getgain_log;
    }

    if (chnNum > 0) {
        v0_5 = IMP_Log_Get_Option();
        var_1c_1 = "fun:%s, Invalid dmic channel.\n";
        v1_3 = 0x560;
        goto label_getgain_log;
    }

    if (gain != 0) {
        *gain = READ_I32(dmicDev, chnNum * 0x160 + devNum * 0x1d0 + 0x94);
        imp_log_fun(4, IMP_Log_Get_Option(), 2, "dmic",
            "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", 0x56a,
            "IMP_DMIC_GetGain", "DMIC Get Gain:%d.\n", *gain);
        return 0;
    }

    v0_5 = IMP_Log_Get_Option();
    var_1c_1 = "fun:%s, dmicGain is NULL.\n";
    v1_3 = 0x564;

label_getgain_log:
    imp_log_fun(6, v0_5, 2, "dmic",
        "/home/user/git/proj/sdk-lv3/src/imp/audio/dmic.c", v1_3,
        "IMP_DMIC_GetGain", var_1c_1, "IMP_DMIC_GetGain");
    return -1;
}
