#include "alcodec/al_allocator.h"
#include "alcodec/al_buffer.h"
#include "alcodec/al_fourcc.h"
#include "alcodec/al_rtos.h"

#include <stdint.h>
#include <stddef.h>

extern void __assert(const char *expr, const char *file, int line, const char *func)
    __attribute__((noreturn));

int32_t AL_Buffer_AllocateChunk(AL_TBuffer *arg1, int32_t arg2, char arg3, uint32_t arg4, int32_t arg5); /* forward decl, ported by T<N> later */
int32_t AL_Buffer_AddChunk(AL_TBuffer *arg1, void *arg2, int32_t arg3, uint8_t arg4, uint32_t arg5); /* forward decl, ported by T<N> later */
void *AL_Buffer_GetDataChunk(AL_TBuffer *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
AL_TBuffer *AL_Buffer_CreateEmpty(AL_TAllocator *arg1, void *arg2); /* forward decl, ported by T<N> later */
int32_t AL_Buffer_AddMetaData(AL_TBuffer *arg1, void *arg2); /* forward decl, ported by T<N> later */

typedef int32_t (*PixMapDestroyFunc)(void *arg1);
typedef void *(*PixMapCloneFunc)(void *arg1);

typedef struct PixMapMetaData {
    uint8_t bytes[0x48];
} PixMapMetaData;

typedef struct PixMapDimension {
    int32_t iWidth;
    int32_t iHeight;
} PixMapDimension;

_Static_assert(sizeof(PixMapMetaData) == 0x48, "PixMapMetaData size");

#define PIXMAP_META_TYPE_OFFSET 0x00
#define PIXMAP_META_DESTROY_OFFSET 0x04
#define PIXMAP_META_CLONE_OFFSET 0x08
#define PIXMAP_META_WIDTH_OFFSET 0x0c
#define PIXMAP_META_HEIGHT_OFFSET 0x10
#define PIXMAP_META_PLANES_OFFSET 0x14
#define PIXMAP_META_FOURCC_OFFSET 0x44
#define PIXMAP_PLANE_SIZE 0x0c
#define PIXMAP_PLANE_CHUNKIDX_OFFSET 0x00
#define PIXMAP_PLANE_OFFSET_OFFSET 0x04
#define PIXMAP_PLANE_PITCH_OFFSET 0x08

static inline uint32_t pixmap_meta_read_u32(const PixMapMetaData *meta, uint32_t offset)
{
    return *(const uint32_t *)(const void *)(meta->bytes + offset);
}

static inline void pixmap_meta_write_u32(PixMapMetaData *meta, uint32_t offset, uint32_t value)
{
    *(uint32_t *)(void *)(meta->bytes + offset) = value;
}

static inline void *pixmap_meta_read_ptr(const PixMapMetaData *meta, uint32_t offset)
{
    return (void *)(uintptr_t)pixmap_meta_read_u32(meta, offset);
}

static inline void pixmap_meta_write_ptr(PixMapMetaData *meta, uint32_t offset, const void *value)
{
    pixmap_meta_write_u32(meta, offset, (uint32_t)(uintptr_t)value);
}

static inline uint32_t pixmap_plane_offset(int32_t plane_idx, uint32_t member_offset)
{
    return PIXMAP_META_PLANES_OFFSET + ((uint32_t)plane_idx * PIXMAP_PLANE_SIZE) + member_offset;
}

static void *AL_PixMapMetaData_Clone_0x396b4(void *arg1);
static void *SrcMeta_Clone(void *arg1);

int32_t SrcMeta_Destroy(void *arg1)
{
    Rtos_Free(arg1);
    return 1;
}

int32_t AL_PixMapMetaData_AddPlane(PixMapMetaData *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5)
{
    int32_t result = arg5 * 0x0c;

    pixmap_meta_write_u32(arg1, (uint32_t)result + 0x14, (uint32_t)arg2);
    pixmap_meta_write_u32(arg1, (uint32_t)result + 0x18, (uint32_t)arg3);
    pixmap_meta_write_u32(arg1, (uint32_t)result + 0x1c, (uint32_t)arg4);
    return result;
}

PixMapMetaData *AL_PixMapMetaData_CreateEmpty(uint32_t arg1)
{
    PixMapMetaData *result = (PixMapMetaData *)Rtos_Malloc(0x48);
    int32_t i;

    if (result == NULL)
        return NULL;

    pixmap_meta_write_u32(result, PIXMAP_META_TYPE_OFFSET, 0);
    pixmap_meta_write_u32(result, PIXMAP_META_WIDTH_OFFSET, 0);
    pixmap_meta_write_u32(result, PIXMAP_META_HEIGHT_OFFSET, 0);
    pixmap_meta_write_ptr(result, PIXMAP_META_DESTROY_OFFSET, SrcMeta_Destroy);
    pixmap_meta_write_ptr(result, PIXMAP_META_CLONE_OFFSET, SrcMeta_Clone);

    i = 0;
    while (i != 4) {
        int32_t i_1 = i;

        i += 1;
        AL_PixMapMetaData_AddPlane(result, -1, 0, 0, i_1);
    }

    pixmap_meta_write_u32(result, PIXMAP_META_FOURCC_OFFSET, arg1);
    return result;
}

PixMapMetaData *AL_PixMapMetaData_Create(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7, int32_t arg8, uint32_t arg9)
{
    PixMapMetaData *result = AL_PixMapMetaData_CreateEmpty(arg9);

    if (result != NULL) {
        pixmap_meta_write_u32(result, PIXMAP_META_WIDTH_OFFSET, (uint32_t)arg1);
        pixmap_meta_write_u32(result, PIXMAP_META_HEIGHT_OFFSET, (uint32_t)arg2);
        AL_PixMapMetaData_AddPlane(result, arg3, arg4, arg5, 0);
        AL_PixMapMetaData_AddPlane(result, arg6, arg7, arg8, 1);
    }

    return result;
}

static void *AL_PixMapMetaData_Clone_0x396b4(void *arg1)
{
    PixMapMetaData *meta = (PixMapMetaData *)arg1;
    PixMapMetaData *result = AL_PixMapMetaData_Create((int32_t)pixmap_meta_read_u32(meta, PIXMAP_META_WIDTH_OFFSET),
                                                      (int32_t)pixmap_meta_read_u32(meta, PIXMAP_META_HEIGHT_OFFSET),
                                                      (int32_t)pixmap_meta_read_u32(meta, pixmap_plane_offset(0, PIXMAP_PLANE_CHUNKIDX_OFFSET)),
                                                      (int32_t)pixmap_meta_read_u32(meta, pixmap_plane_offset(0, PIXMAP_PLANE_OFFSET_OFFSET)),
                                                      (int32_t)pixmap_meta_read_u32(meta, pixmap_plane_offset(0, PIXMAP_PLANE_PITCH_OFFSET)),
                                                      (int32_t)pixmap_meta_read_u32(meta, pixmap_plane_offset(1, PIXMAP_PLANE_CHUNKIDX_OFFSET)),
                                                      (int32_t)pixmap_meta_read_u32(meta, pixmap_plane_offset(1, PIXMAP_PLANE_OFFSET_OFFSET)),
                                                      (int32_t)pixmap_meta_read_u32(meta, pixmap_plane_offset(1, PIXMAP_PLANE_PITCH_OFFSET)),
                                                      pixmap_meta_read_u32(meta, PIXMAP_META_FOURCC_OFFSET));

    AL_PixMapMetaData_AddPlane(result,
                               (int32_t)pixmap_meta_read_u32(meta, pixmap_plane_offset(2, PIXMAP_PLANE_CHUNKIDX_OFFSET)),
                               (int32_t)pixmap_meta_read_u32(meta, pixmap_plane_offset(2, PIXMAP_PLANE_OFFSET_OFFSET)),
                               (int32_t)pixmap_meta_read_u32(meta, pixmap_plane_offset(2, PIXMAP_PLANE_PITCH_OFFSET)), 2);
    AL_PixMapMetaData_AddPlane(result,
                               (int32_t)pixmap_meta_read_u32(meta, pixmap_plane_offset(3, PIXMAP_PLANE_CHUNKIDX_OFFSET)),
                               (int32_t)pixmap_meta_read_u32(meta, pixmap_plane_offset(3, PIXMAP_PLANE_OFFSET_OFFSET)),
                               (int32_t)pixmap_meta_read_u32(meta, pixmap_plane_offset(3, PIXMAP_PLANE_PITCH_OFFSET)), 3);
    return result;
}

static void *SrcMeta_Clone(void *arg1)
{
    return AL_PixMapMetaData_Clone_0x396b4(arg1);
}

int32_t AL_PixMapMetaData_GetOffsetY(PixMapMetaData *arg1)
{
    int32_t result = (int32_t)pixmap_meta_read_u32(arg1, 0x18);

    if ((int32_t)pixmap_meta_read_u32(arg1, 0x24) >= result)
        return result;

    __assert("pMeta->tPlanes[AL_PLANE_Y].iOffset <= pMeta->tPlanes[AL_PLANE_UV].iOffset",
             "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/BufferPixMapMeta.c",
             0x49, "AL_PixMapMetaData_GetOffsetY");
}

int32_t AL_PixMapMetaData_GetOffsetUV(PixMapMetaData *arg1)
{
    int32_t v0 = (int32_t)pixmap_meta_read_u32(arg1, 0x24);

    if (v0 >= (int32_t)(pixmap_meta_read_u32(arg1, 0x1c) * pixmap_meta_read_u32(arg1, PIXMAP_META_HEIGHT_OFFSET)))
        return v0;

    if (AL_IsTiled(pixmap_meta_read_u32(arg1, PIXMAP_META_FOURCC_OFFSET)) != 0) {
        int32_t v0_2 = (int32_t)pixmap_meta_read_u32(arg1, 0x24);
        int32_t v1_4 = (int32_t)(pixmap_meta_read_u32(arg1, 0x1c) * pixmap_meta_read_u32(arg1, PIXMAP_META_HEIGHT_OFFSET));

        if (v1_4 < 0)
            v1_4 += 3;

        if (v0_2 >= (v1_4 >> 2))
            return v0_2;
    }

    __assert("pMeta->tPlanes[AL_PLANE_Y].iPitch * pMeta->tDim.iHeight <= pMeta->tPlanes[AL_PLANE_UV].iOffset || (AL_IsTiled(pMeta->tFourCC) && (pMeta->tPlanes[AL_PLANE_Y].iPitch * pMeta->tDim.iHeight / 4 <= pMeta->tPlanes[AL_PLANE_UV].iOffset))",
             "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/BufferPixMapMeta.c",
             0x51, "AL_PixMapMetaData_GetOffsetUV");
}

int32_t AL_PixMapMetaData_GetLumaSize(PixMapMetaData *arg1)
{
    if (AL_IsTiled(pixmap_meta_read_u32(arg1, PIXMAP_META_FOURCC_OFFSET)) == 0)
        return (int32_t)(pixmap_meta_read_u32(arg1, 0x1c) * pixmap_meta_read_u32(arg1, PIXMAP_META_HEIGHT_OFFSET));

    {
        int32_t v0_4 = (int32_t)(pixmap_meta_read_u32(arg1, 0x1c) * pixmap_meta_read_u32(arg1, PIXMAP_META_HEIGHT_OFFSET));

        if (v0_4 < 0)
            v0_4 += 3;

        return v0_4 >> 2;
    }
}

int32_t AL_PixMapMetaData_GetChromaSize(PixMapMetaData *arg1)
{
    int32_t v0 = AL_GetChromaMode(pixmap_meta_read_u32(arg1, PIXMAP_META_FOURCC_OFFSET));

    if (v0 == 0)
        return 0;

    {
        int32_t s1_1 = (int32_t)pixmap_meta_read_u32(arg1, PIXMAP_META_HEIGHT_OFFSET);

        if (v0 != 1) {
            if (AL_IsTiled(pixmap_meta_read_u32(arg1, PIXMAP_META_FOURCC_OFFSET)) != 0)
                goto label_49980;
            goto label_499ac;
        }

        s1_1 = (((uint32_t)s1_1 >> 0x1f) + s1_1) >> 1;
        if (AL_IsTiled(pixmap_meta_read_u32(arg1, PIXMAP_META_FOURCC_OFFSET)) != 0) {
label_49980:
            {
                int32_t v0_4 = s1_1 * (int32_t)pixmap_meta_read_u32(arg1, 0x28);

                if (v0_4 < 0)
                    v0_4 += 3;

                return v0_4 >> 2;
            }
        }

label_499ac:
        if (AL_IsSemiPlanar(pixmap_meta_read_u32(arg1, PIXMAP_META_FOURCC_OFFSET)) != 0)
            return s1_1 * (int32_t)pixmap_meta_read_u32(arg1, 0x28);

        return (s1_1 * (int32_t)pixmap_meta_read_u32(arg1, 0x28)) << 1;
    }
}

void AddPlanesToMeta(PixMapMetaData *arg1, int32_t arg2, int32_t *arg3, int32_t arg4)
{
    int32_t *s0_1;
    int32_t s1_1;

    if (arg4 <= 0)
        return;

    s0_1 = arg3;
    s1_1 = 0;
    do {
        s1_1 += 1;
        AL_PixMapMetaData_AddPlane(arg1, arg2, s0_1[1], s0_1[2], *s0_1);
        s0_1 = &s0_1[4];
    } while (arg4 != s1_1);
}

AL_TBuffer *AL_PixMapBuffer_Create(AL_TAllocator *arg1, void *arg2, int32_t arg3, int32_t arg4, uint32_t arg5)
{
    AL_TBuffer *result = AL_Buffer_CreateEmpty(arg1, arg2);

    if (result == NULL)
        return NULL;

    {
        PixMapMetaData *v0 = AL_PixMapMetaData_CreateEmpty(arg5);

        if (v0 != NULL) {
            if (AL_Buffer_AddMetaData(result, v0) != 0) {
                pixmap_meta_write_u32(v0, PIXMAP_META_WIDTH_OFFSET, (uint32_t)arg3);
                pixmap_meta_write_u32(v0, PIXMAP_META_HEIGHT_OFFSET, (uint32_t)arg4);
                return result;
            }

            ((PixMapDestroyFunc)pixmap_meta_read_ptr(v0, PIXMAP_META_DESTROY_OFFSET))(v0);
        }
    }

    AL_Buffer_Destroy(result);
    return NULL;
}

int32_t AL_PixMapBuffer_Allocate_And_AddPlanes(AL_TBuffer *arg1, int32_t arg2, char arg3, uint32_t arg4, int32_t *arg5, int32_t arg6, int32_t arg7)
{
    PixMapMetaData *v0 = (PixMapMetaData *)AL_Buffer_GetMetaData(arg1, 0);

    if (v0 != NULL) {
        int32_t v0_2 = AL_Buffer_AllocateChunk(arg1, arg2, (char)(uint8_t)arg3, arg4, arg7);

        if (v0_2 != -1) {
            AddPlanesToMeta(v0, v0_2, arg5, arg6);
            return 1;
        }
    }

    return 0;
}

int32_t AL_PixMapBuffer_AddPlanes(AL_TBuffer *arg1, void *arg2, int32_t arg3, char arg4, uint32_t arg5, int32_t *arg6, int32_t arg7)
{
    PixMapMetaData *v0 = (PixMapMetaData *)AL_Buffer_GetMetaData(arg1, 0);

    if (v0 != NULL) {
        int32_t v0_2 = AL_Buffer_AddChunk(arg1, arg2, arg3, (uint8_t)arg4, arg5);

        if (v0_2 != -1) {
            AddPlanesToMeta(v0, v0_2, arg6, arg7);
            return 1;
        }
    }

    return 0;
}

int32_t AL_PixMapBuffer_GetPlaneAddress(AL_TBuffer *arg1, int32_t arg2)
{
    PixMapMetaData *v0 = (PixMapMetaData *)AL_Buffer_GetMetaData(arg1, 0);

    if (v0 != NULL) {
        uint32_t s0_3 = (uint32_t)(arg2 * 0x0c);
        void *v0_1 = AL_Buffer_GetDataChunk(arg1, (int32_t)pixmap_meta_read_u32(v0, s0_3 + 0x14));

        if (v0_1 != NULL)
            return (int32_t)((uintptr_t)v0_1 + pixmap_meta_read_u32(v0, s0_3 + 0x18));
    }

    return 0;
}

uint32_t AL_PixMapBuffer_GetPlanePhysicalAddress(AL_TBuffer *arg1, int32_t arg2)
{
    PixMapMetaData *v0 = (PixMapMetaData *)AL_Buffer_GetMetaData(arg1, 0);

    if (v0 != NULL) {
        uint32_t s0_3 = (uint32_t)(arg2 * 0x0c);
        uint32_t v0_1 = AL_Buffer_GetPhysicalAddressChunk(arg1, (int32_t)pixmap_meta_read_u32(v0, s0_3 + 0x14));

        if (v0_1 != 0)
            return v0_1 + pixmap_meta_read_u32(v0, s0_3 + 0x18);
    }

    return 0;
}

int32_t AL_PixMapBuffer_GetPlanePitch(AL_TBuffer *arg1, int32_t arg2)
{
    PixMapMetaData *v0 = (PixMapMetaData *)AL_Buffer_GetMetaData(arg1, 0);

    if (v0 != NULL) {
        uint32_t s0_3 = (uint32_t)(arg2 * 0x0c);

        if ((int32_t)pixmap_meta_read_u32(v0, s0_3 + 0x14) != -1)
            return (int32_t)pixmap_meta_read_u32(v0, s0_3 + 0x1c);
    }

    return 0;
}

PixMapDimension *AL_PixMapBuffer_GetDimension(PixMapDimension *arg1, AL_TBuffer *arg2)
{
    PixMapMetaData *v0_1 = (PixMapMetaData *)AL_Buffer_GetMetaData(arg2, 0);

    if (v0_1 == NULL) {
        arg1->iWidth = 0;
        arg1->iHeight = 0;
        return arg1;
    }

    arg1->iWidth = (int32_t)pixmap_meta_read_u32(v0_1, PIXMAP_META_WIDTH_OFFSET);
    arg1->iHeight = (int32_t)pixmap_meta_read_u32(v0_1, PIXMAP_META_HEIGHT_OFFSET);
    return arg1;
}

int32_t AL_PixMapBuffer_SetDimension(AL_TBuffer *arg1, int32_t arg2, int32_t arg3)
{
    PixMapMetaData *v0 = (PixMapMetaData *)AL_Buffer_GetMetaData(arg1, 0);

    if (v0 == NULL)
        return 0;

    pixmap_meta_write_u32(v0, PIXMAP_META_WIDTH_OFFSET, (uint32_t)arg2);
    pixmap_meta_write_u32(v0, PIXMAP_META_HEIGHT_OFFSET, (uint32_t)arg3);
    return 1;
}

uint32_t AL_PixMapBuffer_GetFourCC(AL_TBuffer *arg1)
{
    PixMapMetaData *v0 = (PixMapMetaData *)AL_Buffer_GetMetaData(arg1, 0);

    if (v0 == NULL)
        return 0;

    return pixmap_meta_read_u32(v0, PIXMAP_META_FOURCC_OFFSET);
}

int32_t AL_PixMapBuffer_SetFourCC(AL_TBuffer *arg1, uint32_t arg2)
{
    PixMapMetaData *v0 = (PixMapMetaData *)AL_Buffer_GetMetaData(arg1, 0);

    if (v0 == NULL)
        return 0;

    pixmap_meta_write_u32(v0, PIXMAP_META_FOURCC_OFFSET, arg2);
    return 1;
}

int32_t AL_PixMapBuffer_GetPlaneChunkIdx(AL_TBuffer *arg1, int32_t arg2)
{
    PixMapMetaData *v0 = (PixMapMetaData *)AL_Buffer_GetMetaData(arg1, 0);

    if (v0 == NULL)
        return -1;

    return *(int32_t *)((uint8_t *)v0 + (uint32_t)(arg2 * 0x0c) + 0x14);
}
