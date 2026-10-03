/*
 * hskip_preload.so: device test aid for the run-time IMP_Encoder_SetChnHSkip
 * (docs/T23_EPRC.md, "IDR period").  Preloaded into the streamer, it calls
 * IMP_Encoder_SetChnHSkip on one channel after a delay, from a thread of the
 * streamer's own process (the IMP state lives there):
 *
 *   HSKIP_CHN=0 HSKIP_N=3 HSKIP_DELAY=20 HSKIP_TYPE=0 \
 *   LD_PRELOAD=/tmp/hskip_preload.so timpsd ...
 *
 * HSKIP_N = maxSameSceneCnt (IDR every N GOPs, scene-cut IDRs at GOP
 * boundaries), HSKIP_TYPE = skipType (0 = N1X), HSKIP_DELAY seconds after
 * load; HSKIP_N2/HSKIP_DELAY2 optionally set a second value later.  Every
 * call and the read-back are printed to stderr.
 *
 *   mipsel-linux-gcc -O2 -fPIC -shared -DPLATFORM_T23 (or _T21) \
 *       -o hskip_preload.so hskip_preload.c \
 *       -Iinclude -lpthread
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <imp/imp_encoder.h>

static int env_int(const char *name, int fallback)
{
    const char *v = getenv(name);

    return v && *v ? atoi(v) : fallback;
}

static void set_hskip(int chn, int type, int n)
{
    IMPEncoderAttrHSkip attr, back;
    int ret;

    memset(&attr, 0, sizeof(attr));
    attr.skipType = (IMPSkipType)type;
    attr.maxSameSceneCnt = n;
    ret = IMP_Encoder_SetChnHSkip(chn, &attr);
    memset(&back, 0, sizeof(back));
    fprintf(stderr, "hskip_preload: SetChnHSkip(chn %d, type %d, "
            "maxSameSceneCnt %d) = %d", chn, type, n, ret);
    if (IMP_Encoder_GetChnHSkip(chn, &back) == 0)
        fprintf(stderr, ", read back type %d maxSameSceneCnt %d",
                (int)back.skipType, back.maxSameSceneCnt);
    fprintf(stderr, "\n");
}

static void *worker(void *arg)
{
    int chn = env_int("HSKIP_CHN", 0);
    int type = env_int("HSKIP_TYPE", 0);
    int delay2 = env_int("HSKIP_DELAY2", 0);

    (void)arg;
    sleep((unsigned int)env_int("HSKIP_DELAY", 20));
    set_hskip(chn, type, env_int("HSKIP_N", 3));
    if (delay2 > 0) {
        sleep((unsigned int)delay2);
        set_hskip(chn, type, env_int("HSKIP_N2", 0));
    }
    return NULL;
}

__attribute__((constructor)) static void hskip_preload_init(void)
{
    pthread_t thread;

    if (pthread_create(&thread, NULL, worker, NULL) == 0)
        pthread_detach(thread);
}
