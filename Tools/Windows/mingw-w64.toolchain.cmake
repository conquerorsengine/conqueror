# Windows (x64) oyuncusunu / editörünü Linux'tan MinGW-w64 ile çapraz derleme.
#   Fedora: sudo dnf install mingw64-gcc-c++ mingw64-winpthreads-static
#   Debian / Ubuntu: sudo apt install g++-mingw-w64-x86-64-posix
#   cmake -S . -B build-windows -G Ninja -DCMAKE_TOOLCHAIN_FILE=Tools/Windows/mingw-w64.toolchain.cmake -DCMAKE_BUILD_TYPE=Release
# C++ çalışma zamanı statik bağlanır (CMakeLists: WIN32 AND NOT MSVC -> -static): oyun klasörüne libstdc++ DLL'i gerekmez.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CQ_MINGW_PREFIX "x86_64-w64-mingw32" CACHE STRING "MinGW-w64 tool prefix")
find_program(CMAKE_C_COMPILER NAMES ${CQ_MINGW_PREFIX}-gcc ${CQ_MINGW_PREFIX}-clang REQUIRED)
find_program(CMAKE_CXX_COMPILER NAMES ${CQ_MINGW_PREFIX}-g++ ${CQ_MINGW_PREFIX}-clang++ REQUIRED)
# windres yolda kesme işareti olunca bozulur: sarmalayıcı (Python 3) üzerinden
find_program(CQ_REAL_WINDRES NAMES ${CQ_MINGW_PREFIX}-windres)
set(ENV{CQ_WINDRES} "${CQ_REAL_WINDRES}")
set(CMAKE_RC_COMPILER "${CMAKE_CURRENT_LIST_DIR}/windres-wrapper.py")

# Fedora: /usr/x86_64-w64-mingw32/sys-root/mingw, Debian: /usr/x86_64-w64-mingw32
foreach(_root /usr/${CQ_MINGW_PREFIX}/sys-root/mingw /usr/${CQ_MINGW_PREFIX})
    if(EXISTS ${_root}/include)
        list(APPEND CMAKE_FIND_ROOT_PATH ${_root})
    endif()
endforeach()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
