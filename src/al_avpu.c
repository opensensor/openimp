/*
 * AL AVPU scaffolding (vendor-like AL layer over /dev/avpu)
 *
 * This file contains compile-ready stubs to progressively implement
 * a vendor-like AL layer on Ingenic T31. For now it only opens/tears down
 * the device and exposes placeholders for queue/dequeue.
 */

#include "al_avpu.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <pthread.h>
#include <time.h>

#include "fifo.h"
#include "dma_alloc.h"
#include "device_pool.h"


/* Local copy of minimal /dev/avpu UAPI (do not include driver headers) */
#include <sys/ioctl.h>
#ifndef AVPU_IOC_MAGIC
#define AVPU_IOC_MAGIC 'q'
#endif
struct avpu_reg { unsigned int id; unsigned int value; };
struct avpu_dma_info { uint32_t fd; uint32_t size; uint32_t phy_addr; };
#define AL_CMD_UNBLOCK_CHANNEL _IO(AVPU_IOC_MAGIC, 1)
#define AL_CMD_IP_WRITE_REG    _IOWR(AVPU_IOC_MAGIC, 10, struct avpu_reg)
#define AL_CMD_IP_READ_REG     _IOWR(AVPU_IOC_MAGIC, 11, struct avpu_reg)
#define AL_CMD_IP_WAIT_IRQ     _IOWR(AVPU_IOC_MAGIC, 12, int)
#define GET_DMA_MMAP           _IOWR(AVPU_IOC_MAGIC, 26, struct avpu_dma_info)
#define GET_DMA_FD             _IOWR(AVPU_IOC_MAGIC, 13, struct avpu_dma_info)
#define GET_DMA_PHY            _IOWR(AVPU_IOC_MAGIC, 18, struct avpu_dma_info)
#define JZ_CMD_FLUSH_CACHE     _IOWR(AVPU_IOC_MAGIC, 14, int)

/* Cache flush helper (matches driver struct) */
struct flush_cache_info { unsigned int addr; unsigned int len; unsigned int dir; };
#ifndef DMA_TO_DEVICE
#define DMA_TO_DEVICE 1
#define DMA_FROM_DEVICE 2
#define DMA_BIDIRECTIONAL 3
#endif

/* T31 RMEM cache flush workaround: The kernel's dma_cache_sync() has critical bugs
 * that cause system-wide crashes. RMEM appears to be mapped cache-coherent, so
 * explicit cache flushes are not needed and actually harmful. */
static void avpu_cache_inv(int fd, uint32_t phys, uint32_t len)
{
    /* DISABLED: T31 kernel bug - cache flush causes crashes */
    (void)fd; (void)phys; (void)len;
}

static void avpu_cache_clean(int fd, uint32_t phys, uint32_t len)
{
    /* DISABLED: T31 kernel bug - cache flush causes crashes */
    (void)fd; (void)phys; (void)len;
}

/* Virtual-address cache ops: driver expects a CPU-mapped pointer in addr */
static void avpu_cache_inv_virt(int fd, void* addr, uint32_t len)
{
    /* DISABLED: T31 kernel bug - cache flush causes crashes */
    (void)fd; (void)addr; (void)len;
}

static void avpu_cache_clean_virt(int fd, void* addr, uint32_t len)
{
    /* DISABLED: T31 kernel bug - cache flush causes crashes */
    (void)fd; (void)addr; (void)len;
}

static size_t annexb_effective_size(const uint8_t *buf, size_t maxlen)
{
    if (!buf || maxlen < 4) return 0;
    size_t first = SIZE_MAX;
    size_t last = 0;
    for (size_t i = 0; i + 3 < maxlen; ++i) {
        if ((buf[i] == 0 && buf[i+1] == 0 && buf[i+2] == 1) ||
            (i + 4 < maxlen && buf[i] == 0 && buf[i+1] == 0 && buf[i+2] == 0 && buf[i+3] == 1)) {
            if (first == SIZE_MAX) first = i;
            last = i; /* mark start of last NAL */
        }
    }
    if (first == SIZE_MAX) return 0; /* no start code */
    /* Find end: next start code after last, or trim trailing zeros */
    size_t end = maxlen;
    for (size_t i = last + 3; i + 3 < maxlen; ++i) {
        if ((buf[i] == 0 && buf[i+1] == 0 && buf[i+2] == 1) ||
            (i + 4 < maxlen && buf[i] == 0 && buf[i+1] == 0 && buf[i+2] == 0 && buf[i+3] == 1)) {
            end = i; break;
        }
    }
    if (end == maxlen) {
        /* trim trailing zeros */
        while (end > first && buf[end-1] == 0) end--;
    }
    if (end <= first) return 0;
    return end - first;
}

#define LOG_AL(fmt, ...) fprintf(stderr, "[AL-AVPU] " fmt "\n", ##__VA_ARGS__)

/* --- Sensor probing (/proc/jz/sensor) --- */
static int read_int_file(const char* path, int* out)
{
    FILE* f = fopen(path, "r");
    if (!f) return -1;
    char buf[64];
    if (!fgets(buf, sizeof(buf), f)) { fclose(f); return -1; }
    fclose(f);
    long v = strtol(buf, NULL, 0);
    if (out) *out = (int)v;
    return 0;
}
static int read_str_file(const char* path, char* out, size_t out_sz)
{
    if (!out || out_sz == 0) return -1;
    FILE* f = fopen(path, "r");
    if (!f) return -1;
    size_t n = fread(out, 1, out_sz-1, f);
    fclose(f);
    if (n == 0) return -1;
    out[n] = '\0';
    /* strip trailing newlines */
    for (size_t i = 0; i < n; ++i) { if (out[i] == '\n' || out[i] == '\r') { out[i] = '\0'; break; } }
    return 0;
}
static int probe_sensor(int* sw, int* sh, int* sfps, char* name, size_t name_sz)
{
    int width=0, height=0, fps=0; char nbuf[64]={0};
    int ok_w = read_int_file("/proc/jz/sensor/width", &width) == 0;
    int ok_h = read_int_file("/proc/jz/sensor/height", &height) == 0;
    int ok_f = read_int_file("/proc/jz/sensor/actual_fps", &fps) == 0;
    int ok_n = read_str_file("/proc/jz/sensor/name", nbuf, sizeof(nbuf)) == 0;
    if (sw && ok_w) *sw = width;
    if (sh && ok_h) *sh = height;
    if (sfps && ok_f) *sfps = fps;
    if (name && name_sz && ok_n) { snprintf(name, name_sz, "%s", nbuf); }
    return (ok_w && ok_h) ? 0 : -1;
}

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

/* Driver register window basics for T31 */
#define AVPU_BASE_OFFSET  0x8000
#define AVPU_INTERRUPT_MASK (AVPU_BASE_OFFSET + 0x14)
#define AVPU_INTERRUPT      (AVPU_BASE_OFFSET + 0x18)
/* Observed in driver debug: stream/src push registers */
#define AVPU_REG_SRC_PUSH   (AVPU_BASE_OFFSET + 0x84)   /* 0x8084 */
#define AVPU_REG_STRM_PUSH  (AVPU_BASE_OFFSET + 0x94)   /* 0x8094 */
/* From driver: AXI address offset register for IP */
/* Hypothesized control regs for enabling source/stream paths (bring-up) */
#define AVPU_REG_SRC_CTRL   (AVPU_BASE_OFFSET + 0x80)   /* 0x8080 */
#define AVPU_REG_STRM_CTRL  (AVPU_BASE_OFFSET + 0x90)   /* 0x8090 */

#define AVPU_REG_AXI_ADDR_OFFSET_IP (AVPU_BASE_OFFSET + 0x1208) /* 0x9208 */
/* Per-core control window (doorbell/commit) */
/* Per-core doorbell register (offset +0x08 from core base 0x83F0) */
#define AVPU_REG_CORE0_DOORBELL  (AVPU_BASE_OFFSET + 0x3F8)   /* 0x83F8 */





/* Forward declarations for register IO helpers used below */
static int avpu_ip_write_reg(int fd, unsigned int off, unsigned int val);
static int avpu_ip_read_reg(int fd, unsigned int off, unsigned int *out);

/* OEM-observed control regs (from BN MCP) */
#define AVPU_REG_TOP_CTRL      (AVPU_BASE_OFFSET + 0x54)  /* 0x8054 */
#define AVPU_REG_TOP_STATE     (AVPU_BASE_OFFSET + 0x50)  /* 0x8050 (state/clear) */
#define AVPU_REG_MISC_CTRL     (AVPU_BASE_OFFSET + 0x10)  /* 0x8010: AXI/addr mode per OEM */
/* Per-core register window: base = AVPU_BASE_OFFSET + 0x3F0 + (core<<9) */
static inline unsigned AVPU_CORE_BASE(int core) { return (AVPU_BASE_OFFSET + 0x3F0) + ((unsigned)core << 9); }
#define AVPU_REG_CORE_RESET(c)   (AVPU_CORE_BASE(c) + 0x00) /* ResetCore: write 1,2,4 */
#define AVPU_REG_CORE_CLKCMD(c)  (AVPU_CORE_BASE(c) + 0x04) /* SetClockCommand: set bits[1:0] to 1 */
#define AVPU_REG_CL_ADDR         (AVPU_BASE_OFFSET + 0x3E0) /* 0x83E0: command list start */
#define AVPU_REG_CL_PUSH         (AVPU_BASE_OFFSET + 0x3E4) /* 0x83E4: push/trigger (2) */
#define AVPU_REG_ENC_EN_A        (AVPU_BASE_OFFSET + 0x5F0) /* 0x85F0: enable */
#define AVPU_REG_ENC_EN_B        (AVPU_BASE_OFFSET + 0x5F4) /* 0x85F4: clock/ungate */
#define AVPU_REG_ENC_EN_C        (AVPU_BASE_OFFSET + 0x5E4) /* 0x85E4: enable */



static int avpu_ip_write_reg(int fd, unsigned int off, unsigned int val)
{
    if ((off & 3) != 0) { LOG_AL("WARN: unaligned reg write off=0x%04x", off); return -1; }
    struct avpu_reg *r = NULL;
    if (posix_memalign((void**)&r, 64, sizeof(*r)) != 0 || !r) { errno = ENOMEM; return -1; }
    r->id = off; r->value = val;
    LOG_AL("about to IOCTL WRITE off=0x%04x val=0x%08x arg=%p", off, val, (void*)r);
    int ret = ioctl(fd, AL_CMD_IP_WRITE_REG, r);
    if (ret < 0) { LOG_AL("ioctl WRITE failed off=0x%04x err=%d errno=%d", off, ret, errno); free(r); return -1; }
    LOG_AL("WRITE[0x%04x] <- 0x%08x", off, val);
    free(r);
    return 0;
}

static int avpu_ip_read_reg(int fd, unsigned int off, unsigned int *out)
{
    if ((off & 3) != 0) { LOG_AL("WARN: unaligned reg read off=0x%04x", off); return -1; }
    struct avpu_reg *r = NULL;
    if (posix_memalign((void**)&r, 64, sizeof(*r)) != 0 || !r) { errno = ENOMEM; return -1; }
    r->id = off; r->value = 0;
    int ret = ioctl(fd, AL_CMD_IP_READ_REG, r);
    if (ret < 0) { LOG_AL("ioctl READ failed off=0x%04x err=%d errno=%d", off, ret, errno); free(r); return -1; }
    if (out) *out = r->value;
    LOG_AL("READ[0x%04x] -> 0x%08x", off, r->value);
    free(r);
    return 0;
}

/* Minimal OEM-style interrupt unmask on core: clear mask bits for Enc1 and its +2 companion.
 * Mirrors AL_EncCore_EnableInterrupts behavior limited to our single core case.
 */
static void avpu_enable_interrupts_core(int fd, int core)
{
    unsigned m = 0;
    if (avpu_ip_read_reg(fd, AVPU_INTERRUPT_MASK, &m) < 0)
        return;
    unsigned b0 = 1u << (((unsigned)core << 2) & 0x1F);
    unsigned b2 = 1u << ((((unsigned)core << 2) + 2) & 0x1F);
    unsigned new_m = m | (b0 | b2); /* set bits to enable */
    if (new_m != m)
        (void)avpu_ip_write_reg(fd, AVPU_INTERRUPT_MASK, new_m);
}

/* Compose minimal Enc1 command registers based on OEM SliceParamToCmdRegsEnc1
 * for a single-slice, whole-picture Baseline 4:2:0 encode.
 */
static void fill_cmd_regs_from_oem_enc1(const ALAvpuContext* ctx, uint32_t* cmd)
{
    if (!ctx || !cmd) return;
    /* Zeroed by caller; set fields mirrored from BN decompile */
    /* cmd[0]: base flags and formats (see SliceParamToCmdRegsEnc1) */
    uint32_t c0 = 0;
    c0 |= 0x11;                /* base flags */
    c0 |= (1u << 8);           /* [9:8]=(4:2:0) => 1 */
    /* [11:10]=0 Baseline; [22:20]=0 choose level index 4 => 0 */
    c0 |= (1u << 31);          /* entry valid / start */
    cmd[0] = c0;

    /* cmd[1]: picture dimensions: ((h-1)<<12 | (w-1)) */
    if (ctx->enc_w && ctx->enc_h) {
        uint32_t w1 = ctx->enc_w - 1;
        uint32_t h1 = ctx->enc_h - 1;
        cmd[1] = ((h1 & 0x7FF) << 12) | (w1 & 0x7FF);
    }

    /* cmd[2]: set fixed 0x2000 bit per OEM */
    cmd[2] |= 0x2000u;

    /* cmd[3]: NAL/slice basic flags: low 5 bits NAL type; set IDR for first, else non-IDR */
    uint32_t nalu = (ctx->cl_idx == 0) ? 5u : 1u;
    uint32_t c3 = 0;
    c3 |= nalu & 0x1Fu;        /* NAL unit type */
    c3 |= (1u << 31) | (1u << 30); /* single-slice/full picture flags */
    cmd[3] = c3;

    /* cmd[4]: QP in low 5 bits */
    uint32_t qp = ctx->qp ? (ctx->qp & 0x1F) : 26u;
    cmd[4] = (cmd[4] & ~0x1Fu) | qp;

    /* cmd[7]: macroblock grid ((mb_h-1)<<12 | (mb_w-1)) */
    if (ctx->enc_w && ctx->enc_h) {
        uint32_t mb_w = (ctx->enc_w + 15) / 16;
        uint32_t mb_h = (ctx->enc_h + 15) / 16;
        uint32_t mw1 = (mb_w ? mb_w - 1 : 0) & 0x3FF;
        uint32_t mh1 = (mb_h ? mb_h - 1 : 0) & 0x3FF;
        cmd[7] = (mh1 << 12) | mw1;
        /* cmd[5] and cmd[6]: single slice spanning the whole frame */
        cmd[5] = (1u << 12) | 0u; /* quotient=1, remainder=0 */
        cmd[6] = (1u << 12) | 0u;
    }
}






/* Lightweight register dump to help identify start/status regs during bring-up */
static void avpu_dump_regs(int fd)
{
    struct { unsigned base; unsigned count; } ranges[] = {
        { AVPU_BASE_OFFSET + 0x10, 3 },    /* 0x8010..0x8018 (including MASK/STATUS) */
        { AVPU_BASE_OFFSET + 0x50, 5 },    /* 0x8050..0x8060 (TOP area) */
        { AVPU_BASE_OFFSET + 0x80, 8 },    /* 0x8080..0x809c (SRC/STRM push) */
        { AVPU_BASE_OFFSET + 0xA0, 24 },   /* 0x80A0..0x80FC (control window around queues) */
        { AVPU_BASE_OFFSET + 0x100, 32 },  /* 0x8100..0x817C (more control/status) */


        { AVPU_BASE_OFFSET + 0x200, 32 },  /* 0x8200..0x827C */
        { AVPU_BASE_OFFSET + 0x3F0, 4 },   /* 0x83F0..0x83FC (per-core ctrl window core0) */
        { AVPU_BASE_OFFSET + 0x900, 48 },  /* 0x8900..0x8A2C (if aliased) */
        { AVPU_BASE_OFFSET + 0x1000, 48 }, /* 0x9000..0x90BC */
        { AVPU_BASE_OFFSET + 0x1200, 16 }, /* 0x9200..0x923C (incl. AXI offset) */
    };
    for (unsigned r = 0; r < sizeof(ranges)/sizeof(ranges[0]); ++r) {
        unsigned base = ranges[r].base;
        for (unsigned i = 0; i < ranges[r].count; ++i) {
            unsigned off = base + (i * 4);
            unsigned val = 0; (void)avpu_ip_read_reg(fd, off, &val);
            LOG_AL("REG[0x%04x] = 0x%08x", off, val);
        }
    }
}

/* OEM-mirroring: write back the values we read in the control windows.
 * This is conservative (no guessed values) and still matches OEM behavior of
 * touching these registers inside the commit bracket, which may ungate paths
 * with write-latch semantics. */
static void avpu_oem_mirror_ctrl_windows(int fd)
{
    unsigned win1_base = AVPU_BASE_OFFSET + 0x0A0;  /* 0x80A0..0x80FC */
    unsigned win2_base = AVPU_BASE_OFFSET + 0x0100; /* 0x8100..0x817C */

    for (unsigned i = 0; i < 24; ++i) {
        unsigned off = win1_base + (i * 4);
        unsigned v = 0; (void)avpu_ip_read_reg(fd, off, &v);
        (void)avpu_ip_write_reg(fd, off, v);
    }
    for (unsigned i = 0; i < 32; ++i) {
        unsigned off = win2_base + (i * 4);
        unsigned v = 0; (void)avpu_ip_read_reg(fd, off, &v);
        (void)avpu_ip_write_reg(fd, off, v);
    }
    LOG_AL("oem-mirror: touched 0x80A0..0x80FC and 0x8100..0x817C");
}



static int avpu_get_dma_mmap(int fd, size_t size, AvpuDMABuf *out)
{
    if (!out) return -1;
    struct avpu_dma_info info = { .fd = 0, .size = (uint32_t)size, .phy_addr = 0 };
    if (ioctl(fd, GET_DMA_MMAP, &info) < 0) return -1;

    void *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, info.fd);
    if (map == MAP_FAILED) return -1;

    out->phy_addr = info.phy_addr;
    out->mmap_off = (int)info.fd;
    out->dmabuf_fd = -1;
    out->map = map;
    out->size = size;
    return 0;
}

static int avpu_get_dma_fd_map(int avpu_fd, size_t size, AvpuDMABuf *out)
{
    if (!out) return -1;
    struct avpu_dma_info info = { .fd = 0, .size = (uint32_t)size, .phy_addr = 0 };
    if (ioctl(avpu_fd, GET_DMA_FD, &info) < 0) return -1;

    void *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, info.fd, 0);
    if (map == MAP_FAILED) {
        int saved = errno;
        close(info.fd);
        errno = saved;
        return -1;
    }
    out->phy_addr = info.phy_addr;
    out->mmap_off = 0;
    out->dmabuf_fd = (int)info.fd;
    out->map = map;
    out->size = size;
	return 0;
}



/**
 * ALAvpu_Init - Initialize AVPU context with device fd and encoder params
 * Device fd must be obtained via AL_DevicePool_Open before calling this
 * (OEM parity: no ALAvpu_Open, initialization done in encoder layer)
 */
int ALAvpu_Init(ALAvpuContext *ctx, int fd, const HWEncoderParams *p)
{
    if (!ctx || fd < 0 || !p) return -1;
    memset(ctx, 0, sizeof(*ctx));

    ctx->fd = fd;
    ctx->event_fd = -1; /* can wire later via eventfd if needed */

    /* Initial pool sizing; will be tuned using params later */
    /* Conservative default until RMEM size is confirmed on device */
    ctx->stream_buf_count = 4;
    ctx->stream_buf_size  = 128 * 1024; /* 128KB default */
    ctx->frame_buf_count  = 4;
    ctx->frame_buf_size   = 0x458;      /* matches our VBM frame stub size */

    /* Provision stream buffers from rmem (IMP_Alloc) to avoid driver GET_DMA_* */
    ctx->stream_bufs_used = 0;
    int wanted = ctx->stream_buf_count;
    if (wanted > (int)(sizeof(ctx->stream_bufs)/sizeof(ctx->stream_bufs[0])))
        wanted = (int)(sizeof(ctx->stream_bufs)/sizeof(ctx->stream_bufs[0]));

    /* Probe sensor to auto-derive width/height/fps when not provided */
    int s_w=0, s_h=0, s_fps=0; char s_name[64]={0};
    int have_sensor = (probe_sensor(&s_w, &s_h, &s_fps, s_name, sizeof(s_name)) == 0);
    uint32_t eff_w = p->width ? p->width : (uint32_t)(have_sensor ? s_w : 0);
    uint32_t eff_h = p->height ? p->height : (uint32_t)(have_sensor ? s_h : 0);
    uint32_t eff_fps_num = p->fps_num ? p->fps_num : (uint32_t)(have_sensor ? s_fps : 25);
    uint32_t eff_fps_den = p->fps_den ? p->fps_den : 1;
    if (have_sensor) {
        LOG_AL("sensor: %s %dx%d @ %d fps", s_name[0]?s_name:"(unknown)", s_w, s_h, s_fps);
    } else {
        LOG_AL("sensor: not detected (using provided params if any)");
    }

    /* Cache effective encoding parameters for command-list population */
    ctx->enc_w = eff_w;
    ctx->enc_h = eff_h;
    ctx->fps_num = eff_fps_num;
    ctx->fps_den = eff_fps_den;
    ctx->profile = p->profile;
    ctx->rc_mode = p->rc_mode;
    ctx->qp = p->qp;
    ctx->gop_length = p->gop_length;

    for (int i = 0; i < wanted; ++i) {
        ctx->stream_in_hw[i] = 0;
        /* IMP_Alloc writes a DMABuffer into the 'name' pointer (0x94 bytes) */
        unsigned char info[0x94];
        memset(info, 0, sizeof(info));
        if (IMP_Alloc((char*)info, ctx->stream_buf_size, (char*)"avpu_stream") == 0) {
            void *virt = *(void**)(info + 0x80);
            uint32_t phys = *(uint32_t*)(info + 0x84);
            /* Validate allocation returned valid addresses */
            if (virt == NULL || phys == 0) {
                LOG_AL("IMP_Alloc succeeded but returned invalid addresses at idx=%d (virt=%p phys=0x%08x)", i, virt, phys);
                break;
            }
            ctx->stream_bufs[i].phy_addr = phys;
            ctx->stream_bufs[i].mmap_off = 0;
            ctx->stream_bufs[i].dmabuf_fd = -1;
            ctx->stream_bufs[i].map = virt;
            ctx->stream_bufs[i].size = ctx->stream_buf_size;
            ctx->stream_bufs[i].from_rmem = 1;
            /* Zero the buffer so trailing garbage doesn't confuse decoders */
            if (virt && ctx->stream_buf_size > 0)
                memset(virt, 0, (size_t)ctx->stream_buf_size);
            ++ctx->stream_bufs_used;
            LOG_AL("stream buf[%d]: RMEM phys=0x%08x size=%d virt=%p", i, phys, ctx->stream_buf_size, virt);
        } else {
            LOG_AL("IMP_Alloc failed at idx=%d", i);
            break;
        }
    }

    /* Addressing mode: T31 requires absolute addressing due to kernel bugs with offset mode */
    uint32_t axi_base = 0;
    int use_offsets = 0;

    /* T31 default: absolute addressing (offset mode causes kernel crashes) */
    axi_base = 0;
    use_offsets = 0;
    ctx->disable_axi_offset = 1;
    ctx->force_cl_abs = 0;

    LOG_AL("addr-mode: ABSOLUTE (T31 default - offset mode causes kernel crashes)");

    /* Persist addressing mode */
    ctx->axi_base = axi_base;
    ctx->use_offsets = use_offsets;

    /* Prepare command-list ring (OEM parity): allocate 0x13 entries of 512B */
    {
        const uint32_t CL_ENTRY_SIZE = 0x200; /* 512B */
        const uint32_t CL_COUNT = 0x13;       /* 19 entries */
        size_t cl_bytes = (size_t)CL_ENTRY_SIZE * (size_t)CL_COUNT;
        ctx->cl_ring.phy_addr = 0;
        ctx->cl_ring.map = NULL;
        ctx->cl_ring.size = 0;
        ctx->cl_ring.from_rmem = 0;
        unsigned char info[0x94];
        memset(info, 0, sizeof(info));
        if (IMP_Alloc((char*)info, (int)cl_bytes, (char*)"avpu_cmdlist") == 0) {
            void *virt = *(void**)(info + 0x80);
            uint32_t phys = *(uint32_t*)(info + 0x84);
            /* Validate allocation returned valid addresses */
            if (virt == NULL || phys == 0) {
                LOG_AL("IMP_Alloc succeeded but returned invalid addresses (virt=%p phys=0x%08x); CL disabled", virt, phys);
            } else {
                ctx->cl_ring.phy_addr = phys;
                ctx->cl_ring.map = virt;
                ctx->cl_ring.size = cl_bytes;
                ctx->cl_ring.from_rmem = 1;
                ctx->cl_entry_size = CL_ENTRY_SIZE;
                ctx->cl_count = CL_COUNT;
                ctx->cl_idx = 0;
                /* Zero the ring */
                if (virt && cl_bytes)
                    memset(virt, 0, cl_bytes);
                /* Write basic picture dimensions into first entry (Enc1 cmd regs at +4) */
                if (virt && ctx->enc_w && ctx->enc_h) {
                    uint32_t *cmd = (uint32_t*)virt;
                    uint32_t w = ctx->enc_w ? ctx->enc_w : eff_w;
                    uint32_t h = ctx->enc_h ? ctx->enc_h : eff_h;
                    if (w > 0) w -= 1; if (h > 0) h -= 1;
                    uint32_t dims = ((h & 0x7ff) << 12) | (w & 0x7ff);
                    cmd[1] = dims; /* matches GetPicDimFromCmdRegsEnc1 */
                }
                /* Log first entry start/end like SetCommandListBuffer would generate */
                uint32_t first_start = phys;
                uint32_t first_end   = phys + CL_ENTRY_SIZE;
                LOG_AL("cmdlist ring: phys=0x%08x size=%zu entries=%u entry_size=%u first:[0x%08x..0x%08x)",
                       phys, cl_bytes, CL_COUNT, CL_ENTRY_SIZE, first_start, first_end);
            }
        } else {
            LOG_AL("IMP_Alloc for cmdlist ring failed; proceeding without CL (will block start)");
        }
    }

    /* Prepare base hardware now so encoder is ready before ISP starts delivering frames
     * BN/OEM parity: program base regs but do NOT push CL/SRC yet. */
    ctx->session_ready = 0;
    ctx->hw_prepared = 0;
    do {
        /* Core reset FIRST - this clears all registers */
        unsigned reset_reg = AVPU_REG_CORE_RESET(0);
        (void)avpu_ip_write_reg(fd, reset_reg, 2);
        usleep(1000);
        (void)avpu_ip_write_reg(fd, reset_reg, 4);

        /* Program AXI address offset base AFTER reset (required even in absolute mode - write 0) */
        if (ctx->use_offsets && ctx->axi_base) {
            LOG_AL("program AXI_ADDR_OFFSET_IP(0x%04x) = 0x%08x", AVPU_REG_AXI_ADDR_OFFSET_IP, ctx->axi_base);
            (void)avpu_ip_write_reg(fd, AVPU_REG_AXI_ADDR_OFFSET_IP, ctx->axi_base);
        } else {
            /* In absolute mode, explicitly write 0 to AXI offset register */
            LOG_AL("program AXI_ADDR_OFFSET_IP(0x%04x) = 0x00000000 (absolute mode)", AVPU_REG_AXI_ADDR_OFFSET_IP);
            (void)avpu_ip_write_reg(fd, AVPU_REG_AXI_ADDR_OFFSET_IP, 0);
        }

        /* TOP_CTRL */
        (void)avpu_ip_write_reg(fd, AVPU_REG_TOP_CTRL, 0x00000080);

        /* Mask all IRQs and then clear any pending before enabling blocks */
        {
            unsigned pend = 0;
            (void)avpu_ip_write_reg(fd, AVPU_INTERRUPT_MASK, 0x00000000);
            (void)avpu_ip_read_reg(fd, AVPU_INTERRUPT, &pend);
            if (pend)
                (void)avpu_ip_write_reg(fd, AVPU_INTERRUPT, pend);
        }

        /* Defer unmask until CL/SRC are programmed to avoid early IRQs */
        /* avpu_enable_interrupts_core(fd, 0); */

        /* MISC_CTRL: only enable AXI offset addressing when offsets mode is active */
        if (ctx->use_offsets) {
            (void)avpu_ip_write_reg(fd, AVPU_REG_MISC_CTRL, 0x00001000);
        } else {
            LOG_AL("skip MISC_CTRL (offset mode) in ABSOLUTE mode");
        }

        /* Enable encoder blocks (ENC_EN_A/B/C) */
        (void)avpu_ip_write_reg(fd, AVPU_REG_ENC_EN_A, 0x00000001);
        (void)avpu_ip_write_reg(fd, AVPU_REG_ENC_EN_B, 0x00000001);
        (void)avpu_ip_write_reg(fd, AVPU_REG_ENC_EN_C, 0x00000001);

        /* Seed a single STRM buffer so hardware has a sink (do not burst-fill) */
        if (ctx->stream_bufs_used > 0) {
            uint32_t axi_base = ctx->axi_base;
            int use_offsets = ctx->use_offsets;
            uint32_t phys = ctx->stream_bufs[0].phy_addr;
            uint32_t val = use_offsets ? (phys - axi_base) : phys;
            if (avpu_ip_write_reg(fd, AVPU_REG_STRM_PUSH, val) == 0) {
                ctx->stream_in_hw[0] = 1;
                LOG_AL("seed STRM buf[0] -> reg 0x%04x = 0x%08x%s", AVPU_REG_STRM_PUSH, val, use_offsets?" (off)":"");
            }
        }
        ctx->hw_prepared = 1;
        LOG_AL("base HW prepared (no CL/SRC yet)");
    } while (0);

    LOG_AL("opened avpu ctx=%p fd(local)=%d fd(ctx)=%d, target %ux%u @ %u/%u fps, gop=%d, bitrate=%u, profile=%d",
           (void*)ctx, fd, ctx->fd, eff_w, eff_h, eff_fps_num, eff_fps_den, p->gop_length, p->bitrate, p->profile);

    /* initialize buffer pools / FIFOs placeholders */
    ctx->fifo_streams = NULL;
    ctx->fifo_tags = NULL;

    /* no eventfd yet (owned by upper layer) */
    ctx->event_fd = -1;

    return 0;
}

/**
 * ALAvpu_Deinit - Clean up AVPU context resources
 * Does NOT close the device fd (caller manages via AL_DevicePool_Close)
 * (OEM parity: no ALAvpu_Close, cleanup done in encoder layer)
 */
int ALAvpu_Deinit(ALAvpuContext *ctx)
{
    if (!ctx) return -1;

    /* OEM parity: no userspace IRQ thread or sync primitives to tear down */
    ctx->irq_thread_running = 0;
    ctx->irq_thread = 0;
    ctx->irq_cond = NULL;
    ctx->irq_mutex = NULL;

    /* Clean up stream buffers */
    for (int i = 0; i < ctx->stream_bufs_used; ++i) {
        if (ctx->stream_bufs[i].map && !ctx->stream_bufs[i].from_rmem) {
            munmap(ctx->stream_bufs[i].map, ctx->stream_bufs[i].size);
            ctx->stream_bufs[i].map = NULL;
        }
        if (ctx->stream_bufs[i].dmabuf_fd > 0) {
            close(ctx->stream_bufs[i].dmabuf_fd);
            ctx->stream_bufs[i].dmabuf_fd = -1;
        }
    }

    /* Do NOT close ctx->fd - caller manages via AL_DevicePool_Close */
    /* Do not close event_fd: owned by caller (codec layer) */
    ctx->fifo_streams = NULL;
    ctx->fifo_tags = NULL;
    return 0;
}

int ALAvpu_QueueFrame(ALAvpuContext *ctx, const HWFrameBuffer *frame)
{
    if (!ctx || ctx->fd < 0 || !frame) return -1;
    if (ctx->fd <= 2) {
        LOG_AL("FD invalid for AVPU (ctx=%p fd=%d): refusing to issue ioctls", (void*)ctx, ctx->fd);
        errno = EBADF;
        return -1;
    }
    /* Gate AVPU lazy-start until ISP global stream is active to avoid races */
    extern int ISP_IsStreaming(void);
    if (!ctx->session_ready && (ISP_IsStreaming != NULL)) {
        if (!ISP_IsStreaming()) {
            errno = EAGAIN;
            return -1;
        }
    }

    if (!ctx->session_ready) {
        int fd = ctx->fd;
        /* Lazy-start: configure HW and start IRQ thread on first frame to avoid early IRQs */
        /* OEM parity: no userspace IRQ thread or sync primitives */
        ctx->irq_q_head = ctx->irq_q_tail = 0;
        ctx->irq_mutex = NULL;
        ctx->irq_cond  = NULL;
        ctx->irq_thread_running = 0;

        /* If base HW was prepared in ALAvpu_Open, skip base config here */
        if (!ctx->hw_prepared) {
            /* Core reset FIRST - this clears all registers */
            unsigned reset_reg = AVPU_REG_CORE_RESET(0);
            (void)avpu_ip_write_reg(fd, reset_reg, 2);
            usleep(1000);
            (void)avpu_ip_write_reg(fd, reset_reg, 4);

            /* Program AXI address offset base AFTER reset (required even in absolute mode - write 0) */
            if (ctx->use_offsets && ctx->axi_base) {
                LOG_AL("program AXI_ADDR_OFFSET_IP(0x%04x) = 0x%08x", AVPU_REG_AXI_ADDR_OFFSET_IP, ctx->axi_base);
                (void)avpu_ip_write_reg(fd, AVPU_REG_AXI_ADDR_OFFSET_IP, ctx->axi_base);
            } else {
                /* In absolute mode, explicitly write 0 to AXI offset register */
                LOG_AL("program AXI_ADDR_OFFSET_IP(0x%04x) = 0x00000000 (absolute mode)", AVPU_REG_AXI_ADDR_OFFSET_IP);
                (void)avpu_ip_write_reg(fd, AVPU_REG_AXI_ADDR_OFFSET_IP, 0);
            }

            /* TOP_CTRL */
            (void)avpu_ip_write_reg(fd, AVPU_REG_TOP_CTRL, 0x00000080);
            /* Defer unmask until CL/SRC are programmed to avoid early IRQs */
            /* avpu_enable_interrupts_core(fd, 0); */

            /* MISC_CTRL: only enable AXI offset addressing when offsets mode is active */
            if (ctx->use_offsets) {
                (void)avpu_ip_write_reg(fd, AVPU_REG_MISC_CTRL, 0x00001000);
            } else {
                LOG_AL("skip MISC_CTRL (offset mode) in ABSOLUTE mode");
            }
            /* Enable encoder blocks (ENC_EN_A/C) */
            (void)avpu_ip_write_reg(fd, AVPU_REG_ENC_EN_A, 0x00000001);
            (void)avpu_ip_write_reg(fd, AVPU_REG_ENC_EN_C, 0x00000001);
        }

        /* Defer providing stream buffers until first CL/SRC push to match BN/OEM sequencing */
        /* OEM parity: do not touch IRQ pending from userspace */

        ctx->session_ready = 1;
        LOG_AL("lazy-start: AVPU HW configured; session_ready=1");

    }

    /* For T31 NV12 from ISP, phys_addr points to a VBM header followed by Y/UV planes. */
    /* We don't alter the frame in CPU; just push source to hardware. */


    /* Prepare and program command-list entry for this picture (OEM parity path) */
    if (ctx->cl_ring.phy_addr && ctx->cl_ring.map && ctx->cl_entry_size) {
        uint32_t idx = ctx->cl_idx % ctx->cl_count;
        uint8_t* entry = (uint8_t*)ctx->cl_ring.map + (size_t)idx * ctx->cl_entry_size;
        memset(entry, 0, ctx->cl_entry_size);
        uint32_t* cmd = (uint32_t*)entry;

        /* Fill Enc1 command registers minimally based on OEM decompile */
        fill_cmd_regs_from_oem_enc1(ctx, cmd);

        /* Optional debug: dump first 8 dwords BEFORE any cache op */
        LOG_AL("CL[%u] pre-clean cmd[0..7]=%08x %08x %08x %08x %08x %08x %08x %08x",
               idx, cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6], cmd[7]);

        /* OEM parity: CL in RMEM appears coherent for device; skip cache clean here
           (our JZ_CMD_FLUSH_CACHE on this VA zeroed the entry contents in practice). */
        LOG_AL("CL[%u] post-no-clean cmd[0..7]=%08x %08x %08x %08x %08x %08x %08x %08x",
               idx, cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6], cmd[7]);

        /* BN MCP: SetClockCommand read-modify-write on 0x83F4 (core0), XOR low bits with mask 0x3 */
        {
            unsigned clk = 0;
            (void)avpu_ip_read_reg(ctx->fd, AVPU_REG_CORE_CLKCMD(0), &clk);
            unsigned new_clk = clk ^ 0x3u; /* (($v0 ^ mask) & 3) ^ v0 == v0 ^ (mask & 3) */
            (void)avpu_ip_write_reg(ctx->fd, AVPU_REG_CORE_CLKCMD(0), new_clk);
            LOG_AL("clockcmd: 0x%04x: 0x%08x -> 0x%08x", AVPU_REG_CORE_CLKCMD(0), clk, new_clk);
        }

        /* Program CL start and push (BN/OEM path: 0x83E0/0x83E4) */
        uint32_t axi_base = ctx->axi_base;
        int use_offsets = ctx->use_offsets;
        uint32_t cl_phys = ctx->cl_ring.phy_addr + (idx * ctx->cl_entry_size);
        uint32_t cl_val = use_offsets ? (cl_phys - axi_base) : cl_phys;
        (void)avpu_ip_write_reg(ctx->fd, AVPU_REG_CL_ADDR, cl_val);
        (void)avpu_ip_write_reg(ctx->fd, AVPU_REG_CL_PUSH, 0x00000002);
        LOG_AL("queue: CL start %s=0x%08x (phys=0x%08x) -> [0x%04x], push [0x%04x]=0x2", use_offsets?"off":"abs", cl_val, cl_phys, AVPU_REG_CL_ADDR, AVPU_REG_CL_PUSH);

        /* After programming CL, push the source address for this frame (OEM path pushes SRC around CL commit). Skip cache clean to avoid kernel faults on T31 RMEM VA. */
        {
            uint32_t axi_base2 = ctx->axi_base;
            int use_offsets2 = ctx->use_offsets;
            uint32_t phys2 = frame->phys_addr;
            uint32_t val2 = use_offsets2 ? (phys2 - axi_base2) : phys2;
            (void)avpu_ip_write_reg(ctx->fd, AVPU_REG_SRC_PUSH, val2);
            LOG_AL("push SRC frame %s=0x%08x -> reg 0x%04x", use_offsets2?"off":"phys", val2, AVPU_REG_SRC_PUSH);
        }

        /* Now that CL/SRC are set, unmask interrupts to start receiving IRQs */
        avpu_enable_interrupts_core(ctx->fd, 0);

        /* Ensure at least one STRM buffer is queued before first encode */
        {
            int any_in_hw = 0;
            for (int i = 0; i < ctx->stream_bufs_used; ++i) {
                if (ctx->stream_in_hw[i]) { any_in_hw = 1; break; }
            }
            if (!any_in_hw && ctx->stream_bufs_used > 0) {
                int i = 0; /* push the first buffer */
                uint32_t axi_base3 = ctx->axi_base;
                int use_offsets3 = ctx->use_offsets;
                uint32_t phys3 = ctx->stream_bufs[i].phy_addr;
                uint32_t val3 = use_offsets3 ? (phys3 - axi_base3) : phys3;
                if (avpu_ip_write_reg(ctx->fd, AVPU_REG_STRM_PUSH, val3) == 0) {
                    ctx->stream_in_hw[i] = 1;
                    LOG_AL("seed STRM buf[%d] -> reg 0x%04x = 0x%08x%s", i, AVPU_REG_STRM_PUSH, val3, use_offsets3?" (off)":"");
                }
            }
        }

        /* Debug: read back key control registers around SRC/STRM and IRQ */
        #if AVPU_DEBUG_REGS
        {
            unsigned v;
            (void)avpu_ip_read_reg(ctx->fd, AVPU_REG_TOP_CTRL, &v);
            LOG_AL("dbg TOP_CTRL(0x%04x)=0x%08x", AVPU_REG_TOP_CTRL, v);
            (void)avpu_ip_read_reg(ctx->fd, AVPU_REG_SRC_CTRL, &v);
            LOG_AL("dbg SRC_CTRL(0x%04x)=0x%08x", AVPU_REG_SRC_CTRL, v);
            (void)avpu_ip_read_reg(ctx->fd, AVPU_REG_SRC_PUSH, &v);
            LOG_AL("dbg SRC_PUSH(0x%04x)=0x%08x", AVPU_REG_SRC_PUSH, v);
            (void)avpu_ip_read_reg(ctx->fd, AVPU_REG_STRM_CTRL, &v);
            LOG_AL("dbg STRM_CTRL(0x%04x)=0x%08x", AVPU_REG_STRM_CTRL, v);
            (void)avpu_ip_read_reg(ctx->fd, AVPU_REG_STRM_PUSH, &v);
            LOG_AL("dbg STRM_PUSH(0x%04x)=0x%08x", AVPU_REG_STRM_PUSH, v);
            (void)avpu_ip_read_reg(ctx->fd, AVPU_INTERRUPT_MASK, &v);
            LOG_AL("dbg IRQ_MASK(0x%04x)=0x%08x", AVPU_INTERRUPT_MASK, v);
            (void)avpu_ip_read_reg(ctx->fd, AVPU_INTERRUPT, &v);
            LOG_AL("dbg IRQ_PEND(0x%04x)=0x%08x", AVPU_INTERRUPT, v);
        }
    #endif


        /* OEM parity: do not read IRQ pending here from userspace */

        /* Advance to next CL slot */
        ctx->cl_idx = (idx + 1) % ctx->cl_count;
    }

    return 0;
}

int ALAvpu_DequeueStream(ALAvpuContext *ctx, HWStreamBuffer *out, int timeout_ms)
{
    if (!ctx || ctx->fd < 0 || !out) return -1;

    if (!ctx->session_ready) { errno = EAGAIN; return -1; }

    /* OEM parity: no userspace IRQ wait; perform a short bounded sleep before checking */
    int irq = 0;
    if (timeout_ms > 0) {
        int waited = 0;
        const int step_ms = 5;
        struct timespec nap = {0, step_ms * 1000000L};
        while (waited < timeout_ms) {
            nanosleep(&nap, NULL);
            waited += step_ms;
        }
    }

    /* For now, pick the first queued stream buffer and compute effective AnnexB size. */
    for (int i = 0; i < ctx->stream_bufs_used; ++i) {
        if (ctx->stream_in_hw[i]) {
            out->phys_addr = ctx->stream_bufs[i].phy_addr;
            out->virt_addr = (uint32_t)(uintptr_t)ctx->stream_bufs[i].map;
            /* Skip CPU cache invalidation on T31: RMEM mapping is device-coherent enough and driver JZ_CMD_FLUSH_CACHE on a user VA causes kernel faults. */
            size_t eff = annexb_effective_size((const uint8_t*)(uintptr_t)out->virt_addr, (size_t)ctx->stream_buf_size);
            if (eff == 0) {
                /* No AnnexB header detected; requeue and wait for next IRQ */
                LOG_AL("no AnnexB startcode in STRM buf[%d]; requeue", i);
                {
                    uint32_t axi_base = ctx->axi_base;
                    int use_offsets = ctx->use_offsets;
                    uint32_t phys = out->phys_addr;
                    uint32_t val = use_offsets ? (phys - axi_base) : phys;
                    if (avpu_ip_write_reg(ctx->fd, AVPU_REG_STRM_PUSH, val) == 0) {
                        ctx->stream_in_hw[i] = 1;
                    }
                }
                errno = EAGAIN;
                return -1;
            }
            out->length = (uint32_t)eff;
            out->timestamp = 0; /* TODO */
            out->frame_type = 0; /* TODO */
            out->slice_type = 0; /* TODO */
            ctx->stream_in_hw[i] = 0; /* mark as dequeued */
            LOG_AL("dequeue STRM buf[%d] (irq=%d) phys=0x%08x len=%u (eff)", i, irq, out->phys_addr, out->length);
            return 0;
        }
    }

    errno = EAGAIN;
    return -1;
}

int ALAvpu_ReleaseStream(ALAvpuContext *ctx, HWStreamBuffer *out)
{
    if (!ctx || ctx->fd < 0 || !out) return -1;

    if (!ctx->session_ready) { return 0; }

    /* Return the buffer to hardware: push back to STRM ring */
    for (int i = 0; i < ctx->stream_bufs_used; ++i) {
        if (ctx->stream_bufs[i].phy_addr == out->phys_addr) {
            {
                uint32_t axi_base = ctx->axi_base;
                int use_offsets = ctx->use_offsets;
                uint32_t phys = out->phys_addr;
                uint32_t val = use_offsets ? (phys - axi_base) : phys;
                if (avpu_ip_write_reg(ctx->fd, AVPU_REG_STRM_PUSH, val) < 0) {
                    LOG_AL("STRM push failed (release): reg 0x%04x <- 0x%08x (phys=0x%08x)%s : %s", AVPU_REG_STRM_PUSH, val, phys, use_offsets?" off":"", strerror(errno));
                    return -1;
                }
                ctx->stream_in_hw[i] = 1;
                LOG_AL("release STRM buf[%d] -> reg 0x%04x = 0x%08x%s", i, AVPU_REG_STRM_PUSH, val, use_offsets?" (off)":"");
            }
            return 0;
        }
    }
    errno = EINVAL;
    return -1;
}



int ALAvpu_SetEvent(ALAvpuContext *ctx, int event_fd)
{
    if (!ctx) return -1;
    ctx->event_fd = event_fd;
    return 0;
}


