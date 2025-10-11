/**
 * IMP System Module Implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <imp/imp_system.h>

#define IMP_VERSION "1.1.6"

/* Global state */
static int system_initialized = 0;
static uint64_t timestamp_base = 0;
static pthread_mutex_t system_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Module registry for binding */
#define MAX_MODULES 32
typedef struct {
    int deviceID;
    int groupID;
    void *module_data;
    int in_use;
} ModuleEntry;

static ModuleEntry module_registry[MAX_MODULES];

/* Internal functions */
static uint64_t get_monotonic_time_us(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

static void* find_module(int deviceID, int groupID) {
    for (int i = 0; i < MAX_MODULES; i++) {
        if (module_registry[i].in_use &&
            module_registry[i].deviceID == deviceID &&
            module_registry[i].groupID == groupID) {
            return module_registry[i].module_data;
        }
    }
    return NULL;
}

static int register_module(int deviceID, int groupID, void *data) {
    pthread_mutex_lock(&system_mutex);
    for (int i = 0; i < MAX_MODULES; i++) {
        if (!module_registry[i].in_use) {
            module_registry[i].deviceID = deviceID;
            module_registry[i].groupID = groupID;
            module_registry[i].module_data = data;
            module_registry[i].in_use = 1;
            pthread_mutex_unlock(&system_mutex);
            return 0;
        }
    }
    pthread_mutex_unlock(&system_mutex);
    return -1;
}

static void unregister_module(int deviceID, int groupID) {
    pthread_mutex_lock(&system_mutex);
    for (int i = 0; i < MAX_MODULES; i++) {
        if (module_registry[i].in_use &&
            module_registry[i].deviceID == deviceID &&
            module_registry[i].groupID == groupID) {
            module_registry[i].in_use = 0;
            module_registry[i].module_data = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&system_mutex);
}

/* Public API */

int IMP_System_Init(void) {
    pthread_mutex_lock(&system_mutex);
    
    if (system_initialized) {
        pthread_mutex_unlock(&system_mutex);
        return 0;
    }
    
    /* Initialize module registry */
    memset(module_registry, 0, sizeof(module_registry));
    
    /* Initialize timestamp base */
    timestamp_base = get_monotonic_time_us();
    
    system_initialized = 1;
    pthread_mutex_unlock(&system_mutex);
    
    fprintf(stderr, "[IMP_System] Initialized (stub implementation)\n");
    return 0;
}

int IMP_System_Exit(void) {
    pthread_mutex_lock(&system_mutex);
    
    if (!system_initialized) {
        pthread_mutex_unlock(&system_mutex);
        return 0;
    }
    
    /* Clear module registry */
    memset(module_registry, 0, sizeof(module_registry));
    
    system_initialized = 0;
    pthread_mutex_unlock(&system_mutex);
    
    fprintf(stderr, "[IMP_System] Exited\n");
    return 0;
}

int IMP_System_Bind(IMPCell *srcCell, IMPCell *dstCell) {
    if (srcCell == NULL || dstCell == NULL) {
        fprintf(stderr, "[IMP_System] Bind failed: NULL cell\n");
        return -1;
    }
    
    fprintf(stderr, "[IMP_System] Bind: [%d,%d,%d] -> [%d,%d,%d]\n",
            srcCell->deviceID, srcCell->groupID, srcCell->outputID,
            dstCell->deviceID, dstCell->groupID, dstCell->outputID);
    
    /* In a real implementation, this would set up data flow between modules */
    /* For now, just verify the modules exist (or stub it) */
    
    return 0;
}

int IMP_System_UnBind(IMPCell *srcCell, IMPCell *dstCell) {
    if (srcCell == NULL || dstCell == NULL) {
        fprintf(stderr, "[IMP_System] UnBind failed: NULL cell\n");
        return -1;
    }
    
    fprintf(stderr, "[IMP_System] UnBind: [%d,%d,%d] -> [%d,%d,%d]\n",
            srcCell->deviceID, srcCell->groupID, srcCell->outputID,
            dstCell->deviceID, dstCell->groupID, dstCell->outputID);
    
    return 0;
}

int IMP_System_GetVersion(IMPVersion *pstVersion) {
    if (pstVersion == NULL) {
        return -1;
    }
    
    snprintf(pstVersion->aVersion, sizeof(pstVersion->aVersion), "IMP-%s", IMP_VERSION);
    return 0;
}

const char *IMP_System_GetCPUInfo(void) {
    /* Return CPU info based on platform define */
#if defined(PLATFORM_T10)
    return "T10";
#elif defined(PLATFORM_T20)
    return "T20";
#elif defined(PLATFORM_T21)
    return "T21";
#elif defined(PLATFORM_T23)
    return "T23";
#elif defined(PLATFORM_T30)
    return "T30";
#elif defined(PLATFORM_T31)
    return "T31";
#elif defined(PLATFORM_T40)
    return "T40";
#elif defined(PLATFORM_T41)
    return "T41";
#elif defined(PLATFORM_C100)
    return "C100";
#elif defined(PLATFORM_T15)
    return "T15";
#elif defined(PLATFORM_T20L)
    return "T20L";
#elif defined(PLATFORM_T20X)
    return "T20X";
#elif defined(PLATFORM_T21L)
    return "T21L";
#elif defined(PLATFORM_T21N)
    return "T21N";
#elif defined(PLATFORM_T21Z)
    return "T21Z";
#elif defined(PLATFORM_T30A)
    return "T30A";
#elif defined(PLATFORM_T30L)
    return "T30L";
#elif defined(PLATFORM_T30N)
    return "T30N";
#elif defined(PLATFORM_T30X)
    return "T30X";
#elif defined(PLATFORM_T31A)
    return "T31A";
#elif defined(PLATFORM_T31L)
    return "T31L";
#elif defined(PLATFORM_T31N)
    return "T31N";
#elif defined(PLATFORM_T31X)
    return "T31X";
#else
    return "Unknown";
#endif
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

