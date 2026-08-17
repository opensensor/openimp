#include "t31_stream_layout.h"

#include <string.h>

int openimp_t31_completion_payload_size(const void *status,
                                        size_t status_size,
                                        uint32_t *payload_size)
{
    if (!status || !payload_size ||
        status_size < OPENIMP_T31_COMPLETION_PAYLOAD_SIZE_OFFSET +
                          sizeof(*payload_size))
        return -1;

    memcpy(payload_size,
           (const uint8_t *)status +
               OPENIMP_T31_COMPLETION_PAYLOAD_SIZE_OFFSET,
           sizeof(*payload_size));
    return *payload_size ? 0 : -1;
}

int openimp_t31_stream_layout(uint32_t capacity, uint32_t payload_offset,
                              uint32_t header_size, uint32_t payload_size,
                              OpenIMPT31StreamLayout *layout)
{
    if (!layout || payload_size == 0u || payload_offset > capacity ||
        header_size > payload_offset ||
        payload_size > capacity - payload_offset)
        return -1;

    layout->payload_end = payload_offset + payload_size;
    layout->access_unit_size = header_size + payload_size;
    return 0;
}
