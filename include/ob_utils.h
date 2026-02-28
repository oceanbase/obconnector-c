#ifndef _OB_UTILS_H_
#define _OB_UTILS_H_
#include "mysql.h"
#include "ma_global.h"

#ifdef _WIN32
#define ATOMIC_CAS_LONG(val, cmpv, newv) InterlockedCompareExchange((val), (newv), (cmpv))
#else
#define ATOMIC_CAS_LONG(val, cmpv, newv) __sync_val_compare_and_swap((val), (cmpv), (newv))
#endif

#ifdef _WIN32
int gettimeofday(struct timeval *tp, void *tzp);
#endif

int ob_gettimeofday(struct timeval *tp, void *tzp);
int64_t get_current_time_us();

#endif
