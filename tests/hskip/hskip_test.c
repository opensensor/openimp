/* Host test of src/hskip against the OEM decisions of tests/hskip/hskip_vectors.txt
 * (tools/hskip_oracle.py: i264e_init_skip_header / i264e_decide_slice_type_and_rd of libimp 3.12.0). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hskip/hskip.h"

int main(int argc, char **argv)
{
    FILE *f = fopen(argc > 1 ? argv[1] : "tests/hskip/hskip_vectors.txt", "r");
    char line[256];
    HSkip s;
    unsigned gop = 0, typ = 0, m = 0, n = 0, same = 0, a, b, c;
    int pics = 0, bad = 0, scen = 0, left = 0;

    if (!f) {
        perror("vectors");
        return 2;
    }
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == 'S') {
            int w;

            if (sscanf(line, "S %u %u %u %u %u : %u %u %u", &gop, &typ, &m, &n, &same, &a, &b, &c) != 8)
                return 2;
            hskip_init(&s, gop, typ, m, n, same);
            if (s.a != a || s.b != b || s.buffers != c) {
                printf("FAIL init gop %u type %u m %u n %u: %u %u %u, OEM %u %u %u\n", gop, typ, m, n, s.a, s.b, s.buffers, a, b, c);
                bad++;
            }
            w = 0;
            (void)w;
            scen++;
            left = 240;
        } else if (left > 0) {
            unsigned fn, idr, ft, ltr, reo, mm;
            int ref, rec, idcval;
            HSkipPic p;

            if (sscanf(line, "%u %u %u %d %d %u %u %d %u", &fn, &idr, &ft, &ref, &rec, &ltr, &reo, &idcval, &mm) != 9)
                return 2;
            hskip_decide(&s, 0, 0, &p);
            pics++;
            left--;
            if (p.counter != fn || p.idr != (int)idr || p.ftype != ft || p.ref != ref || p.recon != rec ||
                p.ltr_flag != (int)ltr || p.reorder != (int)reo || (p.mmco ? 2u : 0u) != mm ||
                (p.reorder && idcval != 2 * 16 + 0)) {
                if (bad < 20)
                    printf("FAIL gop %u type %u m %u n %u same %u pic %d: fn %u/%u idr %d/%u ft %u/%u ref %d/%d rec %d/%d ltr %d/%u reo %d/%u mmco %d/%u\n",
                           gop, typ, m, n, same, 240 - left - 1, p.counter, fn, p.idr, idr, p.ftype, ft, p.ref, ref, p.recon, rec,
                           p.ltr_flag, ltr, p.reorder, reo, p.mmco ? 2 : 0, mm);
                bad++;
            }
        }
    }
    printf("hskip: %d scenarios, %d pictures, %d differences\n", scen, pics, bad);
    fclose(f);
    if (argc > 2) {
        /* SetChangeRef second try: tools/hskip_oracle.py --changeref */
        int cp = 0, cbad = 0, cscen = 0, cleft = 0, second = 0;

        f = fopen(argv[2], "r");
        if (!f) {
            perror("changeref vectors");
            return 2;
        }
        while (fgets(line, sizeof(line), f)) {
            if (line[0] == 'S') {
                if (sscanf(line, "S %u %u %u %u %u", &gop, &typ, &m, &n, &same) != 5)
                    return 2;
                hskip_init(&s, gop, typ, m, n, same);
                cscen++;
                cleft = 240;
            } else if (cleft > 0) {
                unsigned fn, c208, c209, c2a5, c2a6, c2a7, c2a9, c2aa, c45e;
                int pf, e0, e1, e2, iq;
                HSkipPic p;
                int32_t mpf;

                if (sscanf(line, "%u %d %u %u %u %u %u %u %u %u %d %d %d %d", &fn, &pf, &c208, &c209, &c2a5, &c2a6, &c2a7, &c2a9,
                           &c2aa, &c45e, &e0, &e1, &e2, &iq) != 14)
                    return 2;
                hskip_decide(&s, 0, 0, &p);
                cp++;
                cleft--;
                mpf = hskip_prev_frame(&s, &p);
                if (hskip_qp_from_idr(&s, &p) != iq && cbad++ < 20)
                    printf("FAIL idr qp gop %u type %u m %u n %u same %u pic %d\n", gop, typ, m, n, same, 240 - cleft - 1);
                hskip_remark(&s, &p);
                if (mpf >= 0)
                    second++;
                if (mpf != pf || p.counter != fn ||
                    (mpf >= 0 && (p.reorder != (int)c208 || c209 != 0 ||
                                  (p.mmco ? 2u : 0u) != c2a5 || p.ftype != c45e ||
                                  (p.mmco && (c2a6 != 1 || c2a7 != 0 || c2a9 != 6 || c2aa != 0)) ||
                                  hskip_keep_second(100, 200) != e0 || hskip_keep_second(200, 100) != e1 ||
                                  hskip_keep_second(150, 150) != e2))) {
                    if (cbad < 20)
                        printf("FAIL changeref gop %u type %u m %u n %u same %u pic %d\n", gop, typ, m, n, same, 240 - cleft - 1);
                    cbad++;
                }
            }
        }
        printf("changeref: %d scenarios, %d pictures (%d with a second try), %d differences\n", cscen, cp, second, cbad);
        bad += cbad;
    }
    if (argc > 3) {
        /* bEnableScenecut: tools/hskip_oracle.py --scenecut */
        int sp = 0, sbad = 0, sscen = 0, sleft = 0, hits = 0;

        f = fopen(argv[3], "r");
        if (!f) {
            perror("scenecut vectors");
            return 2;
        }
        while (fgets(line, sizeof(line), f)) {
            if (line[0] == 'S') {
                unsigned k;

                if (sscanf(line, "S %u %u %u %u %u %u", &gop, &typ, &m, &n, &same, &k) != 6)
                    return 2;
                hskip_init(&s, gop, typ, m, n, same);
                sscen++;
                sleft = 240;
            } else if (sleft > 0) {
                unsigned fn, flag, num, den, big, hit;
                HSkipPic p;

                if (sscanf(line, "%u %u %u %u %u %u", &fn, &flag, &num, &den, &big, &hit) != 6)
                    return 2;
                hskip_decide(&s, 0, 0, &p);
                sp++;
                sleft--;
                if (hit)
                    hits++;
                if (p.counter != fn || hskip_scenecut(&s, &p, (int)flag, num, den, big ? 1600u : 400u, 800u) != (int)hit) {
                    if (sbad < 20)
                        printf("FAIL scenecut gop %u type %u m %u n %u same %u pic %d\n", gop, typ, m, n, same, 240 - sleft - 1);
                    sbad++;
                }
            }
        }
        printf("scenecut: %d scenarios, %d pictures (%d turn into an IDR), %d differences\n", sscen, sp, hits, sbad);
        bad += sbad;
    }
    return bad != 0;
}
