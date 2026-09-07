#include <assert.h>
#include <stdio.h>
#include "../../tools/tuning_startup.h"
static int create_error, start_error, creates, starts, destroys, waits;
static int create_transients, start_transients;
static volatile sig_atomic_t stopped;
static int token;
int OpenIMP_Tuning_Create(OpenIMPTuningController **out,
                         const OpenIMPTuningConfig *config)
{
    (void)config;
    ++creates;
    *out = NULL;
    if (create_transients-- > 0) return create_error;
    *out = (OpenIMPTuningController *)&token;
    return 0;
}
int OpenIMP_Tuning_Start(OpenIMPTuningController *controller)
{
    assert(controller == (OpenIMPTuningController *)&token);
    ++starts;
    return start_transients-- > 0 ? start_error : 0;
}
void OpenIMP_Tuning_Destroy(OpenIMPTuningController *controller)
{
    assert(controller == (OpenIMPTuningController *)&token);
    ++destroys;
}
static void retry_wait(void) { ++waits; }
static void cancel_wait(void) { ++waits; stopped = 1; }
static void reset(void)
{
    creates = starts = destroys = waits = 0;
    create_transients = start_transients = 0;
    stopped = 0;
}
int main(void)
{
    OpenIMPTuningController *out;
    reset(); create_error = -ENOENT; create_transients = 2;
    start_error = -ENODEV; start_transients = 2;
    assert(!tuning_startup(&out, NULL, &stopped, 5, retry_wait));
    assert(out && creates == 5 && starts == 3 && destroys == 2 && waits == 4);
    reset(); start_error = -EOPNOTSUPP; start_transients = 10;
    assert(tuning_startup(&out, NULL, &stopped, 5, retry_wait) == -EOPNOTSUPP);
    assert(!out && creates == 1 && destroys == 1 && !waits);
    reset(); start_error = -EAGAIN; start_transients = 10;
    assert(tuning_startup(&out, NULL, &stopped, 3, retry_wait) == -EAGAIN);
    assert(!out && creates == 4 && destroys == 4 && waits == 3);
    reset(); start_transients = 10;
    assert(tuning_startup(&out, NULL, &stopped, 5, cancel_wait) == -EINTR);
    assert(!out && creates == 1 && destroys == 1 && waits == 1);
    reset(); stopped = 1;
    assert(tuning_startup(&out, NULL, &stopped, 5, retry_wait) == -EINTR);
    assert(!out && !creates);
    assert(!tuning_startup_retryable(-EINVAL));
    assert(!tuning_startup_retryable(-ENOTTY));
    puts("Tuning startup: late device/ISP, bounded retry, fatal errors and cancellation passed");
    return 0;
}
