#ifndef OPENIMP_T41_STREAM_LAYOUT_H
#define OPENIMP_T41_STREAM_LAYOUT_H

#include <stdint.h>

typedef struct {
    uint32_t payload_end;
    uint32_t access_unit_size;
} OpenIMPT41StreamLayout;

/*
 * Validate the exact entropy byte count written by T41 completion hardware
 * and derive the two extents used while joining it to the generated Annex-B
 * prefix.  The completed payload begins at payload_offset, then compaction
 * moves it directly behind header_size.
 */
int openimp_t41_stream_layout(uint32_t capacity, uint32_t payload_offset,
                              uint32_t header_size, uint32_t payload_size,
                              OpenIMPT41StreamLayout *layout);

#endif
