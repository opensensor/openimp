/*
 * isp_gap_test - T20/T21 ISP vendor calls added in the gap work of
 * 2026-10-10: SetDPStrength (T20, T21), SetAntiFogAttr (T21), Set/GetWDRAttr
 * and Set/GetISPHVflip (T20/T10).  src/isp/isp_tseries.c against a fake
 * tuning device (--wrap=ioctl); the expected controls and the caching are
 * those of the vendor libimp (T20 3.12.0, T21 1.0.33).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imp/imp_isp.h"
#include "isp/isp_tseries_dev.h"

ISPDevice *gISP;

/* stubs for the rest of libimp */
int IMP_Log_Get_Option(void) { return 0; }
void imp_log_fun(int level, int option, int type, ...) { (void)level; (void)option; (void)type; }
void *IMP_Alloc(void *info, int size, const char *owner) { (void)info; (void)size; (void)owner; return NULL; }
int DMA_FreePhys(uint32_t phys) { (void)phys; return 0; }
int openimp_video_drop_set(void (*cb)(void)) { (void)cb; return 0; }

#if defined(PLATFORM_T30)
#define WDR_CID 0x8000169      /* T30 1.0.5 */
#else
#define WDR_CID 0x98e912       /* T20/T10 3.12.0 */
#endif
#define TUNING_FD 77
#define G_CTRL 0xc008561bUL
#define S_CTRL 0xc008561cUL
#define TUNING 0xc00c56c6UL

typedef struct { int32_t id; int32_t value; } Ctrl;

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, \
    __LINE__, #c); failures++; } } while (0)

/* fake driver */
static int s_calls, g_calls, t_calls;
static int32_t last_id, last_val;
static int32_t ctrl_val[0x1000];          /* by id & 0xfff */
static int wdr_ret;                       /* errno for the WDR control */
static int32_t last_tuning_sub, last_tuning_val;

int __real_ioctl(int fd, unsigned long request, ...);
int __wrap_ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    void *arg;

    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);
    if (fd != TUNING_FD) {
        errno = EBADF;
        return -1;
    }
    if (request == S_CTRL || request == G_CTRL) {
        Ctrl *c = arg;

        if (request == S_CTRL) {
            s_calls++;
            last_id = c->id;
            last_val = c->value;
        } else {
            g_calls++;
        }
        if (c->id == WDR_CID && wdr_ret) {
            errno = wdr_ret;
            return -1;
        }
        if (request == S_CTRL)
            ctrl_val[c->id & 0xfff] = c->value;
        else
            c->value = ctrl_val[c->id & 0xfff];
        return 0;
    }
    if (request == TUNING) {
        int32_t *w = arg;               /* {dir, subcmd, value} */

        t_calls++;
        last_tuning_sub = w[1];
        last_tuning_val = w[2];
        return 0;
    }
    errno = EINVAL;
    return -1;
}

int main(void)
{
    static ISPDevice isp;
#if defined(PLATFORM_T20) || defined(PLATFORM_T30)
    IMPISPTuningOpsMode h, v;
#endif

    gISP = &isp;
    isp.tuning_fd = TUNING_FD;
    isp.tuning = &isp;
    isp.tuning_state = 2;

    /* SetDPStrength: capped at 200 %, 100 % = driver DPC ratio 128 */
    CHECK(IMP_ISP_Tuning_SetDPStrength(100) == 0);
    CHECK(t_calls == 1 && last_tuning_sub == 0x8000062 && last_tuning_val == 128);
    CHECK(IMP_ISP_Tuning_SetDPStrength(1000) == 0);
    CHECK(last_tuning_val == 255);                       /* 200 % */
    CHECK(IMP_ISP_Tuning_SetDPStrength(0) == 0 && last_tuning_val == 0);
    isp.tuning_state = 1;
    CHECK(IMP_ISP_Tuning_SetDPStrength(100) == -1);      /* tuning not running */
    isp.tuning_state = 2;

#if !defined(PLATFORM_T20)
    /* T21/T30 SetAntiFogAttr: the enum travels as control 0x8000163 */
    CHECK(IMP_ISP_Tuning_SetAntiFogAttr(2) == 0);
    CHECK(last_id == 0x8000163 && last_val == 2);
#endif
#if defined(PLATFORM_T20) || defined(PLATFORM_T30)
    /* ISPHVflip = the two flip controls, hflip first */
    s_calls = 0;
    CHECK(IMP_ISP_Tuning_SetISPHVflip(IMPISP_TUNING_OPS_MODE_ENABLE,
                                      IMPISP_TUNING_OPS_MODE_DISABLE) == 0);
    CHECK(s_calls == 2);
    CHECK(ctrl_val[0x914] == 1 && ctrl_val[0x915] == 0);
    CHECK(IMP_ISP_Tuning_SetISPHVflip(IMPISP_TUNING_OPS_MODE_DISABLE,
                                      IMPISP_TUNING_OPS_MODE_ENABLE) == 0);
    CHECK(ctrl_val[0x914] == 0 && ctrl_val[0x915] == 1);
    CHECK(IMP_ISP_Tuning_GetISPHVflip(&h, &v) == 0);
    CHECK(h == IMPISP_TUNING_OPS_MODE_DISABLE && v == IMPISP_TUNING_OPS_MODE_ENABLE);
    CHECK(IMP_ISP_Tuning_GetISPHVflip(NULL, &v) == -1);
    CHECK(IMP_ISP_Tuning_GetISPHVflip(&h, NULL) == -1);

    /* WDR: V4L2 control (T20 0x98e912, T30 0x8000169), unchanged mode
     * answers without the driver */
    s_calls = 0;
    CHECK(IMP_ISP_Tuning_SetWDRAttr(IMPISP_TUNING_OPS_MODE_DISABLE) == 0);
    CHECK(s_calls == 0);                                 /* cached 0 */
    CHECK(IMP_ISP_Tuning_SetWDRAttr(IMPISP_TUNING_OPS_MODE_ENABLE) == 0);
    CHECK(s_calls == 1 && last_id == WDR_CID && last_val == 1);
    CHECK(IMP_ISP_Tuning_SetWDRAttr(IMPISP_TUNING_OPS_MODE_ENABLE) == 0);
    CHECK(s_calls == 1);
    CHECK(IMP_ISP_Tuning_GetWDRAttr(&h) == 0 && h == IMPISP_TUNING_OPS_MODE_ENABLE);
    wdr_ret = EPERM;                                     /* no WDR buffer */
    CHECK(IMP_ISP_Tuning_SetWDRAttr(IMPISP_TUNING_OPS_MODE_DISABLE) == -1);
    h = IMPISP_TUNING_OPS_MODE_DISABLE;
    CHECK(IMP_ISP_Tuning_GetWDRAttr(&h) == -1);          /* error + cached mode */
    CHECK(h == IMPISP_TUNING_OPS_MODE_ENABLE);
    CHECK(IMP_ISP_Tuning_GetWDRAttr(NULL) == -1);
    isp.tuning_state = 1;
    CHECK(IMP_ISP_Tuning_SetWDRAttr(IMPISP_TUNING_OPS_MODE_DISABLE) == -1);
    CHECK(IMP_ISP_Tuning_GetWDRAttr(&h) == -1);
    isp.tuning_state = 2;
#endif
#if defined(PLATFORM_T30)
    {
        IMPISPWB wb = { .rgain = 0x123, .bgain = 0x2ab }, got;
        IMPISPAeStrategy st = IMPISP_AE_STRATEGY_BUTT;
        IMPISPTemperDenoiseAttr tn = { .type = IMPISP_TEMPER_MANUAL,
                                       .temper_strength = 77 }, tg;

        /* AE strategy: tuning control 0x8000022 (value) */
        t_calls = 0;
        CHECK(IMP_ISP_Tuning_SetAeStrategy((IMPISPAeStrategy)1) == 0);
        CHECK(last_tuning_sub == 0x8000022 && last_tuning_val == 1);
        CHECK(IMP_ISP_Tuning_GetAeStrategy(&st) == 0);
        CHECK(IMP_ISP_Tuning_GetAeStrategy(NULL) == -1);
        /* CWF shift: tuning control 0x8000001, rgain << 16 | bgain */
        CHECK(IMP_ISP_Tuning_Awb_SetCwfShift(&wb) == 0);
        CHECK(last_tuning_sub == 0x8000001 &&
              (uint32_t)last_tuning_val == ((0x123u << 16) | 0x2ab));
        CHECK(IMP_ISP_Tuning_Awb_SetCwfShift(NULL) == -1);
        CHECK(IMP_ISP_Tuning_Awb_GetCwfShift(NULL) == -1);
        memset(&got, 0, sizeof(got));
        CHECK(IMP_ISP_Tuning_Awb_GetCwfShift(&got) == 0);
        /* AF threshold: needs a tuning block, no driver call */
        t_calls = 0;
        CHECK(IMP_ISP_Tuning_SetAFThreshold(0x1234) == 0 && t_calls == 0);
        isp.tuning = NULL;
        CHECK(IMP_ISP_Tuning_SetAFThreshold(1) == -1);
        isp.tuning = &isp;
        /* temper control shares the cache of SetTemperDnsAttr */
        CHECK(IMP_ISP_Tuning_SetTemperDnsCtl(&tn) == 0);
        memset(&tg, 0, sizeof(tg));
        CHECK(IMP_ISP_Tuning_GetTemperDnsAttr(&tg) == 0 &&
              tg.type == IMPISP_TEMPER_MANUAL && tg.temper_strength == 77);
        CHECK(IMP_ISP_Tuning_SetTemperDnsCtl(NULL) == -1);
    }
#endif
    if (failures) {
        fprintf(stderr, "isp_gap_test: %d failure(s)\n", failures);
        return 1;
    }
    puts("isp_gap_test: PASS");
    return 0;
}
