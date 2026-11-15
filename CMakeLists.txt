cmake_minimum_required(VERSION 3.20)
project(ConquerorEngine VERSION 0.571.3 LANGUAGES C CXX)
# macOS: Metal arka ucu Objective-C++ (.mm). Dil en üst düzeyde açılmalı: yalnız Engine/ içinde açılınca Ninja
# üreticisi "CMAKE_OBJCXX_COMPILE_OBJECT not set" hatasıyla duruyordu (CI macOS işi)
if(APPLE)
    enable_language(OBJC OBJCXX)
endif()
# Windows: çalıştırılabilir simgesi (Resources/Logo/AppIcon/Conqueror.rc) için kaynak derleyici (MSVC rc / MinGW windres)
if(WIN32)
    enable_language(RC)
    set(CQ_APP_ICON_RC "${CMAKE_SOURCE_DIR}/Resources/Logo/AppIcon/Conqueror.rc")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# C++ modülleri kullanılmıyor: Ninja + GCC'de modül taraması (P1689) gereksiz ve yolda kesme işareti varken
# ("Conqueror's_Engine") GCC'nin modül eşleyicisi yolu ayrıştıramayıp derlemeyi durduruyordu
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_ENABLE_EXPORTS ON)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# VS çok-config üreteçte de çıktılar Linux ile aynı düzende (build/bin, build/lib) olsun
foreach(_cfg RELEASE DEBUG RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_cfg} ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${_cfg} ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${_cfg} ${CMAKE_BINARY_DIR}/lib)
endforeach()

# Global compiler flags
if(MSVC)
    # /utf-8: kaynaklar UTF-8 (Türkçe yorumlar, simge karakterleri); /bigobj: büyük çeviri birimleri (EditorLayer, SceneSerializer)
    add_compile_options(/W4 /WX- /MP /utf-8 /bigobj)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    # Dağıtılan oyunlar aynı derlemeden gelir: -march=native başka CPU'larda "illegal instruction" verir.
    # Varsayılan taşınabilir taban (x86-64-v2: SSE4.2/POPCNT, 2009+ tüm CPU'lar); yerel ayar isteğe bağlı.
    option(CQ_NATIVE_ARCH "Optimize for this machine's CPU only (-march=native)" OFF)
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(CQ_NATIVE_ARCH)
        add_compile_options(-march=native)
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
        add_compile_options(-march=x86-64-v2)
    endif()
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0)
    elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -DNDEBUG)
    endif()
endif()

# MinGW (GCC/Clang) Windows derlemesi: C++ çalışma zamanı statik bağlanır (oyun klasörüne libc++/libstdc++ DLL'i gerekmez)
if(WIN32 AND NOT MSVC)
    add_link_options(-static)
endif()

# Web (Emscripten, WebAssembly): yalnız oyuncu. Motor iş parçacıkları (iş havuzu, Jolt, doku akışı) Web Worker'larda çalışır
# (SharedArrayBuffer: sayfa COOP / COEP başlıklarıyla sunulur, Build çıktısındaki servis çalışanı bunu GitHub Pages / itch.io'da da
# sağlar); C++ istisnaları WebAssembly istisnalarıyla (tüm güncel tarayıcılar)
if(EMSCRIPTEN)
    set(CQ_RUNTIME_ONLY ON CACHE BOOL "" FORCE)
    # Player Settings > Web > Threads: kapalıyken (varsayılan, her sunucuda açılır) motor iş parçacığı açmaz, işler ana
    # iş parçacığında sırayla yürür
    option(CQ_WEB_THREADS "Web player with threads (SharedArrayBuffer, needs COOP / COEP)" OFF)
    add_compile_options(-fwasm-exceptions)
    add_link_options(-fwasm-exceptions)
    if(CQ_WEB_THREADS)
        add_compile_options(-pthread)
        add_link_options(-pthread)
        add_compile_definitions(CQ_WEB_THREADS=1)
    endif()
endif()

# HarmonyOS (OpenHarmony NDK araç zinciri): araç zinciri dosyası her derlemeye --gcc-toolchain verir, clang onu kullanılmayan
# argüman sayar; -Werror ile derlenen üçüncü taraf kodu (Jolt) bu uyarıda durmasın
if(CMAKE_SYSTEM_NAME STREQUAL "OHOS")
    add_compile_options(-Wno-unused-command-line-argument)
endif()

# Debug/Release configurations
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(CQ_DEBUG)
elseif(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_definitions(CQ_RELEASE)
endif()

# Bellek hatası avı: cmake -DCQ_ENABLE_ASAN=ON (yalnızca motor/editör kodu enstrümante edilir)
option(CQ_ENABLE_ASAN "Build engine/editor code with AddressSanitizer + UBSan" OFF)
function(cq_apply_sanitizers target)
    if(CQ_ENABLE_ASAN AND NOT MSVC)
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize=vptr)
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()

# External dependencies
add_subdirectory(External)

# Engine Core
add_subdirectory(Engine)

# Yalnızca motor + oyuncu: Android (NDK araç zinciri; libconqueror.so, NativeActivity) ve gömülü / otomotiv Linux
# görüntüleri (AGL, Yocto: -DCQ_RUNTIME_ONLY=ON). Editör, başlatıcı ve komut satırı araçları masaüstü uygulamalarıdır.
# APK paketleme: Tools/Android/package_apk.py, AGL katmanı: Tools/AGL/meta-conqueror
option(CQ_RUNTIME_ONLY "Build only the engine and the game player (embedded / automotive images)" OFF)
if(ANDROID OR CQ_RUNTIME_ONLY OR CMAKE_SYSTEM_NAME STREQUAL "QNX")
    add_subdirectory(Runtime)
    return()
endif()

# HarmonyOS PC / tablet (OpenHarmony native SDK araç zinciri, CMAKE_SYSTEM_NAME=OHOS): editör ve oyuncu, HAP'in yüklediği
# paylaşımlı kitaplıklardır (lib/<OHOS_ARCH>/libConquerorEditor.so, libconqueror.so); ArkTS tarafı XComponent yüzeyini verir.
# Başlatıcı (hub) ve komut satırı araçları ayrı süreç olarak çalışamaz (HAP uygulaması tek süreç). HAP şablonu ve paketleyici:
# Tools/HarmonyOS
if(CMAKE_SYSTEM_NAME STREQUAL "OHOS")
    add_subdirectory(Editor)
    add_subdirectory(Runtime)
    add_subdirectory(Main)
    return()
endif()

# Editor
add_subdirectory(Editor)

# Launcher
add_subdirectory(Launcher)

# Runtime
add_subdirectory(Runtime)

# Main executable
add_subdirectory(Main)

# Komut satırı araçları (cqs: ConquerorScript)
add_subdirectory(Tools/cqs)
