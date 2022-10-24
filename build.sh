#!/bin/bash
function clean()
{
  rm -rf CMakeFiles CMakeCache.txt
}
clean

TOP_DIR=$(cd "$(dirname "$0")";pwd)
DEP_DIR=${TOP_DIR}/deps/3rd/usr/local/oceanbase/deps/devel

cmake . \
-DCMAKE_INSTALL_PREFIX=/app/mariadb \
-DWITH_SSL=$DEP_DIR \
-DENABLED_LOCAL_INFILE=1 \
-DDEFAULT_CHARSET=utf8

make -j `cat /proc/cpuinfo | grep processor| wc -l`