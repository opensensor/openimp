/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * OpenIMP test pattern, runtime control (extension beyond the vendor IMP API).
 *
 * The test pattern replaces every captured NV12 frame (before the encoder
 * and, in mode 3, before IVS) with a synthetic picture, so the whole encode
 * path can be checked against a known image without a sensor:
 *
 *   mode 0  off (real sensor picture)
 *   mode 1  static pattern: grey ramp, checkerboard, colour bars, logo in
 *           the middle on a white quiet zone
 *   mode 2  pattern + moving black square + 16 bit frame counter (top left)
 *   mode 3  motion detection test: flat grey (light noise), logo, frame
 *           counter, objects of given size/contrast/speed, either one
 *           built-in object (obj_* fields) or a script file
 *
 * OpenIMP_TestPattern_Set may be called at any time, also while streaming;
 * the next frame uses the new state.  Environment OPENIMP_TEST_PATTERN=0..3
 * is only the start value.  A build that has this API exports the constant
 * OpenIMP_Cap_TestPattern (1).
 *
 * ABI: the structs start with a size field (the caller sets sizeof of ITS
 * struct).  New fields are only appended; Set copies min(size, own size)
 * bytes over the defaults, Get fills min(size, own size) bytes.
 */
#ifndef OPENIMP_TESTPATTERN_PUBLIC_H
#define OPENIMP_TESTPATTERN_PUBLIC_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OPENIMP_TESTPATTERN_ABI 1

#define OPENIMP_TP_OFF     0
#define OPENIMP_TP_PATTERN 1
#define OPENIMP_TP_MOVING  2
#define OPENIMP_TP_MOTION  3

#define OPENIMP_TP_LOGO_NONE 0
#define OPENIMP_TP_LOGO_OPENIMP 1       /* default: OpenIMP banner (dot grid + lettering) */

typedef struct OpenIMPTestPatternParams {
    uint32_t size;          /* sizeof(OpenIMPTestPatternParams) of the caller */
    uint32_t step;          /* mode 2: square speed in px/frame (even, >= 2), default 8 */
    uint32_t logo;          /* OPENIMP_TP_LOGO_*, default OpenIMP banner */
    int32_t  noise;         /* mode 3: peak noise in LSB per frame, 0 = none, default 1 */
    uint32_t obj_size;      /* mode 3 built-in object: edge in px of a 640 px wide frame
                               (scaled with the width), 0 = no built-in object */
    int32_t  obj_contrast;  /* added to Y inside the object (-200..200), default 40 */
    uint32_t obj_speed;     /* px/s of a 640 px wide frame, 0 = static, default 0 */
    uint32_t obj_cell_x;    /* grid cell 0..7 (8 x 6 grid), default 1 */
    uint32_t obj_cell_y;    /* 0..5, default 1 */
    uint32_t obj_on_ms;     /* visible for this long ... (default 3000) */
    uint32_t obj_period_ms; /* ... every period (0 = always visible), default 6000 */
    char     script[128];   /* mode 3 object script file (lines "t=<s> cell=<cx>,<cy> size=<px>
                               contrast=<dY> [dur=<s>] [speed=<px/s>]", time from this Set);
                               empty = OPENIMP_TEST_PATTERN_EVENTS or /tmp/openimp_tp_events */
} OpenIMPTestPatternParams;

typedef struct OpenIMPTestPatternState {
    uint32_t size;          /* sizeof(OpenIMPTestPatternState) of the caller */
    uint32_t abi;           /* OPENIMP_TESTPATTERN_ABI */
    int32_t  mode;          /* 0..3 */
    uint32_t reserved;
    uint64_t frames;        /* frames replaced since the last Set (all channels) */
    uint64_t objects_shown; /* mode 3: object appearances since the last Set */
    uint32_t objects_active;/* mode 3: objects visible right now */
    uint32_t pad;
    OpenIMPTestPatternParams params;   /* active parameters */
} OpenIMPTestPatternState;

/* fill p with the defaults (size set) */
static inline void OpenIMP_TestPattern_ParamsInit(OpenIMPTestPatternParams *p)
{
    memset(p, 0, sizeof *p);
    p->size = (uint32_t)sizeof *p;
    p->step = 8;
    p->logo = OPENIMP_TP_LOGO_OPENIMP;
    p->noise = 1;
    p->obj_contrast = 40;
    p->obj_cell_x = 1;
    p->obj_cell_y = 1;
    p->obj_on_ms = 3000;
    p->obj_period_ms = 6000;
}

/* Switch the pattern.  p == NULL keeps the current parameters.  Returns 0, or -1 with errno
 * EINVAL (mode, field range or size out of range).  Thread safe, takes no lock in the frame path. */
int OpenIMP_TestPattern_Set(int mode, const OpenIMPTestPatternParams *p);

/* Read the state back (s->size must be set).  Returns 0 or -1. */
int OpenIMP_TestPattern_Get(OpenIMPTestPatternState *s);

extern const int OpenIMP_Cap_TestPattern;   /* 1 */

#ifdef __cplusplus
}
#endif
#endif
