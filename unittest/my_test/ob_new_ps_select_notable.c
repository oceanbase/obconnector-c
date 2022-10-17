#include <stdio.h>
#include <string.h>
#include "mysql.h"
#include "test_util.h"


int stmt_select_test_not_exist_table(MYSQL* mysql) {
  my_log("stmt_select_test_not_exist_table");
  const char* pStatement = "select * from test_new_ps";
  MYSQL_STMT    *stmt;
  MYSQL_BIND    bind[4];
  int           ret = 0;
  int           param_count;
  int           filed_count;
  my_ulonglong  affected_rows;
  stmt = mysql_stmt_init(mysql);
  if (!stmt) {
    my_log("msyql_stmt_init failed: %s", mysql_error(mysql));
    ASSERT_EQ(-1, 0, "mysql_stmt_init");
    return -1;
  }
  if (ret = mysql_stmt_prepare_v2(stmt, pStatement, strlen(pStatement), 0)) {
    my_log("mysql_stmt_prepare_v2 failed:%s", mysql_error(mysql));
    ASSERT_EQ(ret, 0, "mysql_stmt_prepare_v2");
  }
  filed_count = mysql_stmt_field_count(stmt);
  my_log("filed_count is %u", filed_count);
  param_count = mysql_stmt_param_count(stmt);
  my_log("param_count is %d", param_count);
  unsigned int iteration_count = 1;
  // mysql_stmt_attr_set(stmt, STMT_ATTR_ITERRATION_COUNT, &iteration_count);
  //stmt->iteration_count = 0;
  if (ret = mysql_stmt_execute_v2(stmt, pStatement, strlen(pStatement), iteration_count, 0, 0)) {
    my_log("mysql_stmt_execute_v2 select failed:%s", mysql_stmt_error(stmt));
    ASSERT_EQ(1, ret, "mysql_stmt_execute_v2");
  } else {
    ASSERT_EQ(1, ret, "mysql_stmt_execute_v2");
  }
  return 0;
}
int stmt_select_test_no_table(MYSQL* mysql) {
  my_log("stmt_select_test_no_table");
  const char* pStatement = "select c1,c2,c3,c4";
  MYSQL_STMT    *stmt;
  MYSQL_BIND    bind[4];
  int           ret = 0;
  int           param_count;
  int           filed_count;
  my_ulonglong  affected_rows;
  stmt = mysql_stmt_init(mysql);
  if (!stmt) {
    my_log("msyql_stmt_init failed: %s", mysql_error(mysql));
    ASSERT_EQ(-1, 0, "mysql_stmt_init");
    return -1;
  }
  if (ret = mysql_stmt_prepare_v2(stmt, pStatement, strlen(pStatement), 0)) {
    my_log("mysql_stmt_prepare_v2 failed:%s", mysql_error(mysql));
    ASSERT_EQ(ret, 0, "mysql_stmt_prepare_v2");
  }
  filed_count = mysql_stmt_field_count(stmt);
  my_log("filed_count is %u", filed_count);
  param_count = mysql_stmt_param_count(stmt);
  my_log("param_count is %d", param_count);
  unsigned int iteration_count = 1;
  // mysql_stmt_attr_set(stmt, STMT_ATTR_ITERRATION_COUNT, &iteration_count);
  //stmt->iteration_count = 0;
  if (ret = mysql_stmt_execute_v2(stmt, pStatement, strlen(pStatement), iteration_count, 0, 0)) {
    my_log("mysql_stmt_execute_v2 select failed:%s", mysql_stmt_error(stmt));
    ASSERT_EQ(1, ret, "mysql_stmt_execute_v2");
  } else {
    ASSERT_EQ(1, ret, "mysql_stmt_init");
  }
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
    my_log("connect failed: %s\n", mysql_error(mysql));
    mysql_close(mysql);
    mysql_library_end();
    return 0;
  } else {
    my_log("connect to %s:%d using %s success", DBHOST, DBPORT, DBUSER);
  }
  int ret = mysql_query(mysql, "SET NAMES utf8");
  if (ret != 0)
  {
    printf("set names failed, %s\n", mysql_error(mysql));
    return ret;
  }
  mysql_query(mysql, "drop table test_new_ps");
  stmt_select_test_not_exist_table(mysql);
  stmt_select_test_no_table(mysql);
  return 0;
}