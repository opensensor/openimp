#ifndef OPENIMP_HSKIP_H
#define OPENIMP_HSKIP_H

/*
 * IMP_Encoder_SetChnHSkip reference structures of the OEM T10/T20 i264e
 * encoder (libimp 3.12.0): which picture is an IDR, which reconstruction
 * buffers a picture predicts from / is written to, and the slice header
 * commands that make a decoder see the same structure (long-term base
 * layer: reference list modification and MMCO).  Platform neutral C
 * (libc only); the decisions are checked against the OEM code by
 * tools/hskip_oracle.py (tests/hskip/hskip_vectors.txt).
 *
 * Skip types (IMPSkipType): 0 N1X (every picture a reference), 1 N2X and
 * 2 N4X (every 2nd / 4th picture is a long-term base picture, the others
 * drop-able enhancement pictures), 3/4 HN1, 5/6 H1M (base picture every m
 * pictures).  Frame types: 0 IDR, 1 LBASE (long-term base), 2 SBASE (short
 * term base), 3 ENHANCE.
 */

#include <stdint.h>

#define HSKIP_FT_IDR     0
#define HSKIP_FT_LBASE   1
#define HSKIP_FT_SBASE   2
#define HSKIP_FT_ENHANCE 3

#define HSKIP_MAX_TYPE   6

typedef struct {
    uint32_t counter;           /* picture counter of the state */
    int32_t ref;                /* reference buffer, -1 none */
    int32_t recon;              /* reconstruction buffer */
    uint32_t ftype;             /* HSKIP_FT_* */
    uint32_t flag;              /* long-term marking */
} HSkipRef;

typedef struct {
    /* the OEM skip header (i264e_init_skip_header) */
    uint32_t gop;               /* [0] */
    uint32_t same;              /* [1] maxSameSceneCnt */
    uint32_t type;              /* [2] skipType */
    uint32_t a, b;              /* [3] [4] base picture spacing */
    uint32_t buffers;           /* [5] reconstruction buffers used (2 or 3) */
    HSkipRef prev, cur;         /* the two states of i264e_decide_slice_type_and_rd */
    uint32_t counter;           /* ctx[0]: pictures since the IDR (-1: none yet) */
} HSkip;

/* what a picture needs (the OEM picture context after the decision) */
typedef struct {
    int idr;                    /* class 1 of the OEM (IDR picture) */
    uint32_t ftype;             /* HSKIP_FT_* */
    uint32_t counter;           /* pictures since the IDR (frame_num) */
    int32_t ref;                /* reference buffer, -1 for the IDR */
    int32_t recon;              /* reconstruction buffer */
    int ltr_flag;               /* IDR: long_term_reference_flag */
    int reorder;                /* P: ref_pic_list_modification, long term 0 */
    int mmco;                   /* P: 1 = {mark the previous picture unused (0), long term idx 0} */
} HSkipPic;

/* i264e_init_skip_header; m, n as IMPEncoderAttrHSkip, black not needed here */
void hskip_init(HSkip *s, uint32_t gop, uint32_t type, uint32_t m, uint32_t n, uint32_t same);

/* i264e_decide_slice_type_and_rd: the decision for the next picture.  force_idr:
 * an IDR was requested (or no picture was coded yet).  scene_idr: the T20 rule of
 * the OEM for the skip types N1X and H1M with maxSameSceneCnt > 0: the rate
 * controller (T20 only, not T10; E+RX(360) == 4, RCT20_Scene) reports a scene cut
 * and the picture opens a GOP, so it is an IDR although the IDR period is longer. */
void hskip_decide(HSkip *s, int force_idr, int scene_idr, HSkipPic *pic);

/* i264e_ratecontrol_start: does the picture carry the HSkip class the rate controller is told about
 * (RCT20_HSkipClass / RCT10_HSkipClass)? */
/* t10: the T10 branch of the OEM controller call has no case for the skip type N1X. */
int hskip_rc_cond(const HSkip *s, const HSkipPic *pic, int t10);

/* Number of reconstruction buffers a skip type needs (2 for N1X/N2X). */
uint32_t hskip_buffers(uint32_t type);

/* BlackEnhance (src/hskip/hskip_black.c): slice data of `mbs` skipped macroblocks;
 * ctx_state: preCtxState of the P slice context 11 (h264_cabac_context_init, QP 0) */
#include <stddef.h>
size_t hskip_black_slice_data(uint8_t *out, size_t cap, uint32_t mbs, uint8_t ctx_state);

#endif
