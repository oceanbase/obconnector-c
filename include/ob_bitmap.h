/************************************************************************************
   Copyright (c) 2021 OceanBase.
   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with this library; if not see <http://www.gnu.org/licenses>
   or write to the Free Software Foundation, Inc.,
   51 Franklin St., Fifth Floor, Boston, MA 02110, USA

   Part of this code includes code from the PHP project which
   is freely available from http://www.php.net
*************************************************************************************/

#ifndef _ob_bitmap_h_
#define _ob_bitmap_h_

#include <ma_global.h>

typedef uint32 ob_bitmap_map;

typedef struct st_bitmap
{
  ob_bitmap_map *bitmap;
  uint n_bits;
} OB_BITMAP;

#ifdef  __cplusplus
extern "C" {
#endif
my_bool ob_bitmap_init(OB_BITMAP *map, uint n_bits);
void ob_bitmap_free(OB_BITMAP *map);
void ob_bitmap_set_bit(OB_BITMAP *map, uint bit);
void ob_bitmap_clear_bit(OB_BITMAP *map, uint bit);
my_bool ob_bitmap_is_set(const OB_BITMAP *map, uint bit);
void ob_bitmap_clear_all(OB_BITMAP *map);
void ob_bitmap_set_all(OB_BITMAP *map);

#ifdef  __cplusplus
}
#endif

#endif /* _ob_bitmap_h_ */
