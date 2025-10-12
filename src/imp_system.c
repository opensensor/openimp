/**
 * IMP System Module Implementation
 * Based on reverse engineering of libimp.so v1.1.6
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <imp/imp_system.h>

#define IMP_VERSION "1.1.6"

/* Forward declarations for subsystem init (to match OEM SystemInit) */
extern int FrameSourceInit(void);
extern int EncoderInit(void);


/* Observer structure for module binding */
typedef struct Observer {
    struct Observer *next;      /* 0x00: Next observer in list */
    void *module;               /* 0x04: Observer module pointer */
    void *frame;                /* 0x08: Frame pointer */
    int output_index;           /* 0x0c: Output index */
} Observer;

/* Module structure - based on AllocModule decompilation */
typedef struct Module {
    char name[16];              /* 0x00: Module name */
    uint32_t field_10;          /* 0x10 */
    uint32_t field_14[10];      /* 0x14-0x3b: Array of fields */
    uint32_t field_3c;          /* 0x3c */
    void *bind_func;            /* 0x40: Bind function pointer */
    void *unbind_func;          /* 0x44: Unbind function pointer */
    void *func_48;              /* 0x48 */
    void *func_4c;              /* 0x4c */
    uint32_t field_50;          /* 0x50 */
    void *observer_list;        /* 0x58: Observer list head */
    void *field_5c;             /* 0x5c */
    uint8_t data[0x80];         /* 0x60-0xdf: Module-specific data */
    uint32_t field_e0;          /* 0xe0 */
    sem_t sem_e4;               /* 0xe4: Semaphore */
    sem_t sem_f4;               /* 0xf4: Semaphore (initialized to 16) */
    pthread_t thread;           /* 0x104: Module thread */
    pthread_mutex_t mutex;      /* 0x108: Module mutex */
    void *field_12c;            /* 0x12c: Self pointer */
    uint32_t group_id;          /* 0x130: Group ID */
    uint32_t output_count;      /* 0x134: Number of outputs */
    uint32_t field_138[3];      /* 0x138-0x143 */
    void *module_ops;           /* 0x144: Module operations */
    /* Extended data follows at 0x148 */
} Module;

/* Global module registry - g_modules at 0x108ca0 */
/* Layout: modules[deviceID][groupID] where deviceID=0-5, groupID=0-5 */
#define MAX_DEVICES 6
#define MAX_GROUPS 6
static Module *g_modules[MAX_DEVICES][MAX_GROUPS];

/* Global state */
static int system_initialized = 0;
static uint64_t timestamp_base = 0;
static pthread_mutex_t system_mutex = PTHREAD_MUTEX_INITIALIZER;

/* CPU ID detection - based on get_cpu_id() */
static int get_cpu_id(void) {
    /* In real implementation, this reads from hardware register
     * For now, return T31 (ID 6) based on platform define */
#if defined(PLATFORM_T31)
    return 6;
#elif defined(PLATFORM_T40)
    return 7;
#elif defined(PLATFORM_T41)
    return 8;
#elif defined(PLATFORM_C100)
    return 9;
#elif defined(PLATFORM_T23)
    return 4;
#elif defined(PLATFORM_T21)
    return 3;
#else
    return 6; /* Default to T31 */
#endif
}

/* Internal functions - based on decompilations */
static uint64_t get_monotonic_time_us(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

/* Forward declarations */
static Module* get_module(int deviceID, int groupID);
Module* IMP_System_GetModule(int deviceID, int groupID);
int remove_observer_from_module(void *src_module, void *dst_module);
static int add_observer(Module *module, Observer *observer);

/* get_module - returns module pointer from g_modules array
 * Based on: return *(((arg1 * 6 + arg2) << 2) + &g_modules) */
static Module* get_module(int deviceID, int groupID) {
    if (deviceID < 0 || deviceID >= MAX_DEVICES ||
        groupID < 0 || groupID >= MAX_GROUPS) {
        return NULL;
    }
    return g_modules[deviceID][groupID];
}

/* AllocModule - allocates and initializes a module structure
 * Based on decompilation at 0x1b01c */
static Module* AllocModule(const char *name, size_t extra_size) {
    size_t total_size = sizeof(Module) + extra_size;
    Module *mod = (Module*)calloc(1, total_size);

    if (mod == NULL) {
        fprintf(stderr, "malloc module error\n");
        return NULL;
    }

    /* Copy name (max 16 chars) */
    size_t name_len = strlen(name);
    if (name_len >= 16) {
        fprintf(stderr, "Module name too long: %zu > 16\n", name_len);
        name_len = 15;
    }
    memcpy(mod->name, name, name_len);
    mod->name[name_len] = '\0';

    /* Initialize semaphores */
    sem_init(&mod->sem_e4, 0, 0);
    sem_init(&mod->sem_f4, 0, 16);

    /* Initialize mutex */
    if (pthread_mutex_init(&mod->mutex, NULL) != 0) {
        fprintf(stderr, "pthread_mutex_init() error\n");
        free(mod);
        return NULL;
    }

    /* Set self pointer */
    mod->field_12c = mod;

    /* Initialize observer list */
    mod->observer_list = NULL;

    return mod;
}

/* FreeModule - frees a module */
static void FreeModule(Module *mod) {
    if (mod == NULL) return;

    pthread_mutex_destroy(&mod->mutex);
    sem_destroy(&mod->sem_e4);
    sem_destroy(&mod->sem_f4);
    free(mod);
}

/* default_bind_func - Default bind implementation
 * Creates an observer and adds it to the source module's observer list */
static int default_bind_func(Module *src, Module *dst, void *output_ptr) {
    (void)output_ptr;  /* Unused in default implementation */

    /* Allocate observer */
    Observer *obs = (Observer*)calloc(1, sizeof(Observer));
    if (obs == NULL) {
        fprintf(stderr, "[System] Failed to allocate observer\n");
        return -1;
    }

    obs->module = dst;
    obs->frame = NULL;
    obs->output_index = 0;
    obs->next = NULL;

    /* Add to source module's observer list */
    if (add_observer(src, obs) < 0) {
        fprintf(stderr, "[System] Failed to add observer\n");
        free(obs);
        return -1;
    }

    fprintf(stderr, "[System] Bound %s -> %s (observer added)\n",
            src->name, dst->name);
    return 0;
}

/* BindObserverToSubject - binds two modules together
 * Based on decompilation at 0x1b388 */
static int BindObserverToSubject(Module *src, Module *dst, void *output_ptr) {
    if (src == NULL) {
        fprintf(stderr, "module_src is NULL!\n");
        return -1;
    }
    if (dst == NULL) {
        fprintf(stderr, "module_dst is NULL!\n");
        return -1;
    }

    /* Call the bind function pointer at offset 0x40 */
    if (src->bind_func != NULL) {
        int (*bind_fn)(Module*, Module*, void*) = (int (*)(Module*, Module*, void*))src->bind_func;
        return bind_fn(src, dst, output_ptr);
    }

    /* No bind function - use default */
    return default_bind_func(src, dst, output_ptr);
}

/* system_bind - internal bind implementation
 * Based on decompilation at 0x1bfe0 */
static int system_bind(IMPCell *srcCell, IMPCell *dstCell) {
    fprintf(stderr, "[System] Bind request: [%d,%d,%d] -> [%d,%d,%d]\n",
            srcCell->deviceID, srcCell->groupID, srcCell->outputID,
            dstCell->deviceID, dstCell->groupID, dstCell->outputID);

    Module *src_module = get_module(srcCell->deviceID, srcCell->groupID);
    Module *dst_module = get_module(dstCell->deviceID, dstCell->groupID);

    if (src_module == NULL) {
        fprintf(stderr, "[System] Bind failed: source module [%d,%d] not found\n",
                srcCell->deviceID, srcCell->groupID);
        return -1;
    }

    if (dst_module == NULL) {
        fprintf(stderr, "[System] Bind failed: destination module [%d,%d] not found\n",
                dstCell->deviceID, dstCell->groupID);
        return -1;
    }

    fprintf(stderr, "[System] Binding [%d,%d,%d] -> [%d,%d,%d]\n",
            srcCell->deviceID, srcCell->groupID, srcCell->outputID,
            dstCell->deviceID, dstCell->groupID, dstCell->outputID);

    /* Check output ID bounds - from decompilation:
     * if (outputID < *(src_module + 0x134)) */
    int output_id = srcCell->outputID;
    if (output_id >= (int)src_module->output_count) {
        fprintf(stderr, "[System] Bind failed: invalid output ID %d (max %u)\n",
                output_id, src_module->output_count);
        return -1;
    }

    /* Calculate output pointer - from decompilation:
     * src_module + 0x128 + ((output_id + 4) << 2) */
    void *output_ptr = (void*)((char*)src_module + 0x128 + ((output_id + 4) << 2));

    return BindObserverToSubject(src_module, dst_module, output_ptr);
}

/* Public API */

int IMP_System_Init(void) {
    pthread_mutex_lock(&system_mutex);

    if (system_initialized) {
        pthread_mutex_unlock(&system_mutex);
        return 0;
    }

    fprintf(stderr, "[System] Initializing...\n");

    /* Initialize module registry */
    memset(g_modules, 0, sizeof(g_modules));

    /* Initialize timestamp base - from system_init decompilation */
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
        fprintf(stderr, "[System] Failed to get time\n");
        pthread_mutex_unlock(&system_mutex);
        return -1;
    }

    timestamp_base = (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;

    /* Get CPU ID */
    get_cpu_id();

    /* Initialize subsystem modules eagerly to match OEM behavior */
    int ret_fs = FrameSourceInit();
    if (ret_fs < 0) {
        pthread_mutex_unlock(&system_mutex);
        fprintf(stderr, "[System] FrameSourceInit failed\n");
        return -1;
    }
    int ret_enc = EncoderInit();
    if (ret_enc < 0) {
        pthread_mutex_unlock(&system_mutex);
        fprintf(stderr, "[System] EncoderInit failed\n");
        return -1;
    }

    fprintf(stderr, "[System] Subsystems initialized\n");

    system_initialized = 1;
    pthread_mutex_unlock(&system_mutex);

    fprintf(stderr, "[System] Initialized (IMP-%s)\n", IMP_VERSION);
    return 0;
}

int IMP_System_Exit(void) {
    pthread_mutex_lock(&system_mutex);

    if (!system_initialized) {
        pthread_mutex_unlock(&system_mutex);
        return 0;
    }

    /* Clear module registry */
    memset(g_modules, 0, sizeof(g_modules));

    /* Cleanup subsystem modules
     * Each subsystem is responsible for its own cleanup
     * when their Destroy/Exit functions are called */

    fprintf(stderr, "[System] Subsystems cleaned up\n");

    system_initialized = 0;
    pthread_mutex_unlock(&system_mutex);

    fprintf(stderr, "[System] Exited\n");
    return 0;
}

/* IMP_System_Bind - based on decompilation at 0x1ddbc */
int IMP_System_Bind(IMPCell *srcCell, IMPCell *dstCell) {
    if (srcCell == NULL) {
        fprintf(stderr, "[System] Bind failed: srcCell is NULL\n");
        return -1;
    }

    if (dstCell == NULL) {
        fprintf(stderr, "[System] Bind failed: dstCell is NULL\n");
        return -1;
    }

    return system_bind(srcCell, dstCell);
}

/* IMP_System_UnBind - unbind two modules */
int IMP_System_UnBind(IMPCell *srcCell, IMPCell *dstCell) {
    if (srcCell == NULL || dstCell == NULL) {
        fprintf(stderr, "[System] UnBind failed: NULL cell\n");
        return -1;
    }

    fprintf(stderr, "[System] UnBind: [%d,%d,%d] -> [%d,%d,%d]\n",
            srcCell->deviceID, srcCell->groupID, srcCell->outputID,
            dstCell->deviceID, dstCell->groupID, dstCell->outputID);

    /* Get source and destination modules */
    Module *src_module = (Module*)IMP_System_GetModule(srcCell->deviceID, srcCell->groupID);
    Module *dst_module = (Module*)IMP_System_GetModule(dstCell->deviceID, dstCell->groupID);

    if (src_module == NULL || dst_module == NULL) {
        fprintf(stderr, "[System] UnBind: module not found\n");
        return -1;
    }

    /* Call unbind function if it exists */
    if (src_module->unbind_func != NULL) {
        int (*unbind_fn)(void*, void*, void*) = (int (*)(void*, void*, void*))src_module->unbind_func;

        /* Calculate output pointer */
        void *output_ptr = (void*)((char*)src_module + 0x128 + ((srcCell->outputID + 4) << 2));

        if (unbind_fn(src_module, dst_module, output_ptr) < 0) {
            fprintf(stderr, "[System] UnBind: unbind function failed\n");
            return -1;
        }
    } else {
        /* No unbind function - just remove observer */
        if (remove_observer_from_module(src_module, dst_module) < 0) {
            fprintf(stderr, "[System] UnBind: failed to remove observer\n");
            return -1;
        }
    }

    fprintf(stderr, "[System] UnBind: success\n");
    return 0;
}

int IMP_System_GetVersion(IMPVersion *pstVersion) {
    if (pstVersion == NULL) {
        return -1;
    }

    snprintf(pstVersion->aVersion, sizeof(pstVersion->aVersion), "IMP-%s", IMP_VERSION);
    return 0;
}

/* IMP_System_GetCPUInfo - based on decompilation at 0x1e02c
 * Returns static string pointer based on CPU ID */
const char *IMP_System_GetCPUInfo(void) {
    int cpu_id = get_cpu_id();

    /* Switch statement from decompilation */
    switch (cpu_id) {
        case 0:  return "T10";
        case 1:
        case 2:  return "T20";
        case 3:  return "T21";
        case 4:  return "T23";
        case 5:  return "T30";
        case 6:  return "T31";
        case 7:  return "T40";
        case 8:  return "T41";
        case 9:  return "C100";
        case 10: return "T15";
        case 11: return "T20L";
        case 12: return "T20X";
        case 13: return "T21L";
        case 14: return "T21N";
        case 15: return "T21Z";
        case 16: return "T30A";
        case 17: return "T30L";
        case 18: return "T30N";
        case 19: return "T30X";
        case 20: return "T31A";
        case 21: return "T31L";
        case 22: return "T31N";
        case 23: return "T31X";
        default: return "Unknown";
    }
}

uint64_t IMP_System_GetTimeStamp(void) {
    uint64_t current_time = get_monotonic_time_us();
    return current_time - timestamp_base;
}

int IMP_System_RebaseTimeStamp(uint64_t basets) {
    pthread_mutex_lock(&system_mutex);
    timestamp_base = basets;
    pthread_mutex_unlock(&system_mutex);

    fprintf(stderr, "[IMP_System] Timestamp rebased to %llu us\n",
            (unsigned long long)basets);
    return 0;
}

/* ========== Observer Pattern Implementation ========== */

/**
 * add_observer - Add observer to module's observer list
 * Based on decompilation at 0x1a920
 */
static int add_observer(Module *module, Observer *observer) {
    if (module == NULL || observer == NULL) {
        return -1;
    }

    /* Add to head of list */
    observer->next = (Observer*)module->observer_list;
    module->observer_list = observer;

    return 0;
}

/**
 * add_observer_to_module - Public wrapper for add_observer
 * Used by other modules to add observers
 */
int add_observer_to_module(void *module, Observer *observer) {
    return add_observer((Module*)module, observer);
}

/**
 * remove_observer - Remove observer from module's observer list
 * Based on decompilation at 0x1a890
 */
static int remove_observer(Module *module, Observer *observer) {
    if (module == NULL || observer == NULL) {
        return -1;
    }

    Observer **curr = (Observer**)&module->observer_list;
    while (*curr != NULL) {
        if (*curr == observer) {
            *curr = observer->next;
            return 0;
        }
        curr = &(*curr)->next;
    }

    return -1;
}

/**
 * remove_observer_from_module - Public wrapper for remove_observer
 * Used by other modules to remove observers
 * Finds observer by destination module and removes it
 */
int remove_observer_from_module(void *src_module, void *dst_module) {
    if (src_module == NULL || dst_module == NULL) {
        return -1;
    }

    Module *module = (Module*)src_module;
    Observer *obs = (Observer*)module->observer_list;
    Observer *prev = NULL;

    /* Find observer with matching destination module */
    while (obs != NULL) {
        if (obs->module == dst_module) {
            /* Found it - remove from list */
            if (prev == NULL) {
                /* First in list */
                module->observer_list = obs->next;
            } else {
                /* Middle or end of list */
                prev->next = obs->next;
            }

            /* Free observer */
            free(obs);
            return 0;
        }

        prev = obs;
        obs = obs->next;
    }

    /* Observer not found */
    return -1;
}

/**
 * notify_observers - Notify all observers with a frame
 * Based on decompilation at 0x1aa54
 */
int notify_observers(Module *module, void *frame) {
    if (module == NULL) {
        return -1;
    }

    Observer *obs = (Observer*)module->observer_list;

    while (obs != NULL) {
        if (obs->module != NULL) {
            Module *dst_module = (Module*)obs->module;

            /* Store frame in observer */
            obs->frame = frame;

            /* Call the update function at offset 0x4c */
            if (dst_module->func_4c != NULL) {
                /* Update function signature: int update(Module *module, void *frame) */
                int (*update_fn)(Module*, void*) = (int (*)(Module*, void*))dst_module->func_4c;

                /* Call update function with module and frame */
                if (update_fn(dst_module, frame) < 0) {
                    fprintf(stderr, "[System] Observer update failed for %s\n",
                            dst_module->name);
                    /* Frame will be released by the observer when done processing */
                }
            }
        }

        obs = obs->next;
    }

    return 0;
}

/* Export for use by other modules */
Module* IMP_System_GetModule(int deviceID, int groupID) {
    return get_module(deviceID, groupID);
}

Module* IMP_System_AllocModule(const char *name, int groupID) {
    Module *mod = AllocModule(name, 0);
    if (mod != NULL) {
        mod->group_id = groupID;
    }
    return mod;
}

int IMP_System_RegisterModule(int deviceID, int groupID, Module *module) {
    if (deviceID < 0 || deviceID >= MAX_DEVICES ||
        groupID < 0 || groupID >= MAX_GROUPS) {
        return -1;
    }

    pthread_mutex_lock(&system_mutex);
    g_modules[deviceID][groupID] = module;

    /* Set output_count to 1 if not already set (offset 0x134) */
    if (module && module->output_count == 0) {
        module->output_count = 1;
    }

    pthread_mutex_unlock(&system_mutex);

    fprintf(stderr, "[System] Registered module [%d,%d]: %s\n",
            deviceID, groupID, module ? module->name : "NULL");

    return 0;
}

