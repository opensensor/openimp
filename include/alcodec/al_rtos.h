#ifndef OPENIMP_ALCODEC_AL_RTOS_H
#define OPENIMP_ALCODEC_AL_RTOS_H

#include <stddef.h>
#include <stdint.h>

void *Rtos_Malloc(size_t size);
void Rtos_Free(void *ptr);
void *Rtos_Memcpy(void *dest, const void *src, size_t n);
void *Rtos_Memmove(void *dest, const void *src, size_t n);
void *Rtos_Memset(void *str, int32_t c, size_t n);
int32_t Rtos_Memcmp(const void *s1, const void *s2, size_t n);
int32_t Rtos_GetTime(void);
int32_t Rtos_Sleep(int32_t ms);
void *Rtos_CreateMutex(void);
void Rtos_DeleteMutex(void *mutex);
uint32_t Rtos_GetMutex(void *mutex);
uint32_t Rtos_ReleaseMutex(void *mutex);
void *Rtos_CreateSemaphore(int32_t initial_count);
void Rtos_DeleteSemaphore(void *sem);
int32_t Rtos_GetSemaphore(void *sem, int32_t timeout_ms);
int32_t Rtos_ReleaseSemaphore(void *sem);
void *Rtos_CreateEvent(char signaled);
int32_t Rtos_DeleteEvent(void *event);
int32_t Rtos_WaitEvent(void *event, int32_t timeout_ms);
int32_t Rtos_SetEvent(void *event);
void *Rtos_CreateThread(void *entry, void *arg);
int32_t Rtos_SetCurrentThreadName(int32_t name);
int32_t Rtos_JoinThread(int32_t *thread);
void Rtos_DeleteThread(void *ptr);
int32_t Rtos_DriverOpen(int32_t path);
int32_t Rtos_DriverClose(void);
int32_t Rtos_DriverIoctl(void);
int32_t Rtos_DriverPoll(int32_t fd, int32_t timeout_ms, int32_t events);
int32_t Rtos_AtomicIncrement(int32_t *value);
int32_t Rtos_AtomicDecrement(int32_t *value);
int32_t Rtos_InvalidateCacheMemory(int32_t addr, int32_t size);
int32_t Rtos_FlushCacheMemory(int32_t addr, int32_t size);

#endif
