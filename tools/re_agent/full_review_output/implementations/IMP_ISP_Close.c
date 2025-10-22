int IMP_ISP_Close(void) {
    if (gISPdev == NULL) {
        LOG_ISP("Close: not opened");
        return 0;
    }

    /* Check if opened flag is >= 2 (sensor enabled) */
    if (gISPdev->opened >= 2) {
        LOG_ISP("Close: sensor still enabled");
        return -1;
    }

    /* Free ISP buffer if allocated */
    if (gISPdev->isp_buffer_phys != 0) {
        IMP_Free(gISPdev->isp_buffer_phys);
        gISPdev->isp_buffer_virt = NULL;
        gISPdev->isp_buffer_phys = 0;
        gISPdev->isp_buffer_size = 0;
    }
    /* Free optional secondary ISP buffer if allocated */
    if (gISPdev->isp_buffer2_phys != 0) {
        IMP_Free(gISPdev->isp_buffer2_phys);
        gISPdev->isp_buffer2_virt = NULL;
        gISPdev->isp_buffer2_phys = 0;
        gISPdev->isp_buffer2_size = 0;
    }

    /* Close devices */
    if (gISPdev->fd >= 0) {
        close(gISPdev->fd);
    }
    if (gISPdev->tisp_fd >= 0) {
        close(gISPdev->tisp_fd);
    }

    /* Free device structure */
    free(gISPdev);
    gISPdev = NULL;

    LOG_ISP("Close: closed ISP device");
    return 0;
}