#ifndef OPENIMP_ALCODEC_AL_LOGGER_H
#define OPENIMP_ALCODEC_AL_LOGGER_H

#include <stdint.h>
#include "al_types.h"

int32_t AL_LoggerInit(AL_TLogger *logger, int32_t writer, int32_t entries, int32_t capacity);
int32_t AL_LoggerDeinit(AL_TLogger *logger);
int32_t AL_Log(AL_TLogger *logger, char *message);

#endif
