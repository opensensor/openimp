/*
 * openimp_ready_event.h -- "something was published" event for a pull
 * reader that must not poll.
 *
 * The producer publishes its item first and then calls
 * openimp_ready_event_notify(), which advances a sequence and wakes every
 * waiter. A reader takes openimp_ready_event_sequence() *before* it looks
 * for an item and, when it found none, sleeps in openimp_ready_event_wait()
 * until the sequence moved or the timeout passed. An item published
 * between the reader's look and its wait has already moved the sequence,
 * so the wait returns at once: no wake-up can be lost.
 *
 * Header-only (static inline) so the host tests exercise exactly this code.
 */
#ifndef OPENIMP_READY_EVENT_H
#define OPENIMP_READY_EVENT_H

#include <pthread.h>
#include <stdint.h>
#include <time.h>

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    unsigned int sequence;
} OpenIMPReadyEvent;

/* CLOCK_MONOTONIC waits: a wall-clock step must not stretch a timeout. */
static inline void openimp_ready_event_init(OpenIMPReadyEvent *event)
{
    pthread_condattr_t attr;

    pthread_mutex_init(&event->lock, NULL);
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&event->cond, &attr);
    pthread_condattr_destroy(&attr);
    event->sequence = 0u;
}

static inline unsigned int
openimp_ready_event_sequence(OpenIMPReadyEvent *event)
{
    return __atomic_load_n(&event->sequence, __ATOMIC_ACQUIRE);
}

static inline void openimp_ready_event_notify(OpenIMPReadyEvent *event)
{
    pthread_mutex_lock(&event->lock);
    __atomic_store_n(&event->sequence, event->sequence + 1u,
                     __ATOMIC_RELEASE);
    pthread_cond_broadcast(&event->cond);
    pthread_mutex_unlock(&event->lock);
}

/* Returns 0 once the sequence differs from `sequence`, -1 on timeout. */
static inline int openimp_ready_event_wait(OpenIMPReadyEvent *event,
                                           unsigned int sequence,
                                           uint32_t timeout_us)
{
    struct timespec until;
    int moved;

    clock_gettime(CLOCK_MONOTONIC, &until);
    until.tv_sec += (time_t)(timeout_us / 1000000u);
    until.tv_nsec += (long)(timeout_us % 1000000u) * 1000L;
    if (until.tv_nsec >= 1000000000L) {
        until.tv_sec++;
        until.tv_nsec -= 1000000000L;
    }
    pthread_mutex_lock(&event->lock);
    while (event->sequence == sequence &&
           pthread_cond_timedwait(&event->cond, &event->lock, &until) == 0)
        ;
    moved = event->sequence != sequence;
    pthread_mutex_unlock(&event->lock);
    return moved ? 0 : -1;
}

#endif /* OPENIMP_READY_EVENT_H */
