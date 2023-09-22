#include <stdio.h>
#include <string.h>
#include "mysql.h"
#include "test_util.h"
#include "ob_object.h"
#include "ob_protocol20.h"
#include "ob_full_link_trace.h"

int main(int argc, char** argv) {
  mysql_library_init(0, NULL, NULL);
  MYSQL *mysql = mysql_init(NULL);
  MYSQL_RES *RES;
  unsigned int timeout = 3000;
  const char *enable_show_trace = "set ob_enable_show_trace = true;";
  const char *disable_show_trace = "set ob_enable_show_trace = false;";
  if (6 == argc) {
    DBHOST = argv[1];
    DBUSER = argv[2];
    DBPASS = argv[3];
    DBNAME = argv[4];
    DBPORT = atoi(argv[5]);

    printf("host is %s, user is %s, pass is %s, name is %s, port is %u\n", DBHOST, DBUSER, DBPASS, DBNAME, DBPORT);
  }

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
  if (mysql_query(mysql, enable_show_trace)) {
    printf("query error:%s\n", mysql_error(mysql));
    return 0;
  } else {
    RES = mysql_store_result(mysql);
  }

  if (mysql_query(mysql, "drop table if exists test_trace;")) {
    printf("query error:%s\n", mysql_error(mysql));
    return 0;
  } else {
    RES = mysql_store_result(mysql);
  }

  if (mysql_query(mysql, "create table test_trace(c1 number);")) {
    printf("query error:%s\n", mysql_error(mysql));
    return 0;
  } else {
    RES = mysql_store_result(mysql);
  }
  if (mysql_query(mysql, "insert into test_trace values (1);")) {
    printf("query error:%s\n", mysql_error(mysql));
    return 0;
  } else {
    RES = mysql_store_result(mysql);
  }
  if (mysql_query(mysql, "commit")) {
    printf("query error:%s\n", mysql_error(mysql));
    return 0;
  } else {
    RES = mysql_store_result(mysql);
  }
  if (mysql_query(mysql, disable_show_trace)) {
    printf("query error:%s\n", mysql_error(mysql));
    return 0;
  } else {
    RES = mysql_store_result(mysql);
  }
  

  mysql_close(mysql);
  mysql_library_end();
  return 0;
}
