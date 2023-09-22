/*
   Copyright (c) 2000, 2018, Oracle and/or its affiliates.
   Copyright (c) 2009, 2019, MariaDB Corporation.
   Copyright (c) 2021 OceanBase.
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335 USA */

#ifndef _ob_serialize_h
#define _ob_serialize_h

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "ma_global.h"


int encode_i64(char *buf, const int64_t buf_len, int64_t *ppos, int64_t val);
int decode_i64(const char *buf, const int64_t data_len, int64_t *ppos, int64_t *val);

int64_t encoded_length_vi64(int64_t val);

/**
 * @brief Encode a integer (up to 64bit) in variable length encoding
 *
 * @param buf pointer to the destination buffer
 * @param end the end pointer to the destination buffer
 * @param val value to encode
 *
 * @return true - success, false - failed
 */
int encode_vi64(char *buf, const int64_t buf_len, int64_t *ppos, int64_t val);

int decode_vi64(const char *buf, const int64_t data_len, int64_t *ppos, int64_t *val);

int64_t encoded_length_vi32(int32_t val);

/**
 * @brief Encode a integer (up to 32bit) in variable length encoding
 *
 * @param buf pointer to the destination buffer
 * @param end the end pointer to the destination buffer
 * @param val value to encode
 *
 * @return true - success, false - failed
 */

int encode_vi32(char *buf, const int64_t buf_len, int64_t *ppos, int32_t val);

int decode_vi32(const char *buf, const int64_t data_len, int64_t *ppos, int32_t *val);

int encode_i8(char *buf, const int64_t buf_len, int64_t *ppos, int8_t val);

int decode_i8(const char *buf, const int64_t data_len, int64_t *ppos, int8_t *val);

int encode_int64_t(char *buf, const int64_t buf_len, int64_t *pos, int64_t val);
int encode_uint64_t(char *buf, const int64_t buf_len, int64_t *pos, uint64_t val);
int encode_int32_t(char *buf, const int64_t buf_len, int64_t *pos, int32_t val);
int encode_uint32_t(char *buf, const int64_t buf_len, int64_t *pos, uint32_t val);
int encode_int16_t(char *buf, const int64_t buf_len, int64_t *pos, int16_t val);
int encode_uint16_t(char *buf, const int64_t buf_len, int64_t *pos, uint16_t val);
int encode_int8_t(char *buf, const int64_t buf_len, int64_t *pos, int8_t val);
int encode_uint8_t(char *buf, const int64_t buf_len, int64_t *pos, uint8_t val);

int decode_int8_t(const char *buf, const int64_t data_len, int64_t *pos, int8_t *val);
int decode_uint8_t(const char *buf, const int64_t data_len, int64_t *pos, uint8_t *val);
int decode_int16_t(const char *buf, const int64_t data_len, int64_t *pos, int16_t *val);
int decode_uint16_t(const char *buf, const int64_t data_len, int64_t *pos, uint16_t *val);
int decode_int32_t(const char *buf, const int64_t data_len, int64_t *pos, int32_t *val);
int decode_uint32_t(const char *buf, const int64_t data_len, int64_t *pos, uint32_t *val);
int decode_int64_t(const char *buf, const int64_t data_len, int64_t *pos, int64_t *val);
int decode_uint64_t(const char *buf, const int64_t data_len, int64_t *pos, uint64_t *val);

int64_t encoded_length_int64_t(int64_t val);
int64_t encoded_length_uint64_t(uint64_t val);
int64_t encoded_length_int32_t(int32_t val);
int64_t encoded_length_uint32_t(uint32_t val);
int64_t encoded_length_int16_t(int16_t val);
int64_t encoded_length_uint16_t(uint16_t val);
int64_t encoded_length_int8_t(int8_t unused);
int64_t encoded_length_uint8_t(uint8_t unused);

int64_t encoded_length_float(float val);
int encode_float(char *buf, const int64_t buf_len, int64_t *pos, float val);
int decode_float(const char *buf, const int64_t data_len, int64_t *pos, float *val);
int64_t encoded_length_double(double val);
int encode_double(char *buf, const int64_t buf_len, int64_t *pos, double val);
int decode_double(const char *buf, const int64_t data_len, int64_t *pos, double *val);

/**
 * @brief Computes the encoded length of vstr(int64,data,null)
 *
 * @param len string length
 *
 * @return the encoded length of str
 */
int64_t encoded_length_vstr_with_len(int64_t len);
int64_t encoded_length_vstr(const char *str);

/**
 * @brief get the decoded length of data len of vstr
 * won't change the pos
 * @return the length of data
 */
int64_t decoded_length_vstr(const char *buf, const int64_t data_len, int64_t pos);

/**
 * @brief Encode a buf as vstr(int64,data,null)
 *
 * @param buf pointer to the destination buffer
 * @param vbuf pointer to the start of the input buffer
 * @param len length of the input buffer
 */
int encode_vstr_with_len(char *buf, const int64_t buf_len, int64_t *ppos, const void *vbuf,
                       int64_t len);

int encode_vstr(char *buf, const int64_t buf_len, int64_t *ppos, const char *s);

const char *decode_vstr_nocopy(const char *buf, const int64_t data_len, int64_t *ppos, int64_t *lenp);

const char *decode_vstr(const char *buf, const int64_t data_len, int64_t *ppos,
                               char *dest, int64_t buf_len, int64_t *lenp);

#define OB_UNIS_ENCODE(obj, type)                                       \
  if (OB_SUCC(ret)) {                                                   \
    if (OB_FAIL(encode_##type(buf, buf_len, pos, obj))) {               \
    }                                                                   \
  }

#define OB_UNIS_DECODE(obj, type)                                       \
  if (OB_SUCC(ret) && *pos < data_len) {                                \
    if (OB_FAIL(decode_##type(buf, data_len, pos, &obj))) {             \
    }                                                                   \
  }

#define OB_UNIS_ADD_LEN(obj, type)                                      \
  len += encoded_length_##type(obj)

#endif
