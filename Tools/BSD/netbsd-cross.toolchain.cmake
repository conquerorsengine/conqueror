# NetBSD'ye Linux'tan çapraz derleme (clang + NetBSD sistem kökü). NetBSD'nin kendisinde bu dosyaya gerek yok:
#   pkgin install cmake ninja-build gtk3+ python312   (X11 temel sistemde: /usr/X11R7)
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
#
# Sistem kökü: NetBSD sürümünün setlerinden (comp: başlıklar ve libstdc++, base: paylaşılan kütüphaneler, xbase + xcomp: X11)
#   mkdir -p sysroot && for s in base comp xbase xcomp; do tar -xJf $s.tar.xz -C sysroot ./usr/include ./usr/lib ./lib ./usr/X11R7; done
#   cmake -S . -B build-netbsd -G Ninja -DCMAKE_TOOLCHAIN_FILE=Tools/BSD/netbsd-cross.toolchain.cmake \
#         -DCQ_NETBSD_SYSROOT=/path/to/sysroot [-DCQ_BSD_LLD=/path/to/ld.lld] -DCQ_RUNTIME_ONLY=ON
set(CMAKE_SYSTEM_NAME NetBSD)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_SYSTEM_VERSION 10.1)

if(NOT CQ_NETBSD_SYSROOT AND DEFINED ENV{CQ_NETBSD_SYSROOT})
    set(CQ_NETBSD_SYSROOT "$ENV{CQ_NETBSD_SYSROOT}")
endif()
if(NOT CQ_NETBSD_SYSROOT)
    message(FATAL_ERROR "Set CQ_NETBSD_SYSROOT to extracted NetBSD sets")
endif()
set(ENV{CQ_NETBSD_SYSROOT} "${CQ_NETBSD_SYSROOT}")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CQ_NETBSD_SYSROOT CQ_BSD_LLD)

set(CMAKE_SYSROOT "${CQ_NETBSD_SYSROOT}")
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET x86_64-unknown-netbsd10.1)
set(CMAKE_CXX_COMPILER_TARGET x86_64-unknown-netbsd10.1)
# NetBSD'nin C++ kütüphanesi libstdc++ (clang'in varsayılanı libc++)
set(CMAKE_CXX_FLAGS_INIT "-stdlib=libstdc++")
# clang'ın NetBSD sürücüsü C++ istisna çözücüsünü (_Unwind_*) bağlamıyor: NetBSD'de libgcc_s içinde
set(CMAKE_CXX_STANDARD_LIBRARIES "-lgcc_s")

# Bağlayıcı: lld
if(NOT CQ_BSD_LLD AND DEFINED ENV{CQ_BSD_LLD})
    set(CQ_BSD_LLD "$ENV{CQ_BSD_LLD}")
endif()
if(NOT CQ_BSD_LLD)
    find_program(CQ_BSD_LLD NAMES ld.lld lld)
endif()
if(CQ_BSD_LLD)
    set(ENV{CQ_BSD_LLD} "${CQ_BSD_LLD}")
    set(CMAKE_EXE_LINKER_FLAGS_INIT "--ld-path=${CQ_BSD_LLD} -L${CQ_NETBSD_SYSROOT}/usr/X11R7/lib")
    set(CMAKE_SHARED_LINKER_FLAGS_INIT "--ld-path=${CQ_BSD_LLD} -L${CQ_NETBSD_SYSROOT}/usr/X11R7/lib")
    set(CMAKE_MODULE_LINKER_FLAGS_INIT "--ld-path=${CQ_BSD_LLD} -L${CQ_NETBSD_SYSROOT}/usr/X11R7/lib")
endif()

# X11 /usr/X11R7 altında
set(CMAKE_FIND_ROOT_PATH "${CQ_NETBSD_SYSROOT}" "${CQ_NETBSD_SYSROOT}/usr/X11R7")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
