# OpenBSD'ye Linux'tan çapraz derleme (clang + OpenBSD sistem kökü). OpenBSD'nin kendisinde bu dosyaya gerek yok:
#   pkg_add cmake ninja gtk+3 python%3   (X11 temel sistemde: /usr/X11R6)
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
#
# Sistem kökü: OpenBSD sürümünün setlerinden (comp: başlıklar ve libc++, base: paylaşılan kütüphaneler, xbase + xshare: X11)
#   mkdir -p sysroot && for s in comp78 base78 xbase78 xshare78; do tar -xzf $s.tgz -C sysroot ./usr/include ./usr/lib ./usr/X11R6; done
# OpenBSD kütüphaneleri sürüm numaralı (libc++.so.11.0) ve "lib*.so" bağı yok: çapraz bağlayıcı paylaşımlı olanı bulsun diye
#   for d in sysroot/usr/lib sysroot/usr/X11R6/lib; do (cd $d && for f in lib*.so.[0-9]*; do b=${f%%.so.*}; [ -e $b.so ] ||
#     ln -s $(ls -1 $b.so.[0-9]* | sort -V | tail -1) $b.so; done); done
#   cmake -S . -B build-openbsd -G Ninja -DCMAKE_TOOLCHAIN_FILE=Tools/BSD/openbsd-cross.toolchain.cmake \
#         -DCQ_OPENBSD_SYSROOT=/path/to/sysroot [-DCQ_BSD_LLD=/path/to/ld.lld] -DCQ_RUNTIME_ONLY=ON
set(CMAKE_SYSTEM_NAME OpenBSD)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_SYSTEM_VERSION 7.8)

if(NOT CQ_OPENBSD_SYSROOT AND DEFINED ENV{CQ_OPENBSD_SYSROOT})
    set(CQ_OPENBSD_SYSROOT "$ENV{CQ_OPENBSD_SYSROOT}")
endif()
if(NOT CQ_OPENBSD_SYSROOT)
    message(FATAL_ERROR "Set CQ_OPENBSD_SYSROOT to extracted OpenBSD sets")
endif()
set(ENV{CQ_OPENBSD_SYSROOT} "${CQ_OPENBSD_SYSROOT}")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CQ_OPENBSD_SYSROOT CQ_BSD_LLD)

set(CMAKE_SYSROOT "${CQ_OPENBSD_SYSROOT}")
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET x86_64-unknown-openbsd7.8)
set(CMAKE_CXX_COMPILER_TARGET x86_64-unknown-openbsd7.8)

# Bağlayıcı: lld (OpenBSD'nin kendi bağlayıcısı da lld)
if(NOT CQ_BSD_LLD AND DEFINED ENV{CQ_BSD_LLD})
    set(CQ_BSD_LLD "$ENV{CQ_BSD_LLD}")
endif()
if(NOT CQ_BSD_LLD)
    find_program(CQ_BSD_LLD NAMES ld.lld lld)
endif()
if(CQ_BSD_LLD)
    set(ENV{CQ_BSD_LLD} "${CQ_BSD_LLD}")
    set(CMAKE_EXE_LINKER_FLAGS_INIT "--ld-path=${CQ_BSD_LLD} -L${CQ_OPENBSD_SYSROOT}/usr/X11R6/lib")
    set(CMAKE_SHARED_LINKER_FLAGS_INIT "--ld-path=${CQ_BSD_LLD} -L${CQ_OPENBSD_SYSROOT}/usr/X11R6/lib")
    set(CMAKE_MODULE_LINKER_FLAGS_INIT "--ld-path=${CQ_BSD_LLD} -L${CQ_OPENBSD_SYSROOT}/usr/X11R6/lib")
endif()

# X11 /usr/X11R6 altında
set(CMAKE_FIND_ROOT_PATH "${CQ_OPENBSD_SYSROOT}" "${CQ_OPENBSD_SYSROOT}/usr/X11R6")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
