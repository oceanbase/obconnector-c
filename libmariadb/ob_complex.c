/*
   Copyright (c) 2000, 2018, Oracle and/or its affiliates.
   Copyright (c) 2009, 2019, MariaDB Corporation.
   Copyright (c) 2021 OceanBase Technology Co.,Ltd.
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335 USA */
#include "ma_common.h"
#include "errmsg.h"
#include "ob_complex.h"
#include "ma_string.h"

static OB_HASH *global_hash = NULL;
static ob_rw_lock_t global_rwlock = OB_RW_INITIALIZER;
static void global_hash_free(void *record);
// #define UNUSED(x) ((void)x)
#define COMPLEX_TYPE_MAX_SQL_LENGTH 32 * 1024
#define COMPLEX_TYPE_MYSQL(M)   (MYSQL_EXTENSION_PTR(M)->mysql)
#define COMPLEX_TYPE_HASH(M)   (MYSQL_EXTENSION_PTR(M)->complex_type_hash)
#define COMPLEX_TYPE_USER_TYPES_SQL \
"SELECT "\
"  0 DEPTH, "\
"  NULL PARENT_OWNER, "\
"  NULL PARENT_TYPE, "\
"  TYPE_NAME CHILD_TYPE, "\
"  0 ATTR_NO, "\
"  SYS_CONTEXT('USERENV', 'CURRENT_USER') CHILD_TYPE_OWNER, "\
"  A.TYPECODE ATTR_TYPE_CODE, "\
"  NULL LENGTH, "\
"  NULL NUMBER_PRECISION, "\
"  NULL SCALE, "\
"  NULL CHARACTER_SET_NAME "\
"FROM "\
"  USER_TYPES A WHERE TYPE_NAME = '%s' "\
"UNION "\
"( "\
"  WITH  "\
"  CTE_RESULT(PARENT_OWNER, PARENT_TYPE, CHILD_TYPE, ATTR_NO, CHILD_TYPE_OWNER, ATTR_TYPE_CODE, LENGTH, NUMBER_PRECISION, SCALE, CHARACTER_SET_NAME)  "\
"  AS ( "\
"    SELECT "\
"      SYS_CONTEXT('USERENV','CURRENT_USER') PARENT_OWNER, "\
"      B.TYPE_NAME PARENT_TYPE, "\
"      B.ELEM_TYPE_NAME CHILD_TYPE, "\
"      0 ATTR_NO, "\
"      B.ELEM_TYPE_OWNER CHILD_TYPE_OWNER, "\
"      NVL(A.TYPECODE, B.ELEM_TYPE_NAME) AS ATTR_TYPE_CODE, "\
"      B.LENGTH LENGTH, "\
"      B.NUMBER_PRECISION NUMBER_PRECISION, "\
"      B.SCALE SCALE, "\
"      B.CHARACTER_SET_NAME CHARACTER_SET_NAME "\
"    FROM "\
"      USER_COLL_TYPES B LEFT JOIN USER_TYPES A ON A.TYPE_NAME = B.ELEM_TYPE_NAME "\
"    UNION "\
"    SELECT "\
"      SYS_CONTEXT('USERENV','CURRENT_USER') PARENT_OWNER, "\
"      B.TYPE_NAME PARENT_TYPE, "\
"      B.ATTR_TYPE_NAME CHILD_TYPE, "\
"      B.ATTR_NO ATTR_NO, "\
"      B.ATTR_TYPE_OWNER CHILD_TYPE_OWNER, "\
"      NVL(A.TYPECODE, B.ATTR_TYPE_NAME) AS ATTR_TYPE_CODE, "\
"      B.LENGTH LENGTH, "\
"      B.NUMBER_PRECISION NUMBER_PRECISION, "\
"      B.SCALE SCALE, "\
"      B.CHARACTER_SET_NAME CHARACTER_SET_NAME "\
"    FROM USER_TYPE_ATTRS B LEFT JOIN USER_TYPES A ON B.ATTR_TYPE_NAME = A.TYPE_NAME ORDER BY ATTR_NO "\
"  ) , "\
"  CTE(DEPTH, PARENT_OWNER, PARENT_TYPE, CHILD_TYPE, ATTR_NO, CHILD_TYPE_OWNER, ATTR_TYPE_CODE, LENGTH, NUMBER_PRECISION, SCALE, CHARACTER_SET_NAME) "\
"  AS ( "\
"    SELECT "\
"      1 DEPTH, "\
"      PARENT_OWNER, "\
"      PARENT_TYPE, "\
"      CHILD_TYPE, "\
"      ATTR_NO, "\
"      CHILD_TYPE_OWNER, "\
"      ATTR_TYPE_CODE, "\
"      LENGTH, "\
"      NUMBER_PRECISION, "\
"      SCALE, CHARACTER_SET_NAME "\
"    FROM CTE_RESULT WHERE PARENT_TYPE = '%s' "\
"    UNION ALL "\
"    SELECT "\
"      DEPTH + 1 DEPTH, "\
"      CTE_RESULT.PARENT_OWNER, "\
"      CTE_RESULT.PARENT_TYPE, "\
"      CTE_RESULT.CHILD_TYPE, "\
"      CTE_RESULT.ATTR_NO, "\
"      CTE_RESULT.CHILD_TYPE_OWNER, "\
"      CTE_RESULT.ATTR_TYPE_CODE, "\
"      CTE_RESULT.LENGTH, "\
"      CTE_RESULT.NUMBER_PRECISION, "\
"      CTE_RESULT.SCALE, "\
"      CTE_RESULT.CHARACTER_SET_NAME "\
"    FROM CTE_RESULT INNER JOIN CTE ON CTE_RESULT.PARENT_TYPE = CTE.CHILD_TYPE "\
"  ) "\
"  SELECT * FROM CTE "\
"); "\

#define COMPLEX_TYPE_ALL_TYPES_SQL \
"SELECT "\
"    0 DEPTH, "\
"    NULL PRAENT_OWNER, "\
"    NULL PARENT_TYPE, "\
"    to_char(TYPE_NAME) CHILD_TYPE, "\
"    0 ATTR_NO, "\
"    OWNER CHILD_TYPE_OWNER, "\
"    A.TYPECODE ATTR_TYPE_CODE, "\
"    NULL LENGTH, "\
"    NULL NUMBER_PRECISION, "\
"    NULL SCALE, "\
"    NULL CHARACTER_SET_NAME "\
"  FROM "\
"    ALL_TYPES A WHERE TYPE_NAME = '%s' AND OWNER = '%s' "\
"  UNION "\
"  ( "\
"  WITH "\
"  CTE_RESULT(PARENT_OWNER, PARENT_TYPE, CHILD_TYPE, ATTR_NO, CHILD_TYPE_OWNER, ATTR_TYPE_CODE, LENGTH, NUMBER_PRECISION, SCALE, CHARACTER_SET_NAME) "\
"  AS ( "\
"      SELECT "\
"        B.OWNER PARENT_OWNER, "\
"        B.TYPE_NAME PARENT_TYPE, "\
"        B.ELEM_TYPE_NAME CHILD_TYPE, "\
"        0 ATTR_NO, "\
"        B.ELEM_TYPE_OWNER CHILD_TYPE_OWNER, "\
"        NVL(A.TYPECODE, B.ELEM_TYPE_NAME) AS ATTR_TYPE_CODE, "\
"        B.LENGTH LENGTH, "\
"        B.NUMBER_PRECISION NUMBER_PRECISION, "\
"        B.SCALE SCALE, "\
"        B.CHARACTER_SET_NAME CHARACTER_SET_NAME "\
"      FROM "\
"        ALL_COLL_TYPES B LEFT JOIN ALL_TYPES A ON A.TYPE_NAME = B.ELEM_TYPE_NAME AND A.OWNER = B.ELEM_TYPE_OWNER "\
"      UNION "\
"      SELECT "\
"        B.OWNER PARENT_OWNER, "\
"        B.TYPE_NAME PARENT_TYPE, "\
"        B.ATTR_TYPE_NAME CHILD_TYPE, "\
"        B.ATTR_NO ATTR_NO, "\
"        B.ATTR_TYPE_OWNER CHILD_TYPE_OWNER, "\
"        NVL(A.TYPECODE, B.ATTR_TYPE_NAME) AS ATTR_TYPE_CODE, "\
"        B.LENGTH LENGTH, "\
"        B.NUMBER_PRECISION NUMBER_PRECISION, "\
"        B.SCALE SCALE, "\
"        B.CHARACTER_SET_NAME CHARACTER_SET_NAME "\
"      FROM ALL_TYPE_ATTRS B LEFT JOIN ALL_TYPES A ON A.TYPE_NAME = B.ATTR_TYPE_NAME AND A.OWNER = B.ATTR_TYPE_OWNER ORDER BY ATTR_NO "\
"  ) , "\
"  CTE(DEPTH, PARENT_OWNER, PARENT_TYPE, CHILD_TYPE, ATTR_NO, CHILD_TYPE_OWNER, ATTR_TYPE_CODE, LENGTH, NUMBER_PRECISION, SCALE, CHARACTER_SET_NAME) "\
"  AS ( "\
"    SELECT "\
"      1 DEPTH, "\
"      PARENT_OWNER, "\
"      PARENT_TYPE, "\
"      CHILD_TYPE, "\
"      ATTR_NO, "\
"      CHILD_TYPE_OWNER, "\
"      ATTR_TYPE_CODE, "\
"      LENGTH, "\
"      NUMBER_PRECISION, "\
"      SCALE, CHARACTER_SET_NAME "\
"    FROM CTE_RESULT WHERE PARENT_TYPE = '%s' AND PARENT_OWNER = '%s' "\
"    UNION ALL "\
"    SELECT "\
"      DEPTH + 1 DEPTH, "\
"      CTE_RESULT.PARENT_OWNER, "\
"      CTE_RESULT.PARENT_TYPE, "\
"      CTE_RESULT.CHILD_TYPE, "\
"      CTE_RESULT.ATTR_NO, "\
"      CTE_RESULT.CHILD_TYPE_OWNER, "\
"      CTE_RESULT.ATTR_TYPE_CODE, "\
"      CTE_RESULT.LENGTH, "\
"      CTE_RESULT.NUMBER_PRECISION, "\
"      CTE_RESULT.SCALE, "\
"      CTE_RESULT.CHARACTER_SET_NAME "\
"    FROM CTE_RESULT INNER JOIN CTE ON CTE_RESULT.PARENT_TYPE = CTE.CHILD_TYPE AND CTE_RESULT.PARENT_OWNER = CTE.CHILD_TYPE_OWNER "\
"  ) "\
"  SELECT * FROM CTE "\
"); "\

#define DEPTH_INDEX 0
#define PARENT_TYPE_INDEX 2
#define CHILD_TYPE_INDEX 3
#define ATTR_NO_INDEX 4
#define CHILD_OWNER_INDEX 5
#define ATTR_TYPE_INDEX 6

static enum_types convert_type(const char *type) {
  if (!strcmp(type, "COLLECTION")) {
    return TYPE_COLLECTION;
  } else if (!strcmp(type, "OBJECT")) {
    return TYPE_OBJECT;
  } else if (!strcmp(type, "NUMBER")) {
    return TYPE_NUMBER;
  } else if (!strcmp(type, "VARCHAR2")) {
    return TYPE_VARCHAR2;
  } else if (!strcmp(type, "CHAR")) {
    return TYPE_CHAR;
  } else if (!strcmp(type, "DATE")) {
    return TYPE_DATE;
  } else if (!strcmp(type, "RAW")) {
    return TYPE_RAW;
  } else {
    return TYPE_MAX;
  }
}

static int STDCALL
parser_complex(MYSQL_RES *result, OB_HASH *hash)
{
  MYSQL_ROW cur; 

  COMPLEX_TYPE *complex_type;
  unsigned char *parent_name;
  COMPLEX_TYPE *parent;
  while ((cur = mysql_fetch_row(result))) {
    unsigned int type_size;
    enum_types type = convert_type(cur[ATTR_TYPE_INDEX]);

    if (TYPE_OBJECT == type || TYPE_COLLECTION == type) {
      complex_type = (COMPLEX_TYPE *) hash_search(hash, (unsigned char*)cur[CHILD_TYPE_INDEX], strlen(cur[CHILD_TYPE_INDEX]));

      if (!complex_type) {
        type_size = TYPE_COLLECTION == type ? sizeof(COMPLEX_TYPE_COLLECTION) : sizeof(COMPLEX_TYPE_OBJECT);
        complex_type = (COMPLEX_TYPE *) calloc(1, type_size);

        if (!complex_type) {
          return (1);
        }

        complex_type->is_valid = 0;
        complex_type->type = type;
        if (TYPE_OBJECT == complex_type->type) {
          ((COMPLEX_TYPE_OBJECT *)complex_type)->attr_no = 0;
        }
        /* name 被初始化成了 0, 所以不需要拷贝最后的 NULL 结束符 */
        memcpy(complex_type->type_name, cur[CHILD_TYPE_INDEX], strlen(cur[CHILD_TYPE_INDEX]));
        memcpy(complex_type->owner_name, cur[CHILD_OWNER_INDEX], strlen(cur[CHILD_OWNER_INDEX]));

        if (hash_insert(hash, (unsigned char *)complex_type)) {
          return (1);
        }
      } else {
        //ReInit node's info in the query
        complex_type->is_valid = 0;
        if (TYPE_OBJECT == complex_type->type) {
          ((COMPLEX_TYPE_OBJECT *)complex_type)->attr_no = 0;
        }
      }
    }

    if (atoi(cur[DEPTH_INDEX]) != 0) {
      parent_name = (unsigned char *)cur[PARENT_TYPE_INDEX];
      parent = (COMPLEX_TYPE *) hash_search(hash, parent_name, strlen((char *)parent_name));

      if (parent == NULL) {
        return (1);
      }

      if (TYPE_OBJECT == parent->type &&
             ((COMPLEX_TYPE_OBJECT *)parent)->attr_no < strtoul(cur[ATTR_NO_INDEX], NULL, 10)) {
        ((COMPLEX_TYPE_OBJECT *)parent)->attr_no = strtoul(cur[ATTR_NO_INDEX], NULL, 10);
      }
    }
  }

  result->data_cursor = result->data->data;

  while ((cur = mysql_fetch_row(result))) {
    enum_types child_type;
    COMPLEX_TYPE_OBJECT *parent_object;
    COMPLEX_TYPE_COLLECTION *parent_collection;

    if (atoi(cur[DEPTH_INDEX]) != 0) {
      parent_name = (unsigned char *)cur[PARENT_TYPE_INDEX];
      parent = (COMPLEX_TYPE *) hash_search(hash, parent_name, strlen((char *)parent_name));

      if (parent == NULL) {
        return (1);
      }

      child_type = convert_type(cur[ATTR_TYPE_INDEX]);

      if (TYPE_OBJECT == parent->type) {
        parent_object = (COMPLEX_TYPE_OBJECT *)parent;

        if (parent_object->child == NULL) {
          parent_object->child = (CHILD_TYPE *) calloc(1, parent_object->attr_no * sizeof(CHILD_TYPE));

          if (parent_object->child == NULL) {
            return (1);
          }
        }

        parent_object->child[atoi(cur[ATTR_NO_INDEX]) - 1].type = child_type;

        if (TYPE_COLLECTION == child_type || TYPE_OBJECT == child_type) {
          complex_type = (COMPLEX_TYPE *)hash_search(hash,
                  (unsigned char *)cur[CHILD_TYPE_INDEX], strlen(cur[CHILD_TYPE_INDEX]));

          if (complex_type == NULL) {
            return (1);
          }

          parent_object->child[atoi(cur[ATTR_NO_INDEX]) - 1].object = complex_type;
        }

        parent_object->init_attr_no++;

        if (parent_object->init_attr_no == parent_object->attr_no) {
          parent->is_valid = 1;
        }

      } else if (TYPE_COLLECTION == parent->type) {
        parent_collection = (COMPLEX_TYPE_COLLECTION *)parent;

        parent_collection->child.type = child_type;

        if (TYPE_COLLECTION == child_type || TYPE_OBJECT == child_type) {
          complex_type = (COMPLEX_TYPE *)hash_search(hash,
                  (unsigned char *)cur[CHILD_TYPE_INDEX], strlen(cur[CHILD_TYPE_INDEX]));

          if (complex_type == NULL) {
            return (1);
          }

          parent_collection->child.object = complex_type;
        }

        parent->is_valid = 1;
      }
    }
  }
  return (0);
}

static int STDCALL
get_type_from_server(MYSQL *mysql, unsigned char *owner_name, unsigned char *type_name, COMPLEX_HASH *complex_hash)
{
  int retval;
  MYSQL_RES *result;
  char sql[COMPLEX_TYPE_MAX_SQL_LENGTH] = {'\0'};

  if (NULL != owner_name) {
    snprintf(sql, COMPLEX_TYPE_MAX_SQL_LENGTH, COMPLEX_TYPE_ALL_TYPES_SQL, type_name, owner_name, type_name, owner_name);
  } else {
    /* 如果 owner 为空, 先从 user_types 视图中获取 */
    snprintf(sql, COMPLEX_TYPE_MAX_SQL_LENGTH, COMPLEX_TYPE_USER_TYPES_SQL, type_name, type_name);
  }

  if (mysql_real_query(mysql, sql, strlen(sql))) {
    return (1);
  }

  if (!(result = mysql_store_result(mysql))) {
    return (1);
  /* 如果 user_types 视图中没有该 type, 在再从 all_types 视图中获取 */
  } else if (0 == mysql_num_rows(result) && NULL == owner_name) {
    mysql_free_result(result);
    snprintf(sql, COMPLEX_TYPE_MAX_SQL_LENGTH, COMPLEX_TYPE_ALL_TYPES_SQL, type_name, "SYS", type_name, "SYS");

    if (mysql_real_query(mysql, sql, strlen(sql))) {
      return (1);
    }

    if (!(result = mysql_store_result(mysql))) {
      return (1);
    }
  }

  ob_rw_wrlock(&(complex_hash->rwlock));
  retval = parser_complex(result, complex_hash->hash);
  ob_rw_unlock(&(complex_hash->rwlock));

  mysql_free_result(result);

  return (retval);
}

static int STDCALL
complex_mysql_init(MYSQL *mysql, MYSQL **complex_mysql)
{
  int ret = 0;

  *complex_mysql = NULL;

  if (!(*complex_mysql = mysql_init(*complex_mysql))) {
    ret = 1;
  } else if (!mysql_real_connect(*complex_mysql, mysql->host, mysql->user, mysql->passwd,
              mysql->db, mysql->port, mysql->unix_socket, 0))
  {
    ret = 1;
  }

  if (ret != 0 && *complex_mysql != NULL) {
    mysql_close(*complex_mysql);
    *complex_mysql = NULL;
  }

  return (ret);
}

static void complex_hash_free(void *record)
{
  COMPLEX_TYPE *header = (COMPLEX_TYPE *)record;
  if (header->type == TYPE_OBJECT) {
    free(((COMPLEX_TYPE_OBJECT *)header)->child);
  }

  free(header);
}

static unsigned char *complex_get_hash_key(const unsigned char *record, uint *length,
             my_bool not_used)
{
  COMPLEX_TYPE *type = (COMPLEX_TYPE *)record;
  *length= strlen((char *)type->type_name);
  UNUSED(not_used);
  return type->type_name;
}

static int STDCALL
complex_hash_init(MYSQL *mysql, unsigned char *hash_key, COMPLEX_HASH **complex_hash)
{
  int ret = 0;

  *complex_hash = NULL;

  if (!(*complex_hash = calloc(1, sizeof(COMPLEX_HASH)))) {
    SET_CLIENT_ERROR(mysql, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
    ret = 1;
  } else if(ob_rw_init(&((*complex_hash)->rwlock))) {
    SET_CLIENT_ERROR(mysql, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
    ret = 1;
  } else if (!((*complex_hash)->hash = calloc(1, sizeof(OB_HASH)))) {
    SET_CLIENT_ERROR(mysql, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
    ret = 1;
  } else if (hash_init((*complex_hash)->hash, 0, 0, 0, (hash_get_key) complex_get_hash_key,
                          complex_hash_free, HASH_CASE_INSENSITIVE)) {
    SET_CLIENT_ERROR(mysql, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
    ret = 1;
  } else if (!((*complex_hash)->hash_key = calloc(1, strlen((char *)hash_key) + 1))) {
    SET_CLIENT_ERROR(mysql, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
    ret = 1;
  } else {
    //memset((*complex_hash)->hash_key, 0, strlen((char *)hash_key) + 1);
    memcpy((*complex_hash)->hash_key, hash_key, strlen((char *)hash_key));
    (*complex_hash)->hash_key[strlen((char *)hash_key)] = '\0';
    if (hash_insert(global_hash, (unsigned char *)(*complex_hash))) {
      SET_CLIENT_ERROR(mysql, CR_UNKNOWN_ERROR, SQLSTATE_UNKNOWN, 0);
      ret = 1;
    }
  }

  if (ret && *complex_hash != NULL) {
    global_hash_free((void *)(*complex_hash));
    *complex_hash = NULL;
  }

  return (ret);
}

static void global_hash_free(void *record)
{
  COMPLEX_HASH *complex_hash = (COMPLEX_HASH *)record;
  if (NULL != complex_hash->hash_key) {
    free(complex_hash->hash_key);
  }

  if (NULL != complex_hash->hash) {
    hash_free(complex_hash->hash);
    free(complex_hash->hash);
  }

  ob_rw_destroy(&(complex_hash->rwlock));
  free(complex_hash);
}

static unsigned char *global_get_hash_key(const unsigned char *record, uint *length,
             my_bool not_used)
{
  COMPLEX_HASH *complex_hash = (COMPLEX_HASH *)record;
  *length= strlen((char *)complex_hash->hash_key);
  UNUSED(not_used);
  return complex_hash->hash_key;
}

static int STDCALL
global_hash_init(MYSQL *mysql)
{
  if (!(global_hash = calloc(1, sizeof(OB_HASH)))) {
    SET_CLIENT_ERROR(mysql, CR_OUT_OF_MEMORY, SQLSTATE_UNKNOWN, 0);
    return 1;
  } else if (hash_init(global_hash, 0, 0, 0, (hash_get_key) global_get_hash_key,
             global_hash_free, HASH_CASE_INSENSITIVE)) {
    hash_free(global_hash);
    free(global_hash);
    SET_CLIENT_ERROR(mysql, CR_UNKNOWN_ERROR, SQLSTATE_UNKNOWN, 0);
    return 1;
  }

  return 0;
}

static const char digits[]= "0123456789abcdef";

char *ma_safe_utoa(int base, ulonglong val, char *buf)
{
  *buf--= 0;
  do {
    *buf--= digits[val % base];
  } while ((val /= base) != 0);
  return buf + 1;
}
static int STDCALL
get_complex_hash(MYSQL *mysql, unsigned char *hash_key, int buffer_len, COMPLEX_HASH **complex_hash)
{
  int ret = 0;

  *complex_hash = NULL;

  if (NULL == global_hash) {
    ob_rw_wrlock(&global_rwlock);
    if (NULL == global_hash && global_hash_init(mysql)) {
      ret = 1;
    }
    ob_rw_unlock(&global_rwlock);
  }
  if (ret == 0) {
    unsigned int hash_length = 0;
    char port[10] = {'\0'};
    char *port_ptr = NULL;
    port_ptr = ma_safe_utoa(10, mysql->port, &port[sizeof(port)-1]);
    if (strlen(mysql->host) + strlen(port_ptr) + strlen(mysql->user) + 1 >= (size_t)buffer_len) {
      ret = 1;
    } else if ((NULL != mysql->db) && (strlen(mysql->host) + strlen(port_ptr) + strlen(mysql->user) +
      strlen(mysql->db) + 1 >= (size_t)buffer_len)) {
      ret = 1;
    } else {
      memcpy(hash_key + hash_length, mysql->host, strlen(mysql->host));
      hash_length += strlen(mysql->host);

      memcpy(hash_key + hash_length, port_ptr, strlen(port_ptr));
      hash_length += strlen(port_ptr);

      memcpy(hash_key + hash_length, mysql->user, strlen(mysql->user));
      hash_length += strlen(mysql->user);
      if (NULL != mysql->db) {
        memcpy(hash_key + hash_length, mysql->db, strlen(mysql->db));
        hash_length += strlen(mysql->db);
      }
      hash_key[hash_length] = '\0';
      ob_rw_rdlock(&global_rwlock);
      *complex_hash = (COMPLEX_HASH *)hash_search(global_hash, hash_key, strlen((char *)hash_key));
      ob_rw_unlock(&global_rwlock);

      if (NULL == *complex_hash) {
        ob_rw_wrlock(&global_rwlock);
        if (NULL == *complex_hash) {
          if (complex_hash_init(mysql, hash_key, complex_hash)) {
            ret = 1;
          }
        }
        ob_rw_unlock(&global_rwlock);
      }
    }
  }

  return (ret);
}

COMPLEX_TYPE* STDCALL
get_complex_type(MYSQL *mysql, unsigned char *owner_name, unsigned char *type_name)
{
  COMPLEX_TYPE *type = NULL;
  COMPLEX_HASH *complex_hash = NULL;
  MYSQL *complex_mysql = NULL;
  unsigned char hash_key[200] = {'\0'};

  if (get_complex_hash(mysql, hash_key, 200, &complex_hash)) {
  } else {
    ob_rw_rdlock(&(complex_hash->rwlock));
    type = (COMPLEX_TYPE *)hash_search(complex_hash->hash, type_name, strlen((char *)type_name));
    ob_rw_unlock(&(complex_hash->rwlock));

    if (NULL == type || type->is_valid != 1) {
      if (complex_mysql_init(mysql, &complex_mysql)) {
      } else if (get_type_from_server(complex_mysql, owner_name, type_name, complex_hash)) {
      } else {
        ob_rw_rdlock(&(complex_hash->rwlock));
        type = (COMPLEX_TYPE *)hash_search(complex_hash->hash, type_name, strlen((char *)type_name));
        ob_rw_unlock(&(complex_hash->rwlock));
      }
    }
  }

  if (complex_mysql != NULL) {
    mysql_close(complex_mysql);
    complex_mysql = NULL;
  }

  if (type && type->is_valid != 1) {
    return (NULL);
  }

  return (type);
}

COMPLEX_TYPE* STDCALL
get_complex_type_with_local(MYSQL *mysql, unsigned char *type_name)
{
  COMPLEX_TYPE *type = NULL;
  COMPLEX_HASH *complex_hash = NULL;
  unsigned char hash_key[200] = {'\0'};
  memset(hash_key, 0, 200);

  if (get_complex_hash(mysql, hash_key, 200, &complex_hash)) {
  } else {
    ob_rw_rdlock(&(complex_hash->rwlock));
    type = (COMPLEX_TYPE *)hash_search(complex_hash->hash, type_name, strlen((char *)type_name));
    ob_rw_unlock(&(complex_hash->rwlock));
  }

  if (type && type->is_valid != 1) {
    return (NULL);
  }

  return (type);
}
