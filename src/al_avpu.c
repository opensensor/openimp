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



/* REMOVED: ALL ALAvpu_* wrapper functions do NOT exist in OEM binary
 * Confirmed via Binary Ninja MCP search - no ALAvpu_Init, ALAvpu_Deinit,
 * ALAvpu_QueueFrame, ALAvpu_DequeueStream, ALAvpu_ReleaseStream, or ALAvpu_SetEvent.
 *
 * OEM architecture (from Binary Ninja decompilation):
 * - Device opening: AL_DevicePool_Open("/dev/avpu") at 0x362dc
 * - Context init: AL_Common_Encoder_CreateChannel (called from AL_Encoder_Create)
 * - Frame encoding: Direct ioctl calls in AL_Common_Encoder_Process
 * - Stream retrieval: Fifo_Dequeue in AL_Codec_Encode_GetStream (0x7a548)
 * - Stream release: AL_Encoder_PutStreamBuffer in AL_Codec_Encode_ReleaseStream (0x7a624)
 *
 * All AVPU operations are performed directly in the encoder layer (codec.c)
 * using direct ioctl calls to /dev/avpu, matching OEM behavior exactly.
 *
 * The following helper functions remain for internal use by codec.c:
 * - avpu_ip_write_reg / avpu_ip_read_reg (direct ioctl wrappers)
 * - fill_cmd_regs_from_oem_enc1 (command list population)
 * - avpu_enable_interrupts_core (interrupt management)
 */

/* End of al_avpu.c - all wrapper functions removed for OEM parity */


