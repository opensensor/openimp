int IMP_ISP_EnableSensor(void) {
    int ret;

    if (gISPdev == NULL) {
        LOG_ISP("EnableSensor: ISP not opened");
        return -1;
    }

    /* Skip AE/AWB ioctls - the streamer doesn't call SetAeAlgoFunc/SetAwbAlgoFunc,
     * so the stock libimp must handle AE/AWB differently (maybe in the driver or
     * automatically enabled). We need to figure out what's actually required.
     */
    LOG_ISP("EnableSensor: proceeding without custom AE/AWB");

    /* CRITICAL: Call ioctl 0x40045626 to get sensor index before streaming
     * This ioctl validates the sensor is ready and returns the sensor index.
     * It must be called before STREAMON.
     */
    int sensor_idx = -1;
    LOG_ISP("EnableSensor: about to call ioctl 0x40045626");
    ret = ioctl(gISPdev->fd, 0x40045626, &sensor_idx);
    LOG_ISP("EnableSensor: ioctl 0x40045626 returned %d", ret);
    if (ret != 0) {
        LOG_ISP("EnableSensor: ioctl 0x40045626 (GET_SENSOR_INDEX) failed: %s", strerror(errno));
        return -1;
    }
    LOG_ISP("EnableSensor: ioctl 0x40045626 succeeded, sensor_idx=%d", sensor_idx);

    if (sensor_idx == -1) {
        LOG_ISP("EnableSensor: sensor index is -1, sensor not ready");
        return -1;
    }

    LOG_ISP("EnableSensor: sensor index validated, proceeding to STREAMON/LINK_SETUP now (OEM parity)");

    /* Start ISP streaming and link setup immediately to match OEM */
    if (ISP_EnsureLinkStreamOn() != 0) {
        LOG_ISP("EnableSensor: failed to start ISP stream + link setup");
        return -1;
    }

    gISPdev->opened += 2;
    sensor_enabled = 1;
    return 0;
}