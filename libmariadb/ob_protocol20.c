#include "ob_protocol20.h"

#include <stddef.h>
#include <string.h>

#include "mysql.h"
#include "ma_global.h"
#include "ob_full_link_trace.h"
#include "ob_rwlock.h"

#define UINT24_MAX (16777215U)
#define EXTRA_TYPE_LENGTH 2
#define EXTRA_LEN_LENGTH 4

#define OB20_SERIALIZE_FUNC_SET(id, funcname) (extra_serialize_funcs[id] = (ExtraInfoSerializeFunc)OB20_EXTRAINFO_SERIALIZE_FUNC(funcname))
#define OB20_SERIALIZE_FUNC_INIT()                                        \
  do                                                                      \
  {                                                                       \
    /* FULL_TRACE */                                                      \
    OB20_SERIALIZE_FUNC_SET(FULL_TRC, flt);                               \
  } while (0)

extern void *ma_multi_malloc(myf MyFlags, ...);

static ob_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;
static int init_flag = 0;
static ExtraInfoSerializeFunc extra_serialize_funcs[OB20_SVR_END + 1];

// CRC table for the CRC-16. The poly is 0x8005 (x^16 + x^15 + x^2 + 1)
uint16_t const ob_crc16_table[256] = {0x0000,
    0xC0C1,
    0xC181,
    0x0140,
    0xC301,
    0x03C0,
    0x0280,
    0xC241,
    0xC601,
    0x06C0,
    0x0780,
    0xC741,
    0x0500,
    0xC5C1,
    0xC481,
    0x0440,
    0xCC01,
    0x0CC0,
    0x0D80,
    0xCD41,
    0x0F00,
    0xCFC1,
    0xCE81,
    0x0E40,
    0x0A00,
    0xCAC1,
    0xCB81,
    0x0B40,
    0xC901,
    0x09C0,
    0x0880,
    0xC841,
    0xD801,
    0x18C0,
    0x1980,
    0xD941,
    0x1B00,
    0xDBC1,
    0xDA81,
    0x1A40,
    0x1E00,
    0xDEC1,
    0xDF81,
    0x1F40,
    0xDD01,
    0x1DC0,
    0x1C80,
    0xDC41,
    0x1400,
    0xD4C1,
    0xD581,
    0x1540,
    0xD701,
    0x17C0,
    0x1680,
    0xD641,
    0xD201,
    0x12C0,
    0x1380,
    0xD341,
    0x1100,
    0xD1C1,
    0xD081,
    0x1040,
    0xF001,
    0x30C0,
    0x3180,
    0xF141,
    0x3300,
    0xF3C1,
    0xF281,
    0x3240,
    0x3600,
    0xF6C1,
    0xF781,
    0x3740,
    0xF501,
    0x35C0,
    0x3480,
    0xF441,
    0x3C00,
    0xFCC1,
    0xFD81,
    0x3D40,
    0xFF01,
    0x3FC0,
    0x3E80,
    0xFE41,
    0xFA01,
    0x3AC0,
    0x3B80,
    0xFB41,
    0x3900,
    0xF9C1,
    0xF881,
    0x3840,
    0x2800,
    0xE8C1,
    0xE981,
    0x2940,
    0xEB01,
    0x2BC0,
    0x2A80,
    0xEA41,
    0xEE01,
    0x2EC0,
    0x2F80,
    0xEF41,
    0x2D00,
    0xEDC1,
    0xEC81,
    0x2C40,
    0xE401,
    0x24C0,
    0x2580,
    0xE541,
    0x2700,
    0xE7C1,
    0xE681,
    0x2640,
    0x2200,
    0xE2C1,
    0xE381,
    0x2340,
    0xE101,
    0x21C0,
    0x2080,
    0xE041,
    0xA001,
    0x60C0,
    0x6180,
    0xA141,
    0x6300,
    0xA3C1,
    0xA281,
    0x6240,
    0x6600,
    0xA6C1,
    0xA781,
    0x6740,
    0xA501,
    0x65C0,
    0x6480,
    0xA441,
    0x6C00,
    0xACC1,
    0xAD81,
    0x6D40,
    0xAF01,
    0x6FC0,
    0x6E80,
    0xAE41,
    0xAA01,
    0x6AC0,
    0x6B80,
    0xAB41,
    0x6900,
    0xA9C1,
    0xA881,
    0x6840,
    0x7800,
    0xB8C1,
    0xB981,
    0x7940,
    0xBB01,
    0x7BC0,
    0x7A80,
    0xBA41,
    0xBE01,
    0x7EC0,
    0x7F80,
    0xBF41,
    0x7D00,
    0xBDC1,
    0xBC81,
    0x7C40,
    0xB401,
    0x74C0,
    0x7580,
    0xB541,
    0x7700,
    0xB7C1,
    0xB681,
    0x7640,
    0x7200,
    0xB2C1,
    0xB381,
    0x7340,
    0xB101,
    0x71C0,
    0x7080,
    0xB041,
    0x5000,
    0x90C1,
    0x9181,
    0x5140,
    0x9301,
    0x53C0,
    0x5280,
    0x9241,
    0x9601,
    0x56C0,
    0x5780,
    0x9741,
    0x5500,
    0x95C1,
    0x9481,
    0x5440,
    0x9C01,
    0x5CC0,
    0x5D80,
    0x9D41,
    0x5F00,
    0x9FC1,
    0x9E81,
    0x5E40,
    0x5A00,
    0x9AC1,
    0x9B81,
    0x5B40,
    0x9901,
    0x59C0,
    0x5880,
    0x9841,
    0x8801,
    0x48C0,
    0x4980,
    0x8941,
    0x4B00,
    0x8BC1,
    0x8A81,
    0x4A40,
    0x4E00,
    0x8EC1,
    0x8F81,
    0x4F40,
    0x8D01,
    0x4DC0,
    0x4C80,
    0x8C41,
    0x4400,
    0x84C1,
    0x8581,
    0x4540,
    0x8701,
    0x47C0,
    0x4680,
    0x8641,
    0x8201,
    0x42C0,
    0x4380,
    0x8341,
    0x4100,
    0x81C1,
    0x8081,
    0x4040};

inline uint16_t ob_crc16_byte(uint16_t crc, const uint8_t data)
{
	return (uint16_t)((crc >> 8) ^ ob_crc16_table[(crc ^ data) & 0xff]);
}

/**
 * ob_crc16 - compute the CRC-16 for the data buffer
 * @crc:	previous CRC value
 * @buffer:	data pointer
 * @len:	number of bytes in the buffer
 *
 * Returns the updated CRC value.
 */
inline uint16_t ob_crc16(uint16_t crc, uint8_t const *buffer, int64_t len)
{
	while (len--) {
		crc = ob_crc16_byte(crc, *buffer++);
  }
	return crc;
}


int ob20_init(Ob20Protocol *ob20protocol, unsigned long conid, my_bool use_flt)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(ob20protocol)) {
    ret = OB_ERROR;
  } else {
    if (!init_flag) {
      ob_mutex_lock(&init_mutex);
      if (!init_flag) {
        OB20_SERIALIZE_FUNC_INIT();                   // init extra info serialize func
        init_flag = 1;
      }
      ob_mutex_unlock(&init_mutex);
    }
    memset(ob20protocol, 0, sizeof(*(ob20protocol)));

    ob20protocol->header.magic_num = OB20_PROTOCOL_MAGIC_NUM;
    ob20protocol->header.version = OB20_PROTOCOL_VERSION_VALUE;
    ob20protocol->header.request_id = rand() % UINT_MAX24;
    ob20protocol->extra_info_list.list = NULL;
    ob20protocol->extra_info_list.current = NULL;
    ob20protocol->header.connection_id = conid;
    ob20protocol->flt = NULL;
    ob20protocol->update_request_id = 1;
    if (use_flt) {
      ob20protocol->flt = malloc(sizeof(FLTInfo));
      if (OB_ISNULL(ob20protocol->flt)) {
        // malloc error
        ret = OB_ERROR;
      } else if (OB_FAIL(flt_init(ob20protocol->flt))) {
        // error
      } else {
        // printf("connect with ob20 and full link trace, connection id is %lu\n", conid);
      }
    }
  }
  
  return ret;
}

void ob20_end(Ob20Protocol *ob20protocol)
{
  if (OB_NOT_NULL(ob20protocol)) {
    clear_extra_info(ob20protocol);
    if (OB_NOT_NULL(ob20protocol->real_write_buffer)) {
      free(ob20protocol->real_write_buffer);
      ob20protocol->real_write_buffer = NULL;
      ob20protocol->real_write_buffer_length = 0;
    }
    if (OB_NOT_NULL(ob20protocol->flt)) {
      flt_end(ob20protocol->flt);
      free(ob20protocol->flt);
      ob20protocol->flt = NULL;
    }
  }
}

void clear_extra_info(Ob20Protocol *ob20protocol)
{
  if (OB_NOT_NULL(ob20protocol)) {
    ob20protocol->extra_info_list.current = ob20protocol->extra_info_list.list;
    while (OB_NOT_NULL(ob20protocol->extra_info_list.list)) {
      ob20protocol->extra_info_list.list = ob20protocol->extra_info_list.list->next;
      list_free(ob20protocol->extra_info_list.current, 0);
      ob20protocol->extra_info_list.current = ob20protocol->extra_info_list.list;
    }
    memset(&ob20protocol->extra_info_list, 0, sizeof(ob20protocol->extra_info_list));
  }
}

void update_request_id(uint32_t *request_id)
{
  *request_id = *request_id + 1;
  if (*request_id > UINT24_MAX) {
    *request_id = 0;
  }
}

uchar *fill_protocol20_header(Ob20Protocol *ob20protocol, size_t len, size_t pkt_nr, size_t complen,uchar *buffer)
{
  size_t pos = 0;

  int3store(&buffer[NET_HEADER_SIZE], complen);
  int3store(&buffer[pos], len);
  buffer[3]=(uchar) (pkt_nr);
  pos += NET_HEADER_SIZE + COMP_HEADER_SIZE;

  int2store(&buffer[pos], ob20protocol->header.magic_num);
  pos += 2;
  int2store(&buffer[pos], ob20protocol->header.version);
  pos += 2;
  int4store(&buffer[pos], ob20protocol->header.connection_id);
  pos += 4;
  int3store(&buffer[pos], ob20protocol->header.request_id);
  pos += 3;
  ob20protocol->header.pkt_seq = pkt_nr;
  int1store(&buffer[pos], ob20protocol->header.pkt_seq);
  pos += 1;
  int4store(&buffer[pos], ob20protocol->header.payload_len);
  pos += 4;
  int4store(&buffer[pos], ob20protocol->header.flag.flags);
  pos += 4;
  int2store(&buffer[pos], ob20protocol->header.reserved);
  pos += 2;
  ob20protocol->header.header_checksum = ob_crc16(0, (uint8_t *)buffer, pos);
  int2store(&buffer[pos], ob20protocol->header.header_checksum);
  pos += 2;

  #ifdef DEBUG_OB20
    printf("////////////////begin ob20 pkt write[%u]//////////////////////////\n", pkt_nr);
    printf("mysql pkt_len is %u\n", len);
    printf("mysql pkt_nr is %u\n", pkt_nr);
    printf("complen is %u\n", complen);
    printf("magic_num is %u\n", ob20protocol->header.magic_num);
    printf("version is %u\n", ob20protocol->header.version);
    printf("connection_id is %u\n", ob20protocol->header.connection_id);
    printf("request_id is %u\n", ob20protocol->header.request_id);
    printf("ob20 pkt_seq is %u\n", ob20protocol->header.pkt_seq);
    printf("ob20 payload_len is %u\n", ob20protocol->header.payload_len);
    printf("flag is %u\n", ob20protocol->header.flag.flags);
    printf("header checksum is %u\n", ob20protocol->header.header_checksum);
    printf("////////////////end ob20 pkt write[%u]//////////////////////////\n", pkt_nr);
#endif

  return buffer + pos;
}


// decode protocol20 header, pass the params pkt_len , pktnr and complen for checksum
int decode_protocol20_header(Ob20Protocol *ob20protocol, uchar *buffer, uint32_t pkt_len, uint32_t pkt_nr, uint32_t complen)
{
  int ret = 0;
  size_t pos = 0;
  uint16_t magic_num;
  uint16_t version;
  uint32_t connection_id;
  uint32_t request_id;
  uint8_t pkt_seq;
  uint32_t payload_len;
  Ob20ProtocolFlags flag;
  uint16_t reserved;
  uint16_t header_checksum;
  uint16_t crc16 = 0;
  char header_buffer[NET_HEADER_SIZE + COMP_HEADER_SIZE];

  // decode
  magic_num = uint2korr(&buffer[pos]);
  pos += 2;
  version = uint2korr(&buffer[pos]);
  pos += 2;
  connection_id = uint4korr(&buffer[pos]);
  pos += 4;
  request_id = uint3korr(&buffer[pos]);
  pos += 3;
  pkt_seq = uint1korr(&buffer[pos]);
  pos += 1;
  payload_len = uint4korr(&buffer[pos]);
  pos += 4;
  flag.flags = uint4korr(&buffer[pos]);
  pos += 4;
  reserved = uint2korr(&buffer[pos]);
  pos += 2;
  header_checksum = uint2korr(&buffer[pos]);

  // checksum
  if (0 != header_checksum) {
    int3store(header_buffer, pkt_len);
    header_buffer[3] = pkt_nr;
    int3store(&header_buffer[NET_HEADER_SIZE], complen);
    crc16 = ob_crc16(crc16, (uint8_t *)header_buffer, NET_HEADER_SIZE + COMP_HEADER_SIZE);
    crc16 = ob_crc16(crc16, (uint8_t *)buffer, pos);
  } else {
    crc16 = 0;
  }

  if (crc16 != header_checksum) {
    ret = 1;        // error
  } else if (magic_num != ob20protocol->header.magic_num) {
    ret = 1;        // error
  } else if (version != ob20protocol->header.version) {
    ret = 1;        // error
  } else if (connection_id != ob20protocol->header.connection_id) {
    ret = 1;        // error
  } else if (request_id != ob20protocol->header.request_id) {
    ret = 1;        // error
  } else {
    ob20protocol->header.pkt_seq++;
    if (pkt_seq != ob20protocol->header.pkt_seq) {
      ret = 1;        // error   
    } else {
      ob20protocol->header.flag = flag;
      ob20protocol->header.payload_len = payload_len;
      ob20protocol->header.pkt_seq = pkt_seq;
      ob20protocol->header.reserved = reserved;
      ob20protocol->header.header_checksum = header_checksum;
    }
  }

#ifdef DEBUG_OB20
    printf("////////////////begin ob20 pkt read[%u]//////////////////////////\n", pkt_nr);
    printf("mysql pkt_len is %u\n", pkt_len);
    printf("mysql pkt_nr is %u\n", pkt_nr);
    printf("complen is %u\n", complen);
    printf("magic_num is %u\n", magic_num);
    printf("version is %u\n", version);
    printf("connection_id is %u\n", connection_id);
    printf("request_id is %u\n", request_id);
    printf("ob20 pkt_seq is %u\n", pkt_seq);
    printf("ob20 payload_len is %u\n", payload_len);
    printf("flag is %u\n", flag.flags);
    printf("header checksum is %u\n", header_checksum);

    if (0 != ret) {
      printf("decode header error\n");
    }
    printf("////////////////end ob20 pkt read[%u]//////////////////////////\n", pkt_nr);
#endif

  return ret;
}

int extrainfo_serialize_flt(char *buf, const int64_t len, int64_t *ppos, void *data)
{
  int ret = OB_SUCCESS;
  int64_t pos = *ppos;
  FLTValueData *flt_data = (FLTValueData *)data;
  int64_t v_len = flt_data->length;

  if (OB_ISNULL(buf)) {
    ret = OB_ERROR;
  } else if (len < pos + EXTRA_TYPE_LENGTH + EXTRA_LEN_LENGTH + v_len) {
    ret = OB_ERROR;
  } else {
    int2store(buf + pos, FULL_TRC);
    pos += 2;
    int4store(buf + pos, v_len);
    pos += 4;

    memcpy(buf + pos, flt_data->value_data_, v_len);
    pos += v_len;

    *ppos = pos;
  }
  return ret;
}

int extrainfo_deserialize_flt(const char *buf, const int64_t len, int64_t *ppos, void *data, const int64_t v_len)
{
  int ret = OB_SUCCESS;
  int64_t pos = *ppos;
  Ob20Protocol *ob20protocol = (Ob20Protocol *)data;
  UNUSED(ob20protocol);
  UNUSED(v_len);
  while (OB_SUCC(ret) && pos < len) {
    int32_t tmp_v_len = 0;
    int64_t tmp_v_pos = 0;
    int16_t type = FLT_EXTRA_INFO_ID_END;
    void *flt_info = NULL;
    
    if (OB_FAIL(flt_resolve_type_and_len(buf, len, &pos, &type, &tmp_v_len))) {
      ret = OB_ERROR;
    } else {
      my_bool know_flt_info = TRUE;

      if (FLT_CONTROL_INFO == type) {
        flt_info = &ob20protocol->flt->control_info_;
      } else if (FLT_APP_INFO == type) {
        flt_info = &ob20protocol->flt->app_info_;
      } else if (FLT_QUERY_INFO == type) {
        flt_info = &ob20protocol->flt->query_info_;
      } else if (FLT_SPAN_INFO == type) {
        flt_info = &ob20protocol->flt->control_info_;
      } else {
        know_flt_info = FALSE;
      }
      if (TRUE == know_flt_info) {
        if (OB_ISNULL(flt_info)) {
          ret = OB_ERROR;
        } else {
          ((FLTInfoBase *)flt_info)->type_ = (FullLinkTraceExtraInfoType)type;
          if (OB_FAIL(flt_deserialize_extra_info(buf + pos, tmp_v_len, &tmp_v_pos, (FullLinkTraceExtraInfoType)type, flt_info))) {
            ret = OB_ERROR;
          } else {
            pos += tmp_v_len;
          }
        }
      } else {
        // For unrecognized keys, need to skip
        pos += tmp_v_len;
      }
    }
  }
  if (OB_SUCC(ret)) {
    *ppos = pos;
  }
  return ret;
}

int64_t extrainfo_get_serialize_size_flt(void *data)
{
  int64_t ret = 0;
  FLTValueData *flt_data = (FLTValueData *)data;
  if (OB_NOT_NULL(flt_data)) {
    ret = EXTRA_TYPE_LENGTH + EXTRA_LEN_LENGTH + flt_data->length;
  }
  return ret;
}

uchar *fill_protocol20_extra_info(Ob20Protocol *ob20protocol, uchar *buffer, size_t buffer_len)
{
  char *packet = (char *)buffer;
  int64_t pos = 0;
  int ret = OB_SUCCESS;

  if (OB_ISNULL(ob20protocol) || OB_ISNULL(ob20protocol->extra_info_list.list)) {
    // do nothing
  } else {
    Ob20ProtocolExtraInfo *extra_info_item;

    ob20protocol->extra_info_list.current = ob20protocol->extra_info_list.list;

    while (NULL != ob20protocol->extra_info_list.current) {
      extra_info_item = ob20protocol->extra_info_list.current->data;
      if (OB_ISNULL(extra_serialize_funcs[extra_info_item->key].serialize_func)) {
        // do nothing
      } if (OB_FAIL(extra_serialize_funcs[extra_info_item->key].serialize_func(packet + pos, buffer_len, &pos, extra_info_item->value))) {
        return buffer;
      }
      ob20protocol->extra_info_list.current = ob20protocol->extra_info_list.current->next;
    }
  }

  return (uchar *)(packet + pos);
}

size_t get_protocol20_extra_info_length(Ob20Protocol *ob20protocol)
{
  size_t ret = 0;
  size_t item_size;
  Ob20ProtocolExtraInfo *extra_info_item;
  ob20protocol->extra_info_list.current = ob20protocol->extra_info_list.list;

  while(NULL != ob20protocol->extra_info_list.current) {
    extra_info_item = (Ob20ProtocolExtraInfo *)ob20protocol->extra_info_list.current->data;
    if (OB_ISNULL(extra_serialize_funcs[extra_info_item->key].get_serialize_size_func)) {
      item_size = 0;
    } else {
      item_size = extra_serialize_funcs[extra_info_item->key].get_serialize_size_func(extra_info_item->value);
    }

    ob20protocol->extra_info_list.current = ob20protocol->extra_info_list.current->next;
    ret += item_size;
  }
  return ret;
}

int decode_protocol20_extra_info(Ob20Protocol *ob20protocol, uchar *buffer)
{
  int ret = OB_SUCCESS;
  int64_t extra_info_len = 0;
  int64_t pos = 0, check_pos;
  ExtraInfoKeyType key_type;
  const char *row = (char *)buffer;
  size_t value_len;

  extra_info_len = uint4korr(row);
  row += 4;

  while (OB_SUCC(ret) && pos < extra_info_len) {
    // key is 2 bytes
    key_type = uint2korr(row + pos);
    pos += 2;
    value_len = uint4korr(row + pos);
    pos += 4;

    check_pos = pos;
    if (OB_ISNULL(extra_serialize_funcs[key_type].deserialize_func)) {
      // If the function pointer is not set, skip the value directly without parsing
      pos += value_len;
    } else if (OB_FAIL(extra_serialize_funcs[key_type].deserialize_func(row, extra_info_len, &pos, ob20protocol, sizeof(*ob20protocol)))) {
      // error
    } else if (pos != check_pos + (int64_t)value_len) {
      // error
      ret = OB_ERROR;
    }
  }
  return ret;
}

int ob20_set_extra_info(MYSQL *mysql, ExtraInfoKeyType key, void *value)
{
  int ret = OB_SUCCESS;
  Ob20Protocol *ob20protocol = mysql->net.ob20protocol;
  LIST *extra_info_list_item = NULL;
  Ob20ProtocolExtraInfo *extra_info_item = NULL;

  if (!(extra_info_list_item = ma_multi_malloc(0,
                                    &extra_info_list_item, sizeof(LIST),
                                    &extra_info_item, sizeof(Ob20ProtocolExtraInfo),
                                    NULL))) {
    ret = OB_ERROR;
  } else {
    extra_info_item->key = key;
    extra_info_item->value = value;
    extra_info_list_item->data = extra_info_item;
    ob20protocol->extra_info_list.list = list_add(ob20protocol->extra_info_list.list, extra_info_list_item);
  }
  return ret;
}
