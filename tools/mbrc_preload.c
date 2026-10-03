/*
 * mbrc_preload.so: device test aid for the run-time IMP_Encoder_SetMbRC
 * (docs/T23_EPRC.md, "Macroblock rate control").  Preloaded into the
 * streamer, it toggles the macroblock rate control of one channel every
 * MBRC_PERIOD seconds, from a thread of the streamer's own process (the IMP
 * state lives there):
 *
 *   MBRC_CHN=0 MBRC_PERIOD=20 MBRC_COUNT=6 \
 *   LD_PRELOAD=/tmp/mbrc_preload.so timpsd ...
 *
 * The first call (after MBRC_PERIOD seconds) switches it on, the next off,
 * and so on, MBRC_COUNT calls in all (0: without end).  Every call and the
 * IMP_Encoder_GetMbRC read-back are printed to stderr.
 *
 *   mipsel-linux-gcc -O2 -fPIC -shared -DPLATFORM_T23 (or _T21) \
 *       -o mbrc_preload.so mbrc_preload.c -Iinclude -lpthread
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <imp/imp_encoder.h>

static int env_int(const char *name, int fallback)
{
    const char *v = getenv(name);

    return v && *v ? atoi(v) : fallback;
}

static void *worker(void *arg)
{
    int chn = env_int("MBRC_CHN", 0);
    int period = env_int("MBRC_PERIOD", 20);
    int count = env_int("MBRC_COUNT", 6);
    int i, on = 1;

    (void)arg;
    for (i = 0; count <= 0 || i < count; i++, on = !on) {
        int ret, back = -1;

        sleep((unsigned int)(period > 0 ? period : 20));
        ret = IMP_Encoder_SetMbRC(chn, on);
        if (IMP_Encoder_GetMbRC(chn, &back) != 0)
            back = -1;
        fprintf(stderr, "mbrc_preload: SetMbRC(chn %d, %d) = %d, read back "
                "%d\n", chn, on, ret, back);
    }
    return NULL;
}

__attribute__((constructor)) static void mbrc_preload_init(void)
{
    pthread_t thread;

    if (pthread_create(&thread, NULL, worker, NULL) == 0)
        pthread_detach(thread);
}
