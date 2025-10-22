int IMP_ISP_DisableSensor(void) {
    LOG_ISP("DisableSensor");

    // Assuming gISPdev is a global pointer to the ISPDevice structure
    if (gISPdev == NULL) {
        LOG_ISP("DisableSensor: ISP not opened");
        return -1;
    }

    // Assuming sensor_enabled is a global or static variable indicating sensor state
    sensor_enabled = 0;

    // Additional logic may be required to properly disable the sensor
    // This could involve ioctl calls or other cleanup operations

    return 0;
}