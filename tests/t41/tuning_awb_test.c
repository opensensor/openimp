#define _GNU_SOURCE
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#define ioctl test_ioctl
#include "../../src/openimp_tuning.c"
#undef ioctl

static int response_error, untouched, gets, sets;
static uint32_t response_red = 2132, response_blue = 2340;
int test_ioctl(int fd, unsigned long command, ...)
{
    va_list args;
    struct t40_tuning_request *request;
    (void)fd;
    assert(command == TISP_VIDIOC_DEFAULT_TUNING);
    va_start(args, command);
    request = va_arg(args, struct t40_tuning_request *);
    va_end(args);
    if (request->id == TISP_CID_OPEN_AWB_TARGET) {
        uint32_t *gains = (void *)request->value_or_pointer;
        ++gets;
        if (response_error) { errno = response_error; return -1; }
        if (!untouched) { gains[0] = response_red; gains[1] = response_blue; }
    } else {
        assert(request->id == TISP_CID_OPEN_AWB_CONTROL);
        ++sets;
    }
    return 0;
}

int main(void)
{
    OpenIMPTuningController c = {0};
    uint16_t r = 1, b = 2;
    assert(!t41_calibrated_awb_target(&c, &r, &b));
    assert(r == 2132 && b == 2340 && c.calibrated_awb_support == 1);
    response_error = ENODATA;
    c.awb_update_samples = 4;
    assert(!t41_adapt_security_awb(&c));
    assert(sets == 0); /* Do not use stale gray-world gains without neutrals. */
    response_error = EINVAL;
    assert(t41_calibrated_awb_target(&c, &r, &b) == -EINVAL);
    assert(c.calibrated_awb_support == 1);
    response_error = 0;
    response_blue = 7000;
    assert(t41_calibrated_awb_target(&c, &r, &b) == -ERANGE);
    assert(r == 2132 && b == 2340);
    c = (OpenIMPTuningController){0};
    untouched = 1;
    assert(t41_calibrated_awb_target(&c, &r, &b) == -EOPNOTSUPP);
    assert(c.calibrated_awb_support == -1);
    gets = 0;
    assert(t41_calibrated_awb_target(&c, &r, &b) == -EOPNOTSUPP && !gets);
    c = (OpenIMPTuningController){0};
    untouched = 0;
    response_error = ENOTTY;
    assert(t41_calibrated_awb_target(&c, &r, &b) == -ENOTTY);
    assert(c.calibrated_awb_support == -1);
    puts("T41 calibrated AWB policy: passed");
    return 0;
}
