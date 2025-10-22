int IMP_IVS_CreateChn(int chnNum, IMPIVSInterface *handler) {
    if (chnNum < 0 || chnNum >= MAX_IVS_CHANNELS) {
        LOG_IVS("CreateChn failed: invalid chn=%d", chnNum);
        return -1;
    }
    if (!handler) {
        LOG_IVS("CreateChn failed: handler=NULL");
        return -1;
    }
    /* Vendor requires either idx[5] or idx[6] set (process callbacks) */
    if (handler->process == NULL && handler->cb6 == NULL) {
        LOG_IVS("CreateChn failed: missing process callbacks (idx5/6)");
        return -1;
    }
    IVSChn *c = &g_ivs_chn[chnNum];
    if (c->iface) {
        LOG_IVS("CreateChn: chn=%d already exists", chnNum);
        return 0;
    }
    memset(c, 0, sizeof(*c));
    c->chn_id = chnNum;
    c->grp_id = -1;
    c->running = 0;
    c->iface = handler;

    /* Initialize semaphores to match vendor behavior: sem_lock=1, sem_result=0 */
    sem_init(&c->sem_frame, 0, 0);
    sem_init(&c->sem_lock, 0, 1);
    sem_init(&c->sem_result, 0, 0);

    /* Optional init callback */
    if (c->iface->init && c->iface->init(c->iface) < 0) {
        LOG_IVS("CreateChn: init callback failed");
        sem_destroy(&c->sem_result);
        sem_destroy(&c->sem_lock);
        sem_destroy(&c->sem_frame);
        c->iface = NULL;
        return -1;
    }

    if (pthread_create(&c->thread, NULL, ivs_worker, c) != 0) {
        LOG_IVS("CreateChn: thread create failed");
        if (c->iface->exit) c->iface->exit(c->iface);
        sem_destroy(&c->sem_result);
        sem_destroy(&c->sem_lock);
        sem_destroy(&c->sem_frame);
        c->iface = NULL;
        return -1;
    }

    LOG_IVS("CreateChn: chn=%d", chnNum);
    return 0;
}