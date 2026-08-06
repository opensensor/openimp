/* Standalone ISP tuning-policy controller for private Ingenic tuning nodes. */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "openimp/openimp_tuning.h"

#define OPENIMP_TUNING_DEFAULT_INTERVAL_MS 40U

#if defined(PLATFORM_T31)
#define TISP_VIDIOC_TUNING 0xc00c56c6U
#define TISP_VIDIOC_S_CTRL 0xc008561cU
#define TISP_CID_TOTAL_GAIN 0x08000027U
#define TISP_V4L2_CID_BRIGHTNESS 0x00980900U
#define TISP_V4L2_CID_CONTRAST 0x00980901U
#define TISP_V4L2_CID_SATURATION 0x00980902U
#define TISP_V4L2_CID_SHARPNESS 0x0098091bU
#else
#if defined(PLATFORM_T41)
#define TISP_VIDIOC_DEFAULT_TUNING 0xc0105435U
#define TISP_CID_T41_AE_EXPR 0x08000023U
#define TISP_CID_OPEN_AWB_CONTROL 0x08ff0001U
#define TISP_CID_OPEN_AE_TARGET 0x08ff0002U
#define TISP_T41_AE_EXPR_BYTES 232U
#define TISP_T41_AE_EXPR_TOTAL_GAIN_OFFSET 204U
#else
#define TISP_VIDIOC_DEFAULT_TUNING 0xc0105436U
#endif
#define TISP_CID_BCSH_HUE 0x08000081U
#define TISP_CID_BRIGHTNESS 0x08000092U
#define TISP_CID_SHARPNESS 0x08000093U
#define TISP_CID_SATURATION 0x08000094U
#define TISP_CID_CONTRAST 0x08000095U
#endif

struct OpenIMPTuningController {
    int fd;
    pthread_mutex_t lock;
    pthread_t worker;
    OpenIMPTuningProfile profile;
    int32_t last_total_gain;
    uint32_t feedback_updates;
    int worker_created;
    int stop;
    int running;
};

static int tuning_open(const char *device)
{
    int fd;

    if (device && *device)
        return open(device, O_RDWR | O_CLOEXEC);
    fd = open("/dev/isp-m0", O_RDWR | O_CLOEXEC);
#if defined(PLATFORM_T31)
    if (fd < 0)
        fd = open("/dev/isp-w02", O_RDWR | O_CLOEXEC);
#endif
    return fd;
}

void OpenIMP_Tuning_DefaultProfile(OpenIMPTuningProfile *profile)
{
    if (!profile)
        return;
    memset(profile, 0, sizeof(*profile));
    profile->kind = OPENIMP_TUNING_PROFILE_SECURITY;
    /* Presets describe policy. Static controls are opt-in so starting the
     * daemon cannot disturb an already-calibrated ISP bank. */
    profile->control_mask = 0;
    profile->brightness = 128;
    profile->contrast = 128;
    profile->saturation = 128;
    profile->sharpness = 128;
    profile->hue = 128;
    profile->gain_feedback = 1;
    profile->auto_white_balance = 0;
    /* Reproduce a converged OEM OS04D10 daylight capture.  The recovered
     * aggregate AWB model remains opt-in: its gray-world mapping is not yet
     * equivalent to the larger OEM model-selection routine. */
    profile->red_gain = 1476;
    profile->blue_gain = 3524;
    profile->exposure_target_q8 = 17600;
#if defined(PLATFORM_T41)
    profile->control_mask |= OPENIMP_TUNING_CONTROL_WHITE_BALANCE |
                             OPENIMP_TUNING_CONTROL_EXPOSURE_TARGET;
#endif
    profile->feedback_interval_ms = OPENIMP_TUNING_DEFAULT_INTERVAL_MS;
}

int OpenIMP_Tuning_ProfilePreset(OpenIMPTuningProfileKind kind,
                                 OpenIMPTuningProfile *profile)
{
    if (!profile ||
        (kind != OPENIMP_TUNING_PROFILE_SECURITY &&
         kind != OPENIMP_TUNING_PROFILE_PSYCHEDELIC))
        return -EINVAL;
    OpenIMP_Tuning_DefaultProfile(profile);
    profile->kind = kind;
#if defined(PLATFORM_T41)
    if (kind == OPENIMP_TUNING_PROFILE_PSYCHEDELIC) {
        /* Preserve the accepted warm open-ISP state as a deliberate effect. */
        profile->red_gain = 1800;
        profile->blue_gain = 3000;
        profile->exposure_target_q8 = 14500;
    }
#endif
    return 0;
}

static int tuning_profile_kind_valid(OpenIMPTuningProfileKind kind)
{
    return kind == OPENIMP_TUNING_PROFILE_SECURITY ||
           kind == OPENIMP_TUNING_PROFILE_PSYCHEDELIC ||
           kind == OPENIMP_TUNING_PROFILE_CUSTOM;
}

#if defined(PLATFORM_T31)
struct t31_tuning_value {
    int32_t direction;
    int32_t id;
    int32_t value;
};

struct t31_v4l2_control {
    int32_t id;
    int32_t value;
};

static int t31_set_control(int fd, int32_t id, int32_t value)
{
    struct t31_v4l2_control control = { id, value };

    return ioctl(fd, TISP_VIDIOC_S_CTRL, &control) < 0 ? -errno : 0;
}

static int tuning_apply(OpenIMPTuningController *controller,
                        const OpenIMPTuningProfile *profile)
{
    int ret = 0;

    if (profile->control_mask & OPENIMP_TUNING_CONTROL_BRIGHTNESS)
        ret = t31_set_control(controller->fd, TISP_V4L2_CID_BRIGHTNESS,
                              profile->brightness);
    if (!ret && (profile->control_mask & OPENIMP_TUNING_CONTROL_CONTRAST))
        ret = t31_set_control(controller->fd, TISP_V4L2_CID_CONTRAST,
                              profile->contrast);
    if (!ret && (profile->control_mask & OPENIMP_TUNING_CONTROL_SATURATION))
        ret = t31_set_control(controller->fd, TISP_V4L2_CID_SATURATION,
                              profile->saturation);
    if (!ret && (profile->control_mask & OPENIMP_TUNING_CONTROL_SHARPNESS))
        ret = t31_set_control(controller->fd, TISP_V4L2_CID_SHARPNESS,
                              profile->sharpness);
    return ret;
}

static int t31_get_total_gain(OpenIMPTuningController *controller,
                              int32_t *total_gain)
{
    struct t31_tuning_value request = { 1, TISP_CID_TOTAL_GAIN, 0 };

    if (ioctl(controller->fd, TISP_VIDIOC_TUNING, &request) < 0)
        return -errno;
    *total_gain = request.value;
    return 0;
}

static int tuning_feedback(OpenIMPTuningController *controller)
{
    int32_t total_gain = 0;
    int ret;

    ret = t31_get_total_gain(controller, &total_gain);
    if (ret || total_gain < 0 || total_gain == controller->last_total_gain)
        return ret;
    ret = t31_set_control(controller->fd, TISP_V4L2_CID_CONTRAST,
        (int32_t)(((uint32_t)total_gain << 8) |
                  controller->profile.contrast));
    if (!ret) {
        controller->last_total_gain = total_gain;
        controller->feedback_updates++;
    }
    return ret;
}
#else
struct t40_tuning_request {
    int32_t channel;
    int32_t is_get;
    int32_t id;
    uintptr_t value_or_pointer;
};

#if defined(PLATFORM_T41)
struct t41_awb_control {
    uint32_t mode;
    uint16_t red_gain;
    uint16_t blue_gain;
};

static int t41_awb_control(int fd, int is_get,
                           struct t41_awb_control *control)
{
    struct t40_tuning_request request = {
        0, is_get, TISP_CID_OPEN_AWB_CONTROL, (uintptr_t)control
    };

    return ioctl(fd, TISP_VIDIOC_DEFAULT_TUNING, &request) < 0 ? -errno : 0;
}

static int t41_ae_target(int fd, int is_get, uint32_t *target)
{
    struct t40_tuning_request request = {
        0, is_get, TISP_CID_OPEN_AE_TARGET, (uintptr_t)target
    };

    return ioctl(fd, TISP_VIDIOC_DEFAULT_TUNING, &request) < 0 ? -errno : 0;
}
#endif

static int t40_set_control(int fd, int32_t id, uint8_t value)
{
    struct t40_tuning_request request = { 0, 0, id,
                                           (uintptr_t)&value };

    return ioctl(fd, TISP_VIDIOC_DEFAULT_TUNING, &request) < 0 ? -errno : 0;
}

static int tuning_apply(OpenIMPTuningController *controller,
                        const OpenIMPTuningProfile *profile)
{
    int ret = 0;

#if defined(PLATFORM_T41)
    if (profile->control_mask & OPENIMP_TUNING_CONTROL_WHITE_BALANCE) {
        struct t41_awb_control control = {
            profile->auto_white_balance ? 1U : 0U,
            profile->red_gain,
            profile->blue_gain,
        };

        ret = t41_awb_control(controller->fd, 0, &control);
    }
    if (!ret &&
        (profile->control_mask & OPENIMP_TUNING_CONTROL_EXPOSURE_TARGET)) {
        uint32_t target = profile->exposure_target_q8;

        ret = t41_ae_target(controller->fd, 0, &target);
    }
#endif
    if (!ret && (profile->control_mask & OPENIMP_TUNING_CONTROL_BRIGHTNESS))
        ret = t40_set_control(controller->fd, TISP_CID_BRIGHTNESS,
                              profile->brightness);
    if (!ret && (profile->control_mask & OPENIMP_TUNING_CONTROL_CONTRAST))
        ret = t40_set_control(controller->fd, TISP_CID_CONTRAST,
                              profile->contrast);
    if (!ret && (profile->control_mask & OPENIMP_TUNING_CONTROL_SATURATION))
        ret = t40_set_control(controller->fd, TISP_CID_SATURATION,
                              profile->saturation);
    if (!ret && (profile->control_mask & OPENIMP_TUNING_CONTROL_SHARPNESS))
        ret = t40_set_control(controller->fd, TISP_CID_SHARPNESS,
                              profile->sharpness);
    if (!ret && (profile->control_mask & OPENIMP_TUNING_CONTROL_HUE))
        ret = t40_set_control(controller->fd, TISP_CID_BCSH_HUE,
                              profile->hue);
    return ret;
}

#if defined(PLATFORM_T41)
static int tuning_feedback(OpenIMPTuningController *controller)
{
    struct t40_tuning_request request;
    uint8_t response[TISP_T41_AE_EXPR_BYTES];
    int32_t total_gain;

    memset(response, 0, sizeof(response));
    request = (struct t40_tuning_request){
        0, 1, TISP_CID_T41_AE_EXPR, (uintptr_t)response
    };
    if (ioctl(controller->fd, TISP_VIDIOC_DEFAULT_TUNING, &request) < 0)
        return -errno;
    memcpy(&total_gain,
           response + TISP_T41_AE_EXPR_TOTAL_GAIN_OFFSET,
           sizeof(total_gain));
    if (total_gain != controller->last_total_gain) {
        controller->last_total_gain = total_gain;
        controller->feedback_updates++;
    }
    return 0;
}
#endif

#endif

#if defined(PLATFORM_T31) || defined(PLATFORM_T41)
static void *tuning_worker(void *opaque)
{
    OpenIMPTuningController *controller = opaque;

    for (;;) {
        uint16_t interval;
        int stop;
        int feedback;

        pthread_mutex_lock(&controller->lock);
        stop = controller->stop;
        feedback = controller->profile.gain_feedback;
        interval = controller->profile.feedback_interval_ms;
        if (feedback && !stop)
            tuning_feedback(controller);
        pthread_mutex_unlock(&controller->lock);
        if (stop)
            break;
        if (!interval)
            interval = OPENIMP_TUNING_DEFAULT_INTERVAL_MS;
        usleep((useconds_t)interval * 1000U);
    }
    return NULL;
}
#endif

int OpenIMP_Tuning_Create(OpenIMPTuningController **controller_out,
                          const OpenIMPTuningConfig *config)
{
    OpenIMPTuningController *controller;
    OpenIMPTuningProfile profile;

    if (!controller_out)
        return -EINVAL;
    *controller_out = NULL;
    OpenIMP_Tuning_DefaultProfile(&profile);
    if (config)
        profile = config->profile;
    if (!tuning_profile_kind_valid(profile.kind))
        return -EINVAL;
    if (!profile.feedback_interval_ms)
        profile.feedback_interval_ms = OPENIMP_TUNING_DEFAULT_INTERVAL_MS;

    controller = calloc(1, sizeof(*controller));
    if (!controller)
        return -ENOMEM;
    controller->fd = tuning_open(config ? config->device : NULL);
    if (controller->fd < 0) {
        int ret = -errno;

        free(controller);
        return ret;
    }
    if (pthread_mutex_init(&controller->lock, NULL) != 0) {
        close(controller->fd);
        free(controller);
        return -ENOMEM;
    }
    controller->profile = profile;
    controller->last_total_gain = -1;
    *controller_out = controller;
    return 0;
}

int OpenIMP_Tuning_Start(OpenIMPTuningController *controller)
{
    int ret;

    if (!controller)
        return -EINVAL;
    pthread_mutex_lock(&controller->lock);
    if (controller->running) {
        pthread_mutex_unlock(&controller->lock);
        return 0;
    }
    ret = tuning_apply(controller, &controller->profile);
    if (ret) {
        pthread_mutex_unlock(&controller->lock);
        return ret;
    }
    controller->stop = 0;
    controller->running = 1;
    pthread_mutex_unlock(&controller->lock);
#if defined(PLATFORM_T31) || defined(PLATFORM_T41)
    if (pthread_create(&controller->worker, NULL, tuning_worker,
                       controller) != 0) {
        pthread_mutex_lock(&controller->lock);
        controller->running = 0;
        pthread_mutex_unlock(&controller->lock);
        return -EAGAIN;
    }
    controller->worker_created = 1;
#endif
    return 0;
}

int OpenIMP_Tuning_Stop(OpenIMPTuningController *controller)
{
    if (!controller)
        return -EINVAL;
    pthread_mutex_lock(&controller->lock);
    controller->stop = 1;
    pthread_mutex_unlock(&controller->lock);
    if (controller->worker_created) {
        pthread_join(controller->worker, NULL);
        controller->worker_created = 0;
    }
    pthread_mutex_lock(&controller->lock);
    controller->running = 0;
    pthread_mutex_unlock(&controller->lock);
    return 0;
}

int OpenIMP_Tuning_SetProfile(OpenIMPTuningController *controller,
                              const OpenIMPTuningProfile *profile)
{
    OpenIMPTuningProfile normalized;
    int ret;

    if (!controller || !profile ||
        !tuning_profile_kind_valid(profile->kind))
        return -EINVAL;
    normalized = *profile;
    if (!normalized.feedback_interval_ms)
        normalized.feedback_interval_ms = OPENIMP_TUNING_DEFAULT_INTERVAL_MS;
    pthread_mutex_lock(&controller->lock);
    ret = tuning_apply(controller, &normalized);
    if (!ret) {
        controller->profile = normalized;
        controller->last_total_gain = -1;
    }
    pthread_mutex_unlock(&controller->lock);
    return ret;
}

int OpenIMP_Tuning_GetStatus(OpenIMPTuningController *controller,
                             OpenIMPTuningStatus *status)
{
    if (!controller || !status)
        return -EINVAL;
    pthread_mutex_lock(&controller->lock);
    status->profile = controller->profile;
    status->last_total_gain = controller->last_total_gain;
    status->feedback_updates = controller->feedback_updates;
    status->active_auto_white_balance =
        controller->profile.auto_white_balance;
    status->active_red_gain = controller->profile.red_gain;
    status->active_blue_gain = controller->profile.blue_gain;
    status->active_exposure_target_q8 =
        controller->profile.exposure_target_q8;
#if defined(PLATFORM_T41)
    {
        struct t41_awb_control control;

        if (!t41_awb_control(controller->fd, 1, &control)) {
            status->active_auto_white_balance = control.mode != 0;
            status->active_red_gain = control.red_gain;
            status->active_blue_gain = control.blue_gain;
        }
        {
            uint32_t target;

            if (!t41_ae_target(controller->fd, 1, &target))
                status->active_exposure_target_q8 = (uint16_t)target;
        }
    }
#endif
    status->running = controller->running;
    pthread_mutex_unlock(&controller->lock);
    return 0;
}

void OpenIMP_Tuning_Destroy(OpenIMPTuningController *controller)
{
    if (!controller)
        return;
    OpenIMP_Tuning_Stop(controller);
    close(controller->fd);
    pthread_mutex_destroy(&controller->lock);
    free(controller);
}
