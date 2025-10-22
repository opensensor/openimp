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
    extern void* IMP_System_AllocModule(const char *name, int groupID);
    void *osd_module = IMP_System_AllocModule("OSD", grpNum);
    if (osd_module == NULL) {
        LOG_OSD("CreateGroup: Failed to allocate module");
        pthread_mutex_unlock(&osd_mutex);
        return -1;
    }

    /* Set output_count to 1 (OSD has 1 output per group) */
    Module *module = (Module*)osd_module;
    module->output_count = 1;

    /* Set update callback */
    module->update_callback = (void*)osd_update;

    extern int IMP_System_RegisterModule(int deviceID, int groupID, void *module);
    IMP_System_RegisterModule(4, grpNum, osd_module);  /* DEV_ID_OSD = 4 */
    LOG_OSD("CreateGroup: registered OSD module [4,%d] with 1 output and update callback", grpNum);

    pthread_mutex_unlock(&osd_mutex);

    LOG_OSD("CreateGroup: grp=%d", grpNum);
    return 0;
}