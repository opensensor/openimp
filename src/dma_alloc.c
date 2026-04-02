/**
 * DMA Allocator Implementation
 * Based on reverse engineering of libimp.so v1.1.6
 * Integrates with kernel driver for physical memory allocation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <pthread.h>

#include "dma_alloc.h"
#include "imp_log_int.h"

/* Best-effort check that a pointer looks like a C string within max bytes */
static int is_probably_cstring(const char *p, size_t max)
{
    if (!p) return 0;
    for (size_t i = 0; i < max; ++i) {
        unsigned char c = (unsigned char)p[i];
        if (c == '\0') return 1;
        if (c < 0x09 || (c > 0x0d && c < 0x20)) return 0; /* control chars */
    }
    return 0;
}

/* Internal DMA buffer record. Exported/OEM-facing info uses IMPDMABufferInfo. */
typedef struct {
    char name[96];              /* 0x00-0x5f: Buffer name */
    char tag[32];               /* 0x60-0x7f: Tag */
    void *virt_addr;            /* Native virtual address */
    uint32_t phys_addr;         /* 0x84: Physical address */
    uint32_t size;              /* 0x88: Buffer size */
    uint32_t flags;             /* 0x8c: Flags */
    uint32_t pool_id;           /* 0x90: Pool ID */
} DMABufferRecord;

/* ioctl commands for memory allocation */
#define IOCTL_MEM_ALLOC     0xc0104d01  /* Allocate memory */
#define IOCTL_MEM_FREE      0xc0104d02  /* Free memory */
#define IOCTL_MEM_GET_PHY   0xc0104d03  /* Get physical address */
#define IOCTL_MEM_FLUSH     0xc0104d04  /* Flush cache */

/* Memory allocation request structure */
typedef struct {
    uint32_t size;              /* Requested size */
    uint32_t align;             /* Alignment */
    uint32_t phys_addr;         /* Output: physical address */
    uint32_t flags;             /* Allocation flags */
} mem_alloc_req_t;

/* Global buffer registry */
#define MAX_DMA_BUFFERS 128
static DMABufferRecord *g_buffer_registry[MAX_DMA_BUFFERS] = {NULL};
static pthread_mutex_t g_registry_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Global state */
static int g_mem_fd = -1;
static pthread_mutex_t g_dma_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_dma_initialized = 0;
static int g_rmem_supported = 0;  /* set to 1 when /dev/rmem accepts our ioctls */

/* RMEM-specific globals (for /dev/rmem bump allocator) */
static int g_is_rmem = 0;
static uint32_t g_rmem_base_phys = 0x06300000; /* 29MB region base (from RE notes) */
static size_t g_rmem_size = (size_t)(29 * 1024 * 1024);
static void *g_rmem_virt_base = NULL;
static size_t g_rmem_offset = 0; /* bump pointer */
static char g_chosen_dev_path[64] = {0};

static const uint32_t kCompatMaxAllocSize = 256u * 1024u * 1024u;

int IMP_FlushCache(void *virt_addr, uint32_t size);

/**
 * Register buffer in global registry
 */
static int register_buffer(DMABufferRecord *buf) {
    pthread_mutex_lock(&g_registry_mutex);

    for (int i = 0; i < MAX_DMA_BUFFERS; i++) {
        if (g_buffer_registry[i] == NULL) {
            g_buffer_registry[i] = buf;
            pthread_mutex_unlock(&g_registry_mutex);
            return 0;
        }
    }

    pthread_mutex_unlock(&g_registry_mutex);
    LOG_DMA("register_buffer: registry full");
    return -1;
}

/**
 * Unregister buffer from global registry
 */
static void unregister_buffer(DMABufferRecord *buf) {
    pthread_mutex_lock(&g_registry_mutex);

    for (int i = 0; i < MAX_DMA_BUFFERS; i++) {
        if (g_buffer_registry[i] == buf) {
            g_buffer_registry[i] = NULL;
            break;
        }
    }

    pthread_mutex_unlock(&g_registry_mutex);
}

/**
 * Lookup buffer by physical address
 */
static DMABufferRecord* lookup_buffer_by_phys(uint32_t phys_addr) {
    pthread_mutex_lock(&g_registry_mutex);

    for (int i = 0; i < MAX_DMA_BUFFERS; i++) {
        if (g_buffer_registry[i] != NULL &&
            g_buffer_registry[i]->phys_addr == phys_addr) {
            DMABufferRecord *buf = g_buffer_registry[i];
            pthread_mutex_unlock(&g_registry_mutex);
            return buf;
        }
    }

    pthread_mutex_unlock(&g_registry_mutex);
    return NULL;
}

static DMABufferRecord* lookup_buffer_containing_phys(uint32_t phys_addr, uint32_t *offset_out) {
    pthread_mutex_lock(&g_registry_mutex);

    for (int i = 0; i < MAX_DMA_BUFFERS; i++) {
        DMABufferRecord *buf = g_buffer_registry[i];
        if (buf == NULL) {
            continue;
        }
        if (phys_addr >= buf->phys_addr && phys_addr < buf->phys_addr + buf->size) {
            if (offset_out != NULL) {
                *offset_out = phys_addr - buf->phys_addr;
            }
            pthread_mutex_unlock(&g_registry_mutex);
            return buf;
        }
    }

    pthread_mutex_unlock(&g_registry_mutex);
    return NULL;
}

static DMABufferRecord* lookup_buffer_containing_virt(const void *virt_addr, uint32_t *offset_out) {
    uintptr_t virt = (uintptr_t)virt_addr;

    pthread_mutex_lock(&g_registry_mutex);

    for (int i = 0; i < MAX_DMA_BUFFERS; i++) {
        DMABufferRecord *buf = g_buffer_registry[i];
        if (buf == NULL || buf->virt_addr == NULL) {
            continue;
        }

        uintptr_t base = (uintptr_t)buf->virt_addr;
        uintptr_t end = base + buf->size;
        if (virt >= base && virt < end) {
            if (offset_out != NULL) {
                *offset_out = (uint32_t)(virt - base);
            }
            pthread_mutex_unlock(&g_registry_mutex);
            return buf;
        }
    }

    pthread_mutex_unlock(&g_registry_mutex);
    return NULL;
}

static void fill_dma_info(IMPDMABufferInfo *info_out, const DMABufferRecord *buf)
{
    if (info_out == NULL || buf == NULL) {
        return;
    }

    memset(info_out, 0, sizeof(*info_out));
    memcpy(info_out->name, buf->name, sizeof(info_out->name));
    memcpy(info_out->tag, buf->tag, sizeof(info_out->tag));
    info_out->virt_addr = (uint32_t)(uintptr_t)buf->virt_addr;
    info_out->phys_addr = buf->phys_addr;
    info_out->size = buf->size;
    info_out->flags = buf->flags;
    info_out->pool_id = buf->pool_id;
}

static int size_arg_is_reasonable(intptr_t size)
{
    return size > 0 && (uint64_t)size <= kCompatMaxAllocSize;
}

static int tag_arg_looks_valid(const char *tag)
{
    return tag != NULL && is_probably_cstring(tag, 32);
}

static int looks_like_pointer_style_alloc(uintptr_t arg1, intptr_t arg2, const char *arg3)
{
    if (arg1 == 0 || arg1 > kCompatMaxAllocSize) {
        return 0;
    }
    if (!size_arg_is_reasonable(arg2)) {
        return 1;
    }
    return !tag_arg_looks_valid(arg3);
}

static int looks_like_pointer_style_pool_alloc(uintptr_t arg2, intptr_t arg3, const char *arg4)
{
    if (arg2 == 0 || arg2 > kCompatMaxAllocSize) {
        return 0;
    }
    if (!size_arg_is_reasonable(arg3)) {
        return 1;
    }
    return !tag_arg_looks_valid(arg4);
}

/**
 * Initialize DMA allocator
 */
static void maybe_override_rmem_from_env(void)
{
    const char *env_base = getenv("OPENIMP_RMEM_BASE");
    const char *env_size = getenv("OPENIMP_RMEM_SIZE");
    if (env_base && *env_base) {
        char *endp = NULL;
        unsigned long v = strtoul(env_base, &endp, 0);
        if (endp && endp != env_base) {
            g_rmem_base_phys = (uint32_t)v;
            LOG_DMA("RMEM base overridden by env: 0x%08x", g_rmem_base_phys);
        }
    }
    if (env_size && *env_size) {
        char *endp = NULL;
        unsigned long v = strtoul(env_size, &endp, 0);
        if (endp && endp != env_size && v > 0) {
            g_rmem_size = (size_t)v;
            LOG_DMA("RMEM size overridden by env: %zu (0x%zx)", g_rmem_size, g_rmem_size);
        }
    }
}

static int dma_init(void) {
    if (g_dma_initialized) {
        return 0;
    }

    pthread_mutex_lock(&g_dma_mutex);

    if (g_dma_initialized) {
        pthread_mutex_unlock(&g_dma_mutex);
        return 0;
    }

    /* Allow environment to override RMEM base/size to match device-specific layout */
    maybe_override_rmem_from_env();

    /* Check if RMEM should be disabled (T31 workaround for kernel bugs) */
    const char *disable_rmem_env = getenv("OPENIMP_DISABLE_RMEM");
    int disable_rmem = (disable_rmem_env && disable_rmem_env[0] == '1');

    const char *candidates[] = {
        "/dev/rmem",
        "/dev/memalloc",
        "/dev/ion-ingenic",
        "/dev/ion",
        "/dev/jz-mm",
        "/dev/mmem",
        "/dev/isp-mem",
        "/dev/vicbuf",
        NULL
    };

    g_mem_fd = -1;
    for (int i = 0; candidates[i] != NULL; i++) {
        /* Skip /dev/rmem if disabled via environment variable */
        if (disable_rmem && strcmp(candidates[i], "/dev/rmem") == 0) {
            LOG_DMA("DMA init: skipping /dev/rmem (OPENIMP_DISABLE_RMEM=1)");
            continue;
        }

        int fd = open(candidates[i], O_RDWR | O_CLOEXEC);
        if (fd >= 0) {
            g_mem_fd = fd;
            g_rmem_supported = 1;
            strncpy(g_chosen_dev_path, candidates[i], sizeof(g_chosen_dev_path) - 1);
            LOG_DMA("DMA init: using %s", candidates[i]);
            break;
        }
    }

    if (g_mem_fd < 0) {
        g_rmem_supported = 0;
        LOG_DMA("DMA init: no DMA device found; using malloc fallback only");
    } else if (strcmp(g_chosen_dev_path, "/dev/rmem") == 0) {
        /* rmem requires mmap; set up a single mapping and bump allocator */
        void *base = mmap(NULL, g_rmem_size, PROT_READ | PROT_WRITE, MAP_SHARED, g_mem_fd, 0);
        if (base == MAP_FAILED) {
            LOG_DMA("DMA init: mmap of /dev/rmem failed (%s); will fall back per-alloc", strerror(errno));
        } else {
            g_rmem_virt_base = base;
            g_is_rmem = 1;
            LOG_DMA("DMA init: /dev/rmem mapped at %p size=%zu base_phys=0x%08x", base, g_rmem_size, g_rmem_base_phys);
        }
    }

    g_dma_initialized = 1;
    pthread_mutex_unlock(&g_dma_mutex);
    return 0;
}

static int dma_free_buffer(DMABufferRecord *buf)
{
    if (buf == NULL) {
        return -1;
    }

    LOG_DMA("Free: phys=0x%x virt=%p", buf->phys_addr, buf->virt_addr);

    if (buf->virt_addr != NULL) {
        if ((buf->flags & 0x2) && g_is_rmem) {
            /* RMEM bump allocations are not individually freed (no-op) */
        } else if ((buf->flags & 0x1) && g_rmem_supported && g_mem_fd >= 0) {
            mem_alloc_req_t req;
            munmap(buf->virt_addr, buf->size);
            memset(&req, 0, sizeof(req));
            req.size = buf->size;
            req.phys_addr = buf->phys_addr;
            if (ioctl(g_mem_fd, IOCTL_MEM_FREE, &req) != 0) {
                LOG_DMA("Free: IOCTL_MEM_FREE failed for phys=0x%x (%s)", buf->phys_addr, strerror(errno));
            }
        } else {
            free(buf->virt_addr);
        }
    }

    unregister_buffer(buf);
    free(buf);
    return 0;
}

static int dma_alloc_descriptor_internal(int pool_id, IMPDMABufferInfo *info_out, int size, const char *tag)
{
    if (info_out == NULL || size <= 0) {
        LOG_DMA("Alloc: invalid parameters");
        return -1;
    }

    if (dma_init() < 0) {
        LOG_DMA("Alloc: initialization failed");
        return -1;
    }

    DMABufferRecord *buf = (DMABufferRecord*)calloc(1, sizeof(DMABufferRecord));
    if (buf == NULL) {
        LOG_DMA("Alloc: calloc failed");
        return -1;
    }

    if (tag_arg_looks_valid(tag)) {
        strncpy(buf->tag, tag, sizeof(buf->tag) - 1);
    } else {
        buf->tag[0] = '\0';
    }
    buf->name[0] = '\0';
    buf->size = (uint32_t)size;
    buf->pool_id = (pool_id >= 0) ? (uint32_t)pool_id : 0;

    if (g_rmem_supported && g_mem_fd >= 0) {
        if (g_is_rmem && g_rmem_virt_base != NULL) {
            size_t align = 4096;
            size_t off = (g_rmem_offset + (align - 1)) & ~(align - 1);
            if (off + (size_t)size <= g_rmem_size) {
                buf->virt_addr = (void*)((uintptr_t)g_rmem_virt_base + off);
                buf->phys_addr = g_rmem_base_phys + (uint32_t)off;
                buf->flags |= 0x2;
                g_rmem_offset = off + (size_t)size;
                LOG_DMA("Alloc: %s size=%d phys=0x%x virt=%p (rmem off=0x%zx)",
                        buf->name[0] ? buf->name : "(unnamed)", size, buf->phys_addr, buf->virt_addr, off);
            } else {
                LOG_DMA("Alloc: /dev/rmem out of memory (requested=%d, used=%zu/%zu); falling back",
                        size, g_rmem_offset, g_rmem_size);
            }
        } else {
            mem_alloc_req_t req;
            memset(&req, 0, sizeof(req));
            req.size = (uint32_t)size;
            req.align = 4096;
            if (ioctl(g_mem_fd, IOCTL_MEM_ALLOC, &req) == 0 && req.phys_addr != 0) {
                void *virt = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, g_mem_fd, (off_t)req.phys_addr);
                if (virt != MAP_FAILED) {
                    buf->virt_addr = virt;
                    buf->phys_addr = req.phys_addr;
                    buf->flags |= 0x1;
                    LOG_DMA("Alloc: %s size=%d phys=0x%x virt=%p (kernel)",
                            buf->name[0] ? buf->name : "(unnamed)", size, buf->phys_addr, buf->virt_addr);
                } else {
                    LOG_DMA("Alloc: mmap failed for phys=0x%x (%s); falling back", req.phys_addr, strerror(errno));
                }
            } else {
                LOG_DMA("Alloc: IOCTL_MEM_ALLOC failed (%s); falling back", strerror(errno));
            }
        }
    }

    if (buf->virt_addr == NULL) {
        if (posix_memalign(&buf->virt_addr, 4096, (size_t)size) != 0) {
            LOG_DMA("Alloc: posix_memalign failed");
            free(buf);
            return -1;
        }
        buf->phys_addr = (uint32_t)(uintptr_t)buf->virt_addr;
        LOG_DMA("Alloc: %s size=%d virt=%p (fallback)", buf->name[0] ? buf->name : "(unnamed)", size, buf->virt_addr);
    }

    if (register_buffer(buf) < 0) {
        LOG_DMA("Alloc: failed to register buffer");
        if ((buf->flags & 0x2) && g_is_rmem) {
            /* RMEM bump allocations cannot be individually rolled back. */
        } else if ((buf->flags & 0x1) && g_rmem_supported && g_mem_fd >= 0) {
            munmap(buf->virt_addr, buf->size);
        } else {
            free(buf->virt_addr);
        }
        free(buf);
        return -1;
    }

    fill_dma_info(info_out, buf);
    return 0;
}

uintptr_t IMP_Alloc(void *name_or_size, intptr_t size, char *tag) {
    uintptr_t arg1 = (uintptr_t)name_or_size;

    if (looks_like_pointer_style_alloc(arg1, size, tag)) {
        IMPDMABufferInfo info;
        memset(&info, 0, sizeof(info));
        if (dma_alloc_descriptor_internal(-1, &info, (int)arg1, "compat") != 0) {
            return (uintptr_t)NULL;
        }
        LOG_DMA("Alloc compat: size=%u virt=0x%08x phys=0x%08x", (unsigned)arg1, info.virt_addr, info.phys_addr);
        return (uintptr_t)info.virt_addr;
    }

    return (uintptr_t)dma_alloc_descriptor_internal(-1, (IMPDMABufferInfo*)name_or_size, (int)size, tag);
}

uintptr_t IMP_PoolAlloc(int pool_id, void *name_or_size, intptr_t size, char *tag) {
    uintptr_t arg2 = (uintptr_t)name_or_size;

    if (looks_like_pointer_style_pool_alloc(arg2, size, tag)) {
        IMPDMABufferInfo info;
        memset(&info, 0, sizeof(info));
        if (dma_alloc_descriptor_internal(pool_id, &info, (int)arg2, "compat_pool") != 0) {
            return (uintptr_t)NULL;
        }
        LOG_DMA("PoolAlloc compat: pool=%d size=%u virt=0x%08x phys=0x%08x", pool_id, (unsigned)arg2, info.virt_addr, info.phys_addr);
        return (uintptr_t)info.virt_addr;
    }

    return (uintptr_t)dma_alloc_descriptor_internal(pool_id, (IMPDMABufferInfo*)name_or_size, (int)size, tag);
}

int IMP_Free(uintptr_t phys_or_virt_addr) {
    DMABufferRecord *buf = NULL;

    if (phys_or_virt_addr == 0) {
        return -1;
    }

    if (phys_or_virt_addr <= UINT32_MAX) {
        buf = lookup_buffer_by_phys((uint32_t)phys_or_virt_addr);
    }
    if (buf == NULL) {
        buf = lookup_buffer_containing_virt((const void*)phys_or_virt_addr, NULL);
    }
    if (buf == NULL && phys_or_virt_addr <= UINT32_MAX) {
        buf = lookup_buffer_containing_phys((uint32_t)phys_or_virt_addr, NULL);
    }
    if (buf == NULL) {
        LOG_DMA("Free: buffer not found in registry (arg=%p)", (void*)phys_or_virt_addr);
        return 0;
    }

    return dma_free_buffer(buf);
}

/**
 * IMP_Get_Info - Get buffer information
 * Based on decompilation at 0x16754
 */
int IMP_Get_Info(void *info_out, uint32_t phys_addr) {
    if (info_out == NULL || phys_addr == 0) {
        return -1;
    }

    /* Look up buffer by physical address */
    DMABufferRecord *buf = lookup_buffer_by_phys(phys_addr);
    if (buf == NULL) {
        LOG_DMA("Get_Info: buffer not found for phys=0x%x", phys_addr);
        return -1;
    }

    fill_dma_info((IMPDMABufferInfo*)info_out, buf);

    LOG_DMA("Get_Info: phys=0x%x, virt=%p, size=%u",
            phys_addr, buf->virt_addr, buf->size);

    return 0;
}

/**
 * IMP_FrameSource_GetPool - Get pool ID for a channel
 * Based on decompilation
 */
int IMP_FrameSource_GetPool(int chn) {
    (void)chn;

    /* Return -1 to indicate no pool available */
    /* This will cause VBM to use IMP_Alloc instead of IMP_PoolAlloc */
    return -1;
}

/**
 * Flush cache for DMA buffer
 */
int IMP_Flush_Cache(uint32_t phys_addr, uint32_t size) {
    if (phys_addr == 0 || size == 0) {
        return -1;
    }

    return IMP_FlushCache(DMA_PhysToVirt(phys_addr), size);
}

int DMA_AllocDescriptor(IMPDMABufferInfo *info_out, int size, const char *tag)
{
    return dma_alloc_descriptor_internal(-1, info_out, size, tag);
}

int DMA_PoolAllocDescriptor(int pool_id, IMPDMABufferInfo *info_out, int size, const char *tag)
{
    return dma_alloc_descriptor_internal(pool_id, info_out, size, tag);
}

int DMA_FreePhys(uint32_t phys_addr)
{
    return IMP_Free((uintptr_t)phys_addr);
}

void *DMA_PhysToVirt(uint32_t phys_addr)
{
    uint32_t offset = 0;
    DMABufferRecord *buf;

    if (phys_addr == 0) {
        return NULL;
    }

    buf = lookup_buffer_containing_phys(phys_addr, &offset);
    if (buf != NULL && buf->virt_addr != NULL) {
        return (void*)((uintptr_t)buf->virt_addr + offset);
    }

    if (dma_init() == 0 && g_is_rmem && g_rmem_virt_base != NULL &&
        phys_addr >= g_rmem_base_phys &&
        phys_addr < g_rmem_base_phys + g_rmem_size) {
        return (void*)((uintptr_t)g_rmem_virt_base + (phys_addr - g_rmem_base_phys));
    }

    return NULL;
}

uint32_t DMA_VirtToPhys(const void *virt_addr)
{
    uint32_t offset = 0;
    DMABufferRecord *buf;
    uintptr_t virt;

    if (virt_addr == NULL) {
        return 0;
    }

    buf = lookup_buffer_containing_virt(virt_addr, &offset);
    if (buf != NULL) {
        return buf->phys_addr + offset;
    }

    virt = (uintptr_t)virt_addr;
    if (dma_init() == 0 && g_is_rmem && g_rmem_virt_base != NULL) {
        uintptr_t base = (uintptr_t)g_rmem_virt_base;
        uintptr_t end = base + g_rmem_size;
        if (virt >= base && virt < end) {
            return g_rmem_base_phys + (uint32_t)(virt - base);
        }
    }

    return (uint32_t)virt;
}



int DMA_Get_RMEM_Base(uint32_t *base_phys_out)
{
    if (base_phys_out == NULL)
        return -1;
    if (g_is_rmem && g_rmem_virt_base != NULL) {
        *base_phys_out = g_rmem_base_phys;
        return 0;
    }
    return -1;
}

int DMA_Is_RMEM(void)
{
    return (g_is_rmem && g_rmem_virt_base != NULL) ? 1 : 0;
}

/* IMP_FlushCache - flush CPU cache for DMA coherency
 * Stub: on MIPS with coherent DMA, this is often a no-op */
int IMP_FlushCache(void *virt_addr, uint32_t size) {
    if (virt_addr == NULL || size == 0) {
        return -1;
    }

    if (dma_init() == 0 && g_rmem_supported && !g_is_rmem && g_mem_fd >= 0) {
        uint32_t phys_addr = DMA_VirtToPhys(virt_addr);
        if (phys_addr != 0) {
            mem_alloc_req_t req;
            memset(&req, 0, sizeof(req));
            req.phys_addr = phys_addr;
            req.size = size;
            if (ioctl(g_mem_fd, IOCTL_MEM_FLUSH, &req) != 0) {
                LOG_DMA("FlushCache: ioctl failed for phys=0x%x (%s)", phys_addr, strerror(errno));
            }
        }
    }

    return 0;
}

void *IMP_Phys_to_Virt(uint32_t phys_addr)
{
    return DMA_PhysToVirt(phys_addr);
}

uint32_t IMP_Virt_to_Phys(void *virt_addr)
{
    return DMA_VirtToPhys(virt_addr);
}

void IMP_PoolFree(void *ptr)
{
    (void)IMP_Free((uintptr_t)ptr);
}

int IMP_PoolFlushCache(void *ptr, uint32_t size)
{
    return IMP_FlushCache(ptr, size);
}

void *IMP_PoolPhys_to_Virt(uint32_t phys_addr)
{
    return DMA_PhysToVirt(phys_addr);
}

uint32_t IMP_PoolVirt_to_Phys(void *virt_addr)
{
    return DMA_VirtToPhys(virt_addr);
}

/* OEM-compatible rmem cache flush: ioctl(rmem_fd, 0xc00c7200, {vaddr, size, dir})
 * This is the exact ioctl the stock libimp's alloc_kmem_flush_cache() uses.
 * The rmem kernel module handles MIPS cache maintenance correctly for DMA. */
#define RMEM_IOCTL_FLUSH_CACHE  0xc00c7200  /* _IOWR('r', 0, 12-byte-struct) */

struct rmem_flush_info {
    unsigned int addr;   /* virtual address */
    unsigned int size;   /* size in bytes */
    unsigned int dir;    /* 1=WBACK, 2=INV */
};

int DMA_RmemFlushCache(void *virt_addr, uint32_t size, int dir)
{
    if (!virt_addr || size == 0) return -1;
    if (dma_init() != 0 || g_mem_fd < 0) return -1;

    /* OEM: ioctl(rmem_fd, 0xc00c7200, {vaddr, size, dir}) */
    struct rmem_flush_info info;
    info.addr = (unsigned int)(uintptr_t)virt_addr;
    info.size = size;
    info.dir = (unsigned int)dir;
    return ioctl(g_mem_fd, RMEM_IOCTL_FLUSH_CACHE, &info);
}
