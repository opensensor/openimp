int IMP_ISP_Tuning_SetContrast(unsigned char contrast) {
    LOG_ISP("SetContrast: %u", contrast);

    if (gISPdev == NULL) {
        LOG_ISP("SetContrast: ISP not opened");
        return -1;
    }

    if (gISPdev->tuning == NULL) {
        LOG_ISP("SetContrast: Tuning structure not initialized");
        return -1;
    }

    gISPdev->tuning->contrast_byte = contrast;
    return 0;
}