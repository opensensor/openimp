#include <stdint.h>
#include <stddef.h>

#include "alcodec/al_allocator.h"
#include "alcodec/al_buffer.h"
#include "alcodec/al_nal.h"
#include "alcodec/al_rtos.h"

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line, const char *function,
                        void *caller);

AL_TBuffer *AL_Buffer_Create_And_Allocate(AL_TAllocator *arg1, int32_t arg2, char arg3, uint32_t arg4,
                                          void *arg5); /* forward decl, ported by T16 later */
int32_t AL_Buffer_Ref(AL_TBuffer *arg1); /* forward decl, ported by T16 later */
int32_t AL_Buffer_Unref(AL_TBuffer *arg1); /* forward decl, ported by T16 later */
int32_t AL_GetAllocSize_MV(int32_t arg1, int32_t arg2, char arg3,
                           int32_t arg4); /* forward decl, ported by T11 later */

static inline int32_t meta_read_s32(const void *ptr, uint32_t offset)
{
    return *(const int32_t *)((const uint8_t *)ptr + offset);
}

static inline uint32_t meta_read_u32(const void *ptr, uint32_t offset)
{
    return *(const uint32_t *)((const uint8_t *)ptr + offset);
}

static inline uint16_t meta_read_u16(const void *ptr, uint32_t offset)
{
    return *(const uint16_t *)((const uint8_t *)ptr + offset);
}

static inline uint8_t meta_read_u8(const void *ptr, uint32_t offset)
{
    return *(const uint8_t *)((const uint8_t *)ptr + offset);
}

static inline void *meta_read_ptr(const void *ptr, uint32_t offset)
{
    return (void *)(uintptr_t)meta_read_u32(ptr, offset);
}

static inline void meta_write_u32(void *ptr, uint32_t offset, uint32_t value)
{
    *(uint32_t *)((uint8_t *)ptr + offset) = value;
}

static inline void meta_write_u16(void *ptr, uint32_t offset, uint16_t value)
{
    *(uint16_t *)((uint8_t *)ptr + offset) = value;
}

static inline void meta_write_u8(void *ptr, uint32_t offset, uint8_t value)
{
    *(uint8_t *)((uint8_t *)ptr + offset) = value;
}

static inline void meta_write_ptr(void *ptr, uint32_t offset, const void *value)
{
    meta_write_u32(ptr, offset, (uint32_t)(uintptr_t)value);
}

static void *clone_circ(void *arg1);
int32_t StreamMeta_Destroy(void *arg1);
static void *StreamMeta_Clone(void *arg1);
void *AL_StreamMetaData_SetSectionFlags(void *arg1, int32_t arg2, int32_t arg3);
void AL_StreamMetaData_ClearAllSections(void *arg1);
int32_t AL_StreamMetaData_GetLastSectionOfFlag(void *arg1, int32_t arg2);
int32_t AL_StreamMetaData_AddSeiSection(void *arg1, char arg2, int32_t arg3, int32_t arg4, int32_t arg5);
int32_t PictureMeta_Destroy(void *arg1);
static void *PictureMeta_Clone(void *arg1);
static int32_t destroy_handle(void *arg1);
static void *clone_handle(void *arg1);
static int32_t destroy_sei(void *arg1);
static void *clone_sei(void *arg1);
static void *create(AL_TAllocator *arg1, int32_t arg2);
static void *clone(void *arg1);
static int32_t destroy(void *arg1);

static int32_t destroy_circ(void *arg1)
{
    Rtos_Free(arg1);
    return 1;
}

void *AL_CircMetaData_Create(int32_t arg1, int32_t arg2, char arg3)
{
    void *result = Rtos_Malloc(0x18);

    if (result != NULL) {
        meta_write_u32(result, 0x0, 2);
        meta_write_u32(result, 0xc, (uint32_t)arg1);
        meta_write_u32(result, 0x10, (uint32_t)arg2);
        meta_write_ptr(result, 0x4, destroy_circ);
        meta_write_u8(result, 0x14, (uint8_t)arg3);
        meta_write_ptr(result, 0x8, clone_circ);
    }

    return result;
}

void *AL_CircMetaData_Clone(void *arg1)
{
    return AL_CircMetaData_Create(meta_read_s32(arg1, 0xc), meta_read_s32(arg1, 0x10), meta_read_u8(arg1, 0x14));
}

static void *clone_circ(void *arg1)
{
    return AL_CircMetaData_Clone(arg1);
}

int32_t StreamMeta_Destroy(void *arg1)
{
    Rtos_Free(meta_read_ptr(arg1, 0x10));
    Rtos_Free(arg1);
    return 1;
}

void *AL_StreamMetaData_Create(int32_t arg1)
{
    uint32_t s1 = (uint32_t)arg1;

    if (s1 != 0U) {
        void *result = Rtos_Malloc(0x18);

        if (result != NULL) {
            void *v0_1;

            meta_write_u32(result, 0x0, 1);
            meta_write_ptr(result, 0x4, StreamMeta_Destroy);
            meta_write_ptr(result, 0x8, StreamMeta_Clone);
            meta_write_u32(result, 0xc, 0);
            meta_write_u16(result, 0x16, (uint16_t)s1);
            meta_write_u32(result, 0x14, 0);
            v0_1 = Rtos_Malloc((size_t)(s1 * 0x14));
            meta_write_ptr(result, 0x10, v0_1);
            if (v0_1 != NULL) {
                return result;
            }

            Rtos_Free(result);
        }
    }

    return NULL;
}

void *AL_StreamMetaData_Clone(void *arg1)
{
    void *result = AL_StreamMetaData_Create(meta_read_u16(arg1, 0x16));
    uint32_t v1_2;

    meta_write_u32(result, 0xc, meta_read_u32(arg1, 0xc));
    meta_write_u32(result, 0x14, meta_read_u32(arg1, 0x14));
    v1_2 = meta_read_u32(arg1, 0x14);
    meta_write_u16(result, 0x16, meta_read_u16(arg1, 0x16));
    if (v1_2 != 0U) {
        int32_t *i = (int32_t *)meta_read_ptr(arg1, 0x10);
        int32_t *a0_2 = (int32_t *)meta_read_ptr(result, 0x10);

        do {
            int32_t t1_1 = *i;
            int32_t t0_1 = i[1];
            int32_t a3_1 = i[2];
            int32_t a2_1 = i[3];
            int32_t a1_2 = i[4];

            i = &i[5];
            *a0_2 = t1_1;
            a0_2[1] = t0_1;
            a0_2[2] = a3_1;
            a0_2[3] = a2_1;
            a0_2[4] = a1_2;
            a0_2 = &a0_2[5];
        } while (i != &i[((uint32_t)(uint16_t)(v1_2 - 1U) + 1U) * 5U]);
    }

    return result;
}

static void *StreamMeta_Clone(void *arg1)
{
    return AL_StreamMetaData_Clone(arg1);
}

uint32_t AL_StreamMetaData_AddSection(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
                                      int32_t arg6)
{
    uint32_t result = meta_read_u32(arg1, 0x14);

    if (result >= (uint32_t)meta_read_u16(arg1, 0x16)) {
        return 0xffffffffU;
    }

    {
        int32_t *v1_4 = (int32_t *)((uint8_t *)meta_read_ptr(arg1, 0x10) + (result * 0x14U));

        *v1_4 = arg2;
        v1_4[1] = arg3;
        v1_4[2] = arg6;
        v1_4[3] = arg4;
        v1_4[4] = arg5;
        meta_write_u32(arg1, 0x14, (uint32_t)((uint16_t)result + 1U));
    }

    return result;
}

int32_t AL_StreamMetaData_ChangeSection(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
                                        int32_t arg6)
{
    uint32_t a1 = (uint32_t)arg2;

    if (arg1 == NULL || a1 >= meta_read_u32(arg1, 0x14)) {
        int32_t a0 = __assert("pMetaData && uSectionID < pMetaData->uNumSection",
                              "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/BufferStreamMeta.c",
                              0x57, "AL_StreamMetaData_ChangeSection", &_gp);

        return (int32_t)(intptr_t)AL_StreamMetaData_SetSectionFlags((void *)(intptr_t)a0, (int16_t)a0, a0);
    }

    {
        int32_t *a1_3 = (int32_t *)((uint8_t *)meta_read_ptr(arg1, 0x10) + (a1 * 0x14U));

        *a1_3 = arg3;
        a1_3[3] = arg5;
        a1_3[1] = arg4;
        a1_3[4] = arg6;
    }

    return arg6;
}

void *AL_StreamMetaData_SetSectionFlags(void *arg1, int32_t arg2, int32_t arg3)
{
    uint32_t a1 = (uint32_t)arg2;

    if (arg1 == NULL || a1 >= meta_read_u32(arg1, 0x14)) {
        AL_StreamMetaData_ClearAllSections((void *)(intptr_t)__assert(
            "pMetaData && uSectionID < pMetaData->uNumSection",
            "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/BufferStreamMeta.c",
            0x62, "AL_StreamMetaData_SetSectionFlags", &_gp));
        return NULL;
    }

    {
        void *result = meta_read_ptr(arg1, 0x10);

        *(int32_t *)((uint8_t *)result + (a1 * 0x14U) + 8U) = arg3;
        return result;
    }
}

void AL_StreamMetaData_ClearAllSections(void *arg1)
{
    if (arg1 != NULL) {
        meta_write_u32(arg1, 0x14, 0);
        return;
    }

    {
        int32_t a0 = __assert("pMetaData",
                              "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/BufferStreamMeta.c",
                              0x69, "AL_StreamMetaData_ClearAllSections", &_gp);

        AL_StreamMetaData_GetLastSectionOfFlag((void *)(intptr_t)a0, a0);
    }
}

int32_t AL_StreamMetaData_GetLastSectionOfFlag(void *arg1, int32_t arg2)
{
    if (arg1 == NULL) {
        int32_t a0 = __assert("pMetaData",
                              "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/BufferStreamMeta.c",
                              0x70, "AL_StreamMetaData_GetLastSectionOfFlag", &_gp);

        return AL_StreamMetaData_AddSeiSection((void *)(intptr_t)a0, (char)a0, a0, a0, a0);
    }

    {
        uint32_t a2 = meta_read_u32(arg1, 0x14);
        void *v1 = meta_read_ptr(arg1, 0x10);
        int32_t result = (int32_t)(a2 - 1U);

        if (a2 != 0U && (arg2 & *(int32_t *)((uint8_t *)v1 + ((uint32_t)result * 0x14U) + 8U)) == 0) {
            int32_t *a0_7 = (int32_t *)((uint8_t *)v1 + (a2 * 0x14U) - 0x20U);
            int32_t i;

            do {
                i = arg2 & *a0_7;
                a0_7 = &a0_7[-5];
                result -= 1;
                if (result == -1) {
                    break;
                }
            } while (i == 0);
        }

        return result;
    }
}

int32_t AL_StreamMetaData_AddSeiSection(void *arg1, char arg2, int32_t arg3, int32_t arg4, int32_t arg5)
{
    if ((uint32_t)(uint8_t)arg2 == 0U) {
        return (int32_t)AL_StreamMetaData_AddSection(arg1, arg3, arg4, arg5, -1, 0);
    }

    {
        int16_t v0_1 = (int16_t)AL_StreamMetaData_GetLastSectionOfFlag(arg1, 0x10000000);
        uint32_t t7_1 = meta_read_u32(arg1, 0x14);
        uint8_t *t5_1 = (uint8_t *)meta_read_ptr(arg1, 0x10);
        uint32_t result;
        uint32_t t8;

        if (t7_1 >= (uint32_t)meta_read_u16(arg1, 0x16)) {
            return -1;
        }

        result = (uint32_t)(uint16_t)(v0_1 + 1);
        t8 = result << 4;
        if ((int32_t)(t7_1 - 1U) >= (int32_t)result) {
            uint8_t *i = t5_1 + (t7_1 * 0x14U);
            uint32_t a1_3 = t7_1 & 0xffffU;

            do {
                int32_t t3_1 = *(int32_t *)(i - 0x14);
                int32_t t2_1 = *(int32_t *)(i - 0x10);
                int32_t t0_1 = *(int32_t *)(i - 8);
                int32_t a3_1 = *(int32_t *)(i - 4);
                int32_t t1_1 = *(int32_t *)(i - 0xc);
                int32_t *v1_8 = (int32_t *)(t5_1 + (a1_3 * 0x14U));

                i -= 0x14;
                *v1_8 = t3_1;
                v1_8[1] = t2_1;
                v1_8[2] = t1_1;
                v1_8[3] = t0_1;
                v1_8[4] = a3_1;
                a1_3 = (uint32_t)(uint16_t)(a1_3 - 1U);
            } while (i != t5_1 + (result << 2) + t8);
        }

        {
            int32_t *t5_2 = (int32_t *)(t5_1 + (result << 2) + t8);

            t5_2[2] = SECTION_SEI_PREFIX;
            t5_2[3] = arg5;
            *t5_2 = arg3;
            t5_2[1] = arg4;
            t5_2[4] = -1;
            meta_write_u32(arg1, 0x14, (uint32_t)((uint16_t)t7_1 + 1U));
        }

        return (int32_t)result;
    }
}

int32_t AL_StreamMetaData_GetUnusedStreamPart(void *arg1)
{
    uint32_t v0_2 = meta_read_u32(arg1, 0x14);
    int32_t *i = (int32_t *)meta_read_ptr(arg1, 0x10);

    if (v0_2 == 0U) {
        return 0;
    }

    {
        int32_t result = 0;

        do {
            int32_t a1_1 = i[1];
            int32_t a0 = *i;
            int32_t result_1;

            i = &i[5];
            result_1 = a0 + a1_1;
            if (result < result_1) {
                result = result_1;
            }
        } while (i != &i[((uint32_t)(uint16_t)(v0_2 - 1U) + 1U) * 5U]);

        return result;
    }
}

int32_t PictureMeta_Destroy(void *arg1)
{
    Rtos_Free(arg1);
    return 1;
}

void *AL_PictureMetaData_Create(void)
{
    void *result = Rtos_Malloc(0x10);

    if (result != NULL) {
        meta_write_u32(result, 0x0, 3);
        meta_write_ptr(result, 0x4, PictureMeta_Destroy);
        meta_write_ptr(result, 0x8, PictureMeta_Clone);
        meta_write_u32(result, 0xc, 9);
    }

    return result;
}

void *AL_PictureMetaData_Clone(void *arg1)
{
    void *result;

    if (arg1 == NULL) {
        return NULL;
    }

    result = AL_PictureMetaData_Create();
    if (result == NULL) {
        return NULL;
    }

    meta_write_u32(result, 0xc, meta_read_u32(arg1, 0xc));
    return result;
}

static void *PictureMeta_Clone(void *arg1)
{
    return AL_PictureMetaData_Clone(arg1);
}

static int32_t destroy_handle(void *arg1)
{
    Rtos_Free(meta_read_ptr(arg1, 0xc));
    Rtos_Free(arg1);
    return 1;
}

void *AL_HandleMetaData_Create(int32_t arg1, int32_t arg2)
{
    void *result = Rtos_Malloc(0x1c);

    if (result == NULL) {
        return NULL;
    }

    {
        void *v0 = Rtos_Malloc((size_t)(arg1 * arg2));

        meta_write_ptr(result, 0xc, v0);
        if (v0 == NULL) {
            Rtos_Free(result);
            return NULL;
        }

        meta_write_u32(result, 0x0, 4);
        meta_write_u32(result, 0x10, 0);
        meta_write_u32(result, 0x14, (uint32_t)arg2);
        meta_write_ptr(result, 0x4, destroy_handle);
        meta_write_u32(result, 0x18, (uint32_t)arg1);
        meta_write_ptr(result, 0x8, clone_handle);
        return result;
    }
}

void *AL_HandleMetaData_Clone(void *arg1)
{
    void *result;

    if (arg1 == NULL) {
        return NULL;
    }

    result = AL_HandleMetaData_Create(meta_read_s32(arg1, 0x18), meta_read_s32(arg1, 0x14));
    if (result == NULL) {
        return NULL;
    }

    if (meta_read_s32(arg1, 0x10) > 0) {
        int32_t t0_1 = meta_read_s32(result, 0x10);
        int32_t i = 0;

        do {
            int32_t a3_1 = i << 2;

            i += 1;
            *(int32_t *)((uint8_t *)meta_read_ptr(result, 0xc) + a3_1) =
                *(int32_t *)((uint8_t *)meta_read_ptr(arg1, 0xc) + a3_1);
            meta_write_u32(result, 0x10, (uint32_t)(t0_1 + i));
        } while (i < meta_read_s32(arg1, 0x10));
    }

    return result;
}

static void *clone_handle(void *arg1)
{
    return AL_HandleMetaData_Clone(arg1);
}

int32_t AL_HandleMetaData_AddHandle(void *arg1, const void *arg2)
{
    int32_t a3 = meta_read_s32(arg1, 0x10);

    if (a3 >= meta_read_s32(arg1, 0x18)) {
        return 0;
    }

    Rtos_Memcpy((uint8_t *)meta_read_ptr(arg1, 0xc) + ((a3 * meta_read_s32(arg1, 0x14)) << 2), arg2,
                (size_t)(meta_read_s32(arg1, 0x14) << 2));
    meta_write_u32(arg1, 0x10, (uint32_t)(a3 + 1));
    return 1;
}

void *AL_HandleMetaData_GetHandle(void *arg1, int32_t arg2)
{
    return (uint8_t *)meta_read_ptr(arg1, 0xc) + ((arg2 * meta_read_s32(arg1, 0x14)) << 2);
}

static int32_t destroy_sei(void *arg1)
{
    Rtos_Free(meta_read_ptr(arg1, 0x10));
    Rtos_Free(meta_read_ptr(arg1, 0x14));
    Rtos_Free(arg1);
    return 1;
}

int32_t AL_SeiMetaData_AddPayload(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5)
{
    uint32_t t0 = (uint32_t)meta_read_u8(arg1, 0xc);
    int32_t result = 0;

    if ((int32_t)t0 < (int32_t)meta_read_u8(arg1, 0xd)) {
        int32_t *v0_1 = (int32_t *)((uint8_t *)meta_read_ptr(arg1, 0x10) + (t0 << 4));

        *v0_1 = arg2;
        v0_1[1] = arg3;
        v0_1[2] = arg4;
        v0_1[3] = arg5;
        result = 1;
        meta_write_u8(arg1, 0xc, (uint8_t)(t0 + 1U));
    }

    return result;
}

void *AL_SeiMetaData_GetBuffer(void *arg1)
{
    uint32_t v0_6 = (uint32_t)meta_read_u8(arg1, 0xc);

    if (v0_6 == 0U) {
        return meta_read_ptr(arg1, 0x14);
    }

    {
        uint8_t *v0_2 = (uint8_t *)meta_read_ptr(arg1, 0x10) + (v0_6 << 4) - 0x10U;

        return (void *)(uintptr_t)(meta_read_u32(v0_2, 8) + meta_read_u32(v0_2, 0xc));
    }
}

void *AL_SeiMetaData_Reset(void *arg1)
{
    int32_t a2 = meta_read_s32(arg1, 0x18);
    void *a0 = meta_read_ptr(arg1, 0x14);

    meta_write_u32(arg1, 0xc, 0);
    return Rtos_Memset(a0, 0, (size_t)a2);
}

void *AL_SeiMetaData_Create(char arg1, int32_t arg2)
{
    uint32_t s2 = (uint32_t)(uint8_t)arg1;
    void *result = Rtos_Malloc(0x1c);

    if (result == NULL) {
        return NULL;
    }

    {
        void *v0 = Rtos_Malloc((size_t)arg2);

        meta_write_ptr(result, 0x14, v0);
        if (v0 != NULL) {
            void *v0_1 = Rtos_Malloc((size_t)(s2 << 4));

            meta_write_ptr(result, 0x10, v0_1);
            if (v0_1 != NULL) {
                meta_write_u32(result, 0x0, 5);
                meta_write_u8(result, 0xd, (uint8_t)s2);
                meta_write_ptr(result, 0x4, destroy_sei);
                meta_write_u32(result, 0x18, (uint32_t)arg2);
                meta_write_ptr(result, 0x8, clone_sei);
                AL_SeiMetaData_Reset(result);
                return result;
            }

            Rtos_Free(meta_read_ptr(result, 0x14));
        }
    }

    Rtos_Free(result);
    return NULL;
}

void *AL_SeiMetaData_Clone(void *arg1)
{
    if (arg1 == NULL) {
        return NULL;
    }

    return AL_SeiMetaData_Create(meta_read_u8(arg1, 0xd), meta_read_s32(arg1, 0x18));
}

static void *clone_sei(void *arg1)
{
    return AL_SeiMetaData_Clone(arg1);
}

static void *create(AL_TAllocator *arg1, int32_t arg2)
{
    void *result = Rtos_Malloc(0x3c);

    if (result == NULL) {
        return NULL;
    }

    {
        AL_TBuffer *v0 = AL_Buffer_Create_And_Allocate(arg1, arg2, 0, 0, AL_Buffer_Destroy);

        meta_write_ptr(result, 0x38, v0);
        if (v0 == NULL) {
            Rtos_Free(result);
            return NULL;
        }

        AL_Buffer_Ref(v0);
        meta_write_u32(result, 0x0, 7);
        meta_write_u32(result, 0xc, 0);
        meta_write_ptr(result, 0x4, destroy);
        meta_write_ptr(result, 0x8, clone);
        return result;
    }
}

static void *clone(void *arg1)
{
    AL_TBuffer *v0 = (AL_TBuffer *)meta_read_ptr(arg1, 0x38);
    void *result = create((AL_TAllocator *)(uintptr_t)meta_read_u32(v0, 0x0), (int32_t)AL_Buffer_GetSize(v0));

    if (result != NULL) {
        int32_t *i = (int32_t *)((uint8_t *)arg1 + 0x10);
        int32_t *v1_1 = (int32_t *)((uint8_t *)result + 0x10);

        do {
            int32_t t0_1 = *i;
            int32_t a3_1 = i[1];
            int32_t a2_1 = i[2];
            int32_t a1_1 = i[3];

            i = &i[4];
            *v1_1 = t0_1;
            v1_1[1] = a3_1;
            v1_1[2] = a2_1;
            v1_1[3] = a1_1;
            v1_1 = &v1_1[4];
        } while (i != (int32_t *)((uint8_t *)arg1 + 0x30));

        {
            int32_t v0_2 = i[1];
            AL_TBuffer *a0_3 = (AL_TBuffer *)meta_read_ptr(result, 0x38);

            *v1_1 = *i;
            v1_1[1] = v0_2;
            Rtos_Memcpy(AL_Buffer_GetData(a0_3), AL_Buffer_GetData((AL_TBuffer *)meta_read_ptr(arg1, 0x38)),
                        AL_Buffer_GetSize((AL_TBuffer *)meta_read_ptr(arg1, 0x38)));
        }
    }

    return result;
}

static int32_t destroy(void *arg1)
{
    AL_Buffer_Unref((AL_TBuffer *)meta_read_ptr(arg1, 0x38));
    Rtos_Free(arg1);
    return 1;
}

void *AL_RateCtrlMetaData_Create(AL_TAllocator *arg1, int32_t arg2, int32_t arg3, char arg4, int32_t arg5)
{
    return create(arg1, AL_GetAllocSize_MV(arg2, arg3, (char)(uint8_t)arg4, arg5) - 0x100);
}
