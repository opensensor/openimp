/*
 * T20/T21 (and T10, which runs the T20 libimp) ISP tuning calls that
 * reach the driver: WaitFrame (0x8000162), the T20 AE/AWB zone ABI and
 * the T20 RawDRC / Sinter / Temper attributes (vendor 3.12.0 sequence).
 * src/isp/isp_tseries.c against a fake tuning ioctl (--wrap=ioctl).
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

int IMP_ISP_Tuning_GetAeZone(void *zone);

ISPDevice *gISP;

/* stubs for the rest of libimp */
int IMP_Log_Get_Option(void) { return 0; }
void imp_log_fun(int level, int option, int type, ...) { (void)level; (void)option; (void)type; }
void *IMP_Alloc(void *info, int size, const char *owner) { (void)info; (void)size; (void)owner; return NULL; }
int DMA_FreePhys(uint32_t phys) { (void)phys; return 0; }

#define TUNING_FD 77
#define TUNING_IOCTL 0xc00c56c6UL
#define G_CTRL 0xc008561bUL
#define S_CTRL 0xc008561cUL

typedef struct {
    int32_t id;
    int32_t value;
} Ctrl;

/* fake driver state: the T20 system table block, the custom controls and
 * the last tuning set */
static uint8_t stab[112];
static uint8_t stab_sent[112];
static int stab_sets;
static int32_t ctrl_temper = -1, ctrl_drc = -1;
static int s_ctrls;
static int32_t set_subcmd;
static int32_t val_set[256];   /* value requests by subcmd & 0xff */
static uint8_t set_bytes[16];

typedef struct {
    int32_t cmd;
    int32_t subcmd;
    void *ptr;
} PtrReq;

static int failures;
static int calls;
static int32_t last_cmd, last_subcmd;
static int driver_ret;
static uint64_t driver_cnt;
static uint32_t seen_timeout;

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, \
    __LINE__, #c); failures++; } } while (0)

/* the video-drop monitor is covered by tests/t31 (video_drop_test) */
int openimp_video_drop_set(void (*cb)(void)) { (void)cb; return 0; }

int __real_ioctl(int fd, unsigned long request, ...);
int __wrap_ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    PtrReq *req;

    va_start(ap, request);
    req = va_arg(ap, PtrReq *);
    va_end(ap);
    if (fd == TUNING_FD && (request == G_CTRL || request == S_CTRL)) {
        Ctrl *c = (Ctrl *)req;
        int32_t *slot = c->id == 0x98e90c ? &ctrl_temper :
                        c->id == 0x98e910 ? &ctrl_drc : NULL;

        if (!slot) {
            errno = EPERM;
            return -1;
        }
        if (request == S_CTRL) {
            s_ctrls++;
            if (c->value > 5 + (slot == &ctrl_drc)) {  /* v4l2 range */
                errno = ERANGE;
                return -1;
            }
            *slot = c->value;
        } else {
            c->value = *slot;
        }
        return 0;
    }
    if (fd == TUNING_FD && request == TUNING_IOCTL &&
        (req->subcmd == 0x8000022 || req->subcmd == 0x8000001 ||
         req->subcmd == 0x8000082)) {
        /* value requests {cmd, subcmd, int32 value} */
        int32_t *w = (int32_t *)req;

        calls++;
        last_cmd = w[0];
        last_subcmd = w[1];
        if (w[0] == 0)
            val_set[w[1] & 0xff] = w[2];
        else
            w[2] = val_set[w[1] & 0xff];
        return 0;
    }
    if (fd != TUNING_FD || request != TUNING_IOCTL) {
        errno = EINVAL;
        return -1;
    }
    calls++;
    last_cmd = req->cmd;
    last_subcmd = req->subcmd;
    if (req->subcmd == 0x8000162) {
        /* driver struct isp_frame_done_info: u32 timeout, (pad), u64 cnt,
         * int reserved = 0x18 bytes; the count is written back also when
         * the wait timed out */
        uint32_t *w = req->ptr;

        seen_timeout = w[0];
        w[2] = (uint32_t)driver_cnt;
        w[3] = (uint32_t)(driver_cnt >> 32);
    }
    if (driver_ret) {
        errno = -driver_ret;
        return -1;
    }
    if (req->cmd == 0) {
        set_subcmd = req->subcmd;
        memcpy(set_bytes, req->ptr, 1);
    }
    if (req->subcmd == 0x800002c) {
        if (req->cmd == 0) {
            memcpy(stab_sent, req->ptr, sizeof(stab_sent));
            stab_sets++;
        } else {
            memcpy(req->ptr, stab, sizeof(stab));
        }
    }
    return 0;
}

static void test_wait_frame(void)
{
    IMPISPWaitFrameAttr attr;

    /* tuning not running: -1, nothing sent, count untouched */
    gISP->tuning_state = 1;
    memset(&attr, 0, sizeof(attr));
    attr.timeout = 100;
    attr.cnt = 5;
    calls = 0;
    CHECK(IMP_ISP_Tuning_WaitFrame(&attr) == -1);
    CHECK(calls == 0 && attr.cnt == 5);
    gISP->tuning_state = 2;
    CHECK(IMP_ISP_Tuning_WaitFrame(NULL) == -1);

    /* a frame end: the 64-bit count comes back */
    driver_ret = 0;
    driver_cnt = 0x100000002ULL;
    attr.timeout = 1000;
    attr.cnt = 0;
    calls = 0;
    CHECK(IMP_ISP_Tuning_WaitFrame(&attr) == 0);
    CHECK(calls == 1 && last_cmd == 1 && last_subcmd == 0x8000162);
    CHECK(seen_timeout == 1000);
    CHECK(attr.cnt == 0x100000002ULL);

    /* timeout: the error, and the count as the driver reported it */
    driver_ret = -ETIMEDOUT;
    driver_cnt = 7;
    CHECK(IMP_ISP_Tuning_WaitFrame(&attr) != 0);
    CHECK(attr.cnt == 7);
    driver_ret = 0;
}

static void test_ae_strategy(void)
{
    IMPISPAeStrategy st = IMPISP_AE_STRATEGY_SPLIT_BALANCED;

    CHECK(IMP_ISP_Tuning_SetAeStrategy(
              IMPISP_AE_STRATEGY_SPLIT_INTEGRATION_PRIORITY) == 0);
    CHECK(last_cmd == 0 && last_subcmd == 0x8000022 && val_set[0x22] == 1);
    CHECK(IMP_ISP_Tuning_GetAeStrategy(&st) == 0 &&
          st == IMPISP_AE_STRATEGY_SPLIT_INTEGRATION_PRIORITY);
    CHECK(IMP_ISP_Tuning_GetAeStrategy(NULL) == -1);
}

static void test_zones(void)
{
    uint16_t ae[225];
    uint8_t awb[1800];

    calls = 0;
    CHECK(IMP_ISP_Tuning_GetAeZone(ae) == 0);
#if defined(PLATFORM_T20)
    CHECK(calls == 1 && last_subcmd == 0x800002f);
    /* one IMPISPAWBZone pointer (15x15 {u16, u16, u32} = 1800 bytes) */
    CHECK(sizeof(IMPISPAWBZone) == 1800);
    CHECK(IMP_ISP_Tuning_GetAwbZone((IMPISPAWBZone *)awb) == 0);
    CHECK(calls == 2 && last_subcmd == 0x8000009);
#else
    CHECK(calls == 1 && last_subcmd == 0x8000030);
    (void)awb;
#endif
}

#if !defined(PLATFORM_T20)
/* T21 1.0.33: Set/GetModuleControl send the key through 0x80000e2 */
static void test_t21_module_control(void)
{
    IMPISPModuleCtl ctl;

    ctl.key = 0x1004;
    set_subcmd = 0;
    CHECK(IMP_ISP_Tuning_SetModuleControl(&ctl) == 0);
    CHECK(set_subcmd == 0x80000e2 && set_bytes[0] == 0x04);
    calls = 0;
    CHECK(IMP_ISP_Tuning_GetModuleControl(&ctl) == 0);
    CHECK(calls == 1 && last_cmd == 1 && last_subcmd == 0x80000e2);
    driver_ret = -EPERM;
    CHECK(IMP_ISP_Tuning_SetModuleControl(&ctl) == 0); /* driver refuses: cache fallback */
    CHECK(IMP_ISP_Tuning_GetModuleControl(&ctl) != 0);
    driver_ret = 0;
}
#endif

#if defined(PLATFORM_T20)
static int nonzero_except(const uint8_t *b, const int *idx, int n)
{
    int i, k, bad = 0;

    for (i = 0; i < 112; i++) {
        int listed = 0;

        for (k = 0; k < n; k++)
            listed |= idx[k] == i;
        if (!listed && b[i])
            bad++;
    }
    return bad;
}

static void test_t20_denoise(void)
{
    uint8_t raw[16];
    IMPISPSinterDenoiseAttr *sinter = (IMPISPSinterDenoiseAttr *)raw;
    IMPISPTemperDenoiseAttr *temper = (IMPISPTemperDenoiseAttr *)raw;
    IMPISPDrcAttr *drc = (IMPISPDrcAttr *)raw;

    /* Sinter MANUAL: manual_sinter + target with their ctrl flags */
    memset(raw, 0, sizeof(raw));
    sinter->enable = IMPISP_TUNING_OPS_MODE_ENABLE;
    sinter->type = IMPISP_TUNING_OPS_TYPE_MANUAL;
    sinter->sinter_strength = 77;
    stab_sets = 0;
    CHECK(IMP_ISP_Tuning_SetSinterDnsAttr(sinter) == 0 && stab_sets == 1);
    CHECK(stab_sent[70] == 1 && stab_sent[10] == 1 && stab_sent[98] == 1 &&
          stab_sent[43] == 77);
    {
        static const int idx[] = { 70, 10, 98, 43 };
        CHECK(nonzero_except(stab_sent, idx, 4) == 0);
    }
    /* AUTO: only manual_sinter := 0 */
    sinter->type = IMPISP_TUNING_OPS_TYPE_AUTO;
    CHECK(IMP_ISP_Tuning_SetSinterDnsAttr(sinter) == 0);
    CHECK(stab_sent[70] == 1 && stab_sent[10] == 0);
    {
        static const int idx[] = { 70 };
        CHECK(nonzero_except(stab_sent, idx, 1) == 0);
    }
    /* RANGE (T20 type 2): sval_max/min into the sinter max/min items */
    sinter->type = (IMPISPTuningOpsType)2;
    raw[9] = 200;
    raw[10] = 20;
    CHECK(IMP_ISP_Tuning_SetSinterDnsAttr(sinter) == 0);
    CHECK(stab_sent[70] == 1 && stab_sent[99] == 1 && stab_sent[44] == 200 &&
          stab_sent[100] == 1 && stab_sent[45] == 20);
    raw[9] = 10;
    stab_sets = 0;
    CHECK(IMP_ISP_Tuning_SetSinterDnsAttr(sinter) == -1 && stab_sets == 0);

    /* Get reads the table */
    memset(stab, 0, sizeof(stab));
    stab[10] = 1;
    stab[43] = 90;
    stab[44] = 180;
    stab[45] = 30;
    memset(raw, 0xee, sizeof(raw));
    CHECK(IMP_ISP_Tuning_GetSinterDnsAttr(sinter) == 0);
    CHECK(sinter->enable == IMPISP_TUNING_OPS_MODE_ENABLE &&
          sinter->type == IMPISP_TUNING_OPS_TYPE_MANUAL &&
          sinter->sinter_strength == 90 && raw[9] == 180 && raw[10] == 30);
    CHECK(raw[12] == 0xee);     /* nothing past the 12-byte attribute */

    /* Temper MANUAL: type control, then the strength byte to 0x8000083 */
    memset(raw, 0, sizeof(raw));
    temper->type = IMPISP_TEMPER_MANUAL;
    temper->temper_strength = 66;
    s_ctrls = 0;
    CHECK(IMP_ISP_Tuning_SetTemperDnsAttr(temper) == 0);
    CHECK(s_ctrls == 1 && ctrl_temper == 2);
    CHECK(set_subcmd == 0x8000083 && set_bytes[0] == 66);
    /* same type again: no control write */
    CHECK(IMP_ISP_Tuning_SetTemperDnsAttr(temper) == 0 && s_ctrls == 1);
    /* RANGE (3): the v4l2 control refuses it, the table takes max/min */
    temper->type = (IMPISPTemperMode)3;
    raw[5] = 150;
    raw[6] = 50;
    stab_sets = 0;
    CHECK(IMP_ISP_Tuning_SetTemperDnsAttr(temper) == 0 && stab_sets == 1);
    CHECK(stab_sent[71] == 1 && stab_sent[11] == 0 && stab_sent[102] == 1 &&
          stab_sent[47] == 150 && stab_sent[103] == 1 && stab_sent[48] == 50);
    /* night mode: the vendor leaves temper alone */
    {
        IMPISPRunningMode night = IMPISP_RUNNING_MODE_NIGHT;
        IMPISPRunningMode day = IMPISP_RUNNING_MODE_DAY;

        (void)IMP_ISP_Tuning_SetISPRunningMode(night);
        temper->type = IMPISP_TEMPER_AUTO;
        s_ctrls = 0;
        stab_sets = 0;
        CHECK(IMP_ISP_Tuning_SetTemperDnsAttr(temper) == 0);
        CHECK(s_ctrls == 0 && stab_sets == 0);
        (void)IMP_ISP_Tuning_SetISPRunningMode(day);
    }
    memset(stab, 0, sizeof(stab));
    stab[11] = 1;
    stab[46] = 99;
    stab[47] = 120;
    stab[48] = 10;
    CHECK(IMP_ISP_Tuning_GetTemperDnsAttr(temper) == 0);
    CHECK(temper->type == IMPISP_TEMPER_MANUAL &&
          temper->temper_strength == 99 && raw[5] == 120 && raw[6] == 10);

    /* RawDRC MANUAL: mode control, then the attribute to 0x80000a0 */
    memset(raw, 0, sizeof(raw));
    drc->mode = IMPISP_DRC_HIGH;
    s_ctrls = 0;
    CHECK(IMP_ISP_Tuning_SetRawDRC(drc) == 0 && ctrl_drc == 2);
    drc->mode = IMPISP_DRC_MANUAL;
    drc->drc_strength = 180;
    set_subcmd = 0;
    CHECK(IMP_ISP_Tuning_SetRawDRC(drc) == 0 && ctrl_drc == 0);
    CHECK(set_subcmd == 0x80000a0);
    /* unchanged strength: nothing sent */
    set_subcmd = 0;
    CHECK(IMP_ISP_Tuning_SetRawDRC(drc) == 0 && set_subcmd == 0);
    /* RANGE (6): Iridix max/min in the table */
    drc->mode = (IMPISPDrcMode)6;
    raw[5] = 220;
    raw[6] = 40;
    stab_sets = 0;
    CHECK(IMP_ISP_Tuning_SetRawDRC(drc) == 0 && stab_sets == 1);
    CHECK(stab_sent[69] == 1 && stab_sent[9] == 0 && stab_sent[96] == 1 &&
          stab_sent[41] == 220 && stab_sent[97] == 1 && stab_sent[42] == 40);
    raw[5] = 30;
    CHECK(IMP_ISP_Tuning_SetRawDRC(drc) == -1);
    memset(stab, 0, sizeof(stab));
    stab[40] = 140;
    stab[41] = 250;
    stab[42] = 5;
    CHECK(IMP_ISP_Tuning_GetRawDRC(drc) == 0);
    CHECK(drc->mode == IMPISP_DRC_MEDIUM && drc->drc_strength == 140 &&
          raw[5] == 250 && raw[6] == 5);
    stab[9] = 1;
    CHECK(IMP_ISP_Tuning_GetRawDRC(drc) == 0 && drc->mode == IMPISP_DRC_MANUAL);

    /* TemperDnsCtl: the strength as a value through 0x8000082, only
     * when it changed */
    memset(raw, 0, sizeof(raw));
    temper->type = IMPISP_TEMPER_MANUAL;
    temper->temper_strength = 140;
    calls = 0;
    CHECK(IMP_ISP_Tuning_SetTemperDnsCtl(temper) == 0);
    CHECK(val_set[0x82] == 140 && calls == 1);
    CHECK(IMP_ISP_Tuning_SetTemperDnsCtl(temper) == 0 && calls == 1);

    /* CWF shift: rgain << 16 | bgain through 0x8000001 */
    {
        IMPISPWB wb;

        memset(&wb, 0, sizeof(wb));
        wb.rgain = 0x123;
        wb.bgain = 0x456;
        CHECK(IMP_ISP_Tuning_Awb_SetCwfShift(&wb) == 0);
        CHECK(val_set[0x01] == 0x1230456);
        memset(&wb, 0, sizeof(wb));
        CHECK(IMP_ISP_Tuning_Awb_GetCwfShift(&wb) == 0 &&
              wb.rgain == 0x123 && wb.bgain == 0x456);
        CHECK(IMP_ISP_Tuning_Awb_SetCwfShift(NULL) == -1);
    }
}
#endif

int main(void)
{
    static ISPDevice isp;

    gISP = &isp;
    isp.tuning_fd = TUNING_FD;
    isp.tuning = &isp;
    isp.tuning_state = 2;
    test_wait_frame();
    test_zones();
    test_ae_strategy();
#if defined(PLATFORM_T20)
    test_t20_denoise();
#else
    test_t21_module_control();
#endif
    if (failures) {
        fprintf(stderr, "isp_t1x_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("isp_t1x_test: ok\n");
    return 0;
}
