/* T40 multi camera entries (host-only, no camera): IMP_ISP_SetCameraInputMode /
 * GetCameraInputMode / SetCameraInputSelect on a private ioctl fixture.
 * Layout, ioctl numbers and result codes follow the vendor T40 1.3.1 libimp
 * disassembly. */
#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#define ioctl test_ioctl
#include "../../src/t40/openimp_p1.c"
#undef ioctl

#ifdef PLATFORM_T41
#error "T40 only"
#endif

/* the public header struct (vendor imp_isp.h IMPISPCameraInputMode) */
typedef struct {
    uint32_t sensor_num, dual_mode;
    struct { uint32_t en, switch_con, switch_con_num; } sw;
    uint32_t joint_mode;
} cam_mode_t;

_Static_assert(sizeof(cam_mode_t) == 24, "mode is 24 bytes");
_Static_assert(offsetof(cam_mode_t, dual_mode) == 4, "dual_mode at 4");
_Static_assert(offsetof(cam_mode_t, sw) == 8, "switch at 8");
_Static_assert(offsetof(cam_mode_t, joint_mode) == 20, "joint_mode at 20");
_Static_assert(TISP_VIDIOC_SET_CAMERA_INPUT_MODE == 0xc0045415U, "mode ioctl");
_Static_assert(TISP_VIDIOC_SET_CAMERA_INPUT_SELECT == 0xc0045419U, "select ioctl");
_Static_assert(sizeof(p1.camera_input) == sizeof(cam_mode_t), "stored copy");

static struct { unsigned long cmd; int fd; unsigned char arg[24]; int calls, result; } io;

int test_ioctl(int fd, unsigned long command, ...)
{
    va_list args;
    void *arg;

    va_start(args, command);
    arg = va_arg(args, void *);
    va_end(args);
    io.fd = fd;
    io.cmd = command;
    io.calls++;
    memcpy(io.arg, arg, command == 0xc0045419UL ? 4 : 24);
    return io.result;
}

int openimp_t31_ivs_source_active(int fs_chn) { (void)fs_chn; return 0; }
void openimp_t31_ivs_capture(int fs_chn, const void *frame) { (void)fs_chn; (void)frame; }
/* link stubs for the rest of the P1 unit, never reached here */
int DMA_AllocDescriptor(IMPDMABufferInfo *i, int size, const char *tag)
{ (void)i; (void)size; (void)tag; abort(); }
int OpenIMP_P2_DMAState(uint32_t *a, uint32_t *b) { (void)a; (void)b; abort(); }
int OpenIMP_P2_DMARegion(uint32_t *a, uint32_t *b, void **c)
{ (void)a; (void)b; (void)c; abort(); }
int DMA_FreePhys(uint32_t p) { (void)p; abort(); }
int DMA_RmemFlushCache(void *a, uint32_t n, int d) { (void)a; (void)n; (void)d; abort(); }
OpenIMPProfileStamp openimp_profile_begin(void) { OpenIMPProfileStamp z; memset(&z, 0, sizeof z); return z; }
void openimp_profile_end(OpenIMPProfileStage st, OpenIMPProfileStamp t) { (void)st; (void)t; }
void openimp_profile_count(OpenIMPProfileCounter c, uint64_t n) { (void)c; (void)n; }
volatile uint32_t openimp_video_drop_frames;
int openimp_video_drop_set(void (*cb)(void)) { (void)cb; return 0; }
int64_t IMP_System_GetTimeStamp(void) { return 0; }
int64_t OpenIMP_P0_NormalizeMonotonicTimeStamp(uint64_t t) { return (int64_t)t; }

static int ok;
#define CHECK(c) do { assert(c); ok++; } while (0)

int main(void)
{
    cam_mode_t m, g;
    uint32_t joints_ok[] = {0, 1, 4, 0x10, 0x40, 0x100, 0x400, 0x1000, 0x4000,
                            0x101, 0x404, 0x1010, 0x3030};
    uint32_t joints_bad[] = {5, 0x50, 0x500, 0x11, 0x14, 0x41,
                             0x1001, 0x4100, 0x8000, 0x4001, 0x4040, 0x10000, 0x7};
    unsigned i;

    memset(&m, 0, sizeof m);
    m.sensor_num = 2;
    m.dual_mode = 4;
    m.sw.en = 1; m.sw.switch_con = 3; m.sw.switch_con_num = 2;

    /* before IMP_ISP_Open */
    CHECK(IMP_ISP_SetCameraInputMode(&m) == -8);
    CHECK(IMP_ISP_GetCameraInputMode(&g) == -8);
    CHECK(IMP_ISP_SetCameraInputSelect(0) == -8);
    CHECK(io.calls == 0);

    prepare_p1();
    p1.isp_open = 1;
    p1.isp_fd = 77;

    CHECK(IMP_ISP_SetCameraInputMode(NULL) == -9);
    CHECK(IMP_ISP_GetCameraInputMode(NULL) == -9);
    CHECK(io.calls == 0);

    /* ranges */
    m.sensor_num = 4; CHECK(IMP_ISP_SetCameraInputMode(&m) == -4);
    m.sensor_num = 3; m.dual_mode = 5; CHECK(IMP_ISP_SetCameraInputMode(&m) == -4);
    CHECK(io.calls == 0);
    m.dual_mode = 4;
    for (i = 0; i < sizeof joints_bad / sizeof *joints_bad; i++) {
        m.joint_mode = joints_bad[i];
        CHECK(IMP_ISP_SetCameraInputMode(&m) == -4);
    }
    CHECK(io.calls == 0);
    for (i = 0; i < sizeof joints_ok / sizeof *joints_ok; i++) {
        m.joint_mode = joints_ok[i];
        CHECK(IMP_ISP_SetCameraInputMode(&m) == 0);
    }
    CHECK(io.calls == (int)(sizeof joints_ok / sizeof *joints_ok));

    /* request encoding: main ISP fd, number, the 24 bytes as given */
    m.sensor_num = 2; m.dual_mode = 2; m.joint_mode = 0;
    io.calls = 0;
    CHECK(IMP_ISP_SetCameraInputMode(&m) == 0);
    CHECK(io.calls == 1 && io.fd == 77 && io.cmd == 0xc0045415UL);
    CHECK(memcmp(io.arg, &m, sizeof m) == 0);
    memset(&g, 0xaa, sizeof g);
    CHECK(IMP_ISP_GetCameraInputMode(&g) == 0);
    CHECK(memcmp(&g, &m, sizeof m) == 0);
    CHECK(io.calls == 1);                       /* Get does not ask the driver */

    /* a failing driver leaves the stored mode alone */
    io.result = -1;
    m.sensor_num = 3;
    CHECK(IMP_ISP_SetCameraInputMode(&m) == -1);
    io.result = 0;
    IMP_ISP_GetCameraInputMode(&g);
    CHECK(g.sensor_num == 2);

    /* select: only in SELECT mode, vinum 0 or 1 */
    io.calls = 0;
    CHECK(IMP_ISP_SetCameraInputSelect(2) == -9);
    CHECK(IMP_ISP_SetCameraInputSelect(-1) == -9);
    CHECK(io.calls == 0);
    CHECK(IMP_ISP_SetCameraInputSelect(1) == 0);
    CHECK(io.calls == 1 && io.fd == 77 && io.cmd == 0xc0045419UL);
    CHECK(*(int32_t *)io.arg == 1);
    m.sensor_num = 2; m.dual_mode = 4;
    CHECK(IMP_ISP_SetCameraInputMode(&m) == 0);
    io.calls = 0;
    CHECK(IMP_ISP_SetCameraInputSelect(0) == -10);
    CHECK(io.calls == 0);
    m.dual_mode = 2;
    CHECK(IMP_ISP_SetCameraInputMode(&m) == 0);
    io.result = -1;
    CHECK(IMP_ISP_SetCameraInputSelect(0) == -1);

    /* P1_INNER phase markers: silent unless OPENIMP_DEBUG_TRACE is set */
    {
        int pfd[2], saved = dup(2);
        char buf[16];
        ssize_t n;

        unsetenv("OPENIMP_DEBUG_TRACE");
        CHECK(pipe(pfd) == 0 && saved >= 0);
        CHECK(fcntl(pfd[0], F_SETFL, O_NONBLOCK) == 0);
        dup2(pfd[1], 2);
        trace_p1("P1_INNER TEST\n");
        dup2(saved, 2);
        n = read(pfd[0], buf, sizeof buf);
        CHECK(n < 0);                   /* nothing was written */
        close(pfd[0]); close(pfd[1]); close(saved);
    }

    printf("t40 camera input: %d checks passed\n", ok);
    return 0;
}
