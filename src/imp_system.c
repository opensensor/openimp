/**
 * IMP System Module Implementation
 * Based on reverse engineering of libimp.so v1.1.6
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <pthread.h>
#include <semaphore.h>
#include <imp/imp_system.h>
#include "dma_alloc.h"
#include "imp_log_int.h"

#define IMP_VERSION "1.1.6"

/* Forward declarations for subsystem init/exit (to match OEM sys_funcs table).
 * Functions not yet implemented in their respective modules get weak stubs below. */
extern int FrameSourceInit(void);
extern int EncoderInit(void);

/* Weak stubs for subsystem init/exit functions not yet implemented.
 * These will be overridden by real implementations when they are added
 * to their respective source files (imp_framesource.c, imp_osd.c, etc.). */
__attribute__((weak)) void FrameSourceExit(void) {}
__attribute__((weak)) int IVSInit(void) { return 0; }
__attribute__((weak)) void IVSExit(void) {}
__attribute__((weak)) int OSDInit(void) { return 0; }
__attribute__((weak)) void OSDExit(void) {}
__attribute__((weak)) int FBInit(void) { return 0; }
__attribute__((weak)) void FBExit(void) {}
__attribute__((weak)) int DsystemInit(void) { return 0; }
__attribute__((weak)) void DsystemExit(void) {}

/* EncoderExit in imp_encoder.c returns int; wrap it for the void exit table */
extern int EncoderExit(void);
static void EncoderExit_wrapper(void) { EncoderExit(); }

/* OEM modify_phyclk_strength - hardware PHY clock strength config
 * Reads/writes hardware registers to configure clock strength.
 * Stubbed here; real implementation uses read_reg_32/write_reg_32. */
static void modify_phyclk_strength(void) {
    /* TODO: implement hardware register access for PHY clock strength */
}

/* sys_funcs table: 6 entries of {name[8], init_func, exit_func}
 * Matches OEM sys_funcs which iterates 6 subsystems in system_init/system_exit */
typedef struct {
    char name[8];
    int (*init)(void);
    void (*exit)(void);
} SysFunc;

static SysFunc sys_funcs[6] = {
    { "FS",     FrameSourceInit, FrameSourceExit },
    { "Enc",    EncoderInit,     EncoderExit_wrapper },
    { "IVS",    IVSInit,         IVSExit },
    { "OSD",    OSDInit,         OSDExit },
    { "FB",     FBInit,          FBExit },
    { "DSys",   DsystemInit,     DsystemExit },
};


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
static uint64_t timestamp_base = 0;

static void sys2_trace(const char *fmt, ...)
{
    int fd = open("/dev/kmsg", O_WRONLY);
    if (fd < 0) return;

    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n > 0) write(fd, buf, (size_t)n);
    close(fd);
}
static pthread_mutex_t system_mutex = PTHREAD_MUTEX_INITIALIZER;

#define MAX_MEM_POOLS 16
typedef struct {
    int requested;
    size_t size;
    char name[32];
} SystemMemPool;

static SystemMemPool g_mem_pools[MAX_MEM_POOLS];

/* CPU ID detection - based on get_cpu_id()
 * OEM reads hardware registers: soc_id (0x1300002c), cppsr (0x10000034),
 * subsoctype (0x13540238), subremark (0x13540231) via get_cpuinfo().
 * Returns an integer 0-0x17 identifying the exact SoC variant. */
static int get_cpu_id(void) {
    /* In real implementation, this reads from hardware registers.
     * For now, return a constant based on platform define. */
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

/* Forward declarations */
static Module* get_module(int deviceID, int groupID);
Module* IMP_System_GetModule(int deviceID, int groupID);
int remove_observer_from_module(void *src_module, void *dst_module);
static int add_observer(Module *module, Observer *observer);
int system_get_bind_src(IMPCell *dstCell, IMPCell *srcCell);

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

/* BindObserverToSubject - binds two modules together
 * Based on decompilation at 0x1b388
 * OEM: unconditionally jumps to *(arg1 + 0x40), no NULL fallback */
static int BindObserverToSubject(Module *src, Module *dst, void *output_ptr) {
    if (src == NULL) {
        puts("module_src is NULL!");
        return -1;
    }
    if (dst == NULL) {
        puts("module_dst is NULL!");
        return -1;
    }

    /* OEM unconditionally calls bind_func at offset 0x40.
     * Guard against NULL here since not all our modules set bind_func yet. */
    int (*bind_fn)(Module*, Module*, void*) = (int (*)(Module*, Module*, void*))src->bind_func;
    if (bind_fn == NULL) {
        sys2_trace("libimp/BIND2: BindObserverToSubject src=%p(%s) dst=%p(%s) outptr=%p bind_fn=NULL\n",
                   src, src->name, dst, dst->name, output_ptr);
        IMP_LOG_ERR("System", "BindObserverToSubject: %s has no bind_func!\n", src->name);
        return -1;
    }
    sys2_trace("libimp/BIND2: BindObserverToSubject src=%p(%s) dst=%p(%s) outptr=%p bind_fn=%p\n",
               src, src->name, dst, dst->name, output_ptr, bind_fn);
    return bind_fn(src, dst, output_ptr);
}

/* UnBindObserverFromSubject - unbinds two modules
 * Based on decompilation at 0x1b3c8
 * OEM: unconditionally jumps to *(arg1 + 0x44), only passes src and dst */
static int UnBindObserverFromSubject(Module *src, Module *dst) {
    if (src == NULL) {
        puts("module_src is NULL!");
        return -1;
    }
    if (dst == NULL) {
        puts("module_dst is NULL!");
        return -1;
    }

    /* OEM unconditionally calls unbind_func at offset 0x44.
     * Guard against NULL since not all our modules set unbind_func yet. */
    int (*unbind_fn)(Module*, Module*) = (int (*)(Module*, Module*))src->unbind_func;
    if (unbind_fn == NULL) {
        IMP_LOG_ERR("System", "UnBindObserverFromSubject: %s has no unbind_func!\n", src->name);
        return -1;
    }
    return unbind_fn(src, dst);
}

/* system_bind - internal bind implementation
 * Based on decompilation at 0x1bfe0 */
static int system_bind(IMPCell *srcCell, IMPCell *dstCell) {
    Module *src_module = get_module(srcCell->deviceID, srcCell->groupID);
    Module *dst_module = get_module(dstCell->deviceID, dstCell->groupID);

    if (src_module == NULL) {
        IMP_LOG_ERR("System", "%s() error: invalid src channel(%d.%d.%d)\n",
                "system_bind", srcCell->deviceID, srcCell->groupID, srcCell->outputID);
        return -1;
    }

    if (dst_module == NULL) {
        IMP_LOG_ERR("System", "%s() error: invalid dst channel(%d.%d.%d)\n",
                "system_bind", dstCell->deviceID, dstCell->groupID, dstCell->outputID);
        return -1;
    }

    IMP_LOG_DBG("System", "%s(): bind DST-%s(%d.%d.%d) to SRC-%s(%d.%d.%d)\n",
            "system_bind", dst_module->name,
            dstCell->deviceID, dstCell->groupID, dstCell->outputID,
            src_module->name,
            srcCell->deviceID, srcCell->groupID, srcCell->outputID);

    /* Check output ID bounds - from decompilation:
     * if (outputID >= *(src_module + 0x134)) */
    int output_id = srcCell->outputID;
    if (output_id >= (int)src_module->output_count) {
        IMP_LOG_ERR("System", "%s() error: invalid SRC:%s()\n",
                "system_bind", src_module->name);
        return -1;
    }

    /* Calculate output pointer - from decompilation:
     * src_module + 0x128 + ((output_id + 4) << 2) */
    void *output_ptr = (void*)((char*)src_module + 0x128 + ((output_id + 4) << 2));

    sys2_trace("libimp/BIND2: system_bind src=%p(%s) %d.%d.%d dst=%p(%s) %d.%d.%d outcnt=%u outptr=%p\n",
               src_module, src_module->name,
               srcCell->deviceID, srcCell->groupID, srcCell->outputID,
               dst_module, dst_module->name,
               dstCell->deviceID, dstCell->groupID, dstCell->outputID,
               src_module->output_count, output_ptr);

    /* OEM calls BindObserverToSubject and always returns 0 */
    BindObserverToSubject(src_module, dst_module, output_ptr);
    return 0;
}

/* system_unbind - internal unbind implementation
 * Based on decompilation at 0x1c278 */
static int system_unbind(IMPCell *srcCell, IMPCell *dstCell) {
    Module *src_module = get_module(srcCell->deviceID, srcCell->groupID);
    Module *dst_module = get_module(dstCell->deviceID, dstCell->groupID);

    if (src_module == NULL) {
        IMP_LOG_ERR("System", "%s() error: invalid src channel(%d.%d.%d)\n",
                "system_unbind", srcCell->deviceID, srcCell->groupID, srcCell->outputID);
        return -1;
    }

    if (dst_module == NULL) {
        IMP_LOG_ERR("System", "%s() error: invalid dst channel(%d.%d.%d)\n",
                "system_unbind", dstCell->deviceID, dstCell->groupID, dstCell->outputID);
        return -1;
    }

    IMP_LOG_DBG("System", "%s(): unbind DST-%s(%d.%d.%d) from SRC-%s(%d.%d.%d)\n",
            "system_unbind", dst_module->name,
            dstCell->deviceID, dstCell->groupID, dstCell->outputID,
            src_module->name,
            srcCell->deviceID, srcCell->groupID, srcCell->outputID);

    /* OEM calls UnBindObserverFromSubject with just src and dst, returns 0 */
    UnBindObserverFromSubject(src_module, dst_module);
    return 0;
}

/* system_gettime - internal time implementation
 * Based on decompilation at 0x1bcc4
 * arg: 0 = CLOCK_MONOTONIC(4), 1 = CLOCK_REALTIME(2), 2 = CLOCK_MONOTONIC_RAW(3) */
static uint64_t system_gettime(int clock_type) {
    clockid_t clk;
    switch (clock_type) {
        case 0: clk = CLOCK_MONOTONIC; break;
        case 1: clk = CLOCK_REALTIME;  break;
        case 2: clk = CLOCK_MONOTONIC_RAW; break;
        default: return (uint64_t)-1;
    }

    struct timespec ts;
    if (clock_gettime(clk, &ts) < 0) {
        return 0;
    }

    uint64_t current = (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
    return current - timestamp_base;
}

/* system_rebasetime - internal rebase implementation
 * Based on decompilation at 0x1bf1c
 * OEM: gets current monotonic time and sets timestamp_base = current_time - arg */
static int system_rebasetime(uint64_t basets) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
        return -1;
    }

    uint64_t current = (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
    timestamp_base = current - basets;
    return 0;
}

/* system_init - internal init implementation
 * Based on decompilation at 0x1d2ac
 * Iterates sys_funcs table (6 entries), calls each init.
 * On failure, rolls back previously-initialized subsystems. */
static int system_init(void) {
    IMP_LOG_DBG("System", "%s()\n", "system_init");

    /* Initialize timestamp base */
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
        IMP_LOG_ERR("System", "Time init error\n");
        return -1;
    }

    timestamp_base = (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;

    /* Get CPU ID */
    get_cpu_id();

    /* Iterate sys_funcs table - 6 subsystems */
    int result = 0;
    for (int i = 0; i < 6; i++) {
        IMP_LOG_DBG("System", "Calling %s\n", sys_funcs[i].name);
        result = sys_funcs[i].init();

        if (result < 0) {
            IMP_LOG_ERR("System", "%s failed\n", sys_funcs[i].name);

            /* Rollback: call exit on previously initialized subsystems */
            for (int j = i - 1; j >= 0; j--) {
                sys_funcs[j].exit();
            }
            return result;
        }
    }

    return result;
}

/* system_exit - internal exit implementation
 * Based on decompilation at 0x1d544
 * Iterates sys_funcs table calling each exit function */
static int system_exit(void) {
    IMP_LOG_DBG("System", "%s\n", "system_exit");

    get_cpu_id();

    for (int i = 0; i < 6; i++) {
        IMP_LOG_DBG("System", "Calling %s\n", sys_funcs[i].name);
        sys_funcs[i].exit();
    }

    return 0;
}

/* Public API */

int IMP_System_Init(void) {
    IMP_LOG_DBG("System", "%s SDK Version:%s-%s built: %s %s\n",
            "IMP_System_Init", IMP_VERSION, "openimp", __DATE__, __TIME__);

    modify_phyclk_strength();
    return system_init();
}

int IMP_System_Exit(void) {
    IMP_LOG_DBG("System", "%s\n", "IMP_System_Exit");
    return system_exit();
}

/* IMP_System_Bind - based on decompilation at 0x1ddbc */
int IMP_System_Bind(IMPCell *srcCell, IMPCell *dstCell) {
    if (srcCell == NULL) {
        IMP_LOG_ERR("System", "%s(): src channel is NULL\n", "IMP_System_Bind");
        return -1;
    }

    if (dstCell == NULL) {
        IMP_LOG_ERR("System", "%s(): dst channel is NULL\n", "IMP_System_Bind");
        return -1;
    }

    return system_bind(srcCell, dstCell);
}

/* IMP_System_UnBind - based on decompilation at 0x1de8c */
int IMP_System_UnBind(IMPCell *srcCell, IMPCell *dstCell) {
    if (srcCell == NULL) {
        IMP_LOG_ERR("System", "%s(): src channel is NULL\n", "IMP_System_UnBind");
        return -1;
    }

    if (dstCell == NULL) {
        IMP_LOG_ERR("System", "%s(): dst channel is NULL\n", "IMP_System_UnBind");
        return -1;
    }

    return system_unbind(srcCell, dstCell);
}

/* IMP_System_GetVersion - based on decompilation at 0x1dd70
 * OEM uses sprintf, not snprintf */
int IMP_System_GetVersion(IMPVersion *pstVersion) {
    if (pstVersion == NULL) {
        return -1;
    }

    sprintf(pstVersion->aVersion, "IMP-%s", IMP_VERSION);
    return 0;
}

/* IMP_System_GetCPUInfo - based on decompilation at 0x1e02c
 * Returns static string pointer based on CPU ID */
const char *IMP_System_GetCPUInfo(void) {
    int cpu_id = get_cpu_id();

    /* Switch statement from OEM decompilation */
    switch (cpu_id) {
        case 0:  return "T10";
        case 1:
        case 2:  return "T10-Lite";
        case 3:  return "T20";
        case 4:  return "T20-Lite";
        case 5:  return "T20-X";
        case 6:  return "T30-Lite";
        case 7:  return "T30-N";
        case 8:  return "T30-X";
        case 9:  return "T30-A";
        case 0xa: return "T30-Z";
        case 0xb: return "T21-L";
        case 0xc: return "T21-N";
        case 0xd: return "T21-X";
        case 0xe: return "T21-Z";
        case 0xf: return "T31-L";
        case 0x10: return "T31-N";
        case 0x11: return "T31-X";
        case 0x12: return "T31-A";
        case 0x13: return "T31-AL";
        case 0x14: return "T31-ZL";
        case 0x15: return "T31-ZC";
        case 0x16: return "T31-LC";
        case 0x17: return "T31-ZX";
        default: return "Unknown";
    }
}

uint64_t IMP_System_GetTimeStamp(void) {
    /* OEM: return system_gettime(0) -- CLOCK_MONOTONIC */
    return system_gettime(0);
}

int IMP_System_RebaseTimeStamp(uint64_t basets) {
    /* OEM: return system_rebasetime(basets) */
    return system_rebasetime(basets);
}

/* IMP_System_GetBindbyDest - based on decompilation at 0x1df5c */
int IMP_System_GetBindbyDest(IMPCell *dstCell, IMPCell *srcCell) {
    if (dstCell == NULL) {
        IMP_LOG_ERR("System", "%s(): dst channel is NULL\n", "IMP_System_GetBindbyDest");
        return -1;
    }

    if (srcCell == NULL) {
        IMP_LOG_ERR("System", "%s(): src channel is NULL\n", "IMP_System_GetBindbyDest");
        return -1;
    }

    return system_get_bind_src(dstCell, srcCell);
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
    sys2_trace("libimp/BIND2: add_observer src=%p(%s) obs=%p dst=%p outidx=%d frame=%p next=%p\n",
               module, module->name, observer, observer->module,
               observer->output_index, observer->frame, observer->next);

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

    extern int VBMUnLockFrame(void *frame);

    Observer *obs = (Observer*)module->observer_list;
    sys2_trace("libimp/BIND2: notify src=%p(%s) frame=%p first=%p\n",
               module, module->name, frame, obs);

    while (obs != NULL) {
        if (obs->module != NULL) {
            Module *dst_module = (Module*)obs->module;

            /* Store frame in observer */
            obs->frame = frame;
            sys2_trace("libimp/BIND2: notify iter obs=%p dst=%p(%s) update=%p frame=%p outidx=%d\n",
                       obs, dst_module, dst_module->name, dst_module->func_4c,
                       frame, obs->output_index);

            /* Call the update function at offset 0x4c */
            if (dst_module->func_4c != NULL) {
                /* Update function signature: int update(Module *module, void *frame) */
                int (*update_fn)(Module*, void*) = (int (*)(Module*, void*))dst_module->func_4c;

                update_fn(dst_module, frame);
            } else {
                sys2_trace("libimp/BIND2: notify skip dst=%p(%s) update=NULL frame=%p\n",
                           dst_module, dst_module->name, frame);
                /* OEM does NOT unlock frames on update failure — the caller
                 * (FrameSource capture thread) owns the frame lifecycle.
                 * Unlocking here caused double-free / use-after-free. */
            }
        }

        obs = obs->next;
    }

    return 0;
}

/* system_get_bind_src - internal get-bind-source implementation
 * Based on decompilation at 0x1c474
 * OEM uses module->field_10 (subject pointer) to find source module */
int system_get_bind_src(IMPCell *dstCell, IMPCell *srcCell) {
    Module *dst_mod = get_module(dstCell->deviceID, dstCell->groupID);
    if (dst_mod == NULL) {
        IMP_LOG_ERR("System", "%s() error: invalid dst channel\n", "system_get_bind_src");
        return -1;
    }

    /* OEM checks field_10 (subject pointer at offset 0x10) */
    void *subject = (void*)(uintptr_t)dst_mod->field_10;
    if (subject == NULL) {
        IMP_LOG_ERR("System", "%s() error: dst channel(%s) has not been binded\n",
                "system_get_bind_src", dst_mod->name);
        return -1;
    }

    /* OEM searches the subject's observer list (at subject+0x3c count, subject+0x14 first entry)
     * to find which output index points to dst_mod */
    /* For now, fall back to linear search across all modules */
    for (int d = 0; d < MAX_DEVICES; d++) {
        for (int g = 0; g < MAX_GROUPS; g++) {
            Module *src = g_modules[d][g];
            if (!src) continue;
            Observer *obs = (Observer*)src->observer_list;
            int idx = 0;
            while (obs) {
                if (obs->module == (void*)dst_mod) {
                    srcCell->deviceID = d;
                    srcCell->groupID = g;
                    srcCell->outputID = idx;
                    return 0;
                }
                obs = obs->next;
                idx++;
            }
        }
    }
    return -1;
}

/* Export for use by other modules */
Module* IMP_System_GetModule(int deviceID, int groupID) {
    return get_module(deviceID, groupID);
}

Module* IMP_System_AllocModule(const char *name, int groupID) {
    Module *mod = AllocModule(name, 0);
    if (mod != NULL) {
        mod->group_id = groupID;
        /* CRITICAL: Also write group_id at the hardcoded OEM offset 0x130.
         * The C struct layout may not match the MIPS32 binary layout due to
         * different sizeof(sem_t)/sizeof(pthread_mutex_t) across toolchains.
         * encoder_update reads group_id from the hardcoded offset 0x130, so
         * we must ensure the value is written there regardless of struct layout. */
        *((uint32_t*)((char*)mod + 0x130)) = (uint32_t)groupID;
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

    IMP_LOG_DBG("System", "Registered module [%d,%d]: %s\n",
            deviceID, groupID, module ? module->name : "NULL");

    return 0;
}

/* Safe accessors to avoid raw offset usage across modules */
int IMP_System_ModuleSetOutputCount(void *module, unsigned int count) {
    if (!module) return -1;
    Module *m = (Module*)module;
    m->output_count = count;
    return 0;
}

int IMP_System_ModuleSetUpdateCallback(void *module, int (*update)(void *module, void *frame)) {
    if (!module) return -1;
    Module *m = (Module*)module;
    m->func_4c = (void*)update;
    return 0;
}

int IMP_System_ModuleGetGroupID(void *module) {
    if (!module) return -1;
    Module *m = (Module*)module;
    return (int)m->group_id;
}

/* Safe bind helpers to avoid duplicate observers */
int IMP_System_IsBound(IMPCell *srcCell, IMPCell *dstCell) {
    if (!srcCell || !dstCell) return 0;
    Module *src = IMP_System_GetModule(srcCell->deviceID, srcCell->groupID);
    Module *dst = IMP_System_GetModule(dstCell->deviceID, dstCell->groupID);
    if (!src || !dst) return 0;
    Observer *obs = (Observer*)src->observer_list;
    while (obs) {
        if (obs->module == (void*)dst) return 1;
        obs = obs->next;
    }
    return 0;
}

int IMP_System_BindIfNeeded(IMPCell *srcCell, IMPCell *dstCell) {
    if (!srcCell || !dstCell) return -1;
    if (IMP_System_IsBound(srcCell, dstCell)) return 0;
    return IMP_System_Bind(srcCell, dstCell);
}

/* IMP_System_MemPoolRequest - OEM just tail-calls IMP_MemPool_InitPool */
int IMP_System_MemPoolRequest(int poolId, size_t size, const char *name) {
    if (poolId < 0 || poolId >= MAX_MEM_POOLS || size == 0) {
        LOG_SYS("MemPoolRequest: invalid args pool=%d size=%zu", poolId, size);
        return -1;
    }

    pthread_mutex_lock(&system_mutex);
    g_mem_pools[poolId].requested = 1;
    g_mem_pools[poolId].size = size;
    memset(g_mem_pools[poolId].name, 0, sizeof(g_mem_pools[poolId].name));
    if (name != NULL) {
        strncpy(g_mem_pools[poolId].name, name, sizeof(g_mem_pools[poolId].name) - 1);
    }
    pthread_mutex_unlock(&system_mutex);

    LOG_SYS("MemPoolRequest: pool=%d size=%zu name=%s", poolId, size, name ? name : "(null)");
    return 0;
}

/* IMP_System_MemPoolFree - OEM calls IMP_MemPool_Release, then clears pool IDs */
int IMP_System_MemPoolFree(int poolId) {
    if (poolId < 0 || poolId >= MAX_MEM_POOLS) {
        LOG_SYS("MemPoolFree: invalid pool=%d", poolId);
        return -1;
    }

    pthread_mutex_lock(&system_mutex);
    memset(&g_mem_pools[poolId], 0, sizeof(g_mem_pools[poolId]));
    pthread_mutex_unlock(&system_mutex);

    LOG_SYS("MemPoolFree: pool=%d", poolId);
    return 0;
}

/* /dev/mem register access — OEM uses read_reg_32/write_reg_32 which mmap
 * a single page around the target physical address via /dev/mem. */
static uint32_t reg_read_write(uint32_t phys_addr, uint32_t *write_val) {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        IMP_LOG_ERR("System", "reg_rw: open /dev/mem failed: %s\n", strerror(errno));
        return 0;
    }

    uint32_t page_size = (uint32_t)sysconf(_SC_PAGESIZE);
    uint32_t page_base = phys_addr & ~(page_size - 1);
    uint32_t page_off = phys_addr & (page_size - 1);

    void *map = mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, page_base);
    if (map == MAP_FAILED) {
        IMP_LOG_ERR("System", "reg_rw: mmap failed for 0x%08x: %s\n", phys_addr, strerror(errno));
        close(fd);
        return 0;
    }

    volatile uint32_t *reg = (volatile uint32_t *)((uint8_t *)map + page_off);
    uint32_t result;

    if (write_val != NULL) {
        *reg = *write_val;
        result = *write_val;
    } else {
        result = *reg;
    }

    munmap(map, page_size);
    close(fd);
    return result;
}

/* IMP_System_ReadReg32 - read a 32-bit hardware register via /dev/mem */
uint32_t IMP_System_ReadReg32(uint32_t addr) {
    return reg_read_write(addr, NULL);
}

/* IMP_System_WriteReg32 - write a 32-bit hardware register via /dev/mem */
void IMP_System_WriteReg32(uint32_t addr, uint32_t val) {
    reg_read_write(addr, &val);
}

/* IMP_System_PhysToVirt - translate physical address to virtual
 * Route through DMA allocator registry / RMEM mapping. */
void *IMP_System_PhysToVirt(uint32_t phys_addr) {
    return DMA_PhysToVirt(phys_addr);
}

/* IMP_System_VirtToPhys - translate virtual address to physical
 * Route through DMA allocator registry / RMEM mapping. */
uint32_t IMP_System_VirtToPhys(void *virt_addr) {
    return DMA_VirtToPhys(virt_addr);
}
