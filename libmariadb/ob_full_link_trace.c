#include "ob_full_link_trace.h"

#include <time.h>
#include <string.h>
#include <stdlib.h>
#if defined(_WIN32)
#include <windows.h>
#include <wincrypt.h>
#else
#include <sys/time.h>
#endif

#include "ob_rwlock.h"
#include "ma_global.h"
#include "mariadb_com.h"
#include "ob_protocol20.h"
#include "ob_serialize.h"

#define TYPE_LENGTH 2
#define LEN_LENGTH 4
#define FLT_SIZE sizeof(float)
#define DBL_SIZE sizeof(double)

#define UUID_PATTERN "%8.8lx-%4.4lx-%4.4lx-%4.4lx-%12.12lx"
#define TRACE_PATTERN "{\"trace_id\":\""UUID_PATTERN"\",\"name\":\"%s\",\"id\":\""UUID_PATTERN"\",\"start_ts\":%ld,\"end_ts\":%ld,\"parent_id\":\""UUID_PATTERN"\",\"is_follow\":%s"
#define UUID_TOSTRING(uuid)                                                   \
((uuid).high_ >> 32), ((uuid).high_ >> 16 & 0xffff), ((uuid).high_ & 0xffff), \
((uuid).low_ >> 48), ((uuid).low_ & 0xffffffffffff)
#define INIT_SPAN(trace, span)                                    \
if (OB_NOT_NULL(span) && 0 == span->span_id_.high_) {             \
  span->span_id_.low_ = xorshift128plus(trace->uuid_random_seed); \
  span->span_id_.high_ = span->start_ts_;                         \
}

#define sint1korr(A)    (*((int8_t*)(A)))

#define FLT_EXTRA_INFO_DEF(id, type) (id2type[id] = type)
#define FLT_EXTRA_INFO_INIT()                                             \
  do                                                                      \
  {                                                                       \
    /* FLT_DRIVER_SPAN_INFO */                                            \
    FLT_EXTRA_INFO_DEF(FLT_DRIVER_SPAN , MYSQL_TYPE_VAR_STRING);          \
    /* APP_INFO */                                                        \
    FLT_EXTRA_INFO_DEF(FLT_CLIENT_IDENTIFIER, MYSQL_TYPE_VAR_STRING);     \
    FLT_EXTRA_INFO_DEF(FLT_MODULE, MYSQL_TYPE_VAR_STRING);                \
    FLT_EXTRA_INFO_DEF(FLT_ACTION, MYSQL_TYPE_VAR_STRING);                \
    FLT_EXTRA_INFO_DEF(FLT_CLIENT_INFO, MYSQL_TYPE_VAR_STRING);           \
    /* QUERY_INFO */                                                      \
    FLT_EXTRA_INFO_DEF(FLT_QUERY_START_TIMESTAMP, MYSQL_TYPE_LONGLONG);   \
    FLT_EXTRA_INFO_DEF(FLT_QUERY_END_TIMESTAMP, MYSQL_TYPE_LONGLONG);     \
    /* CONTROL_INFO */                                                    \
    FLT_EXTRA_INFO_DEF(FLT_LEVEL, MYSQL_TYPE_TINY);                       \
    FLT_EXTRA_INFO_DEF(FLT_SAMPLE_PERCENTAGE, MYSQL_TYPE_DOUBLE);         \
    FLT_EXTRA_INFO_DEF(FLT_RECORD_POLICY, MYSQL_TYPE_TINY);               \
    FLT_EXTRA_INFO_DEF(FLT_PRINT_SAMPLE_PCT, MYSQL_TYPE_DOUBLE);          \
    FLT_EXTRA_INFO_DEF(FLT_SLOW_QUERY_THRES, MYSQL_TYPE_LONGLONG);        \
    /* SPAN_INFO */                                                       \
    FLT_EXTRA_INFO_DEF(FLT_TRACE_ENABLE, MYSQL_TYPE_TINY);                \
    FLT_EXTRA_INFO_DEF(FLT_FORCE_PRINT, MYSQL_TYPE_TINY);                 \
    FLT_EXTRA_INFO_DEF(FLT_TRACE_ID, MYSQL_TYPE_VAR_STRING);              \
    FLT_EXTRA_INFO_DEF(FLT_REF_TYPE, MYSQL_TYPE_TINY);                    \
    FLT_EXTRA_INFO_DEF(FLT_SPAN_ID, MYSQL_TYPE_VAR_STRING);               \
    FLT_EXTRA_INFO_DEF(FLT_EXTRA_INFO_ID_END, MAX_NO_FIELD_TYPES);        \
  } while (0);
#define FLT_SERIALIZE_FUNC_SET(id, funcname) (flt_funcs[id] = (FLTFunc)FLT_SERIALIZE_FUNC(funcname))
#define FLT_SERIALIZE_FUNC_INIT()                                         \
  do                                                                      \
  {                                                                       \
    /* FLT_DRIVER_SPAN_INFO */                                            \
    FLT_SERIALIZE_FUNC_SET(FLT_DRIVER_SPAN_INFO, driverspaninfo);         \
    /* APP_INFO */                                                        \
    FLT_SERIALIZE_FUNC_SET(FLT_APP_INFO, appinfo);                        \
    /* QUERY_INFO */                                                      \
    FLT_SERIALIZE_FUNC_SET(FLT_QUERY_INFO, queryinfo);                    \
    /* CONTROL_INFO */                                                    \
    FLT_SERIALIZE_FUNC_SET(FLT_CONTROL_INFO, controlinfo);                \
    /* SPAN_INFO */                                                       \
    FLT_SERIALIZE_FUNC_SET(FLT_SPAN_INFO, spaninfo);                      \
  } while (0);

const char *tag_str[FLT_TAG_MAX_TYPE] = 
{
  "command_name",
  "client_host"
};

static ob_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;
static int init_flag = 0;
static enum_field_types id2type[FLT_EXTRA_INFO_ID_END + 1];
static FLTFunc flt_funcs[FLT_EXTRA_INFO_TYPE_END + 1];

static uint64_t xorshift128plus(uint64_t *s);

inline void flt_set_send_trans_flag(FLTInfo *flt, my_bool flag)
{
  if (OB_NOT_NULL(flt)) {
    flt->in_trans_ = flag;
  }
}

static inline int64_t get_current_time_us()
{
  struct timeval tv;
  // todo: Here to be compatible with windows
  gettimeofday(&tv, NULL);
  return tv.tv_sec * 1000000 + tv.tv_usec;
}

static inline double flt_get_pct(uint64_t *seed)
{
  uint64_t rand64;
  /* Use the method of generating random numbers in uuid to get random numbers */
  rand64 = xorshift128plus(seed);

  return (double)rand64 / UINT64_MAX;
}

int flt_init(FLTInfo *flt)
{
  int ret = OB_SUCCESS; 

  if (OB_NOT_NULL(flt)) {
    if (!init_flag) {
      ob_mutex_lock(&init_mutex);
      if (!init_flag) {
        FLT_EXTRA_INFO_INIT();                // init id2type
        FLT_SERIALIZE_FUNC_INIT();            // init flt_funcs
        init_flag = 1;
      }
      ob_mutex_unlock(&init_mutex);
    }

    memset(flt, 0, sizeof(*flt));

    flt->client_span_.type_ = FLT_DRIVER_SPAN_INFO;
    flt->app_info_.type_ = FLT_APP_INFO;
    flt->query_info_.type_ = FLT_QUERY_INFO;
    flt->span_info_.type_ = FLT_SPAN_INFO;
    flt->control_info_.type_ = FLT_CONTROL_INFO;
    flt->in_trans_ = FALSE;

    // init control info
    flt->control_info_.level_ = -1;
    flt->control_info_.sample_pct_ = -1;
    flt->control_info_.rp_ = MAX_RECORD_POLICY;
    flt->control_info_.print_sample_pct_ = -1;
    flt->control_info_.slow_query_threshold_ = -1;

    if (OB_FAIL(trace_init(flt))) {
      // trace init error;
    } 
  }

  return ret;
}

void flt_end(FLTInfo *flt)
{
  trace_end(flt);
}

my_bool flt_is_vaild(FLTInfo *flt)
{
  my_bool ret = FALSE;
  if (OB_ISNULL(flt)) {
    // do nothing
  } else {
    FLTControlInfo *control = &flt->control_info_;
    if (control->level_ > 0 && (control->sample_pct_ >= 0 && control->sample_pct_ <= 1) && control->rp_ < MAX_RECORD_POLICY &&
        (control->print_sample_pct_ >= 0 && control->print_sample_pct_ <= 1)) {
      ret = TRUE;
    }
  }
  return ret;
}

int flt_build_request(MYSQL *mysql, FLTInfo *flt)
{
  int ret = OB_SUCCESS;
  ObTrace *trace = OBTRACE(flt);
  ObSpanCtx *span;

  if (OB_ISNULL(trace) || OB_ISNULL(flt)) {
    ret = OB_ERROR;
  } else {
    // build request
    int32_t serialize_size = 0;
    int32_t span_info_serialize_size = 0;
    int32_t client_log_serialize_size = 0;
    int32_t app_info_serialize_size = 0;
    int64_t tmp_trace_id_pos = 0;
    int64_t tmp_span_id_pos = 0;

    span = trace->last_active_span_;

    if (TRUE == trace->trace_enable_ && NULL != span) {
      INIT_SPAN(trace, span);
      // When trace enable is true, span info needs to be sent
      if (OB_FAIL(serialize_UUID(flt->trace_id_, UUID4_SERIALIZE_LEN, &tmp_trace_id_pos, &trace->trace_id_))) {
        // error
      } else if (OB_FAIL(serialize_UUID(flt->span_id_, UUID4_SERIALIZE_LEN, &tmp_span_id_pos, &span->span_id_))) {
        // error
      } else {
        flt->span_info_.trace_id_ = flt->trace_id_;
        flt->span_info_.span_id_ = flt->span_id_;
        flt->span_info_.type_ = FLT_SPAN_INFO; 
        flt->span_info_.trace_enable_ = trace->trace_enable_;
        flt->span_info_.force_print_ = trace->force_print_;
        if (trace->force_print_) {
          FLUSH_TRACE(flt);
        }
      }
    } else {
      flt->span_info_.trace_enable_ = FALSE;
    }

    if (OB_SUCC(ret)) {
      if (0 != trace->log_buf_offset_) {
        flt->client_span_.client_span_ = trace->log_buf_;
      } else {
        flt->client_span_.client_span_ = NULL;
      }

      if (OB_FAIL(flt_get_serialize_size_extra_info(&app_info_serialize_size, &flt->app_info_))) {
        // error
      } else if (OB_FAIL(flt_get_serialize_size_extra_info(&client_log_serialize_size, &flt->client_span_))) {
        // error
      } else if (OB_FAIL(flt_get_serialize_size_extra_info(&span_info_serialize_size, &flt->span_info_))) {
        // error
      } else {
        serialize_size += app_info_serialize_size;
        serialize_size += client_log_serialize_size;
        serialize_size += span_info_serialize_size;
#ifdef DEBUG_OB20
          printf("spaninfo ssize is %d, log ssize is %d, appinfo ssize %d\n", span_info_serialize_size, client_log_serialize_size, app_info_serialize_size);
          if (0 != client_log_serialize_size) {
            printf("log:%s\n", flt->client_span_.client_span_);
          }
#endif
      }

      if (OB_SUCC(ret) && 0 != serialize_size) {
        if (MAX_FLT_SERIALIZE_SIZE < serialize_size) {
          ret = OB_ERROR;
        } else {
          int64_t pos = 0;

          flt->flt_value_data_.length = serialize_size;
          flt->flt_value_data_.value_data_ = trace->flt_serialize_buf_;
        
          if (OB_FAIL(flt_serialize_extra_info(flt->flt_value_data_.value_data_, serialize_size, &pos, &flt->span_info_))) {
            // error
          } else if (pos != span_info_serialize_size) {
            ret = OB_ERROR;
          } else if (0 != client_log_serialize_size && OB_FAIL(flt_serialize_extra_info(flt->flt_value_data_.value_data_, serialize_size, &pos, &flt->client_span_))) {
            // error
          } else if (pos != span_info_serialize_size + client_log_serialize_size) {
            ret = OB_ERROR;
          } else if (0 != app_info_serialize_size && OB_FAIL(flt_serialize_extra_info(flt->flt_value_data_.value_data_, serialize_size, &pos, &flt->app_info_))) {
            // error
          } else if (pos != serialize_size) {
            ret = OB_ERROR;
          } else {
            if (0 != app_info_serialize_size) {
              // clear app info
              memset(&flt->app_info_, 0, sizeof(flt->app_info_));  
              flt->app_info_.type_ = FLT_APP_INFO;
            }
            // reset the log buf offset
            trace->log_buf_offset_ = 0;
            flt->client_span_.client_span_ = NULL;
          }
          if (OB_SUCC(ret)) {
            if (OB_FAIL(ob20_set_extra_info(mysql, FULL_TRC, &flt->flt_value_data_))) {
              // error
            }
          }
        }
      }
    }
  }

  return ret;
}

int flt_deserialize_extra_info(const char *buf, const int64_t len, int64_t *pos, FullLinkTraceExtraInfoType type, void *flt_info)
{
  int ret = OB_SUCCESS;
  while (OB_SUCC(ret) && *pos < len) {
    int32_t val_len = 0;
    int16_t extra_id;
    if (OB_FAIL(flt_resolve_type_and_len(buf, len, pos, &extra_id, &val_len))) {
      ret = OB_ERROR;
    } else if (OB_FAIL(flt_funcs[type].deserialize_field_func((FullLinkTraceExtraInfoId)(extra_id), val_len, buf, len, pos, flt_info))) {
      ret = OB_ERROR;
    } else {
      // do nothing
    }
  }
  return ret;
}

int flt_serialize_extra_info(char *buf, const int64_t len, int64_t *pos, void *flt_info)
{
  int ret = OB_SUCCESS;
  FullLinkTraceExtraInfoType type = ((FLTInfoBase *)flt_info)->type_;

  ret = flt_funcs[type].serialize_func(buf, len, pos, flt_info);

  return ret;
}

int flt_get_serialize_size_extra_info(int32_t *size, void *flt_info)
{
  int ret = OB_SUCCESS;
  FullLinkTraceExtraInfoType type = ((FLTInfoBase *)flt_info)->type_;

  ret = flt_funcs[type].get_serialize_size_func(size, flt_info);

  return ret;
}

// for control info
int flt_get_serialize_size_controlinfo(int32_t *size, void *flt_info)
{
  int ret = OB_SUCCESS;
  int32_t local_size = 0;
  UNUSED(flt_info);

  local_size += TYPE_LENGTH + LEN_LENGTH;
  local_size += flt_get_store_int1_size();
  local_size += flt_get_store_double_size();
  local_size += flt_get_store_int1_size();
  local_size += flt_get_store_double_size();
  local_size += flt_get_store_int8_size();

  *size = local_size;
  return ret;
}

int flt_serialize_controlinfo(char *buf, const int64_t len, int64_t *pos, void *flt_info)
{
  int ret = OB_SUCCESS;
  int64_t org_pos = *pos;
  FLTControlInfo *control_info = (FLTControlInfo *)flt_info;
  // resrver for type and len
  if (*pos + 6 > len) {
    ret = OB_SIZE_OVERFLOW;
  } else {
    *pos += 6;
  }
  if (OB_FAIL(ret)) {
    // do nothing
  } else if (OB_FAIL(flt_store_int1(buf, len, pos, control_info->level_, FLT_LEVEL))) {
    ret = OB_ERROR;
  } else if (OB_FAIL(flt_store_double(buf, len, pos, control_info->sample_pct_, FLT_SAMPLE_PERCENTAGE))) {
    ret = OB_ERROR;
  } else if (OB_FAIL(flt_store_int1(buf, len, pos, control_info->rp_, FLT_RECORD_POLICY))) {
    ret = OB_ERROR;
  } else if (OB_FAIL(flt_store_double(buf, len, pos, control_info->print_sample_pct_, FLT_PRINT_SAMPLE_PCT))) {
    ret = OB_ERROR;
  } else if (OB_FAIL(flt_store_int8(buf, len, pos, control_info->slow_query_threshold_, FLT_SLOW_QUERY_THRES))) {
    ret = OB_ERROR;
  } else {
    // fill type and len in the head
    int32_t total_len = *pos - org_pos - 6;
    if (OB_FAIL(flt_store_type_and_len(buf, len, &org_pos, control_info->type_, total_len))) {
      ret = OB_ERROR;
    } else {
      // do nothing
    }
  }
  return ret;
}

int flt_deserialize_field_controlinfo(FullLinkTraceExtraInfoId extra_id, const int64_t v_len,
                                        const char *buf, const int64_t len, int64_t *pos, void *flt_info)
{
  int ret = OB_SUCCESS;
  FLTControlInfo *flt_control_info = (FLTControlInfo *)flt_info;
  switch(extra_id) {
    case FLT_LEVEL: {
      if (OB_FAIL(flt_get_int1(buf, len, pos, v_len, &flt_control_info->level_))) {
        ret = OB_ERROR;
      } else {
        // do nothing
#ifdef DEBUG_OB20
        printf("level is %hhd\n", flt_control_info->level_);
#endif
      }
      break;
    }
    case FLT_SAMPLE_PERCENTAGE: {
      if (OB_FAIL(flt_get_double(buf, len, pos, v_len, &flt_control_info->sample_pct_))) {
        ret = OB_ERROR;
      } else {
        // do nothing
#ifdef DEBUG_OB20
          printf("sample_pct is %f\n", flt_control_info->sample_pct_);
#endif
      }
      break;
    }
    case FLT_RECORD_POLICY: {
      int8_t v = 0;
      if (OB_FAIL(flt_get_int1(buf, len, pos, v_len, &v))) {
        ret = OB_ERROR;
      } else {
        flt_control_info->rp_ = v;
#ifdef DEBUG_OB20
          printf("rp is %d\n", flt_control_info->rp_);
#endif
      }
      break;
    }
    case FLT_PRINT_SAMPLE_PCT: {
      if (OB_FAIL(flt_get_double(buf, len, pos, v_len, &flt_control_info->print_sample_pct_))) {
        ret = OB_ERROR;
      } else {
        // do nothing
#ifdef DEBUG_OB20
          printf("print_sample_pct is %f\n", flt_control_info->print_sample_pct_);
#endif
      }
      break;
    }
    case FLT_SLOW_QUERY_THRES: {
      if (OB_FAIL(flt_get_int8(buf, len, pos, v_len, &flt_control_info->slow_query_threshold_))) {
        ret = OB_ERROR;
      } else {
        // do nothing
#ifdef DEBUG_OB20
          printf("slow_query_threshold is %ld\n", flt_control_info->slow_query_threshold_);
#endif
      }
      break;
    }
    default: {
      // 这里碰到不能识别的key直接跳过
      *pos += *pos + v_len;
      break;
    }
  }
  return ret;
}
// for control info

// for app info
int flt_get_serialize_size_appinfo(int32_t *size, void *flt_info)
{
  int ret = OB_SUCCESS;
  int32_t local_size = 0;
  FLTAppInfo *app_info = (FLTAppInfo *)flt_info;

  if (OB_NOT_NULL(app_info->identifier_)) {
    local_size += flt_get_store_str_size(strlen(app_info->identifier_));
  }
  if (OB_NOT_NULL(app_info->module_)) {
    local_size += flt_get_store_str_size(strlen(app_info->module_));
  }
  if (OB_NOT_NULL(app_info->action_)) {
    local_size += flt_get_store_str_size(strlen(app_info->action_));
  }
  if (OB_NOT_NULL(app_info->client_info_)) {
    local_size += flt_get_store_str_size(strlen(app_info->client_info_));
  }
  if (0 != local_size) {
    local_size += TYPE_LENGTH + LEN_LENGTH;
  }

  *size = local_size;
  return ret;
}

int flt_serialize_appinfo(char *buf, const int64_t len, int64_t *pos, void *flt_info)
{
  int ret = OB_SUCCESS;
  int64_t org_pos = *pos;
  FLTAppInfo *app_info = (FLTAppInfo *)flt_info;
  // resrver for type and len
  if (*pos + 6 > len) {
    ret = OB_SIZE_OVERFLOW;
  } else {
    *pos += 6;
  }
  if (OB_FAIL(ret)) {
    // do nothing
  } else if (OB_NOT_NULL(app_info->identifier_) && OB_FAIL(flt_store_str(buf, len, pos, app_info->identifier_, strlen(app_info->identifier_), FLT_CLIENT_IDENTIFIER))) {
    ret = OB_ERROR;
  } else if (OB_NOT_NULL(app_info->module_) && OB_FAIL(flt_store_str(buf, len, pos, app_info->module_, strlen(app_info->module_), FLT_MODULE))) {
    ret = OB_ERROR;
  } else if (OB_NOT_NULL(app_info->action_) && OB_FAIL(flt_store_str(buf, len, pos, app_info->action_, strlen(app_info->action_), FLT_ACTION))) {
    ret = OB_ERROR;
  } else if (OB_NOT_NULL(app_info->client_info_) && OB_FAIL(flt_store_str(buf, len, pos, app_info->client_info_, strlen(app_info->client_info_), FLT_CLIENT_INFO))) {
    ret = OB_ERROR;
  } else {
    // fill type and len in the head
    int32_t total_len = *pos - org_pos - 6;
    if (OB_FAIL(flt_store_type_and_len(buf, len, &org_pos, app_info->type_, total_len))) {
      ret = OB_ERROR;
    } else {
      // do nothing
    }
  }
  return ret;
}

int flt_deserialize_field_appinfo(FullLinkTraceExtraInfoId extra_id, const int64_t v_len,
                                        const char *buf, const int64_t len, int64_t *pos, void *flt_info)
{
  int ret = OB_SUCCESS;
  FLTAppInfo *app_info = (FLTAppInfo *)flt_info;
  switch(extra_id) {
    case FLT_CLIENT_IDENTIFIER: {
      if (OB_FAIL(flt_get_str(buf, len, pos, v_len, (char **)&app_info->identifier_))) {
        ret = OB_ERROR;
      } else {
        // do nothing
      }
      break;
    }
    case FLT_MODULE: {
      if (OB_FAIL(flt_get_str(buf, len, pos, v_len, (char **)&app_info->module_))) {
        ret = OB_ERROR;
      } else {
        // do nothing
      }
      break;
    }
    case FLT_ACTION: {
      if (OB_FAIL(flt_get_str(buf, len, pos, v_len, (char **)&app_info->action_))) {
        ret = OB_ERROR;
      } else {
        // do nothing
      }
      break;
    } 
    case FLT_CLIENT_INFO: {
      if (OB_FAIL(flt_get_str(buf, len, pos, v_len, (char **)&app_info->client_info_))) {
        ret = OB_ERROR;
      } else {
        // do nothing
      }
      break;
    }
    default: {
      // get an unrecognized key here, skip it directly
      *pos += *pos + v_len;
      break;
    }
  }
  return ret;
}
// for app info

// for queryinfo
int flt_get_serialize_size_queryinfo(int32_t *size, void *flt_info)
{
  int ret = OB_SUCCESS;
  int32_t local_size = 0;
  UNUSED(flt_info);

  local_size += TYPE_LENGTH + LEN_LENGTH;
  local_size += flt_get_store_int8_size();
  local_size += flt_get_store_int8_size();

  *size = local_size;
  return ret;
}

int flt_serialize_queryinfo(char *buf, const int64_t len, int64_t *pos, void *flt_info)
{
  int ret = OB_SUCCESS;
  int64_t org_pos = *pos;
  FLTQueryInfo *query_info = (FLTQueryInfo *)flt_info;
  // resrver for type and len
  if (*pos + 6 > len) {
    ret = OB_SIZE_OVERFLOW;
  } else {
    *pos += 6;
  }
  if (OB_FAIL(ret)) {
    // do nothing
  } else if (OB_FAIL(flt_store_int8(buf, len, pos, query_info->query_start_timestamp_, FLT_QUERY_START_TIMESTAMP))) {
    ret = OB_ERROR;
  } else if (OB_FAIL(flt_store_int8(buf, len, pos, query_info->query_end_timestamp_, FLT_QUERY_END_TIMESTAMP))) {
    ret = OB_ERROR;
  } else {
    // fill type and len in the head
    int32_t total_len = *pos - org_pos - 6;
    if (OB_FAIL(flt_store_type_and_len(buf, len, &org_pos, query_info->type_, total_len))) {
      ret = OB_ERROR;
    } else {
      // do nothing
    }
  }
  return ret;
}
int flt_deserialize_field_queryinfo(FullLinkTraceExtraInfoId extra_id, const int64_t v_len,
                                        const char *buf, const int64_t len, int64_t *pos, void *flt_info)
{
  int ret = OB_SUCCESS;
  FLTQueryInfo *query_info = (FLTQueryInfo *)flt_info;
  switch(extra_id) {
    case FLT_QUERY_START_TIMESTAMP: {
      if (OB_FAIL(flt_get_int8(buf, len, pos, v_len, &query_info->query_start_timestamp_))) {
        ret = OB_ERROR;
      } else {
        // do nothing
      }
      break;
    }
    case FLT_QUERY_END_TIMESTAMP: {
      if (OB_FAIL(flt_get_int8(buf, len, pos, v_len, &query_info->query_end_timestamp_))) {
        ret = OB_ERROR;
      } else {
        // do nothing
      }
      break;
    }
    default: {
      // get an unrecognized key here, skip it directly
      *pos += *pos + v_len;
      break;
    }
  }
  return ret;
}
// for queryinfo

// for spaninfo
int flt_get_serialize_size_spaninfo(int32_t *size, void *flt_info)
{
  int ret = OB_SUCCESS;
  int32_t local_size = 0;
  FLTSpanInfo *span_info = (FLTSpanInfo *)flt_info;
  // FLTSpanInfo *span_info = (FLTSpanInfo *)flt_info;

  // If trace is enabled, other fields need to be sent
  if (TRUE == span_info->trace_enable_) {
    local_size += TYPE_LENGTH + LEN_LENGTH;
    local_size += flt_get_store_int1_size();
    local_size += flt_get_store_int1_size();
    local_size += flt_get_store_str_size(UUID4_SERIALIZE_LEN);
    local_size += flt_get_store_int1_size();
    local_size += flt_get_store_str_size(UUID4_SERIALIZE_LEN);
  }

  *size = local_size;
  return ret;
}

int flt_serialize_spaninfo(char *buf, const int64_t len, int64_t *pos, void *flt_info)
{
  int ret = OB_SUCCESS;
  int64_t org_pos = *pos;
  FLTSpanInfo *span_info = (FLTSpanInfo *)flt_info;
  // resrver for type and len
  if (TRUE == span_info->trace_enable_) {
    if (*pos + 6 > len) {
      ret = OB_SIZE_OVERFLOW;
    } else {
      *pos += 6;
    }
    if (OB_FAIL(ret)) {
      // do nothing
    } else if (OB_FAIL(flt_store_int1(buf, len, pos, span_info->trace_enable_, FLT_TRACE_ENABLE))) {
      ret = OB_ERROR;
    } else if (OB_FAIL(flt_store_int1(buf, len, pos, span_info->force_print_, FLT_FORCE_PRINT))) {
      ret = OB_ERROR;
    } else if (OB_FAIL(flt_store_str(buf, len, pos, span_info->trace_id_, UUID4_SERIALIZE_LEN, FLT_TRACE_ID))) {
      ret = OB_ERROR;
    } else if (OB_FAIL(flt_store_int1(buf, len, pos, span_info->ref_type_, FLT_REF_TYPE))) {
      ret = OB_ERROR;
    } else if (OB_FAIL(flt_store_str(buf, len, pos, span_info->span_id_, UUID4_SERIALIZE_LEN, FLT_SPAN_ID))) {
      ret = OB_ERROR;
    } else {
      // success  
    }
    if (OB_SUCC(ret)) {
      // fill type and len in the head
      int32_t total_len = *pos - org_pos - 6;
      if (OB_FAIL(flt_store_type_and_len(buf, len, &org_pos, span_info->type_, total_len))) {
        ret = OB_ERROR;
      } else {
        // do nothing
      }
    }
  }
  return ret;
}

int flt_deserialize_field_spaninfo(FullLinkTraceExtraInfoId extra_id, const int64_t v_len,
                                        const char *buf, const int64_t len, int64_t *pos, void *flt_info)
{
  int ret = OB_SUCCESS;
  FLTSpanInfo *span_info = (FLTSpanInfo *)flt_info;
  switch(extra_id) {
    case FLT_TRACE_ENABLE: {
      if (OB_FAIL(flt_get_int1(buf, len, pos, v_len, &span_info->trace_enable_))) {
        ret = OB_ERROR;
      } else {
        // do nothing
      }
      break;
    }
    case FLT_FORCE_PRINT: {
      if (OB_FAIL(flt_get_int1(buf, len, pos, v_len, &span_info->force_print_))) {
        ret = OB_ERROR;
      } else {
        // do nothing
      }
      break;
    }
    case FLT_TRACE_ID: {
      if (OB_FAIL(flt_get_str(buf, len, pos, v_len, (char **)&span_info->trace_id_))) {
        ret = OB_ERROR;
      } else {
        // do nothing
      }
      break;
    }
    case FLT_REF_TYPE: {
      if (OB_FAIL(flt_get_int1(buf, len, pos, v_len, &span_info->ref_type_))) {
        ret = OB_ERROR;
      } else {
        // do nothing
      }
      break;
    }
    case FLT_SPAN_ID: {
      if (OB_FAIL(flt_get_str(buf, len, pos, v_len, (char **)&span_info->span_id_))) {
        ret = OB_ERROR;
      } else {
        // do nothing
      }
      break;
    }
    default: {
      *pos += *pos + v_len;
      break;
    }
  }
  return ret;
}
// for spaninfo

// for driverspaninfo info
int flt_get_serialize_size_driverspaninfo(int32_t *size, void *flt_info)
{
  int ret = OB_SUCCESS;
  int32_t local_size = 0;
  FLTDriverSpanInfo *client_info = (FLTDriverSpanInfo *)flt_info;

  if (NULL != client_info->client_span_) {
    local_size += TYPE_LENGTH + LEN_LENGTH;
    local_size += flt_get_store_str_size(strlen(client_info->client_span_) + 1);
  }

  *size = local_size;
  return ret;
}

int flt_serialize_driverspaninfo(char *buf, const int64_t len, int64_t *pos, void *flt_info)
{
  int ret = OB_SUCCESS;
  int64_t org_pos = *pos;
  FLTDriverSpanInfo *client_info = (FLTDriverSpanInfo *)flt_info;
  // resrver for type and len
  if (*pos + 6 > len) {
    ret = OB_SIZE_OVERFLOW;
  } else {
    *pos += 6;
  }
  if (OB_FAIL(ret)) {
    // do nothing
  } else if (OB_FAIL(flt_store_str(buf, len, pos, client_info->client_span_, strlen(client_info->client_span_) + 1, FLT_DRIVER_SPAN))) {
    ret = OB_ERROR;
  } else {
    // fill type and len in the head
    int32_t total_len = *pos - org_pos - 6;
    if (OB_FAIL(flt_store_type_and_len(buf, len, &org_pos, client_info->type_, total_len))) {
      ret = OB_ERROR;
    } else {
      // do nothing
    }
  }
  return ret;
}

int flt_deserialize_field_driverspaninfo(FullLinkTraceExtraInfoId extra_id, const int64_t v_len,
                                        const char *buf, const int64_t len, int64_t *pos, void *flt_info)
{
  int ret = OB_SUCCESS;
  FLTDriverSpanInfo *client_info = (FLTDriverSpanInfo *)flt_info;
  switch(extra_id) {
    case FLT_CLIENT_IDENTIFIER: {
      if (OB_FAIL(flt_get_str(buf, len, pos, v_len, (char **)&client_info->client_span_))) {
        ret = OB_ERROR;
      } else {
        // do nothing
      }
      break;
    }
    default: {
      *pos += *pos + v_len;
      break;
    }
  }
  return ret;
}
// for driverspaninfo info

// for no support
int flt_deserialize_field_nosupport(FullLinkTraceExtraInfoId extra_id, const int64_t v_len,
                                        const char *buf, const int64_t len, int64_t *pos, void *flt_info)
{
  UNUSED(extra_id);
  UNUSED(v_len);
  UNUSED(buf);
  UNUSED(len);
  UNUSED(pos);
  UNUSED(flt_info);
  return OB_ERROR;
}
int flt_serialize_nosupport(char *buf, const int64_t len, int64_t *pos, void *flt_info)
{
  UNUSED(buf);
  UNUSED(len);
  UNUSED(pos);
  UNUSED(flt_info);
  return OB_ERROR;
}
int flt_get_serialize_size_nosupport(int32_t *size, void *flt_info)
{
  UNUSED(size);
  UNUSED(flt_info);
  return OB_ERROR;
}

// for encode
static int flt_store_int(char *buf, int64_t len, int64_t *ppos, int64_t v, int16_t type, int64_t v_len)
{
  int ret = OB_SUCCESS;
  int64_t pos = *ppos;

  if (OB_ISNULL(buf)) {
    ret = OB_ERROR;
  } else if (len < pos + TYPE_LENGTH + LEN_LENGTH + v_len) {
    ret = OB_ERROR;
  } else {
    int2store(buf + pos, type);
    pos += 2;
    int4store(buf + pos, v_len);
    pos += 4;

    if (1 == v_len) {
      int1store(buf + pos, v);
    } else if (2 == v_len) {
      int2store(buf + pos, v);
    } else if (3 == v_len) {
      int3store(buf + pos, v);
    } else if (4 == v_len) {
      int4store(buf + pos, v);
    } else if (5 == v_len) {
      int5store(buf + pos, v);
    } else if (6 == v_len) {
      int6store(buf + pos, v);
    } else if (8 == v_len) {
      int8store(buf + pos, v);
    }
    pos += v_len;

    *ppos = pos;
  }

  return ret;
}


int flt_store_type_and_len(char *buf, int64_t len, int64_t *ppos, int16_t type, int32_t v_len)
{
  int ret = OB_SUCCESS;
  int64_t pos = *ppos;

  if (OB_ISNULL(buf)) {
    ret = OB_ERROR;
  } else if (len < pos + TYPE_LENGTH + LEN_LENGTH) {
    ret = OB_ERROR;
  } else {
    int2store(buf + pos, type);
    pos += 2;
    int4store(buf + pos, v_len);
    pos += 4;

    *ppos = pos;
  }

  return ret;
}

int flt_store_str(char *buf, int64_t len, int64_t *ppos, const char *str, const uint64_t str_len, int16_t type)
{
  int ret = OB_SUCCESS;
  int64_t pos = *ppos;

  if (OB_ISNULL(buf)) {
    ret = OB_ERROR;
  } else if (len < pos + TYPE_LENGTH + LEN_LENGTH + (int64_t)str_len) {
    ret = OB_ERROR;
  } else {
    int2store(buf + pos, type);
    pos += 2;
    int4store(buf + pos, str_len);
    pos += 4;

    memcpy(buf + pos, str, str_len);
    pos += str_len;

    *ppos = pos;
  }

  return ret;
}

int flt_store_int1(char *buf, int64_t len, int64_t *pos, int8_t v, int16_t type)
{
  return flt_store_int(buf, len, pos, v, type, 1);
}
int flt_store_int2(char *buf, int64_t len, int64_t *pos, int16_t v, int16_t type)
{
  return flt_store_int(buf, len, pos, v, type, 2);
}
int flt_store_int3(char *buf, int64_t len, int64_t *pos, int32_t v, int16_t type)
{
  return flt_store_int(buf, len, pos, v, type, 3);
}
int flt_store_int4(char *buf, int64_t len, int64_t *pos, int32_t v, int16_t type)
{
  return flt_store_int(buf, len, pos, v, type, 4);
}
int flt_store_int5(char *buf, int64_t len, int64_t *pos, int64_t v, int16_t type)
{
  return flt_store_int(buf, len, pos, v, type, 5);
}
int flt_store_int6(char *buf, int64_t len, int64_t *pos, int64_t v, int16_t type)
{
  return flt_store_int(buf, len, pos, v, type, 6);
}
int flt_store_int8(char *buf, int64_t len, int64_t *pos, int64_t v, int16_t type)
{
  return flt_store_int(buf, len, pos, v, type, 7);
}

int flt_store_double(char *buf, const int64_t len, int64_t *ppos, double val, int16_t type)
{
  int ret = OB_SUCCESS;
  int v_len = DBL_SIZE;
  int64_t pos = *ppos;

  if (OB_ISNULL(buf)) {
    ret = OB_ERROR;
  } else if ((uint64_t)len < pos + DBL_SIZE + TYPE_LENGTH + LEN_LENGTH) {
    ret = OB_ERROR;
  } else {
    int2store(buf + pos, type);
    pos += 2;
    int4store(buf + pos, v_len);
    pos += 4;

    memcpy(buf + pos, &val, DBL_SIZE);
    pos += DBL_SIZE;

    *ppos = pos;
  }
  return ret;
}

int flt_store_float(char *buf, const int64_t len, int64_t *ppos, float val, int16_t type)
{
  int ret = OB_SUCCESS;
  int v_len = FLT_SIZE;
  int64_t pos = *ppos;

  if (OB_ISNULL(buf)) {
    ret = OB_ERROR;
  } else if ((uint64_t)len < pos + FLT_SIZE + TYPE_LENGTH + LEN_LENGTH) {
    ret = OB_ERROR;
  } else {
    int2store(buf + pos, type);
    pos += 2;
    int4store(buf + pos, v_len);
    pos += 4;

    memcpy(buf + pos, &val, FLT_SIZE);
    pos += FLT_SIZE;

    *ppos = pos;
  }
  return ret;
}

inline int flt_get_store_str_size(const uint64_t str_len)
{
  return TYPE_LENGTH + LEN_LENGTH + str_len;
}
int flt_get_store_int1_size()
{
  return TYPE_LENGTH + LEN_LENGTH + 1;
}
int flt_get_store_int2_size()
{
  return TYPE_LENGTH + LEN_LENGTH + 2;
}
int flt_get_store_int3_size()
{
  return TYPE_LENGTH + LEN_LENGTH + 3;
}
int flt_get_store_int4_size()
{
  return TYPE_LENGTH + LEN_LENGTH + 4;
}
int flt_get_store_int5_size()
{
  return TYPE_LENGTH + LEN_LENGTH + 5;
}
int flt_get_store_int6_size()
{
  return TYPE_LENGTH + LEN_LENGTH + 6;
}
int flt_get_store_int8_size()
{
  return TYPE_LENGTH + LEN_LENGTH + 8;
}
int flt_get_store_double_size()
{
  return TYPE_LENGTH + LEN_LENGTH + DBL_SIZE;
}
int flt_get_store_float_size()
{
  return TYPE_LENGTH + LEN_LENGTH + FLT_SIZE;
}

static int flt_get_int(const char *buf, int64_t len, int64_t *ppos, int64_t v_len, void *val)
{
  int ret = OB_SUCCESS;
  int64_t pos = *ppos;

  if (OB_ISNULL(buf)) {
    ret = OB_ERROR;
  } else if (pos + v_len > len) {
    ret = OB_ERROR;
  } else {
    if (1 == v_len) {
      *(int8_t *)val = sint1korr(buf + pos);
    } else if (2 == v_len) {
      *(int16_t *)val = sint2korr(buf + pos);
    } else if (3 == v_len) {
      *(int32_t *)val = sint3korr(buf + pos);
    } else if (4 == v_len) {
      *(int32_t *)val = sint4korr(buf + pos);
    } else if (8 == v_len) {
      *(int64_t *)val = sint8korr(buf + pos);
    }

    pos += v_len;
    *ppos = pos;
  }

  return ret;
}

int flt_resolve_type_and_len(const char *buf, int64_t len, int64_t *ppos, int16_t *type, int32_t *v_len)
{
  int ret = OB_SUCCESS;
  int64_t pos = *ppos;

  if (OB_ISNULL(buf)) {
    ret = OB_ERROR;
  } else if (pos + TYPE_LENGTH + LEN_LENGTH > len) {
    ret = OB_ERROR;
  } else {
    *type = sint2korr(buf + pos);
    pos += 2;
    *v_len = sint4korr(buf + pos);
    pos += 4;

    *ppos = pos;
  }
  return ret;
}

int flt_get_str(const char *buf, int64_t len, int64_t *ppos, int64_t str_len, char **str)
{
  int ret = OB_SUCCESS;
  int64_t pos = *ppos;

  if (OB_ISNULL(buf)) {
    ret = OB_ERROR;
  } else if (pos + str_len > len) {
    ret = OB_ERROR;
  } else {
    *str = (char *)buf;

    pos += str_len;
    *ppos = pos;
  }
  return ret;
}

//@{ Signed integer in reverse sequence, write the result to v, and update pos
int flt_get_int1(const char *buf, int64_t len, int64_t *pos, int64_t v_len, int8_t *val)
{
  int ret = OB_SUCCESS;

  if (1 != v_len) {
    ret = OB_ERROR;
  } else {
    ret = flt_get_int(buf, len, pos, v_len, val);
  }

  return ret;
}
int flt_get_int2(const char *buf, int64_t len, int64_t *pos, int64_t v_len, int16_t *val)
{
  int ret = OB_SUCCESS;

  if (2 != v_len) {
    ret = OB_ERROR;
  } else {
    ret = flt_get_int(buf, len, pos, v_len, val);
  }

  return ret;
}
int flt_get_int3(const char *buf, int64_t len, int64_t *pos, int64_t v_len, int32_t *val)
{
  int ret = OB_SUCCESS;

  if (3 != v_len) {
    ret = OB_ERROR;
  } else {
    ret = flt_get_int(buf, len, pos, v_len, val);
  }

  return ret;
}
int flt_get_int4(const char *buf, int64_t len, int64_t *pos, int64_t v_len, int32_t *val)
{
  int ret = OB_SUCCESS;

  if (4 != v_len) {
    ret = OB_ERROR;
  } else {
    ret = flt_get_int(buf, len, pos, v_len, val);
  }

  return ret;
}
int flt_get_int8(const char *buf, int64_t len, int64_t *pos, int64_t v_len, int64_t *val)
{
  int ret = OB_SUCCESS;

  if (8 != v_len) {
    ret = OB_ERROR;
  } else {
    ret = flt_get_int(buf, len, pos, v_len, val);
  }

  return ret;
}

int flt_get_double(const char *buf, int64_t len, int64_t *ppos, int64_t v_len, double *val)
{
  int ret = OB_SUCCESS;
  int64_t pos = *ppos;

  if (OB_ISNULL(buf)) {
    ret = OB_ERROR;
  } else if (pos + v_len > len || sizeof(double) != v_len) {
    ret = OB_ERROR;
  } else {
    *val = (*((double *)(buf + pos)));

    pos += v_len;
    *ppos = pos;
  }
  return ret;
}

int flt_get_float(const char *buf, int64_t len, int64_t *ppos, int64_t v_len, float *val)
{
  int ret = OB_SUCCESS;
  int64_t pos = *ppos;

  if (OB_ISNULL(buf)) {
    ret = OB_ERROR;
  } else if (pos + v_len > len || sizeof(float) != v_len) {
    ret = OB_ERROR;
  } else {
    *val = (*((float *)(buf + pos)));

    pos += v_len;
    *ppos = pos;
  }
  return ret;
}

static uint64_t xorshift128plus(uint64_t *s) {
  uint64_t s1 = s[0];
  const uint64_t s0 = s[1];
  s[0] = s0;
  s1 ^= s1 << 23;
  s[1] = s1 ^ s0 ^ (s1 >> 18) ^ (s0 >> 5);
  return s[1] + s0;
}

int uuid4_init(uint64_t *seed, size_t seed_size) {
#if defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__)
  size_t res;
  FILE *fp = fopen("/dev/urandom", "rb");
  if (!fp) {
    return OB_ERROR;
  }
  res = fread(seed, 1, seed_size, fp);
  fclose(fp);
  if ( res != seed_size ) {
    return OB_ERROR;
  }

#elif defined(_WIN32)
  int res;
  HCRYPTPROV hCryptProv;
  res = CryptAcquireContext(
    &hCryptProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT);
  if (!res) {
    return OB_ERROR;
  }
  res = CryptGenRandom(hCryptProv, (DWORD) seed_size, (PBYTE) seed);
  CryptReleaseContext(hCryptProv, 0);
  if (!res) {
    return OB_ERROR;
  }

#else
  #error "unsupported platform"
#endif
  return OB_SUCCESS;
}


UUID uuid4_generate(uint64_t *seed) {
  uint64_t word;
  UUID ret;
  /* get random */
  word = xorshift128plus(seed);
  // word[1] = xorshift128plus(seed);

  ret.high_ = get_current_time_us();
  ret.low_ = word;
  return ret;
}

ObTrace *get_trace_instance(FLTInfo *flt)
{
  return flt->trace_;
}

int trace_init(FLTInfo *flt)
{
  int index;
  int ret = OB_SUCCESS;
  if (OB_ISNULL(flt)) {
    ret = OB_ERROR;
  } else {
    ObTrace *trace = (ObTrace *)malloc(OBTRACE_DEFAULT_BUFFER_SIZE);
    if (OB_ISNULL(trace)) {
      // malloc error
      ret = OB_ERROR;
    } else {
      memset(trace, 0, sizeof(*trace));
      trace->buffer_size_ = OBTRACE_DEFAULT_BUFFER_SIZE - sizeof(*trace);
      trace->log_buf_ = trace->data_;       // size is MAX_TRACE_LOG_SIZE
      trace->flt_serialize_buf_ = trace->data_ + MAX_TRACE_LOG_SIZE; // size is MAX_FLT_SERIALIZE_SIZE
      trace->offset_ = INIT_OFFSET;
      trace->auto_flush_ = 1;
      trace->trace_enable_ = FALSE;
      trace->force_print_ = FALSE;
      trace->slow_query_print_ = FALSE;
      trace->root_span_id_.low_ = 0;
      trace->root_span_id_.high_ = 0;
      trace->flt = flt;
      flt->trace_ = trace;
      for (index = 0; index < SPAN_CACHE_COUNT; ++index) {
        char *begin_addr = trace->data_  + MAX_TRACE_LOG_SIZE + MAX_FLT_SERIALIZE_SIZE + (index * (sizeof(LIST) + sizeof(ObSpanCtx)));
        LIST *list = (LIST *)begin_addr;
        list->data = (ObSpanCtx *)(begin_addr + sizeof(LIST));
        trace->free_span_list_ = list_add(trace->free_span_list_, list);
      }
      if (OB_FAIL(uuid4_init(trace->uuid_random_seed, sizeof(trace->uuid_random_seed)))) {
        // init uuid seed error
      }
    }
  }
  return ret;
}

void trace_end(FLTInfo *flt)
{
  if (OB_NOT_NULL(flt)) {
    free(flt->trace_);
    flt->trace_ = NULL;
  }
  return;
}

void begin_trace(ObTrace *trace)
{
  if (OB_NOT_NULL(trace)) {
    FLTInfo *flt = trace->flt;

    if (OB_ISNULL(flt)) {
      // error, do not entable trace
      trace->trace_enable_ = FALSE;
    } else if (FALSE == flt_is_vaild(flt)) {
      // error, invalid trace
      trace->trace_enable_ = FALSE;
    } else {
      double trace_pct = flt_get_pct(trace->uuid_random_seed);

      if (trace_pct <= flt->control_info_.sample_pct_) {
        // trace enable
        trace->trace_enable_ = TRUE;
      } else {
        trace->trace_enable_ = FALSE;
      }
    }

    if (TRUE == trace->trace_enable_) {
      trace->trace_id_ = uuid4_generate(trace->uuid_random_seed);
      trace->level_ = flt->control_info_.level_;
      // If the trace hits, the subsequent calculation and printing strategy
      if (RP_ALL == flt->control_info_.rp_) {
        trace->force_print_ = TRUE;
      } else if (RP_SAMPLE_AND_SLOW_QUERY == flt->control_info_.rp_) {
        // Hit print samples need to print
        double print_pct = flt_get_pct(trace->uuid_random_seed);
        if (print_pct <= flt->control_info_.print_sample_pct_) {
          trace->force_print_ = TRUE;
        } else {
          trace->force_print_ = FALSE;
        }
      } else if (RP_ONLY_SLOW_QUERY == flt->control_info_.rp_) {
        trace->force_print_ = FALSE;
      } else {
        trace->force_print_ = FALSE;
      }
    } else {
      // Set the trace level to 0, subsequent spans will not be processed
      trace->level_ = 0;
      trace->force_print_ = FALSE;
      trace->trace_id_.low_ = 0;
      trace->trace_id_.high_ = 0;
    }

#ifdef DEBUG_OB20
      printf("trace enable is %d, force print is %d\n", trace->trace_enable_, trace->force_print_);
#endif
  }
}

void end_trace(ObTrace *trace)
{
  if (OB_NOT_NULL(trace)) {
    LIST *span_elem;
    ObSpanCtx *span;
    while(OB_NOT_NULL(trace->current_span_list_)) {
      span_elem = trace->current_span_list_;
      span = (ObSpanCtx *)span_elem->data;
      if (0 == span->end_ts_) {
        span->end_ts_ = get_current_time_us();
      }
      trace->current_span_list_ = trace->current_span_list_->next;
      // add all elem to free span list
      trace->free_span_list_ = list_add(trace->free_span_list_, span_elem);
    }
    trace->offset_ = INIT_OFFSET;
    trace->policy_ = 0;
    trace->last_active_span_ = NULL;
  }
}

// used by slow query
void flush_first_span(ObTrace *trace)
{
  LIST *span_list;
  ObSpanCtx *span;
  int64_t pos = 0;
  int ret = OB_SUCCESS;

  if (OB_ISNULL(trace) || (0 == trace->trace_id_.low_ && 0 == trace->trace_id_.high_)) {
    // do nothing
  } else if (OB_ISNULL(span_list = trace->current_span_list_)) {
    // do nothing
  } else if (OB_ISNULL(span = (ObSpanCtx *)span_list->data)) {
    // do nothing
  } else {
    ObTagCtx *tag = span->tags_;
    my_bool first = TRUE;
    char buf[MAX_TRACE_LOG_SIZE];
    pos = 0;
    INIT_SPAN(trace, span);
    while (OB_SUCC(ret) && OB_NOT_NULL(tag)) {
      if (pos + 10 >= MAX_TRACE_LOG_SIZE) {
        ret = OB_ERROR;
      } else {
        buf[pos++] = ',';
        if (first) {
          strcpy(buf + pos, "\"tags\":[");
          pos += 8;
          first = FALSE;
        }
        ret = tostring_ObTagCtx(buf, MAX_TRACE_LOG_SIZE, &pos, tag);
        tag = tag->next_;
      }
    }
    if (0 != pos) {
      if (pos + 1 < MAX_TRACE_LOG_SIZE) {
        buf[pos++] = ']';
        buf[pos++] = 0;
      } else {
        buf[MAX_TRACE_LOG_SIZE - 2] = ']';
        buf[MAX_TRACE_LOG_SIZE - 1] = 0;
      }
    }
    if (OB_SUCC(ret)) {
      INIT_SPAN(trace, span->source_span_);
      ret = snprintf(trace->log_buf_ + trace->log_buf_offset_,
                MAX_TRACE_LOG_SIZE - trace->log_buf_offset_,
                TRACE_PATTERN "%s}",
                UUID_TOSTRING(trace->trace_id_),
                "obclient",
                UUID_TOSTRING(span->span_id_),
                span->start_ts_,
                span->end_ts_,
                UUID_TOSTRING(OB_ISNULL(span->source_span_) ? trace->root_span_id_ : span->source_span_->span_id_),
                span->is_follow_ ? "true" : "false",
                buf);
      if (ret < 0 || trace->log_buf_offset_ + ret >= MAX_TRACE_LOG_SIZE) {
        ret = OB_ERROR;
      } else {
        trace->log_buf_offset_ += ret;
        ret = OB_SUCCESS;
      }
    }
    if (OB_SUCC(ret) && 0 != span->end_ts_) {
      trace->current_span_list_ = list_delete(trace->current_span_list_, span_list);
      trace->free_span_list_ = list_add(trace->free_span_list_, span_list);
    }
    span->tags_ = 0;
  }
}

void flush_trace(ObTrace *trace)
{
  LIST *next;
  LIST *span_list;
  ObSpanCtx *span;
  int64_t pos = 0;
  int ret = OB_SUCCESS;

  if (OB_ISNULL(trace) || (0 == trace->trace_id_.low_ && 0 == trace->trace_id_.high_)) {
    // do nothing
  } else if (OB_ISNULL(span_list = trace->current_span_list_)) {
    // do nothing
  } else {
    while (OB_NOT_NULL(span_list)) {
      span = (ObSpanCtx *)span_list->data;
      next = span_list->next;
      if (OB_NOT_NULL(span)) {
        ObTagCtx *tag = span->tags_;
        my_bool first = TRUE;
        char buf[MAX_TRACE_LOG_SIZE] = {0};
        pos = 0;
        INIT_SPAN(trace, span);
        while (OB_SUCC(ret) && OB_NOT_NULL(tag)) {
          if (pos + 10 >= MAX_TRACE_LOG_SIZE) {
            ret = OB_ERROR;
          } else {
            buf[pos++] = ',';
            if (first) {
              strcpy(buf + pos, "\"tags\":[");
              pos += 8;
              first = FALSE;
            }
            ret = tostring_ObTagCtx(buf, MAX_TRACE_LOG_SIZE, &pos, tag);
            tag = tag->next_;
          }
        }
        if (0 != pos) {
          if (pos + 1 < MAX_TRACE_LOG_SIZE) {
            buf[pos++] = ']';
            buf[pos++] = 0;
          } else {
            buf[MAX_TRACE_LOG_SIZE - 2] = ']';
            buf[MAX_TRACE_LOG_SIZE - 1] = 0;
          }
        }
        if (OB_SUCC(ret)) {
          INIT_SPAN(trace, span->source_span_);
          ret = snprintf(trace->log_buf_ + trace->log_buf_offset_,
                    MAX_TRACE_LOG_SIZE - trace->log_buf_offset_,
                    TRACE_PATTERN "%s}",
                    UUID_TOSTRING(trace->trace_id_),
                    "obclient",
                    UUID_TOSTRING(span->span_id_),
                    span->start_ts_,
                    span->end_ts_,
                    UUID_TOSTRING(OB_ISNULL(span->source_span_) ? trace->root_span_id_ : span->source_span_->span_id_),
                    span->is_follow_ ? "true" : "false",
                    buf);
          if (ret < 0 || trace->log_buf_offset_ + ret >= MAX_TRACE_LOG_SIZE) {
            ret = OB_ERROR;
          } else {
            trace->log_buf_offset_ += ret;
            ret = OB_SUCCESS;
          }
        }
        if (OB_SUCC(ret) && 0 != span->end_ts_) {
          trace->current_span_list_ = list_delete(trace->current_span_list_, span_list);
          trace->free_span_list_ = list_add(trace->free_span_list_, span_list);
        }
        span->tags_ = 0;
      }
      span_list = next;
    }
    trace->offset_ = INIT_OFFSET;
  }
}

ObSpanCtx* begin_span(ObTrace *trace, uint32_t span_type, uint8_t level, my_bool is_follow)
{
  ObSpanCtx *new_span = NULL;
#ifdef DEBUG_OB20
    printf("//////////////////////call begin_span ////////////////////////////////\n");
#endif
  if (OB_ISNULL(trace)) {
    // do nothing
  } else if (trace->trace_id_.low_ == 0 && trace->trace_id_.high_ == 0) {
    // trace is not enable
  } else if (level > trace->level_) {
    // do nothing
  } else {
    if (OB_ISNULL(trace->free_span_list_)) {
      FLUSH_TRACE(trace->flt);
    }
    if (OB_NOT_NULL(trace->free_span_list_)) {
      LIST *span_elem = trace->free_span_list_;
      trace->free_span_list_ = trace->free_span_list_->next;
      if (OB_NOT_NULL(span_elem)) {
        new_span = (ObSpanCtx *)span_elem->data;
        new_span->span_type_ = span_type;
        new_span->span_id_.high_ = 0;
        new_span->span_id_.low_ = ++trace->seq_;
        new_span->source_span_ = trace->last_active_span_;
        new_span->is_follow_ = is_follow;
        new_span->start_ts_ = get_current_time_us();
        new_span->end_ts_ = 0;
        new_span->tags_ = NULL;
        trace->last_active_span_ = new_span;
        trace->current_span_list_ = list_add(trace->current_span_list_, span_elem);
      }
    }
  }
  return new_span;
}

void end_span(ObTrace *trace, ObSpanCtx *span)
{
#ifdef DEBUG_OB20
    printf("//////////////////////call end_span ////////////////////////////////\n");
#endif
  if (OB_ISNULL(trace) || OB_ISNULL(trace->flt) || OB_ISNULL(span)) {
    // error
  } else if ((trace->trace_id_.low_ == 0 && trace->trace_id_.high_ == 0) 
      || (span->span_id_.low_ == 0 && span->span_id_.high_ == 0)) {
    // trace id or span id is not inited
  } else {
    FLTInfo      *flt = trace->flt;
    int64_t       slow_query_threshold = flt->control_info_.slow_query_threshold_;
    int64_t       use_time;
    RecordPolicy  rp = flt->control_info_.rp_;

    span->end_ts_ = get_current_time_us();
    use_time = span->end_ts_ - span->start_ts_;
    trace->last_active_span_ = span->source_span_;
    // The current driver has only one span, so directly calculate the end time of this span and then calculate the slow query
    // todo: Check the type of the span, only interactive spans need to record slow queries
    if (RP_ONLY_SLOW_QUERY == rp || RP_SAMPLE_AND_SLOW_QUERY == rp) {
      if (slow_query_threshold != -1 && slow_query_threshold <= use_time) {
        // If you find slow queries, you only need to enable log printing, and send the span log to the server for printing.
#ifdef DEBUG_OB20
          printf("get slow query, use_time is %ld, slow_query_threshold is %ld\n", use_time, slow_query_threshold);
#endif
        trace->slow_query_print_ = TRUE;
      }
    }
#ifdef DEBUG_OB20
      printf("end span spanid:[%lu:%lu]\n", span->span_id_.high_, span->span_id_.low_);
#endif
  }
}

void reset_span(ObTrace *trace)
{
  if (OB_ISNULL(trace) || OB_ISNULL(trace->flt)) {
    // error
  } else if (trace->trace_id_.low_ == 0 && trace->trace_id_.high_ == 0) {
    // trace is not inited
  } else {
    ObSpanCtx *span;
    LIST *next;
    LIST *span_elem = trace->current_span_list_;
    while(OB_NOT_NULL(span_elem)) {
      span = (ObSpanCtx *)span_elem->data;
      next = span_elem->next;
      if (0 != span->end_ts_) {
        trace->current_span_list_ = list_delete(trace->current_span_list_, span_elem);
        trace->free_span_list_ = list_add(trace->free_span_list_, span_elem);
      }
      span->tags_ = NULL;
      span_elem = next;
    }
  }
}

void append_tag(ObTrace *trace, ObSpanCtx *span, uint16_t tag_type, const char *str)
{
  if (OB_ISNULL(trace)) {
    // do nothing
  } else if (OB_ISNULL(str)) {
    // do nothing
  } else if (trace->offset_ + sizeof(ObTagCtx) > trace->buffer_size_) {
    // do nothing
  } else {
    ObTagCtx *tag = (ObTagCtx *)(trace->data_ + trace->offset_);
    tag->next_ = span->tags_;
    span->tags_ = tag;
    tag->tag_type_ = tag_type;
    tag->data_ = str;
    trace->offset_ += sizeof(ObTagCtx);
  }
}

int serialize_UUID(char *buf, const int64_t buf_len, int64_t *pos, UUID *uuid)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(encode_i64(buf, buf_len, pos, uuid->high_))) {
    // LOG_WARN("deserialize failed", K(ret), K(buf), K(buf_len), K(pos));
  } else if (OB_FAIL(encode_i64(buf, buf_len, pos, uuid->low_))) {
    // LOG_WARN("deserialize failed", K(ret), K(buf), K(buf_len), K(pos));
  }
  return ret;
}

int deserialize_UUID(const char *buf, const int64_t buf_len, int64_t *pos, UUID *uuid)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(decode_i64(buf, buf_len, pos, (int64_t *)(&uuid->high_)))) {
    // LOG_WARN("deserialize failed", K(ret), K(buf), K(buf_len), K(pos));
  } else if (OB_FAIL(decode_i64(buf, buf_len, pos, (int64_t *)(&uuid->low_)))) {
    // LOG_WARN("deserialize failed", K(ret), K(buf), K(buf_len), K(pos));
  }
  return ret;
}

DEFINE_TO_STRING_FUNC_FOR(UUID)
{
  int ret = OB_SUCCESS;
  int64_t tmp_pos = *pos; 
  const char *transfer = "0123456789abcdef";
  if (tmp_pos + 37 > buf_len) {
    ret = OB_ERROR;
  } else {
    buf[tmp_pos++] = transfer[src->high_ >> 60 & 0xf];
    buf[tmp_pos++] = transfer[src->high_ >> 56 & 0xf];
    buf[tmp_pos++] = transfer[src->high_ >> 52 & 0xf];
    buf[tmp_pos++] = transfer[src->high_ >> 48 & 0xf];
    buf[tmp_pos++] = transfer[src->high_ >> 44 & 0xf];
    buf[tmp_pos++] = transfer[src->high_ >> 40 & 0xf];
    buf[tmp_pos++] = transfer[src->high_ >> 36 & 0xf];
    buf[tmp_pos++] = transfer[src->high_ >> 32 & 0xf];
    buf[tmp_pos++] = '-';
    buf[tmp_pos++] = transfer[src->high_ >> 28 & 0xf];
    buf[tmp_pos++] = transfer[src->high_ >> 24 & 0xf];
    buf[tmp_pos++] = transfer[src->high_ >> 20 & 0xf];
    buf[tmp_pos++] = transfer[src->high_ >> 16 & 0xf];
    buf[tmp_pos++] = '-';
    buf[tmp_pos++] = transfer[src->high_ >> 12 & 0xf];
    buf[tmp_pos++] = transfer[src->high_ >>  8 & 0xf];
    buf[tmp_pos++] = transfer[src->high_ >>  4 & 0xf];
    buf[tmp_pos++] = transfer[src->high_ >>  0 & 0xf];
    buf[tmp_pos++] = '-';
    buf[tmp_pos++] = transfer[src->low_ >> 60 & 0xf];
    buf[tmp_pos++] = transfer[src->low_ >> 56 & 0xf];
    buf[tmp_pos++] = transfer[src->low_ >> 52 & 0xf];
    buf[tmp_pos++] = transfer[src->low_ >> 48 & 0xf];
    buf[tmp_pos++] = '-';
    buf[tmp_pos++] = transfer[src->low_ >> 44 & 0xf];
    buf[tmp_pos++] = transfer[src->low_ >> 40 & 0xf];
    buf[tmp_pos++] = transfer[src->low_ >> 36 & 0xf];
    buf[tmp_pos++] = transfer[src->low_ >> 32 & 0xf];
    buf[tmp_pos++] = transfer[src->low_ >> 28 & 0xf];
    buf[tmp_pos++] = transfer[src->low_ >> 24 & 0xf];
    buf[tmp_pos++] = transfer[src->low_ >> 20 & 0xf];
    buf[tmp_pos++] = transfer[src->low_ >> 16 & 0xf];
    buf[tmp_pos++] = transfer[src->low_ >> 12 & 0xf];
    buf[tmp_pos++] = transfer[src->low_ >>  8 & 0xf];
    buf[tmp_pos++] = transfer[src->low_ >>  4 & 0xf];
    buf[tmp_pos++] = transfer[src->low_ >>  0 & 0xf];
    buf[tmp_pos  ] = '\0';
  }

  *pos = tmp_pos;
  return ret;
}

DEFINE_TO_STRING_FUNC_FOR(ObTagCtx)
{
  int ret = OB_SUCCESS;
  char from[] = "\"\n\r\\";
  const char *to[] = { "\\\"", "\\n", "\\r", "\\\\"};
  int l = strlen(tag_str[src->tag_type_]);
  int64_t tmp_pos = *pos;
  const char *c = src->data_;
  const char *toc;
  my_bool conv = FALSE;
  int i;
  if (tmp_pos + l + 7 >= buf_len) {
    buf[buf_len - 1] = '\0';
    ret = OB_ERROR;
  } else {
    buf[tmp_pos++] = '{';
    buf[tmp_pos++] = '\"';
    strcpy(buf + tmp_pos, tag_str[src->tag_type_]);
    tmp_pos += l;
    buf[tmp_pos++] = '\"';
    buf[tmp_pos++] = ':';
    buf[tmp_pos] = '\0';
    for (; *c; ++c) {
      if (c != src->data_ && *(c + 1)) {
        // ignore the first one and the last one
        for (i = 0; i < 4; ++i) {
          if (from[i] == *c) {
            for (toc = to[i]; *toc; ++toc) {
              if (tmp_pos < buf_len) {
                buf[tmp_pos++] = *toc;
              } else {
                ret = OB_ERROR;
              }
            }
            conv = TRUE;
            break;
          }
        }
      }
      if (!conv && tmp_pos < buf_len) {
        buf[tmp_pos++] = *c;
      }
    }
  }
  if (OB_FAIL(ret) || tmp_pos + 1 >= buf_len) {
    buf[buf_len - 1] = 0;
  } else {
    buf[tmp_pos++] = '}';
    buf[tmp_pos] = 0;
  }
  if (OB_SUCC(ret)) {
    *pos = tmp_pos;
  }
  return ret;
}

// DEFINE_TO_STRING_FUNC_FOR(ObSpanCtx)
// {
//   int ret = 0;
//   char span_id[UUID4_LEN];
//   char source_span_id[UUID4_LEN] = {0};
//   int64_t span_id_pos = 0;
//   int64_t source_span_id_pos = 0;
//   if (OB_FAIL(tostring_UUID(span_id, UUID4_LEN, &span_id_pos, &src->span_id_))) {
//     // error
//   } else if (OB_NOT_NULL(src->source_span_id_) && (tostring_UUID(source_span_id, UUID4_LEN, &source_span_id_pos, src->source_span_id_))) {
//     // error
//   } else {
//     ret = snprintf(buf + *pos, buf_len - *pos,
//                   "\"name\":\"%s\",\"id\":\"%s\",\"start_ts\":%ld,\"end_ts\":%ld,\"parent_id\":\"%s\",\"is_follow\":%s",
//                   "ObClient",
//                   span_id,
//                   src->start_ts_,
//                   src->end_ts_,
//                   source_span_id,
//                   src->is_follow_ ? "true" : "false");
//     if (ret < 0 || *pos + ret >= buf_len) {
//       ret = OB_ERROR;
//     } else {
//       ObTagCtx* tag = src->tags_;
//       *pos = *pos + ret;
//       ret = OB_SUCCESS;
//       while (OB_SUCC(ret) && OB_NOT_NULL(tag)) {
//         ret = snprintf(buf + *pos, buf_len - *pos, ",");
//         if (ret < 0 || *pos + ret >= buf_len) {
//           ret = OB_ERROR;
//         } else {
//           *pos = *pos + ret;
//           ret = tostring_ObTagCtx(buf, buf_len, pos, tag);
//           tag = tag->next_;
//         }
//       }
//     }
//   }
//   return ret;
// }

int flt_set_module(MYSQL *mysql, const char *module_name)
{
  int ret = 0;
  if (OB_ISNULL(mysql) || FALSE == get_use_full_link_trace(mysql)) {
    // error
    ret = 1;
  } else if (OB_ISNULL(mysql->net.ob20protocol)) {
    ret = 1;
  } else {
    FLTInfo *flt = mysql->net.ob20protocol->flt;
    if (OB_ISNULL(flt)) {
      ret = 1;
    } else {
      flt->app_info_.module_ = module_name;
    }
  }
  return ret;
}

int flt_set_action(MYSQL *mysql, const char *action_name)
{
  int ret = 0;
  if (OB_ISNULL(mysql) || FALSE == get_use_full_link_trace(mysql)) {
    // error
    ret = 1;
  } else if (OB_ISNULL(mysql->net.ob20protocol)) {
    ret = 1;
  } else {
    FLTInfo *flt = mysql->net.ob20protocol->flt;
    if (OB_ISNULL(flt)) {
      ret = 1;
    } else {
      flt->app_info_.action_ = action_name;
    }
  }
  return ret;
}

int flt_set_client_info(MYSQL *mysql, const char *client_info)
{
  int ret = 0;
  if (OB_ISNULL(mysql) || FALSE == get_use_full_link_trace(mysql)) {
    // error
    ret = 1;
  } else if (OB_ISNULL(mysql->net.ob20protocol)) {
    ret = 1;
  } else {
    FLTInfo *flt = mysql->net.ob20protocol->flt;
    if (OB_ISNULL(flt)) {
      ret = 1;
    } else {
      flt->app_info_.client_info_ = client_info;
    }
  }
  return ret;
}

int flt_set_identifier(MYSQL *mysql, const char *identifier)
{
  int ret = 0;
  if (OB_ISNULL(mysql) || FALSE == get_use_full_link_trace(mysql)) {
    // error
    ret = 1;
  } else if (OB_ISNULL(mysql->net.ob20protocol)) {
    ret = 1;
  } else {
    FLTInfo *flt = mysql->net.ob20protocol->flt;
    if (OB_ISNULL(flt)) {
      ret = 1;
    } else {
      flt->app_info_.identifier_ = identifier;
    }
  }
  return ret;
}

int flt_get_control_level(MYSQL *mysql, int *level)
{
  int ret = 0;
  if (OB_ISNULL(mysql) || FALSE == get_use_full_link_trace(mysql)) {
    // error
    ret = 1;
  } else if (OB_ISNULL(mysql->net.ob20protocol)) {
    ret = 1;
  } else {
    FLTInfo *flt = mysql->net.ob20protocol->flt;
    if (OB_ISNULL(flt)) {
      ret = 1;
    } else {
      *level = flt->control_info_.level_;
    }
  }
  return ret;
}

int flt_get_control_sample_pct(MYSQL *mysql, double *sample_pct)
{
  int ret = 0;
  if (OB_ISNULL(mysql) || FALSE == get_use_full_link_trace(mysql)) {
    // error
    ret = 1;
  } else if (OB_ISNULL(mysql->net.ob20protocol)) {
    ret = 1;
  } else {
    FLTInfo *flt = mysql->net.ob20protocol->flt;
    if (OB_ISNULL(flt)) {
      ret = 1;
    } else {
      *sample_pct = flt->control_info_.sample_pct_;
    }
  }
  return ret;
}

int flt_get_control_record_policy(MYSQL *mysql, int *rp)
{
  int ret = 0;
  if (OB_ISNULL(mysql) || FALSE == get_use_full_link_trace(mysql)) {
    // error
    ret = 1;
  } else if (OB_ISNULL(mysql->net.ob20protocol)) {
    ret = 1;
  } else {
    FLTInfo *flt = mysql->net.ob20protocol->flt;
    if (OB_ISNULL(flt)) {
      ret = 1;
    } else {
      *rp = flt->control_info_.rp_;
    }
  }
  return ret;
}

int flt_get_control_print_spct(MYSQL *mysql, double *sample_pct)
{
  int ret = 0;
  if (OB_ISNULL(mysql) || FALSE == get_use_full_link_trace(mysql)) {
    // error
    ret = 1;
  } else if (OB_ISNULL(mysql->net.ob20protocol)) {
    ret = 1;
  } else {
    FLTInfo *flt = mysql->net.ob20protocol->flt;
    if (OB_ISNULL(flt)) {
      ret = 1;
    } else {
      *sample_pct = flt->control_info_.print_sample_pct_;
    }
  }
  return ret;
}

int flt_get_control_slow_threshold(MYSQL *mysql, long int *slow_threshold)
{
  int ret = 0;
  if (OB_ISNULL(mysql) || FALSE == get_use_full_link_trace(mysql)) {
    // error
    ret = 1;
  } else if (OB_ISNULL(mysql->net.ob20protocol)) {
    ret = 1;
  } else {
    FLTInfo *flt = mysql->net.ob20protocol->flt;
    if (OB_ISNULL(flt)) {
      ret = 1;
    } else {
      *slow_threshold = flt->control_info_.slow_query_threshold_;
    }
  }
  return ret;
}
