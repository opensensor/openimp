#ifndef OPENIMP_PROFILE_H
#define OPENIMP_PROFILE_H

#include <stdint.h>

typedef enum {
    OPENIMP_PROFILE_FRAME_SOURCE_WAIT = 0,
    OPENIMP_PROFILE_FRAME_SOURCE_DQBUF,
    OPENIMP_PROFILE_ENCODER_POLL,
    OPENIMP_PROFILE_ENCODE_SUBMIT,
    OPENIMP_PROFILE_SOURCE_STATS,
    OPENIMP_PROFILE_T41_PICTURE_STATE,
    OPENIMP_PROFILE_STREAM_HEADER,
    OPENIMP_PROFILE_COMMAND_BUILD,
    OPENIMP_PROFILE_COMMAND_COPY,
    OPENIMP_PROFILE_AVPU_SUBMIT_IO,
    OPENIMP_PROFILE_CACHE_MAINTENANCE,
    OPENIMP_PROFILE_COMPLETION_STATUS,
    OPENIMP_PROFILE_IRQ_COMPLETION,
    OPENIMP_PROFILE_STREAM_FINALIZE,
    OPENIMP_PROFILE_STREAM_COMPACT,
    OPENIMP_PROFILE_STAGE_COUNT
} OpenIMPProfileStage;

typedef enum {
    OPENIMP_PROFILE_DQBUF_RETRIES = 0,
    OPENIMP_PROFILE_GETSTREAM_RETRIES,
    OPENIMP_PROFILE_CACHE_BYTES,
    OPENIMP_PROFILE_COMPACT_BYTES,
    OPENIMP_PROFILE_STREAM_BYTES,
    OPENIMP_PROFILE_COUNTER_COUNT
} OpenIMPProfileCounter;

typedef struct {
    uint64_t wall_ns;
    uint64_t cpu_ns;
} OpenIMPProfileStamp;

int openimp_profile_enabled(void);
OpenIMPProfileStamp openimp_profile_begin(void);
void openimp_profile_end(OpenIMPProfileStage stage,
                         OpenIMPProfileStamp start);
void openimp_profile_count(OpenIMPProfileCounter counter, uint64_t amount);
void openimp_profile_frame_completed(uint32_t stream_bytes);

#endif
