/* Host test of src/audio/openimp_aec.c (WebRTC AECM).
 *
 * Far end: a synthetic talker (pitch-varying glottal pulses through moving
 * formant resonators, syllable envelope).  Echo: far end convolved with a
 * room impulse response (direct path after 1.5 ms, or 40 ms to exercise
 * AECM's delay estimator, then a 60 ms exponential tail),
 * -6 dB.  Near end: a second talker with another pitch and formants, plus
 * -60 dBFS noise.  Timeline at 16 kHz and 8 kHz:
 *   0-8 s   far end only        -> ERLE (last 4 s, after convergence)
 *   8-10 s  near end only       -> near-end loss (must stay small)
 *   10-14 s double talk         -> echo attenuation in the output
 * Also reports the processing time per second of 16 kHz audio. */
#define _POSIX_C_SOURCE 199309L
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "audio/openimp_aec.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define SECONDS 14
#define RIR_MS 60

static uint32_t seed = 12345;
static double frand(void)
{
    seed = seed * 1664525u + 1013904223u;
    return (double)(seed >> 8) / 16777216.0 * 2.0 - 1.0;
}

/* Two-pole resonator bank driven by a pulse train. */
static void talker(double *out, size_t n, int rate, double f0, double f1,
                   double f2, double syllable_hz, double phase)
{
    double y1[2] = {0, 0}, y2[2] = {0, 0}, t0 = 0;
    size_t i;

    for (i = 0; i < n; i++) {
        double t = (double)i / rate;
        double pitch = f0 * (1.0 + 0.15 * sin(2 * M_PI * 0.7 * t + phase));
        double a = 0.5 - 0.5 * cos(2 * M_PI * syllable_hz * t + phase);
        double fa = f1 * (1.0 + 0.3 * sin(2 * M_PI * 1.3 * t + phase));
        double fb = f2 * (1.0 + 0.2 * sin(2 * M_PI * 0.9 * t + 2 * phase));
        double r = 0.97, x = 0.02 * frand();
        double c1 = 2 * r * cos(2 * M_PI * fa / rate);
        double c2 = 2 * r * cos(2 * M_PI * fb / rate);
        double v1, v2;

        t0 += pitch / rate;
        if (t0 >= 1.0) {
            t0 -= 1.0;
            x += 1.0;
        }
        v1 = x + c1 * y1[0] - r * r * y1[1];
        y1[1] = y1[0];
        y1[0] = v1;
        v2 = x + c2 * y2[0] - r * r * y2[1];
        y2[1] = y2[0];
        y2[0] = v2;
        out[i] = a * a * (v1 + 0.6 * v2);
    }
}

static void normalise(double *x, size_t n, double rms_target)
{
    double e = 0;
    size_t i;

    for (i = 0; i < n; i++)
        e += x[i] * x[i];
    e = sqrt(e / n);
    for (i = 0; i < n; i++)
        x[i] *= rms_target / e;
}

static double energy16(const int16_t *x, size_t from, size_t to)
{
    double e = 0;
    size_t i;

    for (i = from; i < to; i++)
        e += (double)x[i] * x[i];
    return e + 1e-9;
}

static int16_t sat(double v)
{
    return v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)lrint(v);
}

static int run(int rate, size_t frame, int report_cpu, double delay_ms)
{
    size_t direct = (size_t)(rate * delay_ms / 1000);
    size_t n = (size_t)rate * SECONDS;
    size_t rir_n = direct + (size_t)rate * RIR_MS / 1000;
    size_t i, k, s = (size_t)rate;
    double *far = calloc(n, sizeof(double)), *near = calloc(n, sizeof(double));
    double *rir = calloc(rir_n, sizeof(double));
    int16_t *far16 = calloc(n, 2), *mic16 = calloc(n, 2), *out16 = calloc(n, 2);
    int16_t *echo16 = calloc(n, 2), *near16 = calloc(n, 2);
    OpenimpAec *aec;
    struct timespec t0, t1;
    double erle, near_loss, dt_echo_in, dt_out, dt_near, dt_gain, dt_erle, cpu;
    int failed = 0, delay_est;

    if (!far || !near || !rir || !far16 || !mic16 || !out16 || !echo16 ||
        !near16)
        return 1;
    talker(far, n, rate, 120, 600, 1700, 3.1, 0.0);
    normalise(far, n, 3000);
    talker(near, n, rate, 210, 800, 2300, 2.3, 1.7);
    normalise(near, n, 2000);
    /* direct path after delay_ms, exponential decay (RT60 about 90 ms) */
    for (k = direct; k < rir_n; k++)
        rir[k] = 0.5 * exp(-(double)(k - direct) / (rate * 0.013)) * frand();
    rir[direct] = 0.5;
    for (i = 0; i < n; i++) {
        double e = 0;

        for (k = 0; k < rir_n && k <= i; k++)
            e += rir[k] * far[i - k];
        far16[i] = sat(far[i]);
        echo16[i] = sat(e);
        near16[i] = (i >= 8 * s) ? sat(near[i]) : 0;
        mic16[i] = sat(echo16[i] * (i < 8 * s || i >= 10 * s) + near16[i] +
                       30 * frand());
        if (i >= 8 * s && i < 10 * s)
            far16[i] = 0; /* near end only */
    }
    /* the echo is silent while the far end is */
    for (i = 8 * s; i < 10 * s; i++)
        echo16[i] = 0;

    aec = openimp_aec_create(rate);
    if (!aec || openimp_aec_block_samples(aec) != (size_t)rate / 100 ||
        openimp_aec_process(aec, far16, mic16, 1) == 0) {
        fprintf(stderr, "aec create/block check failed\n");
        return 1;
    }
    memcpy(out16, mic16, n * 2);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (i = 0; i + frame <= n; i += frame)
        if (openimp_aec_process(aec, far16 + i, out16 + i, frame) != 0) {
            fprintf(stderr, "process failed at %zu\n", i);
            return 1;
        }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    if (openimp_aec_failed_blocks(aec) != 0) {
        fprintf(stderr, "%lu blocks failed\n", openimp_aec_failed_blocks(aec));
        return 1;
    }
    delay_est = openimp_aec_delay_ms(aec);
    openimp_aec_free(aec);
    cpu = ((t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6) /
          SECONDS;

    erle = 10 * log10(energy16(mic16, 4 * s, 8 * s) /
                      energy16(out16, 4 * s, 8 * s));
    near_loss = 10 * log10(energy16(mic16, 8 * s + s / 2, 10 * s) /
                           energy16(out16, 8 * s + s / 2, 10 * s));
    dt_echo_in = energy16(echo16, 10 * s, 14 * s);
    dt_near = energy16(near16, 10 * s, 14 * s);
    dt_out = energy16(out16, 10 * s, 14 * s);
    {
        /* split the double-talk output into g * near + residual */
        double cross = 0, res = 0, g;

        for (i = 10 * s; i < 14 * s; i++)
            cross += (double)out16[i] * near16[i];
        g = cross / dt_near;
        for (i = 10 * s; i < 14 * s; i++) {
            double r = out16[i] - g * near16[i];
            res += r * r;
        }
        dt_gain = 20 * log10(fabs(g) + 1e-9);
        dt_erle = 10 * log10(dt_echo_in / (res + 1e-9));
    }
    printf("aec %5d Hz frame %3zu delay %4.1f ms: ERLE %.1f dB, near-end only loss %.1f dB, "
           "double talk (echo %+.1f dB over near): near kept at %.1f dB, "
           "echo reduced %.1f dB",
           rate, frame, delay_ms, erle, near_loss, 10 * log10(dt_echo_in / dt_near), dt_gain, dt_erle);
    (void)dt_out;
    printf(", AECM delay %d ms", delay_est);
    if (report_cpu)
        printf(", %.2f ms CPU per s audio (%.2f%% of one host core)", cpu,
               cpu / 10.0);
    printf("\n");
    if (erle < 15.0 || near_loss > 3.0) {
        fprintf(stderr, "aec %d Hz: ERLE %.1f < 15 dB or near loss %.1f > "
                        "3 dB\n", rate, erle, near_loss);
        failed = 1;
    }
    free(far); free(near); free(rir); free(far16); free(mic16); free(out16);
    free(echo16); free(near16);
    return failed;
}

int main(void)
{
    int failed = 0;

    if (openimp_aec_create(44100) || openimp_aec_create(48000)) {
        fprintf(stderr, "AECM must refuse rates other than 8/16 kHz\n");
        return 1;
    }
    failed |= run(16000, 640, 1, 1.5); /* 40 ms frames, typical timps */
    failed |= run(16000, 160, 0, 1.5); /* 10 ms */
    failed |= run(16000, 640, 0, 40.0);
    failed |= run(8000, 320, 1, 1.5);
    return failed;
}
