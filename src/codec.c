/**
 * AL_Codec Implementation
 * Based on reverse engineering of libimp.so v1.1.6
 * Decompiled from addresses 0x7950c (Create), 0x7a180 (Destroy), etc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <imp/imp_encoder.h>
#include <imp/imp_system.h>
#include "fifo.h"
#include "hw_encoder.h"

#include "al_avpu.h"
#include "device_pool.h"
#include "dma_alloc.h"
#include "imp_log_int.h"
#include "kernel_interface.h"
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/syscall.h> /* for SYS_ioctl */

static inline int avpu_sys_ioctl(int fd, unsigned long cmd, void *arg)
{
    /* Bypass libc varargs to avoid any ABI/shim issues on MIPS o32 */
    return (int)syscall(SYS_ioctl, fd, cmd, arg);
}

static inline uint32_t clamp_qp_u32(uint32_t qp)
{
    return qp > 51u ? 51u : qp;
}


/* AVPU ioctl definitions (OEM parity - direct driver access) */
#ifndef AVPU_IOC_MAGIC
#define AVPU_IOC_MAGIC 'q'
#endif

/* Minimal AVPU alloc structures for GET_DMA_MMAP ioctl */
struct avpu_dma_info {
    uint32_t fd;      /* offset for mmap (page-aligned) */
    uint32_t size;    /* requested/returned size */
    uint32_t phy_addr;/* physical address of allocation */
} __attribute__((aligned(4)));

#define GET_DMA_MMAP      _IOWR(AVPU_IOC_MAGIC, 26, struct avpu_dma_info)
#define GET_DMA_FD        _IOWR(AVPU_IOC_MAGIC, 13, struct avpu_dma_info)
#define GET_DMA_PHY       _IOWR(AVPU_IOC_MAGIC, 18, struct avpu_dma_info)
#define AL_CMD_UNBLOCK_CHANNEL _IO(AVPU_IOC_MAGIC, 1)

/* Cache flush ioctl and struct (mirrors driver) */
#ifndef JZ_CMD_FLUSH_CACHE
#define JZ_CMD_FLUSH_CACHE _IOWR(AVPU_IOC_MAGIC, 14, int)
#endif
struct avpu_flush_cache_info {
    unsigned int addr;
    unsigned int len;
    unsigned int dir; /* 1=WBACK(DMA_TO_DEVICE), 2=INV(DMA_FROM_DEVICE), 3=WBACK_INV */
};

/* IMPORTANT: Must be 4-byte aligned for MIPS kernel */
struct avpu_reg {
    unsigned int id;
    unsigned int value;
} __attribute__((aligned(4)));

#define AL_CMD_IP_WRITE_REG    _IOWR(AVPU_IOC_MAGIC, 10, struct avpu_reg)
#define AL_CMD_IP_READ_REG     _IOWR(AVPU_IOC_MAGIC, 11, struct avpu_reg)
#define AL_CMD_IP_WAIT_IRQ     _IOWR(AVPU_IOC_MAGIC, 12, int)

/* AVPU register offsets (from driver and BN decompilation) */
#define AVPU_BASE_OFFSET       0x8000
#define AVPU_INTERRUPT_MASK    (AVPU_BASE_OFFSET + 0x14)
#define AVPU_INTERRUPT         (AVPU_BASE_OFFSET + 0x18)
#define AVPU_REG_TOP_CTRL      (AVPU_BASE_OFFSET + 0x54)
#define AVPU_REG_MISC_CTRL     (AVPU_BASE_OFFSET + 0x10)
#define AVPU_REG_SRC_PUSH      (AVPU_BASE_OFFSET + 0x84)
#define AVPU_REG_STRM_PUSH     (AVPU_BASE_OFFSET + 0x94)
#define AVPU_REG_CL_ADDR       (AVPU_BASE_OFFSET + 0x3E0)
#define AVPU_REG_CL_PUSH       (AVPU_BASE_OFFSET + 0x3E4)
#define AVPU_REG_ENC_EN_A      (AVPU_BASE_OFFSET + 0x5F0)
#define AVPU_REG_ENC_EN_B      (AVPU_BASE_OFFSET + 0x5F4)
#define AVPU_REG_ENC_EN_C      (AVPU_BASE_OFFSET + 0x5E4)
#define AVPU_REG_AXI_ADDR_OFFSET_IP (AVPU_BASE_OFFSET + 0x1208)
#define AVPU_REG_WPP_CORE0_RESET(c) ((AVPU_BASE_OFFSET + 0x20C) + ((unsigned)(c) << 9))
#define AVPU_REG_CORE_STATUS_8230(c) ((AVPU_BASE_OFFSET + 0x230) + ((unsigned)(c) << 9))
#define AVPU_REG_CORE_STATUS_8234(c) ((AVPU_BASE_OFFSET + 0x234) + ((unsigned)(c) << 9))
#define AVPU_REG_CORE_STATUS_8238(c) ((AVPU_BASE_OFFSET + 0x238) + ((unsigned)(c) << 9))
static inline unsigned AVPU_CORE_BASE(int core) { return (AVPU_BASE_OFFSET + 0x3F0) + ((unsigned)core << 9); }
#define AVPU_REG_CORE_RESET(c)   (AVPU_CORE_BASE(c) + 0x00)
#define AVPU_REG_CORE_CLKCMD(c)  (AVPU_CORE_BASE(c) + 0x04)
#define AVPU_REG_CORE_STATUS(c)  (AVPU_CORE_BASE(c) + 0x08)

/* Cache flush via OEM-compatible /dev/rmem ioctl 0xc00c7200.
 * rmem mappings are CACHED — the AVPU reads from physical RAM, not CPU cache.
 * Without flushing, the AVPU reads stale/zeroed data → hang or corrupt output.
 *
 * OEM call chain: Rtos_FlushCacheMemory → alloc_kmem_flush_cache
 *   → ioctl(rmem_fd, 0xc00c7200, {vaddr, size, dir=1})
 *
 * CRITICAL: The previous path using the AVPU driver's JZ_CMD_FLUSH_CACHE
 * (ioctl 0xc004710e → dma_cache_sync(NULL,...)) does NOT reliably flush the
 * MIPS data cache. The rmem driver's flush is the only proven path on T31. */
/* Cache flush via OEM-compatible /dev/rmem ioctl 0xc00c7200.
 * OEM: Rtos_FlushCacheMemory → alloc_kmem_flush_cache → ioctl(rmem_fd, ...)
 * CRITICAL: The rmem mmap MUST use offset=kmem_paddr (not 0) for the
 * flush ioctl to find the correct pages. Fixed in dma_alloc.c. */
static int avpu_flush_cache(int fd, void *virt_addr, unsigned int size, unsigned int dir)
{
    (void)fd;
    return DMA_RmemFlushCache(virt_addr, size, (int)dir);
}

static int avpu_flush_dma_buf(int fd, const char *tag, const AvpuDMABuf *buf, size_t size)
{
    int ret;

    if (fd < 0 || !buf || !buf->map || size == 0) {
        return -1;
    }

    ret = avpu_flush_cache(fd, buf->map, (unsigned int)size, 1 /*WBACK*/);
    LOG_CODEC("AVPU: flush %s phys=0x%08x size=0x%08x ret=%d",
              tag ? tag : "dma", buf->phy_addr, (unsigned int)size, ret);
    return ret;
}

/* Direct ioctl helpers (OEM parity - no wrapper functions) */
static int avpu_write_reg(int fd, unsigned int off, unsigned int val)
{
    if (fd < 0 || (off & 3) != 0) return -1;

    /* Some kernel builds copy more than sizeof(struct avpu_reg). Provide slack. */
    size_t buf_sz = sizeof(struct avpu_reg) + 0x400;
    void *raw = NULL;
    if (posix_memalign(&raw, 16, buf_sz) != 0 || !raw) {
        return -1;
    }
    memset(raw, 0, buf_sz);
    struct avpu_reg *p = (struct avpu_reg*)raw;
    p->id = off;
    p->value = val;

    LOG_CODEC("AVPU write_reg: off=0x%08x val=0x%08x argp=%p", off, val, (void*)p);
    int ret = avpu_sys_ioctl(fd, AL_CMD_IP_WRITE_REG, p);
    free(raw);
    return ret;
}

static int avpu_read_reg_internal(int fd, unsigned int off, unsigned int *out, int verbose)
{
    if (fd < 0 || (off & 3) != 0) return -1;

    size_t buf_sz = sizeof(struct avpu_reg) + 0x400;
    void *raw = NULL;
    if (posix_memalign(&raw, 16, buf_sz) != 0 || !raw) {
        return -1;
    }
    memset(raw, 0, buf_sz);
    struct avpu_reg *p = (struct avpu_reg*)raw;
    p->id = off;
    p->value = 0;

    if (verbose) {
        LOG_CODEC("AVPU read_reg: off=0x%08x argp=%p", off, (void*)p);
    }
    int ret = avpu_sys_ioctl(fd, AL_CMD_IP_READ_REG, p);
    if (ret == 0 && out) *out = p->value;
    free(raw);
    return ret;
}

static int avpu_read_reg(int fd, unsigned int off, unsigned int *out)
{
    return avpu_read_reg_internal(fd, off, out, 1);
}

static int avpu_read_reg_quiet(int fd, unsigned int off, unsigned int *out)
{
    return avpu_read_reg_internal(fd, off, out, 0);
}

/* Allocate a coherent DMA buffer from AVPU driver via GET_DMA_MMAP and mmap it */
static int avpu_alloc_mmap(int fd, size_t size, AvpuDMABuf* out)
{
    if (fd < 0 || !out || size == 0) return -1;
    struct avpu_dma_info info __attribute__((aligned(4)));
    memset(&info, 0, sizeof(info));
    info.size = (uint32_t)size;
    LOG_CODEC("AVPU: about to GET_DMA_MMAP size=%zu info_ptr=%p", size, (void*)&info);
    if (ioctl(fd, GET_DMA_MMAP, &info) != 0) {
        LOG_CODEC("AVPU GET_DMA_MMAP failed: %s", strerror(errno));
        return -1;
    }

    void* map = MAP_FAILED;
    int tries = 0;
    while (tries < 2) {
        map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)info.fd);
        if (map != MAP_FAILED)
            break;
        LOG_CODEC("AVPU mmap failed off=0x%x size=%zu: %s", info.fd, size, strerror(errno));
        /* Some kernels reject offset==0; allocate another buffer to skip id 0 */
        if (errno == EINVAL && info.fd == 0 && tries == 0) {
            memset(&info, 0, sizeof(info));
            info.size = (uint32_t)size;
            LOG_CODEC("AVPU: retry GET_DMA_MMAP to avoid offset 0");
            if (ioctl(fd, GET_DMA_MMAP, &info) != 0) {
                LOG_CODEC("AVPU GET_DMA_MMAP (retry) failed: %s", strerror(errno));
                return -1;
            }
            tries++;
            continue;
        }
        return -1;
    }

    out->phy_addr = info.phy_addr;
    out->mmap_off = info.fd;
    out->dmabuf_fd = -1;
    out->map = map;
    out->size = size;
    out->from_rmem = 0;
    LOG_CODEC("AVPU: dma-mmap ok phys=0x%08x off=0x%x size=%zu map=%p", info.phy_addr, info.fd, size, map);
    return 0;
}


/* Allocate a DMA buffer via IMP_Alloc (OEM parity path) */
static int avpu_alloc_imp(size_t size, const char* tag, AvpuDMABuf* out)
{
    if (!out || size == 0) return -1;
    IMPDMABufferInfo info;
    memset(&info, 0, sizeof(info));
    if (DMA_AllocDescriptor(&info, (int)size, tag ? tag : "AVPU") != 0) {
        LOG_CODEC("AVPU: IMP_Alloc failed (size=%zu, tag=%s)", size, tag ? tag : "AVPU");
        return -1;
    }
    void* virt = (void*)(uintptr_t)info.virt_addr;
    uint32_t phys = info.phys_addr;
    if (!virt || phys == 0) {
        LOG_CODEC("AVPU: IMP_Alloc returned invalid addresses virt=%p phys=0x%08x", virt, phys);
        return -1;
    }
    out->phy_addr = phys;
    out->mmap_off = 0;
    out->dmabuf_fd = -1;
    out->map = virt;
    out->size = size;
    out->from_rmem = 1; /* prevent munmap in destroy; allocator owns lifetime */
    LOG_CODEC("AVPU: imp-alloc ok phys=0x%08x size=%zu virt=%p", phys, size, virt);
    return 0;
}

/* Re-map a DMA buffer as UNCACHED via /dev/mem.
 * The rmem cached mapping's cache flush is broken on T31.
 * /dev/mem with MAP_SHARED + O_SYNC gives an uncached mapping on MIPS. */
static void *avpu_remap_uncached(uint32_t phys_addr, size_t size)
{
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        LOG_CODEC("AVPU: /dev/mem open failed: %s", strerror(errno));
        return NULL;
    }
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
                   fd, (off_t)phys_addr);
    close(fd);
    if (p == MAP_FAILED) {
        LOG_CODEC("AVPU: /dev/mem mmap failed for phys=0x%08x size=%zu: %s",
                  phys_addr, size, strerror(errno));
        return NULL;
    }
    LOG_CODEC("AVPU: /dev/mem uncached remap OK phys=0x%08x -> virt=%p size=%zu",
              phys_addr, p, size);
    return p;
}

static uint32_t avpu_align_up_u32(uint32_t value, uint32_t alignment)
{
    if (alignment == 0)
        return value;
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static uint32_t avpu_get_nv12_luma_lines(uint32_t height)
{
    /* T31 NV12 uses 16-line luma alignment: 1080p occupies 1088 Y lines. */
    return avpu_align_up_u32(height, 16u);
}

static uint32_t avpu_get_nv12_luma_plane_size(uint32_t width, uint32_t height)
{
    return width * avpu_get_nv12_luma_lines(height);
}

static size_t avpu_get_nv12_frame_size(uint32_t width, uint32_t height)
{
    return ((size_t)avpu_get_nv12_luma_plane_size(width, height) * 3u) / 2u;
}

static size_t avpu_get_enc1_ref_region_size(uint32_t width, uint32_t height)
{
    uint32_t aligned_w = avpu_align_up_u32(width, 64u);
    uint32_t aligned_h = avpu_align_up_u32(height, 64u);
    return ((size_t)aligned_w * (size_t)aligned_h * 3u) / 2u;
}

static size_t avpu_get_enc1_map_region_size(uint32_t width, uint32_t height)
{
    uint32_t aligned_w = avpu_align_up_u32(width, 16u);
    uint32_t aligned_h = avpu_align_up_u32(height, 16u);
    uint32_t width_4k_tiles = (aligned_w + 0xFFFu) >> 12;
    uint32_t height_quads = (aligned_h + 3u) >> 2;

    /* OEM AL_GetEncoderFbcMapSize(0, w, h, 16) for 8-bit NV12:
     *   (32 * ceil(width / 4096)) * ceil(height / 4)
     * This must match avpu_get_enc1_fbc_map_pitch() which uses 0x20 (32) per
     * 4K tile. Stock CL has map_sz=0xb80=2944 for 640x360 (32*1*92). */
    return (size_t)(width_4k_tiles * 32u) * (size_t)height_quads;
}

static size_t avpu_get_enc1_mv_region_size(uint32_t width, uint32_t height)
{
    uint32_t lcu_w = (width + 15u) >> 4;
    uint32_t lcu_h = (height + 15u) >> 4;
    size_t lcu_count = (size_t)lcu_w * (size_t)lcu_h;

    /* OEM AL_GetAllocSize_MV(width, height, log2MaxCuSize=4, chromaMode=1) for the
     * current AVC/NV12 8-bit path reduces to:
     *   ((GetBlk16x16(width, height) + 0x10) << 4)
     * where GetBlk16x16 = ceil(width/16) * ceil(height/16).
     * This is much smaller than the earlier coarse estimate and affects the live
     * late-window addresses copied from the reconstructed frame-buffer layout. */
    return (lcu_count + 0x10u) << 4;
}

static uint32_t avpu_get_enc1_max_bitdepth(uint32_t format_word);

static uint32_t avpu_get_enc1_src_pitch(uint32_t width, uint32_t format_word)
{
    uint32_t max_bitdepth = avpu_get_enc1_max_bitdepth(format_word);
    uint32_t pitch = width;

    /* OEM AL_EncGetMinPitch(width, bitdepth, AL_GetSrcStorageMode(...)) uses
     * ComputeRndPitch(..., burst_alignment=16). On our current linear NV12 path
     * AL_GetSrcStorageMode() resolves to 0, so the helper reduces to a 16-byte
     * burst alignment on byte pitch, doubling only for >8-bit sources. */
    if (max_bitdepth != 8u)
        pitch <<= 1;

    return avpu_align_up_u32(pitch, 16u);
}

static uint32_t avpu_get_enc1_rec_pitch(uint32_t width, uint32_t format_word)
{
    uint32_t max_bitdepth = avpu_get_enc1_max_bitdepth(format_word);

    if (max_bitdepth < 9u)
        return ((width + 0x3fu) >> 6) << 8;

    return ((width + 0x3fu) >> 6) * 0x140u;
}

static uint32_t avpu_get_enc1_fbc_map_pitch(uint32_t width)
{
    return ((width + 0xfffu) >> 12) * 0x20u;
}

static uint32_t avpu_get_enc1_max_bitdepth(uint32_t format_word)
{
    uint32_t luma_bitdepth = format_word & 0xFu;
    uint32_t chroma_bitdepth = (format_word >> 4) & 0xFu;
    uint32_t max_bitdepth = (luma_bitdepth > chroma_bitdepth) ? luma_bitdepth : chroma_bitdepth;

    return max_bitdepth ? max_bitdepth : 8u;
}

static size_t avpu_get_enc1_comp_data_size(uint32_t width, uint32_t height, uint32_t format_word)
{
    uint32_t lcu_count = ((width + 15u) >> 4) * ((height + 15u) >> 4);
    uint32_t max_bitdepth = avpu_get_enc1_max_bitdepth(format_word);
    uint32_t chroma_mode = (format_word >> 8) & 0xFu;
    uint32_t comp_lcu_size = (16u * 16u * max_bitdepth) >> 3;

    /* OEM AL_GetAllocSize_CompData(width, height, 16, max_bitdepth,
     * format_word >> 8, 1) for the default AVC/NV12 4:2:0 8-bit path.
     * The inner AL_GetCompLcuSize adds chroma contribution per LCU and then
     * rounds with: ((size + 0x33) >> 5) << 5. cmd[0x6f] is this byte size,
     * not a DMA address. */
    if (chroma_mode == 1u)
        comp_lcu_size += (16u * 16u * max_bitdepth) >> 4;
    else if (chroma_mode == 2u)
        comp_lcu_size <<= 1;
    else if (chroma_mode == 3u)
        comp_lcu_size *= 3u;

    comp_lcu_size = avpu_align_up_u32(comp_lcu_size + 20u, 32u);
    return (size_t)lcu_count * (size_t)comp_lcu_size;
}

static uint32_t avpu_get_enc1_ep1_size(void)
{
    /* OEM AL_GetAllocSizeEP1() is fixed-size on this path. */
    return 0x6400u;
}

static uint32_t avpu_get_enc1_ep2_size(uint32_t width, uint32_t height)
{
    uint32_t blk16 = ((width + 15u) >> 4) * ((height + 15u) >> 4);

    /* OEM AVC path: AL_GetAllocSizeEP2(width, height, codec=AVC)
     * = align128(GetBlk16x16(width, height)) + 0x40. */
    return avpu_align_up_u32(blk16, 128u) + 0x40u;
}

static uint32_t avpu_get_enc1_comp_map_size(uint32_t width, uint32_t height)
{
    /* For the current AVC/NV12 8-bit path, the recovered OEM
     * AL_GetAllocSize_EncCompMap(..., 1) reduces to the same byte count as the
     * encoder FBC map-size helper already modeled by avpu_get_enc1_map_region_size(). */
    return (uint32_t)avpu_get_enc1_map_region_size(width, height);
}

static uint32_t avpu_get_enc1_wpp_size(uint32_t width, uint32_t height)
{
    uint32_t lcu_h = 16u;
    uint32_t lcu_w = (width + 15u) >> 4;
    uint32_t row_count;
    uint32_t row_groups;
    uint32_t group_bytes;

    if (lcu_w == 0u)
        return 0u;

    /* OEM AL_GetAllocSize_WPP(height, 16, lcu_w):
     *   return lcu_w * align128((ceil(ceil(height / 16) / lcu_w) << 2)) * 16;
     * recovered from Binary Ninja decompilation of AL_GetAllocSize_WPP(). */
    row_count = (height + lcu_h - 1u) / lcu_h;
    row_groups = (row_count + lcu_w - 1u) / lcu_w;
    group_bytes = avpu_align_up_u32(row_groups << 2, 128u);
    return lcu_w * group_bytes * lcu_h;
}

static size_t avpu_get_enc1_frame_buf_size(uint32_t width, uint32_t height)
{
    size_t ref_sz = avpu_get_enc1_ref_region_size(width, height);
    size_t map_sz = avpu_get_enc1_map_region_size(width, height);
    size_t mv_sz = avpu_get_enc1_mv_region_size(width, height);
    size_t total = ref_sz + map_sz + mv_sz;
    size_t nv12_sz = avpu_get_nv12_frame_size(width, height);

    if (total < nv12_sz)
        total = nv12_sz;

    return (total + 0xFFFu) & ~(size_t)0xFFFu;
}

static uint32_t avpu_default_enc1_cmd12_a8(uint32_t enc_w)
{
    /* OEM UpdateCommand at 0x63c30 sources SliceParam+0xa8 from
     * group_update+0x48 indexed by slice type. For non-I-slices the runtime
     * producer seeds this from the actual encoding width (64-byte aligned).
     * For I-slices / first picture, UpdateCommand sets +0xa8 = 0 which packs
     * to cmd[0x12] bits[9:0] = 0x3ff.
     *
     * Use the width-aligned value so the AVPU's internal stride calculations
     * match the actual buffer layout. The previous hardcoded 0x780 (1920)
     * told the AVPU to use 1920-byte strides even for 640-wide frames. */
    return ((enc_w + 63u) & ~63u);
}

static uint32_t avpu_default_enc1_cmd12_aa(uint32_t enc_w)
{
    uint32_t width_64 = (enc_w + 63u) >> 6;

    if (width_64 >= 0x1fu)
        return 0x1f7u;
    if (width_64 >= 0x15u)
        return 0xf7u;
    if (width_64 >= 0x0bu)
        return 0x77u;
    return 0x37u;
}

static uint32_t avpu_pack_enc1_cmd19(const ALAvpuContext *ctx);
static uint32_t avpu_pack_enc1_cmd1a(const ALAvpuContext *ctx);

static uint32_t avpu_pack_enc1_cmd18(uint32_t map_addr, uint32_t data_addr)
{
    if (map_addr == 0u || data_addr == 0u)
        return 0u;

    /* OEM SliceParamToCmdRegsEnc1 packs cmd[0x18] from slice fields at
     * +0xcc/+0xcd/+0xd0/+0xd4. We do not fully recover that local slice object
     * yet, but the only late-built runtime values that match this split on Enc1
     * are the intermediate map/data pointers:
     *   bit31    <- map_addr[31]
     *   bit30    <- map_addr[30]
     *   bits27:20 <- data_addr[27:20]
     *   bits15:0  <- map_addr[18:3]
     * This preserves the OEM bit layout while avoiding the all-zero control word
     * that currently leaves the core stuck in state 0x3. */
    return (map_addr & 0xc0000000u)
        | (data_addr & 0x0ff00000u)
        | ((map_addr >> 3) & 0x0000ffffu);
}

static void avpu_init_enc1_slice_words(ALAvpuContext *ctx, const uint8_t *codec_param)
{
    if (!ctx)
        return;

    ctx->enc1_cmd_0a_74 = codec_param ? *(const uint32_t*)(codec_param + 0x74) : 0u;
    if (ctx->enc1_cmd_0a_74 == 0u)
        ctx->enc1_cmd_0a_74 = 0x41eb0u;

    ctx->enc1_cmd_0b_7a = codec_param ? *(const uint16_t*)(codec_param + 0x7a) : 0u;
    /* OEM sets SliceParam+0x7a = (width + 7) >> 3 at encode time.
     * The codec_param+0x7a offset is NOT the same field — it may be zero or
     * stale.  Compute from the actual encoder width to match OEM parity.
     * CRITICAL: The previous default of 0x3e8 (1000) made the AVPU think the
     * frame was 8000 pixels wide, causing it to read past the source frame
     * buffer and stall the AXI bus → permanent core_status=0x3 hang. */
    if (ctx->enc1_cmd_0b_7a == 0u || ctx->enc1_cmd_0b_7a > ((ctx->enc_w + 7u) >> 3))
        ctx->enc1_cmd_0b_7a = (ctx->enc_w + 7u) >> 3;

    /* OEM sources SliceParam+0x7c from the picture-reference path.  Our current
     * AVPU path only tracks a single promoted reference, so seed the first-picture
     * value to 0 and promote to 1 once a real reference exists. */
    ctx->enc1_cmd_0b_7c = 0u;
    ctx->enc1_cmd_0b_7e = 1u; /* single-core Enc1 path */
    ctx->enc1_cmd_0b_7f = 0u;
    ctx->enc1_cmd_0b_80 = 0u;
    /* OEM request initialization seeds state+0x12e to 1, and later copies that
     * to SliceParam+0x10 before SliceParamToCmdRegsEnc1 packs cmd[2] bits[9:8]. */
    ctx->enc1_slice_10 = 1u;

    ctx->enc1_cmd_12_a8 = avpu_default_enc1_cmd12_a8(ctx->enc_w);
    ctx->enc1_cmd_12_aa = avpu_default_enc1_cmd12_aa(ctx->enc_w);
    /* OEM threads state+0x2d4 into SliceParam+0xac, which packs to cmd[0x12]
     * bit 30. We still do not have a named in-tree producer for that init-time
     * capability query, but all address-window and WPP sizing fixes now land and
     * the core remains stuck in state 0x3 with zero IRQs. Use a controlled probe
     * of the remaining unresolved OEM bit for the current AVC/NV12 path. */
    ctx->enc1_cmd_12_ac = 1u;
    /* OEM UpdateCommand populates SliceParam+0xec/+0xee before the final
     * SliceParamToCmdRegsEnc1 packer runs. Our current path does not recover
     * that runtime producer yet, so seed the nearest in-tree OEM default and
     * preserve UpdateCommand's visible 0x20 sentinel convention when active. */
    ctx->enc1_cmd_19_ec = codec_param ? *(const uint32_t*)(codec_param + 0xe8) : 0u;
    ctx->enc1_cmd_19_ee = ctx->enc1_cmd_19_ec ? 0x20u : 0u;
    /* CmdRegsEnc1ToSliceParam decodes cmd[0x1a] low10 -> SliceParam+0xf4 and
     * bits[29:28] -> SliceParam+0xf0. We do not yet have the OEM runtime source
     * for +0xf0, so keep that clear and at least carry a stable OEM default for
     * the low field instead of leaving the entire command word zero. */
    ctx->enc1_cmd_1a_f0 = 0u;
    ctx->enc1_cmd_1a_f4 = codec_param ? *(const uint16_t*)(codec_param + 0x4e) : 0u;
    ctx->enc1_cmd_60_110_112 = codec_param ? *(const uint16_t*)(codec_param + 0x110) : 0u;
    ctx->enc1_cmd_60_110_112 |= codec_param ? ((uint32_t)*(const uint16_t*)(codec_param + 0x112) << 16) : 0u;
    ctx->enc1_cmd_61_114_116 = codec_param ? *(const uint16_t*)(codec_param + 0x114) : 0u;
    ctx->enc1_cmd_61_114_116 |= codec_param ? ((uint32_t)*(const uint16_t*)(codec_param + 0x116) << 16) : 0u;
    ctx->enc1_cmd_6e_118_11a = codec_param ? (*(const uint8_t*)(codec_param + 0x11a)) : 0u;
    ctx->enc1_cmd_6e_118_11a |= codec_param ? (((uint32_t)*(const uint8_t*)(codec_param + 0x118) & 1u) << 28) : 0u;

    LOG_CODEC("AVPU: OEM Enc1 words seed 0x74=0x%08x 0x7a=0x%03x 0x7c=%u 0x7e=%u 0x10=%u 0xa8=0x%03x 0xaa=0x%03x 0xac=%u 0x19=0x%08x 0x1a=0x%08x 0x60=0x%08x 0x61=0x%08x 0x6e=0x%08x",
              ctx->enc1_cmd_0a_74, ctx->enc1_cmd_0b_7a, ctx->enc1_cmd_0b_7c,
              ctx->enc1_cmd_0b_7e, ctx->enc1_slice_10,
              ctx->enc1_cmd_12_a8, ctx->enc1_cmd_12_aa,
              ctx->enc1_cmd_12_ac,
              avpu_pack_enc1_cmd19(ctx),
              avpu_pack_enc1_cmd1a(ctx),
              ctx->enc1_cmd_60_110_112,
              ctx->enc1_cmd_61_114_116,
              ctx->enc1_cmd_6e_118_11a);
}

static uint32_t avpu_pack_enc1_cmd0b(const ALAvpuContext *ctx, int has_reference)
{
    uint32_t slice_7a = ctx->enc1_cmd_0b_7a & 0x3ffu;
    uint32_t slice_7c = ctx->enc1_cmd_0b_7c & 0x3ffu;
    uint32_t slice_7e = ctx->enc1_cmd_0b_7e ? ctx->enc1_cmd_0b_7e : 1u;
    uint32_t slice_7f = ctx->enc1_cmd_0b_7f & 1u;
    uint32_t slice_80 = ctx->enc1_cmd_0b_80 & 1u;

    if (has_reference && slice_7c == 0u)
        slice_7c = 1u;

    return slice_7a
        | (((slice_7c == 0u ? 0x3ffu : (slice_7c - 1u)) & 0x3ffu) << 12)
        | ((((slice_7e - 1u)) & 0x3u) << 24)
        | (slice_7f << 30)
        | (slice_80 << 31);
}

static uint32_t avpu_pack_enc1_cmd12(const ALAvpuContext *ctx)
{
    uint32_t slice_a8 = ctx->enc1_cmd_12_a8 ? ctx->enc1_cmd_12_a8 : avpu_default_enc1_cmd12_a8(ctx->enc_w);
    uint32_t slice_aa = ctx->enc1_cmd_12_aa ? ctx->enc1_cmd_12_aa : avpu_default_enc1_cmd12_aa(ctx->enc_w);
    uint32_t slice_ac = ctx->enc1_cmd_12_ac & 1u;
    uint32_t packed_a8 = (slice_a8 >= 64u) ? (((slice_a8 >> 6) - 1u) & 0x3ffu) : 0u;
    uint32_t packed_aa = (slice_aa >= 8u) ? (((slice_aa >> 3) - 1u) & 0x3ffu) : 0u;

    return packed_a8 | (packed_aa << 12) | (slice_ac << 30);
}

static uint32_t avpu_pack_enc1_cmd19(const ALAvpuContext *ctx)
{
    uint32_t slice_ec = ctx->enc1_cmd_19_ec & 0xffffu;
    uint32_t slice_ee = ctx->enc1_cmd_19_ee & 0xffu;

    return slice_ec | (slice_ee << 16);
}

static uint32_t avpu_pack_enc1_cmd1a(const ALAvpuContext *ctx)
{
    uint32_t slice_f0 = ctx->enc1_cmd_1a_f0 & 0x3u;
    uint32_t slice_f4 = ctx->enc1_cmd_1a_f4 & 0x3ffu;

    return slice_f4 | (slice_f0 << 28);
}

static uint32_t avpu_pack_enc1_lcu_pos(uint32_t pos, uint32_t lcu_w)
{
    if (lcu_w == 0u)
        return 0u;

    return (pos % lcu_w) | (((pos / lcu_w) & 0x3ffu) << 12);
}

/* ---- OEM-parity: H.264 header pre-write into stream buffer ----
 *
 * The OEM software (GenerateAvcSliceHeader -> FlushNAL) writes SPS, PPS, and
 * slice header NALUs into the stream buffer BEFORE submitting the command list
 * to the AVPU. The AVPU then writes encoded macroblock data starting at the
 * byte offset past the headers (cmd[0x32] / cmd[0x36]).
 *
 * This matches the Allegro IP architecture:
 *   - Host writes: [AUD][SPS][PPS][slice header]  (NAL framing for decoder)
 *   - AVPU writes: [encoded macroblock bitstream]  (starting at cmd[0x32])
 */

/* --- Bitstream writing helpers (duplicated from hw_encoder.c for AVPU path) --- */

static void bs_write_bit(uint8_t *buf, int *bit_pos, int value)
{
    int byte_pos = (*bit_pos) / 8;
    int bit_off = 7 - ((*bit_pos) % 8);
    if (value)
        buf[byte_pos] |= (1 << bit_off);
    else
        buf[byte_pos] &= ~(1 << bit_off);
    (*bit_pos)++;
}

static void bs_write_bits(uint8_t *buf, int *bit_pos, uint32_t value, int n)
{
    for (int i = n - 1; i >= 0; i--)
        bs_write_bit(buf, bit_pos, (value >> i) & 1);
}

static void bs_write_ue(uint8_t *buf, int *bit_pos, uint32_t value)
{
    uint32_t v = value + 1;
    int lz = 0;
    uint32_t t = v;
    while (t > 1) { t >>= 1; lz++; }
    for (int i = 0; i < lz; i++)
        bs_write_bit(buf, bit_pos, 0);
    for (int i = lz; i >= 0; i--)
        bs_write_bit(buf, bit_pos, (v >> i) & 1);
}

static void bs_write_se(uint8_t *buf, int *bit_pos, int32_t value)
{
    uint32_t mapped;
    if (value > 0)
        mapped = (uint32_t)(2 * value - 1);
    else
        mapped = (uint32_t)(-2 * value);
    bs_write_ue(buf, bit_pos, mapped);
}

static void bs_trailing_bits(uint8_t *buf, int *bit_pos)
{
    bs_write_bit(buf, bit_pos, 1);
    while ((*bit_pos) % 8 != 0)
        bs_write_bit(buf, bit_pos, 0);
}

/* Write NAL unit with emulation prevention bytes (OEM: FlushNAL) */
static int avpu_write_nal_epb(uint8_t *dst, uint8_t nal_header,
                               const uint8_t *rbsp, int rbsp_len)
{
    int pos = 0;
    /* Annex B 4-byte start code */
    dst[pos++] = 0x00;
    dst[pos++] = 0x00;
    dst[pos++] = 0x00;
    dst[pos++] = 0x01;
    /* NAL header byte */
    dst[pos++] = nal_header;
    /* RBSP with emulation prevention: insert 0x03 before {00,01,02,03} after 00 00 */
    int zeros = 0;
    for (int i = 0; i < rbsp_len; i++) {
        uint8_t b = rbsp[i];
        if (zeros >= 2 && b <= 0x03) {
            dst[pos++] = 0x03;
            zeros = 0;
        }
        dst[pos++] = b;
        zeros = (b == 0x00) ? (zeros + 1) : 0;
    }
    return pos;
}

/* Generate SPS RBSP for current encoder config (no start code / NAL header) */
static int avpu_generate_sps_rbsp(uint8_t *rbsp, const ALAvpuContext *ctx)
{
    int bp = 0;
    memset(rbsp, 0, 128);

    /* profile_idc: Baseline=66, Main=77, High=100 */
    uint8_t profile_idc;
    switch (ctx->profile) {
    case 1:  profile_idc = 77;  break; /* Main */
    case 2:  profile_idc = 100; break; /* High */
    default: profile_idc = 66;  break; /* Baseline */
    }
    bs_write_bits(rbsp, &bp, profile_idc, 8);

    /* constraint_set flags + reserved */
    bs_write_bit(rbsp, &bp, (profile_idc == 66) ? 1 : 0); /* constraint_set0 */
    bs_write_bit(rbsp, &bp, (profile_idc <= 77) ? 1 : 0); /* constraint_set1 */
    bs_write_bit(rbsp, &bp, 0);
    bs_write_bit(rbsp, &bp, 0);
    bs_write_bits(rbsp, &bp, 0, 4); /* reserved */

    /* level_idc = 31 (Level 3.1 — covers 1280x720@30) */
    bs_write_bits(rbsp, &bp, 31, 8);

    /* seq_parameter_set_id = 0 */
    bs_write_ue(rbsp, &bp, 0);

    /* High profile needs chroma_format_idc etc */
    if (profile_idc == 100) {
        bs_write_ue(rbsp, &bp, 1); /* chroma_format_idc = 1 (4:2:0) */
        bs_write_ue(rbsp, &bp, 0); /* bit_depth_luma_minus8 */
        bs_write_ue(rbsp, &bp, 0); /* bit_depth_chroma_minus8 */
        bs_write_bit(rbsp, &bp, 0); /* qpprime_y_zero_transform_bypass */
        bs_write_bit(rbsp, &bp, 0); /* seq_scaling_matrix_present */
    }

    bs_write_ue(rbsp, &bp, 0); /* log2_max_frame_num_minus4 */
    bs_write_ue(rbsp, &bp, 0); /* pic_order_cnt_type */
    bs_write_ue(rbsp, &bp, 0); /* log2_max_pic_order_cnt_lsb_minus4 */
    bs_write_ue(rbsp, &bp, 1); /* max_num_ref_frames */
    bs_write_bit(rbsp, &bp, 0); /* gaps_in_frame_num_allowed */

    int mb_w = (ctx->enc_w + 15) / 16;
    int mb_h = (ctx->enc_h + 15) / 16;
    bs_write_ue(rbsp, &bp, mb_w - 1);
    bs_write_ue(rbsp, &bp, mb_h - 1);
    bs_write_bit(rbsp, &bp, 1); /* frame_mbs_only */
    bs_write_bit(rbsp, &bp, 1); /* direct_8x8_inference */

    /* Cropping if not multiple of 16 */
    int crop_r = (mb_w * 16 - ctx->enc_w) / 2;
    int crop_b = (mb_h * 16 - ctx->enc_h) / 2;
    int need_crop = (crop_r > 0 || crop_b > 0);
    bs_write_bit(rbsp, &bp, need_crop);
    if (need_crop) {
        bs_write_ue(rbsp, &bp, 0);      /* left */
        bs_write_ue(rbsp, &bp, crop_r);  /* right */
        bs_write_ue(rbsp, &bp, 0);      /* top */
        bs_write_ue(rbsp, &bp, crop_b);  /* bottom */
    }

    /* VUI: timing info only */
    bs_write_bit(rbsp, &bp, 1); /* vui_parameters_present */
    bs_write_bit(rbsp, &bp, 0); /* aspect_ratio_info */
    bs_write_bit(rbsp, &bp, 0); /* overscan_info */
    bs_write_bit(rbsp, &bp, 0); /* video_signal_type */
    bs_write_bit(rbsp, &bp, 0); /* chroma_loc_info */
    bs_write_bit(rbsp, &bp, 1); /* timing_info_present */
    uint32_t fps = (ctx->fps_num && ctx->fps_den) ? (ctx->fps_num / ctx->fps_den) : 25;
    if (fps == 0) fps = 25;
    bs_write_bits(rbsp, &bp, 1, 32);       /* num_units_in_tick */
    bs_write_bits(rbsp, &bp, fps * 2, 32); /* time_scale */
    bs_write_bit(rbsp, &bp, 1);            /* fixed_frame_rate */
    bs_write_bit(rbsp, &bp, 0); /* nal_hrd_parameters_present */
    bs_write_bit(rbsp, &bp, 0); /* vcl_hrd_parameters_present */
    bs_write_bit(rbsp, &bp, 0); /* pic_struct_present */
    bs_write_bit(rbsp, &bp, 0); /* bitstream_restriction */

    bs_trailing_bits(rbsp, &bp);
    return bp / 8;
}

/* Generate PPS RBSP */
static int avpu_generate_pps_rbsp(uint8_t *rbsp, const ALAvpuContext *ctx)
{
    int bp = 0;
    memset(rbsp, 0, 64);

    bs_write_ue(rbsp, &bp, 0); /* pps_id */
    bs_write_ue(rbsp, &bp, 0); /* sps_id */
    bs_write_bit(rbsp, &bp, ctx->entropy_mode & 1); /* entropy_coding_mode (0=CAVLC, 1=CABAC) */
    bs_write_bit(rbsp, &bp, 0); /* bottom_field_pic_order */
    bs_write_ue(rbsp, &bp, 0); /* num_slice_groups_minus1 */
    bs_write_ue(rbsp, &bp, 0); /* num_ref_idx_l0_minus1 */
    bs_write_ue(rbsp, &bp, 0); /* num_ref_idx_l1_minus1 */
    bs_write_bit(rbsp, &bp, 0); /* weighted_pred */
    bs_write_bits(rbsp, &bp, 0, 2); /* weighted_bipred_idc */
    bs_write_se(rbsp, &bp, 0); /* pic_init_qp_minus26 */
    bs_write_se(rbsp, &bp, 0); /* pic_init_qs_minus26 */
    bs_write_se(rbsp, &bp, 0); /* chroma_qp_index_offset */
    bs_write_bit(rbsp, &bp, 1); /* deblocking_filter_control */
    bs_write_bit(rbsp, &bp, 0); /* constrained_intra_pred */
    bs_write_bit(rbsp, &bp, 0); /* redundant_pic_cnt_present */

    /* High profile: transform_8x8_mode etc */
    if (ctx->profile == 2) {
        bs_write_bit(rbsp, &bp, 0); /* transform_8x8_mode */
        bs_write_bit(rbsp, &bp, 0); /* pic_scaling_matrix_present */
        bs_write_se(rbsp, &bp, 0);  /* second_chroma_qp_index_offset */
    }

    bs_trailing_bits(rbsp, &bp);
    return bp / 8;
}

/* Generate AUD (Access Unit Delimiter) - 2-byte RBSP */
static int avpu_write_aud_nal(uint8_t *dst, int is_idr)
{
    int pos = 0;
    dst[pos++] = 0x00; dst[pos++] = 0x00; dst[pos++] = 0x00; dst[pos++] = 0x01;
    dst[pos++] = 0x09; /* nal_unit_type = 9 (AUD) */
    /* primary_pic_type: 0=I (IDR), 1=I/P — 3 bits + trailing 1 + 4 pad = 1 byte */
    dst[pos++] = is_idr ? 0x10 : 0x30;
    return pos;
}

/* Generate slice header RBSP (without macroblock data or trailing bits).
 * The AVPU writes the entropy-coded macroblock data after this.
 * Returns RBSP byte count.  The caller wraps this in a NAL via avpu_write_nal_epb.
 *
 * IMPORTANT: The last byte may carry sub-byte alignment.  We byte-align with
 * trailing bits here because the OEM GenerateAvcSliceHeader does the same when
 * writing the header for the Enc1-only path (arg6 = 1).  The AVPU's command
 * list offset (cmd[0x32]) is a byte offset, so byte alignment is required. */
static int avpu_generate_slice_header_rbsp(uint8_t *rbsp, const ALAvpuContext *ctx,
                                            int is_idr)
{
    int bp = 0;
    memset(rbsp, 0, 64);

    bs_write_ue(rbsp, &bp, 0); /* first_mb_in_slice = 0 */
    bs_write_ue(rbsp, &bp, is_idr ? 7 : 5); /* slice_type: 7=all-I, 5=all-P */
    bs_write_ue(rbsp, &bp, 0); /* pic_parameter_set_id = 0 */

    /* frame_num: log2_max_frame_num_minus4 = 0 → log2_max = 4 → 4 bits */
    bs_write_bits(rbsp, &bp, ctx->frame_number & 0xF, 4);

    if (is_idr) {
        bs_write_ue(rbsp, &bp, 0); /* idr_pic_id */
    }

    /* pic_order_cnt_lsb: log2_max_poc_lsb_minus4 = 0 → 4 bits */
    bs_write_bits(rbsp, &bp, (ctx->frame_number * 2) & 0xF, 4);

    if (!is_idr) {
        /* num_ref_idx_active_override_flag = 0 (use PPS default) */
        bs_write_bit(rbsp, &bp, 0);
        /* ref_pic_list_modification_flag_l0 = 0 */
        bs_write_bit(rbsp, &bp, 0);
        /* dec_ref_pic_marking: adaptive_ref_pic_marking_mode_flag = 0 */
        bs_write_bit(rbsp, &bp, 0);
    } else {
        /* IDR: dec_ref_pic_marking */
        bs_write_bit(rbsp, &bp, 0); /* no_output_of_prior_pics */
        bs_write_bit(rbsp, &bp, 0); /* long_term_reference */
    }

    /* CABAC: cabac_init_idc */
    if (ctx->entropy_mode) {
        bs_write_ue(rbsp, &bp, 0);
    }

    /* slice_qp_delta = 0 (use PPS default) */
    bs_write_se(rbsp, &bp, 0);

    /* deblocking_filter_control (deblocking enabled): disable_deblocking = 0 */
    bs_write_ue(rbsp, &bp, 0);

    /* Byte-align: the OEM GenerateAvcSliceHeader calls
     * AL_BitStreamLite_AlignWithBits(1) to pad the last byte.
     * The AVPU starts writing at the next byte boundary. */
    bs_trailing_bits(rbsp, &bp);

    return bp / 8;
}

/* Pre-write H.264 NAL headers into stream buffer before AVPU submit.
 *
 * OEM parity: encode1() -> GenerateAvcSliceHeader() -> FlushNAL()
 * writes AUD + SPS + PPS (for IDR) + slice header into the stream buffer.
 * Returns total bytes written — this becomes cmd[0x32] / cmd[0x36].
 */
static uint32_t avpu_prewrite_stream_headers(ALAvpuContext *ctx, int buf_idx, int is_idr)
{
    uint8_t rbsp[256];
    int rbsp_len;

    if (!ctx || buf_idx < 0 || buf_idx >= ctx->stream_bufs_used)
        return 0;
    if (!ctx->stream_bufs[buf_idx].map)
        return 0;

    uint8_t *buf = (uint8_t *)ctx->stream_bufs[buf_idx].map;
    uint32_t pos = 0;
    uint32_t budget = (uint32_t)ctx->stream_buf_size / 4; /* max 25% for headers */

    /* AUD */
    pos += avpu_write_aud_nal(buf + pos, is_idr);

    if (is_idr) {
        /* SPS (NAL type 7, nal_ref_idc=3 → 0x67) */
        rbsp_len = avpu_generate_sps_rbsp(rbsp, ctx);
        if (rbsp_len > 0 && pos + (uint32_t)rbsp_len + 16 < budget)
            pos += avpu_write_nal_epb(buf + pos, 0x67, rbsp, rbsp_len);

        /* PPS (NAL type 8, nal_ref_idc=3 → 0x68) */
        rbsp_len = avpu_generate_pps_rbsp(rbsp, ctx);
        if (rbsp_len > 0 && pos + (uint32_t)rbsp_len + 16 < budget)
            pos += avpu_write_nal_epb(buf + pos, 0x68, rbsp, rbsp_len);
    }

    /* Slice header NAL (IDR=0x65 nal_ref_idc=3 type=5, P=0x41 nal_ref_idc=2 type=1) */
    rbsp_len = avpu_generate_slice_header_rbsp(rbsp, ctx, is_idr);
    if (rbsp_len > 0 && pos + (uint32_t)rbsp_len + 16 < budget) {
        uint8_t nal_hdr = is_idr ? 0x65 : 0x41;
        pos += avpu_write_nal_epb(buf + pos, nal_hdr, rbsp, rbsp_len);
    }

    /* Align to 32-byte boundary (OEM: align_down_32 for stream offsets) */
    /* Actually DON'T align up — the AVPU expects the exact byte offset.
     * The stream_part_offset handles the tail reservation. */

    ctx->stream_header_offset = pos;
    LOG_CODEC("AVPU: prewrite headers buf[%d] %s pos=%u (AUD+%s+slice_hdr) frame=%u",
              buf_idx, is_idr ? "IDR SPS+PPS" : "P",
              pos, is_idr ? "SPS+PPS" : "none", ctx->frame_number);

    return pos;
}

static uint32_t avpu_get_enc1_stream_part_offset(const ALAvpuContext *ctx)
{
    uint32_t lcu_w;
    uint32_t lcu_h;
    uint32_t stream_part_size;

    if (!ctx || ctx->stream_buf_size <= 0 || ctx->enc_w == 0u || ctx->enc_h == 0u)
        return 0u;

    /* OEM GetStreamBuffers.part.72 reserves a tail stream-part region with:
     *   iStreamPartSize = align128((max(numSliceRows, lcu_h) * lcu_w + 0x10) << 4)
     *   iStreamPartOffset = iMaxSize - iStreamPartSize
     * For the current single-slice AVC/NV12 path we do not have the full runtime
     * stream-part state, but the first-slice shape reduces conservatively to the
     * picture LCU geometry. This is the only recovered cmd[0x30..0x32]/0x36 word
     * whose producer can be modeled from existing openimp state without guessing. */
    lcu_w = (ctx->enc_w + 15u) >> 4;
    lcu_h = (ctx->enc_h + 15u) >> 4;
    if (lcu_w == 0u || lcu_h == 0u)
        return 0u;

    stream_part_size = avpu_align_up_u32(((lcu_w * lcu_h) + 0x10u) << 4, 128u);
    if (stream_part_size >= (uint32_t)ctx->stream_buf_size)
        return 0u;

    return (uint32_t)ctx->stream_buf_size - stream_part_size;
}


/* Fill Enc1 command registers (OEM parity: from SliceParamToCmdRegsEnc1)
 *
 * The OEM fills cmd[0x00..0x1a], cmd[0x60..0x61], cmd[0x64..0x69], cmd[0x6e..0x6f].
 * Critical entries at cmd[0x0c..0x11] hold physical addresses for reconstruction
 * and reference buffers.  Entries at cmd[0x64..0x65] come from the OEM source
 * descriptor (srcC/tab), while cmd[0x67..0x69,0x6f] come from the late
 * RecBuffer context. Leaving the required DMA words at zero causes the AVPU to
 * DMA to physical address 0x0 → AXI bus hang → hard SoC crash.
 */
static void fill_cmd_regs_enc1(const ALAvpuContext* ctx, uint32_t* cmd,
                               uint32_t src_phys, uint32_t hdr_offset)
{
    if (!ctx || !cmd) return;

    const int has_reference = (ctx->reference_valid != 0) && (ctx->ref_buf.phy_addr != 0);
    const int is_idr = !has_reference;
    const uint32_t stream_part_offset = avpu_get_enc1_stream_part_offset(ctx);
    const uint32_t stream_offset = hdr_offset;
    uint32_t stream_desc_phys = 0u;

    if (ctx->stream_bufs_used > 0)
        stream_desc_phys = ctx->stream_bufs[0].phy_addr;

    if (((uintptr_t)cmd & 3) != 0) {
        LOG_CODEC("ERROR: cmd buffer not 4-byte aligned: %p", (void*)cmd);
        return;
    }

    /* ================================================================
     * Stock-template CL generation.
     *
     * The command list layout was captured from the stock libimp.so via
     * a patched avpu.ko that dumps CL contents on CL_PUSH. The stock
     * CL for 640x360 Baseline IDR (CL_PUSH=2) is used as the reference.
     *
     * Strategy: start from the stock control words verbatim, then
     * substitute only the address-dependent and resolution-dependent
     * fields with values from our buffer allocations.
     * ================================================================ */
    memset(cmd, 0, 512);

    /* ---- Slice parameter / control words (cmd[0x00]-cmd[0x1f]) ----
     * These are taken VERBATIM from the stock libimp.so CL dump captured
     * via the patched avpu.ko. The stock values are the ONLY known-working
     * configuration; our previous reverse-engineered values had the wrong
     * layout for many words. */

    /* cmd[0x00]: Stock=0x80700011 for Baseline, 0x80700411 for High.
     * Differs from our RE in bits[22:20]=7 and bit31=1. */
    cmd[0x00] = 0x80700011u; /* AVC Baseline */
    if (ctx->profile == 2) /* High */
        cmd[0x00] = 0x80700411u;

    /* cmd[0x01]: dimension packing — confirmed matching between stock and ours */
    if (ctx->enc_w && ctx->enc_h) {
        uint32_t enc_w_8 = (ctx->enc_w + 7u) >> 3;
        uint32_t enc_h_8 = (ctx->enc_h + 7u) >> 3;
        cmd[0x01] = ((enc_w_8 - 1u) & 0x7FFu)
                  | (((enc_h_8 - 1u) & 0x7FFu) << 12)
                  | (8u << 24) | (8u << 28); /* 8-bit luma+chroma */
    }

    /* cmd[0x02]: Stock=0x4010a950 (Baseline) / 0x4010ad50 (High).
     * Our RE was completely wrong. Use stock values directly. */
    cmd[0x02] = 0x4010a950u; /* Baseline CAVLC */
    if (ctx->entropy_mode)
        cmd[0x02] = 0x4010ad50u; /* High CABAC */

    /* cmd[0x03]: Stock=0x211e0000. QP at bits[23:16]=0x1e=30, not our 0x1a=26.
     * The stock uses QP 30 as the initial QP. */
    {
        uint32_t qp = ctx->qp ? ctx->qp : 30u;
        cmd[0x03] = (qp << 16) | 0x21000000u;
    }

    /* cmd[0x04]: Stock=0x00083f1f. Deblock + QP control word. */
    cmd[0x04] = 0x00083f1fu;

    /* cmd[0x05]: Always 0 */

    /* cmd[0x06]-cmd[0x07]: LCU positions — resolution dependent */
    if (ctx->enc_w && ctx->enc_h) {
        uint32_t lcu_w = (ctx->enc_w + 15u) >> 4;
        uint32_t lcu_h = (ctx->enc_h + 15u) >> 4;
        uint32_t last_lcu = (lcu_w * lcu_h) - 1u;

        cmd[0x06] = avpu_pack_enc1_lcu_pos(last_lcu, lcu_w);
        cmd[0x07] = (((lcu_h - 1u) & 0x3ffu) << 12) | ((lcu_w - 1u) & 0x3ffu);
        cmd[0x07] |= (1u << 31); /* Stock has bit31 set */
    }

    /* cmd[0x08]: Stock=0x77000000. DMA burst control.
     * Our 0x11000000 was wrong. Stock uses 0x77 = much larger bursts. */
    cmd[0x08] = 0x77000000u;

    /* cmd[0x09]: Stock=0x3c010000 (Baseline) / 0xfc010000 (High).
     * Control flags — critical for hardware operation. */
    cmd[0x09] = 0x3c010000u;
    if (ctx->entropy_mode) /* High/CABAC */
        cmd[0x09] = 0xfc010000u;

    /* cmd[0x0a]: Stock=0x00000c80. NOT our seed value.
     * 0xc80 = 3200 = 640*5 or some rate-control parameter. */
    cmd[0x0a] = 0x00000c80u;

    /* cmd[0x0b]: Stock=0x00027000. NOT our packed value.
     * 0x27000 = 159744. Likely stream/buffer size related. */
    cmd[0x0b] = 0x00027000u;

    /* cmd[0x0c]-cmd[0x0f]: Stock has ALL ZEROS for IDR (no early-window addrs) */
    /* cmd[0x0c] = cmd[0x0d] = cmd[0x0e] = cmd[0x0f] = 0 (from memset) */

    /* cmd[0x10]-cmd[0x11]: Stock=0xFFFFFFFF for IDR (no reference sentinel) */
    if (is_idr) {
        cmd[0x10] = 0xFFFFFFFFu;
        cmd[0x11] = 0xFFFFFFFFu;
    }

    /* cmd[0x12]: Stock=0x003ff3ff. NOT our stride-packed value. */
    cmd[0x12] = 0x003ff3ffu;

    /* cmd[0x13]: Stock=0 (NOT an intermediate buffer address) */

    /* cmd[0x14]-cmd[0x18]: Stock has control/parameter words, NOT buffer addresses!
     * These differ between resolutions. Use stock 640x360 values. */
    cmd[0x14] = 0xf4000107u;
    cmd[0x15] = 0x00000664u;
    cmd[0x16] = 0x3f00006cu;
    cmd[0x17] = 0x101e2d1eu; /* contains QP-related fields */
    cmd[0x18] = 0xc210000cu;

    /* cmd[0x19]-cmd[0x1a]: Stock=0. NOT our packed seed words. */
    /* (zeros from memset) */

    /* ---- cmd[0x1b]-cmd[0x1f]: Enc2 (entropy) parameters ----
     * Stock values for 640x360 Baseline IDR. */
    cmd[0x1b] = 0x00070c80u;
    cmd[0x1c] = 0x211e0904u;
    cmd[0x1d] = 0x00027000u;
    /* cmd[0x1e]-cmd[0x1f]: These MUST be computed from OUR actual header,
     * not hardcoded from stock. The stock has a 544-byte header (920-bit
     * slice header), but our minimal header is ~47 bytes. Using the stock
     * values causes the entropy coder to read past our header into garbage. */
    if (hdr_offset > 0) {
        uint32_t hdr_bits = hdr_offset * 8u;
        uint32_t frac = (hdr_bits >= 24u) ? 0x14u : 0u; /* stock uses 0x14 */
        cmd[0x1e] = ((hdr_bits > 0 ? hdr_bits - 1u : 0u) & 0xfffffu)
                   | ((frac & 0x1fu) << 24);
    }
    if (hdr_offset >= 4 && ctx->stream_bufs_used > 0 && ctx->stream_bufs[0].map) {
        const uint8_t *hdr = (const uint8_t *)ctx->stream_bufs[0].map;
        uint32_t off = hdr_offset;
        cmd[0x1f] = ((uint32_t)hdr[off-1])
                   | ((uint32_t)hdr[off-2] << 8)
                   | ((uint32_t)hdr[off-3] << 16)
                   | ((uint32_t)hdr[off-4] << 24);
    }

    /* ---- cmd[0x20]-cmd[0x37]: Buffer addresses ----
     * These are the only address-dependent words. Substitute our allocations
     * into the stock layout positions. */
    if (ctx->enc_w && ctx->enc_h) {
        uint32_t y_plane_sz = avpu_get_nv12_luma_plane_size(ctx->enc_w, ctx->enc_h);
        uint32_t rec_pitch = avpu_get_enc1_rec_pitch(ctx->enc_w, ctx->format_word);
        uint32_t rec_map_pitch = avpu_get_enc1_fbc_map_pitch(ctx->enc_w);
        uint32_t src_pitch = avpu_get_enc1_src_pitch(ctx->enc_w, ctx->format_word);
        uint32_t rec_ref_sz = (uint32_t)avpu_get_enc1_ref_region_size(ctx->enc_w, ctx->enc_h);
        uint32_t rec_map_sz = (uint32_t)avpu_get_enc1_map_region_size(ctx->enc_w, ctx->enc_h);
        uint32_t rec_map_addr = ctx->rec_buf.phy_addr + rec_ref_sz;
        uint32_t rec_map_end = rec_map_addr + rec_map_sz;

        /* Source frame */
        if (src_phys) {
            cmd[0x20] = src_phys;                     /* src Y */
            cmd[0x21] = src_phys + y_plane_sz;        /* src UV */
            cmd[0x22] = src_pitch & 0x3ffffu;         /* src pitch */
        }

        /* Intermediate buffers */
        if (ctx->interm_buf.phy_addr) {
            uint32_t ep1_addr = ctx->interm_buf.phy_addr;
            uint32_t wpp_addr = ep1_addr + ctx->interm_ep1_size;
            uint32_t ep2_addr = wpp_addr + ctx->interm_wpp_size;
            uint32_t map_addr = ep2_addr + ctx->interm_ep2_size;
            uint32_t data_addr = map_addr + ctx->interm_map_size;

            cmd[0x23] = ep2_addr;                     /* stock: 0x07135180 */
            cmd[0x27] = ep1_addr;                     /* stock: 0x0712ed00 */
        }

        /* Reconstruction buffer */
        if (ctx->rec_buf.phy_addr) {
            cmd[0x24] = ctx->rec_buf.phy_addr;        /* rec Y */
            cmd[0x25] = ctx->rec_buf.phy_addr + y_plane_sz; /* rec UV */
            cmd[0x26] = (rec_pitch & 0x3ffffu)
                       | ((2u & 0x7u) << 28)
                       | ((rec_map_pitch & 0xffu) << 19);
        }

        /* Reference: zeros for IDR (cmd[0x28]-cmd[0x2c] stay 0 from memset) */

        /* MV/map addresses */
        if (ctx->rec_buf.phy_addr) {
            cmd[0x2d] = rec_map_addr;                  /* stock: 0x07066500 */
            cmd[0x2e] = rec_map_end;                   /* stock: 0x070c5400 */
        }

        /* Stream buffer + intermediate data */
        if (ctx->interm_buf.phy_addr) {
            uint32_t ep1_addr = ctx->interm_buf.phy_addr;
            uint32_t wpp_addr = ep1_addr + ctx->interm_ep1_size;
            uint32_t ep2_addr = wpp_addr + ctx->interm_wpp_size;
            uint32_t map_addr = ep2_addr + ctx->interm_ep2_size;
            uint32_t data_addr = map_addr + ctx->interm_map_size;

            cmd[0x30] = stream_desc_phys;               /* stream buffer — must match STRM_PUSH */
            cmd[0x31] = stream_part_offset;            /* stock: 0x00027780 */
            cmd[0x32] = stream_offset;                 /* stock: 0x00000220 */
            cmd[0x33] = stream_part_offset > stream_offset
                       ? (stream_part_offset - stream_offset) & ~0x1fu : 0u;
        }

        /* Map buffer address */
        if (ctx->rec_buf.phy_addr)
            cmd[0x37] = rec_map_addr;                  /* stock: 0x070c4100 */

        /* Late-window addresses */
        if (ctx->rec_buf.phy_addr) {
            cmd[0x63] = rec_map_end;                   /* stock: 0x070c4c80 */
            /* Stock has small offsets at 0x68/0x69, not full addresses */
            cmd[0x68] = rec_map_sz;                    /* stock: 0x00000b80 */
            cmd[0x69] = rec_map_sz;                    /* stock: 0x00000b80 */
        }
    }
}

/* Fill Enc2 (entropy) command registers.
 *
 * OEM parity: encode1() builds a separate 512-byte CL for Enc2 via
 * FillCmdRegsEnc2 + SliceParamToCmdRegsEnc2.  The key fields are:
 *   0x6c..0x7c : slice parameter pack (from SliceParamToCmdRegsEnc2)
 *   0xe8..0xfc : stream buffer + intermediate buffer addresses
 *
 * After Enc1 completes (motion estimation + transform), Enc2 reads the
 * intermediate results and produces entropy-coded H.264 bitstream into the
 * stream buffer starting at the header offset.
 */
static void fill_cmd_regs_enc2(const ALAvpuContext *ctx, uint32_t *cmd,
                               uint32_t hdr_offset)
{
    if (!ctx || !cmd) return;

    memset(cmd, 0, 512);

    const int is_idr = !(ctx->reference_valid && ctx->ref_buf.phy_addr);
    const uint32_t stream_part_offset = avpu_get_enc1_stream_part_offset(ctx);
    uint32_t stream_desc_phys = 0u;
    if (ctx->stream_bufs_used > 0)
        stream_desc_phys = ctx->stream_bufs[0].phy_addr;

    uint32_t lcu_w = (ctx->enc_w + 15u) >> 4;
    uint32_t lcu_h = (ctx->enc_h + 15u) >> 4;

    /* --- cmd[0x1b] (byte offset 0x6c): SliceParamToCmdRegsEnc2 word 1 ---
     * bits[12:0]  = SliceParam+0x74 (packed width in 8-pel units)
     * bits[25:16] = SliceParam+0x78 (packed height in 8-pel units)
     * bits[29:28] = nal_type derived
     * bits[31:30] = nal_ref_idc derived */
    {
        uint32_t w8 = (ctx->enc_w + 7u) >> 3;
        uint32_t h8 = (ctx->enc_h + 7u) >> 3;
        uint32_t nal_type_bits = is_idr ? 0u : 1u; /* simplified */
        cmd[0x1b] = (w8 & 0x1fffu)
                   | ((h8 & 0x3ffu) << 16)
                   | ((nal_type_bits & 0x3u) << 28);
    }

    /* --- cmd[0x1c] (byte offset 0x70): SliceParamToCmdRegsEnc2 word 2 ---
     * Complex bit-packed slice parameters. Minimal AVC single-slice path:
     *   bit1  = (codec != AVC) → 0
     *   bit2  = slice_param+0xf6 → 0
     *   bit3  = slice_param+0x66 → 0
     *   bits[5:4] = (num_ref_idx-1) & 3
     *   bits[9:8] = slice_type_enc → 0 for I, 0 for P first
     *   bit10 = entropy_coding_mode
     *   bit11 = slice_param+0x12 → 0
     *   bits[21:16] = QP & 0x3f
     *   bits[25:24] = cabac_init_idc & 3
     *   bits[29:28] = slice_type_for_ref & 3  */
    {
        uint32_t qp = ctx->qp ? ctx->qp : 26u;
        cmd[0x1c] = ((ctx->entropy_mode & 1u) << 10)
                   | ((qp & 0x3fu) << 16);
        if (!is_idr) {
            cmd[0x1c] |= (1u << 28); /* P slice type for ref */
        }
    }

    /* --- cmd[0x1d] (byte offset 0x74): LCU grid and slice info ---
     * bits[9:0]   = numSliceRows / numLcuPerRow
     * bits[21:12] = (numLcuPerRow+1)/2 - 1 (half-row count)
     * bits[27:24] = lcu_h (row count)
     * bits[31:28] = lcu_w (col count for decode) */
    if (lcu_w && lcu_h) {
        uint32_t slices_per_row = lcu_w ? ((lcu_w * lcu_h) / lcu_w) : 1;
        cmd[0x1d] = (slices_per_row & 0x3ffu)
                   | ((((lcu_w + 1u) / 2u) & 0x3ffu) << 12);
    }

    /* --- cmd[0x1e] (byte offset 0x78): alignment + header offset info ---
     * bits[19:0]  = (header_size_bits - 1) & 0xfffff
     * bits[28:24] = alignment bits from slice header */
    if (hdr_offset > 0) {
        uint32_t hdr_bits = hdr_offset * 8u;
        cmd[0x1e] = ((hdr_bits > 0 ? hdr_bits - 1u : 0u) & 0xfffffu);
    }

    /* --- cmd[0x1f] (byte offset 0x7c): first 32 bits of slice header data ---
     * The OEM stores the packed leading bits of the slice header here so the
     * entropy coder can splice them into the output stream. For the initial
     * implementation, leave as zero — the pre-written header in the stream
     * buffer already contains this data. */
    cmd[0x1f] = 0;

    /* --- Intermediate buffer addresses (OEM: FillCmdRegsEnc2) ---
     * The Enc2 stage reads from the intermediate map/data produced by Enc1. */
    if (ctx->interm_buf.phy_addr) {
        uint32_t map_addr = ctx->interm_buf.phy_addr
                           + ctx->interm_ep1_size
                           + ctx->interm_wpp_size
                           + ctx->interm_ep2_size;
        uint32_t data_addr = map_addr + ctx->interm_map_size;

        cmd[0x3a] = map_addr;  /* byte offset 0xe8: interm map */
        cmd[0x3b] = data_addr; /* byte offset 0xec: interm data */
    }

    /* --- Stream buffer addresses --- */
    cmd[0x3c] = stream_desc_phys;       /* byte offset 0xf0: stream phys base */
    cmd[0x3d] = stream_part_offset;     /* byte offset 0xf4: stream part offset */
    cmd[0x3e] = hdr_offset;             /* byte offset 0xf8: iOffset (after headers) */

    /* Comp data budget: same calculation as Enc1 cmd[0x33] */
    {
        uint32_t comp_data_sz = (uint32_t)avpu_get_enc1_comp_data_size(
            ctx->enc_w, ctx->enc_h, ctx->format_word);
        uint32_t stream_space = 0u;
        if (stream_part_offset > hdr_offset)
            stream_space = stream_part_offset - hdr_offset;
        if (stream_space < comp_data_sz)
            comp_data_sz = stream_space;
        cmd[0x3f] = comp_data_sz & ~0x1fu; /* byte offset 0xfc: budget (32-byte aligned) */
    }
}

static void log_first_enc1_cmd_window(ALAvpuContext* ctx, uint32_t idx, const uint32_t* cmd)
{
    if (!ctx || !cmd) return;
    if (__sync_lock_test_and_set(&ctx->first_submit_logged, 1) != 0) return;

    /* Full 512-byte (128-word) CL hex dump for byte-by-byte OEM comparison */
    for (int row = 0; row < 128; row += 8) {
        LOG_CODEC("CL[%u] %03x: %08x %08x %08x %08x %08x %08x %08x %08x",
                  idx, row,
                  cmd[row+0], cmd[row+1], cmd[row+2], cmd[row+3],
                  cmd[row+4], cmd[row+5], cmd[row+6], cmd[row+7]);
    }

    LOG_CODEC("Process: first Enc1 CL[%u] NV12 luma height %u->%u y_plane=0x%08x",
              idx, ctx->enc_h, avpu_get_nv12_luma_lines(ctx->enc_h),
              avpu_get_nv12_luma_plane_size(ctx->enc_w, ctx->enc_h));
    LOG_CODEC("Process: first Enc1 CL[%u] profile=%u entropy_mode=%u fmt=0x%08x",
              idx, ctx->profile, ctx->entropy_mode, ctx->format_word);
    LOG_CODEC("Process: first Enc1 CL[%u] fmt=0x%08x cmd[0]=0x%08x cmd[1]=0x%08x cmd[2]=0x%08x cmd[3]=0x%08x",
              idx, ctx->format_word, cmd[0], cmd[1], cmd[2], cmd[3]);
    LOG_CODEC("Process: first Enc1 CL[%u] cmd[0x04]=0x%08x cmd[0x05]=0x%08x cmd[0x06]=0x%08x cmd[0x07]=0x%08x cmd[0x08]=0x%08x cmd[0x09]=0x%08x",
              idx, cmd[0x04], cmd[0x05], cmd[0x06], cmd[0x07], cmd[0x08], cmd[0x09]);
    LOG_CODEC("Process: first Enc1 CL[%u] cmd[0x1b]=0x%08x cmd[0x1c]=0x%08x cmd[0x1d]=0x%08x cmd[0x1e]=0x%08x cmd[0x1f]=0x%08x",
              idx, cmd[0x1b], cmd[0x1c], cmd[0x1d], cmd[0x1e], cmd[0x1f]);
    LOG_CODEC("Process: first Enc1 CL[%u] cmd[0x04]=0x%08x cmd[0x05]=0x%08x cmd[0x06]=0x%08x cmd[0x07]=0x%08x",
              idx, cmd[0x04], cmd[0x05], cmd[0x06], cmd[0x07]);
    LOG_CODEC("Process: first Enc1 CL[%u] cmd[0x0a]=0x%08x cmd[0x0b]=0x%08x cmd[0x0c]=0x%08x cmd[0x0d]=0x%08x",
              idx, cmd[0x0a], cmd[0x0b], cmd[0x0c], cmd[0x0d]);
    LOG_CODEC("Process: first Enc1 CL[%u] cmd[0x0e]=0x%08x cmd[0x0f]=0x%08x cmd[0x10]=0x%08x cmd[0x11]=0x%08x cmd[0x12]=0x%08x",
              idx, cmd[0x0e], cmd[0x0f], cmd[0x10], cmd[0x11], cmd[0x12]);
    LOG_CODEC("Process: first Enc1 CL[%u] cmd[0x13]=0x%08x cmd[0x14]=0x%08x cmd[0x15]=0x%08x cmd[0x16]=0x%08x",
              idx, cmd[0x13], cmd[0x14], cmd[0x15], cmd[0x16]);
    LOG_CODEC("Process: first Enc1 CL[%u] cmd[0x17]=0x%08x cmd[0x18]=0x%08x cmd[0x19]=0x%08x cmd[0x1a]=0x%08x",
              idx, cmd[0x17], cmd[0x18], cmd[0x19], cmd[0x1a]);
    LOG_CODEC("Process: first Enc1 CL[%u] cmd[0x20]=0x%08x cmd[0x21]=0x%08x cmd[0x22]=0x%08x cmd[0x23]=0x%08x",
              idx, cmd[0x20], cmd[0x21], cmd[0x22], cmd[0x23]);
    LOG_CODEC("Process: first Enc1 CL[%u] cmd[0x24]=0x%08x cmd[0x25]=0x%08x cmd[0x26]=0x%08x cmd[0x27]=0x%08x",
              idx, cmd[0x24], cmd[0x25], cmd[0x26], cmd[0x27]);
    LOG_CODEC("Process: first Enc1 CL[%u] cmd[0x28]=0x%08x cmd[0x29]=0x%08x cmd[0x2a]=0x%08x cmd[0x2b]=0x%08x",
              idx, cmd[0x28], cmd[0x29], cmd[0x2a], cmd[0x2b]);
    LOG_CODEC("Process: first Enc1 CL[%u] cmd[0x2c]=0x%08x cmd[0x2d]=0x%08x cmd[0x2e]=0x%08x cmd[0x2f]=0x%08x",
              idx, cmd[0x2c], cmd[0x2d], cmd[0x2e], cmd[0x2f]);
    LOG_CODEC("Process: first Enc1 CL[%u] cmd[0x30]=0x%08x cmd[0x31]=0x%08x cmd[0x32]=0x%08x cmd[0x33]=0x%08x",
              idx, cmd[0x30], cmd[0x31], cmd[0x32], cmd[0x33]);
    LOG_CODEC("Process: first Enc1 CL[%u] cmd[0x34]=0x%08x cmd[0x35]=0x%08x cmd[0x36]=0x%08x cmd[0x37]=0x%08x",
              idx, cmd[0x34], cmd[0x35], cmd[0x36], cmd[0x37]);
    LOG_CODEC("Process: first Enc1 CL[%u] cmd[0x38]=0x%08x cmd[0x39]=0x%08x",
              idx, cmd[0x38], cmd[0x39]);
    LOG_CODEC("Process: first Enc1 CL[%u] cmd[0x60]=0x%08x cmd[0x61]=0x%08x cmd[0x6e]=0x%08x",
              idx, cmd[0x60], cmd[0x61], cmd[0x6e]);
    LOG_CODEC("Process: first Enc1 CL[%u] cmd[0x64]=0x%08x cmd[0x65]=0x%08x cmd[0x67]=0x%08x cmd[0x68]=0x%08x",
              idx, cmd[0x64], cmd[0x65], cmd[0x67], cmd[0x68]);
    LOG_CODEC("Process: first Enc1 CL[%u] cmd[0x69]=0x%08x cmd[0x6f]=0x%08x",
              idx, cmd[0x69], cmd[0x6f]);
}

static void log_busy_enc1_cmd_window(ALAvpuContext* ctx, uint32_t active_idx, unsigned int skip_count)
{
    if (!ctx || !ctx->cl_ring.map || ctx->cl_entry_size == 0 || ctx->cl_count == 0)
        return;

    const uint8_t *entry = (const uint8_t*)ctx->cl_ring.map + ((size_t)active_idx * ctx->cl_entry_size);
    const uint32_t *cmd = (const uint32_t*)entry;

    LOG_CODEC("Process: busy CL[%u] replay skip_count=%u cmd[0x0c]=0x%08x cmd[0x0d]=0x%08x cmd[0x0e]=0x%08x cmd[0x0f]=0x%08x",
              active_idx, skip_count, cmd[0x0c], cmd[0x0d], cmd[0x0e], cmd[0x0f]);
    LOG_CODEC("Process: busy CL[%u] replay skip_count=%u cmd[0x10]=0x%08x cmd[0x11]=0x%08x cmd[0x12]=0x%08x cmd[0x13]=0x%08x",
              active_idx, skip_count, cmd[0x10], cmd[0x11], cmd[0x12], cmd[0x13]);
    LOG_CODEC("Process: busy CL[%u] replay skip_count=%u cmd[0x14]=0x%08x cmd[0x15]=0x%08x cmd[0x16]=0x%08x cmd[0x17]=0x%08x cmd[0x18]=0x%08x",
              active_idx, skip_count, cmd[0x14], cmd[0x15], cmd[0x16], cmd[0x17], cmd[0x18]);
    LOG_CODEC("Process: busy CL[%u] replay skip_count=%u cmd[0x19]=0x%08x cmd[0x1a]=0x%08x cmd[0x60]=0x%08x cmd[0x61]=0x%08x cmd[0x6e]=0x%08x",
              active_idx, skip_count, cmd[0x19], cmd[0x1a], cmd[0x60], cmd[0x61], cmd[0x6e]);
    LOG_CODEC("Process: busy CL[%u] replay skip_count=%u cmd[0x20]=0x%08x cmd[0x21]=0x%08x cmd[0x22]=0x%08x cmd[0x23]=0x%08x",
              active_idx, skip_count, cmd[0x20], cmd[0x21], cmd[0x22], cmd[0x23]);
    LOG_CODEC("Process: busy CL[%u] replay skip_count=%u cmd[0x24]=0x%08x cmd[0x25]=0x%08x cmd[0x26]=0x%08x cmd[0x27]=0x%08x",
              active_idx, skip_count, cmd[0x24], cmd[0x25], cmd[0x26], cmd[0x27]);
    LOG_CODEC("Process: busy CL[%u] replay skip_count=%u cmd[0x28]=0x%08x cmd[0x29]=0x%08x cmd[0x2a]=0x%08x cmd[0x2b]=0x%08x",
              active_idx, skip_count, cmd[0x28], cmd[0x29], cmd[0x2a], cmd[0x2b]);
    LOG_CODEC("Process: busy CL[%u] replay skip_count=%u cmd[0x2c]=0x%08x cmd[0x2d]=0x%08x cmd[0x2e]=0x%08x cmd[0x2f]=0x%08x",
              active_idx, skip_count, cmd[0x2c], cmd[0x2d], cmd[0x2e], cmd[0x2f]);
    LOG_CODEC("Process: busy CL[%u] replay skip_count=%u cmd[0x30]=0x%08x cmd[0x31]=0x%08x cmd[0x32]=0x%08x cmd[0x33]=0x%08x",
              active_idx, skip_count, cmd[0x30], cmd[0x31], cmd[0x32], cmd[0x33]);
    LOG_CODEC("Process: busy CL[%u] replay skip_count=%u cmd[0x34]=0x%08x cmd[0x35]=0x%08x cmd[0x36]=0x%08x cmd[0x37]=0x%08x",
              active_idx, skip_count, cmd[0x34], cmd[0x35], cmd[0x36], cmd[0x37]);
    LOG_CODEC("Process: busy CL[%u] replay skip_count=%u cmd[0x38]=0x%08x cmd[0x39]=0x%08x",
              active_idx, skip_count, cmd[0x38], cmd[0x39]);
    LOG_CODEC("Process: busy CL[%u] replay skip_count=%u cmd[0x64]=0x%08x cmd[0x65]=0x%08x cmd[0x67]=0x%08x cmd[0x68]=0x%08x cmd[0x69]=0x%08x cmd[0x6f]=0x%08x",
              active_idx, skip_count, cmd[0x64], cmd[0x65], cmd[0x67], cmd[0x68], cmd[0x69], cmd[0x6f]);
}

static void avpu_promote_reference(ALAvpuContext *ctx)
{
    if (!ctx || !ctx->rec_buf.phy_addr || !ctx->ref_buf.phy_addr)
        return;

    AvpuDMABuf prev_ref = ctx->ref_buf;
    AvpuDMABuf prev_trace_ref = ctx->ref_trace_buf;

    ctx->ref_buf = ctx->rec_buf;
    ctx->rec_buf = prev_ref;
    ctx->ref_trace_buf = ctx->rec_trace_buf;
    ctx->rec_trace_buf = prev_trace_ref;
    __sync_synchronize();
    ctx->reference_valid = 1;

    LOG_CODEC("EndEncoding: promoted rec->ref ref=0x%08x next_rec=0x%08x trace_ref=0x%08x next_trace=0x%08x",
              ctx->ref_buf.phy_addr, ctx->rec_buf.phy_addr,
              ctx->ref_trace_buf.phy_addr, ctx->rec_trace_buf.phy_addr);
}

/* OEM uses 24-bit interrupt clear (0xFFFFFF), not 32-bit.
 * Writing to non-existent upper bits can hang the bus on T31. */
#define AVPU_IRQ_CLEAR_MASK 0x00FFFFFFu

/* Enable Encode1 interrupt for core.
 * OEM encode1 calls AL_EncCore_EnableInterrupts(..., num_cores, 0), while the
 * separate Encode2 path explicitly uses AL_EncCore_EnableEnc2Interrupt(). Keep
 * the submit path limited to the base Enc1 bit until the remaining arguments of
 * AL_EncCore_EnableInterrupts are fully recovered. */
static void avpu_enable_interrupts(int fd, int core)
{
    /* Stock libimp uses IRQ mask 0x11 (bits 0 + 4), NOT 0x05 (bits 0 + 2).
     * Bit 0 = Enc1 complete for core 0
     * Bit 4 = Enc1 complete for core 1 (or Enc2 on some configs)
     * The stock NEVER uses bit 2. Match stock exactly. */
    unsigned add_m = 0x11u; /* stock value */
    (void)core;
    unsigned old_m = 0;
    unsigned new_m = add_m;

    if (avpu_read_reg_quiet(fd, AVPU_INTERRUPT_MASK, &old_m) == 0) {
        new_m = old_m | add_m;
    }

    LOG_CODEC("AVPU: enable_interrupts core=%d old=0x%08x add=0x%08x new=0x%08x",
              core, old_m, add_m, new_m);
    avpu_write_reg(fd, AVPU_INTERRUPT_MASK, new_m);
    LOG_CODEC("AVPU: wrote INTERRUPT_MASK=0x%08x", new_m);
}

/* OEM parity: TurnOnGC.constprop.36 → SetClockCommand(ip_ctrl, core, 1)
 * which read-modify-writes bits[1:0] of (core<<9)+0x83F4. */
static void avpu_turn_on_gc(int fd, int core)
{
    unsigned int old = 0;
    unsigned int new_val = 1u;

    if (avpu_read_reg(fd, AVPU_REG_CORE_CLKCMD(core), &old) == 0) {
        new_val = ((old ^ 1u) & 0x3u) ^ old;
        LOG_CODEC("AVPU: TurnOnGC core=%d clkcmd old=0x%08x new=0x%08x", core, old, new_val);
    } else {
        LOG_CODEC("AVPU: TurnOnGC core=%d clkcmd read failed; forcing 0x00000001", core);
    }

    avpu_write_reg(fd, AVPU_REG_CORE_CLKCMD(core), new_val);
}

/* OEM SetClockCommand(ip, core, 0) — turn OFF clock gate via read-modify-write */
static void avpu_turn_off_gc(int fd, int core)
{
    unsigned int old = 0;
    unsigned int new_val = 0u;

    if (avpu_read_reg_quiet(fd, AVPU_REG_CORE_CLKCMD(core), &old) == 0) {
        new_val = ((old ^ 0u) & 0x3u) ^ old; /* clear bits[1:0] */
    }

    avpu_write_reg(fd, AVPU_REG_CORE_CLKCMD(core), new_val);
}

/* Fifo_Init - based on decompilation at 0x7af28 */
static int fifo_init(long *fifo, int max_elements)
{
    fifo[0] = max_elements + 1;
    fifo[1] = 0; /* write_idx */
    fifo[2] = 0; /* read_idx */
    fifo[6] = 0; /* count */
    fifo[7] = 0; /* flag (stored as long, but OEM uses byte) */

    /* Allocate buffer */
    void *buf = malloc((max_elements + 1) * sizeof(void*));
    if (!buf) return 0;
    memset(buf, 0xcd, (max_elements + 1) * sizeof(void*));
    fifo[3] = (long)buf;

    /* Create synchronization primitives */
    pthread_mutex_t *mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
    pthread_cond_t *cond = (pthread_cond_t*)malloc(sizeof(pthread_cond_t));

    if (!mutex || !cond) {
        free(buf);
        free(mutex);
        free(cond);
        return 0;
    }

    pthread_mutex_init(mutex, NULL);
    pthread_cond_init(cond, NULL);

    fifo[4] = (long)mutex;
    fifo[5] = (long)cond;  /* OEM uses event, we use cond var */
    fifo[8] = 0; /* semaphore - not used in our implementation */

    return 1;
}

/* Fifo_Queue - based on decompilation at 0x7b254 */
static int fifo_queue(long *fifo, void *item, unsigned int timeout_ms)
{
    pthread_mutex_t *mutex = (pthread_mutex_t*)fifo[4];
    pthread_cond_t *cond = (pthread_cond_t*)fifo[5];

    pthread_mutex_lock(mutex);

    int max = (int)fifo[0];
    int write_idx = (int)fifo[1];
    void **buf = (void**)fifo[3];

    buf[write_idx] = item;
    fifo[6]++; /* increment count */
    fifo[1] = (write_idx + 1) % max;

    pthread_cond_signal(cond);
    pthread_mutex_unlock(mutex);

    return 1;
}

/* Fifo_Dequeue - based on decompilation at 0x7b384 */
static void* fifo_dequeue(long *fifo, unsigned int timeout_ms)
{
    pthread_mutex_t *mutex = (pthread_mutex_t*)fifo[4];
    pthread_cond_t *cond = (pthread_cond_t*)fifo[5];

    pthread_mutex_lock(mutex);

    int count = (int)fifo[6];

    /* Wait for data if empty */
    while (count <= 0) {
        if (timeout_ms == 0xffffffff) {
            /* Infinite wait */
            pthread_cond_wait(cond, mutex);
        } else {
            /* Timed wait */
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += timeout_ms / 1000;
            ts.tv_nsec += (timeout_ms % 1000) * 1000000;
            if (ts.tv_nsec >= 1000000000) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000;
            }

            if (pthread_cond_timedwait(cond, mutex, &ts) != 0) {
                pthread_mutex_unlock(mutex);
                return NULL;
            }
        }
        count = (int)fifo[6];
    }

    int max = (int)fifo[0];
    int read_idx = (int)fifo[2];
    void **buf = (void**)fifo[3];

    void *result = buf[read_idx];
    fifo[6]--; /* decrement count */
    fifo[2] = (read_idx + 1) % max;

    pthread_mutex_unlock(mutex);

    return result;
}

/* EndEncoding callback - based on OEM's EndEncoding at 0x443b0
 * Called when encoding completes for a frame.
 * This is the callback registered for encoding interrupts.
 *
 * IMPORTANT: This runs in the IRQ thread context (NOT interrupt context),
 * so it's safe to do blocking operations like malloc and FIFO operations.
 */
static void avpu_end_encoding_callback(void *user_data)
{
    ALAvpuContext *ctx = (ALAvpuContext*)user_data;

    LOG_CODEC("EndEncoding callback: encoding completed");

    /* OEM reads status registers from command list entries
     * AL_EncCore_ReadStatusRegsEnc at 0x6d218:
     * - Reads from CL entries at 0x200 byte intervals
     * - Calls EncodingStatusRegsToSliceStatus
     * - Merges status into result structure
     */

    avpu_promote_reference(ctx);
    __sync_fetch_and_add(&ctx->frames_encoded, 1);

    /* Do NOT reset from callback — writing registers from IRQ thread context
     * while hardware is still active causes a hard lockup. The reset is done
     * in the submission path (Process) before the next CL_PUSH instead. */

    LOG_CODEC("EndEncoding: frames_encoded=%d", ctx->frames_encoded);
}

/* EndAvcEntropy callback - separate OEM IRQ slot (core*4+2).
 * We keep this side-effect free until the exact entropy-completion bookkeeping
 * is recovered; critically, it must NOT be treated as EndEncoding. */
static void avpu_end_avc_entropy_callback(void *user_data)
{
    ALAvpuContext *ctx = (ALAvpuContext*)user_data;
    (void)ctx;
    LOG_CODEC("EndAvcEntropy callback");
}

static void avpu_log_gate_regs(ALAvpuContext *ctx, const char *tag)
{
    unsigned int misc_ctrl = 0;
    unsigned int top_ctrl = 0;
    unsigned int axi_off = 0;
    unsigned int enc_en_a = 0;
    unsigned int enc_en_b = 0;
    unsigned int enc_en_c = 0;
    int have_misc_ctrl;
    int have_top_ctrl;
    int have_axi_off;
    int have_enc_en_a;
    int have_enc_en_b;
    int have_enc_en_c;

    if (!ctx || !tag)
        return;

    have_misc_ctrl = (avpu_read_reg_quiet(ctx->fd, AVPU_REG_MISC_CTRL, &misc_ctrl) == 0);
    have_top_ctrl = (avpu_read_reg_quiet(ctx->fd, AVPU_REG_TOP_CTRL, &top_ctrl) == 0);
    have_axi_off = (avpu_read_reg_quiet(ctx->fd, AVPU_REG_AXI_ADDR_OFFSET_IP, &axi_off) == 0);
    have_enc_en_a = (avpu_read_reg_quiet(ctx->fd, AVPU_REG_ENC_EN_A, &enc_en_a) == 0);
    have_enc_en_b = (avpu_read_reg_quiet(ctx->fd, AVPU_REG_ENC_EN_B, &enc_en_b) == 0);
    have_enc_en_c = (avpu_read_reg_quiet(ctx->fd, AVPU_REG_ENC_EN_C, &enc_en_c) == 0);

    LOG_CODEC(
        "AVPU: %s gates misc_ctrl=%s0x%08x top_ctrl=%s0x%08x axi_off=%s0x%08x enc_en_a=%s0x%08x enc_en_b=%s0x%08x enc_en_c=%s0x%08x",
        tag,
        have_misc_ctrl ? "" : "ERR:", misc_ctrl,
        have_top_ctrl ? "" : "ERR:", top_ctrl,
        have_axi_off ? "" : "ERR:", axi_off,
        have_enc_en_a ? "" : "ERR:", enc_en_a,
        have_enc_en_b ? "" : "ERR:", enc_en_b,
        have_enc_en_c ? "" : "ERR:", enc_en_c);
}

static void avpu_probe_reg_write(int fd, const char *tag, unsigned int off, unsigned int expected,
                                 volatile int *out_write_ret, volatile int *out_read_ret,
                                 volatile unsigned int *out_read_val)
{
    unsigned int read_val = 0;
    int write_ret;
    int read_ret;

    if (fd < 0 || !tag)
        return;

    write_ret = avpu_write_reg(fd, off, expected);
    read_ret = avpu_read_reg_quiet(fd, off, &read_val);

    if (out_write_ret)
        *out_write_ret = write_ret;
    if (out_read_ret)
        *out_read_ret = read_ret;
    if (out_read_val)
        *out_read_val = read_val;

    LOG_CODEC(
        "AVPU: %s off=0x%08x write_ret=%d expected=0x%08x read_ret=%d read_val=0x%08x",
        tag,
        off,
        write_ret,
        expected,
        read_ret,
        read_val);
}

static void avpu_log_busy_snapshot(ALAvpuContext *ctx, uint32_t idx, unsigned int core_status)
{
    /* Emit every requested stuck-path snapshot. The earlier one-shot guard caused
     * the most useful snapshot to age out of the device ring buffer before the
     * retained logs were collected, leaving only the CL replay lines. */
    if (!ctx) {
        return;
    }

    unsigned int irq_mask = 0;
    unsigned int irq_pending = 0;
    unsigned int clkcmd = 0;
    unsigned int cl_addr = 0;
    unsigned int wpp_core0_reset = 0;
    unsigned int status_8230 = 0;
    unsigned int status_8234 = 0;
    unsigned int status_8238 = 0;
    int have_irq_mask = (avpu_read_reg_quiet(ctx->fd, AVPU_INTERRUPT_MASK, &irq_mask) == 0);
    int have_irq_pending = (avpu_read_reg_quiet(ctx->fd, AVPU_INTERRUPT, &irq_pending) == 0);
    int have_clkcmd = (avpu_read_reg_quiet(ctx->fd, AVPU_REG_CORE_CLKCMD(0), &clkcmd) == 0);
    int have_cl_addr = (avpu_read_reg_quiet(ctx->fd, AVPU_REG_CL_ADDR, &cl_addr) == 0);
    int have_wpp_core0_reset = (avpu_read_reg_quiet(ctx->fd, AVPU_REG_WPP_CORE0_RESET(0), &wpp_core0_reset) == 0);
    int have_status_8230 = (avpu_read_reg_quiet(ctx->fd, AVPU_REG_CORE_STATUS_8230(0), &status_8230) == 0);
    int have_status_8234 = (avpu_read_reg_quiet(ctx->fd, AVPU_REG_CORE_STATUS_8234(0), &status_8234) == 0);
    int have_status_8238 = (avpu_read_reg_quiet(ctx->fd, AVPU_REG_CORE_STATUS_8238(0), &status_8238) == 0);
    int wait_errno = ctx->irq_wait_errno;

    LOG_CODEC(
        "AVPU: busy snapshot CL[%u] core_status=0x%08x irq_mask=%s0x%08x irq_pending=%s0x%08x clkcmd=%s0x%08x cl_addr=%s0x%08x wpp_reset=%s0x%08x stat8230=%s0x%08x stat8234=%s0x%08x stat8238=%s0x%08x session_ready=%d frames_encoded=%d irq_thread_running=%d irq_thread_started=%d irq_thread_exited=%d last_irq=%d wait_irq_errno=%d (%s) init_trace=%d stream_flush_failures=%d interm_flush_ret=%d cl_flush_ret=%d",
        idx,
        core_status,
        have_irq_mask ? "" : "ERR:", irq_mask,
        have_irq_pending ? "" : "ERR:", irq_pending,
        have_clkcmd ? "" : "ERR:", clkcmd,
        have_cl_addr ? "" : "ERR:", cl_addr,
        have_wpp_core0_reset ? "" : "ERR:", wpp_core0_reset,
        have_status_8230 ? "" : "ERR:", status_8230,
        have_status_8234 ? "" : "ERR:", status_8234,
        have_status_8238 ? "" : "ERR:", status_8238,
        ctx->session_ready,
        ctx->frames_encoded,
        ctx->irq_thread_running,
        ctx->irq_thread_started,
        ctx->irq_thread_exited,
        ctx->last_irq_id,
        wait_errno,
        wait_errno ? strerror(wait_errno) : "ok",
        ctx->init_trace_completed,
        ctx->init_stream_flush_failures,
        ctx->init_interm_flush_ret,
        ctx->init_cl_flush_ret);

    LOG_CODEC(
        "AVPU: busy sticky CL[%u] core_status=0x%08x cl_addr=%s0x%08x frames_encoded=%d last_irq=%d wait_irq_errno=%d init_trace=%d stream_flush_failures=%d interm_flush_ret=%d cl_flush_ret=%d init_misc={w:%d r:%d val:0x%08x} init_top={w:%d r:%d val:0x%08x} status_regs={820c:%s0x%08x 8230:%s0x%08x 8234:%s0x%08x 8238:%s0x%08x}",
        idx,
        core_status,
        have_cl_addr ? "" : "ERR:", cl_addr,
        ctx->frames_encoded,
        ctx->last_irq_id,
        wait_errno,
        ctx->init_trace_completed,
        ctx->init_stream_flush_failures,
        ctx->init_interm_flush_ret,
        ctx->init_cl_flush_ret,
        ctx->init_misc_write_ret,
        ctx->init_misc_read_ret,
        ctx->init_misc_read_val,
        ctx->init_top_write_ret,
        ctx->init_top_read_ret,
        ctx->init_top_read_val,
        have_wpp_core0_reset ? "" : "ERR:", wpp_core0_reset,
        have_status_8230 ? "" : "ERR:", status_8230,
        have_status_8234 ? "" : "ERR:", status_8234,
        have_status_8238 ? "" : "ERR:", status_8238);

    avpu_log_gate_regs(ctx, "busy");
}

static void avpu_log_submit_snapshot(ALAvpuContext *ctx, uint32_t idx, const char *tag)
{
    unsigned int core_status = 0;
    unsigned int irq_mask = 0;
    unsigned int irq_pending = 0;
    unsigned int clkcmd = 0;
    unsigned int cl_addr = 0;
    int have_status;
    int have_irq_mask;
    int have_irq_pending;
    int have_clkcmd;
    int have_cl_addr;

    if (!ctx || !tag)
        return;

    have_status = (avpu_read_reg_quiet(ctx->fd, AVPU_REG_CORE_STATUS(0), &core_status) == 0);
    have_irq_mask = (avpu_read_reg_quiet(ctx->fd, AVPU_INTERRUPT_MASK, &irq_mask) == 0);
    have_irq_pending = (avpu_read_reg_quiet(ctx->fd, AVPU_INTERRUPT, &irq_pending) == 0);
    have_clkcmd = (avpu_read_reg_quiet(ctx->fd, AVPU_REG_CORE_CLKCMD(0), &clkcmd) == 0);
    have_cl_addr = (avpu_read_reg_quiet(ctx->fd, AVPU_REG_CL_ADDR, &cl_addr) == 0);

    LOG_CODEC(
        "AVPU: submit snapshot %s CL[%u] core_status=%s0x%08x irq_mask=%s0x%08x irq_pending=%s0x%08x clkcmd=%s0x%08x cl_addr=%s0x%08x",
        tag,
        idx,
        have_status ? "" : "ERR:", core_status,
        have_irq_mask ? "" : "ERR:", irq_mask,
        have_irq_pending ? "" : "ERR:", irq_pending,
        have_clkcmd ? "" : "ERR:", clkcmd,
        have_cl_addr ? "" : "ERR:", cl_addr);
}

/* OEM parity: IsEnc1AlreadyRunning() reads (core<<9)+0x83f8 and checks bit 1.
 * If this bit is set, AL_EncCore_Encode1() does not push a new command list. */
static int avpu_is_enc1_running(int fd, int core, unsigned int *out_status)
{
    unsigned int status = 0;

    if (avpu_read_reg_quiet(fd, AVPU_REG_CORE_STATUS(core), &status) != 0) {
        LOG_CODEC("AVPU: failed to read Enc1 running state for core %d", core);
        return 0;
    }

    if (out_status) {
        *out_status = status;
    }

    return (status & 0x2u) ? 1 : 0;
}

/* LinuxIpCtrl_RegisterCallBack - based on decompilation at 0x35fd0
 * Register a callback for a specific interrupt ID (0-19).
 * OEM signature: void LinuxIpCtrl_RegisterCallBack(void *ctx, void (*callback)(void*), void *user_data, int irq_id)
 */
static void avpu_register_callback(ALAvpuContext *ctx, void (*callback)(void*), void *user_data, int irq_id)
{
    if (irq_id < 0 || irq_id >= 20) {
        LOG_CODEC("AVPU: invalid IRQ ID %d for callback registration", irq_id);
        return;
    }

    /* OEM: Rtos_GetMutex(*(arg1 + 0xc)) */
    pthread_mutex_lock((pthread_mutex_t*)ctx->irq_mutex);

    /* OEM: Calculate offset: arg1 + (irq_id * 16 - irq_id * 4) + 0x10 = arg1 + (irq_id * 12) + 0x10 */
    int idx = irq_id * 3; /* 3 ints per entry: [callback_fn, user_data, flag] */
    ctx->irq_callbacks[idx] = (long)callback;
    ctx->irq_callbacks[idx + 1] = (long)user_data;

    /* OEM: if (arg2 != 0) ... else *($v0_1 + 0x14) = 1 */
    if (callback != NULL) {
        /* callback is set, clear flag */
        ctx->irq_callbacks[idx + 2] = 0;
    } else {
        /* callback is NULL, set flag to 1 */
        ctx->irq_callbacks[idx + 2] = 1;
    }

    /* OEM: Rtos_ReleaseMutex(...) */
    pthread_mutex_unlock((pthread_mutex_t*)ctx->irq_mutex);

    LOG_CODEC("AVPU: registered callback for IRQ %d (callback=%p, user_data=%p)",
              irq_id, callback, user_data);
}

/* WaitInterruptThread - based on decompilation at 0x35e28
 * This thread waits for AVPU interrupts and dispatches registered callbacks.
 * The OEM uses this for encoding completion notifications.
 */
static void* avpu_irq_thread(void* arg)
{
    ALAvpuContext* ctx = (ALAvpuContext*)arg;
    int fd = ctx->fd;

    ctx->irq_thread_started = 1;
    ctx->irq_thread_exited = 0;
    ctx->irq_wait_errno = 0;
    ctx->last_irq_id = -1;

    LOG_CODEC("IRQ thread: started for fd=%d", fd);

    while (ctx->irq_thread_running) {
        /* Use heap buffer with slack for WAIT_IRQ return value to avoid over-copy issues */
        size_t buf_sz = sizeof(uint32_t) + 0x40;
        void *raw = NULL;
        if (posix_memalign(&raw, 16, buf_sz) != 0 || !raw) {
            LOG_CODEC("IRQ thread: posix_memalign failed");
            break;
        }
        memset(raw, 0xFF, buf_sz);
        uint32_t *p_irq = (uint32_t*)raw;
        *p_irq = 0xFFFFFFFFu;

        /* ioctl($a0_2, 0xc004710c, &var_28) - AL_CMD_IP_WAIT_IRQ */
        if (avpu_sys_ioctl(fd, AL_CMD_IP_WAIT_IRQ, p_irq) == -1) {
            if (errno == EINTR) {
                free(raw);
                continue; /* interrupted by signal, retry */
            }
            if (errno != EINTR) {
                ctx->irq_wait_errno = errno;
                LOG_CODEC("IRQ thread: WAIT_IRQ failed: %s (%d)", strerror(errno), errno);
            }
            free(raw);
            break;
        }

        uint32_t irq_id = *p_irq;
        free(raw);
        ctx->irq_wait_errno = 0;

        /* OEM: if (var_28 u>= 0x14) fprintf(stderr, ...) */
        if (irq_id >= 20) {
            LOG_CODEC("IRQ thread: invalid IRQ ID %d", irq_id);
            continue;
        }

        ctx->last_irq_id = (int)irq_id;
        LOG_CODEC("IRQ thread: IRQ %d received", irq_id);

        /* OEM: Rtos_GetMutex(*(arg1 + 0xc)) */
        pthread_mutex_lock((pthread_mutex_t*)ctx->irq_mutex);

        /* OEM: Calculate callback offset: arg1 + (irq_id * 16 - irq_id * 4) + 0x10
         * This is: arg1 + (irq_id * 12) + 0x10
         * Array of 20 entries, each 12 bytes: [callback_fn, user_data, flag]
         */
        int idx = irq_id * 3; /* 3 ints per entry */
        void (*callback)(void*) = (void(*)(void*))ctx->irq_callbacks[idx];
        void *user_data = (void*)ctx->irq_callbacks[idx + 1];
        int flag = ctx->irq_callbacks[idx + 2];

        /* OEM: if ($t9_1 != 0) $t9_1(...) else if (flag == 0) fprintf(stderr, ...) */
        if (callback != NULL) {
            callback(user_data);
        } else if (flag == 0) {
            LOG_CODEC("IRQ thread: Interrupt %d doesn't have a handler", irq_id);
        }

        /* OEM: Rtos_ReleaseMutex(*(arg1 + 0xc)) */
        pthread_mutex_unlock((pthread_mutex_t*)ctx->irq_mutex);
    }

    ctx->irq_thread_exited = 1;
    LOG_CODEC("IRQ thread: exiting");
    return NULL;
}

/* Compute effective AnnexB stream size (trim trailing zeros) */
static size_t annexb_effective_size(const uint8_t *buf, size_t maxlen)
{
    if (!buf || maxlen < 4) return 0;
    size_t first = (size_t)-1;
    size_t last = 0;
    for (size_t i = 0; i + 3 < maxlen; ++i) {
        if ((buf[i] == 0 && buf[i+1] == 0 && buf[i+2] == 1) ||
            (i + 4 < maxlen && buf[i] == 0 && buf[i+1] == 0 && buf[i+2] == 0 && buf[i+3] == 1)) {
            if (first == (size_t)-1) first = i;
            last = i;
        }
    }
    if (first == (size_t)-1) return 0;
    size_t end = maxlen;
    for (size_t i = last + 3; i + 3 < maxlen; ++i) {
        if ((buf[i] == 0 && buf[i+1] == 0 && buf[i+2] == 1) ||
            (i + 4 < maxlen && buf[i] == 0 && buf[i+1] == 0 && buf[i+2] == 0 && buf[i+3] == 1)) {
            end = i; break;
        }
    }
    if (end == maxlen) {
        while (end > first && buf[end-1] == 0) end--;
    }
    return (end > first) ? (end - first) : 0;
}

/* Codec structure - based on decompilation at 0x7950c */
/* Size: 0x924 bytes */
typedef struct AL_CodecEncode AL_CodecEncode;

struct AL_CodecEncode {
    void *g_pCodec;                 /* 0x000: Global codec pointer */
    uint8_t codec_param[0x794];     /* 0x004: Codec parameters */
    void *encoder;                  /* 0x798: AL_Encoder handle */
    void *event;                    /* 0x79c: Event handle */
    void *callback;                 /* 0x7a0: Callback function */
    void *callback_arg;             /* 0x7a4: Callback argument */
    int channel_id;                 /* 0x7a8: Channel ID + 1 */

    /* Stream buffer pool config - offsets from decompilation */
    int stream_buf_count;           /* 0x7ac: Stream buffer count */
    int stream_buf_size;            /* 0x7b0: Stream buffer size */
    uint8_t stream_pool[0x44];      /* 0x7b4-0x7f7: Stream buffer pool */

    /* FIFOs (control structures allocated dynamically to ensure proper size) */
    void *fifo_frames;              /* Frame FIFO control block */
    void *fifo_streams;             /* Stream FIFO control block */

    /* Frame buffer pool config */
    uint8_t frame_pool_config[0x60]; /* 0x840-0x89f: Frame pool config */
    int frame_buf_count;            /* 0x840: Frame buffer count (from GetSrcFrameCntAndSize) */
    uint8_t frame_pool_data[0x9c];  /* 0x8a0-0x8db: Frame pool data */
    int frame_buf_size;             /* 0x8dc: Frame buffer size (from GetSrcFrameCntAndSize) */

    /* Pixel map buffer pool */
    uint8_t pixmap_pool[0x3c];      /* 0x8e0-0x91b: PixMap buffer pool */
    int frame_count;                /* 0x91c: Frame count */
    int src_fourcc;                 /* 0x918: Source FourCC */
    int metadata_type;              /* 0x920: Metadata type */

    /* Extended fields (not part of binary structure) */
    int hw_encoder_fd;              /* Hardware encoder file descriptor */
    HWEncoderParams hw_params;      /* Hardware encoder parameters */
    int use_hardware;               /* Flag: 1=hardware, 0=software */
    uint32_t entropy_mode;          /* 0=CAVLC, 1=CABAC */
    ALAvpuContext avpu;            /* Vendor-like AL over /dev/avpu (scaffolding) */
};

static void codec_queue_frame_metadata(AL_CodecEncode *enc, void *user_data)
{
    if (enc == NULL || user_data == NULL || enc->fifo_frames == NULL) return;
    if (Fifo_Queue(enc->fifo_frames, user_data, -1) == 0) {
        LOG_CODEC("Process: failed to queue frame metadata %p", user_data);
    }
}

static void *codec_dequeue_frame_metadata(AL_CodecEncode *enc)
{
    if (enc == NULL || enc->fifo_frames == NULL) return NULL;
    return Fifo_Dequeue(enc->fifo_frames, 0);
}

/* Global codec state */
static void *g_pCodec = NULL;
static pthread_mutex_t g_codec_mutex = PTHREAD_MUTEX_INITIALIZER;
static AL_CodecEncode *g_codec_instances[6] = {NULL};

/* Single-owner gate for AVPU to avoid noisy second opens */
static pthread_mutex_t g_avpu_owner_mutex = PTHREAD_MUTEX_INITIALIZER;
static int g_avpu_owner_channel = 0;

/**
 * AL_Codec_Encode_SetDefaultParam - based on decompilation at 0x790b8
 * Sets default encoding parameters
 */
int AL_Codec_Encode_SetDefaultParam(void *param) {
    if (param == NULL) {
        LOG_CODEC("SetDefaultParam: NULL param");
        return -1;
    }

    /* Clear entire structure */
    memset(param, 0, 0x794);

    /* Set default values from decompilation */
    uint8_t *p = (uint8_t*)param;

    /* Basic settings */
    *(int32_t*)(p + 0x00) = 0;          /* codec type */
    *(int32_t*)(p + 0x04) = 0;          /* reserved */
    *(int32_t*)(p + 0x10) = 0x188;      /* NV12 8-bit 4:2:0 format word */
    *(int32_t*)(p + 0x14) = 0x188;      /* width default */
    *(int32_t*)(p + 0x1c) = 8;          /* bit depth */
    *(int32_t*)(p + 0x20) = 0x1000001;  /* H264 codec */
    *(int32_t*)(p + 0x24) = 0x32;       /* profile (50 = high) */
    *(int32_t*)(p + 0x34) = 0x1c;       /* level */
    *(int32_t*)(p + 0x30) = 0x40000;    /* bitrate */

    /* QP settings */
    *(uint8_t*)(p + 0x38) = 0xff;       /* initial QP */
    *(uint8_t*)(p + 0x39) = 0xff;       /* min QP */
    *(uint8_t*)(p + 0x3f) = 1;          /* enable QP */
    *(uint8_t*)(p + 0x44) = 1;          /* enable rate control */

    /* GOP settings */
    *(uint16_t*)(p + 0x4e) = 0xffff;
    *(uint16_t*)(p + 0x50) = 0xffff;
    *(uint16_t*)(p + 0x4a) = 0xffff;
    *(uint16_t*)(p + 0x4c) = 0xffff;

    /* Rate control */
    *(uint8_t*)(p + 0x53) = 3;          /* RC mode */
    *(uint16_t*)(p + 0x8a) = 0xffff;
    *(uint16_t*)(p + 0x8c) = 0xffff;
    *(uint8_t*)(p + 0x55) = 2;
    *(int32_t*)(p + 0x90) = 2;
    *(uint8_t*)(p + 0x6a) = 0xf;
    *(uint16_t*)(p + 0x92) = 0xa;
    *(uint16_t*)(p + 0x94) = 0x11;

    /* Timing */
    *(int32_t*)(p + 0x7c) = 0xaae60;    /* framerate num */
    *(int32_t*)(p + 0x80) = 0xaae60;    /* framerate den */
    *(int32_t*)(p + 0x9c) = 0x1068;

    /* Slice settings */
    *(uint8_t*)(p + 0x52) = 5;
    *(uint8_t*)(p + 0x54) = 5;
    *(int32_t*)(p + 0x74) = 0x41eb0;
    *(uint16_t*)(p + 0x78) = 0x19;
    *(uint16_t*)(p + 0x7a) = 0x3e8;
    *(uint16_t*)(p + 0x84) = 0x19;
    *(uint16_t*)(p + 0x88) = 0x33;

    /* Enable flags */
    *(uint8_t*)(p + 0x56) = 1;
    *(uint8_t*)(p + 0x57) = 1;
    *(uint8_t*)(p + 0x58) = 1;
    *(uint8_t*)(p + 0x6c) = 1;

    /* GOP parameters */
    *(int32_t*)(p + 0xac) = 2;
    *(int32_t*)(p + 0xb4) = 0x7fffffff;
    *(int32_t*)(p + 0xcc) = 3;          /* buffer count */
    *(int32_t*)(p + 0x100) = 4;
    *(int32_t*)(p + 0xb0) = 0x19;       /* GOP length */
    *(int32_t*)(p + 0xe8) = 5;
    *(int32_t*)(p + 0x104) = 5;
    *(uint8_t*)(p + 0x108) = 1;
    *(uint8_t*)(p + 0x10c) = 1;
    *(uint8_t*)(p + 0x110) = 1;
    *(uint8_t*)(p + 0x116) = 1;
    *(uint8_t*)(p + 0x11c) = 1;
    *(uint8_t*)(p + 0x124) = 1;
    *(uint8_t*)(p + 0x128) = 1;

    /* Pixel format */
    strncpy((char*)(p + 0x764), "NV12", 4);
    *(uint8_t*)(p + 0x758) = 1;
    *(uint8_t*)(p + 0x760) = 1;
    *(uint8_t*)(p + 0x768) = 1;
    *(uint8_t*)(p + 0x76c) = 0x10;      /* alignment */

    /* OEM-aligned zeroing of subregions */
    memset(p + 0x12c, 0, 0x600);
    memset(p + 0x72c, 0, 0x18);
    memset(p + 0x744, 0, 8);
    memset(p + 0x74c, 0, 8);
    *(uint32_t*)(p + 0x754) = 0;
    *(uint8_t*)(p + 0x769) = 0; /* per OEM defaults */
    *(uint32_t*)(p + 0x770) = 0;
    *(uint32_t*)(p + 0x774) = 0;
    *(uint32_t*)(p + 0x778) = 0;
    *(uint32_t*)(p + 0x77c) = 0;
    *(uint32_t*)(p + 0x780) = 0;
    *(uint32_t*)(p + 0x784) = 0;

    LOG_CODEC("SetDefaultParam: initialized (OEM-aligned)");
    return 0;
}

/**
 * AL_Codec_Encode_GetSrcFrameCntAndSize - based on decompilation at 0x7a694
 * Returns frame buffer count and size
 */
int AL_Codec_Encode_GetSrcFrameCntAndSize(void *codec, int *cnt, int *size) {
    if (codec == NULL || cnt == NULL || size == NULL) {
        return -1;
    }

    AL_CodecEncode *enc = (AL_CodecEncode*)codec;

    /* From decompilation: offsets 0x840 and 0x8dc */
    *cnt = enc->frame_buf_count;
    *size = enc->frame_buf_size;

    return 0;
}

/**
 * AL_Codec_Encode_GetSrcStreamCntAndSize - based on decompilation at 0x7a6ac
 * Returns stream buffer count and size
 */
int AL_Codec_Encode_GetSrcStreamCntAndSize(void *codec, int *cnt, int *size) {
    if (codec == NULL || cnt == NULL || size == NULL) {
        return -1;
    }

    AL_CodecEncode *enc = (AL_CodecEncode*)codec;

    /* From decompilation: offsets 0x7ac and 0x7b0 */
    *cnt = enc->stream_buf_count;
    *size = enc->stream_buf_size;

    return 0;
}

/**
 * AL_Codec_Encode_Create - based on decompilation at 0x7950c
 * Creates a codec encoder instance
 */
int AL_Codec_Encode_Create(void **codec, void *params) {
    if (codec == NULL || params == NULL) {
        LOG_CODEC("Create: NULL parameters");
        return -1;
    }

    /* Allocate codec structure using real size */
    AL_CodecEncode *enc = (AL_CodecEncode*)malloc(sizeof(AL_CodecEncode));
    if (enc == NULL) {
        LOG_CODEC("Create: malloc failed");
        return -1;
    }

    memset(enc, 0, sizeof(AL_CodecEncode));

    /* Initialize from parameters */
    enc->g_pCodec = g_pCodec;
    memcpy(enc->codec_param, params, 0x794);
    {
        uint32_t profile_idc = *(uint32_t*)(enc->codec_param + 0x24);
        enc->entropy_mode = (profile_idc == IMP_ENC_AVC_PROFILE_IDC_BASELINE) ? 0u : 1u;
    }

    /* OEM-like callback placeholders and event */
    enc->callback = NULL;
    enc->callback_arg = enc;
    int efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (efd >= 0) enc->event = (void*)(uintptr_t)efd;

    /* Set default buffer counts and sizes */
    enc->frame_buf_count = 4;           /* Default frame buffer count */
    enc->frame_buf_size = 0x100000;     /* 1MB per frame */
    enc->stream_buf_count = 7;          /* Default stream buffer count */
    enc->stream_buf_size = 0x20000;     /* 128KB stream buffer size (for encoded H.264 data) */

    /* Allocate and initialize FIFO control structures safely */
    int fifo_size = Fifo_SizeOf();
    enc->fifo_frames = malloc(fifo_size);
    enc->fifo_streams = malloc(fifo_size);
    if (enc->fifo_frames == NULL || enc->fifo_streams == NULL) {
        LOG_CODEC("Create: FIFO alloc failed");
        if (enc->fifo_frames) free(enc->fifo_frames);
        if (enc->fifo_streams) free(enc->fifo_streams);
        free(enc);
        return -1;
    }
    Fifo_Init(enc->fifo_frames, enc->frame_buf_count);
    Fifo_Init(enc->fifo_streams, enc->stream_buf_count);

    /* Set source FourCC to NV12 */
    enc->src_fourcc = 0x3231564e;  /* 'NV12' */
    enc->metadata_type = -1;

    /* Attempt to use hardware encoder via /dev/avpu (lazy-init on first frame) */
    enc->hw_encoder_fd = -1;
    enc->use_hardware = 1;

    LOG_CODEC("Create: hardware encoder will be attempted via /dev/avpu (lazy init)");

    /* Register in global instances */
    pthread_mutex_lock(&g_codec_mutex);
    for (int i = 0; i < 6; i++) {
        if (g_codec_instances[i] == NULL) {
            g_codec_instances[i] = enc;
            enc->channel_id = i + 1;
            pthread_mutex_unlock(&g_codec_mutex);

            *codec = enc;
            LOG_CODEC("Create: codec=%p, channel=%d", enc, i);
            return 0;
        }
    }
    pthread_mutex_unlock(&g_codec_mutex);

    /* No free slots */
    Fifo_Deinit(enc->fifo_frames);
    Fifo_Deinit(enc->fifo_streams);
    free(enc->fifo_frames);
    free(enc->fifo_streams);
    free(enc);
    LOG_CODEC("Create: no free slots");
    return -1;
}

/**
 * AL_Codec_Encode_Destroy - based on decompilation at 0x7a180
 * Destroys a codec encoder instance
 */
int AL_Codec_Encode_Destroy(void *codec) {
    if (codec == NULL) {
        return -1;
    }

    AL_CodecEncode *enc = (AL_CodecEncode*)codec;

    LOG_CODEC("Destroy: codec=%p, channel=%d", codec, enc->channel_id - 1);

    /* Deinitialize hardware encoder(s) - OEM parity: no separate deinit function */
    if (enc->use_hardware == 2 && enc->avpu.fd >= 0) {
        /* Clean up stream buffers and command-list mappings */
        for (int i = 0; i < enc->avpu.stream_bufs_used; ++i) {
            if (enc->avpu.stream_bufs[i].map) {
                if (!enc->avpu.stream_bufs[i].from_rmem) {
                    munmap(enc->avpu.stream_bufs[i].map, enc->avpu.stream_bufs[i].size);
                }
                enc->avpu.stream_bufs[i].map = NULL;
            }
            if (enc->avpu.stream_bufs[i].dmabuf_fd >= 0) {
                close(enc->avpu.stream_bufs[i].dmabuf_fd);
                enc->avpu.stream_bufs[i].dmabuf_fd = -1;
            }
        }
        if (enc->avpu.cl_ring.map) {
            if (!enc->avpu.cl_ring.from_rmem) {
                munmap(enc->avpu.cl_ring.map, enc->avpu.cl_ring.size);
            }
            enc->avpu.cl_ring.map = NULL;
        }
        if (enc->avpu.cl_ring.dmabuf_fd >= 0) {
            close(enc->avpu.cl_ring.dmabuf_fd);
            enc->avpu.cl_ring.dmabuf_fd = -1;
        }
        if (enc->avpu.interm_buf.map) {
            if (!enc->avpu.interm_buf.from_rmem) {
                munmap(enc->avpu.interm_buf.map, enc->avpu.interm_buf.size);
            }
            enc->avpu.interm_buf.map = NULL;
        }
        if (enc->avpu.interm_buf.dmabuf_fd >= 0) {
            close(enc->avpu.interm_buf.dmabuf_fd);
            enc->avpu.interm_buf.dmabuf_fd = -1;
        }

        /* OEM parity: unblock WaitInterruptThread before close/join */
        enc->avpu.irq_thread_running = 0;
        if (enc->avpu.fd >= 0) {
            avpu_sys_ioctl(enc->avpu.fd, AL_CMD_UNBLOCK_CHANNEL, NULL);
        }
        AL_DevicePool_Close(enc->avpu.fd);
        enc->avpu.fd = -1;

        /* Join IRQ thread if it was started */
        if (enc->avpu.irq_thread) {
            pthread_t tid = (pthread_t)enc->avpu.irq_thread;
            pthread_join(tid, NULL);
            enc->avpu.irq_thread = 0;
            LOG_CODEC("AVPU: IRQ thread joined");
        }
        /* Destroy IRQ mutex if allocated */
        if (enc->avpu.irq_mutex) {
            pthread_mutex_destroy((pthread_mutex_t*)enc->avpu.irq_mutex);
            free(enc->avpu.irq_mutex);
            enc->avpu.irq_mutex = NULL;
        }

        pthread_mutex_lock(&g_avpu_owner_mutex);
        if (g_avpu_owner_channel == enc->channel_id) {
            g_avpu_owner_channel = 0;
            LOG_CODEC("AVPU: released ownership by channel=%d", enc->channel_id - 1);
        }
        pthread_mutex_unlock(&g_avpu_owner_mutex);
    }
    if (enc->hw_encoder_fd >= 0) {
        HW_Encoder_Deinit(enc->hw_encoder_fd);
        enc->hw_encoder_fd = -1;
    }

    /* Unregister from global instances */
    pthread_mutex_lock(&g_codec_mutex);
    for (int i = 0; i < 6; i++) {
        if (g_codec_instances[i] == enc) {
            g_codec_instances[i] = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&g_codec_mutex);

    /* Deinitialize FIFOs */
    Fifo_Deinit(enc->fifo_frames);
    Fifo_Deinit(enc->fifo_streams);

    /* Free FIFO control blocks */
    free(enc->fifo_frames);
    free(enc->fifo_streams);

    /* Close OEM-like event if created */
    if (enc->event) {
        int efd = (int)(uintptr_t)enc->event;
        close(efd);
        enc->event = NULL;
    }

    /* Free codec structure */
    free(enc);

    return 0;
}

/**
 * AL_Codec_Encode_Process - based on decompilation at 0x7a334
 * Process a frame for encoding
 */
int AL_Codec_Encode_Process(void *codec, void *frame, void *user_data) {
    if (codec == NULL) {
        LOG_CODEC("Process: NULL codec pointer");
        return -1;
    }

    AL_CodecEncode *enc = (AL_CodecEncode*)codec;

    (void)user_data;

    if (frame == NULL) {
        /* NULL frame means flush */
        LOG_CODEC("Process: flush requested (NULL frame)");
        return 0;
    }

    /* Validate frame pointer - must be a reasonable address */
    uintptr_t frame_addr = (uintptr_t)frame;
    if (frame_addr < 0x10000) {
        LOG_CODEC("Process: invalid frame pointer %p (too small, likely corrupted)", frame);
        return -1;
    }

    /* Encode frame using hardware or software */
    HWStreamBuffer *hw_stream = (HWStreamBuffer*)malloc(sizeof(HWStreamBuffer));
    if (hw_stream == NULL) {
        LOG_CODEC("Process: failed to allocate stream buffer");
        return -1;
    }

    /* Extract frame data from VBM frame structure */
    /* VBMFrame structure layout (0x428 bytes):
     * 0x00: index
     * 0x04: chn
     * 0x08: width
     * 0x0c: height
     * 0x10: pixfmt
     * 0x14: size
     * 0x18: phys_addr
     * 0x1c: virt_addr
     * 0x20-0x427: data
     */
    uint8_t *frame_bytes = (uint8_t*)frame;
    uint32_t width, height, pixfmt, size, phys_addr, virt_addr;

    LOG_CODEC("Process: frame=%p, extracting metadata", frame);

    memcpy(&width, frame_bytes + 0x08, sizeof(uint32_t));
    memcpy(&height, frame_bytes + 0x0c, sizeof(uint32_t));
    memcpy(&pixfmt, frame_bytes + 0x10, sizeof(uint32_t));
    memcpy(&size, frame_bytes + 0x14, sizeof(uint32_t));
    memcpy(&phys_addr, frame_bytes + 0x18, sizeof(uint32_t));
    memcpy(&virt_addr, frame_bytes + 0x1c, sizeof(uint32_t));

    /* Get current timestamp */
    uint64_t timestamp = IMP_System_GetTimeStamp();

    if (enc->use_hardware) {
        /* Lazy-init hardware encoder on first frame */
        if (enc->hw_encoder_fd < 0) {
            /* Build parameters from codec_param (written by channel_encoder_init) */
            uint32_t bitrate = *(uint32_t*)(enc->codec_param + 0x30);
            uint32_t fps_num = *(uint32_t*)(enc->codec_param + 0x7c);
            uint32_t fps_den = *(uint32_t*)(enc->codec_param + 0x80);
            uint32_t gop = *(uint32_t*)(enc->codec_param + 0x44);
            uint32_t profile_idc = *(uint32_t*)(enc->codec_param + 0x24);
            uint32_t rc_mode = *(uint32_t*)(enc->codec_param + 0x2c);
            LOG_CODEC("Process: lazy-init channel_id=%d %ux%u profile_idc=%u entropy_mode=%u",
                      enc->channel_id, width, height, profile_idc, enc->entropy_mode);
            uint32_t init_qp = (*(uint32_t*)(enc->codec_param + 0x38)) & 0xFFu;
            uint32_t max_qp = *(uint32_t*)(enc->codec_param + 0x3c);
            uint32_t min_qp = *(uint32_t*)(enc->codec_param + 0x40);
            if (!gop) gop = *(uint32_t*)(enc->codec_param + 0xb0);

            memset(&enc->hw_params, 0, sizeof(enc->hw_params));
            enc->hw_params.codec_type = HW_CODEC_H264; /* prudynt-t default */
            enc->hw_params.width = width;
            enc->hw_params.height = height;
            enc->hw_params.fps_num = fps_num ? fps_num : 25;
            enc->hw_params.fps_den = fps_den ? fps_den : 1;
            enc->hw_params.gop_length = gop ? gop : 25;
            switch (rc_mode) {
                case 0: enc->hw_params.rc_mode = HW_RC_MODE_FIXQP; break;
                case 2: enc->hw_params.rc_mode = HW_RC_MODE_VBR; break;
                case 1:
                default:
                    enc->hw_params.rc_mode = HW_RC_MODE_CBR;
                    break;
            }
            enc->hw_params.bitrate = bitrate ? bitrate : 2*1000*1000;
            enc->hw_params.max_qp = clamp_qp_u32(max_qp);
            enc->hw_params.min_qp = clamp_qp_u32(min_qp);
            if (enc->hw_params.min_qp > enc->hw_params.max_qp) {
                uint32_t tmp = enc->hw_params.min_qp;
                enc->hw_params.min_qp = enc->hw_params.max_qp;
                enc->hw_params.max_qp = tmp;
            }
            if (init_qp <= 51u) {
                enc->hw_params.qp = init_qp;
            } else if (enc->hw_params.min_qp <= 51u && enc->hw_params.max_qp <= 51u) {
                enc->hw_params.qp = (enc->hw_params.min_qp + enc->hw_params.max_qp) / 2u;
            } else {
                enc->hw_params.qp = 26u;
            }
            /* Map profile_idc to HW profile */
            switch (profile_idc) {
                case 66: enc->hw_params.profile = HW_PROFILE_BASELINE; break; /* Baseline */
                case 77: enc->hw_params.profile = HW_PROFILE_MAIN; break;     /* Main */
                case 100: enc->hw_params.profile = HW_PROFILE_HIGH; break;    /* High */
                default: enc->hw_params.profile = HW_PROFILE_MAIN; break;
            }

            /* Prefer vendor-like AL over /dev/avpu, but gate to a single owner */
            int skip_avpu = 0, current_owner = 0;
            pthread_mutex_lock(&g_avpu_owner_mutex);
            current_owner = g_avpu_owner_channel;
            if (current_owner != 0 && current_owner != enc->channel_id) {
                skip_avpu = 1;
            }
            pthread_mutex_unlock(&g_avpu_owner_mutex);

            if (!skip_avpu) {
                if (enc->avpu.fd > 2) {
                    /* Already open for this channel; do not re-open */
                    pthread_mutex_lock(&g_avpu_owner_mutex);
                    if (g_avpu_owner_channel == 0) {
                        g_avpu_owner_channel = enc->channel_id;
                    }
                    pthread_mutex_unlock(&g_avpu_owner_mutex);

                    enc->use_hardware = 2; /* 2 = AL/AVPU path */
                    /* OEM parity: no ALAvpu_SetEvent - event_fd stored directly */
                    if (enc->event) {
                        enc->avpu.event_fd = (int)(uintptr_t)enc->event;
                    }
                    LOG_CODEC("AVPU: channel=%d already open (fd=%d); skipping re-open", enc->channel_id - 1, enc->avpu.fd);
                } else {
                    /* Open device via device pool (OEM parity: AL_DevicePool_Open at 0x362dc) */
                    int fd = AL_DevicePool_Open("/dev/avpu");
                    if (fd >= 0) {
                        /* Initialize AVPU context directly (OEM parity: no ALAvpu_Init wrapper) */
                        memset(&enc->avpu, 0, sizeof(enc->avpu));
                        enc->avpu.fd = fd;
                        enc->avpu.event_fd = enc->event ? (int)(uintptr_t)enc->event : -1;
                        enc->avpu.frames_encoded = 0;
                        enc->avpu.frame_number = 0;
                        enc->avpu.stream_header_offset = 0;
                        enc->avpu.busy_skip_count = 0;
                        enc->avpu.busy_snapshot_emitted = 0;
                        enc->avpu.first_submit_logged = 0;
                        enc->avpu.init_trace_completed = 0;
                        enc->avpu.init_stream_flush_failures = 0;
                        enc->avpu.init_interm_flush_ret = 0;
                        enc->avpu.init_cl_flush_ret = 0;
                        enc->avpu.init_misc_write_ret = -999;
                        enc->avpu.init_misc_read_ret = -999;
                        enc->avpu.init_misc_read_val = 0;
                        enc->avpu.init_top_write_ret = -999;
                        enc->avpu.init_top_read_ret = -999;
                        enc->avpu.init_top_read_val = 0;
                        enc->avpu.irq_thread_started = 0;
                        enc->avpu.irq_thread_exited = 0;
                        enc->avpu.irq_wait_errno = 0;
                        enc->avpu.last_irq_id = -1;
                        enc->avpu.reference_valid = 0;

                        /* OEM parity: AL_Board_Create allocates mutex and starts
                         * WaitInterruptThread immediately after opening /dev/avpu. */
                        pthread_mutex_t *mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
                        if (mutex) {
                            pthread_mutex_init(mutex, NULL);
                            enc->avpu.irq_mutex = mutex;
                        }
                        memset(enc->avpu.irq_callbacks, 0, sizeof(enc->avpu.irq_callbacks));

                        if (enc->avpu.irq_mutex) {
                            enc->avpu.irq_thread_running = 1;
                            pthread_t tid;
                            if (pthread_create(&tid, NULL, avpu_irq_thread, &enc->avpu) == 0) {
                                enc->avpu.irq_thread = (long)tid;
                                LOG_CODEC("AVPU: WaitInterruptThread started during open");
                            } else {
                                LOG_CODEC("AVPU: failed to start WaitInterruptThread during open");
                                enc->avpu.irq_thread_running = 0;
                            }
                        }

                        /* Cache encoding parameters for command-list population */
                        enc->avpu.enc_w = width;
                        enc->avpu.enc_h = height;
                        enc->avpu.fps_num = enc->hw_params.fps_num;
                        enc->avpu.fps_den = enc->hw_params.fps_den;
                        enc->avpu.profile = enc->hw_params.profile;
                        enc->avpu.rc_mode = enc->hw_params.rc_mode;
                        enc->avpu.qp = enc->hw_params.qp;
                        enc->avpu.entropy_mode = enc->entropy_mode;
                        enc->avpu.gop_length = enc->hw_params.gop_length;
                        enc->avpu.format_word = *(uint32_t*)(enc->codec_param + 0x10);
                        avpu_init_enc1_slice_words(&enc->avpu, enc->codec_param);

                        /* Allocate stream buffers via IMP_Alloc (OEM parity) */
                        enc->avpu.stream_buf_count = 4;
                        enc->avpu.stream_buf_size = 128 * 1024;
                        enc->avpu.stream_bufs_used = 0;
                        int filled = 0;
                        for (int i = 0; i < enc->avpu.stream_buf_count; ++i) {
                            LOG_CODEC("AVPU: alloc stream buf[%d] size=%d (IMP_Alloc)", i, enc->avpu.stream_buf_size);
                            AvpuDMABuf tmp = (AvpuDMABuf){0};
                            if (avpu_alloc_imp((size_t)enc->avpu.stream_buf_size, "AVPU_STRM", &tmp) == 0) {
                                enc->avpu.stream_bufs[filled] = tmp;
                                enc->avpu.stream_in_hw[filled] = 0;
                                memset(enc->avpu.stream_bufs[filled].map, 0, enc->avpu.stream_buf_size);
                                avpu_flush_cache(fd, enc->avpu.stream_bufs[filled].map,
                                                 (unsigned int)enc->avpu.stream_buf_size, 1);
                                LOG_CODEC("AVPU: stream buf[%d] phys=0x%08x size=%d", filled, enc->avpu.stream_bufs[filled].phy_addr, enc->avpu.stream_buf_size);
                                filled++;
                            } else {
                                LOG_CODEC("AVPU: failed to allocate stream buf[%d] via IMP_Alloc", i);
                            }
                        }
                        enc->avpu.stream_bufs_used = filled;

                        /* Allocate command-list ring via IMP_Alloc (0x13 entries x 512B) */
                        enc->avpu.cl_entry_size = 0x200;
                        enc->avpu.cl_count = 0x13;
                        size_t cl_bytes = enc->avpu.cl_entry_size * enc->avpu.cl_count;
                        int cl_ok = 0;
                        if (avpu_alloc_imp(cl_bytes, "AVPU_CL", &enc->avpu.cl_ring) == 0) {
                            cl_ok = 1;
                            void *virt = enc->avpu.cl_ring.map;
                            uint32_t phys = enc->avpu.cl_ring.phy_addr;
                            if ((phys & 3) != 0 || ((uintptr_t)virt & 3) != 0) {
                                LOG_CODEC("ERROR: cmdlist buffer not 4-byte aligned: phys=0x%08x virt=%p", phys, virt);
                            } else {
                                enc->avpu.cl_idx = 0;
                                memset(virt, 0, cl_bytes);
                                LOG_CODEC("AVPU: cmdlist ring phys=0x%08x size=%zu entries=%u", phys, cl_bytes, enc->avpu.cl_count);
                            }
                        } else {
                            LOG_CODEC("AVPU: failed to allocate cmdlist ring via IMP_Alloc (size=%zu)", cl_bytes);
                        }

                        /* Allocate reconstruction and reference frame DMA buffers.
                         * The AVPU hardware writes reconstructed frames and reads
                         * reference frames via physical addresses in the command list.
                         * Without valid addresses the AVPU DMAs to 0x0 → AXI hang. */
                        {
                            /* OEM ref-manager frames are larger than a plain NV12
                             * surface: reference storage plus auxiliary map/MV tails.
                             * Allocate both rec/ref with a conservative combined layout
                             * so the late Enc1 command words never point past the end of
                             * a plain raster-only buffer. */
                            size_t nv12_sz = avpu_get_nv12_frame_size(width, height);
                            size_t aux_frame_sz = avpu_get_enc1_frame_buf_size(width, height);

                            memset(&enc->avpu.rec_buf, 0, sizeof(AvpuDMABuf));
                            memset(&enc->avpu.ref_buf, 0, sizeof(AvpuDMABuf));
                            memset(&enc->avpu.rec_trace_buf, 0, sizeof(AvpuDMABuf));
                            memset(&enc->avpu.ref_trace_buf, 0, sizeof(AvpuDMABuf));
                            memset(&enc->avpu.interm_buf, 0, sizeof(AvpuDMABuf));

                            enc->avpu.interm_ep1_size = avpu_get_enc1_ep1_size();
                            enc->avpu.interm_wpp_size = avpu_get_enc1_wpp_size(width, height);
                            enc->avpu.interm_ep2_size = avpu_get_enc1_ep2_size(width, height);
                            enc->avpu.interm_map_size = avpu_get_enc1_comp_map_size(width, height);
                            enc->avpu.interm_data_size = (uint32_t)avpu_get_enc1_comp_data_size(width, height,
                                                                                                 enc->avpu.format_word);

                            {
                                size_t interm_total_sz = (size_t)enc->avpu.interm_ep1_size
                                                       + (size_t)enc->avpu.interm_wpp_size
                                                       + (size_t)enc->avpu.interm_ep2_size
                                                       + (size_t)enc->avpu.interm_map_size
                                                       + (size_t)enc->avpu.interm_data_size;

                                if (avpu_alloc_imp(interm_total_sz, "AVPU_ITM", &enc->avpu.interm_buf) == 0) {
                                    memset(enc->avpu.interm_buf.map, 0, interm_total_sz);
                                    enc->avpu.init_interm_flush_ret = avpu_flush_dma_buf(fd, "interm_buf", &enc->avpu.interm_buf, interm_total_sz);
                                    LOG_CODEC("AVPU: interm_buf phys=0x%08x size=%zu (ep1=%u wpp=%u ep2=%u map=%u data=%u)",
                                              enc->avpu.interm_buf.phy_addr, interm_total_sz,
                                              enc->avpu.interm_ep1_size, enc->avpu.interm_wpp_size,
                                              enc->avpu.interm_ep2_size, enc->avpu.interm_map_size,
                                              enc->avpu.interm_data_size);
                                } else {
                                    LOG_CODEC("AVPU: WARNING - failed to allocate interm_buf (%zu bytes)", interm_total_sz);
                                }
                            }

                            if (avpu_alloc_imp(aux_frame_sz, "AVPU_REC", &enc->avpu.rec_buf) == 0) {
                                /* Do NOT memset — rec_buf is AVPU output (reconstruction),
                                 * and zeroing 3MB of uncached DMA memory can stall/hang
                                 * the AXI bus on cold boot. */
                                LOG_CODEC("AVPU: rec_buf phys=0x%08x size=%zu (nv12=%zu ref=%zu map=%zu mv=%zu)",
                                          enc->avpu.rec_buf.phy_addr, aux_frame_sz, nv12_sz,
                                          avpu_get_enc1_ref_region_size(width, height),
                                          avpu_get_enc1_map_region_size(width, height),
                                          avpu_get_enc1_mv_region_size(width, height));
                            } else {
                                LOG_CODEC("AVPU: WARNING - failed to allocate rec_buf (%zu bytes)", aux_frame_sz);
                            }

                            if (avpu_alloc_imp(aux_frame_sz, "AVPU_REF", &enc->avpu.ref_buf) == 0) {
                                /* Do NOT memset — ref_buf content is irrelevant for the
                                 * first IDR frame (intra-only), and subsequent frames will
                                 * have valid reconstruction data copied in by the AVPU. */
                                LOG_CODEC("AVPU: ref_buf phys=0x%08x size=%zu", enc->avpu.ref_buf.phy_addr, aux_frame_sz);
                            } else {
                                LOG_CODEC("AVPU: WARNING - failed to allocate ref_buf (%zu bytes)", aux_frame_sz);
                            }

                            if (avpu_alloc_imp(aux_frame_sz, "AVPU_TRC_REC", &enc->avpu.rec_trace_buf) == 0) {
                                LOG_CODEC("AVPU: rec_trace_buf phys=0x%08x size=%zu", enc->avpu.rec_trace_buf.phy_addr, aux_frame_sz);
                            } else {
                                LOG_CODEC("AVPU: WARNING - failed to allocate rec_trace_buf (%zu bytes)", aux_frame_sz);
                            }

                            if (avpu_alloc_imp(aux_frame_sz, "AVPU_TRC_REF", &enc->avpu.ref_trace_buf) == 0) {
                                LOG_CODEC("AVPU: ref_trace_buf phys=0x%08x size=%zu", enc->avpu.ref_trace_buf.phy_addr, aux_frame_sz);
                            } else {
                                LOG_CODEC("AVPU: WARNING - failed to allocate ref_trace_buf (%zu bytes)", aux_frame_sz);
                            }
                        }

                        /* T31 uses absolute addressing (offset mode causes kernel crashes) */
                        enc->avpu.axi_base = 0;
                        enc->avpu.use_offsets = 0;
                        enc->avpu.session_ready = 0;
                        enc->avpu.hw_prepared = 0;

                        pthread_mutex_lock(&g_avpu_owner_mutex);
                        if (g_avpu_owner_channel == 0) {
                            g_avpu_owner_channel = enc->channel_id;
                            LOG_CODEC("AVPU: channel=%d acquired ownership", enc->channel_id - 1);
                        }
                        pthread_mutex_unlock(&g_avpu_owner_mutex);

                        /* FIFOs already initialized at AL_CodecEncode create time
                         * (enc->fifo_frames, enc->fifo_streams at lines 767-778).
                         * OEM uses FIFOs at encoder+0x7f8 (streams) and encoder+0x81c (metadata). */

                        /* Register OEM callbacks (AL_EncCore_Init at 0x6c8d8).
                         * OEM uses CORE index (not channel) for IRQ slots:
                         *   EndEncoding:    core*4     = 0 for core 0
                         *   EndAvcEntropy:  core*4 + 2 = 2 for core 0
                         * T31 has a single AVPU core (core 0). */
                        int core = 0; /* T31: single AVPU core */
                        /* Stock fires IRQ 4 (not 0) for encode completion.
                         * Stock IRQ mask = 0x11 (bits 0+4). Register callbacks
                         * at BOTH slots 0 and 4 to handle whichever fires. */
                        int irq_id0 = 0;                  /* IRQ 0 */
                        int irq_id4 = 4;                  /* IRQ 4: stock EndEncoding */
                        int irq_id2 = 2;                  /* IRQ 2: EndAvcEntropy */

                        avpu_register_callback(&enc->avpu, avpu_end_encoding_callback, &enc->avpu, irq_id0);
                        avpu_register_callback(&enc->avpu, avpu_end_encoding_callback, &enc->avpu, irq_id4);
                        LOG_CODEC("AVPU: registered EndEncoding callback at IRQ %d and %d", irq_id0, irq_id4);

                        if (irq_id2 < 20) {
                            avpu_register_callback(&enc->avpu, avpu_end_avc_entropy_callback, &enc->avpu, irq_id2);
                            LOG_CODEC("AVPU: registered EndAvcEntropy callback at IRQ %d", irq_id2);
                        }

                        enc->use_hardware = 2; /* 2 = AL/AVPU path */
                        LOG_CODEC("Process: AVPU opened fd=%d channel=%d", fd, enc->channel_id - 1);
                    } else {
                        int e = errno;
                        LOG_CODEC("Process: channel=%d AL_DevicePool_Open failed: %s", enc->channel_id - 1, strerror(e));
                        int init_fd = -1;
                        if (HW_Encoder_Init(&init_fd, &enc->hw_params) == 0 && init_fd >= 0) {
                            enc->hw_encoder_fd = init_fd;
                            enc->use_hardware = 1; /* legacy path */
                            LOG_CODEC("Process: legacy HW encoder initialized (fd=%d)", init_fd);
                        } else {
                            LOG_CODEC("Process: no hardware path available; falling back to software");
                            enc->use_hardware = 0;
                        }
                    }
                }
            } else {
                LOG_CODEC("Process: channel=%d skipping AVPU open; already owned by channel=%d", enc->channel_id - 1, current_owner - 1);
                /* Fallback: try legacy non-avpu devices via HW_Encoder_Init */
                int init_fd = -1;
                if (HW_Encoder_Init(&init_fd, &enc->hw_params) == 0 && init_fd >= 0) {
                    enc->hw_encoder_fd = init_fd;
                    enc->use_hardware = 1; /* legacy path */
                    LOG_CODEC("Process: legacy HW encoder initialized (fd=%d)", init_fd);
                } else {
                    LOG_CODEC("Process: no hardware path available; falling back to software");
                    enc->use_hardware = 0;
                }
            }
        }
    }

    if (enc->use_hardware == 1 && enc->hw_encoder_fd >= 0) {
        /* Legacy hardware path (/dev/venc, etc.) */
        HWFrameBuffer hw_frame;
        memset(&hw_frame, 0, sizeof(HWFrameBuffer));
        hw_frame.phys_addr = phys_addr;
        hw_frame.virt_addr = virt_addr;
        hw_frame.size = size;
        hw_frame.width = width;
        hw_frame.height = height;
        hw_frame.pixfmt = pixfmt;
        hw_frame.timestamp = timestamp;
        LOG_CODEC("Process: HW(lgcy) encode frame %ux%u, phys=0x%x, virt=0x%x, size=%u",
                  width, height, phys_addr, virt_addr, size);
        if (HW_Encoder_Encode(enc->hw_encoder_fd, &hw_frame) < 0) {
            LOG_CODEC("Process: legacy hardware encoding failed");
            free(hw_stream);
            return -1;
        }
        if (HW_Encoder_GetStream(enc->hw_encoder_fd, hw_stream, 100) < 0) {
            LOG_CODEC("Process: legacy HW get stream timed out");
            free(hw_stream);
            return 0; /* no stream yet */
        }
    } else if (enc->use_hardware == 2 && enc->avpu.fd >= 0) {
        /* OEM parity: Direct ioctl calls (AL_Common_Encoder_Process) - no ALAvpu_QueueFrame wrapper */
        ALAvpuContext *ctx = &enc->avpu;
        int fd = ctx->fd;
        int submitted = 0;

        /* AL_EncCore_Init: exact OEM sequence from decompilation at 0x6c8d8.
         *
         * OEM order (confirmed from BinaryNinja):
         *   1. Register EndEncoding callback (IRQ slot for core*4)
         *   2. Register EndAvcEntropy callback (IRQ slot for core*4+2)
         *   3. ResetCore: write 1, 2, 4 to (core<<9)+0x83F0 — NO delays
         *   4. Clear interrupts: write 0xFFFFFF to 0x8018
         *   5. Set TOP_CTRL: write 0x80 to 0x8054
         *   6. Set state = 1
         *
         * CRITICAL: The reset writes (1,2,4) MUST be back-to-back with NO
         * usleep between them.  Leaving the core in intermediate reset state
         * while the IRQ handler or other threads access AVPU registers hangs
         * the AXI bus on T31.
         */
        if (!ctx->session_ready) {
            LOG_CODEC("AVPU: AL_EncCore_Init (OEM-exact sequence)");

            /* Stock register write sequence (captured via patched avpu.ko):
             *
             * Phase 1: Init (AL_EncCore_Init)
             *   WR 0x8010 = 0x00001000   MISC_CTRL
             *   WR 0x83f0 = 1,2,4        ResetCore
             *   WR 0x8018 = 0x00ffffff   Clear IRQ
             *   WR 0x8054 = 0x00000080   TOP_CTRL
             *
             * Phase 2: Pre-encode setup
             *   WR 0x83f4 = 0x00000001   Clock gate ON
             *   WR 0x83f0 = 1,2,4        ResetCore AGAIN
             *   WR 0x8014 = 0x00000011   IRQ mask (bits 0+4)
             *   WR 0x83e0/83e4           CL_ADDR + CL_PUSH
             *
             * Phase 3: Post-CL encoder config (CRITICAL - we were missing this!)
             *   WR 0x85f4 = 0x00000001   ENC_EN_B
             *   WR 0x85f0 = 0x00000001   ENC_EN_A
             *   WR 0x8400-0x8428         Encoder config block
             *   WR 0x85e4 = 0x00000001   ENC_EN_C
             */

            /* Phase 1: Init */
            avpu_write_reg(fd, AVPU_REG_MISC_CTRL, 0x00001000);
            avpu_write_reg(fd, AVPU_REG_CORE_RESET(0), 0x00000001);
            avpu_write_reg(fd, AVPU_REG_CORE_RESET(0), 0x00000002);
            avpu_write_reg(fd, AVPU_REG_CORE_RESET(0), 0x00000004);
            avpu_write_reg(fd, AVPU_INTERRUPT, 0x00FFFFFF);
            avpu_write_reg(fd, AVPU_REG_TOP_CTRL, 0x00000080);

            /* Phase 2: Clock + second reset (stock does this before CL_PUSH) */
            avpu_turn_on_gc(fd, 0);
            avpu_write_reg(fd, AVPU_REG_CORE_RESET(0), 0x00000001);
            avpu_write_reg(fd, AVPU_REG_CORE_RESET(0), 0x00000002);
            avpu_write_reg(fd, AVPU_REG_CORE_RESET(0), 0x00000004);

            LOG_CODEC("AVPU: init complete (stock-matched sequence)");
            ctx->session_ready = 1;

            /* Push stream buffers via STRM_PUSH so the hardware DMA engine
             * knows they're available. The CL (cmd[0x30]) specifies where to
             * write, but STRM_PUSH registers the buffer with the DMA controller. */
            if (ctx->stream_bufs_used > 0) {
                for (int i = 0; i < ctx->stream_bufs_used; ++i) {
                    if (ctx->stream_bufs[i].phy_addr) {
                        avpu_write_reg(fd, AVPU_REG_STRM_PUSH, ctx->stream_bufs[i].phy_addr);
                        LOG_CODEC("AVPU: STRM_PUSH buf[%d] phys=0x%08x", i, ctx->stream_bufs[i].phy_addr);
                    }
                }
            }

            LOG_CODEC("AVPU: HW initialized (AL_EncCore_Init)");

        }

        /* Prepare command-list entry (OEM parity: SetCommandListBuffer) */
        if (ctx->cl_ring.phy_addr && ctx->cl_ring.map && ctx->cl_entry_size) {
            uint32_t idx = ctx->cl_idx % ctx->cl_count;
            uint8_t* entry = (uint8_t*)ctx->cl_ring.map + (size_t)idx * ctx->cl_entry_size;

            /* Verify entry alignment */
            if (((uintptr_t)entry & 3) != 0) {
                LOG_CODEC("ERROR: CL entry not 4-byte aligned: %p", (void*)entry);
                free(hw_stream);


                return -1;
            }

            uint32_t* cmd = (uint32_t*)entry;

            /* OEM parity: determine IDR status and pre-write headers into stream buffer.
             * The OEM encode1() calls GenerateAvcSliceHeader() before building the CL,
             * writing SPS+PPS+slice header into the stream buffer. The returned byte
             * count becomes cmd[0x32]/cmd[0x36] so the AVPU writes encoded data after. */
            int is_idr = !(ctx->reference_valid && ctx->ref_buf.phy_addr);

            /* Defensive: Baseline profile (66) MUST use CAVLC. If entropy_mode
             * got corrupted to CABAC, force it back. The AVPU may hang on
             * contradictory Baseline+CABAC configuration. */
            if (ctx->profile == 0 || ctx->profile == 66) {
                if (ctx->entropy_mode != 0) {
                    LOG_CODEC("AVPU: WARN forcing entropy_mode %u->0 (CAVLC) for Baseline profile=%u",
                              ctx->entropy_mode, ctx->profile);
                    ctx->entropy_mode = 0;
                }
            }

            uint32_t hdr_offset = avpu_prewrite_stream_headers(ctx, 0, is_idr);

            /* DMA-flush the stream buffer region containing pre-written headers.
             * dir=1 = DMA_TO_DEVICE (CPU writeback to RAM).
             * The AVPU reads headers from RAM (or at least the stream buffer must be
             * coherent with what the AVPU DMAs to/from). */
            if (hdr_offset > 0 && ctx->stream_bufs[0].map) {
                avpu_flush_cache(fd, ctx->stream_bufs[0].map, hdr_offset, 1 /*WBACK*/);
            }

            /* Fill Enc1 command registers — source addr and header offset go INTO the CL entry */
            fill_cmd_regs_enc1(ctx, cmd, phys_addr, hdr_offset);
            log_first_enc1_cmd_window(ctx, idx, cmd);

            /* OEM AL_EncCore_Encode1() checks IsEnc1AlreadyRunning() before
             * flushing/pushing a new Enc1 command list. Our simplified path
             * uses a single effective rec/ref pair, so matching this gate is
             * important before submitting the next frame. */
            /* Bypass busy check if a previous encode completed — the hardware
             * latches core_status=0x3 after completion. Without draining via
             * GetStream + ReleaseStream, we can't reset yet. Allow submission
             * to continue so the pipeline keeps producing frames. */
            unsigned int core_status = 0;
            if (ctx->frames_encoded > 0) {
                ctx->busy_skip_count = 0;
                /* fall through to submit */
            } else if (avpu_is_enc1_running(fd, 0, &core_status)) {
                unsigned int skip_count = __sync_add_and_fetch(&ctx->busy_skip_count, 1u);
                if (skip_count == 1u || (skip_count % 30u) == 0u) {
                    LOG_CODEC("Process: Enc1 already running; skipping CL[%u] submit to match OEM gating (skip_count=%u core_status=0x%08x)",
                              idx, skip_count, core_status);
                    if (ctx->cl_count != 0) {
                        uint32_t active_idx = (idx + ctx->cl_count - 1u) % ctx->cl_count;
                        log_busy_enc1_cmd_window(ctx, active_idx, skip_count);
                    }
                    avpu_log_busy_snapshot(ctx, idx, core_status);
                }
                free(hw_stream);
                return -1;
            }
            ctx->busy_skip_count = 0;

            /* Flush the mapped command-list region from CPU cache to RAM.
             * OEM AL_EncCore_Encode1 flushes a much larger 0x100000 window
             * before StartEnc1WithCommandList. Our CL ring allocation is only
             * 0x2600 bytes, so the closest safe equivalent is to flush the full
             * mapped ring rather than just the current 0x200-byte entry.
             * dir=1 = DMA_TO_DEVICE (writeback, CPU→RAM).
             *
             * OEM parity: AL_EncCore_Encode1 calls Rtos_FlushCacheMemory(cl_base, 0x100000)
             * which flushes 1MB — on MIPS T31 with ~16-32KB L1 D-cache this effectively
             * flushes the ENTIRE cache. This ensures all DMA buffers (CL, stream headers,
             * intermediate, rec/ref) are coherent. Match that by flushing 1MB. */
            /* Flush ONLY the current CL entry (512 bytes) — the 1MB flush was
             * confirmed broken via avpu.ko CL dump (physical RAM shows stale
             * AVPU status data, not our command words). Small flushes work
             * (stream buffer header 47 bytes flushes correctly). */
            size_t cl_flush_size = ctx->cl_entry_size; /* 512 bytes */
            /* Verify data in CPU cache, then flush */
            LOG_CODEC("Process: CL[%u] pre-flush virt_w0=0x%08x w1=0x%08x entry=%p size=%u",
                      idx, cmd[0], cmd[1], (void*)entry, (unsigned)cl_flush_size);
            int cl_flush_ret = avpu_flush_cache(fd, entry, (unsigned int)cl_flush_size, 1 /*WBACK*/);
            LOG_CODEC("Process: CL[%u] flush ret=%d (rmem+avpu)", idx, cl_flush_ret);
            int trace_submit = (idx == 0 && ctx->frames_encoded == 0);

            /* OEM encode1 enables interrupts before AL_EncCore_Encode1 starts
             * the command list. Match that ordering here instead of unmasking
             * after the source FIFO push. */
            avpu_enable_interrupts(fd, 0);

            /* OEM-aligned submit path: program the command-list address and
             * trigger Enc1 via CL_PUSH. Stream buffers are pre-queued via
             * STRM_PUSH separately; the OEM StartEnc1WithCommandList helper does
             * not issue an extra SRC_PUSH on the Enc1 command-list path. */
            uint32_t cl_phys = ctx->cl_ring.phy_addr + (idx * ctx->cl_entry_size);
            uint32_t ref_phys = ctx->reference_valid ? ctx->ref_buf.phy_addr : 0;
            int cl_addr_ret;
            int cl_push_ret;
            if (trace_submit || cl_flush_ret != 0) {
                LOG_CODEC("AVPU: submit flush CL[%u] ring_phys=0x%08x size=0x%08x ret=%d",
                          idx, ctx->cl_ring.phy_addr, (unsigned int)cl_flush_size, cl_flush_ret);
            }
            if (trace_submit) {
                ctx->init_cl_flush_ret = cl_flush_ret;
                ctx->init_trace_completed = 1;
            }
            LOG_CODEC("Process: CL_ADDR=0x%08x src=0x%08x rec=0x%08x ref=0x%08x CL[%u]",
                      cl_phys, phys_addr,
                      ctx->rec_buf.phy_addr, ref_phys, idx);
            if (trace_submit)
                avpu_log_submit_snapshot(ctx, idx, "pre");

            cl_addr_ret = avpu_write_reg(fd, AVPU_REG_CL_ADDR, cl_phys);
            if (trace_submit) {
                LOG_CODEC("AVPU: submit write CL[%u] CL_ADDR ret=%d", idx, cl_addr_ret);
                avpu_log_submit_snapshot(ctx, idx, "post_cl_addr");
            }

            cl_push_ret = avpu_write_reg(fd, AVPU_REG_CL_PUSH, 0x00000002);
            if (trace_submit) {
                LOG_CODEC("AVPU: submit write CL[%u] CL_PUSH ret=%d val=0x00000002", idx, cl_push_ret);
                avpu_log_submit_snapshot(ctx, idx, "post_cl_push");
            }

            /* CRITICAL: Stock writes encoder config registers AFTER CL_PUSH.
             * Without these, the AVPU hangs at core_status=0x3.
             * Captured from stock libimp register trace via patched avpu.ko. */
            {
                uint32_t y_plane_sz = avpu_get_nv12_luma_plane_size(width, height);
                uint32_t stream_part_offset = avpu_get_enc1_stream_part_offset(ctx);
                uint32_t interm_map_addr = 0, interm_data_addr = 0;
                if (ctx->interm_buf.phy_addr) {
                    uint32_t ep1 = ctx->interm_buf.phy_addr;
                    uint32_t wpp = ep1 + ctx->interm_ep1_size;
                    uint32_t ep2 = wpp + ctx->interm_wpp_size;
                    interm_map_addr = ep2 + ctx->interm_ep2_size;
                    interm_data_addr = interm_map_addr + ctx->interm_map_size;
                }

                /* ENC_EN_B */
                avpu_write_reg(fd, AVPU_REG_ENC_EN_B, 0x00000001);
                /* ENC_EN_A */
                avpu_write_reg(fd, AVPU_REG_ENC_EN_A, 0x00000001);

                /* Encoder config register block 0x8400-0x8428 */
                avpu_write_reg(fd, 0x8400, 0x00000131u);
                avpu_write_reg(fd, 0x8404,
                    (((uint32_t)width - 1u) << 16) | ((uint32_t)height - 1u));
                avpu_write_reg(fd, 0x8408, 0x00010001u);
                avpu_write_reg(fd, 0x840c, (uint32_t)width);
                avpu_write_reg(fd, 0x8410, phys_addr);        /* source Y */
                avpu_write_reg(fd, 0x8414, phys_addr + y_plane_sz); /* source UV */
                /* Stock 0x8418/0x841c values are buffer addresses that don't
                 * obviously match intermediate regions. Try EP1/WPP addresses
                 * which are the working buffers the entropy coder reads from. */
                avpu_write_reg(fd, 0x8418, ctx->interm_buf.phy_addr
                               + ctx->interm_ep1_size); /* WPP buffer */
                avpu_write_reg(fd, 0x841c, ctx->interm_buf.phy_addr); /* EP1 buffer */
                avpu_write_reg(fd, 0x8420, stream_part_offset);
                avpu_write_reg(fd, 0x8424, hdr_offset);
                avpu_write_reg(fd, 0x8428,
                    stream_part_offset > hdr_offset
                        ? (stream_part_offset - hdr_offset) & ~0x1fu : 0u);

                /* ENC_EN_C */
                avpu_write_reg(fd, AVPU_REG_ENC_EN_C, 0x00000001);

                LOG_CODEC("AVPU: post-CL encoder config written (ENC_EN_A/B/C + 0x8400-0x8428)");
            }

            /* OEM parity: For AVC, the Enc2 slice parameters are embedded in the
             * Enc1 CL (at cmd[0x1b]-cmd[0x1f], filled above in fill_cmd_regs_enc1).
             * The OEM only submits a separate CL_PUSH=8 for P/B frames that have
             * reference buffers ($s3_3 != 0 in encode1). For the first IDR frame
             * (no references), only CL_PUSH=2 is submitted.
             *
             * When references exist, encode1 fills a separate Enc2 CL via
             * FillCmdRegsEnc2 + SliceParamToCmdRegsEnc2 and submits CL_PUSH=8.
             */
            if (ctx->reference_valid && ctx->ref_buf.phy_addr) {
                /* P/B frame: submit separate Enc2 CL */
                uint32_t enc2_idx = (idx + 1) % ctx->cl_count;
                uint8_t *enc2_entry = (uint8_t *)ctx->cl_ring.map + (size_t)enc2_idx * ctx->cl_entry_size;
                uint32_t *enc2_cmd = (uint32_t *)enc2_entry;
                uint32_t enc2_phys = ctx->cl_ring.phy_addr + (enc2_idx * ctx->cl_entry_size);

                fill_cmd_regs_enc2(ctx, enc2_cmd, hdr_offset);

                /* Flush CL ring again (includes the new Enc2 entry) */
                avpu_flush_cache(fd, ctx->cl_ring.map, (unsigned int)cl_flush_size, 1 /*WBACK*/);

                int enc2_addr_ret = avpu_write_reg(fd, AVPU_REG_CL_ADDR, enc2_phys);
                int enc2_push_ret = avpu_write_reg(fd, AVPU_REG_CL_PUSH, 0x00000008);

                if (trace_submit) {
                    LOG_CODEC("AVPU: Enc2 CL[%u] phys=0x%08x CL_ADDR ret=%d CL_PUSH=8 ret=%d",
                              enc2_idx, enc2_phys, enc2_addr_ret, enc2_push_ret);
                    avpu_log_submit_snapshot(ctx, enc2_idx, "post_enc2_push");
                }
                LOG_CODEC("Process: Enc2 submitted CL[%u] phys=0x%08x hdr=%u", enc2_idx, enc2_phys, hdr_offset);
                ctx->cl_idx = (idx + 2) % ctx->cl_count;
            } else {
                /* IDR/I frame (no references): Enc2 params are in Enc1 CL,
                 * only CL_PUSH=2 was submitted. No separate Enc2 needed. */
                LOG_CODEC("Process: IDR frame — Enc2 params embedded in Enc1 CL, no CL_PUSH=8");
                ctx->cl_idx = (idx + 1) % ctx->cl_count;
            }

            /* Advance CL index (already set above based on IDR vs P frame) */
            ctx->frame_number++;
            codec_queue_frame_metadata(enc, user_data);
            submitted = 1;

            LOG_CODEC("Process: AVPU queued frame %ux%u phys=0x%x CL[%u] hdr=%u - encoding triggered",
                      width, height, phys_addr, idx, hdr_offset);
        }

        /* Do not dequeue here; GetStream() will handle stream retrieval */
        free(hw_stream);
        return submitted ? 0 : -1;
    } else {
        /* Software fallback */
        HWFrameBuffer hw_frame;
        memset(&hw_frame, 0, sizeof(HWFrameBuffer));
        hw_frame.phys_addr = phys_addr;
        hw_frame.virt_addr = virt_addr;
        hw_frame.width = width;
        hw_frame.height = height;
        hw_frame.timestamp = timestamp;
        LOG_CODEC("Process: SW encode frame %ux%u", width, height);
        if (HW_Encoder_Encode_Software(&hw_frame, hw_stream) < 0) {
            LOG_CODEC("Process: software encoding failed");
            free(hw_stream);
            return -1;
        }
    }

    /* Queue encoded stream to FIFO */
    if (Fifo_Queue(enc->fifo_streams, hw_stream, -1) == 0) {
        LOG_CODEC("Process: failed to queue stream");
        free(hw_stream);
        return -1;
    }
    codec_queue_frame_metadata(enc, user_data);

    LOG_CODEC("Process: encoded and queued stream, length=%u", hw_stream->length);
    return 0;
}

/**
 * AL_Codec_Encode_GetStream - based on decompilation at 0x7a548
 * Get an encoded stream
 */
int AL_Codec_Encode_GetStream(void *codec, void **stream, void **user_data) {
    if (codec == NULL || stream == NULL || user_data == NULL) {
        return -1;
    }

    AL_CodecEncode *enc = (AL_CodecEncode*)codec;

    *user_data = NULL;

    LOG_CODEC("GetStream: use_hw=%d avpu.fd=%d", enc->use_hardware, enc->avpu.fd);

    if (enc->use_hardware == 2 && enc->avpu.fd >= 0) {
        ALAvpuContext *ctx = &enc->avpu;

        LOG_CODEC("GetStream[AVPU]: enc=%d cons=%d session=%d",
                  ctx->frames_encoded, ctx->frames_consumed, ctx->session_ready);

        if (!ctx->session_ready) {
            errno = EAGAIN;
            return -1;
        }

        /* Wait for at least one frame to be encoded, then return data.
         * Track frames_consumed separately so we drain all produced frames. */
        struct timespec nap = {0, 10000000L}; /* 10ms */

        for (int retry = 0; retry < 200; ++retry) {
            if (ctx->frames_encoded > ctx->frames_consumed) {
                /* OEM reads encoded size from CL status at offset +0xC8.
                 * After the AVPU writes encoded data, CL[0xC8/4] = byte offset
                 * where the AVPU stopped writing in the stream buffer.
                 * We also check CL[0x104/4] & 0x3FFFFFFF for entropy-reported bits. */

                /* CL cache invalidate doesn't work (MIPS cache coherency issue
                 * with rmem mappings). But the stream buffer invalidate DOES work.
                 * Read frame size directly from stream buffer by scanning for
                 * valid Annex-B data. */
                int buf_idx = 0;
                uint32_t frame_size = 0;

                if (buf_idx < ctx->stream_bufs_used && ctx->stream_bufs[buf_idx].map) {
                    avpu_flush_cache(ctx->fd, ctx->stream_bufs[buf_idx].map,
                                     (unsigned int)ctx->stream_buf_size, 2 /*INV*/);
                    /* Scan backwards from stream_part_offset to find last non-zero byte.
                     * The AVPU writes encoded data after the header; the remainder
                     * of the buffer is zero-filled from our init memset. */
                    const uint8_t *sb = (const uint8_t*)ctx->stream_bufs[buf_idx].map;
                    uint32_t spo = avpu_get_enc1_stream_part_offset(ctx);
                    uint32_t end = spo > 0 ? spo : (uint32_t)ctx->stream_buf_size;
                    while (end > 0 && sb[end - 1] == 0) end--;
                    frame_size = end;
                }

                /* Debug: check stream buf AND intermediate buf for encoded data */
                if (frame_size <= 48 && ctx->frames_consumed < 3) {
                    /* Stream buffer */
                    if (ctx->stream_bufs[0].map) {
                        avpu_flush_cache(ctx->fd, ctx->stream_bufs[0].map,
                                         (unsigned int)ctx->stream_buf_size, 2);
                        const uint32_t *sw = (const uint32_t*)ctx->stream_bufs[0].map;
                        LOG_CODEC("GetStream[AVPU]: STRM w0=%08x w12=%08x w64=%08x", sw[0], sw[12], sw[64]);
                    }
                    /* Intermediate data buffer (where cmd[0x30] points) */
                    if (ctx->interm_buf.map) {
                        uint32_t data_off = ctx->interm_ep1_size + ctx->interm_wpp_size
                                           + ctx->interm_ep2_size + ctx->interm_map_size;
                        avpu_flush_cache(ctx->fd, ctx->interm_buf.map,
                                         (unsigned int)(data_off + 4096), 2);
                        const uint32_t *iw = (const uint32_t*)((uint8_t*)ctx->interm_buf.map + data_off);
                        LOG_CODEC("GetStream[AVPU]: INTERM @%u w0=%08x w1=%08x w2=%08x w3=%08x",
                                  data_off, iw[0], iw[1], iw[2], iw[3]);
                    }
                }
                LOG_CODEC("GetStream[AVPU]: stream_buf[%d] frame_size=%u",
                          buf_idx, frame_size);

                if (frame_size > 0) {
                    const uint8_t *virt = (const uint8_t*)ctx->stream_bufs[buf_idx].map;
                    HWStreamBuffer *s = (HWStreamBuffer*)malloc(sizeof(HWStreamBuffer));
                    if (!s) return -1;
                    s->phys_addr = ctx->stream_bufs[buf_idx].phy_addr;
                    s->virt_addr = (uint32_t)(uintptr_t)virt;
                    s->length = frame_size;
                    s->timestamp = 0;
                    s->frame_type = 0;
                    s->slice_type = 0;
                    *stream = s;
                    *user_data = codec_dequeue_frame_metadata(enc);
                    ctx->frames_consumed++;
                    LOG_CODEC("GetStream[AVPU]: got stream buf[%d] phys=0x%08x len=%u enc=%d cons=%d",
                             buf_idx, s->phys_addr, s->length, ctx->frames_encoded, ctx->frames_consumed);
                    return 0;
                }

                LOG_CODEC("GetStream[AVPU]: encode completed but frame_size=0 or error");
            }

            nanosleep(&nap, NULL);
        }

        LOG_CODEC("GetStream[AVPU]: TIMEOUT (frames_encoded=%d)", ctx->frames_encoded);
        errno = EAGAIN;
        return -1;
    }

    /* Legacy/SW path: dequeue from our FIFO.
     * Use 100ms timeout instead of blocking forever — the stream_thread
     * needs to retry so it can see use_hardware transition from 1→2
     * when the AVPU path activates on the first frame. */
    void *s = Fifo_Dequeue(enc->fifo_streams, 100);
    if (s == NULL) {
        return -1;
    }

    *stream = s;
    *user_data = codec_dequeue_frame_metadata(enc);
    LOG_CODEC("GetStream: got stream %p", s);
    return 0;
}

/**
 * AL_Codec_Encode_ReleaseStream - based on decompilation at 0x7a624
 * Release an encoded stream
 */
int AL_Codec_Encode_ReleaseStream(void *codec, void *stream, void *user_data) {
    if (codec == NULL || stream == NULL) {
        return -1;
    }

    AL_CodecEncode *enc = (AL_CodecEncode*)codec;

    if (enc->use_hardware == 2 && enc->avpu.fd >= 0) {
        /* OEM parity: Direct ioctl to return buffer (no ALAvpu_ReleaseStream wrapper) */
        HWStreamBuffer *hw_stream = (HWStreamBuffer*)stream;
        ALAvpuContext *ctx = &enc->avpu;
        (void)user_data;

        if (ctx->session_ready) {
            /* Return buffer to hardware via direct ioctl */
            for (int i = 0; i < ctx->stream_bufs_used; ++i) {
                if (ctx->stream_bufs[i].phy_addr == hw_stream->phys_addr) {
                    avpu_write_reg(ctx->fd, AVPU_REG_STRM_PUSH, hw_stream->phys_addr);
                    ctx->stream_in_hw[i] = 1;
                    LOG_CODEC("ReleaseStream[AVPU]: requeued stream buf[%d] phys=0x%x", i, hw_stream->phys_addr);
                    break;
                }
            }
        }

        free(hw_stream);
        return 0;
    }

    /* Legacy/SW path follows libimp semantics (no refcounts) */
    if (user_data != NULL) {
        (void)user_data;
    }

    HWStreamBuffer *hw_stream = (HWStreamBuffer*)stream;
    LOG_CODEC("ReleaseStream: freed stream %p", stream);
    if (hw_stream->virt_addr != 0 && hw_stream->phys_addr == 0) {
        /* Software-encoded stream - free the allocated buffer */
        void *data_ptr = (void*)(uintptr_t)hw_stream->virt_addr;
        free(data_ptr);
        LOG_CODEC("ReleaseStream: freed software-encoded data at %p", data_ptr);
    }
    free(hw_stream);
    return 0;
}

/**
 * AL_Codec_Encode_SetQp - Set QP (Quantization Parameter)
 * Based on decompilation pattern
 */
int AL_Codec_Encode_SetQp(void *codec, void *qp) {
    if (codec == NULL || qp == NULL) {
        LOG_CODEC("SetQp: NULL parameter");
        return -1;
    }

    AL_CodecEncode *enc = (AL_CodecEncode*)codec;

    IMPEncoderQp *imp_qp = (IMPEncoderQp*)qp;
    uint32_t new_qp = imp_qp->qp_p ? imp_qp->qp_p : imp_qp->qp_i;
    new_qp = clamp_qp_u32(new_qp);

    enc->hw_params.qp = new_qp;
    enc->avpu.qp = new_qp;

    LOG_CODEC("SetQp: codec=%p, qp_i=%u qp_p=%u -> active_qp=%u",
              codec, imp_qp->qp_i, imp_qp->qp_p, new_qp);

    return 0;
}

int AL_Codec_Encode_SetEntropyMode(void *codec, int mode) {
    if (codec == NULL) {
        LOG_CODEC("SetEntropyMode: NULL codec");
        return -1;
    }

    AL_CodecEncode *enc = (AL_CodecEncode*)codec;
    /* OEM public API uses 1=CAVLC, 2=CABAC; internal command packing uses 0/1. */
    enc->entropy_mode = (mode == 2) ? 1u : 0u;
    enc->avpu.entropy_mode = enc->entropy_mode;

    LOG_CODEC("SetEntropyMode: codec=%p, mode=%u", codec, enc->entropy_mode);

    return 0;
}

