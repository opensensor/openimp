/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef OPENIMP_CORE_LEASE_H
#define OPENIMP_CORE_LEASE_H

#include <errno.h>
#include <pthread.h>
#include <time.h>

/* A command and its IRQ completion can run on different threads. The mutex
 * protects the owner token, not the lifetime of the command; no thread ever
 * unlocks a mutex locked by another thread. A timeout never revokes a lease. */
typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t changed;
    const void *owner;
} OpenIMPCoreLease;

#define OPENIMP_CORE_LEASE_INITIALIZER \
    { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, NULL }

static inline int openimp_core_acquire(OpenIMPCoreLease *core,
        const void *owner, unsigned int timeout_ms)
{
    struct timespec deadline;
    int ret = 0;

    if (!owner)
        return -EINVAL;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += timeout_ms / 1000u;
    deadline.tv_nsec += (long)(timeout_ms % 1000u) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }
    pthread_mutex_lock(&core->lock);
    if (core->owner == owner) {
        ret = -EBUSY;
        goto out;
    }
    while (core->owner) {
        if (!timeout_ms) {
            ret = -EAGAIN;
            goto out;
        }
        ret = pthread_cond_timedwait(&core->changed, &core->lock, &deadline);
        if (ret) {
            ret = -ret;
            goto out;
        }
    }
    core->owner = owner;
out:
    pthread_mutex_unlock(&core->lock);
    return ret;
}

static inline void openimp_core_release(OpenIMPCoreLease *core, const void *owner)
{
    pthread_mutex_lock(&core->lock);
    if (owner && core->owner == owner) {
        core->owner = NULL;
        pthread_cond_broadcast(&core->changed);
    }
    pthread_mutex_unlock(&core->lock);
}

#endif
