#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "core/globals.h"
#include "imp/imp_isp.h"

int IMP_Log_Get_Option(void);
void imp_log_fun(int level, int option, int type, ...);

typedef struct ISPDevice {
    char dev_name[0x20];
    int32_t fd;
    uint32_t opened;
    uint8_t unk_28[0x50];
    char tuning_path[0x20];
    int32_t tuning_fd;
    void *tuning;
    int32_t mem_fd;
    void *isp_base;
    int32_t tuning_state;
    uint8_t unk_ac[8];
    int32_t wdr_mode;
} ISPDevice;

static char *bpath;
static uint8_t custom_contrast;
static uint8_t custom_sharpness;
static uint32_t global_mode;

int IMP_ISP_Tuning_SetContrast_internal(uint32_t arg1, int32_t arg2);
int IMP_ISP_Tuning_SetSharpness_internal(uint32_t arg1, int32_t arg2);

int IMP_ISP_Open(void)
{
    int32_t result = 0;

    if (gISP == NULL) {
        void *v0_2 = calloc(0xe0, 1);
        gISP = v0_2;
        if (v0_2 == NULL) {
            result = -1;
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
                "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x135,
                "IMP_ISP_Open", "Failed to alloc gISPdev!\n");
            return result;
        }

        __builtin_strcpy((char *)v0_2, "/dev/tx-isp");
        ((ISPDevice *)v0_2)->fd = open((char *)v0_2, 0x80002, 0);
        if (((ISPDevice *)gISP)->fd < 0) {
            result = -1;
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
                "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x145,
                "IMP_ISP_Open", "Cannot open %s\n", gISP);
            return result;
        }

        ((ISPDevice *)gISP)->opened = 1;
        imp_log_fun(3, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x14b,
            "IMP_ISP_Open", "~~~~~~ %s[%d] ~~~~~~~\n", "IMP_ISP_Open", 0x14b);
    }

    return result;
}

int IMP_ISP_Close(void)
{
    ISPDevice *gISP_1 = gISP;

    if (gISP_1 == NULL) {
        return 0;
    }

    if (gISP_1->opened >= 2) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x158,
            "IMP_ISP_Close",
            "Failed to close, because sensor has been deleted!");
        return -1;
    }

    close(gISP_1->fd);
    free(gISP);
    gISP = NULL;
    return 0;
}

int IMP_ISP_SetDefaultBinPath(const char *arg1)
{
    ISPDevice *gISP_1 = gISP;
    int32_t var_1c_1;
    int32_t v0_6;
    const char *a0;
    int32_t a1_2;

    if (gISP_1 == NULL || arg1 == NULL) {
        v0_6 = IMP_Log_Get_Option();
        a1_2 = 0x168;
        var_1c_1 = 0x168;
        a0 = "[ %s:%d ] ISPDEV cannot open\n";
        imp_log_fun(6, v0_6, 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", a1_2,
            "IMP_ISP_SetDefaultBinPath", a0,
            "IMP_ISP_SetDefaultBinPath", var_1c_1);
        return -1;
    }

    if (gISP_1->opened >= 2) {
        v0_6 = IMP_Log_Get_Option();
        a1_2 = 0x16d;
        var_1c_1 = 0x16d;
        a0 = "[ %s:%d ] sensor is runing, please call 'emuisp_disablesensor' firstly\n";
        imp_log_fun(6, v0_6, 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", a1_2,
            "IMP_ISP_SetDefaultBinPath", a0,
            "IMP_ISP_SetDefaultBinPath", var_1c_1);
        return -1;
    }

    if (strlen(arg1) < 0x40) {
        char *bpath_1 = bpath;

        if (bpath_1 == NULL) {
            char *bpath_2 = malloc(0x40);
            bpath = bpath_2;
            bpath_1 = bpath_2;
        }

        sprintf(bpath_1, arg1);
        imp_log_fun(4, IMP_Log_Get_Option(), 2,
            "Bin file path set successfully.new bin path:%s\n",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x179,
            "IMP_ISP_SetDefaultBinPath", bpath);
        return 0;
    }

    v0_6 = IMP_Log_Get_Option();
    a1_2 = 0x172;
    var_1c_1 = 0x172;
    a0 = "[ %s:%d ] path length exceeds upper limit!!!\n";
    imp_log_fun(6, v0_6, 2, "IMP-ISP",
        "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", a1_2,
        "IMP_ISP_SetDefaultBinPath", a0,
        "IMP_ISP_SetDefaultBinPath", var_1c_1);
    return -1;
}

int IMP_ISP_GetDefaultBinPath(char *arg1)
{
    ISPDevice *gISP_1 = gISP;
    int32_t result;

    if (gISP_1 == NULL || arg1 == NULL) {
        result = -1;
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x185,
            "IMP_ISP_GetDefaultBinPath", "[ %s:%d ] ISPDEV cannot open\n",
            "IMP_ISP_GetDefaultBinPath", 0x185);
        return result;
    }

    if (gISP_1->opened == 0) {
        result = -1;
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x18a,
            "IMP_ISP_GetDefaultBinPath",
            "Sensor is runing, please Call 'EmuISP_DisableSensor' firstly\n");
        return result;
    }

    {
        uint32_t var_50[16];
        int32_t result_1 = ioctl(gISP_1->fd, 0xc00456c8, var_50);

        result = result_1;
        if (result_1 != 0) {
            return -1;
        }

        memcpy(arg1, var_50, sizeof(var_50));
    }

    imp_log_fun(4, IMP_Log_Get_Option(), 2,
        "Bin file path get successfully.bin path:%s",
        "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x191,
        "IMP_ISP_GetDefaultBinPath", arg1);
    return result;
}

int IMP_ISP_WDR_ENABLE(int arg1)
{
    ISPDevice *gISP_1 = gISP;
    const char *var_1c_1;
    int32_t v0_3;
    int32_t v1_1;

    if (gISP_1 == NULL) {
        v0_3 = IMP_Log_Get_Option();
        var_1c_1 = "ISPDEV cannot open\n";
        v1_1 = 0x215;
        imp_log_fun(6, v0_3, 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", v1_1,
            "IMP_ISP_WDR_ENABLE", var_1c_1);
        return -1;
    }

    if (arg1 == 1) {
        int32_t v0_2 = ioctl(gISP_1->fd, 0x800456d8, 0);

        if (v0_2 == 0) {
            gISP_1->wdr_mode = arg1;
            return v0_2;
        }

        v0_3 = IMP_Log_Get_Option();
        var_1c_1 = "VIDIOC_SET_WDR_ENABLE() error!\n";
        v1_1 = 0x21b;
        imp_log_fun(6, v0_3, 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", v1_1,
            "IMP_ISP_WDR_ENABLE", var_1c_1);
        return -1;
    }

    if (arg1 != 0) {
        return 0;
    }

    {
        int32_t v0_1 = ioctl(gISP_1->fd, 0x800456d9, 0);

        if (v0_1 == 0) {
            gISP_1->wdr_mode = 0;
            return v0_1;
        }

        v0_3 = IMP_Log_Get_Option();
        var_1c_1 = "VIDIOC_SET_WDR_DISABLE() error!\n";
        v1_1 = 0x221;
        imp_log_fun(6, v0_3, 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", v1_1,
            "IMP_ISP_WDR_ENABLE", var_1c_1);
        return -1;
    }
}

int IMP_ISP_WDR_ENABLE_Get(int *arg1)
{
    int32_t v0_1 = ((ISPDevice *)gISP)->wdr_mode;

    if (v0_1 == 0) {
        *arg1 = 0;
        return 0;
    }

    if (v0_1 == 1) {
        *arg1 = v0_1;
        return 0;
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
        "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x233,
        "IMP_ISP_WDR_ENABLE_Get", "Can not support this wdr mode!!!\n");
    return 0;
}

int IMP_ISP_SetSensorRegister(uint32_t arg1, uint32_t arg2)
{
    ISPDevice *gISP_1 = gISP;
    const char *var_4c;
    int32_t v0_1;
    int32_t v1_5;

    if (gISP_1 == NULL) {
        v0_1 = IMP_Log_Get_Option();
        var_4c = "ISPDEV cannot open\n";
        v1_5 = 0x2d8;
        goto log_error;
    }

    if (gISP_1->opened < 2) {
        v0_1 = IMP_Log_Get_Option();
        var_4c = "Sensor doesn't Run!\n";
        v1_5 = 0x2dd;
        goto log_error;
    }

    if (*(int32_t *)((char *)gISP_1 + 0x48) == 0) {
        v0_1 = IMP_Log_Get_Option();
        var_4c = "There isn't sensor!\n";
        v1_5 = 0x2e2;
        goto log_error;
    }

    if (*(int32_t *)((char *)gISP_1 + 0x48) == 1) {
        struct {
            int32_t sensor_type;
            uint32_t f_04;
            uint32_t f_08;
            uint32_t f_0c;
            uint32_t f_10;
            uint32_t f_14;
            uint32_t f_18;
            uint32_t f_1c;
            uint32_t f_20;
            int32_t reg;
            int32_t zero0;
            int32_t val;
            int32_t zero1;
        } var_40;
        int32_t result;

        var_40.sensor_type = *(int32_t *)((char *)gISP_1 + 0x48);
        var_40.f_04 = *(uint32_t *)((char *)gISP_1 + 0x28);
        var_40.f_08 = *(uint32_t *)((char *)gISP_1 + 0x2c);
        var_40.f_0c = *(uint32_t *)((char *)gISP_1 + 0x30);
        var_40.f_10 = *(uint32_t *)((char *)gISP_1 + 0x34);
        var_40.f_14 = *(uint32_t *)((char *)gISP_1 + 0x38);
        var_40.f_18 = *(uint32_t *)((char *)gISP_1 + 0x3c);
        var_40.f_1c = *(uint32_t *)((char *)gISP_1 + 0x40);
        var_40.f_20 = *(uint32_t *)((char *)gISP_1 + 0x44);
        var_40.reg = (int32_t)arg1;
        var_40.zero0 = 0;
        var_40.val = (int32_t)arg2;
        var_40.zero1 = 0;
        result = ioctl(gISP_1->fd, 0x8038564f, &var_40);
        if (result == 0) {
            return 0;
        }

        puts("sorry,g_register failed!");
        return result;
    }

    v0_1 = IMP_Log_Get_Option();
    var_4c = "Don't support spi sensor!\n";
    v1_5 = 0x2e8;

log_error:
    imp_log_fun(6, v0_1, 2, "IMP-ISP",
        "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", v1_5,
        "IMP_ISP_SetSensorRegister", var_4c);
    return -1;
}

int IMP_ISP_GetSensorRegister(uint32_t arg1, uint32_t *arg2)
{
    ISPDevice *gISP_1 = gISP;
    const char *var_54;
    int32_t v0_1;
    int32_t v1_6;

    if (gISP_1 == NULL) {
        v0_1 = IMP_Log_Get_Option();
        var_54 = "ISPDEV cannot open\n";
        v1_6 = 0x2fc;
        goto log_error;
    }

    if (gISP_1->opened < 2) {
        v0_1 = IMP_Log_Get_Option();
        var_54 = "Sensor doesn't Run!\n";
        v1_6 = 0x301;
        goto log_error;
    }

    if (*(int32_t *)((char *)gISP_1 + 0x48) == 0) {
        v0_1 = IMP_Log_Get_Option();
        var_54 = "There isn't sensor!\n";
        v1_6 = 0x306;
        goto log_error;
    }

    if (*(int32_t *)((char *)gISP_1 + 0x48) == 1) {
        struct {
            int32_t sensor_type;
            uint32_t f_04;
            uint32_t f_08;
            uint32_t f_0c;
            uint32_t f_10;
            uint32_t f_14;
            uint32_t f_18;
            uint32_t f_1c;
            uint32_t f_20;
            int32_t reg;
            int32_t zero0;
            int32_t val;
        } var_48;
        int32_t result;

        var_48.sensor_type = *(int32_t *)((char *)gISP_1 + 0x48);
        var_48.f_04 = *(uint32_t *)((char *)gISP_1 + 0x28);
        var_48.f_08 = *(uint32_t *)((char *)gISP_1 + 0x2c);
        var_48.f_0c = *(uint32_t *)((char *)gISP_1 + 0x30);
        var_48.f_10 = *(uint32_t *)((char *)gISP_1 + 0x34);
        var_48.f_14 = *(uint32_t *)((char *)gISP_1 + 0x38);
        var_48.f_18 = *(uint32_t *)((char *)gISP_1 + 0x3c);
        var_48.f_1c = *(uint32_t *)((char *)gISP_1 + 0x40);
        var_48.f_20 = *(uint32_t *)((char *)gISP_1 + 0x44);
        var_48.reg = (int32_t)arg1;
        var_48.zero0 = 0;
        result = ioctl(gISP_1->fd, 0xc0385650, &var_48);
        if (result != 0) {
            puts("sorry,g_register failed!");
        }

        *arg2 = (uint32_t)var_48.val;
        return result;
    }

    v0_1 = IMP_Log_Get_Option();
    var_54 = "Don't support spi sensor!\n";
    v1_6 = 0x30c;

log_error:
    imp_log_fun(6, v0_1, 2, "IMP-ISP",
        "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", v1_6,
        "IMP_ISP_GetSensorRegister", var_54);
    return -1;
}

int IMP_ISP_Tuning_GetTotalGain(uint32_t *arg1)
{
    ISPDevice *gISP_1 = gISP;

    if (gISP_1 == NULL || gISP_1->tuning == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x550,
            "IMP_ISP_Tuning_GetTotalGain", "get_ispdev error !\n");
        return -1;
    }

    {
        struct {
            int32_t cmd;
            int32_t subcmd;
            int32_t value;
        } var_18 = { 1, 0x8000027, 0 };
        int32_t result = ioctl(gISP_1->tuning_fd, 0xc00c56c6, &var_18);

        if (result == 0) {
            *arg1 = (uint32_t)var_18.value;
        }
        return result;
    }
}

int IMP_ISP_Tuning_SetBrightness(unsigned char arg1)
{
    ISPDevice *gISP_1 = gISP;

    if (gISP_1 != NULL) {
        void *s2_1 = gISP_1->tuning;
        uint32_t s1_1 = (uint32_t)arg1;

        if (s2_1 != NULL) {
            int32_t a0 = gISP_1->tuning_state;

            if (a0 != 2) {
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
                    "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x569,
                    "IMP_ISP_Tuning_SetBrightness",
                    "%s(%d), ispdev->tuning_state is ISPDEV_STATE_RUN\n",
                    "IMP_ISP_Tuning_SetBrightness", 0x569);
                return -1;
            }

            {
                struct {
                    int32_t id;
                    uint32_t value;
                } var_18 = { 0x980900, s1_1 };
                int32_t result = ioctl(gISP_1->tuning_fd, 0xc008561c, &var_18);

                if (result < 0) {
                    imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
                        "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x571,
                        "IMP_ISP_Tuning_SetBrightness",
                        "%s(%d), set VIDIOC_S_CTRL failed\n",
                        "IMP_ISP_Tuning_SetBrightness", 0x571);
                    return result;
                }

                *(uint8_t *)((char *)s2_1 + 8) = (uint8_t)s1_1;
                return result;
            }
        }

        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x565,
            "IMP_ISP_Tuning_SetBrightness", "%s(%d), tuning is NULL\n",
            "IMP_ISP_Tuning_SetBrightness", 0x565);
        return -1;
    }

    return -1;
}

int IMP_ISP_Tuning_GetBrightness(unsigned char *arg1)
{
    ISPDevice *gISP_1 = gISP;

    if (gISP_1 == NULL) {
        return -1;
    }

    {
        void *s1 = gISP_1->tuning;
        int32_t result = -1;

        if (s1 != NULL && arg1 != NULL && gISP_1->tuning_state == 2) {
            struct {
                int32_t id;
                int8_t value;
            } var_18 = { 0x980900, -1 };

            result = ioctl(gISP_1->tuning_fd, 0xc008561b, &var_18);
            if (result == 0) {
                *arg1 = (unsigned char)var_18.value;
                *(uint8_t *)((char *)s1 + 8) = (uint8_t)var_18.value;
            }
            return result;
        }

        return result;
    }
}

int IMP_ISP_Tuning_SetContrast_internal(uint32_t arg1, int32_t arg2)
{
    ISPDevice *gISP_1 = gISP;

    if (gISP_1 != NULL) {
        void *s2_1 = gISP_1->tuning;
        uint32_t custom_contrast_1 = arg1;

        if (s2_1 != NULL) {
            uint32_t v1 = (uint32_t)gISP_1->tuning_state;
            int32_t var_24_1;
            int32_t v0_3;
            const char *a0_1;
            int32_t a1_2;

            if (v1 != 2) {
                v0_3 = IMP_Log_Get_Option();
                a1_2 = 0x59d;
                var_24_1 = 0x59d;
                a0_1 = "%s(%d), ispdev->tuning_state is ISPDEV_STATE_RUN\n";
                imp_log_fun(6, v0_3, 2, "IMP-ISP",
                    "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", a1_2,
                    "IMP_ISP_Tuning_SetContrast_internal", a0_1,
                    "IMP_ISP_Tuning_SetContrast_internal", var_24_1);
                return -1;
            }

            if ((uint32_t)arg2 == v1) {
                uint32_t global_mode_1 = global_mode;

                custom_contrast = (uint8_t)custom_contrast_1;
                if (global_mode_1 == 1) {
                    return 0;
                }
                goto label_9d914;
            }

            if (arg2 == 1) {
                global_mode = (uint32_t)arg2;
            }

            if (arg2 != 0) {
                v0_3 = IMP_Log_Get_Option();
                a1_2 = 0x5ad;
                var_24_1 = 0x5ad;
                a0_1 = "%s(%d), We do not support this mode\n";
                imp_log_fun(6, v0_3, 2, "IMP-ISP",
                    "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", a1_2,
                    "IMP_ISP_Tuning_SetContrast_internal", a0_1,
                    "IMP_ISP_Tuning_SetContrast_internal", var_24_1);
                return -1;
            }

            custom_contrast_1 = (uint32_t)custom_contrast;
            global_mode = v1;

label_9d914:
            {
                struct {
                    int32_t id;
                    uint32_t value;
                } var_18 = { 0x980901, custom_contrast_1 };
                int32_t result = ioctl(gISP_1->tuning_fd, 0xc008561c, &var_18);

                if (result < 0) {
                    imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
                        "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x5b5,
                        "IMP_ISP_Tuning_SetContrast_internal",
                        "%s(%d), set VIDIOC_S_CTRL failed\n",
                        "IMP_ISP_Tuning_SetContrast_internal", 0x5b5);
                    return result;
                }

                *(uint8_t *)((char *)s2_1 + 9) = (uint8_t)custom_contrast_1;
                return result;
            }
        }

        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x599,
            "IMP_ISP_Tuning_SetContrast_internal", "%s(%d), tuning is NULL\n",
            "IMP_ISP_Tuning_SetContrast_internal", 0x599);
        return -1;
    }

    return -1;
}

int IMP_ISP_Tuning_SetContrast(unsigned char arg1)
{
    return IMP_ISP_Tuning_SetContrast_internal((uint32_t)arg1, 2);
}

int IMP_ISP_Tuning_GetContrast(unsigned char *arg1)
{
    ISPDevice *gISP_1 = gISP;

    if (gISP_1 == NULL) {
        return -1;
    }

    {
        void *a1 = gISP_1->tuning;

        if (a1 == NULL || arg1 == NULL || gISP_1->tuning_state != 2) {
            return -1;
        }

        {
            uint8_t custom_contrast_1 = custom_contrast;

            *arg1 = custom_contrast_1;
            *(uint8_t *)((char *)a1 + 9) = custom_contrast_1;
            return 0;
        }
    }
}

int IMP_ISP_Tuning_SetSharpness_internal(uint32_t arg1, int32_t arg2)
{
    ISPDevice *gISP_1 = gISP;

    if (gISP_1 != NULL) {
        void *s2_1 = gISP_1->tuning;
        uint32_t custom_sharpness_1 = arg1;

        if (s2_1 != NULL) {
            uint32_t v1 = (uint32_t)gISP_1->tuning_state;

            if (v1 != 2) {
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
                    "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x5ea,
                    "IMP_ISP_Tuning_SetSharpness_internal",
                    "%s(%d), ispdev->tuning_state is ISPDEV_STATE_RUN\n",
                    "IMP_ISP_Tuning_SetSharpness_internal", 0x5ea);
                return -1;
            }

            if ((uint32_t)arg2 == v1) {
                uint32_t global_mode_1 = global_mode;

                custom_sharpness = (uint8_t)custom_sharpness_1;
                if (global_mode_1 == 1) {
                    return 0;
                }
            } else {
                if (arg2 == 1) {
                    global_mode = (uint32_t)arg2;
                }

                if (arg2 != 0) {
                    puts("We do not support this mode");
                    return -1;
                }

                custom_sharpness_1 = (uint32_t)custom_sharpness;
                global_mode = v1;
            }

            {
                struct {
                    int32_t id;
                    uint32_t value;
                } var_18 = { 0x98091b, custom_sharpness_1 };
                int32_t result = ioctl(gISP_1->tuning_fd, 0xc008561c, &var_18);

                if (result < 0) {
                    imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
                        "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x602,
                        "IMP_ISP_Tuning_SetSharpness_internal",
                        "%s(%d), set VIDIOC_S_CTRL failed\n",
                        "IMP_ISP_Tuning_SetSharpness_internal", 0x602);
                    return result;
                }

                *(uint8_t *)((char *)s2_1 + 0xb) = (uint8_t)custom_sharpness_1;
                return result;
            }
        }

        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x5e6,
            "IMP_ISP_Tuning_SetSharpness_internal", "%s(%d), tuning is NULL\n",
            "IMP_ISP_Tuning_SetSharpness_internal", 0x5e6);
        return -1;
    }

    return -1;
}

int IMP_ISP_Tuning_SetSharpness(unsigned char arg1)
{
    return IMP_ISP_Tuning_SetSharpness_internal((uint32_t)arg1, 2);
}

int IMP_ISP_Tuning_GetSharpness(unsigned char *arg1)
{
    ISPDevice *gISP_1 = gISP;

    if (gISP_1 == NULL) {
        return -1;
    }

    {
        void *a1 = gISP_1->tuning;

        if (a1 == NULL || arg1 == NULL || gISP_1->tuning_state != 2) {
            return -1;
        }

        {
            uint8_t custom_sharpness_1 = custom_sharpness;

            *arg1 = custom_sharpness_1;
            *(uint8_t *)((char *)a1 + 0xb) = custom_sharpness_1;
            return 0;
        }
    }
}

int IMP_ISP_Tuning_SetSaturation(unsigned char arg1)
{
    ISPDevice *gISP_1 = gISP;

    if (gISP_1 != NULL) {
        void *s2_1 = gISP_1->tuning;
        uint32_t s1_1 = (uint32_t)arg1;

        if (s2_1 != NULL) {
            int32_t a0 = gISP_1->tuning_state;

            if (a0 != 2) {
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
                    "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x63a,
                    "IMP_ISP_Tuning_SetSaturation",
                    "%s(%d), ispdev->tuning_state is ISPDEV_STATE_RUN\n",
                    "IMP_ISP_Tuning_SetSaturation", 0x63a);
                return -1;
            }

            {
                struct {
                    int32_t id;
                    uint32_t value;
                } var_18 = { 0x980902, s1_1 };
                int32_t result = ioctl(gISP_1->tuning_fd, 0xc008561c, &var_18);

                if (result < 0) {
                    imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
                        "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x642,
                        "IMP_ISP_Tuning_SetSaturation",
                        "%s(%d), set VIDIOC_S_CTRL failed\n",
                        "IMP_ISP_Tuning_SetSaturation", 0x642);
                    return result;
                }

                *(uint8_t *)((char *)s2_1 + 0xa) = (uint8_t)s1_1;
                return result;
            }
        }

        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x636,
            "IMP_ISP_Tuning_SetSaturation", "%s(%d), tuning is NULL\n",
            "IMP_ISP_Tuning_SetSaturation", 0x636);
        return -1;
    }

    return -1;
}

int IMP_ISP_Tuning_GetSaturation(unsigned char *arg1)
{
    ISPDevice *gISP_1 = gISP;

    if (gISP_1 == NULL) {
        return -1;
    }

    {
        void *s1 = gISP_1->tuning;
        int32_t result = -1;

        if (s1 != NULL && arg1 != NULL && gISP_1->tuning_state == 2) {
            struct {
                int32_t id;
                int8_t value;
            } var_18 = { 0x980902, -1 };

            result = ioctl(gISP_1->tuning_fd, 0xc008561b, &var_18);
            if (result == 0) {
                *arg1 = (unsigned char)var_18.value;
                *(uint8_t *)((char *)s1 + 0xa) = (uint8_t)var_18.value;
            }
            return result;
        }

        return result;
    }
}

int IMP_ISP_Tuning_SetAeComp(int arg1)
{
    ISPDevice *gISP_1 = gISP;

    if (gISP_1 != NULL) {
        if (gISP_1->tuning != NULL) {
            if (gISP_1->tuning_state != 2) {
                return -1;
            }

            {
                struct {
                    int32_t cmd;
                    int32_t subcmd;
                    int32_t value;
                } var_18 = { 0, 0x8000023, arg1 };
                int32_t result = ioctl(gISP_1->tuning_fd, 0xc00c56c6, &var_18);

                if (result == 0) {
                    return 0;
                }

                imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
                    "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x814,
                    "IMP_ISP_Tuning_SetAeComp",
                    "%s(%d),ioctl  IMAGE_TUNING_CID_AE_COMP!\n",
                    "IMP_ISP_Tuning_SetAeComp", 0x814);
                return result;
            }
        }
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
        "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x807,
        "IMP_ISP_Tuning_SetAeComp", "get_ispdev error !\n");
    return -1;
}

int IMP_ISP_Tuning_GetAeComp(int *arg1)
{
    ISPDevice *gISP_1 = gISP;

    if (gISP_1 == NULL || gISP_1->tuning == NULL || arg1 == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x821,
            "IMP_ISP_Tuning_GetAeComp", "get_ispdev error !\n");
        return -1;
    }

    if (gISP_1->tuning_state != 2) {
        return -1;
    }

    {
        struct {
            int32_t cmd;
            int32_t subcmd;
            int32_t value;
        } var_20 = { 1, 0x8000023, 0 };
        int32_t result = ioctl(gISP_1->tuning_fd, 0xc00c56c6, &var_20);

        if (result != 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
                "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x82d,
                "IMP_ISP_Tuning_GetAeComp",
                "%s(%d),ioctl  IMAGE_TUNING_CID_AE_COMP!\n",
                "IMP_ISP_Tuning_GetAeComp", 0x82d);
        }

        *arg1 = var_20.value;
        return result;
    }
}

enum {
    TISP_V4L2_CID_BRIGHTNESS = 0x980900,
    TISP_V4L2_CID_CONTRAST = 0x980901,
    TISP_V4L2_CID_SATURATION = 0x980902,
    TISP_V4L2_CID_HFLIP = 0x980914,
    TISP_V4L2_CID_VFLIP = 0x980915,
    TISP_V4L2_CID_SHARPNESS = 0x98091b,
    TISP_VIDIOC_ENABLE_SENSOR = 0x80045612,
    TISP_VIDIOC_DISABLE_SENSOR = 0x80045613,
    TISP_VIDIOC_GET_SENSOR_INDEX = 0x40045626,
    TISP_VIDIOC_CREATE_LINKS = 0x800456d0,
    TISP_VIDIOC_DESTROY_LINKS = 0x800456d1,
    TISP_VIDIOC_ENABLE_LINKS = 0x800456d2,
    TISP_VIDIOC_DISABLE_LINKS = 0x800456d3,
    TISP_VIDIOC_OPEN_AE_ALGO = 0x800456dd,
    TISP_VIDIOC_CLOSE_AE_ALGO = 0x800456de,
    TISP_VIDIOC_OPEN_AWB_ALGO = 0xc00456e3,
    TISP_VIDIOC_CLOSE_AWB_ALGO = 0xc00456e4,
    TISP_VIDIOC_SET_FRAME_DROP = 0xc00456e6,
    TISP_VIDIOC_GET_FRAME_DROP = 0xc00456e7,
    TISP_VIDIOC_GPIO_INIT_OR_FREE = 0xc00456e8,
    TISP_VIDIOC_GPIO_STA = 0xc00456e9,
    TISP_VIDIOC_TUNING = 0xc00c56c6,
    TISP_VIDIOC_G_CTRL = 0xc008561b,
    TISP_VIDIOC_S_CTRL = 0xc008561c,
    TISP_CID_WB_ATTR = 0x8000004,
    TISP_CID_AWB_WEIGHT = 0x8000006,
    TISP_CID_AWB_HIST = 0x8000007,
    TISP_CID_AWB_CWF_SHIFT = 0x8000008,
    TISP_CID_AWB_ZONE = 0x8000009,
    TISP_CID_WB_ALGO = 0x800000c,
    TISP_CID_AWB_CT = 0x800000d,
    TISP_CID_AWB_CLUSTER = 0x800000e,
    TISP_CID_AWB_CT_TREND = 0x800000f,
    TISP_CID_AE_ATTR = 0x8000020,
    TISP_CID_AE_COMP = 0x8000023,
    TISP_CID_EXPR = 0x8000025,
    TISP_CID_EV_ATTR = 0x8000026,
    TISP_CID_TOTAL_GAIN = 0x8000027,
    TISP_CID_MAX_AGAIN = 0x8000028,
    TISP_CID_MAX_DGAIN = 0x8000029,
    TISP_CID_HILIGHT_DEPRESS = 0x800002a,
    TISP_CID_GAMMA = 0x800002b,
    TISP_CID_MOVESTATE = 0x800002c,
    TISP_CID_AE_WEIGHT = 0x800002d,
    TISP_CID_AE_HIST = 0x800002e,
    TISP_CID_AE_HIST_ORIGIN = 0x800002f,
    TISP_CID_AE_ZONE = 0x8000030,
    TISP_CID_AE_LUMA = 0x8000031,
    TISP_CID_AE_IT_MAX = 0x8000032,
    TISP_CID_AE_MIN = 0x8000033,
    TISP_CID_AE_FREEZE = 0x8000034,
    TISP_CID_AE_ROI = 0x8000035,
    TISP_CID_AE_STATE = 0x8000036,
    TISP_CID_BACKLIGHT_COMP = 0x8000037,
    TISP_CID_DEFOG_STRENGTH = 0x8000039,
    TISP_CID_AF_METRICES = 0x8000043,
    TISP_CID_AF_WEIGHT = 0x8000044,
    TISP_CID_AF_HIST = 0x8000045,
    TISP_CID_DPC_RATIO = 0x8000062,
    TISP_CID_NCU_INFO = 0x8000084,
    TISP_CID_3DNS_RATIO = 0x8000085,
    TISP_CID_2DNS_RATIO = 0x8000086,
    TISP_CID_DRC_RATIO = 0x80000a2,
    TISP_CID_ENABLE_DEFOG = 0x80000a4,
    TISP_CID_BLC_ATTR = 0x80000a5,
    TISP_CID_CSC_ATTR = 0x80000a6,
    TISP_CID_SENSOR_FPS = 0x80000e0,
    TISP_CID_RUNNING_MODE = 0x80000e1,
    TISP_CID_MASK = 0x80000e5,
    TISP_CID_AUTO_ZOOM = 0x80000e8,
    TISP_CID_SCALER_LV = 0x80000e9,
    TISP_CID_WDR_OUTPUT_MODE = 0x80000ea,
    TISP_CID_CCM_ATTR = 0x8000100,
    TISP_CID_BCSH_HUE = 0x8000101,
    TISP_CID_ISP_PROCESS = 0x8000164,
    TISP_CID_FW_FREEZE = 0x8000165,
    TISP_CID_SHADING = 0x8000166
};

typedef struct TSeriesTuningValReq {
    int32_t cmd;
    int32_t subcmd;
    int32_t value;
} TSeriesTuningValReq;

typedef struct TSeriesTuningPtrReq {
    int32_t cmd;
    int32_t subcmd;
    void *ptr;
} TSeriesTuningPtrReq;

typedef struct TSeriesV4L2Ctrl {
    int32_t id;
    int32_t value;
} TSeriesV4L2Ctrl;

typedef struct TSeriesAlgoFunc {
    void *priv;
    void (*open)(void);
    int (*close)(void *);
    void *reserved0;
    void *reserved1;
} TSeriesAlgoFunc;

static uint32_t tseries_sensor_fps_num = 25;
static uint32_t tseries_sensor_fps_den = 1;
static IMPISPHVFLIP tseries_hvflip;
static IMPISPRunningMode tseries_running_mode;
static IMPISPTuningOpsMode tseries_custom_mode;
static IMPISPTuningOpsMode tseries_drc_enable;
static IMPISPAntiflickerAttr tseries_antiflicker_attr;
static IMPISPModuleCtl tseries_module_ctl;
static IMPISPFrontCrop tseries_front_crop;
static IMPISPAETargetList tseries_ae_target_list;
static void *tseries_ae_func_tmp;
static void *tseries_awb_func_tmp;
static int32_t tseries_ae_algo_en;
static int32_t tseries_awb_algo_en;

static int tseries_get_isp(ISPDevice **out);
static int tseries_tuning_set_val(int32_t subcmd, int32_t value);
static int tseries_tuning_get_val(int32_t subcmd, int32_t *value);
static int tseries_tuning_set_ptr(int32_t subcmd, void *ptr);
static int tseries_tuning_get_ptr(int32_t subcmd, void *ptr);
static int tseries_v4l2_set(int32_t id, int32_t value);
static int tseries_v4l2_get(int32_t id, int32_t *value);

static int tseries_get_isp(ISPDevice **out)
{
    ISPDevice *isp = gISP;

    if (out != NULL) {
        *out = isp;
    }

    if (isp == NULL) {
        return -1;
    }

    return 0;
}

static int tseries_tuning_set_val(int32_t subcmd, int32_t value)
{
    ISPDevice *isp;

    if (tseries_get_isp(&isp) != 0 || isp->tuning == NULL || isp->tuning_state != 2) {
        return -1;
    }

    {
        TSeriesTuningValReq req = { 0, subcmd, value };
        return ioctl(isp->tuning_fd, TISP_VIDIOC_TUNING, &req);
    }
}

static int tseries_tuning_get_val(int32_t subcmd, int32_t *value)
{
    ISPDevice *isp;

    if (value == NULL) {
        return -1;
    }

    if (tseries_get_isp(&isp) != 0 || isp->tuning == NULL || isp->tuning_state != 2) {
        return -1;
    }

    {
        TSeriesTuningValReq req = { 1, subcmd, 0 };
        int result = ioctl(isp->tuning_fd, TISP_VIDIOC_TUNING, &req);

        if (result == 0) {
            *value = req.value;
        }

        return result;
    }
}

static int tseries_tuning_set_ptr(int32_t subcmd, void *ptr)
{
    ISPDevice *isp;

    if (ptr == NULL) {
        return -1;
    }

    if (tseries_get_isp(&isp) != 0 || isp->tuning == NULL || isp->tuning_state != 2) {
        return -1;
    }

    {
        TSeriesTuningPtrReq req = { 0, subcmd, ptr };
        return ioctl(isp->tuning_fd, TISP_VIDIOC_TUNING, &req);
    }
}

static int tseries_tuning_get_ptr(int32_t subcmd, void *ptr)
{
    ISPDevice *isp;

    if (ptr == NULL) {
        return -1;
    }

    if (tseries_get_isp(&isp) != 0 || isp->tuning == NULL || isp->tuning_state != 2) {
        return -1;
    }

    {
        TSeriesTuningPtrReq req = { 1, subcmd, ptr };
        return ioctl(isp->tuning_fd, TISP_VIDIOC_TUNING, &req);
    }
}

static int tseries_v4l2_set(int32_t id, int32_t value)
{
    ISPDevice *isp;

    if (tseries_get_isp(&isp) != 0 || isp->tuning == NULL || isp->tuning_state != 2) {
        return -1;
    }

    {
        TSeriesV4L2Ctrl ctrl = { id, value };
        return ioctl(isp->tuning_fd, TISP_VIDIOC_S_CTRL, &ctrl);
    }
}

static int tseries_v4l2_get(int32_t id, int32_t *value)
{
    ISPDevice *isp;

    if (value == NULL) {
        return -1;
    }

    if (tseries_get_isp(&isp) != 0 || isp->tuning == NULL || isp->tuning_state != 2) {
        return -1;
    }

    {
        TSeriesV4L2Ctrl ctrl = { id, -1 };
        int result = ioctl(isp->tuning_fd, TISP_VIDIOC_G_CTRL, &ctrl);

        if (result == 0) {
            *value = ctrl.value;
        }

        return result;
    }
}

int IMP_ISP_Tuning_SetSensorFPS(uint32_t fps_num, uint32_t fps_den)
{
    int result;

    if (fps_den == 0) {
        return -1;
    }

    result = tseries_tuning_set_val(TISP_CID_SENSOR_FPS,
        ((int32_t)fps_num << 16) | (fps_den & 0xffff));
    if (result == 0) {
        tseries_sensor_fps_num = fps_num;
        tseries_sensor_fps_den = fps_den;
    }
    return result;
}

int IMP_ISP_Tuning_GetSensorFPS(uint32_t *fps_num, uint32_t *fps_den)
{
    int32_t value = 0;
    int result;

    if (fps_num == NULL || fps_den == NULL) {
        return -1;
    }

    result = tseries_tuning_get_val(TISP_CID_SENSOR_FPS, &value);
    if (result == 0) {
        tseries_sensor_fps_num = ((uint32_t)value >> 16) & 0xffff;
        tseries_sensor_fps_den = (uint32_t)value & 0xffff;
    }

    *fps_num = tseries_sensor_fps_num;
    *fps_den = tseries_sensor_fps_den;
    return result;
}

int IMP_ISP_Tuning_SetAntiFlickerAttr(IMPISPAntiflickerAttr attr)
{
    tseries_antiflicker_attr = attr;
    return 0;
}

int IMP_ISP_Tuning_GetAntiFlickerAttr(IMPISPAntiflickerAttr *pattr)
{
    if (pattr == NULL) {
        return -1;
    }

    *pattr = tseries_antiflicker_attr;
    return 0;
}

int IMP_ISP_Tuning_SetISPRunningMode(IMPISPRunningMode mode)
{
    int result = tseries_tuning_set_val(TISP_CID_RUNNING_MODE, mode);

    if (result == 0) {
        tseries_running_mode = mode;
        global_mode = mode;
    }
    return result;
}

int IMP_ISP_Tuning_GetISPRunningMode(IMPISPRunningMode *pmode)
{
    int32_t value = 0;
    int result;

    if (pmode == NULL) {
        return -1;
    }

    result = tseries_tuning_get_val(TISP_CID_RUNNING_MODE, &value);
    if (result == 0) {
        tseries_running_mode = value;
        global_mode = value;
    }

    *pmode = tseries_running_mode;
    return result;
}

int IMP_ISP_Tuning_SetISPBypass(IMPISPTuningOpsMode enable)
{
    ISPDevice *isp;
    int32_t sensor_index;
    int32_t bypass_mode;

    if (tseries_get_isp(&isp) != 0) {
        return -1;
    }

    if (ioctl(isp->fd, TISP_VIDIOC_DISABLE_LINKS, 0) != 0) {
        return -1;
    }

    sensor_index = -1;
    if (ioctl(isp->fd, TISP_VIDIOC_DESTROY_LINKS, &sensor_index) != 0) {
        return -1;
    }

    if (tseries_v4l2_set(TISP_CID_ISP_PROCESS, enable) != 0) {
        return -1;
    }

    bypass_mode = (enable == 0) ? 1 : 0;
    if (ioctl(isp->fd, TISP_VIDIOC_CREATE_LINKS, &bypass_mode) != 0) {
        return -1;
    }

    if (ioctl(isp->fd, TISP_VIDIOC_ENABLE_LINKS, 0) != 0) {
        return -1;
    }

    return 0;
}

int IMP_ISP_Tuning_SetISPHflip(IMPISPTuningOpsMode mode)
{
    int result = tseries_v4l2_set(TISP_V4L2_CID_HFLIP, mode);

    if (result == 0) {
        tseries_hvflip = (tseries_hvflip & 2) | (mode ? 1 : 0);
    }
    return result;
}

int IMP_ISP_Tuning_GetISPHflip(IMPISPTuningOpsMode *pmode)
{
    int32_t value = 0;
    int result;

    if (pmode == NULL) {
        return -1;
    }

    result = tseries_v4l2_get(TISP_V4L2_CID_HFLIP, &value);
    if (result == 0) {
        tseries_hvflip = (tseries_hvflip & 2) | (value ? 1 : 0);
    }

    *pmode = (tseries_hvflip & 1) ? IMPISP_TUNING_OPS_MODE_ENABLE
                                  : IMPISP_TUNING_OPS_MODE_DISABLE;
    return result;
}

int IMP_ISP_Tuning_SetISPVflip(IMPISPTuningOpsMode mode)
{
    int result = tseries_v4l2_set(TISP_V4L2_CID_VFLIP, mode);

    if (result == 0) {
        tseries_hvflip = (tseries_hvflip & 1) | (mode ? 2 : 0);
    }
    return result;
}

int IMP_ISP_Tuning_GetISPVflip(IMPISPTuningOpsMode *pmode)
{
    int32_t value = 0;
    int result;

    if (pmode == NULL) {
        return -1;
    }

    result = tseries_v4l2_get(TISP_V4L2_CID_VFLIP, &value);
    if (result == 0) {
        tseries_hvflip = (tseries_hvflip & 1) | (value ? 2 : 0);
    }

    *pmode = (tseries_hvflip & 2) ? IMPISP_TUNING_OPS_MODE_ENABLE
                                  : IMPISP_TUNING_OPS_MODE_DISABLE;
    return result;
}

int IMP_ISP_Tuning_SetMaxAgain(uint32_t gain)
{
    return tseries_tuning_set_val(TISP_CID_MAX_AGAIN, gain);
}

int IMP_ISP_Tuning_GetMaxAgain(uint32_t *pgain)
{
    int32_t value = 0;
    int result;

    if (pgain == NULL) {
        return -1;
    }

    result = tseries_tuning_get_val(TISP_CID_MAX_AGAIN, &value);
    *pgain = value;
    return result;
}

int IMP_ISP_Tuning_SetMaxDgain(uint32_t gain)
{
    return tseries_tuning_set_val(TISP_CID_MAX_DGAIN, gain);
}

int IMP_ISP_Tuning_GetMaxDgain(uint32_t *pgain)
{
    int32_t value = 0;
    int result;

    if (pgain == NULL) {
        return -1;
    }

    result = tseries_tuning_get_val(TISP_CID_MAX_DGAIN, &value);
    *pgain = value;
    return result;
}

int IMP_ISP_Tuning_SetBacklightComp(uint32_t strength)
{
    return tseries_tuning_set_val(TISP_CID_BACKLIGHT_COMP, strength);
}

int IMP_ISP_Tuning_GetBacklightComp(uint32_t *pstrength)
{
    int32_t value = 0;
    int result;

    if (pstrength == NULL) {
        return -1;
    }

    result = tseries_tuning_get_val(TISP_CID_BACKLIGHT_COMP, &value);
    *pstrength = value;
    return result;
}

int IMP_ISP_Tuning_SetDPC_Strength(uint32_t ratio)
{
    return tseries_tuning_set_val(TISP_CID_DPC_RATIO, ratio);
}

int IMP_ISP_Tuning_GetDPC_Strength(uint32_t *pratio)
{
    int32_t value = 0;
    int result;

    if (pratio == NULL) {
        return -1;
    }

    result = tseries_tuning_get_val(TISP_CID_DPC_RATIO, &value);
    *pratio = value;
    return result;
}

int IMP_ISP_Tuning_SetDRC_Strength(uint32_t ratio)
{
    return tseries_tuning_set_val(TISP_CID_DRC_RATIO, ratio);
}

int IMP_ISP_Tuning_GetDRC_Strength(uint32_t *pratio)
{
    int32_t value = 0;
    int result;

    if (pratio == NULL) {
        return -1;
    }

    result = tseries_tuning_get_val(TISP_CID_DRC_RATIO, &value);
    *pratio = value;
    return result;
}

int IMP_ISP_Tuning_SetHiLightDepress(uint32_t strength)
{
    return tseries_tuning_set_val(TISP_CID_HILIGHT_DEPRESS, strength);
}

int IMP_ISP_Tuning_GetHiLightDepress(uint32_t *pstrength)
{
    int32_t value = 0;
    int result;

    if (pstrength == NULL) {
        return -1;
    }

    result = tseries_tuning_get_val(TISP_CID_HILIGHT_DEPRESS, &value);
    *pstrength = value;
    return result;
}

int IMP_ISP_Tuning_SetTemperStrength(uint32_t ratio)
{
    return tseries_tuning_set_val(TISP_CID_3DNS_RATIO, ratio);
}

int IMP_ISP_Tuning_GetTemperStrength(uint32_t *pratio)
{
    int32_t value = 0;
    int result;

    if (pratio == NULL) {
        return -1;
    }

    result = tseries_tuning_get_val(TISP_CID_3DNS_RATIO, &value);
    *pratio = value;
    return result;
}

int IMP_ISP_Tuning_SetSinterStrength(uint32_t ratio)
{
    return tseries_tuning_set_val(TISP_CID_2DNS_RATIO, ratio);
}

int IMP_ISP_Tuning_GetSinterStrength(uint32_t *pratio)
{
    int32_t value = 0;
    int result;

    if (pratio == NULL) {
        return -1;
    }

    result = tseries_tuning_get_val(TISP_CID_2DNS_RATIO, &value);
    *pratio = value;
    return result;
}

int IMP_ISP_Tuning_SetBcshHue(unsigned char hue)
{
    return tseries_tuning_set_val(TISP_CID_BCSH_HUE, hue);
}

int IMP_ISP_Tuning_GetBcshHue(unsigned char *phue)
{
    int32_t value = 0;
    int result;

    if (phue == NULL) {
        return -1;
    }

    result = tseries_tuning_get_val(TISP_CID_BCSH_HUE, &value);
    *phue = value;
    return result;
}

int IMP_ISP_Tuning_SetDefog_Strength(uint32_t strength)
{
    return tseries_tuning_set_val(TISP_CID_DEFOG_STRENGTH, strength);
}

int IMP_ISP_Tuning_GetDefog_Strength(uint32_t *pstrength)
{
    int32_t value = 0;
    int result;

    if (pstrength == NULL) {
        return -1;
    }

    result = tseries_tuning_get_val(TISP_CID_DEFOG_STRENGTH, &value);
    *pstrength = value;
    return result;
}

int IMP_ISP_Tuning_SetWB(IMPISPWB *wb)
{
    return tseries_tuning_set_ptr(TISP_CID_WB_ATTR, wb);
}

int IMP_ISP_Tuning_GetWB(IMPISPWB *wb)
{
    return tseries_tuning_get_ptr(TISP_CID_WB_ATTR, wb);
}

int IMP_ISP_Tuning_GetWB_Statis(IMPISPWB *wb)
{
    return IMP_ISP_Tuning_GetWB(wb);
}

int IMP_ISP_Tuning_GetWB_GOL_Statis(IMPISPWB *wb)
{
    return IMP_ISP_Tuning_GetWB(wb);
}

int IMP_ISP_Tuning_SetExpr(IMPISPExpr *expr)
{
    return tseries_tuning_set_ptr(TISP_CID_EXPR, expr);
}

int IMP_ISP_Tuning_GetExpr(IMPISPExpr *expr)
{
    return tseries_tuning_get_ptr(TISP_CID_EXPR, expr);
}

int IMP_ISP_Tuning_GetEVAttr(IMPISPEVAttr *attr)
{
    return tseries_tuning_get_ptr(TISP_CID_EV_ATTR, attr);
}

int IMP_ISP_Tuning_SetAeWeight(void *weight)
{
    return tseries_tuning_set_ptr(TISP_CID_AE_WEIGHT, weight);
}

int IMP_ISP_Tuning_GetAeWeight(void *weight)
{
    return tseries_tuning_get_ptr(TISP_CID_AE_WEIGHT, weight);
}

int IMP_ISP_Tuning_AE_SetROI(void *roi)
{
    return tseries_tuning_set_ptr(TISP_CID_AE_ROI, roi);
}

int IMP_ISP_Tuning_AE_GetROI(void *roi)
{
    return tseries_tuning_get_ptr(TISP_CID_AE_ROI, roi);
}

int IMP_ISP_Tuning_SetGamma(void *gamma)
{
    return tseries_tuning_set_ptr(TISP_CID_GAMMA, gamma);
}

int IMP_ISP_Tuning_GetGamma(void *gamma)
{
    return tseries_tuning_get_ptr(TISP_CID_GAMMA, gamma);
}

int IMP_ISP_Tuning_SetAeHist(void *hist)
{
    return tseries_tuning_set_ptr(TISP_CID_AE_HIST, hist);
}

int IMP_ISP_Tuning_GetAeHist(void *hist)
{
    return tseries_tuning_get_ptr(TISP_CID_AE_HIST, hist);
}

int IMP_ISP_Tuning_GetAeHist_Origin(void *hist)
{
    return tseries_tuning_get_ptr(TISP_CID_AE_HIST_ORIGIN, hist);
}

int IMP_ISP_Tuning_SetAwbWeight(void *weight)
{
    return tseries_tuning_set_ptr(TISP_CID_AWB_WEIGHT, weight);
}

int IMP_ISP_Tuning_GetAwbWeight(void *weight)
{
    return tseries_tuning_get_ptr(TISP_CID_AWB_WEIGHT, weight);
}

int IMP_ISP_Tuning_WaitFrame(int timeout_ms)
{
    (void)timeout_ms;
    return 0;
}

int IMP_ISP_Tuning_GetSensorAttr(IMPISPSENSORAttr *attr)
{
    if (attr == NULL) {
        return -1;
    }

    memset(attr, 0, sizeof(*attr));
    attr->fps = tseries_sensor_fps_num;
    return 0;
}

int IMP_ISP_Tuning_SetAeAttr(void *ae_attr)
{
    return tseries_tuning_set_ptr(TISP_CID_AE_ATTR, ae_attr);
}

int IMP_ISP_Tuning_GetAeAttr(void *ae_attr)
{
    return tseries_tuning_get_ptr(TISP_CID_AE_ATTR, ae_attr);
}

int IMP_ISP_Tuning_SetModuleControl(IMPISPModuleCtl *ispmodule)
{
    if (ispmodule == NULL) {
        return -1;
    }

    tseries_module_ctl = *ispmodule;
    return 0;
}

int IMP_ISP_Tuning_GetModuleControl(IMPISPModuleCtl *ispmodule)
{
    if (ispmodule == NULL) {
        return -1;
    }

    *ispmodule = tseries_module_ctl;
    return 0;
}

int IMP_ISP_Tuning_SetFrontCrop(IMPISPFrontCrop *ispfrontcrop)
{
    if (ispfrontcrop == NULL) {
        return -1;
    }

    tseries_front_crop = *ispfrontcrop;
    return 0;
}

int IMP_ISP_Tuning_GetFrontCrop(IMPISPFrontCrop *ispfrontcrop)
{
    if (ispfrontcrop == NULL) {
        return -1;
    }

    *ispfrontcrop = tseries_front_crop;
    return 0;
}

int IMP_ISP_Tuning_SetAutoZoom(void *zoom_attr)
{
    return tseries_tuning_set_ptr(TISP_CID_AUTO_ZOOM, zoom_attr);
}

int IMP_ISP_Tuning_SetAeTargetList(IMPISPAETargetList *at_list)
{
    if (at_list == NULL) {
        return -1;
    }

    tseries_ae_target_list = *at_list;
    return 0;
}

int IMP_ISP_Tuning_GetAeTargetList(IMPISPAETargetList *at_list)
{
    if (at_list == NULL) {
        return -1;
    }

    *at_list = tseries_ae_target_list;
    return 0;
}

int IMP_ISP_Tuning_SetISPCustomMode(IMPISPTuningOpsMode mode)
{
    tseries_custom_mode = mode;
    return 0;
}

int IMP_ISP_Tuning_GetISPCustomMode(IMPISPTuningOpsMode *mode)
{
    if (mode == NULL) {
        return -1;
    }

    *mode = tseries_custom_mode;
    return 0;
}

int IMP_ISP_Tuning_EnableDRC(IMPISPTuningOpsMode mode)
{
    tseries_drc_enable = mode;
    return 0;
}

int IMP_ISP_Tuning_GetAeLuma(int *luma)
{
    int32_t value = 0;
    int result;

    if (luma == NULL) {
        return -1;
    }

    result = tseries_tuning_get_val(TISP_CID_AE_LUMA, &value);
    *luma = value;
    return result;
}

int IMP_ISP_Tuning_SetHVFLIP(IMPISPHVFLIP hvflip)
{
    int result;

    result = tseries_v4l2_set(TISP_V4L2_CID_HFLIP, hvflip & 1);
    if (result != 0) {
        return result;
    }

    result = tseries_v4l2_set(TISP_V4L2_CID_VFLIP, (hvflip >> 1) & 1);
    if (result == 0) {
        tseries_hvflip = hvflip;
    }
    return result;
}

int IMP_ISP_Tuning_GetHVFlip(IMPISPHVFLIP *hvflip)
{
    int32_t hflip = 0;
    int32_t vflip = 0;
    int result;

    if (hvflip == NULL) {
        return -1;
    }

    result = tseries_v4l2_get(TISP_V4L2_CID_HFLIP, &hflip);
    if (result != 0) {
        return result;
    }

    result = tseries_v4l2_get(TISP_V4L2_CID_VFLIP, &vflip);
    if (result != 0) {
        return result;
    }

    tseries_hvflip = (hflip ? 1 : 0) | (vflip ? 2 : 0);
    *hvflip = tseries_hvflip;
    return 0;
}

int IMP_ISP_Tuning_GetHVFLIP(IMPISPHVFLIP *hvflip)
{
    return IMP_ISP_Tuning_GetHVFlip(hvflip);
}

int IMP_ISP_Tuning_SetAe_IT_MAX(uint32_t it_max)
{
    return tseries_tuning_set_val(TISP_CID_AE_IT_MAX, it_max);
}

int IMP_ISP_Tuning_GetAE_IT_MAX(uint32_t *it_max)
{
    int32_t value = 0;
    int result;

    if (it_max == NULL) {
        return -1;
    }

    result = tseries_tuning_get_val(TISP_CID_AE_IT_MAX, &value);
    *it_max = value;
    return result;
}

int IMP_ISP_Tuning_SetAeMin(int min_it, int min_again)
{
    struct {
        int min_it;
        int min_again;
    } params = { min_it, min_again };

    return tseries_tuning_set_ptr(TISP_CID_AE_MIN, &params);
}

int IMP_ISP_Tuning_GetAeMin(int *min_it, int *min_again)
{
    struct {
        int min_it;
        int min_again;
    } params = { 0, 0 };
    int result;

    if (min_it == NULL || min_again == NULL) {
        return -1;
    }

    result = tseries_tuning_get_ptr(TISP_CID_AE_MIN, &params);
    *min_it = params.min_it;
    *min_again = params.min_again;
    return result;
}

int IMP_ISP_Tuning_GetAeZone(void *zone)
{
    return tseries_tuning_get_ptr(TISP_CID_AE_ZONE, zone);
}

int IMP_ISP_Tuning_GetAeState(void *state)
{
    return tseries_tuning_get_ptr(TISP_CID_AE_STATE, state);
}

int IMP_ISP_Tuning_GetAwbZone(void *zone_r, void *zone_g, void *zone_b)
{
    struct {
        void *zone_r;
        void *zone_g;
        void *zone_b;
    } zones = { zone_r, zone_g, zone_b };

    return tseries_tuning_get_ptr(TISP_CID_AWB_ZONE, &zones);
}

int IMP_ISP_Tuning_SetAwbHist(void *attr)
{
    return tseries_tuning_set_ptr(TISP_CID_AWB_HIST, attr);
}

int IMP_ISP_Tuning_GetAwbHist(void *hist)
{
    return tseries_tuning_get_ptr(TISP_CID_AWB_HIST, hist);
}

int IMP_ISP_Tuning_SetAwbCt(void *attr)
{
    return tseries_tuning_set_ptr(TISP_CID_AWB_CT, attr);
}

int IMP_ISP_Tuning_GetAWBCt(uint32_t *ct)
{
    int32_t value = 0;
    int result;

    if (ct == NULL) {
        return -1;
    }

    result = tseries_tuning_get_val(TISP_CID_AWB_CT, &value);
    *ct = value;
    return result;
}

int IMP_ISP_Tuning_SetCCMAttr(void *attr)
{
    return tseries_tuning_set_ptr(TISP_CID_CCM_ATTR, attr);
}

int IMP_ISP_Tuning_GetCCMAttr(void *attr)
{
    return tseries_tuning_get_ptr(TISP_CID_CCM_ATTR, attr);
}

int IMP_ISP_Tuning_SetWB_ALGO(int mode)
{
    return tseries_tuning_set_val(TISP_CID_WB_ALGO, mode);
}

int IMP_ISP_Tuning_SetVideoDrop(void *attr)
{
    ISPDevice *isp;

    if (attr == NULL) {
        return -1;
    }

    if (tseries_get_isp(&isp) != 0 || isp->tuning == NULL) {
        return -1;
    }

    *(void **)isp->tuning = attr;
    return 0;
}

int IMP_ISP_Tuning_SetShading(void *attr)
{
    return tseries_tuning_set_ptr(TISP_CID_SHADING, attr);
}

int IMP_ISP_Tuning_SetScalerLv(int chn, int level)
{
    (void)chn;
    return tseries_tuning_set_val(TISP_CID_SCALER_LV, level);
}

int IMP_ISP_Tuning_SetMask(void *attr)
{
    return tseries_tuning_set_ptr(TISP_CID_MASK, attr);
}

int IMP_ISP_Tuning_GetMask(void *attr)
{
    return tseries_tuning_get_ptr(TISP_CID_MASK, attr);
}

int IMP_ISP_Tuning_SetISPProcess(void *attr)
{
    return tseries_tuning_set_ptr(TISP_CID_ISP_PROCESS, attr);
}

int IMP_ISP_Tuning_SetFWFreeze(int enable)
{
    return tseries_tuning_set_val(TISP_CID_FW_FREEZE, enable);
}

int IMP_ISP_Tuning_SetCsc_Attr(void *attr)
{
    return tseries_tuning_set_ptr(TISP_CID_CSC_ATTR, attr);
}

int IMP_ISP_Tuning_GetCsc_Attr(void *attr)
{
    return tseries_tuning_get_ptr(TISP_CID_CSC_ATTR, attr);
}

int IMP_ISP_Tuning_SetWdr_OutputMode(int mode)
{
    return tseries_tuning_set_val(TISP_CID_WDR_OUTPUT_MODE, mode);
}

int IMP_ISP_Tuning_GetWdr_OutputMode(int *mode)
{
    int32_t value = 0;
    int result;

    if (mode == NULL) {
        return -1;
    }

    result = tseries_tuning_get_val(TISP_CID_WDR_OUTPUT_MODE, &value);
    *mode = value;
    return result;
}

int IMP_ISP_Tuning_SetAwbCtTrend(void *attr)
{
    return tseries_tuning_set_ptr(TISP_CID_AWB_CT_TREND, attr);
}

int IMP_ISP_Tuning_GetAwbCtTrend(void *attr)
{
    return tseries_tuning_get_ptr(TISP_CID_AWB_CT_TREND, attr);
}

int IMP_ISP_Tuning_SetAwbClust(void *attr)
{
    return tseries_tuning_set_ptr(TISP_CID_AWB_CLUSTER, attr);
}

int IMP_ISP_Tuning_GetAwbClust(void *attr)
{
    return tseries_tuning_get_ptr(TISP_CID_AWB_CLUSTER, attr);
}

int IMP_ISP_Tuning_SetAfWeight(void *attr)
{
    return tseries_tuning_set_ptr(TISP_CID_AF_WEIGHT, attr);
}

int IMP_ISP_Tuning_GetAfWeight(void *attr)
{
    return tseries_tuning_get_ptr(TISP_CID_AF_WEIGHT, attr);
}

int IMP_ISP_Tuning_SetAfHist(void *attr)
{
    return tseries_tuning_set_ptr(TISP_CID_AF_HIST, attr);
}

int IMP_ISP_Tuning_GetAfHist(void *attr)
{
    return tseries_tuning_get_ptr(TISP_CID_AF_HIST, attr);
}

int IMP_ISP_Tuning_SetAeFreeze(int enable)
{
    return tseries_tuning_set_val(TISP_CID_AE_FREEZE, enable);
}

int IMP_ISP_Tuning_GetNCUInfo(void *info)
{
    return tseries_tuning_get_ptr(TISP_CID_NCU_INFO, info);
}

int IMP_ISP_Tuning_GetNCUAlloc(void *info)
{
    return tseries_tuning_get_ptr(TISP_CID_NCU_INFO, info);
}

int IMP_ISP_Tuning_GetBlcAttr(void *attr)
{
    return tseries_tuning_get_ptr(TISP_CID_BLC_ATTR, attr);
}

int IMP_ISP_Tuning_GetAfZone(void *zone)
{
    return tseries_tuning_get_ptr(TISP_CID_AF_HIST, zone);
}

int IMP_ISP_Tuning_GetAFMetrices(void *metrices)
{
    return tseries_tuning_get_ptr(TISP_CID_AF_METRICES, metrices);
}

int IMP_ISP_Tuning_EnableMovestate(void)
{
    return tseries_tuning_set_val(TISP_CID_MOVESTATE, 1);
}

int IMP_ISP_Tuning_DisableMovestate(void)
{
    return tseries_tuning_set_val(TISP_CID_MOVESTATE, 0);
}

int IMP_ISP_Tuning_EnableDefog(void)
{
    return tseries_tuning_set_val(TISP_CID_ENABLE_DEFOG, 1);
}

int IMP_ISP_Tuning_Awb_SetRgbCoefft(void *attr)
{
    return tseries_tuning_set_ptr(TISP_CID_AWB_CWF_SHIFT, attr);
}

int IMP_ISP_Tuning_Awb_GetRgbCoefft(void *attr)
{
    return tseries_tuning_get_ptr(TISP_CID_AWB_CWF_SHIFT, attr);
}

int IMP_ISP_SetFrameDrop(void *attr)
{
    ISPDevice *isp;

    if (attr == NULL || tseries_get_isp(&isp) != 0) {
        return -1;
    }

    return ioctl(isp->fd, TISP_VIDIOC_SET_FRAME_DROP, attr);
}

int IMP_ISP_GetFrameDrop(void *attr)
{
    ISPDevice *isp;

    if (attr == NULL || tseries_get_isp(&isp) != 0) {
        return -1;
    }

    return ioctl(isp->fd, TISP_VIDIOC_GET_FRAME_DROP, attr);
}

int IMP_ISP_SetFixedContraster(int mode)
{
    (void)mode;
    return 0;
}

int IMP_ISP_SetAeAlgoFunc(void *func)
{
    if (func == NULL) {
        return -1;
    }

    if (tseries_ae_func_tmp != NULL) {
        free(tseries_ae_func_tmp);
    }

    tseries_ae_func_tmp = malloc(sizeof(TSeriesAlgoFunc));
    if (tseries_ae_func_tmp == NULL) {
        return -1;
    }

    memcpy(tseries_ae_func_tmp, func, sizeof(TSeriesAlgoFunc));
    tseries_ae_algo_en = 1;
    return 0;
}

int IMP_ISP_SetAeAlgoFunc_internal(void *func)
{
    ISPDevice *isp;

    if (func == NULL) {
        return -1;
    }

    if (tseries_get_isp(&isp) != 0 || isp->opened >= 2) {
        return -1;
    }

    if (ioctl(isp->fd, TISP_VIDIOC_OPEN_AE_ALGO, func) != 0) {
        return -1;
    }

    return IMP_ISP_SetAeAlgoFunc(func);
}

int IMP_ISP_SetAeAlgoFunc_close(void)
{
    ISPDevice *isp;

    if (tseries_get_isp(&isp) != 0) {
        return -1;
    }

    tseries_ae_algo_en = 0;
    if (ioctl(isp->fd, TISP_VIDIOC_CLOSE_AE_ALGO, 0) != 0) {
        return -1;
    }

    return 0;
}

int IMP_ISP_SetAwbAlgoFunc(void *func)
{
    if (func == NULL) {
        return -1;
    }

    if (tseries_awb_func_tmp != NULL) {
        free(tseries_awb_func_tmp);
    }

    tseries_awb_func_tmp = malloc(sizeof(TSeriesAlgoFunc));
    if (tseries_awb_func_tmp == NULL) {
        return -1;
    }

    memcpy(tseries_awb_func_tmp, func, sizeof(TSeriesAlgoFunc));
    tseries_awb_algo_en = 1;
    return 0;
}

int IMP_ISP_SetAwbAlgoFunc_internal(void *func)
{
    ISPDevice *isp;

    if (func == NULL) {
        return -1;
    }

    if (tseries_get_isp(&isp) != 0 || isp->opened >= 2) {
        return -1;
    }

    if (ioctl(isp->fd, TISP_VIDIOC_OPEN_AWB_ALGO, 0) != 0) {
        return -1;
    }

    return IMP_ISP_SetAwbAlgoFunc(func);
}

int IMP_ISP_SetAwbAlgoFunc_close(void)
{
    ISPDevice *isp;

    if (tseries_get_isp(&isp) != 0) {
        return -1;
    }

    tseries_awb_algo_en = 0;
    if (ioctl(isp->fd, TISP_VIDIOC_CLOSE_AWB_ALGO, 0) != 0) {
        return -1;
    }

    return 0;
}

int IMP_ISP_EnableSensor(void)
{
    ISPDevice *isp;
    int32_t sensor_index = -1;

    if (tseries_get_isp(&isp) != 0) {
        return -1;
    }

    if (tseries_ae_algo_en != 0 && tseries_ae_func_tmp != NULL) {
        IMP_ISP_SetAeAlgoFunc_internal(tseries_ae_func_tmp);
    }

    if (tseries_awb_algo_en != 0 && tseries_awb_func_tmp != NULL) {
        IMP_ISP_SetAwbAlgoFunc_internal(tseries_awb_func_tmp);
    }

    if (ioctl(isp->fd, TISP_VIDIOC_GET_SENSOR_INDEX, &sensor_index) != 0) {
        return -1;
    }

    if (sensor_index == -1) {
        return -1;
    }

    if (ioctl(isp->fd, TISP_VIDIOC_ENABLE_SENSOR, 0) != 0) {
        return -1;
    }

    sensor_index = 0;
    if (ioctl(isp->fd, TISP_VIDIOC_CREATE_LINKS, &sensor_index) != 0) {
        return -1;
    }

    if (ioctl(isp->fd, TISP_VIDIOC_ENABLE_LINKS, 0) != 0) {
        return -1;
    }

    isp->opened += 2;
    return 0;
}

int IMP_ISP_DisableSensor(void)
{
    ISPDevice *isp;
    int32_t sensor_index = -1;

    if (tseries_get_isp(&isp) != 0) {
        return -1;
    }

    if (ioctl(isp->fd, TISP_VIDIOC_GET_SENSOR_INDEX, &sensor_index) != 0) {
        return -1;
    }

    if (sensor_index == -1) {
        return -1;
    }

    if (tseries_ae_algo_en != 0) {
        IMP_ISP_SetAeAlgoFunc_close();
    }

    if (tseries_awb_algo_en != 0) {
        IMP_ISP_SetAwbAlgoFunc_close();
    }

    if (ioctl(isp->fd, TISP_VIDIOC_DISABLE_LINKS, 0) != 0) {
        return -1;
    }

    sensor_index = -1;
    if (ioctl(isp->fd, TISP_VIDIOC_DESTROY_LINKS, &sensor_index) != 0) {
        return -1;
    }

    if (ioctl(isp->fd, TISP_VIDIOC_DISABLE_SENSOR, 0) != 0) {
        return -1;
    }

    isp->opened -= 2;
    return 0;
}

int IMP_ISP_SET_GPIO_INIT_OR_FREE(int *gpio)
{
    ISPDevice *isp;

    if (gpio == NULL || tseries_get_isp(&isp) != 0) {
        return -1;
    }

    return ioctl(isp->fd, TISP_VIDIOC_GPIO_INIT_OR_FREE, gpio);
}

int IMP_ISP_SET_GPIO_STA(int *gpio)
{
    ISPDevice *isp;

    if (gpio == NULL || tseries_get_isp(&isp) != 0) {
        return -1;
    }

    return ioctl(isp->fd, TISP_VIDIOC_GPIO_STA, gpio);
}

/* ----- Missing symbols required by rvd hal_init() -----
 * Ported from libimp.so decomps at 0x9bd84 / 0x9c69c / 0x9e0f8 / 0x9cd40.
 * The struct layout uses raw byte offsets because the ISPDevice struct
 * in this file doesn't fully match the binary's internal layout
 * (fields +0xac=ncu_buf pointer and +0xb4=wdr_buf pointer are unsplit
 * in the struct).
 */

/* T68 forward decls */
int32_t IMP_Alloc(void *info, int32_t size, const char *name);
int32_t IMP_Free(void *info, int32_t phys);

int IMP_ISP_AddSensor(IMPSensorInfo *pinfo)
{
    ISPDevice *isp = (ISPDevice *)gISP;
    uint8_t *isp_b = (uint8_t *)isp;
    char *name = (char *)pinfo;

    if (isp == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x19d,
            "IMP_ISP_AddSensor", "ISPDEV cannot open\n");
        return -1;
    }
    if (isp->opened >= 2) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x1a2,
            "IMP_ISP_AddSensor",
            "Sensor is runing, please Call 'EmuISP_DisableSensor' firstly\n");
        return -1;
    }
    if (ioctl(isp->fd, 0x805056c1, pinfo) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x1a7,
            "IMP_ISP_AddSensor", "VIDIOC_REGISTER_SENSOR(%s) error!\n", pinfo);
        return -1;
    }
    if (bpath != NULL) {
        int32_t bp_ret = ioctl(isp->fd, 0xc00456c7, bpath);
        free(bpath);
        bpath = NULL;
        if (bp_ret != 0) return bp_ret;
    }

    /* Enumerate sensors, match by name.
     * ioctl 0xc050561a writes a 0x50-byte struct: first 4 bytes are the
     * query index (in/out), remaining bytes receive the sensor name.
     * The struct layout matches libimp's stock sensor enum record. */
    int32_t sensor_idx = -1;
    struct {
        int32_t index;
        char    name[0x4c];
    } enum_rec;
    memset(&enum_rec, 0, sizeof(enum_rec));
    while (1) {
        if (ioctl(isp->fd, 0xc050561a, &enum_rec) != 0) break;
        if (strcmp(name, enum_rec.name) == 0) {
            sensor_idx = enum_rec.index;
            break;
        }
        enum_rec.index += 1;
        memset(enum_rec.name, 0, sizeof(enum_rec.name));
    }
    if (sensor_idx == -1) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x1c0,
            "IMP_ISP_AddSensor", "sensor[%s] hasn't been added!\n", name);
        return -1;
    }
    if (ioctl(isp->fd, 0xc0045627, &sensor_idx) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x1c6,
            "IMP_ISP_AddSensor", "Failed to select sensor[%s]!\n", name);
        return -1;
    }

    /* Copy 0x50 bytes of sensor-info into gISP +0x28 via halfword pack */
    memcpy(isp_b + 0x28, pinfo, 0x50);

    /* VIDIOC_GET_BUF_INFO: read ncu buffer size */
    struct { int32_t paddr; int32_t size; } buf_info = {0, 0};
    if (ioctl(isp->fd, 0x800856d5, &buf_info) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x1dc,
            "IMP_ISP_AddSensor", "VIDIOC_GET_BUF_INFO() error!\n");
        return -1;
    }
    imp_log_fun(4, IMP_Log_Get_Option(), 2, "IMP-ISP",
        "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x1e0,
        "IMP_ISP_AddSensor", "%s,%d: paddr = 0x%x, size = 0x%x\n",
        "IMP_ISP_AddSensor", 0x1e0, buf_info.paddr, buf_info.size);

    void *ncu_alloc = malloc(0x94);
    if (ncu_alloc == NULL) {
        printf("error(%s,%d): maloc err\n", "IMP_ISP_AddSensor", 0x1e3);
        return -1;
    }
    if (IMP_Alloc(ncu_alloc, buf_info.size, "ncubuf") != 0) {
        printf("error(%s,%d): IMP_Alloc\n", "IMP_ISP_AddSensor", 0x1e8);
        return -1;
    }
    *(void **)(isp_b + 0xac) = ncu_alloc;
    int32_t ncu_phys = *(int32_t *)((char *)ncu_alloc + 0x84);
    int32_t set_buf = ncu_phys;
    if (ioctl(isp->fd, 0x800856d4, &set_buf) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x1ee,
            "IMP_ISP_AddSensor", "VIDIOC_SET_BUF_INFO() error!\n");
        return -1;
    }

    /* WDR path only if WDR mode flag (+0xb0) is set */
    if (*(int32_t *)(isp_b + 0xb0) != 1) return 0;

    struct { int32_t paddr; int32_t size; } wdr_info = {0, 0};
    if (ioctl(isp->fd, 0x800856d7, &wdr_info) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x1f6,
            "IMP_ISP_AddSensor", "VIDIOC_GET_WDR_BUF_INFO() error!\n");
        return -1;
    }
    imp_log_fun(4, IMP_Log_Get_Option(), 2, "IMP-ISP",
        "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x1fa,
        "IMP_ISP_AddSensor", "%s,%d: paddr = 0x%x, size = 0x%x\n",
        "IMP_ISP_AddSensor", 0x1fa, wdr_info.paddr, wdr_info.size);
    void *wdr_alloc = malloc(0x94);
    if (wdr_alloc == NULL) {
        printf("error(%s,%d): maloc err\n", "IMP_ISP_AddSensor", 0x1fd);
        return -1;
    }
    if (IMP_Alloc(wdr_alloc, wdr_info.size, "wdrbuf") != 0) {
        printf("error(%s,%d): IMP_Alloc\n", "IMP_ISP_AddSensor", 0x202);
        return -1;
    }
    *(void **)(isp_b + 0xb4) = wdr_alloc;
    int32_t wdr_phys = *(int32_t *)((char *)wdr_alloc + 0x84);
    int32_t set_wdr = wdr_phys;
    if (ioctl(isp->fd, 0x800856d6, &set_wdr) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x208,
            "IMP_ISP_AddSensor", "VIDIOC_SET_WDR_BUF_INFO() error!\n");
        return -1;
    }
    return 0;
}

int IMP_ISP_DelSensor(IMPSensorInfo *pinfo)
{
    ISPDevice *isp = (ISPDevice *)gISP;
    uint8_t *isp_b = (uint8_t *)isp;

    if (isp == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x2ab,
            "IMP_ISP_DelSensor", "ISPDEV cannot open\n");
        return -1;
    }
    if (isp->opened >= 2) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x2b0,
            "IMP_ISP_DelSensor",
            "Sensor is runing, please Call 'EmuISP_DisableSensor' firstly\n");
        return -1;
    }
    int32_t sel = -1;
    if (ioctl(isp->fd, 0xc0045627, &sel) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x2b7,
            "IMP_ISP_DelSensor", "Failed to select sensor[%s]!\n", pinfo);
        return -1;
    }
    int32_t r = ioctl(isp->fd, 0x805056c2, pinfo);
    if (r != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x2bc,
            "IMP_ISP_DelSensor", "VIDIOC_REGISTER_SENSOR(%s) error!\n", pinfo);
        return r;
    }

    memset(isp_b + 0x28, 0, 0x50);
    void *ncu = *(void **)(isp_b + 0xac);
    if (ncu != NULL) {
        IMP_Free(ncu, *(int32_t *)((char *)ncu + 0x80));
        free(ncu);
        *(void **)(isp_b + 0xac) = NULL;
    }
    void *wdr = *(void **)(isp_b + 0xb4);
    if (wdr != NULL) {
        IMP_Free(wdr, *(int32_t *)((char *)wdr + 0x80));
        free(wdr);
        *(void **)(isp_b + 0xb4) = NULL;
    }
    return 0;
}

int IMP_ISP_EnableTuning(void)
{
    ISPDevice *isp = (ISPDevice *)gISP;
    uint8_t *isp_b = (uint8_t *)isp;

    if (isp == NULL) return -1;
    if (*(void **)(isp_b + 0x9c) != NULL) return 0;  /* already enabled */

    /* Build "/dev/isp-m0" path at +0x78.
     * Stock binary uses open flags 0x80002 (O_RDWR | O_CLOEXEC), but the
     * Thingino kernel's /dev/isp-m0 driver appears to reject O_CLOEXEC on
     * this particular module build — the legacy openimp impl used plain
     * O_RDWR and got the stream on. Prefer the flag value that works on
     * real hardware over binary-exact. */
    strcpy((char *)(isp_b + 0x78), "/dev/isp-m0");
    int32_t tfd = open((char *)(isp_b + 0x78), O_RDWR);
    if (tfd < 0) {
        /* Fallback: some device trees expose the tuning node as /dev/isp-w02
         * (the second ISP tree). Try it before giving up. */
        strcpy((char *)(isp_b + 0x78), "/dev/isp-w02");
        tfd = open((char *)(isp_b + 0x78), O_RDWR);
    }
    *(int32_t *)(isp_b + 0x98) = tfd;
    if (tfd < 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x476,
            "IMP_ISP_EnableTuning",
            "Cannot open %s (errno=%d): %s\n",
            isp_b + 0x78, errno, strerror(errno));
        /* Free any partial state allocated above this point */
        return -1;
    }
    void *tune = calloc(0x1c, 1);
    if (tune == NULL) { close(tfd); return -1; }
    *(void **)(isp_b + 0x9c) = tune;
    *(int32_t *)(isp_b + 0xa8) = 2;

    int32_t mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    *(int32_t *)(isp_b + 0xa0) = mem_fd;
    if (mem_fd <= 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x485,
            "IMP_ISP_EnableTuning", "Failed to open %s\n", "/dev/mem");
    }
    void *base = mmap(0, 0x10000, PROT_READ | PROT_WRITE, MAP_SHARED,
                      mem_fd, 0x13380000);
    *(void **)(isp_b + 0xa4) = base;
    if (base == NULL || base == MAP_FAILED) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x489,
            "IMP_ISP_EnableTuning", "Failed to mmap isp base addr\n");
    }

    int32_t fps_pack[3] = { 1, 0, 0x80000e0 };
    if (ioctl(*(int32_t *)(isp_b + 0x98), 0xc00c56c6, fps_pack) == 0) {
        *(int32_t *)((char *)tune + 0xc) = (uint32_t)fps_pack[2] >> 16;
        *(int32_t *)((char *)tune + 0x10) = fps_pack[2] & 0xffff;
    }
    *(uint8_t *)((char *)tune + 9) = custom_contrast;
    return 0;
}

int IMP_ISP_DisableTuning(void)
{
    ISPDevice *isp = (ISPDevice *)gISP;
    uint8_t *isp_b = (uint8_t *)isp;
    if (isp == NULL) return 0;

    void *tune = *(void **)(isp_b + 0x9c);
    if (tune != NULL) free(tune);
    int32_t tfd = *(int32_t *)(isp_b + 0x98);
    *(void **)(isp_b + 0x9c) = NULL;
    if (tfd > 0) close(tfd);

    void *base = *(void **)(isp_b + 0xa4);
    if (base != NULL) munmap(base, 0x10000);
    int32_t mem_fd = *(int32_t *)(isp_b + 0xa0);
    if (mem_fd > 0) close(mem_fd);
    *(void **)(isp_b + 0xa4) = NULL;
    *(int32_t *)(isp_b + 0xa0) = 0;
    *(int32_t *)(isp_b + 0xa8) = 0;
    return 0;
}
