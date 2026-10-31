/*
 * su_base_wkup.c - SU_Base_SetWkupMode (T41)
 *
 * The libsysutils of the T41 boards (the open neo-sysutils) writes the mode
 * number into /sys/power/state, which the kernel rejects: the call never did
 * anything.  The vendor libsysutils (T41 SDK 1.2.5) opens the RTC and sends
 * one request to it:
 *
 *   fd = open("/dev/rtc0", O_RDWR);
 *   ioctl(fd, 0x80045209, { 0, 0x2c, mode == 1 ? 0xd2ad2d28 : 0xd2ad2d29 });
 *   close(fd);
 *
 * mode 1 and 2 are the only valid modes (anything else: -1).  OpenIMP keeps
 * the vendor request and exports it from libimp.so, as it does for
 * SU_Base_Shutdown, so it wins over the board's libsysutils.  Returns 0 when
 * the RTC accepted the request, -1 (errno set) otherwise: no /dev/rtc0, a
 * mode other than 1 or 2, or an RTC driver that does not know the request.
 * Difference: the vendor leaves the descriptor open for a bad mode, here it
 * is not opened at all.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* Host tests replace these with fakes (-DOPENIMP_SU_OPEN=... etc.). */
#ifndef OPENIMP_SU_OPEN
#define OPENIMP_SU_OPEN open
#endif
#ifndef OPENIMP_SU_IOCTL
#define OPENIMP_SU_IOCTL ioctl
#endif
#ifndef OPENIMP_SU_CLOSE
#define OPENIMP_SU_CLOSE close
#endif

#define OPENIMP_SU_WKUP_REQUEST 0x80045209UL

int SU_Base_SetWkupMode(int mode)
{
    uint32_t req[3];
    int fd, ret, saved;

    if (mode != 1 && mode != 2) {
        errno = EINVAL;
        return -1;
    }
    fd = OPENIMP_SU_OPEN("/dev/rtc0", O_RDWR);
    if (fd < 0)
        return -1;
    req[0] = 0;
    req[1] = 0x2cU;
    req[2] = mode == 1 ? 0xd2ad2d28U : 0xd2ad2d29U;
    ret = OPENIMP_SU_IOCTL(fd, OPENIMP_SU_WKUP_REQUEST, req);
    saved = errno;
    OPENIMP_SU_CLOSE(fd);
    if (ret < 0) {
        errno = saved;
        return -1;
    }
    return 0;
}
