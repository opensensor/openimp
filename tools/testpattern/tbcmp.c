/* tbcmp: host helper for the encoder test pattern (I420 planar files)
 *  tbcmp gen W H out.yuv                 pattern (openimp_testpattern_gen.h) as I420
 *  tbcmp cmp ref.yuv dec.yuv W H [rot]   stats; rot = 0|cw|ccw: dec is rotated
 *                                         (dec size HxW), rotate back first      */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/openimp_testpattern_gen.h"

static unsigned char *rd(const char *p, size_t n)
{ FILE *f = fopen(p, "rb"); unsigned char *b = malloc(n);
  if (!f || fread(b, 1, n, f) != n) { fprintf(stderr, "read %s\n", p); exit(1); } fclose(f); return b; }

static void gen(int w, int h, unsigned char *out)
{ int pitch = w; unsigned char *nv = malloc((size_t)w * h * 3 / 2);
  openimp_testpattern_nv12(nv, nv + (size_t)w * h, w, h, pitch);
  memcpy(out, nv, (size_t)w * h);
  for (int r = 0; r < h / 2; r++) for (int c = 0; c < w / 2; c++) {
    out[(size_t)w * h + r * (w / 2) + c] = nv[(size_t)w * h + r * w + 2 * c];
    out[(size_t)w * h * 5 / 4 + r * (w / 2) + c] = nv[(size_t)w * h + r * w + 2 * c + 1]; }
  free(nv); }

/* rotate a plane of the decoded (rotated, dw x dh) picture back to w x h */
static void unrot(const unsigned char *s, int dw, int dh, unsigned char *d, int w, int h, int cw)
{ /* original (x,y) -> rotated cw: (h-1-y, x) ; ccw: (y, w-1-x) */
  for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
    int rx = cw ? (h - 1 - y) : y, ry = cw ? x : (w - 1 - x);
    d[y * w + x] = s[ry * dw + rx]; } (void)dh; }

static double psnr(const unsigned char *a, const unsigned char *b, int w, int h, int stride, int x0, int y0, int x1, int y1)
{ double se = 0; long n = 0; (void)h;
  for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) { double d = a[y*stride+x] - b[y*stride+x]; se += d*d; n++; }
  if (!n) return 0; se /= n; return se < 1e-9 ? 99.0 : 10 * log10(255.0 * 255.0 / se); (void)w; }

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

static double mean(const unsigned char *a, int stride, int x0, int y0, int x1, int y1)
{ double s = 0; long n = 0; for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) { s += a[y*stride+x]; n++; } return n ? s / n : 0; }

int main(int argc, char **argv)
{
  if (argc >= 5 && !strcmp(argv[1], "gen")) {
    int w = atoi(argv[2]), h = atoi(argv[3]); size_t n = (size_t)w * h * 3 / 2; unsigned char *o = malloc(n);
    gen(w, h, o); FILE *f = fopen(argv[4], "wb"); fwrite(o, 1, n, f); fclose(f); return 0; }
  if (argc < 6 || strcmp(argv[1], "cmp")) { fprintf(stderr, "usage\n"); return 2; }
  int w = atoi(argv[4]), h = atoi(argv[5]); const char *rot = argc > 6 ? argv[6] : "0";
  size_t n = (size_t)w * h * 3 / 2, ys = (size_t)w * h, cs = ys / 4;
  unsigned char *ref = rd(argv[2], n), *dec = rd(argv[3], n);
  if (strcmp(rot, "0")) { int cw = !strcmp(rot, "cw"); unsigned char *t = malloc(n);
    unrot(dec, h, w, t, w, h, cw); unrot(dec + ys, h / 2, w / 2, t + ys, w / 2, h / 2, cw);
    unrot(dec + ys + cs, h / 2, w / 2, t + ys + cs, w / 2, h / 2, cw); free(dec); dec = t; }
  const unsigned char *rY = ref, *rU = ref + ys, *rV = ref + ys + cs, *dY = dec, *dU = dec + ys, *dV = dec + ys + cs;
  int cw2 = w / 2, ch2 = h / 2;
  printf("ALL psnrY %.2f psnrU %.2f psnrV %.2f ssimY %.4f\n", psnr(rY,dY,w,h,w,0,0,w,h), psnr(rU,dU,cw2,ch2,cw2,0,0,cw2,ch2),
         psnr(rV,dV,cw2,ch2,cw2,0,0,cw2,ch2), ssim(rY,dY,w,0,0,w,h));
  /* range: top ramp ends (luma rows 0..h/3, avoid edges), checkerboard levels */
  int ry0 = h / 12, ry1 = h / 4;
  printf("RANGE rampL ref %.1f dec %.1f | rampR ref %.1f dec %.1f | decmin %d decmax %d (ramp rows)\n",
         mean(rY,w,0,ry0,4,ry1), mean(dY,w,0,ry0,4,ry1), mean(rY,w,w-4,ry0,w,ry1), mean(dY,w,w-4,ry0,w,ry1),
         ({int m=255; for(int y=ry0;y<ry1;y++) for(int x=0;x<w;x++) if(dY[y*w+x]<m) m=dY[y*w+x]; m;}),
         ({int m=0; for(int y=ry0;y<ry1;y++) for(int x=0;x<w;x++) if(dY[y*w+x]>m) m=dY[y*w+x]; m;}));
  { double lo=0,hi=0,rlo=0,rhi=0; long nl=0,nh=0; for (int y=h/3+8;y<2*h/3-8;y++) for (int x=8;x<w-8;x++) { if ((x%32)<6||(x%32)>25||(y%32)<6||(y%32)>25) continue;
      if (rY[y*w+x]<100){lo+=dY[y*w+x];rlo+=rY[y*w+x];nl++;} else {hi+=dY[y*w+x];rhi+=rY[y*w+x];nh++;} }
    printf("CHECKER black ref %.1f dec %.1f | white ref %.1f dec %.1f\n", rlo/nl, lo/nl, rhi/nh, hi/nh); }
  static const char *nm[8] = {"white","yellow","cyan","green","magenta","red","blue","white2"};
  printf("BAR       refY   decY   dY |  refU   decU   dU |  refV   decV   dV | psnrUV  ssimY\n");
  for (int b = 0; b < 8; b++) {
    int cx0 = b * cw2 / 8 + 4, cx1 = (b + 1) * cw2 / 8 - 4, cy0 = h / 6 + 4, cy1 = ch2 - 4;
    double ru = mean(rU,cw2,cx0,cy0,cx1,cy1), du = mean(dU,cw2,cx0,cy0,cx1,cy1), rv = mean(rV,cw2,cx0,cy0,cx1,cy1), dv = mean(dV,cw2,cx0,cy0,cx1,cy1);
    double ry = mean(rY,w,2*cx0,2*cy0,2*cx1,2*cy1), dy = mean(dY,w,2*cx0,2*cy0,2*cx1,2*cy1);
    double pu = psnr(rU,dU,cw2,ch2,cw2,cx0,cy0,cx1,cy1), pv = psnr(rV,dV,cw2,ch2,cw2,cx0,cy0,cx1,cy1);
    printf("BAR %-7s %5.1f %5.1f %+5.1f | %5.1f %5.1f %+5.1f | %5.1f %5.1f %+5.1f | %5.1f %6.4f\n", nm[b], ry, dy, dy-ry, ru, du, du-ru, rv, dv, dv-rv,
           (pu + pv) / 2, ssim(rY,dY,w,2*cx0,2*cy0,2*cx1,2*cy1)); }
  return 0;
}
