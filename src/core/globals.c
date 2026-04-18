#include "core/globals.h"

int32_t FrameSourceInit(void);
void FrameSourceExit(void);
int32_t IVSInit(void);
void IVSExit(void);
int32_t OSDInit(void);
void OSDExit(void);
int32_t EncoderInit(void);
void EncoderExit(void);
int32_t FBInit(void);
void FBExit(void);
int32_t DsystemInit(void);
void DsystemExit(void);

static const AL_TAllocatorVtable g_default_allocator_vtable = {0};
static const AL_TAllocatorVtable g_wrapper_allocator_vtable = {0};

EncoderState *gEncoder = 0;
FrameSourceState *gFrameSource = 0;
ISPDevice *gISP = 0;
IVSState *gIVS = 0;
OSDState *gOSD = 0;
AudioState *gAudio = 0;
AudioState *gAi = 0;
AudioState *gAo = 0;
AencState *gAenc = 0;
AdecState *gAdec = 0;
DmicState *gDmic = 0;
Module *g_modules[6][IMP_MAX_GROUPS] = {{0}};
CodecState *g_pCodec = 0;
SysFuncEntry sys_funcs[6] = {
    { "DSystem", DsystemInit, DsystemExit, {0, 0} },
    { "FrameSource", FrameSourceInit, FrameSourceExit, {0, 0} },
    { "IVS", IVSInit, IVSExit, {0, 0} },
    { "OSD", OSDInit, OSDExit, {0, 0} },
    { "Encoder", EncoderInit, EncoderExit, {0, 0} },
    { "FB", FBInit, FBExit, {0, 0} },
};
uint64_t timestamp_base = 0;
VbmInstance *g_pVbmInstance = 0;
MemPoolNode *g_pMemPoolHead = 0;
void *g_mempool_lock = 0;
AL_TAllocator g_AllocatorDefault = { &g_default_allocator_vtable };
AL_TAllocator g_AllocatorWrapper = { &g_wrapper_allocator_vtable };
AL_TLogger g_RtosLog = {0};
AL_TLogger g_AL_Logger = {0};
AL_THardwareDriver *g_pHardwareDriver = 0;
