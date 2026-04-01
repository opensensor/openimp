/*
 * AL AVPU - Allegro Video Processing Unit abstraction
 *
 * Models the OEM architecture from libimp.so v1.1.6 Binary Ninja decompilation:
 *
 * OEM Layer Stack:
 *   AL_Board_Create         (0x3612c) → allocates LinuxIpControl (0x100 bytes)
 *     LinuxIpControlVtable  → ReadRegister, WriteRegister, RegisterCallBack
 *     WaitInterruptThread   (0x35e28) → polls AL_CMD_IP_WAIT_IRQ
 *     AL_DevicePool_Open    (0x362dc) → ref-counted /dev/avpu fd
 *
 *   AL_EncCore_Init         (0x6c8d8) → registers IRQ callbacks, ResetCore, enables IRQs
 *   AL_EncCore_Encode1      (0x6cbf0) → StartEnc1WithCommandList
 *   AL_EncCore_Reset        (0x6cbcc) → writes 1,2,4 to core reset register
 *
 * Source files (from assert paths in binary):
 *   lib_fpga/DevicePool.c
 *   lib_scheduler_enc/CoreManager.c
 *   lib_encode/Com_Encoder.c
 */

#ifndef OPENIMP_AL_AVPU_H
#define OPENIMP_AL_AVPU_H

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- DMA buffer descriptor ---- */
typedef struct AvpuDMABuf {
    uint32_t phy_addr;   /* physical address returned by driver or rmem */
    int mmap_off;        /* page-aligned offset for mmap (driver-provided) */
    int dmabuf_fd;       /* optional dmabuf fd from GET_DMA_FD */
    void *map;           /* mapped CPU pointer (or rmem virtual) */
    size_t size;         /* size in bytes */
    int from_rmem;       /* 1 if allocated via rmem/IMP_Alloc (do not munmap) */
} AvpuDMABuf;

/* ---- LinuxIpControl vtable (OEM: LinuxIpControlVtable at AL_Board_Create) ----
 *
 * OEM struct layout (0x100 bytes, calloc'd):
 *   [0]  = vtable pointer (LinuxIpControlVtable)
 *   [1]  = fd from AL_DevicePool_Open  (offset +0x04)
 *   [2]  = WaitInterruptThread handle  (offset +0x08)
 *   [3]  = mutex from Rtos_CreateMutex (offset +0x0C)
 *   [4..] = IRQ callback array: 20 slots × 12 bytes each (offset +0x10..+0xF0)
 *           Per slot: [callback_fn, flag, user_data] at +0x10+(idx*12)
 *
 * Vtable layout (function pointer offsets from vtable base):
 *   +0x00: (unused/destroy)
 *   +0x04: ReadRegister   → LinuxIpCtrl_ReadRegister  (0x35db8)
 *   +0x08: WriteRegister  → LinuxIpCtrl_WriteRegister (0x35d50)
 *   +0x0C: RegisterCallBack → LinuxIpCtrl_RegisterCallBack (0x35fd0)
 */

/* Maximum IRQ slots (OEM checks idx < 0x14 = 20) */
#define AL_MAX_IRQ_SLOTS 20

/* IRQ callback slot (12 bytes each in OEM) */
typedef struct {
    void (*callback)(void *user_data);  /* +0x00: callback function */
    int   flag;                         /* +0x04: 1 if no callback, signal instead */
    void *user_data;                    /* +0x08: user data for callback */
} AL_IrqSlot;

/* Forward declaration */
typedef struct AL_IpCtrl AL_IpCtrl;

/* Vtable for IP controller (OEM: LinuxIpControlVtable) */
typedef struct {
    void (*destroy)(AL_IpCtrl *self);
    int  (*read_reg)(AL_IpCtrl *self, unsigned int reg);
    void (*write_reg)(AL_IpCtrl *self, unsigned int reg, unsigned int val);
    void (*register_callback)(AL_IpCtrl *self, void (*cb)(void*), void *user_data, int irq_idx);
} AL_IpCtrlVtable;

/* IP controller object (OEM: 0x100 bytes on MIPS32 from AL_Board_Create)
 * Note: OEM layout is 0x100 bytes on 32-bit MIPS. On 64-bit hosts this struct
 * will be larger due to pointer size differences. The logical layout is preserved. */
struct AL_IpCtrl {
    const AL_IpCtrlVtable *vtable;      /* OEM +0x00 */
    int fd;                              /* OEM +0x04: /dev/avpu fd */
    pthread_t irq_thread;               /* OEM +0x08: WaitInterruptThread */
    pthread_mutex_t *irq_mutex;          /* OEM +0x0C: protects callback array */
    AL_IrqSlot irq_slots[AL_MAX_IRQ_SLOTS]; /* OEM +0x10: callback array */
};

/* ---- EncCore state (OEM: passed to AL_EncCore_Init) ----
 *
 * OEM layout (from AL_EncCore_Init decompilation):
 *   [0]  = IP controller pointer (AL_IpCtrl*)
 *   [1]  = current command list pointer
 *   [3].b = core number (byte)
 *   [4]  = core number (word)
 *   [5]  = param from arg2[0]
 *   [6]  = param from arg2[1]
 *   [7]  = arg5
 *   [8]  = state (1 = initialized)
 *   [9]  = state2 (0)
 *   [0x10] = return value (2)
 */
typedef struct {
    AL_IpCtrl *ip_ctrl;         /* [0]: IP controller */
    void *cmd_list;             /* [1]: current command list buffer */
    uint32_t _reserved2;        /* [2] */
    uint8_t core_id;            /* [3].b: core number */
    uint32_t core_id_w;         /* [4]: core number (word) */
    uint32_t param0;            /* [5]: from init arg2[0] */
    uint32_t param1;            /* [6]: from init arg2[1] */
    uint32_t param2;            /* [7]: from init arg5 */
    int state;                  /* [8]: 1=initialized */
    int state2;                 /* [9] */
    uint32_t _reserved[6];      /* [10..15] */
    uint32_t ret_val;           /* [0x10]: return value (2) */
} AL_EncCoreCtx;

/* ---- ALAvpuContext: combined state for our implementation ---- */
typedef struct ALAvpuContext {
    int fd;                   /* /dev/avpu fd (from AL_DevicePool_Open) */
    int event_fd;             /* optional eventfd for stream readiness */
    AL_IpCtrl *board;         /* IP controller from AL_Board_Create */
    AL_EncCoreCtx enc_core;   /* Encoder core context */

    /* Stream buffer pool config */
    int stream_buf_count;
    int stream_buf_size;
    int frame_buf_count;
    int frame_buf_size;

    /* Stream buffers */
    AvpuDMABuf stream_bufs[16];
    int stream_bufs_used;
    unsigned char stream_in_hw[16];

    /* Addressing */
    uint32_t axi_base;
    int use_offsets;

    /* Command-list ring (OEM: 0x13 entries × 512B) */
    AvpuDMABuf cl_ring;
    uint32_t cl_entry_size;   /* 512 */
    uint32_t cl_count;        /* 0x13 */

    /* Encoding parameters */
    uint32_t enc_w;
    uint32_t enc_h;
    uint32_t fps_num;
    uint32_t fps_den;
    int profile;
    uint32_t rc_mode;
    uint32_t qp;
    uint32_t entropy_mode;
    uint32_t gop_length;
    uint32_t format_word;
    /* OEM Enc1 slice-param words threaded into the AVPU context so command-list
     * packing can match SliceParamToCmdRegsEnc1 instead of width heuristics. */
    uint32_t enc1_cmd_0a_74;
    uint32_t enc1_cmd_0b_7a;
    uint32_t enc1_cmd_0b_7c;
    uint32_t enc1_cmd_0b_7e;
    uint32_t enc1_cmd_0b_7f;
    uint32_t enc1_cmd_0b_80;
    uint32_t enc1_slice_10;
    uint32_t enc1_cmd_12_a8;
    uint32_t enc1_cmd_12_aa;
    uint32_t enc1_cmd_12_ac;
    uint32_t cl_idx;
    volatile int reference_valid;

    volatile int frames_encoded;

    /* Legacy IRQ state (used by codec.c WaitInterruptThread directly)
     * TODO: migrate codec.c to use Board/IpCtrl abstraction, then remove these.
     * In OEM these fields live in AL_IpCtrl (+0x10..+0xF0), not in the encoder context. */
    long irq_callbacks[60];       /* 20 IRQs × 3 longs: [callback, user_data, flag] */
    void *irq_mutex;              /* pthread_mutex_t* for callback access */
    long irq_thread;              /* pthread_t stored as long */
    int irq_thread_running;

    /* Session state */
    int session_ready;
    int hw_prepared;

    /* Reconstruction and reference frame DMA buffers (OEM parity).
     * The AVPU hardware writes reconstructed frames to rec_buf and reads
     * reference frames from ref_buf.  Without valid physical addresses in
     * the command-list, the AVPU DMAs to/from address 0x0, hanging the
     * AXI bus and crashing the SoC. */
    AvpuDMABuf rec_buf;     /* reconstruction buffer (Y + UV planes) */
    AvpuDMABuf ref_buf;     /* reference frame buffer (Y + UV planes) */
} ALAvpuContext;

/* ---- Board / IP Controller API (OEM parity) ---- */

/**
 * AL_Board_Create - Create IP controller for AVPU
 * OEM: 0x3612c - calloc(1, 0x100), sets vtable, opens device, creates IRQ thread
 * @param device_path Path to device (e.g., "/dev/avpu")
 * @return IP controller pointer, or NULL on failure
 */
AL_IpCtrl *AL_Board_Create(const char *device_path);

/**
 * AL_Board_Destroy - Destroy IP controller
 * OEM: 0x36220 - checks magic, calls LinuxIpCtrl_Destroy
 * @param ctrl IP controller to destroy
 */
void AL_Board_Destroy(AL_IpCtrl *ctrl);

/* ---- EncCore API (OEM parity) ---- */

/**
 * AL_EncCore_Init - Initialize encoder core
 * OEM: 0x6c8d8 - registers EndEncoding/EndAvcEntropy callbacks, ResetCore,
 *                clears interrupts (0xFFFFFF to 0x8018), sets TOP_CTRL (0x80 to 0x8054)
 */
int AL_EncCore_Init(AL_EncCoreCtx *ctx, AL_IpCtrl *ip_ctrl, int core,
                    void (*end_encoding_cb)(void*), void *cb_data);

/**
 * AL_EncCore_Deinit - Deinitialize encoder core
 * OEM: 0x6ca68 - unregisters callbacks
 */
void AL_EncCore_Deinit(AL_EncCoreCtx *ctx);

/**
 * AL_EncCore_Reset - Reset encoder core
 * OEM: 0x6cbcc → ResetCore.isra.27 (0x6c398)
 * Writes 1, then 2, then 4 to (core<<9)+0x83F0
 */
void AL_EncCore_Reset(AL_EncCoreCtx *ctx);

/**
 * AL_EncCore_Encode1 - Start Enc1 encoding with command list
 * OEM: 0x6cbf0 → StartEnc1WithCommandList.isra.25 (0x6c320)
 * Writes CL address to (core<<9)+0x83E0, push value to (core<<9)+0x83E4
 */
int AL_EncCore_Encode1(AL_EncCoreCtx *ctx, uint32_t *cmd_regs, void *cmd_regs2, int combined);

/**
 * AL_EncCore_EnableInterrupts - Enable interrupt bits for core
 * OEM: 0x6cf78 - manipulates 0x8014 mask register per core
 */
void AL_EncCore_EnableInterrupts(AL_EncCoreCtx *ctx, int num_cores, int enc1, int enc2, int entropy);

/**
 * AL_EncCore_TurnOnGC - Enable gate clock for core
 * OEM: 0x6c5b8 → TurnOnGC.constprop.36 → SetClockCommand(ip_ctrl, core, 1)
 * SetClockCommand writes to (core<<9)+0x83F4, sets bits[1:0] to value
 */
void AL_EncCore_TurnOnGC(AL_EncCoreCtx *ctx);

/**
 * AL_EncCore_TurnOffGC - Disable gate clock for core
 * OEM: 0x6c618 → TurnOffGC.constprop.35 → SetClockCommand(ip_ctrl, core, 0)
 */
void AL_EncCore_TurnOffGC(AL_EncCoreCtx *ctx);

#ifdef __cplusplus
}
#endif

#endif /* OPENIMP_AL_AVPU_H */

