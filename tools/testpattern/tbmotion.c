/* tbmotion: host evaluation of OPENIMP_TEST_PATTERN=2 (moving square + frame counter)
 *   tbmotion dec.yuv W H [step] [types.txt]
 * dec.yuv  = all decoded frames as I420 (ffmpeg -vsync passthrough -f rawvideo -pix_fmt yuv420p)
 * types.txt = one picture type letter (I/P/B) per line, in decode order (optional)
 * Per frame: counter n read from the 16 cells, expected picture rebuilt from n (same
 * generator/overlay as the device), PSNR/SSIM Y, position error of the square, ghost pixels
 * (dark pixels in the square's row band outside the expected square = smear/reference error).
 * Summary: drops (counter gaps), duplicates, reordering, PSNR/SSIM per picture type.        */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/openimp_testpattern_gen.h"

static double psnr(const unsigned char *a, const unsigned char *b, int stride, int x0, int y0, int x1, int y1)
{ double se = 0; long n = 0;
  for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) { double d = a[y*stride+x] - b[y*stride+x]; se += d*d; n++; }
  if (!n) return 99;
  se /= n;
  return se < 1e-9 ? 99.0 : 10 * log10(255.0 * 255.0 / se); }

static double ssim(const unsigned char *a, const unsigned char *b, int stride, int x0, int y0, int x1, int y1)
{ const double C1 = 6.5025, C2 = 58.5225; double sum = 0; int n = 0;
  for (int by = y0; by + 8 <= y1; by += 4) for (int bx = x0; bx + 8 <= x1; bx += 4) {
    double ma = 0, mb = 0, va = 0, vb = 0, cv = 0;
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) { ma += a[(by+y)*stride+bx+x]; mb += b[(by+y)*stride+bx+x]; }
    ma /= 64; mb /= 64;
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) { double da = a[(by+y)*stride+bx+x]-ma, db = b[(by+y)*stride+bx+x]-mb; va += da*da; vb += db*db; cv += da*db; }
    va /= 63; vb /= 63; cv /= 63;
    sum += ((2*ma*mb + C1) * (2*cv + C2)) / ((ma*ma + mb*mb + C1) * (va + vb + C2)); n++; }
  return n ? sum / n : 0; }

static void ref_frame(int w, int h, unsigned n, unsigned step, unsigned char *ry, unsigned char *ru, unsigned char *rv)
{ unsigned char *nv = calloc(1, (size_t)w * h * 3 / 2);
  openimp_testpattern_nv12(nv, nv + (size_t)w * h, w, h, w);
  openimp_testpattern_overlay(nv, nv + (size_t)w * h, w, h, w, n, step);
  memcpy(ry, nv, (size_t)w * h);
  for (int r = 0; r < h / 2; r++) for (int c = 0; c < w / 2; c++) {
    ru[r*(w/2)+c] = nv[(size_t)w*h + r*w + 2*c]; rv[r*(w/2)+c] = nv[(size_t)w*h + r*w + 2*c + 1]; }
  free(nv); }

int main(int argc, char **argv)
{
  if (argc < 4) { fprintf(stderr, "usage: tbmotion dec.yuv W H [step] [types.txt]\n"); return 2; }
  int w = atoi(argv[2]), h = atoi(argv[3]); unsigned step = argc > 4 ? atoi(argv[4]) : 8; step = step < 2 ? 2 : step & ~1u;
  size_t ys = (size_t)w * h, fs = ys * 3 / 2; FILE *f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 1; }
  FILE *tf = argc > 5 ? fopen(argv[5], "r") : NULL;
  unsigned char *d = malloc(fs), *ry = malloc(ys), *ru = malloc(ys / 4), *rv = malloc(ys / 4);
  int idx = 0, bad = 0, drops = 0, dups = 0, reord = 0, first = -1, last = -1; long pos_bad = 0, ghost_frames = 0;
  double sum[3] = {0}, sums[3] = {0}, mn[3] = {99, 99, 99}, mns[3] = {1, 1, 1}; int cnt[3] = {0}; char tp[64];
  int maxgap = 0;
  printf("idx type    n  psnrY  ssimY  psnrU  psnrV  posErr ghostPx\n");
  while (fread(d, 1, fs, f) == fs) {
    unsigned n = 0; int ok = 1;
    for (int b = 0; b < 16; b++) { int v = d[8 * w + b * 16 + 8]; n = n << 1 | (v > 125); }
    char t = 'P'; if (tf && fgets(tp, sizeof tp, tf)) t = tp[0]; else if (tf) t = '?';
    ref_frame(w, h, n, step, ry, ru, rv);
    unsigned s, y0, x0; openimp_testpattern_square(w, h, n, step, &s, &y0, &x0);
    double py = psnr(ry, d, w, 0, 0, w, h), ss = ssim(ry, d, w, 0, 0, w, h);
    double pu = psnr(ru, d + ys, w / 2, 0, 0, w / 2, h / 2), pv = psnr(rv, d + ys + ys / 4, w / 2, 0, 0, w / 2, h / 2);
    /* square position from the middle row of the band: dark run */
    int row = y0 + s / 2, xa = -1, xb = -1;
    for (int x = 0; x < w; x++) if (d[(size_t)row * w + x] < 80) { if (xa < 0) xa = x; xb = x; }
    int err = xa < 0 ? 9999 : (xa + xb + 1) / 2 - (int)(x0 + s / 2);
    /* ghosts: dark pixels in the band outside expected square +-4 px */
    long gh = 0;
    for (unsigned r = y0; r < y0 + s && r < (unsigned)h; r++) for (int x = 0; x < w; x++) {
      if (x >= (int)x0 - 4 && x < (int)(x0 + s) + 4) continue;
      if (d[(size_t)r * w + x] < 80) gh++; }
    if (first < 0) first = n;
    if (last >= 0) { int dn = (int)n - last; if (dn == 0) dups++; else if (dn < 0 && dn > -1000) reord++;
                     else if (dn > 1) { drops += dn - 1; if (dn - 1 > maxgap) maxgap = dn - 1; } }
    last = n; (void)ok;
    int ti = t == 'I' ? 0 : t == 'B' ? 2 : 1;
    sum[ti] += py; sums[ti] += ss; cnt[ti]++; if (py < mn[ti]) mn[ti] = py; if (ss < mns[ti]) mns[ti] = ss;
    if (abs(err) > 4) pos_bad++;
    if (gh > 50) ghost_frames++;
    if (idx < 12 || py < 35 || abs(err) > 4 || gh > 50) printf("%3d %4c %5u %6.2f %6.4f %6.2f %6.2f %6d %7ld\n", idx, t, n, py, ss, pu, pv, err, gh);
    idx++; (void)bad;
  }
  printf("SUMMARY frames %d, counter %d..%d: drops %d (max gap %d), dups %d, reordered %d\n", idx, first, last, drops, maxgap, dups, reord);
  static const char *nm[3] = {"I", "P", "B"};
  for (int i = 0; i < 3; i++) if (cnt[i]) printf("SUMMARY %s frames %d: psnrY mean %.2f min %.2f, ssimY mean %.4f min %.4f\n", nm[i], cnt[i], sum[i]/cnt[i], mn[i], sums[i]/cnt[i], mns[i]);
  printf("SUMMARY position errors >4px: %ld, frames with ghost >50 px: %ld\n", pos_bad, ghost_frames);
  return 0;
}
