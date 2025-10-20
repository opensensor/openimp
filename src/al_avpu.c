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

static void avpu_cache_inv(int fd, uint32_t phys, uint32_t len)
{
    struct flush_cache_info info = { .addr = phys, .len = len, .dir = DMA_FROM_DEVICE };
    (void)ioctl(fd, JZ_CMD_FLUSH_CACHE, (unsigned long)&info);
}

static void avpu_cache_clean(int fd, uint32_t phys, uint32_t len)
{
    struct flush_cache_info info = { .addr = phys, .len = len, .dir = DMA_TO_DEVICE };
    (void)ioctl(fd, JZ_CMD_FLUSH_CACHE, (unsigned long)&info);
}

/* Virtual-address cache ops: driver expects a CPU-mapped pointer in addr */
static void avpu_cache_inv_virt(int fd, void* addr, uint32_t len)
{
    if (!addr || !len) return;
    struct flush_cache_info info = { .addr = (unsigned int)(uintptr_t)addr, .len = len, .dir = DMA_FROM_DEVICE };
    (void)ioctl(fd, JZ_CMD_FLUSH_CACHE, (unsigned long)&info);
}

static void avpu_cache_clean_virt(int fd, void* addr, uint32_t len)
{
    if (!addr || !len) return;
    struct flush_cache_info info = { .addr = (unsigned int)(uintptr_t)addr, .len = len, .dir = DMA_TO_DEVICE };
    (void)ioctl(fd, JZ_CMD_FLUSH_CACHE, (unsigned long)&info);
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



/* Minimal Enc1 command-list entry fill (strict OEM parity for known fields) */
static void al_avpu_fill_cmd_entry_enc1(ALAvpuContext* ctx, uint32_t* cmd)
{
    if (!ctx || !cmd) return;
    /* Zeroed by caller; fill only fields we can justify from BN HLIL */
    /* cmd[0]: base flags 0x11 per SliceParamToCmdRegsEnc1 (lines ~71663) */
    cmd[0] = 0x11;
    /* cmd[1]: dimensions ((h-1)<<12 | (w-1)) per GetPicDimFromCmdRegsEnc1 and Enc1 mapping */
    if (ctx->enc_w && ctx->enc_h) {
        uint32_t w1 = (ctx->enc_w > 0) ? (ctx->enc_w - 1) : 0;
        uint32_t h1 = (ctx->enc_h > 0) ? (ctx->enc_h - 1) : 0;
        cmd[1] = ((h1 & 0x7ff) << 12) | (w1 & 0x7ff);
    }
    /* cmd[2]: keep OEM-observed constant; rest of baseline fields belong to cmd[0] */
    cmd[2] |= 0x2000;

    /* Baseline/NV12 per HLIL packing (cmd[0]):
       - bits [11:10] come from (arg1[1]-2)&3; set 0 for Baseline (no CABAC/8x8)
       - bits [9:8] come from (arg1[0]-2)&3; set 1 for NV12 4:2:0
       - bits [22:20] from (arg1[2]-4)&7; set 0 by choosing 4 */
    cmd[0] &= ~(3u << 10);
    cmd[0] = (cmd[0] & ~(3u << 8)) | (1u << 8);
    cmd[0] &= ~(7u << 20);
    /* HLIL sets cmd[0] bit 31 from pSP[8]; for a single-slice full picture treat as entry valid */
    cmd[0] |= (1u << 31);

    /* Minimal slice/NAL type in cmd[3] low 5 bits (HLIL packs arg1[0x26] & 0x1f)
       Use H.264 NAL unit types: 5 = IDR, 1 = non-IDR slice.
       Set IDR on the very first CL entry of the session; P-slice otherwise. */
    {
        uint32_t nalu = (ctx->cl_idx == 0) ? 5u : 1u;
        cmd[3] = (cmd[3] & ~0x1Fu) | (nalu & 0x1Fu);
        /* Single-slice, full picture: set high flags per HLIL packing (bits 31:30).
           These correspond to pSP fields [0x35] and [0x34] respectively in OEM. */
        cmd[3] |= (1u << 31) | (1u << 30);
    }

    /* cmd[4] low 5 bits: QP field exists regardless of RC; use ctx->qp or conservative default (26) */
    {
        uint32_t q = ctx->qp ? (ctx->qp & 0x1f) : (26u & 0x1f);
        cmd[4] = (cmd[4] & ~0x1Fu) | q;
    }

    /* cmd[7]: macroblock grid (width/height in MB, minus 1) per HLIL (line ~71722)
       Pack: low 10 bits = (mb_w - 1), bits[21:12] = (mb_h - 1) */
    if (ctx->enc_w && ctx->enc_h) {
        uint32_t mb_w = (ctx->enc_w + 15) / 16;
        uint32_t mb_h = (ctx->enc_h + 15) / 16;
        uint32_t mw1 = mb_w ? (mb_w - 1) : 0;
        uint32_t mh1 = mb_h ? (mb_h - 1) : 0;
        cmd[7] = ((mh1 & 0x3ff) << 12) | (mw1 & 0x3ff);

        /* cmd[5] and cmd[6]: single-slice default derived from MB grid.
           HLIL shows these fields encode quotient/remainder of slice spans vs LCU width.
           For a single slice spanning the whole frame, set remainder=0 and quotient=1. */
        cmd[5] = ((1u & 0x3ff) << 12) | (0u & 0x3ff);
        cmd[6] = ((1u & 0x3ff) << 12) | (0u & 0x3ff);
    }
}


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


static int avpu_minimal_configure(int fd, int core)
{

    /* Read ID/version regs FIRST (OEM does this before writing to MISC_CTRL)
       Note: Avoid reading 0x800C and 0x8008 on T31 kernel 3.10; observed to trigger
       an unaligned access oops in older kernels. */
    /* Skip probe reads on T31 to avoid any early readbacks; writes-only bring-up. */

    /* Program misc/AXI control AFTER probe reads (OEM does NOT write to INTERRUPT_MASK here - doing so causes kernel crash) */
    (void)avpu_ip_write_reg(fd, AVPU_REG_MISC_CTRL, 0x00001000);

    /* Reset core: write 1, then 2, then 4 to CORE_RESET (OEM does this BEFORE clearing interrupts) */
    unsigned reset_reg = AVPU_REG_CORE_RESET(core);
    if (avpu_ip_write_reg(fd, reset_reg, 1) < 0) {
        LOG_AL("CORE_RESET(1) failed (reg=0x%04x): %s", reset_reg, strerror(errno));
        return -1;
    }
    usleep(1000); /* 1ms delay between reset steps */
    if (avpu_ip_write_reg(fd, reset_reg, 2) < 0) {
        LOG_AL("CORE_RESET(2) failed (reg=0x%04x): %s", reset_reg, strerror(errno));
        return -1;
    }
    usleep(1000); /* 1ms delay before final reset step */
    if (avpu_ip_write_reg(fd, reset_reg, 4) < 0) {
        LOG_AL("CORE_RESET(4) failed (reg=0x%04x): %s", reset_reg, strerror(errno));
        return -1;
    }
    /* Clear pending IRQs (OEM uses 0x00FFFFFF) then set TOP_CTRL=0x80 */
    (void)avpu_ip_write_reg(fd, AVPU_INTERRUPT, 0x00FFFFFF);
    (void)avpu_ip_write_reg(fd, AVPU_REG_TOP_CTRL, 0x80);


    /* Turn clock command to ON (SetClockCommand -> set bits[1:0]=1 at CORE_CLKCMD) */
    unsigned clk_reg = AVPU_REG_CORE_CLKCMD(core);
    /* Set clock command ON directly (bits[1:0]=1); avoid readback. */
    unsigned new_v = 0x1u;
    if (avpu_ip_write_reg(fd, clk_reg, new_v) < 0) {
        LOG_AL("CORE_CLKCMD write failed (reg=0x%04x val=0x%08x)", clk_reg, new_v);
        return -1;
    }

    /* Ungate/enable sub-blocks observed in OEM */
    (void)avpu_ip_write_reg(fd, AVPU_REG_ENC_EN_B, 1);
    (void)avpu_ip_write_reg(fd, AVPU_REG_ENC_EN_A, 1);
    (void)avpu_ip_write_reg(fd, AVPU_REG_ENC_EN_C, 1);

    /* OEM keeps TOP_CTRL at 0x80; no tentative bit0 kick here. */
    unsigned top_prev = 0x80u; /* assume vendor default; skip readback */
    LOG_AL("configured core=%d: TOP_CTRL(default)=0x%08x, RESET(1->2->4), CLKCMD=0x%08x", core, top_prev, new_v);
    return 0;
}

static int avpu_ip_write_reg(int fd, unsigned int off, unsigned int val)
{
    if ((off & 3) != 0) { LOG_AL("WARN: unaligned reg write off=0x%04x", off); return -1; }
    struct avpu_reg r; r.id = off; r.value = val;
    int ret = ioctl(fd, AL_CMD_IP_WRITE_REG, &r);
    if (ret < 0) { LOG_AL("ioctl WRITE failed off=0x%04x err=%d", off, ret); return -1; }
    LOG_AL("WRITE[0x%04x] <- 0x%08x", off, val);
    return 0;
}

static int avpu_ip_read_reg(int fd, unsigned int off, unsigned int *out)
{
    if ((off & 3) != 0) { LOG_AL("WARN: unaligned reg read off=0x%04x", off); return -1; }
    struct avpu_reg r; r.id = off; r.value = 0;
    int ret = ioctl(fd, AL_CMD_IP_READ_REG, &r);
    if (ret < 0) { LOG_AL("ioctl READ failed off=0x%04x err=%d", off, ret); return -1; }
    if (out) *out = r.value;
    return 0;
}

static int avpu_wait_irq(int fd, int *irq)
{
    int v = 0;
    if (ioctl(fd, AL_CMD_IP_WAIT_IRQ, &v) < 0) return -1;
    if (irq) *irq = v;
    return 0;
}


/* Enable Enc1/Enc2 interrupt mask bits for core 0 (per OEM: 0x8014, bits (core<<2) and (core<<2)+2) */
static void avpu_enable_interrupts_core(int fd, int core)
{
    unsigned mask = 0;
    (void)avpu_ip_read_reg(fd, AVPU_INTERRUPT_MASK, &mask);
    unsigned b_enc1 = 1u << ((core << 2) & 31);
    unsigned b_enc2 = 1u << (((core << 2) + 2) & 31);
    unsigned new_mask = mask | b_enc1 | b_enc2;
    if (new_mask != mask) {
        (void)avpu_ip_write_reg(fd, AVPU_INTERRUPT_MASK, new_mask);
    }
    unsigned pend = 0; (void)avpu_ip_read_reg(fd, AVPU_INTERRUPT, &pend);
    LOG_AL("IRQ: enable core=%d bits enc1=0x%08x enc2=0x%08x -> mask=0x%08x, pending=0x%08x", core, b_enc1, b_enc2, new_mask, pend);
}

/* Temporarily unmask all interrupts for bring-up */
static void avpu_enable_interrupts_all(int fd)
{
    (void)avpu_ip_write_reg(fd, AVPU_INTERRUPT_MASK, 0xFFFFFFFFu);
    unsigned pend = 0, mask = 0;
    (void)avpu_ip_read_reg(fd, AVPU_INTERRUPT_MASK, &mask);
    (void)avpu_ip_read_reg(fd, AVPU_INTERRUPT, &pend);
    LOG_AL("IRQ: enable ALL mask=0x%08x, pending=0x%08x", mask, pend);
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

static void irq_queue_push(ALAvpuContext* ctx, int irq)
{
    int next = (ctx->irq_q_head + 1) % (int)(sizeof(ctx->irq_queue)/sizeof(ctx->irq_queue[0]));
    if (next == ctx->irq_q_tail) {
        /* drop oldest */
        ctx->irq_q_tail = (ctx->irq_q_tail + 1) % (int)(sizeof(ctx->irq_queue)/sizeof(ctx->irq_queue[0]));
    }
    ctx->irq_queue[ctx->irq_q_head] = irq;
    ctx->irq_q_head = next;
}

static int irq_queue_pop(ALAvpuContext* ctx, int* out_irq)
{
    if (ctx->irq_q_head == ctx->irq_q_tail) return 0;
    int irq = ctx->irq_queue[ctx->irq_q_tail];
    ctx->irq_q_tail = (ctx->irq_q_tail + 1) % (int)(sizeof(ctx->irq_queue)/sizeof(ctx->irq_queue[0]));

    if (out_irq) *out_irq = irq;
    return 1;
}

static void* irq_thread_main(void* arg)
{
    ALAvpuContext* ctx = (ALAvpuContext*)arg;
    pthread_mutex_t* m = (pthread_mutex_t*)ctx->irq_mutex;
    pthread_cond_t*  c = (pthread_cond_t*)ctx->irq_cond;
    if (!m || !c) {
        LOG_AL("irq thread: missing sync primitives (m=%p c=%p)", (void*)m, (void*)c);
        return NULL;
    }
    while (__atomic_load_n(&ctx->irq_thread_running, __ATOMIC_SEQ_CST)) {
        int irq = -1;
        if (avpu_wait_irq(ctx->fd, &irq) < 0) {
            if (!__atomic_load_n(&ctx->irq_thread_running, __ATOMIC_SEQ_CST)) break;
            /* spurious or error; short nap */
            struct timespec ts = {0, 10*1000*1000};
            nanosleep(&ts, NULL);
            continue;
        }
        LOG_AL("irq thread: got irq=%d", irq);
        pthread_mutex_lock(m);
        irq_queue_push(ctx, irq);
        pthread_cond_broadcast(c);
        pthread_mutex_unlock(m);
        /* Signal optional eventfd for OEM parity */
        if (ctx->event_fd >= 0) {
            uint64_t one = 1;
            (void)write(ctx->event_fd, &one, sizeof(one));
        }
    }
    return NULL;
}

int ALAvpu_Open(ALAvpuContext *ctx, const HWEncoderParams *p)
{
    if (!ctx || !p) return -1;
    memset(ctx, 0, sizeof(*ctx));

    int fd = open("/dev/avpu", O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        LOG_AL("open(/dev/avpu) failed: %s", strerror(errno));
        return -1;
    }

    /* Allow clocks to stabilize after device open (kernel enables clocks in bind_channel) */
    usleep(10000); /* 10ms delay */

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

    /* For now, enable session immediately so pushes/IRQ can proceed */
    /* OEM parity: do NOT use AXI offset mode at init; use absolute addresses */
    uint32_t axi_base = 0;
    int use_offsets = 0;
    ctx->force_cl_abs = 0;
    ctx->disable_axi_offset = 1;

    /* Persist addressing mode */
    ctx->axi_base = 0;
    ctx->use_offsets = 0;

    LOG_AL("addr-mode: use_offsets=%d axi_base=0x%08x (OEM ABS)", ctx->use_offsets, ctx->axi_base);

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

    /* Defer hardware configure/unmask to first frame queue to avoid early IRQs on T31.
     * We only prepare software state here; IRQ thread and register programming happen lazily. */
    ctx->session_ready = 0;
    LOG_AL("deferring AVPU HW configure/unmask until first frame is queued");

    LOG_AL("opened avpu ctx=%p fd(local)=%d fd(ctx)=%d, target %ux%u @ %u/%u fps, gop=%d, bitrate=%u, profile=%d",
           (void*)ctx, fd, ctx->fd, eff_w, eff_h, eff_fps_num, eff_fps_den, p->gop_length, p->bitrate, p->profile);

    /* initialize buffer pools / FIFOs placeholders */
    ctx->fifo_streams = NULL;
    ctx->fifo_tags = NULL;

    /* no eventfd yet (owned by upper layer) */
    ctx->event_fd = -1;

    return 0;
}

int ALAvpu_Close(ALAvpuContext *ctx)
{
    if (!ctx) return -1;

    /* stop IRQ thread first */
    if (ctx->irq_thread_running) {
        __atomic_store_n(&ctx->irq_thread_running, 0, __ATOMIC_SEQ_CST);
        /* poke the waiter to exit */
        if (ctx->fd >= 0) (void)ioctl(ctx->fd, AL_CMD_UNBLOCK_CHANNEL, 0);
        if (ctx->irq_thread) {
            pthread_t th = (pthread_t)ctx->irq_thread;
            pthread_join(th, NULL);
            ctx->irq_thread = 0;
        }
    }

    /* teardown sync primitives */
    if (ctx->irq_cond) {
        pthread_cond_destroy((pthread_cond_t*)ctx->irq_cond);
        free(ctx->irq_cond);
        ctx->irq_cond = NULL;
    }
    if (ctx->irq_mutex) {
        pthread_mutex_destroy((pthread_mutex_t*)ctx->irq_mutex);
        free(ctx->irq_mutex);
        ctx->irq_mutex = NULL;
    }

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
    if (ctx->fd >= 0) {
        close(ctx->fd);
        ctx->fd = -1;
    }
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

    if (!ctx->session_ready) {
        int fd = ctx->fd;
        /* Lazy-start: configure HW and start IRQ thread on first frame to avoid early IRQs */
        ctx->irq_q_head = ctx->irq_q_tail = 0;
        pthread_mutex_t* m = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
        pthread_cond_t*  c = (pthread_cond_t*)malloc(sizeof(pthread_cond_t));
        if (!m || !c) {
            LOG_AL("lazy-start: failed to alloc irq sync primitives");
            if (m) free(m);
            if (c) free(c);
            errno = ENOMEM;
            return -1;
        }
        pthread_mutex_init(m, NULL);
        pthread_cond_init(c, NULL);
        ctx->irq_mutex = m;
        ctx->irq_cond  = c;
        ctx->irq_thread_running = 0; /* do not start IRQ thread yet */

        /* Minimal OEM-aligned configure/start sequence (core 0) */
        (void)avpu_minimal_configure(fd, 0);

        /* Do NOT unmask IRQs yet on T31; defer until stable. */

        /* Program command-list start and push (OEM sequence uses 0x83E0/0x83E4) */
        if (ctx->cl_ring.phy_addr != 0 && ctx->cl_entry_size) {
            uint32_t cl_start_abs = ctx->cl_ring.phy_addr + (ctx->cl_idx * ctx->cl_entry_size);
            (void)avpu_ip_write_reg(fd, AVPU_REG_CL_ADDR, cl_start_abs);
            (void)avpu_ip_write_reg(fd, AVPU_REG_CL_PUSH, 0x00000002);
        }

        /* Provide stream buffers to hardware */
        {
            uint32_t axi_base = ctx->axi_base;
            int use_offsets = ctx->use_offsets;
            for (int i = 0; i < ctx->stream_bufs_used; ++i) {
                uint32_t phys = ctx->stream_bufs[i].phy_addr;
                uint32_t val = use_offsets ? (phys - axi_base) : phys;
                if (avpu_ip_write_reg(fd, AVPU_REG_STRM_PUSH, val) == 0) {
                    ctx->stream_in_hw[i] = 1;
                }
            }
        }

        /* Clear any pending bits; keep IRQs masked for now */
        (void)avpu_ip_write_reg(fd, AVPU_INTERRUPT, 0x00FFFFFF);

        ctx->session_ready = 1;
        LOG_AL("lazy-start: AVPU HW configured (IRQs masked); session_ready=1");
    }

    /* For T31 NV12 from ISP, phys_addr points to a VBM header followed by Y/UV planes. */
    /* We don't alter the frame in CPU; just push source to hardware. */

    /* Flush cache for source before device reads (OEM does 1MB); use virtual addr */
    if (frame->virt_addr) {
        avpu_cache_clean_virt(ctx->fd, (void*)(uintptr_t)frame->virt_addr, 0x100000);
    } else {
        LOG_AL("WARN: frame->virt_addr=0; skipping cache clean to avoid kernel fault");
    }

    /* Push the source descriptor/addr to the device. Vendor does this via driver vtable +0x0C. */
    /* If AXI base is set from RMEM, provide offset rather than absolute phys. */
    uint32_t axi_base = ctx->axi_base;
    int use_offsets = ctx->use_offsets;
    uint32_t phys = frame->phys_addr;
    uint32_t val = use_offsets ? (phys - axi_base) : phys;
    LOG_AL("ctx=%p fd=%d: SRC push prepare val=0x%08x (phys=0x%08x)%s", (void*)ctx, ctx->fd, val, phys, use_offsets?" off":"");
    if (avpu_ip_write_reg(ctx->fd, AVPU_REG_SRC_PUSH, val) < 0) {
        LOG_AL("SRC push failed: reg 0x%04x <- 0x%08x (phys=0x%08x)%s : %s", AVPU_REG_SRC_PUSH, val, phys, use_offsets?" off":"", strerror(errno));
        return -1;
    }
    LOG_AL("push SRC frame %s=0x%08x -> reg 0x%04x", use_offsets?"off":"phys", val, AVPU_REG_SRC_PUSH);

    /* Prepare and program command-list entry for this picture (OEM parity path) */
    if (ctx->cl_ring.phy_addr && ctx->cl_ring.map && ctx->cl_entry_size) {
        uint32_t idx = ctx->cl_idx % ctx->cl_count;
        uint8_t* entry = (uint8_t*)ctx->cl_ring.map + (size_t)idx * ctx->cl_entry_size;
        memset(entry, 0, ctx->cl_entry_size);
        uint32_t* cmd = (uint32_t*)entry;

        /* Fill Enc1 command-list entry with deterministic OEM-mapped fields */
        al_avpu_fill_cmd_entry_enc1(ctx, cmd);

        /* Optional debug: dump first 8 dwords of entry */
        LOG_AL("CL[%u] cmd[0..7]=%08x %08x %08x %08x %08x %08x %08x %08x",
               idx, cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5], cmd[6], cmd[7]);
        /* Ensure SRC/STRM controls are enabled (belt-and-suspenders) */
        (void)avpu_ip_write_reg(ctx->fd, AVPU_REG_SRC_CTRL, 1);
        (void)avpu_ip_write_reg(ctx->fd, AVPU_REG_STRM_CTRL, 1);


        /* Clean cache for this CL entry before device reads it (use virtual addr) */
        void* cl_entry_virt = (void*)((uint8_t*)ctx->cl_ring.map + (size_t)idx * ctx->cl_entry_size);
        avpu_cache_clean_virt(ctx->fd, cl_entry_virt, ctx->cl_entry_size);

        /* Ensure clock command is ON (bits[1:0]=1) before commit */
        {
            unsigned clk_reg = AVPU_REG_CORE_CLKCMD(0);
        /* Additional safe ungates observed in OEM footprints (queue path) */
        /* Clear pending before programming */
        (void)avpu_ip_write_reg(ctx->fd, AVPU_INTERRUPT, 0x00FFFFFF);

            unsigned v = 0; (void)avpu_ip_read_reg(ctx->fd, clk_reg, &v);
            unsigned new_v = (v & ~0x3u) | 0x1u;
            if (new_v != v) (void)avpu_ip_write_reg(ctx->fd, clk_reg, new_v);
        }


        /* OEM parity: no commit pulsing or generic ungates here */

        /* IRQ mask per OEM before push */
        (void)avpu_ip_write_reg(ctx->fd, AVPU_INTERRUPT_MASK, 0x00000010);

        /* Program CL start and push (OEM path: 0x83E0/0x83E4) */
        uint32_t cl_phys = ctx->cl_ring.phy_addr + (idx * ctx->cl_entry_size);
        uint32_t cl_start_abs = cl_phys;
        (void)avpu_ip_write_reg(ctx->fd, AVPU_REG_CL_ADDR, cl_start_abs);
        (void)avpu_ip_write_reg(ctx->fd, AVPU_REG_CL_PUSH, 0x00000002);
        LOG_AL("queue: CL start=0x%08x (ABS) -> [0x%04x], push [0x%04x]=0x2", cl_start_abs, AVPU_REG_CL_ADDR, AVPU_REG_CL_PUSH);
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


        /* Peek pending IRQs immediately */
        {
            unsigned pend = 0; (void)avpu_ip_read_reg(ctx->fd, AVPU_INTERRUPT, &pend);
            LOG_AL("post-kick IRQ pending=0x%08x", pend);
        }

        /* Advance to next CL slot */
        ctx->cl_idx = (idx + 1) % ctx->cl_count;
    }

    return 0;
}

int ALAvpu_DequeueStream(ALAvpuContext *ctx, HWStreamBuffer *out, int timeout_ms)
{
    if (!ctx || ctx->fd < 0 || !out) return -1;

    if (!ctx->session_ready) { errno = EAGAIN; return -1; }

    /* Wait for an IRQ indicating stream availability */
    int irq = -1;
    pthread_mutex_t* m = (pthread_mutex_t*)ctx->irq_mutex;
    pthread_cond_t*  c = (pthread_cond_t*)ctx->irq_cond;
    if (!m || !c) { errno = EAGAIN; LOG_AL("DequeueStream: sync primitives not ready"); return -1; }

    struct timespec ts;
    if (timeout_ms >= 0) {
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec  += timeout_ms / 1000;
        ts.tv_nsec += (long)(timeout_ms % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    }

    pthread_mutex_lock(m);
    while (!irq_queue_pop(ctx, &irq)) {
        if (timeout_ms < 0) {
            pthread_cond_wait(c, m);
        } else {
            if (pthread_cond_timedwait(c, m, &ts) == ETIMEDOUT) {
                pthread_mutex_unlock(m);
                errno = EAGAIN;
                return -1;
            }
        }
    }
    pthread_mutex_unlock(m);

    /* For now, pick the first queued stream buffer and compute effective AnnexB size. */
    for (int i = 0; i < ctx->stream_bufs_used; ++i) {
        if (ctx->stream_in_hw[i]) {
            out->phys_addr = ctx->stream_bufs[i].phy_addr;
            out->virt_addr = (uint32_t)(uintptr_t)ctx->stream_bufs[i].map;
            /* Invalidate cache before CPU read: use VIRT address (driver expects CPU-mapped VA) */
            avpu_cache_inv_virt(ctx->fd, (void*)(uintptr_t)out->virt_addr, ctx->stream_buf_size);
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


