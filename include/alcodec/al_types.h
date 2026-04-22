#ifndef OPENIMP_ALCODEC_AL_TYPES_H
#define OPENIMP_ALCODEC_AL_TYPES_H

#include <stdint.h>

#define AL_MAX_CORES 4

typedef struct AL_TBuffer AL_TBuffer;
typedef struct AL_TEncSettings AL_TEncSettings;
typedef struct AL_TEncChanParam AL_TEncChanParam;
typedef struct AL_TGopParam AL_TGopParam;
typedef struct AL_TRCParam AL_TRCParam;
typedef struct AL_TBufPool AL_TBufPool;
typedef struct AL_TStreamMngrCtx AL_TStreamMngrCtx;
typedef struct AL_TScheduler AL_TScheduler;
typedef struct AL_TEncChannel AL_TEncChannel;

typedef struct AL_TAllocatorVtable AL_TAllocatorVtable;
typedef struct AL_TAllocator {
    const AL_TAllocatorVtable *vtable;
} AL_TAllocator;

typedef struct AL_THardwareDriverVtable AL_THardwareDriverVtable;
typedef struct AL_THardwareDriver {
    const AL_THardwareDriverVtable *vtable;
} AL_THardwareDriver;

typedef struct AL_TLogger {
    void *writer;
    void *entries;
    int32_t count;
    int32_t capacity;
    void *mutex;
} AL_TLogger;

#endif
