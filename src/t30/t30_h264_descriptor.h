#ifndef OPENIMP_T30_H264_DESCRIPTOR_H
#define OPENIMP_T30_H264_DESCRIPTOR_H

#include <stddef.h>
#include <stdint.h>

/*
 * Inputs consumed by the T30 Helix H.264 descriptor builder.  Keep this as
 * an OpenIMP-owned structure: the SDK 1.0.5 libimp structure is a private ABI
 * and is deliberately not reproduced here.
 */
typedef struct {
    uint8_t slice_type; /* 0: I/IDR, 1: P */
    uint8_t mb_width;
    uint8_t mb_height;
    uint8_t first_mby;
    uint8_t last_mby;
    uint8_t qp;
    uint8_t raw_format;
    uint8_t dcs_oth;
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
    uint32_t *descriptor;
    size_t descriptor_words;
    /* T20 rate control (src/rc_t20, OEM H264E_T20_SliceInit): 0 keeps the
     * defaults.  max_qp_cap: macroblock QP cap 0x40040 (the OEM writes the
     * application's maxQp); qp_table: run-length macroblock QP table
     * (JZM_QPTabConv words) written into VPU memory 0xc5800 and enabled in
     * 0x4006c; mb_tune: the OEM's macroblock mode tuning for pictures of
     * at least 51x39 macroblocks in a moving scene (0x80034, 0x8003c). */
    uint8_t max_qp_cap;
    uint8_t mb_tune;
    uint16_t qp_table_words;
    const uint32_t *qp_table;
} T30H264SliceConfig;

int T30_H264_BuildDescriptor(const T30H264SliceConfig *config,
                             size_t *pair_count);

/* T10 JZ NVPU (shares the T20 libimp/build; selected at run time).
 * Reconstructions use a one-macroblock border: allocate
 * ReferencePlaneSize() per plane and pass the plane base as
 * reference_y/c and output_y/c. The deblocker writes at the base (the
 * NVPU applies the border itself); the builder adds ReferenceOffset()
 * only to the MCE reference read. */
int T10_H264_BuildDescriptor(const T30H264SliceConfig *config,
                             size_t *pair_count);
size_t T10_H264_ReferenceOffset(uint8_t mb_width, int chroma);
size_t T10_H264_ReferencePlaneSize(uint8_t mb_width, uint8_t mb_height,
                                   int chroma);

#endif
