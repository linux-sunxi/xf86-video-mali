#!/bin/sh
CROSS_COMPILE="arm-none-linux-gnueabi"
ROOT_FS="/scratch/andped01/image3/usr/local"
MALI_DDK="/scratch/andped01/svn/x11_trunk/trunk"

make distclean

PKG_CONFIG_PATH="$ROOT_FS/lib/pkgconfig" \
CC=$CROSS_COMPILE-gcc LD=$CROSS_COMPILE-ld CXX=$CROSS_COMPILE-g++ AR=$CROSS_COMPILE-ar RANLIB=$CROSS_COMPILE-ranlib \
CFLAGS="-O3 -Wall -W -Wextra -I$ROOT_FS/include/libdrm -I$ROOT_FS/include -I$ROOT_FS/include/xorg -I$MALI_DDK/include -I$MALI_DDK/internal/include/khronos -I$MALI_DDK/src/ump/include" \
LDFLAGS="-L$ROOT_FS/lib -lMali -lUMP -lpthread" \
./configure --prefix=$ROOT_FS --host=$CROSS_COMPILE --x-includes=$ROOT_FS/include --x-libraries=$ROOT_FS/lib

make
