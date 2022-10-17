#include <stdio.h>
#include <string.h>
#include "mysql.h"
#include "test_util.h"
/*
 *
 */
int do_stmt_select_with_cursor(MYSQL* mysql) {
  my_log("=========do_stmt_select_with_cursor==========");
  const char* pStatement = "select * from person where id > ?";
  MYSQL_STMT    *stmt;
  MYSQL_BIND    bind[4];
  int           param_count;
  int filed_count;
  //prepare data
  int i = 0;
  char insert_buf[100] = {0};
  for (i = 0; i < 10; i++)
  {
    snprintf(insert_buf, 100, "insert into person values('%d', '%d', 'test_%d', '2021-04-06 10:21:00');", i, i, i);
    // my_log("buf is %s", insert_buf);
    if (mysql_real_query(mysql, insert_buf, strlen(insert_buf))) {
     my_log("mysql_real_query failed:%s", mysql_error(mysql));
    }
  }
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
  ulong cursor_type = CURSOR_TYPE_READ_ONLY;
  ASSERT_EQ(0, mysql_stmt_attr_set(stmt, STMT_ATTR_CURSOR_TYPE, &cursor_type), "mysql_stmt_attr_set");
  unsigned int iteration_count = 1;
  //stmt->iteration_count = 0;
  if (mysql_stmt_execute(stmt)) {
    my_log("mysql_stmt_execute select failed:%s", mysql_stmt_error(stmt));
    return -1;
  }
  // if (mysql_stmt_store_result(stmt)) {
  //   my_log("mysql_stmt_store_result failed :%s", mysql_stmt_error(stmt));
  //   return -1;
  // }
  filed_count = mysql_stmt_field_count(stmt);
  ASSERT_EQ(filed_count, 4, "mysql_stmt_field_count");
  my_ulonglong row_count = mysql_stmt_num_rows(stmt);
  // ASSERT_EQ(row_count, 9, "mysql_stmt_num_rows");
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
  i = 1;
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
  do_stmt_select_with_cursor(mysql);
  return 0;
}
