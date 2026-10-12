/*
 * T40 command words versus the vendor libimp 1.3.1 (host test, no camera).
 *
 * Every expected value below is a word from a vendor command list or EP1
 * table captured at the AVPU push on the Eufy T40XP (SC830AI, H.264 High):
 * cmd[0x12] bits 27:24, the HWRC targets cmd[0x15]/cmd[0x16], the first
 * slice QP for iInitialQP -1, the IDR period and the FixQP lambda words.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "t40/t40_vendor_cmd.h"
#include "t40/t40_ep1.h"

/* t40_ep1.c's scaling-list helpers live in the alcodec objects */
int32_t AL_AVC_GenerateHwScalingList(void *s, int32_t t) { (void)s; return t; }
int32_t AL_AVC_WriteEncHwScalingList(void *s, void *t, uint32_t *o)
{ (void)s; (void)t; (void)o; return 0; }
int32_t AL_AVC_WriteEncHwScalingListT41(void *s, void *t, uint32_t *o)
{ (void)s; (void)t; (void)o; return 0; }

static int failures;

#define EXPECT(cond, ...) do { if (!(cond)) { printf("FAIL: " __VA_ARGS__); \
    printf("\n"); ++failures; } } while (0)

struct hwrc_case {
    uint32_t w, h, kbps, max_kbps, fps, groups;
    uint32_t i15, p15, w16;
};

int main(void)
{
    /* vendor cmd[0x12] = 0x873ff3ff / 0x803ff3ff / 0x883ff3ff / 0x823ff3ff */
    static const uint32_t widths[4] = { 3840u, 1920u, 1280u, 640u };
    static const uint32_t nibble[4] = { 7u, 0u, 8u, 2u };
    /* vendor cmd[0x14..0x16] (groups from cmd[0x14] bits 15:6 + 1) */
    static const struct hwrc_case hw[] = {
        { 3840, 2160, 6000, 6000, 20, 240, 0x149dc, 0xeb9d, 0x8ae },
        { 3840, 2160, 6000, 6000, 15, 240, 0x149dc, 0xeb9d, 0xb92 },
        { 3840, 2160, 6000, 8000, 20, 240, 0x149dc, 0xeb9d, 0xb92 }, /* VBR */
        { 1920, 1080, 3000, 3000, 20, 8, 0x15d4, 0xf97, 0x93 },
        { 640, 360, 1000, 1000, 20, 5, 0x2856, 0x1ccf, 0x10f },
    };
    uint8_t ep1[0x200];
    uint32_t word;
    unsigned int i;

    for (i = 0; i < 4u; ++i)
        EXPECT(t40_vendor_cmd12_hi(widths[i]) == nibble[i],
               "cmd12 nibble %u: %u", widths[i], t40_vendor_cmd12_hi(widths[i]));

    for (i = 0; i < sizeof(hw) / sizeof(hw[0]); ++i) {
        uint32_t lcus = ((hw[i].w + 15u) >> 4) * ((hw[i].h + 15u) >> 4);
        uint32_t w15, w16;

        t40_vendor_hwrc_targets(hw[i].kbps * 1000ull, hw[i].max_kbps * 1000ull,
                                hw[i].fps, 1u, lcus, hw[i].groups, 1,
                                &w15, &w16);
        EXPECT(w15 == hw[i].i15 && w16 == hw[i].w16,
               "hwrc IDR case %u: %#x %#x", i, w15, w16);
        t40_vendor_hwrc_targets(hw[i].kbps * 1000ull, hw[i].max_kbps * 1000ull,
                                hw[i].fps, 1u, lcus, hw[i].groups, 0,
                                &w15, &w16);
        EXPECT(w15 == hw[i].p15, "hwrc P case %u: %#x", i, w15);
    }

    /* vendor IDR slice QP for iInitialQP -1 (cmd[0x03] bits 21:16) */
    EXPECT(t40_vendor_initial_qp(6000000u, 20u, 3840u * 2160u, 0, 15, 48) == 39,
           "initial QP 4K 6M 20fps");
    EXPECT(t40_vendor_initial_qp(6000000u, 15u, 3840u * 2160u, 0, 15, 48) == 36,
           "initial QP 4K 6M 15fps");
    EXPECT(t40_vendor_initial_qp(3000000u, 20u, 1920u * 1080u, 0, 15, 48) == 32,
           "initial QP 1080p 3M");
    EXPECT(t40_vendor_initial_qp(1000000u, 20u, 640u * 360u, 0, 15, 48) == 21,
           "initial QP 360p 1M");
    EXPECT(t40_vendor_initial_qp(100000000u, 20u, 640u * 360u, 0, 15, 48) == 15,
           "initial QP past the table");

    /* vendor cmd[0x17] bits 31:24 for iMinQP/iMaxQP 15/48 and 20/44 */
    EXPECT(t40_vendor_hwrc_min_qp(15u, 48u) == 16u, "hw min 15/48");
    EXPECT(t40_vendor_hwrc_min_qp(20u, 44u) == 20u, "hw min 20/44");

    /* vendor IDR positions: GOP 40 x 2 -> 80, 20 x 2 and 40 x 1 -> 40 */
    EXPECT(t40_vendor_gop_length(40u, 2u) == 80u, "gop 40x2");
    EXPECT(t40_vendor_gop_length(20u, 2u) == 40u, "gop 20x2");
    EXPECT(t40_vendor_gop_length(40u, 1u) == 40u, "gop 40x1");
    EXPECT(t40_vendor_gop_length(40u, 0u) == 40u, "gop 40x0");

    /* vendor FixQP EP1 words: IDR keeps the intra lane only */
    memset(ep1, 0xa5, sizeof(ep1));
    EXPECT(openimp_t40_update_fixqp_ep1(ep1, sizeof(ep1), 1) == 0, "ep1 idr");
    memcpy(&word, ep1 + 30 * 4, 4);
    EXPECT(word == 0x00030000u, "EP1 IDR QP30 %#x", word);
    memcpy(&word, ep1 + 51 * 4, 4);
    EXPECT(word == 0x00280000u, "EP1 IDR QP51 %#x", word);
    EXPECT(ep1[0xd0] == 0xa5, "EP1 update stays inside the lambda words");
    EXPECT(openimp_t40_update_fixqp_ep1(ep1, sizeof(ep1), 0) == 0, "ep1 p");
    memcpy(&word, ep1 + 30 * 4, 4);
    EXPECT(word == 0x08030800u, "EP1 P QP30 %#x", word);
    memcpy(&word, ep1 + 0 * 4, 4);
    EXPECT(word == 0x01010100u, "EP1 P QP0 %#x", word);
    memcpy(&word, ep1 + 51 * 4, 4);
    EXPECT(word == 0x6b286b00u, "EP1 P QP51 %#x", word);

    if (failures) {
        printf("t40 vendor cmd test: %d failures\n", failures);
        return 1;
    }
    printf("t40 vendor cmd test: ok\n");
    return 0;
}
