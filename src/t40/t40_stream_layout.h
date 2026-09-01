#ifndef OPENIMP_T40_STREAM_LAYOUT_H
#define OPENIMP_T40_STREAM_LAYOUT_H

#include <stdint.h>

#define OPENIMP_T40_PAYLOAD_SIZE_MASK 0x3fffffffu

typedef struct {
    uint32_t payload_size;
    uint32_t payload_end;
    uint32_t access_unit_size;
} OpenIMPT40StreamLayout;

/*
 * Validate T40's entropy-byte count from AVPU status register 0x8304 and
 * derive the DMA extent and compacted Annex-B access-unit size.
 */
int openimp_t40_stream_layout(uint32_t capacity, uint32_t payload_offset,
                              uint32_t header_size, uint32_t payload_status,
                              OpenIMPT40StreamLayout *layout);

#endif
