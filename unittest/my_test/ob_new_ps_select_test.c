#include <stdio.h>
#include <string.h>
#include "mysql.h"
#include "test_util.h"

int stmt_select_test_no_param_no_result(MYSQL* mysql) {
  my_log("stmt_select_test_no_param_no_result");
  const char* pStatement = "select * from test_new_ps";
  MYSQL_STMT    *stmt;
  MYSQL_BIND    bind[4];
  int           ret = 0;
  int           param_count;
  int           filed_count;
  my_ulonglong  affected_rows;
  const char* query = "delete from test_new_ps";
  if (ret = mysql_real_query(mysql, query, strlen(query))) {
    my_log("mysql_real_query failed:%s", mysql_error(mysql));
    ASSERT_EQ(ret, 0, "mysql_real_query");
  }
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
  if (mysql_stmt_execute_v2(stmt, pStatement, strlen(pStatement), iteration_count, 0, 0)) {
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

  if (ret = mysql_stmt_bind_result(stmt, bind)) {
    my_log("mysql_stmt_bind_result failed:%s", mysql_stmt_error(stmt));
    ASSERT_EQ(ret, 0, "mysql_stmt_bind_result");
  }
  while (mysql_stmt_fetch(stmt) == 0) {
    printf("%d\t%d\t%s\t%s\n", id, num, name, birthday);
  }
  if (ret = mysql_stmt_close(stmt)) {
    my_log("mysql_stmt_close faled:%s", mysql_stmt_error(stmt));
  }
  ASSERT_EQ(0, 0, "stmt_select_test_no_param_no_result");
  return 0;
}
int stmt_select_test_with_param_no_result(MYSQL* mysql) {
  my_log("stmt_select_test_with_param_no_result");
  const char* pStatement = "select * from test_new_ps where id = ? and num = ? and name = ?";
  MYSQL_STMT    *stmt;
  MYSQL_BIND    bind[4];
  MYSQL_BIND    param[4];
  int           ret = 0;
  int           param_count;
  int           filed_count;
  my_ulonglong  affected_rows;
  const char* query = "delete from test_new_ps";
  if (ret = mysql_real_query(mysql, query, strlen(query))) {
    my_log("mysql_real_query failed:%s", mysql_error(mysql));
    ASSERT_EQ(ret, 0, "mysql_real_query");
  }
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
  int input_id = 0, input_num = 0;
  char input_name[20] = "test";
  unsigned long str_len;
  {
    //id
    param[0].buffer_type = MYSQL_TYPE_LONG;
    param[0].buffer = (char*)&input_id;
    param[0].buffer_length = 0;
    param[0].is_null = 0;
    param[0].length = 0;
    // bind[0].is_unsigned = 0;
  }
  {
    //num
    param[1].buffer_type = MYSQL_TYPE_LONG;
    param[1].buffer = (char*)&input_num;
    param[1].buffer_length = 0;
    param[1].is_null = 0;
    param[1].length = 0;
    // bind[1].is_unsigned = 0;
  }
  {
    //name
    str_len = strlen(input_name);
    my_log("str_len for name is %d", str_len);
    param[2].buffer_type = MYSQL_TYPE_STRING;
    param[2].buffer = (char*)input_name;
    param[2].buffer_length = str_len;
    param[2].is_null = 0;
    param[2].length = &str_len;
    // bind[2].is_unsigned = 0;
  }
  if (mysql_stmt_bind_param(stmt, param)) {
    my_log("bind failed:%s", mysql_stmt_error(stmt));
    return -1;
  }
  unsigned int iteration_count = 0;
  // mysql_stmt_attr_set(stmt, STMT_ATTR_ITERRATION_COUNT, &iteration_count);
  //stmt->iteration_count = 0;
  if (ret = mysql_stmt_execute_v2(stmt, pStatement, strlen(pStatement), iteration_count, 0, 0)) {
    my_log("mysql_stmt_execute_v2 select failed:%s", mysql_stmt_error(stmt));
    ASSERT_EQ(ret, 0, "mysql_stmt_execute_v2");
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

  if (ret = mysql_stmt_bind_result(stmt, bind)) {
    my_log("mysql_stmt_bind_result failed:%s", mysql_stmt_error(stmt));
    ASSERT_EQ(ret, 0, "mysql_stmt_bind_result");
  }
  while (mysql_stmt_fetch(stmt) == 0) {
    my_log("%d\t%d\t%s\t%s", id, num, name, birthday);
  }
  if (ret = mysql_stmt_close(stmt)) {
    my_log("mysql_stmt_close faled:%s", mysql_stmt_error(stmt));
  }
  ASSERT_EQ(0, 0, "stmt_select_test_with_param_no_result");
  return 0;
}
int stmt_select_test_no_param_with_result(MYSQL* mysql) {
  my_log("stmt_select_test");
  const char* pStatement = "select * from test_new_ps";
  MYSQL_STMT    *stmt;
  MYSQL_BIND    bind[4];
  int           ret = 0;
  int           param_count;
  int           filed_count;
  my_ulonglong  affected_rows;
  int           i = 0;
  char          query[1000] = {0};
  snprintf(query, 1000, "delete from test_new_ps");
  if (ret = mysql_real_query(mysql, query, strlen(query))) {
    my_log("mysql_real_query failed:%s", mysql_error(mysql));
    ASSERT_EQ(ret, 0, "mysql_real_query");
  }
  for (i = 0; i < 10; i++)
  {
    snprintf(query, 1000, "insert into test_new_ps(id, num, name, birthday) values(%d,%d, 'name_%d', 'birth_%d')", i ,i, i, i);
    if (ret = mysql_real_query(mysql, query, strlen(query))) {
      my_log("mysql_real_query failed:%s", mysql_error(mysql));
      ASSERT_EQ(ret, 0, "mysql_real_query");
    }
  }
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
  if (mysql_stmt_execute_v2(stmt, pStatement, strlen(pStatement), iteration_count, 0, 0)) {
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

  if (ret = mysql_stmt_bind_result(stmt, bind)) {
    my_log("mysql_stmt_bind_result failed:%s", mysql_stmt_error(stmt));
    ASSERT_EQ(ret, 0, "mysql_stmt_bind_result");
  }
  i = 0;
  while (mysql_stmt_fetch(stmt) == 0) {
    i++;
    printf("%d\t%d\t%s\t%s\n", id, num, name, birthday);
  }
  ASSERT_EQ(10, i, "mysql_stmt_fetch");
  if (ret = mysql_stmt_close(stmt)) {
    my_log("mysql_stmt_close faled:%s", mysql_stmt_error(stmt));
  }
  ASSERT_EQ(0, 0, "stmt_select_test_no_param_with_result");
  return 0;
}
int stmt_select_test_with_param_with_result(MYSQL* mysql) {
  my_log("stmt_select_test_with_param_with_result");
  const char* pStatement = "select * from test_new_ps where id = ? and num = ? and name = ?";
  MYSQL_STMT    *stmt;
  MYSQL_BIND    bind[4];
  MYSQL_BIND    param[4];
  int           ret = 0;
  int           param_count;
  int           filed_count;
  int           i = 0;
  my_ulonglong  affected_rows;
  char query    [400];
  snprintf(query, 400, "delete from test_new_ps");
  if (ret = mysql_real_query(mysql, query, strlen(query))) {
    my_log("mysql_real_query failed:%s", mysql_error(mysql));
    ASSERT_EQ(ret, 0, "mysql_real_query");
  }
  for (i = 0; i < 10; i++)
  {
    snprintf(query, 1000, "insert into test_new_ps(id, num, name, birthday) values(%d,%d, 'name_%d', 'birth_%d')", i ,i, i, i);
    if (ret = mysql_real_query(mysql, query, strlen(query))) {
      my_log("mysql_real_query failed:%s", mysql_error(mysql));
      ASSERT_EQ(ret, 0, "mysql_real_query");
    }
  }
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
  int input_id = 0, input_num = 0;
  char input_name[20] = "name_0";
  unsigned long str_len;
  {
    //id
    param[0].buffer_type = MYSQL_TYPE_LONG;
    param[0].buffer = (char*)&input_id;
    param[0].buffer_length = 0;
    param[0].is_null = 0;
    param[0].length = 0;
    // bind[0].is_unsigned = 0;
  }
  {
    //num
    param[1].buffer_type = MYSQL_TYPE_LONG;
    param[1].buffer = (char*)&input_num;
    param[1].buffer_length = 0;
    param[1].is_null = 0;
    param[1].length = 0;
    // bind[1].is_unsigned = 0;
  }
  {
    //name
    str_len = strlen(input_name);
    my_log("str_len for name is %d", str_len);
    param[2].buffer_type = MYSQL_TYPE_STRING;
    param[2].buffer = (char*)input_name;
    param[2].buffer_length = str_len;
    param[2].is_null = 0;
    param[2].length = &str_len;
    // bind[2].is_unsigned = 0;
  }
  if (mysql_stmt_bind_param(stmt, param)) {
    my_log("bind failed:%s", mysql_stmt_error(stmt));
    return -1;
  }
  unsigned int iteration_count = 0;
  // mysql_stmt_attr_set(stmt, STMT_ATTR_ITERRATION_COUNT, &iteration_count);
  //stmt->iteration_count = 0;
  if (ret = mysql_stmt_execute_v2(stmt, pStatement, strlen(pStatement), iteration_count, 0, 0)) {
    my_log("mysql_stmt_execute_v2 select failed:%s", mysql_stmt_error(stmt));
    ASSERT_EQ(ret, 0, "mysql_stmt_execute_v2");
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

  if (ret = mysql_stmt_bind_result(stmt, bind)) {
    my_log("mysql_stmt_bind_result failed:%s", mysql_stmt_error(stmt));
    ASSERT_EQ(ret, 0, "mysql_stmt_bind_result");
  }
  while (mysql_stmt_fetch(stmt) == 0) {
    my_log("%d\t%d\t%s\t%s", id, num, name, birthday);
  }
  if (ret = mysql_stmt_close(stmt)) {
    my_log("mysql_stmt_close faled:%s", mysql_stmt_error(stmt));
  }
  ASSERT_EQ(0, 0, "stmt_select_test_with_param_with_result");
  return 0;
}
int stmt_select_test_no_param_no_result_exact_not_match(MYSQL* mysql) {
  my_log("stmt_select_test_no_param_no_result");
  const char* pStatement = "select * from test_new_ps";
  MYSQL_STMT    *stmt;
  MYSQL_BIND    bind[4];
  int           ret = 0;
  int           param_count;
  int           filed_count;
  my_ulonglong  affected_rows;
  const char* query = "delete from test_new_ps";
  if (ret = mysql_real_query(mysql, query, strlen(query))) {
    my_log("mysql_real_query failed:%s", mysql_error(mysql));
    ASSERT_EQ(ret, 0, "mysql_real_query");
  }
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
  unsigned int iteration_count = 2;
  // mysql_stmt_attr_set(stmt, STMT_ATTR_ITERRATION_COUNT, &iteration_count);
  //stmt->iteration_count = 0;
  if (ret = mysql_stmt_execute_v2(stmt, pStatement, strlen(pStatement), iteration_count, 0x00000002, 0)) {
    my_log("mysql_stmt_execute_v2 select failed:%s", mysql_stmt_error(stmt));
    ASSERT_EQ(1, ret, "mysql_stmt_execute_v2");
    return -1;
  } else {
    ASSERT_EQ(-1, ret, "mysql_stmt_execute_v2");
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

  if (ret = mysql_stmt_bind_result(stmt, bind)) {
    my_log("mysql_stmt_bind_result failed:%s", mysql_stmt_error(stmt));
    ASSERT_EQ(ret, 0, "mysql_stmt_bind_result");
  }
  while (mysql_stmt_fetch(stmt) == 0) {
    printf("%d\t%d\t%s\t%s\n", id, num, name, birthday);
  }
  if (ret = mysql_stmt_close(stmt)) {
    my_log("mysql_stmt_close faled:%s", mysql_stmt_error(stmt));
  }
  ASSERT_EQ(-1, 0, "stmt_select_test_no_param_no_result");
  return 0;
}
int stmt_select_test_no_param_with_result_exact_not_match(MYSQL* mysql) {
  my_log("stmt_select_test_no_param_with_result_exact_not_match");
  const char* pStatement = "select * from test_new_ps";
  MYSQL_STMT    *stmt;
  MYSQL_BIND    bind[4];
  int           ret = 0;
  int           param_count = 0;
  int           filed_count = 0;
  my_ulonglong  affected_rows;
  int           i = 0;
  char          query[1000] = {0};
  snprintf(query, 1000, "delete from test_new_ps");
  if (ret = mysql_real_query(mysql, query, strlen(query))) {
    my_log("mysql_real_query failed:%s", mysql_error(mysql));
    ASSERT_EQ(ret, 0, "mysql_real_query");
  }
  for (i = 0; i < 10; i++)
  {
    snprintf(query, 1000, "insert into test_new_ps(id, num, name, birthday) values(%d,%d, 'name_%d', 'birth_%d')", i ,i, i, i);
    if (ret = mysql_real_query(mysql, query, strlen(query))) {
      my_log("mysql_real_query failed:%s", mysql_error(mysql));
      ASSERT_EQ(ret, 0, "mysql_real_query");
    }
  }
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
  unsigned int iteration_count = 2;
  // mysql_stmt_attr_set(stmt, STMT_ATTR_ITERRATION_COUNT, &iteration_count);
  //stmt->iteration_count = 0;
  if (ret = mysql_stmt_execute_v2(stmt, pStatement, strlen(pStatement), iteration_count, 0x00000002, 0)) {
    my_log("mysql_stmt_execute_v2 select failed:%s", mysql_stmt_error(stmt));
    ASSERT_EQ(1, ret, "mysql_stmt_execute_v2 with 2");
  } else {
    ASSERT_EQ(1, ret, "mysql_stmt_execute_v2 with 2");
  }
  filed_count = mysql_stmt_field_count(stmt);
  my_log("filed_count is %u", filed_count);
  param_count = mysql_stmt_param_count(stmt);
  my_log("param_count is %d", param_count);
  ASSERT_EQ(filed_count, 4, "mysql_stmt_field_count");
  if (ret = mysql_stmt_execute_v2(stmt, pStatement, strlen(pStatement), 12, 0x00000002, 0)) {
    my_log("mysql_stmt_execute_v2 select failed:%s", mysql_stmt_error(stmt));
    ASSERT_EQ(1, ret, "mysql_stmt_execute_v2 with 12");
  } else {
    ASSERT_EQ(-1, ret, "mysql_stmt_execute_v2 with 12");
  }
  if (ret = mysql_stmt_execute_v2(stmt, pStatement, strlen(pStatement), 10, 0x00000002, 0)) {
    my_log("mysql_stmt_execute_v2 select failed:%s", mysql_stmt_error(stmt));
    ASSERT_EQ(0, ret, "mysql_stmt_execute_v2 with 10");
  } else {
    ASSERT_EQ(0, ret, "mysql_stmt_execute_v2 with 10");
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

  if (ret = mysql_stmt_bind_result(stmt, bind)) {
    my_log("mysql_stmt_bind_result failed:%s", mysql_stmt_error(stmt));
    ASSERT_EQ(ret, 0, "mysql_stmt_bind_result");
  }
  i = 0;
  while (mysql_stmt_fetch(stmt) == 0) {
    i++;
    printf("%d\t%d\t%s\t%s\n", id, num, name, birthday);
  }
  ASSERT_EQ(10, i, "mysql_stmt_fetch");
  if (ret = mysql_stmt_close(stmt)) {
    my_log("mysql_stmt_close faled:%s", mysql_stmt_error(stmt));
  }
  ASSERT_EQ(0, 0, "stmt_select_test_no_param_with_result_exact_not_match");
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
  ret = mysql_query(mysql, "create table test_new_ps(id int, num int, name varchar2(20), birthday varchar2(20))");
  if (ret != 0)
  {
    my_log("create table error, %s\n", mysql_error(mysql));
    return ret;
  }
  stmt_select_test_no_param_no_result(mysql);
  stmt_select_test_no_param_with_result(mysql);
  stmt_select_test_with_param_no_result(mysql);
  stmt_select_test_with_param_with_result(mysql);
  stmt_select_test_no_param_no_result_exact_not_match(mysql);
  stmt_select_test_no_param_with_result_exact_not_match(mysql);
  return 0;
}