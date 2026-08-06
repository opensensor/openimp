#include <stdint.h>
#include <stdio.h>

#include "t40/t41_stream_layout.h"

static int expect_valid(uint32_t capacity, uint32_t payload_offset,
                        uint32_t header_size, uint32_t payload_size,
                        uint32_t payload_end, uint32_t access_unit_size)
{
    OpenIMPT41StreamLayout layout = {0};

    if (openimp_t41_stream_layout(capacity, payload_offset, header_size,
                                  payload_size, &layout) != 0 ||
        layout.payload_end != payload_end ||
        layout.access_unit_size != access_unit_size) {
        fprintf(stderr,
                "valid layout mismatch: cap=%u off=%u hdr=%u payload=%u got=%u/%u expected=%u/%u\n",
                capacity, payload_offset, header_size, payload_size,
                layout.payload_end, layout.access_unit_size,
                payload_end, access_unit_size);
        return 1;
    }
    return 0;
}

static int expect_invalid(uint32_t capacity, uint32_t payload_offset,
                          uint32_t header_size, uint32_t payload_size)
{
    OpenIMPT41StreamLayout layout = {0};

    if (openimp_t41_stream_layout(capacity, payload_offset, header_size,
                                  payload_size, &layout) == 0) {
        fprintf(stderr,
                "invalid layout accepted: cap=%u off=%u hdr=%u payload=%u\n",
                capacity, payload_offset, header_size, payload_size);
        return 1;
    }
    return 0;
}

int main(void)
{
    int failed = 0;

    failed |= expect_valid(0x400000u, 0x220u, 42u, 100000u,
                           0x188c0u, 100042u);
    failed |= expect_valid(0x1000u, 0x220u, 0x220u, 0xde0u,
                           0x1000u, 0x1000u);
    failed |= expect_invalid(0x1000u, 0x220u, 42u, 0u);
    failed |= expect_invalid(0x1000u, 0x220u, 0x221u, 1u);
    failed |= expect_invalid(0x1000u, 0x1001u, 42u, 1u);
    failed |= expect_invalid(0x1000u, 0x220u, 42u, 0xde1u);

    if (openimp_t41_stream_layout(0x1000u, 0x220u, 42u, 1u, NULL) == 0) {
        fprintf(stderr, "NULL result accepted\n");
        failed = 1;
    }

    if (failed)
        return 1;
    puts("T41 stream-layout tests passed");
    return 0;
}
