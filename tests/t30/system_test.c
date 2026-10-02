/*
 * IMP_System_Init/Exit and the timestamp base (src/t40/openimp_p0.c):
 * double Init and Exit, Exit/Init cycles, and Init/Exit, GetTimeStamp and
 * RebaseTimeStamp from several threads at once (meant for TSan as well).
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int IMP_System_Init(void);
int IMP_System_Exit(void);
int64_t IMP_System_GetTimeStamp(void);
int IMP_System_RebaseTimeStamp(int64_t timestamp);

/* Helix JPEG / shared bitstream buffer, called from Init/Exit on T21 */
int OpenIMP_HelixBitstream_Init(void) { return 0; }
void OpenIMP_HelixBitstream_Exit(void) {}
void OpenIMP_HelixJpeg_Exit(void) {}

static int failures;

#define CHECK(cond, what) do {                                          \
        if (!(cond)) {                                                  \
            failures++;                                                 \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, what); \
        }                                                               \
    } while (0)

static int stop;

static void *stamper(void *arg)
{
    (void)arg;
    /* the value jumps with every Init/Rebase of the other threads; this
     * only exercises the shared base */
    while (!__atomic_load_n(&stop, __ATOMIC_RELAXED))
        (void)IMP_System_GetTimeStamp();
    return NULL;
}

static void *cycler(void *arg)
{
    int i;

    (void)arg;
    for (i = 0; i < 2000; i++) {
        IMP_System_Init();
        IMP_System_RebaseTimeStamp(1000);
        IMP_System_Exit();
    }
    return NULL;
}

int main(void)
{
    pthread_t threads[4];
    int i;

    CHECK(IMP_System_Init() == 0, "Init");
    CHECK(IMP_System_Init() == 0, "second Init");
    CHECK(IMP_System_RebaseTimeStamp(5000000) == 0, "Rebase");
    CHECK(IMP_System_GetTimeStamp() >= 5000000, "timestamp after Rebase");
    CHECK(IMP_System_Exit() == 0, "Exit");
    CHECK(IMP_System_Exit() == 0, "second Exit");
    for (i = 0; i < 10; i++)
        CHECK(IMP_System_Init() == 0 && IMP_System_Exit() == 0, "cycle");

    pthread_create(&threads[0], NULL, stamper, NULL);
    pthread_create(&threads[1], NULL, stamper, NULL);
    pthread_create(&threads[2], NULL, cycler, NULL);
    pthread_create(&threads[3], NULL, cycler, NULL);
    pthread_join(threads[2], NULL);
    pthread_join(threads[3], NULL);
    __atomic_store_n(&stop, 1, __ATOMIC_RELAXED);
    pthread_join(threads[0], NULL);
    pthread_join(threads[1], NULL);
    if (failures) {
        fprintf(stderr, "System: %d check(s) failed\n", failures);
        return 1;
    }
    printf("System Init/Exit tests passed\n");
    return 0;
}
