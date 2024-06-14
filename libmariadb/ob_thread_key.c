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

#include "ob_thread_key.h"

#ifndef UNUSED
#define UNUSED(x) ((void)(x))
#endif

#ifdef _WIN32

int ob_create_thread_key(ob_thread_key_t *ob_key, void (*ob_destructor)(void *))
{
  UNUSED(ob_destructor);
  *ob_key = TlsAlloc();
  return (TLS_OUT_OF_INDEXES == *ob_key);
}

int ob_delete_thread_key(ob_thread_key_t ob_key)
{
  return !TlsFree(ob_key);
}

void *ob_get_thread_key(ob_thread_key_t ob_key)
{
  return TlsGetValue(ob_key);
}

int ob_set_thread_key(ob_thread_key_t ob_key, void *value)
{
  return !TlsSetValue(ob_key, value);
}

#else

int ob_create_thread_key(ob_thread_key_t *ob_key, void (*ob_destructor)(void *))
{
  return pthread_key_create(ob_key, ob_destructor);
}

int ob_delete_thread_key(ob_thread_key_t ob_key)
{
  return pthread_key_delete(ob_key);
}

void *ob_get_thread_key(ob_thread_key_t ob_key)
{
  return pthread_getspecific(ob_key);
}

int ob_set_thread_key(ob_thread_key_t ob_key, void *value)
{
  return pthread_setspecific(ob_key, value);
}

#endif