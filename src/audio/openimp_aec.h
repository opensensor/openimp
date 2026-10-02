/* Acoustic echo cancellation for the AI path: WebRTC AECM (fixed point,
 * BSD-3-Clause, vendored under src/audio/webrtc).  The caller supplies the
 * far-end (speaker) reference time-aligned with the near-end microphone
 * samples; both are mono S16 at 8 or 16 kHz. */
#ifndef OPENIMP_AEC_H
#define OPENIMP_AEC_H

#include <stddef.h>
#include <stdint.h>

typedef struct OpenimpAec OpenimpAec;

/* NULL unless sample_rate is 8000 or 16000 and the canceller initialised. */
OpenimpAec *openimp_aec_create(int sample_rate);
/* Samples per 10 ms block; frames passed to openimp_aec_process must be a
 * multiple of it. */
size_t openimp_aec_block_samples(const OpenimpAec *aec);
/* Cancels the echo of far_end in near_end, in place.  samples must be a
 * whole number of 10 ms blocks.  0 on success. */
int openimp_aec_process(OpenimpAec *aec, const int16_t *far_end,
                        int16_t *near_end, size_t samples);
/* AECM's current echo delay estimate in ms, -1 when unknown. */
int openimp_aec_delay_ms(const OpenimpAec *aec);
void openimp_aec_free(OpenimpAec *aec);

#endif
