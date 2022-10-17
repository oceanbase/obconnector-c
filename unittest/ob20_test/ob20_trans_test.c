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

      if (mysql_query(mysql, "create table test_trace(c1 number);")) {
        printf("query 0 error\n");
        return 0;
      } else {
        RES = mysql_store_result(mysql);
      }

  {
    int i = 0;
    for (; i < 20; ++i) {
      if (mysql_query(mysql, "insert into test_trace values (1);")) {
        printf("query one error\n");
        return 0;
      } else {
        RES = mysql_store_result(mysql);
      }
    }
  }

  {
    if (mysql_query(mysql, "commit")) {
      printf("commit error\n");
      return 0;
    } else {
      RES = mysql_store_result(mysql);
    }
  }
      if (mysql_query(mysql, "drop table test_trace;")) {
        printf("query one error\n");
        return 0;
      } else {
        RES = mysql_store_result(mysql);
      }

  mysql_close(mysql);
  mysql_library_end();
  return 0;
}
