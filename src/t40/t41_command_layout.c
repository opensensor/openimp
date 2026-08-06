#include "t41_command_layout.h"

#include <limits.h>
#include <string.h>

static uint16_t openimp_t41_read_u16(const uint8_t *base, size_t offset)
{
    uint16_t value;

    memcpy(&value, base + offset, sizeof(value));
    return value;
}

static uint32_t openimp_t41_read_u32(const uint8_t *base, size_t offset)
{
    uint32_t value;

    memcpy(&value, base + offset, sizeof(value));
    return value;
}

static void openimp_t41_write_u16(uint8_t *base, size_t offset,
                                  uint16_t value)
{
    memcpy(base + offset, &value, sizeof(value));
}

static void openimp_t41_write_u32(uint8_t *base, size_t offset,
                                  uint32_t value)
{
    memcpy(base + offset, &value, sizeof(value));
}

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

int openimp_t41_command_extract_encoding_status(
    const void *slot, size_t slot_size,
    void *slice_status, size_t slice_status_size)
{
    const uint8_t *command = (const uint8_t *)slot;
    const uint8_t *status;
    uint8_t *slice = (uint8_t *)slice_status;
    uint32_t packed_qp;
    uint32_t command_flags;
    uint32_t overflow = 0u;

    if (!command || !slice ||
        !openimp_t41_command_slot_is_valid(slot_size) ||
        slice_status_size < OPENIMP_T41_SLICE_STATUS_SIZE)
        return -1;

    status = command + OPENIMP_T41_CL_STATUS_OFFSET;
    openimp_t41_write_u32(slice, 0x34u,
                          openimp_t41_read_u32(status, 0x28u));
    openimp_t41_write_u32(slice, 0x38u,
                          openimp_t41_read_u32(status, 0x2cu) &
                              0x0fffffffu);
    packed_qp = openimp_t41_read_u32(status, 0x30u);
    openimp_t41_write_u16(slice, 0x3eu, (uint16_t)(packed_qp & 0xffu));
    openimp_t41_write_u16(slice, 0x40u,
                          (uint16_t)((packed_qp >> 8) & 0xffu));
    openimp_t41_write_u16(slice, 0x42u,
                          (uint16_t)(packed_qp >> 16));

    openimp_t41_write_u32(slice, 0x44u,
                          openimp_t41_read_u32(status, 0x34u));
    openimp_t41_write_u32(slice, 0x48u,
                          openimp_t41_read_u32(status, 0x38u));
    openimp_t41_write_u32(slice, 0x4cu,
                          openimp_t41_read_u32(status, 0x3cu));
    openimp_t41_write_u32(slice, 0x50u,
                          openimp_t41_read_u32(status, 0x40u));
    openimp_t41_write_u32(slice, 0x54u,
                          openimp_t41_read_u32(status, 0x44u));
    openimp_t41_write_u32(slice, 0x58u,
                          openimp_t41_read_u32(status, 0x48u));
    openimp_t41_write_u32(slice, 0x5cu,
                          openimp_t41_read_u32(status, 0x4cu));

    openimp_t41_write_u32(slice, 0x14u,
                          openimp_t41_read_u32(status, 0x08u));
    openimp_t41_write_u32(slice, 0x18u,
                          openimp_t41_read_u32(status, 0x0cu));
    openimp_t41_write_u32(slice, 0x1cu,
                          openimp_t41_read_u32(status, 0x10u));
    openimp_t41_write_u32(slice, 0x20u,
                          openimp_t41_read_u32(status, 0x14u));
    openimp_t41_write_u32(slice, 0x24u,
                          openimp_t41_read_u32(status, 0x18u));
    openimp_t41_write_u32(slice, 0x28u,
                          openimp_t41_read_u32(status, 0x1cu));
    openimp_t41_write_u32(slice, 0x2cu,
                          openimp_t41_read_u32(status, 0x20u));
    openimp_t41_write_u32(slice, 0x30u,
                          openimp_t41_read_u16(status, 0x24u));
    openimp_t41_write_u32(slice, 0x10u,
                          openimp_t41_read_u16(status, 0x26u));

    openimp_t41_write_u32(slice, 0x60u,
                          openimp_t41_read_u32(status, 0x50u));
    openimp_t41_write_u32(slice, 0x64u,
                          openimp_t41_read_u32(status, 0x54u));
    openimp_t41_write_u32(slice, 0x68u,
                          openimp_t41_read_u32(status, 0x58u));
    openimp_t41_write_u32(slice, 0x6cu,
                          openimp_t41_read_u32(status, 0x5cu));

    command_flags = openimp_t41_read_u32(command, 0x0cu);
    if ((command_flags & (1u << 11)) == 0u) {
        uint32_t bytes = openimp_t41_read_u32(status, 0x00u) &
                         0x3fffffffu;
        uint32_t budget = openimp_t41_read_u32(command, 0x234u);
        uint32_t rounded_bytes = (bytes + 0x1fu) & ~0x1fu;
        uint32_t rounded_budget = ((budget >> 5) - 1u) << 5;

        overflow = rounded_budget < rounded_bytes;
    }
    slice[2] = (uint8_t)overflow;
    return 0;
}

int openimp_t41_command_extract_entropy_status(
    const void *slot, size_t slot_size,
    void *slice_status, size_t slice_status_size,
    uint32_t stream_budget)
{
    const uint8_t *command = (const uint8_t *)slot;
    const uint8_t *status;
    uint8_t *slice = (uint8_t *)slice_status;
    uint32_t status_word;
    uint32_t bytes;
    uint32_t rounded_bytes;
    uint32_t rounded_budget;
    uint32_t command_flags;

    if (!command || !slice ||
        !openimp_t41_command_slot_is_valid(slot_size) ||
        slice_status_size < OPENIMP_T41_SLICE_STATUS_SIZE)
        return -1;

    command_flags = openimp_t41_read_u32(command, 0x0cu);
    status = command + (((command_flags & (1u << 11)) == 0u)
                        ? OPENIMP_T41_CL_ENTROPY_STATUS_OFFSET
                        : OPENIMP_T41_CL_STATUS_OFFSET);
    status_word = openimp_t41_read_u32(status, 0x00u);
    bytes = status_word & 0x3fffffffu;
    openimp_t41_write_u32(slice, 0x08u, bytes);
    openimp_t41_write_u32(slice, 0x0cu,
                          openimp_t41_read_u32(status, 0x04u));

    if ((command_flags & (1u << 11)) != 0u) {
        uint32_t slice_count =
            (uint32_t)openimp_t41_read_u16(slice, 0x42u) + 1u;

        bytes += (uint32_t)command[0x12u] * slice_count;
    }
    rounded_bytes = (bytes + 0x1fu) & ~0x1fu;
    rounded_budget = ((stream_budget >> 5) - 1u) << 5;
    slice[0] = (uint8_t)(status_word >> 31);
    slice[1] = (uint8_t)(rounded_budget < rounded_bytes);
    return 0;
}

int openimp_t41_slice_status_extract_rate_control(
    const void *slice_status, size_t slice_status_size,
    void *rate_control_stats, size_t rate_control_stats_size)
{
    const uint8_t *slice = (const uint8_t *)slice_status;
    uint8_t *stats = (uint8_t *)rate_control_stats;

    if (!slice || !stats ||
        slice_status_size < OPENIMP_T41_SLICE_STATUS_SIZE ||
        rate_control_stats_size < OPENIMP_T41_RC_STATS_SIZE)
        return -1;

    openimp_t41_write_u32(stats, 0x00u, openimp_t41_read_u32(slice, 0x04u));
    openimp_t41_write_u32(stats, 0x04u, openimp_t41_read_u32(slice, 0x08u));
    openimp_t41_write_u32(stats, 0x08u, openimp_t41_read_u32(slice, 0x0cu));
    openimp_t41_write_u32(stats, 0x0cu, openimp_t41_read_u32(slice, 0x1cu));
    openimp_t41_write_u32(stats, 0x10u, openimp_t41_read_u32(slice, 0x20u));
    openimp_t41_write_u32(stats, 0x14u, openimp_t41_read_u32(slice, 0x24u));
    openimp_t41_write_u32(stats, 0x18u, openimp_t41_read_u32(slice, 0x28u));
    openimp_t41_write_u32(stats, 0x1cu, openimp_t41_read_u32(slice, 0x2cu));
    openimp_t41_write_u32(stats, 0x20u, openimp_t41_read_u32(slice, 0x38u));
    openimp_t41_write_u16(stats, 0x24u, openimp_t41_read_u16(slice, 0x3eu));
    openimp_t41_write_u16(stats, 0x26u, openimp_t41_read_u16(slice, 0x40u));
    return 0;
}
