/*
 * T85: imp/framesource/framesource_tseries.c
 *
 * Ported from libimp.so decompilation (HLIL addresses in range 0x997f0..0xa14xx).
 * Replaces src/imp_framesource.c.
 *
 * Control flow is reproduced from the Binary Ninja decompilation; unknown
 * struct layouts are addressed via raw byte-offset reads like
 * *(int32_t *)((char *)p + 0xNN). The FrameSource channel context is a
 * 0x2e8-byte struct; the framesource global points at channel[0] minus a
 * 0x40-byte control header (alloc_device("Framesource", 0xea0) returns
 * a Module* and gFramesource = module + 0x40).
 *
 * Functions whose logic is too intertwined with internal-state structures
 * (frame_pooling_thread's channel.state interaction with VBM/fifo, and
 * on_framesource_group_data_update's 500+-line transform pipeline) are
 * marked BLOCKED and fall back to a minimal functional path that preserves
 * the existing openimp framechannel semantics so binding/capture/encode
 * keeps working. See BLOCKED: notes inline.
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>

#include "trace_control.h"

#include "imp/imp_common.h"
#include "imp/imp_encoder.h"
#include "imp/imp_framesource.h"
#include "imp/imp_system.h"
#include "core/module.h"
#include "core/globals.h"
#include "core/imp_alloc.h"
#include "kernel_interface.h"
#include "vbm_dq_step.h"
#include "imp_log_int.h"
#if defined(PLATFORM_T23)
#include "t23/openimp_t23_persist.h"
#endif

/* ---------------------------------------------------------------------
 * Compatibility forward declarations (functions ported in other tasks).
 * ------------------------------------------------------------------- */

#include "imp_log_fun.h"

extern void *alloc_device(const char *name, size_t size); /* T72/device.c */
extern void free_device(void *dev);                        /* T72/device.c */
extern int32_t is_has_simd128(void);                       /* T73/sys_core.c */
extern int32_t get_cpu_id(void);                           /* T73/sys_core.c */

/* VBM API declared in kernel_interface.h; these are the additions
 * that aren't in the header. */
extern void   *VBMGetFrameInstance(int arg1, int arg2);
extern int32_t VBMDumpPoolInfo(void);

/* Video helpers from T75 */
extern void   *video_vbm_malloc(int32_t size, int32_t align);

/* Module/system helpers */
extern int32_t notify_observers(Module *module, void *frame);
extern int     add_observer_to_module(void *module, Observer *observer);
extern int     remove_observer_from_module(void *src_module, void *dst_module);
extern Subject *create_group(int32_t arg1, int32_t arg2, char *arg3, void *arg4);
extern int32_t destroy_group(Subject *subject, int32_t dev_id);
/* ISP hook, only used by the existing capture fallback path. */
extern int ISP_EnsureLinkStreamOn(int sensor_idx);

/* Kernel interface (V4L2 helpers ported to kernel_interface.c). */
/* kernel_interface.h already declares fs_* wrappers; we re-include it above. */

extern char _gp;

static void *fs_retaddr(void)
{
    return __builtin_return_address(0);
}

static void fs_thread_trace(const char *fmt, ...)
{
    int trace_kmsg = openimp_debug_trace_enabled();
#if defined(PLATFORM_T23)
    int trace_persist = openimp_t23_persist_enabled();
#else
    int trace_persist = 0;
#endif
    if (!trace_kmsg && !trace_persist)
        return;

    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        if (trace_kmsg) {
            int fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
            if (fd >= 0) {
                write(fd, buf, (size_t)n);
                close(fd);
            }
        }
#if defined(PLATFORM_T23)
        if (trace_persist)
            openimp_t23_persist_write(buf, (size_t)n);
#endif
    }
}

static void fs_user_trace(const char *fmt, ...)
{
    char buf[256];
    va_list ap;

    if (!openimp_debug_trace_enabled()
#if defined(PLATFORM_T23)
        && !openimp_t23_persist_enabled()
#endif
    )
        return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (openimp_debug_trace_enabled())
        imp_log_fun(4, IMP_Log_Get_Option(), 2, "Framesource",
            "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c",
            0x3f0, "frame_pooling_thread", "%s\n", buf);
}

/* Bind callbacks are defined later in the file but are needed during
 * CreateChn so the module can be bindable before EnableChn runs. */
int IMP_FrameSource_CreateChn(int chnNum, IMPFSChnAttr *chn_attr);
int IMP_FrameSource_EnableChn(int chnNum);
void DMA_RmemStreamStarted(void);   /* dma_alloc.h */
int32_t on_framesource_group_data_update(int32_t *arg1);
static void *frame_pooling_thread(void *arg);
static int framesource_bind(void *src_module, void *dst_module, void *output_ptr);
static int framesource_unbind(void *src_module, void *dst_module, void *output_ptr);
static void fs_bind_trace(const char *fmt, ...);
static inline int32_t fs_chan_get_state(int chn);
int32_t release_Frame(int32_t *arg1, void *arg2);
int32_t get_frame(int32_t *arg1, void *arg2);
static void *g_fs_vbm_ops[2] = {
    (void *)get_frame,
    (void *)release_Frame,
};

#define fs_trace fs_bind_trace

/* Debug hook registration (T81-ish); declare here so we only take their
 * addresses in FrameSourceInit. If the dsys_func_* symbols are not linked
 * in this build, the registrations collapse to a no-op weak stub below. */
int dsys_func_share_mem_register(int a, int b, const char *name,
                                 int (*fn)(void *)) __attribute__((weak));
int dsys_func_user_mem_register(int a, int b, const char *name,
                                int (*fn)(void *, void *), void *ctx)
                                __attribute__((weak));

/* sw_resize / simd globals referenced by FrameSourceInit. Weak so a later
 * port can override. */
int sw_resize_use_simd __attribute__((weak)) = 0;
void (*sw_resize)(void) __attribute__((weak)) = NULL;
__attribute__((weak)) void c_resize_c(void) { }
__attribute__((weak)) void c_resize_simd(void) { }

/* Optional misc dump callbacks; weak stubs so missing impl doesn't break. */
int dbg_misc_save_pic(void *a, void *b) __attribute__((weak));
int dbg_misc_system_info(void *a) __attribute__((weak));
int dbg_misc_save_pic(void *a, void *b) { (void)a; (void)b; return 0; }
int dbg_misc_system_info(void *a) { (void)a; return 0; }

/* Dump flag state referenced by release_Frame. */
int dump_base_move_ivs __attribute__((weak)) = 0;

/* g_dbg_fs_snap_yuv is a 4 x 4-int table (5 channels * 16 bytes). */
static int g_dbg_fs_snap_yuv[5 * 4];

/* fs_stat_str: string table indexed by channel state field (0x1c).
 * 0 => "INVALID", 1 => "CREATED", 2 => "ENABLED", 3 => "BOUND". */
static const char *const fs_stat_str[4] = {
    "INVALID", "CREATED", "ENABLED", "BOUND",
};

/* ---------------------------------------------------------------------
 * FrameSource global layout.
 * alloc_device("Framesource", 0xea0) returns a Module*. The stock code
 * stores gFramesource = module + 0x40, so "gFramesource[0]" dereferences
 * the module header word. Channel[i] starts at gFramesource + i*0x2e8.
 *
 * For the T85 port we treat gFrameSource as an opaque byte pointer.
 * ------------------------------------------------------------------- */

#define FS_MAX_CHANNELS   5
#define FS_CHANNEL_SIZE   0x2e8
#define FS_DEV_SIZE       0xea0

/* Offsets into the alloc_device-returned module/header at base `dev_base`.
 * - dev_base + 0x24 : max channels (5)
 * - dev_base + 0x20 : active channel count (grows on CreateChn)
 * - dev_base + 0x40 : self pointer (the exported `gFrameSource`)
 * - dev_base + 0x54 : fps_num (default 1)
 * - dev_base + 0x58 : fps_den
 * - dev_base + 0x5c : changewait counter
 *
 * Offsets relative to `chan = gFramesource + i*0x2e8` (i.e. dev_base + 0x40
 * + i*0x2e8):
 * - chan + 0x00..0x1b  : header/pad
 * - chan + 0x1c        : state (0 invalid, 1 created, 2 enabled)
 * - chan + 0x20..0x6f  : IMPFSChnAttr (0x50 bytes)
 * - chan + 0x68        : pixfmt (stored mid-attr; see HLIL offset decoding)
 * - chan + 0x1c4       : V4L2 fd
 * - chan + 0x1c8       : module pointer
 * - chan + 0x1cc       : nocopy_depth
 * - chan + 0x1d0       : copy_type flag
 * - chan + 0x208       : pthread_mutex_t (channel lock)
 * - chan + 0x220/0x224/0x228 : depth list head pointers (free, ready, pool)
 * - chan + 0x23c       : special (ext channel) data pointer
 * - chan + 0x240       : source channel index (ext -> phy)
 * - chan + 0x244       : sem_t
 * - chan + 0x268       : depth list current head
 * - chan + 0x294       : fifo pointer
 * - chan + 0x2e8       : tail guard / error flag
 * - chan + 0x2ec       : dumpFrameTime flag
 */

/* ---------------------------------------------------------------------
 * Convenience wrappers for byte-offset access.
 * ------------------------------------------------------------------- */

static inline uint8_t *fs_dev_base(void)
{
    /* gFrameSource = dev_base + 0x40. Subtract to reach the module header. */
    return (uint8_t *)gFrameSource - 0x40;
}

static inline uint8_t *fs_channel_base(int chn)
{
    if (gFrameSource == NULL) return NULL;
    return (uint8_t *)gFrameSource + chn * FS_CHANNEL_SIZE;
}

static void fs_direct_encoder_fallback(int chn, void *frame)
{
    Module *enc;
    int32_t frame_slot;
    int32_t rc;

    if (chn < 0 || chn >= FS_MAX_CHANNELS || frame == NULL) return;

    enc = g_modules[1][chn];
    if (enc == NULL || enc->update_fn == NULL) {
        return;
    }

    frame_slot = (int32_t)(intptr_t)frame;
    fs_bind_trace("libimp/FSB: direct-enc-dispatch ch=%d enc=%p update=%p frame=%p\n",
                  chn, enc, enc->update_fn, frame);
    rc = enc->update_fn(enc, &frame_slot);
    fs_bind_trace("libimp/FSB: direct-enc-dispatch-done ch=%d enc=%p rc=%d frame=%p\n",
                  chn, enc, rc, frame);
}

/* ---------------------------------------------------------------------
 * list_add_head / list_del_head / is_list_empty / list_del_FSDepth
 * (0x9a43c .. 0x9a5c8)
 *
 * Stock layout: list node is 0xc bytes:
 *   [0x00] = data pointer
 *   [0x04] = prev
 *   [0x08] = next
 * Each list head is also a 0xc-byte node (data = NULL, prev/next = self).
 * ------------------------------------------------------------------- */

void *list_add_head(void *arg1, void *arg2)
{
    /* Insert `arg1` at the head (after the sentinel `arg2`). */
    void *result = *(void **)((char *)arg2 + 8);
    *(void **)((char *)result + 4) = arg1;
    *(void **)((char *)arg1  + 8) = result;
    *(void **)((char *)arg1  + 4) = arg2;
    *(void **)((char *)arg2  + 8) = arg1;
    return result;
}

void *list_del_head(void **arg1, void *arg2)
{
    /* Remove last node from head list `arg2` (doubly-linked circular) and
     * hand the pointer back via *arg1. */
    void *v1 = *(void **)((char *)arg2 + 8);
    void *v0 = *(void **)((char *)v1   + 8);
    *arg1 = v1;
    *(void **)((char *)v0  + 4) = arg2;
    *(void **)((char *)arg2 + 8) = v0;
    {
        void *v0_1 = *arg1;
        *(void **)((char *)v0_1 + 8) = v0_1;
    }
    {
        void *result = *arg1;
        *(void **)((char *)result + 4) = result;
        return result;
    }
}

int is_list_empty(void *arg1)
{
    /* Stock: return ((*(arg1+8) ^ arg1) u< 1) ? 1 : 0
     * i.e. list is empty iff next == self. */
    return ((uintptr_t)(*(void **)((char *)arg1 + 8)) ^ (uintptr_t)arg1) < 1 ? 1 : 0;
}

void *list_del_FSDepth(void *arg1)
{
    /* Unlink arg1 from wherever it lives in its list. */
    void *result = *(void **)((char *)arg1 + 8);
    *(void **)((char *)result + 4) = *(void **)((char *)arg1 + 4);
    *(void **)((char *)(*(void **)((char *)arg1 + 4)) + 8) = result;
    *(void **)((char *)arg1 + 8) = arg1;
    *(void **)((char *)arg1 + 4) = arg1;
    return result;
}

/* IMP_FrameSource_ReleaseDepthList.isra.5 — walks list, optionally frees
 * the internal buffer at node+0x1c, then frees the node. HLIL 0x9a48c. */
static void fs_release_depth_list(int32_t *arg1, void *arg2, int32_t *arg3)
{
    void *picked = NULL;

    if (arg2 == NULL) {
        return;
    }

    while (is_list_empty(arg2) == 0) {
        list_del_head(&picked, arg2);
        {
            int32_t *a0_1 = (int32_t *)picked;
            void *v0_1 = *(void **)a0_1;

            if (v0_1 != NULL) {
                if (*arg1 == 0) {
                    void *a0_4 = *(void **)((char *)v0_1 + 0x1c);
                    if (a0_4 != NULL) {
                        free(a0_4);
                        v0_1 = *(void **)picked;
                    }
                }
                free(v0_1);
                a0_1 = (int32_t *)picked;
            }
            free(a0_1);
            *arg3 += 1;
        }
    }

    free(arg2);
}

/* ---------------------------------------------------------------------
 * sub_aaab4 — __pure pass-through used extensively by the decomp for
 * tail-call "return this value" constructs (HLIL 0x9aab4).
 * ------------------------------------------------------------------- */

int32_t sub_aaab4(int32_t a, int32_t b, int32_t c)
{
    (void)a; (void)b;
    return c;
}

/* ---------------------------------------------------------------------
 * sub_abf40 — switch-style transformer invoked by
 * on_framesource_group_data_update when a bound consumer supplies a
 * target buffer. BLOCKED: the 400-line MIPS assembly-style body cannot
 * be expressed as portable C; in openimp the bound-consumer path uses
 * notify_observers() instead, so this is stubbed.
 * ------------------------------------------------------------------- */

int32_t sub_abf40(int32_t arg1, int32_t arg2, int32_t arg3)
{
    (void)arg1; (void)arg2;
    /* BLOCKED: sub_abf40 reproduces ~20 per-format copy/rotate/scale paths
     * driven by four global tables; a correct port requires
     * tree_funcs_control/get_xy_max. Returning "did not handle" preserves
     * the caller's fallback to VBMReleaseFrame. */
    return arg3;
}

/* ---------------------------------------------------------------------
 * nv12_copyto_yuyv422 (HLIL 0x9a5c8). Copies an NV12 frame from `arg1`
 * (Y plane followed by UV plane at y-aligned-16 * width) into a YUYV422
 * buffer at `arg2` with width `arg3` and height `arg4`.
 * ------------------------------------------------------------------- */

int32_t nv12_copyto_yuyv422(char *arg1, int32_t arg2, int32_t arg3, int32_t arg4)
{
    if (arg4 != 0) {
        uint32_t t5_1 = (uint32_t)arg3 >> 1;
        char *i = arg1;
        int32_t t3_1 = 0;
        int32_t t0_1 = 0;

        do {
            char *t0_3 = &arg1[t0_1 * arg3 + ((arg4 + 0xf) & 0xfffffff0) * arg3];

            if (t5_1 != 0) {
                int32_t v0_3 = arg2;

                do {
                    char t1_1 = *i;
                    v0_3 += 4;
                    i = &i[2];
                    *(char *)(intptr_t)(v0_3 - 4) = t1_1;
                    {
                        char t1_2 = *t0_3;
                        t0_3 = &t0_3[2];
                        *(char *)(intptr_t)(v0_3 - 3) = t1_2;
                    }
                    *(char *)(intptr_t)(v0_3 - 2) = *(i - 1);
                    *(char *)(intptr_t)(v0_3 - 1) = *(t0_3 - 1);
                } while (&i[t5_1 << 1] != i);

                arg2 += t5_1 << 2;
            }

            t3_1 += 1;
            t0_1 = t3_1 >> 1;
        } while (t3_1 != arg4);
    }
    return 0;
}

/* ---------------------------------------------------------------------
 * set_framesource_fps / set_framesource_changewait_cnt (HLIL 0x9a3e8).
 * ------------------------------------------------------------------- */

int32_t set_framesource_fps(int32_t arg1, int32_t arg2)
{
    uint8_t *base = (uint8_t *)gFrameSource;
    if (base == NULL) {
        return -1;
    }
    *(int32_t *)(base + 4) = arg1;
    *(int32_t *)(base + 8) = arg2;
    return 0;
}

int32_t set_framesource_changewait_cnt(void)
{
    /* Stock: return neg.d(gFramesource u< 1 ? 1 : 0). That's -(0 or 1). */
    return -(gFrameSource == NULL ? 1 : 0);
}

/* ---------------------------------------------------------------------
 * ext_channel_get_frame / ext_channel_release_frame (HLIL 0x997f0 / 0x99888).
 *
 * Extended channels maintain a small frame array at (gFramesource+chn*0x2e8
 * +0x23c). The first int is the array capacity; then a 0xc-byte header
 * followed by capacity slots of 4 bytes each. Each slot holds a pointer to
 * a subframe struct whose header-size is 0xc bytes. The get function walks
 * for the first non-NULL slot; release clears slot `*arg1`.
 * ------------------------------------------------------------------- */

int32_t ext_channel_get_frame(int32_t *arg1, void *arg2)
{
    int32_t *v0_3 = *(int32_t **)((char *)arg2 + arg1[1] * FS_CHANNEL_SIZE + 0x23c);
    int32_t a2 = *v0_3;
    int32_t v1_2 = 0;

    if (a2 <= 0) {
        return -1;
    }

    {
        int32_t *i = (int32_t *)(uintptr_t)v0_3[3];
        char *v0_4 = (char *)&v0_3[4];
        int32_t *v0_5;

        if (i == 0) {
            do {
                v1_2 += 1;
                v0_4 += 4;
                if (v1_2 == a2) {
                    return -1;
                }
                i = *(int32_t **)(v0_4 - 4);
            } while (i == 0);
            v0_5 = &i[0xc];
        } else {
            v0_5 = &i[0xc];
        }

        do {
            int32_t t0_1 = *i;
            int32_t a3_1 = i[1];
            int32_t a2_1 = i[2];
            int32_t v1_3 = i[3];

            i = &i[4];
            *arg1 = t0_1;
            arg1[1] = a3_1;
            arg1[2] = a2_1;
            arg1[3] = v1_3;
            arg1 = &arg1[4];
        } while (i != v0_5);
    }

    return 0;
}

int32_t ext_channel_release_frame(int32_t *arg1, void *arg2)
{
    int32_t *slot_base = *(int32_t **)((char *)arg2 + arg1[1] * FS_CHANNEL_SIZE + 0x23c);
    *(int32_t *)((char *)slot_base + (*arg1 << 2) + 0xc) = 0;
    return 0;
}

/* ---------------------------------------------------------------------
 * dbg_fs_info (HLIL 0x998c0). Debug callback registered via
 * dsys_func_share_mem_register(1,0,"fs_info",...). Writes a formatted
 * multi-line description of the 5 channel attrs into the output buffer.
 * ------------------------------------------------------------------- */

int32_t dbg_fs_info(void *arg1)
{
    uint8_t *dev = fs_dev_base();
    uint8_t *fs  = (uint8_t *)gFrameSource;

    if (fs == NULL) {
        *(int32_t *)((char *)arg1 + 0xc) = -1;
        return -1;
    }

    {
        char *s1 = (char *)arg1 + 0x18;
        int32_t written = 0;

        for (int i = 0; i < FS_MAX_CHANNELS; i++) {
            uint8_t *chan = fs + i * FS_CHANNEL_SIZE;

            if (*(uint8_t **)(chan + 0x1c8) == NULL) {
                continue;
            }

            const char *crop_enabled_str = (*(int32_t *)(chan + 0x2c) == 0)
                                         ? "DISABLE" : "ENABLE";
            const char *pix_str = (*(int32_t *)(chan + 0x28) != 0xa)
                                ? "NOT-NV12" : "NV12";
            const char *scl_str = (*(int32_t *)(chan + 0x40) == 0)
                                ? "DISABLE" : "ENABLE";

            int32_t v0_3 = sprintf(
                s1,
                "CHANNEL(%d)\n\t  INFO\t%5dx%5d\t%8s \t%2d/%2d(fps) \t%8s \n"
                "\t  CROP\t%8s\tleft(%d)\ttop(%d)\twidth(%d)\theight(%d)\n"
                "\tSCALER \t%8s \twidth(%d) \theight(%d)\n",
                i,
                *(int32_t *)(chan + 0x20),
                *(int32_t *)(chan + 0x24),
                fs_stat_str[*(int32_t *)(chan + 0x1c) & 3],
                *(int32_t *)(dev + 0x44),  /* fps_num stored at dev+4 rel to fs */
                *(int32_t *)(dev + 0x48),  /* fps_den */
                pix_str,
                crop_enabled_str,
                *(int32_t *)(chan + 0x30),
                *(int32_t *)(chan + 0x34),
                *(int32_t *)(chan + 0x38),
                *(int32_t *)(chan + 0x3c),
                scl_str,
                *(int32_t *)(chan + 0x44),
                *(int32_t *)(chan + 0x48));

            s1 += v0_3;
            written += v0_3;
        }

        *(int32_t *)((char *)arg1 + 0x14) = written + 1;
        *(int32_t *)((char *)arg1 + 0xc) = 0;
        *(int32_t *)((char *)arg1 + 8) |= 0x100;
        *(int32_t *)((char *)arg1 + 0x10) = 0;
    }
    return 0;
}

/* ---------------------------------------------------------------------
 * dbg_misc_simple_cmd (HLIL 0x9a2d4). Command dispatcher for
 * user-space debug commands (misc channel 3).
 * ------------------------------------------------------------------- */

int32_t dbg_misc_simple_cmd(void *arg1)
{
    if (gFrameSource == NULL) {
        *(int32_t *)((char *)arg1 + 0xc) = -1;
        return -1;
    }

    printf("scmd = %d param1 = %d, param2 = %d, param3 = %d\n",
           *(int32_t *)((char *)arg1 + 0x10),
           *(int32_t *)((char *)arg1 + 0x14),
           *(int32_t *)((char *)arg1 + 0x18),
           *(int32_t *)((char *)arg1 + 0x1c));

    {
        int32_t v0_2 = *(int32_t *)((char *)arg1 + 0x10);

        if (v0_2 == 0x64) {
            int32_t a1_2 = *(int32_t *)((char *)arg1 + 0x14);

            if ((uint32_t)a1_2 >= 3) {
                printf("err: param1 = %d\n", a1_2);
                *(int32_t *)((char *)arg1 + 0xc) = -1;
                return 0;
            }

            {
                int32_t v1_1 = *(int32_t *)((char *)arg1 + 0x18);
                int32_t *a1_4 = &g_dbg_fs_snap_yuv[a1_2 * 4];
                a1_4[0] = 1;
                a1_4[3] = v1_1;
            }
        } else if (v0_2 == 0x2710) {
            int32_t v0_5 = *(int32_t *)((char *)arg1 + 0x14);

            if (v0_5 == 0x64) {
                extern int32_t VBMDumpPoolInfo(void);
                VBMDumpPoolInfo();
                v0_5 = *(int32_t *)((char *)arg1 + 0x14);
            }

            if (v0_5 == 0x320) {
                dump_base_move_ivs = *(int32_t *)((char *)arg1 + 0x18);
            }
        }
    }

    *(int32_t *)((char *)arg1 + 0xc) = 0;
    return 0;
}

/* ---------------------------------------------------------------------
 * FrameSourceInit / FrameSourceExit (HLIL 0x9c5b8 / 0x9c7c4).
 * Allocate the 0xea0-byte device structure, stash gFrameSource, register
 * debug channels.
 * ------------------------------------------------------------------- */

int32_t FrameSourceInit(void)
{
    void *v0_1;

    if (gFrameSource != NULL) {
        return 0;
    }

    v0_1 = alloc_device("Framesource", FS_DEV_SIZE);
    if (v0_1 == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Framesource",
            "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c",
            0x1ae, "alloc_framesource",
            "alloc_device() error\n", &_gp);
        gFrameSource = NULL;
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Framesource",
            "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c",
            0x49b, "FrameSourceInit",
            "Failed to alloc framesource!\n");
        return -1;
    }

    *(int32_t *)((char *)v0_1 + 0x24) = FS_MAX_CHANNELS;
    *(int32_t *)((char *)v0_1 + 0x20) = 0;
    *(void   **)((char *)v0_1 + 0x40) = v0_1;
    *(int32_t *)((char *)v0_1 + 0x54) = 1;
    gFrameSource = (FrameSourceState *)((char *)v0_1 + 0x40);

    if (is_has_simd128() == 0) {
        sw_resize_use_simd = 0;
        sw_resize = c_resize_c;
    } else {
        sw_resize_use_simd = 1;
        sw_resize = c_resize_simd;
    }

    if (dsys_func_share_mem_register) {
        dsys_func_share_mem_register(1, 0, "fs_info", dbg_fs_info);
        dsys_func_share_mem_register(3, 2, "misc_system_info", dbg_misc_system_info);
        dsys_func_share_mem_register(3, 3, "misc_simple_cmd", dbg_misc_simple_cmd);
    }
    if (dsys_func_user_mem_register) {
        dsys_func_user_mem_register(3, 0, "misc_save_pic",
                                    (int (*)(void *, void *))dbg_misc_save_pic, NULL);
    }

    return 0;
}

void FrameSourceExit(void)
{
    uint8_t *dev;

    if (gFrameSource == NULL) {
        return;
    }

    dev = fs_dev_base();
    if (*(int32_t *)((char *)gFrameSource + 0x14) >= 2) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Framesource",
            "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c",
            0x4bd, "FrameSourceExit",
            "Failed to exit, because a Framechannel hasn't be destroy!",
            &_gp);
        return;
    }

    free_device(dev);
    gFrameSource = NULL;
}

/* =====================================================================
 * Simple channel-bookkeeping state.
 *
 * The stock channel context has many fields we don't fully reverse; for the
 * openimp port we keep a parallel bookkeeping array that the functional
 * openimp capture/bind path relies on (VBM pools, capture threads, V4L2
 * fd, bound-module observers). This matches what the existing
 * imp_framesource.c used and lets us implement the public API in decomp-
 * compatible form without losing existing functionality.
 * =================================================================== */

typedef struct FsChnCtx {
    pthread_t   thread;
    sem_t       ready_sem;
    int         fd;
    int         nocopy_depth;
    int         copy_type;
    int         source_chn;
    int         frame_depth;
    IMPFSChnFifoAttr fifo;
    IMPFSChnAttr attr;      /* cached copy used by SnapFrame/GetChnAttr */
    int         created;
    int         running;
    void       *module;
    Subject    *subject;
} FsChnCtx;

static FsChnCtx g_fs_ctx[FS_MAX_CHANNELS];
static pthread_mutex_t g_fs_lock = PTHREAD_MUTEX_INITIALIZER;
/* Flags shared between the API threads and the pooling worker, accessed
 * with FS_FLAG_LOAD/FS_FLAG_STORE (relaxed atomics: plain word loads and
 * stores on MIPS; ordering comes from g_fs_lock and pthread_join). */
#define FS_FLAG_LOAD(x)     __atomic_load_n(&(x), __ATOMIC_RELAXED)
#define FS_FLAG_STORE(x, v) __atomic_store_n(&(x), (v), __ATOMIC_RELAXED)
static int g_fs_thread_entered[FS_MAX_CHANNELS];
static int g_fs_thread_enabled_seen[FS_MAX_CHANNELS];
/* Set by the pooling worker when it leaves its loop (not when cancelled). */
static int g_fs_thread_exited[FS_MAX_CHANNELS];

#if defined(PLATFORM_T31)
/* Stop diagnostics: the pooling worker records where it is, so a
 * DisableChn that times out waiting for it can say where it waits. */
enum {
    FS_STEP_START,          /* thread entry, stderr markers */
    FS_STEP_LOOP,           /* loop head */
    FS_STEP_STATE_WAIT,     /* waiting for ENABLED */
    FS_STEP_SELECT,         /* 25 ms select */
    FS_STEP_IDLE_SLEEP,     /* 1 ms sleep after an empty select */
    FS_STEP_IDLE_RECYCLE,   /* VBMRecycleIdleFrames after an empty select */
    FS_STEP_DQBUF,          /* VBMKernelDequeue, see openimp_vbm_dq_step */
    FS_STEP_DQ_EMPTY_SLEEP, /* 1 ms sleep after an empty DQBUF */
    FS_STEP_NOTIFY,         /* publish + notify_observers */
    FS_STEP_DIRECT_ENC,     /* fs_direct_encoder_fallback */
    FS_STEP_NOTIFY_DONE,    /* frame delivered, next DQBUF follows */
    FS_STEP_EXIT,
};
static const char *const fs_step_names[] = {
    "start", "loop-head", "state-wait", "select", "idle-sleep",
    "idle-recycle", "dqbuf",
    "dq-empty-sleep", "notify", "direct-enc", "notify-done", "exit",
};
static const char *const fs_dq_step_names[] = {
    "-", "ioctl", "ivs-capture", "queue-mutex", "requeue",
};
static volatile int g_fs_step[FS_MAX_CHANNELS];
/* Bumped by every FS_STEP: lets the stop path tell a worker that is stuck
 * in one step from one that keeps moving, without a clock read per step. */
static volatile unsigned int g_fs_step_seq[FS_MAX_CHANNELS];
/* Written by fs_wait_worker_exit (the stopping thread): when it last saw
 * the worker change step, i.e. a lower bound for how long it is in it. */
static uint32_t g_fs_step_seen_ms[FS_MAX_CHANNELS];
static volatile int g_fs_step_iter[FS_MAX_CHANNELS];
#endif

/* Monotonic ms, for the worker-stop wait and the T31 stop diagnostics. */
static uint32_t fs_now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)ts.tv_sec * 1000u + (uint32_t)(ts.tv_nsec / 1000000L);
}

#if defined(PLATFORM_T31)

/* No clock read here: on the 3.10 MIPS kernels without a vDSO every
 * clock_gettime is a syscall, and this runs ~5 times per frame and
 * channel. The stop path times a step by watching g_fs_step_seq. */
#define FS_STEP(chn, step) do {                 \
        g_fs_step[(chn)] = (step);              \
        g_fs_step_seq[(chn)]++;                 \
    } while (0)

static const char *fs_step_name(int step)
{
    if (step < 0 || step >= (int)(sizeof(fs_step_names) / sizeof(fs_step_names[0])))
        return "?";
    return fs_step_names[step];
}

static const char *fs_dq_step_name(int step)
{
    if (step < 0 || step >= (int)(sizeof(fs_dq_step_names) / sizeof(fs_dq_step_names[0])))
        return "?";
    return fs_dq_step_names[step];
}

/* Which T31 ISP driver is loaded, for the log only: the stop order and
 * the cancel fallback depend on the DQBUF behaviour fs_dqbuf() observed
 * (fs_dqbuf_nonblock_mode), not on this name. Both drivers load as
 * tx-isp-t31.ko; only the open one has the ae_freeze / isp_bypass_all
 * module parameters. Done once, at the first successful stream start. */
static const char *fs_t31_isp_driver_name(void)
{
    static const char *driver;

    if (driver)
        return driver;
    if (access("/sys/module/tx_isp_t31/parameters/ae_freeze", F_OK) == 0 ||
        access("/sys/module/tx_isp_t31/parameters/isp_bypass_all", F_OK) == 0)
        driver = "open tx-isp";
    else if (access("/sys/module/tx_isp_t31", F_OK) == 0)
        driver = "stock tx-isp";
    else
        driver = "unknown tx-isp (no /sys/module/tx_isp_t31)";
    return driver;
}

static void fs_t31_log_isp_driver(void)
{
    static int logged;

    if (logged)
        return;
    logged = 1;
    IMP_LOG_INFO("Framesource", "T31 ISP driver: %s",
                 fs_t31_isp_driver_name());
}
#else
#define FS_STEP(chn, step) do { } while (0)
#endif

static void fs_bind_trace(const char *fmt, ...)
{
    static unsigned int capture_loop_trace_count;
    int trace_kmsg = openimp_debug_trace_enabled();
#if defined(PLATFORM_T23)
    int trace_persist = openimp_t23_persist_enabled();
#else
    int trace_persist = 0;
#endif
    if (!trace_kmsg && !trace_persist)
        return;

    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n > 0) {
        if (trace_kmsg &&
            (strstr(buf, "pooling select-") != NULL ||
             strstr(buf, "pooling dq-path") != NULL ||
             strstr(buf, "pooling dequeue") != NULL) &&
            __sync_fetch_and_add(&capture_loop_trace_count, 1) >= 24)
            trace_kmsg = 0;
        if (trace_kmsg) {
            int fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
            if (fd >= 0) {
                write(fd, buf, (size_t)n);
                close(fd);
            }
        }
#if defined(PLATFORM_T23)
        if (trace_persist &&
            strstr(buf, "select-") == NULL &&
            strstr(buf, "dequeue") == NULL &&
            strstr(buf, "release_frame") == NULL &&
            strstr(buf, "notify-empty") == NULL &&
            strstr(buf, " notify ") == NULL &&
            strstr(buf, "direct-enc-dispatch") == NULL)
            openimp_t23_persist_write(buf, (size_t)n);
#endif
    }
}

__attribute__((constructor))
static void framesource_ctor_trace(void)
{
    fs_bind_trace("libimp/FSB: ctor v2 CreateChn=%p EnableChn=%p group_cb=%p pool_thread=%p bind=%p unbind=%p\n",
                  (void *)IMP_FrameSource_CreateChn,
                  (void *)IMP_FrameSource_EnableChn,
                  (void *)on_framesource_group_data_update,
                  (void *)frame_pooling_thread,
                  (void *)framesource_bind,
                  (void *)framesource_unbind);
}

static void framesource_publish_outputs(Module *module, void *frame)
{
    if (module == NULL) return;

    uint32_t count = *(uint32_t *)((char *)module + 0x134);
    if (count == 0) count = 1;
    if (count > 3) count = 3;

    for (uint32_t i = 0; i < count; i++) {
        *(void **)((char *)module + 0x138 + (i * sizeof(void *))) = frame;
    }
}

/* Local: read/write a channel field in the stock byte layout. */
static inline void fs_chan_set_state(int chn, int32_t state)
{
    uint8_t *c = fs_channel_base(chn);
    /* release/acquire: a worker that sees ENABLED also sees the module
     * set up before it (EnableChn) */
    if (c) __atomic_store_n((int32_t *)(c + 0x1c), state, __ATOMIC_RELEASE);
}

static inline int32_t fs_chan_get_state(int chn)
{
    uint8_t *c = fs_channel_base(chn);
    return c ? __atomic_load_n((int32_t *)(c + 0x1c), __ATOMIC_ACQUIRE) : 0;
}

/* ---------------------------------------------------------------------
 * on_framesource_group_data_update — registered as the Subject update
 * callback by CreateChn's create_group() call. The stock 500+line body
 * handles ten-way format transforms, rotation, crop/scaler fallbacks,
 * FIFO rotation and bound-consumer delivery. BLOCKED for byte-exact
 * reproduction: we keep the public observer-notification semantics by
 * calling notify_observers on the subject's underlying module.
 * ------------------------------------------------------------------- */

int32_t on_framesource_group_data_update(int32_t *arg1)
{
    /* arg1 layout from HLIL:
     *   arg1[0] = Subject *   (not used directly by us)
     *   arg1[1] = group/chn id (dev_id-ish)
     *   arg1[2] = group_index (channel number) */
    int chn = (int)arg1[2];
    void *frame = NULL;

    fs_bind_trace("libimp/FSB: group-cb enter arg=%p ch=%d ra=%p\n", arg1, chn, fs_retaddr());

    if (chn < 0 || chn >= FS_MAX_CHANNELS || gFrameSource == NULL) {
        return -1;
    }

    if (VBMGetFrame(chn, &frame) < 0 || frame == NULL) {
        return -1;
    }

    /* BLOCKED: stock dispatches through multiple format/rotation code
     * paths, sub_abf40 fan-out, and FIFO-gated frame delivery. The
     * openimp variant routes via the observer chain. */
    {
        Module *m = g_modules[0][chn]; /* DEV_ID_FS == 0 */
        if (m != NULL) {
            fs_trace("libimp/FS: update_one ch=%d module=%p frame=%p notify start\n",
                     chn, m, frame);
            framesource_publish_outputs(m, frame);
            notify_observers(m, frame);
            fs_trace("libimp/FS: update_one ch=%d module=%p frame=%p notify done\n",
                     chn, m, frame);
        }
    }
    return 0;
}

/* ---------------------------------------------------------------------
 * frame_pooling_thread — tx-isp-module capture is driven by
 * REQBUFS/QBUF/DQBUF. We still spawn the worker before STREAMON to match
 * OEM sequencing, but the worker must not touch the frame fd until the
 * channel state is promoted to ENABLED by EnableChn().
 * ------------------------------------------------------------------- */

static void *frame_pooling_thread(void *arg)
{
    int chn = *(int *)arg;
    FsChnCtx *ctx;
    char name[0x20];
    int poll_count = 0;
    int no_frame_cycles = 0;
    int state_wait_count = 0;
    int software_mode = 0;
    int last_state = -1;
    int first_enabled_seen = 0;

    if (chn < 0 || chn >= FS_MAX_CHANNELS) {
        return NULL;
    }
    ctx = &g_fs_ctx[chn];
    FS_FLAG_STORE(g_fs_thread_entered[chn], 1);
    FS_STEP(chn, FS_STEP_START);
    OPENIMP_TRACE_STDERR("FSDBG thread_entry\n");

    fs_bind_trace("libimp/FSB: pooling-thread start ch=%d ctx=%p arg=%p ra=%p\n",
                  chn, ctx, arg, fs_retaddr());
    fs_thread_trace("libimp/FS: thread-mark ch=%d step=start ctx=%p fd=%d running=%d\n",
                    chn, ctx, ctx->fd, FS_FLAG_LOAD(ctx->running));

    snprintf(name, sizeof(name), "FS(%d)-tick", chn);
    prctl(PR_SET_NAME, name);
    OPENIMP_TRACE_STDERR("FSDBG ch=%d step=post_prctl fd=%d running=%d\n", chn, ctx->fd, FS_FLAG_LOAD(ctx->running));
    while (FS_FLAG_LOAD(ctx->running)) {
        void *frame = NULL;
        Module *m;
        int ch_state;

        pthread_testcancel();
        poll_count++;
        FS_STEP(chn, FS_STEP_LOOP);
#if defined(PLATFORM_T31)
        g_fs_step_iter[chn] = poll_count;
#endif
        if (poll_count <= 2) {
            fs_thread_trace("libimp/FS: thread-mark ch=%d step=loop-top iter=%d fd=%d running=%d\n",
                            chn, poll_count, ctx->fd, FS_FLAG_LOAD(ctx->running));
            OPENIMP_TRACE_STDERR("FSDBG ch=%d step=loop_top iter=%d fd=%d running=%d\n",
                    chn, poll_count, ctx->fd, FS_FLAG_LOAD(ctx->running));
        }
        ch_state = fs_chan_get_state(chn);
        if (ch_state != last_state) {
            fs_trace("libimp/FS: thread-state ch=%d iter=%d state=%d fd=%d running=%d\n",
                     chn, poll_count, ch_state, ctx->fd, FS_FLAG_LOAD(ctx->running));
            last_state = ch_state;
        }

        if (ch_state != 2 || ctx->fd < 0) {
            state_wait_count++;
            if (state_wait_count <= 3 || (state_wait_count % 50) == 0) {
                fs_trace("libimp/FS: thread-wait-enabled ch=%d iter=%d state=%d fd=%d waits=%d\n",
                         chn, poll_count, ch_state, ctx->fd, state_wait_count);
            }
            FS_STEP(chn, FS_STEP_STATE_WAIT);
            usleep(10000);
            continue;
        }
        state_wait_count = 0;
        if (!first_enabled_seen) {
            first_enabled_seen = 1;
            FS_FLAG_STORE(g_fs_thread_enabled_seen[chn], 1);
            fs_trace("libimp/FS: thread-enabled ch=%d iter=%d state=%d fd=%d\n",
                     chn, poll_count, ch_state, ctx->fd);
        }

        if (!software_mode) {
            int drained = 0;
            fd_set rfds;
            struct timeval tv;
            int select_ret;

            if (poll_count <= 2) {
                fs_thread_trace("libimp/FS: thread-mark ch=%d step=select iter=%d fd=%d state=%d errno=%d\n",
                                chn, poll_count, ctx->fd, ch_state, errno);
                OPENIMP_TRACE_STDERR("FSDBG ch=%d step=before_select iter=%d fd=%d state=%d\n",
                        chn, poll_count, ctx->fd, ch_state);
            }

            FD_ZERO(&rfds);
            FD_SET(ctx->fd, &rfds);
            tv.tv_sec = 0;
            tv.tv_usec = 25000;

            errno = 0;
            fs_trace("libimp/FS: pooling select-enter ch=%d fd=%d iter=%d\n",
                     chn, ctx->fd, poll_count);
            FS_STEP(chn, FS_STEP_SELECT);
            select_ret = select(ctx->fd + 1, &rfds, NULL, NULL, &tv);
            fs_trace("libimp/FS: pooling select-exit ch=%d fd=%d iter=%d rc=%d errno=%d\n",
                     chn, ctx->fd, poll_count, select_ret, errno);
            if (select_ret < 0) {
                if (errno == EINTR) {
                    continue;
                }
                no_frame_cycles++;
                if (no_frame_cycles <= 5 || (no_frame_cycles % 50) == 0) {
                    fs_trace("libimp/FS: pooling select-fail ch=%d fd=%d idle=%d errno=%d\n",
                             chn, ctx->fd, no_frame_cycles, errno);
                }
                usleep(1000);
                continue;
            }
            if (select_ret == 0) {
                no_frame_cycles++;
                if (no_frame_cycles <= 5 || (no_frame_cycles % 50) == 0) {
                    fs_trace("libimp/FS: pooling select-timeout ch=%d fd=%d idle=%d\n",
                             chn, ctx->fd, no_frame_cycles);
                }
#if defined(PLATFORM_T31) || defined(PLATFORM_T23) || \
    defined(PLATFORM_T21) || defined(PLATFORM_T30)
                /* A reader that stopped (StopRecvPic) with every buffer
                 * parked in the ready queue leaves no DQBUF to complete;
                 * hand them back so IVS-only capture keeps running.
                 * DisableChn holds g_fs_lock from clearing running until
                 * after the join, so taking it here (never waiting for it)
                 * keeps any QBUF from racing the STREAMOFF/join, and
                 * running is rechecked under it. */
                if (pthread_mutex_trylock(&g_fs_lock) == 0) {
                    if (FS_FLAG_LOAD(ctx->running) && fs_chan_get_state(chn) == 2) {
                        FS_STEP(chn, FS_STEP_IDLE_RECYCLE);
                        VBMRecycleIdleFrames(chn);
                    }
                    pthread_mutex_unlock(&g_fs_lock);
                }
#endif
                FS_STEP(chn, FS_STEP_IDLE_SLEEP);
                usleep(1000);
                continue;
            }

            fs_trace("libimp/FS: pooling dq-path ch=%d fd=%d mode=select-then-dq\n",
                     chn, ctx->fd);

            /* fs_open_device() opens the fd O_NONBLOCK (no F_GETFL/F_SETFL
             * per wake-up). A driver that honours it (open tx-isp) ends the
             * drain below with EAGAIN; the stock one ignores it and sleeps
             * in DQBUF until the next frame, its select() is always ready.
             * fs_dqbuf() learns which and logs it once. */
            while (1) {
                /* No further DQBUF once DisableChn asked us to stop: it has
                 * issued STREAMOFF (or is about to), and a DQBUF on T31
                 * sleeps until the next frame, which may never come. */
                if (!FS_FLAG_LOAD(ctx->running))
                    break;
                if (poll_count <= 2) {
                    fs_thread_trace("libimp/FS: thread-mark ch=%d step=dq-drain iter=%d fd=%d state=%d errno=%d\n",
                                    chn, poll_count, ctx->fd, ch_state, errno);
                    OPENIMP_TRACE_STDERR("FSDBG ch=%d step=before_dq iter=%d fd=%d state=%d\n",
                            chn, poll_count, ctx->fd, ch_state);
                }
                fs_trace("libimp/FS: pooling dequeue-enter ch=%d fd=%d\n",
                         chn, ctx->fd);
                {
                    int dq_ret;

#if defined(PLATFORM_T31)
                    openimp_vbm_dq_step[chn] = VBM_DQ_STEP_NONE;
#endif
                    FS_STEP(chn, FS_STEP_DQBUF);
                    dq_ret = VBMKernelDequeue(chn, ctx->fd, &frame);
                    fs_trace("libimp/FS: pooling dequeue ch=%d fd=%d ret=%d frame=%p\n",
                             chn, ctx->fd, dq_ret, frame);
                    if (dq_ret == 0 && frame != NULL) {
                        fs_user_trace("pooling dequeue-ok ch=%d fd=%d frame=%p", chn, ctx->fd, frame);
                    }
                    /* Not streaming: expected while DisableChn stops the
                     * channel (running cleared before STREAMOFF); while it
                     * should run, the driver stopped the stream. */
                    if (dq_ret == -3 && FS_FLAG_LOAD(ctx->running)) {
                        static unsigned int not_streaming;
                        unsigned int n = __atomic_add_fetch(&not_streaming, 1u,
                                                            __ATOMIC_RELAXED);

                        if (n <= 5u || n % 100u == 0u)
                            IMP_LOG_ERR("Framesource", "chn%d: DQBUF: stream not running (driver stopped it?) (#%u)",
                                        chn, n);
                    }
                    if (dq_ret != 0 || frame == NULL) {
                        /* EAGAIN after at least one frame is the normal end
                         * of a drain on a driver with a real poll: go
                         * straight back to select(), which sleeps until the
                         * next frame. Sleep 1 ms only when select() said
                         * ready but nothing could be dequeued (a poll that
                         * always reports readable with a non-blocking
                         * DQBUF would otherwise spin), and after an error
                         * while still running (POLLERR + -EINVAL when the
                         * driver stopped the stream by itself). */
                        if ((dq_ret == -2 && drained == 0) || dq_ret == 0 ||
                            (dq_ret < 0 && dq_ret != -2 && FS_FLAG_LOAD(ctx->running))) {
                            no_frame_cycles++;
                            if (no_frame_cycles <= 5 || (no_frame_cycles % 50) == 0) {
                                fs_trace("libimp/FS: pooling dequeue-empty ch=%d fd=%d idle=%d state=%d ret=%d\n",
                                         chn, ctx->fd, no_frame_cycles, ch_state, dq_ret);
                            }
                            FS_STEP(chn, FS_STEP_DQ_EMPTY_SLEEP);
                            usleep(1000);
                        }
                        break;
                    }
                }
                no_frame_cycles = 0;
                drained++;

                m = g_modules[0][chn];
                if (m != NULL) {
                    fs_user_trace("pooling notify ch=%d module=%p frame=%p observers=%d",
                                  chn, m, frame, *(int32_t *)((char *)m + 0x3c));
                    fs_trace("libimp/FS: pooling ch=%d fd=%d module=%p frame=%p notify start\n",
                             chn, ctx->fd, m, frame);
                    FS_STEP(chn, FS_STEP_NOTIFY);
                    framesource_publish_outputs(m, frame);
                    notify_observers(m, frame);
                    if (*(int32_t *)((char *)m + 0x3c) == 0) {
                        fs_trace("libimp/FS: pooling ch=%d fd=%d module=%p frame=%p notify-empty direct-enc\n",
                                 chn, ctx->fd, m, frame);
                        FS_STEP(chn, FS_STEP_DIRECT_ENC);
                        fs_direct_encoder_fallback(chn, frame);
                    }
                    FS_STEP(chn, FS_STEP_NOTIFY_DONE);
                    fs_trace("libimp/FS: pooling ch=%d fd=%d module=%p frame=%p notify done\n",
                             chn, ctx->fd, m, frame);
                }
            }
            continue;
        }

        usleep(50000);
        if (VBMGetFrame(chn, &frame) < 0 || frame == NULL) {
            if (poll_count <= 5 || (poll_count % 50) == 0) {
                fs_trace("libimp/FS: pooling software ch=%d no frame available\n", chn);
            }
            continue;
        }

        m = g_modules[0][chn];
        if (m != NULL) {
            fs_trace("libimp/FS: pooling software ch=%d module=%p frame=%p notify start\n",
                     chn, m, frame);
            framesource_publish_outputs(m, frame);
            notify_observers(m, frame);
            if (*(int32_t *)((char *)m + 0x3c) == 0) {
                fs_trace("libimp/FS: pooling software ch=%d module=%p frame=%p notify-empty direct-enc\n",
                         chn, m, frame);
                fs_direct_encoder_fallback(chn, frame);
            }
            fs_trace("libimp/FS: pooling software ch=%d module=%p frame=%p notify done\n",
                     chn, m, frame);
        }
    }

    fs_trace("libimp/FS: pooling-thread exit ch=%d fd=%d running=%d state=%d\n",
             chn, ctx->fd, FS_FLAG_LOAD(ctx->running), fs_chan_get_state(chn));
    FS_STEP(chn, FS_STEP_EXIT);
    FS_FLAG_STORE(g_fs_thread_exited[chn], 1);
    return NULL;
}

/* ---------------------------------------------------------------------
 * release_Frame (HLIL 0x99c74): hands a buffer back to the V4L2 queue.
 * This path is used by the stock pooling thread; in openimp the same
 * transition is handled inside VBMKernelDequeue / VBMPrimeKernelQueue.
 * ------------------------------------------------------------------- */

int32_t release_Frame(int32_t *arg1, void *arg2)
{
    int chn;
    int fd;
    int idx;
    unsigned long phys;
    unsigned int len;
    uint8_t *fs_base = (uint8_t *)arg2;

    if (arg1 == NULL) return -1;

    chn = arg1[1];
    if (chn < 0 || chn >= FS_MAX_CHANNELS) return -1;

    fd = -1;
    if (fs_base != NULL) {
        fd = *(int32_t *)(fs_base + (chn * FS_CHANNEL_SIZE) + 0x1c4);
    }
    if (fd < 0) {
        fd = g_fs_ctx[chn].fd;
    }
    if (fd < 0) {
        fs_trace("libimp/FS: release_frame invalid-fd ch=%d frame=%p\n", chn, arg1);
        return -1;
    }

    idx = arg1[0];
    phys = (unsigned long)(uint32_t)arg1[6];
    len = (unsigned int)arg1[5];
    if (len == 0) {
        fs_trace("libimp/FS: release_frame zero-len ch=%d fd=%d idx=%d\n",
                 chn, fd, idx);
        return -1;
    }

    fs_trace("libimp/FS: release_frame qbuf ch=%d fd=%d idx=%d phys=0x%lx len=%u\n",
             chn, fd, idx, phys, len);
    if (fs_qbuf(fd, idx, phys, len) < 0) {
        fs_trace("libimp/FS: release_frame qbuf-fail ch=%d fd=%d idx=%d\n",
                 chn, fd, idx);
        return -1;
    }

    if (fs_base != NULL) {
        *(int32_t *)(fs_base + (chn * FS_CHANNEL_SIZE) + 0x22c) += 1;
    }
    return 0;
}

/* ---------------------------------------------------------------------
 * get_frame (HLIL 0x99f30): dequeues a V4L2 frame. BLOCKED for byte-
 * exact reproduction — the openimp path uses VBMKernelDequeue from the
 * capture thread.
 * ------------------------------------------------------------------- */

int32_t get_frame(int32_t *arg1, void *arg2)
{
    int chn = arg1[1];
    void *frame = NULL;

    if (chn < 0 || chn >= FS_MAX_CHANNELS) return -1;
    (void)arg2;

    if (VBMGetFrame(chn, &frame) < 0 || frame == NULL) {
        return -1;
    }
    return 0;
}

/* =====================================================================
 * Public API: IMP_FrameSource_*
 *
 * Each entry reproduces the decomp validation order
 * (channel range -> NULL-check -> alignment -> state) and emits
 * imp_log_fun calls with the stock __FILE__/__FUNCTION__/line triples.
 * =================================================================== */

int IMP_FrameSource_SetChnAttr(int chnNum, IMPFSChnAttr *chn_attr)
{
    const char *tag = "IMP_FrameSource_SetChnAttr";

    if (chn_attr == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Framesource",
            "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c",
            0x4cb, tag, "%s(): chnAttr is NULL\n", tag);
        return -1;
    }

    if ((unsigned int)chnNum >= FS_MAX_CHANNELS) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Framesource",
            "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c",
            0x4d0, tag, "%s(): Invalid chnNum %d\n", tag, chnNum);
        return -1;
    }

    if (chn_attr->pixFmt != PIX_FMT_RAW && (chn_attr->picWidth & 0xf) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Framesource",
            "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c",
            0x4da, tag,
            "chnNum=%d, chnAttr->picWidth=%d, should be align to 16\n",
            chnNum, chn_attr->picWidth);
        return -1;
    }

    if (chnNum == 0 && chn_attr->type != FS_PHY_CHANNEL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Framesource",
            "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c",
            0x4df, tag,
            "chnNum=%d type=%d must be set to FS_PHY_CHANNEL(%d)\n",
            0, chn_attr->type, 0);
        return -1;
    }

    if (gFrameSource == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Framesource",
            "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c",
            0x4e6, tag,
            "%s():[chn%d] FrameSource is invalid,maybe system was not inited yet.\n",
            tag, chnNum);
        return -1;
    }

    IMPFSChnAttr fixed;
    memcpy(&fixed, chn_attr, sizeof(fixed));
#ifdef PLATFORM_T20
    /* The T20 ISP scaler / VPU path wedges on an output height that is not a
     * multiple of 8 (480x270: first frame, then no more, soc_vpu start
     * timeout; 1080 is fine).  The OEM width check already demands 16; the
     * height is rounded up here with a warning instead of leaving the
     * channel stuck. */
    if (fixed.pixFmt != PIX_FMT_RAW) {
        if (fixed.scaler.enable && (fixed.scaler.outheight & 0x7)) {
            int h = (fixed.scaler.outheight + 7) & ~7;
            IMP_LOG_WARN("Framesource", "chn%d: scaler.outheight=%d is not a "
                         "multiple of 8, using %d (as GetChnAttr reports)",
                         chnNum, fixed.scaler.outheight, h);
            fixed.scaler.outheight = h;
        }
        if (fixed.picHeight & 0x7) {
            int h = (fixed.picHeight + 7) & ~7;
            IMP_LOG_WARN("Framesource", "chn%d: picHeight=%d is not a multiple "
                         "of 8, using %d (as GetChnAttr reports)",
                         chnNum, fixed.picHeight, h);
            fixed.picHeight = h;
        }
    }
#endif
    chn_attr = &fixed;
    pthread_mutex_lock(&g_fs_lock);
    memcpy(fs_channel_base(chnNum) + 0x20, chn_attr, sizeof(IMPFSChnAttr));
    memcpy(&g_fs_ctx[chnNum].attr, chn_attr, sizeof(IMPFSChnAttr));
    pthread_mutex_unlock(&g_fs_lock);
    return 0;
}

int IMP_FrameSource_GetChnAttr(int chnNum, IMPFSChnAttr *chn_attr)
{
    const char *tag = "IMP_FrameSource_GetChnAttr";

    if (chn_attr == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Framesource",
            "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c",
            0x4f3, tag, "%s(): chnAttr is NULL\n", tag);
        return -1;
    }
    if ((unsigned int)chnNum >= FS_MAX_CHANNELS) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Framesource",
            "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c",
            0x4f8, tag, "%s(): Invalid chnNum %d\n", tag, chnNum);
        return -1;
    }
    if (gFrameSource == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Framesource",
            "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c",
            0x4ff, tag,
            "%s(): FrameSource is invalid,maybe system was not inited yet.\n",
            tag);
        return -1;
    }

    {
        uint8_t *chan = fs_channel_base(chnNum);
        if (*(int32_t *)(chan + 0x20) == 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "Framesource",
                "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c",
                0x506, tag, "%s(): chnAttr was not set yet\n", tag);
            return -1;
        }
        memcpy(chn_attr, chan + 0x20, sizeof(IMPFSChnAttr));
    }
    return 0;
}

int IMP_FrameSource_SetFrameDepthCopyType(int chnNum, int bNoCopy)
{
    const char *tag = "IMP_FrameSource_SetFrameDepthCopyType";
    uint8_t *chan;
    int cur_depth;
    int result = -1;

    if ((unsigned int)chnNum >= FS_MAX_CHANNELS) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Framesource",
            "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c",
            0x52a, tag, "%s(): Invalid chnNum %d\n", tag, chnNum);
        return -1;
    }
    if (gFrameSource == NULL) return -1;

    chan = fs_channel_base(chnNum);
    pthread_mutex_lock((pthread_mutex_t *)(chan + 0x208));

    cur_depth = *(int32_t *)(chan + 0x1cc);
    if (cur_depth != 0) {
        if (bNoCopy != 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "Framesource",
                "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c",
                0x535, tag,
                "%s(): Please use before IMP_FrameSource_SetFrameDepth(%d,%d) when "
                "b_nocopy_depth(%d) != 0\n",
                tag, chnNum, cur_depth, bNoCopy);
        } else {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "Framesource",
                "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c",
                0x538, tag,
                "%s(): Please use after IMP_FrameSource_SetFrameDepth(%d, 0) when "
                "b_nocopy_depth == 0\n",
                tag, chnNum);
        }
        result = -1;
    } else {
        *(int32_t *)(chan + 0x1d0) = (bNoCopy > 0) ? 1 : 0;
        g_fs_ctx[chnNum].copy_type = (bNoCopy > 0) ? 1 : 0;
        result = 0;
    }

    pthread_mutex_unlock((pthread_mutex_t *)(chan + 0x208));
    return result;
}

int IMP_FrameSource_SetFrameDepth(int chnNum, int depth)
{
    const char *tag = "IMP_FrameSource_SetFrameDepth";

    if ((unsigned int)chnNum >= FS_MAX_CHANNELS) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Framesource",
            "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c",
            0x54c, tag, "%s(): Invalid chnNum %d\n", tag, chnNum);
        return -1;
    }
    if (gFrameSource == NULL) return -1;

    {
        uint8_t *chan = fs_channel_base(chnNum);
        pthread_mutex_t *chan_lock = (pthread_mutex_t *)(chan + 0x208);

        if (depth <= 0) {
            int s6;
            int released = 0;

            pthread_mutex_lock(chan_lock);
            s6 = *(int32_t *)(chan + 0x1cc);
            if (s6 <= 0) {
                pthread_mutex_unlock(chan_lock);
                return 0;
            }

            *(int32_t *)(chan + 0x1cc) = 0;
            fs_release_depth_list((int32_t *)(chan + 0x1d0),
                                  *(void **)(chan + 0x220), &released);
            fs_release_depth_list((int32_t *)(chan + 0x1d0),
                                  *(void **)(chan + 0x224), &released);
            fs_release_depth_list((int32_t *)(chan + 0x1d0),
                                  *(void **)(chan + 0x228), &released);

            if (released != s6) {
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "Framesource",
                    "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c",
                    0x5a7, tag,
                    "Release %d not equal to logical depth %d node\n",
                    released, s6, &_gp);
                pthread_mutex_unlock(chan_lock);
                return -1;
            }
            __atomic_store_n(&g_fs_ctx[chnNum].frame_depth, 0,
                             __ATOMIC_RELAXED);
            pthread_mutex_unlock(chan_lock);
            return 0;
        }

        /* BLOCKED (partial): stock allocates three 0xc-byte list heads and
         * `depth` nodes + calloc(0x30) per-node state + calloc(size) for
         * the buffer at +0x1c when copy_type==0. For openimp we store the
         * depth in the ctx and let the kernel pool carry the frame-depth
         * via fs_set_depth. This matches existing imp_framesource.c. */
        pthread_mutex_lock(chan_lock);
        __atomic_store_n(&g_fs_ctx[chnNum].frame_depth, depth,
                         __ATOMIC_RELAXED);
        *(int32_t *)(chan + 0x1cc) = depth;
        pthread_mutex_unlock(chan_lock);
        /* The fd belongs to Enable/DisableChn: check the state and use the
         * fd under g_fs_lock, or the ioctl can land on a closed (or already
         * reused) descriptor. */
        pthread_mutex_lock(&g_fs_lock);
        if (fs_chan_get_state(chnNum) == 2 && g_fs_ctx[chnNum].fd >= 0) {
            fs_set_depth(g_fs_ctx[chnNum].fd, depth);
        }
        pthread_mutex_unlock(&g_fs_lock);
        return 0;
    }
}

int IMP_FrameSource_GetFrameDepth(int chnNum, int *depth)
{
    if ((unsigned int)chnNum >= FS_MAX_CHANNELS || depth == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Framesource",
            "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c",
            0x5bd, "IMP_FrameSource_GetFrameDepth",
            "%s(): Invalid chnNum %d\n", "IMP_FrameSource_GetFrameDepth",
            chnNum, &_gp);
        return -1;
    }
    if (gFrameSource == NULL) return -1;
    *depth = *(int32_t *)(fs_channel_base(chnNum) + 0x1cc);
    return 0;
}

int IMP_FrameSource_CreateChn(int chnNum, IMPFSChnAttr *chn_attr)
{
    const char *tag = "IMP_FrameSource_CreateChn";
    uint8_t *chan;

    fs_bind_trace("libimp/FSB: CreateChn enter ch=%d attr=%p type=%u size=%dx%d fmt=0x%x ra=%p\n",
                  chnNum, chn_attr,
                  chn_attr ? (unsigned)chn_attr->type : 0u,
                  chn_attr ? chn_attr->picWidth : -1,
                  chn_attr ? chn_attr->picHeight : -1,
                  chn_attr ? (unsigned)chn_attr->pixFmt : 0u,
                  fs_retaddr());

    if (gFrameSource == NULL) {
        if (FrameSourceInit() < 0) {
            return -1;
        }
    }

    /* T31 (CPU id 0xb) size clamp (stock). */
    if (get_cpu_id() == 0xb &&
        (chn_attr && (chn_attr->picWidth >= 0x501 || chn_attr->picHeight >= 0x2d1))) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Framesource",
            "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c",
            0x5ea, tag,
            "Error: width/height is too large!!!! frame->width = %d, frame->height = %d\n",
            chn_attr->picWidth, chn_attr->picHeight);
        return -1;
    }
    if ((unsigned int)chnNum >= FS_MAX_CHANNELS) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Framesource",
            "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c",
            0x5ef, tag, "Invalid channel num%d\n", chnNum);
        return -1;
    }
    if (chn_attr == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Framesource",
            "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c",
            0x5f4, tag, "chnNum=%d, chnAttr is NULL\n", chnNum);
        return -1;
    }
    if (chn_attr->pixFmt != PIX_FMT_RAW && (chn_attr->picWidth & 0xf) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Framesource",
            "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c",
            0x5f9, tag,
            "chnNum=%d, chnAttr->picWidth should be align to 16\n", chnNum);
        return -1;
    }
    if (chnNum == 0 && chn_attr->type != FS_PHY_CHANNEL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Framesource",
            "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c",
            0x5fe, tag,
            "chnNum=%d type=%d must be set to FS_PHY_CHANNEL(%d)\n",
            0, chn_attr->type, 0);
        return -1;
    }

    pthread_mutex_lock(&g_fs_lock);
    chan = fs_channel_base(chnNum);

    if (*(int32_t *)(chan + 0x1c) == 0) {
        /* First-time create: initialize pthread primitives at +0x208, +0x1d8,
         * +0x278 and copy the attr block (0x50 bytes at +0x20). */
        pthread_mutex_init((pthread_mutex_t *)(chan + 0x208), NULL);
        pthread_cond_init((pthread_cond_t *)(chan + 0x1d8), NULL);
        pthread_cond_init((pthread_cond_t *)(chan + 0x278), NULL);
        sem_init(&g_fs_ctx[chnNum].ready_sem, 0, 0);
        /* No device yet. Only here: a CreateChn on an enabled channel must
         * not drop the fd DisableChn has to stop and close. */
        g_fs_ctx[chnNum].fd = -1;

        IMP_FrameSource_SetFrameDepth(chnNum, 0);
        IMP_FrameSource_SetFrameDepthCopyType(chnNum, 0);

        {
            char name[0x20];
            void *module_self = *(void **)((char *)gFrameSource);
            snprintf(name, sizeof(name), "%s-%d", "Framesource", chnNum);

            /* create_group assigns g_modules[0][chnNum]. */
            {
                Subject *sub = create_group(0, chnNum, name,
                                            (void *)on_framesource_group_data_update);
                if (sub != NULL) {
                    *(void **)sub = module_self;
                    *(int32_t *)((char *)sub + 0xc) = 3;
                    g_fs_ctx[chnNum].subject = sub;
                }
            }

            {
                Module *m = g_modules[0][chnNum];
                if (m != NULL) {
                    /* system_bind validates outputID against module+0x134.
                     * FrameSource exposes exactly one output per channel. */
                    *(uint32_t *)((char *)m + 0x134) = 1;
                    *(void **)((char *)m + 0x138) = NULL;
                    *(void **)((char *)m + 0x40) = (void *)framesource_bind;
                    *(void **)((char *)m + 0x44) = (void *)framesource_unbind;
                    fs_bind_trace("libimp/FSB: createchn set-bind ch=%d module=%p outcnt=%u bind=%p unbind=%p\n",
                                  chnNum, m, *(uint32_t *)((char *)m + 0x134),
                                  framesource_bind, framesource_unbind);
                } else {
                    fs_bind_trace("libimp/FSB: createchn missing-module ch=%d subject=%p self=%p\n",
                                  chnNum, g_fs_ctx[chnNum].subject, module_self);
                }
            }

            memcpy(chan + 0x20, chn_attr, sizeof(IMPFSChnAttr));
            memset(chan + 0x258, 0, 0x18);
            *(int32_t *)(chan + 0x1c) = 1;
            *(int32_t *)((char *)gFrameSource + 0x14) += 1;
        }
    }

    if (*(int32_t *)(chan + 0x58) == 1) {
        *(int32_t *)(chan + 0x240) = 0;
    }

    {
        struct stat st;
        *(int32_t *)(chan + 0x2ec) = (stat("/tmp/dumpFrameTime", &st) == 0) ? 1 : 0;
    }

    memcpy(&g_fs_ctx[chnNum].attr, chn_attr, sizeof(IMPFSChnAttr));
    g_fs_ctx[chnNum].created = 1;
    pthread_mutex_unlock(&g_fs_lock);
    return 0;
}

#if defined(PLATFORM_T31)
static void fs_rotate_release(int chn);
#endif

int IMP_FrameSource_DestroyChn(int chnNum)
{
    const char *tag = "IMP_FrameSource_DestroyChn";

    if ((unsigned int)chnNum >= FS_MAX_CHANNELS) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Framesource",
            "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c",
            0x613, tag, "%s(): Invalid chnNum %d\n", tag, chnNum);
        return -1;
    }
    if (gFrameSource == NULL) return 0;

    pthread_mutex_lock(&g_fs_lock);

    if (fs_chan_get_state(chnNum) == 2) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Framesource",
            "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c",
            0x622, tag,
            "%s(): channel %d is busy, please disable it firstly\n",
            tag, chnNum);
        pthread_mutex_unlock(&g_fs_lock);
        return -1;
    }
    if (fs_chan_get_state(chnNum) == 0) {
        /* vendor: a channel that was never created (or is already
         * destroyed) is a successful no-op */
        pthread_mutex_unlock(&g_fs_lock);
        return 0;
    }

    if (g_fs_ctx[chnNum].subject != NULL) {
        destroy_group(g_fs_ctx[chnNum].subject, 0);
        g_fs_ctx[chnNum].subject = NULL;
    }

    fs_chan_set_state(chnNum, 0);
    *(int32_t *)((char *)gFrameSource + 0x14) -= 1;
    g_fs_ctx[chnNum].created = 0;
    FS_FLAG_STORE(g_fs_ctx[chnNum].running, 0);
#if defined(PLATFORM_T31)
    fs_rotate_release(chnNum);
#endif
    pthread_mutex_unlock(&g_fs_lock);
    return 0;
}

int IMP_FrameSource_SetMaxDelay(int chnNum, int max_delay)
{
    if (chnNum < 0 || chnNum >= FS_MAX_CHANNELS) return -1;
    (void)max_delay;
    return 0;
}

int IMP_FrameSource_GetMaxDelay(int chnNum, int *max_delay)
{
    if (chnNum < 0 || chnNum >= FS_MAX_CHANNELS || !max_delay) return -1;
    *max_delay = 0;
    return 0;
}

int IMP_FrameSource_SetDelay(int chnNum, int delay)
{
    if (chnNum < 0 || chnNum >= FS_MAX_CHANNELS) return -1;
    (void)delay;
    return 0;
}

int IMP_FrameSource_GetDelay(int chnNum, int *delay)
{
    if (chnNum < 0 || chnNum >= FS_MAX_CHANNELS || !delay) return -1;
    *delay = 0;
    return 0;
}

int IMP_FrameSource_SetChnFifoAttr(int chnNum, IMPFSChnFifoAttr *attr)
{
    int state;

    if (chnNum < 0 || chnNum >= FS_MAX_CHANNELS || attr == NULL) return -1;
    pthread_mutex_lock(&g_fs_lock);
    state = fs_chan_get_state(chnNum);
    if (state != 1 && state != 2) {
        pthread_mutex_unlock(&g_fs_lock);
        return -1;
    }
    g_fs_ctx[chnNum].fifo = *attr;
    pthread_mutex_unlock(&g_fs_lock);
    fs_trace("libimp/FS: SetChnFifoAttr ch=%d state=%d maxdepth=%d depth=%d\n",
             chnNum, state, attr->maxdepth, attr->depth);
    IMP_FrameSource_SetMaxDelay(chnNum, attr->maxdepth);
    return 0;
}

int IMP_FrameSource_GetChnFifoAttr(int chnNum, IMPFSChnFifoAttr *attr)
{
    if (chnNum < 0 || chnNum >= FS_MAX_CHANNELS || attr == NULL) return -1;
    pthread_mutex_lock(&g_fs_lock);
    if (fs_chan_get_state(chnNum) == 0) {
        pthread_mutex_unlock(&g_fs_lock);
        return -1;
    }
    *attr = g_fs_ctx[chnNum].fifo;
    pthread_mutex_unlock(&g_fs_lock);
    return 0;
}

int IMP_FrameSource_CloseNCUInfo(void)
{
    extern void *VBMGetInstance(void);
    void *vbm = VBMGetInstance();
    if (vbm == NULL) return -1;
    *(int32_t *)((uint8_t *)vbm + 0x14) = 1;
    return 0;
}

/* Bind/unbind callbacks registered on the module (offsets +0x40/+0x44). */
static int framesource_bind(void *src_module, void *dst_module, void *output_ptr)
{
    Module *src;
    Module *dst;
    Module **slot_module;
    void **slot_frame;
    int chn;
    int i;

    if (src_module == NULL || dst_module == NULL) {
        fs_bind_trace("libimp/FSB: bind src=%p dst=%p outptr=%p invalid\n",
                      src_module, dst_module, output_ptr);
        return -1;
    }

    src = (Module *)src_module;
    dst = (Module *)dst_module;
    chn = src->channel;
    fs_bind_trace("libimp/FSB: bind src=%p dst=%p outptr=%p\n",
                  src_module, dst_module, output_ptr);

    /* The ported core/module.c observer path uses the raw vendor slot
     * array at +0x14/+0x18 and observer count at +0x3c.  Do not recurse
     * back through add_observer_to_module(); that routes through the
     * module's bind vtable again and never populates the slots that
     * notify_observers() actually scans. */
    slot_module = (Module **)((char *)src + 0x14);
    slot_frame = (void **)((char *)src + 0x18);

    for (i = 0; i < 5; i++) {
        if (*slot_module == dst) {
            *slot_frame = output_ptr;
            *(Module **)((char *)dst + 0x10) = src;
            fs_bind_trace("libimp/FSB: bind refresh src=%p dst=%p slot=%d outptr=%p count=%d\n",
                          src, dst, i, output_ptr,
                          *(int32_t *)((char *)src + 0x3c));
            return 0;
        }

        if (*slot_module == NULL) {
            *slot_module = dst;
            *slot_frame = output_ptr;
            *(int32_t *)((char *)src + 0x3c) += 1;
            *(Module **)((char *)dst + 0x10) = src;
            fs_bind_trace("libimp/FSB: bind success src=%p dst=%p slot=%d outptr=%p count=%d\n",
                          src, dst, i, output_ptr,
                          *(int32_t *)((char *)src + 0x3c));
            if (chn >= 0 &&
                chn < FS_MAX_CHANNELS &&
                g_fs_ctx[chn].created &&
                g_fs_ctx[chn].attr.type == FS_PHY_CHANNEL &&
                fs_chan_get_state(chn) == 1) {
                fs_bind_trace("libimp/FSB: bind ready ch=%d dst=%s state=%d defer-enable-until-StartRecvPic\n",
                              chn, dst->name, fs_chan_get_state(chn));
            }
            return 0;
        }

        slot_module = (Module **)((char *)slot_module + 8);
        slot_frame = (void **)((char *)slot_frame + 8);
    }

    fs_bind_trace("libimp/FSB: bind src=%p dst=%p outptr=%p full count=%d\n",
                  src, dst, output_ptr, *(int32_t *)((char *)src + 0x3c));
    return -1;
}

static int framesource_unbind(void *src_module, void *dst_module, void *output_ptr)
{
    (void)output_ptr;
    if (src_module == NULL || dst_module == NULL) return -1;
    return remove_observer_from_module(src_module, dst_module);
}

static void fs_stop_worker(int chn, FsChnCtx *ctx);
static void fs_close_chn_fd(int chn, FsChnCtx *ctx);

int IMP_FrameSource_EnableChn(int chnNum)
{
    FsChnCtx *ctx;
    uint8_t *chan;
    fs_format_t fmt;
    uint8_t vbm_fmt[0xd0];
    int kernel_sizeimage;
    int requested_bufcnt;
    int bufcnt;
    int vbm_count;
    int queued_ok;
    int initial_queued_ok;
    int frame_depth;

    if (chnNum < 0 || chnNum >= FS_MAX_CHANNELS) return -1;
    if (gFrameSource == NULL) return -1;

    fs_bind_trace("libimp/FSB: EnableChn enter ch=%d gFrameSource=%p ra=%p\n",
                  chnNum, gFrameSource, fs_retaddr());

    pthread_mutex_lock(&g_fs_lock);
    if (fs_chan_get_state(chnNum) == 2) {
        pthread_mutex_unlock(&g_fs_lock);
        return 0;
    }
    if (fs_chan_get_state(chnNum) != 1) {
        pthread_mutex_unlock(&g_fs_lock);
        return -1;
    }

    ctx = &g_fs_ctx[chnNum];
    chan = fs_channel_base(chnNum);
    /* SetFrameDepth stores it under the channel lock, not g_fs_lock */
    frame_depth = __atomic_load_n(&ctx->frame_depth, __ATOMIC_RELAXED);
    fs_trace("libimp/FS: enable start ch=%d state=%d ctx=%p fd=%d attr=%dx%d fmt=0x%x nrVBs=%d depth=%d\n",
             chnNum, fs_chan_get_state(chnNum), ctx, ctx->fd,
             ctx->attr.picWidth, ctx->attr.picHeight, ctx->attr.pixFmt,
             ctx->attr.nrVBs, frame_depth);

    if (ctx->fd < 0) {
        ctx->fd = fs_open_device(chnNum);
        if (ctx->fd < 0) {
            fs_trace("libimp/FS: enable open-fail ch=%d\n", chnNum);
            pthread_mutex_unlock(&g_fs_lock);
            return -1;
        }
        fs_trace("libimp/FS: enable open-ok ch=%d fd=%d\n", chnNum, ctx->fd);
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = 1;
    fmt.width = ctx->attr.picWidth;
    fmt.height = ctx->attr.picHeight;
    fmt.pixelformat = ctx->attr.pixFmt;
    fmt.colorspace = 8;
    fmt.fps_num = 1;
    fmt.crop_enable = ctx->attr.crop.enable;
    fmt.crop_x = ctx->attr.crop.left;
    fmt.crop_y = ctx->attr.crop.top;
    fmt.crop_width = ctx->attr.crop.width;
    fmt.crop_height = ctx->attr.crop.height;
    fmt.scaler_enable = ctx->attr.scaler.enable;
    fmt.scaler_outwidth = ctx->attr.scaler.outwidth;
    fmt.scaler_outheight = ctx->attr.scaler.outheight;

    if (fs_set_format(ctx->fd, &fmt) < 0) {
        fs_trace("libimp/FS: enable set-format-fail ch=%d fd=%d\n", chnNum, ctx->fd);
        fs_close_chn_fd(chnNum, ctx);
        pthread_mutex_unlock(&g_fs_lock);
        return -1;
    }
    fs_trace("libimp/FS: enable set-format-ok ch=%d fd=%d sizeimage=%d\n",
             chnNum, ctx->fd, fmt.sizeimage);
    kernel_sizeimage = fmt.sizeimage;

    memset(vbm_fmt, 0, sizeof(vbm_fmt));
    memcpy(vbm_fmt + 0x00, &ctx->attr.picWidth, sizeof(int));
    memcpy(vbm_fmt + 0x04, &ctx->attr.picHeight, sizeof(int));
    memcpy(vbm_fmt + 0x08, &ctx->attr.pixFmt, sizeof(int));
    memcpy(vbm_fmt + 0x0c, &kernel_sizeimage, sizeof(int));
    vbm_count = ctx->attr.nrVBs;
    /*
     * Preserve the application/OEM buffer count.  On the stock T31 frame
     * channel the 640x360 pipeline requests and queues exactly two 0x56400
     * USERPTR buffers.  Forcing a third slot makes the remote ISP DMA into
     * memory beyond the supported address table and corrupts the allocator's
     * next object (the encoder settings block).
     */
    if (vbm_count < 1) vbm_count = 1;
    memcpy(vbm_fmt + 0x34, &vbm_count, sizeof(int));

    if (VBMCreatePool(chnNum, vbm_fmt, g_fs_vbm_ops, gFrameSource) < 0) {
        fs_trace("libimp/FS: enable VBMCreatePool-fail ch=%d\n", chnNum);
        fs_close_chn_fd(chnNum, ctx);
        pthread_mutex_unlock(&g_fs_lock);
        return -1;
    }

    requested_bufcnt = vbm_count + (frame_depth > 0 ? frame_depth : 0);
    bufcnt = fs_set_buffer_count(ctx->fd, requested_bufcnt);
    if (bufcnt < 0) {
        fs_trace("libimp/FS: enable set-bufcnt-fail ch=%d req=%d\n", chnNum, requested_bufcnt);
        VBMDestroyPool(chnNum);
        fs_close_chn_fd(chnNum, ctx);
        pthread_mutex_unlock(&g_fs_lock);
        return -1;
    }
    fs_trace("libimp/FS: enable set-bufcnt-ok ch=%d req=%d got=%d\n",
             chnNum, requested_bufcnt, bufcnt);

    *(int32_t *)(chan + 0x1c4) = ctx->fd;

    /* T21 folds the bank-count event into REQBUFS and has no 0x800456c5
     * command.  Later frame-channel ABIs issue the extra bank/depth ioctl.
     * Use nrVBs because it matches the pool shape configured above. */
#if defined(PLATFORM_T21) || defined(PLATFORM_T20)
    fs_trace("libimp/FS: enable set-banks-via-reqbufs ch=%d fd=%d banks=%d depth=%d\n",
             chnNum, ctx->fd, vbm_count, frame_depth);
#else
    {
        int banks = ctx->attr.nrVBs;
        if (banks < 1) {
            banks = 1;
        }
        if (fs_set_depth(ctx->fd, banks) < 0) {
            fs_trace("libimp/FS: enable set-banks-fail ch=%d fd=%d banks=%d depth=%d\n",
                     chnNum, ctx->fd, banks, frame_depth);
            VBMFlushFrame(chnNum);
            fs_close_chn_fd(chnNum, ctx);
            VBMDestroyPool(chnNum);
            FS_FLAG_STORE(ctx->running, 0);
            pthread_mutex_unlock(&g_fs_lock);
            return -1;
        }
        fs_trace("libimp/FS: enable set-banks-ok ch=%d fd=%d banks=%d depth=%d\n",
                 chnNum, ctx->fd, banks, frame_depth);
    }
#endif

    queued_ok = VBMFillPool(chnNum);
    if (queued_ok < 0) {
        fs_trace("libimp/FS: enable VBMFillPool-fail ch=%d\n", chnNum);
        fs_close_chn_fd(chnNum, ctx);
        VBMDestroyPool(chnNum);
        pthread_mutex_unlock(&g_fs_lock);
        return -1;
    }
    initial_queued_ok = queued_ok;
    fs_trace("libimp/FS: enable fill-pool-ok ch=%d queued_ok=%d\n", chnNum, queued_ok);

    FS_FLAG_STORE(ctx->running, 1);

    {
        static int chn_id_storage[FS_MAX_CHANNELS];
        chn_id_storage[chnNum] = chnNum;
        FS_FLAG_STORE(g_fs_thread_entered[chnNum], 0);
        FS_FLAG_STORE(g_fs_thread_enabled_seen[chnNum], 0);
        FS_FLAG_STORE(g_fs_thread_exited[chnNum], 0);
        if (pthread_create(&ctx->thread, NULL, frame_pooling_thread,
                           &chn_id_storage[chnNum]) != 0) {
            fs_trace("libimp/FS: enable pthread-create-fail ch=%d fd=%d\n", chnNum, ctx->fd);
            ctx->thread = 0;
            FS_FLAG_STORE(ctx->running, 0);
            fs_stream_off(ctx->fd);
            VBMFlushFrame(chnNum);
            fs_close_chn_fd(chnNum, ctx);
            VBMDestroyPool(chnNum);
            fs_chan_set_state(chnNum, 1);
            pthread_mutex_unlock(&g_fs_lock);
            return -1;
        }
        fs_trace("libimp/FS: enable pthread-create-ok ch=%d tid=%p arg=%p\n",
                 chnNum, (void *)ctx->thread, &chn_id_storage[chnNum]);
        usleep(20000);
        fs_trace("libimp/FS: enable thread-probe ch=%d entered=%d kill0=%d errno=%d\n",
                 chnNum, FS_FLAG_LOAD(g_fs_thread_entered[chnNum]), pthread_kill(ctx->thread, 0), errno);
    }

    fs_trace("libimp/FS: enable thread-ready-skip ch=%d fd=%d\n", chnNum, ctx->fd);

    usleep(5000);
    ISP_EnsureLinkStreamOn(0);

    if (fs_stream_on(ctx->fd) < 0) {
        fs_trace("libimp/FS: enable stream-on-fail ch=%d fd=%d\n", chnNum, ctx->fd);
        /* The worker waits for the ENABLED state: it leaves at once. */
        FS_FLAG_STORE(ctx->running, 0);
        fs_stop_worker(chnNum, ctx);
        VBMFlushFrame(chnNum);
        fs_close_chn_fd(chnNum, ctx);
        VBMDestroyPool(chnNum);
        fs_chan_set_state(chnNum, 1);
        pthread_mutex_unlock(&g_fs_lock);
        return -1;
    }
    fs_trace("libimp/FS: enable stream-on-ok ch=%d fd=%d\n", chnNum, ctx->fd);
#if defined(PLATFORM_T31)
    fs_t31_log_isp_driver();
#endif

    /* Only replay the pool if the pre-STREAMON prime queued nothing. Once the
     * driver has already accepted those QBUFs, replaying the same buffers after
     * STREAMON just hits "buffer already in use" and falsely demotes channel 0. */
    if (initial_queued_ok <= 0) {
        queued_ok = VBMFillPool(chnNum);
        fs_trace("libimp/FS: enable post-stream-fill ch=%d queued_ok=%d initial=%d\n",
                 chnNum, queued_ok, initial_queued_ok);
    } else {
        queued_ok = initial_queued_ok;
        fs_trace("libimp/FS: enable post-stream-fill skip ch=%d queued_ok=%d initial=%d\n",
                 chnNum, queued_ok, initial_queued_ok);
    }

    /* Ensure bind/unbind function pointers on the module, before the
     * worker sees ENABLED: from then on it publishes each frame in the
     * output slot at +0x138, which this would clear again. */
    {
        Module *m = g_modules[0][chnNum];
        if (m != NULL) {
            *(uint32_t *)((char *)m + 0x134) = 1;
            *(void **)((char *)m + 0x138) = NULL;
            *(void **)((char *)m + 0x40) = (void *)framesource_bind;
            *(void **)((char *)m + 0x44) = (void *)framesource_unbind;
            fs_bind_trace("libimp/FSB: enable set-bind ch=%d module=%p outcnt=%u bind=%p unbind=%p\n",
                          chnNum, m, *(uint32_t *)((char *)m + 0x134),
                          framesource_bind, framesource_unbind);
        }
    }

    fs_chan_set_state(chnNum, 2);
    fs_trace("libimp/FS: enable state-promote ch=%d state=%d fd=%d\n",
             chnNum, fs_chan_get_state(chnNum), ctx->fd);
    {
        int spins = 0;
        while (spins < 100 && FS_FLAG_LOAD(g_fs_thread_enabled_seen[chnNum]) == 0) {
            usleep(1000);
            spins++;
        }
        fs_trace("libimp/FS: enable worker-sync ch=%d enabled_seen=%d spins=%d\n",
                 chnNum, FS_FLAG_LOAD(g_fs_thread_enabled_seen[chnNum]), spins);
    }

    if (chnNum == 0 && initial_queued_ok <= 0 && queued_ok <= 0) {
        /* The worker already saw ENABLED: stop it as DisableChn does. */
        FS_FLAG_STORE(ctx->running, 0);
        fs_stream_off(ctx->fd);
        fs_stop_worker(chnNum, ctx);
        VBMFlushFrame(chnNum);
        fs_close_chn_fd(chnNum, ctx);
        VBMDestroyPool(chnNum);
        fs_chan_set_state(chnNum, 1);
        pthread_mutex_unlock(&g_fs_lock);
        return -1;
    }

    fs_trace("libimp/FS: enable done ch=%d state=%d fd=%d\n",
             chnNum, fs_chan_get_state(chnNum), ctx->fd);
    pthread_mutex_unlock(&g_fs_lock);
    DMA_RmemStreamStarted();
    return 0;
}

/* After STREAMOFF the worker leaves within one select timeout (25 ms) plus
 * the delivery of a frame it had already dequeued. Anything near a second
 * is a genuine hang (a consumer blocking notify, a VBM mutex, ...). */
#define FS_WORKER_STOP_TIMEOUT_MS 1000
/* Open driver only: how often a hung stop says it is still waiting. */
#define FS_WORKER_STOP_REPORT_MS 5000

/* Returns the time waited in ms (wall clock), or -1 if the worker has not
 * left within timeout_ms. */
static int fs_wait_worker_exit(int chn, int timeout_ms)
{
    uint32_t start = fs_now_ms();
    uint32_t waited = 0;
#if defined(PLATFORM_T31)
    unsigned int seq = g_fs_step_seq[chn];

    /* The worker stays in its current step at least from now on; every
     * step change seen while waiting moves that point. */
    g_fs_step_seen_ms[chn] = start;
#endif
    while (!FS_FLAG_LOAD(g_fs_thread_exited[chn])) {
        uint32_t now = fs_now_ms();

#if defined(PLATFORM_T31)
        if (g_fs_step_seq[chn] != seq) {
            seq = g_fs_step_seq[chn];
            g_fs_step_seen_ms[chn] = now;
        }
#endif
        waited = now - start;
        if (waited >= (uint32_t)timeout_ms)
            return -1;
        usleep(1000);
    }
    return (int)(fs_now_ms() - start);
}

/* Stop the pooling worker once running is cleared and STREAMOFF issued
 * (the driver's wake-up edge for a worker in select() or DQBUF): let it
 * leave by itself and cancel only a worker still running after
 * FS_WORKER_STOP_TIMEOUT_MS. A worker cancelled while it delivers a frame
 * can leave a VBM, group or encoder mutex locked. */
static void fs_stop_worker(int chn, FsChnCtx *ctx)
{
    if (ctx->thread == 0)
        return;
    if (fs_wait_worker_exit(chn, FS_WORKER_STOP_TIMEOUT_MS) < 0) {
        IMP_LOG_ERR("Framesource", "chn%d: pooling thread still running %d ms after STREAMOFF; cancelling it",
                    chn, FS_WORKER_STOP_TIMEOUT_MS);
        pthread_cancel(ctx->thread);
    }
    pthread_join(ctx->thread, NULL);
    ctx->thread = 0;
}

/* Unpublish the channel's fd (release_Frame reads it from +0x1c4) before
 * closing it, so a late release cannot QBUF on it or on its reuse; a
 * release that read it just before is let finish first. */
static void fs_close_chn_fd(int chn, FsChnCtx *ctx)
{
    int fd = ctx->fd;

    if (fd < 0)
        return;
    *(int32_t *)(fs_channel_base(chn) + 0x1c4) = -1;
    ctx->fd = -1;
    VBMWaitReleases(chn);
    fs_close_device(fd);
}

#if defined(PLATFORM_T31)
/* Stop the stream of every channel still enabled. Called on the way out of
 * the process (destructor, fatal signal handler in core/sys_core.c). The
 * stock tx-isp T31 DQBUF sleeps in wait_event_interruptible() regardless
 * of O_NONBLOCK and, once a signal is pending while the channel streams,
 * loops on -ERESTARTSYS without ever returning to user space; the SIGKILL
 * that exit_group() sends to the other threads is such a signal, the
 * thread then spins at 100 % sys and the process stays a zombie until a
 * reboot. STREAMOFF first clears the streaming flag, after which that
 * DQBUF returns -EINVAL whatever signal is pending. Only ioctls here: this
 * may run from a signal handler, with g_fs_lock possibly held. */
void openimp_fs_stream_off_all(void)
{
    int chn;

    for (chn = 0; chn < FS_MAX_CHANNELS; chn++) {
        FsChnCtx *ctx = &g_fs_ctx[chn];

        if (ctx->fd < 0 || ctx->thread == 0)
            continue;
        FS_FLAG_STORE(ctx->running, 0);
        fs_stream_off_quiet(ctx->fd);
    }
}

__attribute__((destructor))
static void framesource_dtor_stream_off(void)
{
    openimp_fs_stream_off_all();
}
#endif

int IMP_FrameSource_DisableChn(int chnNum)
{
    FsChnCtx *ctx;

    if (chnNum < 0 || chnNum >= FS_MAX_CHANNELS) return -1;
    if (gFrameSource == NULL) return -1;

    pthread_mutex_lock(&g_fs_lock);
    ctx = &g_fs_ctx[chnNum];
    if (fs_chan_get_state(chnNum) != 2) {
        pthread_mutex_unlock(&g_fs_lock);
        return 0;
    }

    FS_FLAG_STORE(ctx->running, 0);
#if defined(PLATFORM_T31)
    /* STREAMOFF first, then let the worker leave by itself. Where the
     * worker can be waiting depends on the driver's DQBUF, which fs_dqbuf()
     * has learnt from the frames so far (fs_dqbuf_nonblock_mode):
     *
     * - Open tx-isp (DQBUF honours O_NONBLOCK, real poll): the worker
     *   waits in select(), which STREAMOFF wakes with POLLERR (and which
     *   times out after 25 ms anyway); its DQBUFs never sleep. It cannot be
     *   stuck in the driver, so the cancel fallback is never needed. A
     *   worker still running after the timeout is in a consumer (notify,
     *   direct encode) or a VBM lock; cancelling it there could leave that
     *   lock held, so keep waiting and say where it is.
     * - Stock tx-isp (DQBUF ignores O_NONBLOCK, select() always ready):
     *   the worker sleeps in DQBUF, in wait_event_interruptible() with no
     *   f_flags check. With no buffer queued to the driver (all held by the
     *   VBM ready queue or a consumer: a short enable without a reader, an
     *   idle snapshot channel) it sleeps there until STREAMOFF, so a stop
     *   that waited for it before STREAMOFF would always run into its
     *   timeout. STREAMOFF clears the streaming flag and wakes the queue in
     *   __vb2_queue_cancel; the woken DQBUF returns -EINVAL (its "Streaming
     *   off, will not wait for buffers" line with a dump_stack() is the
     *   isp_printf error level, not a fault). A DQBUF entered after
     *   STREAMOFF returns -EINVAL at once (the worker also checks running
     *   before every DQBUF). Only here a cancel stays as last resort.
     * - Not known yet (no or too few DQBUFs, e.g. a short enable): treated
     *   as stock. On the open driver the worker is then in select() or a
     *   1 ms sleep and leaves long before the timeout.
     *
     * pthread_cancel is a signal. The stock DQBUF re-enters its wait loop on
     * -ERESTARTSYS and, while the channel streams, never returns to user
     * space: a thread cancelled or killed in DQBUF while streaming spins at
     * 100 % sys and cannot be reaped. Cancelling after STREAMOFF cannot hit
     * that loop (the flag is checked before every wait). */
    if (ctx->fd >= 0)
        fs_stream_off(ctx->fd);
    if (ctx->thread != 0) {
        int waited = fs_wait_worker_exit(chnNum, FS_WORKER_STOP_TIMEOUT_MS);
        int nonblock = fs_dqbuf_nonblock_mode();
        const char *driver = fs_t31_isp_driver_name();

        if (waited < 0) {
            static unsigned int timeouts;
            unsigned int n = ++timeouts;
            int step = g_fs_step[chnNum];
            int dq_step = openimp_vbm_dq_step[chnNum];
            /* At least this long: measured from STREAMOFF or from the
             * last step change seen since. */
            uint32_t in_step = fs_now_ms() - g_fs_step_seen_ms[chnNum];
            int cancel = nonblock != FS_DQ_NONBLOCK_HONOURED;

            /* Genuine hang: say where it is (rate-limited: the first 20,
             * then every 20th) and which driver it happened on. */
            if (n <= 20 || n % 20 == 0)
                IMP_LOG_ERR("Framesource", "chn%d: pooling thread still running %d ms after STREAMOFF (#%u) on %s, DQBUF %s: in step %s (dq %s) for >= %u ms, iter %d, thread_entered %d, enabled_seen %d; %s",
                            chnNum, FS_WORKER_STOP_TIMEOUT_MS, n, driver,
                            fs_dqbuf_nonblock_name(nonblock),
                            fs_step_name(step), fs_dq_step_name(dq_step),
                            in_step, g_fs_step_iter[chnNum],
                            FS_FLAG_LOAD(g_fs_thread_entered[chnNum]),
                            FS_FLAG_LOAD(g_fs_thread_enabled_seen[chnNum]),
                            cancel
                                ? "cancelling it (blocking-DQBUF driver fallback)"
                                : "not cancelling (not in the driver), waiting for it");
            if (cancel) {
                pthread_cancel(ctx->thread);
            } else {
                int total = FS_WORKER_STOP_TIMEOUT_MS;

                while ((waited = fs_wait_worker_exit(
                            chnNum, FS_WORKER_STOP_REPORT_MS)) < 0) {
                    total += FS_WORKER_STOP_REPORT_MS;
                    IMP_LOG_ERR("Framesource", "chn%d: pooling thread still running %d ms after STREAMOFF on %s: in step %s (dq %s)",
                                chnNum, total, driver,
                                fs_step_name(g_fs_step[chnNum]),
                                fs_dq_step_name(openimp_vbm_dq_step[chnNum]));
                }
                IMP_LOG_WARN("Framesource", "chn%d: pooling thread left %d ms after STREAMOFF on %s",
                             chnNum, total + waited, driver);
            }
        } else {
            IMP_LOG_INFO("Framesource", "chn%d: pooling thread left %d ms after STREAMOFF (%s, DQBUF %s, last step %s)",
                         chnNum, waited, driver,
                         fs_dqbuf_nonblock_name(nonblock),
                         fs_step_name(g_fs_step[chnNum]));
        }
        pthread_join(ctx->thread, NULL);
        ctx->thread = 0;
    }
#else
    /* A frame-channel DQBUF may remain asleep in the kernel even after the
     * worker is cancelled.  STREAMOFF is the driver's wakeup edge, so issue
     * it before joining the pooling thread.  Waiting first deadlocks T21
     * shutdown whenever the worker is between completed frames. */
    if (ctx->fd >= 0) {
        fs_stream_off(ctx->fd);
    }
    /* The drivers wake the worker on STREAMOFF (open tx-isp T23: the DQBUF
     * wait ends with -EPIPE; vb2 on T20/T21: poll reports POLLERR and DQBUF
     * fails) and it checks running before every DQBUF, so it leaves by
     * itself; the cancel is only the fallback for a worker that does not. */
    fs_stop_worker(chnNum, ctx);
#endif
    VBMFlushFrame(chnNum);
    /* Close before the pool memory goes back to the allocator: the ISP
     * driver drops the buffer addresses still queued in its hardware FIFO
     * on release, and freed pool pages are handed out again. */
    fs_close_chn_fd(chnNum, ctx);
    VBMDestroyPool(chnNum);

    fs_chan_set_state(chnNum, 1);
    pthread_mutex_unlock(&g_fs_lock);
    return 0;
}

int IMP_FrameSource_SetSource(int extchnNum, int sourcechnNum)
{
    if (extchnNum < 0 || extchnNum >= FS_MAX_CHANNELS) return -1;
    if (sourcechnNum < 0 || sourcechnNum >= 3) return -1;
    if (gFrameSource == NULL) return -1;

    pthread_mutex_lock(&g_fs_lock);
    *(int32_t *)(fs_channel_base(extchnNum) + 0x240) = sourcechnNum;
    g_fs_ctx[extchnNum].source_chn = sourcechnNum;
    pthread_mutex_unlock(&g_fs_lock);
    return 0;
}

int IMP_FrameSource_GetFrame(int chnNum, void **frame)
{
    if (frame == NULL || chnNum < 0 || chnNum >= FS_MAX_CHANNELS) return -1;
    return VBMGetFrame(chnNum, frame);
}

int IMP_FrameSource_GetTimedFrame(int chnNum, void *framets, int block,
                                  void *framedata, void *frame)
{
    (void)framets; (void)block; (void)framedata; (void)frame;
    if (chnNum < 0 || chnNum >= FS_MAX_CHANNELS) return -1;
    /* BLOCKED: stock timed-frame path pulls from the FIFO queue held on
     * the channel (+0x294) with a gettimeofday-based deadline; openimp
     * exposes VBMGetFrame which is non-blocking. */
    return -1;
}

int IMP_FrameSource_ReleaseFrame(int chnNum, void *frame)
{
    if (chnNum < 0 || chnNum >= FS_MAX_CHANNELS || frame == NULL) return -1;
    return VBMReleaseFrame(chnNum, frame);
}

int IMP_FrameSource_SnapFrame(int chnNum, IMPPixelFormat fmt, int width,
                              int height, void *out_buffer, IMPFrameInfo *info)
{
    IMPFSChnAttr attr;
    void *frame = NULL;
    void *src = NULL;
    int src_size = 0;
    size_t expected = 0;

    if (out_buffer == NULL || info == NULL) return -1;
    if (chnNum < 0 || chnNum >= FS_MAX_CHANNELS) return -1;
    if (IMP_FrameSource_GetChnAttr(chnNum, &attr) < 0) return -1;
    if (attr.picWidth != width || attr.picHeight != height ||
        attr.pixFmt != fmt) {
        return -1;
    }

    for (int i = 0; i < 5; i++) {
        if (VBMGetFrame(chnNum, &frame) == 0 && frame != NULL) break;
        usleep(5000);
    }
    if (frame == NULL) return -1;

    if (VBMFrame_GetBuffer(frame, &src, &src_size) < 0 || src == NULL) {
        VBMReleaseFrame(chnNum, frame);
        return -1;
    }

    if (fmt == PIX_FMT_NV12 || fmt == PIX_FMT_NV21) {
        expected = (size_t)width * height * 3 / 2;
    } else if (fmt == PIX_FMT_YUYV422 || fmt == PIX_FMT_UYVY422) {
        expected = (size_t)width * height * 2;
    } else {
        VBMReleaseFrame(chnNum, frame);
        return -1;
    }
    if (src_size < (int)expected) {
        VBMReleaseFrame(chnNum, frame);
        return -1;
    }

    memcpy(out_buffer, src, expected);
    info->width = width;
    info->height = height;
#if defined(PLATFORM_T31)
    /* A rotated channel (SetChnRotate) delivers the rotated size. */
    memcpy(&info->width, (const uint8_t *)frame + 0x08, sizeof(info->width));
    memcpy(&info->height, (const uint8_t *)frame + 0x0c, sizeof(info->height));
#endif
    VBMReleaseFrame(chnNum, frame);
    return 0;
}

int IMP_FrameSource_SetFrameOffset(int chnNum, int offset)
{
    if (chnNum < 0 || chnNum >= FS_MAX_CHANNELS) return -1;
    (void)offset;
    return 0;
}

int IMP_FrameSource_EnableChnUndistort(int chnNum)
{
    if (chnNum < 0 || chnNum >= FS_MAX_CHANNELS) return -1;
    return 0;
}

int IMP_FrameSource_DisableChnUndistort(int chnNum)
{
    if (chnNum < 0 || chnNum >= FS_MAX_CHANNELS) return -1;
    return 0;
}

#if defined(PLATFORM_T31)
/* ---------------------------------------------------------------------
 * Channel rotation (HLIL 0xa3a90 IMP_FrameSource_SetChnRotate, rotate step
 * of on_framesource_group_data_update at 0x9aa5c; docs/T31_ROTATE.md).
 *
 * The vendor rotates in software: every dequeued NV12 frame is rotated
 * into a scratch buffer and copied back over the capture buffer before any
 * consumer (OSD, IVS, encoder) sees it, and frame+0x28 (rotate_osdflag)
 * is set. The encoder channel is created with the rotated size.
 *
 * Runs on the FrameSource dequeue thread through
 * openimp_fs_rotate_capture() (kernel_interface.c), which owns the scratch
 * buffer. The capture record keeps the landscape size in the vendor
 * library; here it is updated to the rotated size so consumers that read
 * the record (JPEG copy, OSD, IVS) see the geometry they get.
 * ------------------------------------------------------------------- */
#include "framesource/nv12_rotate.h"
#include "dma_alloc.h"

#define FS_FRAME_WIDTH       0x08
#define FS_FRAME_HEIGHT      0x0c
#define FS_FRAME_PIXFMT      0x10
#define FS_FRAME_SIZE        0x14
#define FS_FRAME_VIRT        0x1c
#define FS_FRAME_ROTATE_FLAG 0x28   /* IMPFrameInfo.rotate_osdflag */
#define FS_FOURCC_NV12       0x3231564eu
#define FS_FOURCC_NV21       0x3132564eu

/* SetChnRotate publishes {mode, pre-rotation width, height} as one
 * 32-bit word so the dequeue thread never sees a mode with the other
 * call's size: mode in bits 26..31, width in 13..25, height in 0..12
 * (both <= 4096). */
#define FS_ROT_CFG(mode, w, h) \
    (((uint32_t)(mode) << 26) | ((uint32_t)(w) << 13) | (uint32_t)(h))
#define FS_ROT_CFG_MODE(c)   ((int)((c) >> 26))
#define FS_ROT_CFG_WIDTH(c)  (((c) >> 13) & 0x1fffu)
#define FS_ROT_CFG_HEIGHT(c) ((c) & 0x1fffu)

typedef struct {
    uint32_t cfg;           /* FS_ROT_CFG, written by SetChnRotate */
    /* dequeue thread only (and DestroyChn, once the thread has stopped) */
    uint8_t *scratch;
    size_t   scratch_size;
    uint32_t warned_cfg;    /* cfg the warning was printed for */
    int      warned;
    uint32_t stats_frames;
    uint64_t stats_us;
    uint32_t stats_max_us;
    uint32_t stats_total;
} FsRotate;

static FsRotate g_fs_rotate[FS_MAX_CHANNELS];

static int fs_rotate_stats(void)
{
    static int enabled = -1;

    if (enabled < 0) {
        const char *e = getenv("OPENIMP_FS_ROTATE_STATS");

        enabled = e && e[0] && e[0] != '0';
    }
    return enabled;
}

static uint64_t fs_rotate_now_us(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static void fs_rotate_warn(int chn, FsRotate *r, const char *why,
                           uint32_t a, uint32_t b)
{
    if (r->warned)
        return;
    r->warned = 1;
    fprintf(stderr, "[FS] rotate ch%d disabled for this geometry: %s (%u, %u)\n",
            chn, why, a, b);
}

/* Called for every dequeued capture buffer of channel chn, while the
 * buffer is still private to the dequeue thread. */
void openimp_fs_rotate_capture(int chn, void *frame)
{
    uint8_t *f = frame;
    FsRotate *r;
    int mode;
    uint32_t cfg, w, h, ow, oh, fw, fh, pixfmt, size, virt, need, flag = 1;
    uint64_t t0 = 0;

    if (chn < 0 || chn >= FS_MAX_CHANNELS || !f)
        return;
    r = &g_fs_rotate[chn];
    cfg = __atomic_load_n(&r->cfg, __ATOMIC_ACQUIRE);
    mode = FS_ROT_CFG_MODE(cfg);
    if (cfg != r->warned_cfg) {
        r->warned_cfg = cfg;
        r->warned = 0;
    }
    if (mode == NV12_ROT_NONE) {
        if (r->scratch) {
            free(r->scratch);
            r->scratch = NULL;
            r->scratch_size = 0;
        }
        return;
    }
    w = FS_ROT_CFG_WIDTH(cfg);
    h = FS_ROT_CFG_HEIGHT(cfg);
    if (nv12_rotate_out_dims(mode, w, h, &ow, &oh) != 0)
        return;
    memcpy(&fw, f + FS_FRAME_WIDTH, 4);
    memcpy(&fh, f + FS_FRAME_HEIGHT, 4);
    memcpy(&pixfmt, f + FS_FRAME_PIXFMT, 4);
    memcpy(&size, f + FS_FRAME_SIZE, 4);
    memcpy(&virt, f + FS_FRAME_VIRT, 4);
    need = (uint32_t)nv12_rotate_frame_size(w, h);
    if (pixfmt != FS_FOURCC_NV12 && pixfmt != FS_FOURCC_NV21 &&
        pixfmt != PIX_FMT_NV12 && pixfmt != PIX_FMT_NV21) {
        fs_rotate_warn(chn, r, "pixel format is not NV12/NV21", pixfmt, 0);
        return;
    }
    /* The record holds the landscape size until the first rotated frame,
     * the rotated one afterwards. */
    if (!((fw == w && fh == h) || (fw == ow && fh == oh))) {
        fs_rotate_warn(chn, r, "channel size differs from SetChnRotate",
                       fw, fh);
        return;
    }
    if (!virt || size < need) {
        fs_rotate_warn(chn, r, "capture buffer too small", size, need);
        return;
    }
    if (r->scratch_size < need) {
        void *p = NULL;

        free(r->scratch);
        r->scratch = NULL;
        r->scratch_size = 0;
        if (posix_memalign(&p, 64, need) != 0) {
            fs_rotate_warn(chn, r, "no memory for the rotate buffer", need, 0);
            return;
        }
        r->scratch = p;
        r->scratch_size = need;
        fprintf(stderr, "[FS] rotate ch%d: %ux%u -> %ux%u mode %d (software)\n",
                chn, w, h, ow, oh, mode);
    }
    if (fs_rotate_stats())
        t0 = fs_rotate_now_us();

    /* ISP DMA wrote the buffer: drop stale cached lines before reading;
     * write the rotated picture back before OSD (IPU) / AVPU DMA read it. */
    (void)DMA_RmemFlushCache((void *)(uintptr_t)virt, need, 2);
    nv12_rotate((const uint8_t *)(uintptr_t)virt, r->scratch, w, h, mode);
    memcpy((void *)(uintptr_t)virt, r->scratch, need);
    (void)DMA_RmemFlushCache((void *)(uintptr_t)virt, need, 1);

    memcpy(f + FS_FRAME_WIDTH, &ow, 4);
    memcpy(f + FS_FRAME_HEIGHT, &oh, 4);
    memcpy(f + FS_FRAME_ROTATE_FLAG, &flag, 4);

    if (t0) {
        uint32_t dt = (uint32_t)(fs_rotate_now_us() - t0);

        r->stats_us += dt;
        if (dt > r->stats_max_us)
            r->stats_max_us = dt;
        /* first frame at once, then every 100 frames */
        if (++r->stats_frames == 100u || r->stats_total++ == 0u) {
            fprintf(stderr, "[FS] rotate ch%d %ux%u mode %d: avg %u us max %u us /frame\n",
                    chn, w, h, mode, (uint32_t)(r->stats_us / r->stats_frames),
                    r->stats_max_us);
            r->stats_frames = 0;
            r->stats_us = 0;
            r->stats_max_us = 0;
        }
    }
}

/* Vendor: stores rotTo90 and the pre-rotation size, returns 0; to be
 * called before the channel is created, with the encoder channel set to
 * the rotated size. Here it also takes effect on a running channel from
 * its next frame. */
int IMP_FrameSource_SetChnRotate(int chnNum, int rotTo90, int width, int height)
{
    FsRotate *r;
    int mode = rotTo90 & 0xff;
    uint32_t ow, oh;

    if (chnNum < 0 || chnNum >= FS_MAX_CHANNELS) return -1;
    r = &g_fs_rotate[chnNum];
    if (mode == NV12_ROT_NONE) {
        __atomic_store_n(&r->cfg, FS_ROT_CFG(NV12_ROT_NONE, 0, 0),
                         __ATOMIC_RELEASE);
        return 0;
    }
    if (nv12_rotate_out_dims(mode, 1, 1, &ow, &oh) != 0 ||
        width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
        ((width | height) & 1)) {
        fprintf(stderr, "[FS] SetChnRotate ch%d: unsupported rotTo90=%d %dx%d\n",
                chnNum, mode, width, height);
        return -1;
    }
    __atomic_store_n(&r->cfg, FS_ROT_CFG(mode, width, height),
                     __ATOMIC_RELEASE);
    return 0;
}

/* DestroyChn: the dequeue thread has stopped (the channel is not
 * running), so its scratch buffer can go. The SetChnRotate setting stays,
 * as it is made before CreateChn. */
static void fs_rotate_release(int chn)
{
    FsRotate *r;

    if (chn < 0 || chn >= FS_MAX_CHANNELS)
        return;
    r = &g_fs_rotate[chn];
    free(r->scratch);
    r->scratch = NULL;
    r->scratch_size = 0;
}
#else
/* No rotation path on this SoC: report failure for rotTo90 != 0 so callers
 * keep the stream unrotated with matching encoder dimensions. */
int IMP_FrameSource_SetChnRotate(int chnNum, int rotTo90, int width, int height)
{
    (void)width; (void)height;
    if (chnNum < 0 || chnNum >= FS_MAX_CHANNELS) return -1;
    return (rotTo90 & 0xff) == 0 ? 0 : -1;
}
#endif

int IMP_FrameSource_ChnStatQuery(int chnNum, void *stat)
{
    if (chnNum < 0 || chnNum >= FS_MAX_CHANNELS || stat == NULL) return -1;
    /* stat is an IMPFSChnState enum (4 bytes); vendor stores the channel
     * state word (chan + 0x1c: 0 closed, 1 created, 2 running). The former
     * 64-byte memset overran the caller's variable. */
    *(int32_t *)stat = fs_chan_get_state(chnNum);
    return 0;
}

/* IMP_FrameSource_SetPool / IMP_FrameSource_ClearPoolId live in
 * src/video/imp_mempool.c (T77); no duplicate definition here. */
