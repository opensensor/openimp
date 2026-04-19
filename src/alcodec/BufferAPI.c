#include <assert.h>
#include <stdint.h>
#include <stddef.h>

#include "alcodec/al_allocator.h"
#include "alcodec/al_buffer.h"
#include "alcodec/al_rtos.h"

#define BUFFER_CHUNK_MAX 3

#define BUFFER_ALLOCATOR_OFFSET 0x00
#define BUFFER_CHUNK_COUNT_OFFSET 0x04
#define BUFFER_CHUNK_SIZE_OFFSET 0x08
#define BUFFER_CHUNK_HANDLE_OFFSET 0x14
#define BUFFER_CHUNK_MODE_OFFSET 0x20
#define BUFFER_CHUNK_ARG4_OFFSET 0x24
#define BUFFER_MUTEX_OFFSET 0x30
#define BUFFER_REFCOUNT_OFFSET 0x34
#define BUFFER_METADATA_LIST_OFFSET 0x38
#define BUFFER_METADATA_COUNT_OFFSET 0x3c
#define BUFFER_USER_DATA_OFFSET 0x40
#define BUFFER_DESTROY_CB_OFFSET 0x44

static inline uint32_t buffer_read_u32(const AL_TBuffer *buffer, uint32_t offset)
{
    return *(const uint32_t *)((const uint8_t *)buffer + offset);
}

static inline void buffer_write_u32(AL_TBuffer *buffer, uint32_t offset, uint32_t value)
{
    *(uint32_t *)((uint8_t *)buffer + offset) = value;
}

static inline uint8_t buffer_read_u8(const AL_TBuffer *buffer, uint32_t offset)
{
    return *(const uint8_t *)((const uint8_t *)buffer + offset);
}

static inline void buffer_write_u8(AL_TBuffer *buffer, uint32_t offset, uint8_t value)
{
    *(uint8_t *)((uint8_t *)buffer + offset) = value;
}

static inline void *buffer_read_ptr(const AL_TBuffer *buffer, uint32_t offset)
{
    return (void *)(uintptr_t)buffer_read_u32(buffer, offset);
}

static inline void buffer_write_ptr(AL_TBuffer *buffer, uint32_t offset, const void *value)
{
    buffer_write_u32(buffer, offset, (uint32_t)(uintptr_t)value);
}

static inline uint32_t buffer_chunk_offset(uint32_t base, int32_t chunk_idx)
{
    return base + ((uint32_t)chunk_idx << 2);
}

static inline AL_TAllocator *buffer_get_allocator(const AL_TBuffer *buffer)
{
    return (AL_TAllocator *)buffer_read_ptr(buffer, BUFFER_ALLOCATOR_OFFSET);
}

static inline int32_t buffer_get_chunk_count(const AL_TBuffer *buffer)
{
    return (int32_t)(int8_t)buffer_read_u8(buffer, BUFFER_CHUNK_COUNT_OFFSET);
}

static inline void buffer_set_chunk_count(AL_TBuffer *buffer, uint8_t chunk_count)
{
    buffer_write_u8(buffer, BUFFER_CHUNK_COUNT_OFFSET, chunk_count);
}

static inline void *buffer_get_chunk_handle(const AL_TBuffer *buffer, int32_t chunk_idx)
{
    return buffer_read_ptr(buffer, buffer_chunk_offset(BUFFER_CHUNK_HANDLE_OFFSET, chunk_idx));
}

static inline void buffer_set_chunk_handle(AL_TBuffer *buffer, int32_t chunk_idx, void *handle)
{
    buffer_write_ptr(buffer, buffer_chunk_offset(BUFFER_CHUNK_HANDLE_OFFSET, chunk_idx), handle);
}

static inline uint32_t buffer_get_chunk_size(const AL_TBuffer *buffer, int32_t chunk_idx)
{
    return buffer_read_u32(buffer, buffer_chunk_offset(BUFFER_CHUNK_SIZE_OFFSET, chunk_idx));
}

static inline void buffer_set_chunk_size(AL_TBuffer *buffer, int32_t chunk_idx, uint32_t size)
{
    buffer_write_u32(buffer, buffer_chunk_offset(BUFFER_CHUNK_SIZE_OFFSET, chunk_idx), size);
}

static inline uint8_t buffer_get_chunk_mode(const AL_TBuffer *buffer, int32_t chunk_idx)
{
    return buffer_read_u8(buffer, BUFFER_CHUNK_MODE_OFFSET + (uint32_t)chunk_idx);
}

static inline void buffer_set_chunk_mode(AL_TBuffer *buffer, int32_t chunk_idx, uint8_t mode)
{
    buffer_write_u8(buffer, BUFFER_CHUNK_MODE_OFFSET + (uint32_t)chunk_idx, mode);
}

static inline uint32_t buffer_get_metadata_count(const AL_TBuffer *buffer)
{
    return buffer_read_u32(buffer, BUFFER_METADATA_COUNT_OFFSET);
}

static inline void buffer_set_metadata_count(AL_TBuffer *buffer, uint32_t count)
{
    buffer_write_u32(buffer, BUFFER_METADATA_COUNT_OFFSET, count);
}

static inline void **buffer_get_metadata_list(const AL_TBuffer *buffer)
{
    return (void **)buffer_read_ptr(buffer, BUFFER_METADATA_LIST_OFFSET);
}

static inline void buffer_set_metadata_list(AL_TBuffer *buffer, void **list)
{
    buffer_write_ptr(buffer, BUFFER_METADATA_LIST_OFFSET, list);
}

static inline void *buffer_get_mutex(const AL_TBuffer *buffer)
{
    return buffer_read_ptr(buffer, BUFFER_MUTEX_OFFSET);
}

static inline void buffer_set_mutex(AL_TBuffer *buffer, void *mutex)
{
    buffer_write_ptr(buffer, BUFFER_MUTEX_OFFSET, mutex);
}

static inline int32_t *buffer_get_refcount_ptr(AL_TBuffer *buffer)
{
    return (int32_t *)((uint8_t *)buffer + BUFFER_REFCOUNT_OFFSET);
}

static inline void *buffer_get_destroy_cb(const AL_TBuffer *buffer)
{
    return buffer_read_ptr(buffer, BUFFER_DESTROY_CB_OFFSET);
}

static AL_TBuffer *createEmptyBuffer(AL_TAllocator *arg1, void *arg2)
{
    AL_TBuffer *result = (AL_TBuffer *)Rtos_Malloc(0x48);
    void *mutex;

    if (result == NULL)
        return NULL;

    buffer_write_ptr(result, BUFFER_ALLOCATOR_OFFSET, arg1);
    buffer_write_ptr(result, BUFFER_DESTROY_CB_OFFSET, arg2);
    buffer_set_metadata_list(result, NULL);
    buffer_set_metadata_count(result, 0);
    buffer_write_u32(result, BUFFER_REFCOUNT_OFFSET, 0);
    buffer_set_chunk_count(result, 0);
    mutex = Rtos_CreateMutex();
    buffer_set_mutex(result, mutex);
    if (mutex != NULL)
        return result;

    Rtos_Free(result);
    return NULL;
}

static void *Realloc(void *arg1, int32_t arg2, int32_t arg3)
{
    int32_t s3 = arg2;
    void *result = Rtos_Malloc((size_t)arg3);

    if (result == NULL)
        return NULL;

    if (arg1 == NULL)
        return result;

    if ((uint32_t)s3 >= (uint32_t)arg3)
        s3 = arg3;

    Rtos_Memcpy(result, arg1, (size_t)s3);
    Rtos_Free(arg1);
    return result;
}

AL_TBuffer *AL_Buffer_CreateEmpty(AL_TAllocator *arg1, void *arg2)
{
    return createEmptyBuffer(arg1, arg2);
}

int32_t AL_Buffer_AllocateChunk(AL_TBuffer *arg1, int32_t arg2, char arg3, uint32_t arg4, int32_t arg5)
{
    uint32_t s3;
    AL_TAllocator *a0;
    const AL_TAllocatorVtable *vtable;
    void *v0_2;
    int32_t result;

    if (buffer_get_chunk_count(arg1) >= BUFFER_CHUNK_MAX)
        return -1;

    s3 = (uint32_t)(uint8_t)arg3;
    a0 = buffer_get_allocator(arg1);
    vtable = a0->vtable;
    if (s3 != 0) {
        v0_2 = vtable->AllocNamedEmpty(a0, arg2, (char *)(uintptr_t)arg5);
        if (arg2 != 0) {
            if (v0_2 == NULL)
                return -1;
        }
    } else if (vtable->AllocNamed == NULL) {
        v0_2 = vtable->Alloc(a0, arg2);
        if (arg2 != 0) {
            if (v0_2 == NULL)
                return -1;
        }
    } else {
        v0_2 = vtable->AllocNamed(a0, arg2, arg5);
        if (arg2 != 0) {
            if (v0_2 == NULL)
                return -1;
        }
    }

    result = buffer_get_chunk_count(arg1);
    buffer_set_chunk_handle(arg1, result, v0_2);
    buffer_set_chunk_size(arg1, result, (uint32_t)arg2);
    buffer_set_chunk_mode(arg1, result, (uint8_t)s3);
    /* +0x24 holds the per-chunk fourth argument copied verbatim from the OEM code. */
    buffer_write_u32(arg1, buffer_chunk_offset(BUFFER_CHUNK_ARG4_OFFSET, result), arg4);
    buffer_set_chunk_count(arg1, (uint8_t)(result + 1));
    return result;
}

int32_t AL_Buffer_AddChunk(AL_TBuffer *arg1, void *arg2, int32_t arg3, uint8_t arg4, uint32_t arg5)
{
    int32_t result = buffer_get_chunk_count(arg1);

    if (result >= BUFFER_CHUNK_MAX || (arg3 != 0 && arg2 == NULL))
        return -1;

    buffer_set_chunk_handle(arg1, result, arg2);
    buffer_set_chunk_size(arg1, result, (uint32_t)arg3);
    buffer_set_chunk_mode(arg1, result, arg4);
    /* +0x24 holds the per-chunk fifth argument copied verbatim from the OEM code. */
    buffer_write_u32(arg1, buffer_chunk_offset(BUFFER_CHUNK_ARG4_OFFSET, result), arg5);
    buffer_set_chunk_count(arg1, (uint8_t)(result + 1));
    return result;
}

void AL_Buffer_Destroy(AL_TBuffer *arg1)
{
    int32_t i;
    int32_t i_1;

    Rtos_GetMutex(buffer_get_mutex(arg1));
    if (*(int32_t *)((uint8_t *)arg1 + BUFFER_REFCOUNT_OFFSET) != 0) {
        __assert("pBuf->iRefCount == 0", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/BufferAPI.c", 0xba, "AL_Buffer_Destroy");
        __builtin_unreachable();
    }

    i = 0;
    if ((int32_t)buffer_get_metadata_count(arg1) > 0) {
        do {
            void *a0_1 = buffer_get_metadata_list(arg1)[i];
            i += 1;
            ((void (*)(void *))((uint8_t *)a0_1 + 4))(a0_1);
        } while (i < (int32_t)buffer_get_metadata_count(arg1));
    }

    Rtos_Free(buffer_get_metadata_list(arg1));
    if (buffer_get_chunk_count(arg1) > 0) {
        for (i_1 = 0; i_1 < buffer_get_chunk_count(arg1); ++i_1) {
            AL_TAllocator *a0_3 = buffer_get_allocator(arg1);

            if (buffer_get_chunk_mode(arg1, i_1) == 0)
                a0_3->vtable->Free(a0_3, buffer_get_chunk_handle(arg1, i_1));
            else
                a0_3->vtable->FreeEmpty(a0_3, (int32_t)(uintptr_t)buffer_get_chunk_handle(arg1, i_1));
        }
    }

    Rtos_ReleaseMutex(buffer_get_mutex(arg1));
    Rtos_DeleteMutex(buffer_get_mutex(arg1));
    Rtos_Free(arg1);
}

static AL_TBuffer *createBufferWithOneChunk(AL_TAllocator *arg1, void *arg2, int32_t arg3, char arg4, uint32_t arg5, void *arg6)
{
    AL_TBuffer *result = createEmptyBuffer(arg1, arg6);
    int32_t a0;

    if (result == NULL)
        return NULL;

    if (arg3 == 0 || arg2 != NULL) {
        a0 = buffer_get_chunk_count(result);
        buffer_set_chunk_handle(result, a0, arg2);
        buffer_set_chunk_size(result, a0, (uint32_t)arg3);
        buffer_set_chunk_mode(result, a0, (uint8_t)arg4);
        /* +0x24 holds the per-chunk fifth argument copied verbatim from the OEM code. */
        buffer_write_u32(result, buffer_chunk_offset(BUFFER_CHUNK_ARG4_OFFSET, a0), arg5);
        buffer_set_chunk_count(result, (uint8_t)(a0 + 1));
        if (a0 != -1)
            return result;
    }

    AL_Buffer_Destroy(result);
    return NULL;
}

AL_TBuffer *AL_Buffer_WrapData(int32_t arg1, int32_t arg2, char arg3, uint32_t arg4, void *arg5)
{
    return createBufferWithOneChunk(AL_GetWrapperAllocator(), AL_WrapperAllocator_WrapData(arg1, 0, 0), arg2, arg3, arg4, arg5);
}

AL_TBuffer *AL_Buffer_Create(AL_TAllocator *arg1, void *arg2, int32_t arg3, char arg4, uint32_t arg5, void *arg6)
{
    return createBufferWithOneChunk(arg1, arg2, arg3, (char)(uint8_t)arg4, arg5, arg6);
}

AL_TBuffer *AL_Buffer_Create_And_AllocateNamed(AL_TAllocator *arg1, int32_t arg2, char arg3, uint32_t arg4, void *arg5, int32_t arg6)
{
    uint32_t s1 = (uint32_t)(uint8_t)arg3;
    const AL_TAllocatorVtable *vtable = arg1->vtable;
    void *s2;
    AL_TBuffer *result;

    if (s1 != 0)
        s2 = vtable->AllocNamedEmpty(arg1, arg2, (char *)(uintptr_t)arg6);
    else if (vtable->AllocNamed == NULL)
        s2 = vtable->Alloc(arg1, arg2);
    else
        s2 = vtable->AllocNamed(arg1, arg2, arg6);

    if (s2 == NULL)
        return NULL;

    result = createBufferWithOneChunk(arg1, s2, arg2, (char)s1, arg4, arg5);
    if (result == NULL) {
        if (s1 == 0)
            vtable->Free(arg1, s2);
        else
            vtable->FreeEmpty(arg1, (int32_t)(uintptr_t)s2);
    }

    return result;
}

AL_TBuffer *AL_Buffer_Create_And_Allocate(AL_TAllocator *arg1, int32_t arg2, char arg3, uint32_t arg4, void *arg5)
{
    return AL_Buffer_Create_And_AllocateNamed(arg1, arg2, (char)(uint8_t)arg3, arg4, arg5, (int32_t)(uintptr_t)"unknown");
}

void AL_Buffer_SetUserData(AL_TBuffer *arg1, void *arg2)
{
    void *a0_1;

    Rtos_GetMutex(buffer_get_mutex(arg1));
    a0_1 = buffer_get_mutex(arg1);
    buffer_write_ptr(arg1, BUFFER_USER_DATA_OFFSET, arg2);
    Rtos_ReleaseMutex(a0_1);
}

void *AL_Buffer_GetUserData(AL_TBuffer *arg1)
{
    void *result;

    Rtos_GetMutex(buffer_get_mutex(arg1));
    result = buffer_read_ptr(arg1, BUFFER_USER_DATA_OFFSET);
    Rtos_ReleaseMutex(buffer_get_mutex(arg1));
    return result;
}

int32_t AL_Buffer_Ref(AL_TBuffer *arg1)
{
    return Rtos_AtomicIncrement(buffer_get_refcount_ptr(arg1));
}

int32_t AL_Buffer_Unref(AL_TBuffer *arg1)
{
    int32_t result = Rtos_AtomicDecrement(buffer_get_refcount_ptr(arg1));

    if (result < 0) {
        __assert("iRefCount >= 0", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/BufferAPI.c", 0xf2, "AL_Buffer_Unref");
        __builtin_unreachable();
    }

    if (result == 0) {
        int32_t (*t9_1)(AL_TBuffer *) = (int32_t (*)(AL_TBuffer *))buffer_get_destroy_cb(arg1);

        if (t9_1 != NULL)
            return t9_1(arg1);
    }

    return result;
}

void *AL_Buffer_GetMetaData(AL_TBuffer *arg1, int32_t arg2)
{
    int32_t a2;
    void **v1_1;
    int32_t s1_1;

    Rtos_GetMutex(buffer_get_mutex(arg1));
    a2 = (int32_t)buffer_get_metadata_count(arg1);
    if (a2 <= 0) {
        Rtos_ReleaseMutex(buffer_get_mutex(arg1));
        return NULL;
    }

    v1_1 = buffer_get_metadata_list(arg1);
    if (**(int32_t **)v1_1 == arg2) {
        s1_1 = 0;
    } else {
        int32_t v0_3 = 0;
        int32_t a0_2;
        void **v1_2 = &v1_1[1];

        do {
            v0_3 += 1;
            s1_1 = v0_3 << 2;
            if (v0_3 == a2) {
                Rtos_ReleaseMutex(buffer_get_mutex(arg1));
                return NULL;
            }
            a0_2 = **(int32_t **)v1_2;
            v1_2 = &v1_2[1];
        } while (a0_2 != arg2);
    }

    Rtos_ReleaseMutex(buffer_get_mutex(arg1));
    return *(void **)((uint8_t *)buffer_get_metadata_list(arg1) + (uint32_t)s1_1);
}

int32_t AL_Buffer_AddMetaData(AL_TBuffer *arg1, void *arg2)
{
    int32_t a1_1;
    void *v0;
    int32_t v1;
    void *a0_3;

    Rtos_GetMutex(buffer_get_mutex(arg1));
    a1_1 = (int32_t)(buffer_get_metadata_count(arg1) << 2);
    v0 = Realloc(buffer_get_metadata_list(arg1), a1_1, a1_1 + 4);
    if (v0 == NULL) {
        Rtos_ReleaseMutex(buffer_get_mutex(arg1));
        return 0;
    }

    v1 = (int32_t)buffer_get_metadata_count(arg1);
    buffer_set_metadata_list(arg1, (void **)v0);
    *(void **)((uint8_t *)v0 + ((uint32_t)v1 << 2)) = arg2;
    a0_3 = buffer_get_mutex(arg1);
    buffer_set_metadata_count(arg1, (uint32_t)(v1 + 1));
    Rtos_ReleaseMutex(a0_3);
    return 1;
}

int32_t AL_Buffer_RemoveMetaData(AL_TBuffer *arg1, void *arg2)
{
    int32_t a2;
    void **a3_1;
    void **a1;
    void **v0_2;
    int32_t v1_1;
    void *v0_5;
    void *a0_4;

    Rtos_GetMutex(buffer_get_mutex(arg1));
    a2 = (int32_t)buffer_get_metadata_count(arg1);
    if (a2 <= 0) {
        Rtos_ReleaseMutex(buffer_get_mutex(arg1));
        return 0;
    }

    a3_1 = buffer_get_metadata_list(arg1);
    v0_2 = &a3_1[1];
    if (arg2 == *a3_1) {
        a1 = a3_1;
    } else {
        v1_1 = 0;
        do {
            v1_1 += 1;
            a1 = v0_2;
            v0_2 = &v0_2[1];
            if (v1_1 == a2) {
                Rtos_ReleaseMutex(buffer_get_mutex(arg1));
                return 0;
            }
        } while (*(v0_2 - 1) != arg2);
    }

    *a1 = *(void **)((uint8_t *)a3_1 + ((uint32_t)a2 << 2) - 4);
    v0_5 = Realloc(buffer_get_metadata_list(arg1), a2 << 2, (a2 << 2) - 4);
    if (v0_5 != NULL)
        buffer_set_metadata_list(arg1, (void **)v0_5);

    a0_4 = buffer_get_mutex(arg1);
    buffer_set_metadata_count(arg1, buffer_get_metadata_count(arg1) - 1);
    Rtos_ReleaseMutex(a0_4);
    return 1;
}

void *AL_Buffer_GetDataChunk(AL_TBuffer *arg1, int32_t arg2)
{
    int32_t v1_1 = (int32_t)(int8_t)(uint8_t)arg2;
    AL_TAllocator *allocator;

    if (v1_1 < 0 || v1_1 >= buffer_get_chunk_count(arg1))
        return NULL;

    allocator = buffer_get_allocator(arg1);
    return (void *)(uintptr_t)allocator->vtable->GetVirtualAddr(allocator, buffer_get_chunk_handle(arg1, arg2));
}

void *AL_Buffer_GetData(AL_TBuffer *arg1)
{
    return AL_Buffer_GetDataChunk(arg1, 0);
}

uint32_t AL_Buffer_GetPhysicalAddressChunk(AL_TBuffer *arg1, int32_t arg2)
{
    int32_t v1_1 = (int32_t)(int8_t)(uint8_t)arg2;
    AL_TAllocator *allocator;

    if (v1_1 < 0 || v1_1 >= buffer_get_chunk_count(arg1))
        return 0;

    allocator = buffer_get_allocator(arg1);
    return (uint32_t)allocator->vtable->GetPhysicalAddr(allocator, buffer_get_chunk_handle(arg1, arg2));
}

uint32_t AL_Buffer_GetPhysicalAddress(AL_TBuffer *arg1)
{
    return AL_Buffer_GetPhysicalAddressChunk(arg1, 0);
}

uint32_t AL_Buffer_GetSizeChunk(AL_TBuffer *arg1, int32_t arg2)
{
    int32_t v1 = (int32_t)(int8_t)(uint8_t)arg2;
    int32_t result;

    if (v1 < 0)
        return 0;

    result = v1 < buffer_get_chunk_count(arg1) ? 1 : 0;
    if (result == 0)
        return (uint32_t)result;

    return buffer_get_chunk_size(arg1, arg2);
}

uint32_t AL_Buffer_GetSize(AL_TBuffer *arg1)
{
    return AL_Buffer_GetSizeChunk(arg1, 0);
}

void AL_Buffer_MemSet(AL_TBuffer *arg1, int32_t arg2)
{
    int32_t s3 = buffer_get_chunk_count(arg1);
    int32_t s0_1;

    if (s3 <= 0)
        return;

    s0_1 = 0;
    do {
        void *v0_1 = AL_Buffer_GetDataChunk(arg1, s0_1);
        int32_t a1_1 = s0_1;

        s0_1 += 1;
        Rtos_Memset(v0_1, arg2, AL_Buffer_GetSizeChunk(arg1, a1_1));
    } while (s3 != s0_1);
}
