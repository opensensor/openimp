/* T21 SetSinterStrength mapping and 0x800002c block contents. */
#include <stdio.h>
#include "isp/isp_t21_sinter.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    uint8_t b[T21_SINTER_BLOCK_SIZE], z[T21_SINTER_BLOCK_SIZE];
    uint32_t v, i;

    /* timps default 128 -> AUTO block: only byte 70 set (= stock picture) */
    memset(z, 0, sizeof(z));
    memset(b, 0xaa, sizeof(b));
    t21_sinter_strength_block(128, b);
    z[70] = 1;
    CHECK(memcmp(b, z, sizeof(b)) == 0);
    CHECK(t21_sinter_block_strength(b) == 128);

    /* MANUAL: 10, 98 = 1, 43 = strength, 70 = 1, rest zero */
    t21_sinter_strength_block(200, b);
    memset(z, 0, sizeof(z));
    z[10] = z[98] = z[70] = 1; z[43] = 200;
    CHECK(memcmp(b, z, sizeof(b)) == 0);
    CHECK(t21_sinter_block_strength(b) == 200);

    t21_sinter_strength_block(0, b);
    CHECK(b[10] == 1 && b[43] == 0 && t21_sinter_block_strength(b) == 0);
    t21_sinter_strength_block(1000, b);
    CHECK(b[43] == 255 && b[10] == 1);

    /* round trip for every value 0..255 */
    for (v = 0; v < 256; v++) {
        t21_sinter_strength_block(v, b);
        CHECK(b[70] == 1);
        CHECK(t21_sinter_block_strength(b) == v);
        CHECK((b[10] == 0) == (v == 128));
        for (i = 0; i < sizeof(b); i++)
            if (i != 10 && i != 43 && i != 70 && i != 98)
                CHECK(b[i] == 0);
    }
    puts(fails ? "t21_sinter_map_test FAILED" : "t21_sinter_map_test OK");
    return fails != 0;
}
