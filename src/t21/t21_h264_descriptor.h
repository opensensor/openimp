#ifndef OPENIMP_T21_H264_DESCRIPTOR_H
#define OPENIMP_T21_H264_DESCRIPTOR_H

#include <stddef.h>
#include <stdint.h>

/* OpenIMP-owned inputs to the T21 Helix command-list builder.  This is not
 * the private SDK structure recovered from libimp; every hardware address and
 * codec property used below is named and supplied explicitly. */
typedef struct {
    uint8_t slice_type; /* 0: I/IDR, 1: P */
    uint8_t mb_width;
    uint8_t mb_height;
    uint8_t first_mby;
    uint8_t last_mby;
    uint8_t qp;
    uint8_t raw_format;
    uint16_t width;
    uint16_t height;
    const uint8_t *cabac_state;
    uint32_t raw[3];
    uint32_t stride[2];
    uint32_t reference_y;
    uint32_t reference_c;
    uint32_t output_y;
    uint32_t output_c;
    uint32_t bitstream;
    uint32_t scratch_base;
    uint32_t *descriptor;
    size_t descriptor_words;
} T21H264SliceConfig;

int T21_H264_BuildDescriptor(const T21H264SliceConfig *config,
                             size_t *pair_count);

#endif
