/*
 * AL AVPU scaffolding (vendor-like AL layer over /dev/avpu)
 *
 * This is a minimal, compile-ready scaffold to support a vendor-like AL layer.
 * It does NOT implement real encoding yet. The goal is to mirror the control
 * flow and data structures we saw in libimp.so 1.1.6 so we can integrate
 * incrementally.
 */

#ifndef OPENIMP_AL_AVPU_H
#define OPENIMP_AL_AVPU_H

#include <stdint.h>
#include <stddef.h>

#include "hw_encoder.h"  /* For HWFrameBuffer / HWStreamBuffer types */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ALAvpuContext roughly corresponds to the vendor's AL_CodecEncode internal state
 * around offsets 0x798.. etc. We only model what we need now.
 */
typedef struct AvpuDMABuf {
    uint32_t phy_addr;   /* physical address returned by driver or rmem */
    int mmap_off;        /* page-aligned offset for mmap (driver-provided) */
    int dmabuf_fd;       /* optional dmabuf fd from GET_DMA_FD */
    void *map;           /* mapped CPU pointer (or rmem virtual) */
    size_t size;         /* size in bytes */
    int from_rmem;       /* 1 if allocated via rmem/IMP_Alloc (do not munmap) */
} AvpuDMABuf;

typedef struct ALAvpuContext {
    int fd;                   /* /dev/avpu fd */
    int event_fd;             /* optional eventfd for stream readiness */

    /* Stream buffer pool config (matches GetSrcStreamCntAndSize semantics) */
    int stream_buf_count;     /* default ~7 (vendor varies by settings) */
    int stream_buf_size;      /* e.g., 128KB; to be tuned per profile/bitrate */

    /* Source (frame) buffer pool config (GetSrcStreamCntAndSize semantics) */
    int frame_buf_count;      /* e.g., 3-6 frames */
    int frame_buf_size;       /* size per source frame descriptor */

    /* Stream buffers managed via GET_DMA_MMAP */
    AvpuDMABuf stream_bufs[16];
    int stream_bufs_used;     /* actual count provisioned */
    unsigned char stream_in_hw[16]; /* 1 if queued to HW */

    /* Addressing mode: persist AXI base and whether offsets are used */
    uint32_t axi_base;
    int use_offsets;
    int force_cl_abs;         /* debug: force CL start/end to absolute phys addrs */
    int disable_axi_offset;   /* debug: disable AXI offset mode globally */

    /* Command-list ring (userspace-managed; OEM uses 0x13 entries x 512B) */
    AvpuDMABuf cl_ring;       /* backing storage (rmem/IMP_Alloc) */
    uint32_t cl_entry_size;   /* bytes per entry (512) */
    uint32_t cl_count;        /* number of entries (0x13) */

    /* Encoding parameters (cached for command-list fill) */
    uint32_t enc_w;
    uint32_t enc_h;
    uint32_t fps_num;
    uint32_t fps_den;
    int profile;
    uint32_t rc_mode;
    uint32_t qp;
    uint32_t gop_length;

    /* Command-list state */
    uint32_t cl_idx;


    /* Simple IRQ queue to mirror vendor's FIFO wakeups */
    /* WaitInterruptThread support (OEM parity: 0x35e28) */
    long irq_callbacks[60];       /* 20 IRQs * 3 ints each: [callback_fn, user_data, flag] */
    void *irq_mutex;              /* pthread_mutex_t* for callback access */
    long irq_thread;              /* pthread_t stored as long */
    int irq_thread_running;

    /* FIFO structures (OEM parity: Fifo_Init at 0x7af28)
     * OEM uses FIFOs at encoder+0x7f8 (streams) and encoder+0x81c (metadata)
     * Structure layout from decompilation:
     * [0] = max_elements + 1
     * [1] = write_idx
     * [2] = read_idx
     * [3] = buffer pointer
     * [4] = mutex
     * [5] = event
     * [6] = count
     * [7] = flag (byte)
     * [8] = semaphore
     */
    long fifo_streams[9];     /* FIFO for encoded stream buffers */
    long fifo_metadata[9];    /* FIFO for metadata/tags */

    /* Session state */
    int session_ready;        /* when 1, reg pushes + IRQ thread enabled */
    int hw_prepared;          /* base HW configured (regs set, ENC_EN on, no CL yet) */
} ALAvpuContext;

/* REMOVED: ALL ALAvpu_* wrapper functions do NOT exist in OEM binary
 * Confirmed via Binary Ninja MCP search - no ALAvpu_Init, ALAvpu_Deinit,
 * ALAvpu_QueueFrame, ALAvpu_DequeueStream, ALAvpu_ReleaseStream, or ALAvpu_SetEvent.
 *
 * OEM architecture (from Binary Ninja decompilation):
 * - Device opening: AL_DevicePool_Open("/dev/avpu") at 0x362dc
 * - Context init: AL_Common_Encoder_CreateChannel (called from AL_Encoder_Create)
 * - Frame encoding: Direct ioctl calls (AL_CMD_IP_WRITE_REG, AL_CMD_IP_READ_REG)
 * - Stream retrieval: Fifo_Dequeue (not ALAvpu_DequeueStream)
 * - Stream release: AL_Encoder_PutStreamBuffer (not ALAvpu_ReleaseStream)
 *
 * All AVPU operations are performed directly in the encoder layer (codec.c)
 * using direct ioctl calls to /dev/avpu, matching OEM behavior exactly.
 */

#ifdef __cplusplus
}
#endif

#endif /* OPENIMP_AL_AVPU_H */

