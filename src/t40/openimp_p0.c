/* Clean standalone OpenIMP T40 foundation.  No OEM libimp dependency. */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

#define OPENIMP_P0_MAGIC             0x50305434U /* "P0T4" */
#define OPENIMP_MAX_FS_CHANNELS      16
#define OPENIMP_MAX_ENCODER_GROUPS   16
#define OPENIMP_MAX_ENCODER_CHANNELS 16
#define OPENIMP_MAX_BIND_EDGES       64
#define OPENIMP_MAX_POOLS            32
#define OPENIMP_MAX_OSD_REGIONS      64
#define OPENIMP_MAX_IVS_GROUPS       16
#define OPENIMP_ROOT_COUNT           6
#define OPENIMP_ALL_ROOTS            ((1U << OPENIMP_ROOT_COUNT) - 1U)

#ifndef CLOCK_MONOTONIC_RAW
#define CLOCK_MONOTONIC_RAW 4
#endif

enum openimp_root_id {
    OPENIMP_ROOT_DSYSTEM = 0,
    OPENIMP_ROOT_FRAMESOURCE,
    OPENIMP_ROOT_IVS,
    OPENIMP_ROOT_OSD,
    OPENIMP_ROOT_ENCODER,
    OPENIMP_ROOT_FB
};

struct openimp_device_state {
    int fd;
    uint32_t opened;
    uint32_t users;
};

struct openimp_cell {
    int device_id;
    int group_id;
    int output_id;
};

struct openimp_bind_edge {
    uint32_t active;
    struct openimp_cell source;
    struct openimp_cell destination;
};

struct openimp_pool_state {
    int pool_id;
    uint32_t active;
    uint32_t size;
    uint32_t physical;
    void *virtual_address;
    uint32_t users;
};

struct openimp_dsystem_state {
    uint32_t initialized;
    struct openimp_bind_edge binds[OPENIMP_MAX_BIND_EDGES];
    struct openimp_pool_state pools[OPENIMP_MAX_POOLS];
};

struct openimp_isp_state {
    struct openimp_device_state isp;
    struct openimp_device_state tuning;
    struct openimp_device_state sensor;
    uint32_t sensor_registered;
    uint32_t sensor_enabled;
    uint32_t tuning_enabled;
    uint32_t sensor_fps_num;
    uint32_t sensor_fps_den;
    char sensor_name[32];
};

struct openimp_framesource_channel_state {
    int index;
    int pool_id;
    uint32_t created;
    uint32_t enabled;
    uint32_t depth;
    uint32_t bind_users;
    unsigned char attributes[256];
};

struct openimp_framesource_state {
    uint32_t initialized;
    struct openimp_device_state device;
    struct openimp_framesource_channel_state channels[OPENIMP_MAX_FS_CHANNELS];
};

struct openimp_encoder_group_state {
    int index;
    uint32_t created;
    uint32_t channel_mask;
};

struct openimp_encoder_channel_state {
    int index;
    int group_id;
    int stream_pool_id;
    int frame_pool_id;
    uint32_t created;
    uint32_t registered;
    uint32_t receiving;
    uint32_t outstanding_streams;
    unsigned char attributes[512];
};

struct openimp_encoder_state {
    uint32_t initialized;
    struct openimp_device_state avpu;
    struct openimp_encoder_group_state groups[OPENIMP_MAX_ENCODER_GROUPS];
    struct openimp_encoder_channel_state channels[OPENIMP_MAX_ENCODER_CHANNELS];
};

struct openimp_ivs_state {
    uint32_t initialized;
    uint32_t group_mask;
    uint32_t channel_mask[OPENIMP_MAX_IVS_GROUPS];
};

struct openimp_osd_region_state {
    int handle;
    int group_id;
    uint32_t created;
    uint32_t shown;
};

struct openimp_osd_state {
    uint32_t initialized;
    uint32_t pool_size;
    struct openimp_osd_region_state regions[OPENIMP_MAX_OSD_REGIONS];
};

struct openimp_state {
    uint32_t magic;
    uint32_t generation;
    uint32_t initialized_roots;
    uint32_t initialized;
    int main_process;
    int cpu_id;
    uint64_t timestamp_base_us;
    struct openimp_dsystem_state dsystem;
    struct openimp_isp_state isp;
    struct openimp_framesource_state framesource;
    struct openimp_encoder_state encoder;
    struct openimp_ivs_state ivs;
    struct openimp_osd_state osd;
    uint32_t fb_initialized;
};

static struct openimp_state state;
static volatile uint32_t state_lock;

static void lock_state(void)
{
    while (__sync_lock_test_and_set(&state_lock, 1)) {
        while (state_lock)
            ;
    }
}

static void unlock_state(void)
{
    __sync_synchronize();
    state_lock = 0;
}

static uint64_t monotonic_us(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) < 0)
        return 0;
    return (uint64_t)(uint32_t)ts.tv_sec * 1000000ULL +
           (uint64_t)(uint32_t)ts.tv_nsec / 1000ULL;
}

static void reset_isp(void)
{
    memset(&state.isp, 0, sizeof(state.isp));
    state.isp.isp.fd = -1;
    state.isp.tuning.fd = -1;
    state.isp.sensor.fd = -1;
    state.isp.sensor_fps_den = 1;
}

static void prepare_state(void)
{
    int i;

    if (state.magic == OPENIMP_P0_MAGIC)
        return;
    memset(&state, 0, sizeof(state));
    state.magic = OPENIMP_P0_MAGIC;
    state.cpu_id = 22; /* T40-XP */
    reset_isp();
    for (i = 0; i < OPENIMP_MAX_POOLS; i++)
        state.dsystem.pools[i].pool_id = -1;
}

static int root_init(enum openimp_root_id root)
{
    int i;
    uint32_t bit = 1U << root;

    if (state.initialized_roots & bit)
        return 0;
    switch (root) {
    case OPENIMP_ROOT_DSYSTEM:
        memset(&state.dsystem, 0, sizeof(state.dsystem));
        for (i = 0; i < OPENIMP_MAX_POOLS; i++)
            state.dsystem.pools[i].pool_id = -1;
        state.dsystem.initialized = 1;
        break;
    case OPENIMP_ROOT_FRAMESOURCE:
        memset(&state.framesource, 0, sizeof(state.framesource));
        state.framesource.device.fd = -1;
        for (i = 0; i < OPENIMP_MAX_FS_CHANNELS; i++) {
            state.framesource.channels[i].index = i;
            state.framesource.channels[i].pool_id = -1;
        }
        state.framesource.initialized = 1;
        break;
    case OPENIMP_ROOT_IVS:
        memset(&state.ivs, 0, sizeof(state.ivs));
        state.ivs.initialized = 1;
        break;
    case OPENIMP_ROOT_OSD:
        memset(&state.osd, 0, sizeof(state.osd));
        for (i = 0; i < OPENIMP_MAX_OSD_REGIONS; i++) {
            state.osd.regions[i].handle = -1;
            state.osd.regions[i].group_id = -1;
        }
        state.osd.initialized = 1;
        break;
    case OPENIMP_ROOT_ENCODER:
        memset(&state.encoder, 0, sizeof(state.encoder));
        state.encoder.avpu.fd = -1;
        for (i = 0; i < OPENIMP_MAX_ENCODER_GROUPS; i++)
            state.encoder.groups[i].index = i;
        for (i = 0; i < OPENIMP_MAX_ENCODER_CHANNELS; i++) {
            state.encoder.channels[i].index = i;
            state.encoder.channels[i].group_id = -1;
            state.encoder.channels[i].stream_pool_id = -1;
            state.encoder.channels[i].frame_pool_id = -1;
        }
        state.encoder.initialized = 1;
        break;
    case OPENIMP_ROOT_FB:
        state.fb_initialized = 1;
        break;
    default:
        return -1;
    }
    state.initialized_roots |= bit;
    return 0;
}

static void root_exit(enum openimp_root_id root)
{
    uint32_t bit = 1U << root;

    if (!(state.initialized_roots & bit))
        return;
    switch (root) {
    case OPENIMP_ROOT_DSYSTEM:
        memset(&state.dsystem, 0, sizeof(state.dsystem));
        break;
    case OPENIMP_ROOT_FRAMESOURCE:
        memset(&state.framesource, 0, sizeof(state.framesource));
        break;
    case OPENIMP_ROOT_IVS:
        memset(&state.ivs, 0, sizeof(state.ivs));
        break;
    case OPENIMP_ROOT_OSD:
        memset(&state.osd, 0, sizeof(state.osd));
        break;
    case OPENIMP_ROOT_ENCODER:
        memset(&state.encoder, 0, sizeof(state.encoder));
        break;
    case OPENIMP_ROOT_FB:
        state.fb_initialized = 0;
        break;
    }
    state.initialized_roots &= ~bit;
}

int IMP_System_Init(void)
{
    int root;
    int result;
    uint64_t now;

    lock_state();
    if (state.initialized) {
        unlock_state();
        return 0;
    }
    prepare_state();
    now = monotonic_us();
    if (!now) {
        unlock_state();
        return -1;
    }
    state.timestamp_base_us = now;
    for (root = 0; root < OPENIMP_ROOT_COUNT; root++) {
        result = root_init((enum openimp_root_id)root);
        if (result < 0) {
            while (--root >= 0)
                root_exit((enum openimp_root_id)root);
            unlock_state();
            return result;
        }
    }
    state.main_process = 1;
    state.generation++;
    state.initialized = 1;
    unlock_state();
    return 0;
}

int IMP_System_Exit(void)
{
    int root;

    lock_state();
    if (!state.initialized) {
        unlock_state();
        return 0;
    }
    for (root = OPENIMP_ROOT_COUNT - 1; root >= 0; root--)
        root_exit((enum openimp_root_id)root);
    state.main_process = 0;
    state.initialized = 0;
    unlock_state();
    return 0;
}

int IMP_System_GetVersion(char *version)
{
    static const char text[] = "libimp.so T40 openimp-p0";

    if (!version)
        return -1;
    memset(version, 0, 64);
    strncpy(version, text, 63);
    return 0;
}

const char *IMP_System_GetCPUInfo(void)
{
    return "T40-XP";
}

int64_t IMP_System_GetTimeStamp(void)
{
    uint64_t now = monotonic_us();

    if (!now)
        return -1;
    if (!state.timestamp_base_us)
        state.timestamp_base_us = now;
    return (int64_t)(now - state.timestamp_base_us);
}

int IMP_System_RebaseTimeStamp(int64_t timestamp)
{
    uint64_t now = monotonic_us();

    if (!now)
        return -1;
    lock_state();
    state.timestamp_base_us = now - (uint64_t)timestamp;
    unlock_state();
    return 0;
}

int OpenIMP_P0_GetState(uint32_t *generation, uint32_t *roots,
                        int32_t *main_process, int32_t *cpu_id)
{
    if (!generation || !roots || !main_process || !cpu_id)
        return -1;
    *generation = state.generation;
    *roots = state.initialized_roots;
    *main_process = state.main_process;
    *cpu_id = state.cpu_id;
    return (int)state.initialized;
}
