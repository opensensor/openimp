int IMP_OSD_ShowRgn(IMPRgnHandle handle, int grpNum, int showFlag) {
    if (handle < 0 || handle >= MAX_OSD_REGIONS) {
        LOG_OSD("ShowRgn: invalid handle %d", handle);
        return -1;
    }

    if (grpNum < 0 || grpNum >= MAX_OSD_GROUPS) {
        LOG_OSD("ShowRgn: invalid group %d", grpNum);
        return -1;
    }

    pthread_mutex_lock(&osd_mutex);

    if (gosd == NULL) {
        pthread_mutex_unlock(&osd_mutex);
        return -1;
    }

    OSDRegion *rgn = &gosd->regions[handle];

    /* Update the show flag for the region */
    rgn->show_flag = showFlag;

    pthread_mutex_unlock(&osd_mutex);

    LOG_OSD("ShowRgn: handle=%d, grp=%d, show=%d", handle, grpNum, showFlag);
    return 0;
}