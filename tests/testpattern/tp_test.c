/* Host test of the test pattern API: Set/Get semantics, ABI versioning, range checks, per-mode picture
 * properties (banner box centred, quiet zone white, ramps/bars untouched outside the box, counter,
 * object visible in mode 3), switching 0->1->2->3->0, cap symbol, control socket. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include "openimp_testpattern.h"
#include "openimp_testpattern_gen.h"
#include "openimp_testpattern_rt.h"

#define W 1280
#define H 720
static uint8_t buf[W * H * 3 / 2];
#define Y(r, c) buf[(size_t)(r) * W + (c)]

static int render(struct openimp_tp_chan *ch, int want3)
{
    memset(buf, 0, sizeof buf);
    return openimp_tp_render(ch, buf, W, H, W, H, want3);
}

int main(void)
{
    OpenIMPTestPatternParams p;
    OpenIMPTestPatternState s;
    struct openimp_tp_chan ch;
    uint32_t bx, by, bw, bh, q, r, c;
    uint8_t ref[W * H * 3 / 2];

    assert(OpenIMP_Cap_TestPattern == 1);
    memset(&ch, 0, sizeof ch);
    s.size = sizeof s;
    assert(OpenIMP_TestPattern_Get(&s) == 0 && s.mode == 0 && s.abi == 1 && s.params.logo == 1);
    assert(openimp_testpattern_mode() == 0 && render(&ch, 0) == 0);   /* off: nothing written */

    OpenIMP_TestPattern_ParamsInit(&p);
    assert(OpenIMP_TestPattern_Set(7, &p) == -1 && errno == EINVAL);
    p.logo = 3; assert(OpenIMP_TestPattern_Set(1, &p) == -1);
    p.logo = 1; p.obj_cell_x = 8; assert(OpenIMP_TestPattern_Set(1, &p) == -1);
    p.obj_cell_x = 1; p.size = 2; assert(OpenIMP_TestPattern_Set(1, &p) == -1);
    assert(openimp_testpattern_mode() == 0);                          /* failed Set changes nothing */

    /* mode 1: pattern + centred logo on a white quiet zone */
    OpenIMP_TestPattern_ParamsInit(&p);
    assert(OpenIMP_TestPattern_Set(1, &p) == 0 && render(&ch, 1) == 0);   /* mode 3 hook ignores mode 1 */
    assert(render(&ch, 0) == 1);
    assert(openimp_testpattern_logo_box(W, H, &bx, &by, &bw, &bh, &q));
    assert(bx + bw / 2 + 2 >= W / 2 && bx + bw / 2 <= W / 2 && by + bh / 2 + 2 >= H / 2 && by + bh / 2 <= H / 2);             /* centred */
    assert(bh >= H * 18 / 100 && bh <= H * 23 / 100 && q >= (bh - 2 * q) * 15 / 100);
    assert(by >= H / 3 && by + bh <= 2 * H / 3);                      /* inside the checkerboard third */
    for (c = bx; c < bx + bw; c++) { if (!(Y(by, c) == 235 && Y(by + bh - 1, c) == 235)) printf("c=%u %u %u\n", c, Y(by, c), Y(by + bh - 1, c)); assert(Y(by, c) == 235 && Y(by + bh - 1, c) == 235); }
    {
        int dark = 0;
        for (r = by + q; r < by + bh - q; r++) for (c = bx + q; c < bx + bw - q; c++) dark += Y(r, c) < 100;
        assert(dark > 1000);                                          /* logo drawn */
    }
    /* outside the box identical to the plain pattern */
    memset(ref, 0, sizeof ref);
    openimp_testpattern_nv12_logo(ref, ref + (size_t)W * H, W, H, W, 0);
    for (r = 0; r < H; r++) for (c = 0; c < W; c++)
        if (r < by || r >= by + bh || c < bx || c >= bx + bw) assert(Y(r, c) == ref[(size_t)r * W + c]);
    assert(Y(10, 0) == 0 && Y(10, W - 1) == 255);                     /* ramp ends */
    s.size = sizeof s; OpenIMP_TestPattern_Get(&s);
    assert(s.mode == 1 && s.frames == 1);

    /* no logo: plain pattern */
    p.logo = 0; assert(OpenIMP_TestPattern_Set(1, &p) == 0); render(&ch, 0);
    assert(Y(by, bx) == ref[(size_t)by * W + bx]);
    p.logo = 1;

    /* mode 2: counter restarts at every Set */
    assert(OpenIMP_TestPattern_Set(2, &p) == 0);
    for (r = 0; r < 5; r++) render(&ch, 0);
    for (c = 0, r = 0; r < 16; r++) c = c * 2 + (Y(8, r * 16 + 8) > 128);
    assert(c == 4);
    assert(OpenIMP_TestPattern_Set(2, NULL) == 0); render(&ch, 0);
    for (c = 0, r = 0; r < 16; r++) c = c * 2 + (Y(8, r * 16 + 8) > 128);
    assert(c == 0);

    /* mode 3: built-in object, always visible, 48 px at cell (1,1) with +60 */
    p.obj_size = 48; p.obj_contrast = 60; p.obj_period_ms = 0; p.noise = 0;
    assert(OpenIMP_TestPattern_Set(3, &p) == 0);
    assert(render(&ch, 0) == 0);
    assert(render(&ch, 1) == 1);
    assert(Y(H / 6 * 1 + H / 12, W / 8 * 1 + W / 16) == 110 + 60);
    assert(Y(H - 1, W - 1) == 110);
    s.size = sizeof s; OpenIMP_TestPattern_Get(&s);
    assert(s.mode == 3 && s.objects_active == 1 && s.objects_shown == 1 && s.params.obj_size == 48);

    /* objects bigger than a cell (> 60 px of 640x360) are drawn, and moving ones travel +-half a cell */
    p.obj_size = 96; p.obj_contrast = 60; p.obj_speed = 0;
    assert(OpenIMP_TestPattern_Set(3, &p) == 0 && render(&ch, 0) == 0 && render(&ch, 1) == 1);
    assert(Y(H / 6 * 1 + H / 12, W / 8 * 1 + W / 16) == 110 + 60);
    {   /* moving 48 px object: the centre travels the full +-half cell (W/16) */
        int minx = W, maxx = -1, k, r0 = H / 6 * 2 + H / 12, c;
        double ts;
        for (ts = 0; ts < 4.0; ts += 0.02) {
            memset(buf, 110, sizeof buf);
            openimp_tp_object(buf, W, H, W, 2, 2, 48, 60, 100.0, ts);
            for (c = 0; c < W; c++)
                if (Y(r0, c) == 170) { if (c < minx) minx = c; if (c > maxx) maxx = c; }
        }
        k = (maxx - minx) - (48 * W / 640 - 1);
        assert(k >= W / 8 - 4);
    }
    p.obj_speed = 0; p.obj_size = 48; assert(OpenIMP_TestPattern_Set(3, &p) == 0);

    /* ABI: an older (shorter) caller struct keeps the new fields at their defaults */
    {
        struct { uint32_t size, step, logo; } old = { 12, 4, 0 };
        assert(OpenIMP_TestPattern_Set(1, (const OpenIMPTestPatternParams *)&old) == 0);
        s.size = sizeof s; OpenIMP_TestPattern_Get(&s);
        assert(s.params.step == 4 && s.params.logo == 0 && s.params.noise == 1 && s.params.obj_on_ms == 3000);
        s.size = 16; OpenIMP_TestPattern_Get(&s);                      /* short Get */
        assert(s.size == 16 && s.abi == 1 && s.mode == 1);
    }
    assert(OpenIMP_TestPattern_Set(0, NULL) == 0 && openimp_testpattern_mode() == 0 && render(&ch, 0) == 0);
    puts("tp_test: ok");
    return 0;
}
