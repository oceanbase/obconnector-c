/****************************************************************************
   Copyright (C) 2012 Monty Program AB
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
   License along with this library; if not see <http://www.gnu.org/licenses>
   or write to the Free Software Foundation, Inc., 
   51 Franklin St., Fifth Floor, Boston, MA 02110, USA

*****************************************************************************/

/* The implementation for prepared statements was ported from PHP's mysqlnd
   extension, written by Andrey Hristov, Georg Richter and Ulf Wendel 

   Original file header:
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 2006-2011 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  |                 so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Authors: Georg Richter <               >                             |
  |          Andrey Hristov <                >                           |
  |          Ulf Wendel <                 >                              |
  +----------------------------------------------------------------------+
*/

#include "ma_global.h"
#include <ma_sys.h>
#include <ma_string.h>
#include <mariadb_ctype.h>
#include "mysql.h"
#include <math.h> /* ceil() */
#include <limits.h>
#include "ob_complex.h"
#ifdef WIN32
#include <malloc.h>
#endif

#define MYSQL_SILENT

/* ranges for C-binding */
#define UINT_MAX32      0xFFFFFFFFL
#define UINT_MAX24      0x00FFFFFF
#define UINT_MAX16      0xFFFF
#ifndef INT_MIN8
#define INT_MIN8        (~0x7F)
#define INT_MAX8        0x7F
#endif
#define UINT_MAX8       0xFF

 #define MAX_DOUBLE_STRING_REP_LENGTH 300
#if defined(HAVE_LONG_LONG) && !defined(LONGLONG_MIN)
#define LONGLONG_MIN    ((long long) 0x8000000000000000LL)
#define LONGLONG_MAX    ((long long) 0x7FFFFFFFFFFFFFFFLL)
#endif

#define MAX_DBL_STR (3 + DBL_MANT_DIG - DBL_MIN_EXP)

#if defined(HAVE_LONG_LONG) && !defined(ULONGLONG_MAX)
/* First check for ANSI C99 definition: */
#ifdef ULLONG_MAX
#define ULONGLONG_MAX  ULLONG_MAX
#else
#define ULONGLONG_MAX ((unsigned long long)(~0ULL))
#endif
#endif /* defined (HAVE_LONG_LONG) && !defined(ULONGLONG_MAX)*/

#define YY_PART_YEAR 70

MYSQL_PS_CONVERSION mysql_ps_fetch_functions[MYSQL_TYPE_GEOMETRY + 2];
my_bool mysql_ps_subsystem_initialized= 0;


#define NUMERIC_TRUNCATION(val,min_range, max_range)\
  ((((val) > (max_range)) || ((val) < (min_range)) ? 1 : 0))


extern ulong calculate_interval_length(uchar *cp, enum_field_types type);
extern int rewrite_interval(uchar *cp, char *to, const uint to_size, ulong *convert_len, enum_field_types type);
extern ulong calculate_new_time_length_with_nls(MYSQL *mysql, uchar *cp, ulong len, enum_field_types type);
extern ulong rewrite_new_time_with_nls(MYSQL *mysql, uchar *cp, ulong len, char *to, int64_t buf_len, enum_field_types type);

void ma_bmove_upp(register char *dst, register const char *src, register size_t len)
{
  while (len-- != 0) *--dst = *--src;
}

/* {{{ ps_fetch_from_1_to_8_bytes */
void ps_fetch_from_1_to_8_bytes(MYSQL_BIND *r_param, const MYSQL_FIELD * const field,
                unsigned char **row, unsigned int byte_count)
{
  my_bool is_unsigned= test(field->flags & UNSIGNED_FLAG);
  r_param->buffer_length= byte_count;
  switch (byte_count) {
    case 1:
      *(uchar *)r_param->buffer= **row;
      *r_param->error= is_unsigned != r_param->is_unsigned && *(uchar *)r_param->buffer > INT_MAX8;
      break;
    case 2:
      shortstore(r_param->buffer, ((ushort) sint2korr(*row)));
      *r_param->error= is_unsigned != r_param->is_unsigned && *(ushort *)r_param->buffer > INT_MAX16;
      break;
    case 4:
    {
      longstore(r_param->buffer, ((uint32)sint4korr(*row)));
      *r_param->error= is_unsigned != r_param->is_unsigned && *(uint32 *)r_param->buffer > INT_MAX32;
    }
    break;
    case 8:
      {
        ulonglong val= (ulonglong)sint8korr(*row);
        longlongstore(r_param->buffer, val);
        *r_param->error= is_unsigned != r_param->is_unsigned && val > LONGLONG_MAX ;
      }
      break;
    default:
      r_param->buffer_length= 0;
      break;
  }
  (*row)+= byte_count;
}
/* }}} */

static unsigned long long my_strtoull(const char *str, size_t len, const char **end, int *err)
{
  unsigned long long val = 0;
  const char *p = str;
  const char *end_str = p + len;

  for (; p < end_str; p++)
  {
    if (*p < '0' || *p > '9')
      break;

    if (val > ULONGLONG_MAX /10 || val*10 > ULONGLONG_MAX - (*p - '0'))
    {
      *err = ERANGE;
      break;
    }
    val = val * 10 + *p -'0';
  }

  if (p == str)
    /* Did not parse anything.*/
    *err = ERANGE;

  *end = p;
  return val;
}

static long long my_strtoll(const char *str, size_t len, const char **end, int *err)
{
  unsigned long long uval = 0;
  const char *p = str;
  const char *end_str = p + len;
  int neg;

  while (p < end_str && isspace(*p))
    p++;

  if (p == end_str)
  {
    *end = p;
    *err = ERANGE;
    return 0;
  }

  neg = *p == '-';
  if (neg)
    p++;

  uval = my_strtoull(p, (end_str - p), &p, err);
  *end = p;
  if (*err)
    return uval;

  if (!neg)
  {
    /* Overflow of the long long range. */
    if (uval > LONGLONG_MAX)
    {
      *end = p - 1;
      uval = LONGLONG_MAX;
      *err = ERANGE;
    }
    return uval;
  }

  if (uval == (unsigned long long) LONGLONG_MIN)
    return LONGLONG_MIN;

  if (uval > LONGLONG_MAX)
  {
    *end = p - 1;
    uval = LONGLONG_MIN;
    *err = ERANGE;
  }

  return -1LL * uval;
}


static long long my_atoll(const char *str, const char *end_str, int *error)
{
  const char *p=str;
  const char *end;
  long long ret;
  while (p < end_str && isspace(*p))
    p++;

  ret = my_strtoll(p, end_str - p, &end, error);

  while(end < end_str && isspace(*end))
   end++;

  if(end != end_str)
    *error= 1;

  return ret;
}


static unsigned long long my_atoull(const char *str, const char *end_str, int *error)
{
  const char *p = str;
  const char *end;
  unsigned long long ret;

  while (p < end_str && isspace(*p))
    p++;

  ret = my_strtoull(p, end_str - p, &end, error);

  while(end < end_str && isspace(*end))
   end++;

  if(end != end_str)
    *error= 1;

  return ret;
}

double my_atod(const char *number, const char *end, int *error)
{
  double val= 0.0;
  char buffer[MAX_DBL_STR + 1];
  int len= (int)(end - number);

  *error= errno= 0;

  if (len > MAX_DBL_STR)
  {
    *error= 1;
    len= MAX_DBL_STR;
  }

  memcpy(buffer, number, len);
  buffer[len]= '\0';

  val= strtod(buffer, NULL);

  if (errno)
    *error= errno;

  return val;
}


/*
  strtoui() version, that works for non-null terminated strings
*/
static unsigned int my_strtoui(const char *str, size_t len, const char **end, int *err)
{
  unsigned long long ull = my_strtoull(str, len, end, err);
  if (ull > UINT_MAX)
    *err = ERANGE;
  return (unsigned int)ull;
}

/*
  Parse time, in MySQL format.

  the input string needs is in form "hour:minute:second[.fraction]"
  hour, minute and second can have leading zeroes or not,
  they are not necessarily 2 chars.

  Hour must be < 838, minute < 60, second < 60
  Only 6 places of fraction are considered, the value is truncated after 6 places.
*/
static const unsigned int frac_mul[] = { 1000000,100000,10000,1000,100,10 };

static int parse_time(const char *str, size_t length, const char **end_ptr, MYSQL_TIME *tm)
{
  int err= 0;
  const char *p = str;
  const char *end = str + length;
  size_t frac_len;
  int ret=1;

  tm->hour = my_strtoui(p, end-p, &p, &err);
  if (err || tm->hour > 838 || p == end || *p != ':' )
    goto end;

  p++;
  tm->minute = my_strtoui(p, end-p, &p, &err);
  if (err || tm->minute > 59 || p == end || *p != ':')
    goto end;

  p++;
  tm->second = my_strtoui(p, end-p, &p, &err);
  if (err || tm->second > 59)
    goto end;

  ret = 0;
  tm->second_part = 0;

  if (p == end)
    goto end;

  /* Check for fractional part*/
  if (*p != '.')
    goto end;

  p++;
  frac_len = MIN(6,end-p);

  tm->second_part = my_strtoui(p, frac_len, &p, &err);
  if (err)
    goto end;

  if (frac_len < 6)
    tm->second_part *= frac_mul[frac_len];

  ret = 0;

  /* Consume whole fractional part, even after 6 digits.*/
  p += frac_len;
  while(p < *end_ptr)
  {
    if (*p < '0' || *p > '9')
      break;
    p++;
  }
end:
  *end_ptr = p;
  return ret;
}


/*
  Parse date, in MySQL format.

  The input string needs is in form "year-month-day"
  year, month and day can have leading zeroes or not,
  they do not have fixed length.

  Year must be < 10000, month < 12, day < 32

  Years with 2 digits, are converted to values 1970-2069 according to
  usual rules:

  00-69 is converted to 2000-2069.
  70-99 is converted to 1970-1999.
*/
static int parse_date(const char *str, size_t length, const char **end_ptr, MYSQL_TIME *tm)
{
  int err = 0;
  const char *p = str;
  const char *end = str + length;
  int ret = 1;

  tm->year = my_strtoui(p, end - p, &p, &err);
  if (err || tm->year > 9999 || p == end || *p != '-')
    goto end;

  if (p - str == 2) // 2-digit year
    tm->year += (tm->year >= 70) ? 1900 : 2000;

  p++;
  tm->month = my_strtoui(p,end -p, &p, &err);
  if (err || tm->month > 12 || p == end || *p != '-')
    goto end;

  p++;
  tm->day = my_strtoui(p, end -p , &p, &err);
  if (err || tm->day > 31)
    goto end;

  ret = 0;

end:
  *end_ptr = p;
  return ret;
}

/*
  Parse (not null terminated) string representing 
  TIME, DATE, or DATETIME into MYSQL_TIME structure

  The supported formats by this functions are
  - TIME : [-]hours:minutes:seconds[.fraction]
  - DATE : year-month-day
  - DATETIME : year-month-day<space>hours:minutes:seconds[.fraction]

  cf https://dev.mysql.com/doc/refman/8.0/en/datetime.html

  Whitespaces are trimmed from the start and end of the string.
  The function ignores junk at the end of the string.

  Parts of date of time do not have fixed length, so that parsing is compatible with server.
  However server supports additional formats, e.g YYYYMMDD, HHMMSS, which this function does
  not support.

*/
int str_to_TIME(const char *str, size_t length, MYSQL_TIME *tm)
{
  const char *p = str;
  const char *end = str + length;
  int is_time = 0;

  if (!p)
    goto error;

  while (p < end && isspace(*p))
    p++;
  while (p < end && isspace(end[-1]))
    end--;

  if (end -p < 5)
    goto error;

  if (*p == '-')
  {
    tm->neg = 1;
    /* Only TIME can't be negative.*/
    is_time = 1;
    p++;
  }
  else
  {
    int i;
    tm->neg = 0;
    /*
      Date parsing (in server) accepts leading zeroes, thus position of the delimiters
      is not fixed. Scan the string to find out what we need to parse.
    */
    for (i = 1; p + i < end; i++)
    {
      if(p[i] == '-' || p [i] == ':')
      {
        is_time = p[i] == ':';
        break;
      }
    }
  }

  if (is_time)
  {
    if (parse_time(p, end - p, &p, tm))
      goto error;
    
    tm->year = tm->month = tm->day = 0;
    tm->time_type = MYSQL_TIMESTAMP_TIME;
    return 0;
  }

  if (parse_date(p, end - p, &p, tm))
    goto error;

  if (p == end || p[0] != ' ')
  {
    tm->hour = tm->minute = tm->second = tm->second_part = 0;
    tm->time_type = MYSQL_TIMESTAMP_DATE;
    return 0;
  }

  /* Skip space. */
  p++;
  if (parse_time(p, end - p, &p, tm))
    goto error;

  /* In DATETIME, hours must be < 24.*/
  if (tm->hour > 23)
   goto error;

  tm->time_type = MYSQL_TIMESTAMP_DATETIME;
  return 0;

error:
  memset(tm, 0, sizeof(*tm));
  tm->time_type = MYSQL_TIMESTAMP_ERROR;
  return 1;
}


static void convert_froma_string(MYSQL_BIND *r_param, char *buffer, size_t len)
{
  int error= 0;
  switch (r_param->buffer_type)
  {
    case MYSQL_TYPE_TINY:
    {
      longlong val= my_atoll(buffer, buffer + len, &error);
      *r_param->error= error ? 1 : r_param->is_unsigned ? NUMERIC_TRUNCATION(val, 0, UINT_MAX8) : NUMERIC_TRUNCATION(val, INT_MIN8, INT_MAX8) || error > 0;
      int1store(r_param->buffer, (uchar) val);
      r_param->buffer_length= sizeof(uchar);
    }
    break;
    case MYSQL_TYPE_YEAR:
    case MYSQL_TYPE_SHORT:
    {
      longlong val= my_atoll(buffer, buffer + len, &error);
      *r_param->error= error ? 1 : r_param->is_unsigned ? NUMERIC_TRUNCATION(val, 0, UINT_MAX16) : NUMERIC_TRUNCATION(val, INT_MIN16, INT_MAX16) || error > 0;
      shortstore(r_param->buffer, (short)val);
      r_param->buffer_length= sizeof(short);
    }
    break;
    case MYSQL_TYPE_LONG:
    case MYSQL_TYPE_CURSOR:
    {
      longlong val= my_atoll(buffer, buffer + len, &error);
      *r_param->error=error ? 1 : r_param->is_unsigned ? NUMERIC_TRUNCATION(val, 0, UINT_MAX32) : NUMERIC_TRUNCATION(val, INT_MIN32, INT_MAX32) || error > 0;
      longstore(r_param->buffer, (int32)val);
      r_param->buffer_length= sizeof(uint32);
    }
    break;
    case MYSQL_TYPE_LONGLONG:
    {
      longlong val= r_param->is_unsigned ? (longlong)my_atoull(buffer, buffer + len, &error) : my_atoll(buffer, buffer + len, &error);
      *r_param->error= error > 0; /* no need to check for truncation */
      longlongstore(r_param->buffer, val);
      r_param->buffer_length= sizeof(longlong);
    }
    break;
    case MYSQL_TYPE_DOUBLE:
    {
      double val= my_atod(buffer, buffer + len, &error);
      *r_param->error= error > 0; /* no need to check for truncation */
      doublestore((uchar *)r_param->buffer, val);
      r_param->buffer_length= sizeof(double);
    }
    break;
    case MYSQL_TYPE_FLOAT:
    {
      float val= (float)my_atod(buffer, buffer + len, &error);
      *r_param->error= error > 0; /* no need to check for truncation */
      floatstore((uchar *)r_param->buffer, val);
      r_param->buffer_length= sizeof(float);
    }
    break;
    case MYSQL_TYPE_TIME:
    case MYSQL_TYPE_DATE:
    case MYSQL_TYPE_DATETIME:
    case MYSQL_TYPE_TIMESTAMP:
    {
      MYSQL_TIME *tm= (MYSQL_TIME *)r_param->buffer;
      str_to_TIME(buffer, len, tm);
      break;
    }
    break;
    case MYSQL_TYPE_TINY_BLOB:
    case MYSQL_TYPE_MEDIUM_BLOB:
    case MYSQL_TYPE_LONG_BLOB:
    case MYSQL_TYPE_BLOB:
    case MYSQL_TYPE_DECIMAL:
    case MYSQL_TYPE_NEWDECIMAL:
    case MYSQL_TYPE_OB_UROWID:
    default:
    {
      if (len >= r_param->offset)
      {
        char *start= buffer + r_param->offset; /* stmt_fetch_column sets offset */
        char *end= buffer + len;
        size_t copylen= 0;

        if (start < end)
        {
          copylen= end - start;
          if (r_param->buffer_length)
            memcpy(r_param->buffer, start, MIN(copylen, r_param->buffer_length));
        }
        if (copylen < r_param->buffer_length)
          ((char *)r_param->buffer)[copylen]= 0;
        *r_param->error= (copylen > r_param->buffer_length);

      }
      *r_param->length= (ulong)len;
    }
    break;
  }
}

static void convert_from_long(MYSQL_BIND *r_param, const MYSQL_FIELD *field, longlong val, my_bool is_unsigned)
{
  switch (r_param->buffer_type) {
    case MYSQL_TYPE_TINY:
      *(uchar *)r_param->buffer= (uchar)val;
      *r_param->error= r_param->is_unsigned ? NUMERIC_TRUNCATION(val, 0, UINT_MAX8) : NUMERIC_TRUNCATION(val, INT_MIN8, INT_MAX8);
      r_param->buffer_length= 1;
      break;
    case MYSQL_TYPE_SHORT:
    case MYSQL_TYPE_YEAR:
      shortstore(r_param->buffer, (short)val);
      *r_param->error= r_param->is_unsigned ? NUMERIC_TRUNCATION(val, 0, UINT_MAX16) : NUMERIC_TRUNCATION(val, INT_MIN16, INT_MAX16);
      r_param->buffer_length= 2;
      break;
    case MYSQL_TYPE_LONG:
    case MYSQL_TYPE_CURSOR:
      longstore(r_param->buffer, (int32)val);
      *r_param->error= r_param->is_unsigned ? NUMERIC_TRUNCATION(val, 0, UINT_MAX32) : NUMERIC_TRUNCATION(val, INT_MIN32, INT_MAX32);
      r_param->buffer_length= 4;
      break;
    case MYSQL_TYPE_LONGLONG:
      *r_param->error= (val < 0 && r_param->is_unsigned != is_unsigned);
      longlongstore(r_param->buffer, val);
      r_param->buffer_length= 8;
      break;
    case MYSQL_TYPE_DOUBLE:
    {
      volatile double dbl;

      dbl= (is_unsigned) ? ulonglong2double((ulonglong)val) : (double)val;
      doublestore(r_param->buffer, dbl);

      *r_param->error = (dbl != ceil(dbl)) ||
                         (is_unsigned ? (ulonglong )dbl != (ulonglong)val : 
                                        (longlong)dbl != (longlong)val);

      r_param->buffer_length= 8;
      break;
    }
    case MYSQL_TYPE_FLOAT:
    {
      volatile float fval;
      fval= is_unsigned ? (float)(ulonglong)(val) : (float)val;
      floatstore((uchar *)r_param->buffer, fval);
      *r_param->error= (fval != ceilf(fval)) ||
                        (is_unsigned ? (ulonglong)fval != (ulonglong)val : 
                                       (longlong)fval != val);
      r_param->buffer_length= 4;
    }
    break;
    default:
    {
      char *buffer;
      char *endptr;
      uint len;
      my_bool zf_truncated= 0;

      buffer= alloca(MAX(field->length, 22));
      endptr= ma_ll2str(val, buffer, is_unsigned ? 10 : -10);
      len= (uint)(endptr - buffer);

      /* check if field flag is zerofill */
      if (field->flags & ZEROFILL_FLAG)
      {
        uint display_width= MAX(field->length, len);
        if (display_width < r_param->buffer_length)
        {
          ma_bmove_upp(buffer + display_width, buffer + len, len);
          /* coverity[bad_memset] */
          memset((void*) buffer, (int) '0', display_width - len);
          len= display_width;
        }
        else
          zf_truncated= 1;
      }
      convert_froma_string(r_param, buffer, len);
      *r_param->error+= zf_truncated;
    }
    break;
  }
}


/* {{{ ps_fetch_null */
static
void ps_fetch_null(MYSQL_BIND *r_param __attribute__((unused)),
                   const MYSQL_FIELD * field __attribute__((unused)),
                   unsigned char **row __attribute__((unused)))
{
  /* do nothing */
}
/* }}} */

#define GET_LVALUE_FROM_ROW(is_unsigned, data, ucast, scast)\
  (is_unsigned) ? (longlong)(ucast) *(longlong *)(data) : (longlong)(scast) *(longlong *)(data) 
/* {{{ ps_fetch_int8 */
static
void ps_fetch_int8(MYSQL_BIND *r_param, const MYSQL_FIELD * const field,
           unsigned char **row)
{
  switch(r_param->buffer_type) {
    case MYSQL_TYPE_TINY:
      ps_fetch_from_1_to_8_bytes(r_param, field, row, 1);
      break;
    default:
    {
      uchar val= **row;
      longlong lval= field->flags & UNSIGNED_FLAG ? (longlong) val : (longlong)(signed char)val;
      convert_from_long(r_param, field, lval, field->flags & UNSIGNED_FLAG);
      (*row) += 1;
    }
    break;
  }
}
/* }}} */


/* {{{ ps_fetch_int16 */
static
void ps_fetch_int16(MYSQL_BIND *r_param, const MYSQL_FIELD * const field,
           unsigned char **row)
{
  switch (r_param->buffer_type) {
    case MYSQL_TYPE_YEAR:
    case MYSQL_TYPE_SHORT:
      ps_fetch_from_1_to_8_bytes(r_param, field, row, 2);
    break;
    default:
    {
      short sval= sint2korr(*row);
      longlong lval= field->flags & UNSIGNED_FLAG ? (longlong)(ushort) sval : (longlong)sval;
      convert_from_long(r_param, field, lval, field->flags & UNSIGNED_FLAG);
      (*row) += 2;
    }
    break;
  }
}
/* }}} */


/* {{{ ps_fetch_int32 */
static
void ps_fetch_int32(MYSQL_BIND *r_param, const MYSQL_FIELD * const field,
           unsigned char **row)
{
  switch (r_param->buffer_type) {
/*    case MYSQL_TYPE_TINY:
      ps_fetch_from_1_to_8_bytes(r_param, field, row, 1);
      break;
    case MYSQL_TYPE_YEAR:
    case MYSQL_TYPE_SHORT:
      ps_fetch_from_1_to_8_bytes(r_param, field, row, 2);
      break; */
    case MYSQL_TYPE_INT24:
    case MYSQL_TYPE_LONG:
    case MYSQL_TYPE_CURSOR:
      ps_fetch_from_1_to_8_bytes(r_param, field, row, 4);
    break; 
    default:
    {
      int32 sval= sint4korr(*row);
      longlong lval= field->flags & UNSIGNED_FLAG ? (longlong)(uint32) sval : (longlong)sval;
      convert_from_long(r_param, field, lval, field->flags & UNSIGNED_FLAG);
      (*row) += 4;
    }
    break;
  }
}
/* }}} */


/* {{{ ps_fetch_int64 */
static
void ps_fetch_int64(MYSQL_BIND *r_param, const MYSQL_FIELD * const field,
           unsigned char **row)
{
  switch(r_param->buffer_type)
  {
/*    case MYSQL_TYPE_TINY:
      ps_fetch_from_1_to_8_bytes(r_param, field, row, 1);
      break;
    case MYSQL_TYPE_YEAR:
    case MYSQL_TYPE_SHORT:
      ps_fetch_from_1_to_8_bytes(r_param, field, row, 2);
      break;
    case MYSQL_TYPE_INT24:
    case MYSQL_TYPE_LONG:
      ps_fetch_from_1_to_8_bytes(r_param, field, row, 4);
      break; */
    case MYSQL_TYPE_LONGLONG:
      ps_fetch_from_1_to_8_bytes(r_param, field, row, 8);
    break;
    default:
    {
      longlong sval= (longlong)sint8korr(*row);
      longlong lval= field->flags & UNSIGNED_FLAG ? (longlong)(ulonglong) sval : (longlong)sval;
      convert_from_long(r_param, field, lval, field->flags & UNSIGNED_FLAG);
      (*row) += 8;
    }
    break;
  }
}
/* }}} */

static void convert_from_float(MYSQL_BIND *r_param, const MYSQL_FIELD *field, float val, int size __attribute__((unused)))
{
  double check_trunc_val= (val > 0) ? floor(val) : -floor(-val);
  char *buf= (char *)r_param->buffer;
  switch (r_param->buffer_type)
  {
    case MYSQL_TYPE_TINY:
      *buf= (r_param->is_unsigned) ? (uint8)val : (int8)val;
      *r_param->error= check_trunc_val != (r_param->is_unsigned ? (double)((uint8)*buf) :
                                          (double)((int8)*buf));
      r_param->buffer_length= 1;
    break;
    case MYSQL_TYPE_SHORT:
    case MYSQL_TYPE_YEAR:
    {
      if (r_param->is_unsigned)
      {
        ushort sval= (ushort)val;
        shortstore(buf, sval);
        *r_param->error= check_trunc_val != (double)sval;
      } else { 
        short sval= (short)val;
        shortstore(buf, sval);
        *r_param->error= check_trunc_val != (double)sval;
      } 
      r_param->buffer_length= 2;
    }
    break; 
    case MYSQL_TYPE_LONG:
    case MYSQL_TYPE_CURSOR:
    {
      if (r_param->is_unsigned)
      {
        uint32 lval= (uint32)val;
        longstore(buf, lval);
        *r_param->error= (check_trunc_val != (double)lval);
      } else {
        int32 lval= (int32)val;
        longstore(buf, lval);
        *r_param->error= (check_trunc_val != (double)lval);
      }
      r_param->buffer_length= 4;
    }
    break; 
    case MYSQL_TYPE_LONGLONG:
    {
      if (r_param->is_unsigned)
      {
        ulonglong llval= (ulonglong)val;
        longlongstore(buf, llval);
        *r_param->error= (check_trunc_val != (double)llval);
      } else {
        longlong llval= (longlong)val;
        longlongstore(buf, llval);
        *r_param->error= (check_trunc_val != (double)llval);
      }
      r_param->buffer_length= 8;
    }
    break; 
    case MYSQL_TYPE_DOUBLE:
    {
      double dval= (double)val;
      memcpy(buf, &dval, sizeof(double));
      r_param->buffer_length= 8;
    }
    break;
    default:
    {
      char buff[MAX_DOUBLE_STRING_REP_LENGTH];
      size_t length;

      length= MIN(MAX_DOUBLE_STRING_REP_LENGTH - 1, r_param->buffer_length);

      if (isnan(val)) {
        snprintf(buff, length, "%s", "Nan");
        length = 3;
      } else if (isinf(val)) {
        if (val > 0) {
          snprintf(buff, length, "%s", "Inf");
          length = 3;
        } else {
          snprintf(buff, length, "%s", "-Inf");
          length = 4;
        }
      } else {
        if (field->decimals >= NOT_FIXED_DEC)
        {
          length = ma_gcvt(val, MY_GCVT_ARG_FLOAT, (int)length, buff, NULL);
        }
        else
        {
          length = ma_fcvt(val, field->decimals, buff, NULL);
        }
      }

      /* check if ZEROFILL flag is active */
      if (field->flags & ZEROFILL_FLAG)
      {
        /* enough space available ? */
        if (field->length < length || field->length > MAX_DOUBLE_STRING_REP_LENGTH - 1)
          break;
        ma_bmove_upp(buff + field->length, buff + length, length);
        /* coverity[bad_memset] */
        memset((void*) buff, (int) '0', field->length - length);
        length= field->length;
      }

      convert_froma_string(r_param, buff, length);
    }  
    break;
  } 
}

static void convert_from_double(MYSQL_BIND *r_param, const MYSQL_FIELD *field, double val, int size __attribute__((unused)))
{
  double check_trunc_val= (val > 0) ? floor(val) : -floor(-val);
  char *buf= (char *)r_param->buffer;
  switch (r_param->buffer_type)
  {
    case MYSQL_TYPE_TINY:
      *buf= (r_param->is_unsigned) ? (uint8)val : (int8)val;
      *r_param->error= check_trunc_val != (r_param->is_unsigned ? (double)((uint8)*buf) :
                                          (double)((int8)*buf));
      r_param->buffer_length= 1;
    break;
    case MYSQL_TYPE_SHORT:
    case MYSQL_TYPE_YEAR:
    {
      if (r_param->is_unsigned)
      {
        ushort sval= (ushort)val;
        shortstore(buf, sval);
        *r_param->error= check_trunc_val != (double)sval;
      } else { 
        short sval= (short)val;
        shortstore(buf, sval);
        *r_param->error= check_trunc_val != (double)sval;
      } 
      r_param->buffer_length= 2;
    }
    break; 
    case MYSQL_TYPE_LONG:
    case MYSQL_TYPE_CURSOR:
    {
      if (r_param->is_unsigned)
      {
        uint32 lval= (uint32)val;
        longstore(buf, lval);
        *r_param->error= (check_trunc_val != (double)lval);
      } else {
        int32 lval= (int32)val;
        longstore(buf, lval);
        *r_param->error= (check_trunc_val != (double)lval);
      }
      r_param->buffer_length= 4;
    }
    break; 
    case MYSQL_TYPE_LONGLONG:
    {
      if (r_param->is_unsigned)
      {
        ulonglong llval= (ulonglong)val;
        longlongstore(buf, llval);
        *r_param->error= (check_trunc_val != (double)llval);
      } else {
        longlong llval= (longlong)val;
        longlongstore(buf, llval);
        *r_param->error= (check_trunc_val != (double)llval);
      }
      r_param->buffer_length= 8;
    }
    break; 
    case MYSQL_TYPE_FLOAT:
    {
      float fval= (float)val;
      memcpy(buf, &fval, sizeof(float));
      *r_param->error= (*(float*)buf != fval);
      r_param->buffer_length= 4;
    }
    break;
    default:
    {
     char buff[MAX_DOUBLE_STRING_REP_LENGTH];
     size_t length;

     length= MIN(MAX_DOUBLE_STRING_REP_LENGTH - 1, r_param->buffer_length);

     if (isnan(val)) {
       snprintf(buff, length, "%s", "Nan");
       length = 3;
     } else if (isinf(val)) {
       if (val > 0) {
         snprintf(buff, length, "%s", "Inf");
         length = 3;
       } else {
         snprintf(buff, length, "%s", "-Inf");
         length = 4;
       }
     } else {
       if (field->decimals >= NOT_FIXED_DEC)
       {
         length = ma_gcvt(val, MY_GCVT_ARG_DOUBLE, (int)length, buff, NULL);
       }
       else
       {
         length = ma_fcvt(val, field->decimals, buff, NULL);
       }
     }

     /* check if ZEROFILL flag is active */
     if (field->flags & ZEROFILL_FLAG)
     {
       /* enough space available ? */
       if (field->length < length || field->length > MAX_DOUBLE_STRING_REP_LENGTH - 1)
         break;
       ma_bmove_upp(buff + field->length, buff + length, length);
       /* coverity [bad_memset] */
       memset((void*) buff, (int) '0', field->length - length);
       length= field->length;
     }
     convert_froma_string(r_param, buff, length);
    } 
    break;
  } 
}


/* {{{ ps_fetch_double */
static
void ps_fetch_double(MYSQL_BIND *r_param, const MYSQL_FIELD * field , unsigned char **row)
{
  switch (r_param->buffer_type)
  {
    case MYSQL_TYPE_DOUBLE:
    {
      double *value= (double *)r_param->buffer;
      float8get(*value, *row);
      r_param->buffer_length= 8;
    }
    break;
    default:
    {
      double value;
      float8get(value, *row);
      convert_from_double(r_param, field, value, sizeof(double));
    }
    break;
  }
  (*row)+= 8;
}
/* }}} */

/* {{{ ps_fetch_float */
static
void ps_fetch_float(MYSQL_BIND *r_param, const MYSQL_FIELD * field, unsigned char **row)
{
  switch(r_param->buffer_type)
  {
    case MYSQL_TYPE_FLOAT:
    {
      float *value= (float *)r_param->buffer;
      float4get(*value, *row);
      r_param->buffer_length= 4;
      *r_param->error= 0;
    }
    break;
    default:
    {
      float value;
      memcpy(&value, *row, sizeof(float));
      float4get(value, (char *)*row);
      convert_from_float(r_param, field, value, sizeof(float));
    }
    break;
  }
  (*row)+= 4;
}
/* }}} */

static void convert_to_datetime(MYSQL_TIME *t, unsigned char **row, uint len, enum enum_field_types type)
{
  memset(t, 0, sizeof(MYSQL_TIME));

  /* binary protocol for datetime:
     4-bytes:  DATE
     7-bytes:  DATE + TIME
     >7 bytes: DATE + TIME with second_part
  */
  if (len)
  {
    unsigned char *to= *row;
    int has_date= 0;
    uint offset= 7;
    
    if (type == MYSQL_TYPE_TIME)
    {
      t->neg= to[0];
      t->day= (ulong) sint4korr(to + 1);
      t->time_type= MYSQL_TIMESTAMP_TIME;
      offset= 8;
      to++;
    } else
    {
      t->year= (uint) sint2korr(to);
      t->month= (uint) to[2];
      t->day= (uint) to[3];
      t->time_type= MYSQL_TIMESTAMP_DATE;
      if (type == MYSQL_TYPE_DATE)
        return;
      has_date= 1;
    }

    if (len > 4)
    {
      t->hour= (uint) to[4];
      if (type == MYSQL_TYPE_TIME)
        t->hour+= t->day * 24;
      t->minute= (uint) to[5];
      t->second= (uint) to[6];
      if (has_date)
        t->time_type= MYSQL_TIMESTAMP_DATETIME;
    }
    if (len > offset)
    {
      t->second_part= (ulong)sint4korr(to+7);
    }
  }
}

/*
 * ob oracle extened type
 */
static
void ps_fetch_oracle_timestamp(MYSQL_BIND *param,
                               const MYSQL_FIELD *field,
                               uchar **row)
{
  uint buffer_length = 0;
  uint length = net_field_length(row);

  switch (param->buffer_type) {
  case MYSQL_TYPE_OB_TIMESTAMP_NANO:
  case MYSQL_TYPE_OB_TIMESTAMP_WITH_LOCAL_TIME_ZONE:
  case MYSQL_TYPE_OB_TIMESTAMP_WITH_TIME_ZONE: {
    if (length > 16) {
      buffer_length = length - 16 + sizeof(ORACLE_TIME) + 2;
    } else {
      buffer_length = sizeof(ORACLE_TIME);
    }

    if (param->buffer_length < buffer_length) {
      *param->length = buffer_length;
      *param->error = 1;
      *row += length;
    } else {
      ORACLE_TIME *tm = (ORACLE_TIME *)param->buffer;
      uint tz_length = 0;
      uint buffer_offset = 0;
      uchar *to = *row;

      memset(tm, 0, sizeof(ORACLE_TIME));
      tm->century = (int)(*(char*)to++);
      tm->year = (int)(*(char*)to++);
      tm->month = (uint)(*to++);
      tm->day = (uint)(*to++);
      tm->hour = (uint)(*to++);
      tm->minute = (uint)(*to++);
      tm->second = (uint)(*to++);

      tm->second_part = (ulong)sint4korr(to);
      to += 4;
      tm->scale = (uint)(*to++);

      buffer_length = buffer_offset = sizeof(ORACLE_TIME);
      if (length > 12) {
        tm->offset_hour = (int)(*(char*)to++);
        tm->offset_minute = (int)(*(char*)to++);

        tz_length = (uint)(*to++);
        buffer_length += (tz_length + 1);
        if (tz_length > 0 && buffer_offset + tz_length + 1 < param->buffer_length) {
          memcpy((char*)param->buffer + buffer_offset, to, tz_length);
          tm->tz_name = (char*)param->buffer + buffer_offset;
          buffer_offset += tz_length;
          *((char*)param->buffer + buffer_offset) = '\0';
          buffer_offset++;
        }
        to += tz_length;

        tz_length = (uint)(*to++);
        buffer_length += (tz_length + 1);
        if (tz_length > 0 && buffer_offset + tz_length + 1 < param->buffer_length) {
          memcpy((char*)param->buffer + buffer_offset, to, tz_length);
          tm->tz_abbr = (char*)param->buffer + buffer_offset;
          buffer_offset += tz_length;
          *((char*)param->buffer + buffer_offset) = '\0';
          buffer_offset++;
        }
        to += tz_length;
      }
      *param->length = buffer_length;
      *param->error = param->buffer_length < buffer_length;
      *row = to;
    }
    break;
  }
  case MYSQL_TYPE_VAR_STRING:
  case MYSQL_TYPE_STRING:{
    uchar *buffer = *row;
    ulong convert_len = calculate_new_time_length_with_nls(param->mysql, buffer, length, field->type);
    *param->length = convert_len;
    if (param->buffer_length < convert_len) {
      *param->error = 1;
    } else {
      convert_len = rewrite_new_time_with_nls(param->mysql, buffer, length, param->buffer, (uint)(param->buffer_length), field->type);
      *param->length = convert_len;
    }
    *row += length;
    break;
  }
  default: {
    convert_froma_string(param, (char *)*row, length);
    *row += length;
    break;
  }
  }
}

static
void ps_fetch_oracle_interval(MYSQL_BIND *param, const MYSQL_FIELD *field, uchar **row)
{
  uint length = net_field_length(row);
  uchar * buffer = *row;

  switch (param->buffer_type)
  {
  case MYSQL_TYPE_OB_INTERVAL_DS: {
    *param->length = sizeof(ORACLE_INTERVAL);
    if (param->buffer_length < sizeof(ORACLE_INTERVAL) || length != 14) {
      *param->error = 1;
    } else {
      ORACLE_INTERVAL * interval = (ORACLE_INTERVAL*)param->buffer;
      interval->mysql_type = MYSQL_TYPE_OB_INTERVAL_DS;
      interval->data_symbol = (buffer[0] > 0 ? -1 : 1);
      interval->data_object.ds_object.ds_day = sint4korr(buffer + 1);
      interval->data_object.ds_object.ds_hour = buffer[5];
      interval->data_object.ds_object.ds_minute = buffer[6];
      interval->data_object.ds_object.ds_second = buffer[7];
      interval->data_object.ds_object.ds_frac_second = sint4korr(buffer + 8);
      interval->data_object.ds_object.ds_day_scale = buffer[12];
      interval->data_object.ds_object.ds_frac_second_scale = buffer[13];
    }
    break;
  }
  case MYSQL_TYPE_OB_INTERVAL_YM: {
    *param->length = sizeof(ORACLE_INTERVAL);
    if (param->buffer_length < sizeof(ORACLE_INTERVAL) || length != 7) {
      *param->error = 1;
    } else {
      ORACLE_INTERVAL * interval = (ORACLE_INTERVAL*)param->buffer;
      interval->mysql_type = MYSQL_TYPE_OB_INTERVAL_YM;
      interval->data_symbol = (buffer[0] > 0 ? -1 : 1);
      interval->data_object.ym_object.ym_year = sint4korr(buffer + 1);
      interval->data_object.ym_object.ym_month = buffer[5];
      interval->data_object.ym_object.ym_scale = buffer[6];
    }
    break;
  }
  case MYSQL_TYPE_VAR_STRING:
  case MYSQL_TYPE_STRING: {
    ulong convert_len = calculate_interval_length(buffer, field->type);
    *param->length = convert_len;
    if ((length != 7 && length != 14) || param->buffer_length < convert_len) {
      *param->error = 1;
    } else {
      if (!rewrite_interval(buffer, param->buffer, (uint)(param->buffer_length), &convert_len, field->type)) {
        *param->error = 1;
      }
      *param->length = convert_len;
    }
    break;
  }
  default: {
    convert_froma_string(param, (char *)*row, length);
    break;
  }
  }

  *row += length;
}

static
void ps_fetch_oracle_raw(MYSQL_BIND *param, const MYSQL_FIELD *field, uchar **row)
{
  ulong length = net_field_length(row);
  uchar * buffer = *row;
  UNUSED(field);

  switch (param->buffer_type)
  {
  case MYSQL_TYPE_OB_RAW: {
    convert_froma_string(param, (char *)*row, length);
    break;
  }
  case MYSQL_TYPE_VAR_STRING:
  case MYSQL_TYPE_STRING:{
    uchar *to = param->buffer;
    uchar *cp = buffer;
    uchar *end = buffer + length;
    if (param->buffer_length < length * 2) {
      *param->error = 1;
    } else {
      for (; cp < end; cp++, to += 2) {
        sprintf((char *)to, "%02X", *((uchar*)cp));
      }
      (*to++) = 0;
    }
    *param->length = length * 2;
    break;
  }
  default: {
    convert_froma_string(param, (char *)*row, length);
    break;
  }
  }
  *row += length;
}


/* {{{ ps_fetch_datetime */
static
void ps_fetch_datetime(MYSQL_BIND *r_param, const MYSQL_FIELD * field,
                       unsigned char **row)
{
  MYSQL_TIME *t= (MYSQL_TIME *)r_param->buffer;
  unsigned int len= net_field_length(row);

  switch (r_param->buffer_type) {
    case MYSQL_TYPE_DATETIME:
    case MYSQL_TYPE_TIMESTAMP:
      convert_to_datetime(t, row, len, field->type);
      break;
    case MYSQL_TYPE_DATE:
      convert_to_datetime(t, row, len, field->type);
      break;
    case MYSQL_TYPE_TIME:
      convert_to_datetime(t, row, len, field->type);
      t->year= t->day= t->month= 0;
      break;
    case MYSQL_TYPE_YEAR:
    {
      MYSQL_TIME tm;
      convert_to_datetime(&tm, row, len, field->type);
      shortstore(r_param->buffer, tm.year);
      break;
    }
    default: 
    {
      char dtbuffer[60];
      MYSQL_TIME tm;
      size_t length;
      convert_to_datetime(&tm, row, len, field->type);

      switch(field->type) {
      case MYSQL_TYPE_DATE:
        length= sprintf(dtbuffer, "%04u-%02u-%02u", tm.year, tm.month, tm.day);
        break;
      case MYSQL_TYPE_TIME:
        length= sprintf(dtbuffer, "%s%02u:%02u:%02u", (tm.neg ? "-" : ""), tm.hour, tm.minute, tm.second);
        if (field->decimals && field->decimals <= 6)
        {
          char ms[8];
          sprintf(ms, ".%06lu", tm.second_part);
          if (field->decimals < 6)
            ms[field->decimals + 1]= 0;
          length+= strlen(ms);
          strcat(dtbuffer, ms);
        }
        break;
      case MYSQL_TYPE_DATETIME:
      case MYSQL_TYPE_TIMESTAMP:
        length= sprintf(dtbuffer, "%04u-%02u-%02u %02u:%02u:%02u", tm.year, tm.month, tm.day, tm.hour, tm.minute, tm.second);
        if (field->decimals && field->decimals <= 6)
        {
          char ms[8];
          sprintf(ms, ".%06lu", tm.second_part);
          if (field->decimals < 6)
            ms[field->decimals + 1]= 0;
          length+= strlen(ms);
          strcat(dtbuffer, ms);
        }
        break;
      default:
        dtbuffer[0]= 0;
        length= 0;
        break;
      }
      convert_froma_string(r_param, dtbuffer, length);
      break;
    }
  }
  (*row) += len;
}
/* }}} */

/*fetch ob_lob*/
static void fill_ob_lob_locator(OB_LOB_LOCATOR *ob_lob_locator, uchar *row)
{
  ob_lob_locator->magic_code_ = uint4korr(row);
  ob_lob_locator->version_ = uint4korr(row + 4);
  ob_lob_locator->snapshot_version_ = sint8korr(row + 8);
  ob_lob_locator->table_id_ = uint8korr(row + 16);
  ob_lob_locator->column_id_ = uint4korr(row + 24);
  ob_lob_locator->mode_ = uint2korr(row + 28);
  ob_lob_locator->option_ = uint2korr(row + 30);
  ob_lob_locator->payload_offset_ = uint4korr(row + 32);
  ob_lob_locator->payload_size_ = uint4korr(row + 36);
}


static void fill_ob_client_mem_lob_common(ObClientMemLobCommon* common, uchar *row)
{
#define BIT1 0x01
#define BIT4 0x0F
#define BIT8 0xFF
#define BIT15 0x7FFF
  uint32_t tmps = 0;
  common->magic_ = uint4korr(row);
  tmps = uint4korr(row + 4);
  common->version_ = tmps & BIT8;
  tmps >>= 8;
  common->type_ = tmps & BIT4;
  tmps >>= 4;
  common->read_only_ = tmps & BIT1;
  tmps >>= 1;
  common->is_inrow_ = tmps & BIT1;
  tmps >>= 1;
  common->is_open_ = tmps & BIT1;
  tmps >>= 1;
  common->is_simple = tmps & BIT1;
  tmps >>= 1;
  common->has_extern = tmps & BIT1;
  tmps >>= 1;
  common->reserved_ = tmps & BIT15;
}
static void fill_ob_client_mem_lob_extern_header(ObClientMemLobExternHeader* header, uchar *row)
{
#define BIT1 0x01
#define BIT13 0x1FFF
  uint16_t tmps = 0;
  header->snapshot_ver_ = sint8korr(row);
  header->table_id_ = uint8korr(row+8);
  header->column_idx_ = uint4korr(row+16);

  tmps = uint2korr(row + 20);
  header->has_tx_info = tmps & BIT1;
  tmps >>= 1;
  header->has_cid_hash = tmps & BIT1;
  tmps >>= 1;
  header->has_view_info = tmps & BIT1;
  tmps >>= 1;
  header->extern_flags_ = tmps & BIT13;

  header->rowkey_size_ = uint2korr(row + 22);
  header->payload_offset_ = uint4korr(row + 24);
  header->payload_size_ = uint4korr(row + 28);
}
static void fill_ob_lob_locator_v2(OB_LOB_LOCATOR_V2 *ob_lob_locator, uchar *row)
{
  fill_ob_client_mem_lob_common(&ob_lob_locator->common, row);
  if (ob_lob_locator->common.has_extern){
    fill_ob_client_mem_lob_extern_header(&ob_lob_locator->extern_header, row+sizeof(ObClientMemLobCommon));
  }
}

static void fetch_result_ob_lob(MYSQL_BIND *param,
                                const MYSQL_FIELD *field __attribute__((unused)),
                                uchar **row)
{
  ulong length= net_field_length(row);
  ulong copy_length = 0;

  if (param->buffer_length <= 0
      || param->buffer_length < MAX_OB_LOB_LOCATOR_HEADER_LENGTH
      || length < MAX_OB_LOB_LOCATOR_HEADER_LENGTH) {
    *param->error= 1;
  } else {
    ObClientMemLobCommon common;
    fill_ob_client_mem_lob_common(&common, *row);
    if (common.version_ == OBCLIENT_LOB_LOCATORV1) {
      OB_LOB_LOCATOR *ob_lob_locator = (OB_LOB_LOCATOR *)param->buffer;
      fill_ob_lob_locator(ob_lob_locator, *row);

      copy_length = MIN(param->buffer_length - MAX_OB_LOB_LOCATOR_HEADER_LENGTH, length - MAX_OB_LOB_LOCATOR_HEADER_LENGTH);
      memcpy(ob_lob_locator->data_, (*row) + MAX_OB_LOB_LOCATOR_HEADER_LENGTH, copy_length);
      *param->error= copy_length + MAX_OB_LOB_LOCATOR_HEADER_LENGTH < length;
    } else if (common.version_ == OBCLIENT_LOB_LOCATORV2) {
      OB_LOB_LOCATOR_V2 *ob_lob_locatorv2 = (OB_LOB_LOCATOR_V2 *)param->buffer;
      //for oracle mode, common.has_extern = 1
      fill_ob_lob_locator_v2(ob_lob_locatorv2, *row);
      
      copy_length = MIN(param->buffer_length - MAX_OB_LOB_LOCATOR_HEADER_LENGTH, length - MAX_OB_LOB_LOCATOR_HEADER_LENGTH);
      memcpy(ob_lob_locatorv2->data_, (*row) + MAX_OB_LOB_LOCATOR_HEADER_LENGTH, copy_length);
      *param->error = copy_length + MAX_OB_LOB_LOCATOR_HEADER_LENGTH < length;
    }
  }

  *param->length= length;
  *row += length;
}

/*fetch_complex_type*/
static void fetch_result_complex(MYSQL_BIND *param, void *buffer,
                                 CHILD_TYPE *child, uchar **row);
static void *fetch_result_complex_alloc_space(MYSQL_COMPLEX_BIND_BASIC *header,
                                              MYSQL_BIND *param,
                                              ulong length);
static void fill_complex_type(MYSQL_BIND *param, void *buffer,
                              CHILD_TYPE *child);
static ulong get_complex_header_length(enum_types type) {
  switch (type) {
  case TYPE_OBJECT:
    return sizeof(MYSQL_COMPLEX_BIND_OBJECT);
  case TYPE_COLLECTION:
    return sizeof(MYSQL_COMPLEX_BIND_ARRAY);
  case TYPE_VARCHAR2:
  case TYPE_CHAR:
  case TYPE_RAW:
    return sizeof(MYSQL_COMPLEX_BIND_STRING);
  case TYPE_NUMBER:
    return sizeof(MYSQL_COMPLEX_BIND_DECIMAL);
  case TYPE_LONG:
  case TYPE_LONGLONG:
  case TYPE_TINY:
  case TYPE_SHORT:
  case TYPE_FLOAT:
  case TYPE_DOUBLE:
    return sizeof(MYSQL_COMPLEX_BIND_BASIC);
  default:
    return sizeof(MYSQL_COMPLEX_BIND_BASIC);
  }
}
static void fetch_result_str_complex(MYSQL_COMPLEX_BIND_STRING *header,
                                     MYSQL_BIND *param,
                                     uchar **row)
{
  void *buffer;
  ulong length;

  length = net_field_length(row);

  //include end '\0'
  buffer = fetch_result_complex_alloc_space((MYSQL_COMPLEX_BIND_HEADER *)header, param, length + 1);
  if (NULL == buffer) {
    return;
  }

  memcpy(buffer, (char *)*row, length);
  ((uchar *)buffer)[length]= '\0';
  header->length = length;
  *row+= length;
  return;
}
static void fetch_result_long_complex(MYSQL_COMPLEX_BIND_BASIC *header,
                                     MYSQL_BIND *param,
                                     uchar **row)
{
  void *buffer;

  buffer = fetch_result_complex_alloc_space((MYSQL_COMPLEX_BIND_HEADER *)header, param, 4);
  if (NULL == buffer) {
    return;
  }
  longstore(buffer, ((uint32)sint4korr(*row)));
  *row += 4;

  return;
}
static void fetch_result_longlong_complex(MYSQL_COMPLEX_BIND_BASIC *header,
                                          MYSQL_BIND *param,
                                          uchar **row)
{
  void *buffer;
  ulonglong val;

  buffer = fetch_result_complex_alloc_space((MYSQL_COMPLEX_BIND_HEADER *)header, param, 8);
  if (NULL == buffer) {
    return;
  }

  val= (ulonglong)sint8korr(*row);
  longlongstore(buffer, val);
  *row += 8;

  return;
}
static void fetch_result_short_complex(MYSQL_COMPLEX_BIND_BASIC *header,
                                     MYSQL_BIND *param,
                                     uchar **row)
{
  void *buffer;

  buffer = fetch_result_complex_alloc_space((MYSQL_COMPLEX_BIND_HEADER *)header, param, 2);
  if (NULL == buffer) {
    return;
  }
  shortstore(buffer, ((ushort)sint2korr(*row)));
  *row += 2;

  return;
}
static void fetch_result_tiny_complex(MYSQL_COMPLEX_BIND_BASIC *header,
                                      MYSQL_BIND *param,
                                      uchar **row)
{
  void *buffer;

  buffer = fetch_result_complex_alloc_space((MYSQL_COMPLEX_BIND_HEADER *)header, param, 1);
  if (NULL == buffer) {
    return;
  }
  *(uchar *)buffer= **row;
  *row += 1;

  return;
}
static void fetch_result_float_complex(MYSQL_COMPLEX_BIND_BASIC *header,
                                       MYSQL_BIND *param,
                                       uchar **row)
{
  void *buffer;
  float *value;

  buffer = fetch_result_complex_alloc_space((MYSQL_COMPLEX_BIND_HEADER *)header, param, 4);
  if (NULL == buffer) {
    return;
  }
  value= (float *)buffer;
  float4get(*value, *row);

  *row += 4;
  return;
}
static void fetch_result_double_complex(MYSQL_COMPLEX_BIND_BASIC *header,
                                       MYSQL_BIND *param,
                                       uchar **row)
{
  void *buffer;
  double *value;

  buffer = fetch_result_complex_alloc_space((MYSQL_COMPLEX_BIND_HEADER *)header, param, 8);
  if (NULL == buffer) {
    return;
  }
  value= (double *)buffer;
  float8get(*value, *row);

  *row += 8;
  return;
}


static void fetch_result_object_complex(MYSQL_COMPLEX_BIND_OBJECT *header,
                                        MYSQL_BIND *param,
                                        uchar **row)
{
  void *buffer = NULL;
  ulong length = 0;
  uint i = 0;
  COMPLEX_TYPE_OBJECT *object = NULL;
  struct st_complex_type *complex_type = NULL;
  uchar *null_ptr, bit;

  complex_type = get_complex_type(param->mysql, header->owner_name, header->type_name);

  if (complex_type == NULL) {
    *param->error= 1;
    return;
  }

  object = (COMPLEX_TYPE_OBJECT *)complex_type;

  for (i = 0; i < object->attr_no; i++) {
    length += get_complex_header_length(object->child[i].type);
  }

  buffer = fetch_result_complex_alloc_space((MYSQL_COMPLEX_BIND_BASIC *)header, param, length);
  if (NULL == buffer) {
    return;
  }

  null_ptr = *row;
  *row += (object->attr_no + 9)/8;    /* skip null bits */
  bit = 4;          /* first 2 bits are reserved */

  for (i = 0; i < object->attr_no; i++) {
    fill_complex_type(param, buffer, &(object->child[i]));

    if (*null_ptr & bit) {
      ((MYSQL_COMPLEX_BIND_BASIC *)buffer)->is_null = 1;
    } else {
      fetch_result_complex(param, buffer, &(object->child[i]), row);
    }

    buffer = (char*)buffer + get_complex_header_length(object->child[i].type);

    if (!((bit<<=1) & 255)) {
      bit= 1;         /* To next uchar */
      null_ptr++;
    }
  }

  header->length = (char*)buffer - (char*)header->buffer;
  return;
}
static void read_binary_datetime(MYSQL_TIME *tm, uchar **pos)
{
  uint length= net_field_length(pos);

  if (length)
  {
    uchar *to= *pos;

    tm->neg=    0;
    tm->year=   (uint) sint2korr(to);
    tm->month=  (uint) to[2];
    tm->day=    (uint) to[3];

    if (length > 4)
    {
      tm->hour=   (uint) to[4];
      tm->minute= (uint) to[5];
      tm->second= (uint) to[6];
    }
    else
      tm->hour= tm->minute= tm->second= 0;
    tm->second_part= (length > 7) ? (ulong) sint4korr(to+7) : 0;
    tm->time_type= MYSQL_TIMESTAMP_DATETIME;

    *pos+= length;
  }
  else
  {
    memset(tm, 0, sizeof(*tm));
    tm->time_type= MYSQL_TIMESTAMP_DATETIME;
  }
}
static void fetch_result_datetime_complex(MYSQL_COMPLEX_BIND_BASIC *header,
                                          MYSQL_BIND *param,
                                          uchar **row)
{
  MYSQL_TIME *tm = NULL;

  tm = fetch_result_complex_alloc_space(header, param, sizeof(MYSQL_TIME));
  if (NULL == tm) {
    return;
  }

  read_binary_datetime(tm, row);
  return;
}

static void fetch_result_bin_complex(MYSQL_COMPLEX_BIND_STRING *header,
                                     MYSQL_BIND *param,
                                     uchar **row)
{
  void *buffer;
  ulong length;

  length = net_field_length(row);

  buffer = fetch_result_complex_alloc_space((MYSQL_COMPLEX_BIND_HEADER *)header, param, length);
  if (NULL == buffer) {
    return;
  }

  memcpy(buffer, (char *)*row, length);
  header->length = length;
  *row+= length;
  return;
}
static void fetch_result_array_complex(MYSQL_COMPLEX_BIND_ARRAY *header,
                                       MYSQL_BIND *param,
                                       uchar **row,
                                       COMPLEX_TYPE *complex_type)
{
  void *buffer = NULL;
  ulong length = 0;
  uint num = 0;
  uint i = 0;
  COMPLEX_TYPE_COLLECTION *object = NULL;
  uchar *null_ptr, bit;

  if (NULL == complex_type) {
    complex_type = get_complex_type(param->mysql, header->owner_name, header->type_name);

    if (complex_type == NULL) {
      *param->error= 1;
      return;
    }
  } else {/* 匿名数组本地存储信息 */}

  object = (COMPLEX_TYPE_COLLECTION *)complex_type;

  num = net_field_length(row);
  length = num * get_complex_header_length(object->child.type);

  buffer = fetch_result_complex_alloc_space((MYSQL_COMPLEX_BIND_BASIC *)header, param, length);
  if (NULL == buffer) {
    return;
  }

  null_ptr = *row;
  *row += (num + 9)/8;    /* skip null bits */
  bit = 4;          /* first 2 bits are reserved */

  for (i = 0; i < num; i++) {
    fill_complex_type(param, buffer, &(object->child));

    if (*null_ptr & bit) {
      ((MYSQL_COMPLEX_BIND_BASIC *)buffer)->is_null = 1;
    } else {
      fetch_result_complex(param, buffer, &(object->child), row);
    }

    buffer = (char*)buffer + get_complex_header_length(object->child.type);

    if (!((bit<<=1) & 255)) {
      bit= 1;         /* To next uchar */
      null_ptr++;
    }
  }

  header->length = num;
  return;
}
static void fetch_result_complex(MYSQL_BIND *param, void *buffer,
                                 CHILD_TYPE *child, uchar **row)
{
  switch (child->type) {
  case TYPE_NUMBER:
    {
    fetch_result_str_complex((MYSQL_COMPLEX_BIND_STRING *)buffer, param, row);
    break;
    }
  case TYPE_VARCHAR2:
  case TYPE_CHAR:
    {
    fetch_result_str_complex((MYSQL_COMPLEX_BIND_STRING *)buffer, param, row);
    break;
    }
  case TYPE_RAW:
    {
    fetch_result_bin_complex((MYSQL_COMPLEX_BIND_STRING *)buffer, param, row);
    break;
    }
  case TYPE_DATE:
    {
    fetch_result_datetime_complex((MYSQL_COMPLEX_BIND_BASIC *)buffer, param, row);
    break;
    }
  case TYPE_OBJECT:
    {
    fetch_result_object_complex((MYSQL_COMPLEX_BIND_OBJECT *)buffer, param, row);
    break;
    }
  case TYPE_COLLECTION:
    {
    fetch_result_array_complex((MYSQL_COMPLEX_BIND_ARRAY *)buffer, param, row, NULL);
    break;
    }
  case TYPE_LONG:
    {
    fetch_result_long_complex((MYSQL_COMPLEX_BIND_BASIC *)buffer, param, row);
    break;
    }
  case TYPE_LONGLONG:
    {
    fetch_result_longlong_complex((MYSQL_COMPLEX_BIND_BASIC *)buffer, param, row);
    break;
    }
  case TYPE_TINY:
    {
    fetch_result_tiny_complex((MYSQL_COMPLEX_BIND_BASIC *)buffer, param, row);
    break;
    }
  case TYPE_SHORT:
    {
    fetch_result_short_complex((MYSQL_COMPLEX_BIND_BASIC *)buffer, param, row);
    break;
    }
  case TYPE_FLOAT:
    {
    fetch_result_float_complex((MYSQL_COMPLEX_BIND_BASIC *)buffer, param, row);
    break;
    }
  case TYPE_DOUBLE:
    {
    fetch_result_double_complex((MYSQL_COMPLEX_BIND_BASIC *)buffer, param, row);
    break;
    }
  default:
    *param->error= 1;
    break;
  }
  return;
}
static void *fetch_result_complex_alloc_space(MYSQL_COMPLEX_BIND_BASIC *header,
                                              MYSQL_BIND *param,
                                              ulong length)
{
  void *buffer = NULL;
  (*param->length) += length;
  if (1 == *param->error) {
    buffer = (void *)ma_alloc_root(&param->bind_alloc, length);
  } else if (param->offset + length > param->buffer_length) {
    *param->error= 1;
    buffer = (void *)ma_alloc_root(&param->bind_alloc, length);
  } else {
    buffer = (char*)param->buffer + param->offset;
  }

  header->buffer = buffer;
  if (header->buffer) {
    memset(header->buffer, 0, length);
    param->offset += length;
  }

  return (header->buffer);
}
static uint mysql_type_to_object_type(uint mysql_type)
{
  enum_types object_type;
  switch ((enum_field_types)mysql_type) {
  case MYSQL_TYPE_LONG:
  case MYSQL_TYPE_CURSOR:
    {
      object_type = TYPE_LONG;
      break;
    }
  case MYSQL_TYPE_LONGLONG:
    {
      object_type = TYPE_LONGLONG;
      break;
    }
  case MYSQL_TYPE_TINY:
    {
      object_type = TYPE_TINY;
      break;
    }
  case MYSQL_TYPE_SHORT:
    {
      object_type = TYPE_SHORT;
      break;
    }
  case MYSQL_TYPE_FLOAT:
    {
      object_type = TYPE_FLOAT;
      break;
    }
  case MYSQL_TYPE_DOUBLE:
    {
      object_type = TYPE_DOUBLE;
      break;
    }
  case MYSQL_TYPE_NEWDECIMAL:
    {
      object_type = TYPE_NUMBER;
      break;
    }
  case MYSQL_TYPE_VARCHAR:
  case MYSQL_TYPE_VAR_STRING:
  case MYSQL_TYPE_STRING:
    {
      object_type = TYPE_VARCHAR2;
      break;
    }
  case MYSQL_TYPE_OB_RAW:
    {
      object_type = TYPE_RAW;
      break;
    }
  case MYSQL_TYPE_DATETIME:
    {
      object_type = TYPE_DATE;
      break;
    }
  case MYSQL_TYPE_OBJECT:
    {
      object_type = TYPE_OBJECT;
      break;
    }
  case MYSQL_TYPE_ARRAY:
    {
      object_type = TYPE_COLLECTION;
      break;
    }
  default:
    object_type = TYPE_UNKNOW;
    break;
  }
  return object_type;
}
static void fill_complex_type(MYSQL_BIND *param, void *buffer,
                              CHILD_TYPE *child)
{

  switch (child->type) {
  case TYPE_NUMBER:
    {
    MYSQL_COMPLEX_BIND_DECIMAL *header = (MYSQL_COMPLEX_BIND_DECIMAL *)buffer;
    header->buffer_type = MYSQL_TYPE_NEWDECIMAL;
    break;
    }
  case TYPE_VARCHAR2:
  case TYPE_CHAR:
    {
    MYSQL_COMPLEX_BIND_STRING *header = (MYSQL_COMPLEX_BIND_STRING *)buffer;
    header->buffer_type = MYSQL_TYPE_VARCHAR;
    break;
    }
  case TYPE_RAW:
    {
    MYSQL_COMPLEX_BIND_STRING *header = (MYSQL_COMPLEX_BIND_STRING *)buffer;
    header->buffer_type = MYSQL_TYPE_OB_RAW;
    break;
    }
  case TYPE_DATE:
    {
    MYSQL_COMPLEX_BIND_BASIC *header = (MYSQL_COMPLEX_BIND_BASIC *)buffer;
    header->buffer_type = MYSQL_TYPE_DATETIME;
    break;
    }
  case TYPE_LONG:
    {
    MYSQL_COMPLEX_BIND_BASIC *header = (MYSQL_COMPLEX_BIND_BASIC *)buffer;
    header->buffer_type = MYSQL_TYPE_LONG;
    break;
    }
  case TYPE_LONGLONG:
    {
    MYSQL_COMPLEX_BIND_BASIC *header = (MYSQL_COMPLEX_BIND_BASIC *)buffer;
    header->buffer_type = MYSQL_TYPE_LONGLONG;
    break;
    }
  case TYPE_TINY:
    {
    MYSQL_COMPLEX_BIND_BASIC *header = (MYSQL_COMPLEX_BIND_BASIC *)buffer;
    header->buffer_type = MYSQL_TYPE_TINY;
    break;
    }
  case TYPE_SHORT:
    {
    MYSQL_COMPLEX_BIND_BASIC *header = (MYSQL_COMPLEX_BIND_BASIC *)buffer;
    header->buffer_type = MYSQL_TYPE_SHORT;
    break;
    } 
  case TYPE_FLOAT:
    {
    MYSQL_COMPLEX_BIND_BASIC *header = (MYSQL_COMPLEX_BIND_BASIC *)buffer;
    header->buffer_type = MYSQL_TYPE_FLOAT;
    break;
    }
  case TYPE_DOUBLE:
    {
    MYSQL_COMPLEX_BIND_BASIC *header = (MYSQL_COMPLEX_BIND_BASIC *)buffer;
    header->buffer_type = MYSQL_TYPE_DOUBLE;
    break;
    }
  case TYPE_OBJECT:
    {
    MYSQL_COMPLEX_BIND_OBJECT *header = (MYSQL_COMPLEX_BIND_OBJECT *)buffer;
    header->buffer_type = MYSQL_TYPE_OBJECT;
    header->type_name = child->object->type_name;
    header->owner_name = child->object->owner_name;
    break;
    }
  case TYPE_COLLECTION:
    {
    MYSQL_COMPLEX_BIND_ARRAY *header = (MYSQL_COMPLEX_BIND_ARRAY *)buffer;
    header->buffer_type = MYSQL_TYPE_ARRAY;
    header->type_name = child->object->type_name;
    header->owner_name = child->object->owner_name;
    break;
    }
  default:
    *param->error= 1;
    break;
  }
  return;
}
static void fetch_result_type_complex(MYSQL_COMPLEX_BIND_OBJECT *header,
                                      MYSQL_BIND *param,
                                      uchar **row,
                                      COMPLEX_TYPE *complex_type)
{
  if (NULL == complex_type) {
    complex_type = get_complex_type(param->mysql, header->owner_name, header->type_name);
    if (NULL == complex_type) {
      *param->error= 1;
      return;
    }
  } else { /*匿名数组，meta信息不能通过server查询，本地传入 */ }

  if (TYPE_OBJECT == complex_type->type) {
    MYSQL_COMPLEX_BIND_OBJECT *buffer = fetch_result_complex_alloc_space((MYSQL_COMPLEX_BIND_BASIC *)header,
            param, sizeof(MYSQL_COMPLEX_BIND_OBJECT));

    if (NULL == buffer) {
      return;
    }

    buffer->owner_name = header->owner_name;
    buffer->type_name = header->type_name;
    buffer->buffer_type = MYSQL_TYPE_OBJECT;

    fetch_result_object_complex(buffer, param, row);
  } else if (TYPE_COLLECTION == complex_type->type) {
    MYSQL_COMPLEX_BIND_ARRAY *buffer = fetch_result_complex_alloc_space((MYSQL_COMPLEX_BIND_BASIC *)header,
            param, sizeof(MYSQL_COMPLEX_BIND_ARRAY));

    if (NULL == buffer) {
      return;
    }

    buffer->owner_name = header->owner_name;
    buffer->type_name = header->type_name;
    buffer->buffer_type = MYSQL_TYPE_ARRAY;

    fetch_result_array_complex(buffer, param, row, complex_type);
  }

  return;
}

static void ps_fetch_result_type(MYSQL_BIND *param,
                              const MYSQL_FIELD *field,
                              uchar **row)
{
  MYSQL_COMPLEX_BIND_OBJECT header;

  (*param->length) = 0;
  param->offset = 0;
  ma_init_alloc_root(&param->bind_alloc, 2048, 0);

  header.owner_name = field->owner_name;
  header.type_name = field->type_name;

  if (NULL == header.owner_name && NULL == header.type_name) {
    COMPLEX_TYPE_COLLECTION complex_type;
    complex_type.header.type = TYPE_COLLECTION;
    complex_type.header.owner_name[0] = '\0';
    complex_type.header.type_name[0] = '\0';
    complex_type.header.version = field->version;
    complex_type.header.is_valid = TRUE;
    complex_type.child.type = (enum_types)mysql_type_to_object_type(field->elem_type);

    if (TYPE_UNKNOW == complex_type.child.type) {
      *param->error = 1;
    } else {
      fetch_result_type_complex(&header, param, row, (COMPLEX_TYPE *)&complex_type);
    }
  } else {
    fetch_result_type_complex(&header, param, row, NULL);
  }

  ma_free_root(&param->bind_alloc, MYF(0));
  return;
}

/* {{{ ps_fetch_string */
static
void ps_fetch_string(MYSQL_BIND *r_param,
                     const MYSQL_FIELD *field __attribute__((unused)),
                     unsigned char **row)
{
  /* C-API differs from PHP. While PHP just converts string to string,
     C-API needs to convert the string to the defined type with in 
     the result bind buffer.
   */
  ulong field_length= net_field_length(row);

  convert_froma_string(r_param, (char *)*row, field_length);
  (*row) += field_length;
}
/* }}} */

/* {{{ ps_fetch_bin */
static
void ps_fetch_bin(MYSQL_BIND *r_param, 
             const MYSQL_FIELD *field,
             unsigned char **row)
{
  if (field->charsetnr == 63)
  {
    ulong field_length= *r_param->length= net_field_length(row);
    uchar *current_pos= (*row) + r_param->offset,
          *end= (*row) + field_length;
    size_t copylen= 0;

    if (current_pos < end)
    {
      copylen= end - current_pos;
      if (r_param->buffer_length)
        memcpy(r_param->buffer, current_pos, MIN(copylen, r_param->buffer_length));
    }
    if (copylen < r_param->buffer_length &&
        (r_param->buffer_type == MYSQL_TYPE_STRING ||
         r_param->buffer_type == MYSQL_TYPE_JSON))
      ((char *)r_param->buffer)[copylen]= 0;
    *r_param->error= copylen > r_param->buffer_length;
    (*row)+= field_length;
  }
  else
    ps_fetch_string(r_param, field, row);
}
/* }}} */

static
void ps_fetch_mysql_lob(MYSQL_BIND *r_param, const MYSQL_FIELD *field, unsigned char **row)
{
  ulong length = net_field_length(row);
  if (field->charsetnr == 63)
  {
    ulong field_length = *r_param->length = length;
    uchar *current_pos = (*row) + r_param->offset, *end = (*row) + field_length;
    size_t copylen = 0;
    if (current_pos < end)
    {
      copylen = end - current_pos;
      if (r_param->buffer_length)
        memcpy(r_param->buffer, current_pos, MIN(copylen, r_param->buffer_length));
    }
    if (copylen < r_param->buffer_length &&
      (r_param->buffer_type == MYSQL_TYPE_STRING ||
        r_param->buffer_type == MYSQL_TYPE_JSON))
        ((char *)r_param->buffer)[copylen] = 0;
    *r_param->error = copylen > r_param->buffer_length;
  } else {
    convert_froma_string(r_param, (char *)*row, length);
  }
  (*row) += length;
}

/* {{{ ps_fetch_result_skip_direct */
static
void ps_fetch_result_skip_direct(MYSQL_BIND *r_param,
             const MYSQL_FIELD *field,
             unsigned char **row)
{
  if (NULL != r_param) {
    (*r_param->skip_result)(r_param, (MYSQL_FIELD *)field, row);
    *r_param->error= 0;
  }
}
/* }}} */

/* {{{ _mysqlnd_init_ps_subsystem */
void mysql_init_ps_subsystem(void)
{
  memset(mysql_ps_fetch_functions, 0, sizeof(mysql_ps_fetch_functions));
  mysql_ps_fetch_functions[MYSQL_TYPE_NULL].func= ps_fetch_null;
  mysql_ps_fetch_functions[MYSQL_TYPE_NULL].pack_len  = 0;
  mysql_ps_fetch_functions[MYSQL_TYPE_NULL].max_len  = 0;

  mysql_ps_fetch_functions[MYSQL_TYPE_TINY].func    = ps_fetch_int8;
  mysql_ps_fetch_functions[MYSQL_TYPE_TINY].pack_len  = 1;
  mysql_ps_fetch_functions[MYSQL_TYPE_TINY].max_len  = 4;

  mysql_ps_fetch_functions[MYSQL_TYPE_SHORT].func    = ps_fetch_int16;
  mysql_ps_fetch_functions[MYSQL_TYPE_SHORT].pack_len  = 2;
  mysql_ps_fetch_functions[MYSQL_TYPE_SHORT].max_len  = 6;

  mysql_ps_fetch_functions[MYSQL_TYPE_YEAR].func    = ps_fetch_int16;
  mysql_ps_fetch_functions[MYSQL_TYPE_YEAR].pack_len  = 2;
  mysql_ps_fetch_functions[MYSQL_TYPE_YEAR].max_len  = 6;

  mysql_ps_fetch_functions[MYSQL_TYPE_INT24].func    = ps_fetch_int32;
  mysql_ps_fetch_functions[MYSQL_TYPE_INT24].pack_len  = 4;
  mysql_ps_fetch_functions[MYSQL_TYPE_INT24].max_len  = 9;

  mysql_ps_fetch_functions[MYSQL_TYPE_LONG].func    = ps_fetch_int32;
  mysql_ps_fetch_functions[MYSQL_TYPE_LONG].pack_len  = 4;
  mysql_ps_fetch_functions[MYSQL_TYPE_LONG].max_len  = 11;

  mysql_ps_fetch_functions[MYSQL_TYPE_CURSOR].func    = ps_fetch_int32; /* parse cursor it with type of INT32 */
  mysql_ps_fetch_functions[MYSQL_TYPE_CURSOR].pack_len  = 4;
  mysql_ps_fetch_functions[MYSQL_TYPE_CURSOR].max_len  = 11;

  mysql_ps_fetch_functions[MYSQL_TYPE_LONGLONG].func  = ps_fetch_int64;
  mysql_ps_fetch_functions[MYSQL_TYPE_LONGLONG].pack_len= 8;
  mysql_ps_fetch_functions[MYSQL_TYPE_LONGLONG].max_len  = 21;

  mysql_ps_fetch_functions[MYSQL_TYPE_FLOAT].func    = ps_fetch_float;
  mysql_ps_fetch_functions[MYSQL_TYPE_FLOAT].pack_len  = 4;
  mysql_ps_fetch_functions[MYSQL_TYPE_FLOAT].max_len  = MAX_DOUBLE_STRING_REP_LENGTH;

  mysql_ps_fetch_functions[MYSQL_TYPE_DOUBLE].func    = ps_fetch_double;
  mysql_ps_fetch_functions[MYSQL_TYPE_DOUBLE].pack_len  = 8;
  mysql_ps_fetch_functions[MYSQL_TYPE_DOUBLE].max_len  = MAX_DOUBLE_STRING_REP_LENGTH;
  
  mysql_ps_fetch_functions[MYSQL_TYPE_TIME].func  = ps_fetch_datetime;
  mysql_ps_fetch_functions[MYSQL_TYPE_TIME].pack_len  = MYSQL_PS_SKIP_RESULT_W_LEN;
  mysql_ps_fetch_functions[MYSQL_TYPE_TIME].max_len  = 17;

  mysql_ps_fetch_functions[MYSQL_TYPE_DATE].func  = ps_fetch_datetime;
  mysql_ps_fetch_functions[MYSQL_TYPE_DATE].pack_len  = MYSQL_PS_SKIP_RESULT_W_LEN;
  mysql_ps_fetch_functions[MYSQL_TYPE_DATE].max_len  = 10;

  mysql_ps_fetch_functions[MYSQL_TYPE_OB_TIMESTAMP_NANO].func  = ps_fetch_oracle_timestamp;
  mysql_ps_fetch_functions[MYSQL_TYPE_OB_TIMESTAMP_NANO].pack_len  = MYSQL_PS_SKIP_RESULT_W_LEN;
  mysql_ps_fetch_functions[MYSQL_TYPE_OB_TIMESTAMP_NANO].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_OB_TIMESTAMP_WITH_LOCAL_TIME_ZONE].func  = ps_fetch_oracle_timestamp;
  mysql_ps_fetch_functions[MYSQL_TYPE_OB_TIMESTAMP_WITH_LOCAL_TIME_ZONE].pack_len  = MYSQL_PS_SKIP_RESULT_W_LEN;
  mysql_ps_fetch_functions[MYSQL_TYPE_OB_TIMESTAMP_WITH_LOCAL_TIME_ZONE].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_OB_TIMESTAMP_WITH_TIME_ZONE].func  = ps_fetch_oracle_timestamp;
  mysql_ps_fetch_functions[MYSQL_TYPE_OB_TIMESTAMP_WITH_TIME_ZONE].pack_len  = MYSQL_PS_SKIP_RESULT_W_LEN;
  mysql_ps_fetch_functions[MYSQL_TYPE_OB_TIMESTAMP_WITH_TIME_ZONE].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_OB_INTERVAL_YM].func = ps_fetch_oracle_interval;
  mysql_ps_fetch_functions[MYSQL_TYPE_OB_INTERVAL_YM].pack_len = MYSQL_PS_SKIP_RESULT_W_LEN;
  mysql_ps_fetch_functions[MYSQL_TYPE_OB_INTERVAL_YM].max_len = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_OB_INTERVAL_DS].func = ps_fetch_oracle_interval;
  mysql_ps_fetch_functions[MYSQL_TYPE_OB_INTERVAL_DS].pack_len = MYSQL_PS_SKIP_RESULT_W_LEN;
  mysql_ps_fetch_functions[MYSQL_TYPE_OB_INTERVAL_DS].max_len = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_NEWDATE].func    = ps_fetch_string;
  mysql_ps_fetch_functions[MYSQL_TYPE_NEWDATE].pack_len  = MYSQL_PS_SKIP_RESULT_W_LEN;
  mysql_ps_fetch_functions[MYSQL_TYPE_NEWDATE].max_len  = -1;
  
  mysql_ps_fetch_functions[MYSQL_TYPE_DATETIME].func  = ps_fetch_datetime;
  mysql_ps_fetch_functions[MYSQL_TYPE_DATETIME].pack_len= MYSQL_PS_SKIP_RESULT_W_LEN;
  mysql_ps_fetch_functions[MYSQL_TYPE_DATETIME].max_len  = 30;

  mysql_ps_fetch_functions[MYSQL_TYPE_TIMESTAMP].func  = ps_fetch_datetime;
  mysql_ps_fetch_functions[MYSQL_TYPE_TIMESTAMP].pack_len= MYSQL_PS_SKIP_RESULT_W_LEN;
  mysql_ps_fetch_functions[MYSQL_TYPE_TIMESTAMP].max_len  = 30;
  
  mysql_ps_fetch_functions[MYSQL_TYPE_TINY_BLOB].func  = ps_fetch_mysql_lob;
  mysql_ps_fetch_functions[MYSQL_TYPE_TINY_BLOB].pack_len= MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_TINY_BLOB].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_BLOB].func    = ps_fetch_mysql_lob;
  mysql_ps_fetch_functions[MYSQL_TYPE_BLOB].pack_len  = MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_BLOB].max_len  = -1;
  
  mysql_ps_fetch_functions[MYSQL_TYPE_MEDIUM_BLOB].func  = ps_fetch_mysql_lob;
  mysql_ps_fetch_functions[MYSQL_TYPE_MEDIUM_BLOB].pack_len= MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_MEDIUM_BLOB].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_LONG_BLOB].func    = ps_fetch_mysql_lob;
  mysql_ps_fetch_functions[MYSQL_TYPE_LONG_BLOB].pack_len  = MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_LONG_BLOB].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_BIT].func  = ps_fetch_bin;
  mysql_ps_fetch_functions[MYSQL_TYPE_BIT].pack_len  = MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_BIT].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_VAR_STRING].func    = ps_fetch_string;
  mysql_ps_fetch_functions[MYSQL_TYPE_VAR_STRING].pack_len  = MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_VAR_STRING].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_VARCHAR].func    = ps_fetch_string;
  mysql_ps_fetch_functions[MYSQL_TYPE_VARCHAR].pack_len  = MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_VARCHAR].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_STRING].func      = ps_fetch_string;
  mysql_ps_fetch_functions[MYSQL_TYPE_STRING].pack_len    = MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_STRING].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_JSON].func      = ps_fetch_string;
  mysql_ps_fetch_functions[MYSQL_TYPE_JSON].pack_len    = MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_JSON].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_DECIMAL].func    = ps_fetch_string;
  mysql_ps_fetch_functions[MYSQL_TYPE_DECIMAL].pack_len  = MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_DECIMAL].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_NEWDECIMAL].func    = ps_fetch_string;
  mysql_ps_fetch_functions[MYSQL_TYPE_NEWDECIMAL].pack_len  = MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_NEWDECIMAL].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_OB_NUMBER_FLOAT].func    = ps_fetch_string;
  mysql_ps_fetch_functions[MYSQL_TYPE_OB_NUMBER_FLOAT].pack_len  = MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_OB_NUMBER_FLOAT].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_OB_NVARCHAR2].func    = ps_fetch_string;
  mysql_ps_fetch_functions[MYSQL_TYPE_OB_NVARCHAR2].pack_len  = MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_OB_NVARCHAR2].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_OB_NCHAR].func    = ps_fetch_string;
  mysql_ps_fetch_functions[MYSQL_TYPE_OB_NCHAR].pack_len  = MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_OB_NCHAR].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_ENUM].func    = ps_fetch_string;
  mysql_ps_fetch_functions[MYSQL_TYPE_ENUM].pack_len  = MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_ENUM].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_SET].func      = ps_fetch_string;
  mysql_ps_fetch_functions[MYSQL_TYPE_SET].pack_len    = MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_SET].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_OB_UROWID].func      = ps_fetch_string;
  mysql_ps_fetch_functions[MYSQL_TYPE_OB_UROWID].pack_len    = MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_OB_UROWID].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_ORA_BLOB].func      = fetch_result_ob_lob;
  mysql_ps_fetch_functions[MYSQL_TYPE_ORA_BLOB].pack_len    = MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_ORA_BLOB].max_len  = -1;


  mysql_ps_fetch_functions[MYSQL_TYPE_ORA_CLOB].func      = fetch_result_ob_lob;
  mysql_ps_fetch_functions[MYSQL_TYPE_ORA_CLOB].pack_len    = MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_ORA_CLOB].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_OB_RAW].func      = ps_fetch_oracle_raw;
  mysql_ps_fetch_functions[MYSQL_TYPE_OB_RAW].pack_len  = MYSQL_PS_SKIP_RESULT_W_LEN;
  mysql_ps_fetch_functions[MYSQL_TYPE_OB_RAW].max_len   = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_OBJECT].func    = ps_fetch_result_type;
  mysql_ps_fetch_functions[MYSQL_TYPE_OBJECT].pack_len  = MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_OBJECT].max_len  = -1;

  mysql_ps_fetch_functions[MYSQL_TYPE_GEOMETRY].func  = ps_fetch_string;
  mysql_ps_fetch_functions[MYSQL_TYPE_GEOMETRY].pack_len= MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MYSQL_TYPE_GEOMETRY].max_len  = -1;
   
  //It will be used in returning into
  mysql_ps_fetch_functions[MAX_NO_FIELD_TYPES].func  = ps_fetch_result_skip_direct;
  mysql_ps_fetch_functions[MAX_NO_FIELD_TYPES].pack_len= MYSQL_PS_SKIP_RESULT_STR;
  mysql_ps_fetch_functions[MAX_NO_FIELD_TYPES].max_len  = -1;

  mysql_ps_subsystem_initialized= 1;
}
/* }}} */


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */

