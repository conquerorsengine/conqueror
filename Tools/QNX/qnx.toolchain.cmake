# QNX SDP 7.1 / 8.0 araç zinciri (qcc / q++). Önce SDP ortamını yükleyin:
#   source ~/qnx800/qnxsdp-env.sh
#   cmake -S . -B build-qnx -G Ninja -DCMAKE_TOOLCHAIN_FILE=Tools/QNX/qnx.toolchain.cmake -DCQ_QNX_ARCH=aarch64le \
#         -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-qnx --target ConquerorRuntime
# Yalnızca oyuncu derlenir (editör masaüstü uygulamasıdır). Grafik: OpenGL ES 3.x (Screen + EGL), 3B fizik (Jolt)
# QNX'i tanımadığı için kapalı (CQ_ENABLE_JOLT), ses: miniaudio'nun QNX arka ucu yok (sessiz "null" aygıt).
set(CMAKE_SYSTEM_NAME QNX)

set(CQ_QNX_ARCH "aarch64le" CACHE STRING "QNX target architecture: aarch64le or x86_64")
if(CQ_QNX_ARCH STREQUAL "x86_64")
    set(CMAKE_SYSTEM_PROCESSOR x86_64)
else()
    set(CMAKE_SYSTEM_PROCESSOR aarch64)
endif()

if(NOT DEFINED ENV{QNX_HOST} OR NOT DEFINED ENV{QNX_TARGET})
    message(FATAL_ERROR "QNX_HOST / QNX_TARGET are not set: source qnxsdp-env.sh from your QNX SDP first")
endif()
set(QNX_HOST "$ENV{QNX_HOST}")
set(QNX_TARGET "$ENV{QNX_TARGET}")

set(CMAKE_C_COMPILER "${QNX_HOST}/usr/bin/qcc")
set(CMAKE_CXX_COMPILER "${QNX_HOST}/usr/bin/q++")
set(CMAKE_C_COMPILER_TARGET "gcc_nto${CQ_QNX_ARCH}")
set(CMAKE_CXX_COMPILER_TARGET "gcc_nto${CQ_QNX_ARCH}_cxx")

set(CMAKE_FIND_ROOT_PATH "${QNX_TARGET}" "${QNX_TARGET}/${CQ_QNX_ARCH}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CQ_ENABLE_VULKAN OFF CACHE BOOL "" FORCE)
set(CQ_ENABLE_JOLT OFF CACHE BOOL "" FORCE)
set(CQ_DEFAULT_GL_PROFILE "es3.2" CACHE STRING "" FORCE)
