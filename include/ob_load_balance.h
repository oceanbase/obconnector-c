#ifndef _OB_LOAD_BALANCE_H_
#define _OB_LOAD_BALANCE_H_
#include "mysql.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _st_obclient_lb_Address {
  char host[128];
  unsigned int port;
}ObClientLbAddress;

typedef struct _st_obclient_lb_AddressList {
  ObClientLbAddress *address_list;
  unsigned int address_list_count;
}ObClientLbAddressList;

typedef struct _st_obclient_lb_config{
  int retry_all_downs;        //总的次数
  int black_remove_strategy;    //2001
  int black_remove_timeout;     //移出黑名单时间
  int black_append_strategy;    //3002 NORMAL, 3001 RETRYDURATION
  int black_append_retrytimes;  //单个address次数
  int black_append_duration;    //时间duration内，执行>= retrytimes次加黑

  unsigned int mysql_connect_timeout;
  unsigned int mysql_read_timeout;
  unsigned int mysql_write_timeout;
  my_bool mysql_is_local_infile;
  unsigned int mysql_local_infile;
  unsigned int mysql_opt_protocol;
  my_bool mysql_is_proxy_mode;
  my_bool mysql_is_compress;
  my_bool mysql_opt_secure_auth;
  char* mysql_init_command;
  char* mysql_charset_name;
  char* mysql_opt_plugin_dir;
  char* mysql_opt_default_auth;
  char* mysql_program_name;
  char* mysql_ob_proxy_user;
  my_bool mysql_opt_interactive;
  my_bool mysql_report_data_truncation;
  my_bool mysql_opt_reconnect;

  my_bool mysql_opt_use_ssl;
  my_bool mysql_opt_ssl_verify_server_cert;
  char *mysql_opt_ssl_key;
  char *mysql_opt_ssl_cert;
  char *mysql_opt_ssl_ca;
  char *mysql_opt_ssl_capath;
  char *mysql_opt_ssl_cipher;
  char *mysql_opt_ssl_crl;
  char *mysql_opt_ssl_crlpath;
  char *mysql_opt_tls_version;
}ObClientLbConfig;


MYSQL* ob_mysql_real_connect(MYSQL* mysql, const char* tns_name, ObClientLbAddressList *addr_list, ObClientLbConfig *config,
  const char *user, const char *passwd, const char *db, const char *unix_socket, unsigned long client_flag, ObClientLbAddress* success);

#ifdef __cplusplus
}
#endif

#endif
