/*
 * T10 / T20 demask strategy of the vendor libimp 3.12.0 (see demask.h).
 * Written from the disassembly of encoder_channel_h264e_demask_strategy
 * (0x40070), IMP_Encoder_SetChnDemask (0x49160) and the post-picture block
 * of do_channel_process_h264e (0x41dd4); checked against the vendor code
 * under unicorn by tools/demask_oracle.py / tests/demask.
 */
#include "demask.h"

int demask_strategy(DemaskState *s)
{
    int32_t m = s->measure;
    int32_t thr = s->thr;

    if (!s->enable)
        return s->hold != 0;
    if (s->auto_mode) {
        /* the vendor logs "demask auto mode is not supported" (level 5) and
         * answers with the hold state */
        return s->hold != 0;
    }
    if (thr < m) {
        /* above the threshold: the hold counter is reloaded */
        s->hold = s->cnt;
        s->last = m;
        return s->hold != 0;
    }
    if (thr < s->last && !(m < thr - 1)) {
        /* just below the threshold (thr - 1 or thr) right after a picture
         * above it: reloaded again, the measure is not taken over */
        s->hold = s->cnt;
        return s->hold != 0;
    }
    if (s->hold > 0)
        s->hold--;
    s->last = m;
    /* the vendor compares unsigned (sltu): a negative demaskCnt keeps the
     * strategy active */
    return s->hold != 0;
}

int demask_set(DemaskState *s, int enable, int auto_mode, int32_t cnt,
               int32_t thr)
{
    int restore = 0;

    if (!s->enable) {
        if (enable)
            s->sharp = DEMASK_SHARP_MAX;
    } else if (!enable) {
        restore = 1;            /* IMP_ISP_Tuning_SetSharpness_internal(0x80, 0) */
    }
    s->enable = enable ? 1 : 0;
    s->auto_mode = auto_mode ? 1 : 0;
    s->cnt = cnt;
    s->thr = thr;
    return restore;
}

int demask_after_picture(DemaskState *s, uint32_t *value, int *mode)
{
    int active = demask_strategy(s);

    if (!active) {
        if (s->sharp < DEMASK_SHARP_MAX - DEMASK_SHARP_STEP)
            s->sharp += DEMASK_SHARP_STEP;
        else
            s->sharp = DEMASK_SHARP_MAX;
    } else if (s->sharp < DEMASK_SHARP_STEP + 1) {
        s->sharp = 0;
    } else {
        s->sharp -= DEMASK_SHARP_STEP;
    }
    *value = (uint32_t)s->sharp & 0xffu;
    /* mode 1 = demask override, mode 0 = back to the application's value */
    *mode = s->sharp != DEMASK_SHARP_MAX;
    return active;
}
