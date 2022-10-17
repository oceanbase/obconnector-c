#include <stdio.h>
#include <string.h>
#include "mysql.h"
#include "test_util.h"

int stmt_insert_long_data_test(MYSQL* mysql) {
  my_log("stmt_insert_test");
  const char* pStatement = "insert into test_new_ps(id,name) values(?,?)";
  MYSQL_STMT    *stmt;
  MYSQL_BIND    bind[2];
  int           param_count;
  int id = 8;
  int num = 10;
  int ret = 0;
  int i = 0;
  // char table[30] = "test.person";
  // char name[30] = "jh";
  // char birthday[20] = "2018-01-01";
  char name[20] = "data long than";
  my_ulonglong affected_rows;
  stmt = mysql_stmt_init(mysql);
  if (!stmt) {
    my_log("msyql_stmt_init failed: %s\n", mysql_error(mysql));
    return -1;
  }
  if (ret = mysql_stmt_prepare_v2(stmt, pStatement, strlen(pStatement), NULL)) {
    my_log("mysql_stmt_prepare_v2 failed:%s\n", mysql_error(mysql));
    return -1;
  } else {
    ASSERT_EQ(ret , 0, "mysql_stmt_prepare_v2");
  }

  param_count = mysql_stmt_param_count(stmt);
  my_log("param_count is %d", param_count);
  ASSERT_EQ(param_count, 2, "mysql_stmt_param_count");
  memset(bind, 0, sizeof(bind));
  {
    //id
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = (char*)&id;
    bind[0].buffer_length = 0;
    bind[0].is_null = 0;
    bind[0].length = 0;
    // bind[0].is_unsigned = 0;
  }
  {
    //name
    unsigned long str_len = strlen(name);
    my_log("str_len for name is %d", str_len);
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)name;
    bind[1].buffer_length = str_len;
    bind[1].is_null = 0;
    bind[1].length = &str_len;
    // bind[2].is_unsigned = 0;
  }
  if (ret = mysql_stmt_bind_param(stmt, bind)) {
    my_log("bind failed:%s", mysql_stmt_error(stmt));
    return -1;
  } else {
    ASSERT_EQ(ret, 0, "mysql_stmt_bind_param");
  }
  unsigned int iteration_count = 1;
  // mysql_stmt_attr_set(stmt, STMT_ATTR_ITERRATION_COUNT, &iteration_count);
  //stmt->iteration_count = 1;
  for (i = 0; i < 2; i++)  {
    if (mysql_stmt_execute_v2(stmt,pStatement, strlen(pStatement), iteration_count, 0, 0)) {
      my_log("mysql_stmt_execute_v2 insert failed:%s", mysql_stmt_error(stmt));
      return -1;
    }
    affected_rows = mysql_stmt_affected_rows(stmt);
    my_log("insert %d affected rows :%lu", iteration_count,affected_rows);
    ASSERT_EQ(affected_rows, 0, "mysql_stmt_affected_rows");
    snprintf(name, 20, "short");
    if (ret = mysql_stmt_execute_v2(stmt, pStatement, strlen(pStatement),iteration_count, 0, 0)) {
      my_log("mysql_stmt_execute_v2 insert failed:%s", mysql_stmt_error(stmt));
      return -1;
    } else {
      ASSERT_EQ(ret, 0, "mysql_stmt_execute_v2");
    }
    affected_rows = mysql_stmt_affected_rows(stmt);
    my_log("insert %d affected rows :%lu", iteration_count, affected_rows);
    ASSERT_EQ(affected_rows, 1, "mysql_stmt_affected_rows");
  }
  if (ret = mysql_stmt_close(stmt)) {
    my_log("mysql_stmt_close faield:%s", mysql_stmt_error(stmt));
    return -1;
  } else {
    ASSERT_EQ(ret, 0, "mysql_stmt_close");
  }
  return 0;
}
int stmt_insert_data_test(MYSQL* mysql) {
  my_log("stmt_insert_test");
  const char* pStatement = "insert into test_new_ps(id,name) values(?,?)";
  MYSQL_STMT    *stmt;
  MYSQL_BIND    bind[2];
  int           param_count;
  int id = 8;
  int num = 10;
  int ret = 0;
  int i = 0;
  // char table[30] = "test.person";
  // char name[30] = "jh";
  // char birthday[20] = "2018-01-01";
  char name[20] = "data1";
  my_ulonglong affected_rows;
  stmt = mysql_stmt_init(mysql);
  if (!stmt) {
    my_log("msyql_stmt_init failed: %s\n", mysql_error(mysql));
    return -1;
  }
  if (ret = mysql_stmt_prepare_v2(stmt, pStatement, strlen(pStatement), NULL)) {
    my_log("mysql_stmt_prepare_v2 failed:%s\n", mysql_error(mysql));
    return -1;
  } else {
    ASSERT_EQ(ret , 0, "mysql_stmt_prepare_v2");
  }

  param_count = mysql_stmt_param_count(stmt);
  my_log("param_count is %d", param_count);
  ASSERT_EQ(param_count, 2, "mysql_stmt_param_count");
  memset(bind, 0, sizeof(bind));
  {
    //id
    bind[0].buffer_type = MYSQL_TYPE_LONG;
    bind[0].buffer = (char*)&id;
    bind[0].buffer_length = 0;
    bind[0].is_null = 0;
    bind[0].length = 0;
    // bind[0].is_unsigned = 0;
  }
  {
    //name
    unsigned long str_len = strlen(name);
    my_log("str_len for name is %d", str_len);
    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = (char*)name;
    bind[1].buffer_length = str_len;
    bind[1].is_null = 0;
    bind[1].length = &str_len;
    // bind[2].is_unsigned = 0;
  }
  if (ret = mysql_stmt_bind_param(stmt, bind)) {
    my_log("bind failed:%s", mysql_stmt_error(stmt));
    return -1;
  } else {
    ASSERT_EQ(ret, 0, "mysql_stmt_bind_param");
  }
  unsigned int iteration_count = 1;
  // mysql_stmt_attr_set(stmt, STMT_ATTR_ITERRATION_COUNT, &iteration_count);
  //stmt->iteration_count = 1;
  for (i = 0; i < 2; i++)
  {
    if (mysql_stmt_execute_v2(stmt, pStatement, strlen(pStatement), iteration_count, 0, 0)) {
      my_log("mysql_stmt_execute_v2 insert failed:%s", mysql_stmt_error(stmt));
      return -1;
    }
    affected_rows = mysql_stmt_affected_rows(stmt);
    my_log("insert %d affected rows :%lu", iteration_count,affected_rows);
    ASSERT_EQ(affected_rows, 1, "mysql_stmt_affected_rows");
    snprintf(name, 20, "short");
    if (ret = mysql_stmt_execute_v2(stmt, pStatement, strlen(pStatement),iteration_count, 0, 0)) {
      my_log("mysql_stmt_execute_v2 insert failed:%s", mysql_stmt_error(stmt));
      return -1;
    } else {
      ASSERT_EQ(ret, 0, "mysql_stmt_execute_v2");
    }
    affected_rows = mysql_stmt_affected_rows(stmt);
    my_log("insert %d affected rows :%lu", iteration_count, affected_rows);
    ASSERT_EQ(affected_rows, 1, "mysql_stmt_affected_rows");
  }

  if (ret = mysql_stmt_close(stmt)) {
    my_log("mysql_stmt_close faield:%s", mysql_stmt_error(stmt));
    return -1;
  } else {
    ASSERT_EQ(ret, 0, "mysql_stmt_close");
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
    printf("set names error, %s\n", mysql_error(mysql));
    return ret;
  }
  mysql_query(mysql, "drop table test_new_ps");
  ret = mysql_query(mysql, "create table test_new_ps(id int, name varchar2(10))");
  if (ret != 0)
  {
    my_log("create table error, %s\n", mysql_error(mysql));
    return ret;
  }
  stmt_insert_long_data_test(mysql);
  stmt_insert_data_test(mysql);
  return 0;
}