#!/bin/bash

# Detect platform
OS_KERNEL="$(uname -s)"

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

#-DCMAKE_BUILD_TYPE=DEBUG \
#-DCMAKE_C_FLAGS_DEBUG="-g -O0" \
#-DCMAKE_CXX_FLAGS_DEBUG="-g -O0" \

# Parallel build: detect CPU count per platform
if [[ "$OS_KERNEL" == "Darwin" ]]; then
  make -j $(sysctl -n hw.ncpu)
else
  make -j $(cat /proc/cpuinfo | grep processor | wc -l)
fi
