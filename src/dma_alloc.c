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

#define LOG_DMA(fmt, ...) fprintf(stderr, "[DMA] " fmt "\n", ##__VA_ARGS__)

/* DMA buffer structure - based on decompilation */
/* Size: 0x94 bytes (148 bytes) */
typedef struct {
    char name[96];              /* 0x00-0x5f: Buffer name */
    char tag[32];               /* 0x60-0x7f: Tag */
    void *virt_addr;            /* 0x80: Virtual address */
    uint32_t phys_addr;         /* 0x84: Physical address */
    uint32_t size;              /* 0x88: Buffer size */
    uint32_t flags;             /* 0x8c: Flags */
    uint32_t pool_id;           /* 0x90: Pool ID */
} DMABuffer;

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
static DMABuffer *g_buffer_registry[MAX_DMA_BUFFERS] = {NULL};
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

/**
 * Register buffer in global registry
 */
static int register_buffer(DMABuffer *buf) {
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
static void unregister_buffer(DMABuffer *buf) {
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
static DMABuffer* lookup_buffer_by_phys(uint32_t phys_addr) {
    pthread_mutex_lock(&g_registry_mutex);

    for (int i = 0; i < MAX_DMA_BUFFERS; i++) {
        if (g_buffer_registry[i] != NULL &&
            g_buffer_registry[i]->phys_addr == phys_addr) {
            DMABuffer *buf = g_buffer_registry[i];
            pthread_mutex_unlock(&g_registry_mutex);
            return buf;
        }
    }

    pthread_mutex_unlock(&g_registry_mutex);
    return NULL;
}

/**
 * Initialize DMA allocator
 */
static int dma_init(void) {
    if (g_dma_initialized) {
        return 0;
    }

    pthread_mutex_lock(&g_dma_mutex);

    if (g_dma_initialized) {
        pthread_mutex_unlock(&g_dma_mutex);
        return 0;
    }

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

/**
 * IMP_Alloc - Allocate DMA buffer
 * Based on decompilation at 0x16d2c
 */
int IMP_Alloc(char *name, int size, char *tag) {
    if (name == NULL || size <= 0) {
        LOG_DMA("Alloc: invalid parameters");
        return -1;
    }

    if (dma_init() < 0) {
        LOG_DMA("Alloc: initialization failed");
        return -1;
    }

    /* Allocate DMA buffer structure */
    DMABuffer *buf = (DMABuffer*)calloc(1, sizeof(DMABuffer));
    if (buf == NULL) {
        LOG_DMA("Alloc: calloc failed");
        return -1;
    }

    /* Copy name and tag */
    strncpy(buf->name, name, sizeof(buf->name) - 1);
    if (tag != NULL) {
        strncpy(buf->tag, tag, sizeof(buf->tag) - 1);
    }
    buf->size = size;

    /* Try kernel DMA path first if available */
    if (g_rmem_supported && g_mem_fd >= 0) {
        if (g_is_rmem && g_rmem_virt_base != NULL) {
            /* Bump allocate from /dev/rmem mapping */
            size_t align = 4096;
            size_t off = (g_rmem_offset + (align - 1)) & ~(align - 1);
            if (off + (size_t)size <= g_rmem_size) {
                buf->virt_addr = (void*)((uintptr_t)g_rmem_virt_base + off);
                buf->phys_addr = g_rmem_base_phys + (uint32_t)off;
                buf->flags |= 0x2; /* RMEM_BUMP */
                g_rmem_offset = off + (size_t)size;
                LOG_DMA("Alloc: %s size=%d phys=0x%x virt=%p (rmem off=0x%zx)",
                        buf->name[0] ? buf->name : "(unnamed)", size, buf->phys_addr, buf->virt_addr, off);
            } else {
                LOG_DMA("Alloc: /dev/rmem out of memory (requested=%d, used=%zu/%zu); falling back",
                        size, g_rmem_offset, g_rmem_size);
            }
        } else {
            /* Generic kernel allocator path (if present) */
            mem_alloc_req_t req;
            memset(&req, 0, sizeof(req));
            req.size = (uint32_t)size;
            req.align = 4096;
            if (ioctl(g_mem_fd, IOCTL_MEM_ALLOC, &req) == 0 && req.phys_addr != 0) {
                void *virt = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, g_mem_fd, (off_t)req.phys_addr);
                if (virt != MAP_FAILED) {
                    buf->virt_addr = virt;
                    buf->phys_addr = req.phys_addr;
                    buf->flags |= 0x1; /* KERNEL_MMAP */
                    LOG_DMA("Alloc: %s size=%d phys=0x%x virt=%p (kernel)", buf->name[0] ? buf->name : "(unnamed)", size, buf->phys_addr, buf->virt_addr);
                } else {
                    LOG_DMA("Alloc: mmap failed for phys=0x%x (%s); falling back", req.phys_addr, strerror(errno));
                }
            } else {
                LOG_DMA("Alloc: IOCTL_MEM_ALLOC failed (%s); falling back", strerror(errno));
            }
        }
    }

    /* Fallback allocation if kernel path unavailable or failed */
    if (buf->virt_addr == NULL) {
        if (posix_memalign(&buf->virt_addr, 4096, size) != 0) {
            LOG_DMA("Alloc: posix_memalign failed");
            free(buf);
            return -1;
        }
        /* For fallback, physical address is same as virtual (not real DMA) */
        buf->phys_addr = (uint32_t)(uintptr_t)buf->virt_addr;
        LOG_DMA("Alloc: %s size=%d virt=%p (fallback)", buf->name[0] ? buf->name : "(unnamed)", size, buf->virt_addr);
    }

    /* Register buffer in global registry */
    if (register_buffer(buf) < 0) {
        LOG_DMA("Alloc: failed to register buffer");
        free(buf->virt_addr);
        free(buf);
        return -1;
    }

    /* Store buffer info */
    memcpy(name, buf, sizeof(DMABuffer));

    return 0;
}

/**
 * IMP_PoolAlloc - Allocate from a specific pool
 * Based on decompilation
 */
int IMP_PoolAlloc(int pool_id, char *name, int size, char *tag) {
    if (name == NULL || size <= 0) {
        return -1;
    }

    LOG_DMA("PoolAlloc: pool=%d name=%s size=%d", pool_id, name, size);

    /* Pool-based allocation uses the same underlying allocator as IMP_Alloc
     * The pool_id is just metadata to track which pool the buffer belongs to
     * This allows the system to manage buffers by pool for cleanup/tracking */
    int ret = IMP_Alloc(name, size, tag);

    if (ret == 0) {
        /* Set pool ID in buffer for tracking */
        DMABuffer *buf = (DMABuffer*)name;
        buf->pool_id = pool_id;
        LOG_DMA("PoolAlloc: assigned to pool %d", pool_id);
    }

    return ret;
}

/**
 * IMP_Free - Free DMA buffer
 * Based on decompilation
 */
int IMP_Free(uint32_t phys_addr) {
    if (phys_addr == 0) {
        return -1;
    }

    LOG_DMA("Free: phys=0x%x", phys_addr);

    /* Look up buffer by physical address */
    DMABuffer *buf = lookup_buffer_by_phys(phys_addr);
    if (buf == NULL) {
        LOG_DMA("Free: buffer not found in registry");
        return 0;
    }

    /* Unmap/free virtual memory */
    if (buf->virt_addr != NULL) {
        if ((buf->flags & 0x2) && g_is_rmem) {
            /* RMEM bump allocations are not individually freed (no-op) */
        } else if ((buf->flags & 0x1) && g_rmem_supported && g_mem_fd >= 0) {
            /* Kernel-mapped buffer with allocator ioctls */
            munmap(buf->virt_addr, buf->size);
            /* Attempt to free in kernel */
            mem_alloc_req_t req;
            memset(&req, 0, sizeof(req));
            req.size = buf->size;
            req.phys_addr = buf->phys_addr;
            if (ioctl(g_mem_fd, IOCTL_MEM_FREE, &req) != 0) {
                LOG_DMA("Free: IOCTL_MEM_FREE failed for phys=0x%x (%s)", buf->phys_addr, strerror(errno));
            }
        } else {
            /* Fallback allocation - just free */
            free(buf->virt_addr);
        }
    }

    /* Unregister and free buffer structure */
    unregister_buffer(buf);
    free(buf);

    LOG_DMA("Free: freed buffer phys=0x%x", phys_addr);
    return 0;
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
    DMABuffer *buf = lookup_buffer_by_phys(phys_addr);
    if (buf == NULL) {
        LOG_DMA("Get_Info: buffer not found for phys=0x%x", phys_addr);
        return -1;
    }

    /* Copy buffer info to output */
    memcpy(info_out, buf, sizeof(DMABuffer));

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

    /* No-op: kernel cache flush ioctl not used in fallback mode */
    return 0;
}

