#ifndef OPENIMP_T31_STREAM_LAYOUT_H
#define OPENIMP_T31_STREAM_LAYOUT_H

#include <stddef.h>
#include <stdint.h>

#define OPENIMP_T31_COMPLETION_PAYLOAD_SIZE_OFFSET 0x104u

typedef struct {
    uint32_t payload_end;
    uint32_t access_unit_size;
} OpenIMPT31StreamLayout;

int openimp_t31_completion_payload_size(const void *status,
                                        size_t status_size,
                                        uint32_t *payload_size);
int openimp_t31_stream_layout(uint32_t capacity, uint32_t payload_offset,
                              uint32_t header_size, uint32_t payload_size,
                              OpenIMPT31StreamLayout *layout);

#endif
