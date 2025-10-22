int IMP_System_Bind(IMPCell *srcCell, IMPCell *dstCell) {
    if (srcCell == NULL) {
        fprintf(stderr, "[System] Bind failed: srcCell is NULL\n");
        return -1;
    }

    if (dstCell == NULL) {
        fprintf(stderr, "[System] Bind failed: dstCell is NULL\n");
        return -1;
    }

    // Assuming system_bind is a function that handles the actual binding logic
    return system_bind(srcCell, dstCell);
}