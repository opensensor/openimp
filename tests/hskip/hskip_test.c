/* Host test of src/hskip against the OEM decisions of tests/hskip/hskip_vectors.txt
 * (tools/hskip_oracle.py: i264e_init_skip_header / i264e_decide_slice_type_and_rd of libimp 3.12.0).
 * With --t21 first: the T21 1.0.33 vectors (hskip_vectors_t21.txt ...), plus the skip attributes after
 * i264e_validate_parameters (argv[5]) and the scene-cut IDR conditions (argv[6]). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hskip/hskip.h"

static int t21;

static void init(HSkip *s, unsigned gop, unsigned typ, unsigned m, unsigned n, unsigned same)
{
    if (t21)
        hskip_init_t21(s, gop, typ, m, n, same);
    else
        hskip_init(s, gop, typ, m, n, same);
}

int main(int argc, char **argv)
{
    FILE *f;
    char line[256];
    HSkip s;
    unsigned maxrec = 0;
    unsigned gop = 0, typ = 0, m = 0, n = 0, same = 0, a, b, c;
    int pics = 0, bad = 0, scen = 0, left = 0;

    if (argc > 1 && !strcmp(argv[1], "--t21")) {
        t21 = 1;
        argv++;
        argc--;
    }
    f = fopen(argc > 1 ? argv[1] : "tests/hskip/hskip_vectors.txt", "r");
    if (!f) {
        perror("vectors");
        return 2;
    }
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == 'S') {
            int w;

            if (sscanf(line, "S %u %u %u %u %u : %u %u %u", &gop, &typ, &m, &n, &same, &a, &b, &c) != 8)
                return 2;
            init(&s, gop, typ, m, n, same);
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
            if (t21 && typ == 0 && (p.recon > 1 || p.ref > 1))
                maxrec++;               /* N1X must never leave the buffers 0 and 1 */
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
    printf("hskip%s: %d scenarios, %d pictures, %d differences\n", t21 ? " (T21)" : "", scen, pics, bad);
    bad += (int)maxrec;
    if (maxrec)
        printf("FAIL N1X uses a third buffer %u times\n", maxrec);
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
                init(&s, gop, typ, m, n, same);
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
                if (hskip_qp_from_idr(&s, &p) != (t21 ? 0 : iq) && cbad++ < 20)
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
                init(&s, gop, typ, m, n, same);
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
    if (t21 && argc > 4) {
        /* i264e_validate_parameters: tools/hskip_oracle.py --validate */
        unsigned vp = 0, vbad = 0;

        f = fopen(argv[4], "r");
        if (!f) {
            perror("validate vectors");
            return 2;
        }
        while (fgets(line, sizeof(line), f)) {
            unsigned mode, g, ty, mm, nn, sm, sc, bl, z, rc, oty, om, on, osm, osc, obl, ogop;
            HSkipAttr a;

            if (sscanf(line, "%u %u %u %u %u %u %u %u %u : %u %u %u %u %u %u %u %u", &mode, &g, &ty, &mm, &nn, &sm, &sc, &bl, &z, &rc,
                       &oty, &om, &on, &osm, &osc, &obl, &ogop) != 17)
                return 2;
            a.type = ty; a.m = mm; a.n = nn; a.same = sm; a.scenecut = sc; a.black = bl;
            hskip_t21_validate(&a, mode == 3, g);
            vp++;
            if (rc != 0 || a.type != oty || a.m != om || a.n != on || a.same != osm || a.scenecut != osc || a.black != obl || g != ogop) {
                if (vbad < 20)
                    printf("FAIL validate mode %u gop %u type %d m %d n %d same %d sc %u blk %u: type %u m %u n %u same %u sc %u blk %u, OEM %u %u %u %u %u %u\n",
                           mode, g, (int)ty, (int)mm, (int)nn, (int)sm, sc, bl, a.type, a.m, a.n, a.same, a.scenecut, a.black, oty, om, on, osm, osc, obl);
                vbad++;
            }
        }
        printf("validate (T21): %u cases, %u differences\n", vp, vbad);
        bad += (int)vbad;
        fclose(f);
    }
    if (t21 && argc > 5) {
        /* the scene-cut IDR of i264e_decide_slice_type_and_rd: tools/hskip_oracle.py --sceneidr */
        unsigned sp = 0, sbad = 0, scn = 0, sleft = 0, idrs = 0;

        f = fopen(argv[5], "r");
        if (!f) {
            perror("sceneidr vectors");
            return 2;
        }
        while (fgets(line, sizeof(line), f)) {
            if (line[0] == 'S') {
                unsigned k;

                if (sscanf(line, "S %u %u %u %u %u %u", &gop, &typ, &m, &n, &same, &k) != 6)
                    return 2;
                init(&s, gop, typ, m, n, same);
                scn++;
                sleft = 240;
            } else if (sleft > 0) {
                unsigned prev, ctr, idr, me, rm, cl;
                HSkipPic p;
                int scene;

                if (sscanf(line, "%u %u %u %u %u %u", &prev, &ctr, &idr, &me, &rm, &cl) != 6)
                    return 2;
                scene = hskip_t21_scene_idr(s.counter, gop, me >= 2 && me <= 4 && rm != 0, (int)cl);
                hskip_decide(&s, 0, scene, &p);
                sp++;
                sleft--;
                if (idr && prev != 0xffffffffu && ctr == 0)
                    idrs++;
                if (p.counter != ctr || p.idr != (int)idr) {
                    if (sbad < 20)
                        printf("FAIL sceneidr gop %u type %u same %u pic %d: counter %u/%u idr %d/%u\n", gop, typ, same,
                               240 - (int)sleft - 1, p.counter, ctr, p.idr, idr);
                    sbad++;
                }
            }
        }
        printf("sceneidr (T21): %u scenarios, %u pictures (%u IDRs), %u differences\n", scn, sp, idrs, sbad);
        bad += (int)sbad;
        fclose(f);
    }
    return bad != 0;
}
