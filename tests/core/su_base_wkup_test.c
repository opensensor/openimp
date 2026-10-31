/* Host test: SU_Base_SetWkupMode with a fake open/ioctl/close.  See tests/core/su_base_wkup.mk. */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int open_calls, ioctl_calls, close_calls, open_fail, ioctl_fail;
static char open_path[64];
static unsigned long ioctl_req;
static uint32_t ioctl_arg[3];

static int fake_open(const char *p, int flags)
{
    open_calls++;
    strncpy(open_path, p, sizeof(open_path) - 1);
    if (flags != O_RDWR || open_fail) { errno = ENOENT; return -1; }
    return 77;
}

static int fake_ioctl(int fd, unsigned long req, uint32_t *arg)
{
    ioctl_calls++;
    ioctl_req = req;
    if (fd != 77) return -1;
    memcpy(ioctl_arg, arg, sizeof(ioctl_arg));
    if (ioctl_fail) { errno = ENOTTY; return -1; }
    return 0;
}

static int fake_close(int fd) { close_calls++; return fd == 77 ? 0 : -1; }

#define OPENIMP_SU_OPEN fake_open
#define OPENIMP_SU_IOCTL fake_ioctl
#define OPENIMP_SU_CLOSE fake_close
#include "../../src/core/su_base_wkup.c"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

int main(void)
{
    CHECK(SU_Base_SetWkupMode(1) == 0);
    CHECK(open_calls == 1 && ioctl_calls == 1 && close_calls == 1);
    CHECK(!strcmp(open_path, "/dev/rtc0") && ioctl_req == 0x80045209UL);
    CHECK(ioctl_arg[0] == 0 && ioctl_arg[1] == 0x2c && ioctl_arg[2] == 0xd2ad2d28U);
    CHECK(SU_Base_SetWkupMode(2) == 0 && ioctl_arg[2] == 0xd2ad2d29U);
    open_calls = ioctl_calls = close_calls = 0;
    CHECK(SU_Base_SetWkupMode(0) == -1 && errno == EINVAL);
    CHECK(SU_Base_SetWkupMode(3) == -1 && SU_Base_SetWkupMode(-1) == -1);
    CHECK(open_calls == 0 && ioctl_calls == 0 && close_calls == 0);
    open_fail = 1;
    CHECK(SU_Base_SetWkupMode(1) == -1 && errno == ENOENT && ioctl_calls == 0);
    open_fail = 0; ioctl_fail = 1; close_calls = 0;
    CHECK(SU_Base_SetWkupMode(1) == -1 && errno == ENOTTY && close_calls == 1);
    if (failures) return 1;
    printf("su_base_wkup_test: ok\n");
    return 0;
}
