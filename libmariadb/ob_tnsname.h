#ifndef OCI_TNSNAME_H_
#define OCI_TNSNAME_H_
#include "mysql.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define OBCLIENT_TNS_KEY_SIZE    256
#define OBCLIENT_TNS_HOST_BUFFER_SIZE OBCLIENT_TNS_KEY_SIZE   // 可能会有域名的形式，host很长
#define OBCLIENT_TNS_PORT_BUFFER_SIZE 6  // 65535
#define OBCLIENT_TNS_BUFFER_SIZE 1024
#define OBCLIENT_TNS_MEMORY_COUNT 6

typedef struct obclient_tns               ObClientTns;
typedef struct obclient_tns_service       ObClientTnsService;
typedef struct obclient_description_list  ObClientDescriptionList;
typedef struct obclient_description       ObClientDescription;
typedef struct obclient_address_list      ObClientAddressList;
typedef struct obclient_address           ObClientAddress;
typedef struct obclient_connect_data      ObClientConnectData;
typedef struct obclient_black_list_conf   ObClientBlacklistConf;
typedef struct obclient_system_variables  ObClientSystemVariables;
typedef struct obclient_tns_parse_params  ObClientTnsParseParams;

typedef enum enum_obclient_tns_parse_mode {
  TNS_MODE_FILE = 1,
  TNS_MODE_DESCRIPTION
}ObClientTnsParseMode;

typedef enum obclient_tns_token_type
{
  TNS_EQUAL_SIGN,
  TNS_LEFT_PARENTHESES,
  TNS_RIGHT_PARENTHESES,
  TNS_KEY,
  TNS_ERROR_TYPE,
  TNS_MAX_TOKEN_TYPE
} ObClientTnsTokenType;

typedef enum obclient_tns_parse_status
{
  TNS_PARSE_NORMAL,
  TNS_PARSE_QUOTE
} ObClientTnsParseStatus;

// TNS File中的所有关键字, 解析TNS File时使用
typedef enum enum_obclient_lb_key_type
{
  OBCLIENT_LB_ERROR_KEY,
  OBCLIENT_LB_DESCRIPTION,
  OBCLIENT_LB_OBLB,
  OBCLIENT_LB_CONNECT_TIMEOUT,
  OBCLIENT_LB_READ_TIMEOUT,
  OBCLIENT_LB_WRITE_TIMEOUT,
  OBCLIENT_LB_OBLB_GROUP_STRATEGY,
  OBCLIENT_LB_OBLB_RETRY_ALL_DOWNS,
  OBCLIENT_LB_OBLB_RETRY_TIMEOUT,
  OBCLIENT_LB_OBLB_BLACKLIST,
  OBCLIENT_LB_REMOVE_STRATEGY,
  OBCLIENT_LB_NAME,
  OBCLIENT_LB_TIMEOUT,
  OBCLIENT_LB_APPEND_STRATEGY,
  OBCLIENT_LB_RETRY_TIMES,
  OBCLIENT_LB_DRUATION,
  OBCLIENT_LB_ADDRESS_LIST,
  OBCLIENT_LB_OBLB_STRATEGY,
  OBCLIENT_LB_ADDRESS,
  OBCLIENT_LB_PROTOCOL,
  OBCLIENT_LB_HOST,
  OBCLIENT_LB_PORT,
  OBCLIENT_LB_WEIGHT,
  OBCLIENT_LB_CONNECT_DATA,
  OBCLIENT_LB_SERVICE_NAME,
  OBCLIENT_LB_SID,
  OBCLIENT_LB_EXTRA_INFO,
  OBCLIENT_LB_OB_MODE,
  OBCLIENT_LB_USE_DEFAULT_SID,
  OBCLIENT_LB_SESSION_VARIABLE,
  OBCLIENT_LB_MAX_KEY_TYPE
} ObClientLBKeyType;

/*
0 - 1000    表示group LB策略
1001 - 2000 表示每个组内LB的策略
2001 - 3000 表示黑名单删除策略
3001 - 4000 表示黑名单加入策略
*/
typedef enum enum_obclient_lb_option
{
  // GROUP
  OBCLIENT_LB_OPTION_GROUP_ROTATION = 0,     // 组内轮询
  // INTERNEL
  OBCLIENT_LB_OPTION_RANDOM = 1001,          // 随机
  OBCLIENT_LB_OPTION_SERVERAFFINITY,         // 加权随机
  OBCLIENT_LB_OPTION_ROTATION,               // 轮询
  // BLACK LIST DELETE OPTION
  OBCLIENT_LB_OPTION_TIMEOUT = 2001,         // 超时删除
  // BLACK LIST INSERT OPTION
  OBCLIENT_LB_OPTION_RETRY_DERUATION = 3001, // 时间段内失败多少次加入黑名单
  OBCLIENT_LB_OPTION_NORMAL = 3002,          // 直接拉黑
  OBCLIENT_LB_OPTION_MAX
} ObClientLBOption;

struct obclient_system_variables
{
  char  session_variable[OBCLIENT_TNS_KEY_SIZE + 1];
  unsigned long   session_variable_len;
};

struct obclient_tns
{
  ObClientTnsService *tns_service;
  unsigned long       tns_service_count;
};

struct obclient_tns_service
{
  ObClientDescription  *description;
  unsigned long         description_count;
  unsigned long         description_memory;
  ObClientSystemVariables   sys_vars;
  unsigned long     net_service_name_len;
  char              net_service_name[OBCLIENT_TNS_BUFFER_SIZE];
};

struct obclient_black_list_conf
{
  ObClientLBOption      remove_strategy;
  unsigned long         remove_timeout;
  ObClientLBOption      append_strategy;
  unsigned long         retry_times;
  unsigned long         duration;
};

struct obclient_description
{
  ObClientAddressList  *address_list;
  unsigned long        address_list_count;
  unsigned long        address_list_memory;
  ObClientConnectData  *connect_data;
  // more member
  my_bool               oblb;               // TRUE on/ FALSE off
  unsigned long         retry_all_downs;
  unsigned long         connect_timout;     // 单次连接的超时时间
  unsigned long         read_timout;        // 单次读的超时时间
  unsigned long         write_timout;       // 单次写的超时时间
  unsigned long         retry_timeout;      // 整个LB阶段的超时时间
  ObClientLBOption      oblb_strategy;
  ObClientLBOption      oblb_group_strategy;
  ObClientBlacklistConf black_list_conf;
};

struct obclient_address_list
{
  ObClientAddress      *address;
  unsigned long        address_count;
  unsigned long        address_memory;
  my_bool              oblb;               // TRUE on/ FALSE off
  ObClientLBOption     oblb_strategy;
};

struct obclient_address
{
  char          host[OBCLIENT_TNS_HOST_BUFFER_SIZE + 1];     // 长度加一为了调用mysql接口时加上结尾0
  char          protocol[OBCLIENT_TNS_PORT_BUFFER_SIZE];
  unsigned long host_len;
  unsigned long protocol_len;
  unsigned long port;
  unsigned long weight;
};

struct obclient_connect_data
{
  char service_name[OBCLIENT_TNS_KEY_SIZE + 1];   // 长度加一为了调用mysql接口时加上结尾0
  unsigned int  service_name_len;
  char user_extra_info[OBCLIENT_TNS_KEY_SIZE];
  unsigned int  user_extra_info_len;
  char session_variable[OBCLIENT_TNS_KEY_SIZE];
  unsigned int  session_variable_len;
  unsigned long  ob_mode;
  unsigned long  use_default_sid;
};

struct obclient_tns_parse_params
{
  char  tns_buffer[OBCLIENT_TNS_BUFFER_SIZE];
  char  tns_key[OBCLIENT_TNS_KEY_SIZE];
  unsigned int   buffer_pos;
  unsigned int   buffer_len;
  unsigned int   tns_key_len;
  int   is_eof;
  int   mode;  //1=file，2=description
  char *tns_name;
  FILE *tns_file;
  char *description;
  int description_len;
  int description_offset;
  int is_comment;
  ObClientTnsTokenType  tns_token_type;
  ObClientLBKeyType     key_type;
};


int ObClientTnsInit(ObClientTns *tns);
int ObClientTnsClear(ObClientTns *tns);
int ObClientTnsDisplay(ObClientTns *tns);
int ObClientTnsBuild(ObClientTns *tns, const char *dbname, unsigned int dbname_len, my_bool *find);
int ObClientTnsBuildDes(ObClientTns *tns, const char *dbname, unsigned int dbname_len, my_bool *find);

int ObClientTnsServiceCheck(ObClientTnsService *tns_service);
int ObClientTnsServiceInit(ObClientTnsService *tns_service, unsigned int name_size, const char *name);
int ObClientTnsServiceClear(ObClientTnsService *tns_service);
int ObClientTnsServiceDisplay(ObClientTnsService *tns_service, FILE *display_file);
int ObClientTnsServiceBuild(ObClientTns *tns, ObClientTnsParseParams *parse_params);
int ObClientTnsServiceSkip(ObClientTnsParseParams *parse_params);
// 非负载均衡模式下获取extra info
int ObClientTnsServiceExtraInfoGet(ObClientTnsService *tns_service, char *extra_info, int *extra_info_len, int *ob_mode, char* session_variable, int *session_variable_len);
// 非负载均衡模式下获取dblink, 直接获取第一个address，拼接dblink
int ObClientTnsServiceDblinkGet(ObClientTnsService *tns_service, char *dblink, int *dblink_len);

int ObClientDescriptionCheck(ObClientDescription *des);
int ObClientDescriptionInit(ObClientDescription *des);
int ObClientDescriptionClear(ObClientDescription *des);
int ObClientDescriptionDisplay(ObClientDescription *des, FILE *display_file);
int ObClientDescriptionBuild(ObClientTnsService *tns, ObClientTnsParseParams *parse_params);

int ObClientBlacklistConfInit(ObClientBlacklistConf *black_list_conf);
int ObClientBlacklistConfBuild(ObClientBlacklistConf *black, ObClientTnsParseParams *parse_params);
int ObClientRemoveStrategyBuild(ObClientBlacklistConf *black, ObClientTnsParseParams *parse_params);
int ObClientAppendStrategyBuild(ObClientBlacklistConf *black, ObClientTnsParseParams *parse_params);

int ObClientAddressListCheck(ObClientAddressList *address_list);
int ObClientAddressListInit(ObClientAddressList *address_list);
int ObClientAddressListClear(ObClientAddressList *address_list);
int ObClientAddressListDisplay(ObClientAddressList *address_list, FILE *display_file);
int ObClientAddressListBuild(ObClientDescription *des, ObClientTnsParseParams *parse_params);

int ObClientAddressInit(ObClientAddress *address);
int ObClientAddressClear(ObClientAddress *address);
int ObClientAddressDisplay(ObClientAddress *address, FILE *display_file);
int ObClientAddressBuild(ObClientAddressList *address_list, ObClientTnsParseParams *parse_params);

int ObClientConnectDataInit(ObClientConnectData *con_data);
int ObClientConnectDataClear(ObClientConnectData *con_data);
int ObClientConnectDataDisplay(ObClientConnectData *con_data, FILE *display_file);
int ObClientConnectDataBuild(ObClientDescription *des, ObClientTnsParseParams *parse_params);

int ObClientSystemVariablesInit(ObClientSystemVariables *sys_vars);
int ObClientSystemVariablesClear(ObClientSystemVariables *sys_vars);
int ObClientSystemVariablesDisplay(ObClientSystemVariables *sys_vars, FILE *display_file);
int ObClientSystemVariablesBuild(ObClientSystemVariables *sys_vars, ObClientTnsParseParams *parse_params);

// 解析文件相关函数
int ObClientTnsParse(ObClientTnsParseParams *parse_params, ObClientTnsTokenType expect_type);
int ObClientTnsCheckBuffer(ObClientTnsParseParams *parse_params);
int ObClientTnsParseBlank(ObClientTnsParseParams *parse_params);
int ObClientTnsParseKey(ObClientTnsParseParams *parse_params);
int ObClientTnsGetKeyType(ObClientTnsParseParams *parse_params);

#ifdef __cplusplus
}
#endif

#endif
