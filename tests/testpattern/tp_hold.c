/* sleeps N seconds with the test pattern library loaded (control socket test host) */
#include <stdlib.h>
#include <unistd.h>
#include "openimp_testpattern.h"
int main(int argc, char **argv) { (void)OpenIMP_Cap_TestPattern; sleep(argc > 1 ? (unsigned)atoi(argv[1]) : 3); return 0; }
