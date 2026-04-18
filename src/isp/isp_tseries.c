#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

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
