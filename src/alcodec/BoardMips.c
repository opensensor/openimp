#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>

#include "alcodec/al_rtos.h"
#include "alcodec/al_types.h"
#include "imp_log_int.h"
#include "../device_pool.h"

#define BOARD_CTRL_SIZE 0x100
#define BOARD_FD_OFFSET 0x04
#define BOARD_THREAD_OFFSET 0x08
#define BOARD_MUTEX_OFFSET 0x0c
#define BOARD_IRQ_BASE_OFFSET 0x10
#define BOARD_IRQ_SLOT_SIZE 0x0c
#define BOARD_MAX_IRQS 0x14

typedef void (*BoardInterruptCallback)(void *);

typedef struct LinuxIpControlVtable {
    int32_t (*destroy)(void *arg1);
    int32_t (*read_register)(void *arg1, int32_t arg2);
    int32_t (*write_register)(void *arg1, int32_t arg2, int32_t arg3);
    int32_t (*register_callback)(void *arg1, BoardInterruptCallback arg2, void *arg3, char arg4);
} LinuxIpControlVtable;

typedef struct LinuxIpCtrlRegIo {
    int32_t reg;
    int32_t value;
} LinuxIpCtrlRegIo;

int32_t LinuxIpCtrl_WriteRegister(void *arg1, int32_t arg2, int32_t arg3);
int32_t LinuxIpCtrl_ReadRegister(void *arg1, int32_t arg2);
void *WaitInterruptThread(void *arg1);
int32_t LinuxIpCtrl_RegisterCallBack(void *arg1, BoardInterruptCallback arg2, void *arg3, char arg4);
int32_t LinuxIpCtrl_Destroy(void *arg1);

static const LinuxIpControlVtable LinuxIpControlVtableInstance = {
    LinuxIpCtrl_Destroy,
    LinuxIpCtrl_ReadRegister,
    LinuxIpCtrl_WriteRegister,
    LinuxIpCtrl_RegisterCallBack,
};

static uint8_t *GetIrqSlot(void *arg1, uint32_t irq)
{
    return (uint8_t *)arg1 + BOARD_IRQ_BASE_OFFSET + irq * BOARD_IRQ_SLOT_SIZE;
}

int32_t ShowBoardInformation(void *arg1)
{
    const LinuxIpControlVtable *vtable;
    int32_t board_hw_id;

    if (arg1 == NULL) {
        fprintf(stderr, "---- %s(%d):ipCtrl is null ----\n", "ShowBoardInformation", 0xa);
        return 0x80;
    }

    vtable = *(const LinuxIpControlVtable * const *)arg1;
    board_hw_id = vtable->read_register(arg1, 0x8004);
    fwrite("---- FPGA board is ready ----\n", 1, 0x1e, stderr);
    fprintf(stderr, "  Board UID : %X\n", vtable->read_register(arg1, 0x8000));
    fprintf(stderr, "  Board HW ID : %X\n", board_hw_id);
    fprintf(stderr, "  Board rev.  : %X\n", vtable->read_register(arg1, 0x800c));
    fprintf(stderr, "  Board date  : %X\n", vtable->read_register(arg1, 0x8008));
    fwrite("-----------------------------\n", 1, 0x1e, stderr);

    if (board_hw_id != 0x72000460) {
        fprintf(stderr, "Error: Board ID mismatch: %X instead of %X\n", board_hw_id, 0x72000460);
    }

    return 0;
}

int32_t LinuxIpCtrl_WriteRegister(void *arg1, int32_t arg2, int32_t arg3)
{
    int32_t fd = *(int32_t *)((uint8_t *)arg1 + BOARD_FD_OFFSET);
    LinuxIpCtrlRegIo reg_io;
    int32_t result;

    reg_io.reg = arg2;
    reg_io.value = arg3;
    result = ioctl(fd, 0xc008710a, &reg_io);
    if (result < 0) {
        perror("IOCTL failed with ");
        return result;
    }

    switch ((uint32_t)arg2 & 0x1ffU) {
    case 0x010:
    case 0x014:
    case 0x1f0:
    case 0x1f4:
    case 0x1e0:
    case 0x1e4:
    case 0x1f8:
        IMP_LOG_INFO("AVPU", "wr fd=%d reg=0x%04x val=0x%08x", fd, (unsigned)arg2, (unsigned)arg3);
        break;
    default:
        break;
    }

    return result;
}

int32_t LinuxIpCtrl_ReadRegister(void *arg1, int32_t arg2)
{
    int32_t fd = *(int32_t *)((uint8_t *)arg1 + BOARD_FD_OFFSET);
    LinuxIpCtrlRegIo reg_io;

    reg_io.reg = arg2;
    reg_io.value = 0;
    if (ioctl(fd, 0xc008710b, &reg_io) >= 0) {
        switch ((uint32_t)arg2 & 0x1ffU) {
        case 0x010:
        case 0x014:
        case 0x1f4:
        case 0x1f8:
            IMP_LOG_INFO("AVPU", "rd fd=%d reg=0x%04x -> 0x%08x", fd, (unsigned)arg2, (unsigned)reg_io.value);
            break;
        default:
            break;
        }
        return reg_io.value;
    }

    perror("IOCTL failed with ");
    return 0;
}

void *WaitInterruptThread(void *arg1)
{
    IMP_LOG_INFO("AVPU", "wait-thread start fd=%d", *(int32_t *)((uint8_t *)arg1 + BOARD_FD_OFFSET));
    while (1) {
        int32_t fd = *(int32_t *)((uint8_t *)arg1 + BOARD_FD_OFFSET);
        int32_t irq = -1;
        uint8_t *slot;
        BoardInterruptCallback callback;

        if (ioctl(fd, 0xc004710c, &irq) == -1) {
            IMP_LOG_INFO("AVPU", "wait-thread ioctl WAIT_IRQ failed fd=%d errno=%d", fd, *__errno_location());
            break;
        }

        IMP_LOG_INFO("AVPU", "wait-thread irq=%d", irq);

        if ((uint32_t)irq >= BOARD_MAX_IRQS) {
            fprintf(stderr, "Got %d, No interrupt to handle\n", irq);
            return NULL;
        }

        Rtos_GetMutex(*(void **)((uint8_t *)arg1 + BOARD_MUTEX_OFFSET));
        slot = GetIrqSlot(arg1, (uint32_t)irq);
        callback = *(BoardInterruptCallback *)(void *)(slot + 0x00); /* +0x00 callback */
        if (callback != NULL) {
            callback(*(void **)(slot + 0x08)); /* +0x08 user_data */
        } else if (*(uint32_t *)(void *)(slot + 0x04) == 0) { /* +0x04 flag */
            fprintf(stderr, "Interrupt %d doesn't have an handler, signaling it was caught and returning\n", irq);
            callback = *(BoardInterruptCallback *)(void *)(slot + 0x00); /* +0x00 callback */
            if (callback != NULL) {
                callback(*(void **)(slot + 0x08)); /* +0x08 user_data */
            }
        }
        Rtos_ReleaseMutex(*(void **)((uint8_t *)arg1 + BOARD_MUTEX_OFFSET));
    }

    if (*__errno_location() != 4) {
        perror("IOCTL0 failed with ");
    }

    return NULL;
}

int32_t LinuxIpCtrl_RegisterCallBack(void *arg1, BoardInterruptCallback arg2, void *arg3, char arg4)
{
    uint32_t irq = (uint32_t)(uint8_t)arg4;
    uint8_t *slot;
    void *mutex;

    Rtos_GetMutex(*(void **)((uint8_t *)arg1 + BOARD_MUTEX_OFFSET));
    slot = GetIrqSlot(arg1, irq);
    *(BoardInterruptCallback *)(void *)(slot + 0x00) = arg2; /* +0x00 callback */
    if (arg2 != NULL) {
        *(uint32_t *)(void *)(slot + 0x04) = 0; /* +0x04 flag */
        mutex = *(void **)((uint8_t *)arg1 + BOARD_MUTEX_OFFSET);
        *(void **)(slot + 0x08) = arg3; /* +0x08 user_data */
        IMP_LOG_INFO("AVPU", "register-callback irq=%u cb=%p user=%p flag=0", irq, arg2, arg3);
        return (int32_t)Rtos_ReleaseMutex(mutex);
    }

    mutex = *(void **)((uint8_t *)arg1 + BOARD_MUTEX_OFFSET);
    *(uint32_t *)(void *)(slot + 0x04) = 1; /* +0x04 flag */
    *(void **)(slot + 0x08) = arg3; /* +0x08 user_data */
    IMP_LOG_INFO("AVPU", "register-callback irq=%u cb=%p user=%p flag=1", irq, arg2, arg3);
    return (int32_t)Rtos_ReleaseMutex(mutex);
}

int32_t LinuxIpCtrl_Destroy(void *arg1)
{
    ioctl(*(int32_t *)((uint8_t *)arg1 + BOARD_FD_OFFSET), 0x20007101, 0);
    Rtos_JoinThread(*(int32_t **)((uint8_t *)arg1 + BOARD_THREAD_OFFSET));
    Rtos_DeleteThread(*(void **)((uint8_t *)arg1 + BOARD_THREAD_OFFSET));
    Rtos_DeleteMutex(*(void **)((uint8_t *)arg1 + BOARD_MUTEX_OFFSET));
    AL_DevicePool_Close(*(int32_t *)((uint8_t *)arg1 + BOARD_FD_OFFSET));
    free(arg1);
    return 0;
}

AL_THardwareDriver *AL_Board_Create(const char *arg1)
{
    uint8_t *result = calloc(1, BOARD_CTRL_SIZE);
    int32_t fd;
    void *mutex;
    void *thread;

    if (result == NULL) {
        return NULL;
    }

    *(const LinuxIpControlVtable **)(void *)result = &LinuxIpControlVtableInstance;
    fd = AL_DevicePool_Open(arg1);
    *(int32_t *)(void *)(result + BOARD_FD_OFFSET) = fd;
    if (fd >= 0) {
        mutex = Rtos_CreateMutex();
        *(void **)(void *)(result + BOARD_MUTEX_OFFSET) = mutex;
        if (mutex != NULL) {
            thread = Rtos_CreateThread((void *)WaitInterruptThread, result);
            *(void **)(void *)(result + BOARD_THREAD_OFFSET) = thread;
            if (thread != NULL) {
                return (AL_THardwareDriver *)result;
            }

            Rtos_DeleteMutex(*(void **)(void *)(result + BOARD_MUTEX_OFFSET));
        }

        AL_DevicePool_Close(*(int32_t *)(void *)(result + BOARD_FD_OFFSET));
    }

    free(result);
    return NULL;
}

int32_t AL_Board_Destroy(void *arg1)
{
    int32_t result = LinuxIpCtrl_ReadRegister(arg1, 0x8004);

    if (result == 0x72000460) {
        return LinuxIpCtrl_Destroy(arg1);
    }

    return result;
}
