int IMP_System_UnBind(IMPCell *srcCell, IMPCell *dstCell) {
    if (srcCell == NULL || dstCell == NULL) {
        fprintf(stderr, "[System] UnBind failed: NULL cell\n");
        return -1;
    }

    fprintf(stderr, "[System] UnBind: [%d,%d,%d] -> [%d,%d,%d]\n",
            srcCell->deviceID, srcCell->groupID, srcCell->outputID,
            dstCell->deviceID, dstCell->groupID, dstCell->outputID);

    /* Get source and destination modules */
    Module *src_module = (Module*)IMP_System_GetModule(srcCell->deviceID, srcCell->groupID);
    Module *dst_module = (Module*)IMP_System_GetModule(dstCell->deviceID, dstCell->groupID);

    if (src_module == NULL || dst_module == NULL) {
        fprintf(stderr, "[System] UnBind: module not found\n");
        return -1;
    }

    /* Call unbind function if it exists */
    if (src_module->unbind_func != NULL) {
        int (*unbind_fn)(void*, void*, void*) = src_module->unbind_func;

        /* Calculate output pointer */
        void *output_ptr = (void*)((char*)src_module + 0x128 + ((srcCell->outputID + 4) << 2));

        if (unbind_fn(src_module, dst_module, output_ptr) < 0) {
            fprintf(stderr, "[System] UnBind: unbind function failed\n");
            return -1;
        }
    } else {
        /* No unbind function - just remove observer */
        if (remove_observer_from_module(src_module, dst_module) < 0) {
            fprintf(stderr, "[System] UnBind: failed to remove observer\n");
            return -1;
        }
    }

    fprintf(stderr, "[System] UnBind: success\n");
    return 0;
}