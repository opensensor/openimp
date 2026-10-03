#ifndef OPENIMP_T31_AL_RC_H
#define OPENIMP_T31_AL_RC_H

/*
 * T31 Allegro rate controller (OEM libimp 1.1.6, lib_rate_ctrl/RateCtrl_21.c,
 * functions 0x50860..0x56bf0), ported instruction for instruction for the
 * CBR (AL_RateCtrl mode 0, update IIii 0x53360), VBR (mode 1, OOoI),
 * CappedVBR (mode 8, Ooii) and CappedQuality (mode 9, Ooii) controllers.
 *
 * The state block keeps the OEM layout (328 bytes, offsets in the comments)
 * so that a run can be compared byte for byte with the OEM code under
 * emulation (tools/t31_rc_emu, tests/t31/al_rc_trace_test.c).
 */
#include <stdint.h>

/* AL_TRCParam as the T31 libimp fills it (channel param +0x68). */
typedef struct T31AlRcParam {
    uint32_t mode;          /* +0  AL eRCMode: 1 CBR, 2 VBR, 4 CappedVBR, 8 CappedQuality */
    uint32_t initial_rem_delay; /* +4  90 kHz ticks (T31 default 216000) */
    uint32_t cpb_size;      /* +8  90 kHz ticks (T31 default 270000), >= initial_rem_delay */
    uint16_t frame_rate;    /* +12 reduced frame-rate numerator */
    uint16_t clk_ratio;     /* +14 reduced denominator * 1000 */
    uint32_t target_bitrate; /* +16 bit/s */
    uint32_t max_bitrate;   /* +20 bit/s, >= target */
    int16_t initial_qp;     /* +24 */
    int16_t min_qp;         /* +26 */
    int16_t max_qp;         /* +28 */
    int16_t ip_delta;       /* +30 (-1 = automatic) */
    int16_t pb_delta;       /* +32 (-1 = automatic) */
    uint8_t flag34;         /* +34 (T31 IMP: 0) */
    uint8_t pad35;
    int16_t field36;        /* +36 */
    uint16_t field38;       /* +38 */
    uint32_t options;       /* +40 eRcOptions */
    uint32_t num_pixels;    /* +44 */
    uint16_t max_psnr_x100; /* +48 */
    uint16_t max_pel;       /* +50 */
    uint32_t field52;
    uint32_t field56;
    uint32_t max_picture_size; /* +60 bits */
} T31AlRcParam;

/* AL_TGopParam, the first 28 bytes are copied into the state. */
typedef struct T31AlGopParam {
    uint32_t mode;          /* +0  (T31 IMP: 2) */
    uint16_t length;        /* +4 */
    uint8_t num_b;          /* +6 */
    uint8_t field7;         /* +7 */
    uint32_t rest[5];
} T31AlGopParam;

/* HRD / leaky bucket object (state +72, 64 bytes). */
typedef struct T31AlRcHrd {
    uint32_t cpb_bits;      /* +0  cpb_size * max_bitrate / 90000 */
    uint32_t init_delay;    /* +4  ticks */
    uint32_t clk_ratio;     /* +8 */
    uint32_t fps_1000;      /* +12 frame_rate * 1000 */
    uint32_t max_bitrate;   /* +16 */
    uint8_t is_cbr;         /* +20 */
    uint8_t strict;         /* +21 (mode != 9): removal clock never slips */
    uint8_t pad22[2];
    uint32_t arrival;       /* +24 ticks */
    uint32_t arrival_rem;   /* +28 remainder mod max_bitrate */
    uint32_t removal;       /* +32 ticks */
    uint32_t removal_rem;   /* +36 remainder mod fps_1000 */
    uint32_t seconds;       /* +40 rebased whole seconds */
    uint32_t pad44;
    uint32_t total_bits_lo; /* +48 */
    uint32_t total_bits_hi; /* +52 */
    uint32_t pictures;      /* +56 */
    uint32_t idle_ticks;    /* +60 channel idle time (VBR) */
} T31AlRcHrd;

typedef struct T31AlRcState {
    uint8_t needs_init;     /* +0   1 after construction, 0 once initialised */
    uint8_t pad1[3];
    uint32_t fps_1000;      /* +4 */
    uint32_t clk_ratio;     /* +8 */
    uint32_t num_pixels;    /* +12 */
    uint32_t max_psnr;      /* +16 */
    uint32_t max_pel;       /* +20 */
    int16_t min_qp;         /* +24 */
    int16_t max_qp;         /* +26 */
    int16_t p_qp;           /* +28 initial QP (+ IP delta for gop >= 2) */
    int16_t i_qp_ref;       /* +30 initial QP as given */
    int16_t qp;             /* +32 current QP */
    int16_t max_delta;      /* +34 (4) */
    int16_t qp_sync;        /* +36 */
    uint8_t pad38[2];
    uint32_t gop_mode;      /* +40 */
    uint16_t gop_length;    /* +44 */
    uint8_t num_b;          /* +46 */
    uint8_t gop7;           /* +47 */
    uint32_t gop_rest[5];   /* +48..67 */
    uint32_t pad68;
    T31AlRcHrd hrd;         /* +72..135 */
    uint32_t init_level;    /* +136 */
    uint32_t target_frame;  /* +140 target bits per picture */
    uint32_t f144;          /* +144 */
    uint32_t max_frame;     /* +148 max bits per picture */
    uint32_t f152;          /* +152 */
    uint32_t target_bitrate; /* +156 */
    uint32_t max_bitrate;   /* +160 */
    int32_t ip_delta;       /* +164 */
    int32_t pb_delta;       /* +168 */
    int32_t f172;           /* +172 */
    uint32_t ratio_i;       /* +176 I/P size ratio * 1000 (or *10000) */
    uint32_t ratio_b;       /* +180 */
    uint32_t ratio_3;       /* +184 */
    uint32_t f188;          /* +188 */
    int32_t last_type;      /* +192 */
    int32_t prev_type;      /* +196 */
    uint32_t model_size[4]; /* +200..215 per picture type */
    uint32_t model_qp[4];   /* +216..231 */
    int32_t f232;           /* +232 */
    uint32_t f236;          /* +236 */
    int32_t p20_ref;        /* +240 */
    uint32_t last_size;     /* +244 */
    uint32_t step_ratio[4]; /* +248..263 one-QP size ratio * 10000 */
    int32_t gop_pictures;   /* +264 */
    int32_t type_count[3];  /* +268..279 */
    uint8_t opt_bit0;       /* +280 */
    uint8_t f281;           /* +281 */
    uint8_t pad282[2];
    int32_t f284;           /* +284 */
    int32_t f288;           /* +288 */
    int32_t f292;           /* +292 */
    int32_t f296;           /* +296 */
    uint8_t flag300;        /* +300 (0 on T31) */
    uint8_t opt_flag;       /* +301 !((options & 5) == 1) */
    uint8_t auto_ip;        /* +302 */
    uint8_t capped_vbr;     /* +303 1 CappedVBR/VBR, 0 CappedQuality */
    int32_t step;           /* +304 (1) */
    int32_t f308;           /* +308 */
    int32_t f312;           /* +312 */
    int32_t f316;           /* +316 */
    uint8_t flag320;        /* +320 */
    uint8_t flag321;        /* +321 */
    uint8_t opt_bit1;       /* +322 */
    uint8_t opt_bit4;       /* +323 */
    uint8_t pad324[4];
} T31AlRcState;

/* Encoding status fields the controller reads (OEM slice-status block). */
typedef struct T31AlRcStatus {
    uint32_t bits;          /* +4   coded size in bits (max of the two sizes) */
    uint32_t stat20;        /* +20  <- status reg 0x10c */
    uint32_t stat24;        /* +24  <- 0x110 */
    uint32_t stat28;        /* +28  <- 0x114 */
    uint32_t stat32;        /* +32  <- 0x11c */
    uint32_t stat36;        /* +36  <- 0x120 */
    uint32_t stat40;        /* +40  <- 0x124 */
    uint32_t stat44;        /* +44  <- 0x128 */
    uint32_t stat48;        /* +48  <- 0x12c (16 bit) */
    int16_t qp;             /* +60  picture QP */
    uint32_t sse_lo;        /* +96  <- 0x15c */
    uint32_t sse_hi;        /* +100 <- 0x158 */
} T31AlRcStatus;

/* Picture descriptor (OEM frame +32). */
typedef struct T31AlRcPicture {
    uint32_t flags;         /* +4  bit0 IDR, bit1 regular, bit2 scene change, bit7 */
    uint32_t type;          /* +16 0 B, 1 P, 2 I, 3 special, 7 skip */
    int8_t qp_offset;       /* +37 */
    uint8_t fixed_qp;       /* +44 */
    int8_t forced_qp;       /* +45 */
} T31AlRcPicture;

typedef struct T31AlRc {
    T31AlRcState st;
    uint32_t mode;          /* AL_RateCtrl_Init mode */
    int valid;
} T31AlRc;

/* AL_RateCtrl_Init + lI1i: construct the controller for mode 0/1/8/9 and
 * initialise it from the parameters.  Returns 0, -1 on bad mode. */
int t31_al_rc_init(T31AlRc *rc, uint32_t mode, const T31AlRcParam *rcp,
                   const T31AlGopParam *gop);
/* lI1i on an initialised controller (parameter change). */
void t31_al_rc_set_params(T31AlRc *rc, const T31AlRcParam *rcp,
                          const T31AlGopParam *gop);
/* il0i: reset of the run state (no-op before init). */
void t31_al_rc_reset(T31AlRc *rc);
/* Il1i: QP for the picture about to be encoded. */
int16_t t31_al_rc_picture_qp(T31AlRc *rc, const T31AlRcPicture *pic);
/* o11i: picture start bookkeeping after encoding; returns the filler value
 * (0 or -1 for the ported modes). */
int32_t t31_al_rc_picture_start(T31AlRc *rc, const T31AlRcPicture *pic,
                                const T31AlRcStatus *status, uint32_t size_bits);
/* IIii / OOoI / Ooii: the per-picture update.  overflow = frame re-encode flag,
 * extra_bits = filler bits added to the picture size. */
void t31_al_rc_update(T31AlRc *rc, const T31AlRcPicture *pic,
                      const T31AlRcStatus *status, uint32_t size_bits,
                      uint8_t overflow, uint32_t extra_bits);
/* PSNR (dB * 100) exactly as Ooii computes it. */
int32_t t31_al_rc_psnr_x100(uint64_t sse, uint32_t num_pixels, uint32_t max_pel);
/* Fill a status block from the AVPU status register image (EncodingStatus-
 * RegsToSliceStatus 0x6d840); bits and qp are set by the caller. */
void t31_al_rc_status_from_regs(T31AlRcStatus *status, const uint8_t *regs,
                                unsigned int regs_len);
/* c_reduce_fraction + the IMP conversion of fps num/den. */
void t31_al_rc_frame_rate(uint32_t num, uint32_t den, uint16_t *frame_rate,
                          uint16_t *clk_ratio);

#endif
