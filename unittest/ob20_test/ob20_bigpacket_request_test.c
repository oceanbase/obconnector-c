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
  char *big_query;
  size_t big_query_size = 256 * 256 * 256 - 1;   // 16M请求
  
  // init_conn_info(OB_MYSQL_MODE);
  // mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
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

  // malloc 16M size query
  big_query = (char *)malloc(big_query_size);
  if (NULL == big_query) {
    printf("malloc error\n");
    return 0;
  } else {
    memset(big_query, 0, big_query_size);
    memcpy(big_query, "select 1 from dual where 1 = 0;", sizeof("select 1 from dual where 1 = 0;"));
  }

  {
    if (mysql_real_query(mysql, big_query, big_query_size)) {
      printf("query one error, %d, %s\n", mysql_errno(mysql), mysql_error(mysql));
      return 0;
    } else {
      RES = mysql_store_result(mysql);
    }
  }

  mysql_library_end();
  return 0;
}
