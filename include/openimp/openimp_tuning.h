#ifndef OPENIMP_TUNING_H
#define OPENIMP_TUNING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct OpenIMPTuningController OpenIMPTuningController;

typedef enum {
    OPENIMP_TUNING_PROFILE_SECURITY = 0,
    OPENIMP_TUNING_PROFILE_PSYCHEDELIC = 1,
    OPENIMP_TUNING_PROFILE_CUSTOM = 2,
} OpenIMPTuningProfileKind;

enum {
    OPENIMP_TUNING_CONTROL_BRIGHTNESS = 1U << 0,
    OPENIMP_TUNING_CONTROL_CONTRAST = 1U << 1,
    OPENIMP_TUNING_CONTROL_SATURATION = 1U << 2,
    OPENIMP_TUNING_CONTROL_SHARPNESS = 1U << 3,
    OPENIMP_TUNING_CONTROL_HUE = 1U << 4,
    OPENIMP_TUNING_CONTROL_ALL = (1U << 5) - 1U,
};

typedef struct {
    OpenIMPTuningProfileKind kind;
    uint32_t control_mask;
    uint8_t brightness;
    uint8_t contrast;
    uint8_t saturation;
    uint8_t sharpness;
    uint8_t hue;
    uint8_t gain_feedback;
    uint16_t feedback_interval_ms;
} OpenIMPTuningProfile;

typedef struct {
    const char *device;
    OpenIMPTuningProfile profile;
} OpenIMPTuningConfig;

typedef struct {
    OpenIMPTuningProfile profile;
    int32_t last_total_gain;
    uint32_t feedback_updates;
    int running;
} OpenIMPTuningStatus;

void OpenIMP_Tuning_DefaultProfile(OpenIMPTuningProfile *profile);
int OpenIMP_Tuning_ProfilePreset(OpenIMPTuningProfileKind kind,
                                 OpenIMPTuningProfile *profile);
int OpenIMP_Tuning_Create(OpenIMPTuningController **controller,
                          const OpenIMPTuningConfig *config);
int OpenIMP_Tuning_Start(OpenIMPTuningController *controller);
int OpenIMP_Tuning_Stop(OpenIMPTuningController *controller);
int OpenIMP_Tuning_SetProfile(OpenIMPTuningController *controller,
                              const OpenIMPTuningProfile *profile);
int OpenIMP_Tuning_GetStatus(OpenIMPTuningController *controller,
                             OpenIMPTuningStatus *status);
void OpenIMP_Tuning_Destroy(OpenIMPTuningController *controller);

#ifdef __cplusplus
}
#endif

#endif /* OPENIMP_TUNING_H */
