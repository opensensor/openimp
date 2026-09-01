#include "t40_stream_layout.h"

#include <stddef.h>

int openimp_t40_stream_layout(uint32_t capacity, uint32_t payload_offset,
                              uint32_t header_size, uint32_t payload_status,
                              OpenIMPT40StreamLayout *layout)
{
    uint32_t payload_size =
        payload_status & OPENIMP_T40_PAYLOAD_SIZE_MASK;

    if (!layout || payload_size == 0u || payload_offset > capacity ||
        header_size > payload_offset ||
        payload_size > capacity - payload_offset)
        return -1;

    layout->payload_size = payload_size;
    layout->payload_end = payload_offset + payload_size;
    layout->access_unit_size = header_size + payload_size;
    return 0;
}
