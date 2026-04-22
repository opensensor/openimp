#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int32_t IMP_Log_Get_Option(void); /* forward decl, ported by T<N> later */
int32_t imp_log_fun(int32_t level, int32_t option, int32_t type, ...); /* forward decl, ported by T<N> later */

static void *g_lib_audioProcess_handle_storage;
static void **g_lib_audioProcess_handle = &g_lib_audioProcess_handle_storage;

void *load_audioProcess_library(char **arg1, int32_t arg2, char *arg3, char *arg4);
int32_t free_audioProcess_library(void *arg1);

uint32_t _audio_set_volume(int16_t *arg1, uint8_t *arg2, int32_t arg3,
                           int32_t arg4, double arg5, double arg6,
                           double arg7) {
    uint32_t v0 = 0;

    if (arg4 == 8) {
        v0 = (uint32_t)(uintptr_t)"/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/FbcMapSize.c";
        if (arg3 != 0) {
            int16_t *a3_4 = (int16_t *)((char *)arg1 + arg3);

            do {
                arg5 = (double)(float)arg5 * arg6;
                if (!(arg7 <= arg5)) {
                    v0 = (uint32_t)((int32_t)arg5);
                } else {
                    arg5 = arg5 - arg7;
                    v0 = (uint32_t)((int32_t)arg5) | 0x80000000U;
                }
                arg1 += 1;
                *arg2 = (uint8_t)v0;
                arg2 = &arg2[1];
            } while (a3_4 != arg1);
        }
    } else {
        if (arg4 != 0x10) {
            return (uint32_t)imp_log_fun(
                6, IMP_Log_Get_Option(), 2, "audio_common",
                "/home/user/git/proj/sdk-lv3/src/imp/audio/audio_common.c", 0x5d,
                "_audio_set_volume", "error pcmbit \n", NULL);
        }

        v0 = (uint32_t)arg3 >> 0x1f;
        {
            int32_t a3_1 = ((int32_t)v0 + arg3) >> 1;

            if (a3_1 != 0) {
                uint16_t v0_3 = 0;

                do {
                    int32_t v0_1 = (int32_t)(*arg1);
                    uint16_t v1_1 = 0x7fff;
                    uint32_t v0_2;

                    arg1 = &arg1[1];
                    arg5 = (double)(float)v0_1 * arg6;
                    v0_2 = (uint32_t)((int32_t)arg5);
                    v0_3 = (uint16_t)v0_2;
                    if ((int32_t)v0_2 < 0x8000) {
                        if ((int32_t)v0_2 < -0x7fff) {
                            v0_3 = (uint16_t)-0x7fff;
                        }
                        v1_1 = v0_3;
                    }
                    *(uint16_t *)arg2 = v1_1;
                    arg2 = &arg2[2];
                } while (&arg1[a3_1] != arg1);

                return v0_3;
            }
        }
    }

    return v0;
}

int32_t init_audioProcess_library(char *arg1) {
    char *default_lib_path[2];
    char *v0 = getenv("LD_LIBRARY_PATH");

    default_lib_path[0] = "/usr/lib";
    default_lib_path[1] = "/lib";
    if (*g_lib_audioProcess_handle == NULL) {
        void *v0_2 = load_audioProcess_library(default_lib_path, 2, arg1, v0);

        *g_lib_audioProcess_handle = v0_2;
        if (v0_2 == NULL) {
            puts("Usage:please check file:libaudioProcess.so is in path of /lib or /usr/lib");
            puts("If you put file:libaudioProcess.so in your defined path:");
            puts("  you need to set the enviroment variable LD_LIBRARY_PATH");
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "audio_common",
                        "/home/user/git/proj/sdk-lv3/src/imp/audio/audio_common.c",
                        0x6c, "init_audioProcess_library",
                        "fun:%s,load shared library failed\n",
                        "init_audioProcess_library");
            return -1;
        }
    }

    return 0;
}

int32_t deinit_audioProcess_library(void) {
    int32_t result = free_audioProcess_library(*g_lib_audioProcess_handle);

    if (result != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "audio_common",
                    "/home/user/git/proj/sdk-lv3/src/imp/audio/audio_common.c",
                    0x78, "deinit_audioProcess_library",
                    "fun:%s,free audioProcess library failed\n",
                    "deinit_audioProcess_library");
    }
    *g_lib_audioProcess_handle = NULL;
    return result;
}

int32_t Hpf_Version(char *arg1, char arg2) {
    char var_28[0x1f];

    __builtin_strncpy(var_28, "Ingenic High Pass Filter 1.1.0", 0x1f);
    if (arg1 == NULL || (int32_t)arg2 < 0x1f) {
        return -1;
    }

    strncpy(arg1, var_28, 0x1f);
    return 0;
}

int32_t Hpf_gen_filter_coefficients(int16_t *arg1, uint32_t arg2, uint32_t arg3,
                                    int32_t arg4, int32_t arg5, double arg6,
                                    double arg7) {
    float a2_1;
    int32_t a1_2;
    int16_t v1_1;
    int16_t result;

    (void)arg4;
    (void)arg5;

    arg6 = tan(((double)arg3 * 3.141592653589793) / (double)arg2);
    arg7 = (double)(float)arg6 * (double)(float)arg6;
    arg6 = 1.0 / (arg7 + (1.4142135623730951 * (double)(float)arg6) + 1.0);
    a2_1 = (float)((double)4096.0f * arg6);
    a1_2 = (int32_t)a2_1;
    v1_1 = (int16_t)(4096.0f * (float)((2.0 - (2.0 * arg7)) * arg6));
    result = (int16_t)(4096.0f * (float)((((arg7 - (1.4142135623730951 *
                                                    (double)(float)tan(((double)arg3 * 3.141592653589793) /
                                                                       (double)arg2))) +
                                           1.0) *
                                          -1.0) *
                                         arg6));
    *arg1 = (int16_t)a1_2;
    arg1[1] = (int16_t)(-(a1_2) << 1);
    arg1[2] = (int16_t)a1_2;
    arg1[3] = v1_1;
    arg1[4] = result;
    return result;
}

int32_t file_is_exist(char *arg1) {
    if (arg1 != NULL && *arg1 != '\0') {
        return access(arg1, 0) == 0 ? 0 : -1;
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "AudioProcess",
                "/home/user/git/proj/sdk-lv3/src/imp/audio/audio_process.c", 0x18,
                "file_is_exist", "fun:%s,strFilePath is empty\n", "file_is_exist");
    return -1;
}

void *open_shared_library(char *arg1) {
    void *result;

    if (arg1 == NULL) {
        result = NULL;
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "AudioProcess",
                    "/home/user/git/proj/sdk-lv3/src/imp/audio/audio_process.c",
                    0x24, "open_shared_library",
                    "fun:%s, lib file path  is empty\n", "open_shared_library");
    } else {
        void *result_1 = dlopen(arg1, 1);

        result = result_1;
        if (result_1 == NULL) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "AudioProcess",
                        "/home/user/git/proj/sdk-lv3/src/imp/audio/audio_process.c",
                        0x2b, "open_shared_library",
                        "fun:%s dlopen failed.\n", "open_shared_library");
        }
    }

    return result;
}

void *load_audioProcess_library(char **arg1, int32_t arg2, char *arg3, char *arg4) {
    if (arg1 == NULL) {
        return NULL;
    }

    if (arg3 != NULL && arg2 > 0) {
        char **s3_1 = arg1;
        char *str_1 = arg4;

        if (arg4 != NULL) {
            size_t s5_2 = strlen(arg4) + strlen(arg3) + 2;
            char *str = calloc(s5_2, 1);

            if (str == NULL) {
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "AudioProcess",
                            "/home/user/git/proj/sdk-lv3/src/imp/audio/audio_process.c",
                            0x44, "user_defined_lib_path",
                            "fun:%s,malloc space failed\n",
                            "user_defined_lib_path");
            } else if (*str_1 == '\0') {
label_cd29c:
                free(str);
            } else {
                while (1) {
                    char *v0_8 = strchr(str_1, 0x3a);

                    if (v0_8 != NULL) {
                        void *str_3 = str_1;
                        size_t n_1 = (size_t)(v0_8 - str_1);

                        memcpy(str, str_3, n_1);
                        str_1 = &v0_8[1];
                        {
                            size_t v0_5 = strlen(str);

                            str[v0_5] = 0x2f;
                            strcpy(&str[v0_5 + 1], arg3);
                        }
                        if (file_is_exist(str) != 0) {
                            memset(str, 0, s5_2);
                            if (v0_8[1] == '\0') {
                                goto label_cd29c;
                            }
                            continue;
                        }
                    } else {
                        memcpy(str, str_1, strlen(str_1));
                        {
                            size_t v0_10 = strlen(str);

                            str[v0_10] = 0x2f;
                            strcpy(&str[v0_10 + 1], arg3);
                        }
                        if (file_is_exist(str) != 0) {
                            goto label_cd29c;
                        }
                    }

                    {
                        void *v0_22 = open_shared_library(str);

                        free(str);
                        if (v0_22 != NULL) {
                            return v0_22;
                        }
                    }
                    break;
                }
            }
        }

        {
            int32_t s4_1 = 0;

            while (1) {
                char *str_2 = *s3_1;

                if (str_2 == NULL) {
                    imp_log_fun(6, IMP_Log_Get_Option(), 2, "AudioProcess",
                                "/home/user/git/proj/sdk-lv3/src/imp/audio/audio_process.c",
                                0x6c, "default_lib_path",
                                "fun:%s,defaultDir or libFileName is empty\n",
                                "default_lib_path");
                } else {
                    size_t n = strlen(str_2);
                    size_t v0_14 = strlen(arg3);
                    char *v0_15 = calloc(n + v0_14 + 2, 1);

                    if (v0_15 == NULL) {
                        imp_log_fun(6, IMP_Log_Get_Option(), 2, "AudioProcess",
                                    "/home/user/git/proj/sdk-lv3/src/imp/audio/audio_process.c",
                                    0x77, "default_lib_path",
                                    "fun:%s,malloc tmpBuf failed\n",
                                    "default_lib_path");
                    } else {
                        memcpy(v0_15, str_2, n);
                        v0_15[n] = 0x2f;
                        memcpy(v0_15 + n + 1, arg3, v0_14 + 1);
                        if (file_is_exist(v0_15) == 0) {
                            void *v0_13 = open_shared_library(v0_15);

                            free(v0_15);
                            if (v0_13 != NULL) {
                                return v0_13;
                            }
                        } else {
                            s4_1 += 1;
                            s3_1 = &s3_1[1];
                            imp_log_fun(4, IMP_Log_Get_Option(), 2, "AudioProcess",
                                        "/home/user/git/proj/sdk-lv3/src/imp/audio/audio_process.c",
                                        0x80, "default_lib_path",
                                        "file:%s is not exist\n", v0_15);
                            if (arg2 == s4_1) {
                                break;
                            }
                            continue;
                        }
                    }
                }

                s4_1 += 1;
                s3_1 = &s3_1[1];
                if (arg2 == s4_1) {
                    break;
                }
            }
        }
    }

    return NULL;
}

int32_t get_fun_address(void *arg1, void **arg2, int32_t arg3) {
    if (arg1 != NULL && arg2 != NULL) {
        void **s0_1 = arg2;
        int32_t s1_1 = 0;

        if (arg3 > 0) {
            do {
                *((void **)s0_1[1]) = dlsym(arg1, (const char *)s0_1[0]);
                s1_1 += 1;
                if (dlerror() != NULL) {
                    imp_log_fun(6, IMP_Log_Get_Option(), 2, "AudioProcess",
                                "/home/user/git/proj/sdk-lv3/src/imp/audio/audio_process.c",
                                0xae, "get_fun_address",
                                "line:%d,funName:%s dlsym failed\n", 0xae, s0_1[0],
                                NULL);
                    return -1;
                }
                s0_1 = &s0_1[2];
            } while (arg3 != s1_1);

            return 0;
        }
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "AudioProcess",
                "/home/user/git/proj/sdk-lv3/src/imp/audio/audio_process.c", 0xa7,
                "get_fun_address",
                "fun:%s,handle or funArr or num is not right value\n",
                "get_fun_address");
    return -1;
}

int32_t free_audioProcess_library(void *arg1) {
    if (arg1 == NULL) {
        return 0;
    }

    return dlclose(arg1) == 0 ? 0 : -1;
}
