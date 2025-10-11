/**
 * Sysutils Base Module Implementation
 */

#include <stdio.h>
#include <string.h>
#include <sysutils/su_base.h>

#define SU_VERSION "1.0.0"

int SU_Base_GetVersion(SUVersion *version) {
    if (version == NULL) {
        return -1;
    }
    
    snprintf(version->chr, sizeof(version->chr), "SU-%s", SU_VERSION);
    return 0;
}

