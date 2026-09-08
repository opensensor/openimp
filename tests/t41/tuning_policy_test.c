#define _GNU_SOURCE
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#define ioctl test_ioctl
#include "../../src/openimp_tuning.c"
#undef ioctl

static uint32_t active_target;
static struct t41_awb_control active_awb = {1, 1500, 2400};
static int zero_error, untouched, awb_error, ae_sets, wb_sets, model_sets;
static int native_owner, owner_error, scene_gets, target_gets;
int test_ioctl(int fd, unsigned long command, ...)
{
    va_list args;
    struct t40_tuning_request *request;
    (void)fd;
    assert(command == TISP_VIDIOC_DEFAULT_TUNING);
    va_start(args, command);
    request = va_arg(args, struct t40_tuning_request *);
    va_end(args);
    void *value = (void *)request->value_or_pointer;
    if (request->id == TISP_CID_OPEN_AE_TARGET) {
        if (request->is_get) {
            if (!untouched) *(uint32_t *)value = active_target;
        } else {
            if (!*(uint32_t *)value && zero_error) { errno = zero_error; return -1; }
            ++ae_sets;
            active_target = *(uint32_t *)value;
        }
    } else if (request->id == TISP_CID_OPEN_AWB_CONTROL) {
        if (awb_error) { errno = awb_error; return -1; }
        if (request->is_get) *(struct t41_awb_control *)value = active_awb;
        else { active_awb = *(struct t41_awb_control *)value; ++wb_sets; }
    } else if (request->id == TISP_CID_OPEN_COLOR_MODEL) {
        ++model_sets;
    } else if (request->id == TISP_CID_OPEN_AWB_OWNER) {
        assert(request->is_get);
        if (owner_error) { errno = owner_error; return -1; }
        if (native_owner) *(uint32_t *)value = TISP_AWB_OWNER_NATIVE;
    } else if (request->id == TISP_CID_T41_AE_EXPR) {
        int32_t gain = 100000;
        memcpy((uint8_t *)value + TISP_T41_AE_EXPR_TOTAL_GAIN_OFFSET, &gain, sizeof(gain));
    } else if (request->id == TISP_CID_OPEN_AWB_SCENE) {
        ++scene_gets;
        struct t41_awb_scene *scene = value;
        scene->raw_r_q10 = 1500; scene->raw_b_q10 = 5000;
    } else if (request->id == TISP_CID_OPEN_AWB_TARGET) {
        ++target_gets;
        errno = ENODATA; return -1;
    } else {
        assert(!"unexpected tuning control");
    }
    return 0;
}

int main(void)
{
    OpenIMPTuningController c = {0};
    OpenIMP_Tuning_DefaultProfile(&c.profile);
    assert(c.profile.exposure_target_q8 == 0);
    assert(!tuning_apply(&c, &c.profile));
    assert(c.calibrated_ae_policy && active_target == 0);
    assert(c.profile.red_gain == 1500 && c.profile.blue_gain == 2400);
    assert(active_awb.mode == 0 && wb_sets == 1 && model_sets == 0);
    for (int i = 0; i < 100; ++i) assert(!tuning_feedback(&c));
    assert(wb_sets == 1 && model_sets == 0 && active_target == 0);
    assert(c.low_light_evidence == 0); /* high/warm scene cannot select a replay */

    native_owner = 1;
    assert(!tuning_apply(&c, &c.profile));
    assert(c.native_awb_policy && c.profile.auto_white_balance && active_awb.mode == 1);
    assert(c.profile.feedback_interval_ms == 1000);
    scene_gets = target_gets = wb_sets = 0;
    for (int i = 0; i < 100; ++i) assert(!tuning_feedback(&c));
    assert(!scene_gets && !target_gets && !wb_sets);
    c.profile.feedback_interval_ms = 250;
    assert(!tuning_apply(&c, &c.profile) && c.profile.feedback_interval_ms == 250);
    native_owner = 0; owner_error = EAGAIN; wb_sets = 0;
    assert(tuning_apply(&c, &c.profile) == -EAGAIN && !wb_sets);
    owner_error = ENOTTY;
    assert(!tuning_apply(&c, &c.profile) && !c.native_awb_policy && active_awb.mode == 0);
    owner_error = 0;

    c.profile.kind = OPENIMP_TUNING_PROFILE_CUSTOM;
    c.profile.exposure_target_q8 = 15000;
    assert(!tuning_apply(&c, &c.profile) && active_target == 15000);
    OpenIMP_Tuning_DefaultProfile(&c.profile);
    assert(!tuning_apply(&c, &c.profile) && active_target == 0);

    const int legacy[] = {ERANGE, EOPNOTSUPP, ENOTTY};
    for (unsigned int i = 0; i < sizeof(legacy)/sizeof(legacy[0]); ++i) {
        c = (OpenIMPTuningController){0};
        OpenIMP_Tuning_DefaultProfile(&c.profile);
        zero_error = legacy[i];
        assert(!tuning_apply(&c, &c.profile));
        assert(!c.calibrated_ae_policy && active_target == 17600);
        assert(active_awb.red_gain == 1476 && active_awb.blue_gain == 3524);
    }
    zero_error = 0; untouched = 1;
    c = (OpenIMPTuningController){0}; OpenIMP_Tuning_DefaultProfile(&c.profile);
    assert(!tuning_apply(&c, &c.profile));
    assert(!c.calibrated_ae_policy && active_target == 17600);
    untouched = 0;
    const int transient[] = {EAGAIN, ENODEV, EBUSY, EINVAL};
    for (unsigned int i = 0; i < sizeof(transient)/sizeof(transient[0]); ++i) {
        zero_error = transient[i];
        c = (OpenIMPTuningController){0}; OpenIMP_Tuning_DefaultProfile(&c.profile);
        ae_sets = wb_sets = model_sets = 0;
        assert(tuning_apply(&c, &c.profile) == -transient[i]);
        assert(!ae_sets && !wb_sets && !model_sets);
    }
    zero_error = 0; awb_error = EAGAIN;
    assert(tuning_apply(&c, &c.profile) == -EAGAIN);
    assert(!wb_sets && !model_sets);
    puts("T41 calibrated security policy, legacy negotiation and readiness: passed");
    return 0;
}
