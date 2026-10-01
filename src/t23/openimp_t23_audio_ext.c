/* T23 1.3.0 audio extras on top of the shared AI/AO implementation
 * (src/t31/openimp_t31_audio.c):
 *
 *  - IMP_AI_EnableAlgo/DisableAlgo, IMP_AO_EnableAlgo/DisableAlgo: as in the
 *    OEM ai.c/ao.c, read the WebRTC profile (webrtc_profile.ini in the
 *    directory given to IMP_AI_Set_WebrtcProfileIni_Path, default
 *    /etc/webrtc_profile.ini) and enable NS/AGC/HPF for AI, AGC/HPF for AO,
 *    through the public IMP_A[IO]_Enable{Ns,Agc,Hpf} calls;
 *  - IMP_AI_EnableHs/DisableHs: howling suppression with
 *    audio_process_hs_* from libaudioProcess.so, run on every captured
 *    frame ahead of the other effects;
 *  - IMP_A[IO]_IMPDBG_Init/DeInit: the OEM registers callbacks with its
 *    "dsys" debug server; OpenIMP has none, so they succeed and do nothing.
 */

#include <ctype.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <imp/imp_audio.h>

#include "imp_log_int.h"

#define T23_PROFILE_NAME    "webrtc_profile.ini"
#define T23_PROFILE_DEFAULT "/etc/" T23_PROFILE_NAME

extern const char *openimp_t23_audio_profile(void);
extern void openimp_audio_set_ao_agc_mode(int mode);

/* ---- webrtc_profile.ini ---------------------------------------------- */

static void profile_path(char *path, size_t size)
{
    const char *dir = openimp_t23_audio_profile();
    size_t len = dir ? strlen(dir) : 0;
    size_t name = sizeof(T23_PROFILE_NAME);    /* includes the NUL */

    if (!len || len + 1 + name > size) {
        memcpy(path, T23_PROFILE_DEFAULT, sizeof(T23_PROFILE_DEFAULT));
    } else if (len > 4 && !strcmp(dir + len - 4, ".ini")) {
        memcpy(path, dir, len + 1);
    } else {
        /* OEM: sprintf("%s/%s", dir, "webrtc_profile.ini") */
        memcpy(path, dir, len);
        path[len] = '/';
        memcpy(path + len + 1, T23_PROFILE_NAME, name);
    }
}

static char *trim(char *s)
{
    char *end;

    while (isspace((unsigned char)*s))
        s++;
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return s;
}

/* "[section]" followed by "key = value" lines; '#' and ';' comment. */
static int read_profile(const char *path, const char *section,
                        const char *key, char *value, size_t size)
{
    char line[256];
    int in_section = 0;
    FILE *f = fopen(path, "r");

    if (!f)
        return -1;
    while (fgets(line, sizeof(line), f)) {
        char *s = trim(line);
        char *eq;

        if (!*s || *s == '#' || *s == ';')
            continue;
        if (*s == '[') {
            char *close = strchr(s, ']');

            if (close)
                *close = '\0';
            in_section = !strcmp(trim(s + 1), section);
            continue;
        }
        eq = strchr(s, '=');
        if (!in_section || !eq)
            continue;
        *eq = '\0';
        if (strcmp(trim(s), key))
            continue;
        snprintf(value, size, "%s", trim(eq + 1));
        fclose(f);
        return 0;
    }
    fclose(f);
    return -1;
}

static int profile_true(const char *path, const char *section,
                        const char *key)
{
    char value[64];

    return read_profile(path, section, key, value, sizeof(value)) == 0 &&
           !strcmp(value, "true");
}

static int profile_int(const char *path, const char *section,
                       const char *key, int fallback)
{
    char value[64];

    if (read_profile(path, section, key, value, sizeof(value)) != 0)
        return fallback;
    return atoi(value);
}

static int profile_agc_mode(const char *path, const char *section)
{
    char value[64];

    if (read_profile(path, section, "set_mode", value, sizeof(value)) != 0)
        return 2;
    if (!strcmp(value, "kAdaptiveAnalog"))
        return 1;
    if (!strcmp(value, "kFixedDigital"))
        return 3;
    return 2;                       /* kAdaptiveDigital, also the fallback */
}

/* ---- AI / AO algorithm sets ------------------------------------------ */

int IMP_AI_EnableAlgo(int device, int channel)
{
    IMPAudioIOAttr attr;
    char path[320];

    if (device < 0 || device > 1 || channel > 0)
        return -1;
    memset(&attr, 0, sizeof(attr));
    if (IMP_AI_GetPubAttr(device, &attr) != 0)
        return -1;
    profile_path(path, sizeof(path));

    if (profile_true(path, "NS_AI", "NS_enable")) {
        char value[64];
        int level = 2;              /* kHigh */

        if (read_profile(path, "NS_AI", "set_level", value,
                         sizeof(value)) == 0) {
            if (!strcmp(value, "kLow"))
                level = 0;
            else if (!strcmp(value, "kModerate"))
                level = 1;
            else if (!strcmp(value, "kVeryHigh"))
                level = 3;
        }
        if (IMP_AI_EnableNs(&attr, level) != 0)
            IMP_LOG_ERR("AI", "%s: enable ns is fail", __func__);
    }
    if (profile_true(path, "AGC_AI", "AGC_enable")) {
        IMPAudioAgcConfig agc;

        memset(&agc, 0, sizeof(agc));
        IMP_AI_SetAgcMode(profile_agc_mode(path, "AGC_AI"));
        agc.TargetLevelDbfs =
            profile_int(path, "AGC_AI", "set_target_level_dbfs", 3);
        agc.CompressionGaindB =
            profile_int(path, "AGC_AI", "set_compression_gain_db", 1);
        if (IMP_AI_EnableAgc(&attr, agc) != 0)
            IMP_LOG_ERR("AI", "%s: enable agc is fail", __func__);
    }
    if (profile_true(path, "HP_AI", "HP_enable")) {
        IMP_AI_SetHpfCoFrequency(profile_int(path, "HP_AI", "cofrequency",
                                             400));
        if (IMP_AI_EnableHpf(&attr) != 0) {
            IMP_LOG_ERR("AI", "%s: enable hpf is fail", __func__);
            return -1;
        }
    }
    return 0;
}

int IMP_AI_DisableAlgo(int device, int channel)
{
    if (device < 0 || device > 1 || channel > 0)
        return -1;
    IMP_AI_DisableHpf();
    IMP_AI_DisableNs();
    IMP_AI_DisableAgc();
    return 0;
}

int IMP_AO_EnableAlgo(int device, int channel)
{
    IMPAudioIOAttr attr;
    char path[320];

    if (device > 0 || channel > 0)
        return -1;
    memset(&attr, 0, sizeof(attr));
    if (IMP_AO_GetPubAttr(device, &attr) != 0)
        return -1;
    profile_path(path, sizeof(path));

    if (profile_true(path, "AGC_AO", "AGC_enable")) {
        IMPAudioAgcConfig agc;

        memset(&agc, 0, sizeof(agc));
        /* the OEM keeps a separate AO AGC mode (ao_agc_mode) */
        openimp_audio_set_ao_agc_mode(profile_agc_mode(path, "AGC_AO"));
        agc.TargetLevelDbfs =
            profile_int(path, "AGC_AO", "set_target_level_dbfs", 3);
        agc.CompressionGaindB =
            profile_int(path, "AGC_AO", "set_compression_gain_db", 1);
        if (IMP_AO_EnableAgc(&attr, agc) != 0)
            IMP_LOG_ERR("AO", "%s: enable agc is fail", __func__);
    }
    if (profile_true(path, "HP_AO", "HP_enable")) {
        IMP_AO_SetHpfCoFrequency(profile_int(path, "HP_AO", "cofrequency",
                                             400));
        if (IMP_AO_EnableHpf(&attr) != 0) {
            IMP_LOG_ERR("AO", "%s: enable hpf is fail", __func__);
            return -1;
        }
    }
    return 0;
}

int IMP_AO_DisableAlgo(int device, int channel)
{
    if (device > 0 || channel > 0)
        return -1;
    IMP_AO_DisableHpf();
    IMP_AO_DisableAgc();
    return 0;
}

/* ---- howling suppression --------------------------------------------- */

typedef void *(*T23HsCreate)(int sample_rate);
typedef int (*T23HsProcess)(void *handle, int16_t *samples, int count);
typedef void (*T23HsFree)(void *handle);

static pthread_mutex_t hs_lock = PTHREAD_MUTEX_INITIALIZER;
static void *hs_library;
static T23HsCreate hs_create;
static T23HsProcess hs_process;
static T23HsFree hs_free;
static void *hs_handle;
static int hs_enabled;
static int hs_rate;

/* thingino's libaudioProcess (audioProcess-neo) API:
 * hs_create(sample_rate), hs_process(handle, samples, count) in place. */
static int hs_load(void)
{
    if (hs_library)
        return 0;
    hs_library = dlopen("libaudioProcess.so", RTLD_NOW | RTLD_LOCAL);
    if (!hs_library)
        return -1;
    *(void **)&hs_create = dlsym(hs_library, "audio_process_hs_create");
    *(void **)&hs_process = dlsym(hs_library, "audio_process_hs_process");
    *(void **)&hs_free = dlsym(hs_library, "audio_process_hs_free");
    if (!hs_create || !hs_process || !hs_free) {
        dlclose(hs_library);
        hs_library = NULL;
        return -1;
    }
    return 0;
}

int IMP_AI_EnableHs(void)
{
    int ret = 0;

    pthread_mutex_lock(&hs_lock);
    if (!hs_enabled) {
        if (hs_load() != 0) {
            IMP_LOG_ERR("AI", "howling suppression needs libaudioProcess.so");
            ret = -1;
        } else {
            hs_enabled = 1;         /* handle created at the capture rate */
        }
    }
    pthread_mutex_unlock(&hs_lock);
    return ret;
}

int IMP_AI_DisableHs(void)
{
    pthread_mutex_lock(&hs_lock);
    if (hs_handle)
        hs_free(hs_handle);
    hs_handle = NULL;
    hs_enabled = 0;
    hs_rate = 0;
    pthread_mutex_unlock(&hs_lock);
    return 0;
}

void openimp_t23_ai_pre_effects(int16_t *samples, int count, int sample_rate)
{
    if (!__atomic_load_n(&hs_enabled, __ATOMIC_RELAXED) || !samples ||
        count <= 0)
        return;
    pthread_mutex_lock(&hs_lock);
    if (hs_enabled && hs_handle && hs_rate != sample_rate) {
        hs_free(hs_handle);
        hs_handle = NULL;
    }
    if (hs_enabled && !hs_handle) {
        hs_handle = hs_create(sample_rate);
        hs_rate = sample_rate;
    }
    if (hs_enabled && hs_handle)
        (void)hs_process(hs_handle, samples, count);
    pthread_mutex_unlock(&hs_lock);
}

/* ---- IMPDBG ----------------------------------------------------------- */

int IMP_AI_IMPDBG_Init(void)
{
    return 0;
}

int IMP_AI_IMPDBG_DeInit(void)
{
    return 0;
}

int IMP_AO_IMPDBG_Init(void)
{
    return 0;
}

int IMP_AO_IMPDBG_DeInit(void)
{
    return 0;
}
