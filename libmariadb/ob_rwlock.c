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

#include "ob_rwlock.h"

#ifdef _WIN32

int ob_mutex_init(ob_mutex_t *ob_mutex, const ob_mutexattr_t *attr)
{
  InitializeCriticalSection(ob_mutex);
  return 0;
}

int ob_mutex_lock(ob_mutex_t *ob_mutex)
{
  EnterCriticalSection(ob_mutex);
  return 0;
}

int ob_mutex_trylock(ob_mutex_t *ob_mutex)
{
  int ret = 0;

  if (!TryEnterCriticalSection(ob_mutex)) {
    ret = EBUSY;
  } else if (ob_mutex->RecursionCount > 1) {
    LeaveCriticalSection(ob_mutex);
    ret = EBUSY;
  } else {
    ret = 0;
  }

  return ret;
}

int ob_mutex_unlock(ob_mutex_t *ob_mutex)
{
  LeaveCriticalSection(ob_mutex);
  return 0;
}

int ob_mutex_destroy(ob_mutex_t *ob_mutex)
{
  DeleteCriticalSection(ob_mutex);
  return 0;
}

int ob_rw_init(ob_rw_lock_t *ob_rwp)
{
  InitializeSRWLock(&ob_rwp->ob_srwlock);
  ob_rwp->is_exclusive = FALSE;
  return 0;
}

int ob_rw_destroy(ob_rw_lock_t *ob_rwp)
{
  // do nothing
  return 0;
}

int ob_rw_rdlock(ob_rw_lock_t *ob_rwp)
{
  AcquireSRWLockShared(&ob_rwp->ob_srwlock);
  return 0;
}

int ob_rw_tryrdlock(ob_rw_lock_t *ob_rwp)
{
  int ret = 0;
  if (!TryAcquireSRWLockShared(&ob_rwp->ob_srwlock)) {
    ret = EBUSY;
  } else {
    ret = 0;
  }
  return ret;
}

int ob_rw_wrlock(ob_rw_lock_t *ob_rwp)
{
  AcquireSRWLockExclusive(&ob_rwp->ob_srwlock);
  ob_rwp->is_exclusive= TRUE;
  return 0;
}

int ob_rw_trywrlock(ob_rw_lock_t *ob_rwp)
{
  int ret = 0;;
  if (!TryAcquireSRWLockExclusive(&ob_rwp->ob_srwlock)) {
    ret = EBUSY;
  } else {
    ob_rwp->is_exclusive= TRUE;
    ret = 0;
  }
  return ret;
}

int ob_rw_unlock(ob_rw_lock_t *ob_rwp)
{
  if (ob_rwp->is_exclusive) {
    ob_rwp->is_exclusive= FALSE;
    ReleaseSRWLockExclusive(&ob_rwp->ob_srwlock);
  } else {
    ReleaseSRWLockShared(&ob_rwp->ob_srwlock);
  }
  return 0;
}
#else

int ob_mutex_init(ob_mutex_t *ob_mutex, const ob_mutexattr_t *attr)
{
  return pthread_mutex_init(ob_mutex, (pthread_mutexattr_t *)attr);
}

int ob_mutex_lock(ob_mutex_t *ob_mutex)
{
  return pthread_mutex_lock(ob_mutex);
}

int ob_mutex_trylock(ob_mutex_t *ob_mutex)
{
  return pthread_mutex_trylock(ob_mutex);
}

int ob_mutex_unlock(ob_mutex_t *ob_mutex)
{
  return pthread_mutex_unlock(ob_mutex);
}

int ob_mutex_destroy(ob_mutex_t *ob_mutex)
{
  return pthread_mutex_destroy(ob_mutex);
}

int ob_rw_init(ob_rw_lock_t *ob_rwp)
{
  return pthread_rwlock_init(ob_rwp, NULL);
}

int ob_rw_destroy(ob_rw_lock_t *ob_rwp)
{
  return pthread_rwlock_destroy(ob_rwp);
}

int ob_rw_rdlock(ob_rw_lock_t *ob_rwp)
{
  return pthread_rwlock_rdlock(ob_rwp);
}

int ob_rw_tryrdlock(ob_rw_lock_t *ob_rwp)
{
  return pthread_rwlock_tryrdlock(ob_rwp);
}

int ob_rw_wrlock(ob_rw_lock_t *ob_rwp)
{
  return pthread_rwlock_wrlock(ob_rwp);
}

int ob_rw_trywrlock(ob_rw_lock_t *ob_rwp)
{
  return pthread_rwlock_trywrlock(ob_rwp);
}

int ob_rw_unlock(ob_rw_lock_t *ob_rwp)
{
  return pthread_rwlock_unlock(ob_rwp);
}
#endif

