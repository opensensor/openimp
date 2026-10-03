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
    /* OpenIMP extra, not OEM (0 = OEM): a P picture following a P picture
     * gets a QP at most this much below the previous picture's coded QP
     * (stops the static-scene QP sawtooth; rises are not limited). */
    uint32_t qp_down_max;
} EprcParams;

/* Per-picture inputs (i264e_ratecontrol_start, 0x420e8). */
typedef struct {
    uint32_t frames_since_idr;  /* 0: IDR picture */
    uint32_t gop_ctrl;          /* i264e rc+2768 (0 by default) */
    uint32_t r2820;             /* i264e rc+2820 (0 by default) */
    uint32_t r11832;            /* i264e rc+11832 (0 by default) */
    uint32_t pic[7];            /* i264e picture +392..+404 (0 by default) */
    /* T23: the ISP AE zone statistics of the picture (IMPISPZone, 15 x 15
     * words, IMP_ISP_Tuning_GetAeZone), which the OEM VBMGetFrame attaches
     * to the frames of frame source channel 0 (i264e picture +0x1e0/+0x1e4,
     * eprc E+641/E+672).  NULL: none (other channels, the T21 revision). */
    const uint32_t *ae_zone;
} EprcFrameIn;

#define EPRC_AE_ZONES 225u

/* What the picture is coded with. */
typedef struct {
    int32_t type;               /* 2: IDR, 6: SMART GOP-start P, 0: P */
    uint8_t qp;
    uint8_t qp_max, qp_min;     /* macroblock QP window (0x40040, 0x40074) */
    uint16_t lambda[3];         /* 0xb001c, 0xb0020 */
} EprcPicture;

typedef struct Eprc {
    uint8_t *e;                 /* OEM rc + 496 (e_store) */
    uint8_t *p;                 /* OEM state block (rc + 496 + 1620) */
    uint32_t p_size;
    /* the arrays the OEM keeps behind the state block */
    uint8_t *a1628, *a1632, *a1636, *a1640, *a304, *a7140, *a7144, *a7148;
    uint8_t *slice;             /* OEM slice-parameter block */
    uint8_t e_store[EPRC_E_SIZE];
    /* OpenIMP extra state (EprcParams.qp_down_max) */
    uint32_t qp_down_max;
    int32_t cur_type, prev_type, prev_qp;
} Eprc;

/* i264e_ratecontrol_init + eprc_default_set_T21 + JZ_VPU_RC_VIDEO_CFG_T21.
 * Returns 0 on success.  The slice block (EPRC_SLICE_SIZE bytes, zeroed by
 * the caller once) receives the hardware fields at each FrameStart. */
int EPRC_Init(Eprc *rc, const EprcParams *params, uint8_t *slice);
void EPRC_Free(Eprc *rc);

int EPRC_FrameStart(Eprc *rc, const EprcFrameIn *in, EprcPicture *pic);

/* Helix status registers read after each picture, in this order (OEM table
 * at 0xd7a40, T21 0xa7aa0, read through soc_vpu ioctl 0xc0586307 at
 * 0x13100000 + off, T21 0xc0386307 at 0x13200000 + off):
 * 0x80120..0x8014c (12), 0x500e8, 0x500ec, 0x500f0, 0x80080, 0x40094,
 * 0x40098, 0x4009c, 0x400a0, 0x800e8, 0x800ec, 0x800e4, 0x800e0, 0x80168. */
#define EPRC_STAT_REGS 25
extern const uint32_t EPRC_StatRegs[EPRC_STAT_REGS];

/* Returns 1 when the picture is to be coded again with pic (OEM
 * FRAME_REPEATE_JUDGE), else 0. */
int EPRC_FrameEnd(Eprc *rc, uint32_t bytes, const uint32_t regs[EPRC_STAT_REGS],
                  EprcPicture *pic);
/* EPRC_FrameEnd; may_repeat 0 skips FRAME_REPEATE_JUDGE (as the OEM
 * i264e_ratecontrol_is_reenc does when re-encoding is not enabled) and
 * always finishes the picture: for a caller that cannot code it again. */
int EPRC_FrameEndEx(Eprc *rc, uint32_t bytes,
                    const uint32_t regs[EPRC_STAT_REGS], EprcPicture *pic,
                    int may_repeat);
void EPRC_GopInit(Eprc *rc);

/* The picture class (0..6, 5: scene change) the last EPRC_FrameStart
 * reported to i264e (T23 E+1596, T21 E+1592), which
 * i264e_decide_slice_type_and_rd reads for the next picture's scene-cut
 * IDR. */
int32_t EPRC_PictureClass(const Eprc *rc);
int32_t EPRC21_PictureClass(const Eprc *rc);

/* Internal steps, exposed for the emulator comparison (tests). */
void EPRC_DefaultSet(uint8_t *e);
void EPRC_SetupE(uint8_t *e, const EprcParams *params);
int EPRC_VideoCfg(Eprc *rc);
void EPRC_Layout(Eprc *rc, uint8_t *block);

/* The T21 1.0.33 revision of the controller (eprc_t21.c, docs/T23_EPRC.md
 * "Other SoCs"): same interface, the T21 OEM state layout and decisions.
 * The slice block is the T21 H264E_T21_SliceInit input. */
int EPRC21_Init(Eprc *rc, const EprcParams *params, uint8_t *slice);
void EPRC21_Free(Eprc *rc);
int EPRC21_FrameStart(Eprc *rc, const EprcFrameIn *in, EprcPicture *pic);
int EPRC21_FrameEnd(Eprc *rc, uint32_t bytes, const uint32_t regs[EPRC_STAT_REGS],
                    EprcPicture *pic);
int EPRC21_FrameEndEx(Eprc *rc, uint32_t bytes,
                      const uint32_t regs[EPRC_STAT_REGS], EprcPicture *pic,
                      int may_repeat);

#endif
