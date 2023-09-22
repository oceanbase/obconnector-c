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

#include <ma_global.h>
#include <ma_sys.h>            /* Needed for MY_ERRNO_ERANGE */
#include <ma_string.h>
#include <stdlib.h>
#include <errno.h>
#define MY_ERRNO_EDOM   33
#define MY_ERRNO_ERANGE   34

longlong strtoll10(const char *nptr, char **endptr, int *error)
{
  longlong ret = 0;
  char *tmp_endptr = NULL;
  char tmp_endptr_value;
  if (NULL != endptr && NULL != *endptr) {
    tmp_endptr = *endptr;
    tmp_endptr_value = **endptr;
    *tmp_endptr = '\0';
  }
  /*
  * Obclient 2.2.2 changes, obclient 1.x uses my_strtoll10, 2.x uses lib c to provide strtoll after rectification, 
  * the two perform differently on endptr, my_strtoll10 will detect endptr and not convert the bit result, 
  * so set 0 here, Compatible with obclient 1.x
  */
  ret = strtoll(nptr, endptr, 10);
  *error = errno;
  if (NULL != tmp_endptr) {
    *tmp_endptr = tmp_endptr_value;
  }
  return ret;
}