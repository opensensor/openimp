#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/imp_alloc.h"

extern char _gp;

int32_t IMP_Log_Get_Option(void);
int32_t imp_log_fun(int32_t level, int32_t option, int32_t type, ...);

/* forward decl, ported by T<N> later */
int32_t IMP_Alloc(void *arg1, int32_t arg2, int32_t arg3);
/* forward decl, ported by T<N> later */
int32_t IMP_Free(void *arg1, int32_t arg2);
/* forward decl, ported by T<N> later */
int32_t alloc_kmem_get_paddr(int32_t arg1);
/* forward decl, ported by T<N> later */
int32_t alloc_kmem_get_vaddr(int32_t arg1);
/* forward decl, ported by T<N> later */
int32_t alloc_kmem_flush_cache(int32_t arg1, int32_t arg2, int32_t arg3);
/* forward decl, ported by T<N> later */
int32_t mempool_continuous_init(int32_t arg1, int32_t arg2, int32_t arg3);
/* forward decl, ported by T<N> later */
int32_t mempool_continuous_deinit(int32_t arg1);
/* forward decl, ported by T<N> later */
int32_t mempool_continuous_alloc(int32_t arg1, int32_t arg2, char *arg3);
/* forward decl, ported by T<N> later */
void mempool_continuous_free(int32_t arg1, int32_t arg2);
/* forward decl, ported by T<N> later */
int32_t mempool_continuous_dump(int32_t arg1);
/* forward decl, ported by T<N> later */
int32_t mempool_continuous_dump_to_file(int32_t arg1);

typedef struct AllocInfoNode {
    struct AllocInfoNode *next;
    char owner[0x20];
    uint32_t vaddr;
    uint32_t paddr;
    int32_t length;
    int32_t ref_cnt;
    int32_t mem_attr;
} AllocInfoNode;

#if UINTPTR_MAX == 0xffffffffu
_Static_assert(sizeof(AllocInfoNode) == 0x38, "AllocInfoNode size");
#endif

typedef int32_t (*PoolAllocCallback)(void *arg1, int32_t arg2, char *arg3);
typedef void (*PoolFreeCallback)(void *arg1, int32_t arg2);
typedef int32_t (*PoolDumpCallback)(void *arg1);
typedef int32_t (*PoolDumpToFileCallback)(void *arg1);
typedef int32_t (*PoolGetPaddrCallback)(int32_t arg1);
typedef int32_t (*PoolGetVaddrCallback)(int32_t arg1);
typedef int32_t (*PoolFlushCacheCallback)(int32_t arg1, int32_t arg2, int32_t arg3);
typedef int32_t (*PoolDeinitCallback)(int32_t arg1);
typedef int32_t (*PoolInitCallback)(int32_t arg1, int32_t arg2, int32_t arg3);

#define MEMPOOL_MANAGER_COUNT 0x20

#define MEMPOOL_BASE_VADDR_OFFSET 0x04
#define MEMPOOL_BASE_PADDR_OFFSET 0x08
#define MEMPOOL_SIZE_OFFSET 0x0c
#define MEMPOOL_ALLOC_ATTR_OFFSET 0x10
#define MEMPOOL_DEINIT_ARG_OFFSET 0x14
#define MEMPOOL_DEINIT_FN_OFFSET 0x18
#define MEMPOOL_LIST_HEAD_OFFSET 0x1c
#define MEMPOOL_GET_PADDR_OFFSET 0x84
#define MEMPOOL_GET_VADDR_OFFSET 0x88
#define MEMPOOL_FLUSH_CACHE_OFFSET 0x8c
#define MEMPOOL_ALLOC_INIT_OFFSET 0xb0
#define MEMPOOL_ALLOC_ALLOC_OFFSET 0xb4
#define MEMPOOL_ALLOC_FREE_OFFSET 0xb8
#define MEMPOOL_ALLOC_DEINIT_OFFSET 0xbc
#define MEMPOOL_ALLOC_DUMP_OFFSET 0xc0
#define MEMPOOL_ALLOC_DUMP_TO_FILE_OFFSET 0xc4
#define MEMPOOL_NODE_COUNT_OFFSET 0xc8
#define MEMPOOL_POOL_ALLOC_OFFSET 0xcc
#define MEMPOOL_POOL_FREE_OFFSET 0xd4
#define MEMPOOL_POOL_DUMP_TO_FILE_OFFSET 0xe8
#define MEMPOOL_ALLOC_CTX_OFFSET 0xec

static void *g_manager[MEMPOOL_MANAGER_COUNT];

static inline int32_t read_i32(const void *base, uint32_t offset)
{
    return *(int32_t *)((char *)base + offset);
}

static inline void write_i32(void *base, uint32_t offset, int32_t value)
{
    *(int32_t *)((char *)base + offset) = value;
}

static inline void *read_ptr(const void *base, uint32_t offset)
{
    return *(void **)((char *)base + offset);
}

static inline void write_ptr(void *base, uint32_t offset, const void *value)
{
    *(const void **)((char *)base + offset) = value;
}

int32_t pool_alloc_mem(void *arg1, int32_t arg2, char *arg3)
{
    if (arg1 == NULL) {
        return 0;
    }

    return ((PoolAllocCallback)read_ptr(arg1, 0x98))(read_ptr(arg1, 0xd0), arg2, arg3);
}

int32_t dumpMemPoolToFile(void **arg1)
{
    char filename[0x40];
    FILE *stream;
    AllocInfoNode *i;
    PoolDumpToFileCallback callback;

    if (arg1 == NULL) {
        return -1;
    }

    callback = (PoolDumpToFileCallback)arg1[0x2a];
    i = (AllocInfoNode *)arg1;
    if (callback != NULL) {
        callback(arg1[0x34]);
    }

    strcpy(filename, "/tmp/mempool_manager_pool");
    memset(filename + 0x1a, 0, 0x26);
    sprintf(filename + 0x19, "%d", *(((int32_t *)i) - 0x1c));
    if (access(filename, 0) < 0) {
        return 0;
    }

    stream = fopen(filename, "w");
    if (stream == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "JZ_MEMPOOL",
            "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool.c", 0x83,
            "alloc_dump_to_file", "Open %s failed\n", filename);
        return 0;
    }

    do {
        char str[0x1fc];
        int32_t written;

        memset(str, 0, sizeof(str));
        written = sprintf(str, "\ninfo->n_info = %p\n", (void *)i->next);
        written += sprintf(str + written, "info->owner = %s\n", i->owner);
        written += sprintf(str + written, "info->vaddr = 0x%08x\n", i->vaddr);
        written += sprintf(str + written, "info->paddr = 0x%08x\n", i->paddr);
        written += sprintf(str + written, "info->length = %d\n", i->length);
        written += sprintf(str + written, "info->ref_cnt = %d\n", i->ref_cnt);
        written += sprintf(str + written, "info->mem_attr = %d\n", i->mem_attr);
        i = i->next;
        fwrite(str, written, 1, stream);
    } while (i != NULL);

    fclose(stream);
    return 0;
}

void pool_free_mem(void ***arg1, int32_t arg2)
{
    AllocInfoNode *node;
    AllocInfoNode *next;

    if (arg1 == NULL) {
        return;
    }

    ((PoolFreeCallback)arg1[0x27])(arg1[0x34], arg2);
    node = (AllocInfoNode *)*arg1;
    if (node != NULL) {
        next = node->next;
        if (arg2 == (int32_t)node->vaddr) {
            *arg1 = (void **)next;
            free(node);
            return;
        }

        if (next != NULL) {
            while (arg2 != (int32_t)next->vaddr) {
                next = next->next;
                if (next == NULL) {
                    return;
                }

                node = node->next;
            }

            node->next = next->next;
            free(next);
            return;
        }
    } else if (arg2 == (int32_t)((int32_t *)arg1)[9]) {
        *arg1 = NULL;
        free(arg1);
    }
}

void *request_mempool(int32_t arg1, int32_t arg2, int32_t arg3)
{
    void *result;
    void *result_1;
    void *alloc_attr;

    result = g_manager[arg1];
    if (result != NULL) {
        fwrite("already request\n", 1, 0x10, stderr);
        return NULL;
    }

    result_1 = calloc(0xf0, 1);
    if (result_1 == NULL) {
        fprintf(stderr, "malloc error: %d\n", 0xbb);
        return result;
    }

    *(int32_t *)result_1 = arg1;
    write_i32(result_1, MEMPOOL_SIZE_OFFSET, arg2);
    alloc_attr = calloc(0x94, 1);
    write_ptr(result_1, MEMPOOL_ALLOC_ATTR_OFFSET, alloc_attr);
    if (alloc_attr == NULL) {
        fprintf(stderr, "malloc error: %d\n", 0xc6);
        free(result_1);
        return result;
    }

    if (IMP_Alloc(alloc_attr, arg2, arg3) < 0) {
        fwrite("IMP Alloc error\n", 1, 0x10, stderr);
        free(read_ptr(result_1, MEMPOOL_ALLOC_ATTR_OFFSET));
        free(result_1);
        return result;
    }

    g_manager[arg1] = result_1;
    result = result_1;
    /* IMP_Alloc writes base vaddr/paddr at offsets 0x80/0x84 in the 0x94-byte block. */
    write_i32(result_1, MEMPOOL_BASE_VADDR_OFFSET, read_i32(alloc_attr, 0x80));
    write_i32(result_1, MEMPOOL_BASE_PADDR_OFFSET, read_i32(alloc_attr, 0x84));
    return result;
}

int32_t release_mempool(void *arg1)
{
    PoolDeinitCallback callback;

    if (arg1 == NULL) {
        return 0;
    }

    callback = (PoolDeinitCallback)read_ptr(arg1, MEMPOOL_DEINIT_FN_OFFSET);
    if (callback != NULL) {
        callback(read_i32(arg1, MEMPOOL_DEINIT_ARG_OFFSET));
    }

    free(arg1);
    return 0;
}

int32_t get_mempool(int32_t arg1)
{
    int32_t result;

    result = (int32_t)(intptr_t)g_manager[arg1];
    if (result == 0) {
        fprintf(stderr, "should request poolId, poolId: %d\n", arg1);
    }

    return result;
}

int32_t IMP_MemPool_InitPool(int32_t arg1, int32_t arg2, int32_t arg3)
{
    void *pool;
    int32_t init_result;

    pool = request_mempool(arg1, arg2, arg3);
    if (pool == NULL) {
        return -1;
    }

    write_ptr(pool, MEMPOOL_POOL_ALLOC_OFFSET, pool_alloc_mem);
    write_ptr(pool, MEMPOOL_POOL_FREE_OFFSET, pool_free_mem);
    write_ptr(pool, MEMPOOL_POOL_DUMP_TO_FILE_OFFSET, dumpMemPoolToFile);
    write_ptr(pool, MEMPOOL_ALLOC_INIT_OFFSET, mempool_continuous_init);
    write_ptr(pool, MEMPOOL_ALLOC_DEINIT_OFFSET, mempool_continuous_deinit);
    write_ptr(pool, MEMPOOL_ALLOC_ALLOC_OFFSET, mempool_continuous_alloc);
    write_ptr(pool, MEMPOOL_ALLOC_FREE_OFFSET, mempool_continuous_free);
    write_ptr(pool, MEMPOOL_ALLOC_DUMP_OFFSET, mempool_continuous_dump);
    write_ptr(pool, MEMPOOL_ALLOC_DUMP_TO_FILE_OFFSET, mempool_continuous_dump_to_file);
    write_ptr(pool, MEMPOOL_GET_PADDR_OFFSET, alloc_kmem_get_paddr);
    write_ptr(pool, MEMPOOL_GET_VADDR_OFFSET, alloc_kmem_get_vaddr);
    write_ptr(pool, MEMPOOL_FLUSH_CACHE_OFFSET, alloc_kmem_flush_cache);
    init_result = ((PoolInitCallback)read_ptr(pool, MEMPOOL_ALLOC_INIT_OFFSET))(
        read_i32(pool, MEMPOOL_BASE_VADDR_OFFSET), read_i32(pool, MEMPOOL_SIZE_OFFSET), arg3);
    write_i32(pool, MEMPOOL_ALLOC_CTX_OFFSET, init_result);
    if (init_result != 0) {
        return 0;
    }

    release_mempool(pool);
    return -1;
}

int32_t IMP_MemPool_GetById(int32_t arg1)
{
    return get_mempool(arg1);
}

int32_t IMP_MemPool_Release(int32_t arg1)
{
    void *pool;
    int32_t result;
    PoolDeinitCallback callback;

    pool = g_manager[arg1];
    if (pool == NULL) {
        fprintf(stderr, "pool null error, poolId: %d\n", arg1);
        return -1;
    }

    result = read_i32(pool, MEMPOOL_NODE_COUNT_OFFSET);
    if (result != 0) {
        fprintf(stderr, "error, you need free continus block rmem, poolId: %d, node num: %d\n",
            arg1, result);
        return -1;
    }

    callback = (PoolDeinitCallback)read_ptr(pool, MEMPOOL_ALLOC_DEINIT_OFFSET);
    if (callback == NULL) {
        return -1;
    }

    if (callback(read_i32(pool, MEMPOOL_ALLOC_CTX_OFFSET)) < 0) {
        fprintf(stderr, "continus block deinit error,  poolId: %d, node num: %d\n",
            arg1, read_i32(pool, MEMPOOL_NODE_COUNT_OFFSET));
        return -1;
    }

    IMP_Free(read_ptr(pool, MEMPOOL_ALLOC_ATTR_OFFSET), read_i32(pool, MEMPOOL_BASE_VADDR_OFFSET));
    release_mempool(pool);
    return result;
}

int32_t IMP_PoolAlloc_Get_Attr(uint32_t *arg1, uint32_t *arg2)
{
    (void)arg1;
    (void)arg2;
    return 0;
}

int32_t IMP_PoolVirt_to_Phys(void *arg1)
{
    (void)arg1;
    return 0;
}

int32_t IMP_PoolPhys_to_Virt(uint32_t arg1)
{
    (void)arg1;
    return 0;
}

int32_t IMP_PoolAlloc_Set_Attr(uint32_t arg1, uint32_t arg2)
{
    (void)arg1;
    (void)arg2;
    return 0;
}

int32_t IMP_PoolAlloc_Dump(int32_t arg1)
{
    int32_t result;
    PoolDumpCallback callback;

    result = (int32_t)(intptr_t)g_manager[arg1];
    if (result == 0) {
        fprintf(stderr, "should request poolId, poolId: %d\n", arg1);
        return imp_log_fun(6, IMP_Log_Get_Option(), 2, "JZ_MEMPOOL",
            "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool.c", 0x1f6,
            "IMP_PoolAlloc_Dump", "get jz mempool error: %d\n", arg1);
    }

    callback = (PoolDumpCallback)read_ptr((void *)(intptr_t)result, MEMPOOL_POOL_DUMP_TO_FILE_OFFSET);
    if (callback == NULL) {
        return result;
    }

    return callback((char *)(intptr_t)result + MEMPOOL_LIST_HEAD_OFFSET);
}

int32_t IMP_PoolAlloc(int32_t arg1, IMP_Alloc_Info *arg2, int32_t arg3, char *arg4)
{
    void *pool;
    AllocInfoNode *node;
    AllocInfoNode **tail;
    int32_t vaddr;
    int32_t paddr;
    PoolGetPaddrCallback get_paddr;
    PoolFreeCallback free_callback;

    pool = g_manager[arg1];
    if (pool == NULL) {
        fprintf(stderr, "should request poolId, poolId: %d\n", arg1);
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "JZ_MEMPOOL",
            "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool.c", 0x17f,
            "IMP_PoolAlloc", "get Jz mempool error\n");
        return -1;
    }

    imp_log_fun(3, IMP_Log_Get_Option(), 2, "JZ_MEMPOOL",
        "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool.c", 0x184,
        "IMP_PoolAlloc", "poolId: %d================> %s, %d, owner: %s, size: %d\n",
        arg1, "IMP_PoolAlloc", 0x184, arg4, arg3);
    if (read_ptr(pool, MEMPOOL_POOL_ALLOC_OFFSET) == NULL) {
        return -1;
    }

    vaddr = ((PoolAllocCallback)read_ptr(pool, MEMPOOL_POOL_ALLOC_OFFSET))(
        (char *)pool + MEMPOOL_LIST_HEAD_OFFSET, arg3, arg4);
    if (vaddr == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "JZ_MEMPOOL",
            "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool.c", 0x18c,
            "IMP_PoolAlloc", "g_alloc.alloc_mem failed\n", arg1, "IMP_PoolAlloc", 0x184, arg4,
            arg3);
        return -1;
    }

    node = (AllocInfoNode *)calloc(1, sizeof(AllocInfoNode));
    if (node == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "JZ_MEMPOOL",
            "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool.c", 0x193,
            "IMP_PoolAlloc", "Alloc Info struct malloc failed\n");
        free_callback = (PoolFreeCallback)read_ptr(pool, MEMPOOL_POOL_FREE_OFFSET);
        if (free_callback != NULL) {
            free_callback((char *)pool + MEMPOOL_LIST_HEAD_OFFSET, vaddr);
        }

        return -1;
    }

    node->next = NULL;
    strncpy(node->owner, arg4, 0x1f);
    get_paddr = (PoolGetPaddrCallback)read_ptr(pool, MEMPOOL_GET_PADDR_OFFSET);
    node->vaddr = (uint32_t)vaddr;
    node->length = arg3;
    node->ref_cnt = 1;
    if (get_paddr == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "JZ_MEMPOOL",
            "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool.c", 0x1a0,
            "IMP_PoolAlloc", "%s mem_alloc->get_paddr is NULL\n", "IMP_PoolAlloc");
        free(node);
        free_callback = (PoolFreeCallback)read_ptr(pool, MEMPOOL_POOL_FREE_OFFSET);
        if (free_callback != NULL) {
            free_callback((char *)pool + MEMPOOL_LIST_HEAD_OFFSET, vaddr);
        }

        return -1;
    }

    paddr = get_paddr(vaddr);
    node->paddr = (uint32_t)paddr;
    tail = (AllocInfoNode **)((char *)pool + MEMPOOL_LIST_HEAD_OFFSET);
    while (*tail != NULL) {
        tail = &(*tail)->next;
    }

    *tail = node;
    strncpy((char *)arg2 + 0x60, node->owner, 0x1f);
    *(int32_t *)((char *)arg2 + 0x80) = (int32_t)node->vaddr;
    *(int32_t *)((char *)arg2 + 0x84) = paddr;
    *(int32_t *)((char *)arg2 + 0x88) = node->length;
    *(int32_t *)((char *)arg2 + 0x8c) = node->ref_cnt;
    *(int32_t *)((char *)arg2 + 0x90) = node->mem_attr;
    write_i32(pool, MEMPOOL_NODE_COUNT_OFFSET, read_i32(pool, MEMPOOL_NODE_COUNT_OFFSET) + 1);
    IMP_PoolAlloc_Dump(arg1);
    return 0;
}

int32_t IMP_PoolFree(int32_t arg1, IMP_Alloc_Info *arg2, int32_t arg3)
{
    void *pool;
    PoolFreeCallback callback;
    int32_t ref_cnt;

    if (arg2 == NULL || arg3 == 0) {
        return imp_log_fun(6, IMP_Log_Get_Option(), 2, "JZ_MEMPOOL",
            "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool.c", 0x1c3,
            "IMP_PoolFree", "IMPAlloc *alloc == NULL or ptr == NULL\n");
    }

    pool = g_manager[arg1];
    if (pool == NULL) {
        fprintf(stderr, "should request poolId, poolId: %d\n", arg1);
        return imp_log_fun(6, IMP_Log_Get_Option(), 2, "JZ_MEMPOOL",
            "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool.c", 0x1c9,
            "IMP_PoolFree", "get Jz mempool error\n");
    }

    imp_log_fun(3, IMP_Log_Get_Option(), 2, "JZ_MEMPOOL",
        "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool.c", 0x1ce,
        "IMP_PoolFree", "poolId: %d================> %s, %d, owner: %s, size: %d\n",
        arg1, "IMP_PoolFree", 0x1ce, (char *)arg2 + 0x60, *(int32_t *)((char *)arg2 + 0x88));
    callback = (PoolFreeCallback)read_ptr(pool, MEMPOOL_POOL_FREE_OFFSET);
    if (callback != NULL) {
        callback((char *)pool + MEMPOOL_LIST_HEAD_OFFSET, arg3);
    }

    ref_cnt = *(int32_t *)((char *)arg2 + 0x8c) - 1;
    *(int32_t *)((char *)arg2 + 0x8c) = ref_cnt;
    if (ref_cnt == 0) {
        *(int32_t *)((char *)arg2 + 0x80) = 0;
        *(int32_t *)((char *)arg2 + 0x84) = 0;
    }

    write_i32(pool, MEMPOOL_NODE_COUNT_OFFSET, read_i32(pool, MEMPOOL_NODE_COUNT_OFFSET) - 1);
    return IMP_PoolAlloc_Dump(arg1);
}

int32_t IMP_PoolFlushCache(void *arg1, uint32_t arg2)
{
    (void)arg1;
    (void)arg2;
    return 0;
}
