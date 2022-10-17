#include <stdio.h>
#include <string.h>
#include "mysql.h"
#include "test_util.h"
#include "ob_protocol20.h"

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
  int ret = 0;

  {
    const char *module = "mod";
    const char *action = "act";
    flt_set_module(mysql, module);
    flt_set_action(mysql, action);
    if (mysql_query(mysql, "select 1 from dual where 1 = 0;")) {
      printf("query one error\n");
      return 0;
    } else {
      RES = mysql_store_result(mysql);
    }
  }

  {
    // const char *module = "module";
    const char *action = "act1";
    // flt_set_module(mysql, module);
    flt_set_action(mysql, action);
    if (mysql_query(mysql, "select 1 from dual where 1 = 0;")) {
      printf("query two error\n");
      return 0;
    } else {
      RES = mysql_store_result(mysql);
    }
  }

  {
    const char *identifier = "id1";
    flt_set_identifier(mysql, identifier);
    if (mysql_query(mysql, "select 1 from dual where 1 = 0;")) {
      printf("query four error\n");
      return 0;
    } else {
      RES = mysql_store_result(mysql);
    }
  }

  mysql_library_end();
  return 0;
}
