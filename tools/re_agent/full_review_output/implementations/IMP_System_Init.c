int IMP_System_Init(void) {
    IMP_System *system = get_system_instance(); // Assume this function retrieves the singleton instance

    pthread_mutex_lock(&system->system_mutex);

    if (system->system_initialized) {
        pthread_mutex_unlock(&system->system_mutex);
        return 0;
    }

    fprintf(stderr, "[System] Initializing...\n");

    /* Initialize module registry */
    memset(system->g_modules, 0, sizeof(system->g_modules));

    /* Initialize timestamp base */
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) {
        fprintf(stderr, "[System] Failed to get time\n");
        pthread_mutex_unlock(&system->system_mutex);
        return -1;
    }

    system->timestamp_base = (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;

    /* Get CPU ID */
    get_cpu_id();

    /* Initialize subsystem modules eagerly to match OEM behavior */
    int ret_fs = FrameSourceInit();
    if (ret_fs < 0) {
        pthread_mutex_unlock(&system->system_mutex);
        fprintf(stderr, "[System] FrameSourceInit failed\n");
        return -1;
    }
    int ret_enc = EncoderInit();
    if (ret_enc < 0) {
        pthread_mutex_unlock(&system->system_mutex);
        fprintf(stderr, "[System] EncoderInit failed\n");
        return -1;
    }

    fprintf(stderr, "[System] Subsystems initialized\n");

    system->system_initialized = 1;
    pthread_mutex_unlock(&system->system_mutex);

    fprintf(stderr, "[System] Initialized (IMP-%s)\n", IMP_VERSION);
    return 0;
}