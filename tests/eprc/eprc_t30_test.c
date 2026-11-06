/* src/eprc/eprc_t30.c against the OEM T30 rate controller: the vectors were
 * produced by tools/eprc_t30_oracle.py, which runs the OEM T30 1.0.5 code
 * (the uclibc 4.7.2 build the T30X ships) on the same synthetic picture
 * sizes and statistics as below.  An S line with a 19th field 1 uses the
 * "real" statistics (counts in the ranges of a real picture and a non-zero
 * channel cmpx).  H / G lines are H.265 (tools/eprc_t30_oracle.py --h265):
 * per picture the QPs and the CRC-32 of the parameter block and the CU QP
 * map. */
#include "eprc/eprc.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t xorshift(uint32_t s)
{
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

static void statistics(uint32_t frame, uint32_t regs[EPRC30_STAT_REGS])
{
    uint32_t s = 0x9e3779b9u ^ (frame * 0x85ebca6bu);
    int i;

    for (i = 0; i < EPRC30_STAT_REGS; i++) {
        s = xorshift(s);
        regs[i] = s >> (s & 15u);
    }
}

static uint32_t real_statistics(uint32_t frame, uint32_t w, uint32_t h,
                                uint32_t regs[EPRC30_STAT_REGS])
{
    uint32_t mbs = ((w + 15u) / 16u) * ((h + 15u) / 16u);
    uint32_t k = (frame / 20u) % 5u, m;
    int j;

    statistics(frame, regs);
    m = mbs / (1u + k);
    regs[0] = ((regs[0] % (m ? m : 1u)) & 0x7fffu) |
              (((regs[4] % (mbs / 3u ? mbs / 3u : 1u)) & 0x7fffu) << 16);
    regs[1] = (regs[1] % (mbs * (1u + 20u * k))) & 0x7ffffffu;
    regs[2] = (regs[2] % (mbs * (1u + 10u * k))) & 0x7ffffffu;
    for (j = 10; j <= 12; j++)
        regs[j] = regs[j] % (mbs ? mbs : 1u);
    return 1u + (regs[3] % 40000u) * (1u + (frame / 30u) % 3u);
}

static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xffffffffu;
    size_t i;
    int k;

    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
    }
    return ~c;
}

static uint32_t picture_bytes(uint32_t frame, uint32_t qp, int idr,
                              uint64_t pixels, int pattern)
{
    uint64_t cplx = pattern ? ((frame / 40u) % 2u ? 5u : 1u) : 1u + frame % 7u;
    uint64_t size = pixels * cplx / 40u;
    uint32_t i;

    for (i = 0; i < qp; i++)
        size = size * 89u / 100u;
    if (idr)
        size *= 4u;
    if (frame % 37u == 36u)
        size *= 120u;
    return size < 20u ? 20u : (uint32_t)size;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "eprc_t30_vectors.txt";
    FILE *f = fopen(path, "r");
    char line[512];
    Eprc30 rc;
    static uint8_t slice[0x1204];
    static uint8_t qpmap[0x10000];
    EprcParams p;
    int scenario = -1, frame = 0, active = 0, real = 0;
    long checked = 0, failed = 0;

    if (!f) {
        perror(path);
        return 2;
    }
    memset(&rc, 0, sizeof(rc));
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == 'S') {
            int v[19] = {0}, nv;

            nv = sscanf(line, "S %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
                        &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8],
                        &v[9], &v[10], &v[11], &v[12], &v[13], &v[14], &v[15], &v[16],
                        &v[17], &v[18]);
            if (nv != 18 && nv != 19)
                return 2;
            if (active)
                EPRC30_Free(&rc);
            memset(&p, 0, sizeof(p));
            p.rc_mode = (uint32_t)v[0];
            p.width = (uint32_t)v[1];
            p.height = (uint32_t)v[2];
            p.gop = (uint32_t)v[3];
            p.fps_num = (uint32_t)v[4];
            p.fps_den = 1;
            p.min_qp = (uint32_t)v[5];
            p.max_qp = (uint32_t)v[6];
            p.bitrate = (uint32_t)v[7];
            p.max_bitrate = (uint32_t)v[8];
            p.frm_qp_step = (uint32_t)v[9];
            p.gop_qp_step = (uint32_t)v[10];
            p.i_bias = v[11];
            p.static_time = (uint32_t)v[12];
            p.change_pos = (uint32_t)v[13];
            p.quality = (uint32_t)v[14];
            p.init_qp = v[16];
            real = nv == 19 && v[18];
            memset(slice, 0, sizeof(slice));
            if (EPRC30_Init(&rc, &p, slice) != 0)
                return 2;
            active = 1;
            scenario++;
            frame = 0;
        } else if (line[0] == 'H') {
            int v[16], i;
            char *t = line + 1;

            for (i = 0; i < 16; i++)
                v[i] = (int)strtol(t, &t, 10);
            if (active)
                EPRC30_Free(&rc);
            memset(&p, 0, sizeof(p));
            p.codec = 1;
            p.rc_mode = (uint32_t)v[0];
            p.width = (uint32_t)v[1];
            p.height = (uint32_t)v[2];
            p.gop = (uint32_t)v[3];
            p.fps_num = (uint32_t)v[4];
            p.fps_den = 1;
            p.min_qp = (uint32_t)v[5];
            p.max_qp = (uint32_t)v[6];
            p.bitrate = p.max_bitrate = (uint32_t)v[7];
            p.frm_qp_step = (uint32_t)v[8];
            p.gop_qp_step = (uint32_t)v[9];
            p.i_bias = v[10];
            p.static_time = (uint32_t)v[11];
            p.change_pos = (uint32_t)v[12];
            p.quality = (uint32_t)v[13];
            p.init_qp = p.cqp = v[14];
            p.fluct_lvl = (uint32_t)v[15];
            p.ip_ratio = 1.399999976158142;
            memset(slice, 0, sizeof(slice));
            if (EPRC30_Init(&rc, &p, slice) != 0 || EPRC30_QpMapSize(&rc) > sizeof(qpmap))
                return 2;
            EPRC30_SetQpMap(&rc, qpmap, 0x10000000u);
            active = 1;
            scenario++;
            frame = 0;
        } else if (line[0] == 'G' && active) {
            int want[16], n = 0, i, idr;
            unsigned long crc_b, crc_m;
            char *t = line + 1;
            EprcPicture pic;
            uint32_t regs[EPRC30_STAT_REGS], bytes;

            idr = (int)strtol(t, &t, 10);
            crc_b = strtoul(t, &t, 16);
            crc_m = strtoul(t, &t, 16);
            while (n < 16) {
                char *e;
                long v = strtol(t, &e, 10);
                if (e == t)
                    break;
                want[n++] = (int)v;
                t = e;
            }
            EPRC30_FrameStart(&rc, idr, &pic);
            statistics((uint32_t)frame, regs);
            bytes = picture_bytes((uint32_t)frame, pic.qp, idr,
                                  (uint64_t)p.width * p.height, scenario % 2);
            for (i = 0;; i++) {
                checked++;
                if (i >= n || pic.qp != want[i]) {
                    if (failed++ < 10)
                        fprintf(stderr, "%s: scenario %d picture %d step %d: QP %d, OEM %d\n",
                                path, scenario, frame, i, pic.qp, i < n ? want[i] : -1);
                    break;
                }
                if (EPRC30_FrameEnd(&rc, bytes, regs, 0, &pic, 1) != 1)
                    break;
                bytes = bytes / 2u < 20u ? 20u : bytes / 2u;
            }
            if (i + 1 < n && failed++ < 10)
                fprintf(stderr, "%s: scenario %d picture %d: %d re-encodes, OEM %d\n",
                        path, scenario, frame, i, n - 1);
            memset(slice + 0xb4, 0, 4);
            checked++;
            if ((crc32(slice, sizeof(slice)) != crc_b ||
                 crc32(qpmap, EPRC30_QpMapSize(&rc)) != crc_m) && failed++ < 10)
                fprintf(stderr, "%s: scenario %d picture %d: parameter block / QP map differ\n",
                        path, scenario, frame);
            frame++;
        } else if (line[0] == 'F' && active) {
            int want[16], n = 0, since, i, idr;
            char *s = line + 1;
            EprcPicture pic;
            uint32_t regs[EPRC30_STAT_REGS], bytes, cmpx = 0;

            since = (int)strtol(s, &s, 10);
            while (n < 16) {
                char *e;
                long v = strtol(s, &e, 10);
                if (e == s)
                    break;
                want[n++] = (int)v;
                s = e;
            }
            idr = since % (int)p.gop == 0;
            EPRC30_FrameStart(&rc, idr, &pic);
            if (real)
                cmpx = real_statistics((uint32_t)frame, p.width, p.height, regs);
            else
                statistics((uint32_t)frame, regs);
            bytes = picture_bytes((uint32_t)frame, pic.qp, idr,
                                  (uint64_t)p.width * p.height, scenario % 2);
            for (i = 0;; i++) {
                checked++;
                if (i >= n || pic.qp != want[i]) {
                    if (failed++ < 10)
                        fprintf(stderr, "%s: scenario %d picture %d step %d: QP %d, OEM %d\n",
                                path, scenario, frame, i, pic.qp, i < n ? want[i] : -1);
                    break;
                }
                if (EPRC30_FrameEnd(&rc, bytes, regs, cmpx, &pic, 1) != 1)
                    break;
                bytes = bytes / 2u < 20u ? 20u : bytes / 2u;
            }
            if (i + 1 < n && failed++ < 10)
                fprintf(stderr, "%s: scenario %d picture %d: %d re-encodes, OEM %d\n",
                        path, scenario, frame, i, n - 1);
            frame++;
        }
    }
    if (active)
        EPRC30_Free(&rc);
    fclose(f);
    printf("%s: %d scenarios, %ld decisions, %ld differ\n", path, scenario + 1, checked, failed);
    return failed ? 1 : 0;
}
