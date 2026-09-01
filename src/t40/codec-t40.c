/**
 * AL_Codec Implementation
 * Based on reverse engineering of libimp.so v1.1.6
 * Decompiled from addresses 0x7950c (Create), 0x7a180 (Destroy), etc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>

/* The T21 encoder ABI and legacy Helix device seam match T30.  This alias is
 * deliberately translation-unit local; T21 ISP/FrameSource follows T31. */
#if defined(PLATFORM_T21)
#define PLATFORM_T30 1
#endif

#include <imp/imp_encoder.h>
#include <imp/imp_system.h>
#include "codec.h"
#include "fifo.h"
#include "hw_encoder.h"

#include "al_avpu.h"
#include "device_pool.h"
#include "dma_alloc.h"
#include "imp_log_int.h"
#include "kernel_interface.h"
#include "openimp_profile.h"
#if defined(PLATFORM_T31)
#include "t31_rate_control.h"
#include "t31_stream_layout.h"
#endif
#if defined(PLATFORM_T41)
#include "t41_stream_layout.h"
#endif
#if defined(PLATFORM_T23)
#include "t23/openimp_t23_helix_bridge.h"
#include "t23/openimp_t23_persist.h"
#endif
#if defined(PLATFORM_T30)
#include "t30/t30_helix_encoder.h"
#endif
#include "t40_ep1.h"
#if defined(PLATFORM_T41)
#include "t41_command_builder.h"
#include "t41_hw_rate_control.h"
#include "t41_rate_control.h"
#endif
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/syscall.h> /* for SYS_ioctl */

/*
 * T41 reuses the T4 encoder implementation, EP tables, IRQ owner and device
 * pool.  Generation-specific guards still take precedence for its public ABI,
 * cache rules, register map and command-slot transport.  The remaining T41
 * command payload conversion is intentionally isolated inside this backend.
 */
#if defined(PLATFORM_T41)
#define PLATFORM_T40 1
#endif

enum {
    AVPU_STREAM_BUF_FREE = 0,
    AVPU_STREAM_BUF_IN_FLIGHT = 1,
    AVPU_STREAM_BUF_READY = 2,
};

/* Throttled logging: only emit per-frame logs on every Nth frame.
 * Use LOG_CODEC_THROTTLE(ctx, ...) in hot paths instead of LOG_CODEC. */
#define AVPU_LOG_INTERVAL 50
#define AVPU_T31_STREAM_PREFIX_BYTES 0x220u
#define AVPU_T31_PAYLOAD_OFFSET       0x220u
#define AVPU_SHOULD_LOG(ctx) \
    ((ctx) && ((ctx)->frames_encoded < 3 || ((ctx)->frames_encoded % AVPU_LOG_INTERVAL) == 0))
#define LOG_CODEC_THROTTLE(ctx, ...) do { if (AVPU_SHOULD_LOG(ctx)) LOG_CODEC(__VA_ARGS__); } while(0)

static void codec_startup_marker(const char *marker, size_t size)
{
    if (getenv("OPENIMP_STARTUP_TRACE"))
        (void)write(STDERR_FILENO, marker, size);
#if defined(PLATFORM_T23)
    openimp_t23_persist_write(marker, size);
#endif
}

#define CODEC_STARTUP_MARKER(value) \
    codec_startup_marker((value), sizeof(value) - 1u)

static void codec_startup_trace(const char *format, ...)
{
    char message[256];
    va_list arguments;
    int length;

    if (!getenv("OPENIMP_STARTUP_TRACE")
#if defined(PLATFORM_T23)
        && !openimp_t23_persist_enabled()
#endif
    )
        return;
    va_start(arguments, format);
    length = vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);
    if (length > 0) {
        size_t size = (size_t)length;

        if (size >= sizeof(message))
            size = sizeof(message) - 1u;
        if (getenv("OPENIMP_STARTUP_TRACE")) {
            (void)write(STDERR_FILENO, message, size);
            (void)fsync(STDERR_FILENO);
        }
#if defined(PLATFORM_T23)
        openimp_t23_persist_write(message, size);
#endif
    }
}

typedef struct AL_CodecEncode AL_CodecEncode;

static int avpu_queue_completed_stream(ALAvpuContext *ctx, int buf_idx, void *user_data,
                                       const char *source, uint32_t *frame_size_out,
                                       int *flush_ret_out);
#if defined(PLATFORM_T40)
void OpenIMP_P3_FrameStats(uint32_t luma, uint32_t u_mean,
                           uint32_t v_mean);
#endif

#if defined(PLATFORM_T40) || defined(PLATFORM_T31)
/*
 * The OEM board object owns one /dev/avpu wait thread and dispatches IRQs to
 * whichever encoder submitted the core.  AL_DevicePool likewise returns one
 * shared fd.  Keep that ownership shape here: one host waiter, with the
 * active context selected immediately before CL_PUSH.  This is what permits
 * the main and sub AVC channels to alternate on the single T-series core.
 */
static pthread_mutex_t g_tseries_irq_host_lock = PTHREAD_MUTEX_INITIALIZER;
static ALAvpuContext *g_tseries_irq_host;
static ALAvpuContext *volatile g_tseries_irq_owner;
#if defined(PLATFORM_T31)
/* T31's mainline AVPU driver exposes one physical encoder channel. */
static pthread_mutex_t g_t31_encode_core_lock = PTHREAD_MUTEX_INITIALIZER;
#endif
#endif

#define CODEC_READ_U8(base, off)  (*(uint8_t *)((uint8_t *)(base) + (off)))
#define CODEC_READ_U16(base, off) (*(uint16_t *)((uint8_t *)(base) + (off)))
#define CODEC_READ_S32(base, off) (*(int32_t *)((uint8_t *)(base) + (off)))
#define CODEC_READ_U32(base, off) (*(uint32_t *)((uint8_t *)(base) + (off)))
#define CODEC_WRITE_U8(base, off, val)  (*(uint8_t *)((uint8_t *)(base) + (off)) = (uint8_t)(val))
#define CODEC_WRITE_U16(base, off, val) (*(uint16_t *)((uint8_t *)(base) + (off)) = (uint16_t)(val))
#define CODEC_WRITE_S32(base, off, val) (*(int32_t *)((uint8_t *)(base) + (off)) = (int32_t)(val))
#define CODEC_WRITE_U32(base, off, val) (*(uint32_t *)((uint8_t *)(base) + (off)) = (uint32_t)(val))

int EncodingStatusRegsToSliceStatus(const void *status_regs, void *slice_status)
{
    const void *arg1 = status_regs;
    void *arg2 = slice_status;
    int32_t v0_2;
    int32_t v1_2;
    int32_t v1_3;
    int32_t a2_1;
    int32_t a3_1;
    int32_t v0_7;
    int32_t v1_4;
    int32_t v0_17;

    CODEC_WRITE_U32(arg2, 0x34, CODEC_READ_U32(arg1, 0x130));
    CODEC_WRITE_U32(arg2, 0x38, CODEC_READ_U32(arg1, 0x134) & 0x0fffffffU);
    v0_2 = CODEC_READ_S32(arg1, 0x138);
    CODEC_WRITE_U32(arg2, 0x44, CODEC_READ_U32(arg1, 0x13c));
    CODEC_WRITE_U32(arg2, 0x48, CODEC_READ_U32(arg1, 0x140));
    v1_2 = CODEC_READ_S32(arg1, 0x144);
    CODEC_WRITE_U16(arg2, 0x3e, (uint16_t)(v0_2 & 0xff));
    CODEC_WRITE_U32(arg2, 0x4c, v1_2);
    v1_3 = CODEC_READ_S32(arg1, 0x148);
    CODEC_WRITE_U16(arg2, 0x42, (uint16_t)((uint32_t)v0_2 >> 16));
    CODEC_WRITE_U16(arg2, 0x40, (uint16_t)(((uint32_t)v0_2 >> 8) & 0xff));
    CODEC_WRITE_U32(arg2, 0x50, v1_3);
    CODEC_WRITE_U32(arg2, 0x54, CODEC_READ_U32(arg1, 0x14c));
    CODEC_WRITE_U32(arg2, 0x58, CODEC_READ_U32(arg1, 0x150));
    CODEC_WRITE_U32(arg2, 0x5c, CODEC_READ_U32(arg1, 0x154));
    a2_1 = CODEC_READ_S32(arg1, 0x158);
    a3_1 = CODEC_READ_S32(arg1, 0x15c);
    v0_7 = CODEC_READ_S32(arg1, 0x160);
    v1_4 = CODEC_READ_S32(arg1, 0x164);
    CODEC_WRITE_U32(arg2, 0x14, CODEC_READ_U32(arg1, 0x10c));
    CODEC_WRITE_U32(arg2, 0x60, a3_1);
    CODEC_WRITE_U32(arg2, 0x64, a2_1);
    CODEC_WRITE_U32(arg2, 0x68, v1_4);
    CODEC_WRITE_U32(arg2, 0x6c, v0_7);
    CODEC_WRITE_U32(arg2, 0x18, CODEC_READ_U32(arg1, 0x110));
    CODEC_WRITE_U32(arg2, 0x1c, CODEC_READ_U32(arg1, 0x114));
    CODEC_WRITE_U32(arg2, 0x20, CODEC_READ_U32(arg1, 0x11c));
    CODEC_WRITE_U32(arg2, 0x24, CODEC_READ_U32(arg1, 0x120));
    CODEC_WRITE_U32(arg2, 0x28, CODEC_READ_U32(arg1, 0x124));
    CODEC_WRITE_U32(arg2, 0x2c, CODEC_READ_U32(arg1, 0x128));
    CODEC_WRITE_U32(arg2, 0x30, CODEC_READ_U16(arg1, 0x12c));
    CODEC_WRITE_U32(arg2, 0x10, CODEC_READ_U16(arg1, 0x12e));
    v0_17 = (CODEC_READ_U32(arg1, 8) >> 0xb) & 1;
    if (v0_17 != 0) {
        CODEC_WRITE_U8(arg2, 2, 0);
        return v0_17;
    }

    {
        uint8_t v0_23 =
            (uint8_t)(((((CODEC_READ_U32(arg1, 0x104) & 0x3fffffffU) + 0x1fU) >> 5 << 5) <
                       (((CODEC_READ_U32(arg1, 0xcc) >> 5) - 1U) << 5))
                          ? 1
                          : 0) ^
                     1;
        CODEC_WRITE_U8(arg2, 2, v0_23);
        return v0_23;
    }
}

int MergeEncodingStatus(void *merged_status, const void *slice_status)
{
    void *arg1 = merged_status;
    const void *arg2 = slice_status;
    int32_t t7 = (int16_t)CODEC_READ_U16(arg2, 0x3e);
    int32_t t8 = (int16_t)CODEC_READ_U16(arg1, 0x3e);
    int32_t t1_1;
    int32_t t0_1;
    int32_t t5_1;
    int32_t a3_1;
    int32_t v0;
    int32_t t9;
    int32_t t4_1;
    int32_t a2_1;
    int32_t t3_1;
    int32_t v1_1;
    int32_t v0_1;
    int32_t a2_2;
    int32_t a2_3;
    int32_t t5_2;
    int32_t v0_4;
    int32_t v1_5;
    int32_t t6_3;
    int32_t s1_4;
    int32_t s0_1;
    int32_t t9_1;
    int32_t t8_1;
    int32_t t7_6;
    int32_t s2_1;
    int32_t t4_2;
    int32_t t3_3;
    int32_t t2_3;
    int32_t t1_3;
    int32_t t0_3;
    int32_t a3_4;
    int32_t t4_3;
    int32_t t3_4;
    int32_t a2_4;
    int32_t v1_6;
    uint8_t v0_7;
    int32_t t2_4;
    int32_t t1_4;
    uint8_t result;

    if (t7 >= t8) {
        t7 = t8;
    }
    t1_1 = CODEC_READ_S32(arg1, 0x48) + CODEC_READ_S32(arg2, 0x48);
    t0_1 = CODEC_READ_S32(arg1, 0x4c) + CODEC_READ_S32(arg2, 0x4c);
    t5_1 = CODEC_READ_S32(arg1, 0x34) + CODEC_READ_S32(arg2, 0x34);
    a3_1 = CODEC_READ_S32(arg1, 0x50) + CODEC_READ_S32(arg2, 0x50);
    v0 = (int16_t)CODEC_READ_U16(arg2, 0x40);
    t9 = (int16_t)CODEC_READ_U16(arg1, 0x40);
    t4_1 = CODEC_READ_S32(arg1, 0x38) + CODEC_READ_S32(arg2, 0x38);
    a2_1 = CODEC_READ_S32(arg1, 0x54) + CODEC_READ_S32(arg2, 0x54);
    if (t9 >= v0) {
        v0 = t9;
    }
    t3_1 = CODEC_READ_S32(arg1, 0x44) + CODEC_READ_S32(arg2, 0x44);
    v1_1 = CODEC_READ_S32(arg1, 0x58) + CODEC_READ_S32(arg2, 0x58);
    CODEC_WRITE_U16(arg1, 0x42, CODEC_READ_U16(arg1, 0x42) + CODEC_READ_U16(arg2, 0x42));
    CODEC_WRITE_S32(arg1, 0x34, t5_1);
    CODEC_WRITE_S32(arg1, 0x38, t4_1);
    CODEC_WRITE_S32(arg1, 0x44, t3_1);
    CODEC_WRITE_U16(arg1, 0x40, (uint16_t)v0);
    CODEC_WRITE_U16(arg1, 0x3e, (uint16_t)t7);
    CODEC_WRITE_S32(arg1, 0x48, t1_1);
    v0_1 = CODEC_READ_S32(arg1, 0x60);
    CODEC_WRITE_S32(arg1, 0x54, a2_1);
    a2_2 = CODEC_READ_S32(arg2, 0x60);
    CODEC_WRITE_S32(arg1, 0x50, a3_1);
    CODEC_WRITE_S32(arg1, 0x58, v1_1);
    a2_3 = v0_1 + a2_2;
    t5_2 = CODEC_READ_S32(arg2, 0x5c);
    CODEC_WRITE_S32(arg1, 0x64, ((uint32_t)a2_3 < (uint32_t)v0_1 ? 1 : 0) + CODEC_READ_S32(arg1, 0x64) + CODEC_READ_S32(arg2, 0x64));
    v0_4 = CODEC_READ_S32(arg1, 0x68);
    v1_5 = v0_4 + CODEC_READ_S32(arg2, 0x68);
    t6_3 = CODEC_READ_S32(arg1, 0x6c) + CODEC_READ_S32(arg2, 0x6c);
    s1_4 = CODEC_READ_S32(arg2, 0x14);
    s0_1 = CODEC_READ_S32(arg2, 0x30);
    t9_1 = CODEC_READ_S32(arg2, 0x2c);
    t8_1 = CODEC_READ_S32(arg2, 0x28);
    t7_6 = CODEC_READ_S32(arg2, 0x24);
    s2_1 = CODEC_READ_S32(arg1, 0x5c);
    t4_2 = CODEC_READ_S32(arg1, 0x18);
    CODEC_WRITE_S32(arg1, 0x4c, t0_1);
    t3_3 = CODEC_READ_S32(arg1, 0x14) + s1_4;
    t2_3 = CODEC_READ_S32(arg1, 0x30) + s0_1;
    t1_3 = CODEC_READ_S32(arg1, 0x2c) + t9_1;
    t0_3 = CODEC_READ_S32(arg1, 0x28) + t8_1;
    a3_4 = CODEC_READ_S32(arg1, 0x24) + t7_6;
    t4_3 = t4_2 + CODEC_READ_S32(arg2, 0x18);
    CODEC_WRITE_S32(arg1, 0x60, a2_3);
    CODEC_WRITE_S32(arg1, 0x68, v1_5);
    CODEC_WRITE_S32(arg1, 0x6c, ((uint32_t)v1_5 < (uint32_t)v0_4 ? 1 : 0) + t6_3);
    CODEC_WRITE_S32(arg1, 0x5c, s2_1 + t5_2);
    CODEC_WRITE_S32(arg1, 0x18, t4_3);
    CODEC_WRITE_S32(arg1, 0x14, t3_3);
    t3_4 = CODEC_READ_S32(arg2, 0x1c);
    a2_4 = CODEC_READ_S32(arg1, 0x20);
    v1_6 = CODEC_READ_S32(arg1, 0x10);
    v0_7 = CODEC_READ_U8(arg1, 2);
    CODEC_WRITE_S32(arg1, 0x30, t2_3);
    CODEC_WRITE_S32(arg1, 0x2c, t1_3);
    t2_4 = CODEC_READ_S32(arg2, 0x20);
    t1_4 = CODEC_READ_S32(arg2, 0x10);
    CODEC_WRITE_S32(arg1, 0x28, t0_3);
    CODEC_WRITE_S32(arg1, 0x24, a3_4);
    result = v0_7 | CODEC_READ_U8(arg2, 2);
    CODEC_WRITE_S32(arg1, 0x1c, CODEC_READ_S32(arg1, 0x1c) + t3_4);
    CODEC_WRITE_S32(arg1, 0x20, a2_4 + t2_4);
    CODEC_WRITE_S32(arg1, 0x10, v1_6 + t1_4);
    CODEC_WRITE_U8(arg1, 2, result);
    return result;
}

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

/* AVPU register offsets (from driver and OEM userspace decompilation). */
#if defined(PLATFORM_T41)
/*
 * T41 keeps the 'q' ioctl transport but exposes the newer 4 KiB/core register
 * banks directly.  Do not fold these offsets into the T40 0x8000 window: live
 * OEM ioctl traces use the values below verbatim.
 */
#define AVPU_BASE_OFFSET       0x0000
#define AVPU_INTERRUPT_MASK    0x0014
#define AVPU_INTERRUPT         0x0018
#define AVPU_REG_TOP_CTRL      0x0000
#define AVPU_REG_MISC_CTRL     0x0010
#define AVPU_REG_SRC_PUSH      0x0000
#define AVPU_REG_STRM_PUSH     0x0000
#define AVPU_REG_CL_ADDR       0x1000
#define AVPU_REG_CL_ADDR_HI    0x1004
#define AVPU_REG_CL_STATUS     0x1008
#define AVPU_REG_CL_STATUS_HI  0x100c
#define AVPU_REG_CL_CTRL       0x1010
#define AVPU_REG_CL_PUSH       0x1018
#define AVPU_REG_ENC_EN_A      0x0000
#define AVPU_REG_ENC_EN_B      0x0000
#define AVPU_REG_ENC_EN_C      0x0000
#define AVPU_REG_AXI_ADDR_OFFSET_IP 0x0000
#define AVPU_REG_WPP_CORE0_RESET(c) (0x1028u + ((unsigned)(c) << 12))
#define AVPU_REG_CORE_STATUS_8230(c) (0x1030u + ((unsigned)(c) << 12))
#define AVPU_REG_CORE_STATUS_8234(c) (0x1034u + ((unsigned)(c) << 12))
#define AVPU_REG_CORE_STATUS_8238(c) (0x1038u + ((unsigned)(c) << 12))
#define AVPU_REG_CORE_RESET(c)  (0x1028u + ((unsigned)(c) << 12))
#define AVPU_REG_CORE_CLKCMD(c) (0x102cu + ((unsigned)(c) << 12))
#define AVPU_REG_CORE_STATUS(c) (0x101cu + ((unsigned)(c) << 12))
#else
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
#endif

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
/* Cache flush via AVPU driver's JZ_CMD_FLUSH_CACHE ioctl + rmem ioctl.
 * The AVPU driver's dma_cache_sync handles L1. We also try rmem ioctl
 * in case the kernel supports it. */
#define JZ_CMD_FLUSH_CACHE_IOCTL _IOWR('q', 14, int)
struct avpu_flush_info { unsigned int addr; unsigned int len; unsigned int dir; };
/* OEM: Rtos_FlushCacheMemory → alloc_kmem_flush_cache
 *   → ioctl(rmem_fd, 0xc00c7200, {vaddr, size, dir=1})
 * Uses the RMEM fd (not AVPU fd) and the rmem-mapped virtual address. */
#define RMEM_FLUSH_IOCTL 0xc00c7200
struct rmem_flush_info_codec { unsigned int addr; unsigned int size; unsigned int dir; };
static int avpu_flush_cache_internal(int fd, void *virt_addr,
                                     unsigned int size, unsigned int dir,
                                     OpenIMPProfileStage site)
{
    OpenIMPProfileStamp profile;
    int result;

    (void)fd; /* OEM uses rmem_fd, not avpu_fd */
    if (!virt_addr || size == 0) return -1;
#if defined(PLATFORM_T41)
    /* T41's rmem implementation rejects or stalls on short cache ranges.
     * The OEM always asks it to operate on a 1 MiB window, including for
     * the ~228 KiB substream buffers.  Normalize centrally because command,
     * stream and completion paths also call this primitive directly. */
    if (size < 0x100000u)
        size = 0x100000u;
#endif
    profile = openimp_profile_begin();
    result = DMA_RmemFlushCache(virt_addr, size, (int)dir);
    openimp_profile_count(OPENIMP_PROFILE_CACHE_BYTES, size);
    if ((unsigned int)site < OPENIMP_PROFILE_STAGE_COUNT)
        openimp_profile_end_pair(OPENIMP_PROFILE_CACHE_MAINTENANCE,
                                 site, profile);
    else
        openimp_profile_end(OPENIMP_PROFILE_CACHE_MAINTENANCE, profile);
    return result;
}

static int avpu_flush_cache(int fd, void *virt_addr, unsigned int size,
                            unsigned int dir)
{
    return avpu_flush_cache_internal(fd, virt_addr, size, dir,
                                     OPENIMP_PROFILE_STAGE_COUNT);
}

static int avpu_flush_cache_profiled(int fd, void *virt_addr,
                                     unsigned int size, unsigned int dir,
                                     OpenIMPProfileStage stage)
{
    return avpu_flush_cache_internal(fd, virt_addr, size, dir, stage);
}

static int avpu_flush_dma_buf_internal(int fd, const char *tag,
                                       const AvpuDMABuf *buf, size_t size,
                                       OpenIMPProfileStage stage)
{
    size_t flush_size = size;
    int ret;

    if (fd < 0 || !buf || !buf->map || size == 0) {
        return -1;
    }

#if defined(PLATFORM_T41)
    if (flush_size < 0x100000u)
        flush_size = 0x100000u;
#endif
    if ((unsigned int)stage < OPENIMP_PROFILE_STAGE_COUNT)
        ret = avpu_flush_cache_profiled(
            fd, buf->map, (unsigned int)flush_size, 1 /* WBACK */, stage);
    else
        ret = avpu_flush_cache(fd, buf->map, (unsigned int)flush_size,
                               1 /* WBACK */);
    { static unsigned int fl_count = 0; unsigned int c = __sync_add_and_fetch(&fl_count, 1);
      if (c <= 10 || (c % 1000) == 0)
        LOG_CODEC("AVPU: flush %s phys=0x%08x size=0x%08x requested=0x%08x ret=%d [#%u]",
                  tag ? tag : "dma", buf->phy_addr,
                  (unsigned int)flush_size, (unsigned int)size, ret, c);
    }
    return ret;
}

static int avpu_flush_dma_buf(int fd, const char *tag,
                              const AvpuDMABuf *buf, size_t size)
{
    return avpu_flush_dma_buf_internal(fd, tag, buf, size,
                                       OPENIMP_PROFILE_STAGE_COUNT);
}

#if defined(PLATFORM_T41)
static int avpu_flush_dma_buf_profiled(int fd, const char *tag,
                                       const AvpuDMABuf *buf, size_t size,
                                       OpenIMPProfileStage stage)
{
    return avpu_flush_dma_buf_internal(fd, tag, buf, size, stage);
}
#endif

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

    { static unsigned int wr_count = 0; unsigned int c = __sync_add_and_fetch(&wr_count, 1);
      if (c <= 20 || (c % 1000) == 0)
        LOG_CODEC("AVPU write_reg: off=0x%08x val=0x%08x argp=%p [#%u]", off, val, (void*)p, c);
    }
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
        static unsigned int rd_count = 0; unsigned int c = __sync_add_and_fetch(&rd_count, 1);
        if (c <= 20 || (c % 1000) == 0)
            LOG_CODEC("AVPU read_reg: off=0x%08x argp=%p [#%u]", off, (void*)p, c);
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
    out->uncached_map = NULL;
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
    out->uncached_map = NULL;
    out->size = size;
    out->from_rmem = 1; /* prevent munmap in destroy; allocator owns lifetime */
    LOG_CODEC("AVPU: imp-alloc ok phys=0x%08x size=%zu virt=%p", phys, size, virt);
    return 0;
}

/* The IMP graph owns a shared rmem arena during normal Raptor operation. A
 * standalone V4L2 consumer has no P1 FrameSource state, so that arena cannot
 * be discovered. Preserve the proven rmem path when present and fall back to
 * coherent allocations owned by the already-open AVPU channel otherwise. */
static int avpu_alloc_encoder(int fd, size_t size, const char *tag,
                              AvpuDMABuf *out)
{
    if (avpu_alloc_imp(size, tag, out) == 0)
        return 0;
    LOG_CODEC("AVPU: %s falling back to coherent allocation (%zu bytes)",
              tag ? tag : "buffer", size);
    return avpu_alloc_mmap(fd, size, out);
}

/* Release every userspace reference carried by a DMA descriptor.  Coherent
 * GET_DMA_MMAP allocations are VMAs of /dev/avpu, so closing the channel fd
 * before unmapping all of them does not invoke the driver's release method:
 * the VMA keeps struct file alive and T31/T40's single codec->chan slot remains
 * permanently occupied.  IMP/rmem owns its primary mapping, but any uncached
 * /dev/mem alias and optional dmabuf fd are still ours. */
static void avpu_release_dma_buf(AvpuDMABuf *buf)
{
    uint32_t phys_addr;
    int from_rmem;

    if (!buf)
        return;

    phys_addr = buf->phy_addr;
    from_rmem = buf->from_rmem;

    if (buf->uncached_map && buf->uncached_map != MAP_FAILED) {
        munmap(buf->uncached_map, buf->size);
        buf->uncached_map = NULL;
    }
    if (!buf->from_rmem && buf->map && buf->map != MAP_FAILED) {
        munmap(buf->map, buf->size);
        buf->map = NULL;
    }
    if (buf->dmabuf_fd >= 0) {
        close(buf->dmabuf_fd);
        buf->dmabuf_fd = -1;
    }
    if (from_rmem && phys_addr)
        (void)DMA_FreePhys(phys_addr);

    buf->phy_addr = 0;
    buf->mmap_off = 0;
    buf->size = 0;
    buf->from_rmem = 0;
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

static inline void *avpu_cl_ring_base(ALAvpuContext *ctx)
{
    if (!ctx)
        return NULL;
    return ctx->cl_ring.uncached_map ? ctx->cl_ring.uncached_map : ctx->cl_ring.map;
}

static inline void *avpu_cl_submit_ring_base(ALAvpuContext *ctx)
{
    if (!ctx)
        return NULL;
    return ctx->cl_submit_ring.uncached_map ? ctx->cl_submit_ring.uncached_map : ctx->cl_submit_ring.map;
}

static inline uint8_t *avpu_cl_entry_ptr(ALAvpuContext *ctx, uint32_t idx)
{
    uint8_t *base = (uint8_t *)avpu_cl_ring_base(ctx);
    if (!base || ctx->cl_entry_size == 0 || ctx->cl_count == 0)
        return NULL;
    return base + ((size_t)(idx % ctx->cl_count) * ctx->cl_entry_size);
}

static inline uint8_t *avpu_cl_submit_entry_ptr(ALAvpuContext *ctx, uint32_t idx)
{
    uint8_t *base = (uint8_t *)avpu_cl_submit_ring_base(ctx);
    if (!base || ctx->cl_entry_size == 0 || ctx->cl_count == 0)
        return NULL;
    return base + ((size_t)(idx % ctx->cl_count) * ctx->cl_entry_size);
}

static inline uint8_t *avpu_cl_status_ptr(ALAvpuContext *ctx, uint32_t idx)
{
    uint8_t *entry = avpu_cl_entry_ptr(ctx, idx);
#if defined(PLATFORM_T41)
    return (uint8_t *)openimp_t41_command_status_ptr(
        entry, ctx ? ctx->cl_entry_size : 0u);
#else
    return entry;
#endif
}

static inline uint8_t *avpu_cl_submit_status_ptr(ALAvpuContext *ctx,
                                                 uint32_t idx)
{
    uint8_t *entry = avpu_cl_submit_entry_ptr(ctx, idx);
#if defined(PLATFORM_T41)
    return (uint8_t *)openimp_t41_command_status_ptr(
        entry, ctx ? ctx->cl_entry_size : 0u);
#else
    return entry;
#endif
}

static void avpu_log_dma_range(const char *name, const AvpuDMABuf *buf)
{
    uint64_t start;
    uint64_t end;

    if (!name || !buf || buf->phy_addr == 0 || buf->size == 0)
        return;

    start = (uint64_t)buf->phy_addr;
    end = start + (uint64_t)buf->size;
    LOG_CODEC("AVPU: dma range %s phys=[0x%08x..0x%08x) size=0x%zx virt=%p uncached=%p rmem=%d",
              name, buf->phy_addr, (uint32_t)end, buf->size,
              buf->map, buf->uncached_map, buf->from_rmem);
}

static int avpu_dma_ranges_overlap(const AvpuDMABuf *a, const AvpuDMABuf *b)
{
    uint64_t a_start;
    uint64_t a_end;
    uint64_t b_start;
    uint64_t b_end;

    if (!a || !b || a->phy_addr == 0 || b->phy_addr == 0 || a->size == 0 || b->size == 0)
        return 0;

    a_start = (uint64_t)a->phy_addr;
    a_end = a_start + (uint64_t)a->size;
    b_start = (uint64_t)b->phy_addr;
    b_end = b_start + (uint64_t)b->size;
    return (a_start < b_end) && (b_start < a_end);
}

static void avpu_log_dma_overlap(const char *name_a, const AvpuDMABuf *a,
                                 const char *name_b, const AvpuDMABuf *b)
{
    if (!name_a || !name_b || !a || !b)
        return;

    if (!avpu_dma_ranges_overlap(a, b))
        return;

    LOG_CODEC("AVPU: WARNING DMA overlap %s [0x%08x..0x%08x) with %s [0x%08x..0x%08x)",
              name_a,
              a->phy_addr, (uint32_t)((uint64_t)a->phy_addr + (uint64_t)a->size),
              name_b,
              b->phy_addr, (uint32_t)((uint64_t)b->phy_addr + (uint64_t)b->size));
}

static void avpu_log_dma_layout(ALAvpuContext *ctx)
{
    struct NamedBuf {
        const char *name;
        const AvpuDMABuf *buf;
    } named[24];
    int named_count = 0;
    int i;
    int j;
    char stream_name[16][24];

    if (!ctx)
        return;

    LOG_CODEC("AVPU: DMA layout begin (stream_bufs_used=%d cl_count=%u cl_entry_size=%u)",
              ctx->stream_bufs_used, ctx->cl_count, ctx->cl_entry_size);

    for (i = 0; i < ctx->stream_bufs_used && i < 16; ++i) {
        snprintf(stream_name[i], sizeof(stream_name[i]), "stream_buf[%d]", i);
        avpu_log_dma_range(stream_name[i], &ctx->stream_bufs[i]);
        named[named_count].name = stream_name[i];
        named[named_count].buf = &ctx->stream_bufs[i];
        named_count++;
    }

    avpu_log_dma_range("cl_ring", &ctx->cl_ring);
    named[named_count].name = "cl_ring";
    named[named_count].buf = &ctx->cl_ring;
    named_count++;

    avpu_log_dma_range("cl_submit_ring", &ctx->cl_submit_ring);
    named[named_count].name = "cl_submit_ring";
    named[named_count].buf = &ctx->cl_submit_ring;
    named_count++;

    avpu_log_dma_range("interm_buf", &ctx->interm_buf);
    named[named_count].name = "interm_buf";
    named[named_count].buf = &ctx->interm_buf;
    named_count++;

    avpu_log_dma_range("rec_buf", &ctx->rec_buf);
    named[named_count].name = "rec_buf";
    named[named_count].buf = &ctx->rec_buf;
    named_count++;

    avpu_log_dma_range("ref_buf", &ctx->ref_buf);
    named[named_count].name = "ref_buf";
    named[named_count].buf = &ctx->ref_buf;
    named_count++;

    avpu_log_dma_range("rec_trace_buf", &ctx->rec_trace_buf);
    named[named_count].name = "rec_trace_buf";
    named[named_count].buf = &ctx->rec_trace_buf;
    named_count++;

    avpu_log_dma_range("ref_trace_buf", &ctx->ref_trace_buf);
    named[named_count].name = "ref_trace_buf";
    named[named_count].buf = &ctx->ref_trace_buf;
    named_count++;

    for (i = 0; i < named_count; ++i) {
        for (j = i + 1; j < named_count; ++j) {
            avpu_log_dma_overlap(named[i].name, named[i].buf,
                                 named[j].name, named[j].buf);
        }
    }

    LOG_CODEC("AVPU: DMA layout end");
}

static uint32_t avpu_align_up_u32(uint32_t value, uint32_t alignment)
{
    if (alignment == 0)
        return value;
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static uint32_t avpu_align_down_u32(uint32_t value, uint32_t alignment)
{
    if (alignment == 0)
        return value;
    return value & ~(alignment - 1u);
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

static uint32_t avpu_get_stream_buffer_size(uint32_t width, uint32_t height,
                                            uint32_t bitrate)
{
    uint64_t picture_bytes = (uint64_t)width * (uint64_t)height;
    uint64_t aligned;

#if defined(PLATFORM_T31)
    uint64_t lcu_rows = ((uint64_t)height + 15u) >> 4;
    uint64_t block64_count =
        (((uint64_t)width + 63u) >> 6) *
        (((uint64_t)height + 63u) >> 6);
    uint64_t pcm_vcl_size = block64_count * 1792u;
    uint64_t geometry_size =
        pcm_vcl_size + (lcu_rows + 1u) * 0x200u;
    uint64_t rate_size;

    /*
     * T31 OEM GetStreamBufPoolConfig sizes an AVC stream buffer as the
     * greater of:
     *
     *   AL_GetMitigatedMaxNalSize(NV12)
     *   ceil(1.5 * configured_bitrate / 8)
     *       + (ceil(height / 16) + 1) * 0x200
     *
     * and rounds the result to 32 bytes.  GetPcmVclNalSize's NV12/8-bit
     * coefficient is 1792 bytes per 64x64 block.  OEM
     * GetStreamBufPoolConfig applies the 1.5 multiplier to the target
     * bitrate before converting bits to bytes.  For the observed
     * 640x360 FixQP profile, whose codec default is 700kbps, this produces
     * the exact OEM request 0x230c0 without assigning behavior to a
     * particular stream or sensor.
     */
    if (bitrate == 0u)
        bitrate = 2000000u;
    rate_size =
        (((uint64_t)bitrate * 3u) + 15u) / 16u +
        (lcu_rows + 1u) * 0x200u;
    picture_bytes = rate_size > geometry_size ? rate_size : geometry_size;
    aligned = (picture_bytes + 0x1fu) & ~0x1full;
#else
    /*
     * Other generations retain the conservative one-byte-per-pixel sizing.
     */
    (void)bitrate;
    if (picture_bytes < 0x20000u)
        picture_bytes = 0x20000u;
    aligned = (picture_bytes + 0xfffu) & ~0xfffull;
#endif
    if (aligned > (uint64_t)(INT_MAX & ~0x1f))
        aligned = (uint64_t)(INT_MAX & ~0x1f);
    return (uint32_t)aligned;
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

static size_t avpu_get_enc1_map_storage_size(uint32_t width, uint32_t height)
{
    size_t luma_map_size = avpu_get_enc1_map_region_size(
        width, avpu_align_up_u32(height, 64u));

    /*
     * Encoder FBC reference storage carries a half-sized chroma map after the
     * luma map.  Reference surfaces and their maps use the same 64-line
     * storage height even when the visible height only needs 16-line command
     * geometry.  The combined allocation is rounded to the DMA manager's
     * 256-byte boundary.
     */
    return (luma_map_size + (luma_map_size >> 1) + 0xffu) & ~0xffu;
}

static size_t avpu_get_enc1_mv_region_size(uint32_t width, uint32_t height)
{
    uint32_t lcu_w = (width + 15u) >> 4;
    uint32_t lcu_h = (height + 15u) >> 4;
    size_t lcu_count = (size_t)lcu_w * (size_t)lcu_h;

    /* OEM AL_GetAllocSize_MV(width, height, log2MaxCuSize=4, codec=AVC) for the
     * current AVC/NV12 8-bit path reduces to:
     *   ((2 * GetBlk16x16(width, height) + 0x10) << 4)
     * where GetBlk16x16 = ceil(width/16) * ceil(height/16).
     * The multiplier is one for HEVC and two for AVC.  Omitting it allocated
     * only half of the motion-vector tail and let the AVPU overwrite the next
     * DMA object on every P picture. */
    return (2u * lcu_count + 0x10u) << 4;
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

static uint32_t avpu_get_stream_window_budget(const ALAvpuContext *ctx,
                                              uint32_t stream_part_offset,
                                              uint32_t stream_offset)
{
    uint32_t comp_data_sz;
    uint32_t stream_space = 0u;

    if (!ctx)
        return 0u;

    comp_data_sz = (uint32_t)avpu_get_enc1_comp_data_size(
        ctx->enc_w, ctx->enc_h, ctx->format_word);
    if (stream_part_offset > stream_offset)
        stream_space = stream_part_offset - stream_offset;
    if (stream_space < comp_data_sz)
        comp_data_sz = stream_space;

    return comp_data_sz & ~0x1fu;
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
    uint32_t lcu_rows;

    (void)width;
    lcu_rows = (height + 15u) >> 4;

    /*
     * The AVC configuration used by Raptor has one slice and one WPP
     * partition.  AL_GetAllocSize_WPP therefore reduces to one 32-bit state
     * entry per configured LCU row, rounded to the hardware's 128-byte
     * boundary.  The old implementation accidentally multiplied by the
     * picture width and LCU size, inflating this region by hundreds of KiB.
     */
    return avpu_align_up_u32(lcu_rows * sizeof(uint32_t), 128u);
}

static size_t avpu_get_enc1_frame_buf_size(uint32_t width, uint32_t height)
{
    size_t ref_sz = avpu_get_enc1_ref_region_size(width, height);
    size_t map_storage_sz = avpu_get_enc1_map_storage_size(width, height);
    size_t mv_sz = avpu_get_enc1_mv_region_size(width, height);
    size_t total = ref_sz + map_storage_sz + mv_sz;
    size_t nv12_sz = avpu_get_nv12_frame_size(width, height);

    if (total < nv12_sz)
        total = nv12_sz;

    /* Do not page-align this allocation.  The OEM ref manager places the MV
     * allocation immediately after the combined luma/chroma FBC maps. */
    return total;
}

#if defined(PLATFORM_T40) || defined(PLATFORM_T31)
#if defined(PLATFORM_T41)
#define AVPU_T40_EP3_SLOT_SIZE OPENIMP_T41_EP3_SLOT_STRIDE
#define AVPU_T40_EP3_DATA_SIZE OPENIMP_T41_EP3_PER_CORE_SIZE
#define AVPU_T40_EP3_SLOT_COUNT OPENIMP_T41_EP3_SLOT_COUNT
#else
#define AVPU_T40_EP3_SLOT_SIZE 0x1500u
#define AVPU_T40_EP3_DATA_SIZE 0x14a0u
#define AVPU_T40_EP3_SLOT_COUNT 3u
#endif

static const uint32_t avpu_t40_ep3_qp_words[12][3] = {
    { 0x0c090706u, 0x08060504u, 0x130e0a08u },
    { 0x08060504u, 0x06050403u, 0x0e0a0806u },
    { 0x06050403u, 0x05040302u, 0x0a070504u },
    { 0x04030202u, 0x04030201u, 0x07050403u },
    { 0x03020101u, 0x03020101u, 0x05040302u },
    { 0x02010101u, 0x02010101u, 0x03020101u },
    { 0x00000000u, 0x01010101u, 0x00000000u },
    { 0xffffffffu, 0x00000000u, 0x00000000u },
    { 0xfdfeffffu, 0x00000000u, 0x00000000u },
    { 0xfcfdfefeu, 0x00000000u, 0x00000000u },
    { 0xfafbfcfdu, 0x00000000u, 0x00000000u },
    { 0xf8fafbfcu, 0x00000000u, 0x00000000u },
};

static const uint32_t avpu_t40_ep3_lambda_words[103] = {
    0x5b, 0x66, 0x72, 0x80, 0x90, 0xa1, 0xb5, 0xcb, 0xe4, 0x100, 0x11f,
    0x143, 0x16a, 0x196, 0x1c8, 0x200, 0x23f, 0x285, 0x2d4, 0x32d, 0x390,
    0x400, 0x47d, 0x50a, 0x5a8, 0x659, 0x721, 0x800, 0x8fb, 0xa14, 0xb50,
    0xcb3, 0xe41, 0x1000, 0x11f6, 0x1429, 0x16a1, 0x1966, 0x1c82, 0x2000,
    0x23eb, 0x2851, 0x2d41, 0x32cc, 0x3904, 0x4000, 0x47d6, 0x50a3,
    0x5a82, 0x6598, 0x7209, 0x8000, 0x8fad, 0xa145, 0xb505, 0xcb30,
    0xe412, 0x10000, 0x11f5a, 0x1428a, 0x16a0a, 0x19660, 0x1c824,
    0x20000, 0x23eb3, 0x28514, 0x2d414, 0x32cc0, 0x39048, 0x40000,
    0x47d67, 0x50a29, 0x5a828, 0x65980, 0x72090, 0x80000, 0x8facd,
    0xa1451, 0xb504f, 0xcb2ff, 0xe411f, 0x100000, 0x11f59b, 0x1428a3,
    0x16a09e, 0x1965ff, 0x1c823e, 0x200000, 0x23eb36, 0x285146,
    0x2d413d, 0x32cbfd, 0x39047c, 0x400000, 0x47d66b, 0x50a28c,
    0x5a827a, 0x6597fb, 0x7208f8, 0x800000, 0x8facd6, 0xa14518,
    0xb504f3,
};

static uint32_t avpu_t40_ep3_target(uint32_t bitrate, unsigned int slot)
{
    uint64_t target = bitrate;

    /*
     * GetTargetSize() assigns the three hardware RC tables to the GOP's
     * temporal layers.  T31 allocates the same three 0x14a0-byte EP3 banks as
     * T40; its single-reference AVC scheduler submits bank 1 for P and bank 2
     * for IDR.  A live T31 3-Mbit/s dump recovers 3,000,000 * 5 / 7 in bank 1
     * and 3,000,000 in bank 2 before the common 95-percent preprocessing.
     * Preserve the integer-operation order because its rounding is observable
     * in the initialized DMA table.
     */
    if (slot < 2u)
        target = target * 5u / 7u;
    if (slot == 0u)
        target = target * 10u / 13u;
    return (uint32_t)target;
}

static size_t avpu_t40_init_ep3_ring(ALAvpuContext *ctx)
{
    static const uint8_t row_percent[11] = {
        150u, 130u, 120u, 110u, 105u, 100u, 95u, 90u, 80u, 70u, 50u,
    };
    uint8_t *base;
    uint32_t bitrate;
    unsigned int slot;

    if (!ctx || !ctx->rec_trace_buf.map ||
        ctx->rec_trace_buf.size <
            AVPU_T40_EP3_SLOT_SIZE * AVPU_T40_EP3_SLOT_COUNT)
        return 0u;

    base = (uint8_t *)ctx->rec_trace_buf.map;
    bitrate = ctx->bitrate ? ctx->bitrate : 2000000u;
    memset(base, 0,
           AVPU_T40_EP3_SLOT_SIZE * AVPU_T40_EP3_SLOT_COUNT);

    for (slot = 0u; slot < AVPU_T40_EP3_SLOT_COUNT; ++slot) {
        uint32_t *words =
            (uint32_t *)(base + slot * AVPU_T40_EP3_SLOT_SIZE);
        uint32_t target = avpu_t40_ep3_target(bitrate, slot);
        uint32_t base_target = (uint32_t)((uint64_t)target * 95u / 100u);
        unsigned int row;

        for (row = 0u; row < 11u; ++row) {
            uint32_t *record = words + row * 8u;

            record[0] =
                (uint32_t)((uint64_t)base_target * row_percent[row] / 100u);
            record[1] = avpu_t40_ep3_qp_words[row][0];
            record[3] = avpu_t40_ep3_qp_words[row][1];
            record[5] = avpu_t40_ep3_qp_words[row][2];
        }
        words[11u * 8u] = 1u;
        words[11u * 8u + 1u] = avpu_t40_ep3_qp_words[11][0];
        memcpy(words + 16u * 8u, avpu_t40_ep3_lambda_words,
               sizeof(avpu_t40_ep3_lambda_words));
        LOG_CODEC("AVPU: initialized EP3 slot[%u] bitrate=%u target=%u",
                  slot, bitrate, target);
    }

    return AVPU_T40_EP3_DATA_SIZE * AVPU_T40_EP3_SLOT_COUNT;
}

static int avpu_t40_init_ep2(ALAvpuContext *ctx)
{
    static const uint32_t seed_words[4] = {
        0x03e809c4u,
        0x13880100u,
        0x23281b58u,
        0x010a0301u,
    };
    uint8_t *destination;
    uint32_t bounds_word;
    size_t offset;

    if (!ctx || !ctx->interm_buf.map)
        return -1;

    offset = (size_t)ctx->interm_ep1_size +
             (size_t)ctx->interm_wpp_size;
#if defined(PLATFORM_T41)
    /* The T41 intermediary manager reserves 0x100 bytes between the WPP
     * state and EP2.  Both captured channels pass the post-gap address. */
    offset += 0x100u;
#endif
    if (offset + sizeof(seed_words) + sizeof(bounds_word) >
        ctx->interm_buf.size)
        return -1;

    destination = (uint8_t *)ctx->interm_buf.map + offset;
    memcpy(destination, seed_words, sizeof(seed_words));
#if defined(PLATFORM_T31)
    if (ctx->rc_mode == HW_RC_MODE_FIXQP) {
        /*
         * T31 disables HWRC for FixQP and leaves EP2's legal QP range at the
         * OEM defaults (0..51).  Pinning both fields to the configured QP is
         * a T40-shaped interpretation that changes the IDR transform state.
         */
        bounds_word = 0x33000302u;
    } else
#endif
    {
        bounds_word = ((ctx->max_qp & 0xffu) << 24) |
                      ((ctx->min_qp & 0xffu) << 16) |
                      0x00000302u;
    }
    memcpy(destination + sizeof(seed_words), &bounds_word,
           sizeof(bounds_word));
    return 0;
}
#endif

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

static uint32_t avpu_default_enc1_cmd0a_74(uint32_t enc_w)
{
    /* Stock traces show cmd[0x0a] low16 = 0x0c80 = 3200 for BOTH 640x360
     * and 1920x1080. The previous formula (enc_w * 5) was a coincidence for
     * 640 (640*5=3200) but produced 9600 for 1920 — wrong.
     * Until SliceParam+0x74's OEM producer is fully recovered, use the
     * constant 3200 which matches both known stock resolutions. */
    (void)enc_w;
    return 0x0c80u;
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
    uint32_t fallback_74;

    if (!ctx)
        return;

    fallback_74 = avpu_default_enc1_cmd0a_74(ctx->enc_w);

    ctx->enc1_cmd_0a_74 = codec_param ? *(const uint32_t*)(codec_param + 0x74) : 0u;
    if (ctx->enc1_cmd_0a_74 == 0u || ctx->enc1_cmd_0a_74 > 0xffffu)
        ctx->enc1_cmd_0a_74 = fallback_74;

    ctx->enc1_cmd_0b_7a = codec_param ? *(const uint16_t*)(codec_param + 0x7a) : 0u;
    /* OEM sets SliceParam+0x7a = (width + 7) >> 3 at encode time.
     * The codec_param+0x7a offset is NOT the same field — it may be zero or
     * stale.  Compute from the actual encoder width to match OEM parity.
     * CRITICAL: The previous default of 0x3e8 (1000) made the AVPU think the
     * frame was 8000 pixels wide, causing it to read past the source frame
     * buffer and stall the AXI bus → permanent core_status=0x3 hang. */
    if (ctx->enc1_cmd_0b_7a == 0u || ctx->enc1_cmd_0b_7a > ((ctx->enc_w + 7u) >> 3))
        ctx->enc1_cmd_0b_7a = (ctx->enc_w + 7u) >> 3;

    /* OEM sets SliceParam+0x7c to lcu_w for ALL frames (including IDR).
     * Stock 640x360 IDR: cmd[0x0b]=0x00027000 → bits[21:12]=(40-1)=39 → slice_7c=40=lcu_w.
     * Previously seeding 0 produced a 0x3FF sentinel that confused Enc1. */
    ctx->enc1_cmd_0b_7c = (ctx->enc_w + 15u) >> 4;  /* = lcu_w */
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
    ctx->enc1_cmd_6f_94 = codec_param ? *(const uint16_t*)(codec_param + 0x94) : 0u;

    {
        static unsigned int seed_log_count;
        unsigned int count = __sync_add_and_fetch(&seed_log_count, 1);
        if (count <= 4 || (count % 1000) == 0)
            LOG_CODEC("AVPU: OEM Enc1 words seed 0x74=0x%08x 0x7a=0x%03x 0x7c=%u 0x7e=%u 0x10=%u 0xa8=0x%03x 0xaa=0x%03x 0xac=%u 0x19=0x%08x 0x1a=0x%08x 0x60=0x%08x 0x61=0x%08x 0x6e=0x%08x 0x6f=0x%08x [#%u]",
                      ctx->enc1_cmd_0a_74, ctx->enc1_cmd_0b_7a, ctx->enc1_cmd_0b_7c,
                      ctx->enc1_cmd_0b_7e, ctx->enc1_slice_10,
                      ctx->enc1_cmd_12_a8, ctx->enc1_cmd_12_aa,
                      ctx->enc1_cmd_12_ac,
                      avpu_pack_enc1_cmd19(ctx),
                      avpu_pack_enc1_cmd1a(ctx),
                      ctx->enc1_cmd_60_110_112,
                      ctx->enc1_cmd_61_114_116,
                      ctx->enc1_cmd_6e_118_11a,
                      ctx->enc1_cmd_6f_94, count);
    }
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

static uint32_t avpu_get_hw_hdr_offset(uint32_t hdr_offset)
{
    /* Stock keeps the exact byte count in the CL (cmd[0x32]/cmd[0x3e]) but
     * writes the 0x8424/0x8428 register pair with a 32-byte aligned-down view
     * of that offset: e.g. CL=0x220 while WR 0x8424=0x200.
     *
     * IMPORTANT: register 0x8424 must be <= cmd[0x32]. If 0x8424 > cmd[0x32],
     * the AVPU won't write encoded data below the register value, producing
     * zero output. Stock has large headers (0x220) so 0x8424=0x200 works.
     * Our headers are much shorter, so rounding 47 down to 32 invents a gap
     * that does not exist in the stream buffer layout. Keep the stock aligned
     * behavior for larger headers, but preserve the exact offset for small
     * headers so the source-config window starts where our prewritten AU ends. */
    if (hdr_offset < 64u)
        return hdr_offset;
    return avpu_align_down_u32(hdr_offset, 32u);
}

static uint32_t avpu_default_enc2_slice78(uint32_t enc_h)
{
    uint32_t lcu_h;
    uint32_t row_groups;

    if (enc_h == 0u)
        return 7u;

    /*
     * Scale the entropy row-group count from the configured LCU height.
     * This preserves the recovered range while avoiding resolution-specific
     * lookups, so intermediate dimensions follow the same continuous rule.
     */
    lcu_h = (enc_h + 15u) >> 4;
    row_groups = 7u;
    if (lcu_h > 23u)
        row_groups += ((lcu_h - 23u) * 3u + 22u) / 45u;
    if (row_groups > 10u)
        row_groups = 10u;
    return row_groups;
}

static uint32_t avpu_pack_enc2_cmd1b(const ALAvpuContext *ctx)
{
    uint32_t slice_74;
    uint32_t slice_78;
    uint32_t slice_19 = 0u;
    uint32_t slice_1a = 0u;

    if (!ctx)
        return 0u;

    slice_74 = ctx->enc1_cmd_0a_74 ? ctx->enc1_cmd_0a_74
                                   : avpu_default_enc1_cmd0a_74(ctx->enc_w);
    slice_78 = avpu_default_enc2_slice78(ctx->enc_h);

    return (slice_74 & 0x1fffu)
        | ((slice_78 & 0x3ffu) << 16)
        | ((slice_19 & 0x3u) << 28)
        | ((slice_1a & 0x3u) << 30);
}

static uint32_t avpu_pack_enc2_cmd1c(const ALAvpuContext *ctx, int is_idr)
{
    uint32_t qp;
    uint32_t slice_7e;
    uint32_t slice_10;
    uint32_t slice_11;
    uint32_t slice_type;
    uint32_t slice_f6 = 1u;
    uint32_t slice_66 = 0u;
    uint32_t slice_12 = 1u;
    uint32_t slice_1c = 1u;
    uint32_t slice_30;
    uint32_t low_type_flag;

    if (!ctx)
        return 0u;

    qp = ctx->qp ? ctx->qp : 30u;
    slice_7e = ctx->enc1_cmd_0b_7e ? ctx->enc1_cmd_0b_7e : 1u;
    slice_10 = ctx->enc1_slice_10 ? (ctx->enc1_slice_10 & 0x3u) : 1u; /* stock 640x360 has bit8=1 */
    slice_11 = ctx->entropy_mode & 1u;
    /* OEM WriteAvcSliceSegmentHdr emits base AVC slice types:
     *   P -> 0
     *   I/IDR -> 2
     * SliceParamToCmdRegsEnc2 bit1 is NOT a CABAC flag; it packs
     * `(((slice_type < 3) ? 1 : 0) ^ 1)`, which is 0 for both of the
     * current single-slice AVC paths. */
    slice_type = is_idr ? 2u : 0u;
    low_type_flag = (((slice_type < 3u) ? 1u : 0u) ^ 1u) & 1u;
    /* OEM FillSliceParamFromPicParam seeds SliceParam+0x30 from the picture
     * type input and special-cases value 7 -> 1 for the IDR path. The stock
     * 640x360 P-frame word 0x21230904 decodes to +0x30 = 2. */
    slice_30 = is_idr ? 1u : 2u;

    /* OEM SliceParamToCmdRegsEnc2 packs:
     *   bit1  <- (((slice_type < 3) ? 1 : 0) ^ 1)
     *   bit2  <- +0xf6
     *   bit3  <- +0x66
     *   bits5:4 <- (+0x7e - 1)
     *   bits9:8 <- +0x10
     *   bit10 <- +0x11
     *   bit11 <- +0x12
     *   bits21:16 <- +0x28
     *   bits25:24 <- +0x1c
     *   bits29:28 <- +0x30
     *
     * The unresolved late producers are still +0xf6/+0x66/+0x12/+0x1c, but
     * bit1 and +0x30 are now driven from OEM-recovered slice-type inputs
     * instead of guessed entropy logic.
     *
     * Additional OEM clue: AL_ApplyPictCommands copies picture-command payload
     * bytes at offsets 0x66/0x67 into encoder runtime state when the 0x400
     * command flag is present. That makes SliceParam+0x66 a picture-command
     * driven runtime field, not a static entropy/profile toggle. */
    return (low_type_flag << 1)
        | ((slice_f6 & 0x1u) << 2)
        | ((slice_66 & 0x1u) << 3)
        | ((((slice_7e - 1u)) & 0x3u) << 4)
        | (slice_10 << 8)
        | (slice_11 << 10)
        | ((slice_12 & 0x1u) << 11)
        | ((qp & 0x3fu) << 16)
        | ((slice_1c & 0x3u) << 24)
        | ((slice_30 & 0x3u) << 28);
}

static uint32_t avpu_pack_enc2_cmd1d(const ALAvpuContext *ctx)
{
    uint32_t pic_w_8;
    uint32_t lcu_w;
    uint32_t lcu_width_from_pic;
    uint32_t slice_start_lcu = 0u;
    uint32_t slice_row_group = 0u;
    uint32_t slice_col_group = 0u;

    if (!ctx)
        return 0u;

    pic_w_8 = ctx->enc1_cmd_0b_7a ? ctx->enc1_cmd_0b_7a : ((ctx->enc_w + 7u) >> 3);
    lcu_w = (ctx->enc_w + 15u) >> 4;
    lcu_width_from_pic = (pic_w_8 + 1u) >> 1;
    if (lcu_width_from_pic == 0u)
        lcu_width_from_pic = lcu_w;
    if (lcu_width_from_pic == 0u)
        return 0u;

    return (slice_start_lcu & 0x3ffu)
        | (((lcu_width_from_pic - 1u) & 0x3ffu) << 12)
        | ((slice_row_group & 0xfu) << 24)
        | ((slice_col_group & 0xfu) << 28);
}

static uint32_t avpu_pack_enc2_cmd1e(const ALAvpuContext *ctx, uint32_t slice_fc)
{
    uint32_t slice_f8;

    if (!ctx)
        return 0u;

    /* OEM SliceParamToCmdRegsEnc2 packs cmd[0x1e] low20 from SliceParam+0xfc,
     * and UpdateCommand writes +0xfc from the aligned entropy-stream budget.
     * In encode1() this is the same runtime value later stored at CL+0xfc
     * before SliceParamToCmdRegsEnc2() is called. */
    slice_f8 = ctx->slice_header_prefix_bits & 0x1fu;

    return (slice_fc & 0x000fffffu) | (slice_f8 << 24);
}

static uint32_t avpu_pack_enc2_cmd1f(const ALAvpuContext *ctx)
{
    return ctx ? ctx->slice_header_splice_word : 0u;
}

static uint32_t avpu_get_slice_prefix_bits(uint32_t slice_bits)
{
    uint32_t aligned_bits;

    if (slice_bits == 0u)
        return 0u;

    aligned_bits = slice_bits & ~7u;
    if (aligned_bits >= 24u)
        return 16u + (slice_bits & 7u);

    return slice_bits & 0xffu;
}

static uint32_t avpu_get_slice_splice_word(const uint8_t *buf,
                                           uint32_t slice_end,
                                           uint32_t slice_bits)
{
    uint32_t rem_bits;
    uint32_t shift;
    uint32_t b0 = 0u;
    uint32_t b1 = 0u;
    uint32_t b2 = 0u;
    uint32_t b3 = 0u;

    if (!buf || slice_end == 0u)
        return 0u;

    if (slice_end >= 4u) {
        b0 = buf[slice_end - 4u];
        b1 = buf[slice_end - 3u];
        b2 = buf[slice_end - 2u];
        b3 = buf[slice_end - 1u];
    } else {
        if (slice_end > 0u)
            b3 = buf[slice_end - 1u];
        if (slice_end > 1u)
            b2 = buf[slice_end - 2u];
        if (slice_end > 2u)
            b1 = buf[slice_end - 3u];
    }

    rem_bits = slice_bits & 7u;
    shift = (8u - rem_bits) & 31u;
    return ((b0 << 24) | (b1 << 16) | (b2 << 8) | b3) >> shift;
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

#if defined(PLATFORM_T40) || defined(PLATFORM_T31)
    {
        uint32_t mb_w = (ctx->enc_w + 15u) >> 4;
        uint32_t mb_h = (ctx->enc_h + 15u) >> 4;
        uint32_t crop_r = ((mb_w << 4) - ctx->enc_w) >> 1;
        uint32_t crop_b = ((mb_h << 4) - ctx->enc_h) >> 1;
        uint32_t fps_num = ctx->fps_num ? ctx->fps_num : 25u;
        uint32_t fps_den = ctx->fps_den ? ctx->fps_den : 1u;
        uint32_t bitrate = ctx->bitrate ? ctx->bitrate : 2000000u;
        uint64_t cpb_size = (uint64_t)bitrate * 3u;
        uint32_t bit_rate_value_minus1 =
            (uint32_t)(((uint64_t)bitrate + 63u) / 64u - 1u);
        uint32_t cpb_size_value_minus1 =
            (uint32_t)((cpb_size + 63u) / 64u - 1u);
        unsigned int i;

        /*
         * Generate the stock T-series High-profile syntax from the active
         * channel.  T31 and T40 use this same AVC syntax even though their
         * command-list layouts differ.  The previous implementation selected
         * captured byte arrays for exactly 640x360 or 1920x1080, which made
         * SPS dimensions, cropping, timing and HRD silently disagree with any
         * other config.
         */
        bs_write_bits(rbsp, &bp, 100u, 8); /* High */
        bs_write_bits(rbsp, &bp, 0u, 8);   /* constraint flags */
        bs_write_bits(rbsp, &bp, 51u, 8);  /* level 5.1 */
        bs_write_ue(rbsp, &bp, 0u);        /* sps id */
        bs_write_ue(rbsp, &bp, 1u);        /* 4:2:0 */
        bs_write_ue(rbsp, &bp, 0u);        /* 8-bit luma */
        bs_write_ue(rbsp, &bp, 0u);        /* 8-bit chroma */
        bs_write_bit(rbsp, &bp, 0);        /* transform bypass */
        bs_write_bit(rbsp, &bp, 1);        /* scaling matrix present */
        for (i = 0; i < 8u; ++i)
            bs_write_bit(rbsp, &bp, 0);    /* High-profile defaults */

        bs_write_ue(rbsp, &bp, 0u);        /* log2 max frame num - 4 */
        bs_write_ue(rbsp, &bp, 0u);        /* POC type */
        bs_write_ue(rbsp, &bp, 6u);        /* 10-bit POC */
        bs_write_ue(rbsp, &bp, 1u);        /* one reference */
        bs_write_bit(rbsp, &bp, 0);        /* no frame-num gaps */
        bs_write_ue(rbsp, &bp, mb_w - 1u);
        bs_write_ue(rbsp, &bp, mb_h - 1u);
        bs_write_bit(rbsp, &bp, 1);        /* frame_mbs_only */
        bs_write_bit(rbsp, &bp, 1);        /* direct_8x8 inference */

        bs_write_bit(rbsp, &bp, crop_r != 0u || crop_b != 0u);
        if (crop_r != 0u || crop_b != 0u) {
            bs_write_ue(rbsp, &bp, 0u);
            bs_write_ue(rbsp, &bp, crop_r);
            bs_write_ue(rbsp, &bp, 0u);
            bs_write_ue(rbsp, &bp, crop_b);
        }

        /* VUI/HRD is derived from the configured frame rate and bitrate. */
        bs_write_bit(rbsp, &bp, 1);        /* VUI present */
        bs_write_bit(rbsp, &bp, 0);        /* no aspect-ratio override */
        bs_write_bit(rbsp, &bp, 0);        /* no overscan */
        bs_write_bit(rbsp, &bp, 1);        /* video signal present */
        bs_write_bits(rbsp, &bp, 5u, 3);   /* unspecified video format */
        bs_write_bit(rbsp, &bp, 0);        /* limited range */
        bs_write_bit(rbsp, &bp, 1);        /* colour description present */
        bs_write_bits(rbsp, &bp, 1u, 8);   /* BT.709 primaries */
        bs_write_bits(rbsp, &bp, 1u, 8);   /* BT.709 transfer */
        bs_write_bits(rbsp, &bp, 1u, 8);   /* BT.709 matrix */
        bs_write_bit(rbsp, &bp, 1);        /* chroma location present */
        bs_write_ue(rbsp, &bp, 0u);
        bs_write_ue(rbsp, &bp, 0u);
        bs_write_bit(rbsp, &bp, 1);        /* timing present */
        bs_write_bits(rbsp, &bp, fps_den, 32);
        bs_write_bits(rbsp, &bp, fps_num * 2u, 32);
        bs_write_bit(rbsp, &bp, 0);        /* variable cadence permitted */
        bs_write_bit(rbsp, &bp, 0);        /* no NAL HRD */
        bs_write_bit(rbsp, &bp, 1);        /* VCL HRD */
        bs_write_ue(rbsp, &bp, 0u);        /* one CPB */
        bs_write_bits(rbsp, &bp, 0u, 4);   /* bitrate scale: 64 bps */
        bs_write_bits(rbsp, &bp, 2u, 4);   /* CPB scale: 64 bits */
        bs_write_ue(rbsp, &bp, bit_rate_value_minus1);
        bs_write_ue(rbsp, &bp, cpb_size_value_minus1);
        bs_write_bit(rbsp, &bp, ctx->rc_mode == HW_RC_MODE_CBR);
        bs_write_bits(rbsp, &bp, 31u, 5);
        bs_write_bits(rbsp, &bp, 31u, 5);
        bs_write_bits(rbsp, &bp, 31u, 5);
        bs_write_bits(rbsp, &bp, 0u, 5);
        bs_write_bit(rbsp, &bp, 0);        /* low-delay HRD */
        bs_write_bit(rbsp, &bp, 1);        /* pic_struct present */
        bs_write_bit(rbsp, &bp, 0);        /* no bitstream restriction */
        bs_trailing_bits(rbsp, &bp);
        return bp / 8;
    }
#endif

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
#if defined(PLATFORM_T40) || defined(PLATFORM_T31)
        /*
         * The recovered T31 Enc1 command uses the same High-profile transform
         * path as T40.  Advertising transform_8x8_mode=0 makes a decoder
         * interpret the first intra macroblock with the wrong syntax.
         */
        bs_write_bit(rbsp, &bp, 1); /* stock High path uses 8x8 transform */
#else
        bs_write_bit(rbsp, &bp, 0); /* transform_8x8_mode */
#endif
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

#if defined(PLATFORM_T40) || defined(PLATFORM_T31)
static void avpu_stage_stream_prefix(uint8_t *stream, uint32_t prefix_size)
{
    const uint32_t payload_offset = 0x220u;

    if (!stream || prefix_size == 0u || prefix_size > payload_offset)
        return;

    /*
     * The T31 and T40 Enc2 oracles keep the slice prefix right-aligned
     * immediately before the fixed +0x220 entropy-payload boundary.  Enc2
     * uses those preceding bytes while splicing the byte-aligned CABAC
     * payload.  Keep the ordinary copy at stream[0] for the final Annex-B
     * access unit, and mirror it here for the hardware submit.  Completion
     * compacts only the payload back behind stream[0]'s prefix.
     */
    memcpy(stream + payload_offset - prefix_size, stream, prefix_size);
}
#endif

/* Generate slice header RBSP (without macroblock data or trailing bits).
 * The AVPU writes the entropy-coded macroblock data after this.
 * Returns RBSP byte count.  The caller wraps this in a NAL via avpu_write_nal_epb.
 *
 * IMPORTANT: The last byte may carry sub-byte alignment.  We byte-align with
 * trailing bits here because the OEM GenerateAvcSliceHeader does the same when
 * writing the header for the Enc1-only path (arg6 = 1).  The AVPU's command
 * list offset (cmd[0x32]) is a byte offset, so byte alignment is required. */
static int avpu_generate_slice_header_rbsp(uint8_t *rbsp, const ALAvpuContext *ctx,
                                            int is_idr, uint32_t *slice_bits_out)
{
    int bp = 0;
    uint32_t picture_number;
    memset(rbsp, 0, 64);

    /*
     * frame_num and POC restart at every IDR, including an asynchronous IDR
     * requested by a newly connected RTSP client.  Taking monotonic
     * frame_number modulo the configured GOP only works for periodic IDRs;
     * after a forced IDR it made the first P picture advertise an arbitrary
     * frame_num/POC.  OEM starts that picture at 1/2.
     */
    picture_number = is_idr
        ? 0u
        : ctx->frame_number - ctx->idr_frame_number;

    bs_write_ue(rbsp, &bp, 0); /* first_mb_in_slice = 0 */
    /* OEM WriteAvcSliceSegmentHdr writes AL_AVC_SLICE_TYPE[pSH->slice_type],
     * i.e. the base AVC slice type values, not the "all-*" variants.
     * For the current OpenIMP single-slice path that means:
     *   IDR -> I slice -> 2
     *   P   -> P slice -> 0
     * Using 7/5 ("all-I"/"all-P") inflates the host-written slice header and
     * changes the f8/splice bookkeeping seen by Enc2. */
    bs_write_ue(rbsp, &bp, is_idr ? 2u : 0u);
    bs_write_ue(rbsp, &bp, 0); /* pic_parameter_set_id = 0 */

    /* frame_num: log2_max_frame_num_minus4 = 0 → log2_max = 4 → 4 bits */
    bs_write_bits(rbsp, &bp, picture_number & 0xFu, 4);

    if (is_idr) {
#if defined(PLATFORM_T41)
        bs_write_ue(rbsp, &bp, 1); /* recovered T41 idr_pic_id */
#else
        bs_write_ue(rbsp, &bp, 0); /* idr_pic_id */
#endif
    }

    /* The recovered T40 SPS uses log2_max_pic_order_cnt_lsb_minus4=6,
     * while the older generic/T31 SPS uses the minimum four-bit POC. */
#if defined(PLATFORM_T40) || defined(PLATFORM_T31)
    bs_write_bits(rbsp, &bp, (picture_number * 2u) & 0x3ffu, 10);
#else
    bs_write_bits(rbsp, &bp, (picture_number * 2u) & 0xfu, 4);
#endif

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
    if (ctx->entropy_mode && !is_idr) {
#if defined(PLATFORM_T40) || defined(PLATFORM_T31)
        /*
         * The T31 and T40 High-profile Enc1 paths initialize P-picture
         * contexts from CABAC table 1.
         */
        bs_write_ue(rbsp, &bp, 1u);
#else
        bs_write_ue(rbsp, &bp, 0);
#endif
    }

    /* The captured T40 PPS has pic_init_qp_minus26=0.  The slice QP must
     * match the QP packed into the Enc1/Enc2 command words: the current
     * firmware VBR oracle starts its IDR at QP 25 (delta -1), then changes
     * P-picture QP as rate control evolves.  Hard-coding the older CBR
     * delta +8 made the decoder initialize CABAC for QP 34 while the AVPU
     * encoded the payload at another QP, corrupting the first macroblock. */
#if defined(PLATFORM_T40) || defined(PLATFORM_T31)
    bs_write_se(rbsp, &bp, (int32_t)(ctx->qp ? ctx->qp : 30u) - 26);
#else
    bs_write_se(rbsp, &bp, 0);
#endif

    /* deblocking_filter_control (deblocking enabled): disable_deblocking = 0 */
    bs_write_ue(rbsp, &bp, 0);

#if defined(PLATFORM_T40) || defined(PLATFORM_T31)
    bs_write_se(rbsp, &bp, -1); /* slice_alpha_c0_offset_div2 */
    bs_write_se(rbsp, &bp, -1); /* slice_beta_offset_div2 */
#endif

    if (slice_bits_out)
        *slice_bits_out = (uint32_t)bp;

    /* CABAC slice data must begin on a byte boundary.  The AVC syntax uses
     * cabac_alignment_one_bit (all ones), not rbsp_trailing_bits (one then
     * zeros), because AVPU macroblock data follows this host-written prefix. */
#if defined(PLATFORM_T40) || defined(PLATFORM_T31)
    if (ctx->entropy_mode) {
        while ((bp % 8) != 0)
            bs_write_bit(rbsp, &bp, 1);
    } else {
        bs_trailing_bits(rbsp, &bp);
    }
#else
    bs_trailing_bits(rbsp, &bp);
#endif

    return bp / 8;
}

#if defined(PLATFORM_T40) || defined(PLATFORM_T31)
static uint32_t avpu_t40_picture_qp(const ALAvpuContext *ctx, int is_idr)
{
    uint64_t bits_per_lcu_q16;
    uint64_t denominator;
    uint32_t lcu_count;
    uint32_t integer_log;
    uint32_t fraction_log = 0u;
    uint32_t normalized;
    int32_t log2_q8;
    int32_t qp_q8;
    uint32_t qp;
    unsigned int bit;

    if (!ctx)
        return 30u;

    if (ctx->rc_mode == HW_RC_MODE_FIXQP)
        return ctx->qp <= 51u ? ctx->qp : 26u;

#if defined(PLATFORM_T41)
    /* Header and command generation must observe the same controller QP or
     * CABAC starts with a different context from the encoded macroblocks. */
    if (ctx->t41_rate_controller.initialized)
        return openimp_t41_rate_controller_qp(
            &ctx->t41_rate_controller);
#endif
#if defined(PLATFORM_T31)
    if (ctx->t31_rate_controller.initialized)
        qp = openimp_t31_rate_controller_qp(
            &ctx->t31_rate_controller);
    else
#endif
    {

        lcu_count = ((ctx->enc_w + 15u) >> 4) *
                    ((ctx->enc_h + 15u) >> 4);
        denominator = (uint64_t)(ctx->fps_num ? ctx->fps_num : 25u) *
                      (uint64_t)(lcu_count ? lcu_count : 1u);
        bits_per_lcu_q16 =
            ((uint64_t)(ctx->bitrate ? ctx->bitrate : 2000000u) *
             (uint64_t)(ctx->fps_den ? ctx->fps_den : 1u) << 16) /
            denominator;
        if (bits_per_lcu_q16 == 0u)
            bits_per_lcu_q16 = 1u;

        integer_log = 0u;
        while ((bits_per_lcu_q16 >> integer_log) > 1u)
            ++integer_log;

    /*
     * Compute log2(bits-per-LCU) in Q8 without libm.  Normalize to Q30 and
     * extract eight fractional bits by repeated squaring.  The recovered
     * stock starting-QP curve is the usual six-QP-per-doubling relationship,
     * anchored at QP 57 before applying the configured bounds.
     */
        if (integer_log >= 30u)
            normalized =
                (uint32_t)(bits_per_lcu_q16 >> (integer_log - 30u));
        else
            normalized =
                (uint32_t)(bits_per_lcu_q16 << (30u - integer_log));
        for (bit = 0u; bit < 8u; ++bit) {
            uint64_t square = (uint64_t)normalized * normalized;

            normalized = (uint32_t)(square >> 30);
            if (normalized >= (2u << 30)) {
                normalized >>= 1;
                fraction_log |= 1u << (7u - bit);
            }
        }

        log2_q8 = ((int32_t)integer_log - 16) * 256 +
                  (int32_t)fraction_log;
        qp_q8 = 57 * 256 - 6 * log2_q8;
        qp = qp_q8 > 0 ? (uint32_t)((qp_q8 + 255) >> 8) : 0u;
        if (qp < ctx->min_qp)
            qp = ctx->min_qp;
        if (ctx->max_qp != 0u && qp > ctx->max_qp)
            qp = ctx->max_qp;
        if (qp > 51u)
            qp = 51u;
    }

#if defined(PLATFORM_T31)
    /* T31 defines iIPDelta as I-picture QP relative to the following P
     * picture.  The recovered SDK default is -1.  Keep the adjustment here
     * so the generated slice header and both Enc1 command words use the same
     * effective QP. */
    if (is_idr) {
        int32_t idr_qp = (int32_t)qp + ctx->qp_ip_delta;

        if (idr_qp < (int32_t)ctx->min_qp)
            idr_qp = (int32_t)ctx->min_qp;
        if (ctx->max_qp != 0u && idr_qp > (int32_t)ctx->max_qp)
            idr_qp = (int32_t)ctx->max_qp;
        if (idr_qp < 0)
            idr_qp = 0;
        if (idr_qp > 51)
            idr_qp = 51;
        qp = (uint32_t)idr_qp;
    }
#else
    (void)is_idr;
#endif
    return qp;
}

#if defined(PLATFORM_T31)
static int avpu_t31_prepare_picture(ALAvpuContext *ctx)
{
    OpenIMPT31RateController *controller;
    uint32_t min_qp;
    uint32_t max_qp;
    uint32_t bitrate;
    uint32_t fps_num;
    uint32_t fps_den;
    uint32_t gop_length;
    uint32_t initial_qp;

    if (!ctx)
        return -1;
    controller = &ctx->t31_rate_controller;
    if (ctx->rc_mode != HW_RC_MODE_CBR) {
        controller->initialized = 0;
        return 0;
    }

    min_qp = ctx->min_qp <= 51u ? ctx->min_qp : 0u;
    max_qp = ctx->max_qp <= 51u ? ctx->max_qp : 51u;
    if (min_qp > max_qp) {
        uint32_t swap = min_qp;

        min_qp = max_qp;
        max_qp = swap;
    }
    bitrate = ctx->bitrate ? ctx->bitrate : 2000000u;
    fps_num = ctx->fps_num ? ctx->fps_num : 25u;
    fps_den = ctx->fps_den ? ctx->fps_den : 1u;
    gop_length = ctx->gop_length ? ctx->gop_length : fps_num / fps_den;
    if (gop_length == 0u)
        gop_length = 1u;

    if (!controller->initialized || controller->bitrate != bitrate ||
        controller->fps_num != fps_num || controller->fps_den != fps_den ||
        controller->gop_length != gop_length ||
        controller->min_qp != min_qp || controller->max_qp != max_qp) {
        /* Respect iInitialQP and its public bounds.  Re-deriving a QP from
         * resolution and bitrate here made the long-window controller spend
         * many GOPs undoing a starting value the caller never requested. */
        controller->initialized = 0;
        initial_qp = ctx->qp;
        if (openimp_t31_rate_controller_init(
                controller, bitrate, fps_num, fps_den, gop_length,
                min_qp, max_qp, initial_qp) != 0)
            return -1;
        LOG_CODEC("AVPU: T31 rate controller initialized bitrate=%u fps=%u/%u gop=%u qp=%u bounds=%u/%u",
                  bitrate, fps_num, fps_den, gop_length,
                  controller->current_qp, min_qp, max_qp);
    }
    return 0;
}
#endif

/*
 * Recover the hardware-rate-control column grid selected by InitHwRateCtrl.
 * The low sixteen bits of Enc1 cmd[0x14] are two zero-based factors whose
 * product must equal the configured LCU width:
 *
 *   bits  5:0  = columns_per_group - 1
 *   bits 15:6  = group_count - 1
 *
 * Stock searches down from min(lcu_width - 1, 32) and keeps the smallest
 * divisor which leaves fewer than 64 columns per group and fewer than 1024
 * LCUs in that group shape.  Deriving this from the active dimensions is
 * essential: a grid captured at another resolution can leave the AVPU
 * waiting forever for columns which do not exist.
 */
static void avpu_t40_get_hwrc_grid(uint32_t width, uint32_t height,
                                   uint32_t *group_count_out,
                                   uint32_t *columns_per_group_out)
{
    uint32_t lcu_w = (width + 15u) >> 4;
    uint32_t lcu_h = (height + 15u) >> 4;
    uint32_t group_count = lcu_w;
    uint32_t divisor;

    if (lcu_w == 0u) {
        if (group_count_out)
            *group_count_out = 1u;
        if (columns_per_group_out)
            *columns_per_group_out = 1u;
        return;
    }

    divisor = lcu_w > 32u ? 32u : lcu_w - 1u;
    while (divisor >= 5u) {
        if ((lcu_w % divisor) == 0u) {
            uint32_t columns_per_group = lcu_w / divisor;

            if (columns_per_group < 0x41u &&
                (uint64_t)lcu_h * columns_per_group < 0x400u)
                group_count = divisor;
        }
        --divisor;
    }

    if (group_count_out)
        *group_count_out = group_count;
    if (columns_per_group_out)
        *columns_per_group_out = lcu_w / group_count;
}

static uint32_t avpu_t40_pack_hwrc_grid(uint32_t width, uint32_t height)
{
    uint32_t group_count;
    uint32_t columns_per_group;

    avpu_t40_get_hwrc_grid(width, height, &group_count, &columns_per_group);
    return 0xf4000000u
         | (((group_count - 1u) & 0x3ffu) << 6)
         | ((columns_per_group - 1u) & 0x3fu);
}
#endif

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
    uint32_t slice_bits;
    uint32_t slice_nal_pos;
    uint32_t slice_nal_bytes;

    if (!ctx || buf_idx < 0 || buf_idx >= ctx->stream_bufs_used)
        return 0;
    if (!ctx->stream_bufs[buf_idx].map)
        return 0;

    uint8_t *buf = (uint8_t *)ctx->stream_bufs[buf_idx].map;
    uint32_t pos = 0;
    uint32_t budget = (uint32_t)ctx->stream_buf_size / 4; /* max 25% for headers */

    ctx->slice_header_nal_bytes = 0u;
    ctx->slice_header_prefix_bits = 0u;
    ctx->slice_header_splice_word = 0u;

#if defined(PLATFORM_T40) || defined(PLATFORM_T31)
    ALAvpuContext header_ctx = *ctx;

    header_ctx.qp = avpu_t40_picture_qp(ctx, is_idr);
#if defined(PLATFORM_T31)
    /* avpu_t40_picture_qp() already applied the T31 I/P delta. */
    if (header_ctx.qp < header_ctx.min_qp)
        header_ctx.qp = header_ctx.min_qp;
    if (header_ctx.max_qp != 0u && header_ctx.qp > header_ctx.max_qp)
        header_ctx.qp = header_ctx.max_qp;
#endif
    /*
     * T31/T40 Enc1/Enc2 uses nal_ref_idc=1 and no AUD. Build the prefix from
     * current channel state so the SPS macroblock grid, crop, timing and HRD
     * all follow the configured stream instead of a resolution template.
     */
    if (is_idr) {
        rbsp_len = avpu_generate_sps_rbsp(rbsp, &header_ctx);
        if (rbsp_len <= 0 || pos + (uint32_t)rbsp_len + 16u >= budget)
            return 0u;
        pos += (uint32_t)avpu_write_nal_epb(buf + pos, 0x27u,
                                            rbsp, rbsp_len);
        rbsp_len = avpu_generate_pps_rbsp(rbsp, &header_ctx);
        if (rbsp_len <= 0 || pos + (uint32_t)rbsp_len + 16u >= budget)
            return 0u;
        pos += (uint32_t)avpu_write_nal_epb(buf + pos, 0x28u,
                                            rbsp, rbsp_len);
    }
    slice_bits = 0u;
    rbsp_len = avpu_generate_slice_header_rbsp(rbsp, &header_ctx, is_idr,
                                                &slice_bits);
    if (rbsp_len <= 0 || pos + (uint32_t)rbsp_len + 16u >= budget)
        return 0u;
    pos += (uint32_t)avpu_write_nal_epb(buf + pos,
                                        is_idr ? 0x25u : 0x21u,
                                        rbsp, rbsp_len);
    ctx->slice_header_nal_bytes = is_idr ? 10u : pos;
    ctx->slice_header_prefix_bits = 8u;
    ctx->slice_header_splice_word = 0u;
    ctx->stream_header_offset = pos;
    ctx->stream_header_offset_by_buf[buf_idx] = pos;
#if defined(PLATFORM_T31)
    if (ctx->stream_header_shadow &&
        buf_idx >= 0 && buf_idx < 16 &&
        pos <= AVPU_T31_STREAM_PREFIX_BYTES)
        memcpy(ctx->stream_header_shadow +
                   (size_t)buf_idx * AVPU_T31_STREAM_PREFIX_BYTES,
               buf, pos);
#endif
    avpu_stage_stream_prefix(buf, pos);
    if (ctx->frame_number % 50u == 0u)
        LOG_CODEC("AVPU: prewrite headers buf[%d] %s pos=%u slice_nal=%u slice_bits=%u f8=0x%x splice=0x%08x (%s+slice_hdr) frame=%u",
                  buf_idx, is_idr ? "IDR SPS+PPS" : "P",
                  pos, ctx->slice_header_nal_bytes, slice_bits,
                  ctx->slice_header_prefix_bits,
                  ctx->slice_header_splice_word,
                  is_idr ? "SPS+PPS" : "no-PS", ctx->frame_number);
    return pos;
#endif

    const ALAvpuContext *generic_header_ctx = ctx;

    /* AUD */
    pos += avpu_write_aud_nal(buf + pos, is_idr);

    if (is_idr) {
        /* SPS (NAL type 7, nal_ref_idc=3 → 0x67) */
        rbsp_len = avpu_generate_sps_rbsp(rbsp, generic_header_ctx);
        if (rbsp_len > 0 && pos + (uint32_t)rbsp_len + 16 < budget)
            pos += avpu_write_nal_epb(buf + pos, 0x67, rbsp, rbsp_len);

        /* PPS (NAL type 8, nal_ref_idc=3 → 0x68) */
        rbsp_len = avpu_generate_pps_rbsp(rbsp, generic_header_ctx);
        if (rbsp_len > 0 && pos + (uint32_t)rbsp_len + 16 < budget)
            pos += avpu_write_nal_epb(buf + pos, 0x68, rbsp, rbsp_len);
    }

    /* Slice header NAL (IDR=0x65 nal_ref_idc=3 type=5, P=0x41 nal_ref_idc=2 type=1) */
    slice_nal_pos = pos;
    slice_bits = 0u;
    rbsp_len = avpu_generate_slice_header_rbsp(rbsp, generic_header_ctx,
                                                is_idr, &slice_bits);
    if (rbsp_len > 0 && pos + (uint32_t)rbsp_len + 16 < budget) {
        uint8_t nal_hdr = is_idr ? 0x65 : 0x41;
        pos += avpu_write_nal_epb(buf + pos, nal_hdr, rbsp, rbsp_len);
    }
    slice_nal_bytes = pos - slice_nal_pos;
    ctx->slice_header_nal_bytes = slice_nal_bytes;
    ctx->slice_header_prefix_bits = avpu_get_slice_prefix_bits(slice_bits);
    ctx->slice_header_splice_word = avpu_get_slice_splice_word(buf, pos, slice_bits);
    /* OEM header generation does not synthesize a filler NAL just to reach a
     * captured stock byte count. Keep the real header length here so the AU
     * begins with actual AUD/SPS/PPS/slice bytes rather than type-12 filler. */

    ctx->stream_header_offset = pos;
    if (buf_idx >= 0 && buf_idx < 16)
        ctx->stream_header_offset_by_buf[buf_idx] = pos;
#if defined(PLATFORM_T40) || defined(PLATFORM_T31)
    avpu_stage_stream_prefix(buf, pos);
#endif
    if (ctx->frame_number % 50 == 0)
    LOG_CODEC("AVPU: prewrite headers buf[%d] %s pos=%u slice_nal=%u slice_bits=%u f8=0x%x splice=0x%08x (AUD+%s+slice_hdr) frame=%u",
              buf_idx, is_idr ? "IDR SPS+PPS" : "P",
              pos, slice_nal_bytes, slice_bits,
              ctx->slice_header_prefix_bits, ctx->slice_header_splice_word,
              is_idr ? "SPS+PPS" : "none", ctx->frame_number);

    return pos;
}

static uint32_t avpu_get_enc1_stream_part_offset(const ALAvpuContext *ctx)
{
    uint32_t lcu_h;
    uint32_t stream_part_size;

    if (!ctx || ctx->stream_buf_size <= 0 || ctx->enc_w == 0u || ctx->enc_h == 0u)
        return 0u;

    /* OEM GetStreamBuffers.part.72 reserves a tail stream-part region with:
     *   iStreamPartSize =
     *     align128((max(numSliceRows, ceil(height / 2^log2MaxCu)) *
     *               numCore + 0x10) << 4)
     *   iStreamPartOffset = iMaxSize - iStreamPartSize
     * T31 AVC uses log2MaxCu=4, one slice row, and one core. For the observed
     * 640x360 channel this is 0x280 and, with OEM iMaxSize 0x230c0, yields
     * the exact stock command offset 0x22e40. */
    lcu_h = (ctx->enc_h + 15u) >> 4;
    if (lcu_h == 0u)
        return 0u;

    stream_part_size =
        avpu_align_up_u32((lcu_h + 0x10u) << 4, 128u);
    if (stream_part_size >= (uint32_t)ctx->stream_buf_size)
        return 0u;

    return (uint32_t)ctx->stream_buf_size - stream_part_size;
}

#if defined(PLATFORM_T41)
static int avpu_t41_rate_control_coupling_enabled(void)
{
    const char *value = getenv("OPENIMP_T41_RATE_CONTROL_COUPLING");

    return !value || strcmp(value, "0") != 0;
}

static int avpu_t41_exact_rate_control_enabled(const ALAvpuContext *ctx)
{
    /* The recovered o1II object is the OEM mode-1 CBR controller.  VBR uses
     * a different OEM implementation and must not inherit this state merely
     * because it is also a non-FIXQP mode. */
    return ctx && ctx->rc_mode == HW_RC_MODE_CBR &&
           avpu_t41_rate_control_coupling_enabled();
}

static void avpu_t41_qp_bounds(const ALAvpuContext *ctx,
                               uint32_t *min_qp_out,
                               uint32_t *max_qp_out)
{
    uint32_t min_qp = ctx->min_qp <= 51u ? ctx->min_qp : 0u;
    uint32_t max_qp = ctx->max_qp <= 51u ? ctx->max_qp : 51u;

    if (min_qp > max_qp) {
        uint32_t swap = min_qp;

        min_qp = max_qp;
        max_qp = swap;
    }
    if (min_qp_out)
        *min_qp_out = min_qp;
    if (max_qp_out)
        *max_qp_out = max_qp;
}

static int avpu_t41_prepare_picture(ALAvpuContext *ctx, int is_idr)
{
    void *ep3_cpu;
    uint32_t min_qp;
    uint32_t max_qp;
    uint32_t selected_qp;
    uint32_t bitrate;
    uint32_t fps_num;
    uint32_t fps_den;
    uint32_t gop_length;
    unsigned int ep3_slot = is_idr ? 2u : 1u;
    int interm_flush;
    int ep3_flush;

    if (!ctx || !ctx->interm_buf.map || !ctx->rec_trace_buf.map)
        return -1;
    ep3_cpu = ctx->rec_trace_buf.uncached_map
        ? ctx->rec_trace_buf.uncached_map : ctx->rec_trace_buf.map;

    avpu_t41_qp_bounds(ctx, &min_qp, &max_qp);
    selected_qp = ctx->qp <= 51u ? ctx->qp : 34u;
    if (selected_qp < min_qp)
        selected_qp = min_qp;
    if (selected_qp > max_qp)
        selected_qp = max_qp;

    bitrate = ctx->bitrate ? ctx->bitrate : 2000000u;
    fps_num = ctx->fps_num ? ctx->fps_num : 25u;
    fps_den = ctx->fps_den ? ctx->fps_den : 1u;
    gop_length = ctx->gop_length ? ctx->gop_length : fps_num / fps_den;
    if (!gop_length)
        gop_length = 1u;

    if (avpu_t41_exact_rate_control_enabled(ctx)) {
        OpenIMPT41RateController *controller =
            &ctx->t41_rate_controller;

        if (!controller->initialized ||
            controller->bitrate != bitrate ||
            controller->fps_num != fps_num ||
            controller->fps_den != fps_den ||
            controller->gop_length != gop_length ||
            controller->min_qp != min_qp ||
            controller->max_qp != max_qp) {
            /* Both live T41 controller objects observed in OEM start at 38. */
            if (openimp_t41_rate_controller_init(
                    controller, bitrate, fps_num, fps_den, gop_length,
                    min_qp, max_qp, 38u) != 0)
                return -1;
            LOG_CODEC("AVPU: T41 rate controller initialized bitrate=%u fps=%u/%u gop=%u qp=%u bounds=%u/%u",
                      bitrate, fps_num, fps_den, gop_length,
                      controller->current_qp, min_qp, max_qp);
        }
        selected_qp = openimp_t41_rate_controller_qp(controller);
    } else {
        /* A later CBR transition must reconstruct OEM defaults instead of
         * resuming history accumulated under an earlier channel mode. */
        ctx->t41_rate_controller.initialized = 0;
    }
    ctx->t41_rate_control_qp = selected_qp;

    if (openimp_t41_update_ep1_lambda(
            ctx->interm_buf.map, ctx->interm_buf.size,
            is_idr ? 2u : 1u) != 0)
        return -1;
    if (openimp_t41_hwrc_level_set_buffer(
            &ctx->t41_hwrc_level, ep3_cpu,
            ctx->rec_trace_buf.size, ep3_slot) != 0)
        return -1;

    /* T41's rmem cache API requires the normalized 1 MiB operation. */
    interm_flush = avpu_flush_dma_buf_profiled(
        ctx->fd, "t41_ep1_picture", &ctx->interm_buf,
        ctx->interm_ep1_size, OPENIMP_PROFILE_CACHE_EP1_PUBLISH);
    ep3_flush = ctx->rec_trace_buf.uncached_map
        ? 0
        : avpu_flush_dma_buf_profiled(
            ctx->fd, "t41_ep3_picture", &ctx->rec_trace_buf,
            ctx->rec_trace_buf.size, OPENIMP_PROFILE_CACHE_EP3_PUBLISH);
    if (interm_flush != 0 || ep3_flush != 0) {
        LOG_CODEC("AVPU: T41 picture-state flush failed ep1=%d ep3=%d",
                  interm_flush, ep3_flush);
        return -1;
    }
    return 0;
}

static uint32_t avpu_t41_advance_luma_offset(uint32_t current,
                                             uint32_t luma_size,
                                             uint32_t source_pitch)
{
    uint32_t step;

    if (source_pitch > UINT32_MAX / 48u || luma_size == 0u)
        return 0u;
    step = source_pitch * 48u;
    if (step >= luma_size)
        return 0u;
    if (current >= luma_size)
        current %= luma_size;
    return current >= step ? current - step : current + luma_size - step;
}

static int avpu_t41_fill_command(ALAvpuContext *ctx, void *slot,
                                 int stream_buf_idx, uint32_t src_phys,
                                 int is_idr)
{
    OpenIMPT41CommandParams params;
    uint32_t luma_size;
    uint32_t chroma_size;
    uint32_t map_luma_size;
    uint32_t map_slot_size;
    uint32_t mv_slot_size;
    uint32_t maps_base;
    uint32_t mv_base;
    uint32_t picture_number;
    uint32_t current_slot;
    uint32_t previous_slot;
    uint32_t picture_qp;
    uint32_t rate_control_qp;
    uint32_t min_qp;
    uint32_t max_qp;

    if (!ctx || !slot || stream_buf_idx < 0 ||
        stream_buf_idx >= ctx->stream_bufs_used ||
        !ctx->rec_buf.phy_addr || !ctx->interm_buf.phy_addr ||
        !ctx->rec_trace_buf.phy_addr)
        return -1;

    luma_size = openimp_t41_reconstruction_luma_size(ctx->enc_w,
                                                      ctx->enc_h);
    chroma_size = openimp_t41_reconstruction_chroma_size(ctx->enc_w,
                                                          ctx->enc_h);
    map_luma_size = openimp_t41_reconstruction_map_luma_size(ctx->enc_w,
                                                             ctx->enc_h);
    map_slot_size = openimp_t41_reconstruction_map_slot_size(ctx->enc_w,
                                                             ctx->enc_h);
    mv_slot_size = openimp_t41_motion_vector_slot_size(ctx->enc_w,
                                                       ctx->enc_h);
    if (!luma_size || !chroma_size || !map_luma_size || !map_slot_size ||
        !mv_slot_size)
        return -1;

    maps_base = ctx->rec_buf.phy_addr + luma_size + chroma_size;
    mv_base = maps_base + 2u * map_slot_size + 0x100u;
    picture_number = is_idr
        ? 0u : ctx->frame_number - ctx->idr_frame_number;
    current_slot = picture_number & 1u;
    previous_slot = current_slot ^ 1u;

    avpu_t41_qp_bounds(ctx, &min_qp, &max_qp);
    picture_qp = ctx->qp <= 51u ? ctx->qp : 34u;
    if (picture_qp < min_qp)
        picture_qp = min_qp;
    if (picture_qp > max_qp)
        picture_qp = max_qp;
    rate_control_qp = ctx->t41_rate_control_qp;
    if (ctx->rc_mode == HW_RC_MODE_FIXQP || rate_control_qp < min_qp ||
        rate_control_qp > max_qp)
        rate_control_qp = picture_qp;
    else if (avpu_t41_exact_rate_control_enabled(ctx))
        picture_qp = rate_control_qp;

    memset(&params, 0, sizeof(params));
    params.width = ctx->enc_w;
    params.height = ctx->enc_h;
    params.bitrate = ctx->bitrate ? ctx->bitrate : 2000000u;
    params.fps_num = ctx->fps_num ? ctx->fps_num : 25u;
    params.fps_den = ctx->fps_den ? ctx->fps_den : 1u;
    params.min_qp = min_qp;
    params.picture_qp = picture_qp;
    params.max_qp = max_qp;
    params.rate_control_qp = rate_control_qp;
    params.picture_number = picture_number;
    params.is_idr = is_idr;

    params.source_y = src_phys;
    params.source_uv = src_phys +
        avpu_get_nv12_luma_plane_size(ctx->enc_w, ctx->enc_h);

    params.reference_y = ctx->rec_buf.phy_addr;
    params.reference_uv = ctx->rec_buf.phy_addr + luma_size;
    params.reference_map_luma = maps_base + previous_slot * map_slot_size;
    params.reference_map_chroma = params.reference_map_luma + map_luma_size;
    params.reference_luma_offset = ctx->t41_reference_luma_offset;
    params.reference_chroma_offset = params.reference_luma_offset >> 1;

    params.reconstruction_y = ctx->rec_buf.phy_addr;
    params.reconstruction_uv = ctx->rec_buf.phy_addr + luma_size;
    params.reconstruction_map_luma = maps_base + current_slot * map_slot_size;
    params.reconstruction_map_chroma =
        params.reconstruction_map_luma + map_luma_size;
    params.reconstruction_luma_offset = ctx->frame_number == 0u
        ? 0u : avpu_t41_advance_luma_offset(
            params.reference_luma_offset, luma_size,
            avpu_align_up_u32(ctx->enc_w, 16u));
    params.reconstruction_chroma_offset =
        params.reconstruction_luma_offset >> 1;

    params.stream_buffer = ctx->stream_bufs[stream_buf_idx].phy_addr;
    params.stream_part_offset = avpu_get_enc1_stream_part_offset(ctx);
    params.ep1 = ctx->interm_buf.phy_addr;
    params.ep2 = ctx->interm_buf.phy_addr + ctx->interm_ep1_size +
                 ctx->interm_wpp_size + 0x100u;
    params.mv_previous = mv_base + previous_slot * mv_slot_size;
    params.mv_current = mv_base + current_slot * mv_slot_size;
    params.ep3 = ctx->rec_trace_buf.phy_addr +
                 (is_idr ? 2u : 1u) * AVPU_T40_EP3_SLOT_SIZE;

    if (openimp_t41_build_command(slot, ctx->cl_entry_size, &params) != 0)
        return -1;
    ctx->t41_rate_control_qp = rate_control_qp;
    ctx->t41_rate_control_qp_by_buf[stream_buf_idx] = rate_control_qp;
    ctx->t41_pending_luma_offset = params.reconstruction_luma_offset;

    if (ctx->frame_number < 16u || is_idr) {
        LOG_CODEC("Process: T41 command frame=%u picture=%u buf=%d idr=%d qp=%u rcqp=%u src=%08x/%08x rec=%08x/%08x map=%08x/%08x mv=%08x/%08x offsets=%x/%x stream=%08x part=%x ep=%08x/%08x/%08x",
                  ctx->frame_number, picture_number, stream_buf_idx, is_idr,
                  picture_qp, rate_control_qp,
                  params.source_y, params.source_uv,
                  params.reconstruction_y, params.reconstruction_uv,
                  params.reference_map_luma, params.reconstruction_map_luma,
                  params.mv_previous, params.mv_current,
                  params.reference_luma_offset,
                  params.reconstruction_luma_offset,
                  params.stream_buffer, params.stream_part_offset,
                  params.ep1, params.ep2, params.ep3);
    }
    return 0;
}
#endif


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
                               int stream_buf_idx, uint32_t src_phys, uint32_t hdr_offset,
                               int is_idr, uint32_t ref_phys)
{
    if (!ctx || !cmd) return;

    const uint32_t stream_part_offset = avpu_get_enc1_stream_part_offset(ctx);
    const uint32_t stream_offset = hdr_offset;
    const uint32_t stream_budget = avpu_get_stream_window_budget(ctx, stream_part_offset, stream_offset);
    uint32_t stream_desc_phys = 0u;

    if (stream_buf_idx >= 0 && stream_buf_idx < ctx->stream_bufs_used)
        stream_desc_phys = ctx->stream_bufs[stream_buf_idx].phy_addr;

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

    /* cmd[0x03]: SliceParamToCmdRegsEnc1 packs the base QP plus late
     * UpdateCommand bits at positions 30:31 from SliceParam+0x34/+0x35.
     * In the zeroing path that OEM uses together with the cleared
     * cmd[0x19]/cmd[0x1a]/cmd[0x60]/cmd[0x61] state, UpdateCommand forces
     * SliceParam+0x34 = 1. Keep that bit aligned for the current IDR path
     * instead of only carrying the QP/template word. */
    {
        uint32_t qp = ctx->qp ? ctx->qp : 30u;
        cmd[0x03] = (qp << 16) | 0x21000000u;
        if (is_idr)
            cmd[0x03] |= 0x40000000u;
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

    /* cmd[0x0a]: OEM SliceParamToCmdRegsEnc1 overlays the low 16 bits from
     * SliceParam+0x74 onto the template word. */
    cmd[0x0a] = (cmd[0x0a] & 0xffff0000u) | (ctx->enc1_cmd_0a_74 & 0xffffu);

    /* cmd[0x0b]: OEM pack from SliceParam+0x7a/+0x7c/+0x7e/+0x7f/+0x80. */
    cmd[0x0b] = avpu_pack_enc1_cmd0b(ctx, !is_idr);

    /* cmd[0x0c]-cmd[0x11]: Reference frame addresses.
     * IDR: cmd[0x0c..0x0f]=0, cmd[0x10..0x11]=0xFFFFFFFF (sentinel: no ref).
     * P-frame: OEM populates these from the DPB reference list. The reference
     * buffer holds the previously reconstructed frame.  Without these the AVPU
     * has no reference for motion estimation and produces empty residuals. */
    if (is_idr) {
        cmd[0x10] = 0xFFFFFFFFu;
        cmd[0x11] = 0xFFFFFFFFu;
    } else if (ref_phys && ctx->enc_w && ctx->enc_h) {
        uint32_t y_sz = avpu_get_nv12_luma_plane_size(ctx->enc_w, ctx->enc_h);
        uint32_t r_pitch = avpu_get_enc1_rec_pitch(ctx->enc_w, ctx->format_word);
        uint32_t r_map_pitch = avpu_get_enc1_fbc_map_pitch(ctx->enc_w);
        /* OEM: cmd[0x0c..0x0f] mirror cmd[0x24..0x27] layout but for ref */
        cmd[0x0c] = ref_phys;                              /* ref Y */
        cmd[0x0d] = ref_phys + y_sz;                       /* ref UV */
        cmd[0x0e] = (r_pitch & 0x3ffffu)
                   | ((2u & 0x7u) << 28)
                   | ((r_map_pitch & 0xffu) << 19);        /* ref pitch */
        /* cmd[0x0f] = ref EP1 — use same EP1 addr for ref decode buffer */
        if (ctx->interm_buf.phy_addr)
            cmd[0x0f] = ctx->interm_buf.phy_addr;

        cmd[0x10] = ref_phys;                              /* ref Y (again) */
        cmd[0x11] = ref_phys + y_sz;                       /* ref UV (again) */
    }

    /* cmd[0x12]: OEM pack from SliceParam+0xa8/+0xaa/+0xac.
     * CRITICAL: For IDR frames, OEM sets this to 0x003FF3FF (all-sentinel).
     * These are reference frame stride fields — IDR has no reference so the
     * hardware expects sentinel values. Computing width-based defaults for
     * IDR causes the AVPU to write encoded data to wrong addresses. */
    if (is_idr) {
        cmd[0x12] = 0x003FF3FFu;
    } else {
        cmd[0x12] = avpu_pack_enc1_cmd12(ctx);
    }

    /* cmd[0x13]: Stock=0 (NOT an intermediate buffer address) */

    /* cmd[0x14]-cmd[0x18]: Resolution-dependent control/parameter words.
     * These are NOT buffer addresses — they are rate control, lambda, and
     * encoding parameter words packed by SliceParamToCmdRegsEnc1.
     * Values come from stock CL captures at each resolution.
     *
     * cmd[0x17] contains QP at bits[23:16] and bits[7:0] with fixed
     * template bits: (0x10 << 24) | (qp << 16) | (0x2d << 8) | qp.
     */
    /* Generic fallback.  The T40 block below replaces these fields with its
     * configured-picture formulas before submission. */
    cmd[0x14] = 0xf4000107u;
    cmd[0x15] = 0x00000664u;
    cmd[0x16] = 0x3f00006cu;
    cmd[0x18] = 0xc210000cu;
    /* cmd[0x17]: QP-dependent — same formula for all resolutions */
    {
        uint32_t qp17 = ctx->qp ? ctx->qp : 30u;
        cmd[0x17] = (0x10u << 24) | (qp17 << 16) | (0x2du << 8) | qp17;
    }

    /* cmd[0x19]-cmd[0x1a]: OEM pack from SliceParam+0xec/+0xee/+0xf0/+0xf4.
     * We already recover stable seed values for these words; forcing them to
     * zero on IDR is a stronger mismatch than carrying the OEM-shaped packed
     * state through the first frame. */
    cmd[0x19] = avpu_pack_enc1_cmd19(ctx);
    cmd[0x1a] = avpu_pack_enc1_cmd1a(ctx);

    /* ---- cmd[0x1b]-cmd[0x1f]: Enc2 (entropy) parameters ----
     * Keep the embedded Enc1 copy on the same OEM-shaped packers as the
     * standalone Enc2 CL so IDR and P-frame slice words stay aligned. */
    cmd[0x1b] = avpu_pack_enc2_cmd1b(ctx);
    cmd[0x1c] = avpu_pack_enc2_cmd1c(ctx, is_idr);
    cmd[0x1d] = avpu_pack_enc2_cmd1d(ctx);
    /* cmd[0x1e]/cmd[0x1f]: splice metadata for Enc2.
     * GenerateAvcSliceHeader writes SliceParam+0xf8/+0x100 from the actual
     * header bit position and trailing bytes. SliceParam+0xfc is runtime-
     * produced later by UpdateCommand, so this word is only partially proven
     * at the moment. */
    cmd[0x1e] = avpu_pack_enc2_cmd1e(ctx, 0u);
    cmd[0x1f] = avpu_pack_enc2_cmd1f(ctx);

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

            cmd[0x23] = ep2_addr;                     /* stock: 0x07135180 */
            cmd[0x27] = ep1_addr;                     /* stock: 0x0712ed00 */

            /* cmd[0x2c]/cmd[0x2e]: Stock IDR CL has ZEROS here.
             * These are P-frame intermediate buffer pointers (EP1+0x100,
             * WPP+0x100). Only populate for P-frames. */
            if (!is_idr) {
                cmd[0x2c] = ep1_addr + 0x100u;
                cmd[0x2e] = wpp_addr + 0x100u;
            }
        }

        /* Reconstruction buffer */
        if (ctx->rec_buf.phy_addr) {
            cmd[0x24] = ctx->rec_buf.phy_addr;        /* rec Y */
            cmd[0x25] = ctx->rec_buf.phy_addr + y_plane_sz; /* rec UV */
            cmd[0x26] = (rec_pitch & 0x3ffffu)
                       | ((2u & 0x7u) << 28)
                       | ((rec_map_pitch & 0xffu) << 19);
        }

        /* Reference frame addresses — OEM FillCmdRegsEnc1 sets these from
         * the DPB reference list.  For P-frames, the reference buffer is the
         * previously reconstructed frame.  IDR: zeros (no reference). */
        if (!is_idr && ref_phys) {
            cmd[0x28] = ref_phys;                          /* ref Y */
            cmd[0x29] = ref_phys + y_plane_sz;             /* ref UV */
            cmd[0x2a] = (rec_pitch & 0x3ffffu)
                       | ((2u & 0x7u) << 28)
                       | ((rec_map_pitch & 0xffu) << 19);  /* ref pitch */
            /* cmd[0x2b]: ref map base — use the same rec_map region for ref */
            if (rec_map_addr)
                cmd[0x2b] = rec_map_addr;
        }

        /* MV/map addresses */
        if (ctx->rec_buf.phy_addr) {
            cmd[0x2d] = rec_map_addr;                  /* stock: 0x07066500 */
        }

        /* Stream buffer + intermediate data */
        if (ctx->interm_buf.phy_addr) {
            uint32_t ep1_addr = ctx->interm_buf.phy_addr;
            uint32_t wpp_addr = ep1_addr + ctx->interm_ep1_size;
            /* OEM FillCmdRegsEnc1 (encode1 at 0x671b8) puts the intermediate
             * MAP and DATA addresses at cmd[0x34] (byte 0xd0) and cmd[0x35]
             * (byte 0xd4).  These are the SAME map/data addresses that the
             * Enc2 CL uses at cmd[0x3a]/cmd[0x3b].  Without these, Enc1
             * doesn't write intermediate data and Enc2 has nothing to
             * entropy-encode — producing header-only frames. */
            uint32_t map_addr = ctx->interm_buf.phy_addr
                              + ctx->interm_ep1_size
                              + ctx->interm_wpp_size
                              + ctx->interm_ep2_size;
            uint32_t data_addr = map_addr + ctx->interm_map_size;

            cmd[0x30] = stream_desc_phys;               /* stream buffer — must match STRM_PUSH */
            cmd[0x31] = stream_part_offset;            /* stock: 0x00027780 */
            cmd[0x32] = stream_offset;                 /* stock: 0x00000220 */
            cmd[0x33] = stream_budget;
            if (cmd[0x33] != 0u)
                cmd[0x1e] = avpu_pack_enc2_cmd1e(ctx, cmd[0x33] - 1u);
            /* cmd[0x34]/cmd[0x35]: Stock IDR CL has ZEROS. These are
             * intermediate map/data addresses only needed for P-frames
             * (Enc2 entropy coding). Setting them for IDR may confuse
             * the AVPU DMA engine. */
            if (!is_idr) {
                cmd[0x34] = map_addr;
                cmd[0x35] = data_addr;
            }
        }

        /* Map buffer address */
        if (ctx->rec_buf.phy_addr) {
            cmd[0x37] = rec_map_addr;                  /* stock 1080p: 0x06b02100 */

            /* cmd[0x67]: FBC comp-map / map-end address.
             * Stock 1080p has this = cmd[0x37] + map_sz (= map region end).
             * OEM FillCmdRegsEnc1 sources this from *(recBuf + 0x54). */
            cmd[0x67] = rec_map_end;

            /* cmd[0x2e]: reconstruction metadata write target.
             * Stock 1080p IDR has this set (NOT zero) — points past the map
             * region into the MV/metadata area. For P-frames, it's
             * wpp_addr+0x100 (intermediate buffer), but for IDR it's in
             * the rec buffer region. Use map end + MV metadata offset. */
            if (is_idr) {
                cmd[0x2e] = rec_map_end;
            }
        }

        /* OEM encode1() also populates the Enc2 address window inside the
         * same 0x200 command block before CL_PUSH=2. These correspond to the
         * FillCmdRegsEnc2 writes at byte offsets 0xe8..0xfc recovered from
         * ChannelMngr.c, and they are the in-block locations the inline-IDR
         * path needs. Leaving them zero means the entropy stage has no stream
         * or intermediate-buffer addresses even though cmd[0x1b..0x1f] are
         * packed correctly. */
        if (ctx->interm_buf.phy_addr) {
            uint32_t map_addr = ctx->interm_buf.phy_addr
                              + ctx->interm_ep1_size
                              + ctx->interm_wpp_size
                              + ctx->interm_ep2_size;
            uint32_t data_addr = map_addr + ctx->interm_map_size;

            cmd[0x3a] = map_addr;                      /* byte offset 0xe8 */
            cmd[0x3b] = data_addr;                     /* byte offset 0xec */
            cmd[0x3c] = stream_desc_phys;              /* byte offset 0xf0 */
            cmd[0x3d] = stream_part_offset;            /* byte offset 0xf4 */
            cmd[0x3e] = stream_offset;                 /* byte offset 0xf8 */
            cmd[0x3f] = stream_budget;
        }

        /* Enc2 embedded stream buffer section (cmd[0x50]-[0x57]).
         * Required for inline Enc2 within CL_PUSH=2 — the hardware reads
         * stream addresses from this section for the entropy engine output.
         * Stock 640x360 CL shows zeros here in the kernel dump, but this may
         * be a post-completion state. Without these, continuous AVPU IRQs
         * regress to only 2. */
        cmd[0x50] = cmd[0x30];   /* stream buffer phys */
        cmd[0x51] = cmd[0x31];   /* stream_part_offset */
        cmd[0x52] = cmd[0x32];   /* hdr_offset */
        cmd[0x53] = cmd[0x33];   /* budget */
        cmd[0x54] = cmd[0x34];   /* interm map (0 for IDR) */
        cmd[0x55] = cmd[0x35];   /* interm data (0 for IDR) */
        cmd[0x56] = cmd[0x36];   /* 0 */
        cmd[0x57] = cmd[0x37];   /* rec map addr */

        /* Late-window words. The recovered OEM seed state carries non-zero
         * values for cmd[0x60]/cmd[0x61]; keep them for IDR as well so the
         * emitted CL matches the OEM-shaped state we log at startup. */
        cmd[0x60] = ctx->enc1_cmd_60_110_112;
        cmd[0x61] = ctx->enc1_cmd_61_114_116;
        /* OEM SliceParamToCmdRegsEnc1 copies SliceParam+0x14/+0x18 directly
         * into cmd[0x64]/cmd[0x65]. Those are the picture dimensions written
         * by channel_encoder_init into codec_param+0x14/+0x18, and leaving them
         * zero is a known remaining mismatch versus the stock command list. */
        cmd[0x64] = ctx->enc_w;
        cmd[0x65] = ctx->enc_h;
        cmd[0x68] = rec_map_sz;                        /* stock 1080p: 0x2200 ✓ */
        cmd[0x69] = rec_map_sz;                        /* stock 1080p: 0x2200 ✓ */
    }

    cmd[0x6e] = ctx->enc1_cmd_6e_118_11a & 0x100000ffu;
    cmd[0x6f] = ctx->enc1_cmd_6f_94 & 0xffffu;

#if defined(PLATFORM_T40) || defined(PLATFORM_T31)
    {
        uint32_t width = ctx->enc_w;
        uint32_t height = ctx->enc_h;
        uint32_t lcu_w = (width + 15u) >> 4;
        uint32_t lcu_h = (height + 15u) >> 4;
        uint32_t lcu_count = lcu_w * lcu_h;
        uint32_t picture_area_1k =
            (uint32_t)(((uint64_t)width * (uint64_t)height) >> 10);
        uint32_t rec_base = ctx->rec_buf.phy_addr;
        uint32_t rec_frame_size = (uint32_t)avpu_get_enc1_ref_region_size(width, height);
        uint32_t rec_map_size = (uint32_t)avpu_get_enc1_map_region_size(width, height);
        uint32_t rec_y = rec_base;
        uint32_t rec_uv = rec_y + ((uint32_t)avpu_align_up_u32(width, 64u)
                                * (uint32_t)avpu_align_up_u32(height, 64u));
        uint32_t rec_map = rec_y + rec_frame_size;
        uint32_t rec_map_end = rec_map + rec_map_size;
        uint32_t map_storage_size =
            (uint32_t)avpu_get_enc1_map_storage_size(width, height);
        uint32_t mv_data_offset =
            map_storage_size - rec_map_size + 0x100u;
        uint32_t ep3_index;
        uint32_t ep3_phys = 0u;
        uint32_t ep1_row_table_size =
            avpu_align_up_u32(lcu_h * sizeof(uint32_t), 128u);
        uint32_t picture_qp = avpu_t40_picture_qp(ctx, is_idr);
        uint32_t hwrc_group_count;
        uint32_t hwrc_columns_per_group;
        uint64_t hwrc_target;
        uint32_t hwrc_word15;
        uint32_t hwrc_word16;
        uint32_t fps_num = ctx->fps_num ? ctx->fps_num : 25u;
        uint32_t fps_den = ctx->fps_den ? ctx->fps_den : 1u;
        uint32_t min_qp = ctx->min_qp <= 51u ? ctx->min_qp : 0u;
        uint32_t max_qp = ctx->max_qp <= 51u ? ctx->max_qp : 51u;

        uint32_t ref_y = 0u;
        uint32_t ref_uv = 0u;
        uint32_t ref_map = 0u;
        uint32_t ref_map_end = 0u;

        if (!is_idr && ref_phys != 0u) {
            ref_y = ref_phys;
            ref_uv = ref_y + ((uint32_t)avpu_align_up_u32(width, 64u)
                            * (uint32_t)avpu_align_up_u32(height, 64u));
            ref_map = ref_y + rec_frame_size;
            ref_map_end = ref_map + rec_map_size;
        }

#if defined(PLATFORM_T31)
        /*
         * T31 uses two picture-class HWRC states from the three-slot EP3
         * allocation.  The synchronized 1080p/360p OEM oracle selects slot 2
         * for every IDR and slot 1 for every P picture.  Slot 0 is initialized
         * by the codec but is not submitted by this single-reference AVC path.
         * Selecting slots 1/0 here makes IDR and P pictures consume the wrong
         * rate-control history and presents as frame-wide luma/chroma pumping.
         */
        ep3_index = is_idr ? 2u : 1u;
#else
        ep3_index = (2u + 3u - (ctx->frame_number % 3u)) % 3u;
#endif
        if (ctx->rec_trace_buf.phy_addr)
            ep3_phys = ctx->rec_trace_buf.phy_addr +
                       ep3_index * AVPU_T40_EP3_SLOT_SIZE;

        avpu_t40_get_hwrc_grid(width, height, &hwrc_group_count,
                               &hwrc_columns_per_group);
        hwrc_target = ctx->bitrate ? ctx->bitrate : 2000000u;
        if (!is_idr)
            hwrc_target = hwrc_target * 5u / 7u;
        hwrc_target = hwrc_target * 95u / 100u;
        hwrc_word15 = lcu_count
                    ? (uint32_t)(hwrc_target * hwrc_group_count / lcu_count)
                    : 0u;
        hwrc_word16 = (lcu_count && fps_num)
                    ? (uint32_t)(((uint64_t)(ctx->bitrate ? ctx->bitrate : 2000000u) *
                                  fps_den * hwrc_group_count * 4u) /
                                 ((uint64_t)lcu_count * fps_num * 3u))
                    : 0u;

        /* Exact T40XP High/CABAC control shape recovered from live OEM IDR
         * and P command lists. Address-bearing words are substituted below. */
        cmd[0x00] = 0x80700411u;
        cmd[0x02] = 0x4010ad50u;
        cmd[0x03] = (is_idr ? 0x21000000u : 0x11000000u)
                  | (picture_qp << 16);
        cmd[0x04] = 0x00083f1fu;
        cmd[0x08] = 0x77000000u;
        if (!is_idr)
            cmd[0x08] = 0x11000000u;
        cmd[0x09] = 0xfc010000u;
        cmd[0x0a] = (picture_area_1k << 16) | 0x0c80u;
        cmd[0x0b] = ((lcu_w - 1u) & 0x3ffu) << 12;
        if (!is_idr)
            cmd[0x06] |= 0x00000400u;
        memset(&cmd[0x0c], 0, 6u * sizeof(uint32_t));
        cmd[0x10] = 0xffffffffu;
        cmd[0x11] = 0xffffffffu;
        if (is_idr) {
            cmd[0x12] = 0x803ff3ffu;
        } else {
            cmd[0x0c] = 0x00000002u;
            cmd[0x12] = 0x8001b01du;
        }
        /* OEM T40 oracle: this picture-shape control word is 0x1d000000
         * for 1920x1080 and 0x09000000 for 640x360.  Zero happens to leave
         * the small stream decodable, but corrupts the first 1080p CABAC
         * macroblock ("top block unavailable" in ffmpeg). */
        cmd[0x13] =
            (lcu_w ? (((lcu_w + 3u) >> 2) - 1u) : 0u) << 24;
        cmd[0x14] = avpu_t40_pack_hwrc_grid(width, height);
        cmd[0x15] = hwrc_word15 & 0x00ffffffu;
        cmd[0x16] = 0x3f000000u | (hwrc_word16 & 0x00ffffffu);
        cmd[0x17] = (min_qp << 24) | (picture_qp << 16)
                  | (max_qp << 8) | picture_qp;
        cmd[0x18] = (ctx->frame_number <= 1u ? 0xc0000000u : 0x40000000u)
                  | (((hwrc_columns_per_group * 4u + 1u) & 0xffu) << 20)
                  | ((hwrc_word15 >> 7) & 0xffffu);
        cmd[0x19] = 0u;
        cmd[0x1a] = 0u;
        cmd[0x1b] = 0x000a0c80u;
        cmd[0x1c] = cmd[0x03] | 0x00000d06u;
        cmd[0x1d] = cmd[0x0b];
        cmd[0x1e] = lcu_count ? lcu_count - 1u : 0u;
        cmd[0x1f] = 0u;

        cmd[0x20] = src_phys;
        cmd[0x21] = src_phys + avpu_get_nv12_luma_plane_size(width, height);
        cmd[0x22] = avpu_get_enc1_src_pitch(width, ctx->format_word);
        cmd[0x23] = ctx->interm_buf.phy_addr
                  + ctx->interm_ep1_size + ep1_row_table_size;
        cmd[0x24] = rec_y;
        cmd[0x25] = rec_uv;
        cmd[0x26] = (avpu_get_enc1_rec_pitch(width, ctx->format_word) & 0x3ffffu)
                  | (2u << 28)
                  | ((avpu_get_enc1_fbc_map_pitch(width) & 0xffu) << 19);
        cmd[0x27] = ctx->interm_buf.phy_addr;
        memset(&cmd[0x28], 0, 5u * sizeof(uint32_t));
        if (!is_idr) {
            cmd[0x28] = ref_y;
            cmd[0x29] = ref_uv;
            cmd[0x2c] = ref_map_end + mv_data_offset;
        }
        /* cmd[0x2d] is the rotating EP3 hardware-rate-control table, not a
         * reconstruction/reference address.  The OEM scheduler selects slots
         * 2, 1, 0 for pictures 0, 1, 2 and repeats. */
        cmd[0x2d] = ep3_phys;
        cmd[0x2e] = rec_map_end + mv_data_offset;
        cmd[0x2f] = 0u;
        cmd[0x30] = stream_desc_phys;
        cmd[0x31] = stream_part_offset;
        /* The live T40 oracle uses a fixed 0x220-byte hardware payload
         * window.  AVPU DMA writes in bursts and can clobber a short prefix
         * when pointed directly at it, so keep the captured safe offset and
         * compact the payload behind our generated headers on completion. */
        cmd[0x32] = 0x00000220u;
        cmd[0x33] = avpu_get_stream_window_budget(ctx, cmd[0x31],
                                                   cmd[0x32]);
        cmd[0x34] = 0u;
        cmd[0x35] = 0u;
        cmd[0x36] = 0u;
        cmd[0x37] = rec_map;

        memset(&cmd[0x38], 0, (0x67u - 0x38u) * sizeof(uint32_t));
        if (!is_idr)
            cmd[0x38] = ref_map;
#if defined(PLATFORM_T40)
        /*
         * T40's SliceParamToCmdRegsEnc1 writes the configured dimensions
         * after clearing the late scratch window.  T31 does not: every
         * captured OEM T31 command keeps cmd[0x64]/cmd[0x65] zero, including
         * 1080p.  Feeding dimensions into those T31 scratch words makes the
         * reconstructed image fluctuate even when the source frame is steady.
         */
        cmd[0x64] = width;
        cmd[0x65] = height;
#endif
        cmd[0x67] = rec_map_end;
        cmd[0x68] = is_idr ? rec_map_size : ref_map_end;
        cmd[0x69] = rec_map_size;
        cmd[0x6e] = 0u;
        cmd[0x6f] = 0u;

#if defined(PLATFORM_T31)
        /*
         * The T31 and T40 command engines use the same inline AVC layout,
         * but their hardware-rate-control seed is not interchangeable.  A
         * T40 seed here occasionally lets T31 complete an IDR, then more
         * commonly makes the command status and stream buffer collapse into
         * the AVPU's 0x10/0x80/0x99 error-fill patterns.
         *
         * Keep all addresses and picture-shape fields derived from the active
         * channel above, but rebuild the T31 control window from its live OEM
         * command-list semantics:
         *
         *   - initial IDR QP is one below the common bits-per-LCU estimate;
         *   - cmd[0x15] is the per-picture target distributed over the T31
         *     HWRC grid (the 9914/10000 factor recovers OEM's integer seed);
         *   - cmd[0x16] is the long-term per-frame target;
         *   - cmd[0x18]'s low field is cmd[0x15] in 128-bit units.
         *
         * For the captured 640x360, 25 fps, 1 Mbps oracle this produces the
         * exact OEM IDR values 0x6bc, 0x3f000121 and 0xc210000d without
         * baking that resolution or bitrate into the implementation.
         */
        uint32_t t31_picture_qp = picture_qp;
        uint64_t t31_picture_target;
        uint64_t t31_long_term_target;
        uint32_t t31_picture_number =
            is_idr ? 0u : ctx->frame_number - ctx->idr_frame_number;

        /* avpu_t40_picture_qp() already applied the T31 I/P delta. */
        if (t31_picture_qp < min_qp)
            t31_picture_qp = min_qp;
        if (t31_picture_qp > max_qp)
            t31_picture_qp = max_qp;

        t31_picture_target =
            (uint64_t)(ctx->bitrate ? ctx->bitrate : 2000000u) *
            fps_den * hwrc_group_count * 8u;
        if (lcu_count != 0u && fps_num != 0u)
            t31_picture_target /= (uint64_t)lcu_count * fps_num;
        else
            t31_picture_target = 0u;
        if (is_idr) {
            hwrc_word15 =
                (uint32_t)((t31_picture_target * 9914u) / 10000u);
        } else if (lcu_count != 0u && fps_num != 0u) {
            /*
             * OEM P pictures use 85% of the long-term bits-per-group target.
             * Keep the fractional precision until the final divide: this
             * recovers both captured values exactly (0x64 at 1080p/3 Mbps
             * and 0xb8 at 360p/1 Mbps).
             */
            hwrc_word15 = (uint32_t)(
                (uint64_t)(ctx->bitrate ? ctx->bitrate : 2000000u) *
                fps_den * hwrc_group_count * 17u /
                ((uint64_t)lcu_count * fps_num * 20u));
        } else {
            hwrc_word15 = 0u;
        }
        if (lcu_count != 0u && fps_num != 0u) {
            t31_long_term_target =
                (uint64_t)(ctx->bitrate ? ctx->bitrate : 2000000u) *
                fps_den * hwrc_group_count /
                ((uint64_t)lcu_count * fps_num);
        } else {
            t31_long_term_target = 0u;
        }

        cmd[0x03] = (is_idr ? 0x21000000u : 0x11000000u)
                  | (t31_picture_qp << 16);
        /* T31 keeps these fields clear at both captured resolutions.  The
         * 1080p-sized extensions belong to the T40 command shape; T31 gets
         * its source geometry from the companion 0x8400 register block. */
        cmd[0x0a] = 0x00000c80u;
        cmd[0x12] &= 0x7fffffffu;
        cmd[0x13] = 0u;
        cmd[0x15] = hwrc_word15 & 0x00ffffffu;
        cmd[0x16] = 0x3f000000u
                  | ((uint32_t)t31_long_term_target & 0x00ffffffu);
        cmd[0x17] = (min_qp << 24) | (t31_picture_qp << 16)
                  | (max_qp << 8) | t31_picture_qp;
        /*
         * T31 restarts HWRC on the IDR and retains the restart bit for the
         * first P picture of the new GOP.  Later P pictures carry only bit 30.
         * The low field is the current picture target in 128-bit units.
         */
        cmd[0x18] = ((is_idr || t31_picture_number == 1u)
                       ? 0xc0000000u : 0x40000000u)
                  | (((hwrc_columns_per_group * 4u + 1u) & 0xffu) << 20)
                  | ((hwrc_word15 >> 7) & 0xffffu);
        cmd[0x1c] = cmd[0x03] | 0x00000d06u;

        /*
         * T31's cmd[0x0c..0x11] window contains the current and prior
         * picture numbers used by the single-reference AVC scheduler; it is
         * not a physical-address window.  Reset on IDR, then advance in units
         * of two exactly as the OEM command lists do.
         */
        if (!is_idr) {
            uint32_t current = t31_picture_number * 2u;
            uint32_t previous =
                t31_picture_number > 1u ? current - 2u : 0u;

            cmd[0x0c] = current;
            cmd[0x0d] = previous;
            cmd[0x0e] = 0u;
            cmd[0x0f] = previous;
            cmd[0x10] =
                t31_picture_number > 1u ? current - 4u : 0xffffffffu;
            cmd[0x11] = 0xffffffffu;
        }
        /*
         * T31 FixQP bypasses the hardware rate controller instead of feeding
         * it a degenerate min-QP == max-QP range.  The exact OEM 640x360,
         * High/CABAC, QP24 command oracle has:
         *
         *   cmd[0x09]      = 0xfc000000
         *   cmd[0x14..18]  = 0
         *   cmd[0x2d]      = 0
         *
         * Keeping T40's HWRC-enable bit, seed words, and EP3 table active in
         * this mode makes successive T31 IDRs use incompatible rate-control
         * state.  The bitstream remains syntactically valid, but its decoded
         * luma and chroma pump while the captured ISP frames remain steady.
         */
        if (ctx->rc_mode == HW_RC_MODE_FIXQP) {
            cmd[0x09] = 0xfc000000u;
            memset(&cmd[0x14], 0, 5u * sizeof(uint32_t));
            cmd[0x2d] = 0u;
        }
        /*
         * The captured T31 command lists submit cmd[0x32] = 0x220, and the
         * live pre-compaction dump confirms that completed entropy bytes
         * begin there.  The separate 0x200..0x257 staging window holds the
         * host prefix consumed by the hardware and must not redefine the
         * completed payload boundary.
         */
        cmd[0x32] = AVPU_T31_STREAM_PREFIX_BYTES;
        cmd[0x33] = avpu_get_stream_window_budget(ctx, cmd[0x31],
                                                   cmd[0x32]);
#endif
    }
#endif
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
                               int stream_buf_idx, uint32_t hdr_offset, int is_idr)
{
    if (!ctx || !cmd) return;

    memset(cmd, 0, 512);

    const uint32_t stream_part_offset = avpu_get_enc1_stream_part_offset(ctx);
    uint32_t stream_desc_phys = 0u;
    if (stream_buf_idx >= 0 && stream_buf_idx < ctx->stream_bufs_used)
        stream_desc_phys = ctx->stream_bufs[stream_buf_idx].phy_addr;

    /* --- cmd[0x1b] (byte offset 0x6c): SliceParamToCmdRegsEnc2 word 1 ---
     * bits[12:0]  = SliceParam+0x74
     * bits[25:16] = SliceParam+0x78
     * bits[29:28] = SliceParam+0x19
     * bits[31:30] = SliceParam+0x1a */
    cmd[0x1b] = avpu_pack_enc2_cmd1b(ctx);

    /* --- cmd[0x1c] (byte offset 0x70): SliceParamToCmdRegsEnc2 word 2 ---
     * Exact field layout recovered from the OEM SliceParamToCmdRegsEnc2 packer:
     * +0xf6/+0x66/+0x7e/+0x10/+0x11/+0x12/+0x28/+0x1c/+0x30. */
    cmd[0x1c] = avpu_pack_enc2_cmd1c(ctx, is_idr);

    /* --- cmd[0x1d] (byte offset 0x74): OEM SliceParamToCmdRegsEnc2 ---
     * low10      = SliceParam+0x3c / SliceParam+0x108
     * bits[21:12]= ((SliceParam+0x0a + 1) >> 1) - 1
     * bits[27:24]= SliceParam+0x41
     * bits[31:28]= SliceParam+0x40
     *
     * For the current single-slice AVC path the runtime slice start stays at 0,
     * while +0x0a comes from the picture width in 8-pel units (confirmed via
     * GetPicDimFromCmdRegsEnc1).  That yields the known stock 640x360 value
     * 0x00027000 instead of the previous lcu_h/half-row heuristic. */
    cmd[0x1d] = avpu_pack_enc2_cmd1d(ctx);

    /* --- cmd[0x1e]-cmd[0x1f]: slice-header splice metadata.
     * OEM Source: SliceParam+0xf8/+0xfc/+0x100. GenerateAvcSliceHeader writes
     * +0xf8/+0x100, while UpdateCommand writes +0xfc later in the chain.
     * These are not simply the total AU header size or the last 4 bytes of
     * the stream buffer. */
    cmd[0x1e] = avpu_pack_enc2_cmd1e(ctx, 0u);
    cmd[0x1f] = avpu_pack_enc2_cmd1f(ctx);

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
    cmd[0x3f] = avpu_get_stream_window_budget(ctx, stream_part_offset, hdr_offset);
    if (cmd[0x3f] != 0u)
        cmd[0x1e] = avpu_pack_enc2_cmd1e(ctx, cmd[0x3f] - 1u);
}

static void log_first_enc2_cmd_window(ALAvpuContext* ctx, uint32_t idx, const uint32_t* cmd)
{
    if (!ctx || !cmd) return;
    if (__sync_lock_test_and_set(&ctx->first_enc2_submit_logged, 1) != 0) return;

    LOG_CODEC("Process: first Enc2 CL[%u] cmd[0x1b]=0x%08x cmd[0x1c]=0x%08x cmd[0x1d]=0x%08x cmd[0x1e]=0x%08x cmd[0x1f]=0x%08x",
              idx, cmd[0x1b], cmd[0x1c], cmd[0x1d], cmd[0x1e], cmd[0x1f]);
    LOG_CODEC("Process: first Enc2 CL[%u] cmd[0x3a]=0x%08x cmd[0x3b]=0x%08x cmd[0x3c]=0x%08x cmd[0x3d]=0x%08x cmd[0x3e]=0x%08x cmd[0x3f]=0x%08x",
              idx, cmd[0x3a], cmd[0x3b], cmd[0x3c], cmd[0x3d], cmd[0x3e], cmd[0x3f]);
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
    if (!ctx || !avpu_cl_ring_base(ctx) || ctx->cl_entry_size == 0 || ctx->cl_count == 0)
        return;

    const uint8_t *entry = avpu_cl_entry_ptr(ctx, active_idx);
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
#if defined(PLATFORM_T41)
    if (!ctx || !ctx->rec_buf.phy_addr)
        return;

    /* T41's reconstructed data stays at one stable base.  Successive
     * pictures alternate only the embedded map/MV slots, so swapping whole
     * allocations (the T31/T40 ownership model) disconnects those addresses
     * from the reference manager captured in the command oracle. */
    ctx->t41_reference_luma_offset = ctx->t41_pending_luma_offset;
    __sync_synchronize();
    ctx->reference_valid = 1;

    if (ctx->frames_encoded % 50 == 0)
        LOG_CODEC("EndEncoding: promoted T41 embedded reference manager=0x%08x luma_offset=0x%x ep3_ring=0x%08x",
                  ctx->rec_buf.phy_addr,
                  ctx->t41_reference_luma_offset,
                  ctx->rec_trace_buf.phy_addr);
#else
    if (!ctx || !ctx->rec_buf.phy_addr || !ctx->ref_buf.phy_addr)
        return;

    AvpuDMABuf prev_ref = ctx->ref_buf;

    ctx->ref_buf = ctx->rec_buf;
    ctx->rec_buf = prev_ref;
    __sync_synchronize();
    ctx->reference_valid = 1;

    if (ctx->frames_encoded % 50 == 0)
    LOG_CODEC("EndEncoding: promoted rec->ref ref=0x%08x next_rec=0x%08x ep3_ring=0x%08x",
              ctx->ref_buf.phy_addr, ctx->rec_buf.phy_addr,
              ctx->rec_trace_buf.phy_addr);
#endif
}

static pthread_mutex_t *avpu_stream_queue_mutex(ALAvpuContext *ctx)
{
    return (ctx && ctx->stream_queue_mutex)
        ? (pthread_mutex_t *)ctx->stream_queue_mutex
        : NULL;
}

static int avpu_pending_push_locked(ALAvpuContext *ctx, int buf_idx, void *user_data)
{
    int write_idx;

    if (!ctx || ctx->pending_stream_count >= (int)(sizeof(ctx->pending_streams) / sizeof(ctx->pending_streams[0])))
        return 0;

    write_idx = ctx->pending_stream_write;
    ctx->pending_streams[write_idx].buf_idx = buf_idx;
    ctx->pending_streams[write_idx].user_data = user_data;
    ctx->pending_stream_write = (write_idx + 1) % (int)(sizeof(ctx->pending_streams) / sizeof(ctx->pending_streams[0]));
    ctx->pending_stream_count++;
    return 1;
}

static int avpu_pending_pop_locked(ALAvpuContext *ctx, int *buf_idx_out, void **user_data_out)
{
    int read_idx;

    if (buf_idx_out)
        *buf_idx_out = -1;
    if (user_data_out)
        *user_data_out = NULL;

    if (!ctx || ctx->pending_stream_count <= 0)
        return 0;

    read_idx = ctx->pending_stream_read;
    if (buf_idx_out)
        *buf_idx_out = ctx->pending_streams[read_idx].buf_idx;
    if (user_data_out)
        *user_data_out = ctx->pending_streams[read_idx].user_data;
    ctx->pending_streams[read_idx].buf_idx = -1;
    ctx->pending_streams[read_idx].user_data = NULL;
    ctx->pending_stream_read = (read_idx + 1) % (int)(sizeof(ctx->pending_streams) / sizeof(ctx->pending_streams[0]));
    ctx->pending_stream_count--;
    return 1;
}

static int avpu_pending_peek_locked(ALAvpuContext *ctx, int *buf_idx_out, void **user_data_out)
{
    int read_idx;

    if (buf_idx_out)
        *buf_idx_out = -1;
    if (user_data_out)
        *user_data_out = NULL;

    if (!ctx || ctx->pending_stream_count <= 0)
        return 0;

    read_idx = ctx->pending_stream_read;
    if (buf_idx_out)
        *buf_idx_out = ctx->pending_streams[read_idx].buf_idx;
    if (user_data_out)
        *user_data_out = ctx->pending_streams[read_idx].user_data;
    return 1;
}

static int avpu_pending_peek(ALAvpuContext *ctx, int *buf_idx_out, void **user_data_out)
{
    pthread_mutex_t *mutex;
    int ok;

    if (buf_idx_out)
        *buf_idx_out = -1;
    if (user_data_out)
        *user_data_out = NULL;

    if (!ctx)
        return 0;

    mutex = avpu_stream_queue_mutex(ctx);
    if (!mutex)
        return 0;

    pthread_mutex_lock(mutex);
    ok = avpu_pending_peek_locked(ctx, buf_idx_out, user_data_out);
    pthread_mutex_unlock(mutex);
    return ok;
}

static int avpu_acquire_stream_buffer(ALAvpuContext *ctx)
{
    pthread_mutex_t *mutex;
    int buf_idx = -1;

    if (!ctx || ctx->stream_bufs_used <= 0)
        return -1;

    mutex = avpu_stream_queue_mutex(ctx);
    if (!mutex)
        return -1;

    pthread_mutex_lock(mutex);
    for (int n = 0; n < ctx->stream_bufs_used; ++n) {
        int i = (ctx->next_stream_submit + n) % ctx->stream_bufs_used;
        if (ctx->stream_buf_state[i] == AVPU_STREAM_BUF_FREE) {
            ctx->stream_buf_state[i] = AVPU_STREAM_BUF_IN_FLIGHT;
            ctx->stream_in_hw[i] = 0;
            ctx->next_stream_submit = (i + 1) % ctx->stream_bufs_used;
            buf_idx = i;
            break;
        }
    }
    pthread_mutex_unlock(mutex);

    return buf_idx;
}

static int avpu_track_submitted_stream(ALAvpuContext *ctx, int buf_idx, void *user_data)
{
    pthread_mutex_t *mutex;
    int ok = 0;

    if (!ctx || buf_idx < 0 || buf_idx >= ctx->stream_bufs_used)
        return 0;

    mutex = avpu_stream_queue_mutex(ctx);
    if (!mutex)
        return 0;

    pthread_mutex_lock(mutex);
    if (ctx->stream_buf_state[buf_idx] == AVPU_STREAM_BUF_IN_FLIGHT)
        ok = avpu_pending_push_locked(ctx, buf_idx, user_data);
    if (!ok) {
        ctx->stream_buf_state[buf_idx] = AVPU_STREAM_BUF_FREE;
        ctx->stream_in_hw[buf_idx] = 1;
    }
    pthread_mutex_unlock(mutex);

    return ok;
}

static int avpu_complete_next_stream(ALAvpuContext *ctx, int *buf_idx_out, void **user_data_out)
{
    pthread_mutex_t *mutex;
    int buf_idx = -1;
    void *user_data = NULL;
    int ok;

    if (!ctx)
        return 0;

    mutex = avpu_stream_queue_mutex(ctx);
    if (!mutex)
        return 0;

    pthread_mutex_lock(mutex);
    ok = avpu_pending_pop_locked(ctx, &buf_idx, &user_data);
    if (ok && buf_idx >= 0 && buf_idx < ctx->stream_bufs_used)
        ctx->stream_buf_state[buf_idx] = AVPU_STREAM_BUF_READY;
    pthread_mutex_unlock(mutex);

    if (buf_idx_out)
        *buf_idx_out = buf_idx;
    if (user_data_out)
        *user_data_out = user_data;
    return ok;
}

static void avpu_mark_stream_buffer_released(ALAvpuContext *ctx, int buf_idx)
{
    pthread_mutex_t *mutex;

    if (!ctx || buf_idx < 0 || buf_idx >= ctx->stream_bufs_used)
        return;

    mutex = avpu_stream_queue_mutex(ctx);
    if (!mutex)
        return;

    pthread_mutex_lock(mutex);
    ctx->stream_buf_state[buf_idx] = AVPU_STREAM_BUF_FREE;
    ctx->stream_in_hw[buf_idx] = 1;
    pthread_mutex_unlock(mutex);
}

static void avpu_complete_frame(ALAvpuContext *ctx, const char *source)
{
    int buf_idx = -1;
    void *frame_user_data = NULL;
    int frames_encoded;
    uint32_t frame_size = 0;
    int flush_ret = -1;
    int queued = 0;

    if (!ctx)
        return;

    if (!avpu_complete_next_stream(ctx, &buf_idx, &frame_user_data)) {
        LOG_CODEC("%s: completion without pending stream (frame_number=%u enc=%d cons=%d)",
                  source ? source : "EndEncoding",
                  ctx->frame_number, ctx->frames_encoded, ctx->frames_consumed);
    }

    /*
     * Finalize DPB ownership before making the stream visible.  Fifo_Queue
     * wakes Raptor immediately; publishing first lets the next Process enter
     * with reference_valid still clear or with rec/ref still pointing at the
     * just-completed picture.  On T31 that produces a second IDR into the
     * same reconstruction buffer and then a P command against the wrong
     * reference, after which the AVPU stops completing.
     *
     * The hardware completed the reconstruction even if packaging the public
     * stream later fails, so reference promotion and the completion counter
     * belong to the hardware-completion side of the queue boundary.
     */
    if (buf_idx >= 0) {
        avpu_promote_reference(ctx);
        frames_encoded = __sync_add_and_fetch(&ctx->frames_encoded, 1);
        queued = avpu_queue_completed_stream(ctx, buf_idx, frame_user_data, source, &frame_size, &flush_ret);
    }

    if (!queued) {
        if (buf_idx >= 0) {
            avpu_mark_stream_buffer_released(ctx, buf_idx);
            LOG_CODEC("%s: released unqueued stream buf[%d] len=%u flush_ret=%d",
                      source ? source : "EndEncoding",
                      buf_idx, frame_size, flush_ret);
        }
        __sync_add_and_fetch(&ctx->dropped_completions, 1u);
        if (buf_idx >= 0) {
            /* Publish the handoff only after the late-DMA drain and all
             * completion-side buffer handling have finished. */
            __sync_add_and_fetch(&ctx->completions_drained, 1u);
        }
        return;
    }

    openimp_profile_frame_completed(frame_size);

    /* frames_encoded is intentionally advanced before stream publication so
     * DPB ownership is visible to consumers, but it is too early to release
     * T31's single physical encoder to another context: effective-size
     * handling above still drains late DMA and copies the access unit. */
    __sync_add_and_fetch(&ctx->completions_drained, 1u);

    if (frames_encoded % 50 == 0)
    LOG_CODEC("%s: frames_encoded=%d frame_number=%u frames_consumed=%d buf_idx=%d frame_size=%u flush_ret=%d",
              source ? source : "EndEncoding",
              frames_encoded, ctx->frame_number, ctx->frames_consumed,
              buf_idx, frame_size, flush_ret);
}

#if defined(PLATFORM_T31)
static int avpu_t31_payload_size_is_error_fill(uint32_t payload_size)
{
    uint32_t fill = payload_size & 0xffu;

    /*
     * A failed T31 command can leave the status word filled with the same
     * diagnostic byte used for the stream buffer.  Those words can look like
     * plausible in-range byte counts (for example 0x00101010), so a simple
     * buffer-size bound is insufficient.
     */
    if (fill != 0x10u && fill != 0x80u && fill != 0x81u &&
        fill != 0x82u && fill != 0x99u)
        return 0;

    return (payload_size & 0x00ffffffu) == fill * 0x00010101u;
}
#endif

static int avpu_try_recover_sticky_completion(ALAvpuContext *ctx,
                                              unsigned int core_status,
                                              const char *source)
{
    if (!ctx || !ctx->session_ready)
        return 0;

#if defined(PLATFORM_T41)
    /*
     * The measured T41 core transition from 0x80000000 to 0x80000003 does
     * not, by itself, indicate a completed command.  OEM completion arrives
     * through the IRQ and the separate status structure at slot +0x5c0.
     * Until that structure validates, fail closed instead of publishing a
     * synthetic frame from an unported command payload.
     */
    (void)core_status;
    (void)source;
    return 0;
#else
    pthread_mutex_t *mutex;
    int frames_encoded;
    int frames_consumed;
    int pending_stream_count;
    unsigned int submitted_frames;
#if defined(PLATFORM_T31)
    int pending_buf_idx = -1;
    uint32_t pending_cl_idx;
    const uint8_t *pending_status_regs;
    uint32_t pending_payload_size = 0u;
#endif

#if defined(PLATFORM_T31)
    /*
     * A T31 core-status value of 3 is not proof of completion.  Before any
     * real IRQ has been observed, its untouched/error-filled command block
     * can contain an in-range number that looks like a bit count.  Fail
     * closed instead of publishing that synthetic first frame.
     */
    if (ctx->last_irq_id < 0)
        return 0;
#endif

    if ((core_status & 0x3u) != 0x3u)
        return 0;

    frames_encoded = ctx->frames_encoded;
    frames_consumed = ctx->frames_consumed;
    submitted_frames = ctx->frame_number;
    pending_stream_count = 0;

    mutex = avpu_stream_queue_mutex(ctx);
    if (mutex) {
        pthread_mutex_lock(mutex);
        pending_stream_count = ctx->pending_stream_count;
        pthread_mutex_unlock(mutex);
    }

    if (submitted_frames <= (unsigned int)frames_encoded)
        return 0;

    if (frames_encoded != frames_consumed)
        return 0;

    if (pending_stream_count <= 0)
        return 0;

#if defined(PLATFORM_T31)
    /*
     * Core status can become sticky before the submitted command list has
     * received its completion writeback.  Promoting on status alone pops the
     * wrong buffer; the delayed real IRQ then completes the next frame.
     * Require the same exact entropy byte count used by the normal T31
     * callback.  The writeback word at +0x104, unlike cmd[0x4d], matched the
     * DMA payload extent in every archived status-window capture.
     */
    if (!avpu_pending_peek(ctx, &pending_buf_idx, NULL) ||
        pending_buf_idx < 0 || pending_buf_idx >= 16)
        return 0;
    pending_cl_idx = ctx->stream_enc2_cl_idx[pending_buf_idx];
    pending_status_regs = avpu_cl_submit_status_ptr(ctx, pending_cl_idx);
    if (!pending_status_regs)
        return 0;
    if (!ctx->cl_submit_ring.uncached_map)
        avpu_flush_cache(ctx->fd, (void *)pending_status_regs,
                         (unsigned int)ctx->cl_entry_size,
                         0 /* BIDIRECTIONAL */);
    if (openimp_t31_completion_payload_size(
            pending_status_regs, ctx->cl_entry_size,
            &pending_payload_size) != 0 ||
        avpu_t31_payload_size_is_error_fill(pending_payload_size) ||
        ctx->stream_buf_size <= (int)AVPU_T31_PAYLOAD_OFFSET ||
        pending_payload_size > (uint32_t)ctx->stream_buf_size -
            AVPU_T31_PAYLOAD_OFFSET)
        return 0;
    ctx->t31_payload_size_by_buf[pending_buf_idx] = pending_payload_size;
#endif

    LOG_CODEC("%s: recovering sticky completion core_status=0x%08x submitted=%u enc=%d cons=%d pending=%d last_irq=%d",
              source ? source : "AVPU",
              core_status,
              submitted_frames,
              frames_encoded,
              frames_consumed,
              pending_stream_count,
              ctx->last_irq_id);

    avpu_complete_frame(ctx, source ? source : "EndEncoding[sticky]");
    return 1;
#endif
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
#if defined(PLATFORM_T31) || defined(PLATFORM_T41)
    /* T31 and T41 stock audits enable Enc1 completion bit 0 here. */
    unsigned add_m = 0x01u;
#else
    unsigned add_m = 0x11u; /* T40 Enc1 + entropy completion */
#endif
    (void)core;
    unsigned old_m = 0;
    unsigned new_m = add_m;

    if (avpu_read_reg_quiet(fd, AVPU_INTERRUPT_MASK, &old_m) == 0) {
        new_m = old_m | add_m;
    }

    { static unsigned int ei_count = 0; unsigned int c = __sync_add_and_fetch(&ei_count, 1);
      if (c <= 5 || (c % 50) == 0)
        LOG_CODEC("AVPU: enable_interrupts core=%d old=0x%08x add=0x%08x new=0x%08x [#%u]",
                  core, old_m, add_m, new_m, c);
    }
    avpu_write_reg(fd, AVPU_INTERRUPT_MASK, new_m);
}

static void avpu_clear_interrupts(int fd)
{
    static unsigned int clear_count = 0;
    unsigned int c = __sync_add_and_fetch(&clear_count, 1);

    if (c <= 5 || (c % 50) == 0) {
        LOG_CODEC("AVPU: clear interrupts mask=0x%08x [#%u]",
                  AVPU_IRQ_CLEAR_MASK, c);
    }

    avpu_write_reg(fd, AVPU_INTERRUPT, AVPU_IRQ_CLEAR_MASK);
}

/* OEM parity: TurnOnGC.constprop.36 → SetClockCommand(ip_ctrl, core, 1)
 * which read-modify-writes bits[1:0] of (core<<9)+0x83F4. */
static void avpu_turn_on_gc(int fd, int core)
{
    unsigned int old = 0;
    unsigned int new_val = 1u;

    if (avpu_read_reg(fd, AVPU_REG_CORE_CLKCMD(core), &old) == 0) {
        new_val = ((old ^ 1u) & 0x3u) ^ old;
        { static unsigned int gc_count = 0; unsigned int c = __sync_add_and_fetch(&gc_count, 1);
          if (c <= 5 || (c % 1000) == 0)
            LOG_CODEC("AVPU: TurnOnGC core=%d clkcmd old=0x%08x new=0x%08x [#%u]", core, old, new_val, c);
        }
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

#if defined(PLATFORM_T41)
/* OEM AL_EncCore_Reset on T41: select reset clock, pulse reset, restore the
 * clock command, then acknowledge this core through the global control word. */
static void avpu_t41_reset_core(int fd, int core)
{
    unsigned int old = 0;
    unsigned int reset_clock = 2u;
    unsigned int restored_clock = 0u;

    if (avpu_read_reg_quiet(fd, AVPU_REG_CORE_CLKCMD(core), &old) == 0)
        reset_clock = ((old ^ 2u) & 0x3u) ^ old;
    avpu_write_reg(fd, AVPU_REG_CORE_CLKCMD(core), reset_clock);
    avpu_write_reg(fd, AVPU_REG_CORE_RESET(core), 1u);
    if (avpu_read_reg_quiet(fd, AVPU_REG_CORE_CLKCMD(core), &old) == 0)
        restored_clock = old & ~0x3u;
    avpu_write_reg(fd, AVPU_REG_CORE_CLKCMD(core), restored_clock);
    avpu_write_reg(fd, AVPU_REG_MISC_CTRL, 1u << ((unsigned)core + 16u));
}

static void avpu_t41_program_command_slot(int fd, int core,
                                          uint32_t command_phys)
{
    unsigned int control = 0;
    unsigned int base = (unsigned int)core << 12;
    uint32_t status_phys = 0;

    if (openimp_t41_command_status_phys(command_phys, &status_phys) != 0) {
        LOG_CODEC("AVPU: invalid T41 command physical address 0x%08x",
                  command_phys);
        return;
    }

    avpu_write_reg(fd, base + AVPU_REG_CL_ADDR_HI, 0u);
    avpu_write_reg(fd, base + AVPU_REG_CL_ADDR, command_phys);
    avpu_write_reg(fd, base + AVPU_REG_CL_STATUS_HI, 0u);
    avpu_write_reg(fd, base + AVPU_REG_CL_STATUS, status_phys);
    if (avpu_read_reg_quiet(fd, base + AVPU_REG_CL_CTRL, &control) == 0)
        avpu_write_reg(fd, base + AVPU_REG_CL_CTRL, control | 0x1000u);
    if (avpu_read_reg_quiet(fd, base + AVPU_REG_CL_CTRL, &control) == 0)
        avpu_write_reg(fd, base + AVPU_REG_CL_CTRL,
                       control | 0x10000000u);
}
#endif

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
    OpenIMPProfileStamp completion_profile = openimp_profile_begin();
    OpenIMPProfileStamp status_profile = completion_profile;
    ALAvpuContext *ctx = (ALAvpuContext*)user_data;
    struct {
        uint8_t raw[0x168];
    } status_regs;
    struct {
        uint8_t raw[0x70];
    } slice_status;
    struct {
        uint8_t raw[0x70];
    } merged_status;
    uint8_t *status_regs_ptr = NULL;
#if defined(PLATFORM_T41)
    uint8_t *command_slot_ptr = NULL;
#endif
    uint32_t cl_idx = 0;
    uint32_t bitcount = 0;
    uint32_t completed_flag = 0;
    const char *status_source = "empty";
    int buf_idx = -1;
    int completed = 0;
    int have_pending = 0;

    if (ctx && (ctx->frames_encoded < 16 ||
                ctx->frames_encoded % 50 == 0))
    LOG_CODEC("EndEncoding callback: encoding completed (frame %d)", ctx ? ctx->frames_encoded : -1);

    /* OEM reads the 0x200-byte status block from the current readback/status
     * pointer in the core context.  T31 writes completion status back into
     * the submitted CL itself; the separate host-side readback copy retains
     * the pre-submit words.  Other generations still prefer that readback
     * copy and fall back to the submit entry. */
    memset(&status_regs, 0, sizeof(status_regs));
    memset(&slice_status, 0, sizeof(slice_status));
    memset(&merged_status, 0, sizeof(merged_status));

    have_pending = avpu_pending_peek(ctx, &buf_idx, NULL);
    if (have_pending && buf_idx >= 0 && buf_idx < 16) {
        cl_idx = ctx->stream_enc2_cl_idx[buf_idx];
#if defined(PLATFORM_T41)
        /*
         * T41 writes completion status into the submitted slot.  The host
         * readback ring is only a command-building mirror and is never an
         * AVPU DMA target, so invalidating both 1 MiB windows here wastes a
         * substantial part of the 40 ms frame budget.
         */
        if (!ctx->cl_submit_ring.uncached_map &&
            avpu_cl_submit_ring_base(ctx))
            avpu_flush_cache_profiled(
                ctx->fd, ctx->cl_submit_ring.map,
                0x100000, 0 /* BIDIRECTIONAL */,
                OPENIMP_PROFILE_CACHE_COMMAND_COMPLETE);
#elif defined(PLATFORM_T31)
        /* T31 writes completion status into the active submitted slot.  The
         * readback ring is not a DMA target, and invalidating both complete
         * 1 MiB rings here consumed a material part of every frame budget. */
        if (!ctx->cl_submit_ring.uncached_map) {
            void *submit_entry = avpu_cl_submit_entry_ptr(ctx, cl_idx);

            if (submit_entry)
                avpu_flush_cache_profiled(
                    ctx->fd, submit_entry, (unsigned int)ctx->cl_entry_size,
                    0 /* BIDIRECTIONAL */,
                    OPENIMP_PROFILE_CACHE_COMMAND_COMPLETE);
        }
#else
        if (!ctx->cl_ring.uncached_map && avpu_cl_ring_base(ctx))
            avpu_flush_cache(ctx->fd, ctx->cl_ring.map, 0x100000, 0 /* BIDIRECTIONAL */);
        if (!ctx->cl_submit_ring.uncached_map && avpu_cl_submit_ring_base(ctx))
            avpu_flush_cache(ctx->fd, ctx->cl_submit_ring.map, 0x100000, 0 /* BIDIRECTIONAL */);
#endif
#if defined(PLATFORM_T31) || defined(PLATFORM_T41)
        /* These generations direct hardware writeback into the submitted
         * slot. T41's helper additionally advances to the +0x5c0 status
         * half selected through AVPU_REG_CL_STATUS. */
        status_regs_ptr = avpu_cl_submit_status_ptr(ctx, cl_idx);
        if (status_regs_ptr)
            status_source = "submit";
        else {
            status_regs_ptr = avpu_cl_status_ptr(ctx, cl_idx);
            if (status_regs_ptr)
                status_source = "readback";
        }
#else
        status_regs_ptr = avpu_cl_status_ptr(ctx, cl_idx);
        if (status_regs_ptr)
            status_source = "readback";
        else {
            status_regs_ptr = avpu_cl_submit_status_ptr(ctx, cl_idx);
            if (status_regs_ptr)
                status_source = "submit";
        }
#endif
        if (status_regs_ptr)
            memcpy(status_regs.raw, status_regs_ptr, sizeof(status_regs.raw));
#if defined(PLATFORM_T41)
        if (status_regs_ptr)
            command_slot_ptr = status_regs_ptr - OPENIMP_T41_CL_STATUS_OFFSET;
#endif
    }

#if defined(PLATFORM_T31)
    if (ctx && have_pending && buf_idx >= 0 && buf_idx < 16 &&
        status_regs_ptr) {
        uint32_t payload_size = 0u;

        if (openimp_t31_completion_payload_size(
                status_regs.raw, sizeof(status_regs.raw),
                &payload_size) == 0 &&
            !avpu_t31_payload_size_is_error_fill(payload_size) &&
            ctx->stream_buf_size > (int)AVPU_T31_PAYLOAD_OFFSET &&
            payload_size <= (uint32_t)ctx->stream_buf_size -
                AVPU_T31_PAYLOAD_OFFSET) {
            ctx->t31_payload_size_by_buf[buf_idx] = payload_size;
        } else {
            ctx->t31_payload_size_by_buf[buf_idx] = 0u;
        }
    }
#endif

#if defined(PLATFORM_T41)
    if (ctx && have_pending && buf_idx >= 0 && buf_idx < 16 &&
        status_regs_ptr) {
        uint8_t rate_control_stats[OPENIMP_T41_RC_STATS_SIZE];
        OpenIMPT41RateControlFeedback rate_control_feedback;
        uint32_t payload_size;
        int have_encoding_status;
        int have_rate_control_feedback;

        /* T41 does not use the T31/T40 status layout consumed by
         * EncodingStatusRegsToSliceStatus().  The first word at the
         * command slot's +0x5c0 writeback block is the completed entropy
         * payload size in bytes.  It matched the DMA extent exactly on both
         * encoder channels, including IDR and P pictures. */
        memcpy(&payload_size, status_regs.raw, sizeof(payload_size));
        ctx->t41_payload_size_by_buf[buf_idx] = payload_size;
        have_encoding_status = command_slot_ptr &&
            openimp_t41_command_extract_rate_control_status(
                command_slot_ptr, ctx->cl_entry_size,
                (uint32_t)ctx->stream_buf_size,
                slice_status.raw, sizeof(slice_status.raw),
                rate_control_stats, sizeof(rate_control_stats)) == 0;
        have_rate_control_feedback = have_encoding_status &&
            payload_size <= UINT32_MAX / 8u &&
            openimp_t41_rate_control_extract_feedback(
                slice_status.raw, sizeof(slice_status.raw),
                payload_size * 8u, &rate_control_feedback) == 0;
        if (have_encoding_status &&
            (ctx->frames_encoded < 16 || ctx->frames_encoded % 50 == 0)) {
            uint32_t field_1c;
            uint32_t field_20;
            uint32_t field_38;
            uint32_t entropy_bytes;
            uint32_t entropy_aux;
            uint16_t field_3e;
            uint16_t field_40;
            uint16_t field_42;

            memcpy(&field_1c, slice_status.raw + 0x1cu,
                   sizeof(field_1c));
            memcpy(&field_20, slice_status.raw + 0x20u,
                   sizeof(field_20));
            memcpy(&field_38, slice_status.raw + 0x38u,
                   sizeof(field_38));
            memcpy(&entropy_bytes, rate_control_stats + 0x04u,
                   sizeof(entropy_bytes));
            memcpy(&entropy_aux, rate_control_stats + 0x08u,
                   sizeof(entropy_aux));
            memcpy(&field_3e, slice_status.raw + 0x3eu,
                   sizeof(field_3e));
            memcpy(&field_40, slice_status.raw + 0x40u,
                   sizeof(field_40));
            memcpy(&field_42, slice_status.raw + 0x42u,
                   sizeof(field_42));
            LOG_CODEC("T41 encoding status: buf=%d payload=0x%08x entropy=%08x/%08x field1c=0x%08x field20=0x%08x field38=0x%08x qpfields=%u/%u/%u flags=%u/%u/%u rcfeedback=%u/%u/%u/%u/%u",
                      buf_idx, payload_size, entropy_bytes, entropy_aux,
                      field_1c, field_20, field_38,
                      field_3e, field_40, field_42,
                      (unsigned int)slice_status.raw[0],
                      (unsigned int)slice_status.raw[1],
                      (unsigned int)slice_status.raw[2],
                      have_rate_control_feedback
                          ? rate_control_feedback.block_count : 0u,
                      have_rate_control_feedback
                          ? rate_control_feedback.field_20_percent : 0u,
                      have_rate_control_feedback
                          ? rate_control_feedback.field_1c_percent : 0u,
                      have_rate_control_feedback
                          ? rate_control_feedback.field_14_bit_percent : 0u,
                      have_rate_control_feedback
                          ? rate_control_feedback.field_18_quarters_per_block
                          : 0u);
        }
        if (payload_size > 0u &&
            ctx->stream_buf_size >
                (int)OPENIMP_T41_STREAM_PAYLOAD_OFFSET &&
            payload_size <= (uint32_t)ctx->stream_buf_size -
                OPENIMP_T41_STREAM_PAYLOAD_OFFSET) {
            unsigned int completed_ep3_slot =
                ctx->stream_is_idr[buf_idx] ? 2u : 1u;
            void *ep3_cpu = ctx->rec_trace_buf.uncached_map
                ? ctx->rec_trace_buf.uncached_map
                : ctx->rec_trace_buf.map;
            int ep3_invalidate = ctx->rec_trace_buf.uncached_map
                ? 0
                : avpu_flush_cache_profiled(
                    ctx->fd, ctx->rec_trace_buf.map, 0x100000u,
                    0 /* BIDIRECTIONAL */,
                    OPENIMP_PROFILE_CACHE_EP3_COMPLETE);
            int level_update = ep3_invalidate == 0
                ? openimp_t41_hwrc_level_update(
                    &ctx->t41_hwrc_level, ep3_cpu,
                    ctx->rec_trace_buf.size, completed_ep3_slot)
                : -1;

            if (level_update != 0)
                LOG_CODEC("AVPU: T41 EP3 completion handoff failed invalidate=%d update=%d slot=%u",
                          ep3_invalidate, level_update,
                          completed_ep3_slot);

            if (avpu_t41_exact_rate_control_enabled(ctx) &&
                ctx->t41_rate_controller.initialized) {
                uint32_t used_qp =
                    ctx->t41_rate_control_qp_by_buf[buf_idx];
                int controller_ret =
                    openimp_t41_rate_controller_complete(
                        &ctx->t41_rate_controller, payload_size * 8u,
                        ctx->stream_is_idr[buf_idx],
                        have_rate_control_feedback
                            ? &rate_control_feedback : NULL);

                ctx->t41_rate_control_qp =
                    openimp_t41_rate_controller_qp(
                        &ctx->t41_rate_controller);
                if (ctx->frames_encoded < 16 ||
                    ctx->frames_encoded % 50 == 0) {
                    LOG_CODEC("T41 rate controller: buf=%d idr=%u used=%u next=%u target=%u ema=%u vb=%lld complete=%u/%u ret=%d level=%u",
                              buf_idx, ctx->stream_is_idr[buf_idx], used_qp,
                              ctx->t41_rate_control_qp,
                              ctx->t41_rate_controller.target_bits,
                              ctx->t41_rate_controller.smoothed_p_bits,
                              (long long)ctx->t41_rate_controller.virtual_buffer_bits,
                              ctx->t41_rate_controller.completed_pictures,
                              ctx->t41_rate_controller.completed_p_pictures,
                              controller_ret, ctx->t41_hwrc_level.level);
                }
            }
        }
        bitcount = payload_size;
        completed = payload_size > 0u &&
            ctx->stream_buf_size > (int)OPENIMP_T41_STREAM_PAYLOAD_OFFSET &&
            payload_size <= (uint32_t)ctx->stream_buf_size -
                OPENIMP_T41_STREAM_PAYLOAD_OFFSET;
        completed_flag = completed ? 1u : 0u;
    }
#elif defined(PLATFORM_T31)
    bitcount = ctx && buf_idx >= 0 && buf_idx < 16
        ? ctx->t31_payload_size_by_buf[buf_idx] : 0u;
    completed = bitcount > 0u && bitcount <= UINT32_MAX / 8u &&
        ctx->stream_buf_size > (int)AVPU_T31_PAYLOAD_OFFSET &&
        bitcount <= (uint32_t)ctx->stream_buf_size -
            AVPU_T31_PAYLOAD_OFFSET;
    if (completed && ctx->t31_rate_controller.initialized) {
        uint32_t used_qp = ctx->t31_rate_control_qp_by_buf[buf_idx];
        int controller_ret = openimp_t31_rate_controller_complete(
            &ctx->t31_rate_controller, bitcount * 8u, used_qp,
            ctx->stream_is_idr[buf_idx]);

        if (ctx->frames_encoded < 16 || ctx->frames_encoded % 50 == 0) {
            LOG_CODEC("T31 rate controller: buf=%d idr=%u used=%u next=%u target=%u picture_target=%u p_model=%u@%u idr_ema=%u gop_ema=%u gops=%u streak=%u/%u vb=%lld complete=%u/%u ret=%d",
                      buf_idx, ctx->stream_is_idr[buf_idx], used_qp,
                      ctx->t31_rate_controller.current_qp,
                      ctx->t31_rate_controller.target_bits,
                      ctx->t31_rate_controller.picture_target_bits,
                      ctx->t31_rate_controller.model_p_bits,
                      ctx->t31_rate_controller.model_p_qp,
                      ctx->t31_rate_controller.smoothed_idr_bits,
                      ctx->t31_rate_controller.smoothed_gop_model_bits,
                      ctx->t31_rate_controller.completed_gops,
                      ctx->t31_rate_controller.over_target_gops,
                      ctx->t31_rate_controller.under_target_gops,
                      (long long)ctx->t31_rate_controller.virtual_buffer_bits,
                      ctx->t31_rate_controller.completed_pictures,
                      ctx->t31_rate_controller.completed_p_pictures,
                      controller_ret);
        }
    }
    completed_flag = completed ? 1u : 0u;
#else
    completed = EncodingStatusRegsToSliceStatus(&status_regs, &slice_status);
    MergeEncodingStatus(&merged_status, &slice_status);
    memcpy(&bitcount, slice_status.raw + 0x38, sizeof(bitcount));
    memcpy(&completed_flag, slice_status.raw + 0x10, sizeof(completed_flag));
#if defined(PLATFORM_T31)
    if (ctx && status_regs_ptr && buf_idx >= 0 && buf_idx < 16) {
        uint32_t payload_size = 0u;

        /* EntropyStatusRegsToSliceStatus consumes the 30-bit byte count at
         * +0x104.  Live T31 correlation confirms that adding the generated
         * Annex-B header gives the exact published access-unit length. */
        memcpy(&payload_size, status_regs.raw + 0x104u,
               sizeof(payload_size));
        ctx->t31_payload_size_by_buf[buf_idx] =
            payload_size & 0x3fffffffu;
    }
#endif
#endif
    if (ctx && (ctx->frames_encoded < 16 ||
                ctx->frames_encoded % 50 == 0)) {
        LOG_CODEC("EndEncoding status: done=%d bitcount=0x%08x completed_flag=0x%08x pending=%d buf=%d cl=%u status_src=%s"
#if defined(PLATFORM_T31)
                  " t31_payload=0x%08x"
#elif defined(PLATFORM_T41)
                  " t41_payload=0x%08x"
#elif defined(PLATFORM_T31)
                  " t31_payload=0x%08x"
#endif
                  ,
                  completed, bitcount, completed_flag,
                  have_pending, buf_idx, cl_idx,
                  status_source
#if defined(PLATFORM_T31)
                  , (buf_idx >= 0 && buf_idx < 16)
                        ? ctx->t31_payload_size_by_buf[buf_idx] : 0u
#elif defined(PLATFORM_T41)
                  , (buf_idx >= 0 && buf_idx < 16)
                        ? ctx->t41_payload_size_by_buf[buf_idx] : 0u
#elif defined(PLATFORM_T31)
                  , (buf_idx >= 0 && buf_idx < 16)
                        ? ctx->t31_payload_size_by_buf[buf_idx] : 0u
#endif
                  );
    }

    if (!have_pending || !status_regs_ptr) {
        LOG_CODEC("EndEncoding callback: ignoring IRQ without pending CL-backed status (last_irq=%d pending=%d buf=%d cl=%u)",
                  ctx ? ctx->last_irq_id : -1, have_pending, buf_idx, cl_idx);
        return;
    }

#if defined(PLATFORM_T31)
    if (buf_idx < 0 || buf_idx >= 16 ||
        ctx->stream_buf_size <= (int)AVPU_T31_PAYLOAD_OFFSET ||
        ctx->t31_payload_size_by_buf[buf_idx] == 0u ||
        ctx->t31_payload_size_by_buf[buf_idx] >
            (uint32_t)ctx->stream_buf_size - AVPU_T31_PAYLOAD_OFFSET) {
        /*
         * Continue through normal completion so the pending slot is freed.
         * Effective-size validation will refuse to publish this access unit.
         */
        LOG_CODEC("EndEncoding callback: invalid T31 entropy size=%u capacity=%u buf=%d cl=%u",
                  (buf_idx >= 0 && buf_idx < 16)
                      ? ctx->t31_payload_size_by_buf[buf_idx] : 0u,
                  ctx->stream_buf_size > (int)AVPU_T31_PAYLOAD_OFFSET
                      ? (uint32_t)ctx->stream_buf_size -
                            AVPU_T31_PAYLOAD_OFFSET
                      : 0u,
                  buf_idx, cl_idx);
    }
#endif

    /*
     * The T31 OEM lifecycle masks this core and gates its clock after every
     * completed command list.  Leaving both enabled makes the first picture
     * complete, but the next reset sees the old 0x3 core state and can turn
     * the submitted stream into the AVPU's error-fill pattern without a real
     * completion IRQ.  This is intentionally only mask + clock gate: reset
     * remains in the next submission path, where it cannot race writeback.
     */
#if defined(PLATFORM_T31)
    avpu_write_reg(ctx->fd, AVPU_INTERRUPT_MASK, 0u);
    avpu_turn_off_gc(ctx->fd, 0);
#endif

    /*
     * Publish only after the core is quiescent.  Fifo_Queue wakes Raptor's
     * encoder thread immediately, and publishing first lets the next Process
     * race the two lifecycle writes above.
    */
    openimp_profile_end(OPENIMP_PROFILE_COMPLETION_STATUS, status_profile);
    avpu_complete_frame(ctx, "EndEncoding callback");
    openimp_profile_end(OPENIMP_PROFILE_IRQ_COMPLETION,
                        completion_profile);
}

/* EndAvcEntropy callback - separate OEM IRQ slot (core*4+2).
 * We keep this side-effect free until the exact entropy-completion bookkeeping
 * is recovered; critically, it must NOT be treated as EndEncoding. */
static void avpu_end_avc_entropy_callback(void *user_data)
{
    ALAvpuContext *ctx = (ALAvpuContext*)user_data;
    (void)ctx;
    if (ctx && ctx->frames_encoded % 50 == 0)
    LOG_CODEC("EndAvcEntropy callback (frame %d)", ctx->frames_encoded);
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
    /* WAIT_IRQ has historically copied beyond its four-byte result on some
     * vendor kernels, so retain the proven 0x40-byte slack and 16-byte
     * alignment.  The ioctl is synchronous: one thread-local buffer can be
     * reused for the lifetime of the waiter instead of allocating and freeing
     * a tiny heap object for every encoded frame. */
    uint8_t irq_raw[sizeof(uint32_t) + 0x40]
        __attribute__((aligned(16)));

    ctx->irq_thread_started = 1;
    ctx->irq_thread_exited = 0;
    ctx->irq_wait_errno = 0;
    ctx->last_irq_id = -1;

    LOG_CODEC("IRQ thread: started for fd=%d", fd);

    while (ctx->irq_thread_running) {
        uint32_t *p_irq = (uint32_t *)(void *)irq_raw;

        memset(irq_raw, 0xFF, sizeof(irq_raw));
        *p_irq = 0xFFFFFFFFu;

        /* ioctl($a0_2, 0xc004710c, &var_28) - AL_CMD_IP_WAIT_IRQ */
        if (avpu_sys_ioctl(fd, AL_CMD_IP_WAIT_IRQ, p_irq) == -1) {
            if (errno == EINTR)
                continue; /* interrupted by signal, retry */
            if (errno != EINTR) {
                ctx->irq_wait_errno = errno;
                LOG_CODEC("IRQ thread: WAIT_IRQ failed: %s (%d)", strerror(errno), errno);
            }
            break;
        }

        uint32_t irq_id = *p_irq;
        ctx->irq_wait_errno = 0;

        /* OEM: if (var_28 u>= 0x14) fprintf(stderr, ...) */
        if (irq_id >= 20) {
            LOG_CODEC("IRQ thread: invalid IRQ ID %d", irq_id);
            continue;
        }

        ALAvpuContext *dispatch_ctx = ctx;
#if defined(PLATFORM_T40) || defined(PLATFORM_T31)
        if (g_tseries_irq_owner)
            dispatch_ctx = g_tseries_irq_owner;
#endif
        dispatch_ctx->last_irq_id = (int)irq_id;
        { static unsigned int irq_count = 0; unsigned int c = __sync_add_and_fetch(&irq_count, 1);
          if (c <= 5 || (c % 50) == 0)
            LOG_CODEC("IRQ thread: IRQ %d received [#%u]", irq_id, c);
        }

        /* OEM: Rtos_GetMutex(*(arg1 + 0xc)) */
        pthread_mutex_lock((pthread_mutex_t*)dispatch_ctx->irq_mutex);

        /* OEM: Calculate callback offset: arg1 + (irq_id * 16 - irq_id * 4) + 0x10
         * This is: arg1 + (irq_id * 12) + 0x10
         * Array of 20 entries, each 12 bytes: [callback_fn, user_data, flag]
         */
        int idx = irq_id * 3; /* 3 ints per entry */
        void (*callback)(void*) = (void(*)(void*))dispatch_ctx->irq_callbacks[idx];
        void *user_data = (void*)dispatch_ctx->irq_callbacks[idx + 1];
        int flag = dispatch_ctx->irq_callbacks[idx + 2];

        /* OEM: if ($t9_1 != 0) $t9_1(...) else if (flag == 0) fprintf(stderr, ...) */
        if (callback != NULL) {
            callback(user_data);
        } else if (flag == 0) {
            LOG_CODEC("IRQ thread: Interrupt %d doesn't have a handler", irq_id);
        }

        /* OEM: Rtos_ReleaseMutex(*(arg1 + 0xc)) */
        pthread_mutex_unlock((pthread_mutex_t*)dispatch_ctx->irq_mutex);
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

#if !defined(PLATFORM_T31)
static uint32_t avpu_stream_buffer_raw_end(const uint8_t *buf, size_t maxlen)
{
    size_t end = maxlen;

    if (!buf)
        return 0;

    while (end > 0 && buf[end - 1] == 0)
        end--;

    return (uint32_t)end;
}
#endif

static void avpu_format_hex_preview(const uint8_t *buf, size_t len, char *out, size_t out_sz)
{
    size_t pos = 0;

    if (!out || out_sz == 0)
        return;

    out[0] = '\0';
    if (!buf || len == 0)
        return;

    for (size_t i = 0; i < len && pos + 3 < out_sz; ++i) {
        int wrote = snprintf(out + pos, out_sz - pos, "%02x", buf[i]);
        if (wrote < 0 || (size_t)wrote >= out_sz - pos)
            break;
        pos += (size_t)wrote;
        if (i + 1 < len && pos + 2 < out_sz)
            out[pos++] = ' ';
        out[pos] = '\0';
    }
}

static void avpu_log_suspicious_stream_size(ALAvpuContext *ctx, int buf_idx,
                                            const char *source, uint32_t raw_end,
                                            uint32_t annexb, uint32_t chosen)
{
    const uint8_t *sb;
    uint32_t hdr;
    uint32_t nz_after_hdr = 0;
    int32_t first_nz_after_hdr = -1;
    uint32_t preview_off;
    size_t preview_len;
    char preview[3 * 16];
    unsigned int core_status = 0;
    unsigned int irq_pending = 0;
    unsigned int cl_addr = 0;
    unsigned int status_8230 = 0;
    unsigned int status_8234 = 0;
    unsigned int status_8238 = 0;
    int have_core;
    int have_irq_pending;
    int have_cl_addr;
    int have_status_8230;
    int have_status_8234;
    int have_status_8238;

    if (!ctx || buf_idx < 0 || buf_idx >= ctx->stream_bufs_used)
        return;
    if (!ctx->stream_bufs[buf_idx].map)
        return;

    sb = (const uint8_t *)ctx->stream_bufs[buf_idx].map;
    hdr = ctx->stream_header_offset;
    if (buf_idx >= 0 && buf_idx < 16 && ctx->stream_header_offset_by_buf[buf_idx] != 0)
        hdr = ctx->stream_header_offset_by_buf[buf_idx];

    if (hdr < raw_end) {
        for (uint32_t i = hdr; i < raw_end; ++i) {
            if (sb[i] != 0) {
                nz_after_hdr++;
                if (first_nz_after_hdr < 0)
                    first_nz_after_hdr = (int32_t)i;
            }
        }
    }

    preview_off = hdr < (uint32_t)ctx->stream_buf_size ? hdr : raw_end;
    preview_len = 0;
    if (preview_off < raw_end) {
        preview_len = (size_t)(raw_end - preview_off);
        if (preview_len > 16)
            preview_len = 16;
    }
    avpu_format_hex_preview(sb + preview_off, preview_len, preview, sizeof(preview));

    have_core = (avpu_read_reg_quiet(ctx->fd, AVPU_REG_CORE_STATUS(0), &core_status) == 0);
    have_irq_pending = (avpu_read_reg_quiet(ctx->fd, AVPU_INTERRUPT, &irq_pending) == 0);
    have_cl_addr = (avpu_read_reg_quiet(ctx->fd, AVPU_REG_CL_ADDR, &cl_addr) == 0);
    have_status_8230 = (avpu_read_reg_quiet(ctx->fd, AVPU_REG_CORE_STATUS_8230(0), &status_8230) == 0);
    have_status_8234 = (avpu_read_reg_quiet(ctx->fd, AVPU_REG_CORE_STATUS_8234(0), &status_8234) == 0);
    have_status_8238 = (avpu_read_reg_quiet(ctx->fd, AVPU_REG_CORE_STATUS_8238(0), &status_8238) == 0);

    if (ctx->frames_encoded % 50 == 0)
    LOG_CODEC("%s: suspicious stream buf[%d] hdr=%u raw_end=%u annexb=%u chosen=%u nz_after_hdr=%u first_nz=%d preview_off=%u preview=%s core=%s0x%08x irq=%s0x%08x cl=%s0x%08x stat8230=%s0x%08x stat8234=%s0x%08x stat8238=%s0x%08x",
              source ? source : "EndEncoding",
              buf_idx, hdr, raw_end, annexb, chosen, nz_after_hdr, first_nz_after_hdr,
              preview_off, preview_len ? preview : "<none>",
              have_core ? "" : "ERR:", core_status,
              have_irq_pending ? "" : "ERR:", irq_pending,
              have_cl_addr ? "" : "ERR:", cl_addr,
              have_status_8230 ? "" : "ERR:", status_8230,
              have_status_8234 ? "" : "ERR:", status_8234,
              have_status_8238 ? "" : "ERR:", status_8238);
}

#if !defined(PLATFORM_T31)
/* OEM parity: read the hardware-updated iOffset from the active 0x200 status/
 * command block to determine the encoded byte count. In our manual AVPU path,
 * the hardware-visible submit ring is the only block that can contain live
 * writeback. */
static uint32_t avpu_read_hw_stream_end_from_entry(const uint32_t *cmd)
{
    uint32_t hw_end;

    if (!cmd)
        return 0;

    /* Determine whether this was an Enc2 CL (cmd[0x3e] = byte offset 0xf8)
     * or an Enc1-only CL (cmd[0x32] = byte offset 0xc8). */
    if (cmd[0x3c] != 0)
        hw_end = cmd[0x3e];  /* Enc2: iOffset at word 0x3e */
    else
        hw_end = cmd[0x32];  /* Enc1: iOffset at word 0x32 */

    return hw_end;
}

static uint32_t avpu_read_hw_stream_end(ALAvpuContext *ctx, int buf_idx)
{
    uint32_t cl_idx;
    const uint32_t *readback_cmd;
    const uint32_t *submit_cmd;
    uint32_t readback_end = 0;
    uint32_t submit_end = 0;

    if (!ctx || buf_idx < 0 || buf_idx >= 16)
        return 0;
    if (ctx->cl_entry_size == 0)
        return 0;

#if defined(PLATFORM_T41)
    /* The +0x5c0 completion status already supplied the exact payload size.
     * T41 command words are inputs, not an OutputSlice byte-count source. */
    return 0;
#endif

    cl_idx = ctx->stream_enc2_cl_idx[buf_idx];

    /* Cache-invalidate both mirrored CL rings so we can prefer the CPU-visible
     * readback copy but still compare against the submit copy if needed. */
    if (!ctx->cl_ring.uncached_map && avpu_cl_ring_base(ctx))
        avpu_flush_cache(ctx->fd, ctx->cl_ring.map, 0x100000, 0 /*BIDIRECTIONAL*/);
    if (!ctx->cl_submit_ring.uncached_map)
        avpu_flush_cache(ctx->fd, ctx->cl_submit_ring.map, 0x100000, 0 /*BIDIRECTIONAL*/);

    readback_cmd = (const uint32_t *)avpu_cl_entry_ptr(ctx, cl_idx);
    submit_cmd = (const uint32_t *)avpu_cl_submit_entry_ptr(ctx, cl_idx);

    if (readback_cmd)
        readback_end = avpu_read_hw_stream_end_from_entry(readback_cmd);
    if (submit_cmd)
        submit_end = avpu_read_hw_stream_end_from_entry(submit_cmd);

    return readback_end != 0 ? readback_end : submit_end;
}
#endif

#if defined(PLATFORM_T31)
/*
 * T31's inline Enc2 output is mostly already EBSP escaped, but live 1080p
 * captures occasionally retain 00 00 {00,01,02} in the entropy payload.
 * Annex-B then mistakes 00 00 01 for a new NAL boundary and publishes a
 * truncated picture followed by a bogus NAL.  Preserve existing 00 00 03
 * escape bytes and add only the missing ones while compacting the fixed
 * +0x220 payload behind the host-generated slice prefix.
 *
 * Copy into a CPU-owned buffer while escaping so the published bytes never
 * share storage with the still DMA-visible source.
 */
static uint32_t avpu_t31_copy_entropy_ebsp(uint8_t *destination,
                                           uint32_t capacity,
                                           const uint8_t *source,
                                           uint32_t header_size,
                                           uint32_t payload_offset,
                                           uint32_t payload_size,
                                           uint32_t *inserted_out)
{
    uint32_t inserted = 0u;
    uint32_t zero_run = 0u;
    uint32_t i;
    uint32_t dst = header_size;
    uint32_t slice_payload = 0u;

    if (inserted_out)
        *inserted_out = 0u;
    if (!destination || !source || payload_offset > capacity ||
        payload_size > capacity - payload_offset ||
        header_size > capacity)
        return 0u;

    /* Seed the zero run from the host-generated slice header so an escape is
     * also inserted when the forbidden sequence crosses the join boundary. */
    for (i = 0u; i + 3u < header_size; ++i) {
        if (destination[i] == 0u && destination[i + 1u] == 0u &&
            destination[i + 2u] == 1u) {
            slice_payload = i + 4u;
        } else if (i + 4u < header_size &&
                   destination[i] == 0u && destination[i + 1u] == 0u &&
                   destination[i + 2u] == 0u &&
                   destination[i + 3u] == 1u) {
            slice_payload = i + 5u;
        }
    }
    for (i = slice_payload; i < header_size; ++i)
        zero_run = destination[i] == 0u ? zero_run + 1u : 0u;

    for (i = 0u; i < payload_size; ++i) {
        uint8_t byte = source[payload_offset + i];

        if (zero_run >= 2u) {
            if (byte <= 2u) {
                if (dst >= capacity)
                    return 0u;
                destination[dst++] = 3u;
                ++inserted;
                zero_run = 0u;
            } else if (byte == 3u) {
                zero_run = 0u;
            }
        }
        if (dst >= capacity)
            return 0u;
        destination[dst++] = byte;
        if (byte == 0u)
            ++zero_run;
        else
            zero_run = 0u;
    }

    if (inserted_out)
        *inserted_out = inserted;
    return dst;
}
#endif

static uint32_t avpu_stream_buffer_effective_size(ALAvpuContext *ctx, int buf_idx,
                                                  int *flush_ret_out)
{
    const uint8_t *sb;
    uint32_t raw_end;
#if !defined(PLATFORM_T31)
    uint32_t scanned_raw_end;
#endif
    uint32_t hw_end;
    uint32_t frame_size;
#if !defined(PLATFORM_T31)
    size_t annexb;
#endif
#if defined(PLATFORM_T40)
    unsigned int t40_payload_size = 0;
    unsigned int t40_payload_status = 0;
    int have_t40_payload_size = 0;
    int ignore_t40_status_size = 0;
#endif
#if defined(PLATFORM_T31)
    uint32_t t31_payload_size = 0;
    uint32_t t31_header_size = 0;
    OpenIMPT31StreamLayout t31_layout;
    int have_t31_layout = 0;
#endif
#if defined(PLATFORM_T41)
    OpenIMPT41StreamLayout t41_layout;
    int have_t41_layout = 0;
#endif

    if (flush_ret_out) {
        *flush_ret_out = -1;
    }

    if (!ctx || buf_idx < 0 || buf_idx >= ctx->stream_bufs_used)
        return 0;
    if (!ctx->stream_bufs[buf_idx].map)
        return 0;

#if defined(PLATFORM_T31)
    t31_payload_size = ctx->t31_payload_size_by_buf[buf_idx];
    t31_header_size = ctx->stream_header_offset;
    if (ctx->stream_header_offset_by_buf[buf_idx] != 0u)
        t31_header_size = ctx->stream_header_offset_by_buf[buf_idx];
    if (ctx->stream_buf_size <= 0 ||
        openimp_t31_stream_layout(
            (uint32_t)ctx->stream_buf_size, AVPU_T31_PAYLOAD_OFFSET,
            t31_header_size, t31_payload_size, &t31_layout) != 0) {
        LOG_CODEC("AVPU: refusing T31 completion with invalid payload status=%u capacity=%u buf=%d",
                  t31_payload_size,
                  ctx->stream_buf_size > (int)AVPU_T31_PAYLOAD_OFFSET
                      ? (uint32_t)ctx->stream_buf_size -
                          AVPU_T31_PAYLOAD_OFFSET
                      : 0u,
                  buf_idx);
        return 0u;
    }
    have_t31_layout = 1;
#endif

#if defined(PLATFORM_T31) || defined(PLATFORM_T40)
    /* IRQ 0 reports the inline Enc1/Enc2 command complete before the final
     * stream-buffer DMA burst is guaranteed to be visible to the CPU.  T31
     * also needs this drain: compacting immediately can be overwritten by a
     * late burst, restoring unescaped 00 00 00 01 inside the access unit. */
    usleep(2000);
#endif

    /* Invalidate CPU cache so the completed AVPU DMA is visible. T31's exact
     * completion count bounds this operation to the bytes hardware wrote;
     * generations without an exact extent retain their proven full window. */
    {
        int inv_ret;
#if defined(PLATFORM_T41)
        inv_ret = avpu_flush_cache_profiled(
            ctx->fd, ctx->stream_bufs[buf_idx].map,
            0x100000, 0 /* BIDIRECTIONAL */,
            OPENIMP_PROFILE_CACHE_STREAM_COMPLETE);
#elif defined(PLATFORM_T31)
        inv_ret = avpu_flush_cache_profiled(
            ctx->fd, ctx->stream_bufs[buf_idx].map,
            t31_layout.payload_end, 0 /* BIDIRECTIONAL */,
            OPENIMP_PROFILE_CACHE_STREAM_COMPLETE);
#else
        inv_ret = avpu_flush_cache(ctx->fd,
                                   ctx->stream_bufs[buf_idx].map,
                                   0x100000, 0 /* BIDIRECTIONAL */);
#endif
        if (flush_ret_out)
            *flush_ret_out = inv_ret;
    }

    /* Non-T31 generations may expose a live end position in the command list.
     * T31 instead uses its exact +0x104 completion payload count. */
#if defined(PLATFORM_T31)
    /* T31 cmd[0x32] is the submitted payload start, not the completed
     * stream end.  Never expose that fixed offset as the frame length. */
    hw_end = 0u;
#else
    hw_end = avpu_read_hw_stream_end(ctx, buf_idx);
#endif
#if defined(PLATFORM_T40)
    /* On T40 cmd[0x32] is the submitted header bit position.  The live AVPU
     * status mirror at 0x8304 is the completed entropy payload byte count:
     * OEM sub-IDR=0x5e9, and the former flat probe=0x127 (60+295=355 bytes).
     * Read it before the next submission resets the status window. T41 has
     * no T40 0x8304 register window; its completion lives in the command
     * slot's +0x5c0 status block, so retain the zero-buffer extent scan. */
#if defined(PLATFORM_T41)
    have_t40_payload_size = 0;
    ignore_t40_status_size = 1;
#else
    have_t40_payload_size =
        avpu_read_reg_quiet(ctx->fd, 0x8304u, &t40_payload_status) == 0;
    t40_payload_size = t40_payload_status & 0x3fffffffu;
    ignore_t40_status_size = getenv("OPENIMP_T40_IGNORE_STATUS_SIZE") != NULL;
#endif
    hw_end = 0u;
#endif

    /* Diagnostic: also read cmd[0x3e] and cmd[0x52] from the CL — the AVPU
     * might update a different word for inline Enc2. */
    {
        uint32_t cl_idx_diag = ctx->stream_enc2_cl_idx[buf_idx];
        const uint32_t *cmd_rd = (const uint32_t *)avpu_cl_entry_ptr(ctx, cl_idx_diag);
        const uint32_t *cmd_submit = (const uint32_t *)avpu_cl_submit_entry_ptr(ctx, cl_idx_diag);
        uint32_t hdr_off = ctx->stream_header_offset;
        if (buf_idx >= 0 && buf_idx < 16 && ctx->stream_header_offset_by_buf[buf_idx] != 0)
            hdr_off = ctx->stream_header_offset_by_buf[buf_idx];
        if (ctx->frames_encoded % 50 == 0) {
            if (cmd_rd) {
                LOG_CODEC("AVPU diag buf[%d] CL[%u] readback: cmd32=0x%08x cmd3e=0x%08x cmd52=0x%08x cmd3c=0x%08x hdr_off=%u",
                          buf_idx, cl_idx_diag, cmd_rd[0x32], cmd_rd[0x3e], cmd_rd[0x52],
                          cmd_rd[0x3c], hdr_off);
            }
            if (cmd_submit) {
                LOG_CODEC("AVPU diag buf[%d] CL[%u] submit: cmd32=0x%08x cmd3e=0x%08x cmd52=0x%08x cmd3c=0x%08x hdr_off=%u",
                          buf_idx, cl_idx_diag, cmd_submit[0x32], cmd_submit[0x3e], cmd_submit[0x52],
                          cmd_submit[0x3c], hdr_off);
            }
        }
    }

    sb = (const uint8_t *)ctx->stream_bufs[buf_idx].map;
#if defined(PLATFORM_T31)
    raw_end = 0u;
#elif defined(PLATFORM_T41)
    /* T41 completion reports the entropy byte count. Scanning the entire
     * multi-megabyte stream buffer to rediscover it is both redundant and
     * large enough to halve throughput on the target CPU. */
    raw_end = 0u;
    scanned_raw_end = 0u;
#else
    raw_end = avpu_stream_buffer_raw_end(sb, (size_t)ctx->stream_buf_size);
    scanned_raw_end = raw_end;
#endif

#if defined(PLATFORM_T31)
    {
        const uint32_t payload_offset = AVPU_T31_PAYLOAD_OFFSET;
        uint32_t header_size = t31_header_size;
        uint8_t *mutable_stream =
            (uint8_t *)(ctx->stream_bufs[buf_idx].uncached_map
                ? ctx->stream_bufs[buf_idx].uncached_map
                : ctx->stream_bufs[buf_idx].map);

        raw_end = t31_layout.payload_end;

        if (ctx->frames_encoded < 3 ||
            (ctx->frames_encoded % AVPU_LOG_INTERVAL) == 0) {
            LOG_CODEC("AVPU: T31 payload buf[%d] bytes=%u exact_raw_end=%u compacted=%u header=%u",
                      buf_idx, t31_payload_size,
                      t31_layout.payload_end,
                      t31_layout.access_unit_size,
                      header_size);
        }
        if (ctx->frames_encoded == 0u) {
            uint32_t dump_offset;

            for (dump_offset = 0x1f0u; dump_offset < 0x290u;
                 dump_offset += 16u) {
                LOG_CODEC("AVPU: T31 raw %03x: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                          dump_offset,
                          mutable_stream[dump_offset + 0u],
                          mutable_stream[dump_offset + 1u],
                          mutable_stream[dump_offset + 2u],
                          mutable_stream[dump_offset + 3u],
                          mutable_stream[dump_offset + 4u],
                          mutable_stream[dump_offset + 5u],
                          mutable_stream[dump_offset + 6u],
                          mutable_stream[dump_offset + 7u],
                          mutable_stream[dump_offset + 8u],
                          mutable_stream[dump_offset + 9u],
                          mutable_stream[dump_offset + 10u],
                          mutable_stream[dump_offset + 11u],
                          mutable_stream[dump_offset + 12u],
                          mutable_stream[dump_offset + 13u],
                          mutable_stream[dump_offset + 14u],
                          mutable_stream[dump_offset + 15u]);
            }
        }

        if (!have_t31_layout)
            return 0u;

        /* The inline T31 command writes entropy data at the OEM +0x220
         * boundary.  Escape it directly into a CPU-owned per-slot snapshot;
         * never compact inside the DMA buffer that hardware can still touch. */
        if (raw_end > payload_offset && header_size <= payload_offset) {
            uint32_t payload_size = t31_payload_size;
            uint32_t inserted = 0u;
            uint8_t *public_stream;

            if (!ctx->stream_public_copy[buf_idx])
                ctx->stream_public_copy[buf_idx] =
                    malloc((size_t)ctx->stream_buf_size);
            public_stream = (uint8_t *)ctx->stream_public_copy[buf_idx];
            if (!public_stream)
                return 0u;

            if (ctx->stream_header_shadow &&
                header_size <= AVPU_T31_STREAM_PREFIX_BYTES) {
                memcpy(public_stream,
                       ctx->stream_header_shadow +
                           (size_t)buf_idx * AVPU_T31_STREAM_PREFIX_BYTES,
                       header_size);
            } else {
                memcpy(public_stream, ctx->stream_bufs[buf_idx].map,
                       header_size);
            }

            raw_end = avpu_t31_copy_entropy_ebsp(
                public_stream, (uint32_t)ctx->stream_buf_size,
                mutable_stream, header_size, payload_offset,
                payload_size, &inserted);
            if (raw_end == 0u) {
                LOG_CODEC("AVPU: refusing T31 payload snapshot header=%u payload=%u capacity=%u buf=%d",
                          header_size, payload_size,
                          (uint32_t)ctx->stream_buf_size, buf_idx);
                return 0u;
            }
            if (inserted != 0u &&
                (ctx->frames_encoded < 3 ||
                 (ctx->frames_encoded % AVPU_LOG_INTERVAL) == 0u))
                LOG_CODEC("AVPU: T31 EBSP normalized buf[%d] inserted=%u access_unit=%u",
                          buf_idx, inserted, raw_end);
            sb = public_stream;
        }
    }
#endif

#if defined(PLATFORM_T40)
    {
        const uint32_t payload_offset = 0x220u;
        uint32_t header_size = ctx->stream_header_offset;
        uint8_t *mutable_stream = (uint8_t *)ctx->stream_bufs[buf_idx].map;

        if (buf_idx >= 0 && buf_idx < 16 &&
            ctx->stream_header_offset_by_buf[buf_idx] != 0u)
            header_size = ctx->stream_header_offset_by_buf[buf_idx];
#if defined(PLATFORM_T41)
        {
            uint32_t t41_payload_size =
                ctx->t41_payload_size_by_buf[buf_idx];

            if (ctx->stream_buf_size > 0 &&
                openimp_t41_stream_layout(
                    (uint32_t)ctx->stream_buf_size, payload_offset,
                    header_size, t41_payload_size, &t41_layout) == 0) {
                raw_end = t41_layout.payload_end;
                have_t41_layout = 1;
                LOG_CODEC_THROTTLE(ctx,
                                   "AVPU: T41 payload status bytes=%u scan_end=%u compacted=%u",
                                   t41_payload_size, scanned_raw_end,
                                   t41_layout.access_unit_size);
            } else {
                LOG_CODEC("AVPU: refusing T41 completion with invalid payload status=%u capacity=%u buf=%d",
                          t41_payload_size,
                          ctx->stream_buf_size > (int)payload_offset
                              ? (uint32_t)ctx->stream_buf_size - payload_offset
                              : 0u,
                          buf_idx);
                return 0u;
            }
        }
#else
        if (!ignore_t40_status_size &&
            have_t40_payload_size && t40_payload_size > 0u &&
            t40_payload_size <= (uint32_t)ctx->stream_buf_size - payload_offset) {
            raw_end = payload_offset + t40_payload_size;
            LOG_CODEC_THROTTLE(ctx,
                               "AVPU: T40 payload status bytes=%u compacted=%u",
                               t40_payload_size, header_size + t40_payload_size);
        }
#endif
        if (ctx->frames_encoded < 3) {
#if !defined(PLATFORM_T41)
            unsigned int status_values[12] = {0};
            static const unsigned int status_regs[12] = {
                0x8304u, 0x8308u, 0x830cu, 0x8310u,
                0x8314u, 0x8320u, 0x8324u, 0x8330u,
                0x8334u, 0x8338u, 0x835cu, 0x8364u
            };
            unsigned int valid_mask = 0u;

            for (unsigned int i = 0; i < 12u; ++i) {
                if (avpu_read_reg_quiet(ctx->fd, status_regs[i],
                                        &status_values[i]) == 0)
                    valid_mask |= 1u << i;
            }
            LOG_CODEC("AVPU: T40 status buf[%d] valid=0x%03x scan_end=%u status8304=%08x payload8304=%u chosen_raw_end=%u ignore_status=%d "
                      "8308=%08x 830c=%08x 8310=%08x 8314=%08x 8320=%08x 8324=%08x "
                      "8330=%08x 8334=%08x 8338=%08x 835c=%08x 8364=%08x",
                      buf_idx, valid_mask, scanned_raw_end, t40_payload_status,
                      t40_payload_size,
                      raw_end, ignore_t40_status_size,
                      status_values[1], status_values[2], status_values[3],
                      status_values[4], status_values[5], status_values[6],
                      status_values[7], status_values[8], status_values[9],
                      status_values[10], status_values[11]);
#else
            LOG_CODEC("AVPU: T41 payload buf[%d] scan_end=%u chosen_raw_end=%u",
                      buf_idx, scanned_raw_end, raw_end);
#endif
        }
        if (raw_end > payload_offset && header_size <= payload_offset) {
            uint32_t payload_size = raw_end - payload_offset;
            OpenIMPProfileStamp compact_profile = openimp_profile_begin();
            memmove(mutable_stream + header_size,
                    mutable_stream + payload_offset, payload_size);
            openimp_profile_count(OPENIMP_PROFILE_COMPACT_BYTES,
                                  payload_size);
            openimp_profile_end(OPENIMP_PROFILE_STREAM_COMPACT,
                                compact_profile);
#if defined(PLATFORM_T41)
            raw_end = t41_layout.access_unit_size;
#else
            raw_end = header_size + payload_size;
#endif
#if !defined(PLATFORM_T41)
            if (raw_end < (uint32_t)ctx->stream_buf_size)
                memset(mutable_stream + raw_end, 0,
                       (size_t)ctx->stream_buf_size - raw_end);
#endif
#if defined(PLATFORM_T41)
            /* Raptor consumes the same pages through its FrameSource rmem
             * alias.  Publish the compacted AU to RAM before invalidating
             * that second alias in avpu_queue_completed_stream(). */
            if (avpu_flush_cache_profiled(
                    ctx->fd, mutable_stream, raw_end, 1 /* WBACK */,
                    OPENIMP_PROFILE_CACHE_STREAM_PUBLISH) != 0)
                LOG_CODEC("AVPU: T41 compacted stream writeback failed buf[%d] len=%u",
                          buf_idx, raw_end);
#endif
            sb = mutable_stream;
        }
    }
#endif

    /* Diagnostic: dump bytes at header offset and at 0x200 to see where AVPU wrote */
    if (ctx->frames_encoded < 3) {
        const uint32_t *w = (const uint32_t *)sb;
        LOG_CODEC("AVPU diag buf[%d] stream @0x000: %08x %08x %08x %08x",
                  buf_idx, w[0], w[1], w[2], w[3]);
        /* Keep the startup diagnostic within the completed AU extent. */
        {
            uint32_t hdr_off = ctx->stream_header_offset;
            if (buf_idx >= 0 && buf_idx < 16 && ctx->stream_header_offset_by_buf[buf_idx] != 0)
                hdr_off = ctx->stream_header_offset_by_buf[buf_idx];
            uint32_t hdr_words = (hdr_off + 3) / 4;
            uint32_t total_words = (raw_end + 3u) / 4u;
            int first_nz_off = -1;
            for (uint32_t i = hdr_words; i < total_words; i++) {
                if (w[i] != 0) { first_nz_off = (int)(i * 4); break; }
            }
            LOG_CODEC("AVPU diag buf[%d] raw_end=%u hw_end=%u first_nz_after_hdr=%d",
                      buf_idx, raw_end, hw_end, first_nz_off);
            if (first_nz_off >= 0) {
                uint32_t fi = (uint32_t)first_nz_off / 4;
                LOG_CODEC("AVPU diag buf[%d] stream @0x%03x: %08x %08x %08x %08x",
                          buf_idx, first_nz_off, w[fi], w[fi+1], w[fi+2], w[fi+3]);
            }
        }
    }

    {
        uint32_t hdr_off = ctx->stream_header_offset;
        if (buf_idx >= 0 && buf_idx < 16 && ctx->stream_header_offset_by_buf[buf_idx] != 0)
            hdr_off = ctx->stream_header_offset_by_buf[buf_idx];

        /* T31/T41 completion supplies an exact, validated entropy-byte count.
         * Rescanning for Annex-B start codes is redundant and may trim a
         * legitimate zero-valued entropy byte. */
#if defined(PLATFORM_T31)
        /* EBSP normalization can only grow the compacted access unit. */
        if (!have_t31_layout || raw_end < t31_layout.access_unit_size ||
            raw_end > (uint32_t)ctx->stream_buf_size)
            return 0;
        frame_size = raw_end;
#elif defined(PLATFORM_T41)
        if (!have_t41_layout || raw_end != t41_layout.access_unit_size)
            return 0;
        frame_size = t41_layout.access_unit_size;
#else
        /* Use the HW-reported end if it's sane; fall back to a diagnostic
         * Annex-B extent scan on generations without an exact length. */
        if (hw_end > hdr_off &&
            hw_end <= (uint32_t)ctx->stream_buf_size) {
            frame_size = hw_end;
        } else {
            if (raw_end == 0)
                return 0;
            annexb = annexb_effective_size(sb, raw_end);
            frame_size = annexb > 0 ? (uint32_t)annexb : raw_end;
        }
#endif

        if (frame_size <= hdr_off) {
            avpu_log_suspicious_stream_size(ctx, buf_idx, "EndEncoding",
                                            raw_end, (uint32_t)annexb_effective_size(sb, raw_end),
                                            frame_size);
        }
    }

#if defined(PLATFORM_T41)
    {
        const char *dump_dir = getenv("OPENIMP_T41_DUMP_FIRST_AU");

        if (dump_dir && dump_dir[0] != '\0' && ctx->frames_encoded == 1) {
            char dump_path[256];
            int dump_fd;

            snprintf(dump_path, sizeof(dump_path),
                     "%s/openimp-t41-%ux%u-first.h264",
                     dump_dir, ctx->enc_w, ctx->enc_h);
            dump_fd = open(dump_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
            if (dump_fd >= 0) {
                size_t written = 0u;

                while (written < frame_size) {
                    ssize_t n = write(dump_fd, sb + written,
                                      frame_size - written);
                    if (n <= 0)
                        break;
                    written += (size_t)n;
                }
                close(dump_fd);
                LOG_CODEC("AVPU: dumped first T41 AU %s bytes=%zu/%u",
                          dump_path, written, frame_size);
            }
        }
    }
#endif

    return frame_size;
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
    int force_next_idr;             /* Per-codec RequestIDR latch */
    int last_error;                 /* Best-effort OEM-like sticky error */
    IMPEncoderRcAttr rc_attr_cache; /* Control-plane cache for wrapper APIs */
    IMPEncoderFrmRate fps_cache;
    IMPEncoderGopAttr gop_cache;
    int loop_filter_beta_offset;
    int loop_filter_tc_offset;
    /* Direct-AVPU completion descriptors have the same lifetime as their DMA
     * slots.  Keeping them here avoids IRQ-side heap churn and gives each
     * queued buffer a stable descriptor, matching the ownership model needed
     * by mmap/DMABUF-style consumers. */
    HWStreamBuffer avpu_stream_descriptors[16];
    ALAvpuContext avpu;            /* Vendor-like AL over /dev/avpu (scaffolding) */
#if defined(PLATFORM_T23)
    T23HelixBridge t23_helix;      /* T23 Helix /dev/soc_vpu bootstrap */
#endif
#if defined(PLATFORM_T30)
    T30HelixEncoder *t30_helix;    /* Native T30 /dev/soc_vpu encoder */
#endif
};

#if !defined(PLATFORM_T40) && !defined(PLATFORM_T31)
static int avpu_can_use_high_profile_template(const AL_CodecEncode *enc)
{
    return enc && enc->avpu.enc_w != 0u && enc->avpu.enc_h != 0u &&
           enc->avpu.profile == HW_PROFILE_HIGH &&
           enc->avpu.entropy_mode != 0u;
}
#endif

static uint32_t codec_param_read_input_width(const uint8_t *param)
{
    uint16_t width16 = 0;
    uint32_t width32 = 0;

    if (!param)
        return 0;

    memcpy(&width16, param + 0x08, sizeof(width16));
    if (width16 != 0)
        return (uint32_t)width16;

    memcpy(&width16, param + 0x0c, sizeof(width16));
    if (width16 != 0)
        return (uint32_t)width16;

    memcpy(&width32, param + 0x14, sizeof(width32));
    if (width32 != 0 && width32 != 0x188u)
        return width32;

    return 0;
}

static uint32_t codec_param_read_input_height(const uint8_t *param)
{
    uint16_t height16 = 0;
    uint32_t height32 = 0;

    if (!param)
        return 0;

    memcpy(&height16, param + 0x0a, sizeof(height16));
    if (height16 != 0)
        return (uint32_t)height16;

    memcpy(&height16, param + 0x0e, sizeof(height16));
    if (height16 != 0)
        return (uint32_t)height16;

    memcpy(&height32, param + 0x18, sizeof(height32));
    return height32;
}

static uint32_t codec_param_read_codec_type(const uint8_t *param)
{
    uint32_t profile_word = 0;
    uint32_t codec_type;
    uint32_t profile_idc;

    if (!param)
        return IMP_ENC_TYPE_AVC;

    memcpy(&profile_word, param + 0x20, sizeof(profile_word));
    codec_type = (profile_word >> 24) & 0xffu;
    if (codec_type == IMP_ENC_TYPE_AVC ||
        codec_type == IMP_ENC_TYPE_HEVC ||
        codec_type == IMP_ENC_TYPE_JPEG)
        return codec_type;

    profile_idc = profile_word & 0xffu;
    switch (profile_idc) {
    case IMP_ENC_AVC_PROFILE_IDC_BASELINE:
    case IMP_ENC_AVC_PROFILE_IDC_MAIN:
    case IMP_ENC_AVC_PROFILE_IDC_HIGH:
        return IMP_ENC_TYPE_AVC;
    default:
        return IMP_ENC_TYPE_AVC;
    }
}

static uint32_t codec_param_read_profile_idc(const uint8_t *param)
{
    uint32_t profile_word = 0;
    uint32_t profile_idc = 0;

    if (!param)
        return IMP_ENC_AVC_PROFILE_IDC_MAIN;

    memcpy(&profile_word, param + 0x20, sizeof(profile_word));
    if (((profile_word >> 24) & 0xffu) == IMP_ENC_TYPE_AVC)
        return profile_word & 0xffu;
    if (((profile_word >> 24) & 0xffu) == IMP_ENC_TYPE_HEVC)
        return profile_word & 0xffu;
    if (((profile_word >> 24) & 0xffu) == IMP_ENC_TYPE_JPEG)
        return 0;

    memcpy(&profile_idc, param + 0x24, sizeof(profile_idc));
    if (profile_idc == IMP_ENC_AVC_PROFILE_IDC_BASELINE ||
        profile_idc == IMP_ENC_AVC_PROFILE_IDC_MAIN ||
        profile_idc == IMP_ENC_AVC_PROFILE_IDC_HIGH)
        return profile_idc;

    profile_idc = profile_word & 0xffu;
    if (profile_idc == IMP_ENC_AVC_PROFILE_IDC_BASELINE ||
        profile_idc == IMP_ENC_AVC_PROFILE_IDC_MAIN ||
        profile_idc == IMP_ENC_AVC_PROFILE_IDC_HIGH)
        return profile_idc;

    return IMP_ENC_AVC_PROFILE_IDC_MAIN;
}

static uint32_t codec_param_read_rc_mode(const uint8_t *param)
{
    uint32_t rc_mode = 0;

    if (!param)
        return HW_RC_MODE_CBR;

    memcpy(&rc_mode, param + 0x6c, sizeof(rc_mode));
    if (rc_mode == HW_RC_MODE_FIXQP ||
        rc_mode == HW_RC_MODE_CBR ||
        rc_mode == HW_RC_MODE_VBR)
        return rc_mode;

    memcpy(&rc_mode, param + 0x2c, sizeof(rc_mode));
    if (rc_mode == HW_RC_MODE_FIXQP ||
        rc_mode == HW_RC_MODE_CBR ||
        rc_mode == HW_RC_MODE_VBR)
        return rc_mode;

    return HW_RC_MODE_CBR;
}

static uint32_t codec_param_read_bitrate_bps(const uint8_t *param)
{
    uint32_t target_bps = 0;
    uint32_t max_bps = 0;
    uint32_t legacy = 0;

    if (!param)
        return 0;

    memcpy(&target_bps, param + 0x7c, sizeof(target_bps));
    if (target_bps != 0)
        return target_bps;

    memcpy(&max_bps, param + 0x80, sizeof(max_bps));
    if (max_bps != 0)
        return max_bps;

    memcpy(&legacy, param + 0x30, sizeof(legacy));
    if (legacy != 0 && legacy < 1000000u)
        return legacy * 1000u;

    return legacy;
}

static uint32_t codec_param_read_fps_num(const uint8_t *param)
{
    uint16_t fps_num16 = 0;
    uint32_t fps_num32 = 0;

    if (!param)
        return 25;

    memcpy(&fps_num16, param + 0x78, sizeof(fps_num16));
    if (fps_num16 != 0)
        return (uint32_t)fps_num16;

    memcpy(&fps_num32, param + 0x7c, sizeof(fps_num32));
    if (fps_num32 > 0 && fps_num32 <= 240u)
        return fps_num32;

    return 25;
}

static uint32_t codec_param_read_fps_den(const uint8_t *param)
{
    uint16_t fps_clk16 = 0;
    uint32_t fps_den32 = 0;

    if (!param)
        return 1;

    memcpy(&fps_clk16, param + 0x7a, sizeof(fps_clk16));
    if (fps_clk16 != 0) {
        if (fps_clk16 >= 1000u && (fps_clk16 % 1000u) == 0u)
            return (uint32_t)(fps_clk16 / 1000u);
        return 1;
    }

    memcpy(&fps_den32, param + 0x80, sizeof(fps_den32));
    if (fps_den32 > 0 && fps_den32 <= 1000u)
        return fps_den32;

    return 1;
}

static uint32_t codec_param_read_initial_qp(const uint8_t *param)
{
    uint16_t qp16 = 0;
    uint32_t qp32 = 0;

    if (!param)
        return 26;

    memcpy(&qp16, param + 0x84, sizeof(qp16));
    if (qp16 != 0 && qp16 <= 51u)
        return (uint32_t)qp16;

    memcpy(&qp32, param + 0x38, sizeof(qp32));
    if (qp32 <= 51u)
        return qp32;

    return 26;
}

static uint32_t codec_param_read_min_qp(const uint8_t *param)
{
    int8_t qp8 = 0;
    uint32_t qp32 = 0;

    if (!param)
        return 15;

    memcpy(&qp8, param + 0x86, sizeof(qp8));
    if (qp8 > 0 && qp8 <= 51)
        return (uint32_t)qp8;

    memcpy(&qp32, param + 0x40, sizeof(qp32));
    if (qp32 <= 51u)
        return qp32;

    return 15;
}

static uint32_t codec_param_read_max_qp(const uint8_t *param)
{
    uint16_t qp16 = 0;
    uint32_t qp32 = 0;

    if (!param)
        return 45;

    memcpy(&qp16, param + 0x88, sizeof(qp16));
    if (qp16 != 0 && qp16 <= 51u)
        return (uint32_t)qp16;

    memcpy(&qp32, param + 0x3c, sizeof(qp32));
    if (qp32 <= 51u)
        return qp32;

    return 45;
}

static void codec_param_write_input_resolution(uint8_t *param, uint32_t width, uint32_t height)
{
    if (!param)
        return;

    *(uint16_t *)(param + 0x08) = (uint16_t)width;
    *(uint16_t *)(param + 0x0a) = (uint16_t)height;
    *(uint16_t *)(param + 0x0c) = (uint16_t)width;
    *(uint16_t *)(param + 0x0e) = (uint16_t)height;
}

static void codec_param_write_fps(uint8_t *param, const IMPEncoderFrmRate *rate)
{
    uint32_t num;
    uint32_t den;
    uint32_t clk;

    if (!param || !rate)
        return;

    num = rate->frmRateNum ? rate->frmRateNum : 25u;
    den = rate->frmRateDen ? rate->frmRateDen : 1u;
    clk = den * 1000u;
    if (clk > 0xffffu)
        clk = 0xffffu;

    *(uint16_t *)(param + 0x78) = (uint16_t)num;
    *(uint16_t *)(param + 0x7a) = (uint16_t)clk;
}

static void codec_param_write_bitrate_bps(uint8_t *param, uint32_t bitrate_bps)
{
    if (!param)
        return;

    *(uint32_t *)(param + 0x7c) = bitrate_bps;
    *(uint32_t *)(param + 0x80) = bitrate_bps;
}

static void codec_param_write_qp_bounds(uint8_t *param, uint32_t initial_qp,
                                        uint32_t min_qp, uint32_t max_qp)
{
    if (!param)
        return;

    *(uint16_t *)(param + 0x84) = (uint16_t)clamp_qp_u32(initial_qp);
    *(int8_t *)(param + 0x86) = (int8_t)clamp_qp_u32(min_qp);
    *(uint16_t *)(param + 0x88) = (uint16_t)clamp_qp_u32(max_qp);
}

static void avpu_sync_runtime_encode_state(AL_CodecEncode *enc)
{
    uint32_t profile_idc;

    if (!enc)
        return;

    enc->avpu.enc_w = codec_param_read_input_width(enc->codec_param);
    enc->avpu.enc_h = codec_param_read_input_height(enc->codec_param);
    enc->avpu.fps_num = enc->fps_cache.frmRateNum ? enc->fps_cache.frmRateNum : enc->hw_params.fps_num;
    enc->avpu.fps_den = enc->fps_cache.frmRateDen ? enc->fps_cache.frmRateDen : enc->hw_params.fps_den;
    enc->avpu.bitrate = enc->hw_params.bitrate;
    enc->avpu.rc_mode = enc->hw_params.rc_mode;
    enc->avpu.qp = enc->hw_params.qp;
    enc->avpu.min_qp = enc->hw_params.min_qp;
    enc->avpu.max_qp = enc->hw_params.max_qp;
    enc->avpu.entropy_mode = enc->entropy_mode;
    enc->avpu.gop_length = enc->gop_cache.gopLength ? enc->gop_cache.gopLength : enc->hw_params.gop_length;
    enc->avpu.format_word = *(uint32_t *)(enc->codec_param + 0x10);

    profile_idc = codec_param_read_profile_idc(enc->codec_param);
    switch (profile_idc) {
    case 66:
        enc->avpu.profile = HW_PROFILE_BASELINE;
        break;
    case 100:
        enc->avpu.profile = HW_PROFILE_HIGH;
        break;
    case 77:
        enc->avpu.profile = HW_PROFILE_MAIN;
        break;
    default:
        enc->avpu.profile = enc->hw_params.profile;
        break;
    }

#if defined(PLATFORM_T40)
    /* The T40 oracle uses the High/CABAC command template for both the
     * 1920x1080 and 640x360 Raptor AVC channels. */
    enc->avpu.profile = HW_PROFILE_HIGH;
    enc->avpu.entropy_mode = 1u;
#elif defined(PLATFORM_T31)
    /* T31 has recovered command oracles for High/CABAC and
     * Baseline/CAVLC, but not a separate Main/CABAC control shape.  Main
     * requests already generate CABAC headers, so promote them to the proven
     * High command template instead of silently pairing those headers with
     * the incomplete Baseline command window. */
    if (enc->avpu.entropy_mode != 0u) {
        if (enc->avpu.profile != HW_PROFILE_HIGH) {
            LOG_CODEC_THROTTLE(&enc->avpu,
                               "AVPU: promoting %ux%u AVC profile=%u CABAC request to proven T31 High/CABAC template",
                               enc->avpu.enc_w, enc->avpu.enc_h,
                               enc->avpu.profile);
        }
        enc->avpu.profile = HW_PROFILE_HIGH;
    } else {
        enc->avpu.profile = HW_PROFILE_BASELINE;
    }
#else
    if (!avpu_can_use_high_profile_template(enc)) {
        if (enc->avpu.profile != HW_PROFILE_BASELINE || enc->avpu.entropy_mode != 0u) {
            LOG_CODEC_THROTTLE(&enc->avpu,
                               "AVPU: coercing %ux%u AVC profile=%u entropy=%u to Baseline/CAVLC until non-1080p OEM templates are recovered",
                               enc->avpu.enc_w, enc->avpu.enc_h,
                               enc->avpu.profile, enc->avpu.entropy_mode);
        }
        enc->avpu.profile = HW_PROFILE_BASELINE;
        enc->avpu.entropy_mode = 0u;
    }
#endif

    avpu_init_enc1_slice_words(&enc->avpu, enc->codec_param);
}

static void codec_set_error(AL_CodecEncode *enc, int err)
{
    if (!enc)
        return;
    enc->last_error = err;
}

static void codec_sync_rc_cache(AL_CodecEncode *enc)
{
    IMPEncoderRcAttr *rc;
    uint32_t rc_mode;
    uint32_t bitrate_kbps;
    uint32_t qp;

    if (!enc)
        return;

    rc = &enc->rc_attr_cache;
    memset(rc, 0, sizeof(*rc));

    rc_mode = *(uint32_t *)(enc->codec_param + 0x2c);
    switch (rc_mode) {
    case HW_RC_MODE_FIXQP:
        rc->attrRcMode.rcMode = IMP_ENC_RC_MODE_FIXQP;
        break;
    case HW_RC_MODE_CBR:
        rc->attrRcMode.rcMode = IMP_ENC_RC_MODE_CBR;
        break;
    case HW_RC_MODE_VBR:
        rc->attrRcMode.rcMode = IMP_ENC_RC_MODE_VBR;
        break;
    default:
        rc->attrRcMode.rcMode = IMP_ENC_RC_MODE_FIXQP;
        break;
    }

    bitrate_kbps = enc->hw_params.bitrate;
    qp = enc->hw_params.qp;

    rc->outFrmRate = enc->fps_cache;
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    rc->maxGop = enc->gop_cache.gopLength;
#elif !(defined(PLATFORM_T31) || defined(PLATFORM_T40) || defined(PLATFORM_T41))
    rc->attrGop = enc->gop_cache;
#endif

    switch (rc->attrRcMode.rcMode) {
    case IMP_ENC_RC_MODE_CBR:
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
        rc->attrRcMode.attrH264Cbr.outBitRate = bitrate_kbps / 1000u;
        rc->attrRcMode.attrH264Cbr.maxQp = enc->hw_params.max_qp;
        rc->attrRcMode.attrH264Cbr.minQp = enc->hw_params.min_qp;
        rc->attrRcMode.attrH264Cbr.iBiasLvl = 0;
        rc->attrRcMode.attrH264Cbr.frmQPStep = 0;
        rc->attrRcMode.attrH264Cbr.gopQPStep = 0;
        rc->attrRcMode.attrH264Cbr.adaptiveMode = false;
        rc->attrRcMode.attrH264Cbr.gopRelation = false;
#elif defined(PLATFORM_T31) || defined(PLATFORM_T40) || defined(PLATFORM_T41)
        rc->attrRcMode.attrH264Cbr.uTargetBitRate = bitrate_kbps;
        rc->attrRcMode.attrH264Cbr.iInitialQP = (int16_t)clamp_qp_u32(qp);
        rc->attrRcMode.attrH264Cbr.iMinQP = (int16_t)clamp_qp_u32(enc->hw_params.min_qp);
        rc->attrRcMode.attrH264Cbr.iMaxQP = (int16_t)clamp_qp_u32(enc->hw_params.max_qp);
        rc->attrRcMode.attrH264Cbr.iIPDelta =
            (int16_t)enc->avpu.qp_ip_delta;
        rc->attrRcMode.attrH264Cbr.iPBDelta = 0;
        rc->attrRcMode.attrH264Cbr.eRcOptions = 0;
        rc->attrRcMode.attrH264Cbr.uMaxPictureSize = 0;
#else
        rc->attrRcMode.attrH264Cbr.outFrmRate = enc->fps_cache.frmRateNum;
        rc->attrRcMode.attrH264Cbr.maxGop = bitrate_kbps;
        rc->attrRcMode.attrH264Cbr.maxQp = enc->hw_params.max_qp;
        rc->attrRcMode.attrH264Cbr.minQp = enc->hw_params.min_qp;
        rc->attrRcMode.attrH264Cbr.iBiasLvl = 0;
        rc->attrRcMode.attrH264Cbr.frmQPStep = 0;
        rc->attrRcMode.attrH264Cbr.gopQPStep = 0;
        rc->attrRcMode.attrH264Cbr.adaptiveMode = 0;
        rc->attrRcMode.attrH264Cbr.gopRelation = 0;
#endif
        break;

    case IMP_ENC_RC_MODE_VBR:
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    case IMP_ENC_RC_MODE_SMART:
#else
    case IMP_ENC_RC_MODE_CAPPED_VBR:
    case IMP_ENC_RC_MODE_CAPPED_QUALITY:
#endif
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
        rc->attrRcMode.attrH264Vbr.maxBitRate = bitrate_kbps / 1000u;
        rc->attrRcMode.attrH264Vbr.maxQp = enc->hw_params.max_qp;
        rc->attrRcMode.attrH264Vbr.minQp = enc->hw_params.min_qp;
        rc->attrRcMode.attrH264Vbr.staticTime = 1;
        rc->attrRcMode.attrH264Vbr.changePos = 80;
        rc->attrRcMode.attrH264Vbr.qualityLvl = 2;
#elif defined(PLATFORM_T31) || defined(PLATFORM_T40) || defined(PLATFORM_T41)
        rc->attrRcMode.attrH264Vbr.uTargetBitRate = bitrate_kbps;
        rc->attrRcMode.attrH264Vbr.uMaxBitRate = bitrate_kbps;
        rc->attrRcMode.attrH264Vbr.iInitialQP = (int16_t)clamp_qp_u32(qp);
        rc->attrRcMode.attrH264Vbr.iMinQP = (int16_t)clamp_qp_u32(enc->hw_params.min_qp);
        rc->attrRcMode.attrH264Vbr.iMaxQP = (int16_t)clamp_qp_u32(enc->hw_params.max_qp);
        rc->attrRcMode.attrH264Vbr.iIPDelta =
            (int16_t)enc->avpu.qp_ip_delta;
        rc->attrRcMode.attrH264Vbr.iPBDelta = 0;
        rc->attrRcMode.attrH264Vbr.eRcOptions = 0;
        rc->attrRcMode.attrH264Vbr.uMaxPictureSize = 0;
#if !defined(PLATFORM_T41)
        rc->attrRcMode.attrH264Vbr.uMaxPSNR = 0;
#endif
#else
        rc->attrRcMode.attrH264Vbr.outFrmRate = enc->fps_cache.frmRateNum;
        rc->attrRcMode.attrH264Vbr.maxGop = bitrate_kbps;
        rc->attrRcMode.attrH264Vbr.maxQp = enc->hw_params.max_qp;
        rc->attrRcMode.attrH264Vbr.minQp = enc->hw_params.min_qp;
        rc->attrRcMode.attrH264Vbr.staticTime = 0;
#endif
        break;

    case IMP_ENC_RC_MODE_FIXQP:
    default:
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
        rc->attrRcMode.attrH264FixQp.qp = clamp_qp_u32(qp);
#elif defined(PLATFORM_T31) || defined(PLATFORM_T40) || defined(PLATFORM_T41)
#if defined(PLATFORM_T41)
        rc->attrRcMode.attrH264FixQp.iInitialQP =
            (int16_t)clamp_qp_u32(qp);
#else
        rc->attrRcMode.attrH264FixQp.qp = (int16_t)clamp_qp_u32(qp);
#endif
#else
        rc->attrRcMode.attrH264FixQp.outFrmRate = enc->fps_cache.frmRateNum;
        rc->attrRcMode.attrH264FixQp.maxGop = enc->gop_cache.gopLength;
        rc->attrRcMode.attrH264FixQp.qp = qp;
#endif
        break;
    }
}

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

static void avpu_hw_stream_set_user_data(HWStreamBuffer *hw_stream, void *user_data)
{
    uintptr_t bits;

    if (!hw_stream) return;

    bits = (uintptr_t)user_data;
    hw_stream->reserved[0] = (uint32_t)(bits & 0xffffffffu);
#if UINTPTR_MAX > 0xffffffffu
    hw_stream->reserved[1] = (uint32_t)((bits >> 32) & 0xffffffffu);
#else
    hw_stream->reserved[1] = 0;
#endif
}

static void *avpu_hw_stream_get_user_data(const HWStreamBuffer *hw_stream)
{
    uintptr_t bits;

    if (!hw_stream) return NULL;

    bits = (uintptr_t)hw_stream->reserved[0];
#if UINTPTR_MAX > 0xffffffffu
    bits |= ((uintptr_t)hw_stream->reserved[1] << 32);
#endif
    return (void *)bits;
}

static int avpu_queue_completed_stream(ALAvpuContext *ctx, int buf_idx, void *user_data,
                                       const char *source, uint32_t *frame_size_out,
                                       int *flush_ret_out)
{
    AL_CodecEncode *enc;
    HWStreamBuffer *hw_stream;
    uint32_t frame_size;
    uint32_t phys_addr;
    uint32_t virt_addr;
    int flush_ret = -1;
    OpenIMPProfileStamp finalize_profile;

    if (frame_size_out)
        *frame_size_out = 0;
    if (flush_ret_out)
        *flush_ret_out = -1;

    if (!ctx || buf_idx < 0 || buf_idx >= ctx->stream_bufs_used)
        return 0;

    enc = (AL_CodecEncode *)ctx->codec_owner;
    if (!enc || !enc->fifo_streams)
        return 0;

    finalize_profile = openimp_profile_begin();
    frame_size = avpu_stream_buffer_effective_size(ctx, buf_idx, &flush_ret);
    openimp_profile_end(OPENIMP_PROFILE_STREAM_FINALIZE, finalize_profile);
    if (frame_size_out)
        *frame_size_out = frame_size;
    if (flush_ret_out)
        *flush_ret_out = flush_ret;

    if (frame_size == 0) {
        LOG_CODEC("%s: completed stream buf[%d] phys=0x%08x has no payload (flush_ret=%d)",
                  source ? source : "EndEncoding",
                  buf_idx, ctx->stream_bufs[buf_idx].phy_addr, flush_ret);
        return 0;
    }
    {
        uint32_t hdr_off = ctx->stream_header_offset;
        if (buf_idx >= 0 && buf_idx < 16 && ctx->stream_header_offset_by_buf[buf_idx] != 0)
            hdr_off = ctx->stream_header_offset_by_buf[buf_idx];
        if (frame_size <= hdr_off) {
            LOG_CODEC("%s: refusing header-only stream buf[%d] len=%u hdr=%u phys=0x%08x",
                      source ? source : "EndEncoding",
                      buf_idx, frame_size, hdr_off,
                      ctx->stream_bufs[buf_idx].phy_addr);
            return 0;
        }
    }

    /* stream_buf_state prevents this DMA slot from being reused until
     * ReleaseStream, so its descriptor can be reused on the same boundary. */
    hw_stream = &enc->avpu_stream_descriptors[buf_idx];
    memset(hw_stream, 0, sizeof(*hw_stream));

    phys_addr = ctx->stream_bufs[buf_idx].phy_addr;
    virt_addr = (uint32_t)(uintptr_t)ctx->stream_bufs[buf_idx].map;
#if defined(PLATFORM_T31)
    /*
     * avpu_stream_buffer_effective_size() already copied and escaped the raw
     * entropy payload into this stable per-slot public buffer.
     */
    if (ctx->stream_public_copy[buf_idx])
        virt_addr = (uint32_t)(uintptr_t)
            ctx->stream_public_copy[buf_idx];
#endif
#if defined(PLATFORM_T41)
    if (!getenv("OPENIMP_T41_STREAM_COPY_MODE") &&
        ctx->stream_public_alias_valid[buf_idx]) {
        uint64_t public_alias = (uint64_t)phys_addr +
            (uint64_t)ctx->stream_public_alias_delta[buf_idx];

        if (public_alias <= UINT32_MAX) {
            virt_addr = (uint32_t)public_alias;
        }
    }
#endif
    hw_stream->phys_addr = phys_addr;
    hw_stream->virt_addr = virt_addr;
    hw_stream->length = frame_size;
    hw_stream->timestamp = ctx->stream_timestamp[buf_idx];
    hw_stream->frame_type = ctx->stream_is_idr[buf_idx]
        ? HW_FRAME_TYPE_I : HW_FRAME_TYPE_P;
    hw_stream->slice_type = ctx->stream_is_idr[buf_idx]
        ? IMP_ENC_SLICE_I : IMP_ENC_SLICE_P;
    avpu_hw_stream_set_user_data(hw_stream, user_data);

    if (ctx->frames_encoded <= 4 || ctx->frames_encoded % 50 == 0)
    LOG_CODEC("%s: queue completed stream buf[%d] stream=%p phys=0x%08x virt=0x%08x len=%u flush_ret=%d user=%p",
              source ? source : "EndEncoding",
              buf_idx, (void *)hw_stream, phys_addr, virt_addr, frame_size,
              flush_ret, user_data);

    if (Fifo_Queue(enc->fifo_streams, hw_stream, -1) == 0) {
        LOG_CODEC("%s: failed to queue completed stream buf[%d]", source ? source : "EndEncoding", buf_idx);
        return 0;
    }

    if (ctx->frames_encoded <= 4 || ctx->frames_encoded % 50 == 0)
    LOG_CODEC("%s: queued completed stream buf[%d] stream=%p phys=0x%08x virt=0x%08x len=%u flush_ret=%d",
              source ? source : "EndEncoding",
              buf_idx, (void *)hw_stream, phys_addr, virt_addr, frame_size, flush_ret);
    return 1;
}

/* Global codec state */
static void *g_pCodec = NULL;
static pthread_mutex_t g_codec_mutex = PTHREAD_MUTEX_INITIALIZER;
static AL_CodecEncode *g_codec_instances[6] = {NULL};

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

int AL_Codec_Encode_SetStreamBufferCount(void *codec, int count)
{
    AL_CodecEncode *enc = (AL_CodecEncode *)codec;

    if (!enc || count < 1 || count > 16)
        return -1;
    /*
     * The pool is materialized lazily with the first submitted frame.  After
     * that point changing the count would require draining and reallocating
     * live DMA buffers, so reject it explicitly.
     */
    if (enc->avpu.fd >= 0 || enc->avpu.stream_bufs_used != 0)
        return -1;

    enc->stream_buf_count = count;
    if (enc->fifo_streams)
        Fifo_Init(enc->fifo_streams, count);
    LOG_CODEC("SetStreamBufferCount: %d", count);
    return 0;
}

/**
 * AL_Codec_Encode_Create - based on decompilation at 0x7950c
 * Creates a codec encoder instance
 */
int AL_Codec_Encode_Create(void **codec, void *params) {
    CODEC_STARTUP_MARKER("openimp/codec marker A0 Create entry\n");
    codec_startup_trace("openimp/codec startup: Create entry codec=%p params=%p\n",
                        codec, params);
    if (codec == NULL || params == NULL) {
        LOG_CODEC("Create: NULL parameters");
        return -1;
    }

    /* Allocate codec structure using real size */
    AL_CodecEncode *enc = (AL_CodecEncode*)malloc(sizeof(AL_CodecEncode));
    CODEC_STARTUP_MARKER("openimp/codec marker A1 malloc returned\n");
    codec_startup_trace("openimp/codec startup: encoder malloc size=%u result=%p\n",
                        (unsigned int)sizeof(AL_CodecEncode), enc);
    if (enc == NULL) {
        LOG_CODEC("Create: malloc failed");
        return -1;
    }

    memset(enc, 0, sizeof(AL_CodecEncode));
    CODEC_STARTUP_MARKER("openimp/codec marker A2 memset returned\n");
    codec_startup_trace("openimp/codec startup: encoder memset done\n");

    /* Sentinel fd values: memset zeroed everything, but fd=0 is stdin,
     * which causes every 'if (enc->avpu.fd >= 0)' check to be true
     * even when /dev/avpu was never opened. Set to -1 explicitly. */
    enc->avpu.fd = -1;
    enc->avpu.event_fd = -1;
    enc->avpu.cl_ring.dmabuf_fd = -1;
    enc->avpu.cl_submit_ring.dmabuf_fd = -1;
    enc->avpu.interm_buf.dmabuf_fd = -1;
    enc->avpu.rec_buf.dmabuf_fd = -1;
    enc->avpu.ref_buf.dmabuf_fd = -1;
    enc->avpu.rec_trace_buf.dmabuf_fd = -1;
    enc->avpu.ref_trace_buf.dmabuf_fd = -1;
    for (int i = 0; i < 16; i++)
        enc->avpu.stream_bufs[i].dmabuf_fd = -1;

    /* Initialize from parameters */
    enc->g_pCodec = g_pCodec;
    memcpy(enc->codec_param, params, 0x794);
    {
        uint32_t profile_idc = codec_param_read_profile_idc(enc->codec_param);
        enc->entropy_mode = (profile_idc == IMP_ENC_AVC_PROFILE_IDC_BASELINE) ? 0u : 1u;
    }

    /* OEM-like callback placeholders and event */
    enc->callback = NULL;
    enc->callback_arg = enc;
    int efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    CODEC_STARTUP_MARKER("openimp/codec marker A3 eventfd returned\n");
    if (efd >= 0) enc->event = (void*)(uintptr_t)efd;
    codec_startup_trace("openimp/codec startup: eventfd=%d\n", efd);

    /* Set default buffer counts and sizes */
    enc->frame_buf_count = 4;           /* Default frame buffer count */
    enc->frame_buf_size = 0x100000;     /* 1MB per frame */
    enc->stream_buf_count = 7;          /* Default stream buffer count */
    enc->stream_buf_size = 0x100000;

    /* Allocate and initialize FIFO control structures safely */
    int fifo_size = Fifo_SizeOf();
    enc->fifo_frames = malloc(fifo_size);
    enc->fifo_streams = malloc(fifo_size);
    CODEC_STARTUP_MARKER("openimp/codec marker A4 fifo malloc returned\n");
    codec_startup_trace("openimp/codec startup: fifo size=%d frames=%p streams=%p\n",
                        fifo_size, enc->fifo_frames, enc->fifo_streams);
    if (enc->fifo_frames == NULL || enc->fifo_streams == NULL) {
        LOG_CODEC("Create: FIFO alloc failed");
        if (enc->fifo_frames) free(enc->fifo_frames);
        if (enc->fifo_streams) free(enc->fifo_streams);
        free(enc);
        return -1;
    }
    Fifo_Init(enc->fifo_frames, enc->frame_buf_count);
    Fifo_Init(enc->fifo_streams, enc->stream_buf_count);
    CODEC_STARTUP_MARKER("openimp/codec marker A5 fifo init returned\n");
    codec_startup_trace("openimp/codec startup: fifo init done\n");

    /* Set source FourCC to NV12 */
    enc->src_fourcc = 0x3231564e;  /* 'NV12' */
    enc->metadata_type = -1;

    /* Attempt to use hardware encoder via /dev/avpu (lazy-init on first frame) */
    enc->hw_encoder_fd = -1;
    enc->use_hardware = 1;
    enc->last_error = 0;

    enc->fps_cache.frmRateNum = codec_param_read_fps_num(enc->codec_param);
    enc->fps_cache.frmRateDen = codec_param_read_fps_den(enc->codec_param);
    if (enc->fps_cache.frmRateNum == 0)
        enc->fps_cache.frmRateNum = 25;
    if (enc->fps_cache.frmRateDen == 0)
        enc->fps_cache.frmRateDen = 1;

    enc->gop_cache.gopLength = *(uint32_t *)(enc->codec_param + 0xb0);
#if defined(PLATFORM_T31) || defined(PLATFORM_T40) || defined(PLATFORM_T41)
    enc->gop_cache.gopMode = IMP_ENC_GOP_CTRL_MODE_DEFAULT;
#else
    enc->gop_cache.ipQpDelta = 2u;
    enc->gop_cache.gopMode = IMP_ENC_GOP_MODE_NORMALP;
#endif

    enc->hw_params.codec_type = codec_param_read_codec_type(enc->codec_param);
    enc->hw_params.width = codec_param_read_input_width(enc->codec_param);
    enc->hw_params.height = codec_param_read_input_height(enc->codec_param);
    if (enc->hw_params.width != 0u && enc->hw_params.height != 0u) {
        enc->frame_buf_size = (int)avpu_get_nv12_frame_size(
            enc->hw_params.width, enc->hw_params.height);
        enc->stream_buf_size = (int)avpu_get_stream_buffer_size(
            enc->hw_params.width, enc->hw_params.height,
            codec_param_read_bitrate_bps(enc->codec_param));
    }
    enc->hw_params.fps_num = enc->fps_cache.frmRateNum;
    enc->hw_params.fps_den = enc->fps_cache.frmRateDen;
    enc->hw_params.gop_length = enc->gop_cache.gopLength ? enc->gop_cache.gopLength : 25u;
    enc->hw_params.rc_mode = codec_param_read_rc_mode(enc->codec_param);
    enc->hw_params.bitrate = codec_param_read_bitrate_bps(enc->codec_param);
    enc->hw_params.qp = codec_param_read_initial_qp(enc->codec_param);
    enc->hw_params.min_qp = codec_param_read_min_qp(enc->codec_param);
    enc->hw_params.max_qp = codec_param_read_max_qp(enc->codec_param);
    if (enc->hw_params.min_qp > enc->hw_params.max_qp) {
        uint32_t tmp = enc->hw_params.min_qp;
        enc->hw_params.min_qp = enc->hw_params.max_qp;
        enc->hw_params.max_qp = tmp;
    }
    enc->avpu.enc_w = enc->hw_params.width;
    enc->avpu.enc_h = enc->hw_params.height;
    enc->avpu.fps_num = enc->hw_params.fps_num;
    enc->avpu.fps_den = enc->hw_params.fps_den;
    enc->avpu.bitrate = enc->hw_params.bitrate;
    enc->avpu.gop_length = enc->hw_params.gop_length;
    enc->avpu.qp = enc->hw_params.qp;
    enc->avpu.min_qp = enc->hw_params.min_qp;
    enc->avpu.max_qp = enc->hw_params.max_qp;
    enc->avpu.qp_ip_delta = -1;
    enc->avpu.rc_mode = enc->hw_params.rc_mode;

    enc->loop_filter_beta_offset = 0;
    enc->loop_filter_tc_offset = 0;

    codec_sync_rc_cache(enc);
    CODEC_STARTUP_MARKER("openimp/codec marker A6 params complete\n");
    codec_startup_trace("openimp/codec startup: params done size=%ux%u bitrate=%u qp=%u/%u/%u\n",
                        (unsigned int)enc->hw_params.width,
                        (unsigned int)enc->hw_params.height,
                        (unsigned int)enc->hw_params.bitrate,
                        (unsigned int)enc->hw_params.qp,
                        (unsigned int)enc->hw_params.min_qp,
                        (unsigned int)enc->hw_params.max_qp);

    LOG_CODEC("Create: hardware encoder will be attempted via /dev/avpu (lazy init)");

    /* Register in global instances */
    pthread_mutex_lock(&g_codec_mutex);
    CODEC_STARTUP_MARKER("openimp/codec marker A7 global lock acquired\n");
    codec_startup_trace("openimp/codec startup: global codec lock acquired\n");
    for (int i = 0; i < 6; i++) {
        if (g_codec_instances[i] == NULL) {
            g_codec_instances[i] = enc;
            enc->channel_id = i + 1;
            pthread_mutex_unlock(&g_codec_mutex);

            *codec = enc;
            CODEC_STARTUP_MARKER("openimp/codec marker A8 Create complete\n");
            codec_startup_trace("openimp/codec startup: Create done slot=%d codec=%p\n",
                                i, enc);
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
    codec_startup_trace(
        "openimp/codec teardown: entry codec=%p fd=%d ready=%d frames=%d/%d "
        "drained=%u pending=%d irq=%p running=%d\n",
        codec, enc->avpu.fd, enc->avpu.session_ready,
        enc->avpu.frames_encoded, enc->avpu.frames_consumed,
        enc->avpu.completions_drained, enc->avpu.pending_stream_count,
        enc->avpu.irq_thread, enc->avpu.irq_thread_running);

#if defined(PLATFORM_T23)
    OpenIMP_T23_HelixExit(&enc->t23_helix);
#endif
#if defined(PLATFORM_T30)
    OpenIMP_T30_HelixDestroy(enc->t30_helix);
    enc->t30_helix = NULL;
#endif

    /* Deinitialize hardware encoder(s) - OEM parity: no separate deinit function */
    if (enc->use_hardware == 2 && enc->avpu.fd >= 0) {
#if defined(PLATFORM_T40) || defined(PLATFORM_T31)
        /* The channel has already been stopped before codec destruction, so
         * no further CL can be submitted here.  Quiesce the T-series core before
         * releasing any DMA mapping.  Leaving GC clocked with the completed
         * CL and stream mappings torn down makes the next unrelated AXI user
         * (notably Wi-Fi/SCP) wedge the SoC.
         *
         * Keep the reset triplet back-to-back, as in ResetCore, and mask/ack
         * completion first so the waiter cannot dispatch into buffers while
         * destruction proceeds. */
        if (enc->avpu.session_ready) {
            unsigned int core_status = 0;
            unsigned int clkcmd = 0;
            unsigned int irq_mask = 0;
            unsigned int irq_pending = 0;
            int status_ret;
            int clk_ret;
            int mask_read_ret;
            int pending_ret;
            int mask_ret;
            int ack_ret;
            int reset1_ret;
            int reset2_ret;
            int reset4_ret;

            status_ret = avpu_read_reg_quiet(enc->avpu.fd,
                                             AVPU_REG_CORE_STATUS(0),
                                             &core_status);
            clk_ret = avpu_read_reg_quiet(enc->avpu.fd,
                                          AVPU_REG_CORE_CLKCMD(0), &clkcmd);
            mask_read_ret = avpu_read_reg_quiet(enc->avpu.fd,
                                                AVPU_INTERRUPT_MASK,
                                                &irq_mask);
            pending_ret = avpu_read_reg_quiet(enc->avpu.fd,
                                              AVPU_INTERRUPT,
                                              &irq_pending);
            codec_startup_trace(
                "openimp/codec teardown: pre-quiesce status=%d:0x%08x "
                "clk=%d:0x%08x mask=%d:0x%08x pending=%d:0x%08x\n",
                status_ret, core_status, clk_ret, clkcmd,
                mask_read_ret, irq_mask, pending_ret, irq_pending);

            mask_ret = avpu_write_reg(enc->avpu.fd, AVPU_INTERRUPT_MASK, 0u);
            codec_startup_trace(
                "openimp/codec teardown: irq masked ret=%d\n", mask_ret);
            ack_ret = avpu_write_reg(enc->avpu.fd, AVPU_INTERRUPT,
                                     AVPU_IRQ_CLEAR_MASK);
            codec_startup_trace(
                "openimp/codec teardown: irq acked ret=%d\n", ack_ret);
            reset1_ret = avpu_write_reg(enc->avpu.fd,
                                        AVPU_REG_CORE_RESET(0), 1u);
            reset2_ret = avpu_write_reg(enc->avpu.fd,
                                        AVPU_REG_CORE_RESET(0), 2u);
            reset4_ret = avpu_write_reg(enc->avpu.fd,
                                        AVPU_REG_CORE_RESET(0), 4u);
            codec_startup_trace(
                "openimp/codec teardown: reset triplet ret=%d/%d/%d\n",
                reset1_ret, reset2_ret, reset4_ret);
            avpu_turn_off_gc(enc->avpu.fd, 0);
            codec_startup_trace(
                "openimp/codec teardown: core clock gated\n");
            enc->avpu.session_ready = 0;
            LOG_CODEC("AVPU: T-series core quiesced before DMA teardown");
        }
#endif
        /* Stop and join the IRQ waiter while its file and every DMA mapping
         * are still valid.  The old order tore down those resources first,
         * leaving a completion callback able to touch unmapped memory and a
         * blocked WAIT_IRQ thread joined only after its pooled fd was closed. */
        if (enc->avpu.irq_thread) {
            pthread_t tid = (pthread_t)enc->avpu.irq_thread;

            enc->avpu.irq_thread_running = 0;
            codec_startup_trace(
                "openimp/codec teardown: unblocking irq waiter\n");
            avpu_sys_ioctl(enc->avpu.fd, AL_CMD_UNBLOCK_CHANNEL, NULL);
            codec_startup_trace(
                "openimp/codec teardown: joining irq waiter\n");
            pthread_join(tid, NULL);
            enc->avpu.irq_thread = 0;
            codec_startup_trace(
                "openimp/codec teardown: irq waiter joined\n");
            LOG_CODEC("AVPU: IRQ thread joined before DMA teardown");
        }

        /* Drop every DMA mapping before closing /dev/avpu.  Its single-channel
         * driver releases codec->chan only after the final VMA/file reference. */
        codec_startup_trace(
            "openimp/codec teardown: releasing dma buffers count=%d\n",
            enc->avpu.stream_bufs_used);
        for (int i = 0; i < enc->avpu.stream_bufs_used; ++i) {
            free(enc->avpu.stream_public_copy[i]);
            enc->avpu.stream_public_copy[i] = NULL;
            avpu_release_dma_buf(&enc->avpu.stream_bufs[i]);
        }
        avpu_release_dma_buf(&enc->avpu.cl_ring);
        avpu_release_dma_buf(&enc->avpu.cl_submit_ring);
        avpu_release_dma_buf(&enc->avpu.interm_buf);
        avpu_release_dma_buf(&enc->avpu.rec_buf);
        avpu_release_dma_buf(&enc->avpu.ref_buf);
        avpu_release_dma_buf(&enc->avpu.rec_trace_buf);
        avpu_release_dma_buf(&enc->avpu.ref_trace_buf);
        codec_startup_trace(
            "openimp/codec teardown: dma buffers released\n");

        codec_startup_trace(
            "openimp/codec teardown: closing device pool fd=%d\n",
            enc->avpu.fd);
        AL_DevicePool_Close(enc->avpu.fd);
        enc->avpu.fd = -1;
        codec_startup_trace(
            "openimp/codec teardown: device pool closed\n");
#if defined(PLATFORM_T40) || defined(PLATFORM_T31)
        pthread_mutex_lock(&g_tseries_irq_host_lock);
        if (g_tseries_irq_owner == &enc->avpu)
            g_tseries_irq_owner = NULL;
        if (g_tseries_irq_host == &enc->avpu)
            g_tseries_irq_host = NULL;
        pthread_mutex_unlock(&g_tseries_irq_host_lock);
#endif
        /* Destroy IRQ mutex if allocated */
        if (enc->avpu.irq_mutex) {
            pthread_mutex_destroy((pthread_mutex_t*)enc->avpu.irq_mutex);
            free(enc->avpu.irq_mutex);
            enc->avpu.irq_mutex = NULL;
        }
        if (enc->avpu.stream_queue_mutex) {
            pthread_mutex_destroy((pthread_mutex_t*)enc->avpu.stream_queue_mutex);
            free(enc->avpu.stream_queue_mutex);
            enc->avpu.stream_queue_mutex = NULL;
        }
        free(enc->avpu.stream_header_shadow);
        enc->avpu.stream_header_shadow = NULL;

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

/* Emit a standards-compliant baseline grayscale JPEG for the T40 snapshot
 * channels.  The P2 JPEG path is intentionally metadata-only for now, but it
 * must still produce an image that rvd and ordinary decoders can consume.
 * A one-symbol DC table (category 0) plus a one-symbol AC table (EOB) encodes
 * every 8x8 block as constant grey in two bits. */
#if defined(PLATFORM_T40)
static int t40_encode_gray_jpeg(uint32_t width, uint32_t height,
                                uint64_t timestamp, HWStreamBuffer *stream)
{
    uint32_t blocks_x;
    uint32_t blocks_y;
    uint32_t blocks;
    uint32_t scan_bytes;
    uint32_t used_bits;
    uint32_t padding_bits;
    size_t capacity;
    uint8_t *buf;
    size_t pos = 0;
    unsigned int i;

    if (!stream || width == 0u || height == 0u ||
        width > 65535u || height > 65535u)
        return -1;
    blocks_x = (width + 7u) >> 3;
    blocks_y = (height + 7u) >> 3;
    if (blocks_x != 0u && blocks_y > UINT32_MAX / blocks_x)
        return -1;
    blocks = blocks_x * blocks_y;
    scan_bytes = (blocks + 3u) >> 2; /* two zero Huffman bits per block */
    capacity = (size_t)scan_bytes + 256u;
    buf = (uint8_t *)malloc(capacity);
    if (!buf)
        return -1;

#define JPEG_BYTE(value) do { buf[pos++] = (uint8_t)(value); } while (0)
    JPEG_BYTE(0xff); JPEG_BYTE(0xd8);             /* SOI */
    JPEG_BYTE(0xff); JPEG_BYTE(0xe0);             /* APP0 JFIF */
    JPEG_BYTE(0x00); JPEG_BYTE(0x10);
    JPEG_BYTE('J'); JPEG_BYTE('F'); JPEG_BYTE('I'); JPEG_BYTE('F'); JPEG_BYTE(0);
    JPEG_BYTE(1); JPEG_BYTE(1); JPEG_BYTE(0);
    JPEG_BYTE(0); JPEG_BYTE(1); JPEG_BYTE(0); JPEG_BYTE(1); JPEG_BYTE(0); JPEG_BYTE(0);

    JPEG_BYTE(0xff); JPEG_BYTE(0xdb);             /* one 8-bit DQT */
    JPEG_BYTE(0x00); JPEG_BYTE(0x43); JPEG_BYTE(0);
    for (i = 0; i < 64u; i++) JPEG_BYTE(16);

    JPEG_BYTE(0xff); JPEG_BYTE(0xc0);             /* baseline SOF */
    JPEG_BYTE(0x00); JPEG_BYTE(0x0b); JPEG_BYTE(8);
    JPEG_BYTE(height >> 8); JPEG_BYTE(height);
    JPEG_BYTE(width >> 8); JPEG_BYTE(width);
    JPEG_BYTE(1); JPEG_BYTE(1); JPEG_BYTE(0x11); JPEG_BYTE(0);

    JPEG_BYTE(0xff); JPEG_BYTE(0xc4);             /* DC0: code 0 -> category 0 */
    JPEG_BYTE(0x00); JPEG_BYTE(0x14); JPEG_BYTE(0x00);
    JPEG_BYTE(1); for (i = 1; i < 16u; i++) JPEG_BYTE(0); JPEG_BYTE(0);
    JPEG_BYTE(0xff); JPEG_BYTE(0xc4);             /* AC0: code 0 -> EOB */
    JPEG_BYTE(0x00); JPEG_BYTE(0x14); JPEG_BYTE(0x10);
    JPEG_BYTE(1); for (i = 1; i < 16u; i++) JPEG_BYTE(0); JPEG_BYTE(0);

    JPEG_BYTE(0xff); JPEG_BYTE(0xda);             /* grayscale scan */
    JPEG_BYTE(0x00); JPEG_BYTE(0x08); JPEG_BYTE(1);
    JPEG_BYTE(1); JPEG_BYTE(0); JPEG_BYTE(0); JPEG_BYTE(63); JPEG_BYTE(0);
    memset(buf + pos, 0, scan_bytes);
    pos += scan_bytes;
    used_bits = (blocks & 3u) * 2u;
    if (used_bits != 0u) {
        padding_bits = 8u - used_bits;
        buf[pos - 1u] |= (uint8_t)((1u << padding_bits) - 1u);
    }
    JPEG_BYTE(0xff); JPEG_BYTE(0xd9);             /* EOI */
#undef JPEG_BYTE

    stream->virt_addr = (uint32_t)(uintptr_t)buf;
    stream->phys_addr = 0u;
    stream->length = (uint32_t)pos;
    stream->timestamp = timestamp;
    stream->frame_type = HW_FRAME_TYPE_I;
    stream->slice_type = 0u;
    return 0;
}
#endif

#if defined(PLATFORM_T31)
static void avpu_t31_dump_source_once(int channel_id, uint32_t width,
                                      uint32_t height,
                                      const uint8_t *source, uint32_t size)
{
    static volatile unsigned int dumped_channels;
    const char *dump_dir = getenv("OPENIMP_T31_DUMP_SOURCE_DIR");
    unsigned int channel_bit;
    char dump_path[192];
    uint32_t written = 0u;
    int dump_fd;

    if (!dump_dir || !dump_dir[0] || !source || !size ||
        channel_id < 0 || channel_id >= 32)
        return;

    channel_bit = 1u << (unsigned int)channel_id;
    if (__sync_fetch_and_or(&dumped_channels, channel_bit) & channel_bit)
        return;

    snprintf(dump_path, sizeof(dump_path),
             "%s/openimp-source-ch%d-%ux%u.nv12", dump_dir, channel_id,
             width, height);
    dump_fd = open(dump_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (dump_fd < 0) {
        LOG_CODEC("AVPU: source dump open failed path=%s errno=%d",
                  dump_path, errno);
        return;
    }

    while (written < size) {
        ssize_t result = write(dump_fd, source + written, size - written);

        if (result <= 0)
            break;
        written += (uint32_t)result;
    }
    close(dump_fd);
    LOG_CODEC("AVPU: source dump path=%s bytes=%u/%u",
              dump_path, written, size);
}

static void avpu_t31_start_companion_stage(ALAvpuContext *ctx, int fd,
                                            uint32_t width, uint32_t height,
                                            uint32_t phys_addr,
                                            const uint32_t *cmd, int buf_idx,
                                            int trace_submit)
{
    uint32_t y_plane_sz = avpu_get_nv12_luma_plane_size(width, height);
    uint32_t stream_part_offset = cmd[0x31];
    uint32_t hw_hdr_offset = 0x200u;
    uint32_t hw_stream_budget =
        stream_part_offset > hw_hdr_offset
            ? stream_part_offset - hw_hdr_offset : 0u;
    int source_cfg_stream_idx = buf_idx;
    uint32_t source_cfg_stream = cmd[0x30];

    if (ctx->stream_bufs_used > 1) {
        source_cfg_stream_idx = (buf_idx + 1) % ctx->stream_bufs_used;
        source_cfg_stream =
            ctx->stream_bufs[source_cfg_stream_idx].phy_addr;
    }

    avpu_write_reg(fd, AVPU_REG_ENC_EN_B, 0x00000001);
    avpu_write_reg(fd, AVPU_REG_ENC_EN_A, 0x00000001);

    avpu_write_reg(fd, 0x8400, 0x00000131u);
    avpu_write_reg(fd, 0x8404,
                   ((width - 1u) << 16) | (height - 1u));
    avpu_write_reg(fd, 0x8408, 0x00010001u);
    avpu_write_reg(fd, 0x840c, width);
    avpu_write_reg(fd, 0x8410, phys_addr);
    avpu_write_reg(fd, 0x8414, phys_addr + y_plane_sz);
    avpu_write_reg(fd, 0x8418, ctx->interm_buf.phy_addr
                   + ctx->interm_ep1_size); /* WPP start */
    avpu_write_reg(fd, 0x841c, source_cfg_stream);
    avpu_write_reg(fd, 0x8420, stream_part_offset);
    avpu_write_reg(fd, 0x8424, hw_hdr_offset);
    avpu_write_reg(fd, 0x8428, hw_stream_budget);

    avpu_write_reg(fd, AVPU_REG_ENC_EN_C, 0x00000001);

    if (trace_submit) {
        unsigned int cfg_8400 = 0, cfg_8404 = 0, cfg_8408 = 0;
        unsigned int cfg_840c = 0, cfg_8410 = 0, cfg_8414 = 0;
        unsigned int cfg_8418 = 0, cfg_841c = 0, cfg_8420 = 0;
        unsigned int cfg_8424 = 0, cfg_8428 = 0, cfg_85e4 = 0;

        avpu_read_reg_quiet(fd, 0x8400, &cfg_8400);
        avpu_read_reg_quiet(fd, 0x8404, &cfg_8404);
        avpu_read_reg_quiet(fd, 0x8408, &cfg_8408);
        avpu_read_reg_quiet(fd, 0x840c, &cfg_840c);
        avpu_read_reg_quiet(fd, 0x8410, &cfg_8410);
        avpu_read_reg_quiet(fd, 0x8414, &cfg_8414);
        avpu_read_reg_quiet(fd, 0x8418, &cfg_8418);
        avpu_read_reg_quiet(fd, 0x841c, &cfg_841c);
        avpu_read_reg_quiet(fd, 0x8420, &cfg_8420);
        avpu_read_reg_quiet(fd, 0x8424, &cfg_8424);
        avpu_read_reg_quiet(fd, 0x8428, &cfg_8428);
        avpu_read_reg_quiet(fd, AVPU_REG_ENC_EN_C, &cfg_85e4);
        LOG_CODEC("AVPU: post-CL source cfg 8400=%08x 8404=%08x 8408=%08x 840c=%08x 8410=%08x 8414=%08x",
                  cfg_8400, cfg_8404, cfg_8408, cfg_840c, cfg_8410,
                  cfg_8414);
        LOG_CODEC("AVPU: post-CL source cfg 8418=%08x 841c=%08x 8420=%08x 8424=%08x 8428=%08x 85e4=%08x",
                  cfg_8418, cfg_841c, cfg_8420, cfg_8424, cfg_8428,
                  cfg_85e4);
        LOG_CODEC("AVPU: source cfg stream active_idx=%d active=0x%08x alternate_idx=%d alternate=0x%08x",
                  buf_idx, cmd[0x30], source_cfg_stream_idx,
                  source_cfg_stream);
    }

    if (ctx->frame_number % 50 == 0)
        LOG_CODEC("AVPU: T31 encoder config AFTER CL_PUSH while IRQ mutex held");
}
#endif

/**
 * AL_Codec_Encode_Process - based on decompilation at 0x7a334
 * Process a frame for encoding
 */
static int al_codec_encode_process_impl(void *codec, void *frame,
                                        void *user_data) {
    OpenIMPProfileStamp submit_profile;
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

    submit_profile = openimp_profile_begin();

    /* The direct AVPU path publishes a descriptor from its completion
     * callback, so it does not need a speculative per-submit allocation.
     * Legacy and software paths allocate their descriptor only when selected
     * below. */
    HWStreamBuffer *hw_stream = NULL;

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

    /* Throttled: log every 50th frame to avoid syslog overload at 25fps */

    memcpy(&width, frame_bytes + 0x08, sizeof(uint32_t));
    memcpy(&height, frame_bytes + 0x0c, sizeof(uint32_t));
    memcpy(&pixfmt, frame_bytes + 0x10, sizeof(uint32_t));
    memcpy(&size, frame_bytes + 0x14, sizeof(uint32_t));
    memcpy(&phys_addr, frame_bytes + 0x18, sizeof(uint32_t));
    memcpy(&virt_addr, frame_bytes + 0x1c, sizeof(uint32_t));

#if defined(PLATFORM_T40) || defined(PLATFORM_T31)
    /* The ISP has just DMA-written this rmem buffer.  Drop any cached CPU
     * alias before later command-list writebacks can evict stale allocation
     * data over the captured frame.  This is a device-to-device ownership
     * transition: ISP DMA -> AVPU DMA, so invalidate rather than write back.
     *
     * This is required on T31 as well as T40.  The T31 Enc1 submit performs
     * the same OEM-shaped 1 MB cache writeback for its command-list window;
     * without invalidating the captured frame first, that writeback can push
     * stale cached source lines over the ISP's newest DMA frame. */
    if (phys_addr && virt_addr && size) {
        int source_sync_ret;
#if defined(PLATFORM_T41)
        source_sync_ret = avpu_flush_cache_profiled(
            -1, (void *)(uintptr_t)virt_addr, size,
            2 /* DMA_FROM_DEVICE / invalidate */,
            OPENIMP_PROFILE_CACHE_SOURCE_INVALIDATE);
#else
        source_sync_ret = avpu_flush_cache(
            -1, (void *)(uintptr_t)virt_addr, size,
            2 /* DMA_FROM_DEVICE / invalidate */);
#endif
        static unsigned int source_sync_count;
        static int collect_source_stats = -1;
        unsigned int source_sync_index =
            __sync_add_and_fetch(&source_sync_count, 1u);

        if (collect_source_stats < 0) {
            const char *stats_env = getenv("OPENIMP_SOURCE_STATS");

            collect_source_stats =
                stats_env && stats_env[0] && stats_env[0] != '0';
#if defined(PLATFORM_T31)
            if (!collect_source_stats) {
                stats_env = getenv("OPENIMP_T31_FULL_FRAME_STATS");
                collect_source_stats =
                    stats_env && stats_env[0] && stats_env[0] != '0';
            }
#endif
        }

        if (collect_source_stats) {
            OpenIMPProfileStamp stats_profile = openimp_profile_begin();
            const uint8_t *source = (const uint8_t *)(uintptr_t)virt_addr;
            uint32_t luma_size = width * height;
            uint32_t luma_storage_size = size * 2u / 3u;
            uint32_t luma_step;
            uint32_t luma_sample_limit;
            uint32_t uv_size =
                size > luma_storage_size ? size - luma_storage_size : 0u;
            uint32_t uv_step = (uv_size / 2U) / 1024U * 2U;
            uint32_t sample_count = 0;
            uint64_t sample_sum = 0;
            uint32_t uv_count = 0;
            uint64_t u_sum = 0;
            uint64_t v_sum = 0;
            uint32_t offset;
            uint32_t luma_mean;
            uint32_t luma_mean_milli;
            uint32_t u_mean = 128U;
            uint32_t v_mean = 128U;
#if defined(PLATFORM_T31)
            static int full_source_stats = -1;

            if (full_source_stats < 0) {
                const char *stats_env =
                    getenv("OPENIMP_T31_FULL_FRAME_STATS");

                full_source_stats =
                    stats_env && stats_env[0] && stats_env[0] != '0';
            }
#else
            const int full_source_stats = 0;
#endif

            luma_sample_limit = full_source_stats ? luma_size : 4096u;
            luma_step =
                full_source_stats ? 1u : luma_size / luma_sample_limit;
            if (!luma_step)
                luma_step = 1u;
            for (offset = 0;
                 offset < luma_size && sample_count < luma_sample_limit;
                 offset += luma_step) {
                sample_sum += source[offset];
                sample_count++;
            }
            if (uv_step < 2U)
                uv_step = 2U;
            if (luma_storage_size >= luma_size && uv_size >= 2u) {
                const uint8_t *uv = source + luma_storage_size;

                for (offset = 0; offset + 1U < uv_size &&
                                 uv_count < 1024U;
                     offset += uv_step) {
                    u_sum += uv[offset];
                    v_sum += uv[offset + 1U];
                    uv_count++;
                }
            }
            luma_mean =
                sample_count ? (uint32_t)(sample_sum / sample_count) : 0U;
            luma_mean_milli =
                sample_count
                    ? (uint32_t)(sample_sum * 1000u / sample_count)
                    : 0U;
            if (uv_count) {
                u_mean = (uint32_t)(u_sum / uv_count);
                v_mean = (uint32_t)(v_sum / uv_count);
            }
#if defined(PLATFORM_T40)
            OpenIMP_P3_FrameStats(luma_mean, u_mean, v_mean);
#endif
            if (full_source_stats || source_sync_index <= 4u ||
                (source_sync_index % 25u) == 0u) {
            LOG_CODEC("Process: source sync #%u ret=%d phys=0x%08x "
                      "virt=0x%08x size=%u luma_mean=%u.%03u u=%u v=%u "
                      "samples=%u full=%d",
                      source_sync_index, source_sync_ret, phys_addr,
                      virt_addr, size, luma_mean_milli / 1000u,
                      luma_mean_milli % 1000u, u_mean, v_mean,
                      sample_count, full_source_stats);
            }
            openimp_profile_end(OPENIMP_PROFILE_SOURCE_STATS,
                                stats_profile);
        } else if (source_sync_index <= 4u ||
                   (source_sync_index % 250u) == 0u) {
            LOG_CODEC("Process: source sync #%u ret=%d phys=0x%08x "
                      "virt=0x%08x size=%u stats=disabled",
                      source_sync_index, source_sync_ret, phys_addr,
                      virt_addr, size);
        }
#if defined(PLATFORM_T31)
        avpu_t31_dump_source_once(enc->channel_id, width, height,
                                  (const uint8_t *)(uintptr_t)virt_addr,
                                  size);
#endif
    }
#endif

    /* Preserve the capture-completion timestamp populated by DQBUF instead
     * of replacing it with jittery encoder-start wall time.  T41 releases
     * the FrameSource descriptor immediately after submission, so everything
     * needed at AVPU completion must be copied before Process returns. */
    uint64_t timestamp = 0;
#if defined(PLATFORM_T31) || defined(PLATFORM_T30)
    memcpy(&timestamp, (const uint8_t *)frame + 0x20, sizeof(timestamp));
#elif defined(PLATFORM_T41) || defined(PLATFORM_T23)
    memcpy(&timestamp, (const uint8_t *)frame + 0x28, sizeof(timestamp));
#endif

#if defined(PLATFORM_T30)
    if (codec_param_read_codec_type(enc->codec_param) == IMP_ENC_TYPE_AVC) {
        if (!enc->t30_helix) {
            enc->hw_params.width = width;
            enc->hw_params.height = height;
            if (!enc->hw_params.fps_num)
                enc->hw_params.fps_num = 25u;
            if (!enc->hw_params.fps_den)
                enc->hw_params.fps_den = 1u;
            if (!enc->hw_params.gop_length)
                enc->hw_params.gop_length = 25u;
            if (!enc->hw_params.bitrate)
                enc->hw_params.bitrate = 2000000u;
            if (OpenIMP_T30_HelixCreate(&enc->t30_helix,
                                        &enc->hw_params) != 0) {
                codec_set_error(enc, -1);
                return -1;
            }
            enc->use_hardware = 3;
        }
        if (__sync_lock_test_and_set(&enc->force_next_idr, 0))
            OpenIMP_T30_HelixRequestIDR(enc->t30_helix);
        if (OpenIMP_T30_HelixEncode(enc->t30_helix,
                                    (const IMPFrameInfo *)frame,
                                    &hw_stream) != 0) {
            codec_set_error(enc, -1);
            return -1;
        }
        goto queue_encoded_stream;
    }
#endif
    if (!timestamp)
        timestamp = IMP_System_GetTimeStamp();

#if defined(PLATFORM_T23)
    /* T23 has the Helix encoder at /dev/soc_vpu, not the Allegro AVPU used by
     * the newer T-series backend below.  Use Ingenic's standalone YUV codec
     * seam in a stock-linked helper process while the native open Helix
     * descriptor builder is brought over.  Capture, binding and stream
     * ownership remain entirely in OpenIMP. */
    if (codec_param_read_codec_type(enc->codec_param) == IMP_ENC_TYPE_AVC) {
        if (getenv("OPENIMP_T23_SKIP_HELIX")) {
            codec_set_error(enc, -1);
            return -1;
        }
        if (enc->t23_helix.worker_pid <= 0 && !enc->t23_helix.failed) {
            enc->hw_params.width = width;
            enc->hw_params.height = height;
            if (!enc->hw_params.fps_num)
                enc->hw_params.fps_num = 25u;
            if (!enc->hw_params.fps_den)
                enc->hw_params.fps_den = 1u;
            if (!enc->hw_params.gop_length)
                enc->hw_params.gop_length = 25u;
            if (!enc->hw_params.bitrate)
                enc->hw_params.bitrate = 2000000u;
            if (OpenIMP_T23_HelixInit(&enc->t23_helix,
                                      &enc->hw_params) != 0) {
                enc->use_hardware = 0;
                LOG_CODEC("Process: T23 Helix init failed; using software fallback");
            } else {
                enc->use_hardware = 3;
            }
        }
        if (enc->t23_helix.worker_pid > 0) {
            if (__sync_lock_test_and_set(&enc->force_next_idr, 0))
                OpenIMP_T23_HelixRequestIDR(&enc->t23_helix);
            if (OpenIMP_T23_HelixEncode(&enc->t23_helix,
                                        (const IMPFrameInfo *)frame,
                                        &hw_stream) != 0)
                return -1;
            goto queue_encoded_stream;
        }
    }
#endif

    if (enc->use_hardware) {
        /* Lazy-init hardware encoder on first frame */
        if (enc->hw_encoder_fd < 0) {
            /* Build parameters from codec_param (written by channel_encoder_init) */
            uint32_t bitrate_bps = codec_param_read_bitrate_bps(enc->codec_param);
            uint32_t fps_num = codec_param_read_fps_num(enc->codec_param);
            uint32_t fps_den = codec_param_read_fps_den(enc->codec_param);
            uint32_t gop = *(uint32_t*)(enc->codec_param + 0xb0);
            uint32_t profile_idc = codec_param_read_profile_idc(enc->codec_param);
            uint32_t codec_type = codec_param_read_codec_type(enc->codec_param);
            uint32_t rc_mode = codec_param_read_rc_mode(enc->codec_param);
            { static int li_log = 0; if (++li_log <= 3)
                LOG_CODEC("Process: lazy-init channel_id=%d %ux%u codec_type=%u profile_idc=%u entropy_mode=%u",
                          enc->channel_id, width, height, codec_type, profile_idc, enc->entropy_mode);
            }
            uint32_t init_qp = codec_param_read_initial_qp(enc->codec_param);
            uint32_t max_qp = codec_param_read_max_qp(enc->codec_param);
            uint32_t min_qp = codec_param_read_min_qp(enc->codec_param);
            memset(&enc->hw_params, 0, sizeof(enc->hw_params));
            enc->hw_params.codec_type = codec_type;
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
            enc->hw_params.bitrate = bitrate_bps ? bitrate_bps : 2*1000*1000u;
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

            /* OEM parity: each AVC encoder instance may initialize its own
             * AVPU-backed session. The earlier single-owner gate starved chn0
             * as soon as chn1 opened /dev/avpu, which matches the "no stream
             * on chn0" failure seen on target. */
            int try_avpu = (codec_type == IMP_ENC_TYPE_AVC);

            if (try_avpu) {
                if (enc->avpu.fd > 2) {
                    /* Already open for this channel; do not re-open */
                    enc->use_hardware = 2; /* 2 = AL/AVPU path */
                    /* OEM parity: no ALAvpu_SetEvent - event_fd stored directly */
                    if (enc->event) {
                        enc->avpu.event_fd = (int)(uintptr_t)enc->event;
                    }
                    { static int ao_log = 0; if (++ao_log <= 2)
                        LOG_CODEC("AVPU: channel=%d already open (fd=%d); skipping re-open", enc->channel_id - 1, enc->avpu.fd);
                    }
                } else {
                    /* Open device via device pool (OEM parity: AL_DevicePool_Open at 0x362dc) */
                    int fd = AL_DevicePool_Open("/dev/avpu");
                    codec_startup_trace(
                        "openimp/codec startup: device pool open fd=%d\n", fd);
                    if (fd >= 0) {
                        /* Initialize AVPU context directly (OEM parity: no ALAvpu_Init wrapper) */
                        memset(&enc->avpu, 0, sizeof(enc->avpu));
                        enc->avpu.fd = fd;
                        enc->avpu.event_fd = enc->event ? (int)(uintptr_t)enc->event : -1;
                        enc->avpu.cl_ring.dmabuf_fd = -1;
                        enc->avpu.cl_submit_ring.dmabuf_fd = -1;
                        enc->avpu.interm_buf.dmabuf_fd = -1;
                        enc->avpu.rec_buf.dmabuf_fd = -1;
                        enc->avpu.ref_buf.dmabuf_fd = -1;
                        enc->avpu.rec_trace_buf.dmabuf_fd = -1;
                        enc->avpu.ref_trace_buf.dmabuf_fd = -1;
                        for (int i = 0; i < 16; ++i)
                            enc->avpu.stream_bufs[i].dmabuf_fd = -1;
                        enc->avpu.frames_encoded = 0;
                        enc->avpu.frame_number = 0;
                        enc->avpu.idr_frame_number = 0;
                        enc->avpu.stream_header_offset = 0;
                        enc->avpu.busy_skip_count = 0;
                        enc->avpu.busy_snapshot_emitted = 0;
                        enc->avpu.first_submit_logged = 0;
                        enc->avpu.first_enc2_submit_logged = 0;
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
                        enc->avpu.codec_owner = enc;
                        enc->avpu.next_stream_submit = 0;
                        enc->avpu.pending_stream_read = 0;
                        enc->avpu.pending_stream_write = 0;
                        enc->avpu.pending_stream_count = 0;
#if defined(PLATFORM_T31)
                        enc->avpu.stream_header_shadow =
                            (uint8_t *)calloc(16u,
                                AVPU_T31_STREAM_PREFIX_BYTES);
                        if (!enc->avpu.stream_header_shadow)
                            LOG_CODEC("AVPU: failed to allocate T31 stream-prefix shadow");
#endif

                        /* OEM parity: AL_Board_Create allocates mutex and starts
                         * WaitInterruptThread immediately after opening /dev/avpu. */
                        pthread_mutex_t *mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
                        if (mutex) {
                            pthread_mutex_init(mutex, NULL);
                            enc->avpu.irq_mutex = mutex;
                        }
                        pthread_mutex_t *stream_mutex = (pthread_mutex_t*)malloc(sizeof(pthread_mutex_t));
                        if (stream_mutex) {
                            pthread_mutex_init(stream_mutex, NULL);
                            enc->avpu.stream_queue_mutex = stream_mutex;
                        }
                        memset(enc->avpu.irq_callbacks, 0, sizeof(enc->avpu.irq_callbacks));

                        if (enc->avpu.irq_mutex) {
                            int start_irq_thread = 1;
#if defined(PLATFORM_T40) || defined(PLATFORM_T31)
                            pthread_mutex_lock(&g_tseries_irq_host_lock);
                            if (g_tseries_irq_host == NULL) {
                                g_tseries_irq_host = &enc->avpu;
                            } else {
                                start_irq_thread = 0;
                            }
                            pthread_mutex_unlock(&g_tseries_irq_host_lock);
#endif
                            if (start_irq_thread) {
                                enc->avpu.irq_thread_running = 1;
                                pthread_t tid;
                                if (pthread_create(&tid, NULL, avpu_irq_thread, &enc->avpu) == 0) {
                                    enc->avpu.irq_thread = (long)tid;
                                    LOG_CODEC("AVPU: shared WaitInterruptThread started during open");
                                } else {
                                    LOG_CODEC("AVPU: failed to start WaitInterruptThread during open");
                                    enc->avpu.irq_thread_running = 0;
#if defined(PLATFORM_T40) || defined(PLATFORM_T31)
                                    pthread_mutex_lock(&g_tseries_irq_host_lock);
                                    if (g_tseries_irq_host == &enc->avpu)
                                        g_tseries_irq_host = NULL;
                                    pthread_mutex_unlock(&g_tseries_irq_host_lock);
#endif
                                }
                            } else {
                                LOG_CODEC("AVPU: using shared T-series WaitInterruptThread");
                            }
                        }

                        /* Cache live encode state for OEM-shaped command-list population. */
                        avpu_sync_runtime_encode_state(enc);

                        /*
                         * Allocate the public API's configured number of
                         * stream buffers.  Hardcoding sixteen on T31 exhausts
                         * reserved memory at 1080p because the correctly
                         * dimensioned buffers are about 1.5 MiB each.
                         */
                        enc->avpu.stream_buf_count = enc->stream_buf_count;
                        if (enc->avpu.stream_buf_count < 1)
                            enc->avpu.stream_buf_count = 1;
                        if (enc->avpu.stream_buf_count > 16)
                            enc->avpu.stream_buf_count = 16;
                        enc->avpu.stream_buf_size = enc->stream_buf_size > 0
                            ? enc->stream_buf_size
                            : 0x28000;
                        enc->avpu.stream_bufs_used = 0;
                        int filled = 0;
                        for (int i = 0; i < enc->avpu.stream_buf_count; ++i) {
                            LOG_CODEC("AVPU: alloc stream buf[%d] size=%d (IMP_Alloc)", i, enc->avpu.stream_buf_size);
                            AvpuDMABuf tmp = (AvpuDMABuf){0};
                            if (avpu_alloc_encoder(fd, (size_t)enc->avpu.stream_buf_size, "AVPU_STRM", &tmp) == 0) {
#if defined(PLATFORM_T31)
                                /*
                                 * Header/payload compaction happens after
                                 * entropy DMA.  Keep a physical, uncached
                                 * alias for that operation so Raptor's
                                 * zero-copy reader observes the same bytes.
                                 * Pre-submit zeroing and header generation
                                 * remain on the OEM-style cached mapping.
                                 */
                                tmp.uncached_map =
                                    avpu_remap_uncached(tmp.phy_addr, tmp.size);
#endif
                                enc->avpu.stream_bufs[filled] = tmp;
                                enc->avpu.stream_in_hw[filled] = 0;
                                enc->avpu.stream_buf_state[filled] = AVPU_STREAM_BUF_FREE;
                                LOG_CODEC("AVPU: stream buf[%d] pre-zero virt=%p size=%d",
                                          filled, enc->avpu.stream_bufs[filled].map,
                                          enc->avpu.stream_buf_size);
                                memset(enc->avpu.stream_bufs[filled].map, 0, enc->avpu.stream_buf_size);
                                LOG_CODEC("AVPU: stream buf[%d] post-zero pre-flush", filled);
                                {
                                    int stream_init_flush = avpu_flush_cache(
                                        fd, enc->avpu.stream_bufs[filled].map,
                                        (unsigned int)enc->avpu.stream_buf_size, 1);
                                    LOG_CODEC("AVPU: stream buf[%d] post-flush ret=%d",
                                              filled, stream_init_flush);
                                }
                                LOG_CODEC("AVPU: stream buf[%d] phys=0x%08x size=%d", filled, enc->avpu.stream_bufs[filled].phy_addr, enc->avpu.stream_buf_size);
                                filled++;
                            } else {
                                LOG_CODEC("AVPU: failed to allocate stream buf[%d] via IMP_Alloc", i);
                            }
                        }
                        enc->avpu.stream_bufs_used = filled;
                        codec_startup_trace(
                            "openimp/codec startup: stream dma ready=%d requested=%d\n",
                            filled, enc->avpu.stream_buf_count);

                        /* Allocate command-list rings via IMP_Alloc.  T41's
                         * command/status pair occupies one 4 KiB slot, with
                         * the status half at +0x5c0; T31/T40 use 512 bytes. */
#if defined(PLATFORM_T41)
                        enc->avpu.cl_entry_size = OPENIMP_T41_CL_SLOT_SIZE;
#else
                        enc->avpu.cl_entry_size = 0x200;
#endif
                        enc->avpu.cl_count = 0x13;
                        size_t cl_bytes = enc->avpu.cl_entry_size * enc->avpu.cl_count;
                        int cl_ok = 0;
                        if (avpu_alloc_encoder(fd, cl_bytes, "AVPU_CL", &enc->avpu.cl_ring) == 0) {
                            if (avpu_alloc_encoder(fd, cl_bytes, "AVPU_CL_SUBMIT", &enc->avpu.cl_submit_ring) == 0) {
                                cl_ok = 1;
                                void *virt = enc->avpu.cl_ring.map;
                                void *submit_virt = enc->avpu.cl_submit_ring.map;
                                uint32_t phys = enc->avpu.cl_ring.phy_addr;
                                uint32_t submit_phys = enc->avpu.cl_submit_ring.phy_addr;
                                if ((phys & 3) != 0 || ((uintptr_t)virt & 3) != 0 ||
                                    (submit_phys & 3) != 0 || ((uintptr_t)submit_virt & 3) != 0) {
                                    LOG_CODEC("ERROR: cmdlist buffer not 4-byte aligned: readback phys=0x%08x virt=%p submit phys=0x%08x virt=%p",
                                              phys, virt, submit_phys, submit_virt);
                                } else {
                                    enc->avpu.cl_idx = 0;
                                    memset(virt, 0, cl_bytes);
                                    memset(submit_virt, 0, cl_bytes);
#if defined(PLATFORM_T31)
                                    {
                                        int command_publish = avpu_flush_cache(
                                            fd, virt, (unsigned int)cl_bytes,
                                            1 /* WBACK */);
                                        int submit_publish = avpu_flush_cache(
                                            fd, submit_virt,
                                            (unsigned int)cl_bytes,
                                            1 /* WBACK */);

                                        if (command_publish == 0)
                                            enc->avpu.cl_ring.uncached_map =
                                                avpu_remap_uncached(
                                                    phys, cl_bytes);
                                        if (submit_publish == 0)
                                            enc->avpu.cl_submit_ring.uncached_map =
                                                avpu_remap_uncached(
                                                    submit_phys, cl_bytes);

                                        LOG_CODEC(
                                            "AVPU: T31 command rings use uncached mappings command=%p submit=%p publish=%d/%d",
                                            enc->avpu.cl_ring.uncached_map,
                                            enc->avpu.cl_submit_ring.uncached_map,
                                            command_publish, submit_publish);
                                    }
#elif defined(PLATFORM_T41)
                                    {
                                        const char *uncached_command_ring =
                                            getenv("OPENIMP_T41_UNCACHED_COMMAND_RING");

                                        /* Prefer one coherent mapping for the
                                         * hardware-owned submit/status slot.
                                         * Set the environment value to 0 only
                                         * for a cached-path diagnostic A/B. */
                                        if (!uncached_command_ring ||
                                            uncached_command_ring[0] != '0') {
                                            int initial_publish =
                                                avpu_flush_cache(
                                                    fd, submit_virt,
                                                    (unsigned int)cl_bytes,
                                                    1 /* WBACK */);

                                            if (initial_publish == 0)
                                                enc->avpu.cl_submit_ring.uncached_map =
                                                    avpu_remap_uncached(
                                                        submit_phys, cl_bytes);
                                            if (enc->avpu.cl_submit_ring.uncached_map) {
                                                LOG_CODEC("AVPU: T41 submit command/status ring uses coherent uncached mapping %p",
                                                          enc->avpu.cl_submit_ring.uncached_map);
                                            } else {
                                                LOG_CODEC("AVPU: T41 uncached submit ring unavailable publish=%d; retaining cached mapping",
                                                          initial_publish);
                                            }
                                        }
                                    }
#endif
                                    LOG_CODEC("AVPU: cmdlist ring phys=0x%08x size=%zu entries=%u", phys, cl_bytes, enc->avpu.cl_count);
                                    LOG_CODEC("AVPU: submit cmdlist ring phys=0x%08x size=%zu entries=%u",
                                              submit_phys, cl_bytes, enc->avpu.cl_count);
                                }
                            } else {
                                LOG_CODEC("AVPU: failed to allocate submit cmdlist ring via IMP_Alloc (size=%zu)", cl_bytes);
                            }
                        } else {
                            LOG_CODEC("AVPU: failed to allocate cmdlist ring via IMP_Alloc (size=%zu)", cl_bytes);
                        }
                        codec_startup_trace(
                            "openimp/codec startup: command dma ready=%d\n",
                            cl_ok);

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
                            size_t aux_frame_sz;
#if defined(PLATFORM_T41)
                            aux_frame_sz = openimp_t41_reconstruction_manager_size(
                                width, height);
#else
                            aux_frame_sz = avpu_get_enc1_frame_buf_size(width, height);
#endif
                            size_t aux_alloc_sz = aux_frame_sz;

                            memset(&enc->avpu.rec_buf, 0, sizeof(AvpuDMABuf));
                            memset(&enc->avpu.ref_buf, 0, sizeof(AvpuDMABuf));
                            memset(&enc->avpu.rec_trace_buf, 0, sizeof(AvpuDMABuf));
                            memset(&enc->avpu.ref_trace_buf, 0, sizeof(AvpuDMABuf));
                            memset(&enc->avpu.interm_buf, 0, sizeof(AvpuDMABuf));
                            enc->avpu.rec_buf.dmabuf_fd = -1;
                            enc->avpu.ref_buf.dmabuf_fd = -1;
                            enc->avpu.rec_trace_buf.dmabuf_fd = -1;
                            enc->avpu.ref_trace_buf.dmabuf_fd = -1;
                            enc->avpu.interm_buf.dmabuf_fd = -1;
                            enc->avpu.interm_ep1_size = avpu_get_enc1_ep1_size();
                            enc->avpu.interm_wpp_size = avpu_get_enc1_wpp_size(width, height);
                            enc->avpu.interm_ep2_size = avpu_get_enc1_ep2_size(width, height);
#if defined(PLATFORM_T40) || defined(PLATFORM_T31)
                            /*
                             * The inline T-series AVC command consumes EP1,
                             * WPP and EP2 only. Compression-map/data storage
                             * belongs to the reconstructed-frame manager.
                             */
                            enc->avpu.interm_map_size = 0u;
                            enc->avpu.interm_data_size = 0u;
#else
                            enc->avpu.interm_map_size = avpu_get_enc1_comp_map_size(width, height);
                            enc->avpu.interm_data_size = (uint32_t)avpu_get_enc1_comp_data_size(width, height,
                                                                                                 enc->avpu.format_word);
#endif

                            {
                                size_t interm_total_sz = (size_t)enc->avpu.interm_ep1_size
                                                       + (size_t)enc->avpu.interm_wpp_size
                                                       + (size_t)enc->avpu.interm_ep2_size
                                                       + (size_t)enc->avpu.interm_map_size
                                                       + (size_t)enc->avpu.interm_data_size;
#if defined(PLATFORM_T41)
                                interm_total_sz += 0x100u;
#endif

                                if (avpu_alloc_encoder(fd, interm_total_sz, "AVPU_ITM", &enc->avpu.interm_buf) == 0) {
#if !defined(PLATFORM_T41)
                                    int use_fixqp_lda = 0;
#if defined(PLATFORM_T31)
                                    use_fixqp_lda =
                                        enc->avpu.rc_mode == HW_RC_MODE_FIXQP;
#endif
#endif
                                    if (
#if defined(PLATFORM_T41)
                                        openimp_t41_init_ep1(
                                            enc->avpu.interm_buf.map,
                                            interm_total_sz)
#else
                                        openimp_t40_init_ep1(
                                            enc->avpu.interm_buf.map,
                                            interm_total_sz,
                                            use_fixqp_lda)
#endif
                                        != 0) {
                                        memset(enc->avpu.interm_buf.map, 0,
                                               interm_total_sz);
                                        LOG_CODEC("AVPU: ERROR - default EP1 initialization failed");
                                    } else {
                                        LOG_CODEC("AVPU: initialized default AVC EP1 table (%u bytes)",
                                                  enc->avpu.interm_ep1_size);
                                    }
#if defined(PLATFORM_T40) || defined(PLATFORM_T31)
                                    if (avpu_t40_init_ep2(&enc->avpu) != 0)
                                        LOG_CODEC("AVPU: ERROR - default EP2 initialization failed");
                                    else
                                        LOG_CODEC("AVPU: initialized default AVC EP2 table at offset=0x%x",
                                                  enc->avpu.interm_ep1_size +
                                                  enc->avpu.interm_wpp_size
#if defined(PLATFORM_T41)
                                                  + 0x100u
#endif
                                                  );
#endif
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

                            if (avpu_alloc_encoder(fd, aux_alloc_sz, "AVPU_REC", &enc->avpu.rec_buf) == 0) {
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

#if !defined(PLATFORM_T41)
                            if (avpu_alloc_encoder(fd, aux_alloc_sz, "AVPU_REF", &enc->avpu.ref_buf) == 0) {
                                /* Do NOT memset — ref_buf content is irrelevant for the
                                 * first IDR frame (intra-only), and subsequent frames will
                                 * have valid reconstruction data copied in by the AVPU. */
                                LOG_CODEC("AVPU: ref_buf phys=0x%08x size=%zu", enc->avpu.ref_buf.phy_addr, aux_frame_sz);
                            } else {
                                LOG_CODEC("AVPU: WARNING - failed to allocate ref_buf (%zu bytes)", aux_frame_sz);
                            }
#else
                            LOG_CODEC("AVPU: T41 rec manager phys=0x%08x size=%zu map_slot=%u mv_slot=%u (single stable data base)",
                                      enc->avpu.rec_buf.phy_addr, aux_frame_sz,
                                      openimp_t41_reconstruction_map_slot_size(width, height),
                                      openimp_t41_motion_vector_slot_size(width, height));
#endif

#if defined(PLATFORM_T41)
                            /* T41 uses the same recovered table values but a
                             * 0x1420-byte per-core image in three 0x1500-byte
                             * picture-class slots. Keep this separate from
                             * reconstruction storage, matching OEM ownership. */
                            if (avpu_alloc_encoder(fd, OPENIMP_T41_EP3_RING_SIZE,
                                                   "AVPU_EP3", &enc->avpu.rec_trace_buf) == 0) {
                                size_t ep3_initialized =
                                    openimp_t41_hwrc_ring_init(
                                        enc->avpu.rec_trace_buf.map,
                                        enc->avpu.rec_trace_buf.size,
                                        enc->avpu.bitrate ? enc->avpu.bitrate : 2000000u);
                                int ep3_flush = avpu_flush_dma_buf(
                                    fd, "ep3_ring", &enc->avpu.rec_trace_buf,
                                    enc->avpu.rec_trace_buf.size);
                                const char *uncached_ep3 =
                                    getenv("OPENIMP_T41_UNCACHED_EP3_RING");

                                if (ep3_flush == 0 &&
                                    (!uncached_ep3 || uncached_ep3[0] != '0'))
                                    enc->avpu.rec_trace_buf.uncached_map =
                                        avpu_remap_uncached(
                                            enc->avpu.rec_trace_buf.phy_addr,
                                            enc->avpu.rec_trace_buf.size);
                                openimp_t41_hwrc_level_init(
                                    &enc->avpu.t41_hwrc_level);
                                LOG_CODEC("AVPU: T41 ep3_ring phys=0x%08x size=%zu initialized=%zu flush=%d uncached=%p",
                                          enc->avpu.rec_trace_buf.phy_addr,
                                          enc->avpu.rec_trace_buf.size,
                                          ep3_initialized, ep3_flush,
                                          enc->avpu.rec_trace_buf.uncached_map);
                            } else {
                                LOG_CODEC("AVPU: WARNING - failed to allocate T41 EP3 ring");
                            }
#elif defined(PLATFORM_T40) || defined(PLATFORM_T31)
                            /* The old trace-shadow allocations were based on a
                             * false interpretation of cmd[0x2d].  It is really
                             * the three-slot EP3 HW-rate-control ring. */
                            if (avpu_alloc_encoder(fd,
                                                   AVPU_T40_EP3_SLOT_SIZE * AVPU_T40_EP3_SLOT_COUNT,
                                                   "AVPU_EP3", &enc->avpu.rec_trace_buf) == 0) {
                                size_t ep3_initialized = avpu_t40_init_ep3_ring(&enc->avpu);
                                int ep3_flush = avpu_flush_dma_buf(fd, "ep3_ring",
                                                                  &enc->avpu.rec_trace_buf,
                                                                  enc->avpu.rec_trace_buf.size);
                                LOG_CODEC("AVPU: ep3_ring phys=0x%08x size=%zu initialized=%zu flush=%d",
                                          enc->avpu.rec_trace_buf.phy_addr,
                                          enc->avpu.rec_trace_buf.size,
                                          ep3_initialized, ep3_flush);
                            } else {
                                LOG_CODEC("AVPU: WARNING - failed to allocate EP3 ring");
                            }
#else
                            if (avpu_alloc_encoder(fd, aux_alloc_sz, "AVPU_TRC_REC", &enc->avpu.rec_trace_buf) == 0) {
                                LOG_CODEC("AVPU: rec_trace_buf phys=0x%08x size=%zu", enc->avpu.rec_trace_buf.phy_addr, aux_frame_sz);
                            } else {
                                LOG_CODEC("AVPU: WARNING - failed to allocate rec_trace_buf (%zu bytes)", aux_frame_sz);
                            }
                            if (avpu_alloc_encoder(fd, aux_alloc_sz, "AVPU_TRC_REF", &enc->avpu.ref_trace_buf) == 0) {
                                LOG_CODEC("AVPU: ref_trace_buf phys=0x%08x size=%zu", enc->avpu.ref_trace_buf.phy_addr, aux_frame_sz);
                            } else {
                                LOG_CODEC("AVPU: WARNING - failed to allocate ref_trace_buf (%zu bytes)", aux_frame_sz);
                            }
#endif

                            avpu_log_dma_layout(&enc->avpu);
                        }

                        /* Never let a partial allocator result reach the AVPU.
                         * A zero CL/intermediate/reconstruction address is not
                         * a recoverable encode error on T40: it wedges the AXI
                         * fabric before userspace can report the failure. */
                        {
                            int required_dma_ok =
                                enc->avpu.stream_bufs_used > 0 && cl_ok &&
                                enc->avpu.cl_ring.map &&
                                enc->avpu.cl_submit_ring.map &&
                                enc->avpu.interm_buf.map &&
                                enc->avpu.rec_buf.map;
#if (defined(PLATFORM_T40) || defined(PLATFORM_T31)) && \
    !defined(PLATFORM_T41)
                            required_dma_ok = required_dma_ok &&
                                enc->avpu.ref_buf.map &&
                                enc->avpu.rec_trace_buf.map;
#elif defined(PLATFORM_T41)
                            required_dma_ok = required_dma_ok &&
                                enc->avpu.rec_trace_buf.map;
#endif
                            codec_startup_trace(
                                "openimp/codec startup: dma complete=%d stream=%d "
                                "cl=%p/%p interm=%p rec=%p ref=%p ep3=%p\n",
                                required_dma_ok,
                                enc->avpu.stream_bufs_used,
                                enc->avpu.cl_ring.map,
                                enc->avpu.cl_submit_ring.map,
                                enc->avpu.interm_buf.map,
                                enc->avpu.rec_buf.map,
                                enc->avpu.ref_buf.map,
                                enc->avpu.rec_trace_buf.map);
                            if (!required_dma_ok) {
                                int i;

                                LOG_CODEC("AVPU: refusing partial DMA layout");
                                if (enc->avpu.irq_thread) {
                                    pthread_t tid =
                                        (pthread_t)enc->avpu.irq_thread;

                                    enc->avpu.irq_thread_running = 0;
                                    avpu_sys_ioctl(enc->avpu.fd,
                                                   AL_CMD_UNBLOCK_CHANNEL,
                                                   NULL);
                                    pthread_join(tid, NULL);
                                    enc->avpu.irq_thread = 0;
                                }
                                for (i = 0;
                                     i < enc->avpu.stream_bufs_used; ++i)
                                    avpu_release_dma_buf(
                                        &enc->avpu.stream_bufs[i]);
                                avpu_release_dma_buf(&enc->avpu.cl_ring);
                                avpu_release_dma_buf(
                                    &enc->avpu.cl_submit_ring);
                                avpu_release_dma_buf(&enc->avpu.interm_buf);
                                avpu_release_dma_buf(&enc->avpu.rec_buf);
                                avpu_release_dma_buf(&enc->avpu.ref_buf);
                                avpu_release_dma_buf(
                                    &enc->avpu.rec_trace_buf);
                                avpu_release_dma_buf(
                                    &enc->avpu.ref_trace_buf);
                                AL_DevicePool_Close(enc->avpu.fd);
                                enc->avpu.fd = -1;
#if defined(PLATFORM_T40) || defined(PLATFORM_T31)
                                pthread_mutex_lock(
                                    &g_tseries_irq_host_lock);
                                if (g_tseries_irq_owner == &enc->avpu)
                                    g_tseries_irq_owner = NULL;
                                if (g_tseries_irq_host == &enc->avpu)
                                    g_tseries_irq_host = NULL;
                                pthread_mutex_unlock(
                                    &g_tseries_irq_host_lock);
#endif
                                if (enc->avpu.irq_mutex) {
                                    pthread_mutex_destroy(
                                        (pthread_mutex_t *)
                                            enc->avpu.irq_mutex);
                                    free(enc->avpu.irq_mutex);
                                    enc->avpu.irq_mutex = NULL;
                                }
                                if (enc->avpu.stream_queue_mutex) {
                                    pthread_mutex_destroy(
                                        (pthread_mutex_t *)
                                            enc->avpu.stream_queue_mutex);
                                    free(enc->avpu.stream_queue_mutex);
                                    enc->avpu.stream_queue_mutex = NULL;
                                }
                                free(enc->avpu.stream_header_shadow);
                                enc->avpu.stream_header_shadow = NULL;
                                enc->use_hardware = 0;
                                codec_set_error(enc, -ENOMEM);
                                return -1;
                            }
                        }

                        /* T31 uses absolute addressing (offset mode causes kernel crashes) */
                        enc->avpu.axi_base = 0;
                        enc->avpu.use_offsets = 0;
                        enc->avpu.session_ready = 0;
                        enc->avpu.hw_prepared = 0;

                        /* FIFOs already initialized at AL_CodecEncode create time
                         * (enc->fifo_frames, enc->fifo_streams at lines 767-778).
                         * OEM uses FIFOs at encoder+0x7f8 (streams) and encoder+0x81c (metadata). */

                        /* Register OEM callbacks (AL_EncCore_Init at 0x6c8d8). */
                        int irq_id0 = 0;                  /* completion slot */
                        int irq_id2 = 2;                  /* AVC entropy slot */
#if !defined(PLATFORM_T31) && !defined(PLATFORM_T41)
                        int irq_id4 = 4;                  /* live T31 completion IRQ */
#endif

                        avpu_register_callback(&enc->avpu, avpu_end_encoding_callback, &enc->avpu, irq_id0);
                        LOG_CODEC("AVPU: registered callback for IRQ %d (callback=%p, user_data=%p)",
                                  irq_id0, (void*)avpu_end_encoding_callback, (void*)&enc->avpu);
#if !defined(PLATFORM_T31) && !defined(PLATFORM_T41)
                        avpu_register_callback(&enc->avpu, avpu_end_encoding_callback, &enc->avpu, irq_id4);
                        LOG_CODEC("AVPU: registered callback for IRQ %d (callback=%p, user_data=%p)",
                                  irq_id4, (void*)avpu_end_encoding_callback, (void*)&enc->avpu);
                        LOG_CODEC("AVPU: registered EndEncoding callback at IRQ %d and %d", irq_id0, irq_id4);
#else
                        LOG_CODEC("AVPU: registered EndEncoding callback at IRQ %d", irq_id0);
#endif

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
                /* JPEG is deliberately software-backed at P2.  The generic
                 * legacy probe tries /dev/avpu as its last device and sends a
                 * VENC ioctl to it; doing that while AVC owns the T40 core
                 * stops subsequent completion IRQs.  Never touch AVPU from a
                 * non-AVC channel. */
                if (codec_type == IMP_ENC_TYPE_JPEG) {
                    enc->use_hardware = 0;
                    LOG_CODEC("Process: JPEG channel selected direct software path");
                } else {
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
    }

    if (enc->use_hardware == 1 && enc->hw_encoder_fd >= 0) {
        /* Legacy hardware path (/dev/venc, etc.) */
        HWFrameBuffer hw_frame;

        hw_stream = (HWStreamBuffer *)calloc(1, sizeof(*hw_stream));
        if (hw_stream == NULL) {
            LOG_CODEC("Process: failed to allocate legacy stream buffer");
            return -1;
        }
        memset(&hw_frame, 0, sizeof(HWFrameBuffer));
        hw_frame.phys_addr = phys_addr;
        hw_frame.virt_addr = virt_addr;
        hw_frame.size = size;
        hw_frame.width = width;
        hw_frame.height = height;
        hw_frame.pixfmt = pixfmt;
        hw_frame.timestamp = timestamp;
        { static unsigned int hw_count = 0; unsigned int c = __sync_add_and_fetch(&hw_count, 1);
          if (c <= 5 || (c % 50) == 0)
            LOG_CODEC("Process: HW(lgcy) encode frame %ux%u, phys=0x%x, virt=0x%x, size=%u [#%u]",
                      width, height, phys_addr, virt_addr, size, c);
        }
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
        int force_idr = __sync_lock_test_and_set(&enc->force_next_idr, 0);

        /* Keep the AVPU shadow aligned with live control-plane state before
         * each OEM-shaped encode1 submit. */
        avpu_sync_runtime_encode_state(enc);

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
#if defined(PLATFORM_T41)
            avpu_write_reg(fd, AVPU_REG_MISC_CTRL, 0x00000001);
            avpu_write_reg(fd, AVPU_REG_MISC_CTRL, 0x00010000);
            avpu_write_reg(fd, AVPU_REG_CORE_RESET(0), 0x00000002);
            avpu_t41_reset_core(fd, 0);
            avpu_write_reg(fd, AVPU_INTERRUPT, AVPU_IRQ_CLEAR_MASK);
#else
            avpu_write_reg(fd, AVPU_REG_MISC_CTRL, 0x00001000);
            avpu_write_reg(fd, AVPU_REG_CORE_RESET(0), 0x00000001);
            avpu_write_reg(fd, AVPU_REG_CORE_RESET(0), 0x00000002);
            avpu_write_reg(fd, AVPU_REG_CORE_RESET(0), 0x00000004);
            avpu_write_reg(fd, AVPU_INTERRUPT, 0x00FFFFFF);
#endif
            /* The T40 OEM trace has exactly two reset triplets before its
             * first frame: the one above in AL_EncCore_Init, and the
             * per-frame triplet immediately before CL_PUSH below.  An older
             * parity patch also performed the per-frame sequence here,
             * resulting in three resets and losing the state established by
             * callback registration.  Match the observed intermediate IRQ
             * state instead: EndAvcEntropy registration enables bit 4 twice
             * (0 -> 0x10, then 0x10 -> 0x10).
             *
             * TOP_CTRL is likewise absent from the T40 userspace trace and
             * reads back as zero in the live OEM snapshot. */
#if defined(PLATFORM_T41)
            {
                unsigned int irq_mask = 0;
                avpu_read_reg_quiet(fd, AVPU_INTERRUPT_MASK, &irq_mask);
                avpu_write_reg(fd, AVPU_INTERRUPT_MASK,
                               irq_mask | 0x00000004u);
                avpu_read_reg_quiet(fd, AVPU_INTERRUPT_MASK, &irq_mask);
                avpu_write_reg(fd, AVPU_INTERRUPT_MASK,
                               irq_mask | 0x00000004u);
            }
#elif defined(PLATFORM_T40)
            {
                unsigned int irq_mask = 0;
                avpu_read_reg_quiet(fd, AVPU_INTERRUPT_MASK, &irq_mask);
                avpu_write_reg(fd, AVPU_INTERRUPT_MASK,
                               irq_mask | 0x00000010u);
                avpu_read_reg_quiet(fd, AVPU_INTERRUPT_MASK, &irq_mask);
                avpu_write_reg(fd, AVPU_INTERRUPT_MASK,
                               irq_mask | 0x00000010u);
            }
#elif defined(PLATFORM_T31)
            avpu_write_reg(fd, AVPU_REG_TOP_CTRL, 0x00000080);
#endif

            LOG_CODEC("AVPU: init complete (stock-matched sequence)");
            ctx->session_ready = 1;

            /* Push stream buffers via STRM_PUSH so the hardware DMA engine
             * knows they're available. The CL (cmd[0x30]) specifies where to
             * write, but STRM_PUSH registers the buffer with the DMA controller. */
#if !defined(PLATFORM_T40) && !defined(PLATFORM_T31)
            if (ctx->stream_bufs_used > 0) {
                for (int i = 0; i < ctx->stream_bufs_used; ++i) {
                    if (ctx->stream_bufs[i].phy_addr) {
                        avpu_write_reg(fd, AVPU_REG_STRM_PUSH, ctx->stream_bufs[i].phy_addr);
                        ctx->stream_in_hw[i] = 1;
                        ctx->stream_buf_state[i] = AVPU_STREAM_BUF_FREE;
                        LOG_CODEC("AVPU: STRM_PUSH buf[%d] phys=0x%08x", i, ctx->stream_bufs[i].phy_addr);
                    }
                }
            }
#endif

            LOG_CODEC("AVPU: HW initialized (AL_EncCore_Init)");

        }

        /* Prepare command-list entry (OEM parity: SetCommandListBuffer) */
        if (ctx->cl_ring.phy_addr && ctx->cl_submit_ring.phy_addr &&
            avpu_cl_ring_base(ctx) && avpu_cl_submit_ring_base(ctx) && ctx->cl_entry_size) {
            uint32_t idx = ctx->cl_idx % ctx->cl_count;
            uint8_t* entry = avpu_cl_entry_ptr(ctx, idx);

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
            int periodic_idr = 0;
#if defined(PLATFORM_T41)
            int reference_storage_ready = ctx->rec_buf.phy_addr != 0;
#else
            int reference_storage_ready = ctx->ref_buf.phy_addr != 0;
#endif
            if (!force_idr
                && ctx->gop_length > 0u
                && ctx->frame_number != 0u
                && ((ctx->frame_number % ctx->gop_length) == 0u)
                && (ctx->reference_valid != 0)
                && reference_storage_ready) {
                periodic_idr = 1;
            }

            int has_reference = (!force_idr)
                && (!periodic_idr)
                && (ctx->reference_valid != 0)
                && reference_storage_ready;
            int is_idr = !has_reference;
#if defined(PLATFORM_T41)
            uint32_t ref_phys = has_reference ? ctx->rec_buf.phy_addr : 0;
#else
            uint32_t ref_phys = has_reference ? ctx->ref_buf.phy_addr : 0;
#endif
            if (is_idr)
                ctx->idr_frame_number = ctx->frame_number;
            if (force_idr) {
                if (ctx->frame_number % 50 == 0)
                LOG_CODEC("Process: channel=%d forcing next AVPU frame to IDR", enc->channel_id - 1);
            } else if (periodic_idr) {
                if (ctx->frame_number % 50 == 0)
                LOG_CODEC("Process: channel=%d scheduling periodic AVPU IDR at frame=%u gop=%u",
                          enc->channel_id - 1, ctx->frame_number, ctx->gop_length);
            }

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

            /* OEM AL_EncCore_Encode1() checks IsEnc1AlreadyRunning() before
             * flushing/pushing a new Enc1 command list. Our simplified path
             * uses a single effective rec/ref pair plus a single actively-used
             * stream buffer, so matching this gate BEFORE touching buf[0] is
             * important: otherwise a busy/sticky core can cause us to erase the
             * previous encoded output before GetStream drains it. */
            int buf_idx = -1;
            unsigned int core_status = 0;

            /* The earlier local state that produced continuously ticking AVPU
             * interrupts allowed further submissions after the first real IRQ,
             * even though the core-status running bit remained latched. Keep
             * the stricter "don't touch the pending buffer" behavior only
             * before we have observed any real AVPU completion IRQ. */
            if (ctx->last_irq_id < 0) {
                if (ctx->frames_encoded > ctx->frames_consumed) {
                    unsigned int skip_count = __sync_add_and_fetch(&ctx->busy_skip_count, 1u);
                    if (skip_count == 1u || (skip_count % 30u) == 0u) {
                        LOG_CODEC("Process: pending AVPU stream not yet drained; skipping CL[%u] submit (skip_count=%u enc=%d cons=%d)",
                                  idx, skip_count, ctx->frames_encoded, ctx->frames_consumed);
                    }
                    free(hw_stream);
                    return -1;
                }

                if (ctx->pending_stream_count > 0 &&
                    avpu_is_enc1_running(fd, 0, &core_status)) {
                    if (avpu_try_recover_sticky_completion(ctx, core_status, "Process[AVPU]")) {
                        free(hw_stream);
                        return -1;
                    }

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
            } else if (ctx->busy_skip_count != 0) {
                LOG_CODEC("Process: allowing AVPU resubmit after IRQ %d despite latched core state (enc=%d cons=%d)",
                          ctx->last_irq_id, ctx->frames_encoded, ctx->frames_consumed);
            }
            ctx->busy_skip_count = 0;

            buf_idx = avpu_acquire_stream_buffer(ctx);
            if (buf_idx < 0) {
                LOG_CODEC("Process: no free AVPU stream buffer (enc=%d cons=%d pending=%d used=%d)",
                          ctx->frames_encoded, ctx->frames_consumed,
                          ctx->pending_stream_count, ctx->stream_bufs_used);
                free(hw_stream);
                errno = EAGAIN;
                return -1;
            }

#if defined(PLATFORM_T31)
            ctx->t31_payload_size_by_buf[buf_idx] = 0u;
#elif defined(PLATFORM_T41)
            /* A reused buffer must not inherit the preceding completion's
             * payload count if an IRQ arrives without a valid writeback. */
            ctx->t41_payload_size_by_buf[buf_idx] = 0u;
#elif defined(PLATFORM_T31)
            ctx->t31_payload_size_by_buf[buf_idx] = 0u;
#endif

            /* Prepare host-owned stream bytes through the cached mapping.
             * Generations with an exact completion extent only need to clear
             * and publish the header prefix; stale payload bytes are neither
             * scanned nor exposed.
             *
             * CRITICAL: Do NOT use uncached /dev/mem mappings for stream
             * buffers. On MIPS T31, uncached memset corrupts the CPU cache
             * state for the corresponding cached mapping, making subsequent
             * cached writes invisible to both CPU reads and rmem flush.
             * The OEM uses cached-only + rmem flush for all DMA buffers. */
            if (buf_idx < ctx->stream_bufs_used &&
                ctx->stream_bufs[buf_idx].map) {
#if defined(PLATFORM_T41)
                /* Payload length comes from the completion slot, so stale
                 * bytes beyond this picture are never scanned or exposed. */
                memset(ctx->stream_bufs[buf_idx].map, 0,
                       OPENIMP_T41_STREAM_PAYLOAD_OFFSET);
#elif defined(PLATFORM_T31)
                /* T31 completion status supplies the exact payload extent;
                 * only the host-owned header prefix needs clearing. */
                memset(ctx->stream_bufs[buf_idx].map, 0,
                       AVPU_T31_STREAM_PREFIX_BYTES);
#else
                memset(ctx->stream_bufs[buf_idx].map, 0,
                       (size_t)ctx->stream_buf_size);
#endif
            }

#if defined(PLATFORM_T41)
            {
                OpenIMPProfileStamp picture_profile =
                    openimp_profile_begin();
                int picture_result =
                    avpu_t41_prepare_picture(ctx, is_idr);

                openimp_profile_end(
                    OPENIMP_PROFILE_T41_PICTURE_STATE,
                    picture_profile);
                if (picture_result != 0) {
                    LOG_CODEC("Process: failed to prepare T41 rate-control state frame=%u buf=%d",
                              ctx->frame_number, buf_idx);
                    avpu_mark_stream_buffer_released(ctx, buf_idx);
                    free(hw_stream);
                    errno = EIO;
                    return -1;
                }
            }
#endif
#if defined(PLATFORM_T31)
            if (avpu_t31_prepare_picture(ctx) != 0) {
                LOG_CODEC("Process: failed to prepare T31 rate-control state frame=%u buf=%d",
                          ctx->frame_number, buf_idx);
                avpu_mark_stream_buffer_released(ctx, buf_idx);
                free(hw_stream);
                errno = EIO;
                return -1;
            }
            ctx->t31_rate_control_qp_by_buf[buf_idx] =
                avpu_t40_picture_qp(ctx, is_idr);
#endif
            OpenIMPProfileStamp header_profile = openimp_profile_begin();
            uint32_t hdr_offset = avpu_prewrite_stream_headers(
                ctx, buf_idx, is_idr);
            openimp_profile_end(OPENIMP_PROFILE_STREAM_HEADER,
                                header_profile);
            ctx->stream_is_idr[buf_idx] = is_idr ? 1u : 0u;
            ctx->stream_timestamp[buf_idx] = timestamp;
#if defined(PLATFORM_T41)
            {
                const uint32_t external_frame_magic = 0x56344c32u;
                uint32_t frame_magic = 0u;

                memcpy(&frame_magic, frame_bytes + 0x30,
                       sizeof(frame_magic));
            ctx->stream_public_alias_delta[buf_idx] = 0u;
            ctx->stream_public_alias_valid[buf_idx] = 0u;
            if (frame_magic != external_frame_magic &&
                phys_addr != 0u && virt_addr > phys_addr) {
                ctx->stream_public_alias_delta[buf_idx] =
                    virt_addr - phys_addr;
                ctx->stream_public_alias_valid[buf_idx] = 1u;
            }
            }
#endif

            /* Publish the host-written header prefix before AVPU DMA. */
            if (buf_idx < ctx->stream_bufs_used &&
                ctx->stream_bufs[buf_idx].map) {
#if defined(PLATFORM_T41)
                avpu_flush_cache_profiled(
                    fd, ctx->stream_bufs[buf_idx].map,
                    OPENIMP_T41_STREAM_PAYLOAD_OFFSET, 1 /* WBACK */,
                    OPENIMP_PROFILE_CACHE_STREAM_PREPARE);
#elif defined(PLATFORM_T31)
                avpu_flush_cache_profiled(
                    fd, ctx->stream_bufs[buf_idx].map,
                    AVPU_T31_STREAM_PREFIX_BYTES, 1 /* WBACK */,
                    OPENIMP_PROFILE_CACHE_STREAM_PREPARE);
#else
                avpu_flush_cache(fd, ctx->stream_bufs[buf_idx].map,
                                 (unsigned int)ctx->stream_buf_size,
                                 1 /* WBACK */);
#endif
            }

            /* Fill Enc1 command registers — source addr and header offset go INTO the CL entry */
#if defined(PLATFORM_T41)
            {
                OpenIMPProfileStamp command_profile =
                    openimp_profile_begin();
                int command_result = avpu_t41_fill_command(
                    ctx, cmd, buf_idx, phys_addr, is_idr);

                openimp_profile_end(OPENIMP_PROFILE_COMMAND_BUILD,
                                    command_profile);
                if (command_result != 0) {
                    LOG_CODEC("Process: failed to build T41 command frame=%u buf=%d",
                              ctx->frame_number, buf_idx);
                    avpu_mark_stream_buffer_released(ctx, buf_idx);
                    free(hw_stream);
                    errno = EINVAL;
                    return -1;
                }
            }
#else
            {
                OpenIMPProfileStamp command_profile =
                    openimp_profile_begin();

                fill_cmd_regs_enc1(ctx, cmd, buf_idx, phys_addr, hdr_offset,
                                   is_idr, ref_phys);
                openimp_profile_end(OPENIMP_PROFILE_COMMAND_BUILD,
                                    command_profile);
            }
#endif
            if (ctx->frame_number < 16u || is_idr) {
                LOG_CODEC("Process: AVPU submit frame=%u CL[%u] buf=%d src=0x%08x rec=0x%08x ref=0x%08x ref_valid=%d idr=%d force=%d periodic=%d gop=%u cmd03=%08x cmd15=%08x cmd17=%08x cmd18=%08x map37=%08x ref38=%08x map67=%08x ref68=%08x ref69=%08x",
                          ctx->frame_number, idx, buf_idx, phys_addr,
                          ctx->rec_buf.phy_addr, ref_phys,
                          ctx->reference_valid, is_idr,
                          force_idr, periodic_idr, ctx->gop_length,
                          cmd[0x03], cmd[0x15], cmd[0x17], cmd[0x18],
                          cmd[0x37], cmd[0x38], cmd[0x67], cmd[0x68],
                          cmd[0x69]);
            }
            log_first_enc1_cmd_window(ctx, idx, cmd);

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
            /* TEST: Restore 1MB flush from commit 93de1a9 which had continuous
             * AVPU interrupts. The 512-byte flush may leave other DMA buffers
             * (intermediate, rec/ref) incoherent — the 1MB flush on T31's small
             * L1 D-cache effectively flushes the ENTIRE cache. */
            size_t cl_flush_size = 0x100000; /* 1MB — matches OEM + known-good 93de1a9 */
            /* Verify data in CPU cache, then flush */
            if (ctx->frame_number % 50 == 0)
            LOG_CODEC("Process: CL[%u] pre-flush virt_w0=0x%08x w1=0x%08x entry=%p size=%u",
                      idx, cmd[0], cmd[1], (void*)entry, (unsigned)cl_flush_size);
            /* Use dir=0 (DMA_BIDIRECTIONAL = writeback + INVALIDATE) so cache
             * lines are REMOVED after flush. dir=1 (DMA_TO_DEVICE) only writes
             * back but keeps lines in cache as "clean" — then AVPU DMA writes
             * go to RAM but CPU reads stale cached data. */
            uint8_t *submit_entry = avpu_cl_submit_entry_ptr(ctx, idx);
            int cl_flush_ret;
            int submit_flush_ret;
            int command_copy_ret = 0;
            if (!submit_entry) {
                LOG_CODEC("Process: submit CL[%u] missing", idx);
                avpu_mark_stream_buffer_released(ctx, buf_idx);
                free(hw_stream);
                errno = EAGAIN;
                return -1;
            }
            {
                OpenIMPProfileStamp copy_profile = openimp_profile_begin();

#if defined(PLATFORM_T41)
                command_copy_ret = openimp_t41_command_publish(
                    submit_entry, ctx->cl_entry_size,
                    entry, ctx->cl_entry_size);
#else
                memcpy(submit_entry, entry, ctx->cl_entry_size);
#endif
                openimp_profile_end(OPENIMP_PROFILE_COMMAND_COPY,
                                    copy_profile);
            }
            if (command_copy_ret != 0) {
                LOG_CODEC("Process: failed to publish T41 command slot CL[%u]",
                          idx);
                avpu_mark_stream_buffer_released(ctx, buf_idx);
                free(hw_stream);
                errno = EINVAL;
                return -1;
            }
            ctx->enc_core.cmd_list = entry;
#if defined(PLATFORM_T41)
            /* Only the submit ring is consumed by hardware.  Flush from its
             * allocation base so the normalized 1 MiB T41 cache operation
             * covers every slot without starting past the small ring. */
            cl_flush_ret = 0;
            submit_flush_ret = ctx->cl_submit_ring.uncached_map
                ? 0
                : avpu_flush_cache_profiled(
                    fd, ctx->cl_submit_ring.map,
                    (unsigned int)cl_flush_size, 1 /* WBACK */,
                    OPENIMP_PROFILE_CACHE_COMMAND_PUBLISH);
#elif defined(PLATFORM_T31)
            /* The readback entry has already been copied and is never a DMA
             * target. Preserve the proven full-cache publish on the one ring
             * the T31 AVPU actually consumes. */
            cl_flush_ret = 0;
            submit_flush_ret = ctx->cl_submit_ring.uncached_map
                ? 0
                : avpu_flush_cache_profiled(
                    fd, submit_entry, (unsigned int)cl_flush_size,
                    1 /* WBACK */,
                    OPENIMP_PROFILE_CACHE_COMMAND_PUBLISH);
#else
            cl_flush_ret = ctx->cl_ring.uncached_map
                ? 0
                : avpu_flush_cache(fd, entry,
                                   (unsigned int)cl_flush_size,
                                   1 /* WBACK */);
            submit_flush_ret = ctx->cl_submit_ring.uncached_map
                ? 0
                : avpu_flush_cache(fd, submit_entry,
                                   (unsigned int)cl_flush_size,
                                   1 /* WBACK */);
#endif
            if (ctx->frame_number % 50 == 0)
            LOG_CODEC("Process: CL[%u] flush ret=%d submit_ret=%d (rmem+avpu)", idx, cl_flush_ret, submit_flush_ret);
            int trace_submit = (idx == 0 && ctx->frames_encoded == 0);

            /* Record which CL entry owns this stream buffer's completion
             * status. Exact-length generations use that writeback instead of
             * scanning the stream buffer for trailing zeros. */
#if defined(PLATFORM_T40) || defined(PLATFORM_T31)
            /* Both current T-series paths complete inline under CL_PUSH=2. */
            ctx->stream_enc2_cl_idx[buf_idx] = idx;
#else
            ctx->stream_enc2_cl_idx[buf_idx] = has_reference
                ? (idx + 1) % ctx->cl_count   /* P: Enc2 CL at idx+1, read cmd[0x3e] */
                : idx;                         /* IDR: inline Enc2, read cmd[0x32] from Enc1 CL */
#endif

            /* OEM per-frame pre-submit: TurnOnGC + ResetCore + IRQ re-arm
             * before CL_PUSH. The ingenic-sdk driver only transports register
             * ioctls and IRQ indices; it does not perform this reset.
             *
             * OEM libimp per-frame sequence (from HLIL at 0x671b8):
             * 1. SetClockCommand (TurnOnGC)
             * 2. EnableInterrupts
             * 3. Callback +0x42c (FillSourceConfig)
             * 4. Callback +0x430
             * 5. AL_EncCore_Encode1 → Rtos_FlushCacheMemory + CL_PUSH */
#if defined(PLATFORM_T41)
            OpenIMPProfileStamp submit_io_profile = openimp_profile_begin();
            avpu_t41_reset_core(fd, 0);
            avpu_turn_on_gc(fd, 0);
#else
            OpenIMPProfileStamp submit_io_profile = openimp_profile_begin();
            avpu_turn_on_gc(fd, 0);
#if defined(PLATFORM_T40) || defined(PLATFORM_T31)
            avpu_write_reg(fd, AVPU_REG_CORE_RESET(0), 0x00000001);
            avpu_write_reg(fd, AVPU_REG_CORE_RESET(0), 0x00000002);
            avpu_write_reg(fd, AVPU_REG_CORE_RESET(0), 0x00000004);
#else
            avpu_clear_interrupts(fd);
#endif
#endif
            avpu_enable_interrupts(fd, 0);

            /* T41 latches its source configuration before the command-list
             * transaction.  T31 follows the OEM-observed post-push order
             * below, while the submission mutex still excludes IRQ dispatch. */
#if defined(PLATFORM_T41)
            {
                uint32_t y_plane_sz = avpu_get_nv12_luma_plane_size(width, height);
                uint32_t stream_part_offset = avpu_get_enc1_stream_part_offset(ctx);
                uint32_t hw_hdr_offset = avpu_get_hw_hdr_offset(hdr_offset);
                uint32_t hw_stream_budget = avpu_get_stream_window_budget(ctx, stream_part_offset, hw_hdr_offset);

                avpu_write_reg(fd, AVPU_REG_ENC_EN_B, 0x00000001);
                avpu_write_reg(fd, AVPU_REG_ENC_EN_A, 0x00000001);

                avpu_write_reg(fd, 0x8400, 0x00000131u);
                avpu_write_reg(fd, 0x8404,
                    (((uint32_t)width - 1u) << 16) | ((uint32_t)height - 1u));
                avpu_write_reg(fd, 0x8408, 0x00010001u);
                avpu_write_reg(fd, 0x840c, (uint32_t)width);
                avpu_write_reg(fd, 0x8410, phys_addr);
                avpu_write_reg(fd, 0x8414, phys_addr + y_plane_sz);
                avpu_write_reg(fd, 0x8418, ctx->interm_buf.phy_addr
                    + ctx->interm_ep1_size); /* WPP start */
                avpu_write_reg(fd, 0x841c, ctx->interm_buf.phy_addr); /* EP1 base */
                avpu_write_reg(fd, 0x8420, stream_part_offset);
                avpu_write_reg(fd, 0x8424, hw_hdr_offset);
                avpu_write_reg(fd, 0x8428, hw_stream_budget);

                avpu_write_reg(fd, AVPU_REG_ENC_EN_C, 0x00000001);

                if (trace_submit) {
                    unsigned int cfg_8400 = 0, cfg_8404 = 0, cfg_8408 = 0, cfg_840c = 0;
                    unsigned int cfg_8410 = 0, cfg_8414 = 0, cfg_8418 = 0, cfg_841c = 0;
                    unsigned int cfg_8420 = 0, cfg_8424 = 0, cfg_8428 = 0, cfg_85e4 = 0;
                    avpu_read_reg_quiet(fd, 0x8400, &cfg_8400);
                    avpu_read_reg_quiet(fd, 0x8404, &cfg_8404);
                    avpu_read_reg_quiet(fd, 0x8408, &cfg_8408);
                    avpu_read_reg_quiet(fd, 0x840c, &cfg_840c);
                    avpu_read_reg_quiet(fd, 0x8410, &cfg_8410);
                    avpu_read_reg_quiet(fd, 0x8414, &cfg_8414);
                    avpu_read_reg_quiet(fd, 0x8418, &cfg_8418);
                    avpu_read_reg_quiet(fd, 0x841c, &cfg_841c);
                    avpu_read_reg_quiet(fd, 0x8420, &cfg_8420);
                    avpu_read_reg_quiet(fd, 0x8424, &cfg_8424);
                    avpu_read_reg_quiet(fd, 0x8428, &cfg_8428);
                    avpu_read_reg_quiet(fd, AVPU_REG_ENC_EN_C, &cfg_85e4);
                    LOG_CODEC("AVPU: source cfg latched 8400=%08x 8404=%08x 8408=%08x 840c=%08x 8410=%08x 8414=%08x",
                              cfg_8400, cfg_8404, cfg_8408, cfg_840c, cfg_8410, cfg_8414);
                    LOG_CODEC("AVPU: source cfg latched 8418=%08x 841c=%08x 8420=%08x 8424=%08x 8428=%08x 85e4=%08x",
                              cfg_8418, cfg_841c, cfg_8420, cfg_8424, cfg_8428, cfg_85e4);
                }

                if (ctx->frame_number % 50 == 0)
                LOG_CODEC("AVPU: encoder config BEFORE CL_PUSH (hdr=%u)", hdr_offset);
            }
#endif

            /* NOW do CL_ADDR + CL_PUSH (after encoder config is programmed) */
            uint32_t cl_phys = ctx->cl_submit_ring.phy_addr + (idx * ctx->cl_entry_size);
            int cl_addr_ret;
            int cl_push_ret;
            if (trace_submit || cl_flush_ret != 0 || submit_flush_ret != 0) {
                LOG_CODEC("AVPU: submit flush CL[%u] readback_phys=0x%08x submit_phys=0x%08x size=0x%08x ret=%d submit_ret=%d",
                          idx, ctx->cl_ring.phy_addr, ctx->cl_submit_ring.phy_addr,
                          (unsigned int)cl_flush_size, cl_flush_ret, submit_flush_ret);
            }
            if (trace_submit) {
                ctx->init_cl_flush_ret = submit_flush_ret ? submit_flush_ret : cl_flush_ret;
                ctx->init_trace_completed = 1;
            }
            if (ctx->frame_number % 50 == 0)
            LOG_CODEC("Process: CL_ADDR=0x%08x src=0x%08x rec=0x%08x ref=0x%08x CL[%u]",
                      cl_phys, phys_addr,
                      ctx->rec_buf.phy_addr, ref_phys, idx);
            if (trace_submit)
                avpu_log_submit_snapshot(ctx, idx, "pre");

#if defined(PLATFORM_T40) || defined(PLATFORM_T31)
            __sync_synchronize();
            g_tseries_irq_owner = ctx;
            __sync_synchronize();
#endif
#if defined(PLATFORM_T41)
            avpu_t41_program_command_slot(fd, 0, cl_phys);
            cl_addr_ret = 0;
#else
            cl_addr_ret = avpu_write_reg(fd, AVPU_REG_CL_ADDR, cl_phys);
#endif
            if (trace_submit) {
                LOG_CODEC("AVPU: submit write CL[%u] CL_ADDR ret=%d", idx, cl_addr_ret);
                avpu_log_submit_snapshot(ctx, idx, "post_cl_addr");
            }

            /* Reset/re-arm can wake WAIT_IRQ before CL_PUSH.  Do not expose a
             * pending frame to that stale IRQ: it would consume this buffer
             * and gate the core while source configuration is still being
             * programmed.  Publish ownership only at the push boundary and
             * serialize it with callback dispatch through irq_mutex. */
            pthread_mutex_t *submit_irq_mutex =
                (pthread_mutex_t *)ctx->irq_mutex;
            if (submit_irq_mutex)
                pthread_mutex_lock(submit_irq_mutex);
            if (!avpu_track_submitted_stream(ctx, buf_idx, user_data)) {
                if (submit_irq_mutex)
                    pthread_mutex_unlock(submit_irq_mutex);
                LOG_CODEC("Process: failed to track submitted AVPU stream buf[%d]", buf_idx);
                avpu_mark_stream_buffer_released(ctx, buf_idx);
                free(hw_stream);
                errno = EAGAIN;
                return -1;
            }

            cl_push_ret = avpu_write_reg(fd, AVPU_REG_CL_PUSH, 0x00000002);
#if defined(PLATFORM_T31)
            /* OEM traces program the companion 0x8400 block immediately
             * after CL_PUSH.  Keep irq_mutex locked across both operations:
             * publishing the pending stream before the companion stage is
             * ready lets the shared waiter race this register sequence. */
            avpu_t31_start_companion_stage(ctx, fd, width, height, phys_addr,
                                           cmd, buf_idx, trace_submit);
#endif
            if (submit_irq_mutex)
                pthread_mutex_unlock(submit_irq_mutex);
            openimp_profile_end(OPENIMP_PROFILE_AVPU_SUBMIT_IO,
                                submit_io_profile);
            if (trace_submit) {
                LOG_CODEC("AVPU: submit write CL[%u] CL_PUSH ret=%d val=0x00000002", idx, cl_push_ret);
#if !defined(PLATFORM_T40) && !defined(PLATFORM_T31)
                /* Do not probe a live T-series AVPU immediately after
                 * CL_PUSH.  The OEM path returns directly after the write,
                 * and a userspace read while the core owns the register bus
                 * intermittently wedges the SoC before the completion IRQ. */
                avpu_log_submit_snapshot(ctx, idx, "post_cl_push");
#endif
            }
            /* OEM decompilation confirms: AL_EncCore_Encode1 at 0x6cbf0
             * For IDR (arg4=0): CL_PUSH=2 only — inline Enc2 runs within CL_PUSH=2
             * For P-frame (arg4!=0): CL_PUSH=2 then CL_PUSH=8 back-to-back */
#if defined(PLATFORM_T40) || defined(PLATFORM_T31)
            /* Live OEM captures show P pictures use the same single
             * CL_PUSH=2 transaction as IDR.  The separate CL_PUSH=8 Enc2
             * transaction is only used by the older backend path. */
            ctx->enc_core.enc2_cmd_list = entry;
            ctx->cl_idx = (idx + 1) % ctx->cl_count;
            if (ctx->frame_number % 50 == 0)
                LOG_CODEC("Process: T-series %s frame -- inline Enc2 within CL_PUSH=2",
                          is_idr ? "IDR" : "P");
#else
            if (has_reference) {
                uint32_t enc2_idx = (idx + 1) % ctx->cl_count;
                uint8_t *enc2_entry = avpu_cl_entry_ptr(ctx, enc2_idx);
                uint8_t *enc2_submit_entry = avpu_cl_submit_entry_ptr(ctx, enc2_idx);
                uint32_t *enc2_cmd = (uint32_t *)enc2_entry;
                uint32_t enc2_phys = ctx->cl_submit_ring.phy_addr + (enc2_idx * ctx->cl_entry_size);

                fill_cmd_regs_enc2(ctx, enc2_cmd, buf_idx, hdr_offset, is_idr);
                if (ctx->frame_number % 50 == 0)
                LOG_CODEC("Process: Enc2 CL[%u] cmd[0x1b]=0x%08x cmd[0x1c]=0x%08x cmd[0x1d]=0x%08x cmd[0x1e]=0x%08x cmd[0x1f]=0x%08x",
                          enc2_idx, enc2_cmd[0x1b], enc2_cmd[0x1c], enc2_cmd[0x1d],
                          enc2_cmd[0x1e], enc2_cmd[0x1f]);
                log_first_enc2_cmd_window(ctx, enc2_idx, enc2_cmd);

                if (!enc2_submit_entry) {
                    LOG_CODEC("Process: submit Enc2 CL[%u] missing", enc2_idx);
                    avpu_mark_stream_buffer_released(ctx, buf_idx);
                    free(hw_stream);
                    errno = EAGAIN;
                    return -1;
                }
                memcpy(enc2_submit_entry, enc2_entry, ctx->cl_entry_size);
                ctx->enc_core.enc2_cmd_list = enc2_entry;
                avpu_flush_cache(fd, enc2_entry, (unsigned int)cl_flush_size, 1 /*WBACK*/);
                if (!ctx->cl_submit_ring.uncached_map)
                    avpu_flush_cache(fd, enc2_submit_entry, (unsigned int)cl_flush_size, 1 /*WBACK*/);

                avpu_write_reg(fd, AVPU_REG_CL_ADDR, enc2_phys);
                avpu_write_reg(fd, AVPU_REG_CL_PUSH, 0x00000008);
                if (ctx->frame_number % 50 == 0)
                LOG_CODEC("Process: Enc2 submitted CL[%u] phys=0x%08x hdr=%u (P)", enc2_idx, enc2_phys, hdr_offset);
                ctx->cl_idx = (idx + 2) % ctx->cl_count;
            } else {
                ctx->enc_core.enc2_cmd_list = entry;
                if (ctx->frame_number % 50 == 0)
                LOG_CODEC("Process: IDR frame — inline Enc2 within CL_PUSH=2 (OEM parity)");
                ctx->cl_idx = (idx + 1) % ctx->cl_count;
            }
#endif

            /* Advance CL index (already set above based on IDR vs P frame) */
            ctx->frame_number++;
            submitted = 1;

            if (ctx->frame_number % 50 == 0)
            LOG_CODEC("Process: AVPU queued frame %ux%u phys=0x%x CL[%u] hdr=%u - encoding triggered",
                      width, height, phys_addr, idx, hdr_offset);
        }

        /* Do not dequeue here; GetStream() will handle stream retrieval */
        free(hw_stream);
        openimp_profile_end(OPENIMP_PROFILE_ENCODE_SUBMIT, submit_profile);
        return submitted ? 0 : -1;
    } else {
        /* Software fallback */
        uint32_t codec_type = (*(uint32_t*)(enc->codec_param + 0x20) >> 24) & 0xffu;
        int force_idr = __sync_lock_test_and_set(&enc->force_next_idr, 0);
        HWFrameBuffer hw_frame;

        /* reserved[] carries AVPU user_data when explicitly populated.
         * Keeping software descriptors zeroed makes GetStream use its
         * metadata FIFO; heap garbage here used to fill that FIFO and block
         * JPEG Process after four frames. */
        hw_stream = (HWStreamBuffer *)calloc(1, sizeof(*hw_stream));
        if (hw_stream == NULL) {
            LOG_CODEC("Process: failed to allocate software stream buffer");
            return -1;
        }
        memset(&hw_frame, 0, sizeof(HWFrameBuffer));
        hw_frame.phys_addr = phys_addr;
        hw_frame.virt_addr = virt_addr;
        hw_frame.size = size;
        hw_frame.width = width;
        hw_frame.height = height;
        hw_frame.pixfmt = pixfmt;
        hw_frame.timestamp = timestamp;
        if (force_idr) {
            LOG_CODEC("Process: channel=%d forcing next SW frame to IDR", enc->channel_id - 1);
            HW_Encoder_RequestIDR();
        }
        { static unsigned int sw_count = 0; unsigned int c = __sync_add_and_fetch(&sw_count, 1);
          if (c <= 5 || (c % 50) == 0)
            LOG_CODEC("Process: SW encode frame %ux%u codec_type=%u [#%u]", width, height, codec_type, c);
        }
        if (
#if defined(PLATFORM_T40)
            (codec_type == IMP_ENC_TYPE_JPEG
                ? t40_encode_gray_jpeg(width, height, timestamp, hw_stream)
                : HW_Encoder_Encode_Software(&hw_frame, hw_stream, codec_type)) < 0
#else
            HW_Encoder_Encode_Software(&hw_frame, hw_stream, codec_type) < 0
#endif
        ) {
            LOG_CODEC("Process: software encoding failed");
            free(hw_stream);
            return -1;
        }
    }

queue_encoded_stream:
    /* Queue encoded stream to FIFO */
    if (Fifo_Queue(enc->fifo_streams, hw_stream, -1) == 0) {
        LOG_CODEC("Process: failed to queue stream");
        free(hw_stream);
        return -1;
    }
    codec_queue_frame_metadata(enc, user_data);

    /* Throttled: per-frame "encoded and queued" log suppressed */
    openimp_profile_end(OPENIMP_PROFILE_ENCODE_SUBMIT, submit_profile);
    return 0;
}

int AL_Codec_Encode_Process(void *codec, void *frame, void *user_data)
{
#if defined(PLATFORM_T31)
    AL_CodecEncode *enc = (AL_CodecEncode *)codec;
    int frames_before = 0;
    unsigned int submitted_before = 0;
    unsigned int drained_before = 0;
    int ret;

    pthread_mutex_lock(&g_t31_encode_core_lock);
    if (enc != NULL) {
        frames_before = enc->avpu.frames_encoded;
        submitted_before = enc->avpu.frame_number;
        drained_before = enc->avpu.completions_drained;

    }

    ret = al_codec_encode_process_impl(codec, frame, user_data);

    /* Raptor has one thread per encoded stream, but T31 has one physical
     * encoder channel.  Keep that channel assigned to this context until the
     * shared IRQ waiter consumes its command list. */
    if (ret == 0 && enc != NULL && enc->use_hardware == 2 &&
        enc->avpu.fd >= 0 && enc->avpu.frame_number != submitted_before) {
        int completed = 0;

        for (int retry = 0; retry < 2000; ++retry) {
            int pending = avpu_pending_peek(&enc->avpu, NULL, NULL);

            __sync_synchronize();
            if (!pending &&
                enc->avpu.completions_drained != drained_before) {
                completed = 1;
                break;
            }
            usleep(1000);
        }
        if (!completed) {
            LOG_CODEC("Process: T31 serialized completion timeout channel=%d enc=%d/%d drained=%u/%u pending=%d",
                      enc->channel_id - 1, enc->avpu.frames_encoded,
                      frames_before, enc->avpu.completions_drained,
                      drained_before,
                      enc->avpu.pending_stream_count);
        }
    }
    pthread_mutex_unlock(&g_t31_encode_core_lock);
    return ret;
#else
    return al_codec_encode_process_impl(codec, frame, user_data);
#endif
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
        ALAvpuContext *ctx = &enc->avpu;

        if (ctx->frames_consumed % 50 == 0)
        LOG_CODEC("GetStream[AVPU]: enc=%d cons=%d session=%d",
                  ctx->frames_encoded, ctx->frames_consumed, ctx->session_ready);

        if (!ctx->session_ready) {
            errno = EAGAIN;
            return -1;
        }

        for (int retry = 0; retry < 20; ++retry) {
            void *s = Fifo_Dequeue(enc->fifo_streams, 100);
            if (s != NULL) {
                HWStreamBuffer *hw_stream = (HWStreamBuffer *)s;
                *stream = s;
                *user_data = avpu_hw_stream_get_user_data(hw_stream);
                ctx->frames_consumed++;
                if (ctx->frames_consumed % 50 == 0)
                LOG_CODEC("GetStream[AVPU]: got queued stream stream=%p phys=0x%08x virt=0x%08x len=%u enc=%d cons=%d user=%p",
                          (void *)hw_stream, hw_stream->phys_addr,
                          hw_stream->virt_addr, hw_stream->length,
                          ctx->frames_encoded, ctx->frames_consumed, *user_data);
                return 0;
            }

            /*
             * A real T31 completion can be unusable (zero/fill status or a
             * header-only payload).  Its AVPU slot has already been drained,
             * so tell the wrapper to abandon this PollingStream attempt and
             * acquire the next captured frame.  Treating it as an ordinary
             * timeout makes the wrapper retry under its global encode lock
             * for minutes and appears as a permanently black/frozen stream.
             */
            if (ctx->reported_dropped_completions !=
                ctx->dropped_completions) {
                ctx->reported_dropped_completions =
                    ctx->dropped_completions;
                errno = ENODATA;
                return 1;
            }

            unsigned int core_status = 0;
            if (avpu_read_reg_quiet(ctx->fd, AVPU_REG_CORE_STATUS(0), &core_status) == 0)
                avpu_try_recover_sticky_completion(ctx, core_status, "GetStream[AVPU]");
        }

        LOG_CODEC("GetStream[AVPU]: TIMEOUT (frames_encoded=%d frames_consumed=%d)",
                  ctx->frames_encoded, ctx->frames_consumed);
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
    /* FIX: The stream_thread may call GetStream while use_hardware is
     * transitioning from 1→2. Both paths dequeue from the same fifo_streams,
     * so we may get an AVPU-produced HWStreamBuffer here. Always try to
     * extract user_data from the HWStreamBuffer first (AVPU stores it via
     * avpu_hw_stream_set_user_data). Fall back to fifo_frames only if the
     * HWStreamBuffer has no embedded user_data.
     *
     * Without this, the first frame's VBMUnlock never fires because
     * codec_user_data is NULL → VBM buffer idx=0 is never returned to
     * the kernel → DQBUF blocks → no more frames flow. */
    {
        HWStreamBuffer *hw = (HWStreamBuffer *)s;
        void *embedded_user = avpu_hw_stream_get_user_data(hw);
        if (embedded_user != NULL) {
            *user_data = embedded_user;
        } else {
            *user_data = codec_dequeue_frame_metadata(enc);
        }
    }
    /* Throttled: per-frame GetStream log suppressed (see static counter above) */
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
        /* OEM parity: stock AL_Codec_Encode_ReleaseStream does not poke AVPU
         * registers directly here; it returns the stream buffer through the
         * encoder-side stream manager path. In our direct-AVPU scaffolding,
         * the closest equivalent is to mark the completed buffer reusable in
         * local bookkeeping without issuing a second STRM_PUSH/QBUF. */
        HWStreamBuffer *hw_stream = (HWStreamBuffer*)stream;
        ALAvpuContext *ctx = &enc->avpu;
        int matched = 0;
        (void)user_data;

        if (ctx->session_ready) {
            for (int i = 0; i < ctx->stream_bufs_used; ++i) {
                if (ctx->stream_bufs[i].phy_addr == hw_stream->phys_addr) {
                    avpu_mark_stream_buffer_released(ctx, i);
                    if (ctx->frames_consumed % 50 == 0)
                    LOG_CODEC("ReleaseStream[AVPU]: released stream buf[%d] stream=%p phys=0x%08x virt=0x%08x len=%u",
                              i, (void *)hw_stream, hw_stream->phys_addr,
                              hw_stream->virt_addr, hw_stream->length);
                    matched = 1;
                    break;
                }
            }
            if (!matched) {
                LOG_CODEC("ReleaseStream[AVPU]: WARNING unmatched stream=%p phys=0x%08x virt=0x%08x len=%u",
                          (void *)hw_stream, hw_stream->phys_addr,
                          hw_stream->virt_addr, hw_stream->length);
            }
        }

        /* AVPU descriptors are embedded one-per-DMA-slot in the codec.  A
         * legacy heap descriptor can still cross the lazy-init transition,
         * so free only pointers which are not members of that stable pool. */
        {
            int pooled_descriptor = 0;

            for (int i = 0; i < 16; ++i) {
                if (hw_stream == &enc->avpu_stream_descriptors[i]) {
                    pooled_descriptor = 1;
                    break;
                }
            }
            if (!pooled_descriptor)
                free(hw_stream);
        }
        return 0;
    }

    /* Legacy/SW path follows libimp semantics (no refcounts) */
    if (user_data != NULL) {
        (void)user_data;
    }

    HWStreamBuffer *hw_stream = (HWStreamBuffer*)stream;
    /* Throttled: per-frame SW ReleaseStream log suppressed */
    if (hw_stream->virt_addr != 0 && hw_stream->phys_addr == 0) {
        /* Software-encoded stream - free the allocated buffer */
        void *data_ptr = (void*)(uintptr_t)hw_stream->virt_addr;
        free(data_ptr);
        /* Throttled: per-frame SW ReleaseStream free log suppressed */
    }
    free(hw_stream);
    return 0;
}

int AL_Codec_Encode_SetQpBounds(void *codec, int minQp, int maxQp)
{
    AL_CodecEncode *enc;

    if (codec == NULL || minQp < 0 || maxQp < 0)
        return -1;

    enc = (AL_CodecEncode *)codec;
    enc->hw_params.min_qp = clamp_qp_u32((uint32_t)minQp);
    enc->hw_params.max_qp = clamp_qp_u32((uint32_t)maxQp);
    if (enc->hw_params.min_qp > enc->hw_params.max_qp) {
        uint32_t tmp = enc->hw_params.min_qp;
        enc->hw_params.min_qp = enc->hw_params.max_qp;
        enc->hw_params.max_qp = tmp;
    }

    codec_param_write_qp_bounds(enc->codec_param,
                                enc->hw_params.qp ? enc->hw_params.qp : enc->hw_params.min_qp,
                                enc->hw_params.min_qp,
                                enc->hw_params.max_qp);
    enc->avpu.min_qp = enc->hw_params.min_qp;
    enc->avpu.max_qp = enc->hw_params.max_qp;
    codec_sync_rc_cache(enc);
    codec_set_error(enc, 0);

    LOG_CODEC("SetQpBounds: codec=%p min=%u max=%u",
              codec, enc->hw_params.min_qp, enc->hw_params.max_qp);
    return 0;
}

int AL_Codec_Encode_SetBitRate(void *codec, int targetBitrate, int maxBitrate)
{
    AL_CodecEncode *enc;
    uint32_t bitrate_bps;

    if (codec == NULL || targetBitrate < 0 || maxBitrate < 0)
        return -1;

    enc = (AL_CodecEncode *)codec;
    bitrate_bps = (uint32_t)(targetBitrate > 0 ? targetBitrate : maxBitrate);
#if defined(PLATFORM_T30)
    if (enc->t30_helix &&
        OpenIMP_T30_HelixSetBitrate(enc->t30_helix, bitrate_bps) != 0) {
        codec_set_error(enc, -1);
        return -1;
    }
#endif
    enc->hw_params.bitrate = bitrate_bps;
    enc->avpu.bitrate = bitrate_bps;
    codec_param_write_bitrate_bps(enc->codec_param, bitrate_bps);
    codec_sync_rc_cache(enc);
    codec_set_error(enc, 0);

    LOG_CODEC("SetBitRate: codec=%p target=%d max=%d stored=%u",
              codec, targetBitrate, maxBitrate, bitrate_bps);
    return 0;
}

int AL_Codec_Encode_GetRcParam(void *codec, void *rcAttr)
{
    AL_CodecEncode *enc;

    if (codec == NULL || rcAttr == NULL)
        return -1;

    enc = (AL_CodecEncode *)codec;
    codec_sync_rc_cache(enc);
    memcpy(rcAttr, &enc->rc_attr_cache, sizeof(enc->rc_attr_cache));
    return 0;
}

int AL_Codec_Encode_SetRcParam(void *codec, void *rcAttr)
{
    AL_CodecEncode *enc;
    IMPEncoderRcAttr *src;

    if (codec == NULL || rcAttr == NULL)
        return -1;

    enc = (AL_CodecEncode *)codec;
    src = (IMPEncoderRcAttr *)rcAttr;
    memcpy(&enc->rc_attr_cache, src, sizeof(*src));

    enc->fps_cache = src->outFrmRate;
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    enc->gop_cache.gopLength = src->maxGop;
#elif !(defined(PLATFORM_T31) || defined(PLATFORM_T40) || defined(PLATFORM_T41))
    enc->gop_cache = src->attrGop;
#endif
    if (enc->fps_cache.frmRateNum == 0)
        enc->fps_cache.frmRateNum = 25;
    if (enc->fps_cache.frmRateDen == 0)
        enc->fps_cache.frmRateDen = 1;
    if (enc->gop_cache.gopLength == 0)
        enc->gop_cache.gopLength = 25;

    codec_param_write_fps(enc->codec_param, &enc->fps_cache);
    *(uint32_t *)(enc->codec_param + 0xb0) = enc->gop_cache.gopLength;
    enc->hw_params.fps_num = enc->fps_cache.frmRateNum;
    enc->hw_params.fps_den = enc->fps_cache.frmRateDen;
    enc->hw_params.gop_length = enc->gop_cache.gopLength;
    enc->avpu.fps_num = enc->fps_cache.frmRateNum;
    enc->avpu.fps_den = enc->fps_cache.frmRateDen;
    enc->avpu.gop_length = enc->gop_cache.gopLength;

    switch (src->attrRcMode.rcMode) {
    case IMP_ENC_RC_MODE_CBR:
        *(uint32_t *)(enc->codec_param + 0x6c) = HW_RC_MODE_CBR;
        enc->hw_params.rc_mode = HW_RC_MODE_CBR;
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
        enc->hw_params.bitrate =
            src->attrRcMode.attrH264Cbr.outBitRate * 1000u;
        enc->hw_params.min_qp =
            clamp_qp_u32(src->attrRcMode.attrH264Cbr.minQp);
        enc->hw_params.max_qp =
            clamp_qp_u32(src->attrRcMode.attrH264Cbr.maxQp);
        enc->hw_params.qp =
            (enc->hw_params.min_qp + enc->hw_params.max_qp) / 2u;
#elif defined(PLATFORM_T31) || defined(PLATFORM_T40) || defined(PLATFORM_T41)
        enc->hw_params.bitrate = src->attrRcMode.attrH264Cbr.uTargetBitRate;
        enc->hw_params.qp = clamp_qp_u32(src->attrRcMode.attrH264Cbr.iInitialQP);
        enc->hw_params.min_qp = clamp_qp_u32(src->attrRcMode.attrH264Cbr.iMinQP);
        enc->hw_params.max_qp = clamp_qp_u32(src->attrRcMode.attrH264Cbr.iMaxQP);
        enc->avpu.qp_ip_delta = src->attrRcMode.attrH264Cbr.iIPDelta;
#else
        enc->hw_params.bitrate = src->attrRcMode.attrH264Cbr.maxGop;
        enc->hw_params.min_qp = clamp_qp_u32(src->attrRcMode.attrH264Cbr.minQp);
        enc->hw_params.max_qp = clamp_qp_u32(src->attrRcMode.attrH264Cbr.maxQp);
#endif
        break;
    case IMP_ENC_RC_MODE_VBR:
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    case IMP_ENC_RC_MODE_SMART:
#else
    case IMP_ENC_RC_MODE_CAPPED_VBR:
    case IMP_ENC_RC_MODE_CAPPED_QUALITY:
#endif
        *(uint32_t *)(enc->codec_param + 0x6c) = HW_RC_MODE_VBR;
        enc->hw_params.rc_mode = HW_RC_MODE_VBR;
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
        enc->hw_params.bitrate =
            src->attrRcMode.attrH264Vbr.maxBitRate * 1000u;
        enc->hw_params.min_qp =
            clamp_qp_u32(src->attrRcMode.attrH264Vbr.minQp);
        enc->hw_params.max_qp =
            clamp_qp_u32(src->attrRcMode.attrH264Vbr.maxQp);
        enc->hw_params.qp =
            (enc->hw_params.min_qp + enc->hw_params.max_qp) / 2u;
#elif defined(PLATFORM_T31) || defined(PLATFORM_T40) || defined(PLATFORM_T41)
        enc->hw_params.bitrate = src->attrRcMode.attrH264Vbr.uTargetBitRate;
        if (enc->hw_params.bitrate == 0)
            enc->hw_params.bitrate = src->attrRcMode.attrH264Vbr.uMaxBitRate;
        enc->hw_params.qp = clamp_qp_u32(src->attrRcMode.attrH264Vbr.iInitialQP);
        enc->hw_params.min_qp = clamp_qp_u32(src->attrRcMode.attrH264Vbr.iMinQP);
        enc->hw_params.max_qp = clamp_qp_u32(src->attrRcMode.attrH264Vbr.iMaxQP);
        enc->avpu.qp_ip_delta = src->attrRcMode.attrH264Vbr.iIPDelta;
#else
        enc->hw_params.bitrate = src->attrRcMode.attrH264Vbr.maxGop;
        enc->hw_params.min_qp = clamp_qp_u32(src->attrRcMode.attrH264Vbr.minQp);
        enc->hw_params.max_qp = clamp_qp_u32(src->attrRcMode.attrH264Vbr.maxQp);
#endif
        break;
    case IMP_ENC_RC_MODE_FIXQP:
    default:
        *(uint32_t *)(enc->codec_param + 0x6c) = HW_RC_MODE_FIXQP;
        enc->hw_params.rc_mode = HW_RC_MODE_FIXQP;
#if defined(PLATFORM_T41)
        enc->hw_params.qp =
            clamp_qp_u32(src->attrRcMode.attrH264FixQp.iInitialQP);
#else
        enc->hw_params.qp = clamp_qp_u32(src->attrRcMode.attrH264FixQp.qp);
#endif
        enc->hw_params.min_qp = enc->hw_params.qp;
        enc->hw_params.max_qp = enc->hw_params.qp;
        break;
    }

    codec_param_write_bitrate_bps(enc->codec_param, enc->hw_params.bitrate);
    codec_param_write_qp_bounds(enc->codec_param,
                                enc->hw_params.qp,
                                enc->hw_params.min_qp,
                                enc->hw_params.max_qp);
    enc->avpu.bitrate = enc->hw_params.bitrate;
    enc->avpu.rc_mode = enc->hw_params.rc_mode;
    enc->avpu.qp = enc->hw_params.qp;
    enc->avpu.min_qp = enc->hw_params.min_qp;
    enc->avpu.max_qp = enc->hw_params.max_qp;
    codec_sync_rc_cache(enc);
    codec_set_error(enc, 0);
    return 0;
}

int AL_Codec_Encode_GetFrameRate(void *codec, void *fps)
{
    if (codec == NULL || fps == NULL)
        return -1;

    memcpy(fps, &((AL_CodecEncode *)codec)->fps_cache, sizeof(IMPEncoderFrmRate));
    return 0;
}

int AL_Codec_Encode_SetFrameRate(void *codec, void *fps)
{
    AL_CodecEncode *enc;
    IMPEncoderFrmRate *rate;

    if (codec == NULL || fps == NULL)
        return -1;

    enc = (AL_CodecEncode *)codec;
    rate = (IMPEncoderFrmRate *)fps;
    enc->fps_cache = *rate;
    if (enc->fps_cache.frmRateNum == 0)
        enc->fps_cache.frmRateNum = 25;
    if (enc->fps_cache.frmRateDen == 0)
        enc->fps_cache.frmRateDen = 1;

    codec_param_write_fps(enc->codec_param, &enc->fps_cache);
    enc->hw_params.fps_num = enc->fps_cache.frmRateNum;
    enc->hw_params.fps_den = enc->fps_cache.frmRateDen;
    enc->avpu.fps_num = enc->fps_cache.frmRateNum;
    enc->avpu.fps_den = enc->fps_cache.frmRateDen;
    codec_sync_rc_cache(enc);
    codec_set_error(enc, 0);
    return 0;
}

int AL_Codec_Encode_SetQpIPDelta(void *codec, int delta)
{
    AL_CodecEncode *enc;

    if (codec == NULL || delta < -51 || delta > 51)
        return -1;

    enc = (AL_CodecEncode *)codec;
    enc->avpu.qp_ip_delta = delta;
#if defined(PLATFORM_T31) || defined(PLATFORM_T40) || defined(PLATFORM_T41)
    switch (enc->rc_attr_cache.attrRcMode.rcMode) {
    case IMP_ENC_RC_MODE_CBR:
        enc->rc_attr_cache.attrRcMode.attrH264Cbr.iIPDelta = (int16_t)delta;
        break;
    case IMP_ENC_RC_MODE_VBR:
    case IMP_ENC_RC_MODE_CAPPED_VBR:
    case IMP_ENC_RC_MODE_CAPPED_QUALITY:
        enc->rc_attr_cache.attrRcMode.attrH264Vbr.iIPDelta = (int16_t)delta;
        break;
    case IMP_ENC_RC_MODE_FIXQP:
    default:
        break;
    }
#elif defined(PLATFORM_T23) || defined(PLATFORM_T30)
    (void)delta;
#else
    enc->gop_cache.ipQpDelta = (uint32_t)delta;
    enc->rc_attr_cache.attrGop.ipQpDelta = (uint32_t)delta;
#endif
    codec_set_error(enc, 0);
    return 0;
}

int AL_Codec_Encode_RestartGop(void *codec)
{
    return AL_Codec_Encode_RequestIDR(codec);
}

int AL_Codec_Encode_GetGopParam(void *codec, void *gopAttr)
{
    if (codec == NULL || gopAttr == NULL)
        return -1;

    memcpy(gopAttr, &((AL_CodecEncode *)codec)->gop_cache, sizeof(IMPEncoderGopAttr));
    return 0;
}

int AL_Codec_Encode_SetGopParam(void *codec, void *gopAttr)
{
    AL_CodecEncode *enc;
    IMPEncoderGopAttr *gop;

    if (codec == NULL || gopAttr == NULL)
        return -1;

    enc = (AL_CodecEncode *)codec;
    gop = (IMPEncoderGopAttr *)gopAttr;
    enc->gop_cache = *gop;
    if (enc->gop_cache.gopLength == 0)
        enc->gop_cache.gopLength = 25;

    *(uint32_t *)(enc->codec_param + 0xb0) = enc->gop_cache.gopLength;
    enc->hw_params.gop_length = enc->gop_cache.gopLength;
    enc->avpu.gop_length = enc->gop_cache.gopLength;
#if defined(PLATFORM_T23) || defined(PLATFORM_T30)
    enc->rc_attr_cache.maxGop = enc->gop_cache.gopLength;
#elif !(defined(PLATFORM_T31) || defined(PLATFORM_T40) || defined(PLATFORM_T41))
    enc->rc_attr_cache.attrGop = enc->gop_cache;
#endif
    codec_set_error(enc, 0);
    return 0;
}

int AL_Codec_Encode_SetGopLength(void *codec, int gopLength)
{
    IMPEncoderGopAttr gop;

    if (codec == NULL || gopLength <= 0)
        return -1;

    gop = ((AL_CodecEncode *)codec)->gop_cache;
    gop.gopLength = (uint32_t)gopLength;
    return AL_Codec_Encode_SetGopParam(codec, &gop);
}

int AL_Codec_Encode_SetInputResolution(void *codec, int width, int height)
{
    AL_CodecEncode *enc;

    if (codec == NULL || width <= 0 || height <= 0)
        return -1;

    enc = (AL_CodecEncode *)codec;
    codec_param_write_input_resolution(enc->codec_param, (uint32_t)width, (uint32_t)height);
    enc->hw_params.width = (uint32_t)width;
    enc->hw_params.height = (uint32_t)height;
    enc->avpu.enc_w = (uint32_t)width;
    enc->avpu.enc_h = (uint32_t)height;
    enc->frame_buf_size = (int)avpu_get_nv12_frame_size(
        (uint32_t)width, (uint32_t)height);
    enc->stream_buf_size = (int)avpu_get_stream_buffer_size(
        (uint32_t)width, (uint32_t)height, enc->hw_params.bitrate);
    codec_set_error(enc, 0);
    return 0;
}

int AL_Codec_Encode_SetStreamBufferSize(void *codec, int size)
{
    AL_CodecEncode *enc;

    if (codec == NULL || size <= 0)
        return -1;
    enc = (AL_CodecEncode *)codec;
    if (enc->avpu.session_ready)
        return -1;
    enc->stream_buf_size = size;
    codec_set_error(enc, 0);
    return 0;
}

int AL_Codec_Encode_SetLoopFilterBetaOffset(void *codec, int offset)
{
    if (codec == NULL)
        return -1;
    ((AL_CodecEncode *)codec)->loop_filter_beta_offset = offset;
    codec_set_error((AL_CodecEncode *)codec, 0);
    return 0;
}

int AL_Codec_Encode_SetLoopFilterTcOffset(void *codec, int offset)
{
    if (codec == NULL)
        return -1;
    ((AL_CodecEncode *)codec)->loop_filter_tc_offset = offset;
    codec_set_error((AL_CodecEncode *)codec, 0);
    return 0;
}

int AL_Codec_Encode_GetLastError(void *codec)
{
    if (codec == NULL)
        return EINVAL;
    return ((AL_CodecEncode *)codec)->last_error;
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

int AL_Codec_Encode_RequestIDR(void *codec) {
    if (codec == NULL) {
        LOG_CODEC("RequestIDR: NULL codec");
        return -1;
    }

    AL_CodecEncode *enc = (AL_CodecEncode*)codec;
#if defined(PLATFORM_T23)
    if (enc->t23_helix.worker_pid > 0)
        return OpenIMP_T23_HelixRequestIDR(&enc->t23_helix);
#endif
#if defined(PLATFORM_T30)
    if (enc->t30_helix)
        return OpenIMP_T30_HelixRequestIDR(enc->t30_helix);
#endif
    __sync_lock_test_and_set(&enc->force_next_idr, 1);

    { static unsigned int idr_req_count = 0; unsigned int c = __sync_add_and_fetch(&idr_req_count, 1);
      if (c <= 3 || (c % 50) == 0)
        LOG_CODEC("RequestIDR: codec=%p channel=%d pending=1 [#%u]", codec, enc->channel_id - 1, c);
    }

    return 0;
}
