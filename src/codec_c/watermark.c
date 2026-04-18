#include <stdlib.h>

void watermark_deinit(void *arg1)
{
    if (arg1 == 0) {
        return;
    }

    free(arg1);
}
