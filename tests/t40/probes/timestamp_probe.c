#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

extern int64_t IMP_System_GetTimeStamp(void);
extern int IMP_System_RebaseTimeStamp(int64_t basets);

int main(void)
{
    int64_t first;
    int64_t second;
    int64_t rebased;
    int64_t delta;

    first = IMP_System_GetTimeStamp();
    usleep(120000);
    second = IMP_System_GetTimeStamp();
    delta = second - first;
    printf("first=%lld second=%lld delta=%lld\n",
           (long long)first, (long long)second, (long long)delta);
    if (delta < 80000 || delta > 500000)
        return 2;

    if (IMP_System_RebaseTimeStamp(1234567) != 0)
        return 3;
    rebased = IMP_System_GetTimeStamp();
    printf("rebased=%lld\n", (long long)rebased);
    if (rebased < 1234567 || rebased > 1334567)
        return 4;

    return 0;
}
