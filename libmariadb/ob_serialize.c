#include "ob_serialize.h"

inline int64_t encoded_length_vi64(int64_t val)
{
  uint64_t __v = (uint64_t)(val);
  int64_t need_bytes = 0;
  if (__v <= OB_MAX_V1B) {
    need_bytes = 1;
  } else if (__v <= OB_MAX_V2B) {
    need_bytes = 2;
  } else if (__v <= OB_MAX_V3B) {
    need_bytes = 3;
  } else if (__v <= OB_MAX_V4B) {
    need_bytes = 4;
  } else if (__v <= OB_MAX_V5B) {
    need_bytes = 5;
  } else if (__v <= OB_MAX_V6B) {
    need_bytes = 6;
  } else if (__v <= OB_MAX_V7B) {
    need_bytes = 7;
  } else if (__v <= OB_MAX_V8B) {
    need_bytes = 8;
  } else if (__v <= OB_MAX_V9B) {
    need_bytes = 9;
  } else {
    need_bytes = 10;
  }
  return need_bytes;
}

/**
 * @brief Encode a integer (up to 64bit) in variable length encoding
 *
 * @param buf pointer to the destination buffer
 * @param end the end pointer to the destination buffer
 * @param val value to encode
 *
 * @return true - success, false - failed
 */
inline int encode_vi64(char *buf, const int64_t buf_len, int64_t *ppos, int64_t val)
{
  int64_t pos = *ppos;
  uint64_t __v = (uint64_t)(val);
  int ret = ((NULL != buf) &&
             ((buf_len - pos) >= encoded_length_vi64(__v))) ? OB_SUCCESS : OB_SIZE_OVERFLOW;
  if (OB_SUCC(ret)) {
    while (__v > OB_MAX_V1B) {
      *(buf + pos++) = (int8_t)((__v) | 0x80);
      __v >>= 7;
    }
    if (__v <= OB_MAX_V1B) {
      *(buf + pos++) = (int8_t)((__v) & 0x7f);
    }
    *ppos = pos;
  }
  return ret;
}

inline int decode_vi64(const char *buf, const int64_t data_len, int64_t *ppos, int64_t *val)
{
  uint64_t __v = 0;
  uint32_t shift = 0;
  int64_t pos = *ppos;
  int64_t tmp_pos = pos;
  int ret = OB_SUCCESS;
  while (OB_SUCC(ret) && ((*(buf + tmp_pos)) & 0x80)) {
    if (data_len - tmp_pos < 1) {
      ret = OB_DESERIALIZE_ERROR;
      break;
    }
    __v |= ((uint64_t)(*(buf + tmp_pos++)) & 0x7f) << shift;
    shift += 7;
  }
  if (OB_SUCC(ret)) {
    if (data_len - tmp_pos < 1) {
      ret = OB_DESERIALIZE_ERROR;
    } else {
      __v |= (((uint64_t)(*(buf + tmp_pos++)) & 0x7f) << shift);
      *val = (int64_t)(__v);
      pos = tmp_pos;
    }
    *ppos = pos;
  }
  return ret;
}

inline int64_t encoded_length_vi32(int32_t val)
{
  uint32_t __v = (uint64_t)(val);
  int64_t need_bytes = 0;
  if (__v <= OB_MAX_V1B) {
    need_bytes = 1;
  } else if (__v <= OB_MAX_V2B) {
    need_bytes = 2;
  } else if (__v <= OB_MAX_V3B) {
    need_bytes = 3;
  } else if (__v <= OB_MAX_V4B) {
    need_bytes = 4;
  } else {
    need_bytes = 5;
  }
  return need_bytes;

}

/**
 * @brief Encode a integer (up to 32bit) in variable length encoding
 *
 * @param buf pointer to the destination buffer
 * @param end the end pointer to the destination buffer
 * @param val value to encode
 *
 * @return true - success, false - failed
 */

inline int encode_vi32(char *buf, const int64_t buf_len, int64_t *ppos, int32_t val)
{
  int64_t pos = *ppos;
  uint32_t __v = (uint32_t)(val);
  int ret = ((NULL != buf) &&
             ((buf_len - pos) >= encoded_length_vi32(val))) ? OB_SUCCESS : OB_SIZE_OVERFLOW;
  if (OB_SUCC(ret)) {
    while (__v > OB_MAX_V1B) {
      *(buf + pos++) = (int8_t)((__v) | 0x80);
      __v >>= 7;
    }
    if (__v <= OB_MAX_V1B) {
      *(buf + pos++) = (int8_t)((__v) & 0x7f);
    }
    *ppos = pos;
  }
  return ret;

}

inline int decode_vi32(const char *buf, const int64_t data_len, int64_t *ppos, int32_t *val)
{
  uint32_t __v = 0;
  uint32_t shift = 0;
  int ret = OB_SUCCESS;
  int64_t pos = *ppos;
  int64_t tmp_pos = pos;
  while (OB_SUCC(ret) && ((*(buf + tmp_pos)) & 0x80)) {
    if (data_len - tmp_pos < 1) {
      ret = OB_DESERIALIZE_ERROR;
      break;
    }
    __v |= ((uint32_t)(*(buf + tmp_pos++)) & 0x7f) << shift;
    shift += 7;
  }
  if (OB_SUCC(ret)) {
    if (data_len - tmp_pos < 1) {
      ret = OB_DESERIALIZE_ERROR;
    } else {
      __v |= ((uint32_t)(*(buf + tmp_pos++)) & 0x7f) << shift;
      *val = (int32_t)(__v);
      pos = tmp_pos;
    }
    *ppos = pos;
  }
  return ret;
}

inline int64_t encoded_length_i8(int8_t val)
{
  return (int64_t)(sizeof(val));
}

inline int encode_i8(char *buf, const int64_t buf_len, int64_t *ppos, int8_t val)
{
  int64_t pos = *ppos;
  int ret = ((NULL != buf) &&
             ((buf_len - pos) >= (int64_t)(sizeof(val)))) ? OB_SUCCESS : OB_SIZE_OVERFLOW;
  if (OB_SUCC(ret)) {
    *(buf + pos++) = val;
    *ppos = pos;
  }
  return ret;
}

inline int decode_i8(const char *buf, const int64_t data_len, int64_t *ppos, int8_t *val)
{
  int pos = *ppos;
  int ret = (NULL != buf && data_len - pos >= 1) ? OB_SUCCESS : OB_DESERIALIZE_ERROR;
  if (OB_SUCC(ret)) {
    *val = *(buf + pos++);
    *ppos = pos;
  }
  return ret;
}

inline int encode_i64(char *buf, const int64_t buf_len, int64_t *ppos, int64_t val)
{
  int pos = *ppos;
  int ret = ((NULL != buf) &&
             ((buf_len - pos) >= (int64_t)(sizeof(val)))) ? OB_SUCCESS : OB_ERROR;
  if (OB_SUCC(ret)) {
    *(buf + pos++) = (char)(((val) >> 56) & 0xff);
    *(buf + pos++) = (char)(((val) >> 48) & 0xff);
    *(buf + pos++) = (char)(((val) >> 40) & 0xff);
    *(buf + pos++) = (char)(((val) >> 32) & 0xff);
    *(buf + pos++) = (char)(((val) >> 24) & 0xff);
    *(buf + pos++) = (char)(((val) >> 16) & 0xff);
    *(buf + pos++) = (char)(((val) >> 8) & 0xff);
    *(buf + pos++) = (char)((val) & 0xff);

    *ppos = pos;
  }
  return ret;
}

inline int decode_i64(const char *buf, const int64_t data_len, int64_t *ppos, int64_t *val)
{
  int pos = *ppos;
  int ret = (NULL != buf && data_len - pos  >= 8) ? OB_SUCCESS : OB_ERROR;
  if (OB_SUCC(ret)) {
    *val =  (((int64_t)((*(buf + pos++))) & 0xff)) << 56;
    *val |= (((int64_t)((*(buf + pos++))) & 0xff)) << 48;
    *val |= (((int64_t)((*(buf + pos++))) & 0xff)) << 40;
    *val |= (((int64_t)((*(buf + pos++))) & 0xff)) << 32;
    *val |= (((int64_t)((*(buf + pos++))) & 0xff)) << 24;
    *val |= (((int64_t)((*(buf + pos++))) & 0xff)) << 16;
    *val |= (((int64_t)((*(buf + pos++))) & 0xff)) << 8;
    *val |= (((int64_t)((*(buf + pos++))) & 0xff));
  }
  return ret;
}

inline int encode_int64_t(char *buf, const int64_t buf_len, int64_t *pos, int64_t val)
{
  return encode_vi64(buf, buf_len, pos, val);
}
inline int encode_uint64_t(char *buf, const int64_t buf_len, int64_t *pos, uint64_t val)
{
  return encode_vi64(buf, buf_len, pos, (int64_t)(val));
}
inline int encode_int32_t(char *buf, const int64_t buf_len, int64_t *pos, int32_t val)
{
  return encode_vi32(buf, buf_len, pos, val);
}
inline int encode_uint32_t(char *buf, const int64_t buf_len, int64_t *pos, uint32_t val)
{
  return encode_vi32(buf, buf_len, pos, (int32_t)(val));
}
inline int encode_int16_t(char *buf, const int64_t buf_len, int64_t *pos, int16_t val)
{
  return encode_vi32(buf, buf_len, pos, (int32_t)(val));
}
inline int encode_uint16_t(char *buf, const int64_t buf_len, int64_t *pos, uint16_t val)
{
  return encode_vi32(buf, buf_len, pos, (int32_t)(val));
}
inline int encode_int8_t(char *buf, const int64_t buf_len, int64_t *pos, int8_t val)
{
  return encode_i8(buf, buf_len, pos, val);
}
inline int encode_uint8_t(char *buf, const int64_t buf_len, int64_t *pos, uint8_t val)
{
  return encode_i8(buf, buf_len, pos, (int8_t)(val));
}


inline int decode_int8_t(const char *buf, const int64_t data_len, int64_t *pos, int8_t *val)
{
  *val = 0;
  return decode_i8(buf, data_len, pos, val);
}
inline int decode_uint8_t(const char *buf, const int64_t data_len, int64_t *pos, uint8_t *val)
{
  *val = 0;
  return decode_i8(buf, data_len, pos, (int8_t*)val);
}
inline int decode_int16_t(const char *buf, const int64_t data_len, int64_t *pos, int16_t *val)
{
  int32_t v = 0;
  int ret = decode_vi32(buf, data_len, pos, &v);
  *val = (int16_t)(v);
  return ret;
}
inline int decode_uint16_t(const char *buf, const int64_t data_len, int64_t *pos, uint16_t *val)
{
  int32_t v = 0;
  int ret = decode_vi32(buf, data_len, pos, &v);
  *val = (uint16_t)(v);
  return ret;
}
inline int decode_int32_t(const char *buf, const int64_t data_len, int64_t *pos, int32_t *val)
{
  *val = 0;
  return decode_vi32(buf, data_len, pos, val);
}
inline int decode_uint32_t(const char *buf, const int64_t data_len, int64_t *pos, uint32_t *val)
{
  *val = 0;
  return decode_vi32(buf, data_len, pos, (int32_t *)val);
}
inline int decode_int64_t(const char *buf, const int64_t data_len, int64_t *pos, int64_t *val)
{
  *val = 0;
  return decode_vi64(buf, data_len, pos, val);
}
inline int decode_uint64_t(const char *buf, const int64_t data_len, int64_t *pos, uint64_t *val)
{
  *val = 0;
  return decode_vi64(buf, data_len, pos, (int64_t *)val);
}

inline int64_t encoded_length_int64_t(int64_t val)
{
  return encoded_length_vi64(val);
}
inline int64_t encoded_length_uint64_t(uint64_t val)
{
  return encoded_length_vi64((int64_t)val);
}
inline int64_t encoded_length_int32_t(int32_t val)
{
  return encoded_length_vi32(val);
}
inline int64_t encoded_length_uint32_t(uint32_t val)
{
  return encoded_length_vi32((int32_t)val);
}
inline int64_t encoded_length_int16_t(int16_t val)
{
  return encoded_length_vi32(val);
}
inline int64_t encoded_length_uint16_t(uint16_t val)
{
  return encoded_length_vi32((int32_t)val);
}
inline int64_t encoded_length_int8_t(int8_t unused)
{
  UNUSED(unused);
  return 1;
}
inline int64_t encoded_length_uint8_t(uint8_t unused)
{
  UNUSED(unused);
  return 1;
}

inline int64_t encoded_length_float(float val)
{
  int32_t tmp = 0;
  memcpy(&tmp, &val, sizeof(tmp));
  return encoded_length_vi32(tmp);
}

inline int encode_float(char *buf, const int64_t buf_len, int64_t *pos, float val)
{
  int32_t tmp = 0;
  memcpy(&tmp, &val, sizeof(tmp));
  return encode_vi32(buf, buf_len, pos, tmp);
}

inline int decode_float(const char *buf, const int64_t data_len, int64_t *pos, float *val)
{
  int32_t tmp = 0;
  int ret = OB_SUCCESS;
  if ((ret = decode_vi32(buf, data_len, pos, &tmp)) == 0) {
    memcpy(val, &tmp, sizeof(*val));
  }
  return ret;
}

inline int64_t encoded_length_double(double val)
{
  int64_t tmp = 0;
  memcpy(&tmp, &val, sizeof(tmp));
  return encoded_length_vi64(tmp);
}

inline int encode_double(char *buf, const int64_t buf_len, int64_t *pos, double val)
{
  int64_t tmp = 0;
  memcpy(&tmp, &val, sizeof(tmp));
  return encode_vi64(buf, buf_len, pos, tmp);
}

inline int decode_double(const char *buf, const int64_t data_len, int64_t *pos, double *val)
{
  int64_t tmp = 0;
  int ret = OB_SUCCESS;
  if ((ret = decode_vi64(buf, data_len, pos, &tmp)) == 0) {
    memcpy(val, &tmp, sizeof(*val));
  }
  return ret;
}


/**
 * @brief Computes the encoded length of vstr(int64,data,null)
 *
 * @param len string length
 *
 * @return the encoded length of str
 */
inline int64_t encoded_length_vstr_with_len(int64_t len)
{
  return encoded_length_vi64(len) + len + 1;
}

inline int64_t encoded_length_vstr(const char *str)
{
  return encoded_length_vstr_with_len(str ? (int64_t)(strlen(str) + 1) : 0);
}

/**
 * @brief get the decoded length of data len of vstr
 * won't change the pos
 * @return the length of data
 */
inline int64_t decoded_length_vstr(const char *buf, const int64_t data_len, int64_t pos)
{
  int64_t len = -1;
  int64_t tmp_pos = pos;
  if (NULL == buf || data_len < 0 || pos < 0) {
    len = -1;
  } else if (decode_vi64(buf, data_len, &tmp_pos, &len) != 0) {
    len = -1;
  }
  return len;
}

/**
 * @brief Encode a buf as vstr(int64,data,null)
 *
 * @param buf pointer to the destination buffer
 * @param vbuf pointer to the start of the input buffer
 * @param len length of the input buffer
 */
inline int encode_vstr_with_len(char *buf, const int64_t buf_len, int64_t *ppos, const void *vbuf,
                       int64_t len)
{
  int64_t pos = *ppos;
  int ret = ((NULL != buf) && (len >= 0)
             && ((buf_len - pos) >= (int32_t)(encoded_length_vstr_with_len(len))))
            ?  OB_SUCCESS : OB_SIZE_OVERFLOW;
  if (OB_SUCC(ret)) {
    /**
     * even through it's a null string, we can serialize it with
     * lenght 0, and following a '\0'
     */
    ret = encode_vi64(buf, buf_len, &pos, len);
    if (OB_SUCCESS == ret && len > 0 && NULL != vbuf) {
      memcpy(buf + pos, vbuf, len);
      pos += len;
    }
    *(buf + pos++) = 0;
    *ppos = pos;
  }
  return ret;
}

inline int encode_vstr(char *buf, const int64_t buf_len, int64_t *ppos, const char *s)
{
  return encode_vstr_with_len(buf, buf_len, ppos, s, s ? strlen(s) + 1 : 0);
}

inline const char *decode_vstr_nocopy(const char *buf, const int64_t data_len, int64_t *ppos, int64_t *lenp)
{
  int64_t pos = *ppos;
  const char *str = 0;
  int64_t tmp_len = 0;
  int64_t tmp_pos = pos;

  if ((NULL == buf) || (data_len < 0) || (pos < 0) || (NULL == lenp)) {
    //just _return_;
  } else if (decode_vi64(buf, data_len, &tmp_pos, &tmp_len) != OB_SUCCESS) {
    *lenp = -1;
  } else if (tmp_len >= 0) {
    if (data_len - tmp_pos >= tmp_len) {
      str = buf + tmp_pos;
      *lenp = tmp_len++;
      tmp_pos += tmp_len;
      pos = tmp_pos;
    } else {
      *lenp = -1;
    }
  }
  *ppos = pos;
  return str;
}

inline const char *decode_vstr(const char *buf, const int64_t data_len, int64_t *ppos,
                               char *dest, int64_t buf_len, int64_t *lenp)
{
  int64_t pos = *ppos;
  const char *str = 0;
  int64_t tmp_len = 0;
  int64_t tmp_pos = pos;
  if ((NULL == buf) || (data_len < 0) || (pos < 0) || (0 == dest) || buf_len < 0 || (NULL == lenp)) {
    //just _return_;
  } else if (decode_vi64(buf, data_len, &tmp_pos, &tmp_len) != 0 || tmp_len > buf_len) {
    *lenp = -1;
  } else if (tmp_len >= 0) {
    if (data_len - tmp_pos >= tmp_len) {
      str = buf + tmp_pos;
      *lenp = tmp_len++;
      memcpy(dest, str, *lenp);
      tmp_pos += tmp_len;
      pos = tmp_pos;
    } else {
      *lenp = -1;
    }
  }
  *ppos = pos;
  return str;
}
