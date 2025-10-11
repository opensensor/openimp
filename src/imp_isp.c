/**
 * IMP ISP Module Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <imp/imp_isp.h>

#define LOG_ISP(fmt, ...) fprintf(stderr, "[IMP_ISP] " fmt "\n", ##__VA_ARGS__)

/* ISP Device structure (userspace) */
typedef struct {
    char dev_name[32];      /* Device name, e.g. "/dev/tx-isp" */
    int fd;                 /* Main device fd */
    int tisp_fd;            /* Optional tuning device fd ("/dev/tisp"), -1 if not open */
    int opened;             /* Opened flag; +2 indicates sensor enabled */
    uint8_t data[0xb8];     /* Scratch/device data area for mirroring sensor info, etc. */
} ISPDevice;

static ISPDevice *gISPdev = NULL;
static int sensor_enabled = 0;
static int tuning_enabled = 0;

/* Core ISP Functions */

/* IMP_ISP_Open - based on decompilation at 0x8b6ec */
int IMP_ISP_Open(void) {
    if (gISPdev != NULL) {
        LOG_ISP("Open: already opened");
        return 0;
    }

    /* Allocate ISP device structure */
    gISPdev = (ISPDevice*)calloc(1, sizeof(ISPDevice));
    if (gISPdev == NULL) {
        LOG_ISP("Open: failed to allocate ISP device");
        return -1;
    }

    /* Set device name */
    strcpy(gISPdev->dev_name, "/dev/tx-isp");

    /* Open ISP device */
    gISPdev->fd = open(gISPdev->dev_name, O_RDWR | O_NONBLOCK);
    if (gISPdev->fd < 0) {
        LOG_ISP("Open: failed to open %s: %s", gISPdev->dev_name, strerror(errno));
        free(gISPdev);
        gISPdev = NULL;
        return -1;
    }

    /* Open tuning device /dev/tisp (optional) */
    gISPdev->tisp_fd = open("/dev/tisp", O_RDWR);
    if (gISPdev->tisp_fd < 0) {
        LOG_ISP("Open: failed to open /dev/tisp: %s", strerror(errno));
        /* Don't fail - tuning is optional */
        gISPdev->tisp_fd = -1;
    } else {
        LOG_ISP("Open: opened /dev/tisp (fd=%d)", gISPdev->tisp_fd);

        /* Disable front crop immediately to prevent validation errors
         * Based on IMP_ISP_Tuning_SetFrontCrop at 0x955f4 */
        struct {
            int32_t op;      /* 0 = set, 1 = get */
            int32_t ctrl_id; /* 0x80000e3 = front crop control */
            void* data;      /* pointer to fcrop data */
        } ctrl_param;

        struct {
            int32_t enable;  /* 0 = disable fcrop */
            int32_t x;
            int32_t y;
            int32_t width;
            int32_t height;
        } fcrop_data = {0, 0, 0, 0, 0};

        ctrl_param.op = 0;           /* Set operation */
        ctrl_param.ctrl_id = 0x80000e3; /* Front crop control ID */
        ctrl_param.data = &fcrop_data;

        int ret = ioctl(gISPdev->tisp_fd, 0xc00c56c6, &ctrl_param);
        if (ret < 0) {
            LOG_ISP("Open: Warning: failed to disable fcrop: %s", strerror(errno));
        } else {
            LOG_ISP("Open: Front crop disabled");
        }
    }

    /* Mark as opened */
    gISPdev->opened = 1;

    LOG_ISP("Open: opened %s (fd=%d)", gISPdev->dev_name, gISPdev->fd);
    return 0;
}

/* IMP_ISP_Close - based on decompilation at 0x8b8d8 */
int IMP_ISP_Close(void) {
    if (gISPdev == NULL) {
        LOG_ISP("Close: not opened");
        return 0;
    }

    /* Check if opened flag is >= 2 (sensor enabled) */
    if (gISPdev->opened >= 2) {
        LOG_ISP("Close: sensor still enabled");
        return -1;
    }

    /* Close devices */
    if (gISPdev->fd >= 0) {
        close(gISPdev->fd);
    }
    if (gISPdev->tisp_fd >= 0) {
        close(gISPdev->tisp_fd);
    }

    /* Free device structure */
    free(gISPdev);
    gISPdev = NULL;

    LOG_ISP("Close: closed ISP device");
    return 0;
}

/* IMP_ISP_AddSensor - based on decompilation at 0x8bd6c */
int IMP_ISP_AddSensor(IMPSensorInfo *pinfo) {
    if (pinfo == NULL) {
        LOG_ISP("AddSensor: NULL sensor info");
        return -1;
    }

    if (gISPdev == NULL) {
        LOG_ISP("AddSensor: ISP not opened");
        return -1;
    }

    if (gISPdev->opened >= 2) {
        LOG_ISP("AddSensor: sensor already enabled");
        return -1;
    }

    /* Add sensor via ioctl 0x805056c1 */
    if (ioctl(gISPdev->fd, 0x805056c1, pinfo) != 0) {
        LOG_ISP("AddSensor: ioctl failed for %s: %s", pinfo->name, strerror(errno));
        return -1;
    }

    /* Copy sensor info into our scratch area */
    memcpy(&gISPdev->data[0], pinfo, sizeof(IMPSensorInfo));

    LOG_ISP("AddSensor: %s", pinfo->name);
    return 0;
}

int IMP_ISP_AddSensor_VI(IMPVI vi, IMPSensorInfo *pinfo) {
    if (pinfo == NULL) return -1;
    LOG_ISP("AddSensor_VI: vi=%d, sensor=%s", vi, pinfo->name);
    return 0;
}

int IMP_ISP_DelSensor(IMPSensorInfo *pinfo) {
    if (pinfo == NULL) return -1;
    LOG_ISP("DelSensor: %s", pinfo->name);
    return 0;
}

int IMP_ISP_DelSensor_VI(IMPVI vi, IMPSensorInfo *pinfo) {
    if (pinfo == NULL) return -1;
    LOG_ISP("DelSensor_VI: vi=%d, sensor=%s", vi, pinfo->name);
    return 0;
}

/* IMP_ISP_EnableSensor - based on decompilation at 0x98450 */
int IMP_ISP_EnableSensor(void) {
    if (gISPdev == NULL) {
        LOG_ISP("EnableSensor: ISP not opened");
        return -1;
    }

    /* Get sensor index via ioctl 0x40045626 */
    int sensor_idx = -1;
    int ret = ioctl(gISPdev->fd, 0x40045626, &sensor_idx);

    /* The ioctl may fail if sensor is already configured, which is OK */
    if (ret != 0 && errno != EINVAL) {
        LOG_ISP("EnableSensor: failed to get sensor index: %s", strerror(errno));
        /* Don't fail - continue with sensor_idx = -1 */
    }

    /* If sensor_idx is still -1, assume sensor 0 */
    if (sensor_idx == -1) {
        sensor_idx = 0;
        LOG_ISP("EnableSensor: using default sensor index 0");
    }

    /* Defer stream-on and sensor enable to FrameSource path. The OEM starts streaming
     * only after format and buffers are configured on the channel. */
    (void)gISPdev; /* suppress unused warnings if builds differ */

    /* Increment opened flag by 2 to mark sensor as enabled */
    gISPdev->opened += 2;
    sensor_enabled = 1;

    LOG_ISP("EnableSensor: sensor enabled (idx=%d)", sensor_idx);
    return 0;
}

int IMP_ISP_EnableSensor_VI(IMPVI vi, IMPSensorInfo *pinfo) {
    if (pinfo == NULL) return -1;
    LOG_ISP("EnableSensor_VI: vi=%d, sensor=%s", vi, pinfo->name);
    sensor_enabled = 1;
    return 0;
}

int IMP_ISP_DisableSensor(void) {
    LOG_ISP("DisableSensor");
    sensor_enabled = 0;
    return 0;
}

int IMP_ISP_DisableSensor_VI(IMPVI vi) {
    LOG_ISP("DisableSensor_VI: vi=%d", vi);
    sensor_enabled = 0;
    return 0;
}

int IMP_ISP_EnableTuning(void) {
    LOG_ISP("EnableTuning");
    tuning_enabled = 1;
    return 0;
}

int IMP_ISP_DisableTuning(void) {
    LOG_ISP("DisableTuning");
    tuning_enabled = 0;
    return 0;
}

/* ISP Tuning Functions */

int IMP_ISP_Tuning_SetSensorFPS(uint32_t fps_num, uint32_t fps_den) {
    LOG_ISP("SetSensorFPS: %u/%u", fps_num, fps_den);
    return 0;
}

int IMP_ISP_Tuning_GetSensorFPS(uint32_t *fps_num, uint32_t *fps_den) {
    if (fps_num == NULL || fps_den == NULL) return -1;
    *fps_num = 25;
    *fps_den = 1;
    return 0;
}

int IMP_ISP_Tuning_SetAntiFlickerAttr(IMPISPAntiflickerAttr attr) {
    LOG_ISP("SetAntiFlickerAttr: %d", attr);
    return 0;
}

int IMP_ISP_Tuning_SetISPRunningMode(IMPISPRunningMode mode) {
    LOG_ISP("SetISPRunningMode: %d", mode);
    return 0;
}

int IMP_ISP_Tuning_GetISPRunningMode(IMPISPRunningMode *pmode) {
    if (pmode == NULL) return -1;
    *pmode = IMPISP_RUNNING_MODE_DAY;
    return 0;
}

int IMP_ISP_Tuning_SetISPBypass(IMPISPTuningOpsMode enable) {
    LOG_ISP("SetISPBypass: %d", enable);
    return 0;
}

int IMP_ISP_Tuning_SetISPHflip(IMPISPTuningOpsMode mode) {
    LOG_ISP("SetISPHflip: %d", mode);
    return 0;
}

int IMP_ISP_Tuning_SetISPVflip(IMPISPTuningOpsMode mode) {
    LOG_ISP("SetISPVflip: %d", mode);
    return 0;
}

int IMP_ISP_Tuning_SetBrightness(unsigned char bright) {
    LOG_ISP("SetBrightness: %u", bright);
    return 0;
}

int IMP_ISP_Tuning_SetContrast(unsigned char contrast) {
    LOG_ISP("SetContrast: %u", contrast);
    return 0;
}

int IMP_ISP_Tuning_SetSharpness(unsigned char sharpness) {
    LOG_ISP("SetSharpness: %u", sharpness);
    return 0;
}

int IMP_ISP_Tuning_SetSaturation(unsigned char sat) {
    LOG_ISP("SetSaturation: %u", sat);
    return 0;
}

int IMP_ISP_Tuning_SetAeComp(int comp) {
    LOG_ISP("SetAeComp: %d", comp);
    return 0;
}

int IMP_ISP_Tuning_SetMaxAgain(uint32_t gain) {
    LOG_ISP("SetMaxAgain: %u", gain);
    return 0;
}

int IMP_ISP_Tuning_SetMaxDgain(uint32_t gain) {
    LOG_ISP("SetMaxDgain: %u", gain);
    return 0;
}

int IMP_ISP_Tuning_SetBacklightComp(uint32_t strength) {
    LOG_ISP("SetBacklightComp: %u", strength);
    return 0;
}

int IMP_ISP_Tuning_SetDPC_Strength(uint32_t ratio) {
    LOG_ISP("SetDPC_Strength: %u", ratio);
    return 0;
}

int IMP_ISP_Tuning_SetDRC_Strength(uint32_t ratio) {
    LOG_ISP("SetDRC_Strength: %u", ratio);
    return 0;
}

int IMP_ISP_Tuning_SetHiLightDepress(uint32_t strength) {
    LOG_ISP("SetHiLightDepress: %u", strength);
    return 0;
}

int IMP_ISP_Tuning_SetTemperStrength(uint32_t ratio) {
    LOG_ISP("SetTemperStrength: %u", ratio);
    return 0;
}

int IMP_ISP_Tuning_SetSinterStrength(uint32_t ratio) {
    LOG_ISP("SetSinterStrength: %u", ratio);
    return 0;
}

int IMP_ISP_Tuning_SetBcshHue(unsigned char hue) {
    LOG_ISP("SetBcshHue: %u", hue);
    return 0;
}

int IMP_ISP_Tuning_SetDefog_Strength(uint32_t strength) {
    LOG_ISP("SetDefog_Strength: %u", strength);
    return 0;
}

int IMP_ISP_Tuning_SetWB(IMPISPWB *wb) {
    if (wb == NULL) return -1;
    LOG_ISP("SetWB: mode=%d, rgain=%u, bgain=%u", wb->mode, wb->rgain, wb->bgain);
    return 0;
}

int IMP_ISP_Tuning_GetWB(IMPISPWB *wb) {
    if (wb == NULL) return -1;
    wb->mode = IMPISP_WB_MODE_AUTO;
    wb->rgain = 256;
    wb->bgain = 256;
    return 0;
}

