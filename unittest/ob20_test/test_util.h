#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
const char* DBHOST = "";
const char* DBUSER = "";
const char* DBPASS = "";
unsigned int   DBPORT = 0;
const char* DBNAME = "SYS";
const char* DBSOCK = NULL; //"/var/lib/mysql/mysql.sock"
const unsigned long   DBPCNT = 0;
#ifdef __cplusplus
extern "C" {
#endif
enum DB_MODE
{
  OB_MYSQL_MODE = 0,
  OB_ORACLE_MODE = 1
};
void init_conn_info(int mode)
{
  const char* env_name = (mode == OB_ORACLE_MODE) ? "OB_ORACLE_SERVER_USERNAME": "OB_MYSQL_SERVER_USERNAME";
  const char* tmp = getenv(env_name);
  if (tmp != NULL)
  {
    DBUSER = tmp;
  }
  env_name = (mode == OB_ORACLE_MODE) ? "OB_ORACLE_SERVER_PASSWORD" : "OB_MYSQL_SERVER_PASSWORD";
  tmp = getenv(env_name);
  if (tmp != NULL)
  {
    DBPASS = tmp;
  }
  env_name = (mode == OB_ORACLE_MODE) ? "OB_ORACLE_SERVER_HOST" : "OB_MYSQL_SERVER_HOST";
  tmp = getenv(env_name);
  if (tmp != NULL)
  {
    DBHOST = tmp;
  }
  env_name = (mode == OB_ORACLE_MODE) ? "OB_ORACLE_SERVER_PORT" : "OB_MYSQL_SERVER_PORT";
  tmp = getenv(env_name);
  if (tmp != NULL)
  {
    DBPORT = atoi(tmp);
  }
  env_name = (mode == OB_ORACLE_MODE) ? "OB_ORACLE_SERVER_DBNAME" : "OB_MYSQL_SERVER_DBNAME";
  tmp = getenv(env_name);
  if (tmp != NULL)
  {
    DBNAME = tmp;
  }
}
const char * getRelativeFileName(const char* file_name)
{
  int j = 0;
  size_t len = strlen(file_name);
  if (len <= 2) {
    printf("too short \n");
    return file_name;
  }
  const char *ptr = file_name;
  for (j = len - 2; j >=0; j--) {
    if (ptr[j] == '/')
    {
      break;
    }
  }
  return &ptr[j+1];
}
#define ASSERT_EQ(a, b, info) assert_internal(__FILE__, __LINE__, __FUNCTION__, a, b, info)
void assert_internal(const char* file, int lineno, const char* func, int a, int b, const char* info)
{
  if(a == b){
    printf("[%s:%s:%d]: %-40s ------- \e[1;0;32;1m PASS \e[0m\n", getRelativeFileName(file), func, lineno, info);
  }
  else {
    printf("[%s:%s:%d]: %-40s------- \e[1;0;31;1m FAILED \e[0m, expected:%d,real:%d\n",
      getRelativeFileName(file), func, lineno, info, a, b);
  }
}
#define my_log(fmt, ...) \
  my_log_internal(stdout, __FILE__, __LINE__,  fmt, ##__VA_ARGS__)

static void my_log_internal(FILE* fp, const char* file_name, int lineno, const char* fmt, ...) {
  time_t now = time(NULL);
  struct tm* local;
  local = localtime(&now);
  fprintf(fp, "[%04d-%02d-%02d %02d:%02d:%02d][%s:%d] ", 1900 + local->tm_year, local->tm_mon + 1,
          local->tm_mday, local->tm_hour, local->tm_min, local->tm_sec, getRelativeFileName(file_name), lineno);
  va_list ap;
  va_start(ap, fmt);
  vfprintf(fp, fmt, ap);
  fprintf(fp, "\n");
  va_end(ap);
  fflush(fp);
}
#ifdef __cplusplus
}
#endif
