int IMP_System_GetVersion(IMPVersion *pstVersion) {
    if (pstVersion == NULL) {
        return -1;
    }

    snprintf(pstVersion->aVersion, sizeof(pstVersion->aVersion), "IMP-%s", IMP_VERSION);
    return 0;
}