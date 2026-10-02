/* OpenIMP AEC: WebRTC AECM built as one translation unit.
 *
 * The files under src/audio/webrtc are copies from
 * webrtc-audio-processing v0.3.1 (freedesktop.org, commit e882a544),
 * BSD-3-Clause, see src/audio/webrtc/LICENSE, PATENTS and
 * LICENSE_THIRD_PARTY.  The one change, marked "OpenIMP:", is in
 * WebRtcAecm_Create (calloc instead of an unchecked malloc whose error path
 * freed uninitialised pointers).  Only the AECM core and the signal-processing,
 * delay-estimator and ring-buffer helpers it needs are included, all as
 * portable C (no MIPS32_LE/DSP assembly). */

#ifndef NDEBUG
#define NDEBUG
#endif
#ifndef WEBRTC_POSIX
#define WEBRTC_POSIX /* pthread_once in spl_init.c */
#endif

#include "webrtc/common_audio/ring_buffer.c"
#include "webrtc/common_audio/signal_processing/complex_bit_reverse.c"
#include "webrtc/common_audio/signal_processing/complex_fft.c"
#include "webrtc/common_audio/signal_processing/cross_correlation.c"
#include "webrtc/common_audio/signal_processing/division_operations.c"
#include "webrtc/common_audio/signal_processing/downsample_fast.c"
#include "webrtc/common_audio/signal_processing/min_max_operations.c"
#include "webrtc/common_audio/signal_processing/randomization_functions.c"
#include "webrtc/common_audio/signal_processing/real_fft.c"
#include "webrtc/common_audio/signal_processing/spl_init.c"
#include "webrtc/common_audio/signal_processing/spl_sqrt_floor.c"
#include "webrtc/common_audio/signal_processing/vector_scaling_operations.c"
#include "webrtc/modules/audio_processing/utility/delay_estimator.c"
#include "webrtc/modules/audio_processing/utility/delay_estimator_wrapper.c"
#include "webrtc/modules/audio_processing/aecm/aecm_core.c"
#include "webrtc/modules/audio_processing/aecm/aecm_core_c.c"
#include "webrtc/modules/audio_processing/aecm/echo_control_mobile.c"

#include "openimp_aec.h"

/* echoMode 0..4 is AECM's suppression strength; 3 is WebRTC's default,
 * 4 the most aggressive.  Comfort noise is off: the AI path has its own
 * noise suppression and a camera microphone should stay silent rather
 * than hiss during double talk. */
#ifndef OPENIMP_AEC_ECHO_MODE
#define OPENIMP_AEC_ECHO_MODE 3
#endif

struct OpenimpAec {
    void *aecm;
    size_t block;
};

OpenimpAec *openimp_aec_create(int sample_rate)
{
    OpenimpAec *aec;
    AecmConfig config;

    if (sample_rate != 8000 && sample_rate != 16000)
        return NULL;
    aec = calloc(1, sizeof(*aec));
    if (!aec)
        return NULL;
    aec->block = (size_t)sample_rate / 100U;
    aec->aecm = WebRtcAecm_Create();
    config.cngMode = AecmFalse;
    config.echoMode = OPENIMP_AEC_ECHO_MODE;
    if (!aec->aecm || WebRtcAecm_Init(aec->aecm, sample_rate) != 0 ||
        WebRtcAecm_set_config(aec->aecm, config) != 0) {
        openimp_aec_free(aec);
        return NULL;
    }
    return aec;
}

size_t openimp_aec_block_samples(const OpenimpAec *aec)
{
    return aec ? aec->block : 0;
}

int openimp_aec_process(OpenimpAec *aec, const int16_t *far_end,
                        int16_t *near_end, size_t samples)
{
    int16_t out[160];
    size_t offset;

    if (!aec || !far_end || !near_end || samples % aec->block)
        return -1;
    for (offset = 0; offset < samples; offset += aec->block) {
        /* the reference is sample-aligned with the microphone (the codec
         * records both), so no sound-card buffer delay is reported */
        if (WebRtcAecm_BufferFarend(aec->aecm, far_end + offset,
                                    aec->block) != 0 ||
            WebRtcAecm_Process(aec->aecm, near_end + offset, NULL, out,
                               aec->block, 0) != 0)
            return -1;
        memcpy(near_end + offset, out, aec->block * sizeof(out[0]));
    }
    return 0;
}

int openimp_aec_delay_ms(const OpenimpAec *aec)
{
    const AecMobile *mobile;

    if (!aec || !aec->aecm)
        return -1;
    int delay;

    mobile = (const AecMobile *)aec->aecm;
    /* PART_LEN-sample partitions; -2 while the estimator is not sure */
    delay = WebRtc_last_delay(mobile->aecmCore->delay_estimator);
    if (delay < 0)
        return -1;
    return delay * PART_LEN * 1000 / mobile->sampFreq;
}

void openimp_aec_free(OpenimpAec *aec)
{
    if (!aec)
        return;
    if (aec->aecm)
        WebRtcAecm_Free(aec->aecm);
    free(aec);
}
