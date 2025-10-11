/**
 * IMP OSD Module Implementation
 * Based on reverse engineering of libimp.so v1.1.6
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <imp/imp_osd.h>

#define LOG_OSD(fmt, ...) fprintf(stderr, "[OSD] " fmt "\n", ##__VA_ARGS__)

/* OSD Region structure - 0x38 bytes */
#define MAX_OSD_REGIONS 512
#define OSD_REGION_SIZE 0x38

typedef struct OSDRegion {
    int handle;                 /* 0x00: Region handle */
    IMPOSDRgnAttr attr;         /* 0x04: Region attributes (0x20 bytes) */
    uint8_t data_24[0x4];       /* 0x24: Padding */
    void *data_ptr;             /* 0x28: Data pointer */
    struct OSDRegion *prev;     /* 0x2c: Previous in list */
    struct OSDRegion *next;     /* 0x30: Next in list */
    uint8_t allocated;          /* 0x34: Allocation flag */
    uint8_t registered;         /* 0x35: Registration flag */
    uint8_t data_36[0x2];       /* 0x36-0x37: Padding */
} OSDRegion;

/* OSD Group structure - 0x9014 bytes per group */
#define MAX_OSD_GROUPS 4
#define OSD_GROUP_SIZE 0x9014

typedef struct {
    int group_id;               /* 0x00: Group ID */
    uint8_t data_04[0x900c];    /* 0x04-0x900f: Group data */
    uint8_t data_9010[0x4];     /* 0x9010-0x9013: More data */
} OSDGroup;

/* Global OSD state */
typedef struct {
    void *module_ptr;           /* 0x00: Module pointer */
    uint8_t data_04[0x11b4];    /* 0x04-0x11b7: Header data */
    OSDRegion *free_list_head;  /* 0x11b8: Free region list head */
    OSDRegion *free_list_tail;  /* 0x11bc: Free region list tail */
    OSDRegion *used_list_head;  /* 0x11c0: Used region list head */
    OSDRegion *used_list_tail;  /* 0x11c4: Used region list tail */
    void *group_ptrs[4];        /* 0x11c8: Group pointers */
    void *mem_pool;             /* 0x11cc: Memory pool */
    uint8_t data_11d0[0x12e80]; /* Rest of data */
    OSDRegion regions[MAX_OSD_REGIONS]; /* 0x24050: Region array */
    sem_t sem;                  /* 0x2b060: Semaphore */
} OSDState;

/* Global variables */
static OSDState *gosd = NULL;
static pthread_mutex_t osd_mutex = PTHREAD_MUTEX_INITIALIZER;
static int osd_initialized = 0;
static int next_region_handle = 0;

/* Initialize OSD module */
static void osd_init(void) {
    if (osd_initialized) return;

    gosd = (OSDState*)calloc(1, sizeof(OSDState));
    if (gosd == NULL) {
        LOG_OSD("Failed to allocate OSD state");
        return;
    }

    /* Initialize semaphore */
    sem_init(&gosd->sem, 0, 1);

    /* Initialize free list with all regions */
    gosd->free_list_head = &gosd->regions[0];
    gosd->free_list_tail = &gosd->regions[MAX_OSD_REGIONS - 1];

    for (int i = 0; i < MAX_OSD_REGIONS; i++) {
        gosd->regions[i].handle = -1;
        gosd->regions[i].allocated = 0;
        gosd->regions[i].registered = 0;
        gosd->regions[i].data_ptr = NULL;

        if (i > 0) {
            gosd->regions[i].prev = &gosd->regions[i - 1];
        } else {
            gosd->regions[i].prev = NULL;
        }

        if (i < MAX_OSD_REGIONS - 1) {
            gosd->regions[i].next = &gosd->regions[i + 1];
        } else {
            gosd->regions[i].next = NULL;
        }
    }

    gosd->used_list_head = NULL;
    gosd->used_list_tail = NULL;

    osd_initialized = 1;
}

int IMP_OSD_SetPoolSize(int size) {
    LOG_OSD("SetPoolSize: %d bytes", size);
    return 0;
}

/* IMP_OSD_CreateGroup - based on decompilation at 0xc1cf8 */
int IMP_OSD_CreateGroup(int grpNum) {
    if (grpNum < 0 || grpNum >= MAX_OSD_GROUPS) {
        LOG_OSD("CreateGroup failed: invalid group %d", grpNum);
        return -1;
    }

    pthread_mutex_lock(&osd_mutex);

    osd_init();

    if (gosd == NULL) {
        pthread_mutex_unlock(&osd_mutex);
        return -1;
    }

    /* Check if group already exists */
    if (gosd->group_ptrs[grpNum] != NULL) {
        LOG_OSD("CreateGroup: group %d already exists", grpNum);
        pthread_mutex_unlock(&osd_mutex);
        return 0;
    }

    /* TODO: Call create_group(4, grpNum, name, 0xc1ae4) */
    /* For now, just mark as created */
    gosd->group_ptrs[grpNum] = (void*)1; /* Non-NULL to indicate created */

    pthread_mutex_unlock(&osd_mutex);

    LOG_OSD("CreateGroup: grp=%d", grpNum);
    return 0;
}

/* IMP_OSD_DestroyGroup - based on decompilation at 0xc1f2c */
int IMP_OSD_DestroyGroup(int grpNum) {
    if (grpNum < 0 || grpNum >= MAX_OSD_GROUPS) {
        LOG_OSD("DestroyGroup failed: invalid group %d", grpNum);
        return -1;
    }

    pthread_mutex_lock(&osd_mutex);

    if (gosd == NULL || gosd->group_ptrs[grpNum] == NULL) {
        LOG_OSD("DestroyGroup: group %d doesn't exist", grpNum);
        pthread_mutex_unlock(&osd_mutex);
        return -1;
    }

    /* TODO: Call destroy_group() */
    /* TODO: Check if any regions are still registered */

    gosd->group_ptrs[grpNum] = NULL;

    pthread_mutex_unlock(&osd_mutex);

    LOG_OSD("DestroyGroup: grp=%d", grpNum);
    return 0;
}

/* IMP_OSD_CreateRgn - based on decompilation at 0xc225c */
int IMP_OSD_CreateRgn(IMPRgnHandle handle, IMPOSDRgnAttr *prAttr) {
    if (prAttr == NULL) {
        LOG_OSD("CreateRgn failed: NULL attr");
        return -1;
    }

    pthread_mutex_lock(&osd_mutex);

    osd_init();

    if (gosd == NULL) {
        pthread_mutex_unlock(&osd_mutex);
        return -1;
    }

    if (handle < 0 || handle >= MAX_OSD_REGIONS) {
        LOG_OSD("CreateRgn failed: invalid handle %d", handle);
        pthread_mutex_unlock(&osd_mutex);
        return -1;
    }

    sem_wait(&gosd->sem);

    OSDRegion *rgn = &gosd->regions[handle];

    if (rgn->allocated) {
        LOG_OSD("CreateRgn: handle %d already allocated", handle);
        sem_post(&gosd->sem);
        pthread_mutex_unlock(&osd_mutex);
        return -1;
    }

    /* Mark as allocated and copy attributes */
    rgn->handle = handle;
    rgn->allocated = 1;
    rgn->registered = 0;
    memcpy(&rgn->attr, prAttr, sizeof(IMPOSDRgnAttr));

    /* TODO: Allocate data buffer based on type and size */

    sem_post(&gosd->sem);
    pthread_mutex_unlock(&osd_mutex);

    LOG_OSD("CreateRgn: handle=%d, type=%d", handle, prAttr->type);
    return 0;
}

/* IMP_OSD_DestroyRgn - based on decompilation at 0xc297c */
int IMP_OSD_DestroyRgn(IMPRgnHandle handle) {
    pthread_mutex_lock(&osd_mutex);

    if (gosd == NULL) {
        pthread_mutex_unlock(&osd_mutex);
        return -1;
    }

    if (handle < 0 || handle >= MAX_OSD_REGIONS) {
        LOG_OSD("DestroyRgn failed: invalid handle %d", handle);
        pthread_mutex_unlock(&osd_mutex);
        return -1;
    }

    sem_wait(&gosd->sem);

    OSDRegion *rgn = &gosd->regions[handle];

    if (!rgn->allocated) {
        LOG_OSD("DestroyRgn: handle %d not allocated", handle);
        sem_post(&gosd->sem);
        pthread_mutex_unlock(&osd_mutex);
        return -1;
    }

    /* Check if region is registered */
    if (rgn->registered) {
        LOG_OSD("DestroyRgn: handle %d still registered", handle);
        sem_post(&gosd->sem);
        pthread_mutex_unlock(&osd_mutex);
        return -1;
    }

    /* Free data if allocated */
    if (rgn->data_ptr != NULL) {
        free(rgn->data_ptr);
        rgn->data_ptr = NULL;
    }

    /* Clear region */
    rgn->handle = -1;
    rgn->allocated = 0;
    rgn->registered = 0;
    memset(&rgn->attr, 0, sizeof(IMPOSDRgnAttr));

    sem_post(&gosd->sem);
    pthread_mutex_unlock(&osd_mutex);

    LOG_OSD("DestroyRgn: handle=%d", handle);
    return 0;
}

int IMP_OSD_RegisterRgn(IMPRgnHandle handle, int grpNum, IMPOSDGrpRgnAttr *pgrAttr) {
    if (pgrAttr == NULL) return -1;
    LOG_OSD("RegisterRgn: handle=%d, grp=%d", handle, grpNum);
    return 0;
}

int IMP_OSD_UnRegisterRgn(IMPRgnHandle handle, int grpNum) {
    LOG_OSD("UnRegisterRgn: handle=%d, grp=%d", handle, grpNum);
    return 0;
}

int IMP_OSD_SetRgnAttr(IMPRgnHandle handle, IMPOSDRgnAttr *prAttr) {
    if (prAttr == NULL) return -1;
    LOG_OSD("SetRgnAttr: handle=%d", handle);
    return 0;
}

int IMP_OSD_GetRgnAttr(IMPRgnHandle handle, IMPOSDRgnAttr *prAttr) {
    if (prAttr == NULL) return -1;
    LOG_OSD("GetRgnAttr: handle=%d", handle);
    memset(prAttr, 0, sizeof(*prAttr));
    return 0;
}

int IMP_OSD_SetGrpRgnAttr(IMPRgnHandle handle, int grpNum, IMPOSDGrpRgnAttr *pgrAttr) {
    if (pgrAttr == NULL) return -1;
    LOG_OSD("SetGrpRgnAttr: handle=%d, grp=%d", handle, grpNum);
    return 0;
}

int IMP_OSD_GetGrpRgnAttr(IMPRgnHandle handle, int grpNum, IMPOSDGrpRgnAttr *pgrAttr) {
    if (pgrAttr == NULL) return -1;
    LOG_OSD("GetGrpRgnAttr: handle=%d, grp=%d", handle, grpNum);
    memset(pgrAttr, 0, sizeof(*pgrAttr));
    return 0;
}

int IMP_OSD_UpdateRgnAttrData(IMPRgnHandle handle, IMPOSDRgnAttrData *prAttrData) {
    if (prAttrData == NULL) return -1;
    LOG_OSD("UpdateRgnAttrData: handle=%d", handle);
    return 0;
}

int IMP_OSD_ShowRgn(IMPRgnHandle handle, int grpNum, int showFlag) {
    LOG_OSD("ShowRgn: handle=%d, grp=%d, show=%d", handle, grpNum, showFlag);
    return 0;
}

int IMP_OSD_Start(int grpNum) {
    LOG_OSD("Start: grp=%d", grpNum);
    return 0;
}

int IMP_OSD_Stop(int grpNum) {
    LOG_OSD("Stop: grp=%d", grpNum);
    return 0;
}

