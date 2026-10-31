/* Host helper for tests/t23/jpegdec_check.py: jpegdec_cli in.jpg yuv420p|nv12|nv21 out.raw -> prints "w h" */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "t23/openimp_t23_jpegdec.h"

int main(int argc, char **argv)
{
    FILE *f;
    uint8_t *in, *pic;
    long n;
    uint32_t w, h;
    int fmt;

    if (argc < 4)
        return 2;
    fmt = !strcmp(argv[2], "nv12") ? T23_JPEGDEC_NV12 : !strcmp(argv[2], "nv21") ? T23_JPEGDEC_NV21 : T23_JPEGDEC_YUV420P;
    f = fopen(argv[1], "rb");
    if (!f)
        return 2;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    in = malloc((size_t)n);
    if (!in || fread(in, 1, (size_t)n, f) != (size_t)n)
        return 2;
    fclose(f);
    if (t23_jpeg_decode(in, (size_t)n, fmt, 0, 0, &pic, &w, &h) != 0) {
        puts("refused");
        return 1;
    }
    f = fopen(argv[3], "wb");
    fwrite(pic, 1, t23_jpeg_size(w, h), f);
    fclose(f);
    printf("%u %u\n", w, h);
    return 0;
}
