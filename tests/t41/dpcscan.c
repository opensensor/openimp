/* dpcscan: T41 DPC effectiveness. Counts isolated hot pixels in full-size Y frames per DPC setting.
 * usage: dpcscan sensor addr W H it_us again_q10 nframes cfg...   cfg = default | bypass | rN (ratio N) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <imp/imp_common.h>
#include <imp/imp_system.h>
#include <imp/imp_isp.h>
#include <imp/imp_framesource.h>

static IMPSensorInfo sen;
static int W, H, NF = 4;
static uint8_t *persist;   /* count of frames a pixel was hot (threshold 48) */

static int grab(uint8_t **y)
{
    IMPFrameInfo *fr = NULL; int t;
    for (t = 0; t < 40; t++) {
        if (IMP_FrameSource_GetFrame(0, &fr) < 0 || !fr) { usleep(100000); continue; }
        memcpy(*y, (void *)(uintptr_t)fr->virAddr, (size_t)W * H);
        IMP_FrameSource_ReleaseFrame(0, fr);
        return 0;
    }
    return -1;
}

static void settle(int n) { IMPFrameInfo *fr; int k; usleep(500000);
    for (k = 0; k < n; k++) { fr = NULL; if (IMP_FrameSource_GetFrame(0, &fr) == 0 && fr) IMP_FrameSource_ReleaseFrame(0, fr); } }

static void analyze(const char *name, uint8_t *y)
{
    long thr[3] = {24, 48, 96}, cnt[3] = {0, 0, 0}, x, yy; double sum = 0; long mx = 0;
    int pass = 0; (void)pass;
    for (yy = 1; yy < H - 1; yy++) for (x = 1; x < W - 1; x++) {
        uint8_t *p = y + yy * W + x; int c = *p, mxn = 0, i, j;
        sum += c;
        for (j = -1; j <= 1; j++) for (i = -1; i <= 1; i++) if ((i || j) && p[j * W + i] > mxn) mxn = p[j * W + i];
        if (c - mxn > 0) {   /* brighter than all 8 neighbours: isolated peak */
            int d = c - mxn;
            for (i = 0; i < 3; i++) if (d >= thr[i]) cnt[i]++;
            if (d >= 48) persist[yy * W + x]++;
            if (d > mx) mx = d;
        }
    }
    printf("[F] %s mean %.2f peaks>=24:%ld >=48:%ld >=96:%ld maxd %ld\n", name, sum / ((double)W * H), cnt[0], cnt[1], cnt[2], mx);
}

static int setratio(int idx_en, int val)
{
    IMPISPModuleRatioAttr a; memset(&a, 0, sizeof a);
    IMP_ISP_Tuning_GetModule_Ratio(IMPVI_MAIN, &a);
    a.ratio_attr[IMP_ISP_MODULE_DPC].en = idx_en ? IMPISP_TUNING_OPS_MODE_ENABLE : IMPISP_TUNING_OPS_MODE_DISABLE;
    a.ratio_attr[IMP_ISP_MODULE_DPC].ratio = val;
    return IMP_ISP_Tuning_SetModule_Ratio(IMPVI_MAIN, &a);
}
static int setbypass(int on)
{
    IMPISPModuleCtl m; memset(&m, 0, sizeof m);
    IMP_ISP_Tuning_GetModuleControl(IMPVI_MAIN, &m);
    m.bitBypassDPC = on;
    return IMP_ISP_Tuning_SetModuleControl(IMPVI_MAIN, &m);
}

int main(int argc, char **argv)
{
    IMPFSChnAttr fs0; IMPISPAEExprInfo ae, old; int i, f; uint8_t *y;
    if (argc < 9) { fprintf(stderr, "usage\n"); return 2; }
    W = atoi(argv[3]); H = atoi(argv[4]);
    int it = atoi(argv[5]), ag = atoi(argv[6]); NF = atoi(argv[7]);
    memset(&sen, 0, sizeof sen);
    strncpy(sen.name, argv[1], sizeof(sen.name) - 1); sen.cbus_type = TX_SENSOR_CONTROL_INTERFACE_I2C;
    strncpy(sen.i2c.type, argv[1], sizeof(sen.i2c.type) - 1); sen.i2c.addr = (int)strtol(argv[2], NULL, 0);
    sen.rst_gpio = sen.pwdn_gpio = sen.power_gpio = -1;
    if (IMP_ISP_Open() < 0 || IMP_ISP_AddSensor(IMPVI_MAIN, &sen) < 0 || IMP_ISP_EnableSensor(IMPVI_MAIN, &sen) < 0) return 1;
    if (IMP_System_Init() < 0 || IMP_ISP_EnableTuning() < 0) return 1;
    memset(&fs0, 0, sizeof fs0);
    fs0.picWidth = W; fs0.picHeight = H; fs0.pixFmt = PIX_FMT_NV12; fs0.outFrmRateNum = 15; fs0.outFrmRateDen = 1; fs0.nrVBs = 2; fs0.type = FS_PHY_CHANNEL;
    if (IMP_FrameSource_CreateChn(0, &fs0) < 0) return 1;
    IMP_FrameSource_SetFrameDepth(0, 1);
    if (IMP_FrameSource_EnableChn(0) < 0) return 1;
    sleep(3);
    y = malloc((size_t)W * H); persist = calloc((size_t)W * H, 1);
    memset(&old, 0, sizeof old); IMP_ISP_Tuning_GetAeExprInfo(IMPVI_MAIN, &old);
    ae = old;
    if (it > 0) {
        ae.AeMode = IMPISP_TUNING_OPS_TYPE_MANUAL;
        ae.AeIntegrationTimeMode = IMPISP_TUNING_OPS_TYPE_MANUAL; ae.AeIntegrationTimeUnit = ISP_CORE_EXPR_UNIT_US; ae.AeIntegrationTime = it;
        ae.AeAGainManualMode = IMPISP_TUNING_OPS_TYPE_MANUAL; ae.AeAGain = ag;
        printf("[T] manual set ret %d\n", IMP_ISP_Tuning_SetAeExprInfo(IMPVI_MAIN, &ae));
    }
    sleep(2);
    { IMPISPAEExprInfo c; memset(&c, 0, sizeof c); IMP_ISP_Tuning_GetAeExprInfo(IMPVI_MAIN, &c);
      printf("[T] AE it=%u again=%u dgain=%u ispdgain=%u totaldb=%u\n", c.AeIntegrationTime, c.AeAGain, c.AeDGain, c.AeIspDGain, c.TotalGainDb); }
    { IMPISPModuleRatioAttr r; memset(&r, 0, sizeof r); IMP_ISP_Tuning_GetModule_Ratio(IMPVI_MAIN, &r);
      printf("[T] start DPC ratio en=%d val=%d\n", r.ratio_attr[IMP_ISP_MODULE_DPC].en, r.ratio_attr[IMP_ISP_MODULE_DPC].ratio); }
    for (i = 8; i < argc; i++) {
        char *c = argv[i]; int r = 0;
        memset(persist, 0, (size_t)W * H);
        if (!strcmp(c, "bypass")) r = setbypass(1);
        else if (!strcmp(c, "default")) { setbypass(0); }
        else if (c[0] == 'r') { setbypass(0); r = setratio(1, atoi(c + 1)); }
        printf("[C] %s set=%d\n", c, r);
        settle(4);
        for (f = 0; f < NF; f++) { if (grab(&y)) { printf("[E] nopic\n"); break; } analyze(c, y); }
        { long both = 0, k; for (k = 0; k < (long)W * H; k++) if (persist[k] >= NF) both++;
          printf("[P] %s persistent(all %d frames, d>=48): %ld\n", c, NF, both); }
        fflush(stdout);
    }
    setbypass(0);
    if (it > 0) IMP_ISP_Tuning_SetAeExprInfo(IMPVI_MAIN, &old);
    IMP_FrameSource_DisableChn(0); IMP_FrameSource_DestroyChn(0);
    IMP_ISP_DisableTuning(); IMP_System_Exit();
    IMP_ISP_DisableSensor(IMPVI_MAIN); IMP_ISP_DelSensor(IMPVI_MAIN, &sen); IMP_ISP_Close();
    return 0;
}
