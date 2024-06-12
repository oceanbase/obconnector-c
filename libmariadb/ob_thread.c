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
#include "ob_thread.h"

#ifdef _WIN32
#include <process.h>
#include <signal.h>

unsigned int __stdcall ob_win_thread_start(void *win_start_param)
{
  struct ob_thread_start_param *ob_start_param= (struct ob_thread_start_param *)win_start_param;
  ob_start_routine ob_start_func= ob_start_param->func;
  void *ob_start_arg= ob_start_param->arg;
  free(win_start_param);
  (*ob_start_func)(ob_start_arg);
  return 0;
}

ob_thread_t ob_thread_self()
{
  return GetCurrentThreadId();
}

int ob_thread_equal(ob_thread_t ob_thread1, ob_thread_t ob_thread2)
{
  return (ob_thread1 == ob_thread2);
}

int ob_thread_attr_init(ob_thread_attr_t *ob_thread_attr)
{
  ob_thread_attr->ob_thead_state= OB_THREAD_CREATE_JOINABLE;
  ob_thread_attr->ob_thread_stack_size= 0;
  return 0;
}

int ob_thread_attr_destroy(ob_thread_attr_t *ob_thread_attr)
{
  ob_thread_attr->ob_thead_state= OB_THREAD_CREATE_JOINABLE;
  ob_thread_attr->ob_thread_stack_size= 0;
  return 0;
}

int ob_thread_attr_setstacksize(ob_thread_attr_t *ob_thread_attr, size_t ob_stacksize)
{
  ob_thread_attr->ob_thread_stack_size= (DWORD)ob_stacksize;
  return 0;
}

int ob_thread_attr_setdetachstate(ob_thread_attr_t *ob_thread_attr, int ob_ob_thead_state)
{
  ob_thread_attr->ob_thead_state= ob_ob_thead_state;
  return 0;
}

int ob_thread_attr_getstacksize(ob_thread_attr_t *ob_thread_attr, size_t *ob_stacksize)
{
  *ob_stacksize= (size_t)ob_thread_attr->ob_thread_stack_size;
  return 0;
}

void ob_thread_yield()
{
  SwitchToThread();
}

int ob_thread_once(ob_thread_once_t *ob_once_control,  void (*ob_routine)(void))
{
  int ret = 0;
  LONG ob_thread_state;

  if (OB_THREAD_ONCE_DONE == *ob_once_control) {
    ret = 0;
  } else {
    ob_thread_state = InterlockedCompareExchange(ob_once_control, OB_THREAD_ONCE_INPROGRESS, OB_THREAD_ONCE_INIT);

    switch(ob_thread_state)
    {
    case OB_THREAD_ONCE_DONE:
    {
      // do nothing
      break;
    }
    case OB_THREAD_ONCE_INPROGRESS:
    {
      while(OB_THREAD_ONCE_INPROGRESS == *ob_once_control) {
        Sleep(1);   // sleep
      }
      break;
    }
    case OB_THREAD_ONCE_INIT:
    {
      (*ob_routine)();
      *ob_once_control = OB_THREAD_ONCE_DONE;
      break;
    }
    }
  }
  return ret;
}

int ob_thread_create(ob_thread_handle *ob_thread, const ob_thread_attr_t *ob_thread_attr,
                     ob_start_routine ob_thread_func, void *ob_thread_arg)
{
  int ret = 0;
  unsigned int ob_thread_stack_size;
  struct ob_thread_start_param *ob_thread_param;

  if (NULL == (ob_thread_param = (struct ob_thread_start_param *)malloc(sizeof(*ob_thread_param)))) {
    ret = 1;  // error
  } else {
    ob_thread_param->arg  = ob_thread_arg;
    ob_thread_param->func = ob_thread_func;
    ob_thread_stack_size  = ob_thread_attr ? ob_thread_attr->ob_thread_stack_size : 0;
    ob_thread->handle= (HANDLE)_beginthreadex(NULL, ob_thread_stack_size, ob_win_thread_start, ob_thread_param, 0, &ob_thread->thread);
    if (NULL == ob_thread->handle) {
      // error, free param
      free(ob_thread_param);
      ret = 1;
    } else if (ob_thread_attr && OB_THREAD_CREATE_DETACHED == ob_thread_attr->ob_thead_state) {
      CloseHandle(ob_thread->handle);
      ob_thread->handle= NULL;
    }
  }
  if (0 != ret) {
    ob_thread->handle= NULL;
    ob_thread->thread= 0;
  }
  return ret;
}

int ob_thread_join(ob_thread_handle *ob_thread, void **ob_join_ptr)
{
  int ret = 0;
  if (WAIT_OBJECT_0 != WaitForSingleObject(ob_thread->handle, INFINITE)) {
    ret = 1;
  } else if (ob_thread->handle) {
    CloseHandle(ob_thread->handle);
  }
  if (0 != ret) {
    ob_thread->thread= 0;
    ob_thread->handle= NULL;
  }
  return ret;
}

int ob_thread_cancel(ob_thread_handle *ob_thread)
{
  int ret = 0;
  BOOL terminate_ret = FALSE;
  if (ob_thread->handle) {
    if (FALSE == (terminate_ret = TerminateThread(ob_thread->handle, 0))) {
      errno= EINVAL;
      ret = -1;
    } else {
      CloseHandle(ob_thread->handle);
    }
  }
  return ret;
}

void ob_thread_exit(void *ob_thread_exit_ptr)
{
  _endthreadex(0);
}
#else
// for unix

ob_thread_t ob_thread_self()
{
  return pthread_self();
}

int ob_thread_equal(ob_thread_t ob_thread1, ob_thread_t ob_thread2)
{
  return pthread_equal(ob_thread1, ob_thread2);
}

int ob_thread_attr_init(ob_thread_attr_t *ob_thread_attr)
{
  return pthread_attr_init(ob_thread_attr);
}

int ob_thread_attr_destroy(ob_thread_attr_t *ob_thread_attr)
{
  return pthread_attr_destroy(ob_thread_attr);
}

int ob_thread_attr_setstacksize(ob_thread_attr_t *ob_thread_attr, size_t ob_stacksize)
{
  return pthread_attr_setstacksize(ob_thread_attr, ob_stacksize);
}

int ob_thread_attr_setdetachstate(ob_thread_attr_t *ob_thread_attr, int ob_ob_thead_state)
{
  return pthread_attr_setdetachstate(ob_thread_attr, ob_ob_thead_state);
}

int ob_thread_attr_getstacksize(ob_thread_attr_t *ob_thread_attr, size_t *ob_stacksize)
{
  return pthread_attr_getstacksize(ob_thread_attr, ob_stacksize);
}

void ob_thread_yield()
{
  sched_yield();
}

int ob_thread_once(ob_thread_once_t *ob_once_control,  void (*ob_routine)(void))
{
  return pthread_once(ob_once_control, ob_routine);
}

int ob_thread_create(ob_thread_handle *ob_thread, const ob_thread_attr_t *ob_thread_attr,
                     ob_start_routine ob_thread_func, void *ob_thread_arg)
{
  return pthread_create(&ob_thread->thread, ob_thread_attr, ob_thread_func, ob_thread_arg);
}

int ob_thread_join(ob_thread_handle *ob_thread, void **ob_join_ptr)
{
  return pthread_join(ob_thread->thread, ob_join_ptr);
}

int ob_thread_cancel(ob_thread_handle *ob_thread)
{
  return pthread_cancel(ob_thread->thread);
}

void ob_thread_exit(void *ob_thread_exit_ptr)
{
  pthread_exit(ob_thread_exit_ptr);
}

#endif