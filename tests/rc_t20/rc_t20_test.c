/* src/rc_t20 against the OEM T20 rate controller: rc_t20_vectors.txt was
 * produced by tools/rc_t20_oracle.py, which runs the OEM T20 3.12.0 code
 * (i264e_ratecontrol_init/_start/_is_reenc) on synthetic picture sizes and
 * statistics; this test replays the same inputs through RCT20_Init/Start/End
 * and compares every picture QP, picture type and re-encode decision. */
#include "rc_t20/rc_t20.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "rc_t20_vectors.txt";
    FILE *f = fopen(path, "r");
    char line[512];
    RcT20 rc;
    RcT20Picture pic;
    int active = 0, scenario = -1, frame = 0;
    long checked = 0, failed = 0;

    if (!f) {
        perror(path);
        return 2;
    }
    memset(&rc, 0, sizeof(rc));
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == 'S') {
            RcT20Params p;
            unsigned m, w, h, gop, fn, fd, qp, mn, mx, br, frm, gs, rel, st, mbr,
                chg, q, nmq;
            int bias, si, sp;
            float trig;

            if (sscanf(line + 1, "%u %u %u %u %u %u %u %u %u %u %d %u %u %u %u %u "
                       "%u %u %f %u %d %d", &m, &w, &h, &gop, &fn, &fd, &qp, &mn,
                       &mx, &br, &bias, &frm, &gs, &rel, &st, &mbr, &chg, &q,
                       &trig, &nmq, &si, &sp) != 22) {
                fprintf(stderr, "bad line: %s", line);
                return 2;
            }
            RCT20_DefaultParams(&p);
            p.method = m; p.width = w; p.height = h; p.gop = gop;
            p.fps_num = fn; p.fps_den = fd; p.qp = qp; p.min_qp = mn;
            p.max_qp = mx; p.bitrate = br; p.i_bias = bias; p.frm_qp_step = frm;
            p.gop_qp_step = gs; p.gop_relation = rel; p.static_time = st;
            p.max_bitrate = mbr; p.change_pos = chg; p.quality = q;
            p.new_max_qp_trig = trig; p.new_max_qp = nmq; p.mb_rc = 0;
            p.mb_rc2 = 1; p.super_i_bits = si; p.super_p_bits = sp;
            if (RCT20_Init(&rc, &p) != 0) {
                fprintf(stderr, "init failed\n");
                return 1;
            }
            active = 1;
            scenario++;
        } else if (line[0] == 'F' && active) {
            unsigned idr, qp;

            if (sscanf(line + 1, "%d %u %u", &frame, &idr, &qp) != 3)
                return 2;
            RCT20_Start(&rc, (int)idr, NULL, 0, &pic);
            checked++;
            if (pic.qp != qp || pic.idr != (int)idr) {
                if (failed++ < 10)
                    fprintf(stderr, "scenario %d frame %d: qp %u idr %d, "
                            "expected %u %u\n", scenario, frame, pic.qp,
                            pic.idr, qp, idr);
            }
        } else if (line[0] == 'E' && active) {
            RcT20Stats st;
            unsigned re, qp;
            int r;

            if (sscanf(line + 1, "%u %u %u %u %u %u %u", &st.cmpx, &st.bits,
                       &st.reg[0], &st.reg[1], &st.reg[2], &re, &qp) != 7)
                return 2;
            r = RCT20_End(&rc, &st, &pic);
            checked++;
            if (r != (int)re || (re && pic.qp != qp)) {
                if (failed++ < 10)
                    fprintf(stderr, "scenario %d frame %d: re-encode %d qp %u, "
                            "expected %u %u\n", scenario, frame, r, pic.qp,
                            re, qp);
            }
        }
    }
    fclose(f);
    RCT20_Free(&rc);
    printf("rc_t20: %ld decisions of %d scenarios checked, %ld differ\n",
           checked, scenario + 1, failed);
    return failed ? 1 : 0;
}
