#include <stdio.h>
#include <string.h>
#include "mysql.h"
#include "test_util.h"
/*
 *
 */
int do_stmt_delete(MYSQL* mysql) {
  my_log("=========do_stmt_delete==========");
  const char* pStatement = "delete from person";
  MYSQL_STMT    *stmt;
  my_ulonglong affected_rows;
  stmt = mysql_stmt_init(mysql);
  if (!stmt) {
    my_log("msyql_stmt_init failed: %s", mysql_error(mysql));
    ASSERT_EQ(0, -1, "mysql_stmt_init failed");
    return -1;
  }
  if (mysql_stmt_prepare(stmt, pStatement, strlen(pStatement))) {
    my_log("mysql_stmt_prepare failed:%s", mysql_error(mysql));
    ASSERT_EQ(0, -1, "mysql_stmt_prepare failed");
    return -1;
  }
  int param_count = mysql_stmt_param_count(stmt);
  ASSERT_EQ(0, param_count, "mysql_stmt_param_count");
  //stmt->iteration_count = 1;
  if (mysql_stmt_execute(stmt)) {
    my_log("mysql_stmt_execute delete failed:%s\n", mysql_stmt_error(stmt));
    return -1;
  }
  affected_rows = mysql_stmt_affected_rows(stmt);
  ASSERT_EQ(affected_rows, 0, "mysql_stmt_affected_rows");
  if (mysql_stmt_close(stmt)) {
    my_log("mysql_stmt_close faield:%s", mysql_stmt_error(stmt));
    ASSERT_EQ(0, -1, "mysql_stmt_close failed");
    return -1;
  }
  return 0;
}
int do_stmt_insert(MYSQL* mysql) {
  my_log("=========do_stmt_insert==========");
  const char* pStatement = "insert into person(id,num,name,birthday) values(?,?,?,?)";
  MYSQL_STMT    *stmt;
  MYSQL_BIND    bind[6];
  int           param_count;
  int id = 8;
  int num = 10;
  // char table[30] = "test.person";
  // char name[30] = "jh";
  // char birthday[20] = "2018-01-01";
  char* name = "jh";
  char* birthday = "2018-01-01";
  my_ulonglong affected_rows;
  stmt = mysql_stmt_init(mysql);
  if (!stmt) {
    my_log("msyql_stmt_init failed: %s", mysql_error(mysql));
    ASSERT_EQ(0, -1, "mysql_stmt_init failed");
    return -1;
  }
  if (mysql_stmt_prepare(stmt, pStatement, strlen(pStatement))) {
    my_log("mysql_stmt_prepare failed:%s", mysql_error(mysql));
    ASSERT_EQ(0, -1, "mysql_stmt_prepare failed");
    return -1;
  }

  param_count = mysql_stmt_param_count(stmt);
  ASSERT_EQ(param_count, 4, "mysql_stmt_param_count");
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
    bind[3].buffer_type = MYSQL_TYPE_STRING;
    bind[3].buffer = (char*)birthday;
    bind[3].buffer_length = str_len;
    bind[3].is_null = 0;
    // bind[3].length = &str_len;
    // bind[3].is_unsigned = 0;
  }
  if (mysql_stmt_bind_param(stmt, bind)) {
    my_log("bind failed:%s", mysql_stmt_error(stmt));
    ASSERT_EQ(0, -1, "mysql_stmt_bind_param failed");
    return -1;
  }
  unsigned int iteration_count = 1;
  //stmt->iteration_count = 1;
  int i = 0;
  for (i = 1; i < 10; i++) {
    id = i;
    if (mysql_stmt_execute(stmt)) {
      my_log("mysql_stmt_execute insert failed:%s, i is %d", mysql_stmt_error(stmt), i);
      ASSERT_EQ(0, -1, "mysql_stmt_execute failed");
      return -1;
    }
    affected_rows = mysql_stmt_affected_rows(stmt);
    if (affected_rows != 1) {
      ASSERT_EQ(affected_rows, 1, "mysql_stmt_affected_rows");
    }
  }
  if (mysql_stmt_close(stmt)) {
    my_log("mysql_stmt_close faield:%s", mysql_stmt_error(stmt));
    ASSERT_EQ(0, -1, "mysql_stmt_error");
    return -1;
  }
  return 0;
}
int do_stmt_select(MYSQL* mysql) {
  my_log("=========do_stmt_select==========");
  const char* pStatement = "select * from person where id > ?";
  MYSQL_STMT    *stmt;
  MYSQL_BIND    bind[4];
  int           param_count;
  int filed_count;
  my_ulonglong affected_rows;
  stmt = mysql_stmt_init(mysql);
  if (!stmt) {
    my_log("msyql_stmt_init failed: %s", mysql_error(mysql));
    return -1;
  }
  if (mysql_stmt_prepare(stmt, pStatement, strlen(pStatement))) {
    my_log("mysql_stmt_prepare failed:%s", mysql_error(mysql));
    return -1;
  }
  filed_count = mysql_stmt_field_count(stmt);
  param_count = mysql_stmt_param_count(stmt);
  ASSERT_EQ(param_count, 1, "mysql_stmt_param_count");
  int id, num;
  id = 0;
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
  if (mysql_stmt_bind_param(stmt, bind)) {
    my_log("bind failed:%s", mysql_stmt_error(stmt));
    ASSERT_EQ(0, -1, "mysql_stmt_bind_param failed");
    return -1;
  }
  unsigned int iteration_count = 1;
  //stmt->iteration_count = 0;
  if (mysql_stmt_execute(stmt)) {
    my_log("mysql_stmt_execute select failed:%s", mysql_stmt_error(stmt));
    return -1;
  }
  if (mysql_stmt_store_result(stmt)) {
    my_log("mysql_stmt_store_result failed :%s", mysql_stmt_error(stmt));
    return -1;
  }
  filed_count = mysql_stmt_field_count(stmt);
  ASSERT_EQ(filed_count, 4, "mysql_stmt_field_count");
  my_ulonglong row_count = mysql_stmt_num_rows(stmt);
  ASSERT_EQ(row_count, 9, "mysql_stmt_field_count");
  memset(bind, 0, sizeof(bind));
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
    my_log("mysql_stmt_bind_result failed:%s", mysql_stmt_error(stmt));
    return -1;
  }
  int i = 1;
  while (mysql_stmt_fetch(stmt) == 0) {
    if (id != i) {
      ASSERT_EQ(id, i, "check value");
    }
    ++i;
    // my_log("%d\t%d\t%s\t%s\n", id, num, name, birthday);
  }
  mysql_stmt_close(stmt);
  return 0;
}

int main(int argc, char** argv) {
  mysql_library_init(0, NULL, NULL);
  MYSQL *mysql = mysql_init(NULL);
  unsigned int timeout = 3000;
  init_conn_info(OB_MYSQL_MODE);
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
  int ret = 0;
  mysql_query(mysql, "drop table person");
  ret = mysql_query(mysql, "create table person(id int, num int, name varchar(20), birthday varchar(20))");
  if (ret != 0)
  {
    my_log("create table error, %s", mysql_error(mysql));
    return ret;
  }
  do_stmt_delete(mysql);
  do_stmt_insert(mysql);
  do_stmt_select(mysql);
  return 0;
}
