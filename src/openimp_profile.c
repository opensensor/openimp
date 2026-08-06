#include "openimp_profile.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

#ifndef CLOCK_MONOTONIC_RAW
#define CLOCK_MONOTONIC_RAW 4
#endif

#ifndef CLOCK_THREAD_CPUTIME_ID
#define CLOCK_THREAD_CPUTIME_ID 3
#endif

#define OPENIMP_PROFILE_DEFAULT_INTERVAL 250u
#define OPENIMP_PROFILE_MIN_INTERVAL 25u

typedef struct {
    uint64_t calls;
    uint64_t wall_ns;
    uint64_t cpu_ns;
    uint64_t max_wall_ns;
    uint64_t max_cpu_ns;
} OpenIMPProfileStat;

static const char *const profile_stage_names[OPENIMP_PROFILE_STAGE_COUNT] = {
    "frame_source_wait",
    "frame_source_dqbuf",
    "encoder_poll",
    "encode_submit",
    "source_stats",
    "t41_picture_state",
    "stream_header",
    "command_build",
    "command_copy",
    "avpu_submit_io",
    "cache_maintenance",
    "completion_status",
    "irq_completion",
    "stream_finalize",
    "stream_compact",
};

static const char *const profile_counter_names[OPENIMP_PROFILE_COUNTER_COUNT] = {
    "dqbuf_retries",
    "getstream_retries",
    "cache_bytes",
    "compact_bytes",
    "stream_bytes",
};

static OpenIMPProfileStat profile_stats[OPENIMP_PROFILE_STAGE_COUNT];
static uint64_t profile_counters[OPENIMP_PROFILE_COUNTER_COUNT];
static uint64_t profile_completed_frames;
static unsigned int profile_report_interval =
    OPENIMP_PROFILE_DEFAULT_INTERVAL;
static volatile int profile_lock;
static volatile int profile_mode = -1;

static void profile_take_lock(void)
{
    while (__sync_lock_test_and_set(&profile_lock, 1)) {
        while (profile_lock)
            ;
    }
}

static void profile_drop_lock(void)
{
    __sync_synchronize();
    profile_lock = 0;
}

static uint64_t profile_clock_ns(clockid_t clock_id)
{
    struct timespec now;

    if (clock_gettime(clock_id, &now) != 0)
        return 0u;
    return (uint64_t)now.tv_sec * 1000000000ull +
           (uint64_t)now.tv_nsec;
}

int openimp_profile_enabled(void)
{
    int mode = profile_mode;

    if (mode < 0) {
        const char *setting = getenv("OPENIMP_PROFILE");
        const char *interval = getenv("OPENIMP_PROFILE_INTERVAL");
        char *end = NULL;
        unsigned long requested;

        mode = setting && setting[0] != '\0' &&
               strcmp(setting, "0") != 0;
        if (mode && interval && interval[0] != '\0') {
            errno = 0;
            requested = strtoul(interval, &end, 10);
            if (!errno && end != interval && *end == '\0' &&
                requested >= OPENIMP_PROFILE_MIN_INTERVAL &&
                requested <= 1000000ul)
                profile_report_interval = (unsigned int)requested;
        }
        __sync_synchronize();
        profile_mode = mode;
        if (mode)
            syslog(LOG_NOTICE,
                   "openimp-profile: enabled interval_frames=%u",
                   profile_report_interval);
    }
    return mode;
}

OpenIMPProfileStamp openimp_profile_begin(void)
{
    OpenIMPProfileStamp stamp = {0u, 0u};

    if (!openimp_profile_enabled())
        return stamp;
    stamp.wall_ns = profile_clock_ns(CLOCK_MONOTONIC_RAW);
    stamp.cpu_ns = profile_clock_ns(CLOCK_THREAD_CPUTIME_ID);
    return stamp;
}

void openimp_profile_end(OpenIMPProfileStage stage,
                         OpenIMPProfileStamp start)
{
    OpenIMPProfileStat *stat;
    uint64_t wall_end;
    uint64_t cpu_end;
    uint64_t wall_elapsed;
    uint64_t cpu_elapsed;

    if ((unsigned int)stage >= OPENIMP_PROFILE_STAGE_COUNT ||
        start.wall_ns == 0u)
        return;
    wall_end = profile_clock_ns(CLOCK_MONOTONIC_RAW);
    cpu_end = profile_clock_ns(CLOCK_THREAD_CPUTIME_ID);
    wall_elapsed = wall_end >= start.wall_ns ? wall_end - start.wall_ns : 0u;
    cpu_elapsed = cpu_end >= start.cpu_ns ? cpu_end - start.cpu_ns : 0u;

    profile_take_lock();
    stat = &profile_stats[stage];
    ++stat->calls;
    stat->wall_ns += wall_elapsed;
    stat->cpu_ns += cpu_elapsed;
    if (wall_elapsed > stat->max_wall_ns)
        stat->max_wall_ns = wall_elapsed;
    if (cpu_elapsed > stat->max_cpu_ns)
        stat->max_cpu_ns = cpu_elapsed;
    profile_drop_lock();
}

void openimp_profile_count(OpenIMPProfileCounter counter, uint64_t amount)
{
    if (!openimp_profile_enabled() ||
        (unsigned int)counter >= OPENIMP_PROFILE_COUNTER_COUNT)
        return;
    profile_take_lock();
    profile_counters[counter] += amount;
    profile_drop_lock();
}

static void profile_report(uint64_t frames)
{
    OpenIMPProfileStat stats[OPENIMP_PROFILE_STAGE_COUNT];
    uint64_t counters[OPENIMP_PROFILE_COUNTER_COUNT];
    unsigned int i;

    profile_take_lock();
    memcpy(stats, profile_stats, sizeof(stats));
    memcpy(counters, profile_counters, sizeof(counters));
    profile_drop_lock();

    syslog(LOG_NOTICE, "openimp-profile: report frames=%llu",
           (unsigned long long)frames);
    for (i = 0u; i < OPENIMP_PROFILE_STAGE_COUNT; ++i) {
        const OpenIMPProfileStat *stat = &stats[i];
        uint64_t average_wall = stat->calls
            ? stat->wall_ns / stat->calls : 0u;
        uint64_t average_cpu = stat->calls
            ? stat->cpu_ns / stat->calls : 0u;

        syslog(LOG_NOTICE,
               "openimp-profile: frames=%llu stage=%s calls=%llu wall_total_us=%llu wall_avg_us=%llu wall_max_us=%llu cpu_total_us=%llu cpu_avg_us=%llu cpu_max_us=%llu",
               (unsigned long long)frames, profile_stage_names[i],
               (unsigned long long)stat->calls,
               (unsigned long long)(stat->wall_ns / 1000u),
               (unsigned long long)(average_wall / 1000u),
               (unsigned long long)(stat->max_wall_ns / 1000u),
               (unsigned long long)(stat->cpu_ns / 1000u),
               (unsigned long long)(average_cpu / 1000u),
               (unsigned long long)(stat->max_cpu_ns / 1000u));
    }
    for (i = 0u; i < OPENIMP_PROFILE_COUNTER_COUNT; ++i)
        syslog(LOG_NOTICE,
               "openimp-profile: frames=%llu counter=%s value=%llu",
               (unsigned long long)frames, profile_counter_names[i],
               (unsigned long long)counters[i]);
}

void openimp_profile_frame_completed(uint32_t stream_bytes)
{
    uint64_t frames;
    int report;

    if (!openimp_profile_enabled())
        return;
    profile_take_lock();
    frames = ++profile_completed_frames;
    profile_counters[OPENIMP_PROFILE_STREAM_BYTES] += stream_bytes;
    report = (frames % profile_report_interval) == 0u;
    profile_drop_lock();
    if (report)
        profile_report(frames);
}
