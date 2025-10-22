int IMP_IVS_DestroyGroup(int grpNum) {
    if (grpNum < 0 || grpNum >= MAX_IVS_GROUPS) {
        LOG_IVS("DestroyGroup: invalid group number %d", grpNum);
        return -1;
    }

    if (!g_ivs_groups[grpNum]) {
        LOG_IVS("DestroyGroup: group %d doesn't exist", grpNum);
        return -1;
    }

    /* Note: Module memory lifecycle is handled by System; just unregister locally */
    g_ivs_groups[grpNum] = NULL;
    LOG_IVS("DestroyGroup: grp=%d", grpNum);
    return 0;
}