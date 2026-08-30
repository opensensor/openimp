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

static uint32_t find_annexb_start4(const uint8_t *data, uint32_t offset,
                                   uint32_t length)
{
    while (offset + 4u <= length) {
        if (data[offset] == 0u && data[offset + 1u] == 0u &&
            data[offset + 2u] == 0u && data[offset + 3u] == 1u)
            return offset;
        offset++;
    }
    return length;
}

int openimp_t31_annexb_nals(const uint8_t *data, uint32_t length,
                            OpenIMPT31AnnexBNAL *nals, uint32_t capacity)
{
    uint32_t begin;
    uint32_t count = 0u;

    if (!data || !nals || !length || !capacity)
        return -1;

    begin = find_annexb_start4(data, 0u, length);
    if (begin != 0u)
        return 0;

    while (begin < length) {
        uint32_t nal_header = begin + 4u;
        uint32_t next;

        if (nal_header >= length || count >= capacity)
            return -1;
        next = find_annexb_start4(data, nal_header, length);
        if (next <= nal_header)
            return -1;
        nals[count].offset = begin;
        nals[count].length = next - begin;
        nals[count].nal_type = data[nal_header] & 0x1fu;
        count++;
        begin = next;
    }

    return (int)count;
}
