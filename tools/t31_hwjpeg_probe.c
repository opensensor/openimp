/*
 * t31_hwjpeg_probe - encode one NV12 frame on the T31 AVPU hardware JPEG core.
 *
 * Standalone bring-up tool for the JPEG core (core index 1) of the Allegro
 * encoder: register window 0x8400-0x85FF, command zone 0x8400-0x8428, start
 * 0x85E4, status 0x8430-0x8438, completion on IRQ slot 4. The quant and
 * Huffman tables ("EP1") are generated here from the ITU-T T.81 Annex K
 * tables in the layout observed for the stock library.
 *
 * The AVPU driver serves one client at a time: stop the streamer first
 * (e.g. /etc/init.d/S95timps stop or S95prudynt stop) and restart it after.
 *
 * Build: mipsel-linux-gcc -O2 -march=mips32r2 -static -o t31_hwjpeg_probe \
 *            tools/t31_hwjpeg_probe.c
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define AVPU_IOC_MAGIC 'q'

struct avpu_reg {
    unsigned int id;
    unsigned int value;
} __attribute__((aligned(4)));

struct avpu_dma_info {
    uint32_t fd;       /* mmap offset */
    uint32_t size;
    uint32_t phy_addr;
} __attribute__((aligned(4)));

#define AL_CMD_IP_WRITE_REG _IOWR(AVPU_IOC_MAGIC, 10, struct avpu_reg)
#define AL_CMD_IP_READ_REG  _IOWR(AVPU_IOC_MAGIC, 11, struct avpu_reg)
#define AL_CMD_IP_WAIT_IRQ  _IOWR(AVPU_IOC_MAGIC, 12, int)
#define GET_DMA_MMAP        _IOWR(AVPU_IOC_MAGIC, 26, struct avpu_dma_info)

#define REG_BOARD_ID   0x8004
#define REG_MISC_CTRL  0x8010
#define REG_IRQ_MASK   0x8014
#define REG_IRQ_STATUS 0x8018
#define REG_TOP_CTRL   0x8054
#define REG_CORE0_RST  0x83F0
#define REG_JPEG_CMD   0x8400      /* 11 words */
#define REG_JPEG_STAT  0x8430      /* 3 words */
#define REG_JPEG_START 0x85E4
#define REG_JPEG_RST   0x85F0
#define REG_JPEG_CLK   0x85F4
#define REG_JPEG_BUSY  0x85F8
#define JPEG_IRQ_SLOT  4

#define EP1_ALLOC      0x6400u
#define EP1_USED       0x790u
#define STREAM_HDR_OFF 0x200u

static int g_fd = -1;
static int g_verbose;
static volatile sig_atomic_t g_alarm;

static void on_alarm(int sig) { (void)sig; g_alarm = 1; }

static int wr(unsigned int reg, unsigned int val)
{
    struct avpu_reg io = { reg, val };

    if (ioctl(g_fd, AL_CMD_IP_WRITE_REG, &io) < 0) {
        fprintf(stderr, "write 0x%04x=0x%08x failed: %s\n", reg, val, strerror(errno));
        return -1;
    }
    if (g_verbose)
        printf("  WR 0x%04x = 0x%08x\n", reg, val);
    return 0;
}

static int rd(unsigned int reg, unsigned int *val)
{
    struct avpu_reg io = { reg, 0 };

    if (ioctl(g_fd, AL_CMD_IP_READ_REG, &io) < 0) {
        fprintf(stderr, "read 0x%04x failed: %s\n", reg, strerror(errno));
        return -1;
    }
    *val = io.value;
    return 0;
}

static unsigned int rd_or0(unsigned int reg)
{
    unsigned int v = 0;

    rd(reg, &v);
    return v;
}

typedef struct {
    uint8_t *virt;
    uint32_t phys;
    uint32_t size;
} dma_buf;

static int dma_alloc(dma_buf *b, uint32_t size)
{
    struct avpu_dma_info info;
    void *map = MAP_FAILED;

    size = (size + 0xfffu) & ~0xfffu;
    for (int attempt = 0; attempt < 2; attempt++) {
        memset(&info, 0, sizeof info);
        info.size = size;
        if (ioctl(g_fd, GET_DMA_MMAP, &info) < 0) {
            fprintf(stderr, "GET_DMA_MMAP(%u) failed: %s\n", size, strerror(errno));
            return -1;
        }
        map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, g_fd, (off_t)info.fd);
        if (map != MAP_FAILED)
            break;
        /* some kernels reject mmap offset 0: take the next allocation */
        if (!(errno == EINVAL && info.fd == 0 && attempt == 0)) {
            fprintf(stderr, "mmap(off=0x%x) failed: %s\n", info.fd, strerror(errno));
            return -1;
        }
    }
    if (map == MAP_FAILED)
        return -1;
    b->virt = map;
    b->phys = info.phy_addr;
    b->size = size;
    memset(b->virt, 0, size);
    return 0;
}

/* ITU-T T.81 Annex K tables. Quant bases are in raster order. */
static const uint8_t k_natural[64] = {       /* zigzag index -> raster index */
     0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63
};
static const uint8_t k_luma_q[64] = {
    16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55,
    14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62,
    18, 22, 37, 56, 68,109,103, 77, 24, 35, 55, 64, 81,104,113, 92,
    49, 64, 78, 87,103,121,120,101, 72, 92, 95, 98,112,100,103, 99
};
static const uint8_t k_chroma_q[64] = {
    17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99
};
static const uint8_t k_dc_luma_bits[16] = {0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0};
static const uint8_t k_dc_chroma_bits[16] = {0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0};
static const uint8_t k_dc_vals[12] = {0,1,2,3,4,5,6,7,8,9,10,11};
static const uint8_t k_ac_luma_bits[16] = {0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,0x7d};
static const uint8_t k_ac_luma_vals[162] = {
    0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,
    0x07,0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,0x23,0x42,0xb1,0xc1,0x15,0x52,
    0xd1,0xf0,0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,
    0x26,0x27,0x28,0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,
    0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,
    0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,
    0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,
    0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,
    0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,
    0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,
    0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa
};
static const uint8_t k_ac_chroma_bits[16] = {0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,0x77};
static const uint8_t k_ac_chroma_vals[162] = {
    0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,
    0x71,0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,0xa1,0xb1,0xc1,0x09,0x23,0x33,
    0x52,0xf0,0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,
    0x19,0x1a,0x26,0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,
    0x45,0x46,0x47,0x48,0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,
    0x64,0x65,0x66,0x67,0x68,0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,
    0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,
    0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,
    0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,
    0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,
    0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,0xf9,0xfa
};

static void put_u32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }          /* CPU-native LE */
static void put_u16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }

/* IJG scaling, output in zigzag (DQT) order; reciprocals in raster order. */
static void ep1_quant(uint8_t *qz, uint8_t *recip, const uint8_t *base, int quality)
{
    uint32_t s = (quality >= 50 ? 200u - 2u * (unsigned)quality : 5000u / (unsigned)quality) & 0xffffu;

    for (int k = 0; k < 64; k++) {
        uint32_t q = (base[k_natural[k]] * s + 50u) / 100u;

        if (q == 0) q = 1;
        if (q > 255) q = 255;
        qz[k] = (uint8_t)q;
        put_u16(recip + 2 * k_natural[k], (uint16_t)(((0x10000u + (q >> 1)) / q - 1u) & 0xffffu));
    }
}

/* 16 BITS bytes as four u32 words, each holding four bytes MSB-first. */
static void ep1_bits(uint8_t *out, const uint8_t bits[16])
{
    for (int w = 0; w < 4; w++)
        put_u32(out + 4 * w, (uint32_t)bits[4 * w] << 24 | (uint32_t)bits[4 * w + 1] << 16 |
                             (uint32_t)bits[4 * w + 2] << 8 | bits[4 * w + 3]);
}

static int ac_index(uint8_t sym)
{
    if (sym == 0xf0) return 160;                 /* ZRL */
    if (sym == 0x00) return 161;                 /* EOB */
    return ((sym & 0x0f) - 1) * 16 + (sym >> 4); /* (size-1)*16 + run */
}

/* One u32 per symbol: [7:0] next symbol in HUFFVAL order (0xFF = end),
 * [23:8] code, [31:24] length-1. */
static void ep1_codebook(uint8_t *out, const uint8_t bits[16], const uint8_t *vals,
                         int nvals, int is_ac)
{
    uint32_t code = 0;
    int k = 0;

    for (int len = 1; len <= 16; len++) {
        for (int i = 0; i < bits[len - 1] && k < nvals; i++, k++) {
            uint8_t next = k + 1 < nvals ? vals[k + 1] : 0xff;
            int idx = is_ac ? ac_index(vals[k]) : vals[k];

            put_u32(out + 4 * idx, (uint32_t)(len - 1) << 24 | (code & 0xffffu) << 8 | next);
            code++;
        }
        code <<= 1;
    }
}

static void ep1_build(uint8_t *ep1, int quality)
{
    memset(ep1, 0, EP1_USED);
    ep1_quant(ep1 + 0x000, ep1 + 0x080, k_luma_q, quality);
    ep1_quant(ep1 + 0x040, ep1 + 0x100, k_chroma_q, quality);
    put_u32(ep1 + 0x180, 1);
    put_u32(ep1 + 0x184, 0);
    ep1_bits(ep1 + 0x188, k_ac_luma_bits);
    ep1_bits(ep1 + 0x198, k_dc_luma_bits);
    ep1_bits(ep1 + 0x1a8, k_ac_chroma_bits);
    ep1_bits(ep1 + 0x1b8, k_dc_chroma_bits);
    ep1_codebook(ep1 + 0x1c8, k_ac_luma_bits, k_ac_luma_vals, 162, 1);
    ep1_codebook(ep1 + 0x450, k_dc_luma_bits, k_dc_vals, 12, 0);
    ep1_codebook(ep1 + 0x480, k_ac_chroma_bits, k_ac_chroma_vals, 162, 1);
    ep1_codebook(ep1 + 0x708, k_dc_chroma_bits, k_dc_vals, 12, 0);
}

/* Gradients, colour bars and a checkerboard, so orientation, chroma order
 * and block alignment errors are visible at a glance. */
static void make_pattern(uint8_t *y, uint8_t *uv, uint32_t w, uint32_t h, uint32_t pitch)
{
    for (uint32_t r = 0; r < h; r++)
        for (uint32_t c = 0; c < w; c++) {
            uint8_t v;
            if (r < h / 3)
                v = (uint8_t)(c * 255 / (w - 1));
            else if (r < 2 * h / 3)
                v = (uint8_t)(((c / 32) ^ (r / 32)) & 1 ? 235 : 16);
            else
                v = (uint8_t)(r * 255 / (h - 1));
            y[(size_t)r * pitch + c] = v;
        }
    for (uint32_t r = 0; r < h / 2; r++)
        for (uint32_t c = 0; c < w / 2; c++) {
            unsigned bar = c * 8 / (w / 2);
            static const uint8_t bars_u[8] = {128, 16, 166, 54, 202, 90, 240, 128};
            static const uint8_t bars_v[8] = {128, 146, 16, 34, 222, 240, 110, 128};
            uv[(size_t)r * pitch + 2 * c] = r < h / 6 ? 128 : bars_u[bar];
            uv[(size_t)r * pitch + 2 * c + 1] = r < h / 6 ? 128 : bars_v[bar];
        }
}

static int load_nv12(const char *path, uint8_t *y, uint8_t *uv, uint32_t w, uint32_t h,
                     uint32_t pitch)
{
    size_t packed = (size_t)w * h * 3 / 2;
    size_t padded = (size_t)w * ((h + 15) & ~15u) * 3 / 2;
    uint32_t uv_rows_off = h;
    struct stat st;
    FILE *f = fopen(path, "rb");

    if (!f || fstat(fileno(f), &st) < 0) {
        fprintf(stderr, "cannot open %s: %s\n", path, strerror(errno));
        if (f) fclose(f);
        return -1;
    }
    if ((size_t)st.st_size == padded && padded != packed)
        uv_rows_off = (h + 15) & ~15u;     /* UV plane starts after the padded luma */
    else if ((size_t)st.st_size < packed) {
        fprintf(stderr, "%s: %lld bytes, need %zu (NV12 %ux%u)\n", path,
                (long long)st.st_size, packed, w, h);
        fclose(f);
        return -1;
    }
    for (uint32_t r = 0; r < h; r++)
        if (fread(y + (size_t)r * pitch, 1, w, f) != w) goto short_read;
    if (fseek(f, (long)w * uv_rows_off, SEEK_SET) < 0) goto short_read;
    for (uint32_t r = 0; r < h / 2; r++)
        if (fread(uv + (size_t)r * pitch, 1, w, f) != w) goto short_read;
    fclose(f);
    return 0;
short_read:
    fprintf(stderr, "%s: short read\n", path);
    fclose(f);
    return -1;
}

static int write_file(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");

    if (!f || fwrite(data, 1, len, f) != len) {
        fprintf(stderr, "cannot write %s: %s\n", path, strerror(errno));
        if (f) fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

static double now_ms(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
        "usage: %s [options]\n"
        "  -W <width>        frame width (default 1920)\n"
        "  -H <height>       frame height (default 1080)\n"
        "  -q <1..100>       JPEG quality (default 75)\n"
        "  -i <file.nv12>    NV12 input (packed, or luma padded to 16 rows); default: test pattern\n"
        "  -o <file.jpg>     output (default /tmp/hwjpeg.jpg)\n"
        "  -e <file>         also write the generated EP1 table buffer (0x%x bytes)\n"
        "  -E <file>         load EP1 from a file (e.g. a stock libimp dump) instead\n"
        "  -t <ms>           IRQ timeout (default 2000)\n"
        "  -c <word0>        override command word 0 (default 0x131: 4:2:0, 3 comps, bit 8)\n"
        "  -s <bytes>        stream buffer size (default W*H+64KiB; small values test overflow)\n"
        "  -n                skip the global/core-0 init writes\n"
        "  -v                log every register write\n"
        "Stop the streamer first: the AVPU driver accepts one client.\n",
        argv0, EP1_USED);
}

int main(int argc, char **argv)
{
    uint32_t w = 1920, h = 1080;
    int quality = 75, timeout_ms = 2000, skip_init = 0, opt;
    uint32_t cmd0 = 0x1u | 3u << 4 | 1u << 8, strm_override = 0;
    const char *in = NULL, *out = "/tmp/hwjpeg.jpg", *ep1_out = NULL, *ep1_in = NULL;
    dma_buf src, ep1, strm;

    while ((opt = getopt(argc, argv, "W:H:q:i:o:e:E:t:c:s:nvh")) != -1) {
        switch (opt) {
        case 'W': w = (uint32_t)strtoul(optarg, NULL, 0); break;
        case 'H': h = (uint32_t)strtoul(optarg, NULL, 0); break;
        case 'q': quality = atoi(optarg); break;
        case 'i': in = optarg; break;
        case 'o': out = optarg; break;
        case 'e': ep1_out = optarg; break;
        case 'E': ep1_in = optarg; break;
        case 't': timeout_ms = atoi(optarg); break;
        case 'c': cmd0 = (uint32_t)strtoul(optarg, NULL, 0); break;
        case 's': strm_override = (uint32_t)strtoul(optarg, NULL, 0); break;
        case 'n': skip_init = 1; break;
        case 'v': g_verbose = 1; break;
        default: usage(argv[0]); return 2;
        }
    }
    if (w < 16 || h < 16 || w > 4096 || h > 4096 || (w & 1) || (h & 1) ||
        quality < 1 || quality > 100) {
        usage(argv[0]);
        return 2;
    }

    const uint32_t pitch = (w + 15) & ~15u;
    const uint32_t luma_rows = (h + 15) & ~15u;
    const uint32_t uv_off = pitch * luma_rows;
    const uint32_t src_size = uv_off + pitch * (luma_rows / 2);
    const uint32_t strm_size = strm_override ? strm_override : w * h + 0x10000u;

    g_fd = open("/dev/avpu", O_RDWR);
    if (g_fd < 0) {
        fprintf(stderr, "open /dev/avpu: %s (is the streamer still running?)\n", strerror(errno));
        return 1;
    }
    if (dma_alloc(&src, src_size) || dma_alloc(&ep1, EP1_ALLOC) || dma_alloc(&strm, strm_size))
        return 1;
    printf("buffers: src phys=0x%08x size=0x%x  ep1 phys=0x%08x  stream phys=0x%08x size=0x%x\n",
           src.phys, src.size, ep1.phys, strm.phys, strm.size);
    if ((ep1.phys & 31) || (src.phys & 255) || (strm.phys & 255))
        fprintf(stderr, "warning: stock buffers are 256-byte aligned (EP1 must be 32)\n");

    if (in) {
        if (load_nv12(in, src.virt, src.virt + uv_off, w, h, pitch))
            return 1;
    } else {
        make_pattern(src.virt, src.virt + uv_off, w, h, pitch);
    }

    if (ep1_in) {
        FILE *f = fopen(ep1_in, "rb");
        size_t n = f ? fread(ep1.virt, 1, EP1_ALLOC, f) : 0;

        if (f) fclose(f);
        if (n < EP1_USED) {
            fprintf(stderr, "%s: need at least 0x%x bytes\n", ep1_in, EP1_USED);
            return 1;
        }
        printf("EP1 loaded from %s (%zu bytes)\n", ep1_in, n);
    } else {
        ep1_build(ep1.virt, quality);
    }
    if (ep1_out && write_file(ep1_out, ep1.virt, EP1_USED))
        return 1;

    printf("before: board=0x%08x misc=0x%08x mask=0x%08x pending=0x%08x jclk=0x%08x\n",
           rd_or0(REG_BOARD_ID), rd_or0(REG_MISC_CTRL), rd_or0(REG_IRQ_MASK),
           rd_or0(REG_IRQ_STATUS), rd_or0(REG_JPEG_CLK));

    if (!skip_init) {
        /* scheduler init as the stock library does it, even for JPEG only */
        if (wr(REG_MISC_CTRL, 0x1000) || wr(REG_CORE0_RST, 1) || wr(REG_CORE0_RST, 2) ||
            wr(REG_CORE0_RST, 4) || wr(REG_IRQ_STATUS, 0xffffff) || wr(REG_TOP_CTRL, 0x80))
            return 1;
    }
    if (wr(REG_IRQ_MASK, rd_or0(REG_IRQ_MASK) | (1u << JPEG_IRQ_SLOT)) ||
        wr(REG_JPEG_CLK, (rd_or0(REG_JPEG_CLK) & ~3u) | 1u))
        return 1;

    const uint32_t cmd[11] = {
        cmd0,                                  /* default 4:2:0, 3 components, bit 8 as stock */
        (w - 1) << 16 | (h - 1),
        0x00010001u,                           /* JFIF density 1:1 */
        pitch,
        src.phys,
        src.phys + uv_off,
        ep1.phys,
        strm.phys,
        strm.size,
        STREAM_HDR_OFF,
        strm.size - STREAM_HDR_OFF,
    };

    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_alarm;                  /* no SA_RESTART: WAIT_IRQ returns EINTR */
    sigaction(SIGALRM, &sa, NULL);

    printf("0x85f8 before start: 0x%08x\n", rd_or0(REG_JPEG_BUSY));
    if (wr(REG_JPEG_RST, 1))
        return 1;
    for (int i = 0; i < 11; i++)
        if (wr(REG_JPEG_CMD + 4u * i, cmd[i]))
            return 1;
    double t0 = now_ms();
    if (wr(REG_JPEG_START, 1))
        return 1;

    alarm((unsigned)(timeout_ms + 999) / 1000);
    int got = 0;
    while (!got && !g_alarm) {
        int irq = -1;

        if (ioctl(g_fd, AL_CMD_IP_WAIT_IRQ, &irq) < 0) {
            if (errno == EINTR)
                continue;
            fprintf(stderr, "WAIT_IRQ: %s\n", strerror(errno));
            break;
        }
        if (irq == JPEG_IRQ_SLOT)
            got = 1;
        else
            printf("ignoring IRQ slot %d\n", irq);
    }
    alarm(0);
    double hw_ms = now_ms() - t0;

    uint32_t s0 = rd_or0(REG_JPEG_STAT), len = rd_or0(REG_JPEG_STAT + 4), s2 = rd_or0(REG_JPEG_STAT + 8);
    printf("0x85f8 after: 0x%08x\n", rd_or0(REG_JPEG_BUSY));
    printf("status: 0x8430=0x%08x len(0x8434)=%u 0x8438=0x%08x%s  mask=0x%08x\n",
           s0, len, s2, (s2 & 2u) ? " [ERROR/overflow bit]" : "", rd_or0(REG_IRQ_MASK));
    if (!got) {
        fprintf(stderr, "no IRQ slot %d within %d ms\n", JPEG_IRQ_SLOT, timeout_ms);
        write_file("/tmp/hwjpeg-stream.raw", strm.virt, 0x10000);
        fprintf(stderr, "first 64 KiB of the stream buffer -> /tmp/hwjpeg-stream.raw\n");
        return 1;
    }
    printf("hardware time: %.2f ms\n", hw_ms);

    const uint8_t *p = strm.virt + STREAM_HDR_OFF;
    if (len == 0 || len > strm.size - STREAM_HDR_OFF) {
        fprintf(stderr, "implausible length %u\n", len);
        write_file("/tmp/hwjpeg-stream.raw", strm.virt, 0x10000);
        return 1;
    }
    printf("first bytes:");
    for (int i = 0; i < 16; i++)
        printf(" %02x", p[i]);
    printf("\n");
    if (p[0] == 0xff && p[1] == 0xd8)
        printf("SOI present: hardware writes the JFIF headers\n");
    else
        printf("no SOI: output looks like bare entropy data (headers must come from software)\n");
    if (len >= 2 && p[len - 2] == 0xff && p[len - 1] == 0xd9)
        printf("EOI present\n");
    if (write_file(out, p, len))
        return 1;
    printf("wrote %u bytes to %s\n", len, out);
    return (s2 & 2u) ? 3 : 0;
}
