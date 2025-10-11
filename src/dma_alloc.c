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

/* Global state */
static int g_mem_fd = -1;
static pthread_mutex_t g_dma_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_dma_initialized = 0;

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
    
    /* Try to open /dev/mem for physical memory access */
    g_mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (g_mem_fd < 0) {
        LOG_DMA("Failed to open /dev/mem: %s", strerror(errno));
        LOG_DMA("Trying /dev/jz-dma...");
        
        /* Try Ingenic-specific DMA device */
        g_mem_fd = open("/dev/jz-dma", O_RDWR);
        if (g_mem_fd < 0) {
            LOG_DMA("Failed to open /dev/jz-dma: %s", strerror(errno));
            pthread_mutex_unlock(&g_dma_mutex);
            return -1;
        }
    }
    
    g_dma_initialized = 1;
    pthread_mutex_unlock(&g_dma_mutex);
    
    LOG_DMA("Initialized (fd=%d)", g_mem_fd);
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
    
    /* Try to allocate via ioctl first */
    mem_alloc_req_t req;
    memset(&req, 0, sizeof(req));
    req.size = size;
    req.align = 4096;  /* Page alignment */
    req.flags = 0;
    
    if (ioctl(g_mem_fd, IOCTL_MEM_ALLOC, &req) == 0) {
        /* Success - got physical address */
        buf->phys_addr = req.phys_addr;
        
        /* Map physical memory to virtual address */
        buf->virt_addr = mmap(NULL, size, PROT_READ | PROT_WRITE, 
                             MAP_SHARED, g_mem_fd, buf->phys_addr);
        
        if (buf->virt_addr == MAP_FAILED) {
            LOG_DMA("Alloc: mmap failed: %s", strerror(errno));
            
            /* Free physical memory */
            ioctl(g_mem_fd, IOCTL_MEM_FREE, &req);
            free(buf);
            return -1;
        }
        
        LOG_DMA("Alloc: %s size=%d phys=0x%x virt=%p (via ioctl)", 
                name, size, buf->phys_addr, buf->virt_addr);
        
        /* Store buffer info in name for IMP_Get_Info */
        /* This is a hack - in real implementation, we'd have a global registry */
        memcpy(name, buf, sizeof(DMABuffer));
        
        return 0;
    }
    
    /* ioctl failed - fall back to regular allocation */
    LOG_DMA("Alloc: ioctl failed, using malloc fallback");
    
    /* Allocate aligned memory */
    if (posix_memalign(&buf->virt_addr, 4096, size) != 0) {
        LOG_DMA("Alloc: posix_memalign failed");
        free(buf);
        return -1;
    }
    
    /* For fallback, physical address is same as virtual (not real DMA) */
    buf->phys_addr = (uint32_t)(uintptr_t)buf->virt_addr;
    
    LOG_DMA("Alloc: %s size=%d virt=%p (fallback)", name, size, buf->virt_addr);
    
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
    
    /* For now, just use regular allocation */
    /* TODO: Implement actual pool-based allocation */
    int ret = IMP_Alloc(name, size, tag);
    
    if (ret == 0) {
        /* Set pool ID in buffer */
        DMABuffer *buf = (DMABuffer*)name;
        buf->pool_id = pool_id;
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
    
    /* TODO: Look up buffer by physical address */
    /* For now, we can't free without the buffer structure */
    /* In real implementation, we'd have a global registry */
    
    mem_alloc_req_t req;
    memset(&req, 0, sizeof(req));
    req.phys_addr = phys_addr;
    
    if (g_mem_fd >= 0) {
        ioctl(g_mem_fd, IOCTL_MEM_FREE, &req);
    }
    
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
    
    /* TODO: Look up buffer by physical address */
    /* For now, we can't retrieve info without a registry */
    
    LOG_DMA("Get_Info: phys=0x%x (not implemented)", phys_addr);
    
    return -1;
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
    
    if (g_mem_fd < 0) {
        return -1;
    }
    
    mem_alloc_req_t req;
    memset(&req, 0, sizeof(req));
    req.phys_addr = phys_addr;
    req.size = size;
    
    if (ioctl(g_mem_fd, IOCTL_MEM_FLUSH, &req) < 0) {
        LOG_DMA("Flush_Cache: ioctl failed: %s", strerror(errno));
        return -1;
    }
    
    return 0;
}

