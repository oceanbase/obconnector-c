#include <stdio.h>
#include <string.h>
#include "mysql.h"
#include "test_util.h"
/*
 *
 */
int do_pl_test(MYSQL* mysql) {
  my_log("=========do_pl_test==========");
  const char* create_pl = "create or replace procedure pl_test(x in number, y in out varchar2) "\
                          "is begin "\
                          " y:='this is a pl test';"\
                          "END";
  int ret = 0;
  if (mysql_real_query(mysql, create_pl, strlen(create_pl))) {
    my_log("mysql_real_query failed: %s", mysql_error(mysql));
    ASSERT_EQ(0, -1, "mysql_real_query failed");
    return -1;
  }
  const char* pStatement = "call pl_test(?, ?)";
  MYSQL_STMT    *stmt;
  MYSQL_BIND    param_bind[2];
  MYSQL_BIND    res_bind[2];
  int           param_count;
  int id = 8;
  char name[20] = "jh";
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
  ASSERT_EQ(param_count, 2, "mysql_stmt_param_count");
  MYSQL_RES *param_result = NULL;
  MYSQL_FIELD *params = NULL;
  if (NULL == (param_result = mysql_stmt_param_metadata(stmt))) {
    my_log("mysql_stmt_param_metadata failed:%s", mysql_stmt_error(stmt));
  } else if (NULL == (params = mysql_fetch_params(param_result))) {
    my_log("mysql_fetch_params failed:%s", mysql_stmt_error(stmt));
  } else {
	 ASSERT_EQ(params[1].ob_routine_param_inout, 3, "ob_routine_param_inout");
  }
  memset(param_bind, 0, sizeof(param_bind));
  memset(res_bind, 0, sizeof(res_bind));
  {
    //id
    param_bind[0].buffer_type = MYSQL_TYPE_LONG;
    param_bind[0].buffer = (char*)&id;
    param_bind[0].buffer_length = 0;
    param_bind[0].is_null = 0;
    param_bind[0].length = 0;
  }
  {
    //name
    unsigned long str_len = strlen(name);
    param_bind[1].buffer_type = MYSQL_TYPE_STRING;
    param_bind[1].buffer = (char*)name;
    param_bind[1].buffer_length = str_len;
    param_bind[1].is_null = 0;
    param_bind[1].length = &str_len;
    // bind[1].is_unsigned = 0;
  }
  if (mysql_stmt_bind_param(stmt, param_bind)) {
    my_log("bind failed:%s", mysql_stmt_error(stmt));
    ASSERT_EQ(0, -1, "mysql_stmt_bind_param failed");
    return -1;
  }
  if (mysql_stmt_execute(stmt)) {
    my_log("mysql_stmt_execute insert failed:%s", mysql_stmt_error(stmt));
    ASSERT_EQ(0, -1, "mysql_stmt_execute failed");
    return -1;
  }
  char out_buf[20] = {0};
  res_bind[0].buffer_type = MYSQL_TYPE_STRING;
  res_bind[0].buffer = out_buf;
  res_bind[0].buffer_length = sizeof(out_buf);

  if (mysql_stmt_bind_result(stmt, res_bind)) {
    my_log("mysql_stmt_bind_result failed:%s", mysql_stmt_error(stmt));
    return -1;
  }
  int i = 1;
  while (mysql_stmt_fetch(stmt) == 0) {
    // my_log("out_buf is %s", out_buf);
	if (strcmp(out_buf, "this is a pl test") == 0) {
		ASSERT_EQ(0, 0, "mysql_stmt_fetch");
	} else {
		ASSERT_EQ(0, -1, "mysql_stmt_fetch");
	}
  }
  if (mysql_stmt_close(stmt)) {
    my_log("mysql_stmt_close faield:%s", mysql_stmt_error(stmt));
    ASSERT_EQ(0, -1, "mysql_stmt_error");
    return -1;
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
    my_log("connect failed: %s, host:%s, port:%d, user:%s, pass:%s, dbname:%s",
      mysql_error(mysql),DBHOST, DBPORT, DBUSER, DBPASS, DBNAME);
    mysql_close(mysql);
    mysql_library_end();
    return 0;
  } else {
    my_log("connect %s:%d using %s succ", DBHOST, DBPORT, DBUSER);
  }
  do_pl_test(mysql);
  return 0;
}
