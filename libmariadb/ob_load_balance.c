#include <string.h>
#include <stdlib.h>
#if defined(_WIN32)
#include <windows.h>
#include <wincrypt.h>
#else
#include <sys/time.h>
#endif

#include "mysql.h"
#include "ob_rwlock.h"
#include "ma_global.h"
#include "errmsg.h"
#include "ob_utils.h"
#include "ob_tnsname.h"
#include "ob_load_balance.h"

//#define DEBUG_LOAD_BALANCE 1
#define CONNECT_CNT_INIT 32

typedef enum _enum_ob_lb_address_state
{
  OBCLIENT_LB_BLACK,              // black 
  OBCLIENT_LB_WHITE,              // white 
  OBCLIENT_LB_GRAY,               // gray
  OBCLIENT_LB_MAX_STATE
}ObLbAddressState;

typedef struct _st_ob_lb_address {
  char host[128];
  int port;
  int weight;
  int state;
  int64_t black_time;
  int64_t last_time;
  void *connect_info;
}ObLbAddress;

typedef struct _st_ob_lb_address_list {
  ObClientLBOption option;   //ROTATION,SERVERAFFINITY,RANDOM
  unsigned int count;    //count
  unsigned int rotation_flag;
  ObLbAddress *add_arr;
}ObLbAddressList;

typedef struct _st_connect_info {
  int connect_cnt;
  int connect_max;
  int64_t *connect_list;
}ConnectInfo;

static void init_connect_info(ObLbAddress *address) {
  if (NULL == address->connect_info) {
    ConnectInfo *p = malloc(sizeof(ConnectInfo));
    if (NULL != p) {
      p->connect_cnt = 0;
      p->connect_max = CONNECT_CNT_INIT;
      p->connect_list = malloc(sizeof(int64_t) * p->connect_max);
    }
    address->connect_info = p;
  } else {
    ConnectInfo *p = (ConnectInfo*)(address->connect_info);
    if (p->connect_cnt >= p->connect_max - 1) {
      p->connect_max *= 2;
      p->connect_list = realloc(p->connect_list, sizeof(int64_t) * p->connect_max);
    }
  }
}
static void release_connect_info(ObLbAddress *address) {
  if (NULL != address->connect_info) {
    ConnectInfo *p = (ConnectInfo *)(address->connect_info);
    if (NULL != p->connect_list) {
      free(p->connect_list);
      p->connect_list = NULL;
    }
    free(address->connect_info);
    address->connect_info = NULL;
  }
}
static my_bool check_connect_info(ObLbAddress* address) {
  my_bool rst = 1;
  if (NULL != address->connect_info) {
    ConnectInfo *p = (ConnectInfo *)(address->connect_info);
    rst = NULL != p->connect_list ? 1 : 0;
  } else {
    rst = 0;
  }
  return rst;
}

static void append_ob_address_connect_info(ObLbAddress * address) {
  init_connect_info(address);
  if (check_connect_info(address)) {
    ConnectInfo *p = (ConnectInfo*)(address->connect_info);
    int64_t *tmp = (int64_t*)p->connect_list;
    tmp[p->connect_cnt] = get_current_time_us();
    p->connect_cnt++;
    address->last_time = get_current_time_us();
  } else {
    address->state = OBCLIENT_LB_BLACK;
    address->black_time = get_current_time_us();
  }
}

static ObLbAddress* get_ob_address_by_address_list(ObLbAddressList *addr_list)
{
  ObLbAddress* rst = NULL;
  unsigned int cnt = addr_list->count;
  unsigned int i = 0, j = 0;
  unsigned int weight = 0;

  if (cnt > 0) {
    int *idxarr = calloc(1, sizeof(int) * cnt);
    if (idxarr != NULL) {
      int valid_cnt = 0;
      memset(idxarr, 0, sizeof(int) * cnt);
      for (i = 0; i < cnt; i++) {
        if (addr_list->add_arr[i].weight <= 0) {
          addr_list->add_arr[i].weight = 1;
        }
        if (OBCLIENT_LB_WHITE == addr_list->add_arr[i].state) {
          idxarr[valid_cnt] = i;
          weight += addr_list->add_arr[i].weight;
          valid_cnt++;
        }
      }

      if (valid_cnt == 0) {
        rst = NULL;
      } else if (valid_cnt == 1) {
        rst = &(addr_list->add_arr[idxarr[0]]);
      } else {
        switch (addr_list->option) {
        case OBCLIENT_LB_OPTION_RANDOM: {
          unsigned int random_index = (unsigned int)rand() % valid_cnt;
          rst = &(addr_list->add_arr[idxarr[random_index]]);
          break;
        }
        case OBCLIENT_LB_OPTION_SERVERAFFINITY: {
          int rand_number = (int)rand() % weight + 1;
          int index = 0;
          for (index = 0; index < valid_cnt; ++index) {
            rand_number -= addr_list->add_arr[idxarr[index]].weight;
            if (index == valid_cnt - 1 || rand_number <= 0) {
              rst = &(addr_list->add_arr[idxarr[index]]);
              break;
            }
          }
          break;
        }
        case OBCLIENT_LB_OPTION_ROTATION: {
          for (i = addr_list->rotation_flag%cnt; i < cnt; i++) {
            addr_list->rotation_flag++;
            if (OBCLIENT_LB_WHITE == addr_list->add_arr[i].state) {
              rst = &(addr_list->add_arr[i]);
              addr_list->add_arr[i].last_time = get_current_time_us();
              break;
            }
          }
          break;
        }
        }
      }
      free(idxarr);

      if (NULL != rst) {
        append_ob_address_connect_info(rst);
      }
    }
  }
  return rst;
}

static void check_ob_address_black(ObLbAddress * address, ObClientLbConfig *config) {
  if (!check_connect_info(address))
    address->state = OBCLIENT_LB_BLACK;

  if (OBCLIENT_LB_WHITE == address->state) {
    ConnectInfo *info = (ConnectInfo *)address->connect_info;
    switch (config->black_append_strategy) {
      case OBCLIENT_LB_OPTION_NORMAL: {
        address->state = OBCLIENT_LB_BLACK;
        address->black_time = get_current_time_us();
#ifdef DEBUG_LOAD_BALANCE
        printf("white 2 black: %s, %d\n", address->host, address->port);
#endif
      }
      break;
      case OBCLIENT_LB_OPTION_RETRY_DERUATION: {
        int idx = 0;
        int newsize = 0;
        int len = info->connect_cnt;
        int64_t *list = info->connect_list;
        int64_t curtime = get_current_time_us();
        int64_t duration = config->black_append_duration * 1000;// ��λmsתΪus
        for (idx = 0; idx < len; idx++) {
          if (curtime - list[idx] <= duration) {
            break;
          }
        }
        newsize = len - idx;
        if (0 != idx) {
          int64_t* offset = info->connect_list + idx;
          memmove(info->connect_list, offset, newsize);
        }
        info->connect_cnt = newsize;
        if (info->connect_cnt >= config->black_append_retrytimes) {
          address->state = OBCLIENT_LB_BLACK;
          address->black_time = get_current_time_us();
#ifdef DEBUG_LOAD_BALANCE
          printf("white 2 black: %s, %d\n", address->host, address->port);
#endif
        }
      }
      break;
    }
  }
}

static void check_ob_address_white(ObLbAddressList * addr_list, ObClientLbConfig *config) {
  unsigned int i = 0;
  int64_t curtime = get_current_time_us();
  int64_t timeout = config->black_remove_timeout * 1000;
  ObLbAddress* address = addr_list->add_arr;
  if (timeout > 0) {
    for (i = 0; i < addr_list->count; i++) {
      if (OBCLIENT_LB_BLACK == address[i].state) {
        if (curtime - address[i].black_time >= timeout) {
          address[i].state = OBCLIENT_LB_WHITE;
#ifdef DEBUG_LOAD_BALANCE
          printf("black 2 white: %s, %d\n", address->host, address->port);
#endif
        }
      }
    }
  }
}

static int init_oblb_addresslist(ObLbAddressList* list, ObClientLbAddressList* addrlist) {
  int ret = 0;

  if (!list || !addrlist || !addrlist->address_list || addrlist->address_list_count <= 0) {
    ret = -1;
  } else if (OB_ISNULL(list->add_arr = (ObLbAddress*)malloc(addrlist->address_list_count * sizeof(ObLbAddress)))) {
    ret = -1;
  } else {
    unsigned int i = 0;
    memset(list->add_arr, 0, addrlist->address_list_count * sizeof(ObLbAddress));
    for (i = 0; i < addrlist->address_list_count; i++) {
      memcpy(list->add_arr[i].host, addrlist->address_list[i].host, strlen(addrlist->address_list[i].host));
      list->add_arr[i].port = addrlist->address_list[i].port;
      list->add_arr[i].weight = 1;
      list->add_arr[i].state = OBCLIENT_LB_WHITE;
    }
    list->count = addrlist->address_list_count;
    list->rotation_flag = 0;
    list->option = OBCLIENT_LB_OPTION_ROTATION;
  }
  return ret;
}
static int init_oblb_addresslist_tns(ObLbAddressList* list, ObClientAddressList* addrlist) {
  int ret = 0;
  if (!list || !addrlist || !addrlist->address || addrlist->address_count <= 0) {
    ret = -1;
  } else if (OB_ISNULL(list->add_arr = (ObLbAddress*)malloc(addrlist->address_count * sizeof(ObLbAddress)))){
    ret = -1;
  } else {
    unsigned int i = 0;
    memset(list->add_arr, 0, addrlist->address_count * sizeof(ObLbAddress));
    for (i = 0; i < addrlist->address_count; i++) {
      memcpy(list->add_arr[i].host, addrlist->address[i].host, addrlist->address[i].host_len);
      list->add_arr[i].port = addrlist->address[i].port;
      list->add_arr[i].weight = addrlist->address[i].weight;
      list->add_arr[i].state = OBCLIENT_LB_WHITE;
    }
    list->count = addrlist->address_count;
    list->rotation_flag = 0;
    list->option = addrlist->oblb_strategy;
  }
  return ret;
}

static MYSQL* connect_by_addresslist(MYSQL* mysql, ObLbAddressList *addr_list, ObClientLbConfig *config,
  const char *user, const char *passwd, const char *db, const char *unix_socket, unsigned long client_flag, ObClientLbAddress* success)
{
  ObLbAddress *address = NULL;
  MYSQL* tmp = NULL;
  unsigned int i = 0;
  int err = 0;

  unsigned int mysql_connect_timeout = 0;
  unsigned int mysql_read_timeout = 0;
  unsigned int mysql_write_timeout = 0;
  my_bool mysql_is_local_infile = 0;
  unsigned int mysql_local_infile = 0;
  unsigned int mysql_opt_protocol = 0;
  my_bool mysql_is_proxy_mode = 0;
  my_bool mysql_is_compress = 0;
  my_bool mysql_opt_secure_auth = 0;

  char *mysql_init_command = NULL;
  char *mysql_charset_name = NULL;
  char* mysql_opt_plugin_dir = NULL;
  char* mysql_opt_default_auth = NULL;
  char* mysql_program_name = NULL;
  char* mysql_ob_proxy_user = NULL;
  my_bool mysql_opt_interactive = 0;
  my_bool mysql_report_data_truncation = 0;
  my_bool mysql_opt_reconnect = 0;

  my_bool mysql_opt_use_ssl = 0;
  my_bool mysql_opt_ssl_verify_server_cert = 0;
  char *mysql_opt_ssl_key = NULL;
  char *mysql_opt_ssl_cert = NULL;
  char *mysql_opt_ssl_ca = NULL;
  char *mysql_opt_ssl_capath = NULL;
  char *mysql_opt_ssl_cipher = NULL;
  char *mysql_opt_ssl_crl = NULL;
  char *mysql_opt_ssl_crlpath = NULL;
  char *mysql_opt_tls_version = NULL;

  if (mysql == NULL || addr_list == NULL || config == NULL || addr_list->add_arr == NULL || addr_list->count <= 0) {
    return NULL;
  }

  if (addr_list->option != OBCLIENT_LB_OPTION_RANDOM &&
    addr_list->option != OBCLIENT_LB_OPTION_ROTATION &&
    addr_list->option != OBCLIENT_LB_OPTION_SERVERAFFINITY) {
    addr_list->option = OBCLIENT_LB_OPTION_ROTATION;
  }
  if (config->black_append_strategy != OBCLIENT_LB_OPTION_NORMAL &&
    config->black_append_strategy != OBCLIENT_LB_OPTION_RETRY_DERUATION) {
    config->black_append_strategy = OBCLIENT_LB_OPTION_NORMAL;
  }
  if (config->retry_all_downs <= 0) config->retry_all_downs = addr_list->count;
  if (config->black_append_duration <= 0) config->black_append_duration = 60 * 1000;
  if (config->black_append_retrytimes <= 0) config->black_append_retrytimes = 1;

  for (i = 0; i < addr_list->count; i++) {
    addr_list->add_arr[i].state = OBCLIENT_LB_WHITE;
    addr_list->add_arr[i].connect_info = NULL;
    if (addr_list->add_arr[i].weight <= 0) {
      addr_list->add_arr[i].weight = 1;
    }
    init_connect_info(&(addr_list->add_arr[i]));
  }

  //init mysql options
  mysql_connect_timeout = config->mysql_connect_timeout;
  mysql_read_timeout = config->mysql_read_timeout;
  mysql_write_timeout = config->mysql_write_timeout;
  mysql_is_proxy_mode = config->mysql_is_proxy_mode;
  mysql_init_command = config->mysql_init_command;
  mysql_charset_name = config->mysql_charset_name;
  mysql_is_compress = config->mysql_is_compress;
  mysql_is_local_infile = config->mysql_is_local_infile;
  mysql_local_infile = config->mysql_local_infile;
  mysql_opt_protocol = config->mysql_opt_protocol;
  mysql_opt_plugin_dir = config->mysql_opt_plugin_dir;
  mysql_ob_proxy_user = config->mysql_ob_proxy_user;
  mysql_opt_default_auth = config->mysql_opt_default_auth;
  mysql_program_name = config->mysql_program_name;
  mysql_opt_secure_auth = config->mysql_opt_secure_auth;
  mysql_opt_interactive = config->mysql_opt_interactive;
  mysql_report_data_truncation = config->mysql_report_data_truncation;
  mysql_opt_reconnect = config->mysql_opt_reconnect;

  mysql_opt_use_ssl = config->mysql_opt_use_ssl;
  mysql_opt_ssl_verify_server_cert = config->mysql_opt_ssl_verify_server_cert;
  mysql_opt_ssl_key = config->mysql_opt_ssl_key;
  mysql_opt_ssl_cert = config->mysql_opt_ssl_cert;
  mysql_opt_ssl_ca = config->mysql_opt_ssl_ca;
  mysql_opt_ssl_capath = config->mysql_opt_ssl_capath;
  mysql_opt_ssl_cipher = config->mysql_opt_ssl_cipher;
  mysql_opt_ssl_crl = config->mysql_opt_ssl_crl;
  mysql_opt_ssl_crlpath = config->mysql_opt_ssl_crlpath;
  mysql_opt_tls_version = config->mysql_opt_tls_version;

  do {
    //check address white
    check_ob_address_white(addr_list, config);

    //get address
    if (NULL == (address = get_ob_address_by_address_list(addr_list))) {
      break;
    }

    if (OBCLIENT_LB_WHITE != address->state) {
      continue;
    }

#ifdef DEBUG_LOAD_BALANCE
    printf("host:%s,port:%d,state:%d\n", address->host, address->port, address->state);
#endif

    //mysql options 
    if (mysql_connect_timeout > 0)
      mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, &mysql_connect_timeout);
    if (mysql_read_timeout > 0)
      mysql_options(mysql, MYSQL_OPT_READ_TIMEOUT, &mysql_read_timeout);
    if (mysql_write_timeout > 0)
      mysql_options(mysql, MYSQL_OPT_WRITE_TIMEOUT, &mysql_write_timeout);
    if (mysql_is_proxy_mode)
      mysql->ob_server_version = 0xfe;
    if (mysql_init_command && *mysql_init_command)
      mysql_options(mysql, MYSQL_INIT_COMMAND, mysql_init_command);
    if (mysql_charset_name && *mysql_charset_name)
      mysql_options(mysql, MYSQL_SET_CHARSET_NAME, mysql_charset_name);
    if (mysql_is_compress)
      mysql_options(mysql, MYSQL_OPT_COMPRESS, NullS);
    if (mysql_is_local_infile)
      mysql_options(mysql, MYSQL_OPT_LOCAL_INFILE, (char*)&mysql_local_infile);
    if (mysql_opt_protocol)
      mysql_options(mysql, MYSQL_OPT_PROTOCOL, (char*)&mysql_opt_protocol);
    if (mysql_opt_plugin_dir && *mysql_opt_plugin_dir)
      mysql_options(mysql, MYSQL_PLUGIN_DIR, mysql_opt_plugin_dir);
    if (mysql_opt_default_auth && *mysql_opt_default_auth)
      mysql_options(mysql, MYSQL_DEFAULT_AUTH, mysql_opt_default_auth);
    if (mysql_program_name && *mysql_program_name) {
      mysql_options(mysql, MYSQL_OPT_CONNECT_ATTR_RESET, 0);
      mysql_options4(mysql, MYSQL_OPT_CONNECT_ATTR_ADD, "program_name", mysql_program_name);
    }
    if (mysql_ob_proxy_user && *mysql_ob_proxy_user)
      mysql_options(mysql, OB_OPT_PROXY_USER, mysql_ob_proxy_user);
    if (mysql_opt_secure_auth)
      mysql_options(mysql, MYSQL_SECURE_AUTH, (char *)&mysql_opt_secure_auth);
    if (mysql_opt_interactive)
      mysql_options(mysql, MARIADB_OPT_INTERACTIVE, (char *)&mysql_opt_interactive);
    if (mysql_report_data_truncation)
      mysql_options(mysql, MYSQL_REPORT_DATA_TRUNCATION, (char *)&mysql_report_data_truncation);
    if (mysql_opt_reconnect)
      mysql_options(mysql, MYSQL_OPT_RECONNECT, (char *)&mysql_opt_reconnect);

    if (mysql_opt_use_ssl) {
      mysql_ssl_set(mysql, mysql_opt_ssl_key, mysql_opt_ssl_cert, mysql_opt_ssl_ca, mysql_opt_ssl_capath, mysql_opt_ssl_cipher);
      mysql_options(mysql, MYSQL_OPT_SSL_CRL, mysql_opt_ssl_crl);
      mysql_options(mysql, MYSQL_OPT_SSL_CRLPATH, mysql_opt_ssl_crlpath);
      mysql_options(mysql, MARIADB_OPT_TLS_VERSION, mysql_opt_tls_version);
    }
    if (mysql_opt_ssl_verify_server_cert)
      mysql_options(mysql, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, (char*)&mysql_opt_ssl_verify_server_cert);

    //connect to server
    if (NULL != (tmp = mysql_real_connect(mysql, address->host, user, passwd, db, address->port, unix_socket, client_flag))) {
      //connect success
      if (success) {
        memcpy(success->host, address->host, strlen(address->host));
        success->port = address->port;
      }
      break;
    } else {
      //connect fail, user/passwd is error break;
      err = mysql_errno(mysql);
      if (1045 == err) {
        break;
      }
    }

    //check black
    check_ob_address_black(address, config);

    config->retry_all_downs--;
  } while (address && config->retry_all_downs > 0);

  //release connect
  for (i = 0; i < addr_list->count; i++) {
    release_connect_info(&(addr_list->add_arr[i]));
  }

  return tmp;
}

static MYSQL* connect_by_tnsname(MYSQL* mysql, const char* tns_name, ObClientLbConfig * config,
  const char *user, const char* passwd, const char *db, const char* unix_socket, unsigned long client_flag, ObClientLbAddress *success)
{
  int ret = 0;
  MYSQL *tmp = NULL;
  my_bool find = 0;
  ObClientTns tns;
  const char *sid = db;
  char new_user_name[256] = { 0 };
  int user_len = strlen((const char *)user ? user : "");

  if (OB_FAIL(ObClientTnsInit(&tns))) {
    tmp = NULL;
  } else if (OB_FAIL(ObClientTnsBuild(&tns, tns_name, strlen(tns_name), &find))) {
    tmp = NULL;
  } else if (1 != find || !tns.tns_service || !tns.tns_service->description || tns.tns_service->description_count == 0) {
    tmp = NULL;
  } else {
    unsigned int i = 0;
    ObClientDescription *des = tns.tns_service->description;
#ifdef DEBUG_LOAD_BALANCE
    ObClientTnsDisplay(&tns);
#endif
    
    strncpy(new_user_name, (const char *)user ? user : "", user_len);

    if (des && des->connect_data) {
      if (!des->connect_data->use_default_sid) {
        sid = des->connect_data->service_name;
      }
      if (des->connect_data->user_extra_info_len > 0) {
        strncpy(new_user_name + user_len, (const char *)des->connect_data->user_extra_info, des->connect_data->user_extra_info_len);
      }
    }

    //tns config
    config->retry_all_downs = des->retry_all_downs;
    config->black_remove_strategy = des->black_list_conf.remove_strategy;
    config->black_remove_timeout = des->black_list_conf.remove_timeout;
    config->black_append_strategy = des->black_list_conf.append_strategy;
    config->black_append_duration = des->black_list_conf.duration;
    config->black_append_retrytimes = des->black_list_conf.retry_times;

    for (i = 0; i < des->address_list_count; i++) {
      ObLbAddressList list;
      memset(&list, 0, sizeof(list));
#ifdef DEBUG_LOAD_BALANCE
      printf("------------------------------------------\n");
#endif

      if (OB_SUCC(init_oblb_addresslist_tns(&list, &(des->address_list[i])))) {
        tmp = connect_by_addresslist(mysql, &list, config, new_user_name, passwd, sid, unix_socket, client_flag, success);
      }
      if (list.add_arr) {
        free(list.add_arr);
        list.add_arr = NULL;
        list.count = 0;
      }
      if (tmp) {
        break;
      }
    }
  }
  ObClientTnsClear(&tns);
  return tmp;
}

MYSQL* ob_mysql_real_connect(MYSQL* mysql, const char* tns_name, ObClientLbAddressList *addr_list, ObClientLbConfig *config,
  const char *user, const char *passwd, const char *db, const char *unix_socket, unsigned long client_flag, ObClientLbAddress* success)
{
  int ret = 0;
  MYSQL *tmp = NULL;

  if (tns_name && *tns_name) {
    tmp = connect_by_tnsname(mysql, tns_name, config, user, passwd, db, unix_socket, client_flag, success);
  } else {
    ObLbAddressList list;
    memset(&list, 0, sizeof(list));
    if (OB_SUCC(init_oblb_addresslist(&list, addr_list))) {
      tmp = connect_by_addresslist(mysql, &list, config, user, passwd, db, unix_socket, client_flag, success);
    }
    if (list.add_arr) {
      free(list.add_arr);
      list.add_arr = NULL;
      list.count = 0;
    }
  }

  return tmp;
}
