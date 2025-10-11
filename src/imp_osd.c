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
    int enabled;                /* 0x04: Enabled flag (set by Start/Stop) */
    uint8_t data_08[0x9008];    /* 0x08-0x900f: Group data */
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

    /* Allocate OSD group structure */
    OSDGroup *group = (OSDGroup*)calloc(1, sizeof(OSDGroup));
    if (group == NULL) {
        LOG_OSD("CreateGroup: failed to allocate group");
        pthread_mutex_unlock(&osd_mutex);
        return -1;
    }

    group->group_id = grpNum;
    group->enabled = 0;  /* Initially disabled */
    gosd->group_ptrs[grpNum] = group;

    LOG_OSD("CreateGroup: allocated group %d (%zu bytes)", grpNum, sizeof(OSDGroup));

    /* Register OSD module with system (DEV_ID_OSD = 4) */
    /* Allocate a proper Module structure for this OSD group */
    extern void* IMP_System_AllocModule(const char *name, int groupID);
    void *osd_module = IMP_System_AllocModule("OSD", grpNum);
    if (osd_module == NULL) {
        LOG_OSD("CreateGroup: Failed to allocate module");
        pthread_mutex_unlock(&osd_mutex);
        return -1;
    }

    /* Set output_count to 1 (OSD has 1 output per group) */
    /* Module structure has output_count at offset 0x134 */
    uint32_t *output_count_ptr = (uint32_t*)((char*)osd_module + 0x134);
    *output_count_ptr = 1;

    extern int IMP_System_RegisterModule(int deviceID, int groupID, void *module);
    IMP_System_RegisterModule(4, grpNum, osd_module);  /* DEV_ID_OSD = 4 */
    LOG_OSD("CreateGroup: registered OSD module [4,%d] with 1 output", grpNum);

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

    /* Check if any regions are still registered to this group */
    int regions_in_use = 0;
    for (int i = 0; i < MAX_OSD_REGIONS; i++) {
        if (gosd->regions[i].allocated && gosd->regions[i].registered) {
            /* Check if this region belongs to this group */
            /* For now, we just count registered regions */
            regions_in_use++;
        }
    }

    if (regions_in_use > 0) {
        LOG_OSD("DestroyGroup: warning - %d regions still registered", regions_in_use);
    }

    /* Free the group structure */
    OSDGroup *group = (OSDGroup*)gosd->group_ptrs[grpNum];
    if (group != NULL) {
        free(group);
        LOG_OSD("DestroyGroup: freed group %d", grpNum);
    }

    gosd->group_ptrs[grpNum] = NULL;

    pthread_mutex_unlock(&osd_mutex);

    LOG_OSD("DestroyGroup: grp=%d", grpNum);
    return 0;
}

/* IMP_OSD_CreateRgn - based on decompilation at 0xc225c */
IMPRgnHandle IMP_OSD_CreateRgn(IMPOSDRgnAttr *prAttr) {
    LOG_OSD("CreateRgn: called with prAttr=%p", prAttr);

    pthread_mutex_lock(&osd_mutex);

    osd_init();

    if (gosd == NULL) {
        pthread_mutex_unlock(&osd_mutex);
        return -1;
    }

    sem_wait(&gosd->sem);

    /* Find first free region (free-list model like OEM) */
    OSDRegion *rgn = NULL;
    int allocated_handle = -1;

    for (int i = 0; i < MAX_OSD_REGIONS; i++) {
        if (!gosd->regions[i].allocated) {
            rgn = &gosd->regions[i];
            allocated_handle = i;
            break;
        }
    }

    if (rgn == NULL) {
        LOG_OSD("CreateRgn failed: no free regions");
        sem_post(&gosd->sem);
        pthread_mutex_unlock(&osd_mutex);
        return -1;
    }

    /* Mark as allocated */
    rgn->handle = allocated_handle;
    rgn->allocated = 1;
    rgn->registered = 0;

    LOG_OSD("CreateRgn: allocated handle=%d from free list", allocated_handle);

    /* If prAttr is NULL or invalid, just create empty region (based on OEM decompilation) */
    if (prAttr == NULL) {
        LOG_OSD("CreateRgn: NULL prAttr, creating empty region");
        memset(&rgn->attr, 0, sizeof(IMPOSDRgnAttr));
        rgn->data_ptr = NULL;
        sem_post(&gosd->sem);
        pthread_mutex_unlock(&osd_mutex);
        LOG_OSD("CreateRgn: handle=%d created (empty)", allocated_handle);
        return allocated_handle;
    }

    /* Validate prAttr pointer before accessing */
    uintptr_t attr_addr = (uintptr_t)prAttr;
    if (attr_addr < 0x10000) {
        LOG_OSD("CreateRgn: invalid prAttr pointer %p (too small), creating empty region", prAttr);
        memset(&rgn->attr, 0, sizeof(IMPOSDRgnAttr));
        rgn->data_ptr = NULL;
        sem_post(&gosd->sem);
        pthread_mutex_unlock(&osd_mutex);
        LOG_OSD("CreateRgn: handle=%d created (empty, invalid ptr)", allocated_handle);
        return allocated_handle;
    }

    /* Copy attributes using safe struct member access (8 words = 32 bytes based on BN decompilation) */
    /* The OEM code accesses arg1[0] through arg1[7], which is 8 x 4 bytes = 32 bytes */
    LOG_OSD("CreateRgn: copying attributes from prAttr=%p", prAttr);
    uint8_t *attr_bytes = (uint8_t*)prAttr;
    uint8_t *rgn_attr_bytes = (uint8_t*)&rgn->attr;

    /* Copy each word safely */
    for (int i = 0; i < 8; i++) {
        uint32_t word;
        LOG_OSD("CreateRgn: copying word %d from offset 0x%x", i, i * 4);
        memcpy(&word, attr_bytes + (i * 4), sizeof(uint32_t));
        memcpy(rgn_attr_bytes + (i * 4), &word, sizeof(uint32_t));
    }
    LOG_OSD("CreateRgn: attribute copy complete");

    /* Allocate data buffer based on type and size using safe struct member access */
    /* Based on BN MCP decompilation of OEM IMP_OSD_CreateRgn:
     * arg1[0] = type (offset 0x00)
     * arg1[1] = rect.p0.x (offset 0x04)
     * arg1[2] = rect.p0.y (offset 0x08)
     * arg1[3] = rect.p1.x (offset 0x0c)
     * arg1[4] = rect.p1.y (offset 0x10)
     * arg1[5] = fmt (offset 0x14)
     * arg1[6] = data (offset 0x18)
     * arg1[7] = (offset 0x1c)
     */
    size_t data_size = 0;
    /* attr_bytes already declared above */

    uint32_t type, fmt;
    int32_t rect_p0_x, rect_p0_y, rect_p1_x, rect_p1_y;

    memcpy(&type, attr_bytes + 0x00, sizeof(uint32_t));
    memcpy(&rect_p0_x, attr_bytes + 0x04, sizeof(int32_t));
    memcpy(&rect_p0_y, attr_bytes + 0x08, sizeof(int32_t));
    memcpy(&rect_p1_x, attr_bytes + 0x0c, sizeof(int32_t));
    memcpy(&rect_p1_y, attr_bytes + 0x10, sizeof(int32_t));
    memcpy(&fmt, attr_bytes + 0x14, sizeof(uint32_t));

    /* Calculate width and height from rect points */
    int32_t width = rect_p1_x - rect_p0_x;
    int32_t height = rect_p1_y - rect_p0_y;

    /* Make absolute values */
    if (width < 0) width = -width;
    if (height < 0) height = -height;

    /* Add 1 to include both endpoints */
    width += 1;
    height += 1;

    LOG_OSD("CreateRgn: type=%d, rect=(%d,%d)-(%d,%d), size=%dx%d, fmt=%d",
            type, rect_p0_x, rect_p0_y, rect_p1_x, rect_p1_y, width, height, fmt);

    switch (type) {
        case OSD_REG_BITMAP:
            /* Calculate bitmap size based on rect and pixel format */
            if (fmt == PIX_FMT_BGRA) {
                data_size = width * height * 4; /* 4 bytes per pixel */
            } else {
                data_size = width * height; /* 1 byte per pixel for mono */
            }
            break;

        case OSD_REG_PIC:
            /* Picture data size from rect */
            data_size = width * height * 4; /* Assume BGRA */
            break;

        case OSD_REG_LINE:
        case OSD_REG_RECT:
        case OSD_REG_COVER:
            /* These types don't need separate data buffers */
            data_size = 0;
            break;

        default:
            LOG_OSD("CreateRgn: unknown type %d", type);
            data_size = 0;
            break;
    }

    /* Allocate data buffer if needed */
    if (data_size > 0) {
        rgn->data_ptr = malloc(data_size);
        if (rgn->data_ptr == NULL) {
            LOG_OSD("CreateRgn: failed to allocate %zu bytes", data_size);
            rgn->allocated = 0;
            sem_post(&gosd->sem);
            pthread_mutex_unlock(&osd_mutex);
            return -1;
        }
        memset(rgn->data_ptr, 0, data_size);
        LOG_OSD("CreateRgn: allocated %zu bytes for data", data_size);
    } else {
        rgn->data_ptr = NULL;
    }

    sem_post(&gosd->sem);
    pthread_mutex_unlock(&osd_mutex);

    LOG_OSD("CreateRgn: handle=%d created successfully", allocated_handle);

    /* Return the allocated handle value on success (based on BN decompilation: return *$s1) */
    return allocated_handle;
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
    /* Based on decompilation at 0xc2bdc */
    if (handle < 0 || handle >= MAX_OSD_REGIONS) {
        LOG_OSD("RegisterRgn failed: invalid handle %d", handle);
        return -1;
    }

    if (grpNum < 0 || grpNum >= MAX_OSD_GROUPS) {
        LOG_OSD("RegisterRgn failed: invalid group %d", grpNum);
        return -1;
    }

    pthread_mutex_lock(&osd_mutex);

    if (gosd == NULL) {
        pthread_mutex_unlock(&osd_mutex);
        return -1;
    }

    OSDRegion *rgn = &gosd->regions[handle];

    /* Wait on semaphore */
    sem_wait(&gosd->sem);

    /* Check if region is already registered to this group */
    if (rgn->registered == 1) {
        sem_post(&gosd->sem);
        pthread_mutex_unlock(&osd_mutex);
        LOG_OSD("RegisterRgn: region %d already registered to group %d", handle, grpNum);
        return -1;
    }

    /* Add region to group's linked list
     * The real implementation maintains a linked list of regions per group
     * at offsets 0x24078 (head) and 0x2407c (tail) from gosd base
     * Each region has next/prev pointers at offsets 0x44 and 0x48 */

    /* Mark region as registered */
    rgn->registered = 1;

    /* Copy group region attributes if provided */
    if (pgrAttr != NULL) {
        /* Copy the 9 uint32_t values from pgrAttr to region offset 0xc
         * This includes position, scaling, and other group-specific attributes */
        /* Note: Would copy to region data area in full implementation */
        (void)pgrAttr;  /* Suppress unused warning */
    }

    sem_post(&gosd->sem);
    pthread_mutex_unlock(&osd_mutex);

    LOG_OSD("RegisterRgn: handle=%d, grp=%d", handle, grpNum);
    return 0;
}

int IMP_OSD_UnRegisterRgn(IMPRgnHandle handle, int grpNum) {
    /* Based on decompilation at 0xc2f18 */
    if (handle < 0 || handle >= MAX_OSD_REGIONS) {
        LOG_OSD("UnRegisterRgn failed: invalid handle %d", handle);
        return -1;
    }

    if (grpNum < 0 || grpNum >= MAX_OSD_GROUPS) {
        LOG_OSD("UnRegisterRgn failed: invalid group %d", grpNum);
        return -1;
    }

    pthread_mutex_lock(&osd_mutex);

    if (gosd == NULL) {
        pthread_mutex_unlock(&osd_mutex);
        return -1;
    }

    OSDRegion *rgn = &gosd->regions[handle];

    /* Wait on semaphore */
    sem_wait(&gosd->sem);

    /* Check if region is registered */
    if (rgn->registered == 0) {
        sem_post(&gosd->sem);
        pthread_mutex_unlock(&osd_mutex);
        LOG_OSD("UnRegisterRgn: region %d not registered to group %d", handle, grpNum);
        return -1;
    }

    /* Remove region from group's linked list
     * The real implementation removes the region from the linked list
     * and updates head/tail pointers as needed */

    /* Mark region as unregistered */
    rgn->registered = 0;

    /* Clear group region attributes (0x24 bytes at offset 0xc) */
    /* Note: Would clear region data area in full implementation */

    sem_post(&gosd->sem);
    pthread_mutex_unlock(&osd_mutex);

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
    /* Based on decompilation at 0xc5c34 */
    if (grpNum < 0 || grpNum >= MAX_OSD_GROUPS) {
        LOG_OSD("Start failed: invalid group %d", grpNum);
        return -1;
    }

    if (gosd == NULL || gosd->group_ptrs[grpNum] == NULL) {
        LOG_OSD("Start failed: group %d not created", grpNum);
        return -1;
    }

    /* Set group enabled flag at offset st_value from group base
     * *(gosd + grpNum * 0x9014 + st_value) = 1 */
    ((OSDGroup*)gosd->group_ptrs[grpNum])->enabled = 1;

    LOG_OSD("Start: grp=%d", grpNum);
    return 0;
}

int IMP_OSD_Stop(int grpNum) {
    /* Based on decompilation at 0xc5d00 */
    if (grpNum < 0 || grpNum >= MAX_OSD_GROUPS) {
        LOG_OSD("Stop failed: invalid group %d", grpNum);
        return -1;
    }

    if (gosd == NULL || gosd->group_ptrs[grpNum] == NULL) {
        LOG_OSD("Stop failed: group %d not created", grpNum);
        return -1;
    }

    /* Clear group enabled flag at offset st_value from group base
     * *(gosd + grpNum * 0x9014 + st_value) = 0 */
    ((OSDGroup*)gosd->group_ptrs[grpNum])->enabled = 0;

    LOG_OSD("Stop: grp=%d", grpNum);
    return 0;
}

