int IMP_OSD_SetRgnAttr(IMPRgnHandle handle, IMPOSDRgnAttr *prAttr) {
    if (prAttr == NULL) {
        LOG_OSD("SetRgnAttr: NULL attribute pointer");
        return -1;
    }

    if (handle < 0 || handle >= MAX_OSD_REGIONS) {
        LOG_OSD("SetRgnAttr: invalid handle %d", handle);
        return -1;
    }

    pthread_mutex_lock(&osd_mutex);

    if (gosd == NULL) {
        pthread_mutex_unlock(&osd_mutex);
        return -1;
    }

    OSDRegion *rgn = &gosd->regions[handle];

    /* Update region attributes */
    rgn->attr = *prAttr;

    pthread_mutex_unlock(&osd_mutex);

    LOG_OSD("SetRgnAttr: handle=%d, pos=(%d,%d), size=(%dx%d), color=%d",
            handle, prAttr->pos_x, prAttr->pos_y, prAttr->width, prAttr->height, prAttr->color);
    return 0;
}