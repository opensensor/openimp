/*
 * IMP IVS Module Implementation
 * Minimal faithful implementation based on libimp.so 1.1.6 decompilation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

#include <imp/imp_common.h>
#include <imp/imp_system.h>
#include <imp/imp_ivs.h>
#include <imp/imp_ivs_move.h>

#define LOG_IVS(fmt, ...) fprintf(stderr, "[IMP_IVS] " fmt "\n", ##__VA_ARGS__)

/* Opaque interface layout (indexes match vendor usage) */
typedef struct IMPIVSInterface {
    void *param;                 /* [0] param buffer */
    uint32_t param_size;         /* [1] param size */
    void *reserved2;             /* [2] reserved/result ptr */
    int   (*init)(struct IMPIVSInterface*);            /* [3] init */
    void  (*exit)(struct IMPIVSInterface*);            /* [4] exit */
    int   (*process)(struct IMPIVSInterface*, void*);  /* [5] process frame */
    void *cb6;                   /* [6] optional */
    int   (*get_result)(struct IMPIVSInterface*, void**); /* [7] get result */
    int   (*release_result)(struct IMPIVSInterface*, void*); /* [8] release result */
    int   (*get_param)(struct IMPIVSInterface*);       /* [9] get param */
    void *cb10;                  /* [10] optional */
    void  (*flush)(struct IMPIVSInterface*);           /* [11] flush on stop */
} IMPIVSInterface;

/* Default move output mirrors vendor output struct */
/* Use header-defined IMP_IVS_MoveOutput */

#define MAX_IVS_GROUPS    2
#define MAX_IVS_CHANNELS  65

typedef struct {
    int chn_id;
    int grp_id;                  /* -1 if not registered */
    int running;                 /* 0/1 */
    pthread_t thread;            /* worker thread (exists for API compatibility) */
    sem_t sem_frame;             /* unused placeholder (matches vendor having sems) */
    sem_t sem_lock;              /* used as a basic lock semaphore (value 1) */
    sem_t sem_result;            /* posted when a result is available */
    IMPIVSInterface *iface;      /* handler */
} IVSChn;

static void *g_ivs_groups[MAX_IVS_GROUPS];    /* Module* per group */
static IVSChn g_ivs_chn[MAX_IVS_CHANNELS];

/* Worker thread does nothing significant; kept for API parity */
static void *ivs_worker(void *arg) {
    (void)arg;
    while (1) {
        /* Cancellable sleep */
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000000 }; /* 100ms */
        nanosleep(&ts, NULL);
        pthread_testcancel();
    }
    return NULL;
}

/* Update callback invoked by System when FS notifies observers */
static int ivs_update(void *module, void *frame) {
    if (!module || !frame) return 0;
    /* Read group id via safe accessor */
    extern int IMP_System_ModuleGetGroupID(void *module);
    int grp = IMP_System_ModuleGetGroupID(module);
    for (int i = 0; i < MAX_IVS_CHANNELS; i++) {
        IVSChn *c = &g_ivs_chn[i];
        if (c->running && c->grp_id == grp && c->iface) {
            if (c->iface->process) {
                c->iface->process(c->iface, frame);
            }
            /* Signal a result is available */
            sem_post(&c->sem_result);
        }
    }
    return 0;
}

int IMP_IVS_CreateGroup(int grpNum) {
    if (grpNum < 0 || grpNum >= MAX_IVS_GROUPS) {
        LOG_IVS("CreateGroup failed: invalid grp=%d", grpNum);
        return -1;
    }
    if (g_ivs_groups[grpNum]) {
        LOG_IVS("CreateGroup: group %d already exists", grpNum);
        return 0;
    }
    extern void* IMP_System_AllocModule(const char *name, int groupID);
    void *mod = IMP_System_AllocModule("IVS", grpNum);
    if (!mod) return -1;
    /* Set output_count and update callback via safe accessors */
    extern int IMP_System_ModuleSetOutputCount(void *module, unsigned int count);
    extern int IMP_System_ModuleSetUpdateCallback(void *module, int (*update)(void*, void*));
    IMP_System_ModuleSetOutputCount(mod, 1);
    IMP_System_ModuleSetUpdateCallback(mod, ivs_update);
    extern int IMP_System_RegisterModule(int deviceID, int groupID, void *module);
    IMP_System_RegisterModule(DEV_ID_IVS, grpNum, mod);
    g_ivs_groups[grpNum] = mod;
    LOG_IVS("CreateGroup: grp=%d", grpNum);
    return 0;
}

int IMP_IVS_DestroyGroup(int grpNum) {
    if (grpNum < 0 || grpNum >= MAX_IVS_GROUPS) {
        LOG_IVS("DestroyGroup: invalid group number %d", grpNum);
        return -1;
    }

    if (!g_ivs_groups[grpNum]) {
        LOG_IVS("DestroyGroup: group %d doesn't exist", grpNum);
        return -1;
    }

    /* Note: Module memory lifecycle is handled by System; just unregister locally */
    g_ivs_groups[grpNum] = NULL;
    LOG_IVS("DestroyGroup: grp=%d", grpNum);
    return 0;
}

int IMP_IVS_CreateChn(int chnNum, IMPIVSInterface *handler) {
    if (chnNum < 0 || chnNum >= MAX_IVS_CHANNELS) {
        LOG_IVS("CreateChn failed: invalid chn=%d", chnNum);
        return -1;
    }
    if (!handler) {
        LOG_IVS("CreateChn failed: handler=NULL");
        return -1;
    }
    /* Vendor requires either idx[5] or idx[6] set (process callbacks) */
    if (handler->process == NULL && handler->cb6 == NULL) {
        LOG_IVS("CreateChn failed: missing process callbacks (idx5/6)");
        return -1;
    }
    IVSChn *c = &g_ivs_chn[chnNum];
    if (c->iface) {
        LOG_IVS("CreateChn: chn=%d already exists", chnNum);
        return 0;
    }
    memset(c, 0, sizeof(*c));
    c->chn_id = chnNum;
    c->grp_id = -1;
    c->running = 0;
    c->iface = handler;

    /* Initialize semaphores to match vendor behavior: sem_lock=1, sem_result=0 */
    sem_init(&c->sem_frame, 0, 0);
    sem_init(&c->sem_lock, 0, 1);
    sem_init(&c->sem_result, 0, 0);

    /* Optional init callback */
    if (c->iface->init && c->iface->init(c->iface) < 0) {
        LOG_IVS("CreateChn: init callback failed");
        sem_destroy(&c->sem_result);
        sem_destroy(&c->sem_lock);
        sem_destroy(&c->sem_frame);
        c->iface = NULL;
        return -1;
    }

    if (pthread_create(&c->thread, NULL, ivs_worker, c) != 0) {
        LOG_IVS("CreateChn: thread create failed");
        if (c->iface->exit) c->iface->exit(c->iface);
        sem_destroy(&c->sem_result);
        sem_destroy(&c->sem_lock);
        sem_destroy(&c->sem_frame);
        c->iface = NULL;
        return -1;
    }

    LOG_IVS("CreateChn: chn=%d", chnNum);
    return 0;
}

int IMP_IVS_DestroyChn(int chnNum) {
    if (chnNum < 0 || chnNum >= MAX_IVS_CHANNELS) return -1;
    IVSChn *c = &g_ivs_chn[chnNum];
    if (!c->iface) {
        LOG_IVS("DestroyChn: chn=%d not created", chnNum);
        return 0;
    }
    if (c->thread) {
        pthread_cancel(c->thread);
        pthread_join(c->thread, NULL);
        c->thread = 0;
    }
    if (c->iface->exit) c->iface->exit(c->iface);
    sem_destroy(&c->sem_result);
    sem_destroy(&c->sem_lock);
    sem_destroy(&c->sem_frame);
    c->iface = NULL;
    c->running = 0;
    c->grp_id = -1;
    LOG_IVS("DestroyChn: chn=%d", chnNum);
    return 0;
}

int IMP_IVS_RegisterChn(int grpNum, int chnNum) {
    if (grpNum < 0 || grpNum >= MAX_IVS_GROUPS) return -1;
    if (chnNum < 0 || chnNum >= MAX_IVS_CHANNELS) return -1;
    /* Ensure group module exists (library-driven convenience) */
    if (!g_ivs_groups[grpNum]) {
        int rc = IMP_IVS_CreateGroup(grpNum);
        if (rc != 0) {
            LOG_IVS("RegisterChn: failed to create group %d", grpNum);
            return -1;
        }
    }
    IVSChn *c = &g_ivs_chn[chnNum];
    if (!c->iface) {
        LOG_IVS("RegisterChn: chn=%d not created", chnNum);
        return -1;
    }
    if (c->grp_id != -1 && c->grp_id != grpNum) {
        LOG_IVS("RegisterChn: chn=%d already registered to grp=%d", chnNum, c->grp_id);
        return -1;
    }
    c->grp_id = grpNum;

    /* Library-driven auto-bind: FS[group] -> IVS[group] (avoid duplicates) */
    IMPCell src = { .deviceID = DEV_ID_FS, .groupID = grpNum, .outputID = 0 };
    IMPCell dst = { .deviceID = DEV_ID_IVS, .groupID = grpNum, .outputID = 0 };
    extern int IMP_System_BindIfNeeded(IMPCell *srcCell, IMPCell *dstCell);
    int br = IMP_System_BindIfNeeded(&src, &dst);
    if (br != 0) {
        LOG_IVS("RegisterChn: auto-bind FS->IVS failed for grp=%d (rc=%d)", grpNum, br);
        return -1;
    }

    LOG_IVS("RegisterChn: grp=%d, chn=%d (auto-bound FS->IVS)", grpNum, chnNum);
    return 0;
}

int IMP_IVS_UnRegisterChn(int chnNum) {
    if (chnNum < 0 || chnNum >= MAX_IVS_CHANNELS) return -1;
    IVSChn *c = &g_ivs_chn[chnNum];
    if (!c->iface) {
        LOG_IVS("UnRegisterChn: chn=%d not created", chnNum);
        return -1;
    }
    c->grp_id = -1;
    LOG_IVS("UnRegisterChn: chn=%d", chnNum);
    return 0;
}

int IMP_IVS_StartRecvPic(int chnNum) {
    if (chnNum < 0 || chnNum >= MAX_IVS_CHANNELS) return -1;
    IVSChn *c = &g_ivs_chn[chnNum];
    if (!c->iface) return -1;
    if (c->running) {
        LOG_IVS("StartRecvPic: chn=%d already running", chnNum);
        return 0;
    }
    c->running = 1;
    LOG_IVS("StartRecvPic: chn=%d", chnNum);
    return 0;
}

int IMP_IVS_StopRecvPic(int chnNum) {
    if (chnNum < 0 || chnNum >= MAX_IVS_CHANNELS) return -1;
    IVSChn *c = &g_ivs_chn[chnNum];
    if (!c->iface) return -1;
    if (!c->running) {
        LOG_IVS("StopRecvPic: chn=%d already stopped", chnNum);
        return 0;
    }
    c->running = 0;
    if (c->iface->flush) c->iface->flush(c->iface);
    LOG_IVS("StopRecvPic: chn=%d", chnNum);
    return 0;
}

int IMP_IVS_PollingResult(int chnNum, int timeoutMs) {
    if (chnNum < 0 || chnNum >= MAX_IVS_CHANNELS) return -1;
    IVSChn *c = &g_ivs_chn[chnNum];
    if (!c->iface) return -1;
    if (timeoutMs == 0) {
        return sem_trywait(&c->sem_result);
    }
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    if (timeoutMs < 0) {
        /* Infinite wait */
        while (sem_wait(&c->sem_result) != 0) {}
        return 0;
    }
    ts.tv_sec += timeoutMs / 1000;
    ts.tv_nsec += (timeoutMs % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
    return sem_timedwait(&c->sem_result, &ts);
}

int IMP_IVS_GetResult(int chnNum, void **result) {
    if (chnNum < 0 || chnNum >= MAX_IVS_CHANNELS || !result) return -1;
    IVSChn *c = &g_ivs_chn[chnNum];
    if (!c->iface) return -1;
    if (c->iface->get_result) return c->iface->get_result(c->iface, result);
    return -1;
}

int IMP_IVS_ReleaseResult(int chnNum, void *result) {
    if (chnNum < 0 || chnNum >= MAX_IVS_CHANNELS) return -1;
    IVSChn *c = &g_ivs_chn[chnNum];
    if (!c->iface) return -1;
    if (c->iface->release_result) return c->iface->release_result(c->iface, result);
    return 0;
}

/* Default Move interface: minimal no-op algorithm with valid callbacks */
static int move_init(IMPIVSInterface *itf) { (void)itf; return 0; }
static void move_exit(IMPIVSInterface *itf) { (void)itf; }
static int move_process(IMPIVSInterface *itf, void *frame) {
    (void)frame;
    if (!itf) return -1;
    IMP_IVS_MoveOutput *out = (IMP_IVS_MoveOutput*)itf->reserved2;
    if (out) {
        /* No-op algorithm: mark no motion in all ROIs */
        for (int i = 0; i < IMP_IVS_MOVE_MAX_ROI_CNT; i++) out->retRoi[i] = 0;
    }
    return 0;
}
static int move_get_result(IMPIVSInterface *itf, void **out) {
    if (!itf || !out) return -1;
    *out = itf->reserved2;
    return 0;
}
static int move_release_result(IMPIVSInterface *itf, void *res) { (void)itf; (void)res; return 0; }
static int move_get_param(IMPIVSInterface *itf) { (void)itf; return 0; }
static void move_flush(IMPIVSInterface *itf) { (void)itf; }

IMPIVSInterface *IMP_IVS_CreateMoveInterface(IMP_IVS_MoveParam *param) {
    IMPIVSInterface *itf = (IMPIVSInterface*)calloc(1, sizeof(IMPIVSInterface));
    if (!itf) return NULL;
    /* Copy params into internal buffer */
    if (param) {
        itf->param_size = sizeof(*param);
        itf->param = malloc(itf->param_size);
        if (itf->param) memcpy(itf->param, param, itf->param_size);
    }
    itf->reserved2 = calloc(1, sizeof(IMP_IVS_MoveOutput));
    itf->init = move_init;
    itf->exit = move_exit;
    itf->process = move_process;
    itf->get_result = move_get_result;
    itf->release_result = move_release_result;
    itf->get_param = move_get_param;
    itf->flush = move_flush;
    LOG_IVS("CreateMoveInterface");
    return itf;
}

void IMP_IVS_DestroyMoveInterface(IMPIVSInterface *interface) {
    if (!interface) return;
    if (interface->exit) interface->exit(interface);
    free(interface->reserved2);
    free(interface->param);
    free(interface);
    LOG_IVS("DestroyMoveInterface");
}

