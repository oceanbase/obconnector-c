#include "ob_tnsname.h"

#include <errno.h>
#include <stdio.h>
#include <ma_global.h>
#include <ob_oralce_format_models.h>

#define TNS_NO_DATA 100
#define TNS_PARSE_FAIL_BREAK() {if(OB_FAIL(ret)) break;}

#define TNS_PARSE(parse_params, token_type)                                          \
  do                                                                                 \
  {                                                                                  \
    if (OB_SUCC(ret)) {                                                         \
      if (OB_FAIL(ObClientTnsParse(parse_params, token_type))) {;                       \
      } else {                                                                       \
        /* do nothing */                                                             \
      }                                                                              \
    }                                                                                \
  } while (0)

#define TNS_GET_KEY_TYPE(parse_params)                                           \
  do                                                                             \
  {                                                                              \
    if (OB_SUCC(ret)) {                                                     \
      if (OB_FAIL(ObClientTnsGetKeyType(parse_params))) {;                          \
      } else if (OBCLIENT_LB_ERROR_KEY == parse_params->key_type) {                       \
        ret = -1;                                                      \
      }                                                                          \
    }                                                                            \
  } while (0)

#define TNS_PARSE_STRING_VALUE(parse_params, dest_str, dest_len, dest_limit)      \
  do                                                                              \
  {                                                                               \
    TNS_PARSE(parse_params, TNS_KEY);                                             \
    if (OB_SUCC(ret) && parse_params->tns_key_len <= dest_limit) {           \
      dest_len = parse_params->tns_key_len;                                       \
      strncpy(dest_str, parse_params->tns_key, parse_params->tns_key_len);        \
    } else { ;                                                                    \
    }                                                                             \
  } while (0)

#define TNS_PARSE_INTRGER_VALUE(parse_params, des_value)                          \
  do                                                                              \
  {                                                                               \
    int err;                                                                      \
    char *endptr;                                                                 \
    TNS_PARSE(parse_params, TNS_KEY);                                             \
    endptr = parse_params->tns_key + parse_params->tns_key_len;                   \
    des_value = strtoll10(parse_params->tns_key, &endptr, &err);                  \
    if ((parse_params->tns_key_len != endptr - parse_params->tns_key)) {          \
      ret = -1;                                                                   \
    }                                                                             \
  } while (0)

#define TNS_KEY_ARRAY_SIZE(array)  (sizeof(array) / sizeof(const char *))

/**
 * 如果需要加入新的key type，需要确定好字符串长度，加入到相应长度的tns_key_x数组中
 * 并且在tns_keytype_x中设置好type即可
 */
static const char *tns_key_0[] = {0};
static const char *tns_key_1[] = {0};
static const char *tns_key_2[] = {0};
static const char *tns_key_3[] = {"SID"};
static const char *tns_key_4[] = {"OBLB", "HOST", "PORT", "NAME"};
static const char *tns_key_5[] = {0};
static const char *tns_key_6[] = {"WEIGHT"};
static const char *tns_key_7[] = {"ADDRESS", "TIMEOUT", "OB_MODE"};
static const char *tns_key_8[] = {"PROTOCOL", "DURATION" };
static const char *tns_key_9[] = {0};
static const char *tns_key_10[] = {"RETRYTIMES"};
static const char *tns_key_11[] = {"DESCRIPTION", "RETRY_TIMES"};
static const char *tns_key_12[] = {"ADDRESS_LIST", "CONNECT_DATA", "SERVICE_NAME"};
static const char *tns_key_13[] = {"OBLB_STRATEGY"};
static const char *tns_key_14[] = {"OBLB_BLACKLIST"};
static const char *tns_key_15[] = {"REMOVE_STRATEGY", "APPEND_STRATEGY", "USE_DEFAULT_SID"};
static const char *tns_key_16[] = {"SESSION_VARIABLE"};
static const char *tns_key_17[] = {"OBLB_READ_TIMEOUT"};
static const char *tns_key_18[] = {"OB_USER_EXTRA_INFO", "OBLB_RETRY_TIMEOUT", "OBLB_WRITE_TIMEOUT"};
static const char *tns_key_19[] = {"OBLB_GROUP_STRATEGY"};
static const char *tns_key_20[] = {"OBLB_RETRY_ALL_DOWNS", "OBLB_CONNECT_TIMEOUT"};

static ObClientLBKeyType tns_keytype_0[] = { OBCLIENT_LB_ERROR_KEY };
static ObClientLBKeyType tns_keytype_1[] = { OBCLIENT_LB_ERROR_KEY };
static ObClientLBKeyType tns_keytype_2[] = { OBCLIENT_LB_ERROR_KEY };
static ObClientLBKeyType tns_keytype_3[] = { OBCLIENT_LB_SID};
static ObClientLBKeyType tns_keytype_4[] = { OBCLIENT_LB_OBLB, OBCLIENT_LB_HOST, OBCLIENT_LB_PORT, OBCLIENT_LB_NAME};
static ObClientLBKeyType tns_keytype_5[] = { OBCLIENT_LB_ERROR_KEY };
static ObClientLBKeyType tns_keytype_6[] = { OBCLIENT_LB_WEIGHT};
static ObClientLBKeyType tns_keytype_7[] = { OBCLIENT_LB_ADDRESS, OBCLIENT_LB_TIMEOUT, OBCLIENT_LB_OB_MODE};
static ObClientLBKeyType tns_keytype_8[] = { OBCLIENT_LB_PROTOCOL, OBCLIENT_LB_DRUATION};
static ObClientLBKeyType tns_keytype_9[] = { OBCLIENT_LB_ERROR_KEY };
static ObClientLBKeyType tns_keytype_10[] = { OBCLIENT_LB_RETRY_TIMES };
static ObClientLBKeyType tns_keytype_11[] = { OBCLIENT_LB_DESCRIPTION, OBCLIENT_LB_RETRY_TIMES };
static ObClientLBKeyType tns_keytype_12[] = { OBCLIENT_LB_ADDRESS_LIST, OBCLIENT_LB_CONNECT_DATA, OBCLIENT_LB_SERVICE_NAME};
static ObClientLBKeyType tns_keytype_13[] = { OBCLIENT_LB_OBLB_STRATEGY};
static ObClientLBKeyType tns_keytype_14[] = { OBCLIENT_LB_OBLB_BLACKLIST};
static ObClientLBKeyType tns_keytype_15[] = { OBCLIENT_LB_REMOVE_STRATEGY, OBCLIENT_LB_APPEND_STRATEGY, OBCLIENT_LB_USE_DEFAULT_SID};
static ObClientLBKeyType tns_keytype_16[] = { OBCLIENT_LB_SESSION_VARIABLE};
static ObClientLBKeyType tns_keytype_17[] = { OBCLIENT_LB_READ_TIMEOUT};
static ObClientLBKeyType tns_keytype_18[] = { OBCLIENT_LB_EXTRA_INFO, OBCLIENT_LB_OBLB_RETRY_TIMEOUT, OBCLIENT_LB_WRITE_TIMEOUT};
static ObClientLBKeyType tns_keytype_19[] = { OBCLIENT_LB_OBLB_GROUP_STRATEGY };
static ObClientLBKeyType tns_keytype_20[] = { OBCLIENT_LB_OBLB_RETRY_ALL_DOWNS, OBCLIENT_LB_CONNECT_TIMEOUT};

static const void *tns_key_array[] = {
  tns_key_0, tns_key_1, tns_key_2, tns_key_3, tns_key_4, tns_key_5, tns_key_6, tns_key_7, tns_key_8,
  tns_key_9, tns_key_10, tns_key_11, tns_key_12, tns_key_13, tns_key_14, tns_key_15, tns_key_16, tns_key_17,
  tns_key_18, tns_key_19, tns_key_20
};

static const void *tns_keytype_array[] = {
  tns_keytype_0, tns_keytype_1, tns_keytype_2, tns_keytype_3, tns_keytype_4, tns_keytype_5, tns_keytype_6, tns_keytype_7, tns_keytype_8,
  tns_keytype_9, tns_keytype_10, tns_keytype_11, tns_keytype_12, tns_keytype_13, tns_keytype_14, tns_keytype_15, tns_keytype_16, tns_keytype_17,
  tns_keytype_18, tns_keytype_19, tns_keytype_20
};

static const unsigned int tns_key_array_size[] = {
  TNS_KEY_ARRAY_SIZE(tns_key_0), TNS_KEY_ARRAY_SIZE(tns_key_1), TNS_KEY_ARRAY_SIZE(tns_key_2),
  TNS_KEY_ARRAY_SIZE(tns_key_3), TNS_KEY_ARRAY_SIZE(tns_key_4), TNS_KEY_ARRAY_SIZE(tns_key_5),
  TNS_KEY_ARRAY_SIZE(tns_key_6), TNS_KEY_ARRAY_SIZE(tns_key_7), TNS_KEY_ARRAY_SIZE(tns_key_8),
  TNS_KEY_ARRAY_SIZE(tns_key_9), TNS_KEY_ARRAY_SIZE(tns_key_10), TNS_KEY_ARRAY_SIZE(tns_key_11),
  TNS_KEY_ARRAY_SIZE(tns_key_12), TNS_KEY_ARRAY_SIZE(tns_key_13), TNS_KEY_ARRAY_SIZE(tns_key_14),
  TNS_KEY_ARRAY_SIZE(tns_key_15), TNS_KEY_ARRAY_SIZE(tns_key_16), TNS_KEY_ARRAY_SIZE(tns_key_17),
  TNS_KEY_ARRAY_SIZE(tns_key_18), TNS_KEY_ARRAY_SIZE(tns_key_19), TNS_KEY_ARRAY_SIZE(tns_key_20)
};

int ObClientTnsInit(ObClientTns *tns)
{
  int ret = 0;

  if (OB_ISNULL(tns)) {
    ret = -1; 
  } else {
    memset(tns, 0, sizeof(*tns));
    tns->tns_service = NULL;
    tns->tns_service_count = 0;
  }

  return ret;
}

int ObClientTnsClear(ObClientTns *tns)
{
  int ret = 0;

  if (OB_ISNULL(tns)) {
    ret = 0;
  } else if (OB_ISNULL(tns->tns_service) || 0 == tns->tns_service_count) {
    ret = 0;
  } else {
    ObClientTnsServiceClear(tns->tns_service);
  }

  return ret;
}

int ObClientTnsDisplay(ObClientTns *tns)
{
  int       ret = 0;
  FILE       *display_file = stdout;
  const char *display_file_name = NULL;

  if (OB_ISNULL(tns)) {
    ret = -1;
  } else if (0 == tns->tns_service_count || OB_ISNULL(tns->tns_service)) {
    ret = -1;
  } else {
    my_bool need_close = 0;
    if (OB_NOT_NULL(display_file_name = getenv("OBCI_TNS_DISPLAY"))) {
      if (OB_ISNULL(display_file = fopen(display_file_name, "r"))) {
        ret = -1;
      }
      need_close = 1;
    }

    if (OB_SUCC(ret)) {
      unsigned int i;
      ObClientTnsService *tns_service = tns->tns_service;

      fprintf(display_file, "Tns:\n");
      fprintf(display_file, "--tns_serivce_count:%u\n", tns->tns_service_count);

      for (i = 0; i < tns->tns_service_count && OB_SUCC(ret); ++i) {
        if (OB_FAIL(ObClientTnsServiceDisplay(tns_service, display_file))) {
          ;
        }
      }
    }
    if (need_close && display_file) {
      fclose(display_file);
    }
  }
  
  return ret;
}

static int ObClientTnsBuildGetFile(ObClientTnsParseParams *parse_params)
{
  int       ret = 0;
  int         prefix_len;
  char        file_name[OBCLIENT_TNS_KEY_SIZE + 1] = {0};

  if (OB_ISNULL(parse_params)) {
    ret = -1;
  } else {
    if (OB_NOT_NULL(parse_params->tns_name = getenv("OBCI_TNS_FILE"))) {
      strncpy(file_name, parse_params->tns_name, OBCLIENT_TNS_KEY_SIZE);
    } else if (OB_NOT_NULL(parse_params->tns_name = getenv("TNS_ADMIN"))) {
      prefix_len = strlen(parse_params->tns_name);
      strncpy(file_name, parse_params->tns_name, OBCLIENT_TNS_KEY_SIZE);
      strncpy(file_name + prefix_len, "/tnsnames.ora", OBCLIENT_TNS_KEY_SIZE - prefix_len);
    } else if (OB_NOT_NULL(parse_params->tns_name = getenv("ORACLE_HOME"))) {
      prefix_len = strlen(parse_params->tns_name);
      strncpy(file_name, parse_params->tns_name, OBCLIENT_TNS_KEY_SIZE);
      strncpy(file_name + prefix_len, "/network/admin/tnsnames.ora", OBCLIENT_TNS_KEY_SIZE - prefix_len);
    } else {
      ret = TNS_NO_DATA;
    }
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(parse_params->tns_file = fopen(file_name, "r"))) {
        ret = -1;
      }
    }
  }

  return ret;
}

my_bool IsDescriptionTns(char* description, unsigned int description_len) {
  my_bool ret = 0;
  int i = 0, j = 0;
  char str[20] = { 0 }; //(DESCRIPTION=
  for (i = 0; i < (int)description_len && j <13; i++) {
    if (description[i] == ' ' || description[i] == '\t' ||
      description[i] == '\r' || description[i] == '\n') {
      continue;
    }
    str[j++] = description[i];
  }
  if (0 == strncasecmp(str, "(DESCRIPTION=", 13)) {
    ret = 1;
  }
  return ret;
}

int ObClientTnsBuild(ObClientTns *tns, const char *dbname, unsigned int dbname_len, my_bool *find)
{
  int ret = 0;
  ObClientTnsParseParams parse_params;

  memset(&parse_params, 0, sizeof(parse_params));
  parse_params.tns_file = NULL;
  parse_params.mode = TNS_MODE_FILE;
  
  if (OB_ISNULL(tns) || OB_ISNULL(dbname) || OB_ISNULL(find)) {
    ret = -1;
  } else if (IsDescriptionTns((char*)dbname, dbname_len)) {
    ret = ObClientTnsBuildDes(tns, dbname, dbname_len, find);
  } else if (OB_FAIL(ObClientTnsBuildGetFile(&parse_params))) {
    ret = 0;
    *find = FALSE;
  } else {
    *find = FALSE;
    while (OB_SUCC(ret) && 0 == parse_params.is_eof && FALSE == *find) {
      // 解析tns_service name
      TNS_PARSE(&parse_params, TNS_KEY);
      // 解析等于号
      TNS_PARSE(&parse_params, TNS_EQUAL_SIGN);
      // 比较key是否和dbname一样，不同则跳过整个kv对，一样则进入解析流程, 这里大小写敏感
      if (OB_SUCC(ret)) {
        if (dbname_len != parse_params.tns_key_len || 0 != strncmp((const char *)dbname, parse_params.tns_key, dbname_len)) {
          // 这里跳过这个tns service下所有pair
          ret = ObClientTnsServiceSkip(&parse_params);
        } else {
          // 这里进入解析
          if (OB_FAIL(ObClientTnsServiceBuild(tns, &parse_params))) {
            ret = -1;
          } else {
            *find = TRUE;
          }
        }
      }
    }
    if (OB_FAIL(ret)) {
      *find = FALSE;
      if (TNS_NO_DATA == ret) {
        ret = 0;
      }
    }
  }

  if (OB_NOT_NULL(parse_params.tns_file)) {
    fclose(parse_params.tns_file);
  }

  return ret;
}

int ObClientTnsBuildDes(ObClientTns *tns, const char *dbname, unsigned int dbname_len, my_bool *find)
{
  int ret = 0;
  ObClientTnsParseParams parse_params;
  ObClientTnsService *tns_service = NULL;

  memset(&parse_params, 0, sizeof(parse_params));
  parse_params.tns_file = NULL;
  parse_params.mode = TNS_MODE_DESCRIPTION;
  parse_params.description = (char*)dbname;
  parse_params.description_len = dbname_len;
  parse_params.description_offset = 0;

  if (OB_ISNULL(tns) || OB_ISNULL(dbname) || OB_ISNULL(find)) {
    ret = -1;
    *find = FALSE;
  } else {
    *find = FALSE;

    // 解析左侧括号
    TNS_PARSE(&parse_params, TNS_LEFT_PARENTHESES);
    // DESCRIPTION
    TNS_PARSE(&parse_params, TNS_KEY); 
    TNS_GET_KEY_TYPE((&parse_params));
    // 解析等于号
    TNS_PARSE(&parse_params, TNS_EQUAL_SIGN);
    if (OB_SUCC(ret)) {
      if (OBCLIENT_LB_DESCRIPTION != parse_params.key_type) {
        ret = -1;
      } else {
        if (OB_ISNULL(tns_service = malloc(sizeof(*tns_service) + parse_params.tns_key_len))) {
          ret = -1;
        } else if (OB_FAIL(ObClientTnsServiceInit(tns_service, parse_params.tns_key_len, parse_params.tns_key))) {
          ret = -1;
        } else if (OB_FAIL(ObClientDescriptionBuild(tns_service, &parse_params))) {
          ret = -1;
        }
        TNS_PARSE(&parse_params, TNS_RIGHT_PARENTHESES);

        //test end
        if (OB_SUCC(ret)) {
          int st = ObClientTnsParseBlank(&parse_params);
          if (st != TNS_NO_DATA)
            ret = -1;
        }
        if (OB_SUCC(ret) && TNS_RIGHT_PARENTHESES == parse_params.tns_token_type) {
          tns->tns_service_count++;
          if (OB_ISNULL(tns->tns_service)) {
            tns->tns_service = tns_service;
          } 
          // 最后将ret改为success，由调用函数继续解析
          ret = 0;
        } else {
          ObClientTnsServiceClear(tns_service);
          free(tns_service);
          ret = -1;
        }
        if (OB_SUCC(ret)) {
          *find = TRUE;
        }
      }
    }

    if (OB_FAIL(ret)) {
      *find = FALSE;
      if (TNS_NO_DATA == ret) {
        ret = 0;
      }
    }
  }
  return ret;
}

int ObClientTnsServiceCheck(ObClientTnsService *tns_service)
{
  int ret = 0;
  if (OB_ISNULL(tns_service)) {
    ret = -1;
  } else {
    if (tns_service->description_count == tns_service->description_memory) {
      int new_memory = tns_service->description_memory + OBCLIENT_TNS_MEMORY_COUNT;
      char * tmp = realloc(tns_service->description, new_memory * sizeof(ObClientDescription));
      if (tmp) {
        tns_service->description = (ObClientDescription*)tmp;
        tns_service->description_memory = new_memory;
      } else {
        ret = -1;
      }
    }
  }
  return ret;
}
int ObClientTnsServiceInit(ObClientTnsService *tns_service, unsigned int name_size, const char *name)
{
  int ret = 0;

  if (OB_ISNULL(tns_service)) {
    ret = -1; 
  } else {
    memset(tns_service, 0, sizeof(*tns_service));
    tns_service->description_count = 0;
    tns_service->description_memory = 0;
    tns_service->net_service_name_len = name_size;
    strncpy(tns_service->net_service_name, name, min(name_size, OBCLIENT_TNS_BUFFER_SIZE));
    ret = ObClientSystemVariablesInit(&tns_service->sys_vars);
  }

  return ret;
}

int ObClientTnsServiceClear(ObClientTnsService *tns_service)
{
  int ret = 0;

  if (OB_ISNULL(tns_service) || OB_ISNULL(tns_service->description) || 0 == tns_service->description_count) {
    ret = -1;
  } else if (OB_FAIL(ObClientSystemVariablesClear(&tns_service->sys_vars))) {
    ;
  } else {
    unsigned int i;
    for (i = 0; i < tns_service->description_count && OB_SUCC(ret); ++i) {
      ObClientDescription *description = &(tns_service->description[i]);
      ObClientDescriptionClear(description);
    }
    free(tns_service->description);
    tns_service->description = NULL;
    tns_service->description_count = 0;
    tns_service->description_memory = 0;
  }

  return ret;
}

int ObClientTnsServiceDisplay(ObClientTnsService *tns_service, FILE *display_file)
{
  int ret = 0;

  if (OB_ISNULL(tns_service) || OB_ISNULL(display_file)) {
    ret = -1;
  } else if (0 == strncasecmp("OBCISYSTEMVARIABLES", tns_service->net_service_name, tns_service->net_service_name_len)) {
    ret = ObClientSystemVariablesDisplay(&tns_service->sys_vars, display_file);
  } else if (0 == tns_service->description_count || OB_ISNULL(tns_service->description)) {
    ret = -1;
  } else {
    unsigned int i;
    
    fprintf(display_file, "--TnsService:%.*s\n", tns_service->net_service_name_len, tns_service->net_service_name);
    fprintf(display_file, "----description_count:%u\n", tns_service->description_count);

    for (i = 0; i < tns_service->description_count && OB_SUCC(ret); ++i) {
      ObClientDescription *description = &(tns_service->description[i]);
      ObClientDescriptionDisplay(description, display_file);
    }

    ObClientSystemVariablesDisplay(&tns_service->sys_vars, display_file);
  }

  return ret;
}

int ObClientTnsServiceBuild(ObClientTns *tns, ObClientTnsParseParams *parse_params)
{
  int ret = 0;
  ObClientTnsService *tns_service = NULL;

  if (OB_ISNULL(tns) || OB_ISNULL(parse_params)) {
    ret = -1;
  } else {
    int is_sys_vars = FALSE;  

    // 上个函数解析的name
    if (OB_SUCC(ret)) {
      if (OB_ISNULL(tns_service = malloc(sizeof(*tns_service) + parse_params->tns_key_len))) {
        ret = -1;
      } else if (OB_FAIL(ObClientTnsServiceInit(tns_service, parse_params->tns_key_len, parse_params->tns_key))) {
        ret = -1;
      } else if (0 == strncasecmp("OBCISYSTEMVARIABLES", parse_params->tns_key, parse_params->tns_key_len)) {
        is_sys_vars = TRUE;
      }
    }

    if (OB_SUCC(ret)) {
      int parse_ok = 0;
      // 循环处理pair
      while (OB_SUCC(ret)) {
        // 解析左括号
        TNS_PARSE(parse_params, TNS_LEFT_PARENTHESES);
        TNS_PARSE_FAIL_BREAK();
        // 解析key
        TNS_PARSE(parse_params, TNS_KEY);
        // 得到key type
        TNS_GET_KEY_TYPE(parse_params);
        if (is_sys_vars) { //sys_vars中错误的key不影响其他项
          ret = 0;
        }
        // 解析等于号, 上面得到的key不会被覆盖
        TNS_PARSE(parse_params, TNS_EQUAL_SIGN);
        // 这里的key必须得到description，否则报错
        if (OB_SUCC(ret)) {
          if (is_sys_vars) {
            ret = ObClientSystemVariablesBuild(&tns_service->sys_vars, parse_params);
          } else {
            if (OBCLIENT_LB_DESCRIPTION != parse_params->key_type) {
              ret = -1;
            } else {
              ret = ObClientDescriptionBuild(tns_service, parse_params);
            }
          }
        }
        TNS_PARSE(parse_params, TNS_RIGHT_PARENTHESES);
        if (OB_SUCC(ret)) { 
          parse_ok = 1;
        }
      }
      if (parse_ok && TNS_RIGHT_PARENTHESES == parse_params->tns_token_type) {
        tns->tns_service_count++;
        if (OB_ISNULL(tns->tns_service)) {
          tns->tns_service = tns_service;
        }
        ret = 0;
      } else {
        ObClientTnsServiceClear(tns_service);
        free(tns_service);
        ret = -1;
      }
    }
  }
  
  return ret;
}

int ObClientTnsServiceSkip(ObClientTnsParseParams *parse_params)
{
  int ret = 0;

  if (OB_ISNULL(parse_params)) {
    ret = -1;
  } else {
    int left_parentheses = 1;
    char ch;

    if (0 == strncmp("OBCISYSTEMVARIABLES", parse_params->tns_key, parse_params->tns_key_len)) {
      parse_params->is_eof = 1;
    } else {
      // 首先定位到第一个左括号
      TNS_PARSE(parse_params, TNS_LEFT_PARENTHESES);

      while (OB_SUCC(ret) && left_parentheses != 0) {
        if (OB_FAIL(ObClientTnsCheckBuffer(parse_params))) {
          if (TRUE == parse_params->is_eof) {
            ret = TNS_NO_DATA;
          }
        } else {
          while (parse_params->buffer_pos < parse_params->buffer_len
            && left_parentheses != 0) {
            ch = parse_params->tns_buffer[parse_params->buffer_pos++];
            if ('(' == ch) {
              left_parentheses++;
            } else if (')' == ch) {
              left_parentheses--;
            }
          }
        }
      }
    }
  }

  return ret;
}

int ObClientTnsServiceExtraInfoGet(ObClientTnsService *tns_service, char *extra_info, int *extra_info_len, int *ob_mode)
{
  int ret = 0;
  ObClientDescription *des;
  ObClientConnectData *con_data;

  if (OB_ISNULL(tns_service) || OB_ISNULL(extra_info) || OB_ISNULL(extra_info) || OB_ISNULL(ob_mode)) {
    ret = -1;
  } else if (0 == tns_service->description_count || OB_ISNULL(des = tns_service->description)) {
    ret = -1;
  } else if (OB_ISNULL(con_data = des->connect_data)) {
    ret = -1;
  } else if (0 != con_data->user_extra_info_len) {
    *extra_info_len = con_data->user_extra_info_len;
    strncpy(extra_info, con_data->user_extra_info, *extra_info_len);
    *ob_mode = con_data->ob_mode;
  }

  return ret;
}

int ObClientTnsServiceDblinkGet(ObClientTnsService *tns_service, char *dblink, int *dblink_len)
{
  int ret = 0;
  ObClientAddress     *address;
  ObClientDescription *des;
  ObClientAddressList *address_list;
  ObClientConnectData *con_data;

  if (OB_ISNULL(tns_service) || OB_ISNULL(dblink) || OB_ISNULL(dblink_len)) {
    ret = -1;
  } else if (0 == tns_service->description_count || OB_ISNULL(des = tns_service->description)) {
    ret = -1;
  } else if (0 == des->address_list_count || OB_ISNULL(address_list = des->address_list)) {
    ret = -1;
  } else if (0 == address_list->address_count || OB_ISNULL(address = address_list->address)){
    ret = -1;
  } else if (OB_ISNULL(con_data = des->connect_data)) {
    ret = -1;
  } else {
    int tmp_dblink_len = 0;
    if (con_data->use_default_sid) {
      // get dblink from address, host:port, use default schema
      tmp_dblink_len = snprintf(dblink, OBCLIENT_TNS_KEY_SIZE, "%.*s:%d", address->host_len, address->host, address->port);
    } else {
      // get dblink from address, host:port/service_name
      tmp_dblink_len = snprintf(dblink, OBCLIENT_TNS_KEY_SIZE, "%.*s:%d/%.*s",
                              address->host_len, address->host, address->port, con_data->service_name_len, con_data->service_name);
    }
    if (tmp_dblink_len <= 0) {
      ret = -1;
    } else {
      *dblink_len = tmp_dblink_len;
    }
  }

  return ret;
}

int ObClientDescriptionCheck(ObClientDescription *des)
{
  int ret = 0;
  if (OB_ISNULL(des)) {
    ret = -1;
  } else {
    if (des->address_list_count == des->address_list_memory) {
      int new_memory = des->address_list_memory + OBCLIENT_TNS_MEMORY_COUNT;
      char * tmp = realloc(des->address_list, new_memory * sizeof(ObClientAddressList));
      if (tmp) {
        des->address_list = (ObClientAddressList*)tmp;
        des->address_list_memory = new_memory;
      } else {
        ret = -1;
      }
    }
  }
  return ret;
}

int ObClientDescriptionInit(ObClientDescription *des)
{
  int ret = 0;

  if (OB_ISNULL(des)) {
    ret = -1;
  } else {
    memset(des, 0, sizeof(*des));
    if (OB_FAIL(ObClientBlacklistConfInit(&des->black_list_conf))) {
      ret = -1;
    } else if (OB_FAIL(ObClientDescriptionCheck(des))) {
      ret = -1;
    } else {
      ObClientAddressList *address_list = &(des->address_list[0]);
      ObClientAddressListInit(address_list);
      des->address_list_count = 1;    //默认的0给非address list 使用

      des->connect_data = NULL;
      des->retry_all_downs = 120;     // 默认执行120次
      des->retry_timeout = 10000;     // 超时时间，默认10s
      des->connect_timout = 0;        // 超时时间，默认0s, 也就是一直等待
      des->read_timout = 0;           // 读取时间，默认0s, 也就是一直等待
      des->write_timout = 0;          // 发送时间，默认0s, 也就是一直等待
      des->oblb = FALSE;
      des->oblb_group_strategy = OBCLIENT_LB_OPTION_GROUP_ROTATION;
    }
  }

  return ret;
}

int ObClientDescriptionClear(ObClientDescription *des)
{
  int ret = 0;

  if (OB_ISNULL(des)) {
    ret = 0;
  } else {
    if (OB_ISNULL(des->address_list) || 0 == des->address_list_memory) {
      ret = 0;
    } else {
      unsigned int i;
      for (i = 0; i < des->address_list_count && OB_SUCC(ret); ++i) {
        ObClientAddressList *address_list = &(des->address_list[i]);
        ObClientAddressListClear(address_list);
      }
      free(des->address_list);
      des->address_list = NULL;
      des->address_list_count = 0;
      des->address_list_memory = 0;
    }

    if (OB_NOT_NULL(des->connect_data)) {
      ObClientConnectDataClear(des->connect_data);
      free(des->connect_data);
      des->connect_data = NULL;
    }
  }

  return ret;
}

int ObClientDescriptionDisplay(ObClientDescription *des, FILE *display_file)
{
  int ret = 0;

  if (OB_ISNULL(des) || OB_ISNULL(display_file) || 0 == des->address_list_count || OB_ISNULL(des->address_list)) {
    ret = -1;
  } else {
    unsigned int i;

    fprintf(display_file, "----Discription:\n");
    fprintf(display_file, "------oblb:%d\n", des->oblb);
    fprintf(display_file, "------oblb_group_strategy:%d\n", des->oblb_group_strategy);
    fprintf(display_file, "------retry_all_downs:%ld\n", des->retry_all_downs);
    fprintf(display_file, "------retry_timeout:%ld\n", des->retry_timeout);
    fprintf(display_file, "------connect_timeout:%ld\n", des->connect_timout);
    fprintf(display_file, "------read_timeout:%ld\n", des->read_timout);
    fprintf(display_file, "------write_timeout:%ld\n", des->write_timout);
    fprintf(display_file, "------append_strategy:%d\n", des->black_list_conf.append_strategy);
    fprintf(display_file, "------remove_strategy:%d\n", des->black_list_conf.remove_strategy);
    fprintf(display_file, "------remove_timeout:%ld\n", des->black_list_conf.remove_timeout);
    fprintf(display_file, "------duration:%ld\n", des->black_list_conf.duration);
    fprintf(display_file, "------retry_times:%ld\n", des->black_list_conf.retry_times);
    fprintf(display_file, "------address_list_count:%u\n", des->address_list_count);

    for (i = 0; i < des->address_list_count && OB_SUCC(ret); ++i) {
      ObClientAddressListDisplay(&(des->address_list[i]), display_file);
    }

    if (OB_NOT_NULL(des->connect_data)) {
      ObClientConnectDataDisplay(des->connect_data, display_file);
    }
  }

  return ret;
}

int ObClientDescriptionBuild(ObClientTnsService *tns_service, ObClientTnsParseParams *parse_params)
{
  int ret = 0;
  ObClientDescription *des = NULL;

  if (OB_ISNULL(tns_service) || OB_ISNULL(parse_params)) {
    ret = -1;
  } else if (OB_FAIL(ObClientTnsServiceCheck(tns_service))) {
    ret = -1;
  } else if (OB_ISNULL(des = &(tns_service->description[tns_service->description_count]))) {
    ret = -1;
  } else if (OB_FAIL(ObClientDescriptionInit(des))) {
    ret = -1;
  } else {
    // 进入解析description流程
    // 循环处理pair
    while (OB_SUCC(ret)) {
      // 解析左括号
      TNS_PARSE(parse_params, TNS_LEFT_PARENTHESES);
      // 解析key
      TNS_PARSE(parse_params, TNS_KEY);
      // 得到key type
      TNS_GET_KEY_TYPE(parse_params);
      // 解析等于号, 上面得到的key不会被覆盖
      TNS_PARSE(parse_params, TNS_EQUAL_SIGN);
      // 这里处理所有description下面的选项
      if (OB_SUCC(ret)) {
        if (OBCLIENT_LB_OBLB == parse_params->key_type) {
          TNS_PARSE(parse_params, TNS_KEY);
          if (0 == strncasecmp(parse_params->tns_key, "ON", parse_params->tns_key_len)) {
            des->oblb = TRUE;
          } else if (0 == strncasecmp(parse_params->tns_key, "OFF", parse_params->tns_key_len)) {
            des->oblb = FALSE;
          } else {
            ret = -1;
          }
        } else if (OBCLIENT_LB_OBLB_RETRY_ALL_DOWNS == parse_params->key_type) {
          TNS_PARSE_INTRGER_VALUE(parse_params, des->retry_all_downs);
        } else if (OBCLIENT_LB_OBLB_RETRY_TIMEOUT == parse_params->key_type) {
          TNS_PARSE_INTRGER_VALUE(parse_params, des->retry_timeout);
        } else if (OBCLIENT_LB_CONNECT_TIMEOUT == parse_params->key_type) {
          TNS_PARSE_INTRGER_VALUE(parse_params, des->connect_timout);
        } else if (OBCLIENT_LB_READ_TIMEOUT == parse_params->key_type) {
          TNS_PARSE_INTRGER_VALUE(parse_params, des->read_timout);
        } else if (OBCLIENT_LB_WRITE_TIMEOUT == parse_params->key_type) {
          TNS_PARSE_INTRGER_VALUE(parse_params, des->write_timout);
        } else if (OBCLIENT_LB_OBLB_GROUP_STRATEGY == parse_params->key_type) {
          TNS_PARSE(parse_params, TNS_KEY);
          if (0 == strncasecmp(parse_params->tns_key, "ROTATION", parse_params->tns_key_len)) {
            des->oblb_group_strategy = OBCLIENT_LB_OPTION_GROUP_ROTATION;
          } else {
            ret = -1;
          }
        } else if (OBCLIENT_LB_OBLB_BLACKLIST == parse_params->key_type) {
          ret = ObClientBlacklistConfBuild(&des->black_list_conf, parse_params);
        } else if (OBCLIENT_LB_ADDRESS_LIST == parse_params->key_type) {
          ret = ObClientAddressListBuild(des, parse_params);
        } else if (OBCLIENT_LB_CONNECT_DATA == parse_params->key_type) {
          ret = ObClientConnectDataBuild(des, parse_params);
        } else if (OBCLIENT_LB_ADDRESS == parse_params->key_type) {
          // 没有ADDRESSLIST的情况, 兼容之前结构
          ObClientAddressList* address_list = NULL;
          if (OB_FAIL(ObClientDescriptionCheck(des))) {
            ret = -1;
          } else if (OB_ISNULL(address_list = &(des->address_list[0]))) {
            ret = -1;
          } else if (OB_FAIL(ObClientAddressBuild(address_list, parse_params))) {
            ret = -1;
          }
        } else {
          ret = -1;
        }
      }
      TNS_PARSE(parse_params, TNS_RIGHT_PARENTHESES);
    }
    if (TNS_RIGHT_PARENTHESES == parse_params->tns_token_type) {
      tns_service->description_count++;
      ret = 0;
    } else {
      ret = -1;
    }
  }
  
  return ret;
}

int ObClientRemoveStrategyBuild(ObClientBlacklistConf *black, ObClientTnsParseParams *parse_params)
{
  int ret = 0;

  if (OB_ISNULL(black) || OB_ISNULL(parse_params)) {
    ret = -1;
  } else {
    // 进入解析流程
    // 循环处理pair
    while (OB_SUCC(ret)) {
      // 解析左括号
      TNS_PARSE(parse_params, TNS_LEFT_PARENTHESES);
      // 解析key
      TNS_PARSE(parse_params, TNS_KEY);
      // 得到key type
      TNS_GET_KEY_TYPE(parse_params);
      // 解析等于号, 上面得到的key不会被覆盖
      TNS_PARSE(parse_params, TNS_EQUAL_SIGN);
      // 这里处理所有下面的选项
      if (OB_SUCC(ret)) {
        if (OBCLIENT_LB_NAME == parse_params->key_type) {
          TNS_PARSE(parse_params, TNS_KEY);
          if (0 == strncasecmp(parse_params->tns_key, "TIMEOUT", parse_params->tns_key_len)) {
            black->remove_strategy = OBCLIENT_LB_OPTION_TIMEOUT;
          } else {
            ret = -1;
          }
        } else if (OBCLIENT_LB_TIMEOUT == parse_params->key_type) {
          TNS_PARSE_INTRGER_VALUE(parse_params, black->remove_timeout);
        } else {
          ret = -1;
        }
      }
      TNS_PARSE(parse_params, TNS_RIGHT_PARENTHESES);
    }
    if (TNS_RIGHT_PARENTHESES == parse_params->tns_token_type) {
      ret = 0;
    } else {
      ret = -1;
    }
  }
  
  return ret;
}

int ObClientAppendStrategyBuild(ObClientBlacklistConf *black, ObClientTnsParseParams *parse_params)
{
  int ret = 0;

  if (OB_ISNULL(black) || OB_ISNULL(parse_params)) {
    ret = -1;
  } else {
    // 进入解析流程
    // 循环处理pair
    while (OB_SUCC(ret)) {
      // 解析左括号
      TNS_PARSE(parse_params, TNS_LEFT_PARENTHESES);
      // 解析key
      TNS_PARSE(parse_params, TNS_KEY);
      // 得到key type
      TNS_GET_KEY_TYPE(parse_params);
      // 解析等于号, 上面得到的key不会被覆盖
      TNS_PARSE(parse_params, TNS_EQUAL_SIGN);
      // 这里处理所有下面的选项
      if (OB_SUCC(ret)) {
        if (OBCLIENT_LB_NAME == parse_params->key_type) {
          TNS_PARSE(parse_params, TNS_KEY);
          if (0 == strncasecmp(parse_params->tns_key, "RETRYDURATION", parse_params->tns_key_len)) {
            black->append_strategy = OBCLIENT_LB_OPTION_RETRY_DERUATION;
          } else {
            ret = -1;
          }
        } else if (OBCLIENT_LB_RETRY_TIMES == parse_params->key_type) {
          TNS_PARSE_INTRGER_VALUE(parse_params, black->retry_times);
        } else if (OBCLIENT_LB_DRUATION == parse_params->key_type) {
          TNS_PARSE_INTRGER_VALUE(parse_params, black->duration);
        } else {
          ret = -1;
        }
      }
      TNS_PARSE(parse_params, TNS_RIGHT_PARENTHESES);
    }
    if (TNS_RIGHT_PARENTHESES == parse_params->tns_token_type) {
      ret = 0;
    } else {
      ret = -1;
    }
  }
  
  return ret;
}

// 默认黑名单策略：todo 和 jdbc对齐
int ObClientBlacklistConfInit(ObClientBlacklistConf *black_list_conf)
{
  int ret = 0;

  if (OB_ISNULL(black_list_conf)) {
    ret = -1;
  } else {
    memset(black_list_conf, 0, sizeof(*black_list_conf));
    black_list_conf->append_strategy = OBCLIENT_LB_OPTION_NORMAL;
    black_list_conf->remove_strategy = OBCLIENT_LB_OPTION_TIMEOUT;
    black_list_conf->retry_times = 1;                       // 默认1次
    black_list_conf->duration = 10;                         // 默认10ms
    black_list_conf->remove_timeout = 50;                   // 默认50ms
  }

  return ret;
}

int ObClientBlacklistConfBuild(ObClientBlacklistConf *black_list_conf, ObClientTnsParseParams *parse_params)
{
  int ret = 0;

  if (OB_ISNULL(black_list_conf) || OB_ISNULL(parse_params)) {
    ret = -1;
  } else {
    // 进入解析black_list_conf流程
    // 循环处理pair
    while (OB_SUCC(ret)) {
      // 解析左括号
      TNS_PARSE(parse_params, TNS_LEFT_PARENTHESES);
      // 解析key
      TNS_PARSE(parse_params, TNS_KEY);
      // 得到key type
      TNS_GET_KEY_TYPE(parse_params);
      // 解析等于号, 上面得到的key不会被覆盖
      TNS_PARSE(parse_params, TNS_EQUAL_SIGN);
      // 这里处理所有address_list下面的选项
      if (OB_SUCC(ret)) {
        if (OBCLIENT_LB_REMOVE_STRATEGY == parse_params->key_type) {
          ret = ObClientRemoveStrategyBuild(black_list_conf, parse_params);
        } else if (OBCLIENT_LB_APPEND_STRATEGY == parse_params->key_type) {
          ret = ObClientAppendStrategyBuild(black_list_conf, parse_params);
        } else {
          ret = -1;
        }
      }
      TNS_PARSE(parse_params, TNS_RIGHT_PARENTHESES);
    }
    if (TNS_RIGHT_PARENTHESES == parse_params->tns_token_type) {
      ret = 0;
    } else {
      ret = -1;
    }
  }
  
  return ret;
}

int ObClientAddressListCheck(ObClientAddressList *address_list)
{
  int ret = 0;
  if (OB_ISNULL(address_list)) {
    ret = -1;
  } else {
    if (address_list->address_count == address_list->address_memory) {
      int new_memory = address_list->address_memory + OBCLIENT_TNS_MEMORY_COUNT;
      char *tmp = realloc(address_list->address, new_memory * sizeof(ObClientAddress));
      if (tmp) {
        address_list->address = (ObClientAddress*)tmp;
        address_list->address_memory = new_memory;
      } else {
        ret = -1;
      }
    }
  }
  return ret;
}
int ObClientAddressListInit(ObClientAddressList *address_list)
{
  int ret = 0;

  if (OB_ISNULL(address_list)) {
    ret = -1;
  } else {
    memset(address_list, 0, sizeof(ObClientAddressList));
    ret = ObClientAddressListCheck(address_list);
  }

  return ret;
}

int ObClientAddressListClear(ObClientAddressList *address_list)
{
  int ret = 0;
  if (OB_ISNULL(address_list)) {
    ret = 0;
  } else {
    if (OB_ISNULL(address_list->address) || 0 == address_list->address_memory) {
      ret = 0;
    } else {
      free(address_list->address);
      address_list->address = NULL;
      address_list->address_count = 0;
      address_list->address_memory = 0;
    }
  }
  return ret;
}

int ObClientAddressListDisplay(ObClientAddressList *address_list, FILE *display_file)
{
  int ret = 0;

  if (OB_ISNULL(address_list) || OB_ISNULL(display_file) || OB_ISNULL(address_list->address) || 0 == address_list->address_count) {
    ret = -1;
  } else {
    unsigned int i;
    fprintf(display_file, "------AddressList:\n");
    fprintf(display_file, "--------address_count:%u\n", address_list->address_count);
    fprintf(display_file, "--------oblb_strategy:%d\n", address_list->oblb_strategy);
    for (i = 0; i < address_list->address_count && OB_SUCC(ret); ++i) {
      ObClientAddressDisplay(&(address_list->address[i]), display_file);
    }
  }

  return ret;
}

int ObClientAddressListBuild(ObClientDescription *des, ObClientTnsParseParams *parse_params)
{
  int ret = 0;
  ObClientAddressList *address_list = NULL;

  if (OB_ISNULL(des) || OB_ISNULL(parse_params)) {
    ret = -1;
  } else if (OB_FAIL(ObClientDescriptionCheck(des))) {
    ret = -1;
  } else if (OB_ISNULL(address_list = &(des->address_list[des->address_list_count]))) {
    ret = -1;
  } else if (OB_FAIL(ObClientAddressListInit(address_list))) {
  } else {
    // 进入解析address_list流程
    // 循环处理pair
    while (OB_SUCC(ret)) {
      // 解析左括号
      TNS_PARSE(parse_params, TNS_LEFT_PARENTHESES);
      // 解析key
      TNS_PARSE(parse_params, TNS_KEY);
      // 得到key type
      TNS_GET_KEY_TYPE(parse_params);
      // 解析等于号, 上面得到的key不会被覆盖
      TNS_PARSE(parse_params, TNS_EQUAL_SIGN);
      // 这里处理所有address_list下面的选项
      if (OB_SUCC(ret)) {
        if (OBCLIENT_LB_ADDRESS == parse_params->key_type) {
          ret = ObClientAddressBuild(address_list, parse_params);
        } else if (OBCLIENT_LB_OBLB_STRATEGY == parse_params->key_type) {
          TNS_PARSE(parse_params, TNS_KEY);
          if (0 == strncasecmp(parse_params->tns_key, "RANDOM", parse_params->tns_key_len)) {
            address_list->oblb_strategy =  OBCLIENT_LB_OPTION_RANDOM;
          } else if (0 == strncasecmp(parse_params->tns_key, "SERVERAFFINITY", parse_params->tns_key_len)) {
            address_list->oblb_strategy = OBCLIENT_LB_OPTION_SERVERAFFINITY;
          } else if (0 == strncasecmp(parse_params->tns_key, "ROTATION", parse_params->tns_key_len)) {
            address_list->oblb_strategy = OBCLIENT_LB_OPTION_ROTATION;
          } else {
            ret = -1;
          }
        } else if (OBCLIENT_LB_OBLB == parse_params->key_type) {
          TNS_PARSE(parse_params, TNS_KEY);
          if (0 == strncasecmp(parse_params->tns_key, "ON", parse_params->tns_key_len)) {
            address_list->oblb = TRUE;
          } else if (0 == strncasecmp(parse_params->tns_key, "OFF", parse_params->tns_key_len)) {
            address_list->oblb = FALSE;
          } else {
            ret = -1;
          }
        } else {
          ret = -1;
        }
      }
      TNS_PARSE(parse_params, TNS_RIGHT_PARENTHESES);
    }
    if (TNS_RIGHT_PARENTHESES == parse_params->tns_token_type) {
      des->address_list_count++;
      ret = 0;
    } else {
      ret = -1;
    }
  }
  
  return ret;
}

int ObClientAddressInit(ObClientAddress *address)
{
  int ret = 0;

  if (OB_ISNULL(address)) {
    ret = -1;
  } else {
    memset(address, 0, sizeof(*address));
    address->weight = 1;
  }

  return ret;
}

int ObClientAddressClear(ObClientAddress *address)
{
  // now, do nothing
  UNUSED(address);
  return 0;
}

int ObClientAddressDisplay(ObClientAddress *address, FILE *display_file)
{
   int ret = 0;
  
  if (OB_ISNULL(address) || OB_ISNULL(display_file)) {
    ret = -1;
  } else {
    fprintf(display_file, "--------Address:\n");
    fprintf(display_file, "----------host:%.*s\n", address->host_len, address->host);
    fprintf(display_file, "----------port:%d\n", address->port);
    fprintf(display_file, "----------weight:%ld\n", address->weight);
  }

  return ret;
}

int ObClientAddressBuild(ObClientAddressList *address_list, ObClientTnsParseParams *parse_params)
{
  int ret = 0;
  ObClientAddress *address = NULL;

  if (OB_ISNULL(address_list) || OB_ISNULL(parse_params)) {
    ret = -1;
  } else if (OB_FAIL(ObClientAddressListCheck(address_list))) {
    ret = -1;
  } else if (OB_ISNULL(address = &(address_list->address[address_list->address_count]))) {
    ret = -1;
  } else if (OB_FAIL(ObClientAddressInit(address))) {
    ret = -1;
  } else {
    // 进入解析address流程
    // 循环处理pair
    while (OB_SUCC(ret)) {
      // 解析左括号
      TNS_PARSE(parse_params, TNS_LEFT_PARENTHESES);
      // 解析key
      TNS_PARSE(parse_params, TNS_KEY);
      // 得到key type
      TNS_GET_KEY_TYPE(parse_params);
      // 解析等于号, 上面得到的key不会被覆盖
      TNS_PARSE(parse_params, TNS_EQUAL_SIGN);
      // 这里处理所有address下面的选项
      if (OB_SUCC(ret)) {
        if (OBCLIENT_LB_PROTOCOL == parse_params->key_type) {
          TNS_PARSE_STRING_VALUE(parse_params, address->protocol, address->protocol_len, OBCLIENT_TNS_PORT_BUFFER_SIZE);
          if (address->protocol_len != 3 || 0 != strncasecmp(address->protocol, "TCP", address->protocol_len)) {
            ret = -1;
          }
        } else if (OBCLIENT_LB_HOST == parse_params->key_type) {
          TNS_PARSE_STRING_VALUE(parse_params, address->host, address->host_len, OBCLIENT_TNS_HOST_BUFFER_SIZE);
        } else if (OBCLIENT_LB_PORT == parse_params->key_type) {
          TNS_PARSE_INTRGER_VALUE(parse_params, address->port);
        } else if (OBCLIENT_LB_WEIGHT == parse_params->key_type) {
          TNS_PARSE_INTRGER_VALUE(parse_params, address->weight);
        } else {
          ret = -1;
        }
      }
      TNS_PARSE(parse_params, TNS_RIGHT_PARENTHESES);
    }
    if (TNS_RIGHT_PARENTHESES == parse_params->tns_token_type) {
      // 正常结束， 没有data，并且是右括号结束
      address_list->address_count++;
      ret = 0;
    } else {
      ret = -1;
    }
  }
  
  return ret;
}

int ObClientConnectDataInit(ObClientConnectData *con_data)
{
  int ret = 0;

  if (OB_ISNULL(con_data)) {
    ret = -1;
  } else {
    memset(con_data, 0, sizeof(*con_data));
  }

  return ret;
}

int ObClientConnectDataClear(ObClientConnectData *con_data)
{
  // now, do nothing
  UNUSED(con_data);
  return 0;
}

int ObClientConnectDataDisplay(ObClientConnectData *con_data, FILE *display_file)
{
  int ret = 0;
  
  if (OB_ISNULL(con_data) || OB_ISNULL(display_file)) {
    ret = -1;
  } else {
    fprintf(display_file, "--------ConnectData:\n");
    fprintf(display_file, "----------dbname:%.*s\n", con_data->service_name_len, con_data->service_name);
    if (0 != con_data->user_extra_info_len) {
      fprintf(display_file, "----------extra_info:%.*s\n", con_data->user_extra_info_len, con_data->user_extra_info);
      fprintf(display_file, "----------obmode:%ld\n", con_data->ob_mode);
      fprintf(display_file, "----------use_default_sid:%ld\n", con_data->use_default_sid);
    }
  }

  return ret;
}

int ObClientConnectDataBuild(ObClientDescription *des, ObClientTnsParseParams *parse_params)
{
  int ret = 0;
  ObClientConnectData *connect_data = NULL;

  if (OB_ISNULL(des) || OB_ISNULL(parse_params)) {
    ret = -1;
  } else if (OB_ISNULL(connect_data = malloc(sizeof(*connect_data)))) {
    ret = -1;
  } else if (OB_FAIL(ObClientConnectDataInit(connect_data))) {
    ret = -1;
  } else {
    int ret = 0;
    // 进入解析conect_data流程
    // 循环处理pair
    while (OB_SUCC(ret)) {
      // 解析左括号
      TNS_PARSE(parse_params, TNS_LEFT_PARENTHESES);
      // 解析key
      TNS_PARSE(parse_params, TNS_KEY);
      // 得到key type
      TNS_GET_KEY_TYPE(parse_params);
      // 解析等于号, 上面得到的key不会被覆盖
      TNS_PARSE(parse_params, TNS_EQUAL_SIGN);
      // 这里处理所有address_list下面的选项
      if (OB_SUCC(ret)) {
        if (OBCLIENT_LB_SERVICE_NAME == parse_params->key_type || OBCLIENT_LB_SID == parse_params->key_type) {
          TNS_PARSE_STRING_VALUE(parse_params, connect_data->service_name, connect_data->service_name_len, OBCLIENT_TNS_KEY_SIZE);
        } else if (OBCLIENT_LB_EXTRA_INFO == parse_params->key_type) {
          TNS_PARSE_STRING_VALUE(parse_params, connect_data->user_extra_info, connect_data->user_extra_info_len, OBCLIENT_TNS_KEY_SIZE);
        } else if (OBCLIENT_LB_OB_MODE == parse_params->key_type) {
          TNS_PARSE_INTRGER_VALUE(parse_params, connect_data->ob_mode);
        } else if (OBCLIENT_LB_USE_DEFAULT_SID == parse_params->key_type) {
          TNS_PARSE_INTRGER_VALUE(parse_params, connect_data->use_default_sid);
        } else {
          ret = -1;
        }
      }
      TNS_PARSE(parse_params, TNS_RIGHT_PARENTHESES);
    }
    if (TNS_RIGHT_PARENTHESES == parse_params->tns_token_type) {
      // 正常结束， 没有data，并且是右括号结束
      if (OB_ISNULL(des->connect_data)) {
        des->connect_data = connect_data;
        ret = 0;
      } else {
        // error
        free(connect_data);
        ret = -1;
      }
    } else {
      // 解析过程中出现错误，释放空间
      free(connect_data);
      ret = -1;
    }
  }
  
  return ret;
}

int ObClientSystemVariablesInit(ObClientSystemVariables *sys_vars)
{
  int ret = 0;
  if (OB_ISNULL(sys_vars)) {
    ret = -1;
  } else {
    memset(sys_vars, 0, sizeof(*sys_vars));
    sys_vars->session_variable_len = 0;
  }
  return ret;
}

int ObClientSystemVariablesClear(ObClientSystemVariables *sys_vars)
{
  UNUSED(sys_vars);
  return 0;
}

int ObClientSystemVariablesDisplay(ObClientSystemVariables *sys_vars, FILE *display_file)
{
  int ret = 0;
  
  if (OB_ISNULL(sys_vars) || OB_ISNULL(display_file)) {
    ret = -1;
  } else {
    fprintf(display_file, "--------SYSTEMVARIABLE:\n");
    fprintf(display_file, "----------SESSION_VARIABLE:%.*s\n", sys_vars->session_variable_len, sys_vars->session_variable);
  }

  return ret;
}

int ObClientSystemVariablesBuild(ObClientSystemVariables *sys_vars, ObClientTnsParseParams *parse_params)
{
  int ret = 0;

  if (OB_ISNULL(sys_vars) || OB_ISNULL(parse_params)) {
    ret = -1;
  } else {
    if (OBCLIENT_LB_SESSION_VARIABLE == parse_params->key_type) {
      TNS_PARSE_STRING_VALUE(parse_params, sys_vars->session_variable, sys_vars->session_variable_len, OBCLIENT_TNS_KEY_SIZE);
    } else {
      TNS_PARSE(parse_params, TNS_KEY);
      ret = 0;
    }
  }

  return ret;
}

int ObClientTnsCheckBuffer(ObClientTnsParseParams *parse_params)
{
  int ret = 0;

  if (OB_ISNULL(parse_params)) {
    ret = -1;
  } else if (OB_LIKELY(parse_params->buffer_pos < parse_params->buffer_len)) {
    // there are still buffer, do nothing
  } else {
    if (parse_params->is_eof == TRUE) {
      // eof, return error
      ret = -1;
    } else {
      // no data in buffer
      if (parse_params->mode == TNS_MODE_DESCRIPTION) {
        int len = parse_params->description_len - parse_params->description_offset;
        len = len > OBCLIENT_TNS_BUFFER_SIZE ? OBCLIENT_TNS_BUFFER_SIZE : len;
        memcpy(parse_params->tns_buffer, parse_params->description + parse_params->description_offset, len);
        parse_params->buffer_len = len;
        parse_params->buffer_pos = 0;
        parse_params->description_offset+=len;
        if (0 == parse_params->buffer_len) {
          parse_params->is_eof = 1;
          ret = -1;
        }
      } else {
        parse_params->buffer_len = fread(parse_params->tns_buffer, 1, OBCLIENT_TNS_BUFFER_SIZE, parse_params->tns_file);
        parse_params->buffer_pos = 0;
        if (0 == parse_params->buffer_len) {
          parse_params->is_eof = 1;   // set eof
          ret = -1;
        }
      }
    }
  }

  return ret;
}

int ObClientTnsParse(ObClientTnsParseParams *parse_params, ObClientTnsTokenType expect_type)
{
  int ret = 0;

  if (OB_ISNULL(parse_params)) {
    ret = -1;
  } else if (OB_FAIL(ObClientTnsParseBlank(parse_params))) {
    ;
  } else {
    char ch = parse_params->tns_buffer[parse_params->buffer_pos];
    ObClientTnsTokenType got_type;
    switch (ch)
    {
    case '=':
    {
      got_type = TNS_EQUAL_SIGN;
      break;
    }
    case '(':
    {
      got_type = TNS_LEFT_PARENTHESES;
      break;
    }
    case ')':
    {
      got_type = TNS_RIGHT_PARENTHESES;
      break;
    }
    default:
    {
      got_type = TNS_KEY;
      break;
    }
    }
    if (got_type != expect_type) {
      ret = -1;
    } else if (TNS_KEY == got_type) {
      parse_params->tns_token_type = got_type;
      if (OB_FAIL(ObClientTnsParseKey(parse_params))) {
        ;
      }
    } else {
      // 除了key之外的token
      parse_params->tns_token_type = got_type;
      parse_params->buffer_pos++;
    }
  }

  return ret;
}

int ObClientTnsParseBlank(ObClientTnsParseParams *parse_params)
{
  int ret = 0;

  if (OB_ISNULL(parse_params)) {
    ret = -1;
  } else {
    char ch;    

    while (OB_SUCC(ret)) {
      if (OB_FAIL(ObClientTnsCheckBuffer(parse_params))) {
        if (TRUE == parse_params->is_eof) {
          ret = TNS_NO_DATA;
        } else {
          ;
        }
      } else {
        while (parse_params->buffer_pos < parse_params->buffer_len) {
          ch = parse_params->tns_buffer[parse_params->buffer_pos];
          if ('\t' == ch || '\n' == ch || ' ' == ch) {
            parse_params->buffer_pos++;
          } else if ('#' == ch) { // ignore comments
            unsigned int tmp = parse_params->buffer_pos;
            for (; tmp < parse_params->buffer_len; tmp++) {
              char tmpch = parse_params->tns_buffer[tmp];
              if ('\n' == tmpch || '\0' == tmpch)
                break;
            }
            parse_params->buffer_pos = tmp;
          } else {
            break;
          }
        }
        if (parse_params->buffer_pos < parse_params->buffer_len) {
          // jump to blank
          break;
        }
      }
    }
  }

  return ret;
}

int ObClientTnsParseKey(ObClientTnsParseParams *parse_params)
{
  int ret = 0;

  if (OB_ISNULL(parse_params)) {
    ret = -1;
  } else {
    char ch;    
    unsigned int pos = 0;
    ObClientTnsParseStatus parse_status = TNS_PARSE_NORMAL;

    while (OB_SUCC(ret)) {
      if (OB_FAIL(ObClientTnsCheckBuffer(parse_params))) {
        if (1 == parse_params->is_eof) {
          ret = TNS_NO_DATA;
        } else {
        }
      } else {
        while (OB_SUCC(ret) && parse_params->buffer_pos < parse_params->buffer_len && pos < OBCLIENT_TNS_KEY_SIZE) {
          if (TNS_PARSE_NORMAL == parse_status) {
            ch = parse_params->tns_buffer[parse_params->buffer_pos];
            if ('\"' == ch) {
              parse_status = TNS_PARSE_QUOTE;
              parse_params->buffer_pos++;
            } else if ((' ' != ch) && ('\n' != ch) && ('\r' != ch) 
              && ('\t' != ch) && ('=' != ch) && ('(' != ch)
              && (')' != ch)) {
              parse_params->tns_key[pos++] = ch;
              parse_params->buffer_pos++;
            } else {
              break;
            }
          } else if (TNS_PARSE_QUOTE == parse_status) {
            ch = parse_params->tns_buffer[parse_params->buffer_pos];
            if ('\"' == ch) {
              parse_status = TNS_PARSE_NORMAL;
              parse_params->buffer_pos++;
            } else {
              parse_params->tns_key[pos++] = ch;
              parse_params->buffer_pos++;
            }
          } else {
            ret = -1;
          }
        }
        if (OB_SUCC(ret)) {
          if (parse_params->buffer_pos < parse_params->buffer_len) {
            parse_params->tns_key_len = pos;
            break;
          } else if (pos == OBCLIENT_TNS_KEY_SIZE) {
            ret = -1;
          }
        }
      }
    }
  }

  return ret;
}

int ObClientTnsGetKeyType(ObClientTnsParseParams *parse_params)
{
  int ret = 0;
  unsigned int arr_size = sizeof(tns_key_array_size) / sizeof(unsigned int)-1;

  if (OB_ISNULL(parse_params)) {
    ret = -1;
  } else {
    if (parse_params->tns_key_len <= arr_size) {
      unsigned int len = parse_params->tns_key_len;
      const char **key_array = (const char **)tns_key_array[len];
      ObClientLBKeyType *keytype_array = (ObClientLBKeyType *)tns_keytype_array[len];
      unsigned int key_array_size = tns_key_array_size[len];
      parse_params->key_type = OBCLIENT_LB_ERROR_KEY;

      if (key_array_size) {
        unsigned int i = 0;
        for (; i < key_array_size; ++i) {
          if (key_array[i] && 0 == strncasecmp(parse_params->tns_key, key_array[i], len)) {
            parse_params->key_type = keytype_array[i];
          }
        }
      }

      if (OBCLIENT_LB_ERROR_KEY == parse_params->key_type) {
        ret = -1;
      } else {
        ret = 0;
      }
    } else {
      parse_params->key_type = OBCLIENT_LB_ERROR_KEY;
    }
  }

  return ret;
}

