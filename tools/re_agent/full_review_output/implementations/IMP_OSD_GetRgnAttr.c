int IMP_OSD_GetRgnAttr(IMPRgnHandle handle, IMPOSDRgnAttr *prAttr) {
    if (prAttr == NULL) {
        LOG_OSD("GetRgnAttr: NULL attribute pointer");
        return -1;
    }

    if (handle < 0 || handle >= MAX_OSD_REGIONS) {
        LOG_OSD("GetRgnAttr: invalid handle %d", handle);
        return -1;
    }

    pthread_mutex_lock(&osd_mutex);

    if (gosd == NULL) {
        pthread_mutex_unlock(&osd_mutex);
        return -1;
    }

    OSDRegion *rgn = &gosd->regions[handle];

    /* Copy region attributes to the provided structure */
    *prAttr = rgn->attr;

    pthread_mutex_unlock(&osd_mutex);

    LOG_OSD("GetRgnAttr: handle=%d, pos=(%d,%d), size=(%dx%d), color=%d",
            handle, prAttr->pos_x, prAttr->pos_y, prAttr->width, prAttr->height, prAttr->color);
    return 0;
}