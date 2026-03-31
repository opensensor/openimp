/*
 * AL AVPU - Allegro Video Processing Unit abstraction
 *
 * Implements the OEM Board/IpCtrl/EncCore layer from libimp.so v1.1.6.
 * Source file paths from OEM binary assert strings:
 *   lib_fpga/DevicePool.c  (AL_DevicePool_*)
 *   lib_fpga/BoardHardware.c (AL_Board_Create, LinuxIpCtrl_*)
 *   lib_scheduler_enc/CoreManager.c (AL_EncCore_*, ResetCore, SetClockCommand)
 */

#include "al_avpu.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <pthread.h>

#include "device_pool.h"

/* /dev/avpu ioctl definitions */
#ifndef AVPU_IOC_MAGIC
#define AVPU_IOC_MAGIC 'q'
#endif
struct avpu_reg { unsigned int id; unsigned int value; };
#define AL_CMD_UNBLOCK_CHANNEL _IO(AVPU_IOC_MAGIC, 1)
#define AL_CMD_IP_WRITE_REG    _IOWR(AVPU_IOC_MAGIC, 10, struct avpu_reg)
#define AL_CMD_IP_READ_REG     _IOWR(AVPU_IOC_MAGIC, 11, struct avpu_reg)
#define AL_CMD_IP_WAIT_IRQ     _IOWR(AVPU_IOC_MAGIC, 12, int)

#define LOG_AL(fmt, ...) fprintf(stderr, "[AL-AVPU] " fmt "\n", ##__VA_ARGS__)

/* ---- AVPU register map (from OEM decompilation) ---- */
#define AVPU_BASE_OFFSET       0x8000
#define AVPU_INTERRUPT_MASK    (AVPU_BASE_OFFSET + 0x14)  /* 0x8014 */
#define AVPU_INTERRUPT         (AVPU_BASE_OFFSET + 0x18)  /* 0x8018 */
#define AVPU_REG_TOP_CTRL      (AVPU_BASE_OFFSET + 0x54)  /* 0x8054 */

/* Per-core register window: base = 0x83F0 + (core << 9) */
#define CORE_BASE(core) ((AVPU_BASE_OFFSET + 0x3F0) + ((unsigned)(core) << 9))
#define REG_CORE_RESET(c)   (CORE_BASE(c) + 0x00) /* 0x83F0: write 1,2,4 sequence */
#define REG_CORE_CLKCMD(c)  (CORE_BASE(c) + 0x04) /* 0x83F4: clock command bits[1:0] */
#define REG_CORE_DOORBELL(c) (CORE_BASE(c) + 0x08) /* 0x83F8: bit 1 = enc1 running */

/* Per-core command list registers (OEM: (core<<9) + 0x83E0/0x83E4) */
#define REG_CL_ADDR(c)      ((AVPU_BASE_OFFSET + 0x3E0) + ((unsigned)(c) << 9))
#define REG_CL_PUSH(c)      ((AVPU_BASE_OFFSET + 0x3E4) + ((unsigned)(c) << 9))

/* Board identity magic (from AL_Board_Destroy: reads 0x8004, checks == 0x72000460) */
#define AVPU_REG_BOARD_ID      (AVPU_BASE_OFFSET + 0x04)  /* 0x8004 */
#define AVPU_BOARD_MAGIC       0x72000460



/* ================================================================
 * LinuxIpCtrl - Low-level register I/O vtable implementation
 * OEM source: lib_fpga/BoardHardware.c
 * ================================================================ */

/**
 * LinuxIpCtrl_ReadRegister (OEM: 0x35db8)
 * Uses ioctl AL_CMD_IP_READ_REG (0xc008710b).
 * Puts {reg_offset, 0} on stack, returns the value field after ioctl.
 */
static int ipctrl_read_reg(AL_IpCtrl *self, unsigned int reg)
{
    struct avpu_reg r;
    r.id = reg;
    r.value = 0;
    if (ioctl(self->fd, AL_CMD_IP_READ_REG, &r) < 0) {
        LOG_AL("ReadReg[0x%04x] IOCTL failed: %s", reg, strerror(errno));
        return 0;
    }
    return (int)r.value;
}

/**
 * LinuxIpCtrl_WriteRegister (OEM: 0x35d50)
 * Uses ioctl AL_CMD_IP_WRITE_REG (0xc008710a).
 */
static void ipctrl_write_reg(AL_IpCtrl *self, unsigned int reg, unsigned int val)
{
    struct avpu_reg r;
    r.id = reg;
    r.value = val;
    if (ioctl(self->fd, AL_CMD_IP_WRITE_REG, &r) < 0) {
        LOG_AL("WriteReg[0x%04x]=0x%08x IOCTL failed: %s", reg, val, strerror(errno));
    }
}

/**
 * LinuxIpCtrl_RegisterCallBack (OEM: 0x35fd0)
 * Registers an IRQ callback at the given slot index.
 * If cb is NULL, sets flag=1 (signal mode instead of callback mode).
 */
static void ipctrl_register_callback(AL_IpCtrl *self, void (*cb)(void*),
                                     void *user_data, int irq_idx)
{
    if (irq_idx < 0 || irq_idx >= AL_MAX_IRQ_SLOTS) return;
    pthread_mutex_lock(self->irq_mutex);
    self->irq_slots[irq_idx].callback = cb;
    self->irq_slots[irq_idx].user_data = user_data;
    self->irq_slots[irq_idx].flag = (cb == NULL) ? 1 : 0;
    pthread_mutex_unlock(self->irq_mutex);
}

/**
 * LinuxIpCtrl_Destroy (OEM: 0x36098)
 * Unblocks channel, joins IRQ thread, cleans up, closes device.
 */
static void ipctrl_destroy(AL_IpCtrl *self)
{
    if (!self) return;
    /* Unblock the WaitInterruptThread (OEM: ioctl AL_CMD_UNBLOCK_CHANNEL) */
    ioctl(self->fd, AL_CMD_UNBLOCK_CHANNEL, 0);
    /* Join and clean up the IRQ thread */
    if (self->irq_thread) {
        pthread_join(self->irq_thread, NULL);
        self->irq_thread = 0;
    }
    if (self->irq_mutex) {
        pthread_mutex_destroy(self->irq_mutex);
        free(self->irq_mutex);
        self->irq_mutex = NULL;
    }
    AL_DevicePool_Close(self->fd);
    free(self);
}

/* LinuxIpControlVtable (OEM: referenced in AL_Board_Create) */
static const AL_IpCtrlVtable g_LinuxIpControlVtable = {
    .destroy           = ipctrl_destroy,
    .read_reg          = ipctrl_read_reg,
    .write_reg         = ipctrl_write_reg,
    .register_callback = ipctrl_register_callback,
};


/* ================================================================
 * WaitInterruptThread (OEM: 0x35e28)
 *
 * Polls AL_CMD_IP_WAIT_IRQ (ioctl 0xc004710c) in a loop.
 * When an IRQ fires, looks up the callback in the irq_slots array
 * and invokes it. If no callback is registered but flag is set,
 * logs and continues.
 * ================================================================ */
static void *WaitInterruptThread(void *arg)
{
    AL_IpCtrl *self = (AL_IpCtrl *)arg;

    while (1) {
        int irq_idx = -1;
        int ret = ioctl(self->fd, AL_CMD_IP_WAIT_IRQ, &irq_idx);
        if (ret == -1) {
            if (errno == EINTR)
                continue;
            /* AL_CMD_UNBLOCK_CHANNEL causes IOCTL to return error → exit */
            break;
        }

        if (irq_idx < 0 || irq_idx >= AL_MAX_IRQ_SLOTS) {
            LOG_AL("WaitIRQ: got %d, no interrupt to handle", irq_idx);
            continue;
        }

        pthread_mutex_lock(self->irq_mutex);
        AL_IrqSlot *slot = &self->irq_slots[irq_idx];
        if (slot->callback) {
            slot->callback(slot->user_data);
        } else if (!slot->flag) {
            LOG_AL("WaitIRQ: IRQ %d has no handler, ignoring", irq_idx);
        }
        pthread_mutex_unlock(self->irq_mutex);
    }

    return NULL;
}

/* ================================================================
 * AL_Board_Create / AL_Board_Destroy (OEM: 0x3612c / 0x36220)
 * ================================================================ */

AL_IpCtrl *AL_Board_Create(const char *device_path)
{
    /* OEM: calloc(1, 0x100) — 256 bytes on MIPS32. We use sizeof for portability. */
    AL_IpCtrl *ctrl = (AL_IpCtrl *)calloc(1, sizeof(AL_IpCtrl));
    if (!ctrl) return NULL;

    ctrl->vtable = &g_LinuxIpControlVtable;

    int fd = AL_DevicePool_Open(device_path);
    if (fd < 0) {
        LOG_AL("Board_Create: failed to open '%s'", device_path);
        free(ctrl);
        return NULL;
    }
    ctrl->fd = fd;

    /* Create mutex for IRQ callback array */
    ctrl->irq_mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    if (!ctrl->irq_mutex || pthread_mutex_init(ctrl->irq_mutex, NULL) != 0) {
        AL_DevicePool_Close(fd);
        free(ctrl->irq_mutex);
        free(ctrl);
        return NULL;
    }

    /* Create WaitInterruptThread */
    if (pthread_create(&ctrl->irq_thread, NULL, WaitInterruptThread, ctrl) != 0) {
        pthread_mutex_destroy(ctrl->irq_mutex);
        free(ctrl->irq_mutex);
        AL_DevicePool_Close(fd);
        free(ctrl);
        return NULL;
    }

    LOG_AL("Board_Create: OK fd=%d ctrl=%p", fd, (void *)ctrl);
    return ctrl;
}

void AL_Board_Destroy(AL_IpCtrl *ctrl)
{
    if (!ctrl) return;

    /* OEM checks board identity register before destroying */
    int id = ctrl->vtable->read_reg(ctrl, AVPU_REG_BOARD_ID);
    if (id != (int)AVPU_BOARD_MAGIC) {
        LOG_AL("Board_Destroy: unexpected board ID 0x%08x (expected 0x%08x)",
               (unsigned)id, AVPU_BOARD_MAGIC);
    }

    ctrl->vtable->destroy(ctrl);
}

/* ================================================================
 * ResetCore (OEM: ResetCore.isra.27 at 0x6c398)
 *
 * Writes 1, then 2, then 4 to (core<<9)+0x83F0 via the vtable.
 * ================================================================ */
static void ResetCore(AL_IpCtrl *ip, int core)
{
    unsigned reg = REG_CORE_RESET(core);
    ip->vtable->write_reg(ip, reg, 1);
    ip->vtable->write_reg(ip, reg, 2);
    ip->vtable->write_reg(ip, reg, 4);
    LOG_AL("ResetCore: core=%d reg=0x%04x (wrote 1,2,4)", core, reg);
}

/* ================================================================
 * SetClockCommand (OEM: 0x6c428)
 *
 * Reads (core<<9)+0x83F4, sets bits[1:0] to the given value via
 * XOR-mask: new = ((old ^ val) & 3) ^ old
 * ================================================================ */
static void SetClockCommand(AL_IpCtrl *ip, int core, int val)
{
    unsigned reg = REG_CORE_CLKCMD(core);
    int old = ip->vtable->read_reg(ip, reg);
    int new_val = ((old ^ val) & 3) ^ old;
    ip->vtable->write_reg(ip, reg, (unsigned)new_val);
}

/* ================================================================
 * IsEnc1AlreadyRunning (OEM: 0x6c678)
 *
 * Reads doorbell register (core<<9)+0x83F8, checks bit 1.
 * Returns 1 if already running (asserts in OEM), 0 if idle.
 * ================================================================ */
static int IsEnc1AlreadyRunning(AL_IpCtrl *ip, int core)
{
    int val = ip->vtable->read_reg(ip, REG_CORE_DOORBELL(core));
    return (val & 2) ? 1 : 0;
}

/* ================================================================
 * StartEnc1WithCommandList (OEM: StartEnc1WithCommandList.isra.25 at 0x6c320)
 *
 * Writes command list address to (core<<9)+0x83E0, then
 * writes push_val to (core<<9)+0x83E4 to trigger encoding.
 * push_val=2 for Enc1, push_val=8 for combined Enc1+Enc2.
 * ================================================================ */
static void StartEnc1WithCommandList(AL_IpCtrl *ip, int core,
                                     uint32_t *cmd_regs, int push_val)
{
    /* OEM writes the cmd_regs pointer/address to CL_ADDR, then triggers */
    ip->vtable->write_reg(ip, REG_CL_ADDR(core), (unsigned)(uintptr_t)cmd_regs);
    ip->vtable->write_reg(ip, REG_CL_PUSH(core), (unsigned)push_val);
}

/* ================================================================
 * AL_EncCore_* public API implementations
 * ================================================================ */

int AL_EncCore_Init(AL_EncCoreCtx *ctx, AL_IpCtrl *ip_ctrl, int core,
                    void (*end_encoding_cb)(void *), void *cb_data)
{
    if (!ctx || !ip_ctrl) return -1;
    memset(ctx, 0, sizeof(*ctx));

    ctx->ip_ctrl = ip_ctrl;
    ctx->core_id = (uint8_t)core;
    ctx->core_id_w = (uint32_t)core;

    /* OEM: Register EndEncoding callback at IRQ index (core*4)
     * and EndAvcEntropy callback at IRQ index (core*4+2) */
    int irq_enc1 = core * 4;
    int irq_entropy = core * 4 + 2;
    ip_ctrl->vtable->register_callback(ip_ctrl, end_encoding_cb, cb_data, irq_enc1);
    ip_ctrl->vtable->register_callback(ip_ctrl, end_encoding_cb, cb_data, irq_entropy);
    LOG_AL("EncCore_Init: registered callbacks at IRQ %d and %d", irq_enc1, irq_entropy);

    /* OEM: ResetCore */
    ResetCore(ip_ctrl, core);

    /* OEM: Clear all pending interrupts by writing 0xFFFFFF to 0x8018 */
    ip_ctrl->vtable->write_reg(ip_ctrl, AVPU_INTERRUPT, 0xFFFFFF);

    /* OEM: Write 0x80 to TOP_CTRL (0x8054) */
    ip_ctrl->vtable->write_reg(ip_ctrl, AVPU_REG_TOP_CTRL, 0x80);

    ctx->state = 1;   /* OEM: ctx[8] = 1 */
    ctx->state2 = 0;  /* OEM: ctx[9] = 0 */
    ctx->ret_val = 2;  /* OEM: ctx[0x10] = 2 */

    LOG_AL("EncCore_Init: core=%d OK", core);
    return 0;
}

void AL_EncCore_Deinit(AL_EncCoreCtx *ctx)
{
    if (!ctx || !ctx->ip_ctrl) return;

    /* OEM: Unregister callbacks by setting them to NULL */
    int core = (int)ctx->core_id;
    int irq_enc1 = core * 4;
    int irq_entropy = core * 4 + 2;
    ctx->ip_ctrl->vtable->register_callback(ctx->ip_ctrl, NULL, NULL, irq_enc1);
    ctx->ip_ctrl->vtable->register_callback(ctx->ip_ctrl, NULL, NULL, irq_entropy);

    /* OEM also unregisters a third IRQ at (core*4) again (for safety) */
    ctx->ip_ctrl->vtable->register_callback(ctx->ip_ctrl, NULL, NULL, irq_enc1);

    ctx->state = 0;
    LOG_AL("EncCore_Deinit: core=%d", core);
}

void AL_EncCore_Reset(AL_EncCoreCtx *ctx)
{
    if (!ctx || !ctx->ip_ctrl) return;
    ResetCore(ctx->ip_ctrl, (int)ctx->core_id);
}

int AL_EncCore_Encode1(AL_EncCoreCtx *ctx, uint32_t *cmd_regs,
                       void *cmd_regs2, int combined)
{
    if (!ctx || !ctx->ip_ctrl) return -1;
    int core = (int)ctx->core_id;

    /* OEM: if combined, check IsEnc2AlreadyRunning first (we skip for now) */

    if (IsEnc1AlreadyRunning(ctx->ip_ctrl, core)) {
        LOG_AL("EncCore_Encode1: core %d already running!", core);
        return -1;
    }

    ctx->cmd_list = cmd_regs;

    if (!cmd_regs) {
        /* OEM: assert("CmdRegs1_p", ...) */
        LOG_AL("EncCore_Encode1: cmd_regs is NULL!");
        return -1;
    }

    /* Start Enc1 with push_val=2 */
    StartEnc1WithCommandList(ctx->ip_ctrl, core, cmd_regs, 2);

    /* If combined mode (Enc1+Enc2), also start with cmd_regs2 push_val=8 */
    if (combined && cmd_regs2) {
        StartEnc1WithCommandList(ctx->ip_ctrl, core, cmd_regs2, 8);
    }

    return 0;
}

void AL_EncCore_EnableInterrupts(AL_EncCoreCtx *ctx, int num_cores,
                                 int enc1, int enc2, int entropy)
{
    if (!ctx || !ctx->ip_ctrl) return;
    AL_IpCtrl *ip = ctx->ip_ctrl;
    int base_core = (int)ctx->core_id;
    int end_core = base_core + num_cores;

    /* OEM: for each core in range, manipulate the 0x8014 interrupt mask register.
     * enc1 enables bit (core*4), entropy enables bit (core*4+2).
     * The XOR-OR pattern: new = ((bit ^ old) & bit) ^ old  ≡  old | bit */
    if (enc1) {
        for (int c = base_core; c < end_core; c++) {
            int bit = 1 << ((c * 4) & 0x1F);
            int old = ip->vtable->read_reg(ip, AVPU_INTERRUPT_MASK);
            int new_val = ((bit ^ old) & bit) ^ old; /* = old | bit */
            ip->vtable->write_reg(ip, AVPU_INTERRUPT_MASK, (unsigned)new_val);
        }
    }

    if (entropy) {
        for (int c = base_core; c < end_core; c++) {
            int bit = 1 << (((c * 4) + 2) & 0x1F);
            int old = ip->vtable->read_reg(ip, AVPU_INTERRUPT_MASK);
            int new_val = ((bit ^ old) & bit) ^ old;
            ip->vtable->write_reg(ip, AVPU_INTERRUPT_MASK, (unsigned)new_val);
        }
    }

    /* enc2 interrupts at (core*4+1) - not commonly used for H.264 Baseline */
    if (enc2) {
        for (int c = base_core; c < end_core; c++) {
            int bit = 1 << (((c * 4) + 1) & 0x1F);
            int old = ip->vtable->read_reg(ip, AVPU_INTERRUPT_MASK);
            int new_val = ((bit ^ old) & bit) ^ old;
            ip->vtable->write_reg(ip, AVPU_INTERRUPT_MASK, (unsigned)new_val);
        }
    }
}

void AL_EncCore_TurnOnGC(AL_EncCoreCtx *ctx)
{
    if (!ctx || !ctx->ip_ctrl) return;
    /* OEM: TurnOnGC.constprop.36 → acquires mutex, SetClockCommand(ip, core, 1), releases */
    /* We don't have the module array mutex; just do the clock command */
    SetClockCommand(ctx->ip_ctrl, (int)ctx->core_id, 1);
    LOG_AL("TurnOnGC: core=%d", (int)ctx->core_id);
}

void AL_EncCore_TurnOffGC(AL_EncCoreCtx *ctx)
{
    if (!ctx || !ctx->ip_ctrl) return;
    /* OEM: TurnOffGC.constprop.35 → SetClockCommand(ip, core, 0) */
    SetClockCommand(ctx->ip_ctrl, (int)ctx->core_id, 0);
    LOG_AL("TurnOffGC: core=%d", (int)ctx->core_id);
}
