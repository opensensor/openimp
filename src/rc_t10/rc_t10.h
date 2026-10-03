#ifndef OPENIMP_RC_T10_H
#define OPENIMP_RC_T10_H

/*
 * Rate control of the Ingenic T10 H.264 encoder: the T20 3.12.0 build of
 * the OEM libimp runs JZ_VPU_RC_VIDEO_CFG / JZ_VPU_RC_FRAME_RC /
 * JZ_VPU_RC_FRAME_REPEATE_JUDGE with the exported RC_H264_* helpers
 * (0x9eec0..0xa2b2c) when get_cpu_id reports a T10 (docs/T20_RC.md, "T10").
 *
 * Platform neutral (libc and libm only), in the OEM block layout:
 *   E  "h->common.eprc" (i264e parameter + 476), RCT10_E_SIZE bytes;
 *   P  rcPara (RCT10_P_SIZE), S  rcSt (RCT10_S_SIZE).
 */

#include <stddef.h>
#include <stdint.h>

#define RCT10_E_SIZE 232u
#define RCT10_P_SIZE 88u
#define RCT10_S_SIZE 808u

/* The i264e parameters i264e_ratecontrol_init (0x3ab38, T10 branch)
 * reads; method in the i264e numbering (0 FIXQP, 1 CBR, 2 VBR, 3 SMART). */
typedef struct {
    uint32_t method;            /* [188] */
    uint32_t width, height;     /* [44] [48] */
    uint32_t gop;               /* [40] */
    uint32_t fps_num, fps_den;  /* [396] [400] */
    uint32_t qp;                /* [192] FIXQP */
    uint32_t min_qp, max_qp;    /* [196] [200] */
    uint32_t bitrate;           /* [216] CBR kbit/s */
    int32_t i_bias;             /* [228] */
    uint32_t frm_qp_step;       /* [232] */
    uint32_t gop_qp_step;       /* [236] */
    uint32_t gop_relation;      /* [241] */
    uint32_t static_time;       /* [244] */
    uint32_t max_bitrate;       /* [248] VBR/SMART kbit/s */
    uint32_t change_pos;        /* [252] */
    uint32_t quality;           /* [256] */
    float new_max_qp_trig;      /* [260] (default 3.0) */
    uint32_t new_max_qp;        /* [264] (default 51) */
    uint32_t mb_rc2;            /* [272] (default 1) */
    int32_t super_i_bits;       /* [280] */
    int32_t super_p_bits;       /* [284] */
    float ip_factor;            /* [208] (default 1.4) */
} RcT10Params;

void RCT10_DefaultParams(RcT10Params *params);

typedef struct {
    uint32_t cmpx;              /* soc_vpu channel node +44 */
    uint32_t bits;              /* coded size in bits */
} RcT10Stats;

typedef struct {
    int idr;
    uint8_t qp;
} RcT10Picture;

typedef struct RcT10 {
    uint8_t e[RCT10_E_SIZE];
    uint8_t p[RCT10_P_SIZE];
    uint8_t s[RCT10_S_SIZE];
    RcT10Params params;
    uint32_t frames;
    RcT10Stats last;
} RcT10;

int RCT10_Init(RcT10 *rc, const RcT10Params *params);
void RCT10_Start(RcT10 *rc, int idr, RcT10Picture *pic);
/* Returns 1 when the picture is to be coded again with pic->qp. */
int RCT10_End(RcT10 *rc, const RcT10Stats *stats, RcT10Picture *pic);

/* ---- OEM entry points (byte blocks in the OEM layout) ---- */
void RCT10_SetupE(uint8_t *E, const RcT10Params *params);
void RCT10_VideoCfg(uint8_t *E, uint8_t *P, uint8_t *S);
void RCT10_FrameRc(uint8_t *E, uint8_t *P, uint8_t *S);
int RCT10_RepeatJudge(uint8_t *E, uint8_t *P, uint8_t *S);

#endif
