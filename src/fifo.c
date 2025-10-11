/**
 * Fifo Implementation
 * Based on reverse engineering of libimp.so v1.1.6
 * Decompiled from addresses 0x7af28 (Init), 0x7b254 (Queue), 0x7b384 (Dequeue)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <time.h>

#define LOG_FIFO(fmt, ...) fprintf(stderr, "[Fifo] " fmt "\n", ##__VA_ARGS__)

/* Fifo structure - based on decompilation */
typedef struct {
    int max_elements;       /* 0x00: arg1[0] - max elements + 1 */
    int write_idx;          /* 0x04: arg1[1] - write index */
    int read_idx;           /* 0x08: arg1[2] - read index */
    void **buffer;          /* 0x0c: arg1[3] - buffer array */
    pthread_mutex_t mutex;  /* 0x10: arg1[4] - mutex */
    pthread_cond_t cond;    /* 0x14: arg1[5] - condition variable (event) */
    int count;              /* 0x18: arg1[6] - current count */
    uint8_t abort_flag;     /* 0x1c: arg1[7].b - abort flag */
    sem_t semaphore;        /* 0x20: arg1[8] - semaphore */
} Fifo;

/* Public: report control structure size so callers can allocate correctly */
int Fifo_SizeOf(void) { return (int)sizeof(Fifo); }

/**
 * Fifo_Init - based on decompilation at 0x7af28
 * Initializes a FIFO queue with the specified capacity
 */
void Fifo_Init(void *fifo_ptr, int size) {
    if (fifo_ptr == NULL || size <= 0) {
        LOG_FIFO("Init failed: invalid parameters");
        return;
    }

    Fifo *fifo = (Fifo*)fifo_ptr;

    /* Initialize structure */
    fifo->max_elements = size + 1;  /* +1 for circular buffer */
    fifo->write_idx = 0;
    fifo->read_idx = 0;
    fifo->count = 0;
    fifo->abort_flag = 0;

    /* Allocate buffer array */
    int buffer_size = (size + 1) * sizeof(void*);
    fifo->buffer = (void**)malloc(buffer_size);

    if (fifo->buffer == NULL) {
        LOG_FIFO("Init failed: malloc failed");
        return;
    }

    /* Initialize buffer with NULL pointers (safer than 0xcd pattern) */
    memset(fifo->buffer, 0, buffer_size);

    /* Create condition variable (event) */
    if (pthread_cond_init(&fifo->cond, NULL) != 0) {
        LOG_FIFO("Init failed: cond_init failed");
        free(fifo->buffer);
        fifo->buffer = NULL;
        return;
    }

    /* Create semaphore with initial count = size */
    if (sem_init(&fifo->semaphore, 0, size) != 0) {
        LOG_FIFO("Init failed: sem_init failed");
        pthread_cond_destroy(&fifo->cond);
        free(fifo->buffer);
        fifo->buffer = NULL;
        return;
    }

    /* Create mutex */
    if (pthread_mutex_init(&fifo->mutex, NULL) != 0) {
        LOG_FIFO("Init failed: mutex_init failed");
        sem_destroy(&fifo->semaphore);
        pthread_cond_destroy(&fifo->cond);
        free(fifo->buffer);
        fifo->buffer = NULL;
        return;
    }

    LOG_FIFO("Init: size=%d, max_elements=%d", size, fifo->max_elements);
}

/**
 * Fifo_Deinit - based on decompilation at 0x7b054
 * Cleans up FIFO resources
 */
void Fifo_Deinit(void *fifo_ptr) {
    if (fifo_ptr == NULL) {
        return;
    }

    Fifo *fifo = (Fifo*)fifo_ptr;

    /* Set abort flag */
    fifo->abort_flag = 1;

    /* Wake up any waiting threads */
    pthread_cond_broadcast(&fifo->cond);

    /* Destroy synchronization primitives */
    pthread_mutex_destroy(&fifo->mutex);
    pthread_cond_destroy(&fifo->cond);
    sem_destroy(&fifo->semaphore);

    /* Free buffer */
    if (fifo->buffer != NULL) {
        free(fifo->buffer);
        fifo->buffer = NULL;
    }

    LOG_FIFO("Deinit: completed");
}

/**
 * Fifo_Queue - based on decompilation at 0x7b254
 * Adds an item to the FIFO queue
 * Returns 1 on success, 0 on failure
 */
int Fifo_Queue(void *fifo_ptr, void *item, int timeout_ms) {
    if (fifo_ptr == NULL) {
        return 0;
    }

    Fifo *fifo = (Fifo*)fifo_ptr;

    /* Wait for semaphore (space available) */
    int ret;
    if (timeout_ms < 0) {
        /* Infinite wait */
        ret = sem_wait(&fifo->semaphore);
    } else if (timeout_ms == 0) {
        /* Try without waiting */
        ret = sem_trywait(&fifo->semaphore);
    } else {
        /* Timed wait */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout_ms / 1000;
        ts.tv_nsec += (timeout_ms % 1000) * 1000000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
        ret = sem_timedwait(&fifo->semaphore, &ts);
    }

    if (ret != 0) {
        return 0;  /* Timeout or error */
    }

    /* Lock mutex */
    pthread_mutex_lock(&fifo->mutex);

    /* Add item to buffer */
    fifo->buffer[fifo->write_idx] = item;

    /* Update count and write index */
    fifo->count++;
    fifo->write_idx = (fifo->write_idx + 1) % fifo->max_elements;

    /* Signal condition variable (item available) */
    pthread_cond_signal(&fifo->cond);

    /* Unlock mutex */
    pthread_mutex_unlock(&fifo->mutex);

    return 1;  /* Success */
}

/**
 * Fifo_Dequeue - based on decompilation at 0x7b384
 * Removes and returns an item from the FIFO queue
 * Returns item pointer on success, NULL on failure/timeout
 */
void *Fifo_Dequeue(void *fifo_ptr, int timeout_ms) {
    if (fifo_ptr == NULL) {
        return NULL;
    }

    Fifo *fifo = (Fifo*)fifo_ptr;

    /* Lock mutex */
    pthread_mutex_lock(&fifo->mutex);

    /* Wait for item to be available */
    while (fifo->count <= 0) {
        /* Check abort flag */
        if (fifo->abort_flag) {
            pthread_mutex_unlock(&fifo->mutex);
            return NULL;
        }

        /* Wait on condition variable */
        int ret;
        if (timeout_ms < 0) {
            /* Infinite wait */
            ret = pthread_cond_wait(&fifo->cond, &fifo->mutex);
        } else if (timeout_ms == 0) {
            /* No wait */
            pthread_mutex_unlock(&fifo->mutex);
            return NULL;
        } else {
            /* Timed wait */
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += timeout_ms / 1000;
            ts.tv_nsec += (timeout_ms % 1000) * 1000000;
            if (ts.tv_nsec >= 1000000000) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000;
            }
            ret = pthread_cond_timedwait(&fifo->cond, &fifo->mutex, &ts);
        }

        if (ret != 0) {
            /* Timeout or error */
            pthread_mutex_unlock(&fifo->mutex);
            return NULL;
        }

        /* Check count again after waking up */
        if (fifo->count <= 0) {
            continue;
        }

        break;
    }

    /* Get item from buffer */
    void *item = fifo->buffer[fifo->read_idx];

    /* Update count and read index */
    fifo->count--;
    fifo->read_idx = (fifo->read_idx + 1) % fifo->max_elements;

    /* Unlock mutex */
    pthread_mutex_unlock(&fifo->mutex);

    /* Release semaphore (space now available) */
    sem_post(&fifo->semaphore);

    /* Validate item pointer before returning */
    if (item != NULL) {
        uintptr_t item_addr = (uintptr_t)item;
        if (item_addr < 0x10000) {
            LOG_FIFO("Dequeue: invalid item pointer %p (too small), returning NULL", item);
            return NULL;
        }
    }

    return item;
}

/**
 * Fifo_GetMaxElements - based on decompilation at 0x7b52c
 * Returns the maximum capacity of the FIFO
 */
int Fifo_GetMaxElements(void *fifo_ptr) {
    if (fifo_ptr == NULL) {
        return 0;
    }

    Fifo *fifo = (Fifo*)fifo_ptr;
    return fifo->max_elements - 1;  /* -1 because we store max+1 */
}

