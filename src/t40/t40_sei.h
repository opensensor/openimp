/* T40 IMP_Encoder_SetChnSeiAttr: user_data_unregistered SEI NAL (Annex B,
 * emulation prevention), H.264 nal_unit_type 6 or H.265 PREFIX_SEI (39).
 * Host-tested: tests/t40/sei_test.c.
 *
 * Vendor T40 1.3.1 (IMP_Encoder_SetChnSeiAttr 0x6d0a8 ->
 * AL_Codec_Encode_SetSei): SeiMode NONE switches it off, INTRA puts the SEI
 * in front of every intra (IDR) picture, ALL in front of every picture; the
 * payload is the 16-byte uuid followed by userDataSize (<= 128) bytes. */
#ifndef OPENIMP_T40_SEI_H
#define OPENIMP_T40_SEI_H

#include <stdint.h>
#include <string.h>

#define OPENIMP_T40_SEI_UUID 16u
#define OPENIMP_T40_SEI_MAX_DATA 128u
/* start code + 2-byte header + EPB-expanded RBSP (at most 3/2) */
#define OPENIMP_T40_SEI_MAX_NAL \
    (4u + 2u + ((2u + OPENIMP_T40_SEI_UUID + OPENIMP_T40_SEI_MAX_DATA + 1u) * 3u + 1u) / 2u)

typedef struct {
    uint8_t mode;                       /* 0 off, 1 intra, 2 every picture */
    uint8_t size;                       /* user data bytes, <= 128 */
    uint8_t uuid[OPENIMP_T40_SEI_UUID];
    uint8_t data[OPENIMP_T40_SEI_MAX_DATA];
} OpenIMPT40Sei;

static inline int openimp_t40_sei_wanted(const OpenIMPT40Sei *sei, int is_intra)
{
    return sei && (sei->mode == 2u || (sei->mode == 1u && is_intra));
}

/* Writes the whole NAL (00 00 00 01, header, RBSP with 0x03 escapes);
 * returns its length, 0 when cap is too small. */
static inline uint32_t openimp_t40_sei_nal(uint8_t *dst, uint32_t cap, int hevc,
                                           const OpenIMPT40Sei *sei)
{
    uint8_t rbsp[2u + OPENIMP_T40_SEI_UUID + OPENIMP_T40_SEI_MAX_DATA + 1u];
    uint32_t n = sei->size > OPENIMP_T40_SEI_MAX_DATA
        ? OPENIMP_T40_SEI_MAX_DATA : sei->size;
    uint32_t len = 0u, pos = 0u, i;
    int zeros = 0;

    if (!dst || cap < OPENIMP_T40_SEI_MAX_NAL)
        return 0u;
    rbsp[len++] = 5u;                   /* user_data_unregistered */
    rbsp[len++] = (uint8_t)(OPENIMP_T40_SEI_UUID + n);  /* < 255: one byte */
    memcpy(rbsp + len, sei->uuid, OPENIMP_T40_SEI_UUID);
    len += OPENIMP_T40_SEI_UUID;
    memcpy(rbsp + len, sei->data, n);
    len += n;
    rbsp[len++] = 0x80u;                /* rbsp_trailing_bits */

    dst[pos++] = 0u;
    dst[pos++] = 0u;
    dst[pos++] = 0u;
    dst[pos++] = 1u;
    if (hevc) {
        dst[pos++] = 39u << 1;          /* PREFIX_SEI, layer 0 */
        dst[pos++] = 1u;                /* temporal id + 1 */
    } else {
        dst[pos++] = 6u;                /* nal_ref_idc 0, SEI */
    }
    for (i = 0u; i < len; i++) {
        if (zeros >= 2 && rbsp[i] <= 3u) {
            dst[pos++] = 3u;
            zeros = 0;
        }
        dst[pos++] = rbsp[i];
        zeros = rbsp[i] == 0u ? zeros + 1 : 0;
    }
    return pos;
}

#endif
