/*
 * Stand-in for Ingenic's libaudioProcess.so on the host: the HPF, NS and
 * AGC entry points openimp_t31_audio.c resolves with dlsym. The handles are
 * heap blocks with a magic word, so a call on a freed or foreign handle
 * shows (as an abort here, as use-after-free under ASan).
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FAKE_MAGIC 0x41505246u  /* "FRPA" */

typedef struct {
    uint32_t magic;
    int config;
} FakeHandle;

typedef struct {
    int16_t target_level_dbfs;
    int16_t compression_gain_db;
    uint8_t limiter_enable;
} FakeAgcConfig;

static void *fake_create(void)
{
    FakeHandle *handle = calloc(1, sizeof(*handle));

    if (handle)
        handle->magic = FAKE_MAGIC;
    return handle;
}

static void fake_check(void *handle)
{
    if (!handle || ((FakeHandle *)handle)->magic != FAKE_MAGIC)
        abort();
}

static int fake_free(void *handle)
{
    fake_check(handle);
    ((FakeHandle *)handle)->magic = 0;
    free(handle);
    return 0;
}

void audio_process_hpf_create(int16_t *y, int16_t *x, int16_t a, int16_t b,
                              int c, int d)
{
    (void)a; (void)b; (void)c; (void)d;
    memset(y, 0, 4 * sizeof(*y));
    memset(x, 0, 2 * sizeof(*x));
}

int audio_process_hpf_process(int16_t *state, int16_t *samples, int count)
{
    const int16_t *coefficients;

    /* the state carries a pointer to the coefficients (see t31_hpf_setup) */
    memcpy(&coefficients, (unsigned char *)state + 12, sizeof(coefficients));
    if (!coefficients)
        abort();
    (void)samples;
    (void)count;
    return 0;
}

void audio_process_hpf_free(void)
{
}

void *audio_process_ns_create(void)
{
    return fake_create();
}

int audio_process_ns_set_config(void *ns, int rate, int mode)
{
    fake_check(ns);
    ((FakeHandle *)ns)->config = rate + mode;
    return 0;
}

void audio_process_ns_process(void *ns, const float *const *in, int bands,
                              float *const *out)
{
    fake_check(ns);
    (void)bands;
    /* both are 160-float arrays in the caller */
    memcpy(out[0], in[0], 160 * sizeof(float));
}

int audio_process_ns_free(void *ns)
{
    return fake_free(ns);
}

void *audio_process_agc_create(void)
{
    return fake_create();
}

int audio_process_agc_set_config(void *agc, int min, int max, int mode,
                                 int rate, FakeAgcConfig config)
{
    fake_check(agc);
    ((FakeHandle *)agc)->config = min + max + mode + rate +
                                  config.target_level_dbfs;
    return 0;
}

int audio_process_agc_process(void *agc, const int16_t *const *in,
                              size_t bands, size_t samples,
                              int16_t *const *out, int32_t in_level,
                              int32_t *out_level, int16_t echo,
                              uint8_t *saturated)
{
    fake_check(agc);
    (void)bands; (void)in_level; (void)echo;
    if (out[0] != in[0])
        memcpy(out[0], in[0], samples * sizeof(int16_t));
    *out_level = 0;
    *saturated = 0;
    return 0;
}

int audio_process_agc_free(void *agc)
{
    return fake_free(agc);
}
