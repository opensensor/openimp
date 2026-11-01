/*
 * Host test of IMP_ISP_Tuning_SetISPBypass on T10/T20: like the OEM 3.9.0 /
 * 3.12.0 library (0x5a334) one VIDIOC_S_CTRL, id 0x98e907, value = the mode,
 * on the tuning fd; 0 when it succeeds, -1 when it fails or there is no
 * tuning session.  ENABLE and DISABLE both reach the driver, no link ioctl.
 *
 * Runs src/isp/isp_tseries.c (32-bit ABI asserts off, see the Makefile)
 * against a fake ioctl().
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core/globals.h"
#include "imp/imp_isp.h"
#include "isp/isp_tseries_dev.h"

int __real_ioctl(int fd, unsigned long req, ...);
static int ioctl_calls, ioctl_fd = -1, fail_next;
static unsigned long last_req;
static int32_t last_id, last_val;

int __wrap_ioctl(int fd, unsigned long req, ...)
{
    int32_t *p;
    __builtin_va_list ap;

    __builtin_va_start(ap, req);
    p = __builtin_va_arg(ap, int32_t *);
    __builtin_va_end(ap);
    ioctl_calls++;
    ioctl_fd = fd;
    last_req = req;
    last_id = p[0];
    last_val = p[1];
    if (fail_next) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

ISPDevice *gISP;

static int failures;
#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); failures++; } } while (0)

static ISPDevice dev;
static uint8_t tuning_obj[16];

int main(void)
{
    gISP = NULL;
    CHECK(IMP_ISP_Tuning_SetISPBypass(IMPISP_TUNING_OPS_MODE_ENABLE) == -1, "no ISP device");

    memset(&dev, 0, sizeof(dev));
    gISP = &dev;
    CHECK(IMP_ISP_Tuning_SetISPBypass(IMPISP_TUNING_OPS_MODE_DISABLE) == -1, "no tuning object");
    dev.tuning = tuning_obj;
    dev.tuning_fd = 55;
    dev.tuning_state = 1;
    CHECK(IMP_ISP_Tuning_SetISPBypass(IMPISP_TUNING_OPS_MODE_ENABLE) == -1, "tuning not enabled");
    CHECK(ioctl_calls == 0, "failed checks never reach the driver");

    dev.tuning_state = 2;
    CHECK(IMP_ISP_Tuning_SetISPBypass(IMPISP_TUNING_OPS_MODE_ENABLE) == 0, "ENABLE ok");
    CHECK(ioctl_calls == 1 && ioctl_fd == 55, "one ioctl on the tuning fd");
    CHECK(last_req == 0xc008561cUL && last_id == 0x98e907 && last_val == 1,
          "S_CTRL 0x98e907 value 1");
    CHECK(IMP_ISP_Tuning_SetISPBypass(IMPISP_TUNING_OPS_MODE_DISABLE) == 0, "DISABLE ok");
    CHECK(ioctl_calls == 2 && last_req == 0xc008561cUL && last_id == 0x98e907 && last_val == 0,
          "S_CTRL 0x98e907 value 0");

    fail_next = 1;
    CHECK(IMP_ISP_Tuning_SetISPBypass(IMPISP_TUNING_OPS_MODE_DISABLE) == -1, "ioctl failure -> -1");

    if (failures == 0)
        printf("t20 bypass: all checks passed\n");
    return failures ? 1 : 0;
}
