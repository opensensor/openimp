/* Host test for the frame-ready event (src/openimp_ready_event.h) that
 * replaced the encoder's 1 ms capture-frame poll on T31. */
#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "openimp_ready_event.h"

#define ITEMS 20000u

static OpenIMPReadyEvent event;
static pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned int queued;
static unsigned int consumed;

static uint64_t now_us(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

/* The reader's non-blocking pull, like VBMGetFrame. */
static int try_pop(void)
{
    int got = 0;

    pthread_mutex_lock(&queue_lock);
    if (queued) {
        queued--;
        got = 1;
    }
    pthread_mutex_unlock(&queue_lock);
    return got;
}

static void *producer(void *arg)
{
    unsigned int i;

    (void)arg;
    for (i = 0; i < ITEMS; i++) {
        /* Publish first, then notify, as VBMKernelDequeue does. */
        pthread_mutex_lock(&queue_lock);
        queued++;
        pthread_mutex_unlock(&queue_lock);
        openimp_ready_event_notify(&event);
        if ((i & 63u) == 0u) {
            struct timespec pause = { 0, 50000 };

            nanosleep(&pause, NULL);
        }
    }
    return NULL;
}

int main(void)
{
    pthread_t thread;
    unsigned int sequence;
    unsigned int timeouts = 0;
    uint64_t start;

    openimp_ready_event_init(&event);

    /* Nothing published: the wait times out after (about) its timeout. */
    sequence = openimp_ready_event_sequence(&event);
    start = now_us();
    assert(openimp_ready_event_wait(&event, sequence, 20000u) == -1);
    assert(now_us() - start >= 19000u);

    /* Published after the sequence was taken: no sleep at all. */
    sequence = openimp_ready_event_sequence(&event);
    openimp_ready_event_notify(&event);
    start = now_us();
    assert(openimp_ready_event_wait(&event, sequence, 1000000u) == 0);
    assert(now_us() - start < 100000u);
    assert(openimp_ready_event_sequence(&event) == sequence + 1u);

    /* Producer/consumer race: every item arrives and no wait has to run
     * into its timeout (a lost wake-up would show as a 1 s timeout). */
    assert(pthread_create(&thread, NULL, producer, NULL) == 0);
    while (consumed < ITEMS) {
        sequence = openimp_ready_event_sequence(&event);
        if (try_pop()) {
            consumed++;
            continue;
        }
        if (openimp_ready_event_wait(&event, sequence, 1000000u) != 0)
            timeouts++;
    }
    assert(pthread_join(thread, NULL) == 0);
    assert(consumed == ITEMS);
    assert(timeouts == 0u);
    assert(!try_pop());

    puts("T31 ready-event tests passed");
    return 0;
}
