#ifndef OPENIMP_ALCODEC_AL_ALLOCATOR_H
#define OPENIMP_ALCODEC_AL_ALLOCATOR_H

#include "al_types.h"

struct AL_TAllocatorVtable {
    void *(*Alloc)(AL_TAllocator *allocator, int32_t size);
    void (*Free)(AL_TAllocator *allocator, void *buffer);
    intptr_t (*GetVirtualAddr)(AL_TAllocator *allocator, void *buffer);
    intptr_t (*GetPhysicalAddr)(AL_TAllocator *allocator, void *buffer);
    void *(*AllocNamed)(AL_TAllocator *allocator, int32_t size, int32_t name);
    void *(*AllocNamedEmpty)(AL_TAllocator *allocator, int32_t size, char *name);
    int32_t (*FreeEmpty)(AL_TAllocator *allocator, int32_t handle);
    int32_t (*SetExtraMemory)(AL_TAllocator *allocator, void *buffer, int32_t a3, int32_t a4, int32_t a5);
};

AL_TAllocator *AL_GetDefaultAllocator(void);
AL_TAllocator *AL_GetWrapperAllocator(void);
void *AL_WrapperAllocator_WrapData(int32_t data, int32_t physical_addr, int32_t size);

#endif
