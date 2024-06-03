#include "ob_utils.h"

#ifdef _WIN32
int gettimeofday(struct timeval *tp, void *tzp)
{
  time_t clock;
  struct tm tm;
  SYSTEMTIME wtm;

  GetLocalTime(&wtm);
  tm.tm_year = wtm.wYear - 1900;
  tm.tm_mon = wtm.wMonth - 1;
  tm.tm_mday = wtm.wDay;
  tm.tm_hour = wtm.wHour;
  tm.tm_min = wtm.wMinute;
  tm.tm_sec = wtm.wSecond;
  tm.tm_isdst = -1;

  clock = mktime(&tm);
  tp->tv_sec = clock;
  tp->tv_usec = wtm.wMilliseconds * 1000;
  return (0);
}
#endif

int ob_gettimeofday(struct timeval *tp, void *tzp) 
{
  return gettimeofday(tp, tzp);
}

int64_t get_current_time_us()
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec * 1000000 + tv.tv_usec;
}
