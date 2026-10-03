#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "core/globals.h"
#include "imp/imp_isp.h"
#include "isp_ioctl_compat.h"
#include "isp_t21_sinter.h"
#include "dma_alloc.h"
#if defined(PLATFORM_T23)
#include "t23/openimp_t23_persist.h"
#endif

#if defined(PLATFORM_T23)
#define TISP_TUNING_IOCTL 0xc01056c6U
#define TISP_TUNING_SENSOR_FIELD int32_t sensor;
#else
#define TISP_TUNING_IOCTL 0xc00c56c6U
#define TISP_TUNING_SENSOR_FIELD
#endif

#include "imp_log_fun.h"

#include "isp_tseries_dev.h"
#include "isp_mask_rgb2yuv.h"

static char *bpath;
#if defined(PLATFORM_T23)
/* The T23 tuning API (isp_t23_tuning.c) owns the per-sensor contrast. */
uint8_t openimp_t23_isp_custom_contrast(void);
void openimp_t23_isp_tuning_enabled(void);
#define TSERIES_CUSTOM_CONTRAST openimp_t23_isp_custom_contrast()
#else
/* Vendor libimp .data initialises both to 0x80 (T31 HLIL 0x1075d8); the
 * tuning worker pushes (gain << 8) | custom_contrast every second, so 0 here
 * forced contrast to zero until the application called SetContrast. */
static uint8_t custom_contrast = 0x80;
static uint8_t custom_sharpness = 0x80;
/*
 * Vendor libimp (T31 HLIL data_1075d4) initialises this to 2 and only the
 * *_internal(value, 0/1) paths ever change it; nothing derives it from the
 * ISP running mode.  Mirroring the running mode here made Set{Contrast,
 * Sharpness} store-only while in night mode, so those changes were lost.
 * (T23 keeps its own copy, t23_global_mode in isp_t23_tuning.c, also 2.)
 */
static uint32_t global_mode = 2;
#define TSERIES_CUSTOM_CONTRAST custom_contrast
#endif
static int tseries_isp_stream_started;
static int tseries_bypass_link_setup_done;
static pthread_t tseries_tuning_thread;
static volatile int tseries_tuning_thread_stop;
static int tseries_tuning_thread_running;
/* Stop wake-up for the tuning worker: it waits on this condvar for its
 * one-second period instead of sleep(1), so a stop does not stall
 * IMP_ISP_DisableTuning for up to a second. */
static pthread_mutex_t tseries_tuning_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t tseries_tuning_cond;
static pthread_once_t tseries_tuning_cond_once = PTHREAD_ONCE_INIT;
static clockid_t tseries_tuning_clock = CLOCK_REALTIME;
static int32_t tseries_tuning_last_total_gain = -1;
#if !defined(PLATFORM_T21) || defined(PLATFORM_T20)
/* cache-only RawDRC and denoise attributes (T21: driver, see below) */
static IMPISPDrcAttr tseries_raw_drc = {
    .mode = IMPISP_DRC_MANUAL,
    .drc_strength = 128,
    .slop_max = 128,
    .slop_min = 128,
    .black_level = 0,
    .white_level = 0xfff,
};
static IMPISPSinterDenoiseAttr tseries_sinter_dns = {
    .enable = IMPISP_TUNING_OPS_MODE_ENABLE,
    .type = IMPISP_TUNING_OPS_TYPE_AUTO,
    .sinter_strength = 128,
};
static IMPISPTemperDenoiseAttr tseries_temper_dns = {
    .type = IMPISP_TEMPER_AUTO,
    .temper_strength = 128,
};
#endif

int IMP_ISP_Tuning_SetContrast_internal(uint32_t arg1, int32_t arg2);
int IMP_ISP_Tuning_SetSharpness_internal(uint32_t arg1, int32_t arg2);

/* Early forward decl for the kmsg trace helper (defined later in file). */
static void kmsg_trace(const char *fmt, ...);

int IMP_ISP_Open(void)
{
    int32_t result = 0;

    kmsg_trace("libimp/ISP: Open entry gISP=%p\n", (void *)gISP);

    /* Early kmsg trace — confirm this port's IMP_ISP_Open is the one invoked. */
    {
        int kfd = open("/dev/kmsg", O_WRONLY);
        if (kfd >= 0) {
            char buf[128];
            int n = snprintf(buf, sizeof(buf),
                "libimp/ISP: Open entry, gISP=%p (before)\n", (void *)gISP);
            if (n > 0) write(kfd, buf, (size_t)n);
            close(kfd);
        }
    }

    if (gISP == NULL) {
#if defined(PLATFORM_T23)
        void *v0_2 = calloc(0xf0, 1);
#else
        void *v0_2 = calloc(0xe0, 1);
#endif
        gISP = v0_2;
        {
            int kfd = open("/dev/kmsg", O_WRONLY);
            if (kfd >= 0) {
                char buf[128];
                int n = snprintf(buf, sizeof(buf),
                    "libimp/ISP: Open calloc'd gISP=%p\n", (void *)v0_2);
                if (n > 0) write(kfd, buf, (size_t)n);
                close(kfd);
            }
        }
        if (v0_2 == NULL) {
            result = -1;
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
                "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x135,
                "IMP_ISP_Open", "Failed to alloc gISPdev!\n");
            return result;
        }

        __builtin_strcpy((char *)v0_2,
#if defined(PLATFORM_T20)
                         "/dev/v4l-subdev0"
#else
                         "/dev/tx-isp"
#endif
        );
        ((ISPDevice *)v0_2)->fd = open((char *)v0_2, 0x80002, 0);
        kmsg_trace("libimp/ISP: Open %s fd=%d errno=%d\n",
                   (char *)v0_2,
                   ((ISPDevice *)v0_2)->fd,
                   ((ISPDevice *)v0_2)->fd < 0 ? errno : 0);
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

    kmsg_trace("libimp/ISP: Open return=%d gISP=%p\n", result,
               (void *)gISP);

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
            uint32_t size;      /* 0x24, not set by the stock library */
            int32_t reg;        /* 0x28, 64-bit in the kernel */
            int32_t zero0;
            int32_t val;        /* 0x30, 64-bit in the kernel */
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
        _Static_assert(sizeof(var_40) == 0x38, "the kernel copies 0x38 bytes");
        var_40.size = 0;
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
            uint32_t size;      /* 0x24, not set by the stock library */
            int32_t reg;        /* 0x28, 64-bit in the kernel */
            int32_t zero0;
            int32_t val;        /* 0x30, 64-bit in the kernel */
            int32_t zero1;
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
        _Static_assert(sizeof(var_48) == 0x38, "the kernel copies 0x38 bytes");
        var_48.size = 0;
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

#if !defined(PLATFORM_T23) /* T23: isp_t23_tuning.c */
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
            TISP_TUNING_SENSOR_FIELD
        } var_18 = { 1, 0x8000027, 0 };
        int32_t result;
#if defined(PLATFORM_T20)
        /* The T20 apical SDK handler (apical_isp_g_totalgain) does
         * copy_to_user(control->value, &total_gain): the value word is the
         * address of the result, as in stock T20 3.12.0 libimp
         * (IMP_ISP_Tuning_GetTotalGain at 0x5a70c passes &local).  With 0
         * there the copy faulted silently and the gain read back as 0. */
        uint32_t gain = 0;

        var_18.value = (int32_t)(intptr_t)&gain;
        result = ioctl(gISP_1->tuning_fd, TISP_TUNING_IOCTL, &var_18);
        if (result == 0) {
            *arg1 = gain;
        }
#else
        result = ioctl(gISP_1->tuning_fd, TISP_TUNING_IOCTL, &var_18);

        if (result == 0) {
            *arg1 = (uint32_t)var_18.value;
        }
#endif
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
                    TISP_TUNING_SENSOR_FIELD
                } var_18 = { 0, 0x8000023, arg1 };
                int32_t result = ioctl(gISP_1->tuning_fd, TISP_TUNING_IOCTL, &var_18);

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
            TISP_TUNING_SENSOR_FIELD
        } var_20 = { 1, 0x8000023, 0 };
        int32_t result = ioctl(gISP_1->tuning_fd, TISP_TUNING_IOCTL, &var_20);

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
#endif /* !PLATFORM_T23 */

enum {
    TISP_V4L2_CID_BRIGHTNESS = 0x980900,
    TISP_V4L2_CID_CONTRAST = 0x980901,
    TISP_V4L2_CID_SATURATION = 0x980902,
    TISP_V4L2_CID_HFLIP = 0x980914,
    TISP_V4L2_CID_VFLIP = 0x980915,
    TISP_V4L2_CID_POWER_LINE_FREQUENCY = 0x980918,
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
    TISP_VIDIOC_TUNING = TISP_TUNING_IOCTL,
    TISP_VIDIOC_G_CTRL = 0xc008561b,
    TISP_VIDIOC_S_CTRL = 0xc008561c,
    TISP_CID_WB_ATTR = 0x8000004,
    TISP_CID_WB_STATS = 0x8000005,
    TISP_CID_AWB_WEIGHT = 0x8000006,
    TISP_CID_AWB_HIST = 0x8000007,
    TISP_CID_AWB_CWF_SHIFT = 0x8000008,
    TISP_CID_WB_GOL_STATS = 0x8000009,
    TISP_CID_AWB_ZONE = 0x800000b,
    TISP_CID_WB_ALGO = 0x800000c,
    TISP_CID_AWB_CT = 0x800000d,
    TISP_CID_AWB_CLUSTER = 0x800000e,
    TISP_CID_AWB_CT_TREND = 0x800000f,
#if defined(PLATFORM_T23) || defined(PLATFORM_T31)
    /* Verified against the stock T23/T31 firmware s_ctrl/g_ctrl dispatch:
     * 0x8000020 is rejected there; the AE attribute block is 0x8000035. */
    TISP_CID_AE_ATTR = 0x8000035,
#else
    TISP_CID_AE_ATTR = 0x8000020,
#endif
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
#if defined(PLATFORM_T23) || defined(PLATFORM_T31)
    TISP_CID_AE_HIST_ORIGIN = 0x8000031,
#else
    TISP_CID_AE_HIST_ORIGIN = 0x800002f,
#endif
    TISP_CID_AE_ZONE = 0x8000030,
    /* OEM T23/T31 dispatches tisp_get_ae_luma at 0x8000033.  Command
     * 0x8000031 is a different statistics query; treating its first word as
     * luma makes bright scenes read near zero and forces RIC into night mode. */
#if defined(PLATFORM_T21) && !defined(PLATFORM_T20)
    /* T21: the stock libimp sends {1, 0x8000031} on 0xc00c56c6 and the stock
     * and open tx-isp-t21 g_ctrl dispatch tisp_g_ae_luma there; the stock
     * dispatcher rejects 0x8000033.  Same id as claude/t21-bringup c1bd3b0. */
    TISP_CID_AE_LUMA = 0x8000031,
#else
    TISP_CID_AE_LUMA = 0x8000033,
#endif
    TISP_CID_AE_IT_MAX = 0x8000032,
#if defined(PLATFORM_T23) || defined(PLATFORM_T31)
    TISP_CID_AE_MIN = 0x800002f,    /* 16 bytes: it, again, it_short, again_short */
#elif defined(PLATFORM_T21) && !defined(PLATFORM_T20)
    /* T21 1.0.33 libimp Get/SetAeMin send 0x800002f (8 bytes: min
     * integration time, min analogue gain); the T21 kernel rejects
     * 0x8000033 in both directions. */
    TISP_CID_AE_MIN = 0x800002f,
#else
    TISP_CID_AE_MIN = 0x8000033,
#endif
    TISP_CID_AE_FREEZE = 0x8000034,
#if defined(PLATFORM_T23) || defined(PLATFORM_T31) || \
    defined(PLATFORM_T20) || defined(PLATFORM_T21)
    /* 0x8000035 is SetAeAttr on T23/T31: sending ROI weights there switched
     * AE to manual with ROI bytes as its flags and froze it.  The vendor
     * T20 3.12.0 and T21 1.0.33 AE_Set/GetROI send 0x8000024 as well
     * (IMAGE_TUNING_CID_AE_ROI in the T20 SDK); the T20 driver rejects 0x35. */
    TISP_CID_AE_ROI = 0x8000024,
#else
    TISP_CID_AE_ROI = 0x8000035,
#endif
    TISP_CID_AE_STATE = 0x8000036,
    TISP_CID_BACKLIGHT_COMP = 0x8000037,
    TISP_CID_AE_TARGET_LIST = 0x8000038,
    TISP_CID_DEFOG_STRENGTH = 0x8000039,
    /* vendor libimp 1.1.6: AfHist 0x42, AFMetrices 0x43, AfWeight 0x44,
     * GetSensorAttr 0x45, AfZone 0x46 */
    TISP_CID_AF_HIST = 0x8000042,
    TISP_CID_AF_METRICES = 0x8000043,
    TISP_CID_AF_WEIGHT = 0x8000044,
    TISP_CID_SENSOR_ATTR = 0x8000045,
    TISP_CID_AF_ZONE = 0x8000046,
    TISP_CID_DPC_RATIO = 0x8000062,
    TISP_CID_NCU_INFO = 0x8000084,
#if defined(PLATFORM_T20)
    /* T20 SDK tx-isp-core-tuning.h numbering (PRIVATE_BASE + 0x80 block):
     * SINTER_ATTR 0x81 (struct), TEMPER_STRENGTH 0x82 (scalar), DRC_ATTR 0xa0
     * (struct).  The T20 driver rejects the T31 ids 0x85/0x86/0xa2. */
    TISP_CID_2DNS_ATTR = 0x8000081,
    TISP_CID_3DNS_RATIO = 0x8000082,
    TISP_CID_DRC_ATTR = 0x80000a0,
#else
    /* T21 1.0.33 SetTemperStrength/Set/GetDRC_Strength send 0x8000085 and
     * 0x80000a2, the T31 numbers. */
    TISP_CID_3DNS_RATIO = 0x8000085,
    TISP_CID_2DNS_RATIO = 0x8000086,
    TISP_CID_DRC_RATIO = 0x80000a2,
#endif
    TISP_CID_MODULE_CONTROL = 0x80000e2,
    TISP_CID_WAIT_FRAME = 0x8000162,
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
#if defined(PLATFORM_T20)
    /* IMAGE_TUNING_CID_CUSTOM_BASE + 7/8/10 in the T20 V4L2 tuning ABI
     * (T20 3.12.0 SetISPProcess/SetFWFreeze/SetShading). */
    TISP_CID_ISP_PROCESS = 0x0098e907,
    TISP_CID_FW_FREEZE = 0x0098e908,
    TISP_CID_SHADING = 0x0098e90a
#else
    TISP_CID_ISP_PROCESS = 0x8000164,
    TISP_CID_FW_FREEZE = 0x8000165,
    TISP_CID_SHADING = 0x8000166
#endif
};

typedef struct TSeriesTuningValReq {
    int32_t cmd;
    int32_t subcmd;
    int32_t value;
    TISP_TUNING_SENSOR_FIELD
} TSeriesTuningValReq;

typedef struct TSeriesTuningPtrReq {
    int32_t cmd;
    int32_t subcmd;
    void *ptr;
    TISP_TUNING_SENSOR_FIELD
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

#if !defined(PLATFORM_T23) /* T23: isp_t23_tuning.c */
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
#endif /* !PLATFORM_T23 */
static void *tseries_ae_func_tmp;
static void *tseries_awb_func_tmp;
static int32_t tseries_ae_algo_en;
static int32_t tseries_awb_algo_en;

static int tseries_get_isp(ISPDevice **out);
#if !defined(PLATFORM_T23) /* T23: isp_t23_tuning.c */
static int tseries_tuning_set_val(int32_t subcmd, int32_t value);
#endif /* !PLATFORM_T23 */
static int tseries_tuning_get_val(int32_t subcmd, int32_t *value);
#if !defined(PLATFORM_T23) /* T23: isp_t23_tuning.c */
static int tseries_tuning_get_wb_stats(int32_t subcmd, IMPISPWB *wb);
#endif /* !PLATFORM_T23 */
static int tseries_tuning_set_ptr(int32_t subcmd, void *ptr);
static int tseries_tuning_get_ptr(int32_t subcmd, void *ptr);
static int tseries_v4l2_set(int32_t id, int32_t value);
#if !defined(PLATFORM_T23) /* T23: isp_t23_tuning.c */
static int tseries_v4l2_get(int32_t id, int32_t *value);
#endif /* !PLATFORM_T23 */

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

#if !defined(PLATFORM_T23) /* T23: isp_t23_tuning.c */
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
#endif /* !PLATFORM_T23 */

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

#if defined(PLATFORM_T20)
/* Get for T20 apical SDK g_ctrl handlers that copy_to_user() their result
 * through control->value instead of storing it there. */
static int tseries_tuning_get_val_indirect(int32_t subcmd, int32_t *value)
{
    ISPDevice *isp;
    int32_t out = 0;

    if (value == NULL) {
        return -1;
    }

    if (tseries_get_isp(&isp) != 0 || isp->tuning == NULL || isp->tuning_state != 2) {
        return -1;
    }

    {
        TSeriesTuningValReq req = { 1, subcmd, (int32_t)(intptr_t)&out };
        int result = ioctl(isp->tuning_fd, TISP_VIDIOC_TUNING, &req);

        if (result == 0) {
            *value = out;
        }

        return result;
    }
}
#endif

#if !defined(PLATFORM_T23) /* T23: isp_t23_tuning.c */
static int tseries_tuning_get_wb_stats(int32_t subcmd, IMPISPWB *wb)
{
    int32_t packed = 0;
    int result;

    if (wb == NULL) {
        return -1;
    }

    result = tseries_tuning_get_val(subcmd, &packed);
    if (result == 0) {
        wb->rgain = (uint16_t)((uint32_t)packed >> 16);
        wb->bgain = (uint16_t)packed;
    }

    return result;
}
#endif /* !PLATFORM_T23 */

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

#if !defined(PLATFORM_T23) /* T23: isp_t23_tuning.c */
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
#endif /* !PLATFORM_T23 */

#if defined(PLATFORM_T21) /* T21 and T20 */
/*
 * Port of the vendor's local isp_table_tuning_ratio() (T21 1.0.33 libimp
 * 0x4eab0, T20 3.12.0 0x57fc0; both identical).  SetSinterStrength (table
 * 109, both) and the T20 SetTemperStrength (table 132) scale a driver
 * tuning table in userspace instead of using a control id:
 *
 *   1. tuning GET 0x8000161 with {table, 0, 0, 0, 1, NULL} returns the
 *      table geometry (rows, columns, element size 1/2/4);
 *   2. tuning GET again with flag 1 and a rows*cols*size buffer reads it;
 *   3. element 1 of every row (the vendor indexes row*2+1, i.e. a
 *      two-column table) becomes (uint32)(value * ratio) / 100.0, truncated
 *      to the element width;
 *   4. tuning SET 0x8000161 with flag 0 writes the buffer back.
 *
 * The vendor rescales whatever the driver currently holds, so repeated
 * calls compound (150 then 100 leaves 1.5x).  This port keeps the first
 * table it read as the baseline and scales from that, so the ratio is
 * absolute; the baseline is dropped on a running-mode change because the
 * driver may load the other day/night table then.
 */
#define TISP_CID_TABLE_TUNING 0x8000161
#define TSERIES_TABLE_SINTER 109
#define TSERIES_TABLE_TEMPER 132

typedef struct {
    uint32_t table;
    uint32_t rows;
    uint32_t cols;
    uint32_t elem_size;
    uint32_t flag;      /* 1: read (NULL buf: geometry only), 0: write */
    void *buf;
} TSeriesTableReq;

typedef struct {
    uint32_t table;
    uint32_t rows;
    uint32_t cols;
    uint32_t elem_size;
    void *base;
} TSeriesTableBaseline;

static TSeriesTableBaseline tseries_table_baseline[2] = {
    { TSERIES_TABLE_SINTER, 0, 0, 0, NULL },
    { TSERIES_TABLE_TEMPER, 0, 0, 0, NULL },
};

static void tseries_table_baseline_reset(void)
{
    size_t i;

    for (i = 0; i < sizeof(tseries_table_baseline) / sizeof(tseries_table_baseline[0]); i++) {
        free(tseries_table_baseline[i].base);
        tseries_table_baseline[i].base = NULL;
    }
}

static __attribute__((unused)) int tseries_table_tuning_ratio(uint32_t table, uint32_t ratio)
{
    TSeriesTableBaseline *bl = NULL;
    TSeriesTableReq req;
    uint8_t *buf;
    size_t size;
    uint32_t row;
    size_t i;
    int result;

    for (i = 0; i < sizeof(tseries_table_baseline) / sizeof(tseries_table_baseline[0]); i++) {
        if (tseries_table_baseline[i].table == table) {
            bl = &tseries_table_baseline[i];
        }
    }

    memset(&req, 0, sizeof(req));
    req.table = table;
    req.flag = 1;
    result = tseries_tuning_get_ptr(TISP_CID_TABLE_TUNING, &req);
    if (result != 0) {
        return -1;
    }

    size = (size_t)req.rows * req.cols * req.elem_size;
    if (size == 0 || req.cols < 2 ||
        (req.elem_size != 1 && req.elem_size != 2 && req.elem_size != 4)) {
        return -1;
    }

    buf = malloc(size);
    if (buf == NULL) {
        return -1;
    }

    if (bl != NULL && bl->base != NULL && bl->rows == req.rows &&
        bl->cols == req.cols && bl->elem_size == req.elem_size) {
        memcpy(buf, bl->base, size);
    } else {
        req.flag = 1;
        req.buf = buf;
        if (tseries_tuning_get_ptr(TISP_CID_TABLE_TUNING, &req) != 0) {
            free(buf);
            return -1;
        }
        if (bl != NULL) {
            free(bl->base);
            bl->base = malloc(size);
            if (bl->base != NULL) {
                memcpy(bl->base, buf, size);
                bl->rows = req.rows;
                bl->cols = req.cols;
                bl->elem_size = req.elem_size;
            }
        }
    }

    for (row = 0; row < req.rows; row++) {
        uint8_t *p = buf + ((size_t)row * 2 + 1) * req.elem_size;

        if (req.elem_size == 1) {
            *p = (uint8_t)((uint32_t)(*p * ratio) / 100u);
        } else if (req.elem_size == 2) {
            uint16_t v;

            memcpy(&v, p, sizeof(v));
            v = (uint16_t)((uint32_t)(v * ratio) / 100u);
            memcpy(p, &v, sizeof(v));
        } else {
            uint32_t v;

            memcpy(&v, p, sizeof(v));
            v = (v * ratio) / 100u;
            memcpy(p, &v, sizeof(v));
        }
    }

    req.flag = 0;
    req.buf = buf;
    result = tseries_tuning_set_ptr(TISP_CID_TABLE_TUNING, &req);
    free(buf);
    return result == 0 ? 0 : -1;
}

/*
 * T21 1.0.33 / T20 3.12.0 SetISPRunningMode: after 0x80000e1 succeeds the
 * vendor re-sends contrast, brightness, saturation and sharpness through
 * VIDIOC_S_CTRL as cached_byte | ((mode + 1) << 8), in that order, so the
 * driver switches those blocks to their day (0x100) or night (0x200)
 * parameter sets.  The cache is the tuning block bytes +8..+0xb (B/C/S/
 * sharpness), the same bytes Set/GetBrightness etc. maintain.
 */
static void tseries_running_mode_reapply_bcs(ISPDevice *isp, uint32_t mode)
{
    static const struct {
        int32_t id;
        uint8_t offset;
    } ctrls[] = {
        { 0x980901, 9 },   /* contrast */
        { 0x980900, 8 },   /* brightness */
        { 0x980902, 10 },  /* saturation */
        { 0x98091b, 11 },  /* sharpness */
    };
    uint32_t high = (mode + 1) << 8;
    size_t i;

    for (i = 0; i < sizeof(ctrls) / sizeof(ctrls[0]); i++) {
        uint8_t cached = *(uint8_t *)((char *)isp->tuning + ctrls[i].offset);

        if (tseries_v4l2_set(ctrls[i].id, (int32_t)(cached | high)) < 0) {
            kmsg_trace("libimp/ISP: RunningMode %u S_CTRL %#x failed errno=%d\n",
                       mode, ctrls[i].id, errno);
        }
    }
}
#endif /* PLATFORM_T21 */

/*
 * The T31 OEM EnableTuning path starts an isp_tuning_deamon and registers
 * isp_tuning_func_update_total_gain plus isp_tuning_func_contrast_judge.
 * update_total_gain reads TISP_CID_TOTAL_GAIN; contrast_judge then submits
 *
 *     (total_gain << 8) | custom_contrast
 *
 * through V4L2_CID_CONTRAST whenever the gain changes.  This is not merely
 * telemetry: the ISP driver uses the high bits to select the gain-dependent
 * contrast/noise-processing state.  Omitting the daemon leaves that state at
 * its startup value while AE continues to move the sensor gain, which shows
 * up most clearly as crawling shadows on flat dark walls.
 *
 * The OEM isp_tuning_deamon_thread runs these callbacks once per second.
 */
static void tseries_tuning_cond_init(void)
{
    pthread_condattr_t attr;

    if (pthread_condattr_init(&attr) == 0) {
        if (pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) == 0 &&
            pthread_cond_init(&tseries_tuning_cond, &attr) == 0) {
            tseries_tuning_clock = CLOCK_MONOTONIC;
            pthread_condattr_destroy(&attr);
            return;
        }
        pthread_condattr_destroy(&attr);
    }
    tseries_tuning_clock = CLOCK_REALTIME;
    pthread_cond_init(&tseries_tuning_cond, NULL);
}

/* Wait one tuning period (1 s) or until tseries_stop_tuning_worker(). */
static void tseries_tuning_wait_period(void)
{
    struct timespec deadline;
    int rc = 0;

    if (clock_gettime(tseries_tuning_clock, &deadline) != 0) {
        sleep(1);
        return;
    }
    deadline.tv_sec += 1;

    pthread_mutex_lock(&tseries_tuning_lock);
    while (!tseries_tuning_thread_stop) {
        rc = pthread_cond_timedwait(&tseries_tuning_cond,
                                    &tseries_tuning_lock, &deadline);
        if (rc != 0)
            break;
    }
    pthread_mutex_unlock(&tseries_tuning_lock);

    /* Neither a timeout nor a wake-up: never turn the loop into a spin. */
    if (rc != 0 && rc != ETIMEDOUT && !tseries_tuning_thread_stop)
        sleep(1);
}

static void *tseries_tuning_worker(void *unused)
{
    (void)unused;

    while (!tseries_tuning_thread_stop) {
        int32_t total_gain = 0;

        if (tseries_tuning_get_val(TISP_CID_TOTAL_GAIN, &total_gain) == 0 &&
            total_gain >= 0 &&
            total_gain != tseries_tuning_last_total_gain) {
            uint32_t packed =
                ((uint32_t)total_gain << 8) | (uint32_t)TSERIES_CUSTOM_CONTRAST;

            /* Remember the gain before sending, as the OEM daemon does: a
             * driver that rejects the value must not be fed the same one
             * again every second. */
            tseries_tuning_last_total_gain = total_gain;
            if (tseries_v4l2_set(TISP_V4L2_CID_CONTRAST,
                                 (int32_t)packed) == 0) {
                static unsigned int update_count;

                update_count++;
                if (update_count <= 4u || (update_count % 100u) == 0u)
                    kmsg_trace("libimp/ISP: tuning gain/contrast update gain=%d contrast=%u packed=0x%08x count=%u\n",
                               total_gain, TSERIES_CUSTOM_CONTRAST, packed,
                               update_count);
            }
        }

        tseries_tuning_wait_period();
    }

    return NULL;
}

static int tseries_start_tuning_worker(void)
{
    if (tseries_tuning_thread_running)
        return 0;

    pthread_once(&tseries_tuning_cond_once, tseries_tuning_cond_init);
    tseries_tuning_last_total_gain = -1;
    tseries_tuning_thread_stop = 0;
    if (pthread_create(&tseries_tuning_thread, NULL,
                       tseries_tuning_worker, NULL) != 0)
        return -1;

    tseries_tuning_thread_running = 1;
    return 0;
}

static void tseries_stop_tuning_worker(void)
{
    if (!tseries_tuning_thread_running)
        return;

    pthread_mutex_lock(&tseries_tuning_lock);
    tseries_tuning_thread_stop = 1;
    pthread_cond_signal(&tseries_tuning_cond);
    pthread_mutex_unlock(&tseries_tuning_lock);
    pthread_join(tseries_tuning_thread, NULL);
    tseries_tuning_thread_running = 0;
    tseries_tuning_last_total_gain = -1;
}

#if !defined(PLATFORM_T23) /* T23: isp_t23_tuning.c */
int IMP_ISP_Tuning_SetSensorFPS(uint32_t fps_num, uint32_t fps_den)
{
    int result;

    if (fps_den == 0) {
        return -1;
    }

#if defined(PLATFORM_T20)
    {
        /* T20 apical_isp_fps_s_control copy_from_user()s the value from
         * the address in control->value, like the get side below. */
        uint32_t fps = (fps_num << 16) | (fps_den & 0xffff);

        result = tseries_tuning_set_ptr(TISP_CID_SENSOR_FPS, &fps);
    }
#else
    result = tseries_tuning_set_val(TISP_CID_SENSOR_FPS,
        ((int32_t)fps_num << 16) | (fps_den & 0xffff));
#endif
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

#if defined(PLATFORM_T20)
    /* T20 apical_isp_fps_g_control copy_to_user()s through control->value. */
    result = tseries_tuning_get_val_indirect(TISP_CID_SENSOR_FPS, &value);
#else
    result = tseries_tuning_get_val(TISP_CID_SENSOR_FPS, &value);
#endif
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
    int result;

    if (attr != IMPISP_ANTIFLICKER_DISABLE &&
        attr != IMPISP_ANTIFLICKER_50HZ &&
        attr != IMPISP_ANTIFLICKER_60HZ) {
        return -1;
    }

    result = tseries_v4l2_set(TISP_V4L2_CID_POWER_LINE_FREQUENCY,
                              (int32_t)attr);
    if (result == 0) {
        tseries_antiflicker_attr = attr;
    }

    return result;
}

int IMP_ISP_Tuning_GetAntiFlickerAttr(IMPISPAntiflickerAttr *pattr)
{
    int32_t value = 0;
    int result;

    if (pattr == NULL) {
        return -1;
    }

    result = tseries_v4l2_get(TISP_V4L2_CID_POWER_LINE_FREQUENCY, &value);
    if (result != 0 || value < IMPISP_ANTIFLICKER_DISABLE ||
        value > IMPISP_ANTIFLICKER_60HZ) {
        return -1;
    }

    tseries_antiflicker_attr = (IMPISPAntiflickerAttr)value;
    *pattr = tseries_antiflicker_attr;
    return 0;
}

int IMP_ISP_Tuning_SetISPRunningMode(IMPISPRunningMode mode)
{
#if defined(PLATFORM_T20)
    /* T20 apical_isp_day_or_night_s_ctrl copy_from_user()s the mode from
     * the address in control->value.  An inline value made that copy fail
     * silently: the ISP never left day mode (colour, no clip_min_uv 512)
     * while timps' day/night logic believed it had switched. */
    int32_t value = (int32_t)mode;
    int result = tseries_tuning_set_ptr(TISP_CID_RUNNING_MODE, &value);
#else
    int result = tseries_tuning_set_val(TISP_CID_RUNNING_MODE, mode);
#endif

    if (result == 0) {
#if defined(PLATFORM_T21) /* T21 and T20 */
        ISPDevice *isp;

#if defined(PLATFORM_T20)
        /* The vendor uses the value the driver left in &value. */
        if (tseries_get_isp(&isp) == 0 && isp->tuning != NULL) {
            tseries_running_mode_reapply_bcs(isp, (uint32_t)value);
        }
#else
        if (tseries_get_isp(&isp) == 0 && isp->tuning != NULL) {
            tseries_running_mode_reapply_bcs(isp, (uint32_t)mode);
        }
#endif
        if (mode != tseries_running_mode) {
            tseries_table_baseline_reset();
        }
#endif
        tseries_running_mode = mode;
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

#if defined(PLATFORM_T20)
    /* T20 apical_isp_day_or_night_g_ctrl copies its result through
     * control->value like GetTotalGain above; stock T20 libimp
     * (IMP_ISP_Tuning_GetISPRunningMode at 0x5dddc) passes &local. */
    result = tseries_tuning_get_val_indirect(TISP_CID_RUNNING_MODE, &value);
#else
    result = tseries_tuning_get_val(TISP_CID_RUNNING_MODE, &value);
#endif
    if (result == 0) {
        tseries_running_mode = value;
    }

    *pmode = tseries_running_mode;
    return result;
}
#endif /* !PLATFORM_T23 */

int IMP_ISP_Tuning_SetISPBypass(IMPISPTuningOpsMode enable)
{
    ISPDevice *isp;
    int32_t destroy_arg;
    int32_t sensor_index;

    if (tseries_get_isp(&isp) != 0) {
        return -1;
    }

#if defined(PLATFORM_T23)
    if (getenv("OPENIMP_T23_SKIP_BYPASS")) {
        kmsg_trace("libimp/ISP: SetISPBypass skipped by T23 cold-start guard enable=%d\n",
                   enable);
        return 0;
    }
#endif

    kmsg_trace("libimp/ISP: SetISPBypass enter enable=%d isp=%p fd=%d tuning_fd=%d\n",
               enable, (void *)isp, isp->fd, isp->tuning_fd);

#if defined(PLATFORM_T20)
    /* T20 has no userspace-managed media link graph.  The selected sensor
     * owns the fixed pipeline and video0 exposes the ISP-process control. */
    if (tseries_v4l2_set(TISP_CID_ISP_PROCESS, enable) != 0) {
        kmsg_trace("libimp/ISP: SetISPBypass T20 S_CTRL failed value=%d errno=%d\n",
                   enable, errno);
        return -1;
    }
    tseries_bypass_link_setup_done = 1;
    kmsg_trace("libimp/ISP: SetISPBypass T20 S_CTRL ok value=%d\n", enable);
    return 0;
#endif

#if defined(PLATFORM_T21)
    /* T21 exposes one media-link configuration (index zero).  Its OEM libimp
     * nevertheless derives index one for DISABLE, which the driver rejects
     * after the old graph has already been destroyed.  EnableSensor installed
     * the normal processed graph, so disabling an already-disabled bypass is
     * idempotent and must leave that graph intact. */
    if (enable == IMPISP_TUNING_OPS_MODE_DISABLE) {
        kmsg_trace("libimp/ISP: SetISPBypass T21 normal graph already active\n");
        tseries_bypass_link_setup_done = 1;
        return 0;
    }
#endif

    if (ioctl(isp->fd, TISP_VIDIOC_DISABLE_LINKS, 0) != 0) {
        kmsg_trace("libimp/ISP: SetISPBypass DISABLE_LINKS failed errno=%d\n", errno);
        return -1;
    }
    kmsg_trace("libimp/ISP: SetISPBypass DISABLE_LINKS ok\n");

    /* OEM sets DESTROY_LINKS arg to -1 before rebuilding the link graph. */
    destroy_arg = -1;
    if (ioctl(isp->fd, TISP_VIDIOC_DESTROY_LINKS, &destroy_arg) != 0) {
        kmsg_trace("libimp/ISP: SetISPBypass DESTROY_LINKS failed arg=%d errno=%d\n",
                   destroy_arg, errno);
        return -1;
    }
    kmsg_trace("libimp/ISP: SetISPBypass DESTROY_LINKS ok arg=%d\n", destroy_arg);

    if (tseries_v4l2_set(TISP_CID_ISP_PROCESS, enable) != 0) {
        kmsg_trace("libimp/ISP: SetISPBypass S_CTRL ISP_PROCESS failed value=%d errno=%d\n",
                   enable, errno);
        return -1;
    }
    kmsg_trace("libimp/ISP: SetISPBypass S_CTRL ISP_PROCESS ok value=%d\n", enable);

#if defined(PLATFORM_T21)
    /* T21 has one link configuration.  This branch is reachable only when
     * bypass is enabled; the normal path above remains undisturbed. */
    sensor_index = 0;
#else
    sensor_index = -1;
    if (ioctl(isp->fd, TISP_VIDIOC_GET_SENSOR_INDEX, &sensor_index) != 0 ||
        sensor_index < 0) {
        kmsg_trace("libimp/ISP: SetISPBypass GET_SENSOR_INDEX failed errno=%d idx=%d\n",
                   errno, sensor_index);
        return -1;
    }
#endif

    if (ioctl(isp->fd, TISP_VIDIOC_CREATE_LINKS, &sensor_index) != 0) {
        kmsg_trace("libimp/ISP: SetISPBypass CREATE_LINKS failed arg=%d errno=%d\n",
                   sensor_index, errno);
        return -1;
    }
    kmsg_trace("libimp/ISP: SetISPBypass CREATE_LINKS ok arg=%d\n", sensor_index);

    if (ioctl(isp->fd, TISP_VIDIOC_ENABLE_LINKS, 0) != 0) {
        kmsg_trace("libimp/ISP: SetISPBypass ENABLE_LINKS failed errno=%d\n", errno);
        return -1;
    }
    kmsg_trace("libimp/ISP: SetISPBypass ENABLE_LINKS ok arg=%d\n", sensor_index);

    /* Rebuild the graph during bypass reconfiguration, then let the later
     * EnsureLinkStreamOn replay only the STREAMON edge once the frame channels
     * are primed. This matches the legacy/userspace path that previously
     * reached AVPU interrupts without duplicating CREATE/ENABLE. */
    tseries_bypass_link_setup_done = 1;
    tseries_isp_stream_started = 0;
    kmsg_trace("libimp/ISP: SetISPBypass rearm-stream-only sensor=%d\n", sensor_index);

    return 0;
}

int ISP_EnsureLinkStreamOn(int32_t sensor_idx)
{
    ISPDevice *isp;

    if (tseries_get_isp(&isp) != 0) {
        kmsg_trace("libimp/ISP: EnsureLinkStreamOn tseries_get_isp failed\n");
        return -1;
    }

#if defined(PLATFORM_T21)
    /* IMP_ISP_EnableSensor already starts the sensor and enables its link
     * graph.  Stock T21 FrameSource_EnableChn goes directly from REQBUFS to
     * frame-channel STREAMON; replaying the ISP-wide sensor STREAMON here
     * wedges the first interrupt transition. */
    (void)sensor_idx;
    tseries_isp_stream_started = 1;
    kmsg_trace("libimp/ISP: EnsureLinkStreamOn T21 already active\n");
    return 0;
#endif

    if (tseries_isp_stream_started != 0) {
        kmsg_trace("libimp/ISP: EnsureLinkStreamOn already-started bypass_done=%d\n",
                   tseries_bypass_link_setup_done);
        return 0;
    }

    kmsg_trace("libimp/ISP: EnsureLinkStreamOn STREAMON sensor=%d fd=%d\n",
               sensor_idx, isp->fd);
    if (ioctl(isp->fd, 0x80045612, 0) != 0) {
        kmsg_trace("libimp/ISP: EnsureLinkStreamOn STREAMON failed errno=%d\n", errno);
        return -1;
    }

    if (tseries_bypass_link_setup_done == 0) {
        int32_t link_arg = sensor_idx;

        kmsg_trace("libimp/ISP: EnsureLinkStreamOn CREATE_LINKS sensor=%d\n", link_arg);
        if (ioctl(isp->fd, TISP_VIDIOC_CREATE_LINKS, &link_arg) != 0) {
            kmsg_trace("libimp/ISP: EnsureLinkStreamOn CREATE_LINKS failed errno=%d\n", errno);
            return -1;
        }

        kmsg_trace("libimp/ISP: EnsureLinkStreamOn ENABLE_LINKS sensor=%d\n", link_arg);
        if (ioctl(isp->fd, TISP_VIDIOC_ENABLE_LINKS, 0) != 0) {
            kmsg_trace("libimp/ISP: EnsureLinkStreamOn ENABLE_LINKS failed errno=%d\n", errno);
            return -1;
        }
    } else {
        kmsg_trace("libimp/ISP: EnsureLinkStreamOn skip-link-setup bypass_done=1\n");
    }

    tseries_isp_stream_started = 1;
    kmsg_trace("libimp/ISP: EnsureLinkStreamOn done sensor=%d\n", sensor_idx);
    return 0;
}

#if !defined(PLATFORM_T23) /* T23: isp_t23_tuning.c */
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

#if defined(PLATFORM_T20)
/* T20 SDK attribute struct behind TISP_CID_DRC_ATTR (struct
 * isp_core_drc_attr in tx-isp-core-tuning.h).  The driver
 * copy_from_user()s the whole struct, so fill every field. */
typedef struct {
    int32_t mode;              /* ISPMODULE_DRC_MODE */
    uint8_t strength;
    uint8_t slope_max;
    uint8_t slope_min;
    uint16_t black_level;
    uint16_t white_level;
} TSeriesT20DrcAttr;

/* Temper strength is a userspace table scale on T20 (see
 * tseries_table_tuning_ratio); there is nothing to read back, so report the
 * last ratio set (100 = tuning-bin table unchanged). */
static uint32_t tseries_t20_temper_ratio = 100;
#endif

#if defined(PLATFORM_T21) /* T21 and T20 */
/* Same for sinter strength on T21 and T20. */
#if defined(PLATFORM_T20)
static uint32_t tseries_t2x_sinter_ratio = 100;
#else
static uint32_t tseries_t2x_sinter_ratio = 128; /* T21: neutral, see isp_t21_sinter.h */
#endif
#endif

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
#if defined(PLATFORM_T20)
    /* DRC_ATTR set applies attr.strength (IRIDIX_STRENGTH_ID); read the
     * current struct first so the other fields are written back unchanged. */
    TSeriesT20DrcAttr attr;

    memset(&attr, 0, sizeof(attr));
    if (tseries_tuning_get_ptr(TISP_CID_DRC_ATTR, &attr) != 0) {
        return -1;
    }
    attr.strength = (uint8_t)(ratio > 255 ? 255 : ratio);
    return tseries_tuning_set_ptr(TISP_CID_DRC_ATTR, &attr);
#else
    return tseries_tuning_set_val(TISP_CID_DRC_RATIO, ratio);
#endif
}
#endif /* !PLATFORM_T23 */

#if defined(PLATFORM_T21) && !defined(PLATFORM_T20)
/*
 * T21 1.0.33 (isp_tseries.c.o 0x2778..0x3150). The vendor keeps the last
 * DRC mode, temper type and the strengths in its tuning block (zeroed at
 * EnableTuning) and only sends what changed:
 *
 *  - RawDRC: mode through VIDIOC_S_CTRL 0x8000168; in manual mode (0) the
 *    attribute itself through tuning CID 0x80000a0. Get reads 0x80000a0
 *    into the caller's attribute.
 *  - SinterDnsAttr: a 112-byte driver block through tuning CID 0x800002c,
 *    byte 70 = 1, and for MANUAL also bytes 10 and 98 = 1 and byte 43 =
 *    strength. Get reports enable 1, type = (byte 10 != 0), strength =
 *    byte 43. The vendor ignores the enable field on Set.
 *  - TemperDnsAttr / TemperDnsCtl: type through VIDIOC_S_CTRL 0x8000167;
 *    in MANUAL (2) the strength through tuning CID 0x8000083 (Attr, sent
 *    every time) or 0x8000082 (Ctl, only when it changed). Get refreshes
 *    type (G_CTRL 0x8000167) and strength (0x8000083) and returns them.
 */
#define TSERIES_T21_CID_DRC_MODE      0x8000168
#define TSERIES_T21_CID_RAW_DRC       0x80000a0
#define TSERIES_T21_CID_SINTER_DNS    0x800002c
#define TSERIES_T21_CID_TEMPER_TYPE   0x8000167
#define TSERIES_T21_CID_TEMPER_CTL    0x8000082
#define TSERIES_T21_CID_TEMPER_ATTR   0x8000083
#define TSERIES_T21_SINTER_BLOCK      112

static struct {
    uint32_t drc_mode;
    uint8_t drc_strength;
    uint32_t temper_type;
    uint8_t temper_strength;
} tseries_t21_dns_cache;

int IMP_ISP_Tuning_SetRawDRC(IMPISPDrcAttr *attribute)
{
    int result = 0;
    uint32_t mode;

    if (attribute == NULL) {
        return -1;
    }
    mode = (uint32_t)attribute->mode;
    if (mode != tseries_t21_dns_cache.drc_mode) {
        result = tseries_v4l2_set(TSERIES_T21_CID_DRC_MODE, (int32_t)mode);
        if (result == 0) {
            tseries_t21_dns_cache.drc_mode = mode;
        }
    }
    if (mode != IMPISP_DRC_MANUAL ||
        attribute->drc_strength == tseries_t21_dns_cache.drc_strength) {
        return result;
    }
    result = tseries_tuning_set_ptr(TSERIES_T21_CID_RAW_DRC, attribute);
    if (result == 0) {
        tseries_t21_dns_cache.drc_strength = attribute->drc_strength;
    }
    return result;
}

int IMP_ISP_Tuning_GetRawDRC(IMPISPDrcAttr *attribute)
{
    int result;

    if (attribute == NULL) {
        return -1;
    }
    result = tseries_tuning_get_ptr(TSERIES_T21_CID_RAW_DRC, attribute);
    if (result == 0) {
        tseries_t21_dns_cache.drc_mode = (uint32_t)attribute->mode;
        tseries_t21_dns_cache.drc_strength = attribute->drc_strength;
    }
    return result;
}

int IMP_ISP_Tuning_SetSinterDnsAttr(IMPISPSinterDenoiseAttr *attribute)
{
    uint8_t block[TSERIES_T21_SINTER_BLOCK];
    int result;

    if (attribute == NULL) {
        return -1;
    }
    memset(block, 0, sizeof(block));
    if (attribute->type == IMPISP_TUNING_OPS_TYPE_AUTO) {
        t21_sinter_fill_block(block, 0, 0);
    } else if (attribute->type == IMPISP_TUNING_OPS_TYPE_MANUAL) {
        t21_sinter_fill_block(block, 1, attribute->sinter_strength);
    } else {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x4ff,
            "IMP_ISP_Tuning_SetSinterDnsAttr", "unknown sinter type %d\n",
            (int)attribute->type);
    }
    result = tseries_tuning_set_ptr(TSERIES_T21_CID_SINTER_DNS, block);
    if (result != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x506,
            "IMP_ISP_Tuning_SetSinterDnsAttr", "ioctl sinter attr failed\n");
    }
    return result;
}

int IMP_ISP_Tuning_GetSinterDnsAttr(IMPISPSinterDenoiseAttr *attribute)
{
    uint8_t block[TSERIES_T21_SINTER_BLOCK];
    int result;

    if (attribute == NULL) {
        return -1;
    }
    memset(block, 0, sizeof(block));
    result = tseries_tuning_get_ptr(TSERIES_T21_CID_SINTER_DNS, block);
    if (result != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x523,
            "IMP_ISP_Tuning_GetSinterDnsAttr", "ioctl sinter attr failed\n");
    }
    /* the vendor fills the attribute whatever the ioctl returned */
    attribute->enable = IMPISP_TUNING_OPS_MODE_ENABLE;
    attribute->type = block[10] != 0 ? IMPISP_TUNING_OPS_TYPE_MANUAL
                                     : IMPISP_TUNING_OPS_TYPE_AUTO;
    attribute->sinter_strength = block[43];
    return result;
}

static int tseries_t21_temper_set(const IMPISPTemperDenoiseAttr *attribute,
                                  int32_t strength_cid, int only_changes)
{
    uint32_t type;
    int result;

    if (attribute == NULL) {
        return -1;
    }
    type = (uint32_t)attribute->type;
    if (type != tseries_t21_dns_cache.temper_type) {
        result = tseries_v4l2_set(TSERIES_T21_CID_TEMPER_TYPE, (int32_t)type);
        if (result != 0) {
            return result;
        }
        tseries_t21_dns_cache.temper_type = type;
    }
    if (type != IMPISP_TEMPER_MANUAL ||
        (only_changes && attribute->temper_strength ==
                             tseries_t21_dns_cache.temper_strength)) {
        return 0;
    }
    result = tseries_tuning_set_val(strength_cid, attribute->temper_strength);
    if (result == 0) {
        tseries_t21_dns_cache.temper_strength = attribute->temper_strength;
    }
    return result;
}

int IMP_ISP_Tuning_SetTemperDnsCtl(IMPISPTemperDenoiseAttr *attribute)
{
    return tseries_t21_temper_set(attribute, TSERIES_T21_CID_TEMPER_CTL, 1);
}

int IMP_ISP_Tuning_SetTemperDnsAttr(IMPISPTemperDenoiseAttr *attribute)
{
    return tseries_t21_temper_set(attribute, TSERIES_T21_CID_TEMPER_ATTR, 0);
}

int IMP_ISP_Tuning_GetTemperDnsAttr(IMPISPTemperDenoiseAttr *attribute)
{
    int32_t value;
    int result;

    if (attribute == NULL) {
        return -1;
    }
    if (tseries_v4l2_get(TSERIES_T21_CID_TEMPER_TYPE, &value) == 0) {
        tseries_t21_dns_cache.temper_type = (uint32_t)value;
    } else if (gISP == NULL || gISP->tuning == NULL ||
               gISP->tuning_state != 2) {
        return -1;
    }
    result = tseries_tuning_get_val(TSERIES_T21_CID_TEMPER_ATTR, &value);
    if (result == 0) {
        tseries_t21_dns_cache.temper_strength = (uint8_t)value;
    }
    attribute->type = (IMPISPTemperMode)tseries_t21_dns_cache.temper_type;
    attribute->temper_strength = tseries_t21_dns_cache.temper_strength;
    return result;
}
#else
int IMP_ISP_Tuning_SetRawDRC(IMPISPDrcAttr *attribute)
{
    if (!attribute || attribute->mode > IMPISP_DRC_DISABLE)
        return -1;
    tseries_raw_drc = *attribute;
    return 0;
}

int IMP_ISP_Tuning_GetRawDRC(IMPISPDrcAttr *attribute)
{
    if (!attribute)
        return -1;
    *attribute = tseries_raw_drc;
    return 0;
}

int IMP_ISP_Tuning_SetSinterDnsAttr(IMPISPSinterDenoiseAttr *attribute)
{
    if (!attribute || attribute->enable > IMPISP_TUNING_OPS_MODE_ENABLE ||
        attribute->type >= IMPISP_TUNING_OPS_TYPE_BUTT)
        return -1;
    tseries_sinter_dns = *attribute;
    return 0;
}

int IMP_ISP_Tuning_GetSinterDnsAttr(IMPISPSinterDenoiseAttr *attribute)
{
    if (!attribute)
        return -1;
    *attribute = tseries_sinter_dns;
    return 0;
}

int IMP_ISP_Tuning_SetTemperDnsAttr(IMPISPTemperDenoiseAttr *attribute)
{
    if (!attribute || attribute->type > IMPISP_TEMPER_MANUAL)
        return -1;
    tseries_temper_dns = *attribute;
    return 0;
}

int IMP_ISP_Tuning_GetTemperDnsAttr(IMPISPTemperDenoiseAttr *attribute)
{
    if (!attribute)
        return -1;
    *attribute = tseries_temper_dns;
    return 0;
}
#endif /* PLATFORM_T21 && !PLATFORM_T20 */

#if !defined(PLATFORM_T23) /* T23: isp_t23_tuning.c */
int IMP_ISP_Tuning_GetDRC_Strength(uint32_t *pratio)
{
    int32_t value = 0;
    int result;

    if (pratio == NULL) {
        return -1;
    }

#if defined(PLATFORM_T20)
    {
        TSeriesT20DrcAttr attr;

        memset(&attr, 0, sizeof(attr));
        result = tseries_tuning_get_ptr(TISP_CID_DRC_ATTR, &attr);
        value = attr.strength;
    }
#else
    result = tseries_tuning_get_val(TISP_CID_DRC_RATIO, &value);
#endif
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
#if defined(PLATFORM_T20)
    /* T20 3.12.0: isp_table_tuning_ratio(132, min(ratio, 200)); the driver
     * has no temper-strength control. */
    int result = tseries_table_tuning_ratio(TSERIES_TABLE_TEMPER,
                                            ratio > 200 ? 200 : ratio);

    if (result == 0) {
        tseries_t20_temper_ratio = ratio;
    }
    return result;
#else
    return tseries_tuning_set_val(TISP_CID_3DNS_RATIO, ratio);
#endif
}
#endif /* !PLATFORM_T23 */

int IMP_ISP_Tuning_GetTemperStrength(uint32_t *pratio)
{
    int32_t value = 0;
    int result;

    if (pratio == NULL) {
        return -1;
    }

#if defined(PLATFORM_T20)
    (void)value;
    result = 0;
    *pratio = tseries_t20_temper_ratio;
    return result;
#else
    result = tseries_tuning_get_val(TISP_CID_3DNS_RATIO, &value);
    *pratio = value;
    return result;
#endif
}

#if !defined(PLATFORM_T23) /* T23: isp_t23_tuning.c */
int IMP_ISP_Tuning_SetSinterStrength(uint32_t ratio)
{
#if defined(PLATFORM_T21) && !defined(PLATFORM_T20)
    /* T21: the stock table control 0x8000161 is a no-op in the OEM kernel;
     * use SinterDnsAttr (0x800002c), see isp_t21_sinter.h.  128 = AUTO. */
    uint8_t block[T21_SINTER_BLOCK_SIZE];
    int result;

    t21_sinter_strength_block(ratio, block);
    result = tseries_tuning_set_ptr(TSERIES_T21_CID_SINTER_DNS, block);
    if (result == 0) {
        tseries_t2x_sinter_ratio = ratio;
    }
    return result;
#elif defined(PLATFORM_T21) /* T20 */
    /* T20 3.12.0: isp_table_tuning_ratio(109, min(ratio, 200));
     * the drivers reject 0x8000086 and have no sinter-strength control. */
    int result = tseries_table_tuning_ratio(TSERIES_TABLE_SINTER,
                                            ratio > 200 ? 200 : ratio);

    if (result == 0) {
        tseries_t2x_sinter_ratio = ratio;
    }
    return result;
#else
    return tseries_tuning_set_val(TISP_CID_2DNS_RATIO, ratio);
#endif
}
#endif /* !PLATFORM_T23 */

int IMP_ISP_Tuning_GetSinterStrength(uint32_t *pratio)
{
    int32_t value = 0;
    int result;

    if (pratio == NULL) {
        return -1;
    }

#if defined(PLATFORM_T21) && !defined(PLATFORM_T20)
    {
        uint8_t block[T21_SINTER_BLOCK_SIZE];

        (void)value;
        memset(block, 0, sizeof(block));
        result = tseries_tuning_get_ptr(TSERIES_T21_CID_SINTER_DNS, block);
        /* stock kernel (no 0x800002c support): fall back to the last value */
        *pratio = result == 0 && block[T21_SINTER_B_VALID]
                      ? t21_sinter_block_strength(block)
                      : tseries_t2x_sinter_ratio;
        return result;
    }
#elif defined(PLATFORM_T21) /* T20 */
    (void)value;
    result = 0;
    *pratio = tseries_t2x_sinter_ratio;
    return result;
#else
    result = tseries_tuning_get_val(TISP_CID_2DNS_RATIO, &value);
    *pratio = value;
    return result;
#endif
}

#if !defined(PLATFORM_T23) /* T23: isp_t23_tuning.c */
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

/* Vendor ABI: uint8_t *ratio. The stock and open kernels copy one byte from
 * the pointer. The old uint32_t prototype only worked because callers built
 * against the vendor header passed a pointer anyway. */
int IMP_ISP_Tuning_SetDefog_Strength(uint8_t *ratio)
{
    return tseries_tuning_set_ptr(TISP_CID_DEFOG_STRENGTH, ratio);
}

/* The stock kernel copies one byte to the pointer; the open kernel returns
 * the value inline instead. Handle both, and never write more than the
 * caller's one byte (the old uint32_t store overwrote three bytes past it). */
int IMP_ISP_Tuning_GetDefog_Strength(uint8_t *ratio)
{
    ISPDevice *isp;
    int result;

    if (ratio == NULL) {
        return -1;
    }
    if (tseries_get_isp(&isp) != 0 || isp->tuning == NULL || isp->tuning_state != 2) {
        return -1;
    }
    TSeriesTuningValReq req = {
        .cmd = 1,
        .subcmd = TISP_CID_DEFOG_STRENGTH,
        .value = (int32_t)(intptr_t)ratio,
    };

    result = ioctl(isp->tuning_fd, TISP_VIDIOC_TUNING, &req);
    if (result == 0 && req.value != (int32_t)(intptr_t)ratio) {
        *ratio = (uint8_t)req.value;
    }
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
    return tseries_tuning_get_wb_stats(TISP_CID_WB_STATS, wb);
}

int IMP_ISP_Tuning_GetWB_GOL_Statis(IMPISPWB *wb)
{
    return tseries_tuning_get_wb_stats(TISP_CID_WB_GOL_STATS, wb);
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
#if defined(PLATFORM_T20)
    /* T20: the vendor libimp and the SDK driver pass the 4-byte ROI by
     * value in control->value (AE_ROI_ID), not through a pointer. */
    int32_t packed;

    if (roi == NULL) {
        return -1;
    }
    memcpy(&packed, roi, sizeof(packed));
    return tseries_tuning_set_val(TISP_CID_AE_ROI, packed);
#else
    return tseries_tuning_set_ptr(TISP_CID_AE_ROI, roi);
#endif
}

int IMP_ISP_Tuning_AE_GetROI(void *roi)
{
#if defined(PLATFORM_T20)
    int32_t packed = 0;
    int result;

    if (roi == NULL) {
        return -1;
    }
    result = tseries_tuning_get_val(TISP_CID_AE_ROI, &packed);
    if (result == 0) {
        memcpy(roi, &packed, sizeof(packed));
    }
    return result;
#else
    return tseries_tuning_get_ptr(TISP_CID_AE_ROI, roi);
#endif
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

int IMP_ISP_Tuning_WaitFrame(IMPISPWaitFrameAttr *attr)
{
    if (attr == NULL) {
        return -1;
    }
#if defined(PLATFORM_T31)
    {
        /* vendor 1.1.6: the driver exchanges 0x18 bytes (timeout in word 0,
         * the 64-bit frame-done count in words 2-3), so go through a local
         * buffer and copy only the count back */
        uint32_t buf[6] = { attr->timeout, 0, 0, 0, 0, 0 };
        int result = tseries_tuning_get_ptr(TISP_CID_WAIT_FRAME, buf);

        if (result == 0) {
            attr->cnt = (uint64_t)buf[2] | ((uint64_t)buf[3] << 32);
        }
        return result;
    }
#else
    return 0;
#endif
}

int IMP_ISP_Tuning_GetSensorAttr(IMPISPSENSORAttr *attr)
{
    if (attr == NULL) {
        return -1;
    }

    memset(attr, 0, sizeof(*attr));
#if defined(PLATFORM_T31)
    /* vendor 1.1.6 hands the pointer to the driver, which fills
     * {hts, vts, fps, width, height}; a driver without it leaves zeros */
    if (tseries_tuning_get_ptr(TISP_CID_SENSOR_ATTR, attr) == 0 &&
        attr->width && attr->height) {
        return 0;
    }
    memset(attr, 0, sizeof(*attr));
#endif
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
#if defined(PLATFORM_T31)
    /* vendor 1.1.6: the pointer goes to tuning 0x80000e2 (bypass bits) */
    if (tseries_tuning_set_ptr(TISP_CID_MODULE_CONTROL, ispmodule) != 0) {
        return -1;
    }
#endif
    tseries_module_ctl = *ispmodule;
    return 0;
}

int IMP_ISP_Tuning_GetModuleControl(IMPISPModuleCtl *ispmodule)
{
    if (ispmodule == NULL) {
        return -1;
    }
#if defined(PLATFORM_T31)
    return tseries_tuning_get_ptr(TISP_CID_MODULE_CONTROL, ispmodule);
#else
    *ispmodule = tseries_module_ctl;
    return 0;
#endif
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
#if defined(PLATFORM_T31)
    /* vendor 1.1.6: tuning 0x8000038, ten target words by pointer */
    if (tseries_tuning_set_ptr(TISP_CID_AE_TARGET_LIST, at_list) != 0) {
        return -1;
    }
#endif
    tseries_ae_target_list = *at_list;
    return 0;
}

int IMP_ISP_Tuning_GetAeTargetList(IMPISPAETargetList *at_list)
{
    if (at_list == NULL) {
        return -1;
    }
#if defined(PLATFORM_T31)
    return tseries_tuning_get_ptr(TISP_CID_AE_TARGET_LIST, at_list);
#else
    *at_list = tseries_ae_target_list;
    return 0;
#endif
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
#endif /* !PLATFORM_T23 */

int IMP_ISP_Tuning_GetHVFLIP(IMPISPHVFLIP *hvflip)
{
    return IMP_ISP_Tuning_GetHVFlip(hvflip);
}

#if defined(PLATFORM_T20) || defined(PLATFORM_T21) || defined(PLATFORM_T30)
/* IMPISPITAttr of the T10/T20/T21/T30 SDKs (not in the T31-style headers
 * OpenIMP builds against). */
typedef struct {
    int32_t mode;                   /* 0 auto, 1 manual, 2 range */
    uint16_t integration_time;      /* sensor lines, manual mode */
    uint16_t max_integration_time;  /* sensor lines, range mode */
} TSeriesLegacyITAttr;

/*
 * The stock T20 3.12.0, T21 1.0.33 and T30 1.0.5 libraries send a zeroed
 * 0x70-byte AE block on tuning CID 0x0800002C (the same tuning ioctl as
 * SetAeComp) with only the selected mode's fields and enable bytes set:
 *   auto:   [0x3f] = 1
 *   manual: [0x03] = [0x3f] = [0x51] = 1, u16 [0x18] = integration_time
 *   range:  [0x52] = 1, u16 [0x1a] = max_integration_time
 * T20/T21 reject a manual time of 256 lines or more.
 */
#define TISP_CID_LEGACY_IT_ATTR 0x800002c

int IMP_ISP_Tuning_SetIntegrationTime(void *itattr)
{
    const TSeriesLegacyITAttr *attr = itattr;
    uint8_t ae[0x70];

    if (attr == NULL)
        return -1;
    memset(ae, 0, sizeof(ae));
    switch (attr->mode) {
    case 0:
        ae[0x3f] = 1;
        break;
    case 1:
#if defined(PLATFORM_T20) || defined(PLATFORM_T21)
        if (attr->integration_time >= 256)
            return -1;
#endif
        ae[0x03] = 1;
        ae[0x3f] = 1;
        ae[0x51] = 1;
        memcpy(ae + 0x18, &attr->integration_time, sizeof(uint16_t));
        break;
    case 2:
        ae[0x52] = 1;
        memcpy(ae + 0x1a, &attr->max_integration_time, sizeof(uint16_t));
        break;
    default:
        return -1;
    }
    return tseries_tuning_set_ptr(TISP_CID_LEGACY_IT_ATTR, ae);
}

/* Stock T20 3.12.0 reads the same 0x70-byte block back on CID 0x0800002C:
 * mode = [0x03] != 0 (manual flag), integration_time = u16 [0x18],
 * max_integration_time = u16 [0x1a]. */
int IMP_ISP_Tuning_GetIntegrationTime(void *itattr)
{
    TSeriesLegacyITAttr *attr = itattr;
    uint8_t ae[0x70];
    int result;

    if (attr == NULL)
        return -1;
    memset(ae, 0, sizeof(ae));
    result = tseries_tuning_get_ptr(TISP_CID_LEGACY_IT_ATTR, ae);
    if (result != 0)
        return result;
    attr->mode = ae[0x03] != 0;
    memcpy(&attr->integration_time, ae + 0x18, sizeof(uint16_t));
    memcpy(&attr->max_integration_time, ae + 0x1a, sizeof(uint16_t));
    return 0;
}
#endif

#if !defined(PLATFORM_T23) /* T23: isp_t23_tuning.c */
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

/* Vendor ABI (T23/T31): IMPISPAEMin *, four words (it, again, it_short,
 * again_short); the kernel copies 16 bytes. The old (int, int) form made the
 * kernel read eight bytes past a two-word stack object. */
int IMP_ISP_Tuning_SetAeMin(void *ae_min)
{
    return tseries_tuning_set_ptr(TISP_CID_AE_MIN, ae_min);
}

int IMP_ISP_Tuning_GetAeMin(void *ae_min)
{
    return tseries_tuning_get_ptr(TISP_CID_AE_MIN, ae_min);
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

#if defined(PLATFORM_T23) || defined(PLATFORM_T31)
/* The public IMPISPCCMAttr is {ManualEn, SatEn, float ColorMatrix[9]}; the
 * kernel takes 40 bytes: byte 0 manual, byte 1 saturation, then nine
 * coefficients as 14-bit two's-complement Q10 in 32-bit words. The stock
 * T23/T31 libimp converts between the two exactly like this; passing the
 * public struct through made a manual CCM install float bit patterns. */
typedef struct {
    int32_t manual_en;
    int32_t sat_en;
    float matrix[9];
} TSeriesCCMAttr;

typedef struct {
    int8_t manual_en;
    int8_t sat_en;
    uint8_t reserved[2];
    uint32_t coef[9];
} TSeriesKernelCCM;

static uint32_t tseries_ccm_to_q10(float value)
{
    if (value < -1e-5f)
        return ((uint32_t)-(int32_t)(-value * 1024.0f) & 0x1fffu) | 0x2000u;
    return (uint32_t)(int32_t)(value * 1024.0f) & 0x1fffu;
}

static float tseries_ccm_from_q10(uint32_t word)
{
    if (word & 0x2000u)
        return -(float)(-word & 0x1fffu) / 1024.0f;
    return (float)(int32_t)word / 1024.0f;
}

int IMP_ISP_Tuning_SetCCMAttr(void *attr)
{
    const TSeriesCCMAttr *in = attr;
    TSeriesKernelCCM k;

    if (in == NULL)
        return -1;
    memset(&k, 0, sizeof(k));
    k.manual_en = (int8_t)in->manual_en;
    k.sat_en = (int8_t)in->sat_en;
    for (int i = 0; i < 9; i++)
        k.coef[i] = tseries_ccm_to_q10(in->matrix[i]);
    return tseries_tuning_set_ptr(TISP_CID_CCM_ATTR, &k);
}

int IMP_ISP_Tuning_GetCCMAttr(void *attr)
{
    TSeriesCCMAttr *out = attr;
    TSeriesKernelCCM k;
    int result;

    if (out == NULL)
        return -1;
    memset(&k, 0, sizeof(k));
    result = tseries_tuning_get_ptr(TISP_CID_CCM_ATTR, &k);
    if (result == 0) {
        out->manual_en = k.manual_en;
        out->sat_en = k.sat_en;
        for (int i = 0; i < 9; i++)
            out->matrix[i] = tseries_ccm_from_q10(k.coef[i]);
    }
    return result;
}
#else
int IMP_ISP_Tuning_SetCCMAttr(void *attr)
{
    return tseries_tuning_set_ptr(TISP_CID_CCM_ATTR, attr);
}

int IMP_ISP_Tuning_GetCCMAttr(void *attr)
{
    return tseries_tuning_get_ptr(TISP_CID_CCM_ATTR, attr);
}
#endif

int IMP_ISP_Tuning_SetWB_ALGO(int mode)
{
    return tseries_tuning_set_val(TISP_CID_WB_ALGO, mode);
}

/* Vendor (T21 1.0.33 0x6544): with tuning enabled, store the callback
 * under the tuning mutex (NULL clears it) and return 0. It used to be
 * written over the first word of the tuning block and NULL was refused. */
static pthread_mutex_t tseries_video_drop_lock = PTHREAD_MUTEX_INITIALIZER;
static void *tseries_video_drop_cb;

int IMP_ISP_Tuning_SetVideoDrop(void *attr)
{
    ISPDevice *isp;

    if (tseries_get_isp(&isp) != 0 || isp->tuning_state != 2) {
        return -1;
    }

    pthread_mutex_lock(&tseries_video_drop_lock);
    tseries_video_drop_cb = attr;
    pthread_mutex_unlock(&tseries_video_drop_lock);
    return 0;
}

#if defined(PLATFORM_T21) || defined(PLATFORM_T31) /* T21, T20 and T31 */
/* T21 1.0.33 (0x3150/0x31b8/0x324c/0x32b4): VIDIOC_S/G_CTRL with the V4L2
 * scene-mode and colour-effect controls; Get stores the value only when
 * the ioctl succeeds. The T20 3.12.0 kernel handles both controls
 * (apical scene mode and colorfx); the T21 kernel runs the OEM handler.
 * The stock T31 libimp does not export these calls (undefined symbol) and
 * the stock T31 kernel accepts both controls as no-ops; the open-tx-isp
 * T31 driver serves them (scene stored, colorfx AUTO/BW/VIVID/NEGATIVE),
 * so T31 uses the same V4L2 controls (beyond vendor). */
#define TSERIES_V4L2_CID_SCENE_MODE 0x009a091a
#define TSERIES_V4L2_CID_COLORFX    0x0098091f

int IMP_ISP_Tuning_SetSceneMode(IMPISPSceneMode mode)
{
    return tseries_v4l2_set(TSERIES_V4L2_CID_SCENE_MODE, (int32_t)mode);
}

int IMP_ISP_Tuning_GetSceneMode(IMPISPSceneMode *pmode)
{
    int32_t value;
    int result;

    if (pmode == NULL) {
        return -1;
    }
    result = tseries_v4l2_get(TSERIES_V4L2_CID_SCENE_MODE, &value);
    if (result == 0) {
        *pmode = (IMPISPSceneMode)value;
    }
    return result;
}

int IMP_ISP_Tuning_SetColorfxMode(IMPISPColorfxMode mode)
{
    return tseries_v4l2_set(TSERIES_V4L2_CID_COLORFX, (int32_t)mode);
}

int IMP_ISP_Tuning_GetColorfxMode(IMPISPColorfxMode *pmode)
{
    int32_t value;
    int result;

    if (pmode == NULL) {
        return -1;
    }
    result = tseries_v4l2_get(TSERIES_V4L2_CID_COLORFX, &value);
    if (result == 0) {
        *pmode = (IMPISPColorfxMode)value;
    }
    return result;
}
#else /* T30 (T23 has its own, T31 uses the T21 form): no scene/colour-effect ctl */
/* These are declared in the public header for every SoC but the vendor only
 * wires them on T20/T21. Export failing stubs so a T30 app links
 * (instead of hitting an undefined symbol) and reports the missing control
 * once, mirroring the other unsupported calls in the tree. */
static void tseries_scene_colorfx_unsupported(const char *name, int *reported)
{
    if (*reported) {
        return;
    }
    *reported = 1;
    fprintf(stderr, "[IMP-ISP] %s: not supported on this SoC\n", name);
}

int IMP_ISP_Tuning_SetSceneMode(IMPISPSceneMode mode)
{
    static int reported;

    (void)mode;
    tseries_scene_colorfx_unsupported("IMP_ISP_Tuning_SetSceneMode", &reported);
    return -1;
}

int IMP_ISP_Tuning_GetSceneMode(IMPISPSceneMode *pmode)
{
    static int reported;

    (void)pmode;
    tseries_scene_colorfx_unsupported("IMP_ISP_Tuning_GetSceneMode", &reported);
    return -1;
}

int IMP_ISP_Tuning_SetColorfxMode(IMPISPColorfxMode mode)
{
    static int reported;

    (void)mode;
    tseries_scene_colorfx_unsupported("IMP_ISP_Tuning_SetColorfxMode", &reported);
    return -1;
}

int IMP_ISP_Tuning_GetColorfxMode(IMPISPColorfxMode *pmode)
{
    static int reported;

    (void)pmode;
    tseries_scene_colorfx_unsupported("IMP_ISP_Tuning_GetColorfxMode", &reported);
    return -1;
}
#endif /* PLATFORM_T21 || PLATFORM_T31 */

int IMP_ISP_Tuning_SetShading(void *attr)
{
#if defined(PLATFORM_T21) /* T21 and T20 */
    /* T21 1.0.33 / T20 3.12.0 pass the argument as the S_CTRL value. */
    return tseries_v4l2_set(TISP_CID_SHADING, (int32_t)(intptr_t)attr);
#else
    return tseries_tuning_set_ptr(TISP_CID_SHADING, attr);
#endif
}

/* Vendor ABI (T23/T31): IMPISPScalerLv * (channel, method, level); the
 * kernel copies 12 bytes. */
int IMP_ISP_Tuning_SetScalerLv(void *scaler_level)
{
    return tseries_tuning_set_ptr(TISP_CID_SCALER_LV, scaler_level);
}

int IMP_ISP_Tuning_SetMask(void *attr)
{
#if defined(PLATFORM_T31)
    /* Stock T31: RGB masks (mask_type 0) are converted to YUV in the
     * caller's struct once the ISP is up, then one ioctl; a failing ioctl
     * is logged and its result returned. */
    ISPDevice *isp;
    int ret;

    if (attr == NULL) {
        return -1;
    }
    if (tseries_get_isp(&isp) != 0 || isp->tuning == NULL || isp->tuning_state != 2) {
        return -1;
    }
    isp_mask_attr_rgb_to_yuv(attr);
    ret = tseries_tuning_set_ptr(TISP_CID_MASK, attr);
    if (ret != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x10a9,
            "IMP_ISP_Tuning_SetMask", "%s(%d),ioctl  IMP_ISP_Tuning_SetMask!\n",
            "IMP_ISP_Tuning_SetMask", 0x10a9);
    }
    return ret;
#else
    return tseries_tuning_set_ptr(TISP_CID_MASK, attr);
#endif
}

int IMP_ISP_Tuning_GetMask(void *attr)
{
#if defined(PLATFORM_T31)
    /* Stock T31: -1 for NULL or an ISP that is not up, else one ioctl
     * (the kernel returns the attribute as set, i.e. YUV after an RGB
     * SetMask); a failing ioctl is logged and its result returned. */
    ISPDevice *isp;
    int ret;

    if (attr == NULL) {
        return -1;
    }
    if (tseries_get_isp(&isp) != 0 || isp->tuning == NULL || isp->tuning_state != 2) {
        return -1;
    }
    ret = tseries_tuning_get_ptr(TISP_CID_MASK, attr);
    if (ret != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x10c4,
            "IMP_ISP_Tuning_GetMask", "%s(%d),ioctl  IMP_ISP_Tuning_GetMask!\n",
            "IMP_ISP_Tuning_GetMask", 0x10c4);
    }
    return ret;
#else
    return tseries_tuning_get_ptr(TISP_CID_MASK, attr);
#endif
}

int IMP_ISP_Tuning_SetISPProcess(void *attr)
{
#if defined(PLATFORM_T21) /* T21 and T20 */
    /* T21 1.0.33 / T20 3.12.0 pass the argument as the S_CTRL value. */
    return tseries_v4l2_set(TISP_CID_ISP_PROCESS, (int32_t)(intptr_t)attr);
#else
    return tseries_tuning_set_ptr(TISP_CID_ISP_PROCESS, attr);
#endif
}

int IMP_ISP_Tuning_SetFWFreeze(int enable)
{
#if defined(PLATFORM_T21) /* T21 and T20 */
    return tseries_v4l2_set(TISP_CID_FW_FREEZE, enable);
#else
    return tseries_tuning_set_val(TISP_CID_FW_FREEZE, enable);
#endif
}

int IMP_ISP_Tuning_SetCsc_Attr(void *attr)
{
    return tseries_tuning_set_ptr(TISP_CID_CSC_ATTR, attr);
}

int IMP_ISP_Tuning_GetCsc_Attr(void *attr)
{
    return tseries_tuning_get_ptr(TISP_CID_CSC_ATTR, attr);
}
#endif /* !PLATFORM_T23 */

/* T23: IMP_ISP_Tuning_Set/GetSceneMode and Set/GetColorfxMode are in
 * isp_t23_tuning.c (V4L2 controls, like the BCS controls). */

/* Vendor ABI (T31): IMPISPWdrOutputMode *; the kernel copies 4 bytes. */
int IMP_ISP_Tuning_SetWdr_OutputMode(void *mode)
{
    return tseries_tuning_set_ptr(TISP_CID_WDR_OUTPUT_MODE, mode);
}

int IMP_ISP_Tuning_GetWdr_OutputMode(void *mode)
{
    return tseries_tuning_get_ptr(TISP_CID_WDR_OUTPUT_MODE, mode);
}

#if !defined(PLATFORM_T23) /* T23: isp_t23_tuning.c */
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
    return tseries_tuning_get_ptr(TISP_CID_AF_ZONE, zone);
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
#endif /* !PLATFORM_T23 */

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

#if !defined(PLATFORM_T23) /* T23: isp_t23_tuning.c */
int IMP_ISP_SetFixedContraster(int mode)
{
    (void)mode;
    return 0;
}
#endif /* !PLATFORM_T23 */

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

    kmsg_trace("libimp/ISP: EnableSensor entry gISP=%p\n", (void *)gISP);
    if (tseries_get_isp(&isp) != 0) {
        kmsg_trace("libimp/ISP: EnableSensor tseries_get_isp failed\n");
        return -1;
    }

    if (tseries_ae_algo_en != 0 && tseries_ae_func_tmp != NULL) {
        IMP_ISP_SetAeAlgoFunc_internal(tseries_ae_func_tmp);
    }

    if (tseries_awb_algo_en != 0 && tseries_awb_func_tmp != NULL) {
        IMP_ISP_SetAwbAlgoFunc_internal(tseries_awb_func_tmp);
    }

#if defined(PLATFORM_T20)
    if (ioctl(isp->fd, 0x40045626U, &sensor_index) != 0 || sensor_index < 0) {
        kmsg_trace("libimp/ISP: EnableSensor T20 G_INPUT failed idx=%d errno=%d\n",
                   sensor_index, errno);
        return -1;
    }
    if (ioctl(isp->fd, 0x80045612U, &sensor_index) != 0) {
        kmsg_trace("libimp/ISP: EnableSensor T20 STREAMON failed errno=%d\n", errno);
        return -1;
    }
    isp->opened += 2;
    tseries_isp_stream_started = 1;
    tseries_bypass_link_setup_done = 1;
    kmsg_trace("libimp/ISP: EnableSensor T20 STREAMON ok idx=%d\n", sensor_index);
    return 0;
#endif

    if (ioctl(isp->fd, TISP_VIDIOC_GET_SENSOR_INDEX, &sensor_index) != 0) {
        kmsg_trace("libimp/ISP: EnableSensor GET_SENSOR_INDEX failed errno=%d\n", errno);
        return -1;
    }
    kmsg_trace("libimp/ISP: EnableSensor GET_SENSOR_INDEX=%d\n", sensor_index);

    if (sensor_index == -1) {
        kmsg_trace("libimp/ISP: EnableSensor sensor not selected yet\n");
        return -1;
    }

    if (ioctl(isp->fd, TISP_VIDIOC_ENABLE_SENSOR, 0) != 0) {
        kmsg_trace("libimp/ISP: EnableSensor ENABLE_SENSOR failed errno=%d\n", errno);
        return -1;
    }
    kmsg_trace("libimp/ISP: EnableSensor ENABLE_SENSOR ok\n");

    sensor_index = 0;
    if (ioctl(isp->fd, TISP_VIDIOC_CREATE_LINKS, &sensor_index) != 0) {
        kmsg_trace("libimp/ISP: EnableSensor CREATE_LINKS failed arg=%d errno=%d\n",
                   sensor_index, errno);
        return -1;
    }
    kmsg_trace("libimp/ISP: EnableSensor CREATE_LINKS ok arg=%d\n", sensor_index);

    if (ioctl(isp->fd, TISP_VIDIOC_ENABLE_LINKS, 0) != 0) {
        kmsg_trace("libimp/ISP: EnableSensor ENABLE_LINKS failed errno=%d\n", errno);
        return -1;
    }
    kmsg_trace("libimp/ISP: EnableSensor ENABLE_LINKS ok\n");

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

#if defined(PLATFORM_T20)
    if (ioctl(isp->fd, 0x40045626U, &sensor_index) != 0 || sensor_index < 0) {
        kmsg_trace("libimp/ISP: DisableSensor T20 G_INPUT failed idx=%d errno=%d\n",
                   sensor_index, errno);
        return -1;
    }
    if (ioctl(isp->fd, 0x80045613U, &sensor_index) != 0) {
        kmsg_trace("libimp/ISP: DisableSensor T20 STREAMOFF failed errno=%d\n", errno);
        return -1;
    }
    tseries_isp_stream_started = 0;
    tseries_bypass_link_setup_done = 0;
    isp->opened -= 2;
    kmsg_trace("libimp/ISP: DisableSensor T20 STREAMOFF ok idx=%d\n", sensor_index);
    return 0;
#endif

    if (ioctl(isp->fd, TISP_VIDIOC_GET_SENSOR_INDEX, &sensor_index) != 0) {
        kmsg_trace("libimp/ISP: DisableSensor GET_SENSOR_INDEX failed errno=%d\n", errno);
        return -1;
    }
    kmsg_trace("libimp/ISP: DisableSensor GET_SENSOR_INDEX=%d\n", sensor_index);

    if (sensor_index == -1) {
        kmsg_trace("libimp/ISP: DisableSensor sensor not selected yet\n");
        return -1;
    }

    if (tseries_ae_algo_en != 0) {
        IMP_ISP_SetAeAlgoFunc_close();
    }

    if (tseries_awb_algo_en != 0) {
        IMP_ISP_SetAwbAlgoFunc_close();
    }

    if (ioctl(isp->fd, TISP_VIDIOC_DISABLE_LINKS, 0) != 0) {
        kmsg_trace("libimp/ISP: DisableSensor DISABLE_LINKS failed errno=%d\n", errno);
        return -1;
    }
    kmsg_trace("libimp/ISP: DisableSensor DISABLE_LINKS ok\n");

    if (ioctl(isp->fd, TISP_VIDIOC_DESTROY_LINKS, &sensor_index) != 0) {
        kmsg_trace("libimp/ISP: DisableSensor DESTROY_LINKS failed arg=%d errno=%d\n",
                   sensor_index, errno);
        return -1;
    }
    kmsg_trace("libimp/ISP: DisableSensor DESTROY_LINKS ok arg=%d\n", sensor_index);

    if (ioctl(isp->fd, TISP_VIDIOC_DISABLE_SENSOR, 0) != 0) {
        kmsg_trace("libimp/ISP: DisableSensor DISABLE_SENSOR failed errno=%d\n", errno);
        return -1;
    }
    kmsg_trace("libimp/ISP: DisableSensor DISABLE_SENSOR ok\n");

    tseries_isp_stream_started = 0;
    tseries_bypass_link_setup_done = 0;
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

/* ----- Missing symbols required by rvd hal_init() ----- */

/* T68 forward decls */
int32_t IMP_Alloc(void *info, int32_t size, const char *name);
/* The OEM IMP_Free takes (descriptor, address); OpenIMP's takes one address
 * and did not find the descriptor, so these buffers were never freed. */
static inline void isp_free_dma(void *info)
{
    DMA_FreePhys(((const IMPDMABufferInfo *)info)->phys_addr);
}

int IMP_ISP_AddSensor(IMPSensorInfo *pinfo)
{
    ISPDevice *isp = (ISPDevice *)gISP;
    uint8_t *isp_b = (uint8_t *)isp;
    kmsg_trace("libimp/ISP: AddSensor entry gISP=%p\n", (void *)isp);
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
    if (ioctl(isp->fd, 0x805056c1U, pinfo) != 0) {
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

    /* T23's OEM ISPDevice retains the complete 0x54-byte sensor record. */
#if defined(PLATFORM_T23)
    memcpy(isp_b + 0x28, pinfo, 0x54);
#else
    memcpy(isp_b + 0x28, pinfo, 0x50);
#endif

#if defined(PLATFORM_T20)
    /* The T20 video-input node owns all ISP memory.  Unlike T23/T31 there
     * is no GET_BUF/SET_BUF userspace allocation handshake after S_INPUT. */
    return 0;
#endif

#if defined(PLATFORM_T30)
    /*
     * T30 libimp 1.0.5 stops here.  Unlike T23/T31, AddSensor does not
     * allocate or install the ISP MDNS buffer.  The stock T30 ioctl accepts
     * the nominally 8-byte 0x800856d5 request while writing a 12-byte result,
     * so trying the newer allocation path also corrupts our stack.
     */
    return 0;
#endif

    /* Read required ISP buffer size using the platform-specific ABI. */
    tx_isp_buf_t buf_info;
    TXISP_BUF_INIT(buf_info);
    TXISP_BUF_SET_INDEX(buf_info, sensor_idx);
    if (ioctl(isp->fd, TX_ISP_GET_BUF, &buf_info) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x1dc,
            "IMP_ISP_AddSensor", "VIDIOC_GET_BUF_INFO() error!\n");
        return -1;
    }
    kmsg_trace("libimp/ISP: AddSensor GET_BUF idx=%d size=%u struct_sz=%u\n",
        sensor_idx, (unsigned)TXISP_BUF_GET_SIZE(buf_info), (unsigned)sizeof(buf_info));
    imp_log_fun(4, IMP_Log_Get_Option(), 2, "IMP-ISP",
        "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x1e0,
        "IMP_ISP_AddSensor", "%s,%d: size = 0x%x\n",
        "IMP_ISP_AddSensor", 0x1e0, TXISP_BUF_GET_SIZE(buf_info));

    void *ncu_alloc = malloc(0x94);
    if (ncu_alloc == NULL) {
        printf("error(%s,%d): maloc err\n", "IMP_ISP_AddSensor", 0x1e3);
        return -1;
    }
    if (IMP_Alloc(ncu_alloc, TXISP_BUF_GET_SIZE(buf_info), "ncubuf") != 0) {
        printf("error(%s,%d): IMP_Alloc\n", "IMP_ISP_AddSensor", 0x1e8);
        return -1;
    }
#if defined(PLATFORM_T23)
    if ((unsigned int)sensor_idx >= 2u) {
        isp_free_dma(ncu_alloc);
        free(ncu_alloc);
        return -1;
    }
    isp->sensor_alloc[sensor_idx] = ncu_alloc;
#else
    *(void **)(isp_b + 0xac) = ncu_alloc;
#endif
    int32_t ncu_phys = *(int32_t *)((char *)ncu_alloc + 0x84);
    tx_isp_buf_t set_buf;
    TXISP_BUF_INIT(set_buf);
    TXISP_BUF_SET_INDEX(set_buf, sensor_idx);
    TXISP_BUF_SET_PHYS_SIZE(set_buf, (uint32_t)ncu_phys, TXISP_BUF_GET_SIZE(buf_info));
    kmsg_trace("libimp/ISP: AddSensor SET_BUF phys=0x%x size=%u struct_sz=%u\n",
        (unsigned)ncu_phys, (unsigned)TXISP_BUF_GET_SIZE(buf_info), (unsigned)sizeof(set_buf));
    if (ioctl(isp->fd, TX_ISP_SET_BUF, &set_buf) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x1ee,
            "IMP_ISP_AddSensor", "VIDIOC_SET_BUF_INFO() error!\n");
        return -1;
    }

    if (isp->wdr_mode != 1) return 0;

    struct { uint32_t addr; uint32_t size; } wdr_info = {0, 0};
    if (ioctl(isp->fd, 0x800856d7, &wdr_info) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x1f6,
            "IMP_ISP_AddSensor", "VIDIOC_GET_WDR_BUF_INFO() error!\n");
        return -1;
    }
    imp_log_fun(4, IMP_Log_Get_Option(), 2, "IMP-ISP",
        "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x1fa,
        "IMP_ISP_AddSensor", "%s,%d: paddr = 0x%x, size = 0x%x\n",
        "IMP_ISP_AddSensor", 0x1fa, wdr_info.addr, wdr_info.size);
    void *wdr_alloc = malloc(0x94);
    if (wdr_alloc == NULL) {
        printf("error(%s,%d): maloc err\n", "IMP_ISP_AddSensor", 0x1fd);
        return -1;
    }
    if (IMP_Alloc(wdr_alloc, wdr_info.size, "wdrbuf") != 0) {
        printf("error(%s,%d): IMP_Alloc\n", "IMP_ISP_AddSensor", 0x202);
        return -1;
    }
#if defined(PLATFORM_T23)
    isp->wdr_alloc = wdr_alloc;
#else
    *(void **)(isp_b + 0xb4) = wdr_alloc;
#endif
    int32_t wdr_phys = *(int32_t *)((char *)wdr_alloc + 0x84);
    struct { uint32_t addr; uint32_t size; } set_wdr;
    set_wdr.addr = (uint32_t)wdr_phys;
    set_wdr.size = wdr_info.size;
    kmsg_trace("libimp/ISP: AddSensor SET_WDR_BUF phys=0x%x size=%u\n",
        (unsigned)wdr_phys, (unsigned)wdr_info.size);
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
    int32_t r = ioctl(isp->fd, 0x805056c2U, pinfo);
    if (r != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x2bc,
            "IMP_ISP_DelSensor", "VIDIOC_REGISTER_SENSOR(%s) error!\n", pinfo);
        return r;
    }

    memset(isp_b + 0x28, 0,
#if defined(PLATFORM_T23)
           0x54
#else
           0x50
#endif
    );
#if defined(PLATFORM_T20)
    return 0;
#else
#if defined(PLATFORM_T23)
    void *ncu = isp->sensor_alloc[0];
#else
    void *ncu = *(void **)(isp_b + 0xac);
#endif
    if (ncu != NULL) {
        isp_free_dma(ncu);
        free(ncu);
#if defined(PLATFORM_T23)
        isp->sensor_alloc[0] = NULL;
#else
        *(void **)(isp_b + 0xac) = NULL;
#endif
    }
#if defined(PLATFORM_T23)
    void *wdr = isp->wdr_alloc;
#else
    void *wdr = *(void **)(isp_b + 0xb4);
#endif
    if (wdr != NULL) {
        isp_free_dma(wdr);
        free(wdr);
#if defined(PLATFORM_T23)
        isp->wdr_alloc = NULL;
#else
        *(void **)(isp_b + 0xb4) = NULL;
#endif
    }
    return 0;
#endif
}

/* Diagnostic trace — writes to /dev/kmsg which ALWAYS appears in `dmesg`.
 * Stock libsysutils' imp_log_fun routes to a sink we can't see, and rvd
 * doesn't capture stderr, so /dev/kmsg is the only reliable debug path. */
static void kmsg_trace(const char *fmt, ...)
{
    static int kfd = -2;
    char buf[256];
    va_list ap;

    if (kfd == -2) kfd = open("/dev/kmsg", O_WRONLY);
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0)
        return;
    if (kfd >= 0)
        write(kfd, buf, (size_t)n);
#if defined(PLATFORM_T23)
    openimp_t23_persist_write(buf, (size_t)n);
#endif
    if (getenv("OPENIMP_STARTUP_TRACE")) {
        write(STDERR_FILENO, buf, (size_t)n);
        fsync(STDERR_FILENO);
    }
}

int IMP_ISP_EnableTuning(void)
{
    ISPDevice *isp = (ISPDevice *)gISP;

    kmsg_trace("libimp/ISP: EnableTuning entry gISP=%p\n", (void *)isp);
    if (isp != NULL)
        kmsg_trace("libimp/ISP: opened=%u tuning=%p\n",
                   isp->opened, isp->tuning);

    if (isp == NULL) {
        kmsg_trace("libimp/ISP: EnableTuning FAILED — gISP NULL (Open not called)\n");
        return -1;
    }
    if (isp->tuning != NULL) return 0;  /* already enabled */

    /* Build "/dev/isp-m0" path at +0x78.
     * Stock binary uses open flags 0x80002 (O_RDWR | O_CLOEXEC), but the
     * Thingino kernel's /dev/isp-m0 driver appears to reject O_CLOEXEC on
     * this particular module build — the legacy openimp impl used plain
     * O_RDWR and got the stream on. Prefer the flag value that works on
     * real hardware over binary-exact. */
    strcpy(isp->tuning_path,
#if defined(PLATFORM_T20)
           "/dev/video0"
#else
           "/dev/isp-m0"
#endif
    );
    int32_t tfd = open(isp->tuning_path, O_RDWR);
    int32_t err1 = (tfd < 0) ? errno : 0;
    kmsg_trace("libimp/ISP: open(%s, O_RDWR) = %d (errno=%d %s)\n",
               isp->tuning_path, tfd, err1, err1 ? strerror(err1) : "ok");
#if !defined(PLATFORM_T20)
    if (tfd < 0) {
        strcpy(isp->tuning_path, "/dev/isp-w02");
        tfd = open(isp->tuning_path, O_RDWR);
        int32_t err2 = (tfd < 0) ? errno : 0;
        kmsg_trace("libimp/ISP: open(/dev/isp-w02, O_RDWR) = %d (errno=%d %s)\n",
                   tfd, err2, err2 ? strerror(err2) : "ok");
    }
#endif
    isp->tuning_fd = tfd;
    if (tfd < 0) {
        kmsg_trace("libimp/ISP: EnableTuning FAILED — could not open any tuning node\n");
        return -1;
    }
    kmsg_trace("libimp/ISP: EnableTuning opened %s as fd=%d\n",
               isp->tuning_path, tfd);
    void *tune = calloc(0x1c, 1);
    if (tune == NULL) { close(tfd); return -1; }
    isp->tuning = tune;
    isp->tuning_state = 2;

#if defined(PLATFORM_T21) && !defined(PLATFORM_T20)
    /* The vendor zeroes its T21 denoise cache when tuning is enabled, so an
     * unchanged RawDRC/Temper value is re-sent after DisableTuning followed
     * by EnableTuning instead of being suppressed as "already applied". */
    memset(&tseries_t21_dns_cache, 0, sizeof(tseries_t21_dns_cache));
#endif

    int32_t mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    isp->mem_fd = mem_fd;
    if (mem_fd <= 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x485,
            "IMP_ISP_EnableTuning", "Failed to open %s\n", "/dev/mem");
    }
#if defined(PLATFORM_T20) || defined(PLATFORM_T23)
    const size_t isp_map_size = 0x1b000u;
#else
    const size_t isp_map_size = 0x10000u;
#endif
    void *base = mmap(0, isp_map_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                      mem_fd, 0x13380000);
    isp->isp_base = base;
    if (base == NULL || base == MAP_FAILED) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP-ISP",
            "/home/user/git/proj/sdk-lv3/src/imp/isp/isp_tseries.c", 0x489,
            "IMP_ISP_EnableTuning", "Failed to mmap isp base addr\n");
    }

#if defined(PLATFORM_T20)
    {
        /* T20 copies the FPS through control->value (see GetSensorFPS). */
        uint32_t fps = 0;
        TSeriesTuningValReq fps_req = { 1, TISP_CID_SENSOR_FPS,
                                        (int32_t)(intptr_t)&fps };

        if (ioctl(isp->tuning_fd, TISP_VIDIOC_TUNING, &fps_req) == 0) {
            *(int32_t *)((char *)tune + 0xc) = fps >> 16;
            *(int32_t *)((char *)tune + 0x10) = fps & 0xffff;
        }
    }
#else
    TSeriesTuningValReq fps_req = { 1, TISP_CID_SENSOR_FPS, 0 };
    if (ioctl(isp->tuning_fd, TISP_VIDIOC_TUNING,
              &fps_req) == 0) {
        *(int32_t *)((char *)tune + 0xc) = (uint32_t)fps_req.value >> 16;
        *(int32_t *)((char *)tune + 0x10) = fps_req.value & 0xffff;
    }
#endif
    *(uint8_t *)((char *)tune + 9) = TSERIES_CUSTOM_CONTRAST;
#if defined(PLATFORM_T21) /* T21 and T20 */
    {
        /* SetISPRunningMode re-sends the cached brightness, saturation and
         * sharpness bytes.  The vendor leaves them 0 until the app sets or
         * gets them, which would push 0 on a day/night switch; seed them
         * from the driver, or 128 if it has no getter or reports 0. */
        static const struct { int32_t id; uint8_t offset; } seed[] = {
            { 0x980900, 8 }, { 0x980902, 10 }, { 0x98091b, 11 },
        };
        size_t i;

        for (i = 0; i < sizeof(seed) / sizeof(seed[0]); i++) {
            int32_t v = 0;

            if (tseries_v4l2_get(seed[i].id, &v) != 0 || (uint8_t)v == 0) {
                v = 0x80;
            }
            *(uint8_t *)((char *)tune + seed[i].offset) = (uint8_t)v;
        }
    }
#endif
#if defined(PLATFORM_T23)
    openimp_t23_isp_tuning_enabled();
#endif
    if (tseries_start_tuning_worker() != 0)
        kmsg_trace("libimp/ISP: failed to start gain/contrast tuning worker\n");
    return 0;
}

int IMP_ISP_DisableTuning(void)
{
    ISPDevice *isp = (ISPDevice *)gISP;
    if (isp == NULL) return 0;

    tseries_stop_tuning_worker();

    void *tune = isp->tuning;
    if (tune != NULL) free(tune);
    int32_t tfd = isp->tuning_fd;
    isp->tuning = NULL;
    if (tfd > 0) close(tfd);

    void *base = isp->isp_base;
#if defined(PLATFORM_T20) || defined(PLATFORM_T23)
    const size_t isp_map_size = 0x1b000u;
#else
    const size_t isp_map_size = 0x10000u;
#endif
    if (base != NULL && base != MAP_FAILED) munmap(base, isp_map_size);
    int32_t mem_fd = isp->mem_fd;
    if (mem_fd > 0) close(mem_fd);
    isp->isp_base = NULL;
    isp->mem_fd = 0;
    isp->tuning_state = 0;
    return 0;
}
