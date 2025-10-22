int IMP_ISP_Open(void) {
    if (gISPdev != NULL) {
        LOG_ISP("Open: already opened");
        return 0;
    }

    /* Allocate ISP device structure */
    gISPdev = (ISPDevice*)calloc(1, sizeof(ISPDevice));
    if (gISPdev == NULL) {
        LOG_ISP("Open: failed to allocate ISP device");
        return -1;
    }

    /* Set device name */
    strcpy(gISPdev->dev_name, "/dev/tx-isp");

    /* Open ISP device */
    gISPdev->fd = open(gISPdev->dev_name, O_RDWR | O_NONBLOCK);
    if (gISPdev->fd < 0) {
        LOG_ISP("Open: failed to open %s: %s", gISPdev->dev_name, strerror(errno));
        free(gISPdev);
        gISPdev = NULL;
        return -1;
    }

    /* Do not open /dev/tisp here; driver creates it later when streaming starts.
     * We will lazily open it in IMP_ISP_EnableTuning if needed. */
    gISPdev->tisp_fd = -1;

    /* Mark as opened */
    gISPdev->opened = 1;

    LOG_ISP("Open: opened %s (fd=%d)", gISPdev->dev_name, gISPdev->fd);
    return 0;
}