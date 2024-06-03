/************************************************************************
   Copyright (c) 2021 OceanBase.
   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.
   
   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.
   
   You should have received a copy of the GNU Library General Public
   License along with this library; if not, write to the Free
   Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
   MA 02111-1301, USA 

   Part of this code includes code from PHP's mysqlnd extension
   (written by Andrey Hristov, Georg Richter and Ulf Wendel), freely
   available from http://www.php.net/software

*************************************************************************/

#include "ob_cond.h"

#ifdef _WIN32
#include <time.h>
#include <limits.h>
union ft64 {
  FILETIME ft;
  __int64 i64;
};

DWORD ob_get_milliseconds(const struct timespec *ob_timespec)
{
  DWORD ret;
  union ft64 ob_now_time;
  long long ob_millis_time;
  unsigned __int64   obtime;
  static const unsigned __int64 EPOCH = ((unsigned __int64)116444736000000000ULL);

  if (NULL == ob_timespec) {
    ret = INFINITE;
  } else {
    obtime = ob_timespec->tv_sec * 10000000L + EPOCH;
    GetSystemTimeAsFileTime(&ob_now_time.ft);
    ob_millis_time = (obtime - ob_now_time.i64) / 10000;
    if (ob_millis_time < 0) {
      ret = 0;
    } else {
      if (ob_millis_time > UINT_MAX) {
        ob_millis_time= UINT_MAX;
      }
      ret = ob_millis_time;
    }
  }

  return ret;
}

int ob_cond_init(ob_cond_t *ob_cond)
{
  InitializeConditionVariable(ob_cond);
  return 0;
}

int ob_cond_destroy(ob_cond_t *ob_cond)
{
  // do nothing
  return 0;
}

int ob_cond_timedwait(ob_cond_t *ob_cond, ob_mutex_t *ob_mutex, const struct timespec *ob_timespec)
{
  int ret;
  DWORD timeout= ob_get_milliseconds(ob_timespec);
  if (!SleepConditionVariableCS(ob_cond, ob_mutex, timeout)) {
    ret = ETIMEDOUT;
  } else {
    ret = 0;
  }
  return ret;
}

int ob_cond_wait(ob_cond_t *ob_cond, ob_mutex_t *ob_mutex)
{
  int ret;
  if (!SleepConditionVariableCS(ob_cond, ob_mutex, INFINITE)) {
    ret = ETIMEDOUT;
  } else {
    ret = 0;
  }
  return ret;
}

int ob_cond_signal(ob_cond_t *ob_cond)
{
  WakeConditionVariable(ob_cond);
  return 0;
}

int ob_cond_broadcast(ob_cond_t *ob_cond)
{
  WakeAllConditionVariable(ob_cond);
  return 0;
}

#else

int ob_cond_init(ob_cond_t *ob_cond)
{
  return pthread_cond_init(ob_cond, NULL);
}

int ob_cond_destroy(ob_cond_t *ob_cond)
{
  return pthread_cond_destroy(ob_cond);
}

int ob_cond_timedwait(ob_cond_t *ob_cond, ob_mutex_t *ob_mutex, const struct timespec *ob_timespec)
{
  return pthread_cond_timedwait(ob_cond, ob_mutex, ob_timespec);
}

int ob_cond_wait(ob_cond_t *ob_cond, ob_mutex_t *ob_mutex)
{
  return pthread_cond_wait(ob_cond, ob_mutex);
}

int ob_cond_signal(ob_cond_t *ob_cond)
{
  return pthread_cond_signal(ob_cond);
}

int ob_cond_broadcast(ob_cond_t *ob_cond)
{
  return pthread_cond_broadcast(ob_cond);
}

#endif