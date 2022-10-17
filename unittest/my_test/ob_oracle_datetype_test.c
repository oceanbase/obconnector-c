#include <stdio.h>
#include <string.h>
#include "mysql.h"
#include "test_util.h"
/*
 *
 */
int show_query_result(MYSQL* mysql, const char* query, int expect_field_count)
{
  int filed_count = 0;
  if (mysql_real_query(mysql, query, strlen(query))) {
    my_log("mysql_real_query select failed:%s", mysql_error(mysql));
    ASSERT_EQ(0, -1, "mysql_real_query");
    return -1;
  }
  MYSQL_RES * result = mysql_store_result(mysql);
  if (!result) {
    my_log("unexpect null result");
    ASSERT_EQ(0, -1, "mysql_use_result get null");
    return -1;
  }
  filed_count = mysql_num_fields(result);
  if (expect_field_count > 0) {
    ASSERT_EQ(filed_count, expect_field_count, "mysql_num_fields");
  }
  int i = 0;
  MYSQL_FIELD *fields = mysql_fetch_fields(result);
  for(i = 0; i < filed_count; i++)
  {
    printf("%s\t", fields[i].name);
  }
  printf("\n");
  int result_count = 0;
  MYSQL_ROW sql_row;
  while (sql_row = mysql_fetch_row(result)) {
    result_count++;
    for (i = 0; i < filed_count; i++)
    {
      printf("%s,", sql_row[i]);
    }
    printf("\n");
  }
  mysql_free_result(result);
  return 0;
}
int do_text_time_test1(MYSQL* mysql) {
  my_log("=========do_text_time_test1==========");
  const char* select_query = "select * from timetest;";
  // const char* insert_query = "insert into timetest values(sysdate,sysdate,sysdate,sysdate)";
  const char* insert_query = "insert into timetest values(to_date('2021-03-29 02.32.18', 'YYYY-mm-dd HH.MI.SS')"\
                ",to_date('2021-03-29 02.32.18', 'YYYY-mm-dd HH.MI.SS')"
                ",to_date('2021-03-29 02.32.18', 'YYYY-mm-dd HH.MI.SS')"
                ",to_date('2021-03-29 02.32.18', 'YYYY-mm-dd HH.MI.SS'))";
  my_ulonglong affected_rows;
  mysql_real_query(mysql, "delete from timetest", strlen("delete from timetest"));
  mysql_query(mysql, "ALTER SESSION SET NLS_DATE_FORMAT='DD-MON-RR';");
  mysql_query(mysql, "ALTER SESSION SET NLS_TIMESTAMP_FORMAT='DD-MON-RR HH.MI.SSXFF AM';");
  mysql_query(mysql, "ALTER SESSION SET NLS_TIMESTAMP_TZ_FORMAT='DD-MON-RR HH.MI.SSXFF AM TZR';");
  if (mysql_real_query(mysql, insert_query, strlen(insert_query))) {
    my_log("mysql_real_query insert_query failed:%s", mysql_error(mysql));
    ASSERT_EQ(0, -1, "mysql_real_query");
    return -1;
  }
  show_query_result(mysql, "show variables like '%nls_date_format%'", 0);
  int filed_count = 0;
  if (mysql_real_query(mysql, select_query, strlen(select_query))) {
    my_log("mysql_real_query select failed:%s", mysql_error(mysql));
    ASSERT_EQ(0, -1, "mysql_real_query");
    return -1;
  }
  MYSQL_RES * result = mysql_store_result(mysql);
  if (!result) {
    my_log("unexpect null result");
    ASSERT_EQ(0, -1, "mysql_use_result get null");
    return -1;
  }
  filed_count = mysql_num_fields(result);
  ASSERT_EQ(filed_count, 4, "mysql_num_fields");
  int i = 0;
  MYSQL_FIELD *fields = mysql_fetch_fields(result);
  for(i = 0; i < filed_count; i++)
  {
    printf("%s\t", fields[i].name);
  }
  printf("\n");
  int result_count = 0;
  MYSQL_ROW sql_row;
  while (sql_row = mysql_fetch_row(result)) {
    result_count++;
    for (i = 0; i < filed_count; i++)
    {
      printf("%s,", sql_row[i]);
      switch(i){
      case 0:
        ASSERT_EQ(strncmp(sql_row[i], "29-MAR-21", strlen("29-MAR-21")), 0, "DATE_FORMAT");
        break;
      case 1:
        ASSERT_EQ(strncmp(sql_row[i], "29-MAR-21 02.32.18.000 AM", strlen("29-MAR-21 02.32.18.000 AM")), 0, "TIMESTAMP_FORMAT");
        break;
      case 2:
        ASSERT_EQ(strncmp(sql_row[i], "29-MAR-21 02.32.18.000 AM +08:00", strlen("29-MAR-21 02.32.18.000 AM +08:00")),
          0, "TIMESTAMP_WITH_TIMEEZONE_FORMAT");
        break;
      case 3:
        ASSERT_EQ(strncmp(sql_row[i], "29-MAR-21 02.32.18.000 AM", strlen("29-MAR-21 02.32.18.000 AM")),
          0, "TIMESTAMP_WITH_LOCAL_TIMEEZONE_FORMAT");
        break;
      default:
        ASSERT_EQ(0, -1, "unexpect index");
        break;
      }
    }
    // printf("\n");
  }
  mysql_free_result(result);
  mysql_real_query(mysql, "commit", strlen("commit"));
  return 0;
}
int do_text_time_test2(MYSQL* mysql) {
  my_log("=========do_text_time_test1==========");
  const char* select_query = "select * from timetest;";
  // const char* insert_query = "insert into timetest values(sysdate,sysdate,sysdate,sysdate)";
  const char* insert_query = "insert into timetest values(to_date('2021-03-29 02.32.18', 'YYYY-mm-dd HH.MI.SS')"\
                ",to_date('2021-03-29 02.32.18', 'YYYY-mm-dd HH.MI.SS')"
                ",to_date('2021-03-29 02.32.18', 'YYYY-mm-dd HH.MI.SS')"
                ",to_date('2021-03-29 02.32.18', 'YYYY-mm-dd HH.MI.SS'))";
  my_ulonglong affected_rows;
  mysql_real_query(mysql, "delete from timetest", strlen("delete from timetest"));
  mysql_query(mysql, "ALTER SESSION SET NLS_DATE_FORMAT='YYYY-MM-DD';");
  mysql_query(mysql, "ALTER SESSION SET NLS_TIMESTAMP_FORMAT='YYYY-MM-DD HH24:MI:SS.FF';");
  mysql_query(mysql, "ALTER SESSION SET NLS_TIMESTAMP_TZ_FORMAT='YYYY-MM-DD HH24:MI:SS.FF TZR TZD';");
  if (mysql_real_query(mysql, insert_query, strlen(insert_query))) {
    my_log("mysql_real_query insert_query failed:%s", mysql_error(mysql));
    ASSERT_EQ(0, -1, "mysql_real_query");
    return -1;
  }
  show_query_result(mysql, "show variables like '%nls_date_format%'", 0);
  int filed_count = 0;
  if (mysql_real_query(mysql, select_query, strlen(select_query))) {
    my_log("mysql_real_query select failed:%s", mysql_error(mysql));
    ASSERT_EQ(0, -1, "mysql_real_query");
    return -1;
  }
  MYSQL_RES * result = mysql_store_result(mysql);
  if (!result) {
    my_log("unexpect null result");
    ASSERT_EQ(0, -1, "mysql_use_result get null");
    return -1;
  }
  filed_count = mysql_num_fields(result);
  ASSERT_EQ(filed_count, 4, "mysql_num_fields");
  int i = 0;
  MYSQL_FIELD *fields = mysql_fetch_fields(result);
  for(i = 0; i < filed_count; i++)
  {
    printf("%s\t", fields[i].name);
  }
  printf("\n");
  int result_count = 0;
  MYSQL_ROW sql_row;
  while (sql_row = mysql_fetch_row(result)) {
    result_count++;
    for (i = 0; i < filed_count; i++)
    {
      printf("%s,", sql_row[i]);
      switch(i){
      case 0:
        ASSERT_EQ(strncmp(sql_row[i], "2021-03-29", strlen("2021-03-29")), 0, "DATE_FORMAT");
        break;
      case 1:
        ASSERT_EQ(strncmp(sql_row[i], "2021-03-29 02:32:18.000", strlen("2021-03-29 02:32:18.000")), 0, "TIMESTAMP_FORMAT");
        break;
      case 2:
        ASSERT_EQ(strncmp(sql_row[i], "2021-03-29 02:32:18.000 +08:00", strlen("2021-03-29 02:32:18.000 +08:00")),
          0, "TIMESTAMP_WITH_TIMEEZONE_FORMAT");
        break;
      case 3:
        ASSERT_EQ(strncmp(sql_row[i], "2021-03-29 02:32:18.000", strlen("2021-03-29 02:32:18.000")),
          0, "TIMESTAMP_WITH_LOCAL_TIMEEZONE_FORMAT");
        break;
      default:
        ASSERT_EQ(0, -1, "unexpect index");
        break;
      }
    }
    // printf("\n");
  }
  mysql_free_result(result);
  mysql_real_query(mysql, "commit", strlen("commit"));
  return 0;
}
int do_ps_time_test1(MYSQL* mysql) {
  my_log("=========do_text_time_test1==========");
  const char* select_query = "select * from timetest;";
  // const char* insert_query = "insert into timetest values(sysdate,sysdate,sysdate,sysdate)";
  const char* insert_query = "insert into timetest values(to_date('2021-03-29 02.32.18', 'YYYY-mm-dd HH.MI.SS')"\
                ",to_date('2021-03-29 02.32.18', 'YYYY-mm-dd HH.MI.SS')"
                ",to_date('2021-03-29 02.32.18', 'YYYY-mm-dd HH.MI.SS')"
                ",to_date('2021-03-29 02.32.18', 'YYYY-mm-dd HH.MI.SS'))";
  my_ulonglong affected_rows;
  mysql_real_query(mysql, "delete from timetest", strlen("delete from timetest"));
  MYSQL_STMT    *stmt;
  MYSQL_BIND    bind[4];
  int filed_count = 0;
  int param_count = 0;
  int i = 0;
  mysql_query(mysql, "ALTER SESSION SET NLS_DATE_FORMAT='DD-MON-RR';");
  mysql_query(mysql, "ALTER SESSION SET NLS_TIMESTAMP_FORMAT='DD-MON-RR HH.MI.SSXFF AM';");
  mysql_query(mysql, "ALTER SESSION SET NLS_TIMESTAMP_TZ_FORMAT='DD-MON-RR HH.MI.SSXFF AM TZR';");
  if (mysql_real_query(mysql, insert_query, strlen(insert_query))) {
    my_log("mysql_real_query insert_query failed:%s", mysql_error(mysql));
    ASSERT_EQ(0, -1, "mysql_real_query");
    return -1;
  }
  show_query_result(mysql, "show variables like '%nls_date_format%'", 0);
  stmt = mysql_stmt_init(mysql);
  if (!stmt) {
    my_log("msyql_stmt_init failed: %s", mysql_error(mysql));
    return -1;
  }
  if (mysql_stmt_prepare(stmt, select_query, strlen(select_query))) {
    my_log("mysql_stmt_prepare failed:%s", mysql_error(mysql));
    return -1;
  }
  filed_count = mysql_stmt_field_count(stmt);
  param_count = mysql_stmt_param_count(stmt);
  ASSERT_EQ(filed_count, 4, "mysql_stmt_field_count");
  ASSERT_EQ(param_count, 0, "mysql_stmt_param_count");
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
  ASSERT_EQ(row_count, 1, "mysql_stmt_field_count");
  memset(bind, 0, sizeof(bind));
  char buffer[4][128];
  memset(buffer, 0, 4* 128);
  for (i = 0; i < 4; i++)
  {
    bind[i].buffer = buffer[i];
    bind[i].buffer_length = 128;
    bind[i].buffer_type = MYSQL_TYPE_STRING;
  }
  if (mysql_stmt_bind_result(stmt, bind)) {
    my_log("mysql_stmt_bind_result failed:%s", mysql_stmt_error(stmt));
    return -1;
  }
  while (mysql_stmt_fetch(stmt) == 0) {
    for (i =0; i < filed_count; i++)
    {
      my_log("col[%d]:%s", i, buffer[i]);
    }
  }
  mysql_stmt_close(stmt);
  mysql_real_query(mysql, "commit", strlen("commit"));
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
  int ret = 0;
  mysql_real_query(mysql, "drop table timetest", strlen("drop table timetest"));
  ret = mysql_query(mysql, "create table timetest(tme date,tmestp timestamp(3),"\
                       "tmestp_tz timestamp(3) with time zone,"\
                       "tmpstp_tzl timestamp(3) with local time zone);");
  if (ret != 0){
    printf("create table failed, %s\n", mysql_error(mysql));
    return ret;
  }
  do_text_time_test1(mysql);
  do_text_time_test2(mysql);
  do_ps_time_test1(mysql);
  return 0;
}
