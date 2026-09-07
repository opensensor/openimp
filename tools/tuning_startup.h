/* Startup is allowed to precede device creation and ISP object initialization.
 * Retry only transient readiness failures; bad controls/ABI errors stay fatal. */
#ifndef OPENIMP_TUNING_STARTUP_H
#define OPENIMP_TUNING_STARTUP_H
#include <errno.h>
#include <signal.h>
#include "openimp/openimp_tuning.h"

static inline int tuning_startup_retryable(int error)
{
    return error == -ENOENT || error == -ENODEV || error == -ENXIO ||
           error == -EAGAIN || error == -EBUSY;
}

static inline int tuning_startup(OpenIMPTuningController **out,
                                const OpenIMPTuningConfig *config,
                                volatile sig_atomic_t *stopped,
                                unsigned int retries,
                                void (*wait_retry)(void))
{
    unsigned int attempt = 0;
    int ret;
    *out = NULL;
    for (;;) {
        if (*stopped)
            return -EINTR;
        ret = OpenIMP_Tuning_Create(out, config);
        if (!ret)
            ret = OpenIMP_Tuning_Start(*out);
        if (!ret) {
            if (!*stopped)
                return 0;
            ret = -EINTR;
        }
        if (*out) {
            OpenIMP_Tuning_Destroy(*out);
            *out = NULL;
        }
        if (!tuning_startup_retryable(ret) || attempt++ >= retries)
            return ret;
        wait_retry();
    }
}
#endif
