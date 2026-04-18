#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "alcodec/al_rtos.h"

extern void __assert(const char *expr, const char *file, int32_t line, const char *func);

/* forward decl, ported by T<N> later */
int32_t AL_DmaAlloc_FlushCache(int32_t addr, int32_t size, int32_t dir);

int32_t Rtos_GetTime(void)
{
    struct timespec ts;
    int32_t v1_2;
    int32_t result;

    clock_gettime(1, &ts);
    v1_2 = (int32_t)ts.tv_nsec / 0x3e8;
    result = (int32_t)((int64_t)v1_2 + ((int64_t)(int32_t)ts.tv_sec * 0x3e8));
    return result;
}

int32_t Rtos_Sleep(int32_t ms)
{
    return usleep((useconds_t)(ms * 0x3e8));
}

void *Rtos_CreateMutex(void)
{
    void *result;
    pthread_mutexattr_t attr;

    result = Rtos_Malloc(0x18);
    if (result == NULL) {
        __assert("pMutex", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_rtos/lib_rtos.c",
            0x11d, "Rtos_CreateMutex");
        Rtos_DeleteMutex(NULL);
        return NULL;
    }

    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, 1);
    pthread_mutex_init((pthread_mutex_t *)result, &attr);
    return result;
}

void Rtos_DeleteMutex(void *mutex)
{
    if (mutex == NULL) {
        return;
    }

    pthread_mutex_destroy((pthread_mutex_t *)mutex);
    Rtos_Free(mutex);
}

uint32_t Rtos_GetMutex(void *mutex)
{
    if (mutex == NULL) {
        __assert("pMutex", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_rtos/lib_rtos.c",
            0x139, "Rtos_GetMutex");
        Rtos_ReleaseMutex(NULL);
        return 0;
    }

    return pthread_mutex_lock((pthread_mutex_t *)mutex) == 0 ? 1U : 0U;
}

uint32_t Rtos_ReleaseMutex(void *mutex)
{
    if (mutex == NULL) {
        __assert("Mutex", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_rtos/lib_rtos.c",
            0x147, "Rtos_ReleaseMutex");
        Rtos_CreateSemaphore(0);
        return 0;
    }

    return pthread_mutex_unlock((pthread_mutex_t *)mutex) == 0 ? 1U : 0U;
}

void *Rtos_CreateSemaphore(int32_t initial_count)
{
    void *result;

    result = Rtos_Malloc(0x10);
    if (result != NULL) {
        sem_init((sem_t *)result, 0, (unsigned int)initial_count);
    }
    return result;
}

void Rtos_DeleteSemaphore(void *sem)
{
    if (sem == NULL) {
        return;
    }

    sem_destroy((sem_t *)sem);
    Rtos_Free(sem);
}

int32_t Rtos_GetSemaphore(void *sem, int32_t timeout_ms)
{
    int32_t ret;

    if (sem == NULL) {
        return 0;
    }

    if (timeout_ms == 0) {
        while (1) {
            ret = sem_trywait((sem_t *)sem);
            if (ret != -1) {
                return ret < 1 ? 1 : 0;
            }

            if (*__errno_location() != 4) {
                return 0;
            }
        }
    }

    if (timeout_ms == -1) {
        int32_t err;

        do {
            ret = sem_wait((sem_t *)sem);
            if (ret != -1) {
                return ret < 1 ? 1 : 0;
            }
            err = *__errno_location();
        } while (err == 4);

        return 0;
    }

    {
        struct timespec timeout;

        timeout.tv_sec = (uint32_t)timeout_ms / 0x3e8;
        timeout.tv_nsec = ((uint32_t)timeout_ms % 0x3e8) * 0xf4240;

        while (1) {
            ret = sem_timedwait((sem_t *)sem, &timeout);
            if (ret != -1) {
                return ret < 1 ? 1 : 0;
            }

            if (*__errno_location() != 4) {
                return 0;
            }
        }
    }
}

int32_t Rtos_ReleaseSemaphore(void *sem)
{
    if (sem == NULL) {
        return 0;
    }

    sem_post((sem_t *)sem);
    return 1;
}

void *Rtos_CreateEvent(char signaled)
{
    char *result;

    result = (char *)Rtos_Malloc(0x50);
    if (result != NULL) {
        pthread_mutex_init((pthread_mutex_t *)result, NULL);
        pthread_cond_init((pthread_cond_t *)(result + 0x18), NULL);
        /* +0x48: event signaled flag */
        *(result + 0x48) = signaled;
    }
    return result;
}

int32_t Rtos_DeleteEvent(void *event)
{
    if (event != NULL) {
        pthread_cond_destroy((pthread_cond_t *)((char *)event + 0x18));
        pthread_mutex_destroy((pthread_mutex_t *)event);
        Rtos_Free(event);
    }

    return 0;
}

int32_t Rtos_WaitEvent(void *event, int32_t timeout_ms)
{
    int32_t result;

    if (event == NULL) {
        return 0;
    }

    pthread_mutex_lock((pthread_mutex_t *)event);
    if (timeout_ms != -1) {
        struct timeval tv;
        struct timespec ts;
        int32_t i;

        gettimeofday(&tv, NULL);
        ts.tv_nsec = (((uint32_t)timeout_ms % 0x3e8) * 0x3e8 + (int32_t)tv.tv_usec) * 0x3e8;
        ts.tv_sec = (int32_t)tv.tv_sec + ((uint32_t)timeout_ms / 0x3e8);

        do {
            /* +0x48: event signaled flag */
            if (*(uint8_t *)((char *)event + 0x48) != 0) {
                goto label_1;
            }

            i = pthread_cond_timedwait((pthread_cond_t *)((char *)event + 0x18),
                (pthread_mutex_t *)event, &ts);
        } while (i == 0);

        result = 0;
    } else if (*(uint8_t *)((char *)event + 0x48) != 0) {
label_1:
        /* +0x48: event signaled flag */
        *(char *)((char *)event + 0x48) = 0;
        result = 1;
    } else {
        while (pthread_cond_wait((pthread_cond_t *)((char *)event + 0x18),
                   (pthread_mutex_t *)event) == 0) {
            /* +0x48: event signaled flag */
            if (*(uint8_t *)((char *)event + 0x48) != 0) {
                goto label_1;
            }
        }

        result = 0;
    }

    pthread_mutex_unlock((pthread_mutex_t *)event);
    return result;
}

int32_t Rtos_SetEvent(void *event)
{
    int32_t ret;

    pthread_mutex_lock((pthread_mutex_t *)event);
    ret = pthread_cond_signal((pthread_cond_t *)((char *)event + 0x18));
    /* +0x48: event signaled flag */
    *(char *)((char *)event + 0x48) = 1;
    pthread_mutex_unlock((pthread_mutex_t *)event);
    return ret < 1 ? 1 : 0;
}

void *Rtos_CreateThread(void *entry, void *arg)
{
    void *result;

    result = Rtos_Malloc(4);
    if (result != NULL) {
        pthread_create((pthread_t *)result, NULL, (void *(*)(void *))entry, arg);
    }
    return result;
}

int32_t Rtos_SetCurrentThreadName(int32_t name)
{
    return prctl(0xf, (unsigned long)(uintptr_t)name, 0, 0, 0);
}

int32_t Rtos_JoinThread(int32_t *thread)
{
    return pthread_join((pthread_t)*thread, NULL) < 1 ? 1 : 0;
}

int32_t Rtos_DriverOpen(int32_t path)
{
    int32_t result;

    result = open((const char *)(uintptr_t)path, 2);
    if (result == -1) {
        return 0;
    }
    return result;
}

int32_t Rtos_DriverPoll(int32_t fd, int32_t timeout_ms, int32_t events)
{
    struct pollfd pfd;
    int16_t requested_events;

    requested_events = (events & 1) != 0 ? 2 : 0;
    if ((events & 2) != 0) {
        requested_events |= 1;
    }
    if ((events & 4) != 0) {
        requested_events |= 4;
    }
    if ((events & 8) != 0) {
        requested_events = requested_events | 8;
    }

    pfd.fd = fd;
    pfd.events = requested_events;
    pfd.revents = 0;
    return poll(&pfd, 1, timeout_ms);
}

int32_t Rtos_AtomicIncrement(int32_t *value)
{
    int32_t old_value;
    int success;

    __sync_synchronize();
    do {
        old_value = *value;
        success = __sync_bool_compare_and_swap(value, old_value, old_value + 1);
    } while (success == 0);
    __sync_synchronize();
    return old_value + 1;
}

int32_t Rtos_AtomicDecrement(int32_t *value)
{
    int32_t old_value;
    int success;

    __sync_synchronize();
    do {
        old_value = *value;
        success = __sync_bool_compare_and_swap(value, old_value, old_value - 1);
    } while (success == 0);
    __sync_synchronize();
    return old_value - 1;
}

int32_t Rtos_InvalidateCacheMemory(int32_t addr, int32_t size)
{
    return AL_DmaAlloc_FlushCache(addr, size, 2);
}

int32_t Rtos_FlushCacheMemory(int32_t addr, int32_t size)
{
    return AL_DmaAlloc_FlushCache(addr, size, 1);
}
