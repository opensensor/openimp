/* T40 SetChnSeiAttr: the user_data_unregistered SEI NAL (src/t40/t40_sei.h)
 * and the access-unit check that has to accept it in front of the VCL NAL
 * (src/t40/t31_stream_layout.c).  Host test, no camera. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "t40/t40_sei.h"
#include "t40/t31_stream_layout.h"

/* strip start code, header and 0x03 escapes */
static uint32_t unescape(const uint8_t *nal, uint32_t len, uint32_t hdr, uint8_t *out)
{
    uint32_t i, n = 0, zeros = 0;

    for (i = 4u + hdr; i < len; i++) {
        if (zeros >= 2 && nal[i] == 3u) {
            zeros = 0;
            continue;
        }
        out[n++] = nal[i];
        zeros = nal[i] == 0u ? zeros + 1 : 0;
    }
    return n;
}

static uint32_t put(uint8_t *dst, const uint8_t *nal, uint32_t len)
{
    memcpy(dst, nal, len);
    return len;
}

int main(void)
{
    OpenIMPT40Sei sei;
    uint8_t nal[OPENIMP_T40_SEI_MAX_NAL], rbsp[256], au[1024];
    uint32_t len, n, i, pos;
    OpenIMPT31AvcAuCheck rep;
    static const uint8_t sps[] = { 0, 0, 0, 1, 0x27, 0x64, 0x00, 0x1f };
    static const uint8_t pps[] = { 0, 0, 0, 1, 0x28, 0xee, 0x3c, 0x80 };
    static const uint8_t idr[] = { 0, 0, 0, 1, 0x25, 0x88, 0x84, 0x21 };
    static const uint8_t pslice[] = { 0, 0, 0, 1, 0x21, 0x9a, 0x02, 0x04 };
    static const uint8_t vps[] = { 0, 0, 0, 1, 0x40, 0x01, 0x0c, 0x01 };
    static const uint8_t hsps[] = { 0, 0, 0, 1, 0x42, 0x01, 0x01, 0x01 };
    static const uint8_t hpps[] = { 0, 0, 0, 1, 0x44, 0x01, 0xc1, 0x72 };
    static const uint8_t hidr[] = { 0, 0, 0, 1, 0x26, 0x01, 0xaf, 0x10 };
    static const uint8_t htrail[] = { 0, 0, 0, 1, 0x02, 0x01, 0xd0, 0x08 };

    memset(&sei, 0, sizeof(sei));
    /* wanted() */
    assert(!openimp_t40_sei_wanted(&sei, 1));
    sei.mode = 1;
    assert(openimp_t40_sei_wanted(&sei, 1) && !openimp_t40_sei_wanted(&sei, 0));
    sei.mode = 2;
    assert(openimp_t40_sei_wanted(&sei, 0) && openimp_t40_sei_wanted(&sei, 1));

    /* H.264: zeros in uuid and data force 0x03 escapes */
    for (i = 0; i < 16; i++)
        sei.uuid[i] = (uint8_t)(i < 4 ? 0 : i);
    sei.size = 128;
    for (i = 0; i < 128; i++)
        sei.data[i] = (uint8_t)(i % 3 == 2 ? 1 : 0);
    assert(openimp_t40_sei_nal(nal, OPENIMP_T40_SEI_MAX_NAL - 1u, 0, &sei) == 0u);
    len = openimp_t40_sei_nal(nal, sizeof(nal), 0, &sei);
    assert(len > 0 && len <= OPENIMP_T40_SEI_MAX_NAL);
    assert(nal[0] == 0 && nal[1] == 0 && nal[2] == 0 && nal[3] == 1 && nal[4] == 6);
    for (i = 5; i + 2 < len; i++)            /* no start code inside */
        assert(!(nal[i] == 0 && nal[i + 1] == 0 && nal[i + 2] <= 2));
    n = unescape(nal, len, 1, rbsp);
    assert(n == 2 + 16 + 128 + 1);
    assert(rbsp[0] == 5 && rbsp[1] == 16 + 128);
    assert(!memcmp(rbsp + 2, sei.uuid, 16) && !memcmp(rbsp + 18, sei.data, 128));
    assert(rbsp[n - 1] == 0x80);

    /* H.265 prefix SEI header, uuid only */
    sei.size = 0;
    len = openimp_t40_sei_nal(nal, sizeof(nal), 1, &sei);
    assert(nal[4] == 0x4e && nal[5] == 0x01);
    n = unescape(nal, len, 2, rbsp);
    assert(n == 2 + 16 + 1 && rbsp[1] == 16);

    /* AU check: SEI between the parameter sets and the IDR slice */
    sei.size = 5;
    memcpy(sei.data, "hello", 5);
    len = openimp_t40_sei_nal(nal, sizeof(nal), 0, &sei);
    pos = put(au, sps, sizeof(sps));
    pos += put(au + pos, pps, sizeof(pps));
    pos += put(au + pos, nal, len);
    pos += put(au + pos, idr, sizeof(idr));
    assert(openimp_t31_au_check_sei(au, pos, 1, &rep, 0) == 0u);
    assert(rep.nal_count == 4 && rep.nals[2].nal_type == 6);
    /* P: SEI first */
    pos = put(au, nal, len);
    pos += put(au + pos, pslice, sizeof(pslice));
    assert(openimp_t31_au_check_sei(au, pos, 0, &rep, 0) == 0u);
    /* plain AUs still pass, a misplaced SEI does not */
    pos = put(au, pslice, sizeof(pslice));
    assert(openimp_t31_au_check_sei(au, pos, 0, &rep, 0) == 0u);
    pos = put(au, pslice, sizeof(pslice));
    pos += put(au + pos, nal, len);
    assert(openimp_t31_au_check_sei(au, pos, 0, &rep, 0) & OPENIMP_T31_AU_BAD_ORDER);
    pos = put(au, sps, sizeof(sps));
    pos += put(au + pos, nal, len);
    pos += put(au + pos, pps, sizeof(pps));
    pos += put(au + pos, idr, sizeof(idr));
    assert(openimp_t31_au_check_sei(au, pos, 1, &rep, 0) & OPENIMP_T31_AU_BAD_ORDER);

    /* HEVC: VPS SPS PPS SEI IDR, and SEI TRAIL */
    len = openimp_t40_sei_nal(nal, sizeof(nal), 1, &sei);
    pos = put(au, vps, sizeof(vps));
    pos += put(au + pos, hsps, sizeof(hsps));
    pos += put(au + pos, hpps, sizeof(hpps));
    pos += put(au + pos, nal, len);
    pos += put(au + pos, hidr, sizeof(hidr));
    assert(openimp_t31_au_check_sei(au, pos, 1, &rep, 1) == 0u);
    pos = put(au, nal, len);
    pos += put(au + pos, htrail, sizeof(htrail));
    assert(openimp_t31_au_check_sei(au, pos, 0, &rep, 1) == 0u);

    /* the plain checks (T31, T40 without SEI) still refuse an SEI */
    pos = put(au, nal, len);
    pos += put(au + pos, htrail, sizeof(htrail));
    assert(openimp_t31_hevc_au_check(au, pos, 0, &rep) != 0u);
    len = openimp_t40_sei_nal(nal, sizeof(nal), 0, &sei);
    pos = put(au, nal, len);
    pos += put(au + pos, pslice, sizeof(pslice));
    assert(openimp_t31_avc_au_check(au, pos, 0, &rep) != 0u);

    puts("T40 SEI: all checks passed");
    return 0;
}
