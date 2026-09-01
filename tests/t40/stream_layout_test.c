#include <stdint.h>
#include <stdio.h>

#include "t40/t40_stream_layout.h"

static int expect_valid(uint32_t capacity, uint32_t payload_offset,
                        uint32_t header_size, uint32_t payload_status,
                        uint32_t payload_size, uint32_t payload_end,
                        uint32_t access_unit_size)
{
    OpenIMPT40StreamLayout layout = {0};

    if (openimp_t40_stream_layout(capacity, payload_offset, header_size,
                                  payload_status, &layout) != 0 ||
        layout.payload_size != payload_size ||
        layout.payload_end != payload_end ||
        layout.access_unit_size != access_unit_size) {
        fprintf(stderr,
                "valid layout mismatch: cap=%u off=%u hdr=%u status=0x%08x got=%u/%u/%u expected=%u/%u/%u\n",
                capacity, payload_offset, header_size, payload_status,
                layout.payload_size, layout.payload_end,
                layout.access_unit_size, payload_size, payload_end,
                access_unit_size);
        return 1;
    }
    return 0;
}

static int expect_invalid(uint32_t capacity, uint32_t payload_offset,
                          uint32_t header_size, uint32_t payload_status)
{
    OpenIMPT40StreamLayout layout = {0};

    if (openimp_t40_stream_layout(capacity, payload_offset, header_size,
                                  payload_status, &layout) == 0) {
        fprintf(stderr,
                "invalid layout accepted: cap=%u off=%u hdr=%u status=0x%08x\n",
                capacity, payload_offset, header_size, payload_status);
        return 1;
    }
    return 0;
}

int main(void)
{
    int failed = 0;

    /* Archived T40 values: OEM sub-IDR=0x5e9, flat probe=0x127. */
    failed |= expect_valid(0x384000u, 0x220u, 42u, 0x5e9u,
                           0x5e9u, 0x809u, 0x613u);
    failed |= expect_valid(0x384000u, 0x220u, 60u, 0xc0000127u,
                           0x127u, 0x347u, 0x163u);
    failed |= expect_valid(0x1000u, 0x220u, 0x220u, 0xde0u,
                           0xde0u, 0x1000u, 0x1000u);
    failed |= expect_invalid(0x1000u, 0x220u, 42u, 0u);
    failed |= expect_invalid(0x1000u, 0x220u, 0x221u, 1u);
    failed |= expect_invalid(0x1000u, 0x1001u, 42u, 1u);
    failed |= expect_invalid(0x1000u, 0x220u, 42u, 0xde1u);

    if (openimp_t40_stream_layout(0x1000u, 0x220u, 42u, 1u,
                                  NULL) == 0) {
        fprintf(stderr, "NULL result accepted\n");
        failed = 1;
    }

    if (failed)
        return 1;
    puts("T40 stream-layout tests passed");
    return 0;
}
