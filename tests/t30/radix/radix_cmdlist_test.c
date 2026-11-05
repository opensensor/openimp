/*
 * Host regression test for t30_radix_build_cmdlist(): the command lists of
 * 64 fixed random parameter blocks (radix_case.h) must hash like the lists
 * the vendor libimp 1.0.5 H265E_T30_SliceInit wrote for the same blocks
 * (recorded with `radix_diff <vendor libimp.so> -golden 64` under
 * qemu-mipsel, 2026-11-05).  Pointer fields of the block do not enter the
 * list, so the 64-bit host passes the state and QP tables directly.
 */
#include <stdio.h>
#include <string.h>

#include "t30/radix/t30_radix_cmdlist.h"
#include "radix_case.h"

static const struct { size_t words; uint32_t hash; } vendor_lists[64] = {
    { 1334, 0xab15fce9u },
    {  886, 0xf4fadc7au },
    { 1244, 0x11d7662au },
    { 1228, 0x45560545u },
    { 1244, 0x0af259e3u },
    { 1480, 0x346cfe54u },
    {  942, 0x6be25a1fu },
    { 1326, 0xb99c4c33u },
    { 1428, 0x6cb7798cu },
    { 1386, 0xfbe5d2c4u },
    {  760, 0xf50784c8u },
    { 1190, 0x8802d75du },
    {  874, 0x0cb0d33cu },
    { 1004, 0xb2ba801du },
    { 1670, 0xadf17591u },
    { 1244, 0x6cfadf2fu },
    { 1620, 0x091932feu },
    {  768, 0x03ce1dacu },
    { 1040, 0xd2121934u },
    { 1130, 0x13283795u },
    {  824, 0x7c567fdeu },
    { 1486, 0xaf535b2cu },
    { 1302, 0x4b12f899u },
    { 1480, 0xa5ba0927u },
    { 1228, 0x2a8d6172u },
    { 1158, 0x8486b203u },
    { 1098, 0x6f378ea1u },
    {  974, 0xa6b733a6u },
    { 1110, 0xa4b88e0cu },
    { 1630, 0x1441d838u },
    {  754, 0xc938a98fu },
    { 1018, 0x69d89777u },
    {  856, 0xd4bc8011u },
    { 1306, 0xb276672du },
    { 1072, 0xc6ef2aaau },
    { 1374, 0x8612ecb5u },
    { 1416, 0x23dc5c9cu },
    {  818, 0xc14857d0u },
    { 1316, 0xd72460b1u },
    {  868, 0x66791521u },
    { 1122, 0xe1080259u },
    { 1110, 0x91e17299u },
    { 1342, 0xc712d77fu },
    {  792, 0x2d068eb0u },
    { 1422, 0x4f96834cu },
    { 1218, 0x008dea00u },
    { 1064, 0x3e448e41u },
    { 1070, 0x1332894bu },
    { 1168, 0x6bf17fd8u },
    { 1422, 0x94155adau },
    {  796, 0x01e76db0u },
    {  954, 0xe2a56101u },
    { 1226, 0x387d0c95u },
    { 1294, 0x1e0137a4u },
    {  950, 0xa45f0c72u },
    { 1080, 0xa343d91bu },
    {  900, 0x31124b39u },
    { 1246, 0x1bbd6e82u },
    { 1306, 0xe9e07c7du },
    { 1010, 0x35324a96u },
    { 1238, 0x60246ca2u },
    { 1132, 0x205a6f8eu },
    { 1278, 0x8377fe6cu },
    {  956, 0xdeabc465u },
};

int main(void)
{
    int bad = 0;

    for (int c = 0; c < 64; c++) {
        static uint8_t si[T30_RADIX_SI_SIZE], state[256];
        static uint32_t qpt[256], list[8192];
        size_t n;
        uint32_t h;

        radix_make_case(c, si, state, qpt);
        n = t30_radix_build_cmdlist(si, list, state, qpt);
        h = radix_hash(list, n);
        if (n != vendor_lists[c].words || h != vendor_lists[c].hash) {
            printf("case %d: %zu words hash %08x, vendor %zu words hash %08x\n", c,
                   n, h, vendor_lists[c].words, vendor_lists[c].hash);
            bad++;
        }
        if (list[n - 1] != 0xc0008008u || list[n - 3] != 0x80020018u ||
            list[n - 4] != 1u) {
            printf("case %d: list does not end with EFE run + TERM\n", c);
            bad++;
        }
        if (rsi_u8(si, RSI_MD_BC_CLOSE) != (rsi_u8(si, RSI_SOBEL_EN) == 0)) {
            printf("case %d: md_bc_close not stored\n", c);
            bad++;
        }
    }
    printf("radix_cmdlist_test: %s (64 vendor lists)\n", bad ? "FAIL" : "PASS");
    return bad != 0;
}
