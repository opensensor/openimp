#ifndef OPENIMP_CORE_GLOBALS_H
#define OPENIMP_CORE_GLOBALS_H

#include <stdint.h>
#include "core/module.h"
#include "alcodec/al_allocator.h"
#include "alcodec/al_logger.h"
#include "alcodec/al_types.h"

#define IMP_MAX_GROUPS 6

typedef struct EncoderState EncoderState;
typedef struct FrameSourceState FrameSourceState;
typedef struct ISPDevice ISPDevice;
typedef struct IVSState IVSState;
typedef struct OSDState OSDState;
typedef struct AudioState AudioState;
typedef struct AencState AencState;
typedef struct AdecState AdecState;
typedef struct DmicState DmicState;
typedef struct CodecState CodecState;
typedef struct VbmInstance VbmInstance;
typedef struct MemPoolNode MemPoolNode;

typedef struct SysFuncEntry {
    char name[0x20];
    int32_t (*init)(void);
    void (*exit)(void);
    uint32_t reserved[2];
} SysFuncEntry;

extern EncoderState *gEncoder;
extern FrameSourceState *gFrameSource;
extern ISPDevice *gISP;
extern IVSState *gIVS;
extern OSDState *gOSD;
extern AudioState *gAudio;
extern AudioState *gAi;
extern AudioState *gAo;
extern AencState *gAenc;
extern AdecState *gAdec;
extern DmicState *gDmic;
extern Module *g_modules[6][IMP_MAX_GROUPS];
extern CodecState *g_pCodec;
extern SysFuncEntry sys_funcs[6];
extern uint64_t timestamp_base;
extern VbmInstance *g_pVbmInstance;
extern MemPoolNode *g_pMemPoolHead;
extern void *g_mempool_lock;
extern AL_TAllocator g_AllocatorDefault;
extern AL_TAllocator g_AllocatorWrapper;
extern AL_TLogger g_RtosLog;
extern AL_TLogger g_AL_Logger;
extern AL_THardwareDriver *g_pHardwareDriver;

/* Encoder trace-buffer dump toggle. Set by AL_Common_Encoder_SetTraces
 * (lib_encode/Com_Encoder.c, T44 wave) and read by AL_IntermMngr_GetBuffer
 * (lib_buf_mngt/IntermMngr.c, T32) + AL_CleanupMemory
 * (lib_common/AllocatorDefault.c, T10). Stock ELF exports this as a
 * single .bss global — keep as one definition in src/core/globals.c.
 */
extern uint32_t AL_CLEAN_BUFFERS;

#endif
