/**
 * Device Pool - Reference-counted device file descriptor pool
 * Based on AL_DevicePool_Open decompilation at 0x362dc
 */

#include "device_pool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#define LOG_DEVPOOL(fmt, ...) fprintf(stderr, "[DevicePool] " fmt "\n", ##__VA_ARGS__)

#define MAX_DEVICES 32  /* 0x20 in decompilation */

typedef struct {
    char *name;        /* Device path (strdup'd) */
    int refcount;      /* Reference count */
    int fd;            /* File descriptor */
} DevicePoolEntry;

static DevicePoolEntry g_device_pool[MAX_DEVICES];
static pthread_mutex_t g_device_pool_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_device_pool_init = 0;

/**
 * Cleanup function called at exit
 */
static void AL_DevicePool_Deinit(void) {
    pthread_mutex_lock(&g_device_pool_mutex);
    
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (g_device_pool[i].name != NULL) {
            if (g_device_pool[i].fd >= 0) {
                close(g_device_pool[i].fd);
            }
            free(g_device_pool[i].name);
            g_device_pool[i].name = NULL;
            g_device_pool[i].fd = -1;
            g_device_pool[i].refcount = 0;
        }
    }
    
    g_device_pool_init = 0;
    pthread_mutex_unlock(&g_device_pool_mutex);
    
    LOG_DEVPOOL("Deinit: device pool cleaned up");
}

/**
 * AL_DevicePool_Open - Open a device with reference counting
 * Based on decompilation at 0x362dc
 * 
 * @param device_path Path to device (e.g., "/dev/avpu")
 * @return File descriptor on success, -1 on failure
 */
int AL_DevicePool_Open(const char *device_path) {
    if (device_path == NULL) {
        return -1;
    }
    
    /* Initialize pool on first use */
    if (g_device_pool_init == 0) {
        pthread_mutex_lock(&g_device_pool_mutex);
        if (g_device_pool_init == 0) {
            /* Initialize all entries */
            for (int i = 0; i < MAX_DEVICES; i++) {
                g_device_pool[i].name = NULL;
                g_device_pool[i].fd = -1;
                g_device_pool[i].refcount = 0;
            }
            
            /* Register cleanup handler */
            atexit(AL_DevicePool_Deinit);
            
            g_device_pool_init = 1;
            LOG_DEVPOOL("Init: device pool initialized");
        }
        pthread_mutex_unlock(&g_device_pool_mutex);
    }
    
    pthread_mutex_lock(&g_device_pool_mutex);
    
    int result = -1;
    
    /* First pass: check if device is already open */
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (g_device_pool[i].refcount > 0 && 
            g_device_pool[i].name != NULL &&
            strcmp(device_path, g_device_pool[i].name) == 0) {
            /* Found existing entry - increment refcount and return fd */
            g_device_pool[i].refcount++;
            result = g_device_pool[i].fd;
            LOG_DEVPOOL("Open: '%s' already open (fd=%d, refcount=%d)", 
                       device_path, result, g_device_pool[i].refcount);
            goto done;
        }
    }
    
    /* Second pass: find empty slot and open device */
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (g_device_pool[i].refcount == 0) {
            /* Empty slot - open device */
            int fd = open(device_path, O_RDWR);
            if (fd < 0) {
                LOG_DEVPOOL("Open: failed to open '%s': %s", device_path, strerror(errno));
                result = -1;
                goto done;
            }
            
            /* Store in pool */
            g_device_pool[i].name = strdup(device_path);
            g_device_pool[i].fd = fd;
            g_device_pool[i].refcount = 1;
            
            result = fd;
            LOG_DEVPOOL("Open: opened '%s' (fd=%d, slot=%d)", device_path, fd, i);
            goto done;
        }
    }
    
    /* No empty slots */
    LOG_DEVPOOL("Open: device pool full (max=%d)", MAX_DEVICES);
    result = -1;
    
done:
    pthread_mutex_unlock(&g_device_pool_mutex);
    return result;
}

/**
 * AL_DevicePool_Close - Close a device (decrement refcount)
 * 
 * @param fd File descriptor to close
 * @return 0 on success, -1 on failure
 */
int AL_DevicePool_Close(int fd) {
    if (fd < 0) {
        return -1;
    }
    
    pthread_mutex_lock(&g_device_pool_mutex);
    
    int result = -1;
    
    /* Find the entry with this fd */
    for (int i = 0; i < MAX_DEVICES; i++) {
        if (g_device_pool[i].fd == fd && g_device_pool[i].refcount > 0) {
            g_device_pool[i].refcount--;
            
            if (g_device_pool[i].refcount == 0) {
                /* Last reference - close fd and free name */
                close(g_device_pool[i].fd);
                free(g_device_pool[i].name);
                g_device_pool[i].name = NULL;
                g_device_pool[i].fd = -1;
                
                LOG_DEVPOOL("Close: closed fd=%d (refcount=0)", fd);
            } else {
                LOG_DEVPOOL("Close: fd=%d (refcount=%d)", fd, g_device_pool[i].refcount);
            }
            
            result = 0;
            break;
        }
    }
    
    if (result < 0) {
        LOG_DEVPOOL("Close: fd=%d not found in pool", fd);
    }
    
    pthread_mutex_unlock(&g_device_pool_mutex);
    return result;
}

