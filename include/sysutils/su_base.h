/**
 * Sysutils Base Module
 * 
 * Base utilities for system operations
 */

#ifndef __SU_BASE_H__
#define __SU_BASE_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Sysutils version structure
 */
typedef struct {
    char chr[64];       /**< Version string */
} SUVersion;

/**
 * Get sysutils library version
 * 
 * @param version Pointer to version structure
 * @return 0 on success, negative on error
 */
int SU_Base_GetVersion(SUVersion *version);

#ifdef __cplusplus
}
#endif

#endif /* __SU_BASE_H__ */

