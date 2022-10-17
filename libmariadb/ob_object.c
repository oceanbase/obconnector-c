#include <ob_object.h>
#include <stddef.h>
#include "ob_serialize.h"

static int get_map(ObObjType ob_type, const ObMySQLTypeMap **map)
{
  int ret = 0;
  if (ob_type >= ObMaxType) {
    ret = 1;
  }

  if (OB_SUCC(ret)) {
    *map = type_maps_ + ob_type;
  }

  return ret;
}

int get_mysql_type(ObObjType ob_type, enum_field_types *mysql_type)
{
  const ObMySQLTypeMap *map = NULL;
  int ret = 0;

  if ((ret = get_map(ob_type, &map)) == 0) {
    *mysql_type = map->mysql_type;
  }
  return ret;
}

int get_ob_type(ObObjType *ob_type, enum_field_types mysql_type)
{
  int ret = 0;
  switch (mysql_type) {
    case MYSQL_TYPE_NULL:
      *ob_type = ObNullType;
      break;
    case MYSQL_TYPE_TINY:
      *ob_type = ObTinyIntType;
      break;
    case MYSQL_TYPE_SHORT:
      *ob_type = ObSmallIntType;
      break;
    case MYSQL_TYPE_INT24:
      *ob_type = ObMediumIntType;
      break;
    case MYSQL_TYPE_LONG:
      *ob_type = ObInt32Type;
      break;
    case MYSQL_TYPE_LONGLONG:
      *ob_type = ObIntType;
      break;
    case MYSQL_TYPE_FLOAT:
      *ob_type = ObFloatType;
      break;
    case MYSQL_TYPE_DOUBLE:
      *ob_type = ObDoubleType;
      break;
    case MYSQL_TYPE_TIMESTAMP:
      *ob_type = ObTimestampType;
      break;
    case MYSQL_TYPE_DATETIME:
      *ob_type = ObDateTimeType;
      break;
    case MYSQL_TYPE_TIME:
      *ob_type = ObTimeType;
      break;
    case MYSQL_TYPE_DATE:
      *ob_type = ObDateType;
      break;
    case MYSQL_TYPE_YEAR:
      *ob_type = ObYearType;
      break;
    case MYSQL_TYPE_VARCHAR:
    case MYSQL_TYPE_STRING:
    case MYSQL_TYPE_VAR_STRING:
      *ob_type = ObVarcharType;
      break;
    case MYSQL_TYPE_TINY_BLOB:
      *ob_type = ObTinyTextType;
      break;
    case MYSQL_TYPE_BLOB:
      *ob_type = ObTextType;
      break;
    case MYSQL_TYPE_MEDIUM_BLOB:
      *ob_type = ObMediumTextType;
      break;
    case MYSQL_TYPE_LONG_BLOB:
      *ob_type = ObLongTextType;
      break;
    case MYSQL_TYPE_OB_TIMESTAMP_WITH_TIME_ZONE:
      *ob_type = ObTimestampTZType;
      break;
    case MYSQL_TYPE_OB_TIMESTAMP_WITH_LOCAL_TIME_ZONE:
      *ob_type = ObTimestampLTZType;
      break;
    case MYSQL_TYPE_OB_TIMESTAMP_NANO:
      *ob_type = ObTimestampNanoType;
      break;
    case MYSQL_TYPE_OB_RAW:
      *ob_type = ObRawType;
      break;
    case MYSQL_TYPE_DECIMAL:
    case MYSQL_TYPE_NEWDECIMAL:
      *ob_type = ObNumberType;
      break;
    case MYSQL_TYPE_BIT:
      *ob_type = ObBitType;
      break;
    case MYSQL_TYPE_ENUM:
      *ob_type = ObEnumType;
      break;
    case MYSQL_TYPE_SET:
      *ob_type = ObSetType;
      break;
    case MYSQL_TYPE_OBJECT:
      *ob_type = ObExtendType;
      break;
    default:
      ret = 1;
  }
  return ret;
}

/* obj get/set function */
OB_INLINE int8_t get_tinyint(const ObObj *obj) { return (int8_t)(obj->v_.int64_); }
OB_INLINE int16_t get_smallint(const ObObj *obj) { return (int16_t)(obj->v_.int64_); }
OB_INLINE int32_t get_mediumint(const ObObj *obj) { return (int32_t)(obj->v_.int64_); }
OB_INLINE int32_t get_int32(const ObObj *obj) { return (int32_t)(obj->v_.int64_); }
OB_INLINE int64_t get_int(const ObObj *obj) { return (int64_t)(obj->v_.int64_); }

OB_INLINE uint8_t get_utinyint(const ObObj *obj) { return (uint8_t)(obj->v_.uint64_); }
OB_INLINE uint16_t get_usmallint(const ObObj *obj) { return (uint16_t)(obj->v_.uint64_); }
OB_INLINE uint32_t get_umediumint(const ObObj *obj) { return (uint32_t)(obj->v_.uint64_); }
OB_INLINE uint32_t get_uint32(const ObObj *obj) { return (uint32_t)(obj->v_.uint64_); }
OB_INLINE uint64_t get_uint64(const ObObj *obj) { return (uint64_t)(obj->v_.uint64_); }

OB_INLINE float get_float(const ObObj *obj) { return obj->v_.float_; }
OB_INLINE double get_double(const ObObj *obj) { return obj->v_.double_; }
OB_INLINE float get_ufloat(const ObObj *obj) { return obj->v_.float_; }
OB_INLINE double get_udouble(const ObObj *obj) { return obj->v_.double_; }

OB_INLINE ObObjType get_type(ObObjMeta *meta) { return (ObObjType)meta->type_; }

OB_INLINE void set_null_meta(ObObjMeta *meta) { meta->type_ = (uint8_t)(ObNullType); meta->cs_level_ = CS_LEVEL_IGNORABLE; meta->cs_type_ = CS_TYPE_BINARY; }
OB_INLINE void set_tinyint_meta(ObObjMeta *meta) { meta->type_ = (uint8_t)(ObTinyIntType); meta->cs_level_ = CS_LEVEL_NUMERIC;meta->cs_type_ = CS_TYPE_BINARY; }
OB_INLINE void set_smallint_meta(ObObjMeta *meta) { meta->type_ = (uint8_t)(ObSmallIntType); meta->cs_level_ = CS_LEVEL_NUMERIC;meta->cs_type_ = CS_TYPE_BINARY; }
OB_INLINE void set_mediumint_meta(ObObjMeta *meta) { meta->type_ = (uint8_t)(ObMediumIntType); meta->cs_level_ = CS_LEVEL_NUMERIC;meta->cs_type_ = CS_TYPE_BINARY; }
OB_INLINE void set_int32_meta(ObObjMeta *meta) { meta->type_ = (uint8_t)(ObInt32Type); meta->cs_level_ = CS_LEVEL_NUMERIC;meta->cs_type_ = CS_TYPE_BINARY; }
OB_INLINE void set_int_meta(ObObjMeta *meta) { meta->type_ = (uint8_t)(ObIntType); meta->cs_level_ = CS_LEVEL_NUMERIC;meta->cs_type_ = CS_TYPE_BINARY; }
OB_INLINE void set_utinyint_meta(ObObjMeta *meta) { meta->type_ = (uint8_t)(ObUTinyIntType); meta->cs_level_ = CS_LEVEL_NUMERIC;meta->cs_type_ = CS_TYPE_BINARY; }
OB_INLINE void set_usmallint_meta(ObObjMeta *meta) { meta->type_ = (uint8_t)(ObUSmallIntType); meta->cs_level_ = CS_LEVEL_NUMERIC;meta->cs_type_ = CS_TYPE_BINARY; }
OB_INLINE void set_umediumint_meta(ObObjMeta *meta) { meta->type_ = (uint8_t)(ObUMediumIntType); meta->cs_level_ = CS_LEVEL_NUMERIC;meta->cs_type_ = CS_TYPE_BINARY; }
OB_INLINE void set_uint32_meta(ObObjMeta *meta) { meta->type_ = (uint8_t)(ObUInt32Type); meta->cs_level_ = CS_LEVEL_NUMERIC;meta->cs_type_ = CS_TYPE_BINARY; }
OB_INLINE void set_uint64_meta(ObObjMeta *meta) { meta->type_ = (uint8_t)(ObUInt64Type); meta->cs_level_ = CS_LEVEL_NUMERIC;meta->cs_type_ = CS_TYPE_BINARY; }
OB_INLINE void set_float_meta(ObObjMeta *meta) { meta->type_ = (uint8_t)(ObFloatType); meta->cs_level_ = CS_LEVEL_NUMERIC;meta->cs_type_ = CS_TYPE_BINARY; }
OB_INLINE void set_double_meta(ObObjMeta *meta) { meta->type_ = (uint8_t)(ObDoubleType); meta->cs_level_ = CS_LEVEL_NUMERIC;meta->cs_type_ = CS_TYPE_BINARY; }
OB_INLINE void set_ufloat_meta(ObObjMeta *meta) { meta->type_ = (uint8_t)(ObUFloatType); meta->cs_level_ = CS_LEVEL_NUMERIC;meta->cs_type_ = CS_TYPE_BINARY; }
OB_INLINE void set_udouble_meta(ObObjMeta *meta) { meta->type_ = (uint8_t)(ObUDoubleType); meta->cs_level_ = CS_LEVEL_NUMERIC;meta->cs_type_ = CS_TYPE_BINARY; }
OB_INLINE void set_varchar_meta(ObObjMeta *meta) { meta->type_ = (uint8_t)(ObVarcharType); }

inline void set_tinyint(ObObj *obj, const int8_t value)
{
  set_tinyint_meta(&obj->meta_);
  obj->v_.int64_ = (int64_t)(value);
}

inline void set_tinyint_value(ObObj *obj, const int8_t value)
{
  obj->v_.int64_ = (int64_t)(value);
}

inline void set_smallint(ObObj *obj, const int16_t value)
{
  set_smallint_meta(&obj->meta_);
  obj->v_.int64_ = (int64_t)(value);
}

inline void set_smallint_value(ObObj *obj, const int16_t value)
{
  obj->v_.int64_ = (int64_t)(value);
}

inline void set_mediumint(ObObj *obj, const int32_t value)
{
  set_mediumint_meta(&obj->meta_);
  obj->v_.int64_ = (int64_t)(value);
}

inline void set_int32(ObObj *obj, const int32_t value)
{
  set_int32_meta(&obj->meta_);
  obj->v_.int64_ = (int64_t)(value);
}

inline void set_int32_value(ObObj *obj, const int32_t value)
{
//  meta_.set_int32();
  obj->v_.int64_ = (int64_t)(value);
}

inline void set_int(ObObj *obj, const int64_t value)
{
  set_int_meta(&obj->meta_);
  obj->v_.int64_ = value;
}

inline void set_int_value(ObObj *obj, const int64_t value)
{
  obj->v_.int64_ = value;
}

inline void set_utinyint(ObObj *obj, const uint8_t value)
{
  set_utinyint_meta(&obj->meta_);
  obj->v_.uint64_ = (uint64_t)(value);
}

inline void set_usmallint(ObObj *obj, const uint16_t value)
{
  set_usmallint_meta(&obj->meta_);
  obj->v_.uint64_ = (uint64_t)(value);
}

inline void set_umediumint(ObObj *obj, const uint32_t value)
{
  set_umediumint_meta(&obj->meta_);
  obj->v_.uint64_ = (uint64_t)(value);
}

inline void set_uint32(ObObj *obj, const uint32_t value)
{
  set_uint32_meta(&obj->meta_);
  obj->v_.uint64_ = (uint64_t)(value);
}

inline void set_uint64(ObObj *obj, const uint64_t value)
{
  set_uint64_meta(&obj->meta_);
  obj->v_.uint64_ = value;
}

inline void set_float(ObObj *obj, const float value)
{
  set_float_meta(&obj->meta_);
  obj->v_.float_ = value;
}

inline void set_float_value(ObObj *obj, const float value)
{
//  meta_.set_float();
  obj->v_.float_ = value;
}

inline void set_ufloat(ObObj *obj, const float value)
{
  set_ufloat_meta(&obj->meta_);
  obj->v_.float_ = value;
}

inline void set_double(ObObj *obj, const double value)
{
  set_double_meta(&obj->meta_);
  obj->v_.double_ = value;
}

inline void set_double_value(ObObj *obj, const double value)
{
  obj->v_.double_ = value;
}

inline void set_udouble(ObObj *obj, const double value)
{
  set_udouble_meta(&obj->meta_);
  obj->v_.double_ = value;
}

inline void set_varchar(ObObj *obj, const char *ptr, int32_t size)
{
  set_varchar_meta(&obj->meta_);
  obj->meta_.cs_level_ = CS_LEVEL_IMPLICIT;
  obj->v_.string_ = ptr;
  obj->val_len_ = size;
}
/* obj get/set function */

// general serialization functions generator
#define DEF_SERIALIZE_FUNCS(OBJTYPE, TYPE, VTYPE)                       \
  int obj_val_serialize_##OBJTYPE(const ObObj *obj, char *buf, const int64_t buf_len, int64_t *pos) \
  {                                                                     \
   int ret = OB_SUCCESS;                                                \
   OB_UNIS_ENCODE(get_##TYPE(obj), VTYPE);                              \
   return ret;                                                          \
  }                                                                     \
                                                                        \
  int obj_val_deserialize_##OBJTYPE(ObObj *obj, const char *buf, const int64_t data_len, int64_t *pos) \
  {                                                                     \
    int ret = OB_SUCCESS;                                               \
    VTYPE v;                                                            \
    OB_UNIS_DECODE(v, VTYPE);                                           \
    if (OB_SUCC(ret)) {                                                 \
      set_##TYPE(obj, v);                                               \
    }                                                                   \
    return ret;                                                         \
  }                                                                     \
                                                                        \
  int64_t obj_val_get_serialize_size_##OBJTYPE(const ObObj *obj)         \
  {                                                                     \
   int64_t len = 0;                                                     \
   OB_UNIS_ADD_LEN(get_##TYPE(obj), VTYPE);                             \
   return len;                                                          \
  }

// general generator for numeric types
#define DEF_NUMERIC_FUNCS(OBJTYPE, TYPE, VTYPE, SQL_FORMAT, STR_FORMAT, HTYPE) \
  DEF_SERIALIZE_FUNCS(OBJTYPE, TYPE, VTYPE)

// ObTinyIntType=1,                // int8, aka mysql boolean type
DEF_NUMERIC_FUNCS(ObTinyIntType, tinyint, int8_t, "%hhd", "'%hhd'", int64_t);
// ObSmallIntType=2,               // int16
DEF_NUMERIC_FUNCS(ObSmallIntType, smallint, int16_t, "%hd", "'%hd'", int64_t);
// ObMediumIntType=3,              // int24
DEF_NUMERIC_FUNCS(ObMediumIntType, mediumint, int32_t, "%d", "'%d'", int64_t);
// ObInt32Type=4,                 // int32
DEF_NUMERIC_FUNCS(ObInt32Type, int32, int32_t, "%d", "'%d'", int64_t);
// ObIntType=5,                    // int64, aka bigint
DEF_NUMERIC_FUNCS(ObIntType, int, int64_t, "%ld", "'%ld'", int64_t);
// ObUTinyIntType=6,                // uint8
DEF_NUMERIC_FUNCS(ObUTinyIntType, utinyint, uint8_t, "%hhu", "'%hhu'", uint64_t);
// ObUSmallIntType=7,               // uint16
DEF_NUMERIC_FUNCS(ObUSmallIntType, usmallint, uint16_t, "%hu", "'%hu'", uint64_t);
// ObUMediumIntType=8,              // uint24
DEF_NUMERIC_FUNCS(ObUMediumIntType, umediumint, uint32_t, "%u", "'%u'", uint64_t);
// ObUInt32Type=9,                    // uint32
DEF_NUMERIC_FUNCS(ObUInt32Type, uint32, uint32_t, "%u", "'%u'", uint64_t);
// ObUInt64Type=10,                 // uint64
DEF_NUMERIC_FUNCS(ObUInt64Type, uint64, uint64_t, "%lu", "'%lu'", uint64_t);
// ObFloatType=11,                  // single-precision floating point
DEF_NUMERIC_FUNCS(ObFloatType, float, float, "%2f", "'%2f'", double);
// ObDoubleType=12,                 // double-precision floating point
DEF_NUMERIC_FUNCS(ObDoubleType, double, double, "%2lf", "'%2lf'", double);
// ObUFloatType=13,            // unsigned single-precision floating point
DEF_NUMERIC_FUNCS(ObUFloatType, ufloat, float, "%2f", "'%2f'", double);
// ObUDoubleType=14,           // unsigned double-precision floating point
DEF_NUMERIC_FUNCS(ObUDoubleType, udouble, double, "%2lf", "'%2lf'", double);

// for obstring
int obj_val_serialize_ObVarcharType(const ObObj *obj, char *buf, const int64_t buf_len, int64_t *pos)
{                                                                     
 int ret = OB_SUCCESS;
 encode_vstr_with_len(buf, buf_len, pos, obj->v_.string_, obj->val_len_);
 return ret;
}                                                                     
                                                                      
int obj_val_deserialize_ObVarcharType(ObObj *obj, const char *buf, const int64_t data_len, int64_t *pos)
{                                                                     
  int ret = OB_SUCCESS;
  int64_t len = 0;
  const int64_t MINIMAL_NEEDED_SIZE = 2; //at least need two bytes
  if (OB_ISNULL(buf) || OB_UNLIKELY((data_len - *pos) < MINIMAL_NEEDED_SIZE)) {
    ret = OB_ERROR;
  } else {
    obj->v_.string_ = (char *)(decode_vstr_nocopy(buf, data_len, pos, &len));
    if (OB_ISNULL(obj->v_.string_)) {
      ret = OB_ERROR;
    } else {
      obj->val_len_ = len;
    }
  }
  return ret;
}                                                                     
                                                                      
int64_t obj_val_get_serialize_size_ObVarcharType(const ObObj *obj)
{                                                                     
  return encoded_length_vstr_with_len(obj->val_len_);
}

// for null type
int obj_val_serialize_ObNullType(const ObObj *obj, char *buf, const int64_t buf_len, int64_t *pos)
{                                                                     
 int ret = OB_ERROR;
 UNUSED(obj);
 UNUSED(buf);
 UNUSED(buf_len);
 UNUSED(pos);
 return ret;
}                                                                     
                                                                      
int obj_val_deserialize_ObNullType(ObObj *obj, const char *buf, const int64_t data_len, int64_t *pos)
{                                                                     
  int ret = OB_ERROR;
  UNUSED(obj);
  UNUSED(buf);
  UNUSED(data_len);
  UNUSED(pos);
  return ret;
}                                                                     
                                                                      
int64_t obj_val_get_serialize_size_ObNullType(const ObObj *obj)
{                                                                     
  UNUSED(obj);
  return 0;
}

int serialize_ObObjMeta(const ObObjMeta *objmeta, char *buf, const int64_t buf_len, int64_t *pos)
{
  int ret = OB_SUCCESS;
  encode_uint8_t(buf, buf_len, pos, objmeta->type_);
  encode_uint8_t(buf, buf_len, pos, objmeta->cs_level_);
  encode_uint8_t(buf, buf_len, pos, objmeta->cs_type_);
  encode_int8_t(buf, buf_len, pos, objmeta->scale_);
  return ret;
}
int deserialize_ObObjMeta(ObObjMeta *objmeta,const char *buf, const int64_t data_len, int64_t *pos)
{
  int ret = OB_SUCCESS;
  decode_uint8_t(buf, data_len, pos, &objmeta->type_);
  decode_uint8_t(buf, data_len, pos, &objmeta->cs_level_);
  decode_uint8_t(buf, data_len, pos, &objmeta->cs_type_);
  decode_int8_t(buf, data_len, pos, &objmeta->scale_);
  return ret;
}

int64_t get_serialize_size_ObObjMeta(const ObObjMeta *objmeta)
{
  int64_t len = 0;
  len += encoded_length_uint8_t(objmeta->type_);
  len += encoded_length_uint8_t(objmeta->cs_level_);
  len += encoded_length_uint8_t(objmeta->cs_type_);
  len += encoded_length_int8_t(objmeta->scale_);
  return len;
}

#define DEF_FUNC_ENTRY(OBJTYPE)                  \
  {                                              \
    obj_val_serialize_##OBJTYPE,                 \
    obj_val_deserialize_##OBJTYPE,               \
    obj_val_get_serialize_size_##OBJTYPE,        \
  }

ObObjTypeFuncs OBJ_FUNCS[ObMaxType] =
{
  DEF_FUNC_ENTRY(ObNullType),  // 0
  DEF_FUNC_ENTRY(ObTinyIntType),  // 1
  DEF_FUNC_ENTRY(ObSmallIntType), // 2
  DEF_FUNC_ENTRY(ObMediumIntType),  // 3
  DEF_FUNC_ENTRY(ObInt32Type),      // 4
  DEF_FUNC_ENTRY(ObIntType),        // 5
  DEF_FUNC_ENTRY(ObUTinyIntType),   // 6
  DEF_FUNC_ENTRY(ObUSmallIntType),  // 7
  DEF_FUNC_ENTRY(ObUMediumIntType), // 8
  DEF_FUNC_ENTRY(ObUInt32Type),     // 9
  DEF_FUNC_ENTRY(ObUInt64Type),     // 10
  DEF_FUNC_ENTRY(ObFloatType),      // 11
  DEF_FUNC_ENTRY(ObDoubleType),     // 12
  DEF_FUNC_ENTRY(ObUFloatType),     // 13
  DEF_FUNC_ENTRY(ObUDoubleType),    // 14
  DEF_FUNC_ENTRY(ObNullType),     // 15
  DEF_FUNC_ENTRY(ObNullType),  // 16: unumber is the same as number
  DEF_FUNC_ENTRY(ObNullType),  // 17
  DEF_FUNC_ENTRY(ObNullType), // 18
  DEF_FUNC_ENTRY(ObNullType),  // 19
  DEF_FUNC_ENTRY(ObNullType),  // 20
  DEF_FUNC_ENTRY(ObNullType),  // 21
  DEF_FUNC_ENTRY(ObVarcharType),  // 22, varchar
  DEF_FUNC_ENTRY(ObNullType),     // 23, char
  DEF_FUNC_ENTRY(ObNullType),  // 24, hex_string
  DEF_FUNC_ENTRY(ObNullType),  // 25, ext
  DEF_FUNC_ENTRY(ObNullType),  // 26, unknown
  DEF_FUNC_ENTRY(ObNullType),          // 27
  DEF_FUNC_ENTRY(ObNullType),          // 28
  DEF_FUNC_ENTRY(ObNullType),          // 29
  DEF_FUNC_ENTRY(ObNullType),          // 30
  DEF_FUNC_ENTRY(ObNullType),          // 31
  DEF_FUNC_ENTRY(ObNullType),          // 32
  DEF_FUNC_ENTRY(ObNullType),          // 33
  DEF_FUNC_ENTRY(ObNullType),          // 34
  DEF_FUNC_ENTRY(ObNullType),          // 35
  DEF_FUNC_ENTRY(ObNullType),   // 36, timestamp with time zone
  DEF_FUNC_ENTRY(ObNullType),  // 37, timestamp with local time zone
  DEF_FUNC_ENTRY(ObNullType), // 38, timestamp (9)
  DEF_FUNC_ENTRY(ObNullType),           // 39, raw
  DEF_FUNC_ENTRY(ObNullType),          // 40
  DEF_FUNC_ENTRY(ObNullType),          // 41
  DEF_FUNC_ENTRY(ObNullType),          // 42
  DEF_FUNC_ENTRY(ObNullType),     // 43, nvarchar2
  DEF_FUNC_ENTRY(ObNullType),         // 44, nchar
};

int serialize_ObObj(const ObObj *obj, char *buf, const int64_t buf_len, int64_t *pos)
{
  int ret = OB_SUCCESS;
  serialize_ObObjMeta(&obj->meta_, buf, buf_len, pos);
  if (OB_SUCC(ret)) {
    if (obj->meta_.type_ <= ObMaxType) {
      ret = OBJ_FUNCS[get_type((ObObjMeta *)&obj->meta_)].serialize(obj, buf, buf_len, pos);
    } else {
      ret = OB_ERROR;
    }
  }
  return ret;
}

int deserialize_ObObj(ObObj *obj, const char *buf, const int64_t data_len, int64_t *pos)
{
  int ret = OB_SUCCESS;
  deserialize_ObObjMeta(&obj->meta_, buf, data_len, pos);
  if (OB_SUCC(ret)) {
    if (obj->meta_.type_ <= ObMaxType) {
      ret = OBJ_FUNCS[get_type((ObObjMeta *)&obj->meta_)].deserialize(obj, buf, data_len, pos);
    } else {
      ret = OB_ERROR;
    }
  }
  return ret;
}

int64_t get_serialize_size_ObObj(const ObObj *obj)
{
  int64_t len = 0;
  len += get_serialize_size_ObObjMeta(&obj->meta_);
  len += OBJ_FUNCS[get_type((ObObjMeta *)&obj->meta_)].get_serialize_size(obj);
  return len;
}
/* obj serialize/deserialize function */
