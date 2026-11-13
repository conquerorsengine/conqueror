# ARM64 Linux'a (aarch64-linux-gnu: Raspberry Pi 4 / 5, ARM dizüstüleri, Jetson ...) x86_64 Linux'tan çapraz derleme
# (clang + lld + Debian arm64 paketlerinden sistem kökü). ARM64 makinenin kendisinde bu dosyaya gerek yok:
#   apt install cmake ninja-build g++ libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgtk-3-dev
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
#
#   Tools/Linux/make-arm64-sysroot.sh ~/arm64-sysroot          (Raspberry Pi OS bookworm: CQ_ARM64_SUITE=bookworm)
#   cmake -S . -B build-arm64 -G Ninja -DCMAKE_TOOLCHAIN_FILE=Tools/Linux/arm64-cross.toolchain.cmake \
#         -DCQ_ARM64_SYSROOT=~/arm64-sysroot [-DCQ_ARM64_LLD=/path/to/ld.lld] -DCQ_RUNTIME_ONLY=ON -DGLFW_BUILD_WAYLAND=OFF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT CQ_ARM64_SYSROOT AND DEFINED ENV{CQ_ARM64_SYSROOT})
    set(CQ_ARM64_SYSROOT "$ENV{CQ_ARM64_SYSROOT}")
endif()
if(NOT CQ_ARM64_SYSROOT)
    message(FATAL_ERROR "Set CQ_ARM64_SYSROOT to a sysroot made with Tools/Linux/make-arm64-sysroot.sh")
endif()
set(ENV{CQ_ARM64_SYSROOT} "${CQ_ARM64_SYSROOT}")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CQ_ARM64_SYSROOT CQ_ARM64_LLD)

set(CMAKE_SYSROOT "${CQ_ARM64_SYSROOT}")
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_LIBRARY_ARCHITECTURE aarch64-linux-gnu)

if(NOT CQ_ARM64_LLD AND DEFINED ENV{CQ_ARM64_LLD})
    set(CQ_ARM64_LLD "$ENV{CQ_ARM64_LLD}")
endif()
if(NOT CQ_ARM64_LLD)
    find_program(CQ_ARM64_LLD NAMES ld.lld lld)
endif()
if(CQ_ARM64_LLD)
    set(ENV{CQ_ARM64_LLD} "${CQ_ARM64_LLD}")
    set(CMAKE_EXE_LINKER_FLAGS_INIT "--ld-path=${CQ_ARM64_LLD}")
    set(CMAKE_SHARED_LINKER_FLAGS_INIT "--ld-path=${CQ_ARM64_LLD}")
    set(CMAKE_MODULE_LINKER_FLAGS_INIT "--ld-path=${CQ_ARM64_LLD}")
endif()

set(CMAKE_FIND_ROOT_PATH "${CQ_ARM64_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
