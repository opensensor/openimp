#ifndef OPENIMP_EPRC_H
#define OPENIMP_EPRC_H

/*
 * Picture rate control of the Ingenic Helix H.264 encoders (T21, T23).
 *
 * The OEM libimp runs i264e on top of a rate controller whose entry points
 * are JZ_VPU_RC_VIDEO_CFG_T21, JZ_VPU_RC_FRAME_START_T21 and
 * JZ_VPU_RC_FRAME_END_T21 ("eprc", see docs/T23_EPRC.md).  This module is a
 * reimplementation from the T23 1.3.0 disassembly.  It is platform neutral:
 * no hardware access, no OS calls besides malloc/free and libm.  The caller
 * feeds it one picture at a time:
 *
 *   EPRC_FrameStart()  ->  picture type, QP and the per-picture hardware
 *                          rate-control fields (EprcPicture)
 *   (encode)
 *   EPRC_FrameEnd()    <-  coded size and the Helix statistics registers
 *
 * State is kept in the OEM memory layout (byte offsets as in the
 * disassembly) so that it can be compared with the OEM controller run under
 * an emulator; docs/T23_EPRC.md names the fields that are understood.
 */

#include <stddef.h>
#include <stdint.h>

#define EPRC_MODE_CQP   0
#define EPRC_MODE_CBR   1
#define EPRC_MODE_VBR   2
#define EPRC_MODE_SMART 3

#define EPRC_GOP_NORMALP 0
#define EPRC_GOP_SMARTP  1

/* Size of the OEM "rc" block that starts at i264e rc + 496. */
#define EPRC_E_SIZE 1840u
/* Size of the OEM slice-parameter block (the H264E_T21_SliceInit input). */
#define EPRC_SLICE_SIZE 0x1200u

/* The i264e parameters the OEM i264e_ratecontrol_init reads (offsets in
 * the OEM i264e parameter block in brackets). */
typedef struct {
    uint32_t width, height;     /* [56] [60] */
    uint32_t rc_mode;           /* [200] EPRC_MODE_* */
    uint32_t gop;               /* [44] */
    uint32_t fps_num, fps_den;  /* [408] [412] */
    uint32_t min_qp, max_qp;    /* [208] [212] */
    uint32_t bitrate;           /* [228] kbit/s, CBR */
    uint32_t max_bitrate;       /* [260] kbit/s, VBR/SMART */
    int32_t i_bias;             /* [240] iBiasLvl */
    uint32_t frm_qp_step;       /* [244] */
    uint32_t gop_qp_step;       /* [248] */
    uint32_t static_time;       /* [256] */
    uint32_t change_pos;        /* [264] */
    uint32_t quality;           /* [268] */
    uint32_t cqp;               /* [204] FIXQP QP */
    uint32_t bg_interval_gops;  /* [2756] SMART background interval in GOPs */
    int32_t init_qp;            /* i264e_ratecontrol_init a1; < 0: none */
    uint32_t field52;           /* [52] */
} EprcParams;

typedef struct Eprc {
    uint8_t e[EPRC_E_SIZE];     /* OEM rc + 496 */
    uint8_t *p;                 /* OEM state block (rc + 496 + 1620) */
    uint32_t p_size;
    /* the arrays the OEM keeps behind the state block */
    uint8_t *a1628, *a1632, *a1636, *a1640, *a304, *a7140, *a7144, *a7148;
    uint8_t *slice;             /* OEM slice-parameter block */
} Eprc;

/* i264e_ratecontrol_init + eprc_default_set_T21 + JZ_VPU_RC_VIDEO_CFG_T21.
 * Returns 0 on success.  The slice block (EPRC_SLICE_SIZE bytes, zeroed by
 * the caller once) receives the hardware fields at each FrameStart. */
int EPRC_Init(Eprc *rc, const EprcParams *params, uint8_t *slice);
void EPRC_Free(Eprc *rc);

/* Internal steps, exposed for the emulator comparison (tests). */
void EPRC_DefaultSet(uint8_t *e);
void EPRC_SetupE(uint8_t *e, const EprcParams *params);
int EPRC_VideoCfg(Eprc *rc);
void EPRC_Layout(Eprc *rc, uint8_t *block);

#endif
