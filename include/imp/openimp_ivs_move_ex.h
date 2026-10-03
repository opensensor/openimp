/* OpenIMP beyond-vendor extension of the IVS "move" interface.
 *
 * Opt-in motion analysis ("motion v2") that runs next to the vendor
 * frame-difference algorithm of IMP_IVS_CreateMoveInterface():
 *
 *   - a background model per grid cell (running mean + running noise level,
 *     global brightness compensation) instead of a plain frame difference;
 *   - suppression for a few seconds after a day/night (IR) switch, a large
 *     sensor gain jump or a global brightness jump;
 *   - grouping of moving cells into objects with a minimum size and a
 *     minimum persistence;
 *   - bounding boxes and a strength per object.
 *
 * Nothing changes unless it is switched on, either with the environment
 * (OPENIMP_MOTION_V2=1, read when the move interface is created) or with
 * OpenIMP_IVS_MoveSetConfigEx(). With every feature off the vendor
 * IMP_IVS_MoveOutput results are bit-identical to an OpenIMP without this
 * extension. IMP_IVS_MoveOutput itself is never changed.
 *
 * ABI rules (stable, versioned):
 *   - every struct starts with `size` and `version`; the caller sets size to
 *     sizeof() of the struct it was compiled with and version to
 *     OPENIMP_IVS_MOVE_EX_VERSION;
 *   - the library reads/writes at most min(size, its own sizeof) bytes and
 *     sets `version` in outputs to the version it implements;
 *   - fields are only ever appended; existing fields keep offset and meaning;
 *   - streamers should reference the functions as weak symbols and fall
 *     back to the vendor API when they are NULL (vendor libimp, older
 *     OpenIMP).
 */
#ifndef OPENIMP_IVS_MOVE_EX_H
#define OPENIMP_IVS_MOVE_EX_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OPENIMP_IVS_MOVE_EX_VERSION 1

/* OpenIMP_IVS_MoveConfigEx.features */
#define OPENIMP_MOVE_F_BACKGROUND 0x1u  /* per-cell background model */
#define OPENIMP_MOVE_F_SUPPRESS   0x2u  /* hold-off after IR / gain / luma jumps */
#define OPENIMP_MOVE_F_BLOBS      0x4u  /* objects: min size + min persistence */
#define OPENIMP_MOVE_F_OVERRIDE   0x8u  /* IMP_IVS_MoveOutput.retRoi[] from v2 */
#define OPENIMP_MOVE_F_ALL        0xfu

typedef struct {
    uint32_t size;          /* sizeof(OpenIMP_IVS_MoveConfigEx) of the caller */
    uint32_t version;       /* OPENIMP_IVS_MOVE_EX_VERSION */
    uint32_t features;      /* OPENIMP_MOVE_F_*; 0 = vendor behaviour only */
    int32_t learn_shift;    /* background learning rate 1/2^n, 1..10 (4) */
    int32_t thresh_k;       /* cell threshold = k/16 x cell noise, 16..255 (48) */
    int32_t min_delta;      /* minimum cell mean change, luma levels 1..64 (10) */
    int32_t suppress_ms;    /* hold-off after a switch / jump, 0..60000 (4000) */
    int32_t jump_pct;       /* gain / brightness jump that suppresses, 5..90 (25) */
    int32_t min_cells;      /* minimum object size in grid cells, 1..64 (3) */
    int32_t min_frames;     /* minimum object persistence, analysed frames 1..30 (2) */
    int32_t reserved[6];    /* 0 */
} OpenIMP_IVS_MoveConfigEx;

#define OPENIMP_IVS_MOVE_EX_MAX_OBJ 16

typedef struct {
    int16_t x0, y0, x1, y1; /* inclusive, pixels of the IVS input frame
                               (frame_w x frame_h, the bound stream) */
    uint16_t strength;      /* 0..1000, grows with area and contrast */
    uint16_t cells;         /* moving grid cells of the object */
    uint16_t age;           /* consecutive analysed frames it was seen */
    uint16_t id;            /* track id, 1..65535, stable while tracked */
} OpenIMP_IVS_MoveObject;

/* OpenIMP_IVS_MoveOutputEx.flags */
#define OPENIMP_MOVE_EX_ACTIVE     0x1u /* v2 analysis is running */
#define OPENIMP_MOVE_EX_SUPPRESSED 0x2u /* inside a hold-off window */
#define OPENIMP_MOVE_EX_OVERRIDE   0x4u /* retRoi[] of this result came from v2 */
#define OPENIMP_MOVE_EX_WARMUP     0x8u /* background still being learned */

/* OpenIMP_IVS_MoveOutputEx.suppress */
#define OPENIMP_MOVE_SUPP_DAYNIGHT 0x1u /* ISP running mode changed */
#define OPENIMP_MOVE_SUPP_GAIN     0x2u /* sensor total gain jumped */
#define OPENIMP_MOVE_SUPP_LUMA     0x4u /* global brightness jumped */

typedef struct {
    uint32_t size;          /* in: sizeof() of the caller; out: bytes written */
    uint32_t version;       /* out: OPENIMP_IVS_MOVE_EX_VERSION of the library */
    int64_t timestamp;      /* frame timestamp (IMPFrameInfo) of the last analysed frame */
    uint32_t seq;           /* counts results of this channel */
    uint32_t flags;         /* OPENIMP_MOVE_EX_* */
    uint32_t suppress;      /* OPENIMP_MOVE_SUPP_* reasons of the current hold-off */
    uint32_t frame_w, frame_h;
    uint32_t grid_w, grid_h;    /* analysis grid (cells) */
    uint32_t legacy_roi[2];     /* bit i: vendor algorithm retRoi[i] != 0 */
    uint32_t v2_roi[2];         /* bit i: an object covers ROI i */
    uint32_t obj_cnt;
    uint32_t reserved[4];
    OpenIMP_IVS_MoveObject obj[OPENIMP_IVS_MOVE_EX_MAX_OBJ];
} OpenIMP_IVS_MoveOutputEx;

/* Extension data of the result returned by the last IMP_IVS_GetResult() on
 * this channel. Call it after IMP_IVS_GetResult() and before the next one.
 * Works for every move channel; with v2 off only legacy_roi/seq/frame_* are
 * filled and flags is 0. Returns 0, or -1 with errno EINVAL (bad channel,
 * NULL, size < 16) / ENOENT (no move interface on the channel). */
int OpenIMP_IVS_MoveGetResultEx(int channel, OpenIMP_IVS_MoveOutputEx *out);

/* Replace the v2 configuration of a move channel at run time (takes effect
 * with the next analysed frame; switching features on starts a new
 * background warm-up). Fields beyond cfg->size keep their defaults; values
 * out of range are clamped. features = 0 turns v2 off. */
int OpenIMP_IVS_MoveSetConfigEx(int channel, const OpenIMP_IVS_MoveConfigEx *cfg);
int OpenIMP_IVS_MoveGetConfigEx(int channel, OpenIMP_IVS_MoveConfigEx *cfg);

#ifdef __cplusplus
}
#endif

#endif
