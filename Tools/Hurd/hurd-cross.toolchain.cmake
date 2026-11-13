# GNU/Hurd'a (Debian GNU/Hurd, hurd-amd64) Linux'tan çapraz derleme (clang + Debian paketlerinden sistem kökü). Hurd'ün kendisinde
# bu dosyaya gerek yok:
#   apt install cmake ninja-build g++ libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgtk-3-dev
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGLFW_BUILD_WAYLAND=OFF && cmake --build build
#
# Sistem kökü: debian-ports hurd-amd64 paketleri (libc0.3-dev, hurd-dev, gnumach-dev, libstdc++-15-dev, libgcc-15-dev, libx11-dev ...)
# her .deb için: ar x paket.deb && tar -xf data.tar.* -C sysroot
#   cmake -S . -B build-hurd -G Ninja -DCMAKE_TOOLCHAIN_FILE=Tools/Hurd/hurd-cross.toolchain.cmake \
#         -DCQ_HURD_SYSROOT=/path/to/sysroot [-DCQ_HURD_LLD=/path/to/ld.lld] -DCQ_RUNTIME_ONLY=ON -DGLFW_BUILD_WAYLAND=OFF
set(CMAKE_SYSTEM_NAME GNU)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if(NOT CQ_HURD_SYSROOT AND DEFINED ENV{CQ_HURD_SYSROOT})
    set(CQ_HURD_SYSROOT "$ENV{CQ_HURD_SYSROOT}")
endif()
if(NOT CQ_HURD_SYSROOT)
    message(FATAL_ERROR "Set CQ_HURD_SYSROOT to a sysroot extracted from Debian hurd-amd64 packages")
endif()
set(ENV{CQ_HURD_SYSROOT} "${CQ_HURD_SYSROOT}")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CQ_HURD_SYSROOT CQ_HURD_LLD)

set(CMAKE_SYSROOT "${CQ_HURD_SYSROOT}")
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET x86_64-unknown-hurd-gnu)
set(CMAKE_CXX_COMPILER_TARGET x86_64-unknown-hurd-gnu)
set(CMAKE_LIBRARY_ARCHITECTURE x86_64-gnu)

if(NOT CQ_HURD_LLD AND DEFINED ENV{CQ_HURD_LLD})
    set(CQ_HURD_LLD "$ENV{CQ_HURD_LLD}")
endif()
if(NOT CQ_HURD_LLD)
    find_program(CQ_HURD_LLD NAMES ld.lld lld)
endif()
if(CQ_HURD_LLD)
    set(ENV{CQ_HURD_LLD} "${CQ_HURD_LLD}")
    set(CMAKE_EXE_LINKER_FLAGS_INIT "--ld-path=${CQ_HURD_LLD}")
    set(CMAKE_SHARED_LINKER_FLAGS_INIT "--ld-path=${CQ_HURD_LLD}")
    set(CMAKE_MODULE_LINKER_FLAGS_INIT "--ld-path=${CQ_HURD_LLD}")
endif()

set(CMAKE_FIND_ROOT_PATH "${CQ_HURD_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
