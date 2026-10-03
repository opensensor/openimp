/*
 * imp_log_fun.h -- the one declaration of the OEM-shaped logging entry
 * points exported by libimp.so (implemented in core/imp_log.c).
 */
#ifndef IMP_LOG_FUN_H
#define IMP_LOG_FUN_H

int IMP_Log_Get_Option(void);
int imp_log_fun(int level, int option, int type, ...);

#endif /* IMP_LOG_FUN_H */
