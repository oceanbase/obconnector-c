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

#include "ob_bitmap.h"
#include "string.h"

#define n_bytes_in_map(map) (((map)->n_bits + 7)/8)

my_bool ob_bitmap_init(OB_BITMAP *map, uint n_bits)
{
  uint size_in_bytes;

  map->n_bits = n_bits;
  size_in_bytes = n_bytes_in_map(map);

  if (!(map->bitmap = (ob_bitmap_map *)malloc(size_in_bytes))) {
    return (1);
  }

  ob_bitmap_clear_all(map);
  return (0);
}

void ob_bitmap_set_bit(OB_BITMAP *map, uint bit)
{
  if (bit < map->n_bits) {
    ((uchar*)map->bitmap)[bit / 8] |= (1 << (bit & 7));
  }
}

void ob_bitmap_clear_bit(OB_BITMAP *map, uint bit)
{
  if (bit < map->n_bits) {
    ((uchar*)map->bitmap)[bit / 8] &= ~(1 << (bit & 7));
  }
}

my_bool ob_bitmap_is_set(const OB_BITMAP *map, uint bit)
{
  if (bit < map->n_bits) {
    return ((uchar*)map->bitmap)[bit / 8] & (1 << (bit & 7));
  } else {
    return 0;
  }
}

void ob_bitmap_clear_all(OB_BITMAP *map)
{
  memset(map->bitmap, 0, n_bytes_in_map(map));
}

void ob_bitmap_set_all(OB_BITMAP *map)
{
  memset(map->bitmap, 0xFF, n_bytes_in_map(map));
}

void ob_bitmap_free(OB_BITMAP *map)
{
  free(map->bitmap);
  map->bitmap = NULL;
  map->n_bits = 0;
}