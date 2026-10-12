/* T40 ABI layout: the numbers OpenIMP relies on, checked against the vendor
 * T40 1.3.1 headers (T40_HEADERS) with a 32-bit MIPS compiler (compile-only,
 * the static asserts are the test; no camera).  The same numbers appear in
 * src/t23/openimp_t23_osd_abi.h and src/t31/openimp_t31_ivs_abi.h, which
 * abi_internal_test.c compiles for PLATFORM_T40. */
#include <stddef.h>
#include <stdint.h>

#include <imp/imp_common.h>
#include <imp/imp_encoder.h>
#include <imp/imp_framesource.h>
#include <imp/imp_isp.h>
#include <imp/imp_ivs.h>
#include <imp/imp_ivs_base_move.h>
#include <imp/imp_ivs_move.h>
#include <imp/imp_osd.h>

#define SIZE_IS(type, n) _Static_assert(sizeof(type) == (n), #type " size")
#define OFF_IS(type, member, n) \
    _Static_assert(offsetof(type, member) == (n), #type "." #member)

_Static_assert(sizeof(void *) == 4, "needs a 32-bit target compiler");

/* IMP_OSD: header 1.3.1/en; the vendor libimp 1.3.1 itself copies 1948 bytes
 * (colType[64]) - see openimp_t23_osd_abi.h */
SIZE_IS(IMPOSDRgnAttr, 1772);
OFF_IS(IMPOSDRgnAttr, fmt, 28);
OFF_IS(IMPOSDRgnAttr, data, 32);
OFF_IS(IMPOSDRgnAttr, osdispdraw, 48);
OFF_IS(IMPOSDRgnAttr, fontData, 1640);
OFF_IS(IMPOSDRgnAttr, mosaicAttr, 1744);
SIZE_IS(IMPOSDIspDraw, 1592);
SIZE_IS(IMPOSDGrpRgnAttr, 36);
SIZE_IS(IMPISPMASKAttr, 288);

/* frame record: no direct_phyAddr on T40 */
SIZE_IS(IMPFrameInfo, 0x30);
OFF_IS(IMPFrameInfo, virAddr, 0x1c);
OFF_IS(IMPFrameInfo, pool, 0x20);
OFF_IS(IMPFrameInfo, timeStamp, 0x28);

/* IVS (identical to T41 1.2.0 except BaseMoveOutput, whose header lacks the
 * timestamp the libimp fills) */
SIZE_IS(IMP_IVS_MoveParam, 0x450);
OFF_IS(IMP_IVS_MoveParam, skipFrameCnt, 0xd0);
OFF_IS(IMP_IVS_MoveParam, frameInfo, 0xd8);
OFF_IS(IMP_IVS_MoveParam, roiRect, 0x108);
OFF_IS(IMP_IVS_MoveParam, roiRectCnt, 0x448);
SIZE_IS(IMP_IVS_MoveOutput, 0xd0);
SIZE_IS(IMP_IVS_BaseMoveParam, 0x40);
SIZE_IS(IMP_IVS_BaseMoveOutput, 12);
SIZE_IS(IMPRect, 16);

/* ISP tuning payloads the vendor T40 kernel copies */
SIZE_IS(IMPISPSENSORAttr, 20);
SIZE_IS(IMPISPGammaAttr, 264);
SIZE_IS(IMPISPCCMAttr, 44);
SIZE_IS(IMPISPModuleCtl, 4);
SIZE_IS(IMPISPModuleRatioAttr, 128);
SIZE_IS(IMPISPAutoZoom, 60);
SIZE_IS(IMPISPAEScenceAttr, 52);
SIZE_IS(IMPISPCoefftWb, 6);
SIZE_IS(IMPISPWdrOutputMode, 4);
SIZE_IS(IMPISPFrameDropAttr, 36);

/* IMP_Encoder_Set/GetChnSeiAttr (the vendor reads mode, uuid, userData at
 * 0/4/8 and the size byte at 12) and IMP_ISP_Get/SetDrawAttr */
SIZE_IS(IMPEncoderSeiAttr, 16);
OFF_IS(IMPEncoderSeiAttr, uuid, 4);
OFF_IS(IMPEncoderSeiAttr, userData, 8);
OFF_IS(IMPEncoderSeiAttr, userDataSize, 12);
SIZE_IS(IMPISPDrawAttr, 896);
SIZE_IS(IMPISPDrawUnitAttr, 28);

int t40_abi_layout_checked;
