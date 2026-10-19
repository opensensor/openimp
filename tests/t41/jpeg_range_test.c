/* The T40/T41 software JPEG must pass the ISP's Y code values through
 * unchanged (what the vendor JPEG core does: no range conversion, no CSC;
 * the vendor core only writes the JFIF header).  A flat 16x16 picture of
 * Y = v must give a first Y block whose DC coefficient is (v - 128) * 8 at
 * quality 100 (quantiser step 1), i.e. 0 stays 0 and 255 stays 255 after the
 * decoder's inverse level shift.  Synthetic picture only. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include "hw_encoder.h"

#define W 16u
#define H 16u

static uint8_t pic[W * H * 3u / 2u];

/* first DC difference of the scan: standard luma DC Huffman table */
static int first_dc(const uint8_t *d, uint32_t len)
{
    static const uint8_t cat_len[12] = {2, 3, 3, 3, 3, 3, 4, 5, 6, 7, 8, 9};
    static const uint16_t cat_code[12] = {0x00, 0x02, 0x03, 0x04, 0x05, 0x06,
                                          0x0e, 0x1e, 0x3e, 0x7e, 0xfe, 0x1fe};
    uint32_t i = 2, bits = 0, nbits = 0, pos;
    int cat = -1, k, val = 0;

    while (i + 4 < len && !(d[i] == 0xff && d[i + 1] == 0xda))
        i += 2u + ((uint32_t)d[i + 2] << 8 | d[i + 3]);
    pos = i + 2u + ((uint32_t)d[i + 2] << 8 | d[i + 3]);
    for (k = 0; k < 3 && pos < len; k++) {   /* 3 bytes cover 9 + 11 bits */
        bits = bits << 8 | d[pos];
        nbits += 8;
        if (d[pos++] == 0xff)
            pos++;                            /* stuffed 0x00 */
    }
    for (k = 0; k < 12; k++)
        if ((bits >> (nbits - cat_len[k])) == cat_code[k]) {
            cat = k;
            break;
        }
    assert(cat >= 0);
    nbits -= cat_len[cat];
    if (cat) {
        val = (int)((bits >> (nbits - (uint32_t)cat)) & ((1u << cat) - 1u));
        if (val < (1 << (cat - 1)))
            val -= (1 << cat) - 1;
    }
    return val;
}

int main(void)
{
    static const uint8_t codes[] = {0, 1, 16, 128, 235, 254, 255};
    unsigned int n;

    mallopt(M_MMAP_MAX, 0); /* stream address is a 32-bit field */
    for (n = 0; n < sizeof(codes); n++) {
        HWFrameBuffer f;
        HWStreamBuffer s;

        memset(pic, codes[n], W * H);
        memset(pic + W * H, 128, W * H / 2u);
        memset(&f, 0, sizeof(f));
        memset(&s, 0, sizeof(s));
        f.virt_addr = (uint32_t)(uintptr_t)pic;
        f.size = sizeof(pic);
        f.width = W;
        f.height = H;
        f.pixfmt = 0x3231564eu;
        f.pitch = W;
        assert(HW_Encoder_Encode_NV12_JPEG(&f, &s, 100u) == 0);
        assert(first_dc((const uint8_t *)(uintptr_t)s.virt_addr, s.length) ==
               ((int)codes[n] - 128) * 8);
    }
    puts("jpeg range ok");
    return 0;
}
