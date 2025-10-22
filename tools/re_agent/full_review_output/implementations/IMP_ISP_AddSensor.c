int IMP_ISP_AddSensor(ISPDevice *dev, const char *arg1, char *bpath) {
    if (dev == NULL || arg1 == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, 0xea498, 0xea4b0, 0x19d, 0xec984, 0xea884);
        return -1;
    }

    if (dev->status >= 2) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, 0xea498, 0xea4b0, 0x1a2, 0xec984, 0xea818);
        return -1;
    }

    if (ioctl(dev->fd, 0x805056c1, arg1) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, 0xea498, 0xea4b0, 0x1a7, 0xec984, 0xea898, arg1);
        return -1;
    }

    if (bpath != NULL) {
        int result = ioctl(dev->fd, 0xc00456c7, bpath);
        free(bpath);
        bpath = NULL;
        if (result != 0) {
            return result;
        }
    }

    int result = -1;
    int result_4 = 0;
    while (ioctl(dev->fd, 0xc050561a, &result_4) == 0) {
        char str2[80];
        if (strcmp(arg1, str2) == 0) {
            result = result_4;
            break;
        }
        result_4++;
    }

    if (result == -1) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, 0xea498, 0xea4b0, 0x1c0, 0xec984, 0xea8bc, arg1);
        return result;
    }

    if (ioctl(dev->fd, 0xc0045627, &result) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, 0xea498, 0xea4b0, 0x1c6, 0xec984, 0xea8dc, arg1);
        return -1;
    }

    memcpy(dev->sensor_data, arg1, sizeof(dev->sensor_data));

    int var_38;
    if (ioctl(dev->fd, 0x800856d5, &var_38) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, 0xea498, 0xea4b0, 0x1dc, 0xec984, 0xea8fc);
        return -1;
    }

    imp_log_fun(4, IMP_Log_Get_Option(), 2, 0xea498, 0xea4b0, 0x1e0, 0xec984, 0xea91c, 0xec984, 0x1e0, var_38, 0);

    void *mem = malloc(0x94);
    if (mem == NULL) {
        printf(0xea940, 0xec984, 0x1e3);
        return -1;
    }

    if (IMP_Alloc(mem, 0, 0xea95c) != 0) {
        printf(0xea964, 0xec984, 0x1e8);
        free(mem);
        return -1;
    }

    dev->sensor_mem = mem;
    var_38 = *((int *)(mem + 0x84));
    if (ioctl(dev->fd, 0x800856d4, &var_38) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, 0xea498, 0xea4b0, 0x1ee, 0xec984, 0xea980);
        return -1;
    }

    if (dev->init_status != 1) {
        return 0;
    }

    var_38 = 0;
    if (ioctl(dev->fd, 0x800856d7, &var_38) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, 0xea498, 0xea4b0, 0x1f6, 0xec984, 0xea9a0);
        return -1;
    }

    imp_log_fun(4, IMP_Log_Get_Option(), 2, 0xea498, 0xea4b0, 0x1fa, 0xec984, 0xea91c, 0xec984, 0x1fa, var_38, 0);

    void *additional_mem = malloc(0x94);
    if (additional_mem == NULL) {
        printf(0xea940, 0xec984, 0x1fd);
        return -1;
    }

    if (IMP_Alloc(additional_mem, 0, 0xea9c4) != 0) {
        printf(0xea964, 0xec984, 0x202);
        free(additional_mem);
        return -1;
    }

    dev->additional_mem = additional_mem;
    var_38 = *((int *)(additional_mem + 0x84));
    if (ioctl(dev->fd, 0x800856d6, &var_38) != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, 0xea498, 0xea4b0, 0x208, 0xec984, 0xea9cc);
        return -1;
    }

    return 0;
}