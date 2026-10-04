/* T41 P3 tuning controls: vendor control IDs, envelope and pointer
 * pass-through of the public structures (host test, needs the T41 1.2.0
 * vendor headers via T41_HEADERS). */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <imp/imp_isp.h>

int OpenIMP_P1_TuningIOCtl(uint32_t command, void *argument);
int OpenIMP_P1_SetDefaultBinPath(IMPVI_NUM num, const char *path);

static struct {
    uint32_t command;
    int32_t vinum, direction, control;
    uintptr_t payload;
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
    return last.result;
}

int OpenIMP_P1_SetDefaultBinPath(IMPVI_NUM num, const char *path)
{
    (void)num;
    (void)path;
    return 0;
}

#define EXPECT(call, dir, id, ptr)                                            \
    do {                                                                      \
        int before = last.calls;                                              \
        assert((call) == 0);                                                  \
        assert(last.calls == before + 1);                                     \
        assert(last.command == 0xc0105435U);                                  \
        assert(last.vinum == IMPVI_MAIN);                                     \
        assert(last.direction == (dir));                                      \
        assert(last.control == (id));                                         \
        assert(last.payload == (uintptr_t)(ptr));                             \
    } while (0)

int main(void)
{
    IMPISPAEScenceAttr scence;
    IMPISPModuleRatioAttr ratio;
    IMPISPAEExprInfo expr;
    IMPISPCCMAttr ccm;
    IMPISPGammaAttr gamma;
    IMPISPCSCAttr csc;
    IMPISPModuleCtl ctl;
    IMPISPAutoZoom zoom;
    IMPISPWdrOutputMode wdr;
    int before;

    /* Vendor structure sizes the open-tx-isp routes rely on. */
    assert(sizeof(IMPISPAEScenceAttr) == 52 || sizeof(void *) != 4);
    assert(sizeof(IMPISPModuleRatioAttr) == 128);
    assert(sizeof(IMPISPAEExprInfo) == 232 || sizeof(void *) != 4);

    EXPECT(IMP_ISP_Tuning_SetAeScenceAttr(IMPVI_MAIN, &scence), 0, 0x08000024, &scence);
    EXPECT(IMP_ISP_Tuning_GetAeScenceAttr(IMPVI_MAIN, &scence), 1, 0x08000024, &scence);
    EXPECT(IMP_ISP_Tuning_SetModule_Ratio(IMPVI_MAIN, &ratio), 0, 0x080000a4, &ratio);
    EXPECT(IMP_ISP_Tuning_GetModule_Ratio(IMPVI_MAIN, &ratio), 1, 0x080000a4, &ratio);
    EXPECT(IMP_ISP_Tuning_SetAeExprInfo(IMPVI_MAIN, &expr), 0, 0x08000023, &expr);
    EXPECT(IMP_ISP_Tuning_GetAeExprInfo(IMPVI_MAIN, &expr), 1, 0x08000023, &expr);
    EXPECT(IMP_ISP_Tuning_SetCCMAttr(IMPVI_MAIN, &ccm), 0, 0x08000080, &ccm);
    EXPECT(IMP_ISP_Tuning_GetCCMAttr(IMPVI_MAIN, &ccm), 1, 0x08000080, &ccm);
    EXPECT(IMP_ISP_Tuning_SetGammaAttr(IMPVI_MAIN, &gamma), 0, 0x08000025, &gamma);
    EXPECT(IMP_ISP_Tuning_SetISPCSCAttr(IMPVI_MAIN, &csc), 0, 0x08000096, &csc);
    EXPECT(IMP_ISP_Tuning_SetModuleControl(IMPVI_MAIN, &ctl), 0, 0x08000072, &ctl);
    EXPECT(IMP_ISP_Tuning_SetAutoZoom(IMPVI_MAIN, &zoom), 0, 0x08000077, &zoom);
    EXPECT(IMP_ISP_Tuning_SetWdrOutputMode(IMPVI_MAIN, &wdr), 0, 0x08000054, &wdr);

    /* Invalid arguments never reach the driver. */
    before = last.calls;
    assert(IMP_ISP_Tuning_SetAeScenceAttr(IMPVI_MAIN, NULL) == -1);
    assert(IMP_ISP_Tuning_SetModule_Ratio(IMPVI_BUTT, &ratio) == -1);
    assert(last.calls == before);

    /* Driver errors (e.g. -EOPNOTSUPP) are returned, not hidden. */
    last.result = -1;
    assert(IMP_ISP_Tuning_SetCCMAttr(IMPVI_MAIN, &ccm) == -1);
    puts("t41 p3 controls tests passed");
    return 0;
}
