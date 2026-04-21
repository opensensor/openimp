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

#include "imp/imp_common.h"
#include "imp/imp_encoder.h"
#include "imp/imp_framesource.h"
#include "imp/imp_system.h"
#include "core/module.h"
#include "core/globals.h"
#include "core/imp_alloc.h"
#include "kernel_interface.h"

/* ---------------------------------------------------------------------
 * Compatibility forward declarations (functions ported in other tasks).
 * ------------------------------------------------------------------- */

int32_t IMP_Log_Get_Option(void); /* T69 */
void imp_log_fun(int level, int option, int type, ...); /* T69 */

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
    int fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    if (fd < 0) return;

    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) write(fd, buf, (size_t)n);
    close(fd);
}

static void fs_user_trace(const char *fmt, ...)
{
    char buf[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    imp_log_fun(4, IMP_Log_Get_Option(), 2, "Framesource",
        "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c",
        0x3f0, "frame_pooling_thread", "%s\n", buf);
}

/* Bind callbacks are defined later in the file but are needed during
 * CreateChn so the module can be bindable before EnableChn runs. */
int IMP_FrameSource_CreateChn(int chnNum, IMPFSChnAttr *chn_attr);
int IMP_FrameSource_EnableChn(int chnNum);
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

static void fs_hal_promote_channel(int chnNum, const IMPFSChnAttr *chn_attr)
{
    int rc;

    if (chnNum < 0 || chnNum >= FS_MAX_CHANNELS) return;
    if (chn_attr == NULL) return;
    if (chn_attr->type != FS_PHY_CHANNEL) return;
    if (gFrameSource == NULL) return;
    if (fs_chan_get_state(chnNum) != 1) return;

    fs_bind_trace("libimp/FSB: auto-promote enter ch=%d state=%d type=%u\n",
                  chnNum, fs_chan_get_state(chnNum), (unsigned)chn_attr->type);
    rc = IMP_FrameSource_EnableChn(chnNum);
    fs_bind_trace("libimp/FSB: auto-promote exit ch=%d rc=%d state=%d\n",
                  chnNum, rc, fs_chan_get_state(chnNum));
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
                printf("err: param1 = %d\n");
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
static volatile int g_fs_thread_entered[FS_MAX_CHANNELS];
static volatile int g_fs_thread_enabled_seen[FS_MAX_CHANNELS];

static void fs_bind_trace(const char *fmt, ...)
{
    int fd = open("/dev/kmsg", O_WRONLY);
    if (fd < 0) return;

    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n > 0) write(fd, buf, (size_t)n);
    close(fd);
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
    if (c) *(int32_t *)(c + 0x1c) = state;
}

static inline int32_t fs_chan_get_state(int chn)
{
    uint8_t *c = fs_channel_base(chn);
    return c ? *(int32_t *)(c + 0x1c) : 0;
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
    g_fs_thread_entered[chn] = 1;
    write(2, "FSDBG thread_entry\n", 19);

    fs_bind_trace("libimp/FSB: pooling-thread start ch=%d ctx=%p arg=%p ra=%p\n",
                  chn, ctx, arg, fs_retaddr());
    fs_thread_trace("libimp/FS: thread-mark ch=%d step=start ctx=%p fd=%d running=%d\n",
                    chn, ctx, ctx->fd, ctx->running);

    snprintf(name, sizeof(name), "FS(%d)-tick", chn);
    prctl(PR_SET_NAME, name);
    dprintf(2, "FSDBG ch=%d step=post_prctl fd=%d running=%d\n", chn, ctx->fd, ctx->running);
    while (ctx->running) {
        void *frame = NULL;
        Module *m;
        int ch_state;

        pthread_testcancel();
        poll_count++;
        if (poll_count <= 2) {
            fs_thread_trace("libimp/FS: thread-mark ch=%d step=loop-top iter=%d fd=%d running=%d\n",
                            chn, poll_count, ctx->fd, ctx->running);
            dprintf(2, "FSDBG ch=%d step=loop_top iter=%d fd=%d running=%d\n",
                    chn, poll_count, ctx->fd, ctx->running);
        }
        ch_state = fs_chan_get_state(chn);
        if (ch_state != last_state) {
            fs_trace("libimp/FS: thread-state ch=%d iter=%d state=%d fd=%d running=%d\n",
                     chn, poll_count, ch_state, ctx->fd, ctx->running);
            last_state = ch_state;
        }

        if (ch_state != 2 || ctx->fd < 0) {
            state_wait_count++;
            if (state_wait_count <= 3 || (state_wait_count % 50) == 0) {
                fs_trace("libimp/FS: thread-wait-enabled ch=%d iter=%d state=%d fd=%d waits=%d\n",
                         chn, poll_count, ch_state, ctx->fd, state_wait_count);
            }
            usleep(10000);
            continue;
        }
        state_wait_count = 0;
        if (!first_enabled_seen) {
            first_enabled_seen = 1;
            g_fs_thread_enabled_seen[chn] = 1;
            fs_trace("libimp/FS: thread-enabled ch=%d iter=%d state=%d fd=%d\n",
                     chn, poll_count, ch_state, ctx->fd);
        }

        if (!software_mode) {
            int flags;
            fd_set rfds;
            struct timeval tv;
            int select_ret;

            if (poll_count <= 2) {
                fs_thread_trace("libimp/FS: thread-mark ch=%d step=select iter=%d fd=%d state=%d errno=%d\n",
                                chn, poll_count, ctx->fd, ch_state, errno);
                dprintf(2, "FSDBG ch=%d step=before_select iter=%d fd=%d state=%d\n",
                        chn, poll_count, ctx->fd, ch_state);
            }

            FD_ZERO(&rfds);
            FD_SET(ctx->fd, &rfds);
            tv.tv_sec = 0;
            tv.tv_usec = 25000;
            select_ret = select(ctx->fd + 1, &rfds, NULL, NULL, &tv);
            fs_trace("libimp/FS: pooling select ch=%d fd=%d ret=%d errno=%d\n",
                     chn, ctx->fd, select_ret, errno);
            if (select_ret < 0) {
                if (errno == EINTR) {
                    continue;
                }
                no_frame_cycles++;
                if (no_frame_cycles <= 5 || (no_frame_cycles % 50) == 0) {
                    fs_trace("libimp/FS: pooling select-fail ch=%d fd=%d idle=%d\n",
                             chn, ctx->fd, no_frame_cycles);
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
                continue;
            }

            /* tx-isp's capture contract is driven by STREAM_ON + REQBUFS/QBUF/
             * DQBUF. select() already told us the fd is readable, and some
             * open-tx-isp variants leave the private POLL_FRAME ioctl parked
             * indefinitely here, which prevents any frame from ever reaching
             * the encoder path. */
            fs_trace("libimp/FS: pooling select-ready ch=%d fd=%d state=%d\n",
                     chn, ctx->fd, ch_state);
            fs_trace("libimp/FS: pooling dq-path ch=%d fd=%d mode=select-then-dq\n",
                     chn, ctx->fd);

            flags = fcntl(ctx->fd, F_GETFL, 0);
            if (flags >= 0 && (flags & O_NONBLOCK) == 0) {
                if (fcntl(ctx->fd, F_SETFL, flags | O_NONBLOCK) == 0) {
                    fs_trace("libimp/FS: pooling set-nonblock ch=%d fd=%d old=0x%x\n",
                             chn, ctx->fd, flags);
                }
            }

            while (1) {
                if (poll_count <= 2) {
                    fs_thread_trace("libimp/FS: thread-mark ch=%d step=dq-drain iter=%d fd=%d state=%d errno=%d\n",
                                    chn, poll_count, ctx->fd, ch_state, errno);
                    dprintf(2, "FSDBG ch=%d step=before_dq iter=%d fd=%d state=%d\n",
                            chn, poll_count, ctx->fd, ch_state);
                }
                fs_trace("libimp/FS: pooling dequeue-enter ch=%d fd=%d\n",
                         chn, ctx->fd);
                {
                    int dq_ret = VBMKernelDequeue(chn, ctx->fd, &frame);
                    fs_trace("libimp/FS: pooling dequeue ch=%d fd=%d ret=%d frame=%p\n",
                             chn, ctx->fd, dq_ret, frame);
                    if (dq_ret == 0 && frame != NULL) {
                        fs_user_trace("pooling dequeue-ok ch=%d fd=%d frame=%p", chn, ctx->fd, frame);
                    }
                    if (dq_ret != 0 || frame == NULL) {
                        if (dq_ret == -2 || dq_ret == 0) {
                            if (no_frame_cycles <= 5 || (no_frame_cycles % 50) == 0) {
                                fs_trace("libimp/FS: pooling dequeue-empty ch=%d fd=%d idle=%d state=%d\n",
                                         chn, ctx->fd, no_frame_cycles, ch_state);
                            }
                        }
                        break;
                    }
                }
                no_frame_cycles = 0;

                m = g_modules[0][chn];
                if (m != NULL) {
                    fs_user_trace("pooling notify ch=%d module=%p frame=%p observers=%d",
                                  chn, m, frame, *(int32_t *)((char *)m + 0x3c));
                    fs_trace("libimp/FS: pooling ch=%d fd=%d module=%p frame=%p notify start\n",
                             chn, ctx->fd, m, frame);
                    framesource_publish_outputs(m, frame);
                    notify_observers(m, frame);
                    if (*(int32_t *)((char *)m + 0x3c) == 0) {
                        fs_trace("libimp/FS: pooling ch=%d fd=%d module=%p frame=%p notify-empty direct-enc\n",
                                 chn, ctx->fd, m, frame);
                        fs_direct_encoder_fallback(chn, frame);
                    }
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
             chn, ctx->fd, ctx->running, fs_chan_get_state(chn));
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

    if (chnNum >= FS_MAX_CHANNELS) {
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
    if (chnNum >= FS_MAX_CHANNELS) {
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

    if (chnNum >= FS_MAX_CHANNELS) {
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

    if (chnNum >= FS_MAX_CHANNELS) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Framesource",
            "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c",
            0x54c, tag, "%s(): Invalid chnNum %d\n", tag, chnNum);
        return -1;
    }
    if (gFrameSource == NULL) return -1;

    {
        uint8_t *chan = fs_channel_base(chnNum);
        pthread_mutex_t *chan_lock = (pthread_mutex_t *)(chan + 0x208);
        int state = *(int32_t *)(chan + 0x1c);

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
            g_fs_ctx[chnNum].frame_depth = 0;
            pthread_mutex_unlock(chan_lock);
            return 0;
        }

        /* BLOCKED (partial): stock allocates three 0xc-byte list heads and
         * `depth` nodes + calloc(0x30) per-node state + calloc(size) for
         * the buffer at +0x1c when copy_type==0. For openimp we store the
         * depth in the ctx and let the kernel pool carry the frame-depth
         * via fs_set_depth. This matches existing imp_framesource.c. */
        pthread_mutex_lock(chan_lock);
        g_fs_ctx[chnNum].frame_depth = depth;
        *(int32_t *)(chan + 0x1cc) = depth;
        pthread_mutex_unlock(chan_lock);
        if (state == 2 && g_fs_ctx[chnNum].fd >= 0) {
            fs_set_depth(g_fs_ctx[chnNum].fd, depth);
        }
        return 0;
    }
}

int IMP_FrameSource_GetFrameDepth(int chnNum, int *depth)
{
    if (chnNum >= FS_MAX_CHANNELS || depth == NULL) {
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
    if (chnNum >= FS_MAX_CHANNELS) {
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
    g_fs_ctx[chnNum].fd = -1;
    pthread_mutex_unlock(&g_fs_lock);
    fs_hal_promote_channel(chnNum, chn_attr);
    return 0;
}

int IMP_FrameSource_DestroyChn(int chnNum)
{
    const char *tag = "IMP_FrameSource_DestroyChn";

    if (chnNum >= FS_MAX_CHANNELS) {
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
        pthread_mutex_unlock(&g_fs_lock);
        return -1;
    }

    if (g_fs_ctx[chnNum].subject != NULL) {
        destroy_group(g_fs_ctx[chnNum].subject, 0);
        g_fs_ctx[chnNum].subject = NULL;
    }

    fs_chan_set_state(chnNum, 0);
    *(int32_t *)((char *)gFrameSource + 0x14) -= 1;
    g_fs_ctx[chnNum].created = 0;
    g_fs_ctx[chnNum].running = 0;
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
    int thread_started = 0;

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
    fs_trace("libimp/FS: enable start ch=%d state=%d ctx=%p fd=%d attr=%dx%d fmt=0x%x nrVBs=%d depth=%d\n",
             chnNum, fs_chan_get_state(chnNum), ctx, ctx->fd,
             ctx->attr.picWidth, ctx->attr.picHeight, ctx->attr.pixFmt,
             ctx->attr.nrVBs, ctx->frame_depth);

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

    if (fs_set_format(ctx->fd, &fmt) < 0) {
        fs_trace("libimp/FS: enable set-format-fail ch=%d fd=%d\n", chnNum, ctx->fd);
        fs_close_device(ctx->fd);
        ctx->fd = -1;
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
    /* OEM T31 path honors the channel's requested VB count here.
     * Raptor configures nrVBs=2 and the stock driver/libimp sequence
     * uses REQBUFS(2). Forcing 3 changes both the VBM pool geometry and
     * the frame-channel REQBUFS depth, which is a concrete divergence from
     * the working path. Keep a minimum of 2 so invalid/zero configs still
     * produce a usable queue, but do not silently inflate 2 -> 3. */
    if (vbm_count < 2) vbm_count = 2;
    memcpy(vbm_fmt + 0x34, &vbm_count, sizeof(int));

    if (VBMCreatePool(chnNum, vbm_fmt, g_fs_vbm_ops, gFrameSource) < 0) {
        fs_trace("libimp/FS: enable VBMCreatePool-fail ch=%d\n", chnNum);
        fs_close_device(ctx->fd);
        ctx->fd = -1;
        pthread_mutex_unlock(&g_fs_lock);
        return -1;
    }

    requested_bufcnt = vbm_count + (ctx->frame_depth > 0 ? ctx->frame_depth : 0);
    bufcnt = fs_set_buffer_count(ctx->fd, requested_bufcnt);
    if (bufcnt < 0) {
        fs_trace("libimp/FS: enable set-bufcnt-fail ch=%d req=%d\n", chnNum, requested_bufcnt);
        VBMDestroyPool(chnNum);
        fs_close_device(ctx->fd);
        ctx->fd = -1;
        pthread_mutex_unlock(&g_fs_lock);
        return -1;
    }
    fs_trace("libimp/FS: enable set-bufcnt-ok ch=%d req=%d got=%d\n",
             chnNum, requested_bufcnt, bufcnt);

    *(int32_t *)(chan + 0x1c4) = ctx->fd;

    /* OEM sequence includes 0x800456c5 during channel enable. The HLIL for
     * frame_channel_unlocked_ioctl shows this path dispatching a remote event
     * as part of the enable flow, so openimp should not gate it on an explicit
     * frame-depth setting from the app. Use nrVBs as the bank count because it
     * matches the pool shape we just configured with REQBUFS. */
    {
        int banks = ctx->attr.nrVBs;
        if (banks < 1) {
            banks = 1;
        }
        if (fs_set_depth(ctx->fd, banks) < 0) {
            fs_trace("libimp/FS: enable set-banks-fail ch=%d fd=%d banks=%d depth=%d\n",
                     chnNum, ctx->fd, banks, ctx->frame_depth);
            VBMFlushFrame(chnNum);
            VBMDestroyPool(chnNum);
            fs_close_device(ctx->fd);
            ctx->fd = -1;
            ctx->running = 0;
            pthread_mutex_unlock(&g_fs_lock);
            return -1;
        }
        fs_trace("libimp/FS: enable set-banks-ok ch=%d fd=%d banks=%d depth=%d\n",
                 chnNum, ctx->fd, banks, ctx->frame_depth);
    }

    queued_ok = VBMFillPool(chnNum);
    if (queued_ok < 0) {
        fs_trace("libimp/FS: enable VBMFillPool-fail ch=%d\n", chnNum);
        VBMDestroyPool(chnNum);
        fs_close_device(ctx->fd);
        ctx->fd = -1;
        pthread_mutex_unlock(&g_fs_lock);
        return -1;
    }
    fs_trace("libimp/FS: enable fill-pool-ok ch=%d queued_ok=%d\n", chnNum, queued_ok);

    ctx->running = 1;

    {
        static int chn_id_storage[FS_MAX_CHANNELS];
        chn_id_storage[chnNum] = chnNum;
        g_fs_thread_entered[chnNum] = 0;
        g_fs_thread_enabled_seen[chnNum] = 0;
        if (pthread_create(&ctx->thread, NULL, frame_pooling_thread,
                           &chn_id_storage[chnNum]) != 0) {
            fs_trace("libimp/FS: enable pthread-create-fail ch=%d fd=%d\n", chnNum, ctx->fd);
            fs_stream_off(ctx->fd);
            VBMFlushFrame(chnNum);
            VBMDestroyPool(chnNum);
            fs_close_device(ctx->fd);
            ctx->fd = -1;
            ctx->running = 0;
            fs_chan_set_state(chnNum, 1);
            pthread_mutex_unlock(&g_fs_lock);
            return -1;
        }
        thread_started = 1;
        fs_trace("libimp/FS: enable pthread-create-ok ch=%d tid=%p arg=%p\n",
                 chnNum, (void *)ctx->thread, &chn_id_storage[chnNum]);
        usleep(20000);
        fs_trace("libimp/FS: enable thread-probe ch=%d entered=%d kill0=%d errno=%d\n",
                 chnNum, g_fs_thread_entered[chnNum], pthread_kill(ctx->thread, 0), errno);
    }

    fs_trace("libimp/FS: enable thread-ready-skip ch=%d fd=%d\n", chnNum, ctx->fd);

    usleep(5000);
    ISP_EnsureLinkStreamOn(0);

    if (fs_stream_on(ctx->fd) < 0) {
        fs_trace("libimp/FS: enable stream-on-fail ch=%d fd=%d\n", chnNum, ctx->fd);
        if (thread_started) {
            pthread_cancel(ctx->thread);
            pthread_join(ctx->thread, NULL);
            ctx->thread = 0;
            thread_started = 0;
        }
        VBMFlushFrame(chnNum);
        VBMDestroyPool(chnNum);
        fs_close_device(ctx->fd);
        ctx->fd = -1;
        ctx->running = 0;
        fs_chan_set_state(chnNum, 1);
        pthread_mutex_unlock(&g_fs_lock);
        return -1;
    }
    fs_trace("libimp/FS: enable stream-on-ok ch=%d fd=%d\n", chnNum, ctx->fd);

    /* The open tx-isp path can accept pre-STREAMON QBUFs without making them
     * deliverable to DQBUF immediately. Replay the pool after STREAMON so the
     * hardware path has active buffers in its final streaming state. */
    queued_ok = VBMFillPool(chnNum);
    fs_trace("libimp/FS: enable post-stream-fill ch=%d queued_ok=%d\n",
             chnNum, queued_ok);

    fs_chan_set_state(chnNum, 2);
    fs_trace("libimp/FS: enable state-promote ch=%d state=%d fd=%d\n",
             chnNum, fs_chan_get_state(chnNum), ctx->fd);
    {
        int spins = 0;
        while (spins < 100 && g_fs_thread_enabled_seen[chnNum] == 0) {
            usleep(1000);
            spins++;
        }
        fs_trace("libimp/FS: enable worker-sync ch=%d enabled_seen=%d spins=%d\n",
                 chnNum, g_fs_thread_enabled_seen[chnNum], spins);
    }

    if (chnNum == 0 && queued_ok <= 0) {
        fs_stream_off(ctx->fd);
        if (thread_started) {
            pthread_cancel(ctx->thread);
            pthread_join(ctx->thread, NULL);
            ctx->thread = 0;
        }
        VBMFlushFrame(chnNum);
        VBMDestroyPool(chnNum);
        fs_close_device(ctx->fd);
        ctx->fd = -1;
        ctx->running = 0;
        fs_chan_set_state(chnNum, 1);
        pthread_mutex_unlock(&g_fs_lock);
        return -1;
    }

    /* Ensure bind/unbind function pointers on the module. */
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

    fs_trace("libimp/FS: enable done ch=%d state=%d fd=%d\n",
             chnNum, fs_chan_get_state(chnNum), ctx->fd);
    pthread_mutex_unlock(&g_fs_lock);
    return 0;
}

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

    ctx->running = 0;
    if (ctx->thread != 0) {
        pthread_cancel(ctx->thread);
        pthread_join(ctx->thread, NULL);
        ctx->thread = 0;
    }

    if (ctx->fd >= 0) {
        fs_stream_off(ctx->fd);
    }
    VBMFlushFrame(chnNum);
    VBMDestroyPool(chnNum);
    if (ctx->fd >= 0) {
        fs_close_device(ctx->fd);
        ctx->fd = -1;
    }

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

int IMP_FrameSource_SetChnRotate(int chnNum, int rotation, int height, int width)
{
    (void)rotation; (void)height; (void)width;
    if (chnNum < 0 || chnNum >= FS_MAX_CHANNELS) return -1;
    return 0;
}

int IMP_FrameSource_ChnStatQuery(int chnNum, void *stat)
{
    if (chnNum < 0 || chnNum >= FS_MAX_CHANNELS || stat == NULL) return -1;
    memset(stat, 0, 64);
    return 0;
}

/* IMP_FrameSource_SetPool / IMP_FrameSource_ClearPoolId live in
 * src/video/imp_mempool.c (T77); no duplicate definition here. */
