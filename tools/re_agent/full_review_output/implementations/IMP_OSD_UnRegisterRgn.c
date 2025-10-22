int IMP_OSD_UnRegisterRgn(IMPRgnHandle handle, int grpNum) {
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

    /* Remove region from group's linked list */
    if (rgn->prev) {
        rgn->prev->next = rgn->next;
    }
    if (rgn->next) {
        rgn->next->prev = rgn->prev;
    }
    rgn->next = NULL;
    rgn->prev = NULL;

    /* Mark region as unregistered */
    rgn->registered = 0;

    /* Clear group region attributes */
    memset(rgn->attributes, 0, sizeof(rgn->attributes));

    sem_post(&gosd->sem);
    pthread_mutex_unlock(&osd_mutex);

    LOG_OSD("UnRegisterRgn: handle=%d, grp=%d", handle, grpNum);
    return 0;
}