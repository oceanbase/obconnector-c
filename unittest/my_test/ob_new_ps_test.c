#include <stdio.h>
#include <string.h>
#include "mysql.h"
#include "test_util.h"

/*
 * simple delete
 */
int stmt_delete_test(MYSQL* mysql) {
  my_log("stmt_delete_test");
  const char* pStatement = "delete from person";
  MYSQL_STMT    *stmt;
  my_ulonglong affected_rows;
  int ret = 0;
  stmt = mysql_stmt_init(mysql);
  if (!stmt) {
    my_log("msyql_stmt_init failed: %s\n", mysql_error(mysql));
    ASSERT_EQ(-1, 0, "mysql_stmt_init");
    return -1;
  }
  if (ret = mysql_stmt_prepare_v2(stmt, pStatement, strlen(pStatement), NULL)) {
    my_log("mysql_stmt_prepare_v2 failed:%s\n", mysql_error(mysql));
    return -1;
  }
  int param_count = mysql_stmt_param_count(stmt);
  my_log("param_count is %d", param_count);
  ASSERT_EQ(param_count, 0, "mysql_stmt_param_count");
  //stmt->iteration_count = 1;
  unsigned int iteration_count = 1;
  // mysql_stmt_attr_set(stmt, STMT_ATTR_ITERRATION_COUNT, &iteration_count);
  if (ret = mysql_stmt_execute_v2(stmt, pStatement, strlen(pStatement), iteration_count, 0, 0)) {
    my_log("mysql_stmt_execute_v2 delete failed:%s", mysql_stmt_error(stmt));
    ASSERT_EQ(ret, 0, "mysql_stmt_execute_v2");
    return -1;
  } else {
    ASSERT_EQ(0, 0, "mysql_stmt_execute_v2");
  }
  affected_rows = mysql_stmt_affected_rows(stmt);
  my_log("affected rows :%lu\n", affected_rows);
  ASSERT_EQ(0, 0, "mysql_stmt_affected_rows");
  if (ret = mysql_stmt_execute_v2(stmt, pStatement, strlen(pStatement), iteration_count, 0, 0)) {
    my_log("mysql_stmt_execute_v2 delete failed:%s\n", mysql_stmt_error(stmt));
    ASSERT_EQ(ret, 0, "mysql_stmt_execute_v2");
  } else {
    ASSERT_EQ(0, 0, "mysql_stmt_execute_v2");
  }
  if (ret = mysql_stmt_close(stmt)) {
    printf("mysql_stmt_close faield:%s\n", mysql_stmt_error(stmt));
    ASSERT_EQ(ret, 0, "mysql_stmt_close");
    return -1;
  } else {
    ASSERT_EQ(ret, 0, "mysql_stmt_close");
  }
  return 0;
}
int stmt_insert_test(MYSQL* mysql) {
  my_log("stmt_insert_test");
  const char* pStatement = "insert into person(id,num,name,birthday) values(?,?,?,?)";
  MYSQL_STMT    *stmt;
  MYSQL_BIND    bind[6];
  int           param_count;
  int id = 8;
  int num = 10;
  // char table[30] = "test.person";
  // char name[30] = "jh";
  // char birthday[20] = "2018-01-01";
  char* name = "test";
  char* birthday = "2021-04-06";
  my_ulonglong affected_rows;
  stmt = mysql_stmt_init(mysql);
  if (!stmt) {
    my_log("msyql_stmt_init failed: %s\n", mysql_error(mysql));
    return -1;
  }
  if (mysql_stmt_prepare_v2(stmt, pStatement, strlen(pStatement), NULL)) {
    my_log("mysql_stmt_prepare_v2 failed:%s\n", mysql_error(mysql));
    return -1;
  }

  param_count = mysql_stmt_param_count(stmt);
  my_log("param_count is %d", param_count);
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
    //num
    bind[1].buffer_type = MYSQL_TYPE_LONG;
    bind[1].buffer = (char*)&num;
    bind[1].buffer_length = 0;
    bind[1].is_null = 0;
    bind[1].length = 0;
    // bind[1].is_unsigned = 0;
  }
  {
    //name
    unsigned long str_len = strlen(name);
    printf("str_len for name is %d\n", str_len);
    bind[2].buffer_type = MYSQL_TYPE_STRING;
    bind[2].buffer = (char*)name;
    bind[2].buffer_length = str_len;
    bind[2].is_null = 0;
    bind[2].length = &str_len;
    // bind[2].is_unsigned = 0;
  }
  {
    //birthday
    unsigned long str_len = strlen(birthday);
    printf("str_len for birthday is %d\n", str_len);
    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)birthday;
    bind[3].buffer_length = str_len;
    bind[3].is_null = 0;
    // bind[3].length = &str_len;
    // bind[3].is_unsigned = 0;
  }
  if (mysql_stmt_bind_param(stmt, bind)) {
    printf("bind failed:%s\n", mysql_stmt_error(stmt));
    return -1;
  }
  unsigned int iteration_count = 1;
  // mysql_stmt_attr_set(stmt, STMT_ATTR_ITERRATION_COUNT, &iteration_count);
  //stmt->iteration_count = 1;
  int i = 0;
  for (i = 1; i < 10; i++) {
    id = i;
    if (mysql_stmt_execute_v2(stmt, pStatement, strlen(pStatement), 1, 0, 0)) {
      printf("mysql_stmt_execute_v2 insert failed:%s, i is %d\n", mysql_stmt_error(stmt), i);
      return -1;
    }
    affected_rows = mysql_stmt_affected_rows(stmt);
    printf("insert %d affected rows :%lu\n", i, affected_rows);

  }
  if (mysql_commit(mysql)) {
    printf("mysql_commit faield:%s\n", mysql_stmt_error(stmt));
  }
  if (mysql_stmt_close(stmt)) {
    printf("mysql_stmt_close faield:%s\n", mysql_stmt_error(stmt));
    return -1;
  }
  return 0;
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
  stmt_delete_test(mysql);
  stmt_insert_test(mysql);
  stmt_select_test(mysql);
  return 0;
}