#include "t41_command_layout.h"

#include <limits.h>

/*
 * Recovered from the 29-entry table consumed by OEM PrepareCommand.  Entries
 * whose word count is zero are omitted.  Keeping this oracle independent of
 * codec state makes the eventual T41 payload writer directly host-testable.
 */
const OpenIMPT41CommandRange openimp_t41_command_ranges[] = {
    {   0u,  7u },
    {   8u,  6u },
    {  24u,  6u },
    {  32u,  6u },
    {  40u, 19u },
    {  64u,  1u },
    {  66u, 24u },
    { 108u,  4u },
    { 112u, 12u },
    { 124u,  4u },
    { 128u,  3u },
    { 136u,  6u },
    { 144u,  5u },
    { 152u, 14u },
    { 176u,  8u },
    { 192u,  1u },
    { 236u,  2u },
};

const size_t openimp_t41_command_range_count =
    sizeof(openimp_t41_command_ranges) /
    sizeof(openimp_t41_command_ranges[0]);

int openimp_t41_command_slot_is_valid(size_t slot_size)
{
    return slot_size >= OPENIMP_T41_CL_SLOT_SIZE &&
           OPENIMP_T41_CL_STATUS_OFFSET <= slot_size &&
           OPENIMP_T41_CL_STATUS_SIZE <=
               slot_size - OPENIMP_T41_CL_STATUS_OFFSET;
}

void *openimp_t41_command_status_ptr(void *slot, size_t slot_size)
{
    if (!slot || !openimp_t41_command_slot_is_valid(slot_size))
        return NULL;
    return (uint8_t *)slot + OPENIMP_T41_CL_STATUS_OFFSET;
}

const void *openimp_t41_command_status_const_ptr(const void *slot,
                                                 size_t slot_size)
{
    if (!slot || !openimp_t41_command_slot_is_valid(slot_size))
        return NULL;
    return (const uint8_t *)slot + OPENIMP_T41_CL_STATUS_OFFSET;
}

int openimp_t41_command_status_phys(uint32_t command_phys,
                                    uint32_t *status_phys)
{
    if (!status_phys || command_phys > UINT32_MAX - OPENIMP_T41_CL_STATUS_OFFSET)
        return -1;
    *status_phys = command_phys + OPENIMP_T41_CL_STATUS_OFFSET;
    return 0;
}
