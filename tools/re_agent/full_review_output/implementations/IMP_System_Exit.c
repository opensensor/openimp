int IMP_System_Exit(void) {
    IMP_System *system = get_system_instance(); // Assume this function retrieves the singleton instance

    pthread_mutex_lock(&system->system_mutex);

    if (!system->system_initialized) {
        pthread_mutex_unlock(&system->system_mutex);
        return 0;
    }

    /* Clear module registry */
    memset(system->g_modules, 0, sizeof(system->g_modules));

    /* Cleanup subsystem modules
     * Each subsystem is responsible for its own cleanup
     * when their Destroy/Exit functions are called */

    fprintf(stderr, "[System] Subsystems cleaned up\n");

    system->system_initialized = 0;
    pthread_mutex_unlock(&system->system_mutex);

    fprintf(stderr, "[System] Exited\n");
    return 0;
}