/* T40 P3 tuning controls: the vendor T40 1.3.1 libimp control numbers
 * (from its disassembly), the 16-byte request envelope, pointer
 * pass-through of the public structures and the CCM conversion.
 * Host test, needs the T40 1.3.1 vendor headers via T40_HEADERS; no camera. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <imp/imp_isp.h>

int OpenIMP_P1_TuningIOCtl(uint32_t command, void *argument);
int OpenIMP_P1_SetDefaultBinPath(IMPVI_NUM num, const char *path);
int OpenIMP_P1_GetDefaultBinPath(IMPVI_NUM num, char *path, size_t size);
int OpenIMP_P1_IspIOCtl(uint32_t command, void *argument);
int OpenIMP_P1_GetSensorName(char name[32], int32_t *cbus_type);

static struct { uint32_t command; uint32_t value; int result; int err; int calls; } ispnode;

int OpenIMP_P1_IspIOCtl(uint32_t command, void *argument)
{
    ispnode.command = command;
    if (command == 0xc0045430U)
        memcpy(&ispnode.value, argument, 4);
    ispnode.calls++;
    if (ispnode.result)
        errno = ispnode.err;
    return ispnode.result;
}

static struct { uint32_t command; int32_t value; int result; int calls; } opened;

int OpenIMP_P1_IspIOCtlOpened(uint32_t command, void *argument);
int OpenIMP_P1_IspIOCtlOpened(uint32_t command, void *argument)
{
    opened.command = command;
    memcpy(&opened.value, argument, 4);
    opened.calls++;
    if (opened.result)
        errno = opened.result;
    return opened.result ? -1 : 0;
}

int OpenIMP_P1_GetSensorName(char name[32], int32_t *cbus_type)
{
    memset(name, 0, 32);
    *cbus_type = 0;
    return 0;
}

int OpenIMP_P1_SetDefaultBinPath(IMPVI_NUM num, const char *path)
{
    (void)num;
    (void)path;
    return 0;
}

int OpenIMP_P1_GetDefaultBinPath(IMPVI_NUM num, char *path, size_t size)
{
    (void)num;
    (void)path;
    (void)size;
    return -1;
}

/* IMP_ISP_Tuning_SwitchBin: ISP-enabled flag and the kernel's view */
static int sb_enabled;
static struct { uint32_t enable; char bname[128]; } sb_seen;

int OpenIMP_P1_TuningEnabled(void);
int OpenIMP_P1_TuningEnabled(void)
{
    return sb_enabled;
}

static struct {
    uint32_t command;
    int32_t vinum, direction, control;
    uintptr_t payload;
    unsigned char copy[64];         /* the payload bytes at the ioctl */
    unsigned char reply[64];        /* what the "driver" writes on a get */
    int calls;
    int result;
} last;

static uint32_t rm_kernel;  /* running mode the "kernel" holds */
static unsigned char draw_kernel[896];  /* draw attr the "kernel" holds */

int OpenIMP_P1_TuningIOCtl(uint32_t command, void *argument)
{
    /* Same layout as the P3 envelope: 16 bytes on the 32-bit target. */
    const struct {
        int32_t vinum, direction, control;
        uintptr_t payload;
    } *request = argument;

    last.command = command;
    last.vinum = request->vinum;
    last.direction = request->direction;
    last.control = request->control;
    last.payload = request->payload;
    last.calls++;
    if (request->control == 0x080000a5)
        memcpy(&sb_seen, (const void *)request->payload, sizeof(sb_seen) > 68 ? 68 : sizeof(sb_seen));
    if (request->control == 0x08000096 || request->control == 0x08000091) {
        /* CSC / WaitFrameDone: the library hands a 64 / 24 byte block */
        memcpy(last.copy, (void *)request->payload,
               request->control == 0x08000096 ? 64 : 24);
        if (request->direction)
            memcpy((void *)request->payload, last.reply,
                   request->control == 0x08000096 ? 64 : 24);
    }
    if (request->control == 0x08000071 && request->direction) {
        /* running mode get: the stock kernel answers in the payload word */
        ((struct { int32_t a, b, c; uintptr_t payload; } *)argument)->payload =
            (uintptr_t)rm_kernel;
    }
    if (request->control == 0x08000071 && !request->direction)
        rm_kernel = *(const uint32_t *)request->payload;
    if (request->control == 0x080000a0) {
        if (request->direction)
            memcpy((void *)request->payload, draw_kernel, sizeof(draw_kernel));
        else
            memcpy(draw_kernel, (const void *)request->payload, sizeof(draw_kernel));
    }
    if (request->control == 0x08000073 || request->control == 0x08000081)
        memcpy(last.copy, (const void *)request->payload,
               request->control == 0x08000073 ? 4 : 1);
    if (request->control == 0x08000080) {       /* CCM: 40-byte payload */
        if (request->direction) {
            memcpy((void *)request->payload, last.reply, 40);
        } else {
            memcpy(last.copy, (void *)request->payload, 40);
        }
    }
    return last.result;
}

#define EXPECT(call, dir, id, ptr)                                            \
    do {                                                                      \
        int before = last.calls;                                              \
        assert((call) == 0);                                                  \
        assert(last.calls == before + 1);                                     \
        assert(last.command == 0xc0105436U);    /* T40; T41 is ...35 */       \
        assert(last.vinum == IMPVI_MAIN);                                     \
        assert(last.direction == (dir));                                      \
        assert(last.control == (id));                                         \
        assert(last.payload == (uintptr_t)(ptr));                             \
    } while (0)

static uint32_t word(const unsigned char *p, int index)
{
    uint32_t v;

    memcpy(&v, p + 4 + 4 * index, 4);
    return v;
}

int main(void)
{
    IMPISPAEScenceAttr scence;
    IMPISPModuleRatioAttr ratio;
    IMPISPGammaAttr gamma;
    IMPISPModuleCtl ctl;
    IMPISPAutoZoom zoom;
    IMPISPWdrOutputMode wdr;
    IMPISPSENSORAttr sensor;
    IMPISPCoefftWb coefft;
    IMPISPScalerLvAttr scaler;
    IMPISPCCMAttr ccm;
    unsigned char *wire;
    int before;
    int i;

    /* Payload sizes of the vendor 1.3.1 header (copied by the T40 kernel). */
    assert(sizeof(IMPISPSENSORAttr) == 20);
    assert(sizeof(IMPISPModuleRatioAttr) == 128);
    assert(sizeof(IMPISPGammaAttr) == 264);
    assert(sizeof(IMPISPCCMAttr) == 44);
    assert(sizeof(IMPISPModuleCtl) == 4);
    assert(sizeof(IMPISPWdrOutputMode) == 4);

    /* Control numbers read from the vendor T40 1.3.1 libimp. */
    EXPECT(IMP_ISP_Tuning_SetAeScenceAttr(IMPVI_MAIN, &scence), 0, 0x08000024, &scence);
    EXPECT(IMP_ISP_Tuning_GetAeScenceAttr(IMPVI_MAIN, &scence), 1, 0x08000024, &scence);
    EXPECT(IMP_ISP_Tuning_SetModule_Ratio(IMPVI_MAIN, &ratio), 0, 0x080000a4, &ratio);
    EXPECT(IMP_ISP_Tuning_GetModule_Ratio(IMPVI_MAIN, &ratio), 1, 0x080000a4, &ratio);
    memset(&gamma, 0, sizeof(gamma));
    gamma.Curve_type = IMP_ISP_GAMMA_CURVE_USER;
    EXPECT(IMP_ISP_Tuning_SetGammaAttr(IMPVI_MAIN, &gamma), 0, 0x08000025, &gamma);
    EXPECT(IMP_ISP_Tuning_GetGammaAttr(IMPVI_MAIN, &gamma), 1, 0x08000025, &gamma);
    EXPECT(IMP_ISP_Tuning_SetModuleControl(IMPVI_MAIN, &ctl), 0, 0x08000072, &ctl);
    EXPECT(IMP_ISP_Tuning_GetModuleControl(IMPVI_MAIN, &ctl), 1, 0x08000072, &ctl);
    EXPECT(IMP_ISP_Tuning_SetAutoZoom(IMPVI_MAIN, &zoom), 0, 0x08000077, &zoom);
    EXPECT(IMP_ISP_Tuning_GetAutoZoom(IMPVI_MAIN, &zoom), 1, 0x08000077, &zoom);
    EXPECT(IMP_ISP_Tuning_SetWdrOutputMode(IMPVI_MAIN, &wdr), 0, 0x08000054, &wdr);
    EXPECT(IMP_ISP_Tuning_GetWdrOutputMode(IMPVI_MAIN, &wdr), 1, 0x08000054, &wdr);
    EXPECT(IMP_ISP_Tuning_GetSensorAttr(IMPVI_MAIN, &sensor), 1, 0x08000033, &sensor);
    /* T40 uses 0x9b for the RGB coefficients (T41: 0x98) */
    EXPECT(IMP_ISP_Tuning_Awb_SetRgbCoefft(IMPVI_MAIN, &coefft), 0, 0x0800009b, &coefft);
    EXPECT(IMP_ISP_Tuning_Awb_GetRgbCoefft(IMPVI_MAIN, &coefft), 1, 0x0800009b, &coefft);
    EXPECT(IMP_ISP_SetScalerLv(IMPVI_MAIN, &scaler), 0, 0x080000a6, &scaler);

    /* Vendor argument checks: no ioctl */
    before = last.calls;
    assert(IMP_ISP_Tuning_GetSensorAttr(IMPVI_MAIN, NULL) != 0);
    assert(IMP_ISP_Tuning_SetGammaAttr(IMPVI_MAIN, NULL) != 0);
    gamma.Curve_type = IMP_ISP_GAMMA_CURVE_BUTT;        /* 4: refused */
    assert(IMP_ISP_Tuning_SetGammaAttr(IMPVI_MAIN, &gamma) != 0);
    assert(IMP_ISP_Tuning_SetCCMAttr(IMPVI_MAIN, NULL) != 0);
    assert(IMP_ISP_Tuning_GetCCMAttr(IMPVI_MAIN, NULL) != 0);
    assert(IMP_ISP_Tuning_GetSensorAttr(IMPVI_BUTT, &sensor) != 0);
    assert(last.calls == before);

    /* A failing driver is reported to the caller */
    last.result = -1;
    assert(IMP_ISP_Tuning_GetSensorAttr(IMPVI_MAIN, &sensor) != 0);
    last.result = 0;

    /* CCM set: 40-byte {manual, sat, pad, u32[9]}; 1/1024 steps with a sign
     * in bit 13 and the low 13 bits two's complement of the magnitude. */
    memset(&ccm, 0, sizeof(ccm));
    ccm.ManualEn = IMPISP_TUNING_OPS_MODE_ENABLE;
    ccm.SatEn = IMPISP_TUNING_OPS_MODE_DISABLE;
    ccm.ColorMatrix[0] = 1.0f;
    ccm.ColorMatrix[1] = -1.0f;
    ccm.ColorMatrix[2] = 0.5f;
    ccm.ColorMatrix[3] = -0.5f;
    ccm.ColorMatrix[4] = 0.0f;
    ccm.ColorMatrix[5] = -0.0005f;      /* -0.512/1024: truncates to 0 magnitude */
    ccm.ColorMatrix[6] = 1.9990234375f; /* 2047/1024 */
    ccm.ColorMatrix[7] = -3.9990234375f;/* -4095/1024 */
    ccm.ColorMatrix[8] = -0.0000005f;   /* inside the -1e-5 dead band */
    memset(last.copy, 0xee, sizeof(last.copy));
    assert(IMP_ISP_Tuning_SetCCMAttr(IMPVI_MAIN, &ccm) == 0);
    assert(last.control == 0x08000080 && last.direction == 0);
    assert(last.payload != (uintptr_t)&ccm);        /* a converted copy */
    assert(last.copy[0] == 1 && last.copy[1] == 0);
    assert(word(last.copy, 0) == 0x0400U);
    assert(word(last.copy, 1) == 0x3c00U);          /* (-1024 & 0x1fff) | 0x2000 */
    assert(word(last.copy, 2) == 0x0200U);
    assert(word(last.copy, 3) == 0x3e00U);
    assert(word(last.copy, 4) == 0x0000U);
    assert(word(last.copy, 5) == 0x2000U);          /* sign set, magnitude 0 */
    assert(word(last.copy, 6) == 0x07ffU);
    assert(word(last.copy, 7) == 0x3001U);          /* -4095 & 0x1fff = 0x1001 */
    assert(word(last.copy, 8) == 0x0000U);
    assert(ccm.ColorMatrix[1] == -1.0f);            /* caller's matrix intact */

    /* CCM get decodes the same representation */
    memset(last.reply, 0, sizeof(last.reply));
    last.reply[0] = 1;
    last.reply[1] = 1;
    {
        uint32_t w[9] = { 0x0400, 0x3c00, 0x0200, 0x3e00, 0, 0x2000, 0x07ff,
                          0x3001, 0x0001 };
        memcpy(last.reply + 4, w, sizeof(w));
    }
    memset(&ccm, 0, sizeof(ccm));
    assert(IMP_ISP_Tuning_GetCCMAttr(IMPVI_MAIN, &ccm) == 0);
    assert(last.direction == 1 && last.control == 0x08000080);
    assert(ccm.ManualEn == IMPISP_TUNING_OPS_MODE_ENABLE);
    assert(ccm.SatEn == IMPISP_TUNING_OPS_MODE_ENABLE);
    assert(ccm.ColorMatrix[0] == 1.0f && ccm.ColorMatrix[1] == -1.0f);
    assert(ccm.ColorMatrix[2] == 0.5f && ccm.ColorMatrix[3] == -0.5f);
    assert(ccm.ColorMatrix[4] == 0.0f && ccm.ColorMatrix[5] == 0.0f);
    assert(ccm.ColorMatrix[6] == 1.9990234375f);
    assert(ccm.ColorMatrix[7] == -4095.0f / 1024.0f);
    assert(ccm.ColorMatrix[8] == 1.0f / 1024.0f);

    /* encode/decode round trip over the whole 1/1024 grid used by the ISP */
    wire = last.reply;
    for (i = -4095; i <= 4095; i += 7) {
        float c = (float)i / 1024.0f;
        uint32_t w;

        ccm.ColorMatrix[0] = c;
        assert(IMP_ISP_Tuning_SetCCMAttr(IMPVI_MAIN, &ccm) == 0);
        w = word(last.copy, 0);
        memset(wire, 0, 64);
        memcpy(wire + 4, &w, 4);
        assert(IMP_ISP_Tuning_GetCCMAttr(IMPVI_MAIN, &ccm) == 0);
        assert(ccm.ColorMatrix[0] == c);
    }

    /* BCSH accessors (vendor T40 1.3.1): the get reads a per-vinum cache and
     * never reaches the driver (0 even when the driver would fail), the set
     * sends the pointer ioctl and caches on success; -4 num>=4, -9 NULL,
     * -6 driver failure. */
    {
        unsigned char v = 77, g = 0;
        int calls;

        last.result = -1;                  /* a driver get would fail */
        calls = last.calls;
        assert(IMP_ISP_Tuning_GetBrightness(IMPVI_MAIN, &g) == 0 && g == 128);
        assert(IMP_ISP_Tuning_GetContrast(IMPVI_MAIN, &g) == 0 && g == 128);
        assert(IMP_ISP_Tuning_GetSharpness(IMPVI_MAIN, &g) == 0 && g == 128);
        assert(IMP_ISP_Tuning_GetSaturation(IMPVI_MAIN, &g) == 0 && g == 128);
        assert(last.calls == calls);
        assert(IMP_ISP_Tuning_SetBrightness(IMPVI_MAIN, &v) == -6);
        assert(IMP_ISP_Tuning_GetBrightness(IMPVI_MAIN, &g) == 0 && g == 128);
        last.result = 0;
        EXPECT(IMP_ISP_Tuning_SetBrightness(IMPVI_MAIN, &v), 0, 0x08000092, &v);
        EXPECT(IMP_ISP_Tuning_SetSharpness(IMPVI_MAIN, &v), 0, 0x08000093, &v);
        EXPECT(IMP_ISP_Tuning_SetSaturation(IMPVI_MAIN, &v), 0, 0x08000094, &v);
        EXPECT(IMP_ISP_Tuning_SetContrast(IMPVI_MAIN, &v), 0, 0x08000095, &v);
        assert(IMP_ISP_Tuning_GetBrightness(IMPVI_MAIN, &g) == 0 && g == 77);
        assert(IMP_ISP_Tuning_SetBrightness(IMPVI_MAIN, NULL) == -9);
        assert(IMP_ISP_Tuning_SetBrightness((IMPVI_NUM)4, &v) == -4);
        v = 5;
        assert(IMP_ISP_Tuning_SetContrast(IMPVI_MAIN, &v) == 0);
        assert(IMP_ISP_Tuning_GetContrast(IMPVI_MAIN, &g) == 0 && g == 5);
        assert(IMP_ISP_Tuning_GetBrightness(IMPVI_MAIN, &g) == 0 && g == 77);
    }

    /* Get/SetMask (vendor T40 1.3.1): 288-byte payload, control 0x08000074,
     * YUV bytes computed for enabled RGB blocks, Get restores type + RGB. */
    {
        IMPISPMASKAttr mk, back;
        unsigned char *b = (unsigned char *)&mk;

        memset(&mk, 0, sizeof mk);
        mk.mask_chx[1][2].mask_en = 1;
        mk.mask_chx[1][2].mask_type = IMPISP_MASK_TYPE_RGB;
        mk.mask_chx[1][2].mask_value.mask_argb.r_value = 255;
        mk.mask_chx[1][2].mask_value.mask_argb.g_value = 255;
        mk.mask_chx[1][2].mask_value.mask_argb.b_value = 255;
        mk.mask_chx[0][0].mask_value.mask_argb.r_value = 9;   /* disabled */
        EXPECT(IMP_ISP_Tuning_SetMask(IMPVI_MAIN, &mk), 0, 0x08000074, &mk);
        assert(mk.mask_chx[1][2].mask_value.mask_ayuv.y_value == 255);
        assert(mk.mask_chx[1][2].mask_value.mask_ayuv.u_value == 128);
        assert(mk.mask_chx[1][2].mask_value.mask_ayuv.v_value == 128);
        assert(mk.mask_chx[0][0].mask_value.mask_ayuv.y_value == 0);
        assert((b + (1 * 4 + 2) * 24)[19] == 255);
        memset(&back, 0, sizeof back);                  /* driver reply */
        back.mask_chx[1][2].mask_en = 1;
        back.mask_chx[1][2].mask_type = IMPISP_MASK_TYPE_YUV;
        EXPECT(IMP_ISP_Tuning_GetMask(IMPVI_MAIN, &back), 1, 0x08000074, &back);
        assert(back.mask_chx[1][2].mask_type == IMPISP_MASK_TYPE_RGB);
        assert(back.mask_chx[1][2].mask_value.mask_argb.r_value == 255);
        assert(IMP_ISP_Tuning_SetMask(IMPVI_MAIN, NULL) == -9);
        assert(IMP_ISP_Tuning_GetMask((IMPVI_NUM)4, &back) == -4);
        last.result = -1;
        assert(IMP_ISP_Tuning_SetMask(IMPVI_MAIN, &mk) == -6);
        assert(IMP_ISP_Tuning_GetMask(IMPVI_MAIN, &back) == -6);
        last.result = 0;
    }

    /* IMP_ISP_Tuning_SwitchBin (vendor T40 1.3.1 libimp disassembly): control
     * 0x80000a5 set, payload = IMPISPBinAttr (68 bytes), vendor check
     * order and return codes, NUL-terminated copy to the kernel. */
    {
        IMPISPBinAttr bin;
        int n;

        assert(sizeof(IMPISPBinAttr) == 68);
        memset(&bin, 0, sizeof(bin));
        bin.enable = IMPISP_TUNING_OPS_MODE_ENABLE;
        strcpy(bin.bname, "/tmp/night-bin");
        n = last.calls;
        sb_enabled = 1;
        assert(IMP_ISP_Tuning_SwitchBin(IMPVI_MAIN, &bin) == 0);
        assert(last.calls == n + 1);
        assert(last.command == 0xc0105436U);
        assert(last.vinum == IMPVI_MAIN && last.direction == 0);
        assert(last.control == 0x080000a5);
        assert(last.payload != 0 && last.payload != (uintptr_t)&bin);
        assert(sb_seen.enable == 1 && !strcmp(sb_seen.bname, "/tmp/night-bin"));
        /* enable 0 = back to the default bin; reaches the kernel too */
        bin.enable = IMPISP_TUNING_OPS_MODE_DISABLE;
        assert(IMP_ISP_Tuning_SwitchBin(IMPVI_MAIN, &bin) == 0);
        assert(sb_seen.enable == 0 && last.calls == n + 2);
        /* an unterminated name is cut before the kernel sees it */
        memset(bin.bname, 'x', sizeof(bin.bname));
        bin.enable = IMPISP_TUNING_OPS_MODE_ENABLE;
        assert(IMP_ISP_Tuning_SwitchBin(IMPVI_MAIN, &bin) == 0);
        assert(sb_seen.bname[sizeof(bin.bname) - 1] == '\0');
        assert(sb_seen.bname[sizeof(bin.bname) - 2] == 'x');
        assert(bin.bname[sizeof(bin.bname) - 1] == 'x');    /* caller intact */
        n = last.calls;
        /* vendor refusals: no ioctl, vendor codes */
        assert(IMP_ISP_Tuning_SwitchBin(IMPVI_MAIN, NULL) == -9);
        assert(IMP_ISP_Tuning_SwitchBin((IMPVI_NUM)4, &bin) == -4);
        bin.enable = (IMPISPTuningOpsMode)2;
        assert(IMP_ISP_Tuning_SwitchBin(IMPVI_MAIN, &bin) == -10);
        bin.enable = IMPISP_TUNING_OPS_MODE_ENABLE;
        sb_enabled = 0;                                 /* ISP not running */
        assert(IMP_ISP_Tuning_SwitchBin(IMPVI_MAIN, &bin) == -5);
        sb_enabled = 1;
        assert(last.calls == n);
        /* T40 order: sensor number before the NULL test */
        assert(IMP_ISP_Tuning_SwitchBin((IMPVI_NUM)4, NULL) == -4);

        last.result = -1;                               /* driver refuses */
        assert(IMP_ISP_Tuning_SwitchBin(IMPVI_MAIN, &bin) == -6);
        last.result = 0;
    }

    /* More pass-throughs (control numbers from the vendor 1.3.1 libimp). */
    {
        IMPISPStatisConfig statis;
        IMPISPFaceAttr face;
        IMPISPHLDCAttr hldc;
        IMPISPAeExpListAttr explist;
        IMPISPAeSpeedAttr speed;
        IMPISPAFStatisInfo af;
        IMPISPAFMetricsInfo metric;
        IMPISPFixedContrastAttr fixed;
        IMPISPTuningOpsMode bypass;
        IMPISPCSCAttr csc;
        IMPISPWaitFrameAttr wait;

        EXPECT(IMP_ISP_Tuning_SetStatisConfig(IMPVI_MAIN, &statis), 0, 0x08000000, &statis);
        EXPECT(IMP_ISP_Tuning_GetStatisConfig(IMPVI_MAIN, &statis), 1, 0x08000000, &statis);
        EXPECT(IMP_ISP_Tuning_SetFaceAe(IMPVI_MAIN, &face), 0, 0x08000035, &face);
        EXPECT(IMP_ISP_Tuning_GetFaceAe(IMPVI_MAIN, &face), 1, 0x08000035, &face);
        EXPECT(IMP_ISP_Tuning_SetFaceAwb(IMPVI_MAIN, &face), 0, 0x08000014, &face);
        EXPECT(IMP_ISP_Tuning_GetFaceAwb(IMPVI_MAIN, &face), 1, 0x08000014, &face);
        EXPECT(IMP_ISP_Tuning_SetHLDCAttr(IMPVI_MAIN, &hldc), 0, 0x08000097, &hldc);
        EXPECT(IMP_ISP_Tuning_GetHLDCAttr(IMPVI_MAIN, &hldc), 1, 0x08000097, &hldc);
        EXPECT(IMP_ISP_Tuning_SetAeExpList(IMPVI_MAIN, &explist), 0, 0x08000099, &explist);
        EXPECT(IMP_ISP_Tuning_GetAeExpList(IMPVI_MAIN, &explist), 1, 0x08000099, &explist);
        EXPECT(IMP_ISP_Tuning_SetAeSpeed(IMPVI_MAIN, &speed), 0, 0x0800009a, &speed);
        EXPECT(IMP_ISP_Tuning_GetAeSpeed(IMPVI_MAIN, &speed), 1, 0x0800009a, &speed);
        EXPECT(IMP_ISP_Tuning_GetAfStatistics(IMPVI_MAIN, &af), 1, 0x08000031, &af);
        EXPECT(IMP_ISP_Tuning_GetAFMetricesInfo(IMPVI_MAIN, &metric), 1, 0x08000034, &metric);
        EXPECT(IMP_ISP_Tuning_SetFixedContraster(IMPVI_MAIN, &fixed), 0, 0x08000098, &fixed);
        {
            IMPISPOSDAttr osd;
            uint32_t dq = 33333;

            EXPECT(IMP_ISP_SetOSDAttr(IMPVI_MAIN, &osd), 0, 0x080000a1, &osd);
            EXPECT(IMP_ISP_GetOSDAttr(IMPVI_MAIN, &osd), 1, 0x080000a1, &osd);
            /* SetPreDqtime: the pointer goes to the ISP node, vendor codes */
            assert(IMP_ISP_SetPreDqtime(IMPVI_MAIN, &dq) == 0);
            assert(ispnode.command == 0xc0045430U && ispnode.value == 33333U);
            assert(IMP_ISP_SetPreDqtime((IMPVI_NUM)4, &dq) == -4);
            assert(IMP_ISP_SetPreDqtime(IMPVI_MAIN, NULL) == -9);
            ispnode.result = -1; ispnode.err = EIO;
            assert(IMP_ISP_SetPreDqtime(IMPVI_MAIN, &dq) == -6);
            ispnode.err = ENODEV;
            assert(IMP_ISP_SetPreDqtime(IMPVI_MAIN, &dq) == -8);
            ispnode.result = 0;
        }

        /* ISP bypass is a library-held flag (0 before any set), no ioctl */
        before = last.calls;
        bypass = (IMPISPTuningOpsMode)7;
        assert(IMP_ISP_Tuning_GetISPBypass(IMPVI_MAIN, &bypass) == 0);
        assert(bypass == 0);
        bypass = IMPISP_TUNING_OPS_MODE_ENABLE;
        assert(IMP_ISP_Tuning_SetISPBypass(IMPVI_MAIN, &bypass) == 0);
        bypass = (IMPISPTuningOpsMode)7;
        assert(IMP_ISP_Tuning_GetISPBypass(IMPVI_MAIN, &bypass) == 0);
        assert(bypass == IMPISP_TUNING_OPS_MODE_ENABLE);
        assert(IMP_ISP_Tuning_GetISPBypass(IMPVI_MAIN, NULL) != 0);
        assert(last.calls == before);

        /* WaitFrameDone: 24-byte block, timeout out, counter/IRQ type back */
        assert(sizeof(IMPISPWaitFrameAttr) == 24);
        memset(&wait, 0xa5, sizeof(wait));
        wait.timeout = 250;
        memset(last.reply, 0, sizeof(last.reply));
        { uint64_t cnt = 0x1122334455667788ULL; uint32_t irq = 3;
          memcpy(last.reply + 8, &cnt, 8); memcpy(last.reply + 16, &irq, 4); }
        before = last.calls;
        assert(IMP_ISP_Tuning_WaitFrameDone(IMPVI_MAIN, &wait) == 0);
        assert(last.calls == before + 1 && last.control == 0x08000091 &&
               last.direction == 1);
        { uint32_t t; memcpy(&t, last.copy, 4); assert(t == 250); }
        assert(wait.timeout == 250 && wait.cnt == 0x1122334455667788ULL &&
               (uint32_t)wait.irqtype == 3);
        assert(((unsigned char *)&wait)[4] == 0xa5);   /* padding untouched */
        assert(IMP_ISP_Tuning_WaitFrameDone(IMPVI_MAIN, NULL) != 0);

        /* CSC (T40 wire: gamut word, 9 x 10-bit, 6 bytes) */
        before = last.calls;
        memset(&csc, 0, sizeof(csc));
        csc.ColorGamut = IMP_ISP_CG_BUTT;               /* 5: refused */
        assert(IMP_ISP_Tuning_SetISPCSCAttr(IMPVI_MAIN, &csc) != 0);
        assert(last.calls == before);
        csc.ColorGamut = IMP_ISP_CG_BT709_FULL;
        EXPECT(IMP_ISP_Tuning_SetISPCSCAttr(IMPVI_MAIN, &csc), 0, 0x08000096, last.payload);
        { uint32_t g; memcpy(&g, last.copy, 4); assert(g == 2); }
        csc.ColorGamut = IMP_ISP_CG_USER;
        csc.Matrix.CscCoef[0] = 0.299f;     /* 306.18 + .5 -> 306 */
        csc.Matrix.CscCoef[1] = -0.5f;      /* -511.5 -> -511 & 0x3ff = 0x201 */
        csc.Matrix.CscCoef[8] = 0.5f;      /* 1.0 would wrap to 0 (10-bit mask) */
        csc.Matrix.CscOffset[0] = 0x80;
        csc.Matrix.CscOffset[1] = 0x10;
        csc.Matrix.CscClip[0] = 1; csc.Matrix.CscClip[1] = 2;
        csc.Matrix.CscClip[2] = 3; csc.Matrix.CscClip[3] = 4;
        EXPECT(IMP_ISP_Tuning_SetISPCSCAttr(IMPVI_MAIN, &csc), 0, 0x08000096, last.payload);
        { uint32_t w;
          memcpy(&w, last.copy, 4); assert(w == 4);
          memcpy(&w, last.copy + 4, 4); assert(w == 306);
          memcpy(&w, last.copy + 8, 4); assert(w == 0x201);
          memcpy(&w, last.copy + 36, 4); assert(w == 512);
          assert(last.copy[40] == 0x80 && last.copy[41] == 0x10);
          assert(last.copy[42] == 1 && last.copy[45] == 4); }
        /* get, user gamut: signed words / 1024, entries 3,4,7,8 negated */
        memset(last.reply, 0, sizeof(last.reply));
        { int32_t v[9] = {307, 100, 0, 512, -256, 0, 0, 64, 1024}; uint32_t g = 4;
          memcpy(last.reply, &g, 4); memcpy(last.reply + 4, v, 36);
          last.reply[40] = 9; last.reply[41] = 8; last.reply[42] = 7;
          last.reply[45] = 6; }
        memset(&csc, 0x55, sizeof(csc));
        assert(IMP_ISP_Tuning_GetISPCSCAttr(IMPVI_MAIN, &csc) == 0);
        assert(csc.ColorGamut == IMP_ISP_CG_USER);
        assert(csc.Matrix.CscCoef[0] == 307.0f / 1024.0f);
        assert(csc.Matrix.CscCoef[3] == -0.5f && csc.Matrix.CscCoef[4] == 0.25f);
        assert(csc.Matrix.CscCoef[7] == -64.0f / 1024.0f && csc.Matrix.CscCoef[8] == -1.0f);
        assert(csc.Matrix.CscCoef[1] == 100.0f / 1024.0f);
        assert(csc.Matrix.CscOffset[0] == 9 && csc.Matrix.CscClip[3] == 6);
        /* get, other gamut: structure zeroed behind the gamut word */
        memset(last.reply, 0, sizeof(last.reply));
        { uint32_t g = 1; memcpy(last.reply, &g, 4); }
        memset(&csc, 0x55, sizeof(csc));
        assert(IMP_ISP_Tuning_GetISPCSCAttr(IMPVI_MAIN, &csc) == 0);
        assert(csc.ColorGamut == IMP_ISP_CG_BT601_LIMITED);
        assert(csc.Matrix.CscCoef[0] == 0.0f && csc.Matrix.CscClip[3] == 0);
    }

    /* HVFLIP / hue / running mode (vendor T40 1.3.1, device FAILs of
     * 2026-10-11: GetHVFlip -1, running-mode get read back garbage). */
    {
        IMPISPHVFLIP f = IMPISP_FLIP_H_MODE, g = (IMPISPHVFLIP)99;
        IMPISPRunningMode m, r;
        unsigned char h = 20, hg = 0;

        sb_enabled = 1;
        /* GetHVFlip: cache only, 0 before any Set, never the driver */
        before = last.calls;
        assert(IMP_ISP_Tuning_GetHVFlip(IMPVI_MAIN, &g) == 0 && g == IMPISP_FLIP_NORMAL_MODE);
        assert(last.calls == before);
        /* SetHVFLIP: pointer to the mode, control 0x08000073 */
        EXPECT(IMP_ISP_Tuning_SetHVFLIP(IMPVI_MAIN, &f), 0, 0x08000073, &f);
        { uint32_t w; memcpy(&w, last.copy, 4); assert(w == IMPISP_FLIP_H_MODE); }
        before = last.calls;
        assert(IMP_ISP_Tuning_GetHVFlip(IMPVI_MAIN, &g) == 0 && g == IMPISP_FLIP_H_MODE);
        assert(last.calls == before);
        /* vendor checks: no ioctl */
        f = IMPISP_FLIP_MODE_BUTT;
        assert(IMP_ISP_Tuning_SetHVFLIP(IMPVI_MAIN, &f) == -4);
        assert(IMP_ISP_Tuning_SetHVFLIP(IMPVI_MAIN, NULL) == -9);
        assert(IMP_ISP_Tuning_SetHVFLIP((IMPVI_NUM)4, &f) == -4);
        assert(IMP_ISP_Tuning_GetHVFlip(IMPVI_MAIN, NULL) == -9);
        sb_enabled = 0;
        f = IMPISP_FLIP_V_MODE;
        assert(IMP_ISP_Tuning_SetHVFLIP(IMPVI_MAIN, &f) == -5);
        sb_enabled = 1;
        assert(last.calls == before);
        /* a failing driver: -6, cache unchanged */
        last.result = -1;
        assert(IMP_ISP_Tuning_SetHVFLIP(IMPVI_MAIN, &f) == -6);
        last.result = 0;
        assert(IMP_ISP_Tuning_GetHVFlip(IMPVI_MAIN, &g) == 0 && g == IMPISP_FLIP_H_MODE);

        /* hue: byte pointer, control 0x08000081; Get from the cache */
        before = last.calls;
        assert(IMP_ISP_Tuning_GetBcshHue(IMPVI_MAIN, &hg) == 0 && hg == 128);
        assert(last.calls == before);
        EXPECT(IMP_ISP_Tuning_SetBcshHue(IMPVI_MAIN, &h), 0, 0x08000081, &h);
        assert(last.copy[0] == 20);
        before = last.calls;
        assert(IMP_ISP_Tuning_GetBcshHue(IMPVI_MAIN, &hg) == 0 && hg == 20);
        assert(last.calls == before);
        last.result = -1;
        h = 230;
        assert(IMP_ISP_Tuning_SetBcshHue(IMPVI_MAIN, &h) == -6);
        last.result = 0;
        assert(IMP_ISP_Tuning_GetBcshHue(IMPVI_MAIN, &hg) == 0 && hg == 20);
        assert(IMP_ISP_Tuning_SetBcshHue(IMPVI_MAIN, NULL) == -9);
        assert(IMP_ISP_Tuning_SetBcshHue((IMPVI_NUM)4, &h) == -4);

        /* running mode: set by pointer, get answered in the payload word */
        m = IMPISP_RUNNING_MODE_NIGHT;
        EXPECT(IMP_ISP_Tuning_SetISPRunningMode(IMPVI_MAIN, &m), 0, 0x08000071, &m);
        r = (IMPISPRunningMode)9;
        before = last.calls;
        assert(IMP_ISP_Tuning_GetISPRunningMode(IMPVI_MAIN, &r) == 0);
        assert(last.calls == before + 1 && last.direction == 1 && last.control == 0x08000071);
        assert(r == IMPISP_RUNNING_MODE_NIGHT);
        m = IMPISP_RUNNING_MODE_DAY;
        assert(IMP_ISP_Tuning_SetISPRunningMode(IMPVI_MAIN, &m) == 0);
        r = (IMPISPRunningMode)9;
        assert(IMP_ISP_Tuning_GetISPRunningMode(IMPVI_MAIN, &r) == 0 && r == IMPISP_RUNNING_MODE_DAY);
        m = IMPISP_RUNNING_MODE_BUTT;
        before = last.calls;
        assert(IMP_ISP_Tuning_SetISPRunningMode(IMPVI_MAIN, &m) == -4);
        assert(IMP_ISP_Tuning_SetISPRunningMode(IMPVI_MAIN, NULL) == -9);
        assert(IMP_ISP_Tuning_GetISPRunningMode(IMPVI_MAIN, NULL) == -9);
        assert(last.calls == before);
        sb_enabled = 0;
    }

    /* Get/SetDrawAttr (vendor 0x83478/0x82f34): RGB -> YUV for enabled
     * RGB units, pointer to the kernel, Get restores type and RGB */
    {
        static IMPISPDrawAttr d, g;
        IMPISPDrawUnitAttr *u = &d.draw_chx[1][3];

        memset(&d, 0, sizeof(d));
        u->type = IMP_ISP_DRAW_WIND;
        u->color_type = 0;              /* RGB */
        u->draw_cfg.wind.wind_enable = 1;
        u->draw_cfg.wind.wind_left = 64;
        u->draw_cfg.wind.wind_width = 128;
        u->draw_cfg.wind.wind_color.mask_argb.r_value = 255;
        u->draw_cfg.wind.wind_color.mask_argb.g_value = 0;
        u->draw_cfg.wind.wind_color.mask_argb.b_value = 0;
        d.draw_chx[0][0].type = IMP_ISP_DRAW_LINE;      /* disabled: untouched */
        d.draw_chx[0][0].draw_cfg.line.line_color.mask_argb.r_value = 9;
        sb_enabled = 0;
        before = last.calls;
        assert(IMP_ISP_SetDrawAttr(IMPVI_MAIN, &d) == -5);
        assert(IMP_ISP_GetDrawAttr(IMPVI_MAIN, &g) == -5);
        sb_enabled = 1;
        assert(IMP_ISP_SetDrawAttr(IMPVI_MAIN, NULL) == -9);
        assert(IMP_ISP_SetDrawAttr((IMPVI_NUM)4, &d) == -4);
        assert(last.calls == before);
        EXPECT(IMP_ISP_SetDrawAttr(IMPVI_MAIN, &d), 0, 0x080000a0, &d);
        /* red in BT.601 full range: Y about 76, V high, U low */
        assert(u->draw_cfg.wind.wind_color.mask_ayuv.y_value > 60 &&
               u->draw_cfg.wind.wind_color.mask_ayuv.y_value < 90);
        assert(u->draw_cfg.wind.wind_color.mask_ayuv.v_value > 200);
        assert(u->draw_cfg.wind.wind_color.mask_ayuv.u_value < 110);
        assert(draw_kernel[(16 + 3) * 28 + 21] == u->draw_cfg.wind.wind_color.mask_ayuv.y_value);
        assert(d.draw_chx[0][0].draw_cfg.line.line_color.mask_ayuv.y_value == 0);
        /* the kernel hands back YUV only and type 1 (YUV) */
        memset(draw_kernel + (16 + 3) * 28 + 18, 0, 3);
        draw_kernel[(16 + 3) * 28 + 4] = 1;
        memset(&g, 0x55, sizeof(g));
        EXPECT(IMP_ISP_GetDrawAttr(IMPVI_MAIN, &g), 1, 0x080000a0, &g);
        assert(g.draw_chx[1][3].color_type == 0);
        assert(g.draw_chx[1][3].draw_cfg.wind.wind_color.mask_argb.r_value == 255);
        assert(g.draw_chx[1][3].draw_cfg.wind.wind_left == 64);
        last.result = -1;
        assert(IMP_ISP_SetDrawAttr(IMPVI_MAIN, &d) == -6);
        assert(IMP_ISP_GetDrawAttr(IMPVI_MAIN, &g) == -6);
        last.result = 0;
        sb_enabled = 0;
    }

    /* OSD_Set/GetRgnAttr_ISP: IMPOSDIspDraw parts at +48/+944/+1352 */
    {
        static unsigned char rgn[1772];
        uint32_t t;
        int show = -1;
        int IMP_OSD_SetRgnAttr_ISP(int, void *, int);
        int IMP_OSD_GetRgnAttr_ISP(int, void *, int *);

        sb_enabled = 1;
        memset(rgn, 0, sizeof(rgn));
        t = 11;                                   /* OSD_REG_ISP_LINE_RECT */
        memcpy(rgn, &t, 4);
        EXPECT(IMP_OSD_SetRgnAttr_ISP(0, rgn, 1), 0, 0x080000a0, rgn + 48);
        t = 12;                                   /* OSD_REG_ISP_COVER */
        memcpy(rgn, &t, 4);
        EXPECT(IMP_OSD_SetRgnAttr_ISP(0, rgn, 0), 0, 0x08000074, rgn + 1352);
        before = last.calls;
        t = 10;                                   /* ISP_PIC: other API */
        memcpy(rgn, &t, 4);
        assert(IMP_OSD_SetRgnAttr_ISP(0, rgn, 1) == -1);
        t = 7;
        memcpy(rgn, &t, 4);
        assert(IMP_OSD_SetRgnAttr_ISP(0, rgn, 1) == -1);
        assert(IMP_OSD_SetRgnAttr_ISP(2, rgn, 1) == -1);
        assert(IMP_OSD_SetRgnAttr_ISP(0, NULL, 1) == -1);
        assert(IMP_OSD_GetRgnAttr_ISP(0, rgn, NULL) == -1);
        assert(last.calls == before);
        assert(IMP_OSD_GetRgnAttr_ISP(0, rgn, &show) == 0);
        assert(show == 1);   /* vendor order: the ISP_PIC call stored its flag before refusing */
        assert(last.calls == before + 3 && last.control == 0x08000074 &&
               last.direction == 1 && last.payload == (uintptr_t)(rgn + 1352));
        last.result = -1;
        assert(IMP_OSD_GetRgnAttr_ISP(0, rgn, &show) == -1);
        t = 11;
        memcpy(rgn, &t, 4);
        assert(IMP_OSD_SetRgnAttr_ISP(0, rgn, 1) == -1);
        last.result = 0;
        sb_enabled = 0;
    }

    /* StartNightMode: ISP-device ioctl 0xc004542e with &num */
    assert(IMP_ISP_StartNightMode(IMPVI_MAIN) == 0);
    assert(opened.calls == 1 && opened.command == 0xc004542eU && opened.value == 0);
    assert(IMP_ISP_StartNightMode((IMPVI_NUM)4) == -4 && opened.calls == 1);
    opened.result = ENODEV;
    assert(IMP_ISP_StartNightMode(IMPVI_MAIN) == -8);
    opened.result = EINVAL;
    assert(IMP_ISP_StartNightMode(IMPVI_MAIN) == -1);
    opened.result = 0;

    puts("T40 P3 controls: all checks passed");
    return 0;
}
