/*
 * T31 IMP_ISP_Tuning_GetAwbZone (vendor 1.1.x ABI): src/isp/isp_tseries.c
 * with PLATFORM_T31 against a fake tuning ioctl.
 *  - IMPISPAWBZone is the vendor block: 675 bytes, zone_r at 0, zone_g at
 *    225, zone_b at 450, one byte per zone;
 *  - the call sends one request {1, 0x800000b, awb_zone} with the caller's
 *    block (no copy, nothing beyond the block is written);
 *  - -1 without tuning, with NULL, and when the driver has no fresh
 *    statistics (-EAGAIN); the caller's block is then left untouched;
 *  - the capability marker OpenIMP_Cap_AwbZone is 1.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imp/imp_isp.h"
#include "isp/isp_tseries_dev.h"

extern const int OpenIMP_Cap_AwbZone;

ISPDevice *gISP;

int IMP_Log_Get_Option(void) { return 0; }
void imp_log_fun(int level, int option, int type, ...) { (void)level; (void)option; (void)type; }
void *IMP_Alloc(void *info, int size, const char *owner) { (void)info; (void)size; (void)owner; return NULL; }
int DMA_FreePhys(uint32_t phys) { (void)phys; return 0; }
int openimp_video_drop_set(void (*cb)(void)) { (void)cb; return 0; }
int IMP_Encoder_RequestIDR(int chn) { (void)chn; return 0; }

#define TUNING_FD 77
#define TUNING_IOCTL 0xc00c56c6UL

typedef struct { int32_t cmd, subcmd; void *ptr; } PtrReq;

static int failures;
#define CHECK(c) do { if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static int calls, last_cmd;
static uint32_t last_subcmd;
static void *last_ptr;
static int driver_fresh;            /* 0: the driver answers -EAGAIN */
static uint8_t driver_block[675];   /* what the driver copies out */

int __wrap_ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    PtrReq *req;

    va_start(ap, request);
    req = va_arg(ap, PtrReq *);
    va_end(ap);
    if (fd != TUNING_FD || request != TUNING_IOCTL) {
        errno = EINVAL;
        return -1;
    }
    calls++;
    last_cmd = req->cmd;
    last_subcmd = (uint32_t)req->subcmd;
    last_ptr = req->ptr;
    if (!driver_fresh) {
        errno = EAGAIN;
        return -1;
    }
    memcpy(req->ptr, driver_block, sizeof(driver_block));
    return 0;
}

int main(void)
{
    static ISPDevice isp;
    struct { IMPISPAWBZone zone; uint8_t guard[16]; } buf;
    unsigned int i;

    /* ABI: the vendor 1.1.x block */
    CHECK(sizeof(IMPISPAWBZone) == 675);
    CHECK(offsetof(IMPISPAWBZone, zone_r) == 0);
    CHECK(offsetof(IMPISPAWBZone, zone_g) == 225);
    CHECK(offsetof(IMPISPAWBZone, zone_b) == 450);
    CHECK(sizeof(buf.zone.zone_r[0]) == 1);
    CHECK(OpenIMP_Cap_AwbZone == 1);

    /* no tuning: -1 without an ioctl */
    gISP = NULL;
    CHECK(IMP_ISP_Tuning_GetAwbZone(&buf.zone) == -1 && calls == 0);
    memset(&isp, 0, sizeof(isp));
    isp.tuning = &isp;
    isp.tuning_fd = TUNING_FD;
    isp.tuning_state = 1;
    gISP = &isp;
    CHECK(IMP_ISP_Tuning_GetAwbZone(&buf.zone) == -1 && calls == 0);
    isp.tuning_state = 2;

    /* NULL: -1 without an ioctl */
    CHECK(IMP_ISP_Tuning_GetAwbZone(NULL) == -1 && calls == 0);

    /* no fresh statistics: -1, the caller's block is untouched */
    memset(&buf, 0x5a, sizeof(buf));
    driver_fresh = 0;
    CHECK(IMP_ISP_Tuning_GetAwbZone(&buf.zone) == -1);
    CHECK(calls == 1 && last_cmd == 1 && last_subcmd == 0x800000bu &&
          last_ptr == &buf.zone);
    for (i = 0; i < sizeof(buf); i++)
        CHECK(((uint8_t *)&buf)[i] == 0x5a);

    /* fresh statistics: 0, the driver's 675 bytes land in the caller's
     * block in the vendor order, nothing beyond it */
    for (i = 0; i < 225; i++) {
        driver_block[i] = (uint8_t)(100 + i % 100);        /* r */
        driver_block[225 + i] = (uint8_t)(i % 256);        /* g */
        driver_block[450 + i] = (uint8_t)(255 - i % 256);  /* b */
    }
    driver_fresh = 1;
    CHECK(IMP_ISP_Tuning_GetAwbZone(&buf.zone) == 0);
    CHECK(calls == 2 && last_cmd == 1 && last_subcmd == 0x800000bu &&
          last_ptr == &buf.zone);
    for (i = 0; i < 225; i++) {
        CHECK(buf.zone.zone_r[i] == (uint8_t)(100 + i % 100));
        CHECK(buf.zone.zone_g[i] == (uint8_t)(i % 256));
        CHECK(buf.zone.zone_b[i] == (uint8_t)(255 - i % 256));
    }
    for (i = 0; i < sizeof(buf.guard); i++)
        CHECK(buf.guard[i] == 0x5a);

    if (failures)
        fprintf(stderr, "%d failure(s)\n", failures);
    else
        puts("isp_t31_awbzone_test: ok");
    return failures ? 1 : 0;
}
