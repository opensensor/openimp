/* T40 P3 tuning controls: the vendor T40 1.3.1 libimp control numbers
 * (from its disassembly), the 16-byte request envelope, pointer
 * pass-through of the public structures and the CCM conversion.
 * Host test, needs the T40 1.3.1 vendor headers via T40_HEADERS; no camera. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <imp/imp_isp.h>

int OpenIMP_P1_TuningIOCtl(uint32_t command, void *argument);
int OpenIMP_P1_SetDefaultBinPath(IMPVI_NUM num, const char *path);
int OpenIMP_P1_GetDefaultBinPath(IMPVI_NUM num, char *path, size_t size);
int OpenIMP_P1_IspIOCtl(uint32_t command, void *argument);
int OpenIMP_P1_GetSensorName(char name[32], int32_t *cbus_type);

int OpenIMP_P1_IspIOCtl(uint32_t command, void *argument)
{
    (void)command;
    (void)argument;
    return 0;
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

static struct {
    uint32_t command;
    int32_t vinum, direction, control;
    uintptr_t payload;
    unsigned char copy[64];         /* the payload bytes at the ioctl */
    unsigned char reply[64];        /* what the "driver" writes on a get */
    int calls;
    int result;
} last;

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

    puts("T40 P3 controls: all checks passed");
    return 0;
}
