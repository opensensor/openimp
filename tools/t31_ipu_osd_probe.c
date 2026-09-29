/*
 * t31_ipu_osd_probe - exercise the T31 IPU OSD blender (/dev/ipu) directly.
 *
 * The stock libimp draws PIC (BGRA bitmap) and COVER regions with the IPU:
 * IOCTL_IPU_START with a 38-word struct ipu_param, up to four OSD channels
 * blended in place into an NV12 frame (jz_ipu_v13 driver, thingino kernel
 * patch). This tool checks the reconstructed parameter words and measures
 * what a pass costs, so the OpenIMP OSD backend can be designed around it:
 *
 *   layer 0  BGRA bitmap, per-pixel alpha           at (32, 32)
 *   layer 1  same bitmap, pixel x global alpha 128  at (32, 128)
 *   layer 2  COVER mask, opaque red                 at (224, 32)
 *   layer 3  COVER mask, blue with alpha 128        at (224, 128)
 *
 * The bitmap has five colour bands (red, green, blue, white, black) and an
 * alpha ramp 0..255 across its width. Outputs (in -d, default /tmp):
 *   ipu-before.nv12, ipu-after.nv12, ipu-bitmap.bgra, ipu-samples.csv
 * Analyse on a PC with tools/ipu_osd_analyze.py.
 *
 * Needs contiguous DMA memory, taken from /dev/avpu (one client only):
 * stop the streamer first and restart it afterwards.
 *
 * Build: mipsel-linux-gcc -O2 -march=mips32r2 -static -o t31_ipu_osd_probe \
 *            tools/t31_ipu_osd_probe.c
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

/* jz_ipu_v13.h */
#define JZIPU_IOC_MAGIC 'I'
#define IOCTL_IPU_START           _IO(JZIPU_IOC_MAGIC, 106)
#define IOCTL_IPU_BUF_FLUSH_CACHE _IO(JZIPU_IOC_MAGIC, 118)
#define IPU_CMD_OSD(ch)           (1u << (ch))
#define HAL_FMT_BGRA_8888         5u
#define HAL_FMT_NV12              0x18u

struct ipu_osd_ch {
    uint32_t fmt, para, bak_argb, pos_x, pos_y, src_w, src_h, buf_p;
};

struct ipu_param {
    uint32_t cmd, bg_w, bg_h, bg_fmt, bg_buf_p, out_fmt;
    struct ipu_osd_ch ch[4];
};
_Static_assert(sizeof(struct ipu_param) == 0x98, "struct ipu_param is 38 words");

struct ipu_flush_cache_para {
    void *addr;
    unsigned int size;
};

/* /dev/avpu coherent DMA allocation (same as t31_hwjpeg_probe) */
struct avpu_dma_info {
    uint32_t fd;
    uint32_t size;
    uint32_t phy_addr;
} __attribute__((aligned(4)));
#define GET_DMA_MMAP _IOWR('q', 26, struct avpu_dma_info)

typedef struct {
    uint8_t *virt;
    uint32_t phys;
    uint32_t size;
} dma_buf;

#define BMP_W 128
#define BMP_H 64
#define COVER_W 96
#define COVER_H 64

static int g_avpu = -1;

static int dma_alloc(dma_buf *b, uint32_t size)
{
    struct avpu_dma_info info;
    void *map = MAP_FAILED;

    size = (size + 0xfffu) & ~0xfffu;
    for (int attempt = 0; attempt < 2; attempt++) {
        memset(&info, 0, sizeof info);
        info.size = size;
        if (ioctl(g_avpu, GET_DMA_MMAP, &info) < 0) {
            fprintf(stderr, "GET_DMA_MMAP(%u): %s\n", size, strerror(errno));
            return -1;
        }
        map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, g_avpu, (off_t)info.fd);
        if (map != MAP_FAILED)
            break;
        if (!(errno == EINVAL && info.fd == 0 && attempt == 0)) {
            fprintf(stderr, "mmap: %s\n", strerror(errno));
            return -1;
        }
    }
    if (map == MAP_FAILED)
        return -1;
    b->virt = map;
    b->phys = info.phy_addr;
    b->size = size;
    return 0;
}

/* Colour word for COVER as the stock libimp computes it (no +16 on Y). */
static uint32_t cover_word(uint32_t argb)
{
    int a = (int)(argb >> 24), r = (int)(argb >> 16) & 255, g = (int)(argb >> 8) & 255,
        b = (int)argb & 255;
    int y = (66 * r + 129 * g + 25 * b + 128) >> 8;
    int u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
    int v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;

    return (uint32_t)a << 24 | (uint32_t)(y & 255) << 16 | (uint32_t)(u & 255) << 8 |
           (uint32_t)(v & 255);
}

/* Bitmap channel para (stock _ipu_set_osdx_para, T31 branch, BGRA source). */
static uint32_t pic_para(int global_alpha_en, int alpha)
{
    uint32_t v = global_alpha_en ? 0x020347FDu : 0x020347F9u;

    return (uint32_t)(alpha & 0xff) << 3 | (v & 0xfffff807u);
}

/* Mask channel para (stock _ipu_set_osdx_mask, T31 branch). */
static uint32_t mask_para(int global_alpha_en, int alpha)
{
    uint32_t v = (uint32_t)(alpha & 0xff) << 3 |
                 (global_alpha_en ? 0x02034005u : 0x02034001u);

    return (v & 0xfffc3fffu) | 0x00800000u;
}

static double now_us(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

static int write_file(const char *dir, const char *name, const void *p, size_t n)
{
    char path[256];
    FILE *f;

    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "wb");
    if (!f || fwrite(p, 1, n, f) != n) {
        fprintf(stderr, "write %s: %s\n", path, strerror(errno));
        if (f) fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
        "usage: %s [-W width] [-H height] [-d outdir] [-n timing_iterations] [-l mask] [-v]\n"
        "  -W/-H   frame size, multiples of 16 (default 640x368)\n"
        "  -l      layer mask to blend, bits 0-3 (default 0xf)\n"
        "  -n      also time N passes: 4 layers in one ioctl, one layer per ioctl,\n"
        "          and the stock per-region 1 MiB IOCTL_IPU_BUF_FLUSH_CACHE\n"
        "Stop the streamer first (DMA memory comes from /dev/avpu).\n", argv0);
}

int main(int argc, char **argv)
{
    uint32_t w = 640, h = 368, layers = 0xf;
    int iters = 0, verbose = 0, opt;
    const char *dir = "/tmp";
    dma_buf frame, bmp;
    struct ipu_param p;
    int ipu;

    while ((opt = getopt(argc, argv, "W:H:d:n:l:vh")) != -1) {
        switch (opt) {
        case 'W': w = (uint32_t)strtoul(optarg, NULL, 0); break;
        case 'H': h = (uint32_t)strtoul(optarg, NULL, 0); break;
        case 'd': dir = optarg; break;
        case 'n': iters = atoi(optarg); break;
        case 'l': layers = (uint32_t)strtoul(optarg, NULL, 0) & 0xf; break;
        case 'v': verbose = 1; break;
        default: usage(argv[0]); return 2;
        }
    }
    if ((w & 15) || (h & 15) || w < 352 || h < 208 || w > 4096 || h > 4096 || !layers) {
        usage(argv[0]);
        return 2;
    }

    ipu = open("/dev/ipu", O_RDWR);
    if (ipu < 0) {
        fprintf(stderr, "open /dev/ipu: %s (CONFIG_JZ_IPU?)\n", strerror(errno));
        return 1;
    }
    g_avpu = open("/dev/avpu", O_RDWR);
    if (g_avpu < 0) {
        fprintf(stderr, "open /dev/avpu: %s (stop the streamer first)\n", strerror(errno));
        return 1;
    }
    if (dma_alloc(&frame, w * h * 3 / 2) || dma_alloc(&bmp, BMP_W * BMP_H * 4))
        return 1;
    printf("frame phys=0x%08x %ux%u  bitmap phys=0x%08x\n", frame.phys, w, h, bmp.phys);

    /* background: horizontal luma ramp 16..235, neutral chroma */
    for (uint32_t y = 0; y < h; y++)
        for (uint32_t x = 0; x < w; x++)
            frame.virt[y * w + x] = (uint8_t)(16 + x * 219 / (w - 1));
    memset(frame.virt + w * h, 128, w * h / 2);

    /* bitmap: 5 colour bands, alpha ramp across the width; memory B,G,R,A */
    static const uint32_t bands[5] = {0xff0000, 0x00ff00, 0x0000ff, 0xffffff, 0x000000};
    for (int y = 0; y < BMP_H; y++)
        for (int x = 0; x < BMP_W; x++) {
            uint32_t rgb = bands[y * 5 / BMP_H];
            uint32_t a = (uint32_t)(x * 255 / (BMP_W - 1));
            uint32_t px = a << 24 | rgb;           /* little endian: B,G,R,A */
            memcpy(bmp.virt + (y * BMP_W + x) * 4, &px, 4);
        }

    write_file(dir, "ipu-before.nv12", frame.virt, w * h * 3 / 2);
    write_file(dir, "ipu-bitmap.bgra", bmp.virt, BMP_W * BMP_H * 4);

    memset(&p, 0, sizeof p);
    p.bg_w = w;
    p.bg_h = h;
    p.bg_fmt = HAL_FMT_NV12;
    p.out_fmt = HAL_FMT_NV12;
    p.bg_buf_p = frame.phys;
    p.ch[0] = (struct ipu_osd_ch){HAL_FMT_BGRA_8888, pic_para(0, 255), 0,
                                  32, 32, BMP_W, BMP_H, bmp.phys};
    p.ch[1] = (struct ipu_osd_ch){HAL_FMT_BGRA_8888, pic_para(1, 128), 0,
                                  32, 128, BMP_W, BMP_H, bmp.phys};
    p.ch[2] = (struct ipu_osd_ch){HAL_FMT_BGRA_8888, mask_para(0, 255),
                                  cover_word(0xffff0000u), 224, 32, COVER_W, COVER_H, 0};
    p.ch[3] = (struct ipu_osd_ch){HAL_FMT_BGRA_8888, mask_para(0, 128),
                                  cover_word(0x800000ffu), 224, 128, COVER_W, COVER_H, 0};
    p.cmd = layers;
    for (int k = 0; k < 4; k++)
        if (verbose || (layers >> k & 1))
            printf("ch%d fmt=%u para=0x%08x bak=0x%08x pos=(%u,%u) %ux%u buf=0x%08x\n", k,
                   p.ch[k].fmt, p.ch[k].para, p.ch[k].bak_argb, p.ch[k].pos_x,
                   p.ch[k].pos_y, p.ch[k].src_w, p.ch[k].src_h, p.ch[k].buf_p);

    double t0 = now_us();
    if (ioctl(ipu, IOCTL_IPU_START, &p) < 0) {
        fprintf(stderr, "IOCTL_IPU_START: %s (see dmesg)\n", strerror(errno));
        return 1;
    }
    printf("IOCTL_IPU_START ok, %.0f us\n", now_us() - t0);
    write_file(dir, "ipu-after.nv12", frame.virt, w * h * 3 / 2);

    /* per-channel samples along the alpha ramp / inside the covers */
    char path[256];
    snprintf(path, sizeof path, "%s/ipu-samples.csv", dir);
    FILE *csv = fopen(path, "w");
    if (csv) {
        fprintf(csv, "layer,x,y,alpha,src_rgb,bg_y,out_y,out_u,out_v\n");
        for (int k = 0; k < 4; k++) {
            if (!(layers >> k & 1))
                continue;
            int rows = k < 2 ? 5 : 1;
            for (int band = 0; band < rows; band++)
                for (int sx = 0; sx < (k < 2 ? BMP_W : COVER_W); sx += 8) {
                    uint32_t x = p.ch[k].pos_x + (uint32_t)sx;
                    uint32_t y = p.ch[k].pos_y + (uint32_t)(k < 2 ? band * BMP_H / 5 + 4 : 8);
                    uint32_t rgb = k < 2 ? bands[band] : (k == 2 ? 0xff0000u : 0x0000ffu);
                    int alpha = k < 2 ? sx * 255 / (BMP_W - 1) : (k == 2 ? 255 : 128);
                    uint8_t by = frame.virt[y * w + x];
                    const uint8_t *uv = frame.virt + w * h + (y / 2) * w + (x & ~1u);
                    uint8_t before_y = (uint8_t)(16 + x * 219 / (w - 1));
                    fprintf(csv, "%d,%u,%u,%d,%06x,%u,%u,%u,%u\n", k, x, y, alpha, rgb,
                            before_y, by, uv[0], uv[1]);
                }
        }
        fclose(csv);
    }
    printf("wrote %s/ipu-{before,after}.nv12 (%ux%u), ipu-bitmap.bgra (%dx%d), ipu-samples.csv\n",
           dir, w, h, BMP_W, BMP_H);

    if (iters > 0) {
        struct ipu_param one;
        struct ipu_flush_cache_para fc = { frame.virt, 0x100000 };
        double t;

        if (fc.size > frame.size)
            fc.size = frame.size;
        t = now_us();
        for (int i = 0; i < iters; i++)
            ioctl(ipu, IOCTL_IPU_START, &p);
        printf("timing: %d layers in one pass: %.1f us/pass\n",
               __builtin_popcount(layers), (now_us() - t) / iters);
        t = now_us();
        for (int i = 0; i < iters; i++)
            for (int k = 0; k < 4; k++) {
                if (!(layers >> k & 1))
                    continue;
                one = p;
                one.cmd = IPU_CMD_OSD(0);
                one.ch[0] = p.ch[k];
                ioctl(ipu, IOCTL_IPU_START, &one);
            }
        printf("timing: one pass per layer:   %.1f us/frame\n", (now_us() - t) / iters);
        t = now_us();
        for (int i = 0; i < iters; i++)
            ioctl(ipu, IOCTL_IPU_BUF_FLUSH_CACHE, &fc);
        printf("timing: IOCTL_IPU_BUF_FLUSH_CACHE %u bytes: %.1f us (stock: once per region per frame)\n",
               fc.size, (now_us() - t) / iters);
    }
    return 0;
}
