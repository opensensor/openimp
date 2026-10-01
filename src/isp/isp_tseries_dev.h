/*
 * Private libimp ISP device object of the T-series implementation
 * (isp_tseries.c).  Shared with the T23 tuning API (isp_t23_tuning.c),
 * which addresses the same object the stock library keeps in gISP.
 */
#ifndef OPENIMP_ISP_TSERIES_DEV_H
#define OPENIMP_ISP_TSERIES_DEV_H

#include <stddef.h>
#include <stdint.h>

#include "core/globals.h"

typedef struct ISPDevice {
    char dev_name[0x20];
    int32_t fd;
    uint32_t opened;
#if defined(PLATFORM_T23)
    uint8_t sensor_info[0x54];
#else
    uint8_t unk_28[0x50];
#endif
    char tuning_path[0x20];
    int32_t tuning_fd;
    void *tuning;
    int32_t mem_fd;
    void *isp_base;
    int32_t tuning_state;
#if defined(PLATFORM_T23)
    void *sensor_alloc[2];
    int32_t wdr_mode;
    void *wdr_alloc;
#else
    uint8_t unk_ac[8];
    int32_t wdr_mode;
#endif
} ISPDevice;

#if defined(PLATFORM_T23)
_Static_assert(offsetof(ISPDevice, tuning_path) == 0x7c,
               "T23 ISP tuning path ABI mismatch");
_Static_assert(offsetof(ISPDevice, tuning_fd) == 0x9c,
               "T23 ISP tuning fd ABI mismatch");
_Static_assert(offsetof(ISPDevice, tuning) == 0xa0,
               "T23 ISP tuning object ABI mismatch");
_Static_assert(offsetof(ISPDevice, tuning_state) == 0xac,
               "T23 ISP tuning state ABI mismatch");
_Static_assert(offsetof(ISPDevice, sensor_alloc) == 0xb0,
               "T23 ISP sensor allocation ABI mismatch");
#endif

#endif /* OPENIMP_ISP_TSERIES_DEV_H */
