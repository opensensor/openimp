#ifndef OPENIMP_T23_PERSIST_H
#define OPENIMP_T23_PERSIST_H

#include <stddef.h>

int openimp_t23_persist_enabled(void);
void openimp_t23_persist_write(const char *message, size_t size);
void openimp_t23_persist_trace(const char *format, ...)
    __attribute__((format(printf, 1, 2)));

#endif
