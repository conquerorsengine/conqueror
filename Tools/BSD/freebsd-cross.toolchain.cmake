# FreeBSD'ye Linux'tan çapraz derleme (clang + FreeBSD sistem kökü). FreeBSD'nin kendisinde bu dosyaya gerek yok:
#   pkg install cmake ninja pkgconf xorgproto libX11 libXrandr libXinerama libXcursor libXi libxkbcommon wayland gtk3
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
#
# Sistem kökü: FreeBSD sürümünün base.txz dosyasından
#   mkdir -p sysroot && tar -xf base.txz -C sysroot ./usr/include ./usr/lib ./lib
# X11 başlıkları (GLFW X11 arka ucu) sistem kökünde /usr/local/include altında olmalı (xorgproto + libX11 paketleri).
#   cmake -S . -B build-freebsd -G Ninja -DCMAKE_TOOLCHAIN_FILE=Tools/BSD/freebsd-cross.toolchain.cmake \
#         -DCQ_FREEBSD_SYSROOT=/path/to/sysroot [-DCQ_FREEBSD_LLD=/path/to/ld.lld]
set(CMAKE_SYSTEM_NAME FreeBSD)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_SYSTEM_VERSION 14.5)

if(NOT CQ_FREEBSD_SYSROOT AND DEFINED ENV{CQ_FREEBSD_SYSROOT})
    set(CQ_FREEBSD_SYSROOT "$ENV{CQ_FREEBSD_SYSROOT}")
endif()
if(NOT CQ_FREEBSD_SYSROOT)
    message(FATAL_ERROR "Set CQ_FREEBSD_SYSROOT to an extracted FreeBSD base.txz")
endif()
# try_compile alt projeleri de aynı değerleri görsün
set(ENV{CQ_FREEBSD_SYSROOT} "${CQ_FREEBSD_SYSROOT}")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CQ_FREEBSD_SYSROOT CQ_FREEBSD_LLD)

set(CMAKE_SYSROOT "${CQ_FREEBSD_SYSROOT}")
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET x86_64-unknown-freebsd14.5)
set(CMAKE_CXX_COMPILER_TARGET x86_64-unknown-freebsd14.5)

# FreeBSD ELF bağlayıcısı: lld (ana makinedeki GNU ld FreeBSD hedefini bilmez)
if(NOT CQ_FREEBSD_LLD AND DEFINED ENV{CQ_FREEBSD_LLD})
    set(CQ_FREEBSD_LLD "$ENV{CQ_FREEBSD_LLD}")
endif()
if(NOT CQ_FREEBSD_LLD)
    find_program(CQ_FREEBSD_LLD NAMES ld.lld lld)
endif()
if(CQ_FREEBSD_LLD)
    set(ENV{CQ_FREEBSD_LLD} "${CQ_FREEBSD_LLD}")
    set(CMAKE_EXE_LINKER_FLAGS_INIT "--ld-path=${CQ_FREEBSD_LLD}")
    set(CMAKE_SHARED_LINKER_FLAGS_INIT "--ld-path=${CQ_FREEBSD_LLD}")
    set(CMAKE_MODULE_LINKER_FLAGS_INIT "--ld-path=${CQ_FREEBSD_LLD}")
endif()

set(CMAKE_FIND_ROOT_PATH "${CQ_FREEBSD_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
