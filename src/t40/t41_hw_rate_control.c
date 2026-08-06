#include "t41_hw_rate_control.h"

#include <string.h>

static const uint32_t openimp_t41_hwrc_qp_words[12][3] = {
    { 0x0c090706u, 0x08060504u, 0x130e0a08u },
    { 0x08060504u, 0x06050403u, 0x0e0a0806u },
    { 0x06050403u, 0x05040302u, 0x0a070504u },
    { 0x04030202u, 0x04030201u, 0x07050403u },
    { 0x03020101u, 0x03020101u, 0x05040302u },
    { 0x02010101u, 0x02010101u, 0x03020101u },
    { 0x00000000u, 0x01010101u, 0x00000000u },
    { 0xffffffffu, 0x00000000u, 0x00000000u },
    { 0xfdfeffffu, 0x00000000u, 0x00000000u },
    { 0xfcfdfefeu, 0x00000000u, 0x00000000u },
    { 0xfafbfcfdu, 0x00000000u, 0x00000000u },
    { 0xf8fafbfcu, 0x00000000u, 0x00000000u },
};

static const uint32_t openimp_t41_hwrc_lambda_words[103] = {
    0x5b, 0x66, 0x72, 0x80, 0x90, 0xa1, 0xb5, 0xcb, 0xe4, 0x100, 0x11f,
    0x143, 0x16a, 0x196, 0x1c8, 0x200, 0x23f, 0x285, 0x2d4, 0x32d, 0x390,
    0x400, 0x47d, 0x50a, 0x5a8, 0x659, 0x721, 0x800, 0x8fb, 0xa14, 0xb50,
    0xcb3, 0xe41, 0x1000, 0x11f6, 0x1429, 0x16a1, 0x1966, 0x1c82, 0x2000,
    0x23eb, 0x2851, 0x2d41, 0x32cc, 0x3904, 0x4000, 0x47d6, 0x50a3,
    0x5a82, 0x6598, 0x7209, 0x8000, 0x8fad, 0xa145, 0xb505, 0xcb30,
    0xe412, 0x10000, 0x11f5a, 0x1428a, 0x16a0a, 0x19660, 0x1c824,
    0x20000, 0x23eb3, 0x28514, 0x2d414, 0x32cc0, 0x39048, 0x40000,
    0x47d67, 0x50a29, 0x5a828, 0x65980, 0x72090, 0x80000, 0x8facd,
    0xa1451, 0xb504f, 0xcb2ff, 0xe411f, 0x100000, 0x11f59b, 0x1428a3,
    0x16a09e, 0x1965ff, 0x1c823e, 0x200000, 0x23eb36, 0x285146,
    0x2d413d, 0x32cbfd, 0x39047c, 0x400000, 0x47d66b, 0x50a28c,
    0x5a827a, 0x6597fb, 0x7208f8, 0x800000, 0x8facd6, 0xa14518,
    0xb504f3,
};

static uint32_t openimp_t41_hwrc_target(uint32_t bitrate,
                                        unsigned int slot)
{
    uint64_t target = bitrate;

    if (slot < 2u)
        target = target * 5u / 7u;
    if (slot == 0u)
        target = target * 10u / 13u;
    return (uint32_t)target;
}

size_t openimp_t41_hwrc_ring_init(void *ring, size_t ring_size,
                                  uint32_t bitrate)
{
    static const uint8_t row_percent[11] = {
        150u, 130u, 120u, 110u, 105u, 100u, 95u, 90u, 80u, 70u, 50u,
    };
    uint8_t *base = (uint8_t *)ring;
    unsigned int slot;

    if (!base || ring_size < OPENIMP_T41_EP3_RING_SIZE)
        return 0u;

    memset(base, 0, OPENIMP_T41_EP3_RING_SIZE);
    for (slot = 0u; slot < OPENIMP_T41_EP3_SLOT_COUNT; ++slot) {
        uint32_t *words = (uint32_t *)(base +
            (size_t)slot * OPENIMP_T41_EP3_SLOT_STRIDE);
        uint32_t target = openimp_t41_hwrc_target(bitrate, slot);
        uint32_t base_target =
            (uint32_t)((uint64_t)target * 95u / 100u);
        unsigned int row;

        for (row = 0u; row < 11u; ++row) {
            uint32_t *record = words + row * 8u;

            record[0] = (uint32_t)((uint64_t)base_target *
                                    row_percent[row] / 100u);
            record[1] = openimp_t41_hwrc_qp_words[row][0];
            record[3] = openimp_t41_hwrc_qp_words[row][1];
            record[5] = openimp_t41_hwrc_qp_words[row][2];
        }
        words[11u * 8u] = 1u;
        words[11u * 8u + 1u] = openimp_t41_hwrc_qp_words[11][0];
        memcpy(words + 16u * 8u, openimp_t41_hwrc_lambda_words,
               sizeof(openimp_t41_hwrc_lambda_words));
    }

    return OPENIMP_T41_EP3_PER_CORE_SIZE *
           OPENIMP_T41_EP3_SLOT_COUNT;
}

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
