#include <stdio.h>
#include <string.h>
#include "mysql.h"
#include "test_util.h"
/*
 *
 */
inline long long get_cur_time()
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec * 1000000 + tv.tv_usec;
}
int do_stmt_select_query(MYSQL* mysql) {
  my_log("=========do_stmt_select_query==========");
  const char* select_query = "select * from test;";
  const char* insert_query = "insert into test values(1, 'test1')";
  int filed_count;
  my_ulonglong affected_rows;
  int64_t t_start, t_end;
  t_start = get_cur_time();
  if (mysql_real_query(mysql, insert_query, strlen(insert_query))) {
    my_log("mysql_real_query insert_query failed:%s", mysql_error(mysql));
    ASSERT_EQ(0, -1, "mysql_real_query");
    return -1;
  }
  if (mysql_real_query(mysql, select_query, strlen(select_query))) {
    my_log("mysql_real_query select failed:%s", mysql_error(mysql));
    ASSERT_EQ(0, -1, "mysql_real_query");
    return -1;
  }
  t_end = get_cur_time();
  // printf("mysql_real_query cost time %ld us\n", t_end - t_start);
  t_start = t_end;
  MYSQL_RES * result = mysql_use_result(mysql);
  t_end = get_cur_time();
  // printf("store result cost time %ld us\n", t_end - t_start);
  t_start = t_end;
  if (!result) {
    my_log("unexpect null result");
    ASSERT_EQ(0, -1, "mysql_use_result get null");
    return -1;
  }
  filed_count = mysql_num_fields(result);
  ASSERT_EQ(filed_count, 2, "mysql_num_fields");
  int i = 0;
  MYSQL_FIELD *fields = mysql_fetch_fields(result);
  for(i = 0; i < filed_count; i++)
  {
    printf("%s\t", fields[i].name);
    if (i == 0 && strcasecmp(fields[i].name, "ID") != 0) {
      ASSERT_EQ(0, -1, "invalid field");
    } else if (i == 1 && strcasecmp(fields[i].name, "NAME") != 0) {
      ASSERT_EQ(0, -1, "invalid field");
    }
  }
  printf("\n");
  int result_count = 0;
  MYSQL_ROW sql_row;
  while (sql_row = mysql_fetch_row(result)) {
    result_count++;
    for (i = 0; i < filed_count; i++)
    {
      //
    }
  }
  t_end = get_cur_time();
  // printf("fetch %d  cost time %ld us\n", result_count, t_end - t_start);
  mysql_free_result(result);
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
  mysql_real_query(mysql, "drop table test", strlen("drop table test"));
  ret = mysql_query(mysql, "create table test(id int, name varchar(20))");
  if (ret != 0){
    printf("create table failed, %s\n", mysql_error(mysql));
    return ret;
  }
  do_stmt_select_query(mysql);
  return 0;
}
