#ifndef NET_COMMON_H
#define NET_COMMON_H

#include <stdio.h>
#include <stdint.h>

#define NET_ERROR     -1
#define NET_PARAM_ERR -2

#define F_NONBLOCK   1

#define NET_LOG_DBG(fmt, args...)   printf(fmt,##args)
#define NET_LOG_INF(fmt, args...)   printf(fmt,##args)
#define NET_LOG_WRN(fmt, args...)   printf(fmt,##args)
#define NET_LOG_ERR(fmt, args...)   printf(fmt,##args)

typedef uint64_t clock_time_t;

clock_time_t gw_current_time_ms();


void string_assign(char **dst, char *src);

#endif
