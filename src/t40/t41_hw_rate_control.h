#ifndef OPENIMP_T41_HW_RATE_CONTROL_H
#define OPENIMP_T41_HW_RATE_CONTROL_H

#include <stddef.h>
#include <stdint.h>

#define OPENIMP_T41_EP3_PER_CORE_SIZE      0x1420u
#define OPENIMP_T41_EP3_SLOT_STRIDE         0x1500u
#define OPENIMP_T41_EP3_SLOT_COUNT          3u
#define OPENIMP_T41_EP3_RING_SIZE           \
    (OPENIMP_T41_EP3_SLOT_STRIDE * OPENIMP_T41_EP3_SLOT_COUNT)
#define OPENIMP_T41_EP3_HISTORY_OFFSET      0x1360u
#define OPENIMP_T41_EP3_HISTORY_WORD_COUNT  36u
#define OPENIMP_T41_EP3_LEVEL_OFFSET        0x1400u

typedef struct OpenIMPT41HWRCLevelState {
    uint32_t level;
} OpenIMPT41HWRCLevelState;

/* Initialize the three picture-class tables recovered from OEM
 * PreprocessHwRateCtrl. Returns the initialized per-core byte count. */
size_t openimp_t41_hwrc_ring_init(void *ring, size_t ring_size,
                                  uint32_t bitrate);

/* These helpers reproduce the OEM manager's scalar transition. The caller
 * must provide a cache-coherent or explicitly synchronized EP3 mapping. */
void openimp_t41_hwrc_level_init(OpenIMPT41HWRCLevelState *state);
int openimp_t41_hwrc_level_update(OpenIMPT41HWRCLevelState *state,
                                  const void *ring, size_t ring_size,
                                  unsigned int completed_slot);
int openimp_t41_hwrc_level_set_buffer(
    const OpenIMPT41HWRCLevelState *state,
    void *ring, size_t ring_size, unsigned int next_slot);

#endif
