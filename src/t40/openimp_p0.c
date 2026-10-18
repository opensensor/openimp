/* Clean standalone OpenIMP T40 foundation.  No OEM libimp dependency. */

#include <pthread.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "trace_control.h"

#if defined(PLATFORM_T21) && !defined(PLATFORM_T20)
/* T21: the stock EncoderInit allocates the shared Helix bitstream buffer
 * ("vpuBs") */
#include "t30/helix_bitstream.h"
#define OPENIMP_HELIX_SHARED_BS 1
#endif
#if defined(PLATFORM_T21) || defined(PLATFORM_T30) || defined(PLATFORM_T23)
/* Helix JPEG (src/t30/helix_jpeg.c): its buffers and VPU channel go at
 * IMP_System_Exit, as the stock EncoderExit */
#include "t30/helix_jpeg.h"
#define OPENIMP_HELIX_JPEG 1
#endif

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
/* A mutex, not a spin lock: these SoCs have one CPU, so a thread spinning
 * for the lock (the FrameSource worker normalises every capture timestamp
 * under it) keeps the holder from running until it is preempted, and for
 * good if the spinner has a real-time priority. */
static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;

static void lock_state(void)
{
    pthread_mutex_lock(&state_lock);
}

static void unlock_state(void)
{
    pthread_mutex_unlock(&state_lock);
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
#if defined(PLATFORM_T20)
    state.cpu_id = 12; /* T20X */
#elif defined(PLATFORM_T21)
    state.cpu_id = 17; /* T21N */
#elif defined(PLATFORM_T31)
    state.cpu_id = 21; /* T31X */
#elif defined(PLATFORM_T30)
    state.cpu_id = 20; /* T30X */
#elif defined(PLATFORM_T41)
    state.cpu_id = 23; /* T41NQ */
#else
    state.cpu_id = 22; /* T40-XP */
#endif
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
#if defined(OPENIMP_HELIX_SHARED_BS)
        /* a failure is logged; the first channel tries again */
        (void)OpenIMP_HelixBitstream_Init();
#endif
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
#if defined(OPENIMP_HELIX_JPEG)
        OpenIMP_HelixJpeg_Exit();
#endif
#if defined(OPENIMP_HELIX_SHARED_BS)
        OpenIMP_HelixBitstream_Exit();
#endif
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

    if (openimp_startup_trace_enabled()) {
        static const char message[] = "libimp/System: Init entry\n";

        write(STDERR_FILENO, message, sizeof(message) - 1u);
        fsync(STDERR_FILENO);
    }
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
        if (openimp_startup_trace_enabled()) {
            char message[64];
            int length = snprintf(message, sizeof(message),
                                  "libimp/System: root %d begin\n", root);

            if (length > 0) {
                write(STDERR_FILENO, message, (size_t)length);
                fsync(STDERR_FILENO);
            }
        }
        result = root_init((enum openimp_root_id)root);
        if (openimp_startup_trace_enabled()) {
            char message[64];
            int length = snprintf(message, sizeof(message),
                                  "libimp/System: root %d ret=%d\n", root,
                                  result);

            if (length > 0) {
                write(STDERR_FILENO, message, (size_t)length);
                fsync(STDERR_FILENO);
            }
        }
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
#if defined(PLATFORM_T20)
    static const char text[] = "libimp.so T20 openimp";
#elif defined(PLATFORM_T21)
    static const char text[] = "libimp.so T21 openimp";
#elif defined(PLATFORM_T23)
    static const char text[] = "libimp.so T23 openimp";
#elif defined(PLATFORM_T30)
    static const char text[] = "libimp.so T30 openimp";
#elif defined(PLATFORM_T31)
    static const char text[] = "libimp.so T31 openimp";
#elif defined(PLATFORM_T41)
    static const char text[] = "libimp.so T41 openimp";
#else
    static const char text[] = "libimp.so T40 openimp-p0";
#endif

    if (!version)
        return -1;
    memset(version, 0, 64);
    strncpy(version, text, 63);
    return 0;
}

#if defined(PLATFORM_T20)
extern int32_t get_cpu_id(void);
#endif

#if (defined(PLATFORM_T21) && !defined(PLATFORM_T20)) || defined(PLATFORM_T23) || \
    defined(PLATFORM_T31) || \
    (!defined(PLATFORM_T20) && !defined(PLATFORM_T21) && !defined(PLATFORM_T23) && \
     !defined(PLATFORM_T30) && !defined(PLATFORM_T31) && !defined(PLATFORM_T41))
#include <fcntl.h>
#include <sys/mman.h>

/* One 32-bit SoC register through /dev/mem (the vendor get_cpuinfo does the
 * same); -1 when it cannot be read. */
static int p0_read_soc_reg(uint32_t addr, uint32_t *value)
{
    long page = sysconf(_SC_PAGESIZE);
    volatile uint32_t *regs;
    int fd;

    if (page <= 0)
        page = 4096;
    fd = open("/dev/mem", O_RDONLY | O_SYNC | O_CLOEXEC);
    if (fd < 0)
        return -1;
    regs = mmap(NULL, (size_t)page, PROT_READ, MAP_SHARED, fd,
                (off_t)(addr & ~((uint32_t)page - 1u)));
    close(fd);
    if (regs == MAP_FAILED)
        return -1;
    *value = regs[(addr & ((uint32_t)page - 1u)) / 4u];
    munmap((void *)regs, (size_t)page);
    return 0;
}

/* Sub-type variant of the SoC, decoded like the vendor get_cpu_id (T31
 * 1.1.5, T23 1.1.2): soc id 0x1300002c (family nibble 1, id 0x21 T21, 0x23
 * T23, 0x31 T31), CPPSR 0x10000034 (byte), 0x13540230 (byte 2) and
 * 0x13540238 (upper half) select the variant.  Returns NULL when the
 * registers cannot be read or do not match this build's SoC (the caller
 * then keeps the constant it always reported).  Read once. */
static const char *p0_soc_variant(void)
{
    static const char *cached;
    static int done;
    uint32_t soc, cppsr, d, c;
    unsigned int sub, chip, flag;

    if (done)
        return cached;
    done = 1;
#if !defined(PLATFORM_T21) && !defined(PLATFORM_T23) && !defined(PLATFORM_T31)
    /* T40, vendor get_cpu_id (T40 1.x libimp 0x1bf70): soc id 0x1300002c
     * (family nibble 1, chip id 0x40), the upper half of 0x13540250 selects
     * the variant: 0x1111 T40-N, 0x7777 T40-XP, 0x8888 T40-NN, 0x4444 T40-A;
     * 0 is T40-N; any other value is "Unknown" in the vendor library. */
    (void)cppsr; (void)d; (void)c; (void)flag; (void)sub; (void)chip;
    if (p0_read_soc_reg(0x1300002cu, &soc) || (soc >> 28) != 1u ||
        ((soc >> 12) & 0xffffu) != 0x40u ||
        p0_read_soc_reg(0x13540250u, &c))
        return NULL;
    switch ((c >> 16) & 0xffffu) {
    case 0x0000u:
    case 0x1111u: cached = "T40-N"; break;
    case 0x7777u: cached = "T40-XP"; break;
    case 0x8888u: cached = "T40-NN"; break;
    case 0x4444u: cached = "T40-A"; break;
    default:      cached = "Unknown"; break;
    }
    return cached;
#endif
    if (p0_read_soc_reg(0x1300002cu, &soc) || (soc >> 28) != 1u ||
        p0_read_soc_reg(0x10000034u, &cppsr) ||
        p0_read_soc_reg(0x13540230u, &d) ||
        p0_read_soc_reg(0x13540238u, &c))
        return NULL;
    chip = (soc >> 12) & 0xffffu;
    sub = (c >> 16) & 0xffffu;
    flag = (d >> 16) & 0xffu;
#if defined(PLATFORM_T21)
    if (chip != 0x21u)
        return NULL;
    if ((cppsr & 0xffu) == 0x10u)
        cached = "T21-L";
    else if ((cppsr & 0xffu) != 1u)
        return NULL;
    else if (flag == 0)
        cached = sub == 0x3333u ? "T21-L" : sub == 0x1111u ? "T21-N"
               : sub == 0x5555u ? "T21-Z" : "T21-X";
    else if (flag == 3u || flag == 0xfu)
        cached = "T21-L";
    else if (flag == 1u || flag == 7u)
        cached = "T21-N";
#elif defined(PLATFORM_T23)
    (void)cppsr; (void)flag;
    if (chip != 0x23u)
        return NULL;
    cached = sub == 0x7777u ? "T23-ZN" : "T23-N";
#else
    (void)cppsr;
    if (chip != 0x31u)
        return NULL;
    if (flag == 0) {
        switch (sub) {
        case 0x3333u: cached = "T31L"; break;
        case 0x1111u: cached = "T31N"; break;
        case 0x2222u: cached = "T31X"; break;
        case 0x4444u: cached = "T31A"; break;
        case 0x5555u: cached = "T31ZL"; break;
        case 0x6666u: cached = "T31ZX"; break;
        case 0xccccu: cached = "T31AL"; break;
        case 0xddddu: cached = "T31ZC"; break;
        case 0xeeeeu: cached = "T31LC"; break;
        default:      cached = "T31ZX"; break;
        }
    } else if (flag == 3u || flag == 0xfu) {
        cached = "T31L";
    } else if (flag == 1u || flag == 7u) {
        cached = "T31N";
    }
#endif
    return cached;
}
#endif

const char *IMP_System_GetCPUInfo(void)
{
#if defined(PLATFORM_T20)
    /* vendor IMP_System_GetCPUInfo: 0 "T10", 1/2 "T10-Lite" */
    switch (get_cpu_id()) {
    case 0:
        return "T10";
    case 1:
    case 2:
        return "T10-Lite";
    default:
        return "T20-X";
    }
#elif defined(PLATFORM_T21)
    {
        const char *variant = p0_soc_variant();

        return variant ? variant : "T21-N";
    }
#elif defined(PLATFORM_T23)
    {
        const char *variant = p0_soc_variant();

        return variant ? variant : "T23-N";
    }
#elif defined(PLATFORM_T30)
    return "T30-X";
#elif defined(PLATFORM_T31)
    {
        const char *variant = p0_soc_variant();

        return variant ? variant : "T31X";
    }
#elif defined(PLATFORM_T41)
    return "T41NQ";
#else
    {
        const char *variant = p0_soc_variant();

        return variant ? variant : "T40-XP";
    }
#endif
}

int64_t IMP_System_GetTimeStamp(void)
{
    uint64_t now = monotonic_us();
    uint64_t base;

    if (!now)
        return -1;
    /* the 64-bit base is two words on MIPS32: read (and set) it under
     * the lock, as Init and RebaseTimeStamp write it */
    lock_state();
    if (!state.timestamp_base_us)
        state.timestamp_base_us = now;
    base = state.timestamp_base_us;
    unlock_state();
    return (int64_t)(now - base);
}

/* FrameSource DQBUF timestamps use the same absolute monotonic clock as the
 * system timestamp base.  Keep the conversion here so the T31/T41 driver
 * seam does not duplicate or guess P0's private rebase state. */
int64_t OpenIMP_P0_NormalizeMonotonicTimeStamp(uint64_t timestamp)
{
    uint64_t base;

    lock_state();
    base = state.timestamp_base_us;
    unlock_state();
    if (!base || timestamp < base)
        return -1;
    return (int64_t)(timestamp - base);
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
