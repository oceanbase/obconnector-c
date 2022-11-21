/* Copyright (C) 2000 MySQL AB & MySQL Finland AB & TCX DataKonsult AB
   2012-2016 SkySQL AB, MariaDB Corporation AB
   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with this library; if not, write to the Free
   Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
   MA 02111-1301, USA */

/* Write and read of logical packets to/from socket
 ** Writes are cached into net_buffer_length big packets.
 ** Read packets are reallocated dynamically when reading big packets.
 ** Each logical packet has the following pre-info:
 ** 3 byte length & 1 byte package-number.
 */


#include <ma_global.h>
#include <mysql.h>
#include <ma_pvio.h>
#include <ma_sys.h>
#include <ma_string.h>
#include "mysql.h"
#include "ma_server_error.h"
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <ma_pvio.h>
#include <ma_common.h>
#ifndef _WIN32
#include <poll.h>
#endif
#include <ob_protocol20.h>

#define MAX_PACKET_LENGTH (256L*256L*256L-1)
#define MAX_PACKET_LENGTH_WITH_OB20 (MAX_PACKET_LENGTH - OB20_HEADER_SIZE - OB20_TAILER_SIZE - OB20_EXTRAINFO_LENGTH_SIZE)

#ifndef HAVE_COMPRESS
#define HAVE_COMPRESS
#endif

/* net_buffer_length and max_allowed_packet are defined in mysql.h
   See bug conc-57
 */
#undef net_buffer_length

#undef max_allowed_packet
ulong max_allowed_packet=1024L * 1024L * 1024L;
ulong net_read_timeout=  NET_READ_TIMEOUT;
ulong net_write_timeout= NET_WRITE_TIMEOUT;
ulong net_buffer_length= 8192;	/* Default length. Enlarged if necessary */
// ulong net_buffer_length= 10L * 1024L * 1024L;	/* 10M:change for ob20 */

#if !defined(_WIN32)
#include <sys/socket.h>
#else
#undef MYSQL_SERVER			/* Win32 can't handle interrupts */
#endif
#if !defined(_WIN32) && !defined(HAVE_BROKEN_NETINET_INCLUDES) && !defined(__BEOS__)
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#if !defined(alpha_linux_port)
#include <netinet/tcp.h>
#endif
#endif

/*
 ** Give error if a too big packet is found
 ** The server can change this with the -O switch, but because the client
 ** can't normally do this the client should have a bigger max-buffer.
 */

static int ma_net_write_buff(NET *net,const char *packet, size_t len);


/* Init with packet info */

int ma_net_init(NET *net, MARIADB_PVIO* pvio)
{
  if (!(net->buff=(uchar*) malloc(net_buffer_length)))
    return 1;
  if (!net->extension)
    return 1;

  memset(net->buff, 0, net_buffer_length);

  net->max_packet_size= MAX(net_buffer_length, max_allowed_packet);
  net->buff_end=net->buff+(net->max_packet=net_buffer_length);
  net->pvio = pvio;
  net->error=0; net->return_status=0;
  net->read_timeout=(uint) net_read_timeout;		/* Timeout for read */
  net->compress_pkt_nr= net->pkt_nr= 0;
  net->write_pos=net->read_pos = net->buff;
  net->last_error[0]= net->sqlstate[0] =0;

  net->compress=0; net->reading_or_writing=0;
  net->where_b = net->remain_in_buf=0;
  net->last_errno=0;

  net->use_ob20protocol = 0;

  if (pvio != 0)					/* If real connection */
  {
    ma_pvio_get_handle(pvio, &net->fd);
    ma_pvio_blocking(pvio, 1, 0);
    ma_pvio_fast_send(pvio);
  }
  return 0;
}

void ma_net_end(NET *net)
{
  free(net->buff);
  net->buff=0;
  if (net->ob20protocol) {
    ob20_end(net->ob20protocol);
    free(net->ob20protocol);
    net->ob20protocol = 0;
    net->use_ob20protocol = 0;
    net->compress = 0;
  }
}

/* Realloc the packet buffer */

static my_bool net_realloc(NET *net, size_t length)
{
  uchar *buff;
  size_t pkt_length;

  if (length >= net->max_packet_size)
  {
    net->error=1;
    net->last_errno=ER_NET_PACKET_TOO_LARGE;
    return(1);
  }
  pkt_length = (length+IO_SIZE-1) & ~(IO_SIZE-1);
  /* reallocate buffer:
     size= pkt_length + NET_HEADER_SIZE + COMP_HEADER_SIZE */
  if (!(buff=(uchar*) realloc(net->buff, 
          pkt_length + NET_HEADER_SIZE + COMP_HEADER_SIZE)))
  {
    net->error=1;
    return(1);
  }
  net->buff=net->write_pos=buff;
  net->buff_end=buff+(net->max_packet=(unsigned long)pkt_length);
  return(0);
}

/* Remove unwanted characters from connection */
void ma_net_clear(NET *net)
{
  if (net->extension->multi_status > COM_MULTI_OFF)
    return;
  net->compress_pkt_nr= net->pkt_nr=0;				/* Ready for new command */
  net->write_pos=net->buff;
  return;
}

/* Flush write_buffer if not empty. */
int ma_net_flush(NET *net)
{
  int error=0;

  /* don't flush if pipelined query is in progress */
  if (net->extension->multi_status > COM_MULTI_OFF)
    return 0;

  if (net->buff != net->write_pos)
  {
    error=ma_net_real_write(net,(char*) net->buff,
        (size_t) (net->write_pos - net->buff));
    net->write_pos=net->buff;
  }
  if (net->compress)
    net->pkt_nr= net->compress_pkt_nr;
  return(error);
}

/*****************************************************************************
 ** Write something to server/client buffer
 *****************************************************************************/

/*
 ** Write a logical packet with packet header
 ** Format: Packet length (3 bytes), packet number(1 byte)
 **         When compression is used a 3 byte compression length is added
 ** NOTE: If compression is used the original package is destroyed!
 */

int ma_net_write(NET *net, const uchar *packet, size_t len)
{
  uchar buff[NET_HEADER_SIZE];
  // update ob20 request id
  if (net->use_ob20protocol && OB_NOT_NULL(net->ob20protocol)) {
    update_request_id(&net->ob20protocol->header.request_id);
  }
  while (len >= MAX_PACKET_LENGTH)
  {
    const ulong max_len= MAX_PACKET_LENGTH;
    int3store(buff,max_len);
    buff[3]= (uchar)net->pkt_nr++;
    if (ma_net_write_buff(net,(char*) buff,NET_HEADER_SIZE) ||
        ma_net_write_buff(net, (char *)packet, max_len))
      return 1;
    packet+= max_len;
    len-= max_len;
  }
  /* write last remaining packet, size can be zero */
  int3store(buff, len);
  buff[3]= (uchar)net->pkt_nr++;
  if (ma_net_write_buff(net,(char*) buff,NET_HEADER_SIZE) ||
      ma_net_write_buff(net, (char *)packet, len))
    return 1;
  return 0;
}

int ma_net_write_command(NET *net, uchar command,
    const char *packet, size_t len,
    my_bool disable_flush)
{
  uchar buff[NET_HEADER_SIZE+1];
  size_t buff_size= NET_HEADER_SIZE + 1;
  size_t length= 1 + len; /* 1 extra byte for command */
  int rc;

  buff[NET_HEADER_SIZE]= 0;
  buff[4]=command;

  // update ob20 request id
  if (net->use_ob20protocol && OB_NOT_NULL(net->ob20protocol)) {
    update_request_id(&net->ob20protocol->header.request_id);
  }

  if (length >= MAX_PACKET_LENGTH)
  {
    len= MAX_PACKET_LENGTH - 1;
    do
    {
      int3store(buff, MAX_PACKET_LENGTH);
      buff[3]= (net->compress) ? 0 : (uchar) (net->pkt_nr++);

      if (ma_net_write_buff(net, (char *)buff, buff_size) ||
          ma_net_write_buff(net, packet, len))
        return(1);
      packet+= len;
      length-= MAX_PACKET_LENGTH;
      len= MAX_PACKET_LENGTH;
      buff_size= NET_HEADER_SIZE; /* don't send command for further packets */
    } while (length >= MAX_PACKET_LENGTH);
    len= length;
  }
  int3store(buff,length);
  buff[3]= (net->compress) ? 0 :(uchar) (net->pkt_nr++);
  rc= test (ma_net_write_buff(net,(char *)buff, buff_size) || 
      ma_net_write_buff(net,packet,len));
  if (!rc && !disable_flush)
    return test(ma_net_flush(net)); 
  return rc;
}


static int ma_net_write_buff(NET *net,const char *packet, size_t len)
{
  size_t left_length;
  size_t extra_info_length = 0;

  if (!len)
    return 0;

  // 传入ma_net_real_write的buffer必须为MAX_PACKET_LENGTH - extra_info_length
  if (net->use_ob20protocol) {
    extra_info_length = get_protocol20_extra_info_length(net->ob20protocol);
    // 1.如果extra info length大于能存放的最大长度, 直接报错
    // 2.如果extra info length + 当前buffer里存放的packet大于最大长度，单独发一个extra info包
    // 3.如果extra info length + 当前buffer里存放的packet + 参数传入的packet length大于最大长度，
    //    将当前buffer中的buffer和extra info发送出去
    // 4.如果所有的都能放下，直接走到下面的流程
    if (extra_info_length > MAX_PACKET_LENGTH_WITH_OB20) {
      return 1;   //error
    } else if (extra_info_length + (size_t)(net->write_pos - net->buff) > MAX_PACKET_LENGTH_WITH_OB20) {
      if (ma_net_real_write(net,(char*) net->buff, 0)) {  // buffer为0，只写20头和extra info
        return 1;  // error
      }
    } else if (extra_info_length + (size_t)(net->write_pos - net->buff) + len > MAX_PACKET_LENGTH_WITH_OB20) {
      // 将当前的buffer和extra info写入到网络包中
      if (ma_net_real_write(net,(char*) net->buff, (size_t)(net->write_pos - net->buff))) {
        return 1;
      }
      net->write_pos= net->buff;
    } else {
      // 当前所有都能放入一个包中, 这个分支会走到最后memcpy，然后结束
    }
  }

  if (net->max_packet > MAX_PACKET_LENGTH && net->compress) {
    if (net->use_ob20protocol) {
      left_length= (size_t)(MAX_PACKET_LENGTH_WITH_OB20 - (net->write_pos - net->buff));
    } else {
      left_length= (size_t)(MAX_PACKET_LENGTH - (net->write_pos - net->buff));
    }
  } else {
    left_length=(size_t) (net->buff_end - net->write_pos);
  }

  if (net->use_ob20protocol && len > left_length && net->buf_length < net->max_packet_size - 1) {
    size_t write_len = net->write_pos - net->buff;
    size_t resize_len = len + write_len;

    if (resize_len >= net->max_packet_size) {
      resize_len = net->max_packet_size - 1;
    }
    if (net_realloc(net, resize_len))
    {
      return 1;
    }
    net->write_pos += write_len;
    left_length=(size_t) (net->buff_end - net->write_pos);
  }

  if (len > left_length)
  {
    if (net->write_pos != net->buff)
    {
      memcpy((char*) net->write_pos,packet,left_length);
      if (ma_net_real_write(net,(char*) net->buff,
            (size_t)(net->write_pos - net->buff) + left_length))
        return 1;
      packet+=left_length;
      len-=left_length;
      net->write_pos= net->buff;
    }
    if (net->compress)
    {
      if (net->use_ob20protocol) {
        left_length= MAX_PACKET_LENGTH_WITH_OB20;
      } else {
        /* uncompressed length is stored in 3 bytes,so
          packet can't be > 0xFFFFFF */
        left_length= MAX_PACKET_LENGTH;
      }
      while (len > left_length)
      {
        if (ma_net_real_write(net, packet, left_length))
          return 1;
        packet+= left_length;
        len-= left_length;
      }
    }
    if (len > net->max_packet)
      return(test(ma_net_real_write(net, packet, len)));
  }
  memcpy((char*) net->write_pos,packet,len);
  net->write_pos+=len;
  return 0;
}

unsigned char *mysql_net_store_length(unsigned char *packet, size_t length);

/*  Read and write using timeouts */

int ma_net_real_write(NET *net, const char *packet, size_t len)
{
  ssize_t length;
  char *pos,*end;

  if (net->error == 2)
    return(-1);				/* socket can't be used */

  net->reading_or_writing=2;
#ifdef HAVE_COMPRESS
  if (net->compress)
  {
    size_t complen;
    uchar *b;
    uchar *check_result;
    uint header_length=NET_HEADER_SIZE+COMP_HEADER_SIZE;

    if (net->use_ob20protocol)
    {
      // use ob20 protocol
      uint32_t ob20_header_length = header_length + OB20_HEADER_SIZE;
      uint32_t extra_info_length = 0;
      uint32_t ob20_packet_len = 0;
      uint32_t real_write_buffer_length = 0;
      uint32_t crc32 = 0;

      extra_info_length = get_protocol20_extra_info_length(net->ob20protocol);
      if (0 != extra_info_length) {
        ob20_packet_len = len + extra_info_length +  OB20_HEADER_SIZE + OB20_TAILER_SIZE + OB20_EXTRAINFO_LENGTH_SIZE;
      } else {
        ob20_packet_len = len + extra_info_length +  OB20_HEADER_SIZE + OB20_TAILER_SIZE;
      }

      real_write_buffer_length = ob20_packet_len + NET_HEADER_SIZE + COMP_HEADER_SIZE + 1;
      if (net->ob20protocol->real_write_buffer_length < real_write_buffer_length) {
        net->ob20protocol->real_write_buffer = realloc(net->ob20protocol->real_write_buffer, real_write_buffer_length);
        if (OB_ISNULL(net->ob20protocol->real_write_buffer)) {
          net->last_errno=ER_OUT_OF_RESOURCES;
          net->error=2;
          net->reading_or_writing=0;
          return(1);
        } else {
          net->ob20protocol->real_write_buffer_length = real_write_buffer_length;
        }
      }
      b = net->ob20protocol->real_write_buffer;

      // store extra info
      if (0 != extra_info_length) {
        int4store(b + ob20_header_length, extra_info_length);

        check_result = fill_protocol20_extra_info(net->ob20protocol, b + ob20_header_length + OB20_EXTRAINFO_LENGTH_SIZE, extra_info_length);
        net->ob20protocol->header.flag.st_flags.OB_IS_NEW_EXTRA_INFO = 1;
        net->ob20protocol->header.flag.st_flags.OB_EXTRA_INFO_EXIST = 1;
        net->ob20protocol->header.flag.st_flags.OB_IS_LAST_PACKET = 0;
        if (check_result != b + ob20_header_length  + OB20_EXTRAINFO_LENGTH_SIZE + extra_info_length) {
          return 1;
        } else {
          extra_info_length += OB20_EXTRAINFO_LENGTH_SIZE; // storing extra_info_length use 4 bytes
        }
        // clear extra info
        clear_extra_info(net->ob20protocol);
      } else {
        net->ob20protocol->header.flag.st_flags.OB_EXTRA_INFO_EXIST = 0;
        net->ob20protocol->header.flag.st_flags.OB_IS_NEW_EXTRA_INFO = 0;
      }

      // store basic info
      memcpy(b + ob20_header_length + extra_info_length, packet, len);

      // store crc64 checksum
      crc32 = ob_crc32(crc32, (char *)b + ob20_header_length, len + extra_info_length);
      int4store(b + ob20_header_length + extra_info_length + len, crc32);

      net->ob20protocol->header.payload_len = len + extra_info_length;
      check_result = fill_protocol20_header(net->ob20protocol, ob20_packet_len, net->compress_pkt_nr++, 0, b);
      if (check_result != b + ob20_header_length) {
        return 1;
      }

      len = ob20_packet_len + NET_HEADER_SIZE + COMP_HEADER_SIZE;
      packet = (char *)b;
    }
    else
    {
      if (!(b=(uchar*) malloc(len + NET_HEADER_SIZE + COMP_HEADER_SIZE + 1)))
      {
        net->last_errno=ER_OUT_OF_RESOURCES;
        net->error=2;
        net->reading_or_writing=0;
        return(1);
      }
      memcpy(b+header_length,packet,len);

      if (_mariadb_compress((unsigned char*) b+header_length,&len,&complen))
      {
        complen=0;
      }

      int3store(&b[NET_HEADER_SIZE],complen);
      int3store(b,len);
      b[3]=(uchar) (net->compress_pkt_nr++);
      len+= header_length;
      packet= (char*) b;
    }
  }
#endif /* HAVE_COMPRESS */

  pos=(char*) packet; end=pos+len;
  while (pos != end)
  {
    if ((length=ma_pvio_write(net->pvio,(uchar *)pos,(size_t) (end-pos))) <= 0)
    {
      net->error=2;				/* Close socket */
      net->last_errno= ER_NET_ERROR_ON_WRITE;
      net->reading_or_writing=0;
#ifdef HAVE_COMPRESS
      if (net->compress && !net->use_ob20protocol)
        free((char*) packet);       // 2.0协议buffer自管理
#endif
      return(1);
    }
    pos+=length;
  }
#ifdef HAVE_COMPRESS
  if (net->compress && !net->use_ob20protocol)
    free((char*) packet);       // 2.0协议buffer自管理
#endif
  net->reading_or_writing=0;
  return(((int) (pos != end)));
}

/*****************************************************************************
 ** Read something from server/clinet
 *****************************************************************************/
static ulong ma_real_read(NET *net, size_t *complen)
{
  uchar *pos;
  ssize_t length;
  uint i;
  ulong len=packet_error;
  size_t remain= (net->compress ? NET_HEADER_SIZE+COMP_HEADER_SIZE :
      NET_HEADER_SIZE);
  *complen = 0;

  net->reading_or_writing=1;

  pos = net->buff + net->where_b;		/* net->packet -4 */
  for (i=0 ; i < 2 ; i++)
  {
    while (remain > 0)
    {
      /* First read is done with non blocking mode */
      if ((length=ma_pvio_cache_read(net->pvio, pos,remain)) <= 0L)
      {
        len= packet_error;
        net->error=2;				/* Close socket */
        goto end;
      }
      remain -= (ulong) length;
      pos+= (ulong) length;
    }

    if (i == 0)
    {					/* First parts is packet length */
      ulong helping;
      net->pkt_nr= net->buff[net->where_b + 3];
      net->compress_pkt_nr= ++net->pkt_nr;
#ifdef HAVE_COMPRESS
      if (net->compress)
      {
        /* complen is > 0 if package is really compressed */
        *complen=uint3korr(&(net->buff[net->where_b + NET_HEADER_SIZE]));
      }
#endif

      len=uint3korr(net->buff+net->where_b);
      if (!len)
        goto end;
      helping = max(len,(ulong)*complen) + net->where_b;
      /* The necessary size of net->buff */
      if (helping >= net->max_packet)
      {
        if (net_realloc(net, helping))
        {
          len= packet_error;		/* Return error */
          goto end;
        }
      }
      pos=net->buff + net->where_b;
      remain = len;
    }
  }

end:
  net->reading_or_writing=0;
  return(len);
}

ulong ma_net_read(NET *net)
{
  size_t len,complen;

#ifdef HAVE_COMPRESS
  if (!net->compress)
  {
#endif
    len = ma_real_read (net,(size_t *)&complen);
    if (len == MAX_PACKET_LENGTH)
    {
      /* multi packet read */
      size_t length= 0;
      ulong last_pos= net->where_b;

      do 
      {
        length+= len;
        net->where_b+= (unsigned long)len;
        len= ma_real_read(net, &complen);
      } while (len == MAX_PACKET_LENGTH);
      net->where_b= last_pos;
      if (len != packet_error)
        len+= length;
    }
    net->read_pos = net->buff + net->where_b;
    if (len != packet_error)
      net->read_pos[len]=0;		/* Safeguard for mysql_use_result */
    return (ulong)len;
#ifdef HAVE_COMPRESS
  }
  else
  {
    /* 
       compressed protocol:

       --------------------------------------
       packet_length       3
       sequence_id         1
       uncompressed_length 3
       --------------------------------------
       compressed data     packet_length - 7
       --------------------------------------

       Another packet will follow if:
       packet_length == MAX_PACKET_LENGTH

       Last package will be identified by
       - packet_length is zero (special case)
       - packet_length < MAX_PACKET_LENGTH
     */

    size_t packet_length,
           buffer_length;
    size_t current= 0, start= 0;
    my_bool is_multi_packet= 0;

    /* check if buffer is empty */
    if (!net->remain_in_buf)
    {
      buffer_length= 0;
    }
    else
    {
      /* save position and restore \0 character */
      buffer_length= net->buf_length;
      current= net->buf_length - net->remain_in_buf;
      start= current;
      net->buff[net->buf_length - net->remain_in_buf]=net->save_char;
    }
    for (;;)
    {
      if (buffer_length - current >= 4)
      {
        uchar *pos= net->buff + current;
        packet_length= uint3korr(pos);

        /* check if we have last package (special case: zero length) */
        if (!packet_length)
        {
          current+= 4; /* length + sequence_id,
                          no more data will follow */
          break;
        }
        if (packet_length + 4 <= buffer_length - current)
        {
          if (!is_multi_packet)
          {
            current= current + packet_length + 4;
          }
          else
          {
            /* remove packet_header */
            memmove(net->buff + current, 
                net->buff + current + 4,
                buffer_length - current);
            buffer_length-= 4;
            current+= packet_length;
          }
          /* do we have last packet ? */
          if (packet_length != MAX_PACKET_LENGTH)
          {
            is_multi_packet= 0;
            break;
          }
          else
            is_multi_packet= 1;
          if (start)
          {
            memmove(net->buff, net->buff + start,
                buffer_length - start);
            /* decrease buflen*/
            buffer_length-= start;
            start= 0;
          }
          continue;
        }
      }
      if (start)
      { 
        memmove(net->buff, net->buff + start, buffer_length - start);
        /* decrease buflen and current */
        current -= start;
        buffer_length-= start;
        start= 0;
      }

      net->where_b=(unsigned long)buffer_length;

      if ((packet_length = ma_real_read(net,(size_t *)&complen)) == packet_error)
        return packet_error;
      if (net->use_ob20protocol)
      {
        /*
        OceanBase 2.0 Protocol Format：

        0       1         2           3          4        Byte
        +-----------------+----------------------+
        |    Magic Num    |       Version        |
        +-----------------+----------------------+
        |            Connection Id               |
        +-----------------------------+----------+
        |         Request Id          |   Seq    |
        +-----------------------------+----------+
        |            PayLoad Length              |
        +----------------------------------------+
        |                Flag                    |
        +-----------------+----------------------+
        |    Reserved     |Header Checksum(CRC16)|
        +-----------------+----------------------+
        |        ... PayLoad  Data ...           |----------+
        +----------------------------------------+          |
        |    Tailer PayLoad Checksum (CRC32)     |          |
        +----------------------------------------+          |
                                                            |
                                                            |
                                  +--------------------------+
                                  |
                                  |
                                  v
        +-------------------+-------------------+-------------------------------------+
        | Extra Len(4Byte)  |  Extra Info(K/V)  |  Basic Info(Standard MySQL Packet)  |
        +-------------------+-------------------+-------------------------------------+
        */

        unsigned char *ob20_current = 0; 
        unsigned char *ob20_start = 0;
        size_t ob20_extra_info_size = 0;
        uint32_t crc32 = 0;
        uint32_t packet_crc32;
        ob20_current = net->buff + net->where_b;
        ob20_start = ob20_current;

        if (decode_protocol20_header(net->ob20protocol, ob20_current, packet_length, net->pkt_nr - 1, 0)) {
          net->error=2;			/* caller will close socket */
          // 这里考虑定义一个新的错误码
          net->last_errno=ER_NET_READ_ERROR;
          return packet_error;
        }

        ob20_current += OB20_HEADER_SIZE;

        // first check checksum
        packet_crc32 = uint4korr(&ob20_current[net->ob20protocol->header.payload_len]);
        if (packet_crc32) {
          crc32 = ob_crc32(crc32, (char *)ob20_current, net->ob20protocol->header.payload_len);
          if (crc32 != packet_crc32) {
            net->error=2;			/* caller will close socket */
            net->last_errno=ER_NET_READ_ERROR;
            return packet_error;
          }
        } else {
          // 如果server回包的crc32是0，当前不检查crc，后续加上开关
        }
        // clear last extra info
        clear_extra_info(net->ob20protocol);

        // for extra info
        if (1 == net->ob20protocol->header.flag.st_flags.OB_EXTRA_INFO_EXIST){
          // get extra info size
          ob20_extra_info_size = uint4korr(ob20_current);
#ifdef DEBUG_OB20
          printf("get extra info and ob20_extra_info size is %lu\n", ob20_extra_info_size);
#endif
          // decode extra info
          if (1 == net->ob20protocol->header.flag.st_flags.OB_IS_NEW_EXTRA_INFO
            && decode_protocol20_extra_info(net->ob20protocol, ob20_current)) {
          }
          ob20_extra_info_size += OB20_EXTRAINFO_LENGTH_SIZE;
          ob20_current += ob20_extra_info_size;
        }

        // change buffer to raw mysql packet, same as compress protocol
        memmove(ob20_start, ob20_current, net->ob20protocol->header.payload_len - ob20_extra_info_size);
        // update complen , length of raw mysql packet length
        complen = net->ob20protocol->header.payload_len - ob20_extra_info_size;
      } 
      else if (_mariadb_uncompress((unsigned char*) net->buff + net->where_b, &packet_length, &complen))
      {
        net->error=2;			/* caller will close socket */
        net->last_errno=ER_NET_UNCOMPRESS_ERROR;
        break;
        return packet_error;
      }
      buffer_length+= complen;
    }
    /* set values */
    net->buf_length= (unsigned long)buffer_length;
    net->remain_in_buf= (unsigned long)(buffer_length - current);
    net->read_pos= net->buff + start + 4;
    len= current - start - 4;
    if (is_multi_packet)
      len-= 4;
    net->save_char= net->read_pos[len];	/* Must be saved */
    net->read_pos[len]=0;		/* Safeguard for mysql_use_result */
  }
#endif
  return (ulong)len;
}

int net_add_multi_command(NET *net, uchar command, const uchar *packet,
    size_t length)
{
  if (net->extension->multi_status == COM_MULTI_OFF)
  {
    return(1);
  }
  /* don't increase packet number */
  net->compress_pkt_nr= net->pkt_nr= 0;
  return ma_net_write_command(net, command, (const char *)packet, length, 1);
}

