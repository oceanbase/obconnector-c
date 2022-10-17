#include <stdio.h>
#include <string.h>
#include "mysql.h"
#include "test_util.h"

#define check_error(mysql, status )  \
  check_error_internal(__FILE__, __LINE__, mysql, status)
int check_error_internal(const char* file_name, int line_no, MYSQL* mysql, int status ) {
  if (status != 0) {
    printf("[%s:%d] (error:%d, msg:%s)\n", file_name, line_no, mysql_errno(mysql), mysql_error(mysql));
  }
  return 0;
}
#define check_stmt_error(stmt, status) \
  check_stmt_error_internal(__FILE__, __LINE__, stmt, status)
int check_stmt_error_internal(const char* file_name, int line_no, MYSQL_STMT* stmt, int status) {
  if (status != 0) {
    printf("[%s:%d] (error:%d, msg:%s)\n",  file_name, line_no, mysql_stmt_errno(stmt), mysql_stmt_error(stmt));
  }
  return 0;
}
int stmt_insert_array_test(MYSQL* mysql)
{
  my_log("stmt_insert_array_test");
  int        status = 0;
  MYSQL_STMT *stmt;
  stmt = mysql_stmt_init(mysql);
  if (!stmt) {
      my_log("msyql_stmt_init failed: %s\n", mysql_error(mysql));
      ASSERT_EQ(-1, 0, "mysql_stmt_init");
      return -1;
  }
  MYSQL_BIND ps_params[3];
  int        int_data[3];
  my_bool    is_null[3];
  printf("===========testPlArrayIn===========\n");
  /* set up stored procedure */
  check_error(mysql, mysql_query(mysql, "DROP TABLE TEST_ARRAY"));
  check_error(mysql, mysql_query(mysql, "CREATE TABLE TEST_ARRAY (c1 int)"));
  check_error(mysql, mysql_query(mysql, "set autocommit = 0"));
  const char *insert_sql = "insert into TEST_ARRAY values(?)";
  check_stmt_error(stmt, mysql_stmt_prepare(stmt, insert_sql, strlen(insert_sql)));
  /* initialize parameters: p_in, p_out, p_inout (all INT) */
  memset(ps_params, 0, sizeof (ps_params));

  //MYSQL_COMPLEX_BIND_BASIC number_struct[3];
  MYSQL_COMPLEX_BIND_STRING number_struct[3];
  int number[3] = {2, 3, 4};
  char str[3][20];
  int i = 0;
  for (i = 0; i < 3; i++) {
    number_struct[i].buffer_type = MYSQL_TYPE_NEWDECIMAL;
    snprintf(str[i], 20, "%d", i * 10 + 3);
    number_struct[i].buffer = &str[i];
    number_struct[i].length = strlen(str[i]);
    number_struct[i].is_null = 0;
  }
  MYSQL_COMPLEX_BIND_ARRAY array_struct;
  array_struct.buffer_type = MYSQL_TYPE_ARRAY;
  // type_name null will use anonymous array
  // array_struct.type_name = "int_array";
  array_struct.type_name = NULL;
  array_struct.buffer = number_struct;
  array_struct.length = 3;
  array_struct.is_null = 0;

  ps_params[0].buffer_type = MYSQL_TYPE_OBJECT;
  ps_params[0].buffer = (char *) &array_struct;
  ps_params[0].buffer_length = sizeof(MYSQL_COMPLEX_BIND_ARRAY);
  ps_params[0].is_null = 0;

   /* bind parameters */
  check_stmt_error(stmt, status = mysql_stmt_bind_param(stmt, ps_params));

  /* assign values to parameters and execute statement */
  int_data[0] = 10; /* p_in */
  int_data[1] = 20; /* p_out */
  int_data[2] = 30; /* p_inout */

  unsigned long type;
  const int ARRAY_TYPE_BINDING = 0x08;
  const int BATCH_ERROR_MODE = 0x16;

  type = (unsigned long) ARRAY_TYPE_BINDING;
  stmt->flags = ARRAY_TYPE_BINDING;
  // check_stmt_error(stmt, status = mysql_stmt_attr_set(stmt, STMT_ATTR_CURSOR_TYPE, (void*) &type));
  check_stmt_error(stmt, status = mysql_stmt_execute(stmt));
  int affected_rows = mysql_stmt_affected_rows(stmt);
  ASSERT_EQ(affected_rows, 3, "mysql_stmt_affected_rows");
  stmt->flags = 0;
  check_stmt_error(stmt, mysql_stmt_prepare(stmt, "commit", strlen("commit")));
  check_stmt_error(stmt, mysql_stmt_execute(stmt));
  return status;
}
int stmt_select_test(MYSQL* mysql) {
  my_log("stmt_select_test");
  const char* pStatement = "select * from person";
  MYSQL_STMT    *stmt;
  MYSQL_BIND    bind[4];
  int           param_count;
  int filed_count;
  my_ulonglong affected_rows;
  stmt = mysql_stmt_init(mysql);
  if (!stmt) {
    printf("msyql_stmt_init failed: %s\n", mysql_error(mysql));
    return -1;
  }
  if (mysql_stmt_prepare_v2(stmt, pStatement, strlen(pStatement), 0)) {
    printf("mysql_stmt_prepare_v2 failed:%s\n", mysql_error(mysql));
    return -1;
  }
  filed_count = mysql_stmt_field_count(stmt);
  my_log("filed_count is %u", filed_count);
  param_count = mysql_stmt_param_count(stmt);
  my_log("param_count is %d", param_count);
  unsigned int iteration_count = 1;
  // mysql_stmt_attr_set(stmt, STMT_ATTR_ITERRATION_COUNT, &iteration_count);
  //stmt->iteration_count = 0;
  if (mysql_stmt_execute_v2(stmt, pStatement, strlen(pStatement), 0, 0, 0)) {
    my_log("mysql_stmt_execute_v2 select failed:%s", mysql_stmt_error(stmt));
    return -1;
  }
  // if (mysql_stmt_store_result(stmt)) {
  //   my_log("mysql_stmt_store_result failed :%s", mysql_stmt_error(stmt));
  //   return -1;
  // }
  filed_count = mysql_stmt_field_count(stmt);
  my_log("filed_count is %u", filed_count);
  my_ulonglong row_count = mysql_stmt_num_rows(stmt);
  my_log("row count is %u", row_count);
  memset(bind, 0, sizeof(bind));
  int id, num;
  char name[20];
  char birthday[20];
  bind[0].buffer_type = MYSQL_TYPE_LONG;
  bind[0].buffer = &id;
  bind[1].buffer_type = MYSQL_TYPE_LONG;
  bind[1].buffer = &num;
  bind[2].buffer_type = MYSQL_TYPE_STRING;
  bind[2].buffer = name;
  bind[2].buffer_length = sizeof(name);
  bind[3].buffer_type = MYSQL_TYPE_STRING;
  bind[3].buffer = birthday;
  bind[3].buffer_length = sizeof(birthday);

  if (mysql_stmt_bind_result(stmt, bind)) {
    printf("mysql_stmt_bind_result failed:%s", mysql_stmt_error(stmt));
    return -1;
  }
  row_count = 0;
  while (mysql_stmt_fetch(stmt) == 0) {
    printf("%d\t%d\t%s\t%s\n", id, num, name, birthday);
    row_count++;
  }
  ASSERT_EQ(row_count, 9, "row_count");
  mysql_stmt_close(stmt);

  return 0;
}
int main(int argc, char** argv) {
  mysql_library_init(0, NULL, NULL);
  MYSQL *mysql = mysql_init(NULL);
  unsigned int timeout = 3000;
  init_conn_info(OB_ORACLE_MODE);
  mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);

  if (mysql_real_connect(mysql, DBHOST, DBUSER, DBPASS, DBNAME, DBPORT, DBSOCK, DBPCNT) == NULL)
  {
    my_log("connect failed: %s, host:%s, port:%d, user:%s, pass:%s, dbname:%s",
      mysql_error(mysql),DBHOST, DBPORT, DBUSER, DBPASS, DBNAME);
    mysql_close(mysql);
    mysql_library_end();
    return 0;
  } else {
    my_log("connect %s:%d using %s succ", DBHOST, DBPORT, DBUSER);
  }
  int ret = mysql_query(mysql, "SET NAMES utf8");
  if (ret != 0)
  {
    printf("set names error, %s\n", mysql_error(mysql));
    return ret;
  }
  mysql_query(mysql, "drop table person");
  ret = mysql_query(mysql, "create table person(id int, num int, name varchar2(20), birthday varchar2(20))");
  if (ret != 0)
  {
    my_log("create table error, %s\n", mysql_error(mysql));
    return ret;
  }
  stmt_insert_array_test(mysql);
  // stmt_select_test(mysql);
  return 0;
}