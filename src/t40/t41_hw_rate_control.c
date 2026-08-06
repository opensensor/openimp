#include "t41_hw_rate_control.h"

#include <string.h>

static int openimp_t41_hwrc_level_offset(const void *ring, size_t ring_size,
                                         unsigned int slot,
                                         size_t *level_offset)
{
    size_t slot_offset;
    size_t offset;

    if (!ring || !level_offset || slot >= OPENIMP_T41_EP3_SLOT_COUNT)
        return -1;

    slot_offset = (size_t)slot * OPENIMP_T41_EP3_SLOT_STRIDE;
    offset = slot_offset + OPENIMP_T41_EP3_LEVEL_OFFSET;
    if (offset > ring_size || sizeof(uint32_t) > ring_size - offset)
        return -1;

    *level_offset = offset;
    return 0;
}

void openimp_t41_hwrc_level_init(OpenIMPT41HWRCLevelState *state)
{
    if (state)
        state->level = 0u;
}

int openimp_t41_hwrc_level_update(OpenIMPT41HWRCLevelState *state,
                                  const void *ring, size_t ring_size,
                                  unsigned int completed_slot)
{
    size_t level_offset;

    if (!state || openimp_t41_hwrc_level_offset(
                      ring, ring_size, completed_slot, &level_offset) != 0)
        return -1;

    memcpy(&state->level, (const uint8_t *)ring + level_offset,
           sizeof(state->level));
    return 0;
}

int openimp_t41_hwrc_level_set_buffer(
    const OpenIMPT41HWRCLevelState *state,
    void *ring, size_t ring_size, unsigned int next_slot)
{
    size_t level_offset;

    if (!state || openimp_t41_hwrc_level_offset(
                      ring, ring_size, next_slot, &level_offset) != 0)
        return -1;

    memcpy((uint8_t *)ring + level_offset, &state->level,
           sizeof(state->level));
    return 0;
}
