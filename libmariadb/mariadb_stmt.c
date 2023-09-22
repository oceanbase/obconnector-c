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

  Part of this code includes code from the PHP project which
  is freely available from http://www.php.net
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
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
   | Authors: Georg Richter <georg@mysql.com>                             |
   |          Andrey Hristov <andrey@mysql.com>                           |
   |          Ulf Wendel <uwendel@mysql.com>                              |
   +----------------------------------------------------------------------+
   */

#include "ma_global.h"
#include <ma_sys.h>
#include <ma_string.h>
#include <mariadb_ctype.h>
#include "mysql.h"
#include "errmsg.h"
#include <ma_pvio.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>
#include <mysql/client_plugin.h>
#include <ma_common.h>
#include "ma_priv.h"
#include "ob_complex.h"
#include "ob_protocol20.h"
#include "ob_full_link_trace.h"

#define DBUG_RETURN(a) return (a)
#define UPDATE_STMT_ERROR(stmt)\
SET_CLIENT_STMT_ERROR((stmt), (stmt)->mysql->net.last_errno, (stmt)->mysql->net.sqlstate, (stmt)->mysql->net.last_error)

#define STMT_NUM_OFS(type, a, r) (((type *)(a))[r])
#define MADB_RESET_ERROR     1
#define MADB_RESET_LONGDATA  2
#define MADB_RESET_SERVER    4
#define MADB_RESET_BUFFER    8
#define MADB_RESET_STORED   16

#define MAX_TIME_STR_LEN 13
#define MAX_DATE_STR_LEN 5
#define MAX_DATETIME_STR_LEN 12

#define MAX_DATE_REP_LENGTH 5
#define MAX_TIME_REP_LENGTH 13
#define MAX_DATETIME_REP_LENGTH 12
#define MAX_ORACLE_TIMESTAMP_REP_LENGTH 13
#define MAX_DOUBLE_STRING_REP_LENGTH 331

#define SUPPORT_PREPARE_EXECUTE_VERSION 20276
#define SUPPORT_SEND_FETCH_FLAG_VERSION 20276
#define SUPPORT_SEND_PLARRAY_MAXRARR_LEN 30201
#define SUPPORT_PLARRAY_BINDBYNAME 20207
static const char SINGLE_QUOTE = '\'';
static const char DOUBLE_QUOTE = '"';
#define MY_TEST(a)    ((a) ? 1 : 0)
typedef struct
{
  MA_MEM_ROOT fields_ma_alloc_root;
  MA_MEM_ROOT binds_ma_alloc_root; //as for oracle mode fetch return meta, mem root should seperate
} MADB_STMT_EXTENSION;

static my_bool net_stmt_close(MYSQL_STMT *stmt, my_bool remove);
static int madb_alloc_stmt_fields(MYSQL_STMT *stmt);

static my_bool is_not_null= 0;
static my_bool is_null= 1;

extern int mthd_my_read_query_result(MYSQL *mysql);
extern int ma_read_ok_packet(MYSQL *mysql, uchar *pos, ulong length);

void stmt_set_error(MYSQL_STMT *stmt,
                  unsigned int error_nr,
                  const char *sqlstate,
                  const char *format,
                  ...)
{
  va_list ap;
  const char *error= NULL;

  if (error_nr >= CR_MIN_ERROR && error_nr <= CR_MYSQL_LAST_ERROR)
    error= ER(error_nr);
  else if (error_nr >= CER_MIN_ERROR && error_nr <= CR_MARIADB_LAST_ERROR)
    error= CER(error_nr);

  stmt->last_errno= error_nr;
  ma_strmake(stmt->sqlstate, sqlstate, SQLSTATE_LENGTH);
  va_start(ap, format);
  vsnprintf(stmt->last_error, MYSQL_ERRMSG_SIZE,
            format ? format : error ? error : "", ap);
  va_end(ap);
  return;
}

my_bool mthd_supported_buffer_type(enum enum_field_types type)
{
  switch (type) {
  case MYSQL_TYPE_BIT:
  case MYSQL_TYPE_BLOB:
  case MYSQL_TYPE_DATE:
  case MYSQL_TYPE_DATETIME:
  case MYSQL_TYPE_DECIMAL:
  case MYSQL_TYPE_DOUBLE:
  case MYSQL_TYPE_FLOAT:
  case MYSQL_TYPE_GEOMETRY:
  case MYSQL_TYPE_INT24:
  case MYSQL_TYPE_LONG:
  case MYSQL_TYPE_LONG_BLOB:
  case MYSQL_TYPE_LONGLONG:
  case MYSQL_TYPE_MEDIUM_BLOB:
  case MYSQL_TYPE_NEWDATE:
  case MYSQL_TYPE_NEWDECIMAL:
  case MYSQL_TYPE_NULL:
  case MYSQL_TYPE_SHORT:
  case MYSQL_TYPE_STRING:
  case MYSQL_TYPE_JSON:
  case MYSQL_TYPE_TIME:
  case MYSQL_TYPE_TIMESTAMP:
  case MYSQL_TYPE_TINY:
  case MYSQL_TYPE_TINY_BLOB:
  case MYSQL_TYPE_VAR_STRING:
  case MYSQL_TYPE_YEAR:
  case MYSQL_TYPE_OBJECT:
  case MYSQL_TYPE_ARRAY:
  case MYSQL_TYPE_STRUCT:
  case MYSQL_TYPE_CURSOR:
  case MYSQL_TYPE_OB_TIMESTAMP_WITH_TIME_ZONE:
  case MYSQL_TYPE_OB_TIMESTAMP_WITH_LOCAL_TIME_ZONE:
  case MYSQL_TYPE_OB_TIMESTAMP_NANO:
  case MYSQL_TYPE_OB_INTERVAL_YM:
  case MYSQL_TYPE_OB_INTERVAL_DS:
  case MYSQL_TYPE_OB_NUMBER_FLOAT:
  case MYSQL_TYPE_OB_NVARCHAR2:
  case MYSQL_TYPE_OB_NCHAR:
  case MYSQL_TYPE_OB_UROWID:
  case MYSQL_TYPE_ORA_BLOB:
  case MYSQL_TYPE_ORA_CLOB:
  case MYSQL_TYPE_OB_RAW:
    return 1;
    break;
  default:
    return 0;
    break;
  }
}

static my_bool madb_reset_stmt(MYSQL_STMT *stmt, unsigned int flags);
static my_bool mysql_stmt_internal_reset(MYSQL_STMT *stmt, my_bool is_close);
static int stmt_unbuffered_eof(MYSQL_STMT *stmt __attribute__((unused)),
                               uchar **row __attribute__((unused)))
{
  return MYSQL_NO_DATA;
}

static uint mysql_store_length_size(ulonglong num)
{
  if (num < (ulonglong) 251LL)
    return 1;
  if (num < (ulonglong) 65536LL)
    return 3;
  if (num < (ulonglong) 16777216LL)
    return 4;
  return 9;
}

static int stmt_unbuffered_fetch(MYSQL_STMT *stmt, uchar **row)
{
  ulong pkt_len;

  pkt_len= ma_net_safe_read(stmt->mysql);

  if (pkt_len == packet_error)
  {
    stmt->fetch_row_func= stmt_unbuffered_eof;
    return(1);
  }

  if (stmt->mysql->net.read_pos[0] == 254)
  {
    *row = NULL;
    stmt->fetch_row_func= stmt_unbuffered_eof;
    return(MYSQL_NO_DATA);
  }
  else
    *row = stmt->mysql->net.read_pos;
  stmt->result.rows++;
  return(0);
}

static int stmt_buffered_fetch(MYSQL_STMT *stmt, uchar **row)
{
  if (!stmt->result_cursor)
  {
    *row= NULL;
    stmt->state= MYSQL_STMT_FETCH_DONE;
    return MYSQL_NO_DATA;
  }
  stmt->state= MYSQL_STMT_USER_FETCHING;
  *row= (uchar *)stmt->result_cursor->data;

  stmt->result_cursor= stmt->result_cursor->next;
  return 0;
}

int mthd_stmt_read_all_rows(MYSQL_STMT *stmt)
{
  MYSQL_DATA *result= &stmt->result;
  MYSQL_ROWS *current, **pprevious;
  ulong packet_len;
  unsigned char *p;

  pprevious= &result->data;

  while ((packet_len = ma_net_safe_read(stmt->mysql)) != packet_error)
  {
    p= stmt->mysql->net.read_pos;
    if (packet_len > 7 || p[0] != 254)
    {
      /* allocate space for rows */
      if (!(current= (MYSQL_ROWS *)ma_alloc_root(&result->alloc, sizeof(MYSQL_ROWS) + packet_len)))
      {
        SET_CLIENT_STMT_ERROR(stmt, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
        return(1);
      }
      current->data= (MYSQL_ROW)(current + 1);
      *pprevious= current;
      pprevious= &current->next;

      /* copy binary row, we will encode it during mysql_stmt_fetch */
      memcpy((char *)current->data, (char *)p, packet_len);

      if (stmt->update_max_length)
      {
        uchar *null_ptr, bit_offset= 4;
        uchar *cp= p;
        unsigned int i;

        cp++; /* skip first byte */
        null_ptr= cp;
        cp+= (stmt->field_count + 9) / 8;

        for (i=0; i < stmt->field_count; i++)
        {
          if (!(*null_ptr & bit_offset))
          {
            if (mysql_ps_fetch_functions[stmt->fields[i].type].pack_len < 0)
            {
              /* We need to calculate the sizes for date and time types */
              size_t len= net_field_length(&cp);
              switch(stmt->fields[i].type) {
              case MYSQL_TYPE_TIME:
              case MYSQL_TYPE_DATE:
              case MYSQL_TYPE_DATETIME:
              case MYSQL_TYPE_TIMESTAMP:
                stmt->fields[i].max_length= mysql_ps_fetch_functions[stmt->fields[i].type].max_len;
                break;
              default:
                if (len > stmt->fields[i].max_length)
                  stmt->fields[i].max_length= (ulong)len;
                break;
              }
              cp+= len;
            }
            else
            {
              if (stmt->fields[i].flags & ZEROFILL_FLAG)
              {
                size_t len= MAX(stmt->fields[i].length, mysql_ps_fetch_functions[stmt->fields[i].type].max_len);
                if (len > stmt->fields[i].max_length)
                  stmt->fields[i].max_length= (unsigned long)len;
              }
              else if (!stmt->fields[i].max_length)
              {
                stmt->fields[i].max_length= mysql_ps_fetch_functions[stmt->fields[i].type].max_len;
              }
              cp+= mysql_ps_fetch_functions[stmt->fields[i].type].pack_len;
            }
          }
          if (!((bit_offset <<=1) & 255))
          {
            bit_offset= 1; /* To next byte */
            null_ptr++;
          }
        }
      }
      current->length= packet_len;
      result->rows++;
    } else  /* end of stream */
    {
      *pprevious= 0;
      /* sace status info */
      p++;
      stmt->upsert_status.warning_count= stmt->mysql->warning_count= uint2korr(p);
      p+=2;
      stmt->upsert_status.server_status= stmt->mysql->server_status= uint2korr(p);
      stmt->result_cursor= result->data;
      return(0);
    }
  }
  stmt->result_cursor= 0;
  SET_CLIENT_STMT_ERROR(stmt, stmt->mysql->net.last_errno, stmt->mysql->net.sqlstate,
      stmt->mysql->net.last_error);
  return(1);
}
// static int reinit_stmt_field(MYSQL_STMT *stmt);
static int madb_reinit_result_set_metadata(MYSQL_STMT *stmt);
static int
do_stmt_read_row_from_oracle_implicit_cursor(MYSQL_STMT *stmt, unsigned char **row, my_bool is_need_fetch_from_server);
static int stmt_cursor_fetch(MYSQL_STMT *stmt, uchar **row)
{
  uchar buf[STMT_ID_LENGTH + 4];
  MYSQL_DATA *result= &stmt->result;
  int back_status = 0;
  if (stmt->state < MYSQL_STMT_USE_OR_STORE_CALLED)
  {
    SET_CLIENT_STMT_ERROR(stmt, CR_COMMANDS_OUT_OF_SYNC, SQLSTATE_UNKNOWN, 0);
    return(1);
  }
  if (stmt->mysql == NULL)
  {
    SET_CLIENT_STMT_ERROR(stmt, CR_UNKNOWN_ERROR, SQLSTATE_UNKNOWN, 0);
    return(1);
  }
  if (stmt->mysql->oracle_mode)
  {
    return do_stmt_read_row_from_oracle_implicit_cursor(stmt, row, FALSE);
  }
  back_status = stmt->mysql->status;
  /* do we have some prefetched rows available ? */
  if (stmt->result_cursor)
    return(stmt_buffered_fetch(stmt, row));
  //if no scroll cursor, when last row will return no data ,and next request will fetch to server
  if (stmt->upsert_status.server_status & SERVER_STATUS_LAST_ROW_SENT)
    stmt->upsert_status.server_status&=  ~SERVER_STATUS_LAST_ROW_SENT;
  else
  {
    int4store(buf, stmt->stmt_id);
    int4store(buf + STMT_ID_LENGTH, stmt->prefetch_rows);

    if (stmt->mysql->methods->db_command(stmt->mysql, COM_STMT_FETCH, (char *)buf, sizeof(buf), 1, stmt))
    {
      UPDATE_STMT_ERROR(stmt);
      return(1);
    }

    /* free previously allocated buffer */
    ma_free_root(&result->alloc, MYF(MY_KEEP_PREALLOC));
    result->data= 0;
    result->rows= 0;
    if (stmt->mysql->oracle_mode)
    {
      //oracle mode return mata info
      if (mthd_my_read_query_result(stmt->mysql))
      {
         SET_CLIENT_STMT_ERROR(stmt, stmt->mysql->net.last_errno, stmt->mysql->net.sqlstate,
          stmt->mysql->net.last_error);
        return (1);
      } else if (madb_reinit_result_set_metadata(stmt)) //TODO need to check if could skip next
      {
        DBUG_RETURN(1);
      }else {
        //the status will change in mthd_my_read_query_result, reset it here
        stmt->mysql->status = back_status;
      }
    }
    if (stmt->mysql->methods->db_stmt_read_all_rows(stmt))
      return(1);

    return(stmt_buffered_fetch(stmt, row));
  }
  /* no more cursor data available */
  *row= NULL;
  return(MYSQL_NO_DATA);
}

static void skip_result_fixed(MYSQL_BIND *param,
                  MYSQL_FIELD *field __attribute__((unused)),
                  uchar **row)

{
  (*row)+= param->pack_length;
}


static void skip_result_with_length(MYSQL_BIND *param __attribute__((unused)),
                    MYSQL_FIELD *field __attribute__((unused)),
                    uchar **row)

{
  ulong length= net_field_length(row);
  (*row)+= length;
}


static void skip_result_string(MYSQL_BIND *param __attribute__((unused)),
                   MYSQL_FIELD *field,
                   uchar **row)

{
  ulong length= net_field_length(row);
  (*row)+= length;
  if (field->max_length < length)
    field->max_length= length;
}

static int
do_stmt_read_row_from_oracle_cursor(MYSQL_STMT *stmt, unsigned char **row, my_bool is_need_buffer_row)
{
  int back_status = stmt->mysql->status;
  if (stmt->result_cursor)
    return(stmt_buffered_fetch(stmt, row));
  //no scroll cursor when last row return no_data
  if (stmt->upsert_status.server_status & SERVER_STATUS_LAST_ROW_SENT)
    stmt->upsert_status.server_status &= ~SERVER_STATUS_LAST_ROW_SENT;
  else
  {
    MYSQL_DATA *result= &stmt->result;
    MYSQL* mysql = stmt->mysql;
//    int ret = 0;
    uchar* buf = NULL;
    uchar* p   = NULL;
    ulong buf_len = 0;
    ulong buf_offset = 0;
    ulong null_byte_offset = 0;
    ulong pkt_len = 0;

    UNUSED(null_byte_offset);
    UNUSED(buf_offset);
    UNUSED(pkt_len);
    UNUSED(is_need_buffer_row);

    if (!mysql)
    {
      SET_CLIENT_STMT_ERROR(stmt, CR_SERVER_LOST, SQLSTATE_UNKNOWN, 0);
      return(1);
    }
    buf_len = STMT_ID_LENGTH /*statement id*/
              + 4 /*number of rows to fetch*/;

    if (NULL == (p = buf = malloc(buf_len)))
    {
      SET_CLIENT_STMT_ERROR(stmt, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
      return(1);
    }
    memset(buf, 0, buf_len);
    int4store(p, stmt->stmt_id);
    int4store(p + STMT_ID_LENGTH, stmt->prefetch_rows);

    if (stmt->mysql->methods->db_command(stmt->mysql, COM_STMT_FETCH, (char *)buf, buf_len, 1, stmt))
    {
      UPDATE_STMT_ERROR(stmt);
      free(buf);
      return(1);
    }

    /* free previously allocated buffer */
    ma_free_root(&result->alloc, MYF(MY_KEEP_PREALLOC));
    result->data= 0;
    result->rows= 0;
    if (stmt->mysql->oracle_mode)
    {
      //oracle mode return mata info
      if (mthd_my_read_query_result(stmt->mysql))
      {
        SET_CLIENT_STMT_ERROR(stmt, stmt->mysql->net.last_errno, stmt->mysql->net.sqlstate,
          stmt->mysql->net.last_error);
        free(buf);
        return (1);
      }
      /** Not need to reinit fields for stmt, for it cannot update the MYSQL_RES's fileds which has returned before.
       *  If need get last the fields info for fetch, it need open it and get newest MYSQL_RES. */
      else if (madb_alloc_stmt_fields(stmt))
      {
        DBUG_RETURN(1);
      } 
      else {
        //the status will change in mthd_my_read_query_result, reset it here
        stmt->mysql->status = back_status;
      }
    }
    if (stmt->mysql->methods->db_stmt_read_all_rows(stmt)) /* mthd_stmt_read_all_rows */
    {
      free(buf);
      return(1);
    }
    stmt->upsert_status.server_status = mysql->server_status;
    //if (ret = stmt_buffered_fetch(stmt, row)) {
    //  free(buf);
    //  return (ret);
    //}
    free(buf);
    return 0;
  }
  /* no more cursor data available */
  *row= NULL;
  return(MYSQL_NO_DATA);
}

/* Interface used by Cursor data of PL/NON-Block's out parameters for OBCI or same situation.
 * The result juse be stored to stmt->result_cursor. */
int STDCALL mysql_stmt_fetch_oracle_cursor(MYSQL_STMT *stmt)
{
  int rc;
  uchar *row;
  // enter ("mysql_stmt_fetch");

  if ((rc= do_stmt_read_row_from_oracle_cursor(stmt, &row, FALSE)))
  {
    stmt->state= MYSQL_STMT_FETCH_DONE;
    stmt->mysql->status= MYSQL_STATUS_READY;
    /* to fetch data again, stmt must be executed again */
    return(rc);
  }
  //rc= stmt->mysql->methods->db_stmt_fetch_to_bind(stmt, row);
  stmt->state= MYSQL_STMT_USER_FETCHING;
  CLEAR_CLIENT_ERROR(stmt->mysql);
  CLEAR_CLIENT_STMT_ERROR(stmt);
  return rc;
//  return mysql_stmt_fetch(stmt);
}

static int
do_stmt_read_row_from_oracle_implicit_cursor(MYSQL_STMT *stmt, unsigned char **row, my_bool is_need_fetch_from_server)
{
  int back_status = stmt->mysql->status;
  if (stmt->result_cursor && !is_need_fetch_from_server)
    return(stmt_buffered_fetch(stmt, row));
  //no scroll cursor when last row return no_data
  if (!(stmt->execute_mode & EXECUTE_STMT_SCROLLABLE_READONLY) &&
       (stmt->upsert_status.server_status & SERVER_STATUS_LAST_ROW_SENT))
    stmt->upsert_status.server_status &= ~SERVER_STATUS_LAST_ROW_SENT;
  else if ((stmt->execute_mode & EXECUTE_STMT_SCROLLABLE_READONLY) &&
          (stmt->upsert_status.server_status & SERVER_STATUS_LAST_ROW_SENT) &&
          (stmt->orientation == CURSOR_FETCH_DEFAULT||
           stmt->orientation == CURSOR_FETCH_NEXT))
  {
    //scroll cursor when last_row_sent will return no_data
  }
  else
  {
    MYSQL_BIND *param, *end;
    uint pos = 0;
    MYSQL_DATA *result= &stmt->result;
    MYSQL* mysql = stmt->mysql;
    int ret = 0;
    int extend_flag = 0;
    uchar* buf = NULL;
    uchar* p   = NULL;
    ulong buf_len = 0;
    ulong buf_offset = 0;
    ulong null_byte_offset = 0;
    my_bool send_fetch_flag = FALSE;
    ulong pkt_len = 0;
    my_bool column_flag = FALSE;
    ulong bind_count;
    
    if (!mysql)
    {
      SET_CLIENT_STMT_ERROR(stmt, CR_SERVER_LOST, SQLSTATE_UNKNOWN, 0);
      return(1);
    }
    buf_len = STMT_ID_LENGTH /*statement id*/
              + 4 /*number of rows to fetch*/
              + 2 /*orientation*/
              + 4 /*offset to be used change the current row position */
              + 4 /*extend flag*/
              + 1 /*prealloc for null bitmap */;
    if (NULL == (p = buf = malloc(buf_len)))
    {
      SET_CLIENT_STMT_ERROR(stmt, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
      return(1);
    }
    memset(buf, 0, buf_len);
    int4store(p, stmt->stmt_id);
    int4store(p + STMT_ID_LENGTH, stmt->prefetch_rows);
    int2store(p + 8, stmt->orientation);
    int4store(p + 10, stmt->fetch_offset);
    if (stmt->execute_mode & EXECUTE_STMT_SCROLLABLE_READONLY)
    {
      if (get_support_send_fetch_flag(stmt->mysql))
      {
        send_fetch_flag = TRUE;
        //int4store(buf + 14, FETCH_RETURN_EXTRA_OK);
        extend_flag = FETCH_RETURN_EXTRA_OK;
      }
    }

    for (param = stmt->bind, end = param + stmt->field_count; param < end; ++param)
    {
      if (param->piece_data_used)
      {
        extend_flag |= FETCH_HAS_PIECE_COLUMN;
        column_flag = TRUE;
        break;
      }
    }

    int4store(p + 14, extend_flag);
    p += 18;

    if (stmt->field_count && column_flag)
    {
      uint null_count = 0;

      /* Reserve place for null-marker bytes */
      null_count= (stmt->field_count+7) /8;
      buf_offset = p - buf;
      null_byte_offset = buf_offset;
      /* realloc memory if space if not enough for null_count(all field) */
      if (null_count > buf_len - buf_offset)
      {
        buf_len = buf_offset + null_count; /* get new length */
        if (NULL == (buf = (uchar*)realloc(buf, buf_len)))
        {
          SET_CLIENT_STMT_ERROR(stmt, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
          return(1);
        }
        p = buf + buf_offset;
        memset(p, 0, null_count); //null bitmap
        p += null_count;
      }

      bind_count = stmt->field_count;
      end = param + bind_count;
      for (pos = 0; pos < stmt->field_count; pos++)
      {
        if (stmt->bind[pos].piece_data_used)
        {
          (buf + null_byte_offset)[pos/8] |= (uchar) (1 << (pos & 7));
        }
      }
    }

    if (stmt->mysql->methods->db_command(stmt->mysql, COM_STMT_FETCH, (char *)buf, buf_len, 1, stmt))
    {
      UPDATE_STMT_ERROR(stmt);
      free(buf);
      return(1);
    }

    /* free previously allocated buffer */
    ma_free_root(&result->alloc, MYF(MY_KEEP_PREALLOC));
    result->data= 0;
    result->rows= 0;
    if (stmt->mysql->oracle_mode)
    {
      //oracle mode return mata info
      if (mthd_my_read_query_result(stmt->mysql))
      {
        SET_CLIENT_STMT_ERROR(stmt, stmt->mysql->net.last_errno, stmt->mysql->net.sqlstate,
          stmt->mysql->net.last_error);
        free(buf);
        return (1);
      }
      /** Not need to reinit fields for stmt, for it cannot update the MYSQL_RES's fileds which has returned before.
       *  If need get last the fields info for fetch, it need open it and get newest MYSQL_RES.
      else if (reinit_stmt_field(stmt))
      {
        DBUG_RETURN(1);
      } */
      else {
        //the status will change in mthd_my_read_query_result, reset it here
        stmt->mysql->status = back_status;
      }
    }
    if (stmt->mysql->methods->db_stmt_read_all_rows(stmt))
    {
      free(buf);
      return(1);
    }
    if (send_fetch_flag) {
      //extra ok packet
      if (mysql->server_status & SERVER_MORE_RESULTS_EXIST)
      {
        if ((pkt_len= ma_net_safe_read(mysql)) == packet_error)
        {
          free(buf);
          return (1);
        }
        mysql->server_status &= ~(SERVER_MORE_RESULTS_EXIST);
        if (mysql->net.read_pos[0] == 0) {
          ma_read_ok_packet(mysql, &mysql->net.read_pos[1], pkt_len);//extra ok packet
          stmt->upsert_status.affected_rows= stmt->mysql->affected_rows;
        }
        else
        {
          end_server(mysql);
          SET_CLIENT_ERROR(mysql, CR_MALFORMED_PACKET, SQLSTATE_UNKNOWN, 0);
          snprintf(mysql->net.last_error, MYSQL_ERRMSG_SIZE, "Wrong packet: unexpect pkt");
          free(buf);
          return (1);
        }
      }
    }
    stmt->upsert_status.server_status = mysql->server_status;
    if ((ret = stmt_buffered_fetch(stmt, row))) {
      free(buf);
      return (ret);
    }
    free(buf);
    return 0;
  }
  /* no more cursor data available */
  *row= NULL;
  return(MYSQL_NO_DATA);
}
int STDCALL mysql_stmt_fetch_oracle_implicit_cursor(MYSQL_STMT *stmt, my_bool is_need_fetch_from_server)
{
  uchar *row;
  int rc = 0;
  if (stmt->state <= MYSQL_STMT_EXECUTED)
  {
    SET_CLIENT_STMT_ERROR(stmt, CR_COMMANDS_OUT_OF_SYNC, SQLSTATE_UNKNOWN, 0);
    return(1);
  }
  if ((rc = do_stmt_read_row_from_oracle_implicit_cursor(stmt, &row, is_need_fetch_from_server)))
  {
    stmt->state= MYSQL_STMT_FETCH_DONE;
    stmt->mysql->status= MYSQL_STATUS_READY;
    /* to fetch data again, stmt must be executed again */
    return(rc);
  }
  rc= stmt->mysql->methods->db_stmt_fetch_to_bind(stmt, row);
  stmt->state= MYSQL_STMT_USER_FETCHING;
  CLEAR_CLIENT_ERROR(stmt->mysql);
  CLEAR_CLIENT_STMT_ERROR(stmt);
  return rc;
}

int STDCALL mysql_stmt_fetch_oracle_buffered_result(MYSQL_STMT *stmt)
{
  uchar *row;
  int rc = 0;
  if (stmt->state <= MYSQL_STMT_EXECUTED)
  {
    SET_CLIENT_STMT_ERROR(stmt, CR_COMMANDS_OUT_OF_SYNC, SQLSTATE_UNKNOWN, 0);
    return(1);
  }
  /* just read the buffered result for some situation, not to fetch the result from ObServer any more */
  if ((rc = stmt_buffered_fetch(stmt, &row)))
  {
    stmt->state= MYSQL_STMT_FETCH_DONE;
    stmt->mysql->status= MYSQL_STATUS_READY;
    /* to fetch data again, stmt must be executed again */
    return(rc);
  }
  rc= stmt->mysql->methods->db_stmt_fetch_to_bind(stmt, row);
  stmt->state= MYSQL_STMT_USER_FETCHING;
  CLEAR_CLIENT_ERROR(stmt->mysql);
  CLEAR_CLIENT_STMT_ERROR(stmt);
  return rc;
}

/* flush one result set */
void mthd_stmt_flush_unbuffered(MYSQL_STMT *stmt)
{
  ulong packet_len;
  int in_resultset= stmt->state > MYSQL_STMT_EXECUTED &&
                    stmt->state < MYSQL_STMT_FETCH_DONE;
  while ((packet_len = ma_net_safe_read(stmt->mysql)) != packet_error)
  {
    uchar *pos= stmt->mysql->net.read_pos;
    if (!in_resultset && *pos == 0) /* OK */
    {
      pos++;
      net_field_length(&pos);
      net_field_length(&pos);
      stmt->mysql->server_status= uint2korr(pos);
      goto end;
    }
    if (packet_len < 8 && *pos == 254) /* EOF */
    {
      if (mariadb_connection(stmt->mysql))
      {
        stmt->mysql->server_status= uint2korr(pos + 3);
        if (in_resultset)
          goto end;
        in_resultset= 1;
      }
      else
        goto end;
    }
  }
end:
  stmt->state= MYSQL_STMT_FETCH_DONE;
}

int mthd_stmt_fetch_to_bind(MYSQL_STMT *stmt, unsigned char *row)
{
  uint i;
  size_t truncations= 0;
  unsigned char *null_ptr, bit_offset= 4;
  row++; /* skip status byte */
  null_ptr= row;
  row+= (stmt->field_count + 9) / 8;

  for (i=0; i < stmt->field_count; i++)
  {
    /* save row position for fetching values in pieces */
    if (*null_ptr & bit_offset)
    {
      if (stmt->result_callback)
        stmt->result_callback(stmt->user_data, i, NULL);
      else
      {
        if (!stmt->bind[i].is_null)
          stmt->bind[i].is_null= &stmt->bind[i].is_null_value;
        *stmt->bind[i].is_null= 1;
        stmt->bind[i].u.row_ptr= NULL;
      }
    } else
    {
      stmt->bind[i].mysql = stmt->mysql;
      stmt->bind[i].u.row_ptr= row;
      if (!stmt->bind_result_done ||
          stmt->bind[i].flags & MADB_BIND_DUMMY)
      {
        unsigned long length;

        if (stmt->result_callback)
          stmt->result_callback(stmt->user_data, i, &row);
        else {
          if (mysql_ps_fetch_functions[stmt->fields[i].type].pack_len >= 0)
            length= mysql_ps_fetch_functions[stmt->fields[i].type].pack_len;
          else
            length= net_field_length(&row);
          row+= length;
          if (!stmt->bind[i].length)
            stmt->bind[i].length= &stmt->bind[i].length_value;
          *stmt->bind[i].length= stmt->bind[i].length_value= length;
        }
      }
      else
      {
        if (!stmt->bind[i].length)
          stmt->bind[i].length= &stmt->bind[i].length_value;
        if (!stmt->bind[i].is_null)
          stmt->bind[i].is_null= &stmt->bind[i].is_null_value;
        *stmt->bind[i].is_null= 0;
        if (stmt->bind[i].no_need_to_parser_result) {
          //skip directly
          mysql_ps_fetch_functions[MAX_NO_FIELD_TYPES].func(&stmt->bind[i], &stmt->fields[i], &row);
        } else {
          mysql_ps_fetch_functions[stmt->fields[i].type].func(&stmt->bind[i], &stmt->fields[i], &row);
        }
        if (stmt->mysql->options.report_data_truncation)
          truncations+= *stmt->bind[i].error;
      }
    }

    if (!((bit_offset <<=1) & 255)) {
      bit_offset= 1; /* To next byte */
      null_ptr++;
    }
  }
  return((truncations) ? MYSQL_DATA_TRUNCATED : 0);
}

MYSQL_RES *_mysql_stmt_use_result(MYSQL_STMT *stmt)
{
  MYSQL *mysql= stmt->mysql;

  if (!stmt->field_count ||
      (!stmt->cursor_exists && mysql->status != MYSQL_STATUS_STMT_RESULT) ||
      (stmt->cursor_exists && mysql->status != MYSQL_STATUS_READY) ||
      (stmt->state != MYSQL_STMT_WAITING_USE_OR_STORE))
  {
    SET_CLIENT_ERROR(mysql, CR_COMMANDS_OUT_OF_SYNC, SQLSTATE_UNKNOWN, 0);
    return(NULL);
  }

  CLEAR_CLIENT_STMT_ERROR(stmt);

  stmt->state = MYSQL_STMT_USE_OR_STORE_CALLED;
  if (!stmt->cursor_exists) {
    if (stmt->use_prepare_execute) {
      //new protocol should all cursor, but aray binding batch error not cursor
      stmt->fetch_row_func = stmt_buffered_fetch;
    } else{
      stmt->fetch_row_func= stmt_unbuffered_fetch; //mysql_stmt_fetch_unbuffered_row;
    }
  }
  else
    stmt->fetch_row_func= stmt_cursor_fetch;

  return(NULL);
}

unsigned char *mysql_net_store_length(unsigned char *packet, size_t length)
{
  if (length < (unsigned long long) L64(251)) {
    *packet = (unsigned char) length;
    return packet + 1;
  }

  if (length < (unsigned long long) L64(65536)) {
    *packet++ = 252;
    int2store(packet,(uint) length);
    return packet + 2;
  }

  if (length < (unsigned long long) L64(16777216)) {
    *packet++ = 253;
    int3store(packet,(ulong) length);
    return packet + 3;
  }
  *packet++ = 254;
  int8store(packet, length);
  return packet + 8;
}

static long ma_get_length(MYSQL_STMT *stmt, unsigned int param_nr, unsigned long row_nr)
{
  if (!stmt->params[param_nr].length)
    return 0;
  if (stmt->param_callback)
    return (long)*stmt->params[param_nr].length;
  if (stmt->row_size)
    return *(long *)((char *)stmt->params[param_nr].length + row_nr * stmt->row_size);
  else
    return stmt->params[param_nr].length[row_nr];
}

static signed char ma_get_indicator(MYSQL_STMT *stmt, unsigned int param_nr, unsigned long row_nr)
{
  if (!MARIADB_STMT_BULK_SUPPORTED(stmt) ||
      !stmt->array_size ||
      !stmt->params[param_nr].u.indicator)
    return 0;
  if (stmt->param_callback)
    return *stmt->params[param_nr].u.indicator;
  if (stmt->row_size)
    return *((char *)stmt->params[param_nr].u.indicator + (row_nr * stmt->row_size));
  return stmt->params[param_nr].u.indicator[row_nr];
}

static void *ma_get_buffer_offset(MYSQL_STMT *stmt, enum enum_field_types type,
                                  void *buffer, unsigned long row_nr)
{
  if (stmt->param_callback)
    return buffer;

  if (stmt->array_size)
  {
    int len;
    if (stmt->row_size)
      return (void *)((char *)buffer + stmt->row_size * row_nr);
    len= mysql_ps_fetch_functions[type].pack_len;
    if (len > 0)
      return (void *)((char *)buffer + len * row_nr);
    return ((void **)buffer)[row_nr];
  }
  return buffer;
}

static void store_param_object_type(MYSQL *mysql, unsigned char **pos, MYSQL_COMPLEX_BIND_OBJECT *param)
{
  ulong len;
  struct st_complex_type *complex_type = get_complex_type_with_local(mysql, param->type_name);

  if (NULL != complex_type) {
    //schema_name
    len= strlen((char *)complex_type->owner_name);
    *pos = mysql_net_store_length(*pos, len);
    memcpy(*pos, complex_type->owner_name, len);
    *pos += len;

    //type_name
    len= strlen((char *)complex_type->type_name);
    *pos = mysql_net_store_length(*pos, len);
    memcpy(*pos, complex_type->type_name, len);
    *pos += len;

    //version
    *pos = mysql_net_store_length(*pos, complex_type->version);
  } else {
    //schema_name
    *pos = mysql_net_store_length(*pos, 0);

    //type_name
    len= strlen((char *)param->type_name);
    *pos = mysql_net_store_length(*pos, len);
    memcpy(*pos, param->type_name, len);
    *pos += len;

    //version
    *pos = mysql_net_store_length(*pos, 0);
  }
}

static void store_param_plarray_type(MYSQL *mysql, unsigned char **pos, MYSQL_COMPLEX_BIND_PLARRAY *param);

static void store_param_array_type(MYSQL *mysql, unsigned char **pos, MYSQL_COMPLEX_BIND_ARRAY *param)
{
  MYSQL_COMPLEX_BIND_HEADER *header = (MYSQL_COMPLEX_BIND_HEADER *)param->buffer;

  //shcema_name
  *pos = mysql_net_store_length(*pos, 0);
  //type_name
  *pos = mysql_net_store_length(*pos, 0);
  //version
  *pos = mysql_net_store_length(*pos, 0);

  if (MYSQL_TYPE_OBJECT == header->buffer_type) {
    **pos = MYSQL_TYPE_OBJECT;
    (*pos)++;

    store_param_object_type(mysql, pos, (MYSQL_COMPLEX_BIND_OBJECT *)header);
  } else if (MYSQL_TYPE_ARRAY == header->buffer_type) {
    **pos = MYSQL_TYPE_OBJECT;
    (*pos)++;

    store_param_array_type(mysql, pos, (MYSQL_COMPLEX_BIND_ARRAY *) header);
  }  else if (MYSQL_TYPE_PLARRAY == header->buffer_type) {
    **pos = MYSQL_TYPE_OBJECT;
    (*pos)++;

    store_param_plarray_type(mysql, pos, (MYSQL_COMPLEX_BIND_PLARRAY *) header);
  } else {
    **pos = (uchar) header->buffer_type;
    (*pos)++;
  }
}

// plarray can have no elements and cannot be treated like array
static void store_param_plarray_type(MYSQL *mysql, unsigned char **pos, MYSQL_COMPLEX_BIND_PLARRAY *param)
{
  MYSQL_COMPLEX_BIND_HEADER *header = (MYSQL_COMPLEX_BIND_HEADER *)param->buffer;

  //shcema_name
  *pos = mysql_net_store_length(*pos, 0);
  //type_name
  *pos = mysql_net_store_length(*pos, 0);
  //version
  *pos = mysql_net_store_length(*pos, 0);

  if (MYSQL_TYPE_OBJECT == param->elem_type) {
    **pos = MYSQL_TYPE_OBJECT;
    (*pos)++;

    store_param_object_type(mysql, pos, (MYSQL_COMPLEX_BIND_OBJECT *)header);
  } else if (MYSQL_TYPE_ARRAY == param->elem_type) {
    **pos = MYSQL_TYPE_OBJECT;
    (*pos)++;

    store_param_array_type(mysql, pos, (MYSQL_COMPLEX_BIND_ARRAY *) header);
  } else if (MYSQL_TYPE_PLARRAY == param->elem_type) {
    **pos = MYSQL_TYPE_OBJECT;
    (*pos)++;

    store_param_plarray_type(mysql, pos, (MYSQL_COMPLEX_BIND_PLARRAY *) header);
  } else {
    **pos = (uchar) param->elem_type;
    (*pos)++;
  }
}

static void store_param_type(MYSQL *mysql, unsigned char **pos, MYSQL_BIND *param)
{
  uint typecode = param->buffer_type | (param->is_unsigned ? 32768 : 0);
  int2store(*pos, typecode);
  *pos+= 2;

  if (MYSQL_TYPE_OBJECT == param->buffer_type) {
    MYSQL_COMPLEX_BIND_OBJECT *header = (MYSQL_COMPLEX_BIND_OBJECT *)param->buffer;
    if (NULL == header->type_name) {
      if (MYSQL_TYPE_PLARRAY == header->buffer_type) {
        store_param_plarray_type(mysql, pos, (MYSQL_COMPLEX_BIND_PLARRAY *)header);
      } else {
        store_param_array_type(mysql, pos, (MYSQL_COMPLEX_BIND_ARRAY *)header);
      }
    } else {
      store_param_object_type(mysql, pos, header);
    }
  }
}
static void store_param_tinyint_complex(unsigned char **pos, MYSQL_COMPLEX_BIND_HEADER *param)
{
  *((*pos)++)= *(uchar *) param->buffer;
}

static void store_param_short_complex(unsigned char **pos, MYSQL_COMPLEX_BIND_HEADER *param)
{
  short value= *(short*) param->buffer;
  int2store(*pos,value);
  (*pos)+=2;
}

static void store_param_int32_complex(unsigned char **pos, MYSQL_COMPLEX_BIND_HEADER *param)
{
  int32 value= *(int32*) param->buffer;
  int4store(*pos,value);
  (*pos)+=4;
}

static void store_param_int64_complex(unsigned char **pos, MYSQL_COMPLEX_BIND_HEADER *param)
{
  longlong value= *(longlong*) param->buffer;
  int8store(*pos,value);
  (*pos)+= 8;
}

static void store_param_float_complex(unsigned char **pos, MYSQL_COMPLEX_BIND_HEADER *param)
{
  float value= *(float*) param->buffer;
  float4store(*pos, value);
  (*pos)+= 4;
}

static void store_param_double_complex(unsigned char **pos, MYSQL_COMPLEX_BIND_HEADER *param)
{
  double value= *(double*) param->buffer;
  float8store(*pos, value);
  (*pos)+= 8;
}

static void store_param_time_complex(unsigned char **pos, MYSQL_COMPLEX_BIND_HEADER *param)
{
  MYSQL_TIME *tm= (MYSQL_TIME *) param->buffer;
  uchar buff[MAX_TIME_REP_LENGTH], *tmp;
  uint length;

  tmp= buff+1;
  tmp[0]= tm->neg ? 1: 0;
  int4store(tmp+1, tm->day);
  tmp[5]= (uchar) tm->hour;
  tmp[6]= (uchar) tm->minute;
  tmp[7]= (uchar) tm->second;
  int4store(tmp+8, tm->second_part);
  if (tm->second_part)
    length= 12;
  else if (tm->hour || tm->minute || tm->second || tm->day)
    length= 8;
  else
    length= 0;
  buff[0]= (char) length++;
  memcpy((char *)*pos, buff, length);
  (*pos) += length;
}
static void net_store_datetime(unsigned char **pos, MYSQL_TIME *t)
{
  char t_buffer[MAX_DATETIME_STR_LEN];
  uint len= 0;

  int2store(t_buffer + 1, t->year);
  t_buffer[3]= (char) t->month;
  t_buffer[4]= (char) t->day;
  t_buffer[5]= (char) t->hour;
  t_buffer[6]= (char) t->minute;
  t_buffer[7]= (char) t->second;
  if (t->second_part)
  {
    int4store(t_buffer + 8, t->second_part);
    len= 11;
  }
  else if (t->hour || t->minute || t->second)
    len= 7;
  else if (t->year || t->month || t->day)
    len= 4;
  else
    len=0;
  t_buffer[0]= len++;
  memcpy(*pos, t_buffer, len);
  (*pos)+= len;
}
static void store_param_date_complex(unsigned char **pos, MYSQL_COMPLEX_BIND_HEADER *param)
{
  MYSQL_TIME tm= *((MYSQL_TIME *) param->buffer);
  tm.hour= tm.minute= tm.second= tm.second_part= 0;
  net_store_datetime(pos, &tm);
}

static void store_param_datetime_complex(unsigned char **pos, MYSQL_COMPLEX_BIND_HEADER *param)
{
  MYSQL_TIME *tm= (MYSQL_TIME *)param->buffer;
  net_store_datetime(pos, tm);
}

static void store_param_oracle_timestamp_nano_complex(unsigned char **pos, MYSQL_COMPLEX_BIND_HEADER *param)
{
  ORACLE_TIME *tm= (ORACLE_TIME *) param->buffer;
  uchar buff[MAX_ORACLE_TIMESTAMP_REP_LENGTH];

  buff[0] = 12;

  buff[1]= (char) tm->century;
  buff[2]= (char) tm->year;
  buff[3]= (uchar) tm->month;
  buff[4]= (uchar) tm->day;
  buff[5]= (uchar) tm->hour;
  buff[6]= (uchar) tm->minute;
  buff[7]= (uchar) tm->second;
  int4store(buff+8, tm->second_part);
  buff[12] = 9;

  memcpy((char *)*pos, buff, MAX_ORACLE_TIMESTAMP_REP_LENGTH);
  (*pos) += MAX_ORACLE_TIMESTAMP_REP_LENGTH;
}

static void store_param_oracle_timestamp_tz_complex(unsigned char **pos, MYSQL_COMPLEX_BIND_HEADER *param)
{
  ORACLE_TIME *tm= (ORACLE_TIME *) param->buffer;
  uchar *to;
  unsigned length = 16;
  unsigned tz_length;

  store_param_oracle_timestamp_nano_complex(pos, param);

  *((*pos)++) = (char) tm->offset_hour;
  *((*pos)++) = (char) tm->offset_minute;

  if (tm->tz_name != NULL) {
    tz_length = strlen(tm->tz_name);
    to= mysql_net_store_length(*pos, tz_length);
    memcpy(to, tm->tz_name, tz_length);
    *pos = to + tz_length;
    length += tz_length;
  } else {
    *pos = mysql_net_store_length(*pos, 0);
  }

  if (tm->tz_abbr != NULL) {
    tz_length = strlen(tm->tz_abbr);
    to= mysql_net_store_length(*pos, tz_length);
    memcpy(to, tm->tz_abbr, tz_length);
    *pos = to + tz_length;
    length += tz_length;
  } else {
    *pos = mysql_net_store_length(*pos, 0);
  }

  *(*pos - length - 1) = length;
}

static void store_param_str_complex(unsigned char **pos, MYSQL_COMPLEX_BIND_STRING *param)
{
  /* param->length is always set in mysql_stmt_bind_param */
  ulong length= param->length;
  uchar *to= mysql_net_store_length(*pos, length);
  memcpy(to, param->buffer, length);
  *pos = to+length;
}

static void store_param_str(MYSQL_STMT *stmt, int column, unsigned char **pos, unsigned long row_nr, ulong len)
{
  MYSQL_COMPLEX_BIND_STRING header;
  void *buf= ma_get_buffer_offset(stmt, stmt->params[column].buffer_type,
                                  stmt->params[column].buffer, row_nr);
  header.buffer = buf;
  header.length = len;

  store_param_str_complex(pos, &header);
}

static void ob_client_mem_lob_common_to_buff(unsigned char** pos, uint32_t *len, ObClientMemLobCommon* common)
{
  uint32_t tmps = 0;
  int4store(*pos, common->magic_);
  *pos += 4; *len -= 4;
  tmps <<= 15;
  tmps |= common->reserved_;
  tmps <<= 1;
  tmps |= common->has_extern;
  tmps <<= 1;
  tmps |= common->is_simple;
  tmps <<= 1;
  tmps |= common->is_open_;
  tmps <<= 1;
  tmps |= common->is_inrow_;
  tmps <<= 1;
  tmps |= common->read_only_;
  tmps <<= 4;
  tmps |= common->type_;
  tmps <<= 8;
  tmps |= common->version_;
  int4store(*pos, tmps);
  *pos += 4; *len -= 4;
}
static void ob_client_mem_lob_extern_header_to_buff(unsigned char** pos, uint32_t *len, ObClientMemLobExternHeader *header)
{
  uint16_t tmps = 0;
  int8store(*pos, header->snapshot_ver_);
  *pos += 8; *len -= 8;
  int8store(*pos, header->table_id_);
  *pos += 8; *len -= 8;
  int4store(*pos, header->column_idx_);
  *pos += 4; *len -= 4;

  tmps <<= 13;
  tmps |= header->extern_flags_;
  tmps <<= 1;
  tmps |= header->has_view_info;
  tmps <<= 1;
  tmps |= header->has_cid_hash;
  tmps <<= 1;
  tmps |= header->has_tx_info;
  int2store(*pos, tmps);
  *pos += 2; *len -= 2;

  int2store(*pos, header->rowkey_size_);
  *pos += 2; *len -= 2;
  int4store(*pos, header->payload_offset_);
  *pos += 4; *len -= 4;
  int4store(*pos, header->payload_size_);
  *pos += 4; *len -= 4;
}
static void store_ob_lob_locator_v2(unsigned char** pos, uint32_t* len, OB_LOB_LOCATOR_V2* ob_lob_locator) {
  ob_client_mem_lob_common_to_buff(pos, len, &ob_lob_locator->common);
  if (ob_lob_locator->common.has_extern) {
    ob_client_mem_lob_extern_header_to_buff(pos, len, &ob_lob_locator->extern_header);
  }
}
static void store_ob_lob_locator_v1(unsigned char** pos, uint32_t *len, OB_LOB_LOCATOR* lob_locator){
  int4store(*pos, lob_locator->magic_code_);
  *pos += 4; *len -= 4;
  int4store(*pos, lob_locator->version_);
  *pos += 4; *len -= 4;
  int8store(*pos, lob_locator->snapshot_version_);
  *pos += 8; *len -= 8;
  int8store(*pos, lob_locator->table_id_);
  *pos += 8; *len -= 8;
  int4store(*pos, lob_locator->column_id_);
  *pos += 4; *len -= 4;
  int2store(*pos, lob_locator->mode_);
  *pos += 2; *len -= 2;
  int2store(*pos, lob_locator->option_);
  *pos += 2; *len -= 2;
  int4store(*pos, lob_locator->payload_offset_);
  *pos += 4; *len -= 4;
  int4store(*pos, lob_locator->payload_size_);
  *pos += 4; *len -= 4;
}

static void store_param_ob_lob_complex(unsigned char **pos, MYSQL_COMPLEX_BIND_HEADER *param)
{
  OB_LOB_LOCATOR *lob_locator = (OB_LOB_LOCATOR *) param->buffer;
  uint8_t version = get_ob_lob_locator_version(lob_locator);
  if (version == OBCLIENT_LOB_LOCATORV1) {
    uint32_t len = MAX_OB_LOB_LOCATOR_HEADER_LENGTH;
    *pos = mysql_net_store_length(*pos, MAX_OB_LOB_LOCATOR_HEADER_LENGTH + lob_locator->payload_size_ + lob_locator->payload_offset_);

    store_ob_lob_locator_v1(pos, &len, lob_locator);

    memcpy((char *)*pos, lob_locator->data_, lob_locator->payload_size_ + lob_locator->payload_offset_);
    *pos += lob_locator->payload_size_ + lob_locator->payload_offset_;
  } else if (version == OBCLIENT_LOB_LOCATORV2) {
    uint32_t len = MAX_OB_LOB_LOCATOR_HEADER_LENGTH;
    OB_LOB_LOCATOR_V2 *lob_locatorv2 = (OB_LOB_LOCATOR_V2 *)param->buffer;
    *pos = mysql_net_store_length(*pos, MAX_OB_LOB_LOCATOR_HEADER_LENGTH + lob_locatorv2->extern_header.payload_size_ + lob_locatorv2->extern_header.payload_offset_);
    
    store_ob_lob_locator_v2(pos, &len, lob_locatorv2);

    memcpy((char *)*pos, lob_locatorv2->data_, lob_locatorv2->extern_header.payload_size_ + lob_locatorv2->extern_header.payload_offset_);
    *pos += lob_locatorv2->extern_header.payload_size_ + lob_locatorv2->extern_header.payload_offset_;
  }
}

static void store_param_ob_lob(MYSQL_STMT *stmt, int column, unsigned char **pos, unsigned long row_nr)
{
  void *buf= ma_get_buffer_offset(stmt, stmt->params[column].buffer_type,
                                  stmt->params[column].buffer, row_nr);
  MYSQL_COMPLEX_BIND_HEADER header;
  header.buffer = buf;

  store_param_ob_lob_complex(pos, &header);
}
static void store_param_object_complex(unsigned char **pos, MYSQL_COMPLEX_BIND_OBJECT *param, my_bool is_plarray_send_maxrarrlen);
static void store_param_array_complex(unsigned char **pos, MYSQL_COMPLEX_BIND_ARRAY *param, my_bool is_plarray_send_maxrarrlen);

static void store_param_all_complex(unsigned char **pos, MYSQL_COMPLEX_BIND_HEADER *header, my_bool is_plarray_send_maxrarrlen)
{
  switch (header->buffer_type) {
    case MYSQL_TYPE_TINY:
      store_param_tinyint_complex(pos, header);
      break;
    case MYSQL_TYPE_SHORT:
      store_param_short_complex(pos, header);
      break;
    case MYSQL_TYPE_LONG:
    case MYSQL_TYPE_CURSOR:
      store_param_int32_complex(pos, header);
      break;
    case MYSQL_TYPE_LONGLONG:
      store_param_int64_complex(pos, header);
      break;
    case MYSQL_TYPE_FLOAT:
      store_param_float_complex(pos, header);
      break;
    case MYSQL_TYPE_DOUBLE:
      store_param_double_complex(pos, header);
      break;
    case MYSQL_TYPE_TIME:
      store_param_time_complex(pos, header);
      break;
    case MYSQL_TYPE_DATE:
      store_param_date_complex(pos, header);
      break;
    case MYSQL_TYPE_DATETIME:
    case MYSQL_TYPE_TIMESTAMP:
      store_param_datetime_complex(pos, header);
      break;
    case MYSQL_TYPE_ORA_BLOB:
    case MYSQL_TYPE_ORA_CLOB:
      store_param_ob_lob_complex(pos, header);
      break;
    case MYSQL_TYPE_OB_TIMESTAMP_NANO:
    case MYSQL_TYPE_OB_TIMESTAMP_WITH_LOCAL_TIME_ZONE:
      store_param_oracle_timestamp_nano_complex(pos, header);
      break;
    case MYSQL_TYPE_OB_TIMESTAMP_WITH_TIME_ZONE:
      store_param_oracle_timestamp_tz_complex(pos, header);
      break;
    case MYSQL_TYPE_TINY_BLOB:
    case MYSQL_TYPE_MEDIUM_BLOB:
    case MYSQL_TYPE_LONG_BLOB:
    case MYSQL_TYPE_OB_RAW:
    case MYSQL_TYPE_BLOB:
    case MYSQL_TYPE_VARCHAR:
    case MYSQL_TYPE_VAR_STRING:
    case MYSQL_TYPE_STRING:
    case MYSQL_TYPE_OB_NVARCHAR2:
    case MYSQL_TYPE_OB_NCHAR:
    case MYSQL_TYPE_DECIMAL:
    case MYSQL_TYPE_NEWDECIMAL:
    case MYSQL_TYPE_OB_NUMBER_FLOAT:
    case MYSQL_TYPE_JSON:
    case MYSQL_TYPE_OB_UROWID:
      store_param_str_complex(pos, (MYSQL_COMPLEX_BIND_STRING *)header);
      break;
    case MYSQL_TYPE_ARRAY:
      store_param_array_complex(pos, (MYSQL_COMPLEX_BIND_ARRAY *)header, is_plarray_send_maxrarrlen);
      break;
    case MYSQL_TYPE_OBJECT:
      store_param_object_complex(pos, (MYSQL_COMPLEX_BIND_OBJECT *)header, is_plarray_send_maxrarrlen);
      break;
    default:
      break;
  }
}
static void store_param_object_complex(unsigned char **pos, MYSQL_COMPLEX_BIND_OBJECT *param, my_bool is_plarray_send_maxrarrlen)
{
  unsigned int count = 0;
  unsigned int i = 0;
  unsigned char *null_buff = NULL;
  uint null_count;
  void *buffer_end = (char*)param->buffer + param->length;
  MYSQL_COMPLEX_BIND_HEADER *header = (MYSQL_COMPLEX_BIND_HEADER *) param->buffer;

  while (header != buffer_end) {
    skip_param_complex(&header);
    count++;
  }

  null_buff = *pos;
  null_count = (count + 7) /8;
  memset(null_buff, 0, null_count);

  *pos += null_count;
  header = (MYSQL_COMPLEX_BIND_HEADER *) param->buffer;

  for (; i < count; i++) {
    if (header->is_null) {
      null_buff[i/8]|=  (uchar) (1 << (i & 7));
    } else {
      store_param_all_complex(pos, header, is_plarray_send_maxrarrlen);
    }
    skip_param_complex(&header);
  }
}
static void store_param_array_complex(unsigned char **pos, MYSQL_COMPLEX_BIND_ARRAY *param, my_bool is_plarray_send_maxrarrlen)
{
  unsigned int i = 0;
  unsigned char *null_buff = NULL;
  uint null_count;
  MYSQL_COMPLEX_BIND_HEADER *header = (MYSQL_COMPLEX_BIND_HEADER *) param->buffer;
  MYSQL_COMPLEX_BIND_PLARRAY *plarray = (MYSQL_COMPLEX_BIND_PLARRAY *) param;
  
  if (TRUE == is_plarray_send_maxrarrlen && MYSQL_TYPE_PLARRAY == param->buffer_type
      && plarray->length != plarray->maxrarr_len) {
    // 这里本来是lenenc_int, 如果是PLArray,并且length != maxrarr_len, 默认第一字节为fe,后面会有8字节
    // 高位4字节为maxrarr_len, 低位4字节为length
    int1store((*pos), 0xfe);
    *pos += 1;
    int4store((*pos), plarray->length);
    *pos += 4;
    int4store((*pos), plarray->maxrarr_len);
    *pos += 4;
  } else {
    *pos = mysql_net_store_length(*pos, param->length);
  }
  null_buff = *pos;
  null_count = (param->length + 7) /8;
  memset(null_buff, 0, null_count);

  *pos += null_count;

  for (; i < param->length; i++) {
    if (header->is_null) {
      null_buff[i/8]|=  (uchar) (1 << (i & 7));
    } else {
      store_param_all_complex(pos, header, is_plarray_send_maxrarrlen);
    }
    skip_param_complex(&header);
  }
}

static void store_param_piece_array_complex(unsigned char **pos, MYSQL_COMPLEX_BIND_ARRAY *param)
{
  // server协商：对于piece分段的数据，只传数组长度
  *pos = mysql_net_store_length(*pos, param->length);
}

static void store_param_object(MYSQL_STMT *stmt, int column, unsigned char **pos, unsigned long row_nr)
{
  void *buf= ma_get_buffer_offset(stmt, stmt->params[column].buffer_type,
                                  stmt->params[column].buffer, row_nr);
  MYSQL_COMPLEX_BIND_OBJECT *header = (MYSQL_COMPLEX_BIND_OBJECT *)buf;
  my_bool is_send_plarray_maxrarrlen = get_support_send_plarray_maxrarr_len(stmt->mysql);

  if (MYSQL_TYPE_ARRAY == header->buffer_type || MYSQL_TYPE_PLARRAY == header->buffer_type) {
    store_param_array_complex(pos, (MYSQL_COMPLEX_BIND_ARRAY *)header, is_send_plarray_maxrarrlen);
  } else if (MYSQL_TYPE_OBJECT == header->buffer_type) {
    store_param_object_complex(pos, header, is_send_plarray_maxrarrlen);
  }
}


#define STORE_PARAM(name) \
static void store_param_##name(MYSQL_STMT *stmt, int column, unsigned char **pos, unsigned long row_nr) \
{ \
  MYSQL_COMPLEX_BIND_HEADER header; \
  void *buf= ma_get_buffer_offset(stmt, stmt->params[column].buffer_type, \
                                  stmt->params[column].buffer, row_nr);\
  header.buffer = buf; \
  store_param_##name##_complex(pos, &header);  \
}

// STORE_PARAM(tinyint)  // unused
// STORE_PARAM(short)    // unused
STORE_PARAM(int32)
// STORE_PARAM(int64)   // unused
// STORE_PARAM(float)   // unused
// STORE_PARAM(double)  // unused
// STORE_PARAM(time)    // unused
// STORE_PARAM(date)    // unused
// STORE_PARAM(datetime)  // unused
STORE_PARAM(oracle_timestamp_nano)
STORE_PARAM(oracle_timestamp_tz)

int store_param(MYSQL_STMT *stmt, int column, unsigned char **p, unsigned long row_nr)
{
  void *buf= ma_get_buffer_offset(stmt, stmt->params[column].buffer_type,
                                  stmt->params[column].buffer, row_nr);
  signed char indicator= ma_get_indicator(stmt, column, row_nr);

  switch (stmt->params[column].buffer_type) {
  case MYSQL_TYPE_TINY:
    int1store(*p, (*(uchar *)buf));
    (*p) += 1;
    break;
  case MYSQL_TYPE_SHORT:
  case MYSQL_TYPE_YEAR:
    int2store(*p, (*(short *)buf));
    (*p) += 2;
    break;
  case MYSQL_TYPE_FLOAT:
    float4store(*p, (*(float *)buf));
    (*p) += 4;
    break;
  case MYSQL_TYPE_DOUBLE:
    float8store(*p, (*(double *)buf));
    (*p) += 8;
    break;
  case MYSQL_TYPE_LONGLONG:
    int8store(*p, (*(ulonglong *)buf));
    (*p) += 8;
    break;
  case MYSQL_TYPE_LONG:
  case MYSQL_TYPE_INT24:
    int4store(*p, (*(int32 *)buf));
    (*p)+= 4;
    break;
  case MYSQL_TYPE_TIME:
  {
    /* binary encoding:
       Offset     Length  Field
       0          1       Length
       1          1       negative
       2-5        4       day
       6          1       hour
       7          1       ninute
       8          1       second;
       9-13       4       second_part
       */
    MYSQL_TIME *t= (MYSQL_TIME *)ma_get_buffer_offset(stmt, stmt->params[column].buffer_type,
                                                      stmt->params[column].buffer, row_nr);
    char t_buffer[MAX_TIME_STR_LEN];
    uint len= 0;

    t_buffer[1]= t->neg ? 1 : 0;
    int4store(t_buffer + 2, t->day);
    t_buffer[6]= (uchar) t->hour;
    t_buffer[7]= (uchar) t->minute;
    t_buffer[8]= (uchar) t->second;
    if (t->second_part)
    {
      int4store(t_buffer + 9, t->second_part);
      len= 12;
    }
    else if (t->day || t->hour || t->minute || t->second)
      len= 8;
    t_buffer[0]= len++;
    memcpy(*p, t_buffer, len);
    (*p)+= len;
    break;
  }
  case MYSQL_TYPE_DATE:
  case MYSQL_TYPE_TIMESTAMP:
  case MYSQL_TYPE_DATETIME:
  {
    /* binary format for date, timestamp and datetime
       Offset     Length  Field
       0          1       Length
       1-2        2       Year
       3          1       Month
       4          1       Day
       5          1       Hour
       6          1       minute
       7          1       second
       8-11       4       secondpart
       */
    MYSQL_TIME *t= (MYSQL_TIME *)ma_get_buffer_offset(stmt, stmt->params[column].buffer_type,
                                                      stmt->params[column].buffer, row_nr);
    char t_buffer[MAX_DATETIME_STR_LEN];
    uint len= 0;

    int2store(t_buffer + 1, t->year);
    t_buffer[3]= (char) t->month;
    t_buffer[4]= (char) t->day;
    t_buffer[5]= (char) t->hour;
    t_buffer[6]= (char) t->minute;
    t_buffer[7]= (char) t->second;
    if (t->second_part)
    {
      int4store(t_buffer + 8, t->second_part);
      len= 11;
    }
    else if (t->hour || t->minute || t->second)
      len= 7;
    else if (t->year || t->month || t->day)
      len= 4;
    else
      len=0;
    t_buffer[0]= len++;
    memcpy(*p, t_buffer, len);
    (*p)+= len;
    break;
  }
  case MYSQL_TYPE_TINY_BLOB:
  case MYSQL_TYPE_MEDIUM_BLOB:
  case MYSQL_TYPE_LONG_BLOB:
  case MYSQL_TYPE_BLOB:
  case MYSQL_TYPE_OB_RAW:
  case MYSQL_TYPE_VARCHAR:
  case MYSQL_TYPE_VAR_STRING:
  case MYSQL_TYPE_STRING:
  case MYSQL_TYPE_JSON:
  case MYSQL_TYPE_DECIMAL:
  case MYSQL_TYPE_NEWDECIMAL:
  {
    ulong len;
    /* to is after p. The latter hasn't been moved */
    uchar *to;

    if (indicator == STMT_INDICATOR_NTS)
      len= -1;
    else
      len= ma_get_length(stmt, column, row_nr);

    if (len == (ulong)-1)
      len= (ulong)strlen((char *)buf);

    to = mysql_net_store_length(*p, len);

    if (len)
      memcpy(to, buf, len);
    (*p) = to + len;
    break;
  }
  //begin add for oboracle type support
    case MYSQL_TYPE_CURSOR:
      store_param_int32(stmt, column, p, row_nr);
      break;
    case MYSQL_TYPE_OB_TIMESTAMP_WITH_TIME_ZONE:
      store_param_oracle_timestamp_tz(stmt, column, p, row_nr);
      break;
    case MYSQL_TYPE_OB_TIMESTAMP_NANO:
    case MYSQL_TYPE_OB_TIMESTAMP_WITH_LOCAL_TIME_ZONE:
      store_param_oracle_timestamp_nano(stmt, column, p, row_nr);
      break;
    case MYSQL_TYPE_ORA_BLOB:
    case MYSQL_TYPE_ORA_CLOB:
      store_param_ob_lob(stmt, column, p, row_nr);
      break;
    case MYSQL_TYPE_OB_NVARCHAR2:
    case MYSQL_TYPE_OB_NCHAR:
    case MYSQL_TYPE_OB_NUMBER_FLOAT:
    case MYSQL_TYPE_OB_UROWID:
      {
        ulong len;
         if (indicator == STMT_INDICATOR_NTS)
          len= -1;
        else
          len= ma_get_length(stmt, column, row_nr);
        if (len == (ulong)-1)
          len= (ulong)strlen((char *)buf);
        store_param_str(stmt, column, p, row_nr, len);
      }
      break;
    case MYSQL_TYPE_OBJECT:
      store_param_object(stmt, column, p, row_nr);
      break;
    //end add for oboracle type support
  default:
    /* unsupported parameter type */
    SET_CLIENT_STMT_ERROR(stmt, CR_UNSUPPORTED_PARAM_TYPE, SQLSTATE_UNKNOWN, 0);
    return 1;
  }
  return 0;
}

static ulong calculate_param_complex_len(MYSQL_COMPLEX_BIND_HEADER *header);
static ulong calculate_param_oracle_timestamp_len(ORACLE_TIME * tm);

static ulong calculate_param_object_len(MYSQL_COMPLEX_BIND_OBJECT *param)
{
  ulong len = 0;
  int i = 0;
  void *buffer_end = (char*)param->buffer + param->length;
  MYSQL_COMPLEX_BIND_HEADER *header = (MYSQL_COMPLEX_BIND_HEADER *) param->buffer;

  while (header != buffer_end) {
    i++;
    len += calculate_param_complex_len(header);
    skip_param_complex(&header);
  }
  len += (i + 7) /8;

  return len;
}
int convert_type_to_complex(enum enum_field_types type)
{
  switch (type) {
    case MYSQL_TYPE_TINY:
    case MYSQL_TYPE_SHORT:
    case MYSQL_TYPE_LONG:
    case MYSQL_TYPE_LONGLONG:
    case MYSQL_TYPE_FLOAT:
    case MYSQL_TYPE_DOUBLE:
    case MYSQL_TYPE_TIME:
    case MYSQL_TYPE_DATE:
    case MYSQL_TYPE_DATETIME:
    case MYSQL_TYPE_TIMESTAMP:
    case MYSQL_TYPE_CURSOR:
    case MYSQL_TYPE_ORA_BLOB:
    case MYSQL_TYPE_ORA_CLOB:
    case MYSQL_TYPE_OB_TIMESTAMP_NANO:
    case MYSQL_TYPE_OB_TIMESTAMP_WITH_TIME_ZONE:
    case MYSQL_TYPE_OB_TIMESTAMP_WITH_LOCAL_TIME_ZONE:
      return sizeof(MYSQL_COMPLEX_BIND_HEADER);
    case MYSQL_TYPE_TINY_BLOB:
    case MYSQL_TYPE_MEDIUM_BLOB:
    case MYSQL_TYPE_LONG_BLOB:
    case MYSQL_TYPE_OB_RAW:
    case MYSQL_TYPE_BLOB:
    case MYSQL_TYPE_VARCHAR:
    case MYSQL_TYPE_VAR_STRING:
    case MYSQL_TYPE_STRING:
    case MYSQL_TYPE_OB_NVARCHAR2:
    case MYSQL_TYPE_OB_NCHAR:
    case MYSQL_TYPE_DECIMAL:
    case MYSQL_TYPE_NEWDECIMAL:
    case MYSQL_TYPE_OB_NUMBER_FLOAT:
    case MYSQL_TYPE_JSON:
    case MYSQL_TYPE_OB_UROWID:
      return sizeof(MYSQL_COMPLEX_BIND_STRING);
    default:
      return sizeof(MYSQL_COMPLEX_BIND_HEADER);
  }
}

void skip_param_complex(MYSQL_COMPLEX_BIND_HEADER **param)
{
  MYSQL_COMPLEX_BIND_HEADER *header = *param;
  switch (header->buffer_type) {
    case MYSQL_TYPE_TINY:
    case MYSQL_TYPE_SHORT:
    case MYSQL_TYPE_LONG:
    case MYSQL_TYPE_LONGLONG:
    case MYSQL_TYPE_FLOAT:
    case MYSQL_TYPE_DOUBLE:
    case MYSQL_TYPE_TIME:
    case MYSQL_TYPE_DATE:
    case MYSQL_TYPE_DATETIME:
    case MYSQL_TYPE_TIMESTAMP:
    case MYSQL_TYPE_CURSOR:
    case MYSQL_TYPE_ORA_BLOB:
    case MYSQL_TYPE_ORA_CLOB:
    case MYSQL_TYPE_OB_TIMESTAMP_NANO:
    case MYSQL_TYPE_OB_TIMESTAMP_WITH_TIME_ZONE:
    case MYSQL_TYPE_OB_TIMESTAMP_WITH_LOCAL_TIME_ZONE:
      (*param)++;
      break;
    case MYSQL_TYPE_TINY_BLOB:
    case MYSQL_TYPE_MEDIUM_BLOB:
    case MYSQL_TYPE_LONG_BLOB:
    case MYSQL_TYPE_OB_RAW:
    case MYSQL_TYPE_BLOB:
    case MYSQL_TYPE_VARCHAR:
    case MYSQL_TYPE_VAR_STRING:
    case MYSQL_TYPE_STRING:
    case MYSQL_TYPE_OB_NVARCHAR2:
    case MYSQL_TYPE_OB_NCHAR:
    case MYSQL_TYPE_DECIMAL:
    case MYSQL_TYPE_NEWDECIMAL:
    case MYSQL_TYPE_OB_NUMBER_FLOAT:
    case MYSQL_TYPE_JSON:
    case MYSQL_TYPE_OB_UROWID:
      *param = (MYSQL_COMPLEX_BIND_HEADER *)(((MYSQL_COMPLEX_BIND_STRING *)header) + 1);
      /*
        For variable length types user must set either length or
        buffer_length.
      */
      break;
    case MYSQL_TYPE_ARRAY:
      *param = (MYSQL_COMPLEX_BIND_HEADER *)(((MYSQL_COMPLEX_BIND_ARRAY *)header) + 1);
      break;
    case MYSQL_TYPE_OBJECT:
      *param = (MYSQL_COMPLEX_BIND_HEADER *)(((MYSQL_COMPLEX_BIND_OBJECT *)header) + 1);
      break;
    default:
      break;
  }
}

static ulong calculate_param_plarray_len(MYSQL_COMPLEX_BIND_PLARRAY *param)
{
  ulong len = 0;
  uint i = 0;
  MYSQL_COMPLEX_BIND_HEADER *header = (MYSQL_COMPLEX_BIND_HEADER *) param->buffer;

  //这里按照协议本来是lenenc_int类型，为了扩展这里默认9字节，高位4字节存放maxrarr_len，低位4字节存放length
  //只有maxrarr_len不等于length的时候才需要都穿，否则只传一个
  if (param->maxrarr_len != param->length) {
    len += 9;
  } else {
    len += mysql_store_length_size(param->length);
  }
  len += (param->length + 7) /8;

  for (i = 0; i < param->length; i++) {
    len += calculate_param_complex_len(header);
    skip_param_complex(&header);
  }

  return len;
}

static ulong calculate_param_array_len(MYSQL_COMPLEX_BIND_ARRAY *param)
{
  ulong len = 0;
  uint i = 0;
  MYSQL_COMPLEX_BIND_HEADER *header = (MYSQL_COMPLEX_BIND_HEADER *) param->buffer;

  len += mysql_store_length_size(param->length);
  len += (param->length + 7) /8;

  for (i = 0; i < param->length; i++) {
    len += calculate_param_complex_len(header);
    skip_param_complex(&header);
  }

  return len;
}

static ulong calculate_param_complex_len(MYSQL_COMPLEX_BIND_HEADER *header)
{
  ulong len = 0;
  switch (header->buffer_type) {
    case MYSQL_TYPE_TINY:
      len = 1;
      break;
    case MYSQL_TYPE_SHORT:
      len = 2;
      break;
    case MYSQL_TYPE_LONG:
    case MYSQL_TYPE_CURSOR:
      len = 4;
      break;
    case MYSQL_TYPE_LONGLONG:
      len = 8;
      break;
    case MYSQL_TYPE_FLOAT:
      len = 4;
      break;
    case MYSQL_TYPE_DOUBLE:
      len = 8;
      break;
    case MYSQL_TYPE_TIME:
      len = MAX_TIME_REP_LENGTH;
      break;
    case MYSQL_TYPE_DATE:
      len = MAX_DATE_REP_LENGTH;
      break;
    case MYSQL_TYPE_DATETIME:
    case MYSQL_TYPE_TIMESTAMP:
      len = MAX_DATETIME_REP_LENGTH;
      break;
    case MYSQL_TYPE_ORA_BLOB:
    case MYSQL_TYPE_ORA_CLOB: {
      OB_LOB_LOCATOR *ob_lob_locator= (OB_LOB_LOCATOR *) header->buffer;
      len = MAX_OB_LOB_LOCATOR_HEADER_LENGTH + ob_lob_locator->payload_offset_ + ob_lob_locator->payload_size_;
      len += mysql_store_length_size(len);
      break;
    }
    case MYSQL_TYPE_OB_TIMESTAMP_WITH_TIME_ZONE:
      len = calculate_param_oracle_timestamp_len((ORACLE_TIME *) header->buffer);
      break;
    case MYSQL_TYPE_OB_TIMESTAMP_NANO:
    case MYSQL_TYPE_OB_TIMESTAMP_WITH_LOCAL_TIME_ZONE:
      len = MAX_ORACLE_TIMESTAMP_REP_LENGTH;
      break;
    case MYSQL_TYPE_TINY_BLOB:
    case MYSQL_TYPE_MEDIUM_BLOB:
    case MYSQL_TYPE_LONG_BLOB:
    case MYSQL_TYPE_OB_RAW:
    case MYSQL_TYPE_BLOB:
    case MYSQL_TYPE_VARCHAR:
    case MYSQL_TYPE_VAR_STRING:
    case MYSQL_TYPE_STRING:
    case MYSQL_TYPE_OB_NVARCHAR2:
    case MYSQL_TYPE_OB_NCHAR:
    case MYSQL_TYPE_DECIMAL:
    case MYSQL_TYPE_NEWDECIMAL:
    case MYSQL_TYPE_OB_NUMBER_FLOAT:
    case MYSQL_TYPE_JSON:
    case MYSQL_TYPE_OB_UROWID:
      len = ((MYSQL_COMPLEX_BIND_STRING *) header)->length;
      len += mysql_store_length_size(len);
      break;
    case MYSQL_TYPE_ARRAY:
      len = calculate_param_array_len((MYSQL_COMPLEX_BIND_ARRAY *)header);
      break;
    case MYSQL_TYPE_OBJECT:
      len = calculate_param_object_len((MYSQL_COMPLEX_BIND_OBJECT *)header);
      break;
    default:
      len = 0;
  }

  if (header->is_null) {
    return 0;
  } else {
    return len;
  }
}

static ulong calculate_param_oracle_timestamp_len(ORACLE_TIME * tm)
{
  ulong len = 17;

  if (tm->tz_name != NULL) {
    len += strlen(tm->tz_name);
  }

  if (tm->tz_abbr != NULL) {
    len += strlen(tm->tz_abbr);
  }

  return len;
}

static ulong calculate_param_ob_lob_len(MYSQL_BIND *param)
{
  ulong len = 0;

  if (NULL !=  param->buffer) {
    OB_LOB_LOCATOR *ob_lob_locator= (OB_LOB_LOCATOR *) param->buffer;
    if (OBCLIENT_LOB_LOCATORV1 == get_ob_lob_locator_version(ob_lob_locator)) {
      len = MAX_OB_LOB_LOCATOR_HEADER_LENGTH + ob_lob_locator->payload_offset_ + ob_lob_locator->payload_size_;
      len += mysql_store_length_size(len);
    } else if (OBCLIENT_LOB_LOCATORV2 == get_ob_lob_locator_version(ob_lob_locator)) {
      OB_LOB_LOCATOR_V2 *ob_lob_locatorv2 = (OB_LOB_LOCATOR_V2*)param->buffer;
      len = MAX_OB_LOB_LOCATOR_HEADER_LENGTH + ob_lob_locatorv2->extern_header.payload_offset_ + ob_lob_locatorv2->extern_header.payload_size_;
      len += mysql_store_length_size(len);
    }
  }
  return len;
}

static ulong calculate_param_len(MYSQL_BIND *param)
{
  if (MYSQL_TYPE_OBJECT == param->buffer_type) {
    MYSQL_COMPLEX_BIND_OBJECT *header = (MYSQL_COMPLEX_BIND_OBJECT *)param->buffer;
    if (MYSQL_TYPE_ARRAY == header->buffer_type || MYSQL_TYPE_PLARRAY == header->buffer_type) {
      if (TRUE == get_support_send_plarray_maxrarr_len(param->mysql)) {
        return calculate_param_plarray_len((MYSQL_COMPLEX_BIND_PLARRAY *)header); 
      } else {
        return calculate_param_array_len((MYSQL_COMPLEX_BIND_ARRAY *)header);
      }
    } else {
      return calculate_param_object_len(header);
    }
  } else if (MYSQL_TYPE_OB_TIMESTAMP_WITH_TIME_ZONE == param->buffer_type) {
    return calculate_param_oracle_timestamp_len((ORACLE_TIME *)param->buffer);
  } else if (MYSQL_TYPE_ORA_BLOB == param->buffer_type || MYSQL_TYPE_ORA_CLOB == param->buffer_type) {
    return calculate_param_ob_lob_len(param);
  } else {
    return *param->length;
  }
}

/* {{{ mysqlnd_stmt_execute_generate_simple_request */
unsigned char* mysql_stmt_execute_generate_simple_request(MYSQL_STMT *stmt, size_t *request_len)
{
  /* execute packet has the following format:
     Offset   Length      Description
     -----------------------------------------
     0             4      Statement id
     4             1      Flags (cursor type)
     5             4      Iteration count
     -----------------------------------------
     if (stmt->param_count):
     6  (paramcount+7)/8  null bitmap
     ------------------------------------------
     if (stmt->send_types_to_server):
     param_count*2    parameter types
     1st byte: parameter type
     2nd byte flag:
              unsigned flag (32768)
              indicator variable exists (16384)
     ------------------------------------------
     n      data from bind_buffer

     */

  size_t length= 1024;
  size_t free_bytes= 0;
  size_t null_byte_offset= 0;
  uint i;

  uchar *start= NULL, *p;

  /* preallocate length bytes */
  /* check: gr */
  if (!(start= p= (uchar *)malloc(length)))
    goto mem_error;

  int4store(p, stmt->stmt_id);
  p += STMT_ID_LENGTH;

  /* flags is 4 bytes, we store just 1 */
  int1store(p, (unsigned char) stmt->flags);
  p++;

  int4store(p, 1);
  p+= 4;

  if (stmt->param_count)
  {
    size_t null_count= (stmt->param_count + 7) / 8;

    free_bytes= length - (p - start);
    if (null_count + 20 > free_bytes)
    {
      size_t offset= p - start;
      length+= offset + null_count + 20;
      if (!(start= (uchar *)realloc(start, length)))
        goto mem_error;
      p= start + offset;
    }

    null_byte_offset= p - start;
    memset(p, 0, null_count);
    p += null_count;

    int1store(p, stmt->send_types_to_server);
    p++;

    free_bytes= length - (p - start);

    /* Store type information:
       2 bytes per type
       */
    if (stmt->send_types_to_server)
    {
      if (free_bytes < stmt->param_count * 2 + 20)
      {
        size_t offset= p - start;
        length= offset + stmt->param_count * 2 + 20;
        if (!(start= (uchar *)realloc(start, length)))
          goto mem_error;
        p= start + offset;
      }
      for (i = 0; i < stmt->param_count; i++)
      {
        store_param_type(stmt->mysql, &p, &stmt->params[i]);
      }
    }

    /* calculate data size */
    for (i=0; i < stmt->param_count; i++)
    {
      size_t size= 0;
      my_bool has_data= TRUE;

      if (stmt->params[i].long_data_used)
      {
        has_data= FALSE;
        stmt->params[i].long_data_used= 0;
      }

      //NULL value
      if (!stmt->params[i].buffer ||
            (stmt->params[i].is_null && *stmt->params[i].is_null))
      {
        has_data= FALSE;
      }

      if (has_data)
      {
        switch (stmt->params[i].buffer_type) {
        case MYSQL_TYPE_NULL:
          has_data= FALSE;
          break;
        case MYSQL_TYPE_TINY:
        case MYSQL_TYPE_SHORT:
        case MYSQL_TYPE_YEAR:
        case MYSQL_TYPE_INT24:
        case MYSQL_TYPE_LONG:
        case MYSQL_TYPE_LONGLONG:
        case MYSQL_TYPE_FLOAT:
        case MYSQL_TYPE_DOUBLE:
        case MYSQL_TYPE_CURSOR:
          /* type len has definitly defined in mysql_ps_fetch_functions */
          size+= mysql_ps_fetch_functions[stmt->params[i].buffer_type].pack_len;
          break;
        case MYSQL_TYPE_TINY_BLOB:
        case MYSQL_TYPE_MEDIUM_BLOB:
        case MYSQL_TYPE_LONG_BLOB:
        case MYSQL_TYPE_BLOB:
        case MYSQL_TYPE_VARCHAR:
        case MYSQL_TYPE_VAR_STRING:
        case MYSQL_TYPE_STRING:
        case MYSQL_TYPE_JSON:
        case MYSQL_TYPE_DECIMAL:
        case MYSQL_TYPE_NEWDECIMAL:
        case MYSQL_TYPE_GEOMETRY:
        case MYSQL_TYPE_NEWDATE:
        case MYSQL_TYPE_ENUM:
        case MYSQL_TYPE_BIT:
        case MYSQL_TYPE_SET:
        case MYSQL_TYPE_OB_RAW:
        case MYSQL_TYPE_OB_TIMESTAMP_NANO:
        case MYSQL_TYPE_OB_UROWID:
        case MYSQL_TYPE_OB_TIMESTAMP_WITH_LOCAL_TIME_ZONE:
        case MYSQL_TYPE_TIME:
        case MYSQL_TYPE_DATE:
        case MYSQL_TYPE_DATETIME:
        case MYSQL_TYPE_TIMESTAMP:
          size+= 5; /* max 8 bytes for size */
          size+= (size_t)ma_get_length(stmt, i, 0);
          break;
        //add to support oboracle type
        case MYSQL_TYPE_ORA_BLOB:
        case MYSQL_TYPE_ORA_CLOB:
        case MYSQL_TYPE_OB_TIMESTAMP_WITH_TIME_ZONE:
        case MYSQL_TYPE_OBJECT:
          size += (size_t) calculate_param_len(&stmt->params[i]);
          break;
        //end add to support oboracle type
        default:
          //get buffer len from param buffer
          size += (size_t)ma_get_length(stmt, i, 0);
          break;
        }
      }
      free_bytes= length - (p - start);
      if (free_bytes < size + 20)
      {
        size_t offset= p - start;
        length= MAX(2 * length, offset + size + 20);
        if (!(start= (uchar *)realloc(start, length)))
          goto mem_error;
        p= start + offset;
      }
      if (((stmt->params[i].is_null && *stmt->params[i].is_null) ||
             stmt->params[i].buffer_type == MYSQL_TYPE_NULL ||
             !stmt->params[i].buffer))
      {
        has_data= FALSE;
        (start + null_byte_offset)[i/8] |= (unsigned char) (1 << (i & 7));
      }

      if (has_data)
      {
        store_param(stmt, i, &p, 0);
      }
    }
  }
  stmt->send_types_to_server= 0;
  *request_len = (size_t)(p - start);
  return start;
mem_error:
  SET_CLIENT_STMT_ERROR(stmt, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
  free(start);
  *request_len= 0;
  return NULL;
}
/* }}} */

/* {{{ mysql_stmt_skip_paramset */
my_bool mysql_stmt_skip_paramset(MYSQL_STMT *stmt, uint row)
{
  uint i;
  for (i=0; i < stmt->param_count; i++)
  {
    if (ma_get_indicator(stmt, i, row) == STMT_INDICATOR_IGNORE_ROW)
      return '\1';
  }
  
  return '\0';
}
/* }}} */

/* {{{ mysql_stmt_execute_generate_bulk_request */
unsigned char* mysql_stmt_execute_generate_bulk_request(MYSQL_STMT *stmt, size_t *request_len)
{
  /* execute packet has the following format:
     Offset   Length      Description
     -----------------------------------------
     0             4      Statement id
     4             2      Flags (cursor type):
                            STMT_BULK_FLAG_CLIENT_SEND_TYPES = 128
                            STMT_BULK_FLAG_INSERT_ID_REQUEST = 64
     -----------------------------------------
     if (stmt->send_types_to_server):
     for (i=0; i < param_count; i++)
       1st byte: parameter type
       2nd byte flag:
              unsigned flag (32768)
     ------------------------------------------
     for (i=0; i < param_count; i++)
                   1      indicator variable
                            STMT_INDICATOR_NONE 0
                            STMT_INDICATOR_NULL 1
                            STMT_INDICATOR_DEFAULT 2
                            STMT_INDICATOR_IGNORE 3
                            STMT_INDICATOR_SKIP_SET 4
                   n      data from bind buffer

     */

  size_t length= 1024;
  size_t free_bytes= 0;
  ushort flags= 0;
  uint i, j;

  uchar *start= NULL, *p;

  if (!MARIADB_STMT_BULK_SUPPORTED(stmt))
  {
    stmt_set_error(stmt, CR_FUNCTION_NOT_SUPPORTED, "IM001",
                   CER(CR_FUNCTION_NOT_SUPPORTED), "Bulk operation");
    return NULL;
  }

  if (!stmt->param_count)
  {
    stmt_set_error(stmt, CR_BULK_WITHOUT_PARAMETERS, "IM001",
                   CER(CR_BULK_WITHOUT_PARAMETERS));
    return NULL;
  }

  /* preallocate length bytes */
  if (!(start= p= (uchar *)malloc(length)))
    goto mem_error;

  int4store(p, stmt->stmt_id);
  p += STMT_ID_LENGTH;

  /* todo: request to return auto generated ids */
  if (stmt->send_types_to_server)
    flags|= STMT_BULK_FLAG_CLIENT_SEND_TYPES;
  int2store(p, flags);
  p+=2;

  /* When using mariadb_stmt_execute_direct stmt->paran_count is
     not knowm, so we need to assign prebind_params, which was previously
     set by mysql_stmt_attr_set
  */
  if (!stmt->param_count && stmt->prebind_params)
    stmt->param_count= stmt->prebind_params;

  if (stmt->param_count)
  {
    free_bytes= length - (p - start);

    /* Store type information:
       2 bytes per type
       */
    if (stmt->send_types_to_server)
    {
      if (free_bytes < stmt->param_count * 2 + 20)
      {
        size_t offset= p - start;
        length= offset + stmt->param_count * 2 + 20;
        if (!(start= (uchar *)realloc(start, length)))
          goto mem_error;
        p= start + offset;
      }
      for (i = 0; i < stmt->param_count; i++)
      {
        /* this differs from mysqlnd, c api supports unsigned !! */
        uint buffer_type= stmt->params[i].buffer_type | (stmt->params[i].is_unsigned ? 32768 : 0);
        int2store(p, buffer_type);
        p+= 2;
      }
    }

    /* calculate data size */
    for (j=0; j < stmt->array_size; j++)
    {
      /* If callback for parameters was specified, we need to
         update bind information for new row */
      if (stmt->param_callback)
        stmt->param_callback(stmt->user_data, stmt->params, j);

      if (mysql_stmt_skip_paramset(stmt, j))
        continue;

      for (i=0; i < stmt->param_count; i++)
      {
        size_t size= 0;
        my_bool has_data= TRUE;
        signed char indicator= ma_get_indicator(stmt, i, j);
        /* check if we need to send data */
        if (indicator > 0)
          has_data= FALSE;
        size= 1;

        /* Please note that mysql_stmt_send_long_data is not supported
           current when performing bulk execute */

        if (has_data)
        {
          switch (stmt->params[i].buffer_type) {
          case MYSQL_TYPE_NULL:
            has_data= FALSE;
            indicator= STMT_INDICATOR_NULL;
            break;
          case MYSQL_TYPE_TINY_BLOB:
          case MYSQL_TYPE_MEDIUM_BLOB:
          case MYSQL_TYPE_LONG_BLOB:
          case MYSQL_TYPE_BLOB:
          case MYSQL_TYPE_VARCHAR:
          case MYSQL_TYPE_VAR_STRING:
          case MYSQL_TYPE_STRING:
          case MYSQL_TYPE_JSON:
          case MYSQL_TYPE_DECIMAL:
          case MYSQL_TYPE_NEWDECIMAL:
          case MYSQL_TYPE_GEOMETRY:
          case MYSQL_TYPE_NEWDATE:
          case MYSQL_TYPE_ENUM:
          case MYSQL_TYPE_BIT:
          case MYSQL_TYPE_SET:
            size+= 5; /* max 8 bytes for size */
            if (!stmt->param_callback)
            {
              if (indicator == STMT_INDICATOR_NTS ||
                (!stmt->row_size && ma_get_length(stmt,i,j) == -1))
              {
                  size+= strlen(ma_get_buffer_offset(stmt,
                                                     stmt->params[i].buffer_type,
                                                     stmt->params[i].buffer,j));
              }
              else
                size+= (size_t)ma_get_length(stmt, i, j);
            }
            else {
              size+= stmt->params[i].buffer_length;
            }
            break;
          default:
            size+= mysql_ps_fetch_functions[stmt->params[i].buffer_type].pack_len;
            break;
          }
        }
        free_bytes= length - (p - start);
        if (free_bytes < size + 20)
        {
          size_t offset= p - start;
          length= MAX(2 * length, offset + size + 20);
          if (!(start= (uchar *)realloc(start, length)))
            goto mem_error;
          p= start + offset;
        }

        int1store(p, indicator > 0 ? indicator : 0);
        p++;
        if (has_data) {
          store_param(stmt, i, &p, (stmt->param_callback) ? 0 : j);
        }
      }
    }

  }
  stmt->send_types_to_server= 0;
  *request_len = (size_t)(p - start);
  return start;
mem_error:
  SET_CLIENT_STMT_ERROR(stmt, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
  free(start);
  *request_len= 0;
  return NULL;
}
/* }}} */
/*!
 *******************************************************************************

 \fn        unsigned long long mysql_stmt_affected_rows
 \brief     returns the number of affected rows from last mysql_stmt_execute
 call

 \param[in]  stmt The statement handle
 *******************************************************************************
 */
unsigned long long STDCALL mysql_stmt_affected_rows(MYSQL_STMT *stmt)
{
  return stmt->upsert_status.affected_rows;
}

my_bool STDCALL mysql_stmt_attr_get(MYSQL_STMT *stmt, enum enum_stmt_attr_type attr_type, void *value)
{
  switch (attr_type) {
    case STMT_ATTR_STATE:
      *(enum mysql_stmt_state *)value= stmt->state;
      break;
    case STMT_ATTR_UPDATE_MAX_LENGTH:
      *(my_bool *)value= stmt->update_max_length;
      break;
    case STMT_ATTR_CURSOR_TYPE:
      *(unsigned long *)value= stmt->flags;
      break;
    case STMT_ATTR_PREFETCH_ROWS:
      *(unsigned long *)value= stmt->prefetch_rows;
      break;
    case STMT_ATTR_PREBIND_PARAMS:
      *(unsigned int *)value= stmt->prebind_params;
      break;
    case STMT_ATTR_ARRAY_SIZE:
      *(unsigned int *)value= stmt->array_size;
      break;
    case STMT_ATTR_ROW_SIZE:
      *(size_t *)value= stmt->row_size;
      break;
    case STMT_ATTR_CB_USER_DATA:
      *((void **)value) = stmt->user_data;
      break;
    default:
      return(1);
  }
  return(0);
}

my_bool STDCALL mysql_stmt_attr_set(MYSQL_STMT *stmt, enum enum_stmt_attr_type attr_type, const void *value)
{
  switch (attr_type) {
  case STMT_ATTR_UPDATE_MAX_LENGTH:
    stmt->update_max_length= *(my_bool *)value;
    break;
  case STMT_ATTR_CURSOR_TYPE:
    if (*(ulong *)value > (unsigned long) CURSOR_TYPE_READ_ONLY)
    {
      SET_CLIENT_STMT_ERROR(stmt, CR_NOT_IMPLEMENTED, SQLSTATE_UNKNOWN, 0);
      return(1);
    }
    stmt->flags = *(ulong *)value;
    break;
  case STMT_ATTR_PREFETCH_ROWS:
    if (*(ulong *)value == 0)
      *(long *)value= MYSQL_DEFAULT_PREFETCH_ROWS;
    else
      stmt->prefetch_rows= *(long *)value;
    break;
  case STMT_ATTR_ARRAY_BIND:
  {
    ulong array_bind_type;
    array_bind_type = value ? *(ulong*) value : 0UL;

    if (CURSOR_TYPE_ARRAY_BIND & array_bind_type) {
      stmt->flags |= CURSOR_TYPE_ARRAY_BIND;
    } else {
      stmt->flags &= ~CURSOR_TYPE_ARRAY_BIND;
    }

    if (CURSOR_TYPE_SAVE_EXCEPTION & array_bind_type) {
      stmt->flags |= CURSOR_TYPE_SAVE_EXCEPTION;
    } else {
      stmt->flags &= ~CURSOR_TYPE_SAVE_EXCEPTION;
    }
    break;
  }
  case STMT_ATTR_PREBIND_PARAMS:
    if (stmt->state > MYSQL_STMT_INITTED)
    {
      mysql_stmt_internal_reset(stmt, 1);
      net_stmt_close(stmt, 0);
      stmt->state= MYSQL_STMT_INITTED;
      stmt->params= 0;
    }
    stmt->prebind_params= *(unsigned int *)value;
    break;
  case STMT_ATTR_ARRAY_SIZE:
    stmt->array_size= *(unsigned int *)value;
    break;
  case STMT_ATTR_ROW_SIZE:
    stmt->row_size= *(size_t *)value;
    break;
  case STMT_ATTR_CB_RESULT:
    stmt->result_callback= (ps_result_callback)value;
    break;
  case STMT_ATTR_CB_PARAM:
    stmt->param_callback= (ps_param_callback)value;
    break;
  case STMT_ATTR_CB_USER_DATA:
    stmt->user_data= (void *)value;
    break;
  default:
    SET_CLIENT_STMT_ERROR(stmt, CR_NOT_IMPLEMENTED, SQLSTATE_UNKNOWN, 0);
    return(1);
  }
  return(0);
}

my_bool STDCALL mysql_stmt_bind_param(MYSQL_STMT *stmt, MYSQL_BIND *bind)
{
  MYSQL *mysql= stmt->mysql;

  if (!mysql)
  {
    SET_CLIENT_STMT_ERROR(stmt, CR_SERVER_LOST, SQLSTATE_UNKNOWN, 0);
    return(1);
  }

  /* If number of parameters was specified via mysql_stmt_attr_set we need to realloc
     them, e.g. for mariadb_stmt_execute_direct()
     */
  if ((stmt->state < MYSQL_STMT_PREPARED || stmt->state >= MYSQL_STMT_EXECUTED) &&
       stmt->prebind_params > 0)
  {
    if (!stmt->params && stmt->prebind_params)
    {
      if (!(stmt->params= (MYSQL_BIND *)ma_alloc_root(&stmt->mem_root, stmt->prebind_params * sizeof(MYSQL_BIND))))
      {
        SET_CLIENT_STMT_ERROR(stmt, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
        return(1);
      }
      memset(stmt->params, '\0', stmt->prebind_params * sizeof(MYSQL_BIND));
    }
    stmt->param_count= stmt->prebind_params;
  }
  else if (stmt->state < MYSQL_STMT_PREPARED) {
    SET_CLIENT_STMT_ERROR(stmt, CR_NO_PREPARE_STMT, SQLSTATE_UNKNOWN, 0);
    return(1);
  }

  if (stmt->param_count && bind)
  {
    uint i;

    memcpy(stmt->params, bind, sizeof(MYSQL_BIND) * stmt->param_count);
    stmt->send_types_to_server= 1;

    for (i=0; i < stmt->param_count; i++)
    {
      if (stmt->mysql->methods->db_supported_buffer_type &&
          !stmt->mysql->methods->db_supported_buffer_type(stmt->params[i].buffer_type))
      {
        SET_CLIENT_STMT_ERROR(stmt, CR_UNSUPPORTED_PARAM_TYPE, SQLSTATE_UNKNOWN, 0);
        return(1);
      }
      if (!stmt->params[i].is_null)
        stmt->params[i].is_null= &is_not_null;

      if (stmt->params[i].long_data_used)
        stmt->params[i].long_data_used= 0;

      if (!stmt->params[i].length)
        stmt->params[i].length= &stmt->params[i].buffer_length;

      switch(stmt->params[i].buffer_type) {
      case MYSQL_TYPE_NULL:
        stmt->params[i].is_null= &is_null;
        break;
      case MYSQL_TYPE_TINY:
        stmt->params[i].buffer_length= 1;
        break;
      case MYSQL_TYPE_SHORT:
      case MYSQL_TYPE_YEAR:
        stmt->params[i].buffer_length= 2;
        break;
      case MYSQL_TYPE_LONG:
      case MYSQL_TYPE_FLOAT:
        stmt->params[i].buffer_length= 4;
        break;
      case MYSQL_TYPE_LONGLONG:
      case MYSQL_TYPE_DOUBLE:
        stmt->params[i].buffer_length= 8;
        break;
      case MYSQL_TYPE_DATETIME:
      case MYSQL_TYPE_TIMESTAMP:
        stmt->params[i].buffer_length= 12;
        break;
      case MYSQL_TYPE_TIME:
        stmt->params[i].buffer_length= 13;
        break;
      case MYSQL_TYPE_DATE:
        stmt->params[i].buffer_length= 5;
        break;
      //begin add for oboracle type support
      case MYSQL_TYPE_CURSOR:
        stmt->params[i].buffer_length= 4;
        break;
      case MYSQL_TYPE_OB_TIMESTAMP_WITH_TIME_ZONE:
        break;
      case MYSQL_TYPE_OB_TIMESTAMP_NANO:
      case MYSQL_TYPE_OB_TIMESTAMP_WITH_LOCAL_TIME_ZONE:
        stmt->params[i].buffer_length= 13;
        break;
      case MYSQL_TYPE_ORA_BLOB:
      case MYSQL_TYPE_ORA_CLOB:
      case MYSQL_TYPE_OB_RAW:
      case MYSQL_TYPE_VARCHAR:
      case MYSQL_TYPE_OB_NVARCHAR2:
      case MYSQL_TYPE_OB_NCHAR:
      case MYSQL_TYPE_OB_NUMBER_FLOAT:
      case MYSQL_TYPE_OB_UROWID:
      case MYSQL_TYPE_OBJECT:
      //end add for oboracle type support
      case MYSQL_TYPE_STRING:
      case MYSQL_TYPE_JSON:
      case MYSQL_TYPE_VAR_STRING:
      case MYSQL_TYPE_BLOB:
      case MYSQL_TYPE_TINY_BLOB:
      case MYSQL_TYPE_MEDIUM_BLOB:
      case MYSQL_TYPE_LONG_BLOB:
      case MYSQL_TYPE_DECIMAL:
      case MYSQL_TYPE_NEWDECIMAL:
        break;
      default:
        SET_CLIENT_STMT_ERROR(stmt, CR_UNSUPPORTED_PARAM_TYPE, SQLSTATE_UNKNOWN, 0);
        return(1);
        break;
      }
    }
  }
  stmt->bind_param_done= stmt->send_types_to_server= 1;

  CLEAR_CLIENT_STMT_ERROR(stmt);
  return(0);
}

my_bool STDCALL mysql_stmt_bind_result(MYSQL_STMT *stmt, MYSQL_BIND *bind)
{
  uint i;

  if (stmt->state < MYSQL_STMT_PREPARED)
  {
    SET_CLIENT_STMT_ERROR(stmt, CR_NO_PREPARE_STMT, SQLSTATE_UNKNOWN, 0);
    return(1);
  }

  if (!stmt->field_count)
  {
    SET_CLIENT_STMT_ERROR(stmt, CR_NO_STMT_METADATA, SQLSTATE_UNKNOWN, 0);
    return(1);
  }

  if (!bind)
    return(1);

  /* In case of a stored procedure we don't allocate memory for bind
     in mysql_stmt_prepare
     */

  if (stmt->field_count && !stmt->bind)
  {
    MA_MEM_ROOT *binds_ma_alloc_root =
                &((MADB_STMT_EXTENSION *)stmt->extension)->binds_ma_alloc_root;
    if (!(stmt->bind= (MYSQL_BIND *)ma_alloc_root(binds_ma_alloc_root, stmt->field_count * sizeof(MYSQL_BIND))))
    {
      SET_CLIENT_STMT_ERROR(stmt, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
      return(1);
    }
  }

  memcpy(stmt->bind, bind, sizeof(MYSQL_BIND) * stmt->field_count);

  for (i=0; i < stmt->field_count; i++)
  {
    if (stmt->mysql->methods->db_supported_buffer_type &&
        !stmt->mysql->methods->db_supported_buffer_type(bind[i].buffer_type))
    {
      SET_CLIENT_STMT_ERROR(stmt, CR_UNSUPPORTED_PARAM_TYPE, SQLSTATE_UNKNOWN, 0);
      return(1);
    }

    if (!stmt->bind[i].is_null)
      stmt->bind[i].is_null= &stmt->bind[i].is_null_value;
    if (!stmt->bind[i].length)
      stmt->bind[i].length= &stmt->bind[i].length_value;
    if (!stmt->bind[i].error)
      stmt->bind[i].error= &stmt->bind[i].error_value;

    stmt->bind[i].skip_result= skip_result_fixed;
    /* set length values for numeric types */
    switch(bind[i].buffer_type) {
    case MYSQL_TYPE_NULL:
      *stmt->bind[i].length= stmt->bind[i].length_value= 0;
      stmt->bind[i].pack_length = 0;
      break;
    case MYSQL_TYPE_TINY:
      *stmt->bind[i].length= stmt->bind[i].length_value= 1;
      stmt->bind[i].pack_length = 1;
      break;
    case MYSQL_TYPE_SHORT:
    case MYSQL_TYPE_YEAR:
      *stmt->bind[i].length= stmt->bind[i].length_value= 2;
      stmt->bind[i].pack_length = 2;
      break;
    case MYSQL_TYPE_INT24:
    case MYSQL_TYPE_LONG:
    case MYSQL_TYPE_FLOAT:
      *stmt->bind[i].length= stmt->bind[i].length_value= 4;
      stmt->bind[i].pack_length = 4;
      break;
    case MYSQL_TYPE_LONGLONG:
    case MYSQL_TYPE_DOUBLE:
      *stmt->bind[i].length= stmt->bind[i].length_value= 8;
      stmt->bind[i].pack_length = 8;
      break;
    case MYSQL_TYPE_TIME:
    case MYSQL_TYPE_DATE:
    case MYSQL_TYPE_DATETIME:
    case MYSQL_TYPE_TIMESTAMP:
      *stmt->bind[i].length= stmt->bind[i].length_value= sizeof(MYSQL_TIME);
      stmt->bind[i].skip_result= skip_result_with_length;
      break;
    case MYSQL_TYPE_DECIMAL:
    case MYSQL_TYPE_NEWDECIMAL:
    case MYSQL_TYPE_OB_NUMBER_FLOAT:
    case MYSQL_TYPE_ENUM:
    case MYSQL_TYPE_SET:
    case MYSQL_TYPE_GEOMETRY:
    case MYSQL_TYPE_TINY_BLOB:
    case MYSQL_TYPE_MEDIUM_BLOB:
    case MYSQL_TYPE_LONG_BLOB:
    case MYSQL_TYPE_BLOB:
    case MYSQL_TYPE_ORA_BLOB:
    case MYSQL_TYPE_ORA_CLOB:
    case MYSQL_TYPE_OB_RAW:
    case MYSQL_TYPE_VAR_STRING:
    case MYSQL_TYPE_STRING:
    case MYSQL_TYPE_OB_NVARCHAR2:
    case MYSQL_TYPE_OB_NCHAR:
    case MYSQL_TYPE_BIT:
    case MYSQL_TYPE_NEWDATE:
    case MYSQL_TYPE_JSON:
    case MYSQL_TYPE_OBJECT:
    case MYSQL_TYPE_OB_TIMESTAMP_NANO:
    case MYSQL_TYPE_OB_TIMESTAMP_WITH_LOCAL_TIME_ZONE:
    case MYSQL_TYPE_OB_TIMESTAMP_WITH_TIME_ZONE:
    case MYSQL_TYPE_OB_UROWID:
      stmt->bind[i].skip_result= skip_result_string;
      break;
    default:
      break;
    }
    /* not need to change buffer_type to MAX_NO_FIELD_TYPES, it will be set in 
     * mysql_stmt_fetch_oracle_cursor to skip if stmt->bind[i].no_need_to_parser_result set
     */
    //if (stmt->bind[i].no_need_to_parser_result) {
    //  /* skip field directly not need to parser the result for this field */
    //  stmt->bind[i].buffer_type= MAX_NO_FIELD_TYPES;
    //}
  }
  stmt->bind_result_done= 1;
  CLEAR_CLIENT_STMT_ERROR(stmt);

  return(0);
}

static my_bool net_stmt_close(MYSQL_STMT *stmt, my_bool remove)
{
  char stmt_id[STMT_ID_LENGTH];
  MA_MEM_ROOT *fields_ma_alloc_root= &((MADB_STMT_EXTENSION *)stmt->extension)->fields_ma_alloc_root;
  MA_MEM_ROOT *binds_ma_alloc_root= &((MADB_STMT_EXTENSION *)stmt->extension)->binds_ma_alloc_root;

  /* clear memory */
  ma_free_root(&stmt->result.alloc, MYF(0)); /* allocated in mysql_stmt_store_result */
  ma_free_root(&stmt->mem_root,MYF(0));
  ma_free_root(&stmt->param_fields_mem_root,MYF(0)); /* allocted in mysql_stmt_init */
  ma_free_root(fields_ma_alloc_root, MYF(0));
  ma_free_root(binds_ma_alloc_root, MYF(0));

  if (stmt->mysql)
  {
    CLEAR_CLIENT_ERROR(stmt->mysql);

    /* remove from stmt list */
    if (remove)
      stmt->mysql->stmts= list_delete(stmt->mysql->stmts, &stmt->list);

    /* check if all data are fetched */
    if (stmt->mysql->status != MYSQL_STATUS_READY)
    {
      do {
        stmt->mysql->methods->db_stmt_flush_unbuffered(stmt);
      } while(mysql_stmt_more_results(stmt));
      stmt->mysql->status= MYSQL_STATUS_READY;
    }
    if (stmt->state > MYSQL_STMT_INITTED)
    {
      int4store(stmt_id, stmt->stmt_id);
      if (stmt->mysql->methods->db_command(stmt->mysql,COM_STMT_CLOSE, stmt_id,
                                           sizeof(stmt_id), 1, stmt))
      {
        UPDATE_STMT_ERROR(stmt);
        return 1;
      }
    }
  }
  return 0;
}

my_bool STDCALL mysql_stmt_close(MYSQL_STMT *stmt)
{
  my_bool rc= 1;

  if (stmt)
  {
    if (stmt->mysql && stmt->mysql->net.pvio)
      mysql_stmt_internal_reset(stmt, 1);

    rc= net_stmt_close(stmt, 1);

    free(stmt->extension);
    free(stmt);
  }
  return(rc);
}

void STDCALL mysql_stmt_data_seek(MYSQL_STMT *stmt, unsigned long long offset)
{
  unsigned long long i= offset;
  MYSQL_ROWS *ptr= stmt->result.data;

  while(i-- && ptr)
    ptr= ptr->next;

  stmt->result_cursor= ptr;
  stmt->state= MYSQL_STMT_USER_FETCHING;

  return;
}

unsigned int STDCALL mysql_stmt_errno(MYSQL_STMT *stmt)
{
  return stmt->last_errno;
}

const char * STDCALL mysql_stmt_error(MYSQL_STMT *stmt)
{
  return (const char *)stmt->last_error;
}

int mthd_stmt_fetch_row(MYSQL_STMT *stmt, unsigned char **row)
{
  if (stmt->mysql->oracle_mode && stmt->fetch_row_func == NULL)
  {
    stmt->fetch_row_func= stmt_cursor_fetch;
  }
  return stmt->fetch_row_func(stmt, row);
}

int STDCALL mysql_stmt_fetch(MYSQL_STMT *stmt)
{
  unsigned char *row;
  int rc;

  if (stmt->state <= MYSQL_STMT_EXECUTED)
  {
    SET_CLIENT_STMT_ERROR(stmt, CR_COMMANDS_OUT_OF_SYNC, SQLSTATE_UNKNOWN, 0);
    return(1);
  }

  if (stmt->state < MYSQL_STMT_WAITING_USE_OR_STORE || !stmt->field_count)
  {
    SET_CLIENT_STMT_ERROR(stmt, CR_COMMANDS_OUT_OF_SYNC, SQLSTATE_UNKNOWN, 0);
    return(1);
  } else if (stmt->state== MYSQL_STMT_WAITING_USE_OR_STORE)
  {
    stmt->default_rset_handler(stmt);
  }

  if (stmt->state == MYSQL_STMT_FETCH_DONE)
    return(MYSQL_NO_DATA);

  if ((rc= stmt->mysql->methods->db_stmt_fetch(stmt, &row)))
  {
    stmt->state= MYSQL_STMT_FETCH_DONE;
    stmt->mysql->status= MYSQL_STATUS_READY;
    /* to fetch data again, stmt must be executed again */
    return(rc);
  }

  rc= stmt->mysql->methods->db_stmt_fetch_to_bind(stmt, row);

  stmt->state= MYSQL_STMT_USER_FETCHING;
  /* Different with 1.x 
   * Important mariadb will clean all error for stmt->mysql if get correct result. It is different for us
   * to get error message when ResultSet + Error Packet is get
   */
  CLEAR_CLIENT_ERROR(stmt->mysql);
  CLEAR_CLIENT_STMT_ERROR(stmt);
  return(rc);
}

static void fetch_result_with_piece(MYSQL_BIND *bind, MYSQL_FIELD *field, uchar **row)
{
  ulong length = *bind->length;
  ulong copy_length = MIN(length, bind->buffer_length);
  UNUSED(field);
  memcpy(bind->buffer, (char *)*row, copy_length);
  *bind->error = copy_length < length;
}

int STDCALL mysql_stmt_fetch_column(MYSQL_STMT *stmt, MYSQL_BIND *bind, unsigned int column, unsigned long offset)
{
  if (stmt->state < MYSQL_STMT_USER_FETCHING || column >= stmt->field_count ||
      stmt->state == MYSQL_STMT_FETCH_DONE)  {
    SET_CLIENT_STMT_ERROR(stmt, CR_NO_DATA, SQLSTATE_UNKNOWN, 0);
    return(1);
  }

  if (!stmt->bind[column].u.row_ptr)
  {
    /* we set row_ptr only for columns which contain data, so this must be a NULL column */
    if (bind[0].is_null)
      *bind[0].is_null= 1;
  }
  else
  {
    unsigned char *save_ptr;
    if (bind[0].length)
      *bind[0].length= *stmt->bind[column].length;
    else
      bind[0].length= &stmt->bind[column].length_value;
    if (bind[0].is_null)
      *bind[0].is_null= 0;
    else
      bind[0].is_null= &bind[0].is_null_value;
    if (!bind[0].error)
      bind[0].error= &bind[0].error_value;
    *bind[0].error= 0;
    bind[0].offset= offset;
    bind[0].mysql = stmt->mysql;
    save_ptr= stmt->bind[column].u.row_ptr;
    if (bind->piece_data_used)
      fetch_result_with_piece(&bind[0], &stmt->fields[column], &stmt->bind[column].u.row_ptr);
    else
      mysql_ps_fetch_functions[stmt->fields[column].type].func(&bind[0], &stmt->fields[column], &stmt->bind[column].u.row_ptr);
    stmt->bind[column].u.row_ptr= save_ptr;
  }
  return(0);
}

unsigned int STDCALL mysql_stmt_field_count(MYSQL_STMT *stmt)
{
  return stmt->field_count;
}

my_bool STDCALL mysql_stmt_free_result(MYSQL_STMT *stmt)
{
  return madb_reset_stmt(stmt, MADB_RESET_LONGDATA | MADB_RESET_STORED |
                               MADB_RESET_BUFFER | MADB_RESET_ERROR);
}

MYSQL_STMT * STDCALL mysql_stmt_init(MYSQL *mysql)
{

  MYSQL_STMT *stmt= NULL;

  if (!(stmt= (MYSQL_STMT *)calloc(1, sizeof(MYSQL_STMT))) ||
      !(stmt->extension= (MADB_STMT_EXTENSION *)calloc(1, sizeof(MADB_STMT_EXTENSION))))
  {
    free(stmt);
    SET_CLIENT_ERROR(mysql, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
    return(NULL);
  }


  /* fill mysql's stmt list */
  stmt->list.data= stmt;
  stmt->mysql= mysql;
  stmt->stmt_id= 0;
  mysql->stmts= list_add(mysql->stmts, &stmt->list);


  /* clear flags */
  strcpy(stmt->sqlstate, "00000");

  stmt->state= MYSQL_STMT_INITTED;

  /* set default */
  stmt->prefetch_rows= 1;

  //need to check to free
  ma_init_alloc_root(&stmt->mem_root, 2048, 2048);
  ma_init_alloc_root(&stmt->result.alloc, 4096, 4096);
  ma_init_alloc_root(&stmt->param_fields_mem_root, 2048, 2048);
  ma_init_alloc_root(&((MADB_STMT_EXTENSION *)stmt->extension)->fields_ma_alloc_root, 2048, 2048);
  ma_init_alloc_root(&((MADB_STMT_EXTENSION *)stmt->extension)->binds_ma_alloc_root, 2048, 2048);

  return(stmt);
}

my_bool mthd_stmt_read_prepare_response(MYSQL_STMT *stmt)
{
  ulong packet_length;
  uchar *p;

  if ((packet_length= ma_net_safe_read(stmt->mysql)) == packet_error)
    return(1);

  p= (uchar *)stmt->mysql->net.read_pos;

  if (0xFF == p[0])  /* Error occurred */
  {
    return(1);
  }

  p++;
  stmt->stmt_id= uint4korr(p);
  p+= 4;
  stmt->field_count= uint2korr(p);
  p+= 2;
  stmt->param_count= uint2korr(p);
  p+= 2;

  /* filler */
  p++;
  /* for backward compatibility we also update mysql->warning_count */
  stmt->mysql->warning_count= stmt->upsert_status.warning_count= uint2korr(p);
  return(0);
}

my_bool mthd_stmt_get_param_metadata(MYSQL_STMT *stmt)
{
  MYSQL_DATA *result;
  MA_MEM_ROOT *param_fields_mem_root= &stmt->param_fields_mem_root;
  if (!(result= stmt->mysql->methods->db_read_rows(stmt->mysql, (MYSQL_FIELD *)0,
                                                   7 + ma_extended_type_info_rows(stmt->mysql))))
    return(1);
  if (!(stmt->param_fields= unpack_fields(stmt->mysql, result, param_fields_mem_root,
          stmt->param_count, 0)))
    return(1);
  return(0);
}

my_bool mthd_stmt_get_result_metadata(MYSQL_STMT *stmt)
{
  MYSQL_DATA *result;
  MA_MEM_ROOT *fields_ma_alloc_root= &((MADB_STMT_EXTENSION *)stmt->extension)->fields_ma_alloc_root;

  if (!(result= stmt->mysql->methods->db_read_rows(stmt->mysql, (MYSQL_FIELD *)0,
                                                   7 + ma_extended_type_info_rows(stmt->mysql))))
    return(1);
  if (!(stmt->fields= unpack_fields(stmt->mysql, result, fields_ma_alloc_root,
          stmt->field_count, 0)))
    return(1);
  return(0);
}

int STDCALL mysql_stmt_warning_count(MYSQL_STMT *stmt)
{
  return stmt->upsert_status.warning_count;
}

int STDCALL mysql_stmt_prepare(MYSQL_STMT *stmt, const char *query, unsigned long length)
{
  MYSQL *mysql= stmt->mysql;
  int rc= 1;
  int ret = 0;
  my_bool is_multi= 0;
  FLT_DECLARE;
  
  if (!stmt->mysql)
  {
    SET_CLIENT_STMT_ERROR(stmt, CR_SERVER_LOST, SQLSTATE_UNKNOWN, 0);
    return(1);
  }

  if (length == (unsigned long) -1)
    length= (unsigned long)strlen(query);

  /* clear flags */
  CLEAR_CLIENT_STMT_ERROR(stmt);
  CLEAR_CLIENT_ERROR(stmt->mysql);
  stmt->upsert_status.affected_rows= mysql->affected_rows= (unsigned long long) ~0;

  /* check if we have to clear results */
  if (stmt->state > MYSQL_STMT_INITTED)
  {
    char stmt_id[STMT_ID_LENGTH];
    is_multi= (mysql->net.extension->multi_status > COM_MULTI_OFF);
    /* We need to semi-close the prepared statement:
       reset stmt and free all buffers and close the statement
       on server side. Statement handle will get a new stmt_id */

    if (!is_multi && FALSE == get_use_protocol_ob20(mysql))
      ma_multi_command(mysql, COM_MULTI_ENABLED);

    if (mysql_stmt_internal_reset(stmt, 1))
      goto fail;

    ma_free_root(&stmt->mem_root, MYF(MY_KEEP_PREALLOC));
    ma_free_root(&stmt->param_fields_mem_root, MYF(MY_KEEP_PREALLOC));
    ma_free_root(&((MADB_STMT_EXTENSION *)stmt->extension)->fields_ma_alloc_root, MYF(0));
    ma_free_root(&((MADB_STMT_EXTENSION *)stmt->extension)->binds_ma_alloc_root, MYF(0));

    stmt->param_count= 0;
    stmt->field_count= 0;
    stmt->params= 0;

    int4store(stmt_id, stmt->stmt_id);
    if (mysql->methods->db_command(mysql, COM_STMT_CLOSE, stmt_id,
                                         sizeof(stmt_id), 1, stmt))
      goto fail;
  }

  FLT_BEFORE_COMMAND(0, FLT_TAG_COMMAND_NAME, "\"mysql_stmt_prepare\"");

  ret = mysql->methods->db_command(mysql, COM_STMT_PREPARE, query, length, 1, stmt);
  // end trace
  FLT_AFTER_COMMAND;

  if (ret) {
    goto fail;
  }

  if (!is_multi && mysql->net.extension->multi_status == COM_MULTI_ENABLED && FALSE == get_use_protocol_ob20(mysql))
    ma_multi_command(mysql, COM_MULTI_END);
  
  if (mysql->net.extension->multi_status > COM_MULTI_OFF)
    return 0;

  if (mysql->methods->db_read_prepare_response &&
      mysql->methods->db_read_prepare_response(stmt))
    goto fail;

  /* metadata not supported yet */

  if (stmt->param_count &&
      stmt->mysql->methods->db_stmt_get_param_metadata(stmt))
  {
    goto fail;
  }

  /* allocated bind buffer for parameters */
  if (stmt->field_count &&
      stmt->mysql->methods->db_stmt_get_result_metadata(stmt))
  {
    goto fail;
  }
  if (stmt->param_count)
  {
    if (stmt->prebind_params)
    {
      if (stmt->prebind_params != stmt->param_count)
      {
        SET_CLIENT_STMT_ERROR(stmt, CR_INVALID_PARAMETER_NO, SQLSTATE_UNKNOWN, 0);
        goto fail;
      }
    } else {
      if (!(stmt->params= (MYSQL_BIND *)ma_alloc_root(&stmt->mem_root, stmt->param_count * sizeof(MYSQL_BIND))))
      {
        SET_CLIENT_STMT_ERROR(stmt, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
        goto fail;
      }
      memset(stmt->params, '\0', stmt->param_count * sizeof(MYSQL_BIND));
    }
  }
  /* allocated bind buffer for result */
  if (stmt->field_count)
  {
    MA_MEM_ROOT *binds_ma_alloc_root= &((MADB_STMT_EXTENSION *)stmt->extension)->binds_ma_alloc_root;
    if (!(stmt->bind= (MYSQL_BIND *)ma_alloc_root(binds_ma_alloc_root, stmt->field_count * sizeof(MYSQL_BIND))))
    {
      SET_CLIENT_STMT_ERROR(stmt, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
      goto fail;
    }
    memset(stmt->bind, 0, sizeof(MYSQL_BIND) * stmt->field_count);
  }
  stmt->state = MYSQL_STMT_PREPARED;

   return(0);

fail:
  stmt->state= MYSQL_STMT_INITTED;
  UPDATE_STMT_ERROR(stmt);
  return(rc);
}

int STDCALL mysql_stmt_store_result(MYSQL_STMT *stmt)
{
  unsigned int last_server_status;

  if (!stmt->mysql)
  {
    SET_CLIENT_STMT_ERROR(stmt, CR_SERVER_LOST, SQLSTATE_UNKNOWN, 0);
    return(1);
  }

  if (!stmt->field_count)
    return(0);

  /* test_pure_coverage requires checking of error_no */
  if (stmt->last_errno)
    return(1);

  if (stmt->state < MYSQL_STMT_EXECUTED)
  {
    SET_CLIENT_ERROR(stmt->mysql, CR_COMMANDS_OUT_OF_SYNC, SQLSTATE_UNKNOWN, 0);
    SET_CLIENT_STMT_ERROR(stmt, CR_COMMANDS_OUT_OF_SYNC, SQLSTATE_UNKNOWN, 0);
    return(1);
  }

  last_server_status= stmt->mysql->server_status;

  /* if stmt is a cursor, we need to tell server to send all rows */
  if (stmt->cursor_exists && stmt->mysql->status == MYSQL_STATUS_READY)
  {
    char buff[STMT_ID_LENGTH + 4];
    int4store(buff, stmt->stmt_id);
    int4store(buff + STMT_ID_LENGTH, (int)~0);

    if (stmt->mysql->methods->db_command(stmt->mysql, COM_STMT_FETCH,
                                         buff, sizeof(buff), 1, stmt))
    {
      UPDATE_STMT_ERROR(stmt);
      return(1);
    }
  }
  else if (stmt->mysql->status != MYSQL_STATUS_STMT_RESULT)
  {
    SET_CLIENT_ERROR(stmt->mysql, CR_COMMANDS_OUT_OF_SYNC, SQLSTATE_UNKNOWN, 0);
    SET_CLIENT_STMT_ERROR(stmt, CR_COMMANDS_OUT_OF_SYNC, SQLSTATE_UNKNOWN, 0);
    return(1);
  }

  if (stmt->mysql->methods->db_stmt_read_all_rows(stmt))
  {
    /* error during read - reset stmt->data */
    ma_free_root(&stmt->result.alloc, 0);
    stmt->result.data= NULL;
    stmt->result.rows= 0;
    stmt->mysql->status= MYSQL_STATUS_READY;
    return(1);
  }

  /* workaround for MDEV 6304:
     more results not set if the resultset has
     SERVER_PS_OUT_PARAMS set
   */
  if (last_server_status & SERVER_PS_OUT_PARAMS &&
      !(stmt->mysql->oracle_mode) && /* It not have any resultsets returned in Oracle mode */
      !(stmt->mysql->server_status & SERVER_MORE_RESULTS_EXIST))
    stmt->mysql->server_status|= SERVER_MORE_RESULTS_EXIST;

  stmt->result_cursor= stmt->result.data;
  stmt->fetch_row_func= stmt_buffered_fetch;
  stmt->mysql->status= MYSQL_STATUS_READY;

  if (!stmt->result.rows)
    stmt->state= MYSQL_STMT_FETCH_DONE;
  else
    stmt->state= MYSQL_STMT_USE_OR_STORE_CALLED;

  /* set affected rows: see bug 2247 */
  stmt->upsert_status.affected_rows= stmt->result.rows;
  stmt->mysql->affected_rows= stmt->result.rows;

  return(0);
}

static int madb_alloc_stmt_fields(MYSQL_STMT *stmt)
{
  uint i;
  MA_MEM_ROOT *fields_ma_alloc_root= &((MADB_STMT_EXTENSION *)stmt->extension)->fields_ma_alloc_root;
  MA_MEM_ROOT *binds_ma_alloc_root = &((MADB_STMT_EXTENSION *)stmt->extension)->binds_ma_alloc_root;

  if (stmt->mysql->field_count)
  {
    ma_free_root(fields_ma_alloc_root, MYF(0));
    ma_free_root(binds_ma_alloc_root, MYF(0));
    if (!(stmt->fields= (MYSQL_FIELD *)ma_alloc_root(fields_ma_alloc_root,
            sizeof(MYSQL_FIELD) * stmt->mysql->field_count)))
    {
      SET_CLIENT_STMT_ERROR(stmt, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
      return(1);
    }
    memset(stmt->fields, 0, sizeof(MYSQL_FIELD) * stmt->mysql->field_count);
    stmt->field_count= stmt->mysql->field_count;

    for (i=0; i < stmt->field_count; i++)
    {
      memcpy(&stmt->fields[i], &stmt->mysql->fields[i], sizeof(MYSQL_FIELD));
      if (stmt->mysql->fields[i].db)
        stmt->fields[i].db= ma_strdup_root(fields_ma_alloc_root, stmt->mysql->fields[i].db);
      if (stmt->mysql->fields[i].table)
        stmt->fields[i].table= ma_strdup_root(fields_ma_alloc_root, stmt->mysql->fields[i].table);
      if (stmt->mysql->fields[i].org_table)
        stmt->fields[i].org_table= ma_strdup_root(fields_ma_alloc_root, stmt->mysql->fields[i].org_table);
      if (stmt->mysql->fields[i].name)
        stmt->fields[i].name= ma_strdup_root(fields_ma_alloc_root, stmt->mysql->fields[i].name);
      if (stmt->mysql->fields[i].org_name)
        stmt->fields[i].org_name= ma_strdup_root(fields_ma_alloc_root, stmt->mysql->fields[i].org_name);
      if (stmt->mysql->fields[i].catalog)
        stmt->fields[i].catalog= ma_strdup_root(fields_ma_alloc_root, stmt->mysql->fields[i].catalog);
      stmt->fields[i].def= stmt->mysql->fields[i].def ? ma_strdup_root(fields_ma_alloc_root, stmt->mysql->fields[i].def) : NULL;
      stmt->fields[i].extension=
                stmt->mysql->fields[i].extension ?
                ma_field_extension_deep_dup(fields_ma_alloc_root,
                                            stmt->mysql->fields[i].extension) :
                NULL;
      if (MYSQL_TYPE_OBJECT == stmt->fields[i].type) {
        if (stmt->mysql->fields[i].owner_name)
          stmt->fields[i].owner_name = (unsigned char *)ma_strdup_root(fields_ma_alloc_root, (char *)(stmt->mysql->fields[i].owner_name)); 
        if (stmt->mysql->fields[i].type_name)
          stmt->fields[i].owner_name = (unsigned char *)ma_strdup_root(fields_ma_alloc_root, (char *)(stmt->mysql->fields[i].type_name)); 
      }
    }
    if (!(stmt->bind= (MYSQL_BIND *)ma_alloc_root(binds_ma_alloc_root, stmt->field_count * sizeof(MYSQL_BIND))))
    {
      SET_CLIENT_STMT_ERROR(stmt, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
      return(1);
    }
    memset(stmt->bind, 0, stmt->field_count * sizeof(MYSQL_BIND));
    stmt->bind_result_done= 0;
  }
  return(0);
}

static int mthd_my_read_prepare_execute_result(MYSQL *mysql)
{
  // uchar *pos;
  ulong field_count;
  MYSQL_DATA *fields;
  field_count = mysql->field_count;
  free_old_query(mysql);      /* Free old result */
// get_info:
  // pos=(uchar*) mysql->net.read_pos;
  if (!(mysql->server_status & SERVER_STATUS_AUTOCOMMIT))
    mysql->server_status|= SERVER_STATUS_IN_TRANS;

  if (!(fields=mysql->methods->db_read_rows(mysql,(MYSQL_FIELD*) 0,
                                            7 + ma_extended_type_info_rows(mysql))))
    return(-1);
  if (!(mysql->fields=unpack_fields(mysql, fields, &mysql->field_alloc,
            (uint) field_count, 1)))
    return(-1);
  mysql->status=MYSQL_STATUS_GET_RESULT;
  mysql->field_count=field_count;
  return(0);
}
static int stmt_read_prepare_execute_response(MYSQL_STMT* stmt);
int stmt_read_execute_response(MYSQL_STMT *stmt)
{
  MYSQL *mysql= stmt->mysql;
  int ret;
  my_bool is_exact_fetch_error = FALSE;

  if (!mysql)
    return(1);
  if (stmt->use_prepare_execute) {
    ret = stmt_read_prepare_execute_response(stmt);
  } else {
    ret= test((mysql->methods->db_read_stmt_result &&
                 mysql->methods->db_read_stmt_result(mysql)));
  }
  /* if a reconnect occurred, our connection handle is invalid */
  if (!stmt->mysql)
    return(1);

  /* update affected rows, also if an error occurred */
  stmt->upsert_status.affected_rows= stmt->mysql->affected_rows;
  //as for use_prepare_execute exact fetch may error after result
  // we should handle it specially
  if (ret)
  {
    SET_CLIENT_STMT_ERROR(stmt, mysql->net.last_errno, mysql->net.sqlstate,
       mysql->net.last_error);
    if (stmt->use_prepare_execute && (mysql->net.last_errno == 1403|| mysql->net.last_errno == 1422))
      is_exact_fetch_error = TRUE;
    if (stmt->is_handle_returning_into) /* not handing result with returning ... into (maybe has result)*/
      is_exact_fetch_error = TRUE;
    else if (stmt->has_added_user_fields) /* handling result with added_user_fields, no matter it is ERR_PKT */
      is_exact_fetch_error = TRUE;
    else if (!is_exact_fetch_error)
      stmt->state= MYSQL_STMT_PREPARED;
    else
      stmt->state= MYSQL_STMT_EXECUTED;
    if (stmt->use_prepare_execute) {
      //when use_preaprae_execute status may be changed to MYSQL_STATUS_GET_RESULT in read_query_result
      mysql->status = MYSQL_STATUS_READY;
    }
    if (!is_exact_fetch_error)
      return(1);
  }
  stmt->upsert_status.last_insert_id= mysql->insert_id;
  stmt->upsert_status.server_status= mysql->server_status;
  stmt->upsert_status.warning_count= mysql->warning_count;

  if (ret == 0)
  {
    CLEAR_CLIENT_ERROR(mysql);
    CLEAR_CLIENT_STMT_ERROR(stmt);

    stmt->execute_count++;
    stmt->send_types_to_server= 0;

    stmt->state= MYSQL_STMT_EXECUTED;
  }

  if (mysql->field_count)
  {
    /*force update stmt->fields by execute reponse, for it not incorrect fields info returned by STMT_PREPARE */
    //TODO check to update field info for stmt for it has been set by OUT parma field info in mysql_stmt_prepare
    //if (!stmt->field_count ||
    //    mysql->server_status & SERVER_MORE_RESULTS_EXIST) /* fix for ps_bug: test_misc */
    //{
      if (madb_reinit_result_set_metadata(stmt))
        return 1;
//      MA_MEM_ROOT *fields_ma_alloc_root=
//                  &((MADB_STMT_EXTENSION *)stmt->extension)->fields_ma_alloc_root;
//      MA_MEM_ROOT *binds_ma_alloc_root=
//                  &((MADB_STMT_EXTENSION *)stmt->extension)->binds_ma_alloc_root;
//      uint i;
//
//      ma_free_root(fields_ma_alloc_root, MYF(0));
//      ma_free_root(binds_ma_alloc_root, MYF(0));
//      if (!(stmt->bind= (MYSQL_BIND *)ma_alloc_root(binds_ma_alloc_root,
//              sizeof(MYSQL_BIND) * mysql->field_count)) ||
//          !(stmt->fields= (MYSQL_FIELD *)ma_alloc_root(fields_ma_alloc_root,
//              sizeof(MYSQL_FIELD) * mysql->field_count)))
//      {
//        SET_CLIENT_STMT_ERROR(stmt, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
//        return(1);
//      }
//      memset(stmt->bind, 0, sizeof(MYSQL_BIND) * mysql->field_count);
//      stmt->field_count= mysql->field_count;
//
//      for (i=0; i < stmt->field_count; i++)
//      {
//        memcpy(&stmt->fields[i], &mysql->fields[i], sizeof(MYSQL_FIELD));
//
//        /* since  all pointers will be incorrect if another statement will
//           be executed, so we need to allocate memory and copy the
//           information */
//        if (mysql->fields[i].db)
//          stmt->fields[i].db= ma_strdup_root(fields_ma_alloc_root, mysql->fields[i].db);
//        if (mysql->fields[i].table)
//          stmt->fields[i].table= ma_strdup_root(fields_ma_alloc_root, mysql->fields[i].table);
//        if (mysql->fields[i].org_table)
//          stmt->fields[i].org_table= ma_strdup_root(fields_ma_alloc_root, mysql->fields[i].org_table);
//        if (mysql->fields[i].name)
//          stmt->fields[i].name= ma_strdup_root(fields_ma_alloc_root, mysql->fields[i].name);
//        if (mysql->fields[i].org_name)
//          stmt->fields[i].org_name= ma_strdup_root(fields_ma_alloc_root, mysql->fields[i].org_name);
//        if (mysql->fields[i].catalog)
//          stmt->fields[i].catalog= ma_strdup_root(fields_ma_alloc_root, mysql->fields[i].catalog);
//        if (mysql->fields[i].def)
//          stmt->fields[i].def= ma_strdup_root(fields_ma_alloc_root, mysql->fields[i].def);
//        stmt->fields[i].extension=
//                mysql->fields[i].extension ?
//                ma_field_extension_deep_dup(fields_ma_alloc_root,
//                                            mysql->fields[i].extension) :
//                NULL;
//      }
    //}

    if ((stmt->upsert_status.server_status & SERVER_STATUS_CURSOR_EXISTS)  &&
        ((stmt->flags & CURSOR_TYPE_READ_ONLY) || stmt->use_prepare_execute)) 
    {
      stmt->cursor_exists = TRUE;
      mysql->status = MYSQL_STATUS_READY;

      /* Only cursor read */
      stmt->default_rset_handler = _mysql_stmt_use_result;

    } else if (stmt->flags & CURSOR_TYPE_READ_ONLY)
    {
      /*
         We have asked for CURSOR but got no cursor, because the condition
         above is not fulfilled. Then...
         This is a single-row result set, a result set with no rows, EXPLAIN,
         SHOW VARIABLES, or some other command which either a) bypasses the
         cursors framework in the server and writes rows directly to the
         network or b) is more efficient if all (few) result set rows are
         precached on client and server's resources are freed.
         */

      /* preferred is buffered read */
      if (mysql_stmt_store_result(stmt))
        return 1;
      stmt->mysql->status= MYSQL_STATUS_STMT_RESULT;
    } else
    {
      /* preferred is unbuffered read */
      stmt->default_rset_handler = _mysql_stmt_use_result;
      if (stmt->use_prepare_execute) {
        //here set this state to skip default_rset_handler
        stmt->state = MYSQL_STMT_USE_OR_STORE_CALLED;
        if (!stmt->cursor_exists) {
          stmt->fetch_row_func = stmt_buffered_fetch;
        } else
          stmt->fetch_row_func= stmt_cursor_fetch;
        stmt->mysql->status = MYSQL_STATUS_READY;
      } else {
        stmt->mysql->status= MYSQL_STATUS_STMT_RESULT;
      }
    }
    stmt->state= MYSQL_STMT_WAITING_USE_OR_STORE;
    /* in certain cases parameter types can change: For example see bug
       4026 (SELECT ?), so we need to update field information */
//    if (mysql->field_count == stmt->field_count)
//    {
//      uint i;
//      if (!stmt->use_prepare_execute) {
//         for (i=0; i < stmt->field_count; i++)
//        {
//          stmt->fields[i].type= mysql->fields[i].type;
//          stmt->fields[i].length= mysql->fields[i].length;
//          stmt->fields[i].flags= mysql->fields[i].flags;
//          stmt->fields[i].decimals= mysql->fields[i].decimals;
//          stmt->fields[i].charsetnr= mysql->fields[i].charsetnr;
//          stmt->fields[i].max_length= mysql->fields[i].max_length;
//        }
//      }
//     
//    } else
//    {
//      /* table was altered, see test_wl4166_2  */
//      SET_CLIENT_STMT_ERROR(stmt, CR_NEW_STMT_METADATA, SQLSTATE_UNKNOWN, 0);
//      return(1);
//    }
  }
  return(ret);
}

my_bool cli_read_piece_result(MYSQL_STMT *stmt)
{
  MYSQL *mysql= stmt->mysql;
  ulong len=0;


  if ((len = ma_net_safe_read(mysql)) == packet_error || len == 0)
  {
    end_server(mysql);
    return(1);
  }

  if (mysql->net.read_pos[0] == 0)
  {
    ma_read_ok_packet(mysql, &mysql->net.read_pos[1], len);//ok packet
  } else
  {
    end_server(mysql);
    SET_CLIENT_STMT_ERROR(stmt, CR_MALFORMED_PACKET, SQLSTATE_UNKNOWN, "Wrong packet: unexpect pkt");
    return(1);
  }

  return(0);
}

int STDCALL mysql_stmt_execute(MYSQL_STMT *stmt)
{
  MYSQL *mysql= stmt->mysql;
  char *request;
  int ret;
  size_t request_len= 0;
  FLT_DECLARE;

  if (!stmt->mysql)
  {
    SET_CLIENT_STMT_ERROR(stmt, CR_SERVER_LOST, SQLSTATE_UNKNOWN, 0);
    return(1);
  }

  if (stmt->state < MYSQL_STMT_PREPARED)
  {
    SET_CLIENT_ERROR(mysql, CR_COMMANDS_OUT_OF_SYNC, SQLSTATE_UNKNOWN, 0);
    SET_CLIENT_STMT_ERROR(stmt, CR_COMMANDS_OUT_OF_SYNC, SQLSTATE_UNKNOWN, 0);
    return(1);
  }

  if (stmt->param_count && !stmt->bind_param_done)
  {
    SET_CLIENT_STMT_ERROR(stmt, CR_PARAMS_NOT_BOUND, SQLSTATE_UNKNOWN, 0);
    return(1);
  }

  if (stmt->state == MYSQL_STMT_WAITING_USE_OR_STORE)
  {
    stmt->default_rset_handler = _mysql_stmt_use_result;
    stmt->default_rset_handler(stmt);
  }
  if (stmt->state > MYSQL_STMT_WAITING_USE_OR_STORE && stmt->state < MYSQL_STMT_FETCH_DONE && !stmt->result.data)
  {
    if (!stmt->cursor_exists)
      do {
        stmt->mysql->methods->db_stmt_flush_unbuffered(stmt);
      } while(mysql_stmt_more_results(stmt));
    stmt->state= MYSQL_STMT_PREPARED;
    stmt->mysql->status= MYSQL_STATUS_READY;
  }

  /* clear data, in case mysql_stmt_store_result was called */
  if (stmt->result.data)
  {
    ma_free_root(&stmt->result.alloc, MYF(MY_KEEP_PREALLOC));
    stmt->result_cursor= stmt->result.data= 0;
  }
  /* CONC-344: set row count to zero */
  stmt->result.rows= 0;
  if (stmt->array_size > 0)
    request= (char *)mysql_stmt_execute_generate_bulk_request(stmt, &request_len);
  else
    request= (char *)mysql_stmt_execute_generate_simple_request(stmt, &request_len);

  if (!request)
    return 1;


  FLT_BEFORE_COMMAND(0, FLT_TAG_COMMAND_NAME, "\"mysql_stmt_execute\"");

  if (0 == ret) {
    ret= stmt->mysql->methods->db_command(mysql, 
                                          stmt->array_size > 0 ? COM_STMT_BULK_EXECUTE : COM_STMT_EXECUTE,
                                          request, request_len, 1, stmt);
    if (request)
      free(request);

    if (ret) {
      UPDATE_STMT_ERROR(stmt);
      ret = 1;
    } else if (mysql->net.extension->multi_status > COM_MULTI_OFF) {
      ret = 0;
    } else {
      ret = (stmt_read_execute_response(stmt));
    }
  }

  FLT_AFTER_COMMAND;

  return ret;
}

static my_bool madb_reset_stmt(MYSQL_STMT *stmt, unsigned int flags)
{
  MYSQL *mysql= stmt->mysql;
  my_bool ret= 0;

  if (!stmt->mysql)
  {
    SET_CLIENT_STMT_ERROR(stmt, CR_SERVER_LOST, SQLSTATE_UNKNOWN, 0);
    return(1);
  }

  /* clear error */
  if (flags & MADB_RESET_ERROR)
  {
    CLEAR_CLIENT_ERROR(stmt->mysql);
    CLEAR_CLIENT_STMT_ERROR(stmt);
  }

  if (stmt->stmt_id)
  {
    /* free buffered resultset, previously allocated
     * by mysql_stmt_store_result
     */
    if (flags & MADB_RESET_STORED &&
        stmt->result_cursor)
    {
      ma_free_root(&stmt->result.alloc, MYF(MY_KEEP_PREALLOC));
      stmt->result.data= NULL;
      stmt->result.rows= 0;
      stmt->result_cursor= NULL;
      stmt->mysql->status= MYSQL_STATUS_READY;
      stmt->state= MYSQL_STMT_FETCH_DONE;
    }

    /* if there is a pending result set, we will flush it */
    if (flags & MADB_RESET_BUFFER)
    {
      if (stmt->state == MYSQL_STMT_WAITING_USE_OR_STORE)
      {
        stmt->default_rset_handler(stmt);
        stmt->state = MYSQL_STMT_USER_FETCHING;
      }

      if (stmt->mysql->status!= MYSQL_STATUS_READY && stmt->field_count)
      {
        mysql->methods->db_stmt_flush_unbuffered(stmt);
        mysql->status= MYSQL_STATUS_READY;
      }
    }

    if (flags & MADB_RESET_SERVER)
    {
      /* reset statement on server side */
      if (stmt->mysql && stmt->mysql->status == MYSQL_STATUS_READY &&
          stmt->mysql->net.pvio)
      {
        unsigned char cmd_buf[STMT_ID_LENGTH];
        int4store(cmd_buf, stmt->stmt_id);
        if ((ret= stmt->mysql->methods->db_command(mysql,COM_STMT_RESET, (char *)cmd_buf,
                                                   sizeof(cmd_buf), 0, stmt)))
        {
          UPDATE_STMT_ERROR(stmt);
          return(ret);
        }
      }
    }

    if (flags & MADB_RESET_LONGDATA)
    {
      if (stmt->params)
      {
        ulonglong i;
        for (i=0; i < stmt->param_count; i++)
        {
          if (stmt->params[i].long_data_used)
            stmt->params[i].long_data_used= 0;
          if (stmt->params[i].piece_data_used)
            stmt->params[i].piece_data_used= 0;
        }
      }
    }

  }
  return(ret);
}

static my_bool mysql_stmt_internal_reset(MYSQL_STMT *stmt, my_bool is_close)
{
  MYSQL *mysql= stmt->mysql;
  my_bool ret= 1;
  unsigned int flags= MADB_RESET_LONGDATA | MADB_RESET_BUFFER | MADB_RESET_ERROR;

  if (!mysql)
  {
    /* connection could be invalid, e.g. after mysql_stmt_close or failed reconnect
       attempt (see bug CONC-97) */
    SET_CLIENT_STMT_ERROR(stmt, CR_SERVER_LOST, SQLSTATE_UNKNOWN, 0);
    return(1);
  }

  if (stmt->state >= MYSQL_STMT_USER_FETCHING &&
      stmt->fetch_row_func == stmt_unbuffered_fetch)
    flags|= MADB_RESET_BUFFER;

  ret= madb_reset_stmt(stmt, flags);

  if (stmt->stmt_id)
  {
    if ((stmt->state > MYSQL_STMT_EXECUTED &&
        stmt->mysql->status != MYSQL_STATUS_READY) ||
        stmt->mysql->server_status & SERVER_MORE_RESULTS_EXIST)
    {
      /* flush any pending (multiple) result sets */
      if (stmt->state == MYSQL_STMT_WAITING_USE_OR_STORE)
      {
        stmt->default_rset_handler(stmt);
        stmt->state = MYSQL_STMT_USER_FETCHING;
      }

      if (stmt->field_count)
      {
        while (mysql_stmt_next_result(stmt) == 0);
        stmt->mysql->status= MYSQL_STATUS_READY;
      }
    }
    if (!is_close)
      ret= madb_reset_stmt(stmt, MADB_RESET_SERVER);
    stmt->state= MYSQL_STMT_PREPARED;
  }
  else
    stmt->state= MYSQL_STMT_INITTED;

  stmt->upsert_status.affected_rows= mysql->affected_rows;
  stmt->upsert_status.last_insert_id= mysql->insert_id;
  stmt->upsert_status.server_status= mysql->server_status;
  stmt->upsert_status.warning_count= mysql->warning_count;
  mysql->status= MYSQL_STATUS_READY;

  return(ret);
}

MYSQL_RES * STDCALL mysql_stmt_result_metadata(MYSQL_STMT *stmt)
{
  MYSQL_RES *res;

  if (!stmt->field_count)
    return(NULL);

  /* aloocate result set structutr and copy stmt information */
  if (!(res= (MYSQL_RES *)calloc(1, sizeof(MYSQL_RES))))
  {
    SET_CLIENT_STMT_ERROR(stmt, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
    return(NULL);
  }

  res->eof= 1;
  res->fields= stmt->fields;
  res->field_count= stmt->field_count;
  return(res);
}

my_bool STDCALL mysql_stmt_reset(MYSQL_STMT *stmt)
{
  if (stmt->stmt_id > 0 &&
      stmt->stmt_id != (unsigned long) -1)
    return mysql_stmt_internal_reset(stmt, 0);
  return 0;
}

const char * STDCALL mysql_stmt_sqlstate(MYSQL_STMT *stmt)
{
  return stmt->sqlstate;
}

MYSQL_ROW_OFFSET STDCALL mysql_stmt_row_tell(MYSQL_STMT *stmt)
{
  return(stmt->result_cursor);
}

unsigned long STDCALL mysql_stmt_param_count(MYSQL_STMT *stmt)
{
  return stmt->param_count;
}

MYSQL_ROW_OFFSET STDCALL mysql_stmt_row_seek(MYSQL_STMT *stmt, MYSQL_ROW_OFFSET new_row)
{
  MYSQL_ROW_OFFSET old_row; /* for returning old position */

  old_row= stmt->result_cursor;
  stmt->result_cursor= new_row;

  return(old_row);
}

my_bool STDCALL mysql_stmt_send_long_data(MYSQL_STMT *stmt, uint param_number,
    const char *data, unsigned long length)
{
  CLEAR_CLIENT_ERROR(stmt->mysql);
  CLEAR_CLIENT_STMT_ERROR(stmt);

  if (stmt->state < MYSQL_STMT_PREPARED || !stmt->params)
  {
    SET_CLIENT_STMT_ERROR(stmt, CR_NO_PREPARE_STMT, SQLSTATE_UNKNOWN, 0);
    return(1);
  }

  if (param_number >= stmt->param_count)
  {
    SET_CLIENT_STMT_ERROR(stmt, CR_INVALID_PARAMETER_NO, SQLSTATE_UNKNOWN, 0);
    return(1);
  }

  if (length || !stmt->params[param_number].long_data_used)
  {
    int ret;
    size_t packet_len= STMT_ID_LENGTH + 2 + length;
    uchar *cmd_buff= (uchar *)calloc(1, packet_len);
    int4store(cmd_buff, stmt->stmt_id);
    int2store(cmd_buff + STMT_ID_LENGTH, param_number);
    memcpy(cmd_buff + STMT_ID_LENGTH + 2, data, length);
    stmt->params[param_number].long_data_used= 1;
    ret= stmt->mysql->methods->db_command(stmt->mysql, COM_STMT_SEND_LONG_DATA,
                                         (char *)cmd_buff, packet_len, 1, stmt);
    if (ret)
      UPDATE_STMT_ERROR(stmt);
    free(cmd_buff);
    return(ret);
  }
  return(0);
}

my_bool STDCALL mysql_stmt_send_piece_data(MYSQL_STMT *stmt, unsigned int param_number,
        const char *data, unsigned long length, char piece_type, char is_null)
{

  int ret = 0;
  size_t packet_len;
  uchar *cmd_buff;

  if (param_number >= stmt->param_count)
  {
    SET_CLIENT_STMT_ERROR(stmt, CR_INVALID_PARAMETER_NO, SQLSTATE_UNKNOWN, 0);
    return(1);
  }

  /*
    Send long data packet if there is data or we're sending long data
    for the first time.
  */
  /**
   * Packet: HEADER + PAYLOAD
   *   HEADER:
   *     stmt-id : 4B
   *     param-no : 2B
   *     piece-type : 1B
   *     is-null : 1B
   *     data-len : 8B (fix length)
   *   PAYLOAD:
   *     data : ${data-len}Bytes
   */
  packet_len= PIECE_HEADER_SIZE + length; /* PIECE_HEADER_SIZE:16 */
  cmd_buff= NULL;
  if (NULL == (cmd_buff = (uchar *)calloc(1, packet_len))) {
    SET_CLIENT_STMT_ERROR(stmt, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
    return(1);
  }
  int4store(cmd_buff, stmt->stmt_id);
  int2store(cmd_buff + STMT_ID_LENGTH, param_number);
  cmd_buff[STMT_ID_LENGTH + 2]= piece_type;
  cmd_buff[STMT_ID_LENGTH + 3]= is_null;
  int8store(cmd_buff + STMT_ID_LENGTH + 4, length);
  memcpy(cmd_buff + PIECE_HEADER_SIZE, data, length);

 // stmt->params[param_number].piece_data_used= 1;
  ret= stmt->mysql->methods->db_command(stmt->mysql, COM_STMT_SEND_PIECE_DATA,
                                        (char *)cmd_buff, packet_len, 1, stmt);
  if (ret || cli_read_piece_result(stmt))
    UPDATE_STMT_ERROR(stmt);
  free(cmd_buff);
  return(ret);
//  }
}

unsigned long long STDCALL mysql_stmt_insert_id(MYSQL_STMT *stmt)
{
  return stmt->upsert_status.last_insert_id;
}

unsigned long long STDCALL mysql_stmt_num_rows(MYSQL_STMT *stmt)
{
  return stmt->result.rows;
}

MYSQL_RES* STDCALL mysql_stmt_param_metadata(MYSQL_STMT *stmt __attribute__((unused)))
{
  MYSQL_RES *res;

  if (!stmt->param_count)
    return(NULL);

  /* aloocate result set structutr and copy stmt information */
  if (!(res= (MYSQL_RES *)calloc(1, sizeof(MYSQL_RES))))
  {
    SET_CLIENT_STMT_ERROR(stmt, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
    return(NULL);
  }

  res->eof= 1;
  res->param_fields = stmt->param_fields;
  res->param_count = stmt->param_count;
  return(res);
}
/* Store type of parameter in network buffer. */
static ulong calculate_param_object_type_len(MYSQL *mysql, MYSQL_COMPLEX_BIND_OBJECT *param)
{
  ulong retval;
  ulong len;
  struct st_complex_type *complex_type = get_complex_type(mysql, param->owner_name, param->type_name);

  if (NULL != complex_type) {
    //schema_name
    len = strlen((char *)complex_type->owner_name);
    len += mysql_store_length_size(len);
    retval = len;

    //type_name
    len = strlen((char *)complex_type->type_name);
    len += mysql_store_length_size(len);
    retval += len;

    //version
    len = mysql_store_length_size(complex_type->version);
    retval += len;
  } else {
    //schema_name
    retval = 1;

    //type_name
    len = strlen((char *)param->type_name);
    len += mysql_store_length_size(len);
    retval += len;

    //version
    retval += 1;
  }

  return retval;
}

static ulong calculate_param_array_type_len(MYSQL *mysql, MYSQL_COMPLEX_BIND_ARRAY *param)
{
  ulong len;
  MYSQL_COMPLEX_BIND_HEADER *header = (MYSQL_COMPLEX_BIND_HEADER *)param->buffer;

  //shcema_name
  //type_name
  //version
  len = 3;

  //elem_type
  len += 1;
  if (MYSQL_TYPE_OBJECT == header->buffer_type) {
    MYSQL_COMPLEX_BIND_OBJECT sub_header;
    sub_header.owner_name = ((MYSQL_COMPLEX_BIND_OBJECT*)header)->owner_name;
    sub_header.type_name = ((MYSQL_COMPLEX_BIND_OBJECT*)header)->type_name;
    len += calculate_param_object_type_len(mysql, &sub_header);
  } else if (MYSQL_TYPE_ARRAY == header->buffer_type) {
    MYSQL_COMPLEX_BIND_ARRAY sub_header;
    sub_header.buffer = header->buffer;
    len += calculate_param_array_type_len(mysql, &sub_header);
  }

  return len;
}

static ulong calculate_param_type_len(MYSQL *mysql, MYSQL_BIND *param)
{
  ulong len;
  MYSQL_COMPLEX_BIND_OBJECT *header = (MYSQL_COMPLEX_BIND_OBJECT *)param->buffer;
  if (NULL == header->type_name) {
    len = calculate_param_array_type_len(mysql, (MYSQL_COMPLEX_BIND_ARRAY *)header);
  } else {
    len = calculate_param_object_type_len(mysql, header);
  }

  return len;
}

my_bool STDCALL mysql_stmt_more_results(MYSQL_STMT *stmt)
{
  /* MDEV 4604: Server doesn't set MORE_RESULT flag for
                OutParam result set, so we need to check
                for SERVER_MORE_RESULTS_EXIST and for
                SERVER_PS_OUT_PARAMS)
  */
  return (stmt &&
          stmt->mysql &&
          ((stmt->mysql->server_status & SERVER_MORE_RESULTS_EXIST) ||
           (stmt->mysql->server_status & SERVER_PS_OUT_PARAMS)));
}

int STDCALL mysql_stmt_next_result(MYSQL_STMT *stmt)
{
  int rc= 0;

  if (!stmt->mysql)
  {
    SET_CLIENT_STMT_ERROR(stmt, CR_SERVER_LOST, SQLSTATE_UNKNOWN, 0);
    return(1);
  }

  if (stmt->state < MYSQL_STMT_EXECUTED)
  {
    SET_CLIENT_ERROR(stmt->mysql, CR_COMMANDS_OUT_OF_SYNC, SQLSTATE_UNKNOWN, 0);
    SET_CLIENT_STMT_ERROR(stmt, CR_COMMANDS_OUT_OF_SYNC, SQLSTATE_UNKNOWN, 0);
    return(1);
  }

  if (!mysql_stmt_more_results(stmt))
    return(-1);

  if (stmt->state > MYSQL_STMT_EXECUTED &&
      stmt->state < MYSQL_STMT_FETCH_DONE)
    madb_reset_stmt(stmt, MADB_RESET_ERROR | MADB_RESET_BUFFER | MADB_RESET_LONGDATA);
  stmt->state= MYSQL_STMT_WAITING_USE_OR_STORE;

  if (mysql_next_result(stmt->mysql))
  {
    stmt->state= MYSQL_STMT_FETCH_DONE;
    SET_CLIENT_STMT_ERROR(stmt, stmt->mysql->net.last_errno, stmt->mysql->net.sqlstate,
        stmt->mysql->net.last_error);
    return(1);
  }

  if (stmt->mysql->status == MYSQL_STATUS_GET_RESULT)
    stmt->mysql->status= MYSQL_STATUS_STMT_RESULT; 

  if (stmt->mysql->field_count)
    rc= madb_alloc_stmt_fields(stmt);
  else
  {
    stmt->upsert_status.affected_rows= stmt->mysql->affected_rows;
    stmt->upsert_status.last_insert_id= stmt->mysql->insert_id;
    stmt->upsert_status.server_status= stmt->mysql->server_status;
    stmt->upsert_status.warning_count= stmt->mysql->warning_count;
  }

  stmt->field_count= stmt->mysql->field_count;
  stmt->result.rows= 0;

  return(rc);
}

int STDCALL mariadb_stmt_execute_direct(MYSQL_STMT *stmt,
                                      const char *stmt_str,
                                      size_t length)
{
  MYSQL *mysql;
  my_bool emulate_cmd;
  my_bool clear_result= 0;

  if (!stmt)
    return 1;

  mysql= stmt->mysql;
  if (!mysql)
  {
    SET_CLIENT_STMT_ERROR(stmt, CR_SERVER_LOST, SQLSTATE_UNKNOWN, 0);
    return 1;
  }

  emulate_cmd= !(!(stmt->mysql->server_capabilities & CLIENT_MYSQL) &&
      (stmt->mysql->extension->mariadb_server_capabilities &
      (MARIADB_CLIENT_STMT_BULK_OPERATIONS >> 32))) || mysql->net.compress;

  /* Server versions < 10.2 don't support execute_direct, so we need to 
     emulate it */
  if (emulate_cmd)
  {
    int rc;

    /* avoid sending close + prepare in 2 packets */
    if ((rc= mysql_stmt_prepare(stmt, stmt_str, (unsigned long)length)))
      return rc;
    return mysql_stmt_execute(stmt);
  }

  if (ma_multi_command(mysql, COM_MULTI_ENABLED))
  {
    SET_CLIENT_STMT_ERROR(stmt, CR_COMMANDS_OUT_OF_SYNC, SQLSTATE_UNKNOWN, 0);
    return 1;
  }

  if (length == (size_t) -1)
    length= strlen(stmt_str);

  /* clear flags */
  CLEAR_CLIENT_STMT_ERROR(stmt);
  CLEAR_CLIENT_ERROR(stmt->mysql);
  stmt->upsert_status.affected_rows= mysql->affected_rows= (unsigned long long) ~0;

  /* check if we have to clear results */
  if (stmt->state > MYSQL_STMT_INITTED)
  {
    /* We need to semi-close the prepared statement:
       reset stmt and free all buffers and close the statement
       on server side. Statement handle will get a new stmt_id */
    char stmt_id[STMT_ID_LENGTH];

    if (mysql_stmt_internal_reset(stmt, 1))
      goto fail;

    ma_free_root(&stmt->mem_root, MYF(MY_KEEP_PREALLOC));
    ma_free_root(&stmt->param_fields_mem_root, MYF(MY_KEEP_PREALLOC));
    ma_free_root(&((MADB_STMT_EXTENSION *)stmt->extension)->fields_ma_alloc_root, MYF(0));
    ma_free_root(&((MADB_STMT_EXTENSION *)stmt->extension)->binds_ma_alloc_root, MYF(0));
    stmt->field_count= 0;
    stmt->param_count= 0;
    stmt->params= 0;

    int4store(stmt_id, stmt->stmt_id);
    if (mysql->methods->db_command(mysql, COM_STMT_CLOSE, stmt_id,
                                         sizeof(stmt_id), 1, stmt))
      goto fail;
  }
  stmt->stmt_id= -1;
  if (mysql->methods->db_command(mysql, COM_STMT_PREPARE, stmt_str, length, 1, stmt))
    goto fail;

  /* in case prepare fails, we need to clear the result package from execute, which
     is always an error packet (invalid statement id) */
  clear_result= 1;

  stmt->state= MYSQL_STMT_PREPARED;
  /* Since we can't determine stmt_id here, we need to set it to -1, so server will know that the
   * execute command belongs to previous prepare */
  stmt->stmt_id= -1;
  if (mysql_stmt_execute(stmt))
    goto fail;

  /* flush multi buffer */
  if (ma_multi_command(mysql, COM_MULTI_END))
    goto fail;

  /* read prepare response */
  if (mysql->methods->db_read_prepare_response &&
    mysql->methods->db_read_prepare_response(stmt))
  goto fail;

  clear_result= 0;

  /* metadata not supported yet */

  if (stmt->param_count &&
      stmt->mysql->methods->db_stmt_get_param_metadata(stmt))
  {
    goto fail;
  }

  /* allocated bind buffer for parameters */
  if (stmt->field_count &&
      stmt->mysql->methods->db_stmt_get_result_metadata(stmt))
  {
    goto fail;
  }

  /* allocated bind buffer for result */
  if (stmt->field_count)
  {
    MA_MEM_ROOT *binds_ma_alloc_root = &((MADB_STMT_EXTENSION *)stmt->extension)->binds_ma_alloc_root;
    if (!(stmt->bind= (MYSQL_BIND *)ma_alloc_root(binds_ma_alloc_root, stmt->field_count * sizeof(MYSQL_BIND))))
    {
      SET_CLIENT_STMT_ERROR(stmt, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
      goto fail;
    }
    memset(stmt->bind, 0, sizeof(MYSQL_BIND) * stmt->field_count);
  }
  stmt->state = MYSQL_STMT_PREPARED;

  /* read execute response packet */
  return stmt_read_execute_response(stmt);
fail:
  /* check if we need to set error message */
  if (!mysql_stmt_errno(stmt))
    UPDATE_STMT_ERROR(stmt);
  if (clear_result) {
    do {
      stmt->mysql->methods->db_stmt_flush_unbuffered(stmt);
    } while(mysql_stmt_more_results(stmt));
  }
  stmt->state= MYSQL_STMT_INITTED;
  return 1;
}

MYSQL_FIELD * STDCALL mariadb_stmt_fetch_fields(MYSQL_STMT *stmt)
{
  if (stmt)
    return stmt->fields;
  return NULL;
}


/*
 * from obproxy to compute crc checksum
 */
#if defined(__GNUC__) && defined(__x86_64__)
/* opcodes taken from objdump of "crc32b (%%rdx), %%rcx"
for RHEL4 support (GCC 3 doesn't support this instruction) */
#define crc32_sse42_byte \
  asm(".byte 0xf2, 0x48, 0x0f, 0x38, 0xf0, 0x0a" \
      : "=c"(crc) : "c"(crc), "d"(buf)); \
  len--, buf++

/* opcodes taken from objdump of "crc32q (%%rdx), %%rcx"
for RHEL4 support (GCC 3 doesn't support this instruction) */
#define crc32_sse42_quadword \
  asm(".byte 0xf2, 0x48, 0x0f, 0x38, 0xf1, 0x0a" \
      : "=c"(crc) : "c"(crc), "d"(buf)); \
  len -= 8, buf += 8

inline static uint64_t crc64_sse42(uint64_t uCRC64,
                                   const char *buf, int64_t len)
{
  uint64_t crc = uCRC64;

  if (NULL != buf && len > 0) {
    while (len && ((uint64_t) buf & 7)) {
      crc32_sse42_byte;
    }

    while (len >= 32) {
      crc32_sse42_quadword;
      crc32_sse42_quadword;
      crc32_sse42_quadword;
      crc32_sse42_quadword;
    }

    while (len >= 8) {
      crc32_sse42_quadword;
    }

    while (len) {
      crc32_sse42_byte;
    }
  }

  return crc;
}
#endif /* defined(__GNUC__) && defined(__x86_64__) */
/*
 * for arm
 */
#if 0
#if defined(__GNUC__) && defined(__aarch64__)
#define CRC32CX(crc, value) __asm__("crc32cx %w[c], %w[c], %x[v]":[c]"+r"(crc):[v]"r"(value))
#define CRC32CW(crc, value) __asm__("crc32cw %w[c], %w[c], %w[v]":[c]"+r"(crc):[v]"r"(value))
#define CRC32CH(crc, value) __asm__("crc32ch %w[c], %w[c], %w[v]":[c]"+r"(crc):[v]"r"(value))
#define CRC32CB(crc, value) __asm__("crc32cb %w[c], %w[c], %w[v]":[c]"+r"(crc):[v]"r"(value))
static uint64_t crc64_arm64(uint64_t crc, const char* p, uint64_t len)
{
  int64_t length = len;
  while ((length -= sizeof(uint64_t)) >= 0) {
    CRC32CX(crc, *((uint64_t*)p));
    p +=  sizeof(uint64_t);
  }
  if (length & sizeof(uint32_t)) {
    CRC32CW(crc, *((uint32_t*)p));
    p +=  sizeof(uint32_t);
  }
  if (length & sizeof(uint16_t)) {
    CRC32CH(crc, *((uint16_t*)p));
    p +=  sizeof(uint16_t);
  }
  if (length & sizeof(uint8_t)) {
    CRC32CB(crc, *((uint8_t*)p));
    p +=  sizeof(uint8_t);
  }
  return crc;
}
#endif
#endif

static uint64_t crc64_sse42_manually(uint64_t crc, const char *buf, int64_t len)
{
  /**
   * crc32tab is generated by:
   *   // bit-reversed poly 0x1EDC6F41
   *   const uint32_t poly = 0x82f63b78;
   *   for (int n = 0; n < 256; n++) {
   *       uint32_t c = (uint32_t)n;
   *       for (int k = 0; k < 8; k++)
   *           c = c & 1 ? poly ^ (c >> 1) : c >> 1;
   *       crc32tab[n] = c;
   *   }
   */
  int64_t i = 0;
  static const  uint32_t crc32tab[] =
  {
    0x00000000L, 0xf26b8303L, 0xe13b70f7L, 0x1350f3f4L, 0xc79a971fL,
    0x35f1141cL, 0x26a1e7e8L, 0xd4ca64ebL, 0x8ad958cfL, 0x78b2dbccL,
    0x6be22838L, 0x9989ab3bL, 0x4d43cfd0L, 0xbf284cd3L, 0xac78bf27L,
    0x5e133c24L, 0x105ec76fL, 0xe235446cL, 0xf165b798L, 0x030e349bL,
    0xd7c45070L, 0x25afd373L, 0x36ff2087L, 0xc494a384L, 0x9a879fa0L,
    0x68ec1ca3L, 0x7bbcef57L, 0x89d76c54L, 0x5d1d08bfL, 0xaf768bbcL,
    0xbc267848L, 0x4e4dfb4bL, 0x20bd8edeL, 0xd2d60dddL, 0xc186fe29L,
    0x33ed7d2aL, 0xe72719c1L, 0x154c9ac2L, 0x061c6936L, 0xf477ea35L,
    0xaa64d611L, 0x580f5512L, 0x4b5fa6e6L, 0xb93425e5L, 0x6dfe410eL,
    0x9f95c20dL, 0x8cc531f9L, 0x7eaeb2faL, 0x30e349b1L, 0xc288cab2L,
    0xd1d83946L, 0x23b3ba45L, 0xf779deaeL, 0x05125dadL, 0x1642ae59L,
    0xe4292d5aL, 0xba3a117eL, 0x4851927dL, 0x5b016189L, 0xa96ae28aL,
    0x7da08661L, 0x8fcb0562L, 0x9c9bf696L, 0x6ef07595L, 0x417b1dbcL,
    0xb3109ebfL, 0xa0406d4bL, 0x522bee48L, 0x86e18aa3L, 0x748a09a0L,
    0x67dafa54L, 0x95b17957L, 0xcba24573L, 0x39c9c670L, 0x2a993584L,
    0xd8f2b687L, 0x0c38d26cL, 0xfe53516fL, 0xed03a29bL, 0x1f682198L,
    0x5125dad3L, 0xa34e59d0L, 0xb01eaa24L, 0x42752927L, 0x96bf4dccL,
    0x64d4cecfL, 0x77843d3bL, 0x85efbe38L, 0xdbfc821cL, 0x2997011fL,
    0x3ac7f2ebL, 0xc8ac71e8L, 0x1c661503L, 0xee0d9600L, 0xfd5d65f4L,
    0x0f36e6f7L, 0x61c69362L, 0x93ad1061L, 0x80fde395L, 0x72966096L,
    0xa65c047dL, 0x5437877eL, 0x4767748aL, 0xb50cf789L, 0xeb1fcbadL,
    0x197448aeL, 0x0a24bb5aL, 0xf84f3859L, 0x2c855cb2L, 0xdeeedfb1L,
    0xcdbe2c45L, 0x3fd5af46L, 0x7198540dL, 0x83f3d70eL, 0x90a324faL,
    0x62c8a7f9L, 0xb602c312L, 0x44694011L, 0x5739b3e5L, 0xa55230e6L,
    0xfb410cc2L, 0x092a8fc1L, 0x1a7a7c35L, 0xe811ff36L, 0x3cdb9bddL,
    0xceb018deL, 0xdde0eb2aL, 0x2f8b6829L, 0x82f63b78L, 0x709db87bL,
    0x63cd4b8fL, 0x91a6c88cL, 0x456cac67L, 0xb7072f64L, 0xa457dc90L,
    0x563c5f93L, 0x082f63b7L, 0xfa44e0b4L, 0xe9141340L, 0x1b7f9043L,
    0xcfb5f4a8L, 0x3dde77abL, 0x2e8e845fL, 0xdce5075cL, 0x92a8fc17L,
    0x60c37f14L, 0x73938ce0L, 0x81f80fe3L, 0x55326b08L, 0xa759e80bL,
    0xb4091bffL, 0x466298fcL, 0x1871a4d8L, 0xea1a27dbL, 0xf94ad42fL,
    0x0b21572cL, 0xdfeb33c7L, 0x2d80b0c4L, 0x3ed04330L, 0xccbbc033L,
    0xa24bb5a6L, 0x502036a5L, 0x4370c551L, 0xb11b4652L, 0x65d122b9L,
    0x97baa1baL, 0x84ea524eL, 0x7681d14dL, 0x2892ed69L, 0xdaf96e6aL,
    0xc9a99d9eL, 0x3bc21e9dL, 0xef087a76L, 0x1d63f975L, 0x0e330a81L,
    0xfc588982L, 0xb21572c9L, 0x407ef1caL, 0x532e023eL, 0xa145813dL,
    0x758fe5d6L, 0x87e466d5L, 0x94b49521L, 0x66df1622L, 0x38cc2a06L,
    0xcaa7a905L, 0xd9f75af1L, 0x2b9cd9f2L, 0xff56bd19L, 0x0d3d3e1aL,
    0x1e6dcdeeL, 0xec064eedL, 0xc38d26c4L, 0x31e6a5c7L, 0x22b65633L,
    0xd0ddd530L, 0x0417b1dbL, 0xf67c32d8L, 0xe52cc12cL, 0x1747422fL,
    0x49547e0bL, 0xbb3ffd08L, 0xa86f0efcL, 0x5a048dffL, 0x8ecee914L,
    0x7ca56a17L, 0x6ff599e3L, 0x9d9e1ae0L, 0xd3d3e1abL, 0x21b862a8L,
    0x32e8915cL, 0xc083125fL, 0x144976b4L, 0xe622f5b7L, 0xf5720643L,
    0x07198540L, 0x590ab964L, 0xab613a67L, 0xb831c993L, 0x4a5a4a90L,
    0x9e902e7bL, 0x6cfbad78L, 0x7fab5e8cL, 0x8dc0dd8fL, 0xe330a81aL,
    0x115b2b19L, 0x020bd8edL, 0xf0605beeL, 0x24aa3f05L, 0xd6c1bc06L,
    0xc5914ff2L, 0x37faccf1L, 0x69e9f0d5L, 0x9b8273d6L, 0x88d28022L,
    0x7ab90321L, 0xae7367caL, 0x5c18e4c9L, 0x4f48173dL, 0xbd23943eL,
    0xf36e6f75L, 0x0105ec76L, 0x12551f82L, 0xe03e9c81L, 0x34f4f86aL,
    0xc69f7b69L, 0xd5cf889dL, 0x27a40b9eL, 0x79b737baL, 0x8bdcb4b9L,
    0x988c474dL, 0x6ae7c44eL, 0xbe2da0a5L, 0x4c4623a6L, 0x5f16d052L,
    0xad7d5351L
  };

  for (i = 0; i < len; ++i)
  {
    crc = crc32tab[(crc ^ buf[i]) & 0xff] ^ (crc >> 8);
  }

  return crc;
}
typedef uint64_t (*ObCRC64Func)(uint64_t, const char *, int64_t);
uint64_t crc64_sse42_dispatch(uint64_t crc, const char *buf, int64_t len)
{
  uint32_t a = 0;
  uint32_t b = 0;
  uint32_t c = 0;
  uint32_t d = 0;
  ObCRC64Func ob_crc64_sse42_func;
#if defined(__GNUC__) && defined(__x86_64__)
  asm("cpuid": "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "0"(1));
  if (c & (1 << 20)) {
    ob_crc64_sse42_func = &crc64_sse42;
  } else {
    ob_crc64_sse42_func = &crc64_sse42_manually;
  }
// #elif defined(__GNUC__) && defined(__aarch64__)
//   ob_crc64_sse42_func = &crc64_arm64;
#else
  ob_crc64_sse42_func = &crc64_sse42_manually;
#endif
  return (*ob_crc64_sse42_func)(crc, buf, len);
}
uint32 ob_crc32(uint64_t crc, const char *buf, int64_t len)
{
  uint64_t crc64 = crc64_sse42_dispatch(crc, buf, len);
  return crc64 & 0xffffffff;
}

uint64 ob_crc64(uint64_t crc, const char *buf, int64_t len)
{
  uint64_t crc64 = crc64_sse42_dispatch(crc, buf, len);
  return crc64;
}

typedef enum enum_sql_char_state
{
  NORMAL_CHAR_STATE = 0,
  SINGLE_QUOTE_STATE, //find single quote
  DOUBLE_QUOTE_STATE, //find double quote
  SINGLE_LINE_COMMENT_STATE, //find -- ,single line comment
  MULTI_LINE_COMMENT_STATE //find /* multi line comment
} sql_char_state;

static unsigned int find_sql_param_count(const char *sql, int sql_len)
{
  int param_count = 0;
  int i = 0;
  int char_state = NORMAL_CHAR_STATE;
  int is_line_first = 1;
  if (sql == NULL)
  {
    return param_count;
  }
  for (i = 0; i < sql_len; i++)
  {
    switch(char_state)
    {
      case NORMAL_CHAR_STATE:
      {
        if (sql[i] == SINGLE_QUOTE)
        {
          char_state = SINGLE_QUOTE_STATE;
        }
        else if (sql[i] == DOUBLE_QUOTE)
        {
          char_state = DOUBLE_QUOTE_STATE;
        }
        else if (is_line_first && sql[i] == '-' && i+1 < sql_len && sql[i+1] == '-') // line begin with --
        {
          char_state = SINGLE_LINE_COMMENT_STATE;
          ++i;
        }
        else if (sql[i] == '/' && i+1 < sql_len && sql[i+1] == '*') // /*
        {
          char_state = MULTI_LINE_COMMENT_STATE;
          ++i;
        }
        else if (sql[i] == '?')
        {
          ++param_count;
        }
        //Not need to handle like .. escape '/' escpically, it count be '/" in Oracle.        
//        else if ((sql[i] == 'l' || sql[i] == 'L') && i > 0 && sql_len - i > 4
//                      //&& (sql[i-1] == ' ' || sql[i-1] == '\r' || sql[i-1] == '\n')
//                      && 0 == strncasecmp((char *)(&(sql[i])), "like", 4)
//                      && (sql[i+1] == ' ' || sql[i+1] == '\r' || sql[i+1] == '\n')
//                  ) { // check and handle the like/LIKE 
//        
//        } 
        break;
      }
      case SINGLE_QUOTE_STATE:
      {
        /** Important! In oracle escape character for ' only is ', such as:
         *   --select '''' from dual;
         *     output: '
         *   --select 'name'||'''' from dual;
         *     output: name''
         *  \ name is not the escape character of '(quote) and "(double quote).
         * */
        //if (sql[i] == SINGLE_QUOTE && sql[i - 1] != '\\')
        if (sql[i] == SINGLE_QUOTE)
        {
          char_state = NORMAL_CHAR_STATE;
        }
        break;
      }
      case DOUBLE_QUOTE_STATE:
      {
        //if (sql[i] == DOUBLE_QUOTE && sql[i - 1] != '\\')
        if (sql[i] == DOUBLE_QUOTE)
        {
          char_state = NORMAL_CHAR_STATE;
        }
        break;
      }
      case SINGLE_LINE_COMMENT_STATE:
      {
        if (sql[i] == '\n')
        {
          char_state = NORMAL_CHAR_STATE;
        }
        break;
      }
      case MULTI_LINE_COMMENT_STATE:
      {
        if (sql[i] == '*' && i+1 < sql_len && sql[i+1] == '/')// */
        {
          char_state = NORMAL_CHAR_STATE;
          ++i;
        }
        break;
      }
      default:
        break;
    }
    is_line_first = 0;
    if (sql[i] == '\n')
    {
      is_line_first = 1;
    }
  }
  return param_count;
}
/*
 * judge where can send plarray maxrarr len
 * use  version to judge
 */
my_bool determine_send_plarray_maxrarr_len(MYSQL *mysql)
{
  my_bool bret = FALSE;
  int ival = SEND_PLARRAY_MAXRARRLEN_AUTO_OPEN; //default is AUTO
  int tmp_val = 0;
  char* env = getenv("ENABLE_PLARRAY_MAXRARRLEN");
  if (env) {
    tmp_val = atoi(env);
    if (tmp_val >= 0 && tmp_val < SEND_PLARRAY_MAXRARRLEN_FLAG_MAX) {
      ival = tmp_val;
    }
  }
  if (ival == SEND_PLARRAY_MAXRARRLEN_FORCE_OPEN) {
    bret = TRUE;
  } else if (ival == SEND_PLARRAY_MAXRARRLEN_FORCE_CLOSE) {
    bret = FALSE;
  } else if (NULL != mysql) {
    if (!mysql->oracle_mode) {
      // only oracle mode use new protocol
    } else {
      if (mysql->ob_server_version >= SUPPORT_SEND_PLARRAY_MAXRARR_LEN) {
        bret = TRUE;
      }
    }
  }
  if (mysql) {
    mysql->can_send_plarray_maxrarr_len = bret;
  }
  DBUG_RETURN(bret);
}

my_bool get_support_send_plarray_maxrarr_len(MYSQL *mysql)
{
  if (mysql) {
    return mysql->can_send_plarray_maxrarr_len;
  }
  return FALSE;
}
/*
 * judge where can use PL bindbyname
 * use  version to judge
 */
my_bool determine_plarray_bindbyname(MYSQL *mysql)
{
  my_bool bret = FALSE;
  int ival = PLARRAY_BINDBYNAME_AUTO_OPEN; //default is AUTO
  int tmp_val = 0;
  char* env = getenv("ENABLE_PLARRAY_BINDBYNAME");
  if (env) {
    tmp_val = atoi(env);
    if (tmp_val >= 0 && tmp_val < PLARRAY_BINDBYNAME_FLAG_MAX) {
      ival = tmp_val;
    }
  }
  if (ival == PLARRAY_BINDBYNAME_FORCE_OPEN) {
    bret = TRUE;
  } else if (ival == PLARRAY_BINDBYNAME_FORCE_CLOSE) {
    bret = FALSE;
  } else if (NULL != mysql) {
    if (!mysql->oracle_mode) {
      // only oracle mode use new protocol
    } else {
      if (mysql->ob_server_version >= SUPPORT_PLARRAY_BINDBYNAME) {
        bret = TRUE;
      }
    }
  }
  if (mysql) {
    mysql->can_plarray_bindbyname = bret;
  }
  DBUG_RETURN(bret);
}

my_bool get_support_plarray_bindbyname(MYSQL *mysql)
{
  if (mysql) {
    return mysql->can_plarray_bindbyname;
  }
  return FALSE;
}

/*
 * judge where can use protocol ob20 
 * use capability flag to judge
 */
my_bool determine_protocol_ob20(MYSQL *mysql)
{
  my_bool bret = TRUE;
  int ival = PROTOCOL_OB20_AUTO_OPEN; //default is AUTO
  int tmp_val = 0;
  char* env = getenv("ENABLE_PROTOCOL_OB20");
  if (env) {
    tmp_val = atoi(env);
    if (tmp_val >= 0 && tmp_val < PROTOCOL_OB20_FLAG_MAX) {
      ival = tmp_val;
    }
  }
  if (ival == PROTOCOL_OB20_FORCE_OPEN) {
    bret = TRUE;
  } else if (ival == PROTOCOL_OB20_FORCE_CLOSE) {
    bret = FALSE;
  }
  if (mysql) {
    mysql->can_use_protocol_ob20 = bret;
  }
  DBUG_RETURN(bret);
}

my_bool get_use_protocol_ob20(MYSQL *mysql)
{
  my_bool bret = FALSE;
  if (mysql && (mysql->capability & OBCLIENT_CAP_OB_PROTOCOL_V2)) {
    bret = TRUE;
  }
  return bret;
}

my_bool determine_full_link_trace(MYSQL *mysql)
{
  my_bool bret = TRUE;
  int ival = PROTOCOL_FLT_AUTO_OPEN; //default is AUTO
  int tmp_val = 0;
  char* env = getenv("ENABLE_FLT");
  if (env) {
    tmp_val = atoi(env);
    if (tmp_val >= 0 && tmp_val < PROTOCOL_FLT_FLAG_MAX) {
      ival = tmp_val;
    }
  }
  if (ival == PROTOCOL_FLT_FORCE_OPEN) {
    bret = TRUE;
  } else if (ival == PROTOCOL_FLT_FORCE_CLOSE) {
    bret = FALSE;
  }
  if (mysql) {
    mysql->can_use_full_link_trace = bret;
  }
  DBUG_RETURN(bret);
}

my_bool get_use_full_link_trace(MYSQL *mysql)
{
  my_bool bret = FALSE;
  if (mysql && (mysql->capability & OBCLIENT_CAP_FULL_LINK_TRACE)) {
    bret = TRUE;
    if (mysql->capability & OBCLIENT_CAP_PROXY_NEW_EXTRA_INFO) {
      // do nothing, bret = TRUE;
    } else {
      bret = FALSE;
    }
  }
  return bret;
}

my_bool determine_flt_show_trace(MYSQL *mysql)
{
  my_bool bret = TRUE;
  int ival = FLT_SHOW_TRACE_AUTO_OPEN; //default is AUTO
  int tmp_val = 0;
  char* env = getenv("ENABLE_FLT_SHOW_TRACE");
  if (env) {
    tmp_val = atoi(env);
    if (tmp_val >= 0 && tmp_val < FLT_SHOW_TRACE_FLAG_MAX) {
      ival = tmp_val;
    }
  }
  if (ival == FLT_SHOW_TRACE_FORCE_OPEN) {
    bret = TRUE;
  } else if (ival == FLT_SHOW_TRACE_FORCE_CLOSE) {
    bret = FALSE;
  }
  if (mysql) {
    mysql->can_use_flt_show_trace = bret;
  }
  DBUG_RETURN(bret);
}

my_bool get_use_flt_show_trace(MYSQL *mysql)
{
  my_bool bret = FALSE;
  if (mysql && (mysql->capability & OBCLIENT_CAP_FULL_LINK_TRACE)
    && (mysql->capability & OBCLIENT_CAP_PROXY_NEW_EXTRA_INFO)) {
    bret = TRUE;
    if (mysql->capability & OBCLIENT_CAP_PROXY_FULL_LINK_TRACE_SHOW_TRACE) {
      // do nothing, bret = TRUE;
    } else {
      bret = FALSE;
    }
  }
  return bret;
}

my_bool determine_ob_client_lob_locatorv2(MYSQL *mysql)
{
  my_bool bret = TRUE;
  int ival = OB_CLIENT_LOB_LOCATORV2_AUTO_OPEN; //default is AUTO
  int tmp_val = 0;
  char* env = getenv("ENABLE_OB_CLIENT_LOB_LOCATORV2");
  if (env) {
    tmp_val = atoi(env);
    if (tmp_val >= 0 && tmp_val < OB_CLIENT_LOB_LOCATORV2_FLAY_MAX) {
      ival = tmp_val;
    }
  }
  if (ival == OB_CLIENT_LOB_LOCATORV2_FORCE_OPEN) {
    bret = TRUE;
  } else if (ival == OB_CLIENT_LOB_LOCATORV2_FORCE_CLOSE) {
    bret = FALSE;
  }
  if (mysql) {
    mysql->can_use_ob_client_lob_locatorv2 = bret;
  }
  DBUG_RETURN(bret);
}
my_bool get_use_ob_client_lob_locatorv2(MYSQL *mysql)
{
  if (mysql) {
    return mysql->can_use_ob_client_lob_locatorv2;
  }
  return FALSE;
}

uint8_t get_ob_lob_locator_version(void *lob)
{
  uint8_t ver = 0;
  if (NULL == lob) {
    ver = 0;
  } else {
    ObClientMemLobCommon *plob = (ObClientMemLobCommon*)lob;
    ver = plob->version_;
  }
  return ver;
}
int64_t get_ob_lob_payload_data_len(void *lob)
{
  int64_t len = -1;
  if (NULL == lob) {
    len = -1;
  } else {
    ObClientMemLobCommon *plob = (ObClientMemLobCommon*)lob;
    if (plob->version_ == OBCLIENT_LOB_LOCATORV1) {
      OB_LOB_LOCATOR *tmp = (OB_LOB_LOCATOR*)lob;
      len = tmp->payload_size_;
    } else if (plob->version_ == OBCLIENT_LOB_LOCATORV2) {
      OB_LOB_LOCATOR_V2 *tmp = (OB_LOB_LOCATOR_V2*)lob;
      if (0 == tmp->common.has_extern)
        len = -1;

      if (tmp->common.is_inrow_) {
        len = tmp->extern_header.payload_size_;
      } else {
        //这种情况下的数据大小是位置在
        //sizeof(ObMemLobCommon ) + sizeof(ObMemLobExternHeader) + sizeof(uint16)+ ex_size + rowkey_size + sizeof(ObLobCommon)+sizeof(ObLobData)
        int tmp_len = tmp->extern_header.payload_offset_+ tmp->extern_header.payload_size_;
        char *tmp_buf = tmp->data_;
        uint16_t ex_size = uint2korr(tmp_buf);
        int offset = MAX_OB_LOB_LOCATOR_HEADER_LENGTH + sizeof(uint16) + ex_size + tmp->extern_header.rowkey_size_ + sizeof(ObClientLobCommon) + sizeof(ObClientLobData);
        if (tmp_len > offset) {
          len = uint8korr(tmp_buf + offset - 8);
        }
      }
    }
  }
  return len;
}

int prepare_execute_v2(MYSQL *mysql, MYSQL_STMT* stmt, const char* query, MYSQL_BIND* params) {
  int ret = 0;
  int length = strlen(query);
  if (get_use_prepare_execute(mysql)) {
    void* extend_arg = NULL;
    if (mysql_stmt_prepare_v2(stmt, query, length, extend_arg)) {
      ret = -1;
    } else if (params && mysql_stmt_bind_param(stmt, params)) {
      ret = -1;
    } else if (mysql_stmt_execute_v2(stmt, query, length, 1, 0, extend_arg)) {
      ret = -1;
    }
  } else {
    if (mysql_stmt_prepare(stmt, query, length)) {
      ret = -1;
    } else if (params && mysql_stmt_bind_param(stmt, params)) {
      ret = -1;
    } else if (mysql_stmt_execute(stmt)) {
      ret = -1;
    }
  }
  return ret;
}
int stmt_get_data_from_lobv2( MYSQL *mysql, void * lob, enum_field_types type, 
  int64_t char_offset, int64_t byte_offset, int64_t char_len, int64_t byte_len, char *buf, const int64_t buf_len, int64_t *data_len, int64_t *act_len)
{
  int ret = -1;
  const char *read_sql = "call DBMS_LOB.read(?, ?, ?, ?)";
  MYSQL_BIND param_bind[4];
  MYSQL_BIND param_res[2];
  MYSQL_STMT* stmt = NULL;
  int64_t length = 0;
  OB_LOB_LOCATOR_V2 *loblocator = (OB_LOB_LOCATOR_V2*)lob;

  char_offset = char_offset;
  char_len = char_len;

  if (!mysql) {
    SET_CLIENT_STMT_ERROR(stmt, CR_SERVER_LOST, unknown_sqlstate, NULL);
    DBUG_RETURN(1);
  }
  if (NULL == lob) {
    SET_CLIENT_STMT_ERROR(stmt, CR_UNKNOWN_ERROR, unknown_sqlstate, NULL);
    DBUG_RETURN(1);
  }
  if (OBCLIENT_LOB_LOCATORV2 != get_ob_lob_locator_version(lob)) {
    SET_CLIENT_STMT_ERROR(stmt, CR_UNKNOWN_ERROR, unknown_sqlstate, NULL);
    DBUG_RETURN(1);
  }
  if (type != MYSQL_TYPE_ORA_CLOB && type != MYSQL_TYPE_ORA_BLOB) {
    SET_CLIENT_STMT_ERROR(stmt, CR_UNKNOWN_ERROR, unknown_sqlstate, NULL);
    DBUG_RETURN(1);
  }
  if (1 != loblocator->common.has_extern) {
    SET_CLIENT_STMT_ERROR(stmt, CR_UNKNOWN_ERROR, unknown_sqlstate, NULL);
    DBUG_RETURN(1);
  }

  if (1 == loblocator->common.is_inrow_) {
    length = byte_len > buf_len ? buf_len : byte_len;
    length = length > loblocator->extern_header.payload_size_ ? loblocator->extern_header.payload_size_ : length;
    memcpy(buf, loblocator->data_ + loblocator->extern_header.payload_offset_, length);
    if (data_len)
      *data_len = length;
    if (act_len)
      *act_len = loblocator->extern_header.payload_size_;
    ret = 0;
  } else {
    int64_t tmp_len = byte_len + 1;
    char *tmp_buf = calloc(1, byte_len);
    if (NULL != tmp_buf) {
      //params
      memset(param_bind, 0, sizeof(param_bind));
      param_bind[0].buffer = lob;
      param_bind[0].buffer_length = loblocator->extern_header.payload_size_ + loblocator->extern_header.payload_offset_ + MAX_OB_LOB_LOCATOR_HEADER_LENGTH;
      param_bind[0].buffer_type = type;
      param_bind[1].buffer = (char *)&tmp_len;
      param_bind[1].buffer_type = MYSQL_TYPE_LONGLONG;
      param_bind[2].buffer = (char *)&byte_offset;
      param_bind[2].buffer_type = MYSQL_TYPE_LONGLONG;
      param_bind[3].is_null = &param_bind[3].is_null_value;
      *param_bind[3].is_null = 1;
      if (MYSQL_TYPE_ORA_CLOB == type) {
        param_bind[3].buffer_type = MYSQL_TYPE_VAR_STRING;
      } else {
        param_bind[3].buffer_type = MYSQL_TYPE_OB_RAW;
      }

      //result
      memset(param_res, 0, sizeof(param_res));
      param_res[0].error = &param_res[0].error_value;
      param_res[0].is_null = &param_res[0].is_null_value;
      param_res[0].buffer = &length;
      param_res[0].buffer_type = MYSQL_TYPE_LONGLONG;
      param_res[1].error = &param_res[1].error_value;
      param_res[1].is_null = &param_res[1].is_null_value;
      param_res[1].length = &param_res[1].length_value;
      param_res[1].buffer = tmp_buf;
      param_res[1].buffer_length = tmp_len;
      if (MYSQL_TYPE_ORA_CLOB == type) {
        param_res[1].buffer_type = MYSQL_TYPE_VAR_STRING;
      } else {
        param_res[1].buffer_type = MYSQL_TYPE_OB_RAW;
      }

      if (NULL == (stmt = mysql_stmt_init(mysql))) {
        ret = -1;
      } else if (prepare_execute_v2(mysql, stmt, read_sql, param_bind)) {
        ret = -1;
      } else if (mysql_stmt_bind_result(stmt, param_res)) {
        ret = -1;
      } else {
        ret = mysql_stmt_fetch(stmt);
        if (0 == ret) {
          *data_len = param_res[1].length_value;
          *act_len = *data_len;
          if (*data_len > byte_len) {
            *data_len = byte_len > buf_len ? buf_len : byte_len;
          } else {
            *data_len = *data_len > buf_len ? buf_len : *data_len;
          }
          memcpy(buf, tmp_buf, *data_len);
        }
      }

      if (NULL != stmt) {
        mysql_stmt_close(stmt);
      }

      free(tmp_buf);
      tmp_buf = NULL;
    }
  }
  return ret;
}

my_bool set_nls_format(MYSQL *mysql)
{
  my_bool bret = TRUE;
  if (mysql->oracle_mode) {
    char *nls_date_format = getenv("NLS_DATE_FORMAT");
    char *nls_timestamp_format = getenv("NLS_TIMESTAMP_FORMAT");
    char *nls_timestamp_tz_format = getenv("NLS_TIMESTAMP_TZ_FORMAT");

    if (NULL != nls_date_format) {
      char change_date_format_sql[100];
      snprintf(change_date_format_sql, 100, "ALTER SESSION SET NLS_DATE_FORMAT='%s';", nls_date_format);
      if (mysql_query(mysql, change_date_format_sql)) {
        bret = FALSE;
      }
    }
    if (bret && NULL != nls_timestamp_format) {
      char change_timestamp_format_sql[100];
      snprintf(change_timestamp_format_sql, 100, "ALTER SESSION SET NLS_TIMESTAMP_FORMAT='%s';", nls_timestamp_format);
      if (mysql_query(mysql, change_timestamp_format_sql)) {
        bret = FALSE;
      }
    }
    if (bret && NULL != nls_timestamp_tz_format) {
      char change_timestamp_tz_format_sql[100];
      snprintf(change_timestamp_tz_format_sql, 100, "ALTER SESSION SET NLS_TIMESTAMP_TZ_FORMAT='%s';", nls_timestamp_tz_format);
      if (mysql_query(mysql, change_timestamp_tz_format_sql)) {
        bret = FALSE;
      }
    }
  }
  return bret;
}
/*
 * judge where can use prepare_execute protocol
 * use  version to judge
 */
my_bool determine_use_prepare_execute(MYSQL *mysql)
{
  my_bool bret = FALSE;
  int ival = PREPARE_EXECUTE_AUTO_OPEN; //default is AUTO
  int tmp_val = 0;
  char* env = getenv("ENABLE_PREPARE_EXECUTE");
  //default use client_cursor;
  if (env) {
    tmp_val = atoi(env);
    if (tmp_val >= 0 && tmp_val < PREPARE_EXECUTE_FLAG_MAX)
    {
      ival = tmp_val;
    }
  }
  if (ival == PREPARE_EXECUTE_FORCE_OPEN)
  {
    bret = TRUE;
  }
  else if (ival == PREPARE_EXECUTE_FORCE_CLOSE)
  {
    bret = FALSE;
  }
  else if (NULL != mysql)
  {
    if (!mysql->oracle_mode)
    {
      // only oracle mode use new protocol
    }
    else
    {
      if (mysql->ob_server_version >= SUPPORT_PREPARE_EXECUTE_VERSION)
      {
        bret = TRUE;
      }
    }
  }
  if (mysql)
  {
    mysql->can_use_prepare_execute = bret;
  }
  DBUG_RETURN(bret);
}
my_bool get_use_prepare_execute(MYSQL* mysql)
{
  if (mysql)
  {
    return mysql->can_use_prepare_execute;
  }
  return FALSE;
}
my_bool get_use_preapre_execute(MYSQL* mysql)
{
  if (mysql)
  {
    return mysql->can_use_prepare_execute;
  }
  return FALSE;
}
my_bool get_support_send_fetch_flag(MYSQL *mysql)
{
  my_bool bret = FALSE;
  if (mysql)
  {
    if (mysql->oracle_mode && mysql->ob_server_version >= SUPPORT_SEND_FETCH_FLAG_VERSION)
    {
      bret = TRUE;
    }
  }
  DBUG_RETURN(bret);
}

my_bool STDCALL is_returning_result(MYSQL_STMT *stmt)
{
  return stmt != NULL ? stmt->is_handle_returning_into : FALSE;
}

my_bool STDCALL has_added_user_fields(MYSQL_STMT *stmt)
{
  return stmt != NULL ? stmt->has_added_user_fields : FALSE;
}

my_bool STDCALL is_pl_out_result(MYSQL_STMT *stmt)
{
  return stmt != NULL ? stmt->is_pl_out_resultset : FALSE;
}

unsigned long STDCALL stmt_pre_exe_req_ext_flag_get(MYSQL_STMT *stmt)
{
  return stmt != NULL ? stmt->ext_flag : 0;
}

void STDCALL stmt_pre_exe_req_ext_flag_set(MYSQL_STMT *stmt, unsigned long flag)
{
  if (NULL != stmt) {
    stmt->ext_flag = flag;
  }
}

/*
 * mysql_stmt_prepare_v2 prepare local
 */
int STDCALL
mysql_stmt_prepare_v2(MYSQL_STMT *stmt, const char *query,
                           unsigned long length,
                           void* extend_arg)
{
  MYSQL *mysql= stmt->mysql;
  PREPARE_EXTEND_ARGS *args = (PREPARE_EXTEND_ARGS *)extend_arg;
  int rc= 1;
  if (!mysql)
  {
    /* mysql can be reset in mysql_close called from mysql_reconnect */
    SET_CLIENT_STMT_ERROR(stmt, CR_SERVER_LOST, unknown_sqlstate, NULL);
    DBUG_RETURN(1);
  }

  if (!mysql->can_use_prepare_execute)
  {
    //not support will use old api
    DBUG_RETURN(mysql_stmt_prepare(stmt, query, length));
  }
  /*
    Reset the last error in any case: that would clear the statement
    if the previous prepare failed.
  */
  if (length == (unsigned long) -1)
    length= (unsigned long)strlen(query);

  /* clear flags */
  CLEAR_CLIENT_STMT_ERROR(stmt);
  CLEAR_CLIENT_ERROR(stmt->mysql);
  stmt->upsert_status.affected_rows= mysql->affected_rows= (unsigned long long) ~0;
  stmt->use_prepare_execute = TRUE;
  if ((int) stmt->state > (int) MYSQL_STMT_INITTED)
  {
    /* This is second prepare with another statement */
    if (mysql_stmt_internal_reset(stmt, 1))
      DBUG_RETURN(1);
    /*
      These members must be reset for API to
      function in case of error or misuse.
    */
    stmt->bind_param_done= stmt->bind_result_done= FALSE;
    stmt->param_count= stmt->field_count= 0;
    ma_free_root(&stmt->mem_root, MYF(MY_KEEP_PREALLOC));
    ma_free_root(&((MADB_STMT_EXTENSION *)stmt->extension)->fields_ma_alloc_root, MYF(MY_KEEP_PREALLOC));
    ma_free_root(&((MADB_STMT_EXTENSION *)stmt->extension)->binds_ma_alloc_root, MYF(MY_KEEP_PREALLOC));
    ma_free_root(&stmt->param_fields_mem_root, MYF(MY_KEEP_PREALLOC));
    stmt->param_fields = 0;
    stmt->field_count = 0;
    stmt->params = 0;

    /*
      Close statement in server

      If there was a 'use' result from another statement, or from
      mysql_use_result it won't be freed in mysql_stmt_free_result and
      we should get 'Commands out of sync' here.
    */
    if (stmt->stmt_id > 0) {
      char stmt_id[STMT_ID_LENGTH];
      int4store(stmt_id, stmt->stmt_id);
      if (mysql->methods->db_command(mysql, COM_STMT_CLOSE, stmt_id,
                                           sizeof(stmt_id), 1, stmt))
        goto fail;
    }
  }
  /*add for support prepare_execute protocol*/
  stmt->stmt_id = 0;
  stmt->check_sum = ob_crc32(0, query, length);
  if (NULL == args) {
    stmt->param_count = find_sql_param_count(query, length);
  } else {
    stmt->param_count = args->params_count;
  }
  /*
  alloc_root will return valid address even in case when param_count
  and field_count are zero. Thus we should never rely on stmt->bind
  or stmt->params when checking for existence of placeholders or
  result set.
*/
  if (!(stmt->params= (MYSQL_BIND *) ma_alloc_root(&stmt->mem_root,
            sizeof(MYSQL_BIND)*
                                                (stmt->param_count +
                                                 stmt->field_count))))
  {
    SET_CLIENT_STMT_ERROR(stmt, CR_OUT_OF_MEMORY, unknown_sqlstate, NULL);
    DBUG_RETURN(1);
  }
  stmt->bind = NULL;
  stmt->state= MYSQL_STMT_PREPARED;
  DBUG_RETURN(0);
fail:
  stmt->state= MYSQL_STMT_INITTED;
  UPDATE_STMT_ERROR(stmt);
  return(rc);
}
static int
mysql_stmt_execute_describe_only(MYSQL_STMT *stmt, const char *query, ulong length)
{
  MYSQL *mysql= stmt->mysql;
  unsigned int check_sum = 0;
  int rc = 1;
  int ret = 0;
  FLT_DECLARE;

  if (!mysql)
  {
    /* mysql can be reset in mysql_close called from mysql_reconnect */
    SET_CLIENT_STMT_ERROR(stmt, CR_SERVER_LOST, unknown_sqlstate, NULL);
    DBUG_RETURN(1);
  }

  /*
    Reset the last error in any case: that would clear the statement
    if the previous prepare failed.
  */
  if (length == (unsigned long) -1)
    length= (unsigned long)strlen(query);

  /* clear flags */
  CLEAR_CLIENT_STMT_ERROR(stmt);
  CLEAR_CLIENT_ERROR(mysql);
  stmt->upsert_status.affected_rows= mysql->affected_rows= (unsigned long long) ~0;
  stmt->last_errno= 0;
  stmt->last_error[0]= '\0';
  stmt->use_prepare_execute = TRUE;
  check_sum = ob_crc32(0, query, length);
  if (check_sum != stmt->check_sum)
  {
    SET_CLIENT_STMT_ERROR(stmt, CR_SERVER_LOST, unknown_sqlstate, NULL);
    snprintf(stmt->last_error, MYSQL_ERRMSG_SIZE, "not same sql with prepare");
    DBUG_RETURN(1);
  }
  if ((int) stmt->state > (int) MYSQL_STMT_INITTED)
  {
    /* This is second prepare with another statement */               /* 4 bytes - stmt id */
    if (mysql_stmt_internal_reset(stmt, 1))
      goto fail;
    /*
      These members must be reset for API to
      function in case of error or misuse.
    */
    stmt->bind_param_done= stmt->bind_result_done= FALSE;
    stmt->param_count= stmt->field_count= 0;
    ma_free_root(&stmt->mem_root, MYF(MY_KEEP_PREALLOC));
    ma_free_root(&((MADB_STMT_EXTENSION *)stmt->extension)->fields_ma_alloc_root, MYF(MY_KEEP_PREALLOC));
    ma_free_root(&((MADB_STMT_EXTENSION *)stmt->extension)->binds_ma_alloc_root, MYF(MY_KEEP_PREALLOC));
    ma_free_root(&stmt->param_fields_mem_root, MYF(MY_KEEP_PREALLOC));
    stmt->param_fields = 0;
    stmt->field_count= 0;
    stmt->params= 0;

    /*
      Close statement in server

      If there was a 'use' result from another statement, or from
      mysql_use_result it won't be freed in mysql_stmt_free_result and
      we should get 'Commands out of sync' here.
    */
    if (stmt->stmt_id > 0) {
      char stmt_id[STMT_ID_LENGTH];
      int4store(stmt_id, stmt->stmt_id);
      if (mysql->methods->db_command(mysql, COM_STMT_CLOSE, stmt_id,
                                           sizeof(stmt_id), 1, stmt))
        goto fail;
    }
  }

  FLT_BEFORE_COMMAND(0, FLT_TAG_COMMAND_NAME, "\"mysql_stmt_execute_describe_only\"");

  if (mysql->methods->db_command(mysql, COM_STMT_PREPARE, query, length, 1, stmt))
    ret = 1;

  if (0 == ret) {
    if (mysql->methods->db_read_prepare_response && mysql->methods->db_read_prepare_response(stmt))
      ret = 1;
  }

  if (0 == ret) {
    if (stmt->param_count && mysql->methods->db_stmt_get_param_metadata(stmt))
      ret =1;
  }

  FLT_AFTER_COMMAND;

  if (1 == ret) {
    goto fail;
  }

   /* allocated bind buffer for parameters */
  if (stmt->field_count &&
      mysql->methods->db_stmt_get_result_metadata(stmt))
  {
    goto fail;
  }
  if (stmt->param_count)
  {
    if (stmt->prebind_params)
    {
      if (stmt->prebind_params != stmt->param_count)
      {
        SET_CLIENT_STMT_ERROR(stmt, CR_INVALID_PARAMETER_NO, SQLSTATE_UNKNOWN, 0);
        goto fail;
      }
    } else {
      if (!(stmt->params= (MYSQL_BIND *)ma_alloc_root(&stmt->mem_root, stmt->param_count * sizeof(MYSQL_BIND))))
      {
        SET_CLIENT_STMT_ERROR(stmt, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
        goto fail;
      }
      memset(stmt->params, '\0', stmt->param_count * sizeof(MYSQL_BIND));
    }
  }
  stmt->bind_size = 0;
  stmt->bind= 0;
  stmt->state= MYSQL_STMT_PREPARED;
  DBUG_RETURN(0);
fail:
  stmt->state= MYSQL_STMT_INITTED;
  UPDATE_STMT_ERROR(stmt);
  return(rc);
}
unsigned char* mysql_stmt_prepare_execute_generate_request(MYSQL_STMT* stmt, const char *query,
                                  unsigned long query_length, size_t* request_len)
{
  size_t length= 1024;
  size_t free_bytes= 0;
  size_t null_byte_offset= 0;
  size_t offset = 0;
  uint i;
  uchar *start= NULL, *p;
  size_t null_count = 0;
  MYSQL* mysql = stmt->mysql;
   /* preallocate length bytes */
  /* check: gr */
  if (!(start= p= (uchar *)malloc(length)))
    goto mem_error;
  int4store(p, stmt->stmt_id);
  p += STMT_ID_LENGTH;
  /* flags is 4 bytes, we store just 1 */
  int1store(p, (unsigned char) stmt->flags);
  p++;
  int4store(p, stmt->iteration_count);
  p += 4;
  p = mysql_net_store_length(p, query_length);
  offset = p - start;
  free_bytes = length - offset;
  if (free_bytes < query_length + 4 + 20) { //store query and param_count
    length = offset + query_length + length;// alloc more
    if (!(start = (uchar*) realloc(start, length))) {
      goto mem_error;
    }
    p = start + offset;
  }
  memcpy(p, query, query_length);
  p += query_length;
  int4store(p, stmt->param_count);
  p +=4;

  if (stmt->param_count) {
    ulong extra_length = 0;
    for (i = 0; i < stmt->param_count; i++)
    {
      MYSQL_BIND* param = &stmt->params[i];
      if (MYSQL_TYPE_OBJECT == param->buffer_type)
      {
        extra_length += calculate_param_type_len(mysql, param);
      }
    }
    null_count = (stmt->param_count + 7)/8;
    offset = p - start;
    free_bytes = length - offset;
    if (null_count + 20 > free_bytes)
    {
      length = offset + null_count + 20 + length;
      if (!(start = (uchar*) realloc(start, length)))
        goto mem_error;
      p = start + offset;
    }
    null_byte_offset = p - start;
    memset(p, 0, null_count); //null bitmap
    p += null_count;
    int1store(p, stmt->send_types_to_server);
    p++;
    offset =  p - start;
    free_bytes = length - offset;
    /*
       Store type information:
       2 bytes per type
    */
    if (stmt->send_types_to_server)
    {
      if (free_bytes < stmt->param_count * 2 + 20 + extra_length)
      {
        length = offset + stmt->param_count * 2 + 20 + length + extra_length;
        if (!(start = (uchar*) realloc(start, length)))
          goto mem_error;
        p = start + offset;
      }
      for (i = 0; i < stmt->param_count; i++)
      {
        store_param_type(mysql, &p, &stmt->params[i]);
      }
    }
      /* calculate data size */
    for (i=0; i < stmt->param_count; i++)
    {
      size_t size= 0;
      my_bool has_data= TRUE;

      if (stmt->params[i].long_data_used)
      {
        has_data= FALSE;
        stmt->params[i].long_data_used= 0;
      }

      if (!stmt->params[i].buffer ||
            (stmt->params[i].is_null && *stmt->params[i].is_null))
      {
        has_data= FALSE;
      }

      if (has_data)
      {
        switch (stmt->params[i].buffer_type) {
        case MYSQL_TYPE_NULL:
          has_data= FALSE;
          break;
        case MYSQL_TYPE_TINY:
        case MYSQL_TYPE_SHORT:
        case MYSQL_TYPE_YEAR:
        case MYSQL_TYPE_INT24:
        case MYSQL_TYPE_LONG:
        case MYSQL_TYPE_LONGLONG:
        case MYSQL_TYPE_FLOAT:
        case MYSQL_TYPE_DOUBLE:
        case MYSQL_TYPE_CURSOR:
          /* type len has definitly defined in mysql_ps_fetch_functions */
          size+= mysql_ps_fetch_functions[stmt->params[i].buffer_type].pack_len;
          break;
        case MYSQL_TYPE_TINY_BLOB:
        case MYSQL_TYPE_MEDIUM_BLOB:
        case MYSQL_TYPE_LONG_BLOB:
        case MYSQL_TYPE_BLOB:
        case MYSQL_TYPE_VARCHAR:
        case MYSQL_TYPE_VAR_STRING:
        case MYSQL_TYPE_STRING:
        case MYSQL_TYPE_JSON:
        case MYSQL_TYPE_DECIMAL:
        case MYSQL_TYPE_NEWDECIMAL:
        case MYSQL_TYPE_GEOMETRY:
        case MYSQL_TYPE_NEWDATE:
        case MYSQL_TYPE_ENUM:
        case MYSQL_TYPE_BIT:
        case MYSQL_TYPE_SET:
        case MYSQL_TYPE_OB_RAW:
        case MYSQL_TYPE_OB_TIMESTAMP_NANO:
        case MYSQL_TYPE_OB_UROWID:
        case MYSQL_TYPE_OB_TIMESTAMP_WITH_LOCAL_TIME_ZONE:
        case MYSQL_TYPE_TIME:
        case MYSQL_TYPE_DATE:
        case MYSQL_TYPE_DATETIME:
        case MYSQL_TYPE_TIMESTAMP:
          size+= 5; /* max 8 bytes for size */
          size+= (size_t)ma_get_length(stmt, i, 0);
          break;
        //add to support oboracle type
        case MYSQL_TYPE_ORA_BLOB:
        case MYSQL_TYPE_ORA_CLOB:
        case MYSQL_TYPE_OB_TIMESTAMP_WITH_TIME_ZONE:
        case MYSQL_TYPE_OBJECT:
          size += (size_t) calculate_param_len(&stmt->params[i]);
          break;
        //end add to support oboracle type
        default:
          //size+= mysql_ps_fetch_functions[stmt->params[i].buffer_type].pack_len;
          size += (size_t)ma_get_length(stmt, i, 0);
          break;
        }
      }
      free_bytes= length - (p - start);
      if (free_bytes < size + 20)
      {
        size_t offset= p - start;
        length= MAX(2 * length, offset + size + 20);
        if (!(start= (uchar *)realloc(start, length)))
          goto mem_error;
        p= start + offset;
      }
      if (((stmt->params[i].is_null && *stmt->params[i].is_null) ||
             stmt->params[i].buffer_type == MYSQL_TYPE_NULL ||
             !stmt->params[i].buffer))
      {
        has_data= FALSE;
        /*Skip set NULL flat for piece_data_used when param->buffer is NULL. */
        if (!(!stmt->params[i].buffer && stmt->params[i].piece_data_used))
        {
          (start + null_byte_offset)[i/8] |= (unsigned char) (1 << (i & 7));
        }
      }

      if (has_data)
      {
        if (stmt->params[i].piece_data_used && MYSQL_TYPE_OBJECT == stmt->params[i].buffer_type)
        {
          /* Communicate with OBServer: send array lenth if it is ARRAY BINDING for piece data */
          MYSQL_COMPLEX_BIND_OBJECT *header = (MYSQL_COMPLEX_BIND_OBJECT *)stmt->params[i].buffer;
          if (NULL != header && MYSQL_TYPE_ARRAY == header->buffer_type)
          {
            store_param_piece_array_complex(&p, (MYSQL_COMPLEX_BIND_ARRAY *)header);
          }
        } else {
          store_param(stmt, i, &p, 0);
        }
      }
    }
  }

  free_bytes= length - (p - start);
  if (free_bytes < 16)      // 16 bytes in the end
  {
    size_t offset= p - start;
    length= offset + 16;
    if (!(start= (uchar *)realloc(start, length)))
      goto mem_error;
    p= start + offset;
  }

   //execute_mode
  int4store(p, stmt->execute_mode);
  p += 4;
  //num_close_stmt_count, now always is 0
  int4store(p, 0);
  p += 4;
  //check_sum
  if (stmt->check_sum == 0)
  {
    //maybe after describe,has stmt_id but no checksum
    stmt->check_sum = ob_crc32(0, query, query_length);
  }
  int4store(p, stmt->check_sum);
  p += 4;
   //extend_flag
  int4store(p, stmt->ext_flag);
  p += 4;
  stmt->send_types_to_server= 0;
  *request_len = (size_t)(p - start);
  return start;
mem_error:
  SET_CLIENT_STMT_ERROR(stmt, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
  free(start);
  *request_len= 0;
  return NULL;
}

static int madb_update_stmt_fields(MYSQL_STMT *stmt) // same as update_stmt_fields
{
  MYSQL_FIELD *field= stmt->mysql->fields;
  MYSQL_FIELD *field_end= field + stmt->field_count;
  MYSQL_FIELD *stmt_field= stmt->fields;

  MA_MEM_ROOT *fields_ma_alloc_root= &((MADB_STMT_EXTENSION *)stmt->extension)->fields_ma_alloc_root;

  if (stmt->field_count != stmt->mysql->field_count)
  {
    if (is_returning_result(stmt) && has_added_user_fields(stmt)) {
      /*
        The result field has changed due to that it executed prepared_stmt
        with iter 1 at first and execute prepared_stmt with iter >1 next,
        and the result has the flag PRE_EXE_EXTEND_FLAG_RETURNING/
        PRE_EXE_EXTEND_FLAG_ADD_USER_FIELD. eg.
        user and result fields' count is 5 in iter 1, user fields' count is 5
        and result fields' count is 8 in iter >1(sql_no/error_code/error_msg is added).
       */
      madb_alloc_stmt_fields(stmt); //need to remalloc it

      field_end= field + stmt->field_count;
      stmt_field= stmt->fields;
    } else {
      /*
        The tables used in the statement were altered,
        and the query now returns a different number of columns.
        There is no way to continue without reallocating the bind
        array:
        - if the number of columns increased, mysql_stmt_fetch()
        will write beyond allocated memory
        - if the number of columns decreased, some user-bound
        buffers will be left unassigned without user knowing
        that.
      */
      SET_CLIENT_STMT_ERROR(stmt, CR_NEW_STMT_METADATA, SQLSTATE_UNKNOWN, 0);
      return 1;
    }
  }

  for (; field < field_end; ++field, ++stmt_field)
  {
    memcpy(stmt_field, field, sizeof(MYSQL_FIELD));
/*
 * Not need to set one by one
    stmt_field->charsetnr= field->charsetnr;
    stmt_field->length   = field->length;
    stmt_field->type     = field->type;
    stmt_field->flags    = field->flags;
    stmt_field->decimals = field->decimals;
    stmt_field->max_length = field->max_length;
*/

    /* 判断 owner_name 是否为空, 避免每次都申请内存, 如果一直 execute, 不重新 prepare, 内存就释放不了 */
    if (MYSQL_TYPE_OBJECT == field->type && NULL == stmt_field->owner_name && NULL != field->owner_name) {
      stmt_field->owner_name= (unsigned char *)ma_memdup_root(fields_ma_alloc_root,
                                                              (char*)field->owner_name,
                                                              field->owner_name_length+1);
      stmt_field->owner_name[field->owner_name_length] = 0;
      stmt_field->type_name= (unsigned char *)ma_memdup_root(fields_ma_alloc_root,
                                                              (char*)field->type_name,
                                                              field->type_name_length+1);
      stmt_field->type_name[field->type_name_length] = 0;
    }
  }
  return 0;
}

static int madb_reinit_result_set_metadata(MYSQL_STMT *stmt)
{
  /* Server has sent result set metadata */
  if (stmt->field_count == 0)
  {
    /*
      This is 'SHOW'/'EXPLAIN'-like query. Current implementation of
      prepared statements can't send result set metadata for these queries
      on prepare stage. Read it now.
    */

    stmt->field_count= stmt->mysql->field_count;

    return madb_alloc_stmt_fields(stmt);
  }
  else
  {
    /*
      Update result set metadata if it for some reason changed between
      prepare and execute, i.e.:
      - in case of 'SELECT ?' we don't know column type unless data was
      supplied to mysql_stmt_execute, so updated column type is sent
      now.
      - if data dictionary changed between prepare and execute, for
      example a table used in the query was altered.
      Note, that now (4.1.3) we always send metadata in reply to
      COM_STMT_EXECUTE (even if it is not necessary), so either this or
      previous branch always works.
      TODO: send metadata only when it's really necessary and add a warning
      'Metadata changed' when it's sent twice.
    */
    return madb_update_stmt_fields(stmt);
  }

  return 0;
}

static int stmt_read_prepare_execute_response(MYSQL_STMT* stmt)
{
  MYSQL *mysql= stmt->mysql;
  NET *net;
  ulong pkt_len;

  if (!mysql)
    return(1);
  net= &mysql->net;
  pkt_len = 0;
  if ((pkt_len= ma_net_safe_read(mysql)) == packet_error)
    return(1);
  if (net->read_pos[0] != 0)
  {
    end_server(mysql);
    SET_CLIENT_ERROR(mysql, CR_MALFORMED_PACKET, SQLSTATE_UNKNOWN, 0);
    DBUG_RETURN(1);
  }
  else if (pkt_len < 17)
  {
    end_server(mysql);
    SET_CLIENT_ERROR(mysql, CR_MALFORMED_PACKET, SQLSTATE_UNKNOWN, 0);
    snprintf(net->last_error, MYSQL_ERRMSG_SIZE, "Wrong packet len:%lu", pkt_len);
    DBUG_RETURN(1);
  }
  else
  {
    uchar *pos = (uchar*) mysql->net.read_pos;
    uint field_count, param_count;
    uint extend_flag;
    my_bool has_result_set = FALSE;
    stmt->stmt_id= uint4korr(pos+1); pos+= 5;
    field_count=   uint2korr(pos);   pos+= 2;
    param_count=   uint2korr(pos);   pos+= 2;
    mysql->warning_count= uint2korr(pos+1); //skip reserved
    pos +=3;
    extend_flag= uint4korr(pos); //extend_flag
    /**
     * PRE_EXE_EXTEND_FLAG_RETURNING= 1;
     * PRE_EXE_EXTEND_FLAG_ADD_USER_FIELD= 1<<1;
     * PRE_EXE_EXTEND_FLAG_PLOUT= 1<<2;
     * */
    stmt->is_handle_returning_into = extend_flag & PRE_EXE_EXTEND_FLAG_RETURNING;
    stmt->has_added_user_fields = extend_flag & PRE_EXE_EXTEND_FLAG_ADD_USER_FIELD;
    stmt->is_pl_out_resultset = extend_flag & PRE_EXE_EXTEND_FLAG_PLOUT;
    pos +=4;
    has_result_set = (pos[0] == 0 ? FALSE: TRUE);
    pos +=1;
    if (stmt->stmt_id == 0)
    {
      end_server(mysql);
      SET_CLIENT_ERROR(mysql, CR_MALFORMED_PACKET, SQLSTATE_UNKNOWN, 0);
      snprintf(net->last_error, MYSQL_ERRMSG_SIZE, "Wrong packet invalid stmt_id:%lu", stmt->stmt_id);
      DBUG_RETURN(1);
    }
    if (param_count != 0)
    {
      if (stmt->is_handle_returning_into && stmt->field_count + stmt->param_count == param_count)
      {
        /* It needs to exec prepare operation for the sql  with piece data send and returning ... into, Like:
         *      UPDATE TABLE ta SET c1 = :v1, c2 =:v2, c3 = :v3 RETURNING c0 INTO :c0
         * IN STMT_PREPARE:
         *    returning stmt->param_count = 3, and field_count = 1
         * IN STMT_PREPARE_EXECUTE(V2):
         *    returning param_count = 4, and filed_count = 1/4(single is 1, and arraybinding is 4) 
         *       (decide by has_added_user_fields flag)
         * So we need update lastest param count for returning ... into, to read and parse all the  param meta
         * */
         stmt->param_count = param_count;
      }
      if (param_count != stmt->param_count)
      {
        end_server(mysql);
        SET_CLIENT_ERROR(mysql, CR_MALFORMED_PACKET, SQLSTATE_UNKNOWN, 0);
        snprintf(net->last_error, MYSQL_ERRMSG_SIZE, "Wrong packet: client_param_count:%d, resp_param_count:%d",
          stmt->param_count, param_count);
        DBUG_RETURN(1);
      }
      //prepare会返回参数信息，PL执行时也会返回，释放避免内存重复分配
      if (stmt->param_fields)
      {
        ma_free_root(&stmt->param_fields_mem_root, MYF(MY_KEEP_PREALLOC));
        stmt->param_fields = 0;
      }
      if (mysql->methods->db_stmt_get_param_metadata(stmt))
          DBUG_RETURN(1);
    }
    free_old_query(mysql);
    if (field_count != 0)
    {
      //stmt->field_count = mysql->field_count = (uint) field_count;
      mysql->field_count = (uint) field_count;
      if (mthd_my_read_prepare_execute_result(mysql))
      // if (mthd_my_read_query_result(stmt->mysql))
      {
        DBUG_RETURN(1);
      }
      ///** Not need to reinit fields for stmt, for it cannot update the MYSQL_RES's fileds which has returned before.
      // *  If need get last the fields info for fetch, it need open it and get newest MYSQL_RES.
      else if (madb_reinit_result_set_metadata(stmt))
      {
        DBUG_RETURN(1);
      }
      //*/
    }
    if (has_result_set) {
      if (mthd_stmt_read_all_rows(stmt))
      {
        DBUG_RETURN(1);
      }
      // stmt->data_cursor = stmt->result.data;
    }
    if ((pkt_len= ma_net_safe_read(mysql)) == packet_error)
      DBUG_RETURN(1);
    if (mysql->net.read_pos[0] == 0) {
      ma_read_ok_packet(mysql, &mysql->net.read_pos[1], pkt_len);//extra ok packet
      stmt->upsert_status.affected_rows= stmt->mysql->affected_rows;
    }
    else
    {
      end_server(mysql);
      SET_CLIENT_ERROR(mysql, CR_MALFORMED_PACKET, SQLSTATE_UNKNOWN, 0);
      DBUG_RETURN(1);
    }
  }
  DBUG_RETURN(0);
}
int STDCALL mysql_stmt_execute_v2(MYSQL_STMT *stmt,
                                  const char *query,
                                  unsigned long length,
                                  unsigned int iteration_count,
                                  int execute_mode,
                                  void* extend_arg)
{
  MYSQL *mysql= stmt->mysql;
  int ret = 0;
  char *request;
  size_t request_len= 0;
  int arg = 0;
  FLT_DECLARE;

  if (!mysql)
  {
    SET_CLIENT_STMT_ERROR(stmt, CR_SERVER_LOST, SQLSTATE_UNKNOWN, 0);
    DBUG_RETURN(1);
  }
  if (!mysql->can_use_prepare_execute) {
    DBUG_RETURN(mysql_stmt_execute(stmt));
  }
  if (!stmt->use_prepare_execute) {
    SET_CLIENT_STMT_ERROR(stmt, CR_NO_PREPARE_STMT, unknown_sqlstate, NULL);
    snprintf(stmt->last_error, MYSQL_ERRMSG_SIZE, "must use prepare_v2 first");
    DBUG_RETURN(1);
  }
 
  if (stmt->state < MYSQL_STMT_PREPARED)
  {
    SET_CLIENT_ERROR(mysql, CR_COMMANDS_OUT_OF_SYNC, SQLSTATE_UNKNOWN, 0);
    SET_CLIENT_STMT_ERROR(stmt, CR_COMMANDS_OUT_OF_SYNC, SQLSTATE_UNKNOWN, 0);
    return(1);
  }

  stmt->execute_mode = execute_mode;
  stmt->iteration_count = iteration_count;
  if (extend_arg != NULL)
  {
    arg = *(int*)extend_arg & NEED_DATA_AT_EXEC_FLAG;
  }
  if (execute_mode & 0x00000010 || arg)//describe only
  {
    DBUG_RETURN(mysql_stmt_execute_describe_only(stmt, query, length));
  }
  if (stmt->param_count && !stmt->bind_param_done)
  {
    SET_CLIENT_STMT_ERROR(stmt, CR_PARAMS_NOT_BOUND, SQLSTATE_UNKNOWN, 0);
    return(1);
  }
  if (stmt->state == MYSQL_STMT_WAITING_USE_OR_STORE)
  {
    stmt->default_rset_handler = _mysql_stmt_use_result;
    stmt->default_rset_handler(stmt);
  }
  if (stmt->state > MYSQL_STMT_WAITING_USE_OR_STORE && stmt->state < MYSQL_STMT_FETCH_DONE && !stmt->result.data)
  {
    if (!stmt->cursor_exists)
      do {
        stmt->mysql->methods->db_stmt_flush_unbuffered(stmt);
      } while(mysql_stmt_more_results(stmt));
    stmt->state= MYSQL_STMT_PREPARED;
    stmt->mysql->status= MYSQL_STATUS_READY;
  }

  /* clear data, in case mysql_stmt_store_result was called */
  if (stmt->result.data)
  {
    ma_free_root(&stmt->result.alloc, MYF(MY_KEEP_PREALLOC));
    stmt->result_cursor= stmt->result.data= 0;
  }
  /* CONC-344: set row count to zero */
  stmt->result.rows= 0;
  request= (char *)mysql_stmt_prepare_execute_generate_request(stmt,query, length, &request_len);

  if (!request)
    return 1;

  FLT_BEFORE_COMMAND(0, FLT_TAG_COMMAND_NAME, "\"mysql_stmt_execute_v2\"");

  if (0 == ret) {
    ret= stmt->mysql->methods->db_command(mysql,
                                          COM_STMT_PREPARE_EXECUTE,
                                          request, request_len, 1, stmt);
    if (request)
    {
      free(request);
      request = 0;
    }

    if (ret) {
      UPDATE_STMT_ERROR(stmt);
      ret = 1;
    } else  if (mysql->net.extension->multi_status > COM_MULTI_OFF) {
      ret = 0;
    } else {
      ret = (stmt_read_execute_response(stmt));
    }
  }

  // end trace
  FLT_AFTER_COMMAND;

  return ret;
}

my_bool read_piece_data_from_server(MYSQL_STMT* stmt, uint param_number,
  uchar* piece_type, uchar* is_null, ulong* piece_data_len, uchar** row)
{
  // int rc = 0;
  MYSQL *mysql= stmt->mysql;
  NET *net= &mysql->net;
  ulong len=0;

  UNUSED(param_number);

  if (net->pvio != 0)
    len=ma_net_safe_read(stmt->mysql); //done

  if (len == packet_error || len == 0)
  {
    end_server(mysql);
    return(1);
  }

  if (len < 10)
  {
    end_server(mysql);
    SET_CLIENT_STMT_ERROR(stmt, CR_MALFORMED_PACKET, SQLSTATE_UNKNOWN, 0);
    snprintf(stmt->last_error, MYSQL_ERRMSG_SIZE, "Wrong packet len:%lu", len);
    return(1);
  }
  else
  {
    uchar *pos = (uchar*) mysql->net.read_pos;
    *piece_type = pos[0];
    *is_null    = pos[1];
    *piece_data_len = uint8korr(pos + 2); pos += 10;
    *row = pos;
    //rc = stmt_fetch_column_row(stmt, row, param_number);
  }

  return(0);
}

my_bool STDCALL
mysql_stmt_read_piece_data(MYSQL_STMT *stmt, unsigned int param_number,
    unsigned short orientation, int scroll_offset, unsigned long data_len,
    unsigned char *piece_type, unsigned long *ret_data_len)
{
  uchar *row;
  MYSQL_BIND  *param;
  MYSQL_DATA  *result= &stmt->result;
  uchar is_null = 0;
  int truncation_count= 0;
  MYSQL *mysql;
  uchar buff[4 /* statement id */ +
             2 /* orientation */ +
             4 /* offset to be used change the current row position */ +
             2 /* column id*/ +
             8 /* piece data size*/];

  if (NULL == piece_type || NULL == ret_data_len) {
    SET_CLIENT_STMT_ERROR(stmt, CR_NULL_POINTER, unknown_sqlstate, NULL);
    return(1);
  }

  /*
    We only need to check for stmt->param_count, if it's not null
    prepare was done.
  */
  if (param_number >= stmt->field_count)
  {
    SET_CLIENT_STMT_ERROR(stmt, CR_INVALID_PARAMETER_NO, unknown_sqlstate, NULL);
    return(1);
  }

  param = stmt->bind + param_number;

  /*
    Send long data packet if there is data or we're sending long data
    for the first time.
  */

  mysql= stmt->mysql;
  /* Packet header: stmt id (4 bytes), param no (2 bytes) */
  ma_free_root(&result->alloc, MYF(MY_KEEP_PREALLOC));
  result->data= 0;
  result->rows= 0;
  memset(buff, 0, sizeof(buff));
  int4store(buff, stmt->stmt_id);
  int2store(buff + 4, orientation);
  int4store(buff + 6, scroll_offset);
  int2store(buff + 10, param_number);
  int8store(buff + 12, data_len);

  /*
    Note that we don't get any ok packet from the server in this case
    This is intentional to save bandwidth.
  */
  if ((stmt->mysql->methods->db_command)(mysql, COM_STMT_GET_PIECE_DATA, (char *)buff, sizeof(buff), 1, stmt))
  {
    /*
      Don't set stmt error if stmt->mysql is NULL, as the error in this case
      has already been set by mysql_prune_stmt_list().
    */
    if (stmt->mysql)
      UPDATE_STMT_ERROR(stmt);
    return(1);
  }

  if (read_piece_data_from_server(stmt, param_number, piece_type, &is_null, ret_data_len, &row))
    return(1);

  /* need change the state to MYSQL_STMT_USER_FETCHING, to read the buffer data */
  stmt->state= MYSQL_STMT_USER_FETCHING;
  CLEAR_CLIENT_ERROR(stmt->mysql);
  CLEAR_CLIENT_STMT_ERROR(stmt);

  if (is_null == 1) {
    *param->is_null = 1;
  } else {
    *param->is_null = 0;
    param->u.row_ptr = row;
    param->mysql = stmt->mysql;
    if (param->buffer_length <= 0) {
      *param->error = 1;
    } else {
      ulong copy_length = MIN(*ret_data_len, param->buffer_length);
      memcpy(param->buffer, (char *)*row, copy_length);
      *param->error = copy_length < *ret_data_len;
    }
    *param->length = *ret_data_len;
    truncation_count += *param->error;
  }
  if (truncation_count && stmt->mysql->options.report_data_truncation) //TODO need to check same as stmt->bind_result_done & REPORT_DATA_TRUNCATION
    return MYSQL_DATA_TRUNCATED;

  return(0);
}
