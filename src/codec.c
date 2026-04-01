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
static inline unsigned AVPU_CORE_BASE(int core) { return (AVPU_BASE_OFFSET + 0x3F0) + ((unsigned)core << 9); }
#define AVPU_REG_CORE_RESET(c)   (AVPU_CORE_BASE(c) + 0x00)
#define AVPU_REG_CORE_CLKCMD(c)  (AVPU_CORE_BASE(c) + 0x04)
#define AVPU_REG_CORE_STATUS(c)  (AVPU_CORE_BASE(c) + 0x08)

/* Cache flush via /dev/avpu JZ_CMD_FLUSH_CACHE ioctl.
 * rmem mappings are CACHED — the AVPU reads from physical RAM, not CPU cache.
 * Without flushing, the AVPU reads stale/zeroed data → hang or corrupt output.
 * OEM calls Rtos_FlushCacheMemory → AL_DmaAlloc_FlushCache → IMP_FlushCache
 * which does ioctl(rmem_fd, 0xc00c7200, {phys, size, dir=1}).
 * We use the AVPU's JZ_CMD_FLUSH_CACHE which takes {virt_addr, size, dir}. */
#define JZ_CMD_FLUSH_CACHE_IOCTL _IOWR('q', 14, int)
struct flush_cache_info {
    unsigned int addr;
    unsigned int len;
    unsigned int dir;   /* 1=WBACK(DMA_TO_DEVICE), 2=INV(DMA_FROM_DEVICE), 0=WBACK_INV */
};
static int avpu_flush_cache(int fd, void *virt_addr, unsigned int size, unsigned int dir)
{
    if (fd < 0 || !virt_addr || size == 0) return -1;
    struct flush_cache_info info;
    info.addr = (unsigned int)(uintptr_t)virt_addr;
    info.len = size;
    info.dir = dir;
    return avpu_sys_ioctl(fd, JZ_CMD_FLUSH_CACHE_IOCTL, &info);
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

static int avpu_read_reg(int fd, unsigned int off, unsigned int *out)
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

    LOG_CODEC("AVPU read_reg: off=0x%08x argp=%p", off, (void*)p);
    int ret = avpu_sys_ioctl(fd, AL_CMD_IP_READ_REG, p);
    if (ret == 0 && out) *out = p->value;
    free(raw);
    return ret;
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

static uint32_t avpu_default_enc1_cmd12_a8(uint32_t enc_w)
{
    return avpu_align_up_u32(enc_w, 64u);
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

static void avpu_init_enc1_slice_words(ALAvpuContext *ctx, const uint8_t *codec_param)
{
    if (!ctx)
        return;

    ctx->enc1_cmd_0a_74 = codec_param ? *(const uint32_t*)(codec_param + 0x74) : 0u;
    if (ctx->enc1_cmd_0a_74 == 0u)
        ctx->enc1_cmd_0a_74 = 0x41eb0u;

    ctx->enc1_cmd_0b_7a = codec_param ? *(const uint16_t*)(codec_param + 0x7a) : 0u;
    if (ctx->enc1_cmd_0b_7a == 0u)
        ctx->enc1_cmd_0b_7a = 0x3e8u;

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
    ctx->enc1_cmd_12_ac = 0u;

    LOG_CODEC("AVPU: OEM Enc1 words seed 0x74=0x%08x 0x7a=0x%03x 0x7c=%u 0x7e=%u 0x10=%u 0xa8=0x%03x 0xaa=0x%03x 0xac=%u",
              ctx->enc1_cmd_0a_74, ctx->enc1_cmd_0b_7a, ctx->enc1_cmd_0b_7c,
              ctx->enc1_cmd_0b_7e, ctx->enc1_slice_10,
              ctx->enc1_cmd_12_a8, ctx->enc1_cmd_12_aa,
              ctx->enc1_cmd_12_ac);
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


/* Fill Enc1 command registers (OEM parity: from SliceParamToCmdRegsEnc1)
 *
 * The OEM fills cmd[0x00..0x1a], cmd[0x60..0x61], cmd[0x64..0x69], cmd[0x6e..0x6f].
 * Critical entries at cmd[0x0c..0x11] hold physical addresses for reconstruction
 * and reference buffers.  Entries at cmd[0x64..0x69,0x6f] hold additional buffer
 * addresses from the RecBuffer context.  Leaving these at zero causes the AVPU
 * to DMA to physical address 0x0 → AXI bus hang → hard SoC crash.
 */
static void fill_cmd_regs_enc1(const ALAvpuContext* ctx, uint32_t* cmd, uint32_t src_phys)
{
    if (!ctx || !cmd) return;

    const int has_reference = (ctx->reference_valid != 0) && (ctx->ref_buf.phy_addr != 0);
    const int is_idr = !has_reference;

    /* Ensure cmd pointer is 4-byte aligned (MIPS requirement) */
    if (((uintptr_t)cmd & 3) != 0) {
        LOG_CODEC("ERROR: cmd buffer not 4-byte aligned: %p", (void*)cmd);
        return;
    }

    /* Initialize entire command buffer to zero first (512 bytes = 128 uint32_t) */
    memset(cmd, 0, 512);

    /* cmd[0]: OEM SliceParamToCmdRegsEnc1 at 0x6dd5c.
     *
     * Confirmed from the OEM setup path at 0x6730c:
     *   slice_param[0] = 2                 (AVC codec enum)
     *   slice_param[1] = profile enum      (2/3/4)
     *   slice_param[2] = fixed AVC value   (packs to bits[22:20])
     *   slice_param[3] = log2_lcu_size     (packs to bits[26:24])
     *
     * CmdRegsEnc1ToSliceParam decodes bits[26:24] back to slice_param[3] and
     * uses it for the 1 << log2_lcu_size geometry math, so this is not a
     * generic picture-format field. For AVC both OEM fields are 4, which pack
     * to zero in their respective bitfields.
     */
    uint32_t codec_field = 2u;
    uint32_t profile_field = ((uint32_t)ctx->profile & 0x3u) + 2u;
    uint32_t min_cu_field = 4u;
    uint32_t lcu_size_field = 4u;
    uint32_t c0 = 0x11u | (1u << 31);
    c0 |= ((codec_field - 2u) & 0x3u) << 8;
    c0 |= ((profile_field - 2u) & 0x3u) << 10;
    c0 |= ((min_cu_field - 4u) & 0x7u) << 20;
    c0 |= ((lcu_size_field - 4u) & 0x7u) << 24;
    cmd[0] = c0;

    /* cmd[1]: OEM packs SliceParam[0x0a]/[0x0c], not raw pixels.
     * The setup path stores these as (width + 7) >> 3 and (height + 7) >> 3
     * before SliceParamToCmdRegsEnc1 applies the final -1 packing.
     *
     * OEM also fills bits[27:24] and bits[31:28] from SliceParam[0x19]/[0x1a].
     * For the current openimp AVC path those come from the packed source format
     * word at codec_param+0x10, which is 0x188 for NV12 8-bit 4:2:0.
     */
    if (ctx->enc_w && ctx->enc_h) {
        uint32_t enc_w_8 = (ctx->enc_w + 7u) >> 3;
        uint32_t enc_h_8 = (ctx->enc_h + 7u) >> 3;
        uint32_t bitdepth_luma = ctx->format_word & 0xFu;
        uint32_t bitdepth_chroma = (ctx->format_word >> 4) & 0xFu;
        cmd[1] = (((enc_h_8 - 1u) & 0x7FFu) << 12) | ((enc_w_8 - 1u) & 0x7FFu);
        cmd[1] |= (bitdepth_luma & 0xFu) << 24;
        cmd[1] |= (bitdepth_chroma & 0xFu) << 28;
    }

    /* cmd[2]: OEM SliceParamToCmdRegsEnc1 packs many slice-param fields here.
     * The confirmed first-picture AVC path comes from FillSliceParamFromPicParam:
     *   - fixed bit 13 (0x2000)
     *   - entropy/CABAC bit at 10
     *   - SliceParam[0x0f] = 3 for the first coded picture, which packs to bits[6:4]
     * Our prior low-bit IDR marker was guessed and has no OEM backing. */
    uint32_t slice_10 = is_idr ? (ctx->enc1_slice_10 & 0x3u) : 0u;
    cmd[2] = 0x2000u | (slice_10 << 8) | ((ctx->entropy_mode & 1u) << 10);
    if (is_idr) {
        cmd[2] |= 3u << 4;
    }

    /* cmd[3]: OEM first-picture AVC path special-cases arg3[0xc] == 7 and sets:
     *   SliceParam[0x28] = 0x1a  -> bits[23:16]
     *   SliceParam[0x30] = 1     -> bits[29:28]
     * Keep the confirmed AVC NAL unit type in bits[4:0].  Do not force the
     * previously guessed top bits for the first picture. */
    uint32_t nalu = is_idr ? 5u : 1u;
    if (is_idr) {
        cmd[3] = (nalu & 0x1Fu) | (0x1au << 16) | (1u << 28);
    } else {
        cmd[3] = (nalu & 0x1Fu) | (1u << 31) | (1u << 30);
    }

    /* cmd[4]: QP in low 5 bits */
    cmd[4] = (ctx->qp ? ctx->qp : 26) & 0x1F;

    /* cmd[7]: macroblock grid ((mb_h-1)<<12 | (mb_w-1)) */
    if (ctx->enc_w && ctx->enc_h) {
        uint32_t mb_w = (ctx->enc_w + 15) / 16;
        uint32_t mb_h = (ctx->enc_h + 15) / 16;
        cmd[7] = (((mb_h - 1) & 0x3FF) << 12) | ((mb_w - 1) & 0x3FF);
        cmd[5] = (1u << 12) | 0u; /* single slice */
        cmd[6] = (1u << 12) | 0u;
    }

    /* ---- Reconstruction & reference buffer addresses (OEM: SliceParamToCmdRegsEnc1) ----
     *
     * OEM fills these from SliceParam offsets 0x84..0xa4, which in turn are
     * filled by AL_EncRecBuffer_FillPlaneDesc with physical addresses.
     * Layout (NV12): Y plane at base, UV plane at base + width * align16(height).
     *
     * Without valid addresses here the AVPU DMAs to 0x0 → AXI hang.
     */
    if (ctx->enc_w && ctx->enc_h) {
        uint32_t y_plane_sz = avpu_get_nv12_luma_plane_size(ctx->enc_w, ctx->enc_h);

        /* ---- OEM SliceParamToCmdRegsEnc1 address window ----
         * HLIL confirms cmd[0x0c..0x11] are copied from SliceParam+0x84..0xa4.
         * Our reverse-engineering points to rec/ref DMA descriptors living in
         * these slots; keep source addresses in the middle pair so all six words
         * stay non-zero and plausible for the current AVC/NV12 path.
         */

        /* cmd[0x0a..0x0b]: OEM packs SliceParam+0x74 and +0x7a/+0x7c/+0x7e/+0x7f/+0x80,
         * not raw width/stride words. */
        cmd[0x0a] = ctx->enc1_cmd_0a_74;
        cmd[0x0b] = avpu_pack_enc1_cmd0b(ctx, has_reference);

        /* cmd[0x0c..0x0d]: reconstruction buffer */
        if (ctx->rec_buf.phy_addr) {
            cmd[0x0c] = ctx->rec_buf.phy_addr;                /* Rec Y */
            cmd[0x0d] = ctx->rec_buf.phy_addr + y_plane_sz;   /* Rec UV */
        }

        /* cmd[0x0e..0x0f]: source frame physical addresses */
        if (src_phys) {
            cmd[0x0e] = src_phys;                    /* Source Y phys addr */
            cmd[0x0f] = src_phys + y_plane_sz;       /* Source UV phys addr (NV12) */
        }

        /* cmd[0x10..0x11]: reference frame buffer.
         * OEM obtains these from RefMngr/DPB only when a ref picture exists. */
        if (has_reference) {
            cmd[0x10] = ctx->ref_buf.phy_addr;                /* Ref Y */
            cmd[0x11] = ctx->ref_buf.phy_addr + y_plane_sz;   /* Ref UV */
        }

        /* cmd[0x12]: OEM packs SliceParam+0xa8/+0xaa/+0xac from the encoder's
         * internal group-update state, not direct source-buffer stride heuristics. */
        cmd[0x12] = avpu_pack_enc1_cmd12(ctx);

        /* ---- RecBuffer context addresses (OEM: arg3 offsets) ----
         * Reconstruction-side descriptors still exist on the first picture;
         * only the reference-side descriptors should stay zero until a real
         * reference picture has been promoted on EndEncoding. */
        if (ctx->rec_buf.phy_addr) {
            cmd[0x64] = ctx->rec_buf.phy_addr;                   /* arg3+0x14: rec Y */
            cmd[0x65] = ctx->rec_buf.phy_addr + y_plane_sz;      /* arg3+0x18: rec UV */
        }
        if (has_reference) {
            cmd[0x67] = ctx->ref_buf.phy_addr + y_plane_sz;      /* arg3+0x54: ref UV */
            cmd[0x68] = ctx->ref_buf.phy_addr;                   /* arg3+0x34: ref Y */
            cmd[0x69] = ctx->ref_buf.phy_addr + y_plane_sz;      /* arg3+0x44: ref UV alt */
            cmd[0x6f] = ctx->ref_buf.phy_addr;                   /* arg3+0x94: ref base */
        }
    }
}

static void log_first_enc1_cmd_window(const ALAvpuContext* ctx, uint32_t idx, const uint32_t* cmd)
{
    if (!ctx || !cmd) return;
    if (idx != 0 || ctx->frames_encoded != 0) return;

    LOG_CODEC("Process: first Enc1 CL[%u] NV12 luma height %u->%u y_plane=0x%08x",
              idx, ctx->enc_h, avpu_get_nv12_luma_lines(ctx->enc_h),
              avpu_get_nv12_luma_plane_size(ctx->enc_w, ctx->enc_h));
    LOG_CODEC("Process: first Enc1 CL[%u] fmt=0x%08x cmd[0]=0x%08x cmd[1]=0x%08x cmd[2]=0x%08x cmd[3]=0x%08x",
              idx, ctx->format_word, cmd[0], cmd[1], cmd[2], cmd[3]);
    LOG_CODEC("Process: first Enc1 CL[%u] cmd[0x0a]=0x%08x cmd[0x0b]=0x%08x cmd[0x0c]=0x%08x cmd[0x0d]=0x%08x",
              idx, cmd[0x0a], cmd[0x0b], cmd[0x0c], cmd[0x0d]);
    LOG_CODEC("Process: first Enc1 CL[%u] cmd[0x0e]=0x%08x cmd[0x0f]=0x%08x cmd[0x10]=0x%08x cmd[0x11]=0x%08x cmd[0x12]=0x%08x",
              idx, cmd[0x0e], cmd[0x0f], cmd[0x10], cmd[0x11], cmd[0x12]);
    LOG_CODEC("Process: first Enc1 CL[%u] cmd[0x64]=0x%08x cmd[0x65]=0x%08x cmd[0x67]=0x%08x cmd[0x68]=0x%08x",
              idx, cmd[0x64], cmd[0x65], cmd[0x67], cmd[0x68]);
    LOG_CODEC("Process: first Enc1 CL[%u] cmd[0x69]=0x%08x cmd[0x6f]=0x%08x",
              idx, cmd[0x69], cmd[0x6f]);
}

static void avpu_promote_reference(ALAvpuContext *ctx)
{
    if (!ctx || !ctx->rec_buf.phy_addr || !ctx->ref_buf.phy_addr)
        return;

    AvpuDMABuf prev_ref = ctx->ref_buf;
    ctx->ref_buf = ctx->rec_buf;
    ctx->rec_buf = prev_ref;
    __sync_synchronize();
    ctx->reference_valid = 1;

    LOG_CODEC("EndEncoding: promoted rec->ref ref=0x%08x next_rec=0x%08x",
              ctx->ref_buf.phy_addr, ctx->rec_buf.phy_addr);
}

/* OEM uses 24-bit interrupt clear (0xFFFFFF), not 32-bit.
 * Writing to non-existent upper bits can hang the bus on T31. */
#define AVPU_IRQ_CLEAR_MASK 0x00FFFFFFu

/* Enable interrupts for core (OEM parity: AL_EncCore_EnableInterrupts at 0x6cf78) */
static void avpu_enable_interrupts(int fd, int core)
{
    /* Avoid read_reg path (problematic on some kernels). Program mask directly. */
    unsigned b0 = 1u << (((unsigned)core << 2) & 0x1F);
    unsigned b2 = 1u << ((((unsigned)core << 2) + 2) & 0x1F);
    unsigned new_m = (b0 | b2);

    /* Ack any stale pending interrupts before enabling */
    avpu_write_reg(fd, AVPU_INTERRUPT, AVPU_IRQ_CLEAR_MASK);

    LOG_CODEC("AVPU: enable_interrupts core=%d, mask=0x%08x (bits: 0x%x | 0x%x)", core, new_m, b0, b2);
    avpu_write_reg(fd, AVPU_INTERRUPT_MASK, new_m);
    LOG_CODEC("AVPU: wrote INTERRUPT_MASK=0x%08x", new_m);
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

    /* For now, just signal that data is ready by incrementing a counter
     * The GetStream function will check stream buffers and queue to FIFO
     * This avoids touching stream buffers from the callback thread
     */
    avpu_promote_reference(ctx);
    __sync_fetch_and_add(&ctx->frames_encoded, 1);

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

/* OEM parity: IsEnc1AlreadyRunning() reads (core<<9)+0x83f8 and checks bit 1.
 * If this bit is set, AL_EncCore_Encode1() does not push a new command list. */
static int avpu_is_enc1_running(int fd, int core)
{
    unsigned int status = 0;

    if (avpu_read_reg(fd, AVPU_REG_CORE_STATUS(core), &status) != 0) {
        LOG_CODEC("AVPU: failed to read Enc1 running state for core %d", core);
        return 0;
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
                LOG_CODEC("IRQ thread: WAIT_IRQ failed: %s", strerror(errno));
            }
            free(raw);
            break;
        }

        uint32_t irq_id = *p_irq;
        free(raw);

        /* OEM: if (var_28 u>= 0x14) fprintf(stderr, ...) */
        if (irq_id >= 20) {
            LOG_CODEC("IRQ thread: invalid IRQ ID %d", irq_id);
            continue;
        }

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
                                LOG_CODEC("AVPU: stream buf[%d] phys=0x%08x size=%d (imp-alloc)", filled, enc->avpu.stream_bufs[filled].phy_addr, enc->avpu.stream_buf_size);
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
                                LOG_CODEC("AVPU: cmdlist ring phys=0x%08x size=%zu entries=%u (imp-alloc)", phys, cl_bytes, enc->avpu.cl_count);
                            }
                        } else {
                            LOG_CODEC("AVPU: failed to allocate cmdlist ring via IMP_Alloc (size=%zu)", cl_bytes);
                        }

                        /* Allocate reconstruction and reference frame DMA buffers.
                         * The AVPU hardware writes reconstructed frames and reads
                         * reference frames via physical addresses in the command list.
                         * Without valid addresses the AVPU DMAs to 0x0 → AXI hang. */
                        {
                            /* T31 NV12 uses 16-line luma alignment; match the
                             * FrameSource/kernel sizeimage (e.g. 1920x1080 -> 3133440). */
                            size_t nv12_sz = avpu_get_nv12_frame_size(width, height);
                            nv12_sz = (nv12_sz + 0xFFF) & ~(size_t)0xFFF; /* page-align */

                            memset(&enc->avpu.rec_buf, 0, sizeof(AvpuDMABuf));
                            memset(&enc->avpu.ref_buf, 0, sizeof(AvpuDMABuf));

                            if (avpu_alloc_imp(nv12_sz, "AVPU_REC", &enc->avpu.rec_buf) == 0) {
                                /* Do NOT memset — rec_buf is AVPU output (reconstruction),
                                 * and zeroing 3MB of uncached DMA memory can stall/hang
                                 * the AXI bus on cold boot. */
                                LOG_CODEC("AVPU: rec_buf phys=0x%08x size=%zu", enc->avpu.rec_buf.phy_addr, nv12_sz);
                            } else {
                                LOG_CODEC("AVPU: WARNING - failed to allocate rec_buf (%zu bytes)", nv12_sz);
                            }

                            if (avpu_alloc_imp(nv12_sz, "AVPU_REF", &enc->avpu.ref_buf) == 0) {
                                /* Do NOT memset — ref_buf content is irrelevant for the
                                 * first IDR frame (intra-only), and subsequent frames will
                                 * have valid reconstruction data copied in by the AVPU. */
                                LOG_CODEC("AVPU: ref_buf phys=0x%08x size=%zu", enc->avpu.ref_buf.phy_addr, nv12_sz);
                            } else {
                                LOG_CODEC("AVPU: WARNING - failed to allocate ref_buf (%zu bytes)", nv12_sz);
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

                        /* Register OEM callbacks (AL_EncCore_Init / AL_EncCore_EnableInterrupts).
                         * OEM uses base bit (ch*4) for EndEncoding and base+2 for
                         * EndAvcEntropy. For channel 0: IRQ 0 and IRQ 2; for
                         * channel 1: IRQ 4 and IRQ 6, etc.
                         */
                        int channel = enc->channel_id - 1; /* 0-based */
                        int base_bit = (channel * 4) & 0x1f;
                        int irq_id0 = base_bit;               /* e.g., 0, 4, 8, ... */
                        int irq_id2 = (base_bit + 2) & 0x1f;   /* e.g., 2, 6, 10, ... */

                        avpu_register_callback(&enc->avpu, avpu_end_encoding_callback, &enc->avpu, irq_id0);
                        LOG_CODEC("AVPU: registered EndEncoding callback for channel %d (IRQ %d)", channel, irq_id0);

                        /* OEM maps the +2 interrupt to EndAvcEntropy, not EndEncoding. */
                        if (irq_id2 < 20) {
                            avpu_register_callback(&enc->avpu, avpu_end_avc_entropy_callback, &enc->avpu, irq_id2);
                            LOG_CODEC("AVPU: registered EndAvcEntropy callback for channel %d (IRQ %d)", channel, irq_id2);
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

            /* Step 0: MISC_CTRL — OEM writes 0x1000 during scheduler init.
             * This enables the AVPU's AXI/DMA master. Without it, the hardware
             * receives CL_PUSH but cannot read the command list from RAM. */
            avpu_write_reg(fd, AVPU_REG_MISC_CTRL, 0x00001000);

            /* Step 1: ResetCore — rapid back-to-back writes, NO sleeps */
            LOG_CODEC("AVPU: ResetCore (1,2,4) to reg 0x%x", AVPU_REG_CORE_RESET(0));
            avpu_write_reg(fd, AVPU_REG_CORE_RESET(0), 0x00000001);
            avpu_write_reg(fd, AVPU_REG_CORE_RESET(0), 0x00000002);
            avpu_write_reg(fd, AVPU_REG_CORE_RESET(0), 0x00000004);

            /* Step 2: Clear interrupts (OEM: 0xFFFFFF — 24-bit, NOT 32-bit) */
            avpu_write_reg(fd, AVPU_INTERRUPT, 0x00FFFFFF);

            /* Step 3: TOP_CTRL per OEM */
            avpu_write_reg(fd, AVPU_REG_TOP_CTRL, 0x00000080);

            ctx->session_ready = 1;

            /* Enable only our core interrupts without readback */
            avpu_enable_interrupts(fd, 0);

            /* Pre-queue ALL stream buffers so hardware always has space */
            if (ctx->stream_bufs_used > 0) {
                for (int i = 0; i < ctx->stream_bufs_used; ++i) {
                    if (!ctx->stream_in_hw[i] && ctx->stream_bufs[i].phy_addr) {
                        avpu_write_reg(fd, AVPU_REG_STRM_PUSH, ctx->stream_bufs[i].phy_addr);
                        ctx->stream_in_hw[i] = 1;
                        LOG_CODEC("AVPU: pre-queued stream buf[%d] phys=0x%08x", i, ctx->stream_bufs[i].phy_addr);
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

            /* Fill Enc1 command registers — source addr goes INTO the CL entry */
            fill_cmd_regs_enc1(ctx, cmd, phys_addr);
            log_first_enc1_cmd_window(ctx, idx, cmd);

            /* OEM AL_EncCore_Encode1() checks IsEnc1AlreadyRunning() before
             * flushing/pushing a new Enc1 command list. Our simplified path
             * uses a single effective rec/ref pair, so matching this gate is
             * important before submitting the next frame. */
            if (avpu_is_enc1_running(fd, 0)) {
                LOG_CODEC("Process: Enc1 already running; skipping CL[%u] submit to match OEM gating", idx);
                free(hw_stream);
                return -1;
            }

            /* Flush CL entry from CPU cache to physical RAM.
             * rmem mappings are CACHED — without this the AVPU reads stale data.
             * OEM: Rtos_FlushCacheMemory(cmdlist_entry, 0x100000) before CL_PUSH.
             * dir=1 = DMA_TO_DEVICE (writeback, CPU→RAM). */
            avpu_flush_cache(fd, cmd, ctx->cl_entry_size, 1 /*WBACK*/);

            /* OEM StartEnc1WithCommandList: write CL_ADDR then CL_PUSH only. */
            uint32_t cl_phys = ctx->cl_ring.phy_addr + (idx * ctx->cl_entry_size);
            uint32_t ref_phys = ctx->reference_valid ? ctx->ref_buf.phy_addr : 0;
            LOG_CODEC("Process: CL_ADDR=0x%08x src=0x%08x rec=0x%08x ref=0x%08x CL[%u]",
                      cl_phys, phys_addr,
                      ctx->rec_buf.phy_addr, ref_phys, idx);
            avpu_write_reg(fd, AVPU_REG_CL_ADDR, cl_phys);
            avpu_write_reg(fd, AVPU_REG_CL_PUSH, 0x00000002);

            /* Advance to next CL slot */
            ctx->cl_idx = (idx + 1) % ctx->cl_count;
            codec_queue_frame_metadata(enc, user_data);
            submitted = 1;

            LOG_CODEC("Process: AVPU queued frame %ux%u phys=0x%x CL[%u] - encoding triggered", width, height, phys_addr, idx);
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

    if (enc->use_hardware == 2 && enc->avpu.fd >= 0) {
        /* OEM parity: Poll for encoded data, then check stream buffers
         * The EndEncoding callback increments frames_encoded counter.
         * We poll for the counter, then safely check stream buffers.
         */
        ALAvpuContext *ctx = &enc->avpu;

        if (!ctx->session_ready) {
            errno = EAGAIN;
            return -1;
        }

        LOG_CODEC("GetStream[AVPU]: polling for encoded frame (frames_encoded=%d)...", ctx->frames_encoded);

        /* Poll for frames_encoded to increment (max 2 seconds) */
        int initial_count = ctx->frames_encoded;
        struct timespec nap = {0, 10000000L}; /* 10ms */

        for (int retry = 0; retry < 200; ++retry) {
            if (ctx->frames_encoded > initial_count) {
                for (int i = 0; i < ctx->stream_bufs_used; ++i) {
                    if (ctx->stream_in_hw[i]) {
                        const uint8_t *virt = (const uint8_t*)ctx->stream_bufs[i].map;
                        size_t eff = annexb_effective_size(virt, ctx->stream_buf_size);
                        if (eff > 0) {
                            HWStreamBuffer *s = (HWStreamBuffer*)malloc(sizeof(HWStreamBuffer));
                            if (!s) return -1;
                            s->phys_addr = ctx->stream_bufs[i].phy_addr;
                            s->virt_addr = (uint32_t)(uintptr_t)virt;
                            s->length = (uint32_t)eff;
                            s->timestamp = 0;
                            s->frame_type = 0;
                            s->slice_type = 0;
                            ctx->stream_in_hw[i] = 0;
                            *stream = s;
                            *user_data = codec_dequeue_frame_metadata(enc);
                            LOG_CODEC("GetStream[AVPU]: ✓ got stream buf[%d] phys=0x%08x len=%u",
                                     i, s->phys_addr, s->length);
                            return 0;
                        }
                    }
                }
                LOG_CODEC("GetStream[AVPU]: frames_encoded incremented but no valid data found");
            }

            nanosleep(&nap, NULL);
        }

        /* Timeout */
        LOG_CODEC("GetStream[AVPU]: ✗ TIMEOUT - no stream data after 2s (frames_encoded=%d)", ctx->frames_encoded);
        errno = EAGAIN;
        return -1;
    }

    /* Legacy/SW path: dequeue from our FIFO (wait indefinitely) */
    void *s = Fifo_Dequeue(enc->fifo_streams, -1);
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

