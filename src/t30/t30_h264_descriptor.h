#ifndef OPENIMP_T30_H264_DESCRIPTOR_H
#define OPENIMP_T30_H264_DESCRIPTOR_H

#include <stddef.h>
#include <stdint.h>

/*
 * Inputs consumed by the T30 Helix IDR descriptor builder.  Keep this as an
 * OpenIMP-owned structure: the SDK 1.0.5 libimp structure is a private ABI
 * and is deliberately not reproduced here.
 */
typedef struct {
    uint8_t mb_width;
    uint8_t mb_height;
    uint8_t first_mby;
    uint8_t last_mby;
    uint8_t qp;
    uint8_t raw_format;
    uint8_t dcs_oth;
    const uint8_t *cabac_state;
    uint32_t raw[3];
    uint32_t stride[2];
    uint32_t output_y;
    uint32_t output_c;
    uint32_t bitstream;
    uint32_t *descriptor;
    size_t descriptor_words;
} T30H264IDRConfig;

int T30_H264_BuildIDRDescriptor(const T30H264IDRConfig *config,
                                size_t *pair_count);

#endif
