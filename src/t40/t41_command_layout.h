#ifndef OPENIMP_T41_COMMAND_LAYOUT_H
#define OPENIMP_T41_COMMAND_LAYOUT_H

#include <stddef.h>
#include <stdint.h>

/*
 * Measured T41 command-list ABI.  Unlike T31/T40, command and status share a
 * 4 KiB slot and the hardware receives distinct pointers for each half.
 */
#define OPENIMP_T41_CL_SLOT_SIZE       0x1000u
#define OPENIMP_T41_CL_STATUS_OFFSET   0x05c0u
#define OPENIMP_T41_CL_STATUS_SIZE     0x0168u
#define OPENIMP_T41_CL_ENTROPY_OFFSET  0x0800u
#define OPENIMP_T41_CL_ENTROPY_SIZE    0x003cu

typedef struct OpenIMPT41CommandRange {
    uint16_t word_offset;
    uint16_t word_count;
} OpenIMPT41CommandRange;

/* Non-empty ranges copied by the OEM PrepareCommand descriptor table. */
extern const OpenIMPT41CommandRange openimp_t41_command_ranges[];
extern const size_t openimp_t41_command_range_count;

int openimp_t41_command_slot_is_valid(size_t slot_size);
void *openimp_t41_command_status_ptr(void *slot, size_t slot_size);
const void *openimp_t41_command_status_const_ptr(const void *slot,
                                                 size_t slot_size);
int openimp_t41_command_status_phys(uint32_t command_phys,
                                    uint32_t *status_phys);

#endif
