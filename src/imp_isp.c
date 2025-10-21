/**
 * IMP ISP Module Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/stat.h>


#include <pthread.h>
#include <sys/ioctl.h>
#include <imp/imp_isp.h>
#include "isp_ioctl_compat.h"


/* Forward declarations for DMA allocation */
int IMP_Alloc(char *name, int size, char *tag);
int IMP_Free(uint32_t phys_addr);

/* Forward declarations for logging (vendor functions) */

/* Forward declarations for vendor-like tuning getters used internally */
int IMP_ISP_Tuning_GetTotalGain(uint32_t *pgain);

int IMP_Log_Get_Option(void);
void imp_log_fun(int level, int option, int type, ...);

#define LOG_ISP(fmt, ...) fprintf(stderr, "[IMP_ISP] " fmt "\n", ##__VA_ARGS__)



/* Forward declaration: ISP global stream/link start */
int ISP_EnsureLinkStreamOn(int sensor_idx);

/* Forward declaration to allow pointer use in ISPDevice */
struct ISPTuningState;

/* ISP Device structure (userspace) */
typedef struct {
    char dev_name[32];      /* Device name, e.g. "/dev/tx-isp" */
    int fd;                 /* Main device fd */
    int tisp_fd;            /* Optional tuning device fd ("/dev/tisp"), -1 if not open */
    int opened;             /* Opened flag; +2 indicates sensor enabled */
    uint8_t data[0xb8];     /* Scratch/device data area for mirroring sensor info, etc. */
    void *isp_buffer_virt;  /* Virtual address of ISP RAW buffer */
    unsigned long isp_buffer_phys; /* Physical address of ISP RAW buffer */
    uint32_t isp_buffer_size;      /* Size of ISP RAW buffer */
    /* Optional secondary RAW buffer (some sensors/driver configs request this) */
    void *isp_buffer2_virt;        /* Virtual address of second RAW buffer */
    unsigned long isp_buffer2_phys;/* Physical address of second RAW buffer */
    uint32_t isp_buffer2_size;     /* Size of second RAW buffer */
    /* Safe pointer to internal tuning state (vendor kept this at +0x9c) */
    struct ISPTuningState *tuning;
} ISPDevice;

static ISPDevice *gISPdev = NULL;
static int sensor_enabled = 0;
static int tuning_enabled = 0;
static int isp_stream_started = 0; /* Deferred ISP streaming flag */
static int bypass_link_setup_done = 0;  /* Track if SetISPBypass already did LINK_SETUP */

/* Expose streaming state for other modules (e.g., AVPU) to gate init safely */
int ISP_IsStreaming(void) {
    return isp_stream_started;
}

/* ISP tuning daemon and related methods (safe struct access version) */

typedef void (*IspTuningFunc)(void* arg);

typedef struct {
    /* Match vendor semantics but use safe, explicit fields */
    char            name[0x14];   /* up to 20 chars incl. NUL in vendor */
    int             used;         /* non-zero when slot is used */
    IspTuningFunc   fn;           /* callback */
    void*           arg;          /* opaque arg passed to callback */
} IspTuningSlot;

#define ISP_TUNING_MAX_SLOTS 10

static pthread_mutex_t g_isp_deamon_mutex = PTHREAD_MUTEX_INITIALIZER;
/* Cached total gain (kept as a legacy cache; primary lives in ISPTuningState) */
static uint32_t g_total_gain = 0;


/* Safe internal tuning state (mirrors vendor internal allocation at +0x9c) */
typedef struct ISPTuningState {
    /* Byte used by contrastjudge; vendor read *(state + 9) */
    uint8_t contrast_byte;
    uint8_t _pad[3];
    /* Cached total gain and last-sent total gain for contrastjudge */
    uint32_t total_gain;
    uint32_t last_total_gain;

    /* Daemon registry/state (centralized) */
    uint32_t tuning_mask;                     /* bitmask controlling slots */
    IspTuningSlot slots[ISP_TUNING_MAX_SLOTS];
    int daemon_info;                          /* non-zero while thread should run */
    int daemon_init;                          /* set on first enable */
    pthread_t daemon_thread;                  /* thread id */

    /* Video drop detection state */
    uint32_t vic_frd_c;                       /* last counter from /proc */
    unsigned video_drop_status;               /* consecutive same-count detections */
    unsigned video_drop_notify_c;             /* notifications emitted (limited) */
    void (*video_drop_cb)(void);              /* optional callback hook */
} ISPTuningState;

static void* isp_tuning_deamon_thread(void* arg)
{
    (void)arg;

    /* Name the thread to match vendor */
    prctl(PR_SET_NAME, "isp_tuning_deamon_thread");

    pthread_mutex_lock(&g_isp_deamon_mutex);

    ISPTuningState *st = (gISPdev ? gISPdev->tuning : NULL);
    if (st && st->daemon_info != 0) {
        for (;;) {
            /* Optional runtime override of mask via file (vendor behavior) */
            FILE* fp = fopen("/tmp/isp_tuning_func", "r");
            if (fp) {
                unsigned int mask = 0;
                /* Try hex first, then decimal */
                if (fscanf(fp, "%x", &mask) == 1 || fscanf(fp, "%u", &mask) == 1) {
                    st->tuning_mask = mask;
                }
                fclose(fp);
            }

            int executed = 0;
            for (int i = 0; i < ISP_TUNING_MAX_SLOTS; ++i) {
                IspTuningSlot *slot = &st->slots[i];
                IspTuningFunc fn = slot->fn;
                void* cb_arg = slot->arg;
                uint32_t bit = (uint32_t)1u << (i & 31);

                if (fn != NULL && (st->tuning_mask & bit) != 0) {
                    /* Call while holding the mutex, like vendor code */
                    fn(cb_arg);
                    executed++;
                }
            }

            if (executed == 0) {
                LOG_ISP("isp_tuning_deamon_thread: no functions executed; exiting");
                break;
            }

            pthread_mutex_unlock(&g_isp_deamon_mutex);
            sleep(1);
            pthread_mutex_lock(&g_isp_deamon_mutex);

            if (st->daemon_info == 0) {
                break;
            }
        }
    }

    pthread_mutex_unlock(&g_isp_deamon_mutex);
    return NULL;
}

static int isp_tuning_deamon_func_add(const char* name, IspTuningFunc fn)
{
    if (name == NULL || fn == NULL) return -1;

    size_t nlen = strnlen(name, sizeof(((IspTuningSlot*)0)->name));
    if (nlen >= sizeof(((IspTuningSlot*)0)->name)) {
        LOG_ISP("deamon_func_add: name too long (max %zu)", sizeof(((IspTuningSlot*)0)->name) - 1);
        return -1;
    }

    pthread_mutex_lock(&g_isp_deamon_mutex);

    ISPTuningState *st = (gISPdev ? gISPdev->tuning : NULL);
    if (!st || st->daemon_init == 0) {
        LOG_ISP("deamon_func_add: daemon not initialized");
        pthread_mutex_unlock(&g_isp_deamon_mutex);
        return -1;
    }

    /* Duplicate check */
    for (int i = 0; i < ISP_TUNING_MAX_SLOTS; ++i) {
        if (st->slots[i].used && st->slots[i].fn == fn &&
            strncmp(st->slots[i].name, name, sizeof(st->slots[i].name)) == 0) {
            LOG_ISP("deamon_func_add: duplicate entry for '%s'", name);
            pthread_mutex_unlock(&g_isp_deamon_mutex);
            return 0;
        }
    }

    /* Find free slot */
    for (int i = 0; i < ISP_TUNING_MAX_SLOTS; ++i) {
        if (!st->slots[i].used && st->slots[i].fn == NULL) {
            /* Fill slot safely */
            IspTuningSlot* s = &st->slots[i];
            memset(s, 0, sizeof(*s));
            memcpy(s->name, name, nlen);
            s->name[nlen] = '\0';
            s->used = 1;
            s->fn = fn;
            s->arg = NULL; /* vendor sets arg to 0 on add */

            /* Enable this slot's bit by default like vendor */
            st->tuning_mask |= (uint32_t)1u << (i & 31);

            /* Start daemon thread if not running */
            if (st->daemon_info == 0 && st->daemon_thread == 0) {
                int rc = pthread_create(&st->daemon_thread, NULL, isp_tuning_deamon_thread, NULL);
                if (rc != 0) {
                    st->daemon_thread = 0;
                    LOG_ISP("deamon_func_add: pthread_create failed: %d", rc);
                } else {
                    st->daemon_info = 1;
                }
            }

            pthread_mutex_unlock(&g_isp_deamon_mutex);
            return 0;
        }
    }

    pthread_mutex_unlock(&g_isp_deamon_mutex);
    LOG_ISP("deamon_func_add: registry full");
    return -1;
}

static int isp_tuning_deamon_func_del(const char* name, IspTuningFunc fn)
{
    if (name == NULL || fn == NULL) return -1;

    pthread_mutex_lock(&g_isp_deamon_mutex);

    ISPTuningState *st = (gISPdev ? gISPdev->tuning : NULL);
    if (!st || st->daemon_init == 0) {
        LOG_ISP("deamon_func_del: daemon not initialized");
        pthread_mutex_unlock(&g_isp_deamon_mutex);
        return -1;
    }

    int removed = 0;
    for (int i = 0; i < ISP_TUNING_MAX_SLOTS; ++i) {
        IspTuningSlot* s = &st->slots[i];
        if (s->used && s->fn == fn && strncmp(s->name, name, sizeof(s->name)) == 0) {
            memset(s, 0, sizeof(*s));
            removed++;
        }
    }

    if (removed >= 2) {
        LOG_ISP("deamon_func_del: removed %d entries (unexpected duplicates)", removed);
    }

    pthread_mutex_unlock(&g_isp_deamon_mutex);
    return 0;
}
/* Default periodic tuning callback (placeholder) */
static void tuning_periodic(void* arg)
{
    (void)arg;
    /* Placeholder: vendor uses daemon to run periodic tuning tasks. */
}

/* Vendor-registered daemon callbacks (safe placeholders) */
static void tuning_update_total_gain(void* arg)
{
    (void)arg;
    uint32_t gain = 0;
    if (IMP_ISP_Tuning_GetTotalGain(&gain) < 0) {
        if (gISPdev && gISPdev->tuning) gISPdev->tuning->total_gain = 0;
        g_total_gain = 0;
        return;
    }
    if (gISPdev && gISPdev->tuning) gISPdev->tuning->total_gain = gain;
    g_total_gain = gain; /* keep legacy cache in sync */
}

static void tuning_contrastjudge(void* arg)
{
    (void)arg;

    if (gISPdev == NULL || gISPdev->tisp_fd < 0)
        return;

    /* Only run after sensor/link is enabled to avoid EPERM before streaming */
    if (gISPdev->opened < 2)
        return;

    ISPTuningState *st = gISPdev->tuning;
    if (st == NULL)
        return;

    /* Act only when total_gain is non-zero and changed */
    if (st->total_gain == 0 || st->total_gain == st->last_total_gain)
        return;

    unsigned char contrast = st->contrast_byte;

    /* BN MCP: ioctl 0xc008561c expects { id (V4L2_CID_*), value } where id=0x00980901 (CONTRAST) */
    struct {
        uint32_t id;     /* 0x00980901 = V4L2_CID_CONTRAST */
        uint32_t value;  /* contrast value (lower 8 bits used) */
    } cmd;

    cmd.id = 0x00980901u;
    cmd.value = (uint32_t)contrast;

    static int cj_disabled = 0;
    if (cj_disabled)
        return;

    if (ioctl(gISPdev->tisp_fd, 0xc008561c, &cmd) < 0) {
        int e = errno;
        /* On EPERM/ENOTTY, disable this function quietly to avoid log spam */
        if (e == EPERM || e == ENOTTY) {
            cj_disabled = 1;
            /* Do not modify tuning_mask here; cj_disabled will skip future calls */
        } else {
            LOG_ISP("contrastjudge: ioctl(0x%08x) failed: %s", 0xc008561c, strerror(e));
        }
        return;
    }

    st->last_total_gain = st->total_gain;
}

static void tuning_videodrop(void* arg)
{
    (void)arg;

    ISPTuningState *st = (gISPdev ? gISPdev->tuning : NULL);
    if (!st) return;

    FILE *fp = fopen("/proc/jz/isp/isp-w02", "r");
    if (fp == NULL) {
        LOG_ISP("videodrop: failed to open %s: %s", "/proc/jz/isp/isp-w02", strerror(errno));
        return;
    }

    unsigned int cnt = 0;
    if (fscanf(fp, "%u", &cnt) != 1) {
        LOG_ISP("videodrop: failed to read from %s", "/proc/jz/isp/isp-w02");
        fclose(fp);
        return;
    }

    if (cnt == st->vic_frd_c) {
        st->video_drop_status++;
    } else {
        st->vic_frd_c = cnt;
        st->video_drop_status = 0;
        st->video_drop_notify_c = 0;
    }

    if (st->video_drop_status >= 2) {
        st->video_drop_status = 0;
        if (st->video_drop_notify_c < 4) {
            st->video_drop_notify_c++;
            LOG_ISP("videodrop: video drop detected (notify #%u)", st->video_drop_notify_c);
            if (st->video_drop_cb) {
                st->video_drop_cb();
            }
        }
    }

    fclose(fp);
}


/* AE/AWB algorithm state */
static int ae_algo_en = 0;
static int awb_algo_en = 0;
static void *ae_func_tmp = NULL;
static void *awb_func_tmp = NULL;

/* AE/AWB Algorithm Data Buffer
 * Based on driver decompilation at tisp_ae_algo_init:
 * - Driver copies 0x80 bytes from userspace: private_copy_from_user($v0_114, arg3, 0x80)
 * - Driver WRITES to offsets 0x08-0x7f, filling the buffer with AE/AWB parameters
 * - First field must be version magic 0x336ac
 * - This is NOT a callback structure - it's a DATA BUFFER for AE/AWB parameters
 *
 * The driver writes values like:
 *   *(arg2 + 0x08) = 0
 *   *(arg2 + 0x0c) = data_c46e8
 *   *(arg2 + 0x10) = data_c46e0
 *   *(arg2 + 0x14) = 0x400
 *   ... etc up to offset 0x7f
 *
 * The structure MUST be exactly 0x80 (128) bytes.
 */
typedef struct {
    uint32_t version;                   /* 0x00: Version magic - MUST be 0x336ac */
    uint8_t data[0x80 - 4];             /* 0x04-0x7f: AE/AWB parameter data filled by driver */
} IMPISPAlgoData;

/* AE/AWB thread functions
 * The vendor creates a pthread after the AE ioctl succeeds.
 * This thread must periodically provide AE/AWB updates to the ISP.
 */
static pthread_t ae_thread = 0;
static pthread_t awb_thread = 0;
static volatile int ae_thread_running = 0;
static volatile int awb_thread_running = 0;

/* AE thread - based on vendor's impisp_algo_thread1 at 0x8abe4 */
static void* ae_thread_func(void* arg) {
    LOG_ISP("AE thread started");
    ae_thread_running = 1;

    /* Allocate buffers matching vendor sizes */
    uint8_t ae_stats_buf[0x7d8];  /* AE stats from ISP */
    uint8_t ae_result_buf[0x34];   /* AE result to send back */

    /* Initialize buffers */
    memset(ae_stats_buf, 0, sizeof(ae_stats_buf));
    memset(ae_result_buf, 0, sizeof(ae_result_buf));

    /* Set version magic - CRITICAL */
    *(uint32_t*)ae_stats_buf = 0x336ac;
    *(uint32_t*)ae_result_buf = 0x336ac;

    /* Main loop - exactly like vendor */
    while (1) {
        pthread_testcancel();

        /* Get AE statistics from ISP - ioctl 0x800456db */
        if (ioctl(gISPdev->fd, 0x800456db, ae_stats_buf) != 0) {
            LOG_ISP("AE thread: ioctl 0x800456db failed: %s", strerror(errno));
            imp_log_fun(6, IMP_Log_Get_Option(), 2, 0xea498, 0xea4b0, 0x1222, 0xebc4c, 0xea538, 0xebc4c, 0x1222, NULL);
            return NULL;
        }

        /* Call the AE run callback to process stats and generate result
         * Vendor calls: (*(gISPdev + 0xc4))(priv_data_self, &stats, &result)
         * We don't have the callback, so we need to implement a basic AE algorithm
         * or just pass through the stats without modification.
         *
         * For now, the result buffer stays as initialized (zeros with version magic).
         * The ISP should use its built-in AE if we don't provide valid results.
         */

        /* Set AE result back to ISP - ioctl 0x800456dc */
        if (ioctl(gISPdev->fd, 0x800456dc, ae_result_buf) != 0) {
            LOG_ISP("AE thread: ioctl 0x800456dc failed: %s", strerror(errno));
            imp_log_fun(6, IMP_Log_Get_Option(), 2, 0xea498, 0xea4b0, 0x122c, 0xebc4c, 0xea56c, 0xebc4c, 0x122c, NULL);
            return NULL;
        }

        /* No sleep in vendor code - it runs as fast as the ioctls allow */
    }

    LOG_ISP("AE thread stopped");
    return NULL;
}

/* AWB thread - based on vendor's impisp_algo_thread5 at 0x8ae4c */
static void* awb_thread_func(void* arg) {
    LOG_ISP("AWB thread started");
    awb_thread_running = 1;

    /* Allocate buffers matching vendor sizes */
    uint8_t awb_stats_buf[0x2c3];  /* AWB stats from ISP */
    uint8_t awb_result_buf[0x14];   /* AWB result to send back */

    /* Initialize buffers */
    memset(awb_stats_buf, 0, sizeof(awb_stats_buf));
    memset(awb_result_buf, 0, sizeof(awb_result_buf));

    /* Set version magic - CRITICAL */
    *(uint32_t*)awb_stats_buf = 0x336ac;
    *(uint32_t*)awb_result_buf = 0x336ac;

    /* Main loop - exactly like vendor */
    while (1) {
        pthread_testcancel();

        /* Get AWB statistics from ISP - ioctl 0xc00456e1 */
        if (ioctl(gISPdev->fd, 0xc00456e1, awb_stats_buf) != 0) {
            LOG_ISP("AWB thread: ioctl 0xc00456e1 failed: %s", strerror(errno));
            imp_log_fun(6, IMP_Log_Get_Option(), 2, 0xea498, 0xea4b0, 0x128a, 0xebbf8, 0xea5a0, 0xebbf8, 0x128a, NULL);
            return NULL;
        }

        /* Call the AWB run callback to process stats and generate result
         * Vendor calls: (*(gISPdev + 0xd8))(awb_priv_data_self, &stats, &result)
         * We don't have the callback, so we need to implement a basic AWB algorithm
         * or just pass through the stats without modification.
         *
         * For now, the result buffer stays as initialized (zeros with version magic).
         * The ISP should use its built-in AWB if we don't provide valid results.
         */

        /* Set AWB result back to ISP - ioctl 0xc00456e2 */
        if (ioctl(gISPdev->fd, 0xc00456e2, awb_result_buf) != 0) {
            LOG_ISP("AWB thread: ioctl 0xc00456e2 failed: %s", strerror(errno));
            imp_log_fun(6, IMP_Log_Get_Option(), 2, 0xea498, 0xea4b0, 0x1290, 0xebbf8, 0xea5d4, 0xebbf8, 0x1290, NULL);
            return NULL;
        }

        /* No sleep in vendor code - it runs as fast as the ioctls allow */
    }

    LOG_ISP("AWB thread stopped");
    return NULL;
}

/* Global AE/AWB data buffers
 * These are passed to the ioctls and the driver fills them with AE/AWB parameters
 * The version field MUST be 0x336ac for the driver to accept the buffer
 * The data array is zero-initialized and will be filled by the driver
 */
static IMPISPAlgoData g_ae_data = {
    .version = 0x336ac        /* CRITICAL: Driver validates this magic number */
    /* data is automatically zero-initialized, driver will fill it */
};

static IMPISPAlgoData g_awb_data = {
    .version = 0x336ac        /* CRITICAL: Driver validates this magic number */
    /* data is automatically zero-initialized, driver will fill it */
};

/* Core ISP Functions */

/* IMP_ISP_Open - based on decompilation at 0x8b6ec */
int IMP_ISP_Open(void) {
    if (gISPdev != NULL) {
        LOG_ISP("Open: already opened");
        return 0;
    }

    /* Allocate ISP device structure */
    gISPdev = (ISPDevice*)calloc(1, sizeof(ISPDevice));
    if (gISPdev == NULL) {
        LOG_ISP("Open: failed to allocate ISP device");
        return -1;
    }

    /* Set device name */
    strcpy(gISPdev->dev_name, "/dev/tx-isp");

    /* Open ISP device */
    gISPdev->fd = open(gISPdev->dev_name, O_RDWR | O_NONBLOCK);
    if (gISPdev->fd < 0) {
        LOG_ISP("Open: failed to open %s: %s", gISPdev->dev_name, strerror(errno));
        free(gISPdev);
        gISPdev = NULL;
        return -1;
    }

    /* Do not open /dev/tisp here; driver creates it later when streaming starts.
     * We will lazily open it in IMP_ISP_EnableTuning if needed. */
    gISPdev->tisp_fd = -1;

    /* Mark as opened */
    gISPdev->opened = 1;

    LOG_ISP("Open: opened %s (fd=%d)", gISPdev->dev_name, gISPdev->fd);
    return 0;
}

/* IMP_ISP_Close - based on decompilation at 0x8b8d8 */
int IMP_ISP_Close(void) {
    if (gISPdev == NULL) {
        LOG_ISP("Close: not opened");
        return 0;
    }

    /* Check if opened flag is >= 2 (sensor enabled) */
    if (gISPdev->opened >= 2) {
        LOG_ISP("Close: sensor still enabled");
        return -1;
    }

    /* Free ISP buffer if allocated */
    if (gISPdev->isp_buffer_phys != 0) {
        IMP_Free(gISPdev->isp_buffer_phys);
        gISPdev->isp_buffer_virt = NULL;
        gISPdev->isp_buffer_phys = 0;
        gISPdev->isp_buffer_size = 0;
    }
    /* Free optional secondary ISP buffer if allocated */
    if (gISPdev->isp_buffer2_phys != 0) {
        IMP_Free(gISPdev->isp_buffer2_phys);
        gISPdev->isp_buffer2_virt = NULL;
        gISPdev->isp_buffer2_phys = 0;
        gISPdev->isp_buffer2_size = 0;
    }

    /* Close devices */
    if (gISPdev->fd >= 0) {
        close(gISPdev->fd);
    }
    if (gISPdev->tisp_fd >= 0) {
        close(gISPdev->tisp_fd);
    }

    /* Free device structure */
    free(gISPdev);
    gISPdev = NULL;

    LOG_ISP("Close: closed ISP device");
    return 0;
}

/* IMP_ISP_AddSensor - based on decompilation at 0x8bd6c */
int IMP_ISP_AddSensor(IMPSensorInfo *pinfo) {
    if (pinfo == NULL) {
        LOG_ISP("AddSensor: NULL sensor info");
        return -1;
    }

    if (gISPdev == NULL) {
        LOG_ISP("AddSensor: ISP not opened");
        return -1;
    }

    if (gISPdev->opened >= 2) {
        LOG_ISP("AddSensor: sensor already enabled");
        return -1;
    }

    /* Log the key IMPSensorInfo fields for debugging */
    LOG_ISP("AddSensor: name='%s' cbus=%d i2c.type='%s' i2c.addr=0x%x rst_gpio=%d",
            pinfo->name, pinfo->cbus_type, pinfo->i2c.type, pinfo->i2c.addr,
            pinfo->rst_gpio);
    LOG_ISP("AddSensor: pwdn_gpio=%d power_gpio=%d",
            pinfo->pwdn_gpio, pinfo->power_gpio);

    /* REGISTER_SENSOR - Stock T23 libimp calls this first */
    LOG_ISP("AddSensor: calling REGISTER_SENSOR ioctl(0x%08x)", TX_ISP_REGISTER_SENSOR);
    if (ioctl(gISPdev->fd, TX_ISP_REGISTER_SENSOR, pinfo) != 0) {
        LOG_ISP("AddSensor: REGISTER_SENSOR failed: %s (errno=%d)", strerror(errno), errno);
        return -1;
    }
    LOG_ISP("AddSensor: REGISTER_SENSOR succeeded");

    /* Enumerate sensors to find the one we just registered */
    int sensor_idx = -1;
    int idx = 0;
    struct {
        int index;
        char name[32];
        char extra[44]; /* padding to make total 80 bytes */
    } enum_input;

    while (1) {
        memset(&enum_input, 0, sizeof(enum_input));
        enum_input.index = idx;

        if (ioctl(gISPdev->fd, 0xc050561a, &enum_input) != 0) {
            /* End of enumeration */
            break;
        }

        LOG_ISP("AddSensor: enum idx=%d name='%s'", idx, enum_input.name);

        if (strcmp(pinfo->name, enum_input.name) == 0) {
            sensor_idx = idx;
            LOG_ISP("AddSensor: found matching sensor at index %d", sensor_idx);
        }
        idx++;
    }

    if (sensor_idx == -1) {
        LOG_ISP("AddSensor: sensor %s not found in enumeration", pinfo->name);
        return -1;
    }

    /* Copy sensor info into our scratch area */
    memcpy(&gISPdev->data[0], pinfo, sizeof(IMPSensorInfo));

    /* CRITICAL: Set active sensor input via ioctl 0xc0045627 (TX_ISP_SENSOR_SET_INPUT) */
    int input_index = sensor_idx;
    LOG_ISP("AddSensor: calling TX_ISP_SENSOR_SET_INPUT with index=%d", input_index);

    if (ioctl(gISPdev->fd, 0xc0045627, &input_index) != 0) {
        LOG_ISP("AddSensor: TX_ISP_SENSOR_SET_INPUT failed: %s", strerror(errno));
        return -1;
    }

    LOG_ISP("AddSensor: TX_ISP_SENSOR_SET_INPUT succeeded");

    /* CRITICAL: Get buffer info via TX_ISP_GET_BUF (platform-dependent) */
    tx_isp_buf_t buf_info;
    TXISP_BUF_INIT(buf_info);
    TXISP_BUF_SET_INDEX(buf_info, input_index);
    LOG_ISP("AddSensor: TX_ISP_GET_BUF using index=input_index=%d", input_index);

    if (ioctl(gISPdev->fd, TX_ISP_GET_BUF, &buf_info) != 0) {
        LOG_ISP("AddSensor: TX_ISP_GET_BUF failed: %s", strerror(errno));
        return -1;
    }

    LOG_ISP("AddSensor: ISP buffer size=%u (platform struct size=%zu)",
            (unsigned)TXISP_BUF_GET_SIZE(buf_info), sizeof(buf_info));

    /* CRITICAL: Allocate DMA buffer for ISP RAW data */
    char isp_buf_info[0x94];
    memset(isp_buf_info, 0, sizeof(isp_buf_info));

    if (IMP_Alloc(isp_buf_info, buf_info.size, "isp_raw") != 0) {
        LOG_ISP("AddSensor: failed to allocate ISP buffer of size %u", buf_info.size);
        return -1;
    }

    typedef struct {
        char name[96];
        char tag[32];
        void *virt_addr;
        uint32_t phys_addr;
        uint32_t size;
        uint32_t flags;
        uint32_t pool_id;
    } DMABuffer;

    DMABuffer *dma_buf = (DMABuffer*)isp_buf_info;
    gISPdev->isp_buffer_virt = dma_buf->virt_addr;
    gISPdev->isp_buffer_phys = dma_buf->phys_addr;
    gISPdev->isp_buffer_size = dma_buf->size;

    LOG_ISP("AddSensor: allocated ISP buffer: virt=%p phys=0x%lx size=%u",
            gISPdev->isp_buffer_virt, gISPdev->isp_buffer_phys, gISPdev->isp_buffer_size);
    /* DO NOT memset the ISP buffer - the ISP firmware manages it entirely */

    /* CRITICAL: Set buffer address via TX_ISP_SET_BUF (platform-dependent) */
    tx_isp_buf_t set_buf;
    TXISP_BUF_INIT(set_buf);
    TXISP_BUF_SET_INDEX(set_buf, sensor_idx);
    TXISP_BUF_SET_PHYS_SIZE(set_buf, gISPdev->isp_buffer_phys, TXISP_BUF_GET_SIZE(buf_info));

    LOG_ISP("AddSensor: calling TX_ISP_SET_BUF (0x%08x) with phys=0x%lx size=%u",
            TX_ISP_SET_BUF, gISPdev->isp_buffer_phys, (unsigned)TXISP_BUF_GET_SIZE(buf_info));

    if (ioctl(gISPdev->fd, TX_ISP_SET_BUF, &set_buf) != 0) {
        LOG_ISP("AddSensor: TX_ISP_SET_BUF failed: %s", strerror(errno));
        IMP_Free(gISPdev->isp_buffer_phys);
        gISPdev->isp_buffer_virt = NULL;
        gISPdev->isp_buffer_phys = 0;
        gISPdev->isp_buffer_size = 0;
        return -1;
    }

    LOG_ISP("AddSensor: TX_ISP_SET_BUF succeeded");

    /* Optional: query and set a second RAW buffer if driver requests it (0x800856d7/56d6) */
    struct { uint32_t addr; uint32_t size; } buf2_info;
    memset(&buf2_info, 0, sizeof(buf2_info));
    if (ioctl(gISPdev->fd, 0x800856d7, &buf2_info) == 0 && buf2_info.size > 0) {
        LOG_ISP("AddSensor: secondary ISP buffer requested: size=%u", buf2_info.size);
        char isp_buf2_info[0x94];
        memset(isp_buf2_info, 0, sizeof(isp_buf2_info));
        if (IMP_Alloc(isp_buf2_info, buf2_info.size, "ISP RAW2") == 0) {
            DMABuffer *dma2 = (DMABuffer*)isp_buf2_info;
            gISPdev->isp_buffer2_virt = dma2->virt_addr;
            gISPdev->isp_buffer2_phys = dma2->phys_addr;
            gISPdev->isp_buffer2_size = dma2->size;

            struct { uint32_t addr; uint32_t size; } set_buf2;
            set_buf2.addr = gISPdev->isp_buffer2_phys;
            set_buf2.size = buf2_info.size;
            if (ioctl(gISPdev->fd, 0x800856d6, &set_buf2) != 0) {
                LOG_ISP("AddSensor: TX_ISP_SET_BUF(2) failed: %s", strerror(errno));
                IMP_Free(gISPdev->isp_buffer2_phys);
                gISPdev->isp_buffer2_virt = NULL;
                gISPdev->isp_buffer2_phys = 0;
                gISPdev->isp_buffer2_size = 0;
            } else {
                LOG_ISP("AddSensor: TX_ISP_SET_BUF(2) succeeded");
            }
        } else {
            LOG_ISP("AddSensor: IMP_Alloc for second buffer failed");
        }
    } else {
        LOG_ISP("AddSensor: secondary ISP buffer not requested or ioctl failed");
    }

    LOG_ISP("AddSensor: %s (idx=%d, buf_size=%u)", pinfo->name, sensor_idx, buf_info.size);
    return 0;
}

int IMP_ISP_AddSensor_VI(IMPVI vi, IMPSensorInfo *pinfo) {
    if (pinfo == NULL) return -1;
    LOG_ISP("AddSensor_VI: vi=%d, sensor=%s", vi, pinfo->name);
    return 0;
}

int IMP_ISP_DelSensor(IMPSensorInfo *pinfo) {
    if (pinfo == NULL) {
        LOG_ISP("DelSensor: NULL sensor info");
        return -1;
    }

    LOG_ISP("DelSensor: %s", pinfo->name);

    // Assuming additional logic is needed to actually delete the sensor
    // This could involve ioctl calls or other cleanup operations

    return 0;
}

int IMP_ISP_DelSensor_VI(IMPVI vi, IMPSensorInfo *pinfo) {
    if (pinfo == NULL) return -1;
    LOG_ISP("DelSensor_VI: vi=%d, sensor=%s", vi, pinfo->name);
    return 0;
}

/* IMP_ISP_EnableSensor - based on decompilation at 0x98450 */
int IMP_ISP_EnableSensor(void) {
    int ret;

    if (gISPdev == NULL) {
        LOG_ISP("EnableSensor: ISP not opened");
        return -1;
    }

    /* Skip AE/AWB ioctls - the streamer doesn't call SetAeAlgoFunc/SetAwbAlgoFunc,
     * so the stock libimp must handle AE/AWB differently (maybe in the driver or
     * automatically enabled). We need to figure out what's actually required.
     */
    LOG_ISP("EnableSensor: proceeding without custom AE/AWB");

    /* CRITICAL: Call ioctl 0x40045626 to get sensor index before streaming
     * This ioctl validates the sensor is ready and returns the sensor index.
     * It must be called before STREAMON.
     */
    int sensor_idx = -1;
    LOG_ISP("EnableSensor: about to call ioctl 0x40045626");
    ret = ioctl(gISPdev->fd, 0x40045626, &sensor_idx);
    LOG_ISP("EnableSensor: ioctl 0x40045626 returned %d", ret);
    if (ret != 0) {
        LOG_ISP("EnableSensor: ioctl 0x40045626 (GET_SENSOR_INDEX) failed: %s", strerror(errno));
        return -1;
    }
    LOG_ISP("EnableSensor: ioctl 0x40045626 succeeded, sensor_idx=%d", sensor_idx);

    if (sensor_idx == -1) {
        LOG_ISP("EnableSensor: sensor index is -1, sensor not ready");
        return -1;
    }

    LOG_ISP("EnableSensor: sensor index validated, proceeding to STREAMON/LINK_SETUP now (OEM parity)");

    /* Start ISP streaming and link setup immediately to match OEM */
    if (ISP_EnsureLinkStreamOn(sensor_idx) != 0) {
        LOG_ISP("EnableSensor: failed to start ISP stream + link setup");
        return -1;
    }

    gISPdev->opened += 2;
    sensor_enabled = 1;
    return 0;
}

/* Internal: ensure ISP global STREAMON and LINK_STREAM_ON are active (idempotent) */
int ISP_EnsureLinkStreamOn(int sensor_idx) {
    if (gISPdev == NULL) {
        LOG_ISP("EnsureLinkStreamOn: ISP not opened");
        return -1;
    }
    if (isp_stream_started) {
        LOG_ISP("EnsureLinkStreamOn: already started (bypass_link_setup_done=%d)", bypass_link_setup_done);
        return 0;
    }
    int ret;
    /* OEM passes 0 (NULL) for STREAMON, not &type */
    LOG_ISP("EnsureLinkStreamOn: calling ioctl 0x80045612 (ISP STREAMON) [arg=0]");
    ret = ioctl(gISPdev->fd, 0x80045612, 0);
    if (ret != 0) {
        LOG_ISP("EnsureLinkStreamOn: STREAMON failed: %s", strerror(errno));
        return -1;
    }

    /* Skip LINK_SETUP if SetISPBypass already did it (to preserve bypass configuration) */
    if (!bypass_link_setup_done) {
        /* Per OEM decompilation, LINK_SETUP expects a pointer to sensor_idx */
        LOG_ISP("EnsureLinkStreamOn: calling ioctl 0x800456d0 (LINK_SETUP) [arg=&sensor_idx=%d]", sensor_idx);
        int link_arg = sensor_idx;
        ret = ioctl(gISPdev->fd, 0x800456d0, &link_arg);
        if (ret != 0) {
            LOG_ISP("EnsureLinkStreamOn: LINK_SETUP failed: %s", strerror(errno));
            return -1;
        }
        LOG_ISP("EnsureLinkStreamOn: LINK_SETUP succeeded, sensor_idx=%d", link_arg);

        /* OEM passes 0 (NULL) for LINK_STREAM_ON, not &type */
        LOG_ISP("EnsureLinkStreamOn: calling ioctl 0x800456d2 (LINK_STREAM_ON) [arg=0]");
        ret = ioctl(gISPdev->fd, 0x800456d2, 0);
        if (ret != 0) {
            LOG_ISP("EnsureLinkStreamOn: LINK_STREAM_ON failed: %s", strerror(errno));
            return -1;
        }
    } else {
        LOG_ISP("EnsureLinkStreamOn: skipping LINK_SETUP/LINK_STREAM_ON (already done by SetISPBypass)");
    }

    isp_stream_started = 1;
    LOG_ISP("EnsureLinkStreamOn: ISP streaming started");
    return 0;
}

int IMP_ISP_EnableSensor_VI(IMPVI vi, IMPSensorInfo *pinfo) {
    if (pinfo == NULL) return -1;
    LOG_ISP("EnableSensor_VI: vi=%d, sensor=%s", vi, pinfo->name);
    sensor_enabled = 1;
    return 0;
}

int IMP_ISP_DisableSensor(void) {
    LOG_ISP("DisableSensor");

    // Assuming gISPdev is a global pointer to the ISPDevice structure
    if (gISPdev == NULL) {
        LOG_ISP("DisableSensor: ISP not opened");
        return -1;
    }

    // Assuming sensor_enabled is a global or static variable indicating sensor state
    sensor_enabled = 0;

    // Additional logic may be required to properly disable the sensor
    // This could involve ioctl calls or other cleanup operations

    return 0;
}

int IMP_ISP_DisableSensor_VI(IMPVI vi) {
    LOG_ISP("DisableSensor_VI: vi=%d", vi);
    sensor_enabled = 0;
    return 0;
}

int IMP_ISP_EnableTuning(void) {
    if (gISPdev == NULL) {
        LOG_ISP("EnableTuning: ISP not opened");
        return -1;
    }

    if (gISPdev->tisp_fd >= 0) {
        LOG_ISP("EnableTuning: already enabled");
        return 0;
    }

    /* Open /dev/isp-m0 for tuning interface */
    char tisp_dev[32];
    snprintf(tisp_dev, sizeof(tisp_dev), "/dev/isp-m0");

    gISPdev->tisp_fd = open(tisp_dev, O_RDWR | O_NONBLOCK);
    if (gISPdev->tisp_fd < 0) {
        LOG_ISP("EnableTuning: failed to open %s: %s", tisp_dev, strerror(errno));
        return -1;
    }

    LOG_ISP("EnableTuning: opened %s (fd=%d)", tisp_dev, gISPdev->tisp_fd);

    /* Allocate safe tuning state (vendor keeps internal at +0x9c) */
    if (gISPdev->tuning == NULL) {
        gISPdev->tuning = (struct ISPTuningState*)calloc(1, sizeof(ISPTuningState));
        if (!gISPdev->tuning) {
            LOG_ISP("EnableTuning: failed to allocate tuning state");
            close(gISPdev->tisp_fd);
            gISPdev->tisp_fd = -1;
            return -1;
        }
        /* Initialize contrast byte from current setting if available */
        unsigned char c = 0;
        if (IMP_ISP_Tuning_GetContrast(&c) == 0) {
            gISPdev->tuning->contrast_byte = c;
        }
    }

    /* Call ioctl 0xc00c56c6 to initialize tuning with default FPS */
    struct {
        uint32_t cmd;
        uint32_t subcmd;
        uint32_t value;  /* fps_num << 16 | fps_den */
    } tuning_init;

    tuning_init.cmd = 1;
    tuning_init.subcmd = 0x80000e0;
    tuning_init.value = (25 << 16) | 1;  /* Default 25/1 fps */

    if (ioctl(gISPdev->tisp_fd, 0xc00c56c6, &tuning_init) != 0) {
        LOG_ISP("EnableTuning: ioctl 0xc00c56c6 failed: %s", strerror(errno));
        close(gISPdev->tisp_fd);
        gISPdev->tisp_fd = -1;
        free(gISPdev->tuning);
        gISPdev->tuning = NULL;
        return -1;
    }

    LOG_ISP("EnableTuning: tuning initialized successfully");
    tuning_enabled = 1;

    /* Initialize/clear daemon registry on first enable (vendor behavior) */
    pthread_mutex_lock(&g_isp_deamon_mutex);
    ISPTuningState *st = gISPdev->tuning;
    if (st->daemon_init == 1) {
        LOG_ISP("EnableTuning: daemon already initialized");
    } else {
        memset(st->slots, 0, sizeof(st->slots));
        st->tuning_mask = 0;
        st->daemon_info = 0;
        st->daemon_thread = 0;
        st->daemon_init = 1;
    }
    pthread_mutex_unlock(&g_isp_deamon_mutex);

    /* Register vendor-named periodic functions */
    isp_tuning_deamon_func_add("update_total_gain", tuning_update_total_gain);
    isp_tuning_deamon_func_add("contrastjudge", tuning_contrastjudge);
    isp_tuning_deamon_func_add("videodrop", tuning_videodrop);

    /* Persist current mask for external override (thread accepts hex/dec) */
    {
        FILE* fp = fopen("/tmp/isp_tuning_func", "w+");
        if (fp) {
            char buf[32];
            int n = snprintf(buf, sizeof(buf), "%u\n", st->tuning_mask);
            if (n > 0) fwrite(buf, 1, (size_t)n, fp);
            fclose(fp);
        }
    }

    return 0;
}

int IMP_ISP_DisableTuning(void) {
    LOG_ISP("DisableTuning");
    tuning_enabled = 0;
    if (gISPdev && gISPdev->tuning) {
        free(gISPdev->tuning);
        gISPdev->tuning = NULL;
    }
    return 0;
}

/* ISP Tuning Functions */

int IMP_ISP_Tuning_SetSensorFPS(uint32_t fps_num, uint32_t fps_den) {
    if (gISPdev == NULL || gISPdev->tisp_fd < 0) {
        LOG_ISP("SetSensorFPS: tuning not enabled");
        return -1;
    }

    LOG_ISP("SetSensorFPS: %u/%u", fps_num, fps_den);

    /* Call ioctl 0xc00c56c6 with FPS parameters */
    struct {
        uint32_t cmd;
        uint32_t subcmd;
        uint32_t value;  /* fps_num << 16 | fps_den */
    } fps_cmd;

    fps_cmd.cmd = 0;
    fps_cmd.subcmd = 0x80000e0;
    fps_cmd.value = (fps_num << 16) | fps_den;

    if (ioctl(gISPdev->tisp_fd, 0xc00c56c6, &fps_cmd) != 0) {
        LOG_ISP("SetSensorFPS: ioctl 0xc00c56c6 failed: %s", strerror(errno));
        return -1;
    }

    LOG_ISP("SetSensorFPS: FPS set successfully to %u/%u", fps_num, fps_den);
    return 0;
}

int IMP_ISP_Tuning_GetSensorFPS(uint32_t *fps_num, uint32_t *fps_den) {
    LOG_ISP("GetSensorFPS: called with fps_num=%p fps_den=%p", fps_num, fps_den);
    if (fps_num == NULL || fps_den == NULL) {
        LOG_ISP("GetSensorFPS: NULL pointer passed");
        return -1;
    }
    *fps_num = 25;
    *fps_den = 1;
    LOG_ISP("GetSensorFPS: returning 25/1");
    return 0;
}

int IMP_ISP_Tuning_SetAntiFlickerAttr(IMPISPAntiflickerAttr attr) {
    LOG_ISP("SetAntiFlickerAttr: %d", attr);
    return 0;
}

int IMP_ISP_Tuning_SetISPRunningMode(IMPISPRunningMode mode) {
    if (gISPdev == NULL || gISPdev->tisp_fd < 0) {
        LOG_ISP("SetISPRunningMode: tuning not enabled");
        return -1;
    }

    LOG_ISP("SetISPRunningMode: %d", mode);

    /* Call ioctl 0xc00c56c6 with running mode
     * CRITICAL: Field order must be cmd, subcmd, value (same as other tuning ioctls)
     */
    struct {
        uint32_t cmd;
        uint32_t subcmd;
        uint32_t value;
    } mode_cmd;

    mode_cmd.cmd = 0;
    mode_cmd.subcmd = 0x80000e1;
    mode_cmd.value = mode;

    if (ioctl(gISPdev->tisp_fd, 0xc00c56c6, &mode_cmd) != 0) {
        LOG_ISP("SetISPRunningMode: ioctl 0xc00c56c6 failed: %s", strerror(errno));
        return -1;
    }

    LOG_ISP("SetISPRunningMode: mode set successfully to %d", mode);
    return 0;
}

int IMP_ISP_Tuning_GetISPRunningMode(IMPISPRunningMode *pmode) {
    if (pmode == NULL) return -1;
    *pmode = IMPISP_RUNNING_MODE_DAY;
    return 0;
}

int IMP_ISP_Tuning_SetISPBypass(IMPISPTuningOpsMode enable) {
    if (gISPdev == NULL) {
        LOG_ISP("SetISPBypass: ISP not opened");
        return -1;
    }
    LOG_ISP("SetISPBypass: %d", enable);

    /* Step 1: ioctl 0x800456d3 with NULL (OEM path) */
    if (ioctl(gISPdev->fd, 0x800456d3, 0) != 0) {
        LOG_ISP("SetISPBypass: ioctl 0x800456d3 failed: %s", strerror(errno));
        return -1;
    }

    /* Step 2: ioctl 0x800456d1 with &var (OEM path) */
    int var = -1;
    if (ioctl(gISPdev->fd, 0x800456d1, &var) != 0) {
        LOG_ISP("SetISPBypass: ioctl 0x800456d1 failed: %s", strerror(errno));
        return -1;
    }

    /* Step 3: tuning ioctl on /dev/isp-m0: 0xc008561c with {cmd=0x8000164, value=enable} */
    if (gISPdev->tisp_fd < 0) {
        LOG_ISP("SetISPBypass: tuning not enabled (tisp_fd<0)");
        return -1;
    }
    struct { int cmd; int value; } bypass_cmd;
    bypass_cmd.cmd = 0x8000164;
    bypass_cmd.value = (int)enable;
    if (ioctl(gISPdev->tisp_fd, 0xc008561c, &bypass_cmd) != 0) {
        LOG_ISP("SetISPBypass: tuning ioctl 0xc008561c failed: %s", strerror(errno));
        return -1;
    }

    /* Step 4: LINK_SETUP with arg = (enable==0)?1:0, then LINK_STREAM_ON with arg=0 */
    int link_arg = (enable == 0) ? 1 : 0;
    if (ioctl(gISPdev->fd, 0x800456d0, &link_arg) != 0) {
        LOG_ISP("SetISPBypass: LINK_SETUP failed: %s", strerror(errno));
        return -1;
    }
    /* OEM passes 0 (NULL) for LINK_STREAM_ON, not &type */
    if (ioctl(gISPdev->fd, 0x800456d2, 0) != 0) {
        LOG_ISP("SetISPBypass: LINK_STREAM_ON failed: %s", strerror(errno));
        return -1;
    }

    /* Mark that we've done LINK_SETUP so ISP_EnsureLinkStreamOn doesn't redo it */
    bypass_link_setup_done = 1;
    isp_stream_started = 1;  /* Also mark streaming as started */

    return 0;
}

int IMP_ISP_Tuning_SetISPHflip(IMPISPTuningOpsMode mode) {
    LOG_ISP("SetISPHflip: %d", mode);
    return 0;
}

int IMP_ISP_Tuning_SetISPVflip(IMPISPTuningOpsMode mode) {
    LOG_ISP("SetISPVflip: %d", mode);
    return 0;
}

int IMP_ISP_Tuning_SetBrightness(unsigned char bright) {
    LOG_ISP("SetBrightness: %u", bright);

    // Assuming gISPdev is a global pointer to the ISPDevice structure
    if (gISPdev == NULL) {
        LOG_ISP("SetBrightness: ISP not opened");
        return -1;
    }

    // Assuming there is a mechanism to set brightness in the ISP device
    // This could involve an ioctl call or direct register manipulation
    // Example (pseudo-code):
    // int ret = ioctl(gISPdev->fd, IOCTL_SET_BRIGHTNESS, &bright);
    // if (ret != 0) {
    //     LOG_ISP("SetBrightness: ioctl failed: %s", strerror(errno));
    //     return -1;
    // }

    return 0;
}

int IMP_ISP_Tuning_SetContrast(unsigned char contrast) {
    LOG_ISP("SetContrast: %u", contrast);

    if (gISPdev == NULL) {
        LOG_ISP("SetContrast: ISP not opened");
        return -1;
    }

    if (gISPdev->tuning == NULL) {
        LOG_ISP("SetContrast: Tuning structure not initialized");
        return -1;
    }

    gISPdev->tuning->contrast_byte = contrast;
    return 0;
}

int IMP_ISP_Tuning_SetSharpness(unsigned char sharpness) {
    LOG_ISP("SetSharpness: %u", sharpness);

    // Assuming gISPdev is a global pointer to the ISPDevice structure
    if (gISPdev == NULL) {
        LOG_ISP("SetSharpness: ISP not opened");
        return -1;
    }

    // Assuming there is a mechanism to set sharpness in the ISP device
    // This could involve an ioctl call or direct register manipulation
    // Example (pseudo-code):
    // int ret = ioctl(gISPdev->fd, IOCTL_SET_SHARPNESS, &sharpness);
    // if (ret != 0) {
    //     LOG_ISP("SetSharpness: ioctl failed: %s", strerror(errno));
    //     return -1;
    // }

    return 0;
}

int IMP_ISP_Tuning_SetSaturation(unsigned char sat) {
    LOG_ISP("SetSaturation: %u", sat);

    // Assuming gISPdev is a global pointer to the ISPDevice structure
    if (gISPdev == NULL) {
        LOG_ISP("SetSaturation: ISP not opened");
        return -1;
    }

    // Assuming there is a mechanism to set saturation in the ISP device
    // This could involve an ioctl call or direct register manipulation
    // Example (pseudo-code):
    // int ret = ioctl(gISPdev->fd, IOCTL_SET_SATURATION, &sat);
    // if (ret != 0) {
    //     LOG_ISP("SetSaturation: ioctl failed: %s", strerror(errno));
    //     return -1;
    // }

    return 0;
}

int IMP_ISP_Tuning_SetAeComp(int comp) {
    LOG_ISP("SetAeComp: %d", comp);
    return 0;
}

int IMP_ISP_Tuning_SetMaxAgain(uint32_t gain) {
    LOG_ISP("SetMaxAgain: %u", gain);
    return 0;
}

int IMP_ISP_Tuning_SetMaxDgain(uint32_t gain) {
    LOG_ISP("SetMaxDgain: %u", gain);
    return 0;
}

int IMP_ISP_Tuning_SetBacklightComp(uint32_t strength) {
    LOG_ISP("SetBacklightComp: %u", strength);
    return 0;
}

int IMP_ISP_Tuning_GetTotalGain(uint32_t *pgain) {
    if (pgain == NULL) return -1;
    if (gISPdev == NULL || gISPdev->tisp_fd < 0) return -1;

    struct {
        uint32_t cmd;     /* 1 = read */
        uint32_t subcmd;  /* 0x8000027 per vendor */
        uint32_t value;   /* out: total gain */
    } req;

    req.cmd = 1;
    req.subcmd = 0x8000027u;
    req.value = 0;

    if (ioctl(gISPdev->tisp_fd, 0xc00c56c6, &req) < 0) {
        LOG_ISP("GetTotalGain: ioctl failed: %s", strerror(errno));
        return -1;
    }

    *pgain = req.value;
    return 0;
}


int IMP_ISP_Tuning_SetDPC_Strength(uint32_t ratio) {
    LOG_ISP("SetDPC_Strength: %u", ratio);
    return 0;
}

int IMP_ISP_Tuning_SetDRC_Strength(uint32_t ratio) {
    LOG_ISP("SetDRC_Strength: %u", ratio);
    return 0;
}

int IMP_ISP_Tuning_SetHiLightDepress(uint32_t strength) {
    LOG_ISP("SetHiLightDepress: %u", strength);
    return 0;
}

int IMP_ISP_Tuning_SetTemperStrength(uint32_t ratio) {
    LOG_ISP("SetTemperStrength: %u", ratio);
    return 0;
}

int IMP_ISP_Tuning_SetSinterStrength(uint32_t ratio) {
    LOG_ISP("SetSinterStrength: %u", ratio);
    return 0;
}

int IMP_ISP_Tuning_SetBcshHue(unsigned char hue) {
    LOG_ISP("SetBcshHue: %u", hue);
    return 0;
}

int IMP_ISP_Tuning_SetDefog_Strength(uint32_t strength) {
    LOG_ISP("SetDefog_Strength: %u", strength);
    return 0;
}

int IMP_ISP_Tuning_SetWB(IMPISPWB *wb) {
    if (wb == NULL) return -1;
    LOG_ISP("SetWB: mode=%d, rgain=%u, bgain=%u", wb->mode, wb->rgain, wb->bgain);
    return 0;
}

int IMP_ISP_Tuning_GetWB(IMPISPWB *wb) {
    if (wb == NULL) return -1;
    wb->mode = IMPISP_WB_MODE_AUTO;
    wb->rgain = 256;
    wb->bgain = 256;
    return 0;
}

/* ISP Tuning Get Functions - based on Binary Ninja decompilations */

int IMP_ISP_Tuning_GetBrightness(unsigned char *pbright) {
    if (pbright == NULL) {
        LOG_ISP("GetBrightness: NULL pointer provided");
        return -1;
    }

    // Assuming gISPdev is a global pointer to the ISPDevice structure
    if (gISPdev == NULL) {
        LOG_ISP("GetBrightness: ISP not opened");
        return -1;
    }

    // Assuming there is a mechanism to get brightness from the ISP device
    // This could involve an ioctl call or direct register access
    // Example (pseudo-code):
    // int ret = ioctl(gISPdev->fd, IOCTL_GET_BRIGHTNESS, pbright);
    // if (ret != 0) {
    //     LOG_ISP("GetBrightness: ioctl failed: %s", strerror(errno));
    //     return -1;
    // }

    *pbright = 128;  /* Default middle value */
    LOG_ISP("GetBrightness: %u", *pbright);
    return 0;
}

int IMP_ISP_Tuning_GetContrast(unsigned char *pcontrast) {
    if (pcontrast == NULL) {
        LOG_ISP("GetContrast: NULL pointer provided");
        return -1;
    }

    // Assuming gISPdev is a global pointer to the ISPDevice structure
    if (gISPdev == NULL) {
        LOG_ISP("GetContrast: ISP not opened");
        return -1;
    }

    // Assuming there is a mechanism to get contrast from the ISP device
    // This could involve an ioctl call or direct register access
    // Example (pseudo-code):
    // int ret = ioctl(gISPdev->fd, IOCTL_GET_CONTRAST, pcontrast);
    // if (ret != 0) {
    //     LOG_ISP("GetContrast: ioctl failed: %s", strerror(errno));
    //     return -1;
    // }

    *pcontrast = 128;  /* Default middle value */
    LOG_ISP("GetContrast: %u", *pcontrast);
    return 0;
}

int IMP_ISP_Tuning_GetSharpness(unsigned char *psharpness) {
    if (psharpness == NULL) {
        LOG_ISP("GetSharpness: NULL pointer provided");
        return -1;
    }

    // Assuming gISPdev is a global pointer to the ISPDevice structure
    if (gISPdev == NULL) {
        LOG_ISP("GetSharpness: ISP not opened");
        return -1;
    }

    // Assuming there is a mechanism to get sharpness from the ISP device
    // This could involve an ioctl call or direct register access
    // Example (pseudo-code):
    // int ret = ioctl(gISPdev->fd, IOCTL_GET_SHARPNESS, psharpness);
    // if (ret != 0) {
    //     LOG_ISP("GetSharpness: ioctl failed: %s", strerror(errno));
    //     return -1;
    // }

    *psharpness = 128;  /* Default middle value */
    LOG_ISP("GetSharpness: %u", *psharpness);
    return 0;
}

int IMP_ISP_Tuning_GetSaturation(unsigned char *psat) {
    if (psat == NULL) {
        LOG_ISP("GetSaturation: NULL pointer provided");
        return -1;
    }

    // Assuming gISPdev is a global pointer to the ISPDevice structure
    if (gISPdev == NULL) {
        LOG_ISP("GetSaturation: ISP not opened");
        return -1;
    }

    // Assuming there is a mechanism to get saturation from the ISP device
    // This could involve an ioctl call or direct register access
    // Example (pseudo-code):
    // int ret = ioctl(gISPdev->fd, IOCTL_GET_SATURATION, psat);
    // if (ret != 0) {
    //     LOG_ISP("GetSaturation: ioctl failed: %s", strerror(errno));
    //     return -1;
    // }

    *psat = 128;  /* Default middle value */
    LOG_ISP("GetSaturation: %u", *psat);
    return 0;
}

int IMP_ISP_Tuning_SetVideoDropCallback(void (*cb)(void)) {
    if (gISPdev == NULL || gISPdev->tuning == NULL) return -1;
    gISPdev->tuning->video_drop_cb = cb;
    return 0;
}

int IMP_ISP_Tuning_GetAeComp(int *pcomp) {
    if (pcomp == NULL) return -1;
    *pcomp = 0;  /* Default no compensation */
    LOG_ISP("GetAeComp: %d", *pcomp);
    return 0;
}

int IMP_ISP_Tuning_GetBacklightComp(uint32_t *pstrength) {
    if (pstrength == NULL) return -1;
    *pstrength = 0;  /* Default off */
    LOG_ISP("GetBacklightComp: %u", *pstrength);
    return 0;
}

int IMP_ISP_Tuning_GetHiLightDepress(uint32_t *pstrength) {
    if (pstrength == NULL) return -1;
    *pstrength = 0;  /* Default off */
    LOG_ISP("GetHiLightDepress: %u", *pstrength);
    return 0;
}

int IMP_ISP_Tuning_GetBcshHue(unsigned char *phue) {
    if (phue == NULL) return -1;
    *phue = 128;  /* Default middle value */
    LOG_ISP("GetBcshHue: %u", *phue);
    return 0;
}

int IMP_ISP_Tuning_GetEVAttr(IMPISPEVAttr *attr) {
    if (attr == NULL) return -1;
    /* Return default EV attributes */
    memset(attr, 0, sizeof(IMPISPEVAttr));
    LOG_ISP("GetEVAttr");
    return 0;
}

int IMP_ISP_Tuning_GetWB_Statis(IMPISPWB *wb) {
    if (wb == NULL) return -1;
    /* Return default WB statistics */
    wb->mode = IMPISP_WB_MODE_AUTO;
    wb->rgain = 256;
    wb->bgain = 256;
    LOG_ISP("GetWB_Statis");
    return 0;
}

int IMP_ISP_Tuning_GetWB_GOL_Statis(IMPISPWB *wb) {
    if (wb == NULL) return -1;
    /* Return default WB GOL statistics */
    wb->mode = IMPISP_WB_MODE_AUTO;
    wb->rgain = 256;
    wb->bgain = 256;
    LOG_ISP("GetWB_GOL_Statis");
    return 0;
}

