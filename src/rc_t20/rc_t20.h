#ifndef OPENIMP_RC_T20_H
#define OPENIMP_RC_T20_H

/*
 * Rate control of the Ingenic T20 H.264 encoder (OEM libimp 3.12.0,
 * JZ_VPU_RC_VIDEO_CFG_T20 / JZ_VPU_RC_FRAME_RC_T20 /
 * JZ_VPU_RC_FRAME_REPEATE_JUDGE_T20, see docs/T20_RC.md).  T20 only: the
 * T10 build of the same library runs a different controller.
 *
 * Platform neutral (no hardware access; libc and libm only).  The state is
 * kept in the three OEM blocks so that every step can be compared with the
 * OEM code under an emulator:
 *
 *   E  the "eprc_t20" block (i264e parameter + 704), RCT20_E_SIZE bytes:
 *      configuration, per-picture inputs/outputs and the macroblock maps;
 *   P  rcPara (RCT20_P_SIZE), S  rcSt (RCT20_S_SIZE).
 *
 * Per picture the caller runs RCT20_Start() (type and QP of the picture
 * from the statistics of the previous one), codes the picture, then
 * RCT20_End() with its size; when that returns 1 the picture is coded
 * again with the new QP and RCT20_End() runs again.
 */

#include <stddef.h>
#include <stdint.h>

#define RCT20_E_SIZE (0x90000u + 400u)
#define RCT20_P_SIZE 128u
#define RCT20_S_SIZE 872u

/* OEM rate control modes (E+0) */
#define RCT20_MODE_CBR   0
#define RCT20_MODE_VBR   1
#define RCT20_MODE_FIXQP 2
#define RCT20_MODE_SMART 3

/* Hardware statistics registers read after each picture, at 0x13200000 +
 * offset (OEM i264e_ratecontrol_priv_init 0x3a890, via soc_vpu ioctl
 * 0xc0386307). */
#define RCT20_STAT_REG_COUNT 3
extern const uint32_t RCT20_StatRegs[RCT20_STAT_REG_COUNT];

/* The i264e parameters i264e_ratecontrol_init (0x3ab38) reads, already
 * clamped by i264e_validate_parameters.  The method uses the i264e
 * numbering (0 FIXQP, 1 CBR, 2 VBR, 3 SMART). */
typedef struct {
    uint32_t method;            /* [188] */
    uint32_t width, height;     /* [44] [48] */
    uint32_t gop;               /* [40] */
    uint32_t fps_num, fps_den;  /* [396] [400] */
    uint32_t qp;                /* [192] FIXQP: P QP, I QP = qp - 3 */
    uint32_t min_qp, max_qp;    /* [196] [200] */
    uint32_t bitrate;           /* [216] CBR kbit/s */
    int32_t i_bias;             /* [228] iBiasLvl */
    uint32_t frm_qp_step;       /* [232] */
    uint32_t gop_qp_step;       /* [236] */
    uint32_t gop_relation;      /* [241] */
    uint32_t static_time;       /* [244] VBR/SMART (stored, not used) */
    uint32_t max_bitrate;       /* [248] VBR/SMART kbit/s */
    uint32_t change_pos;        /* [252] */
    uint32_t quality;           /* [256] */
    float new_max_qp_trig;      /* [260] (default 3.0) */
    uint32_t new_max_qp;        /* [264] (default 51) */
    uint32_t mb_rc;             /* [268] (default 1) */
    uint32_t mb_rc2;            /* [272] (default 1) */
    int32_t super_i_bits;       /* [280] (default 19660800) */
    int32_t super_p_bits;       /* [284] (default 14043429) */
} RcT20Params;

void RCT20_DefaultParams(RcT20Params *params);

/* Statistics of a coded picture (OEM i264e_ratecontrol_end 0x3d1c0). */
typedef struct {
    uint32_t cmpx;              /* soc_vpu channel node +44 */
    uint32_t bits;              /* coded size in bits */
    uint32_t reg[RCT20_STAT_REG_COUNT];
} RcT20Stats;

typedef struct RcT20 {
    uint8_t *e;                 /* RCT20_E_SIZE bytes (allocated by Init) */
    uint8_t p[RCT20_P_SIZE];
    uint8_t s[RCT20_S_SIZE];
    RcT20Params params;
    uint32_t frames;            /* pictures started */
    RcT20Stats last;            /* statistics of the last coded picture */
} RcT20;

/* Picture decision. */
typedef struct {
    int idr;                    /* 1: I (IDR) picture */
    uint8_t qp;
} RcT20Picture;

/* rc->e may point to RCT20_E_SIZE bytes to reuse, else NULL. */
int RCT20_Init(RcT20 *rc, const RcT20Params *params);
void RCT20_Free(RcT20 *rc);

/* idr: the GOP position says IDR.  luma/stride: the picture's luma plane
 * (macroblock rate control reads it; may be NULL when E+215 is 0). */
void RCT20_Start(RcT20 *rc, int idr, const uint8_t *luma, uint32_t stride,
                 RcT20Picture *pic);
/* Returns 1 when the picture is to be coded again with pic->qp. */
int RCT20_End(RcT20 *rc, const RcT20Stats *stats, RcT20Picture *pic);

/* ---- OEM entry points and steps (byte blocks in the OEM layout) ---- */
void RCT20_DefaultSet(uint8_t *E);
void RCT20_SetupE(uint8_t *E, const RcT20Params *params);
void RCT20_VideoCfg(uint8_t *E, uint8_t *P, uint8_t *S);
/* luma: the picture's luma plane (E+220 in the OEM layout holds its 32-bit
 * address; only the macroblock rate control reads it). */
void RCT20_FrameRc(uint8_t *E, uint8_t *P, uint8_t *S, const uint8_t *luma);
int RCT20_RepeatJudge(uint8_t *E, uint8_t *P, uint8_t *S);

int32_t RCT20_UpdateQp(double x);
void RCT20_CheckScene(uint8_t *P, uint8_t *S);
void RCT20_QpLimit(uint8_t *P, uint8_t *S);
void RCT20_UpdateMaxQp(uint8_t *P, uint8_t *S, int32_t bitrate);
int32_t RCT20_CalcOtherIQp(uint8_t *S);
void RCT20_CalcIFrameQp(uint8_t *P, uint8_t *S);
void RCT20_CalcFirstIQp(uint8_t *P, uint8_t *S, int32_t bits);
void RCT20_CalcPFrameQp(uint8_t *P, uint8_t *S);
void RCT20_Frames(uint8_t *P, uint8_t *S);
void RCT20_RDModel(int32_t n, uint8_t *S);
void RCT20_MADModel(int32_t n, uint8_t *S);
void RCT20_UpdateModelCoeff(uint8_t *P, uint8_t *S);
void RCT20_CbrUpdateGopInfo(uint8_t *P, uint8_t *S);
void RCT20_VbrUpdateGopInfo(uint8_t *P, uint8_t *S);

/* Macroblock rate control (rc_t20_mb.c): luma activity classes, the
 * per-macroblock QP map and its hardware table. */
void RCT20_CalMBFlag(uint8_t *E, const uint8_t *luma);
void RCT20_CalMBQP(uint8_t *E);
void RCT20_MBQpReencode(uint8_t *E);
int32_t RCT20_QPTabConv(const uint8_t *qp, int32_t n, uint8_t *tab);

#endif
