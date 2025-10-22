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
    OSDGroup *group = gosd->group_ptrs[grpNum];
    if (group != NULL) {
        free(group);
        LOG_OSD("DestroyGroup: freed group %d", grpNum);
    }

    gosd->group_ptrs[grpNum] = NULL;

    pthread_mutex_unlock(&osd_mutex);

    LOG_OSD("DestroyGroup: grp=%d", grpNum);
    return 0;
}