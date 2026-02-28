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

*************************************************************************/

#include "ob_cond.h"
#include "ob_utils.h"

#ifndef _WIN32
#include <sys/time.h>
#endif

#ifdef _WIN32
#include <time.h>
#include <limits.h>

DWORD ob_get_wait_milliseconds(const struct timespec *ob_timespec)
{
  DWORD ret;
  struct timeval tv;
  long long wait_ms;

  if (NULL == ob_timespec) {
    ret = INFINITE;
  } else {
    ob_gettimeofday(&tv, NULL);
    wait_ms = ((unsigned __int64)ob_timespec->tv_sec * 1000000L + ob_timespec->tv_nsec / 1000) -
      ((unsigned __int64)tv.tv_sec * 1000000L + tv.tv_usec);
    if (wait_ms < 0) {
      ret = 0;
    } else {
      wait_ms = wait_ms / 1000;
      if (wait_ms > UINT_MAX) {
        wait_ms = UINT_MAX;
      }
      ret = wait_ms;
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
  DWORD timeout = ob_get_wait_milliseconds(ob_timespec);
  if (timeout > 0 && !SleepConditionVariableCS(ob_cond, ob_mutex, timeout)) {
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

int ob_cond_timedwait_us(ob_cond_t *cond, ob_mutex_t *mutex, unsigned long long wait_us)
{
  int ret;
  DWORD timeout = wait_us/1000;
  if (timeout > 0 && !SleepConditionVariableCS(cond, mutex, timeout)) {
    ret = ETIMEDOUT;
  } else {
    ret = 0;
  }
  return ret;
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

int ob_cond_timedwait_us(ob_cond_t *cond, ob_mutex_t *mutex, unsigned long long wait_us)
{
  struct timespec ts;
  struct timeval tv;
  unsigned long long abs_time = 0;
  gettimeofday(&tv, NULL);
  abs_time = wait_us + (tv.tv_sec * 1000000 + tv.tv_usec);
  ts.tv_sec = abs_time / 1000000;
  ts.tv_nsec = (abs_time % 1000000) * 1000;
  return pthread_cond_timedwait(cond, mutex, &tv);
}

#endif