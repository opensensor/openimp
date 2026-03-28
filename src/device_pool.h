/**
 * Device Pool - Reference-counted device file descriptor pool
 * Based on AL_DevicePool_Open decompilation at 0x362dc
 */

#ifndef DEVICE_POOL_H
#define DEVICE_POOL_H

#include <errno.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Open a device with reference counting
 * If the device is already open, returns the existing fd and increments refcount
 * Otherwise opens the device and stores it in the pool
 * 
 * @param device_path Path to device (e.g., "/dev/avpu")
 * @return File descriptor on success, -1 on failure
 */
int AL_DevicePool_Open(const char *device_path);

/**
 * Close a device (decrement refcount)
 * When refcount reaches 0, the fd is actually closed
 * 
 * @param fd File descriptor to close
 * @return 0 on success, -1 on failure
 */
int AL_DevicePool_Close(int fd);

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_POOL_H */

