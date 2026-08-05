#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "t40/t41_command_layout.h"

int main(void)
{
    uint8_t slot[OPENIMP_T41_CL_SLOT_SIZE];
    uint32_t status_phys = 0;
    size_t i;
    size_t previous_end = 0;

    assert(openimp_t41_command_slot_is_valid(sizeof(slot)));
    assert(!openimp_t41_command_slot_is_valid(sizeof(slot) - 1u));
    assert(openimp_t41_command_status_ptr(slot, sizeof(slot)) ==
           slot + OPENIMP_T41_CL_STATUS_OFFSET);
    assert(openimp_t41_command_status_const_ptr(slot, sizeof(slot)) ==
           slot + OPENIMP_T41_CL_STATUS_OFFSET);
    assert(openimp_t41_command_status_ptr(NULL, sizeof(slot)) == NULL);

    assert(openimp_t41_command_status_phys(0x06337200u, &status_phys) == 0);
    assert(status_phys == 0x063377c0u);
    assert(openimp_t41_command_status_phys(UINT32_MAX, &status_phys) == -1);
    assert(openimp_t41_command_status_phys(0u, NULL) == -1);

    assert(openimp_t41_command_range_count == 17u);
    for (i = 0; i < openimp_t41_command_range_count; ++i) {
        size_t start = (size_t)openimp_t41_command_ranges[i].word_offset * 4u;
        size_t end = start +
            (size_t)openimp_t41_command_ranges[i].word_count * 4u;

        assert(openimp_t41_command_ranges[i].word_count != 0u);
        assert(start >= previous_end);
        assert(end <= OPENIMP_T41_CL_STATUS_OFFSET);
        previous_end = end;
    }
    assert(previous_end == 0x03b8u);
    assert(OPENIMP_T41_CL_STATUS_OFFSET + OPENIMP_T41_CL_STATUS_SIZE <=
           OPENIMP_T41_CL_ENTROPY_OFFSET);
    assert(OPENIMP_T41_CL_ENTROPY_OFFSET + OPENIMP_T41_CL_ENTROPY_SIZE <=
           OPENIMP_T41_CL_SLOT_SIZE);

    puts("T41 command layout: OK");
    return 0;
}
