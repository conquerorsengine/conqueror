# External Dependencies

# Tüm external library'leri -fPIC ile derle (shared library desteği için)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Üçüncü taraf kitaplıklar statik. (Masaüstünde ilk tanımlayan GLFW'nin OFF seçeneğiydi; GLFW'siz derlemelerde
# — Android — Assimp'in ON varsayılanı önbelleğe yazılıp spdlog/glslang/Jolt'u paylaşımlı yapıyordu.)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)

# GLFW
if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "QNX")
    # Android / QNX'te GLFW yok: motor aynı API'yi Engine/src/Platform/Android/GLFWAndroid.cpp (NativeActivity + EGL) ve
    # Engine/src/Platform/QNX/GLFWQnx.cpp (Screen + EGL) ile sağlar
    add_library(glfw INTERFACE)
    target_include_directories(glfw INTERFACE glfw/include)
else()
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    # Wayland başlıkları yoksa (OpenBSD / NetBSD temel sistemi, GNU/Hurd) GLFW yalnız X11 ile derlenir; kullanıcı açıkça
    # GLFW_BUILD_WAYLAND verdiyse ona dokunulmaz
    if(UNIX AND NOT APPLE AND NOT DEFINED GLFW_BUILD_WAYLAND)
        find_path(CQ_WAYLAND_CLIENT_INCLUDE wayland-client-core.h)
        if(NOT CQ_WAYLAND_CLIENT_INCLUDE)
            message(STATUS "Wayland headers not found: GLFW is built with X11 only")
            set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
        endif()
    endif()
    add_subdirectory(glfw)
    # GNU/Hurd glibc'si clock_gettime / CLOCK_MONOTONIC için özellik makrosu ister (Linux'ta GLFW kendisi tanımlar)
    if(CMAKE_SYSTEM_NAME STREQUAL "GNU")
        target_compile_definitions(glfw PRIVATE _GNU_SOURCE)
    endif()
endif()

# GLAD
add_library(glad STATIC
    glad/src/glad.c
)
target_include_directories(glad PUBLIC glad/include)

# GLM
add_subdirectory(glm)

# glslang (GLSL -> SPIR-V derleyici; shader önbelleği ve Vulkan/Metal/DX yolu)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_PCH OFF CACHE BOOL "" FORCE)
add_subdirectory(glslang)

# SPIRV-Cross (yansıtma + GLSL/HLSL/MSL'ye çapraz derleme)
set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SHARED OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_STATIC ON CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_UTIL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
add_subdirectory(SPIRV-Cross)

# spdlog
add_subdirectory(spdlog)

# ImGui
# Motor DLL olarak derlenirse Engine.dll + exe aynı ImGui context'ini (GImGui) paylaşmalı; o durumda imgui
# paylaşılan DLL olur. Varsayılan: motor Windows/macOS'ta statik (tek kopya), imgui de statik.
option(CQ_IMGUI_SHARED "Build ImGui/ImGuizmo as DLLs (only needed with a DLL engine on Windows)" OFF)
if(WIN32 AND CQ_IMGUI_SHARED)
    add_library(imgui SHARED
        imgui/imgui.cpp
        imgui/imgui_demo.cpp
        imgui/imgui_draw.cpp
        imgui/imgui_tables.cpp
        imgui/imgui_widgets.cpp
        imgui_automation/CQImGuiAutomation.cpp
    )
    # NOT: target_compile_definitions parantezli değeri VS vcxproj'da bölüyor;
    # /D bayrağı compile_options ile verbatim geçilir. MinGW (GCC/Clang) normal tanım kullanır.
    if(MSVC)
        target_compile_options(imgui PRIVATE "/DIMGUI_API=__declspec(dllexport)")
        target_compile_options(imgui INTERFACE "/DIMGUI_API=__declspec(dllimport)")
    else()
        target_compile_definitions(imgui PRIVATE "IMGUI_API=__declspec(dllexport)")
        target_compile_definitions(imgui INTERFACE "IMGUI_API=__declspec(dllimport)")
    endif()
else()
    add_library(imgui STATIC
        imgui/imgui.cpp
        imgui/imgui_demo.cpp
        imgui/imgui_draw.cpp
        imgui/imgui_tables.cpp
        imgui/imgui_widgets.cpp
        imgui_automation/CQImGuiAutomation.cpp
    )
endif()
target_include_directories(imgui PUBLIC imgui imgui_automation)
# UI otomasyon kancaları (Engine Automation sistemi widget'ları etiketle bulur/tıklar)
target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_TEST_ENGINE)

# ImNodes
add_library(imnodes STATIC
    imnodes/imnodes.cpp
    imnodes/imnodes_internal.h
    imnodes/imnodes.h
)
target_include_directories(imnodes PUBLIC imnodes)
target_link_libraries(imnodes PUBLIC imgui)

# ImGuiColorTextEdit
add_library(imguicolortextedit STATIC
    ImGuiColorTextEdit/TextEditor.cpp
)
target_include_directories(imguicolortextedit PUBLIC ImGuiColorTextEdit)
target_link_libraries(imguicolortextedit PUBLIC imgui)

# yaml-cpp
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
add_subdirectory(yaml-cpp)

# EnTT
add_subdirectory(entt)

# ImGuizmo
# ImGuizmo.h eskiden kendi API'si için IMGUI_API makrosunu yeniden kullanıyordu;
# imgui paylaşılan DLL olunca bu makro dllimport'a dönüşüp statik imguizmo
# linkini bozuyordu. ImGuizmo.h artık IMGUZMO_API kullanıyor, Windows'ta
# imguizmo da paylaşılan DLL yapılır (tek state, tek context).
if(WIN32 AND CQ_IMGUI_SHARED)
    add_library(imguizmo SHARED
        ImGuizmo/ImGuizmo.cpp
    )
    if(MSVC)
        target_compile_options(imguizmo PRIVATE "/DIMGUZMO_API=__declspec(dllexport)")
        target_compile_options(imguizmo INTERFACE "/DIMGUZMO_API=__declspec(dllimport)")
    else()
        target_compile_definitions(imguizmo PRIVATE "IMGUZMO_API=__declspec(dllexport)")
        target_compile_definitions(imguizmo INTERFACE "IMGUZMO_API=__declspec(dllimport)")
    endif()
else()
    add_library(imguizmo STATIC
        ImGuizmo/ImGuizmo.cpp
    )
endif()
target_include_directories(imguizmo PUBLIC ImGuizmo)
target_link_libraries(imguizmo PUBLIC imgui)

# STB
add_library(stb STATIC
    stb/stb_image_impl.cpp
)
target_include_directories(stb PUBLIC stb)

# FreeType
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
# FreeType FT_DISABLE_ZLIB ile kendi gömülü zlib'siz derlenir;
# önceki cache'den kalma ZLIB_* freetype'i bozmasın diye burada temizle
unset(ZLIB_FOUND CACHE)
unset(ZLIB_FOUND)
unset(ZLIB_INCLUDE_DIR CACHE)
unset(ZLIB_INCLUDE_DIRS CACHE)
unset(ZLIB_LIBRARY CACHE)
unset(ZLIB_LIBRARIES CACHE)
add_subdirectory(freetype)

# zlib — External'deki hali kullanılacak (Windows'ta sistem zlib yok, Assimp de bunu kullanır)
# FreeType FT_DISABLE_ZLIB ile derlendiği için ZLIB beklemez; Assimp için ZLIB_* burada verilir.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/zlib/CMakeLists.txt")
    set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_SHARED ON CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(zlib)
else()
    message(WARNING "zlib not found in External/zlib")
endif()

# Assimp (External/zlib'i kullanır)
set(ZLIB_FOUND TRUE)
set(ZLIB_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/zlib;${CMAKE_CURRENT_BINARY_DIR}/zlib")
set(ZLIB_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/zlib;${CMAKE_CURRENT_BINARY_DIR}/zlib")
set(ZLIB_LIBRARY zlibstatic)
set(ZLIB_LIBRARIES zlibstatic)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB OFF CACHE BOOL "" FORCE)
add_subdirectory(assimp)
# MinGW (msvcrt): assimp eski CRT sürümünde _fstat32'yi seçiyor, msvcrt.dll bunu vermiyor; _fstat64 yolunu seçtir
if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_definitions(assimp PRIVATE __MSVCRT_VERSION__=0x0601)
endif()

# NFD (Native File Dialog) — yalnızca editör/başlatıcı kullanır; oyuncuda dosya iletişim kutusu yok (Android, QNX ve
# yalnız oyuncu derlemelerinde GTK / X11 bağımlılığı gelmesin)
if(ANDROID OR CQ_RUNTIME_ONLY OR CMAKE_SYSTEM_NAME STREQUAL "QNX")
    add_library(nfd INTERFACE)
    target_include_directories(nfd INTERFACE nativefiledialog-extended/src/include)
else()
    set(NFD_PORTAL OFF CACHE BOOL "" FORCE)
    add_subdirectory(nativefiledialog-extended)
endif()

# Box2D (2D Physics)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/box2d/CMakeLists.txt)
    set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
    set(BOX2D_BUILD_TESTBED OFF CACHE BOOL "" FORCE)
    set(BOX2D_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    add_subdirectory(box2d)
else()
    message(WARNING "Box2D not found in External/box2d - 2D physics will not be available")
endif()

# JoltPhysics (3D Physics)
# QNX: Jolt platformu tanımıyor (Core.h "Unknown platform"); QNX'te 3B fizik varsayılan kapalı, 2B fizik (Box2D) açık
set(_cq_jolt_default ON)
if(CMAKE_SYSTEM_NAME STREQUAL "QNX")
    set(_cq_jolt_default OFF)
endif()
option(CQ_ENABLE_JOLT "Build JoltPhysics (3D physics backend)" ${_cq_jolt_default})
if(CQ_ENABLE_JOLT AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/JoltPhysics/Build/CMakeLists.txt)
    set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
    set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
    set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
    set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
    set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
    if(ANDROID)
        # Android (API < 29) öykünmeli TLS kullanır; Jolt'un ThinLTO nesneleri bağlamada yerel TLS üretip
        # motorun öykünmeli TLS başvurularıyla (ProfileThread::sInstance) çakışıyordu
        set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
    endif()
    # Projenin geri kalanı /MD (dinamik CRT) kullanır; Jolt varsayılan /MT ile LNK2038 verir
    set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
    # Jolt'un GPU hesaplama arka uçları (saç simülasyonu vb.) motorda kullanılmıyor: gölgelendirici derleyicisi
    # (glslc/dxc) gerektirmesin ve ikiliye girmesin
    foreach(_jolt_gpu JPH_USE_DX12 JPH_USE_VK JPH_USE_MTL JPH_USE_CPU_COMPUTE)
        set(${_jolt_gpu} OFF CACHE BOOL "" FORCE)
    endforeach()
    # Dağıtılabilir ikili: AVX/AVX2/FMA/F16C gerektirmesin (x86-64-v2 tabanı); yerel derlemede tam hız
    if(NOT CQ_NATIVE_ARCH)
        foreach(_jolt_opt USE_AVX USE_AVX2 USE_AVX512 USE_LZCNT USE_TZCNT USE_F16C USE_FMADD)
            set(${_jolt_opt} OFF CACHE BOOL "" FORCE)
        endforeach()
    endif()
    add_subdirectory(JoltPhysics/Build)
    # MinGW GCC: Jolt'un -Werror'u Windows API işlev işaretçisi dönüşümünü hata sayıyor
    if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(Jolt PRIVATE -Wno-cast-function-type)
    endif()
else()
    message(WARNING "JoltPhysics not found in External/JoltPhysics - 3D physics will not be available")
endif()

# zstd
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/zstd/build/cmake/CMakeLists.txt")
    set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(zstd/build/cmake)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/zstd/lib/zstd.h")
    # build/cmake altmodülü eksikse External/zstd/lib kaynaklarından minimal statik lib kur
    # (Windows'ta sistem zstd yok, kaldırmadan devam etmek için)
    file(GLOB ZSTD_COMMON_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/zstd/lib/common/*.c")
    file(GLOB ZSTD_COMPRESS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/zstd/lib/compress/*.c")
    file(GLOB ZSTD_DECOMPRESS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/zstd/lib/decompress/*.c")
    file(GLOB ZSTD_DICT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/zstd/lib/dictBuilder/*.c")
    add_library(libzstd_static STATIC
        ${ZSTD_COMMON_SOURCES}
        ${ZSTD_COMPRESS_SOURCES}
        ${ZSTD_DECOMPRESS_SOURCES}
        ${ZSTD_DICT_SOURCES}
    )
    target_compile_definitions(libzstd_static PRIVATE ZSTD_DISABLE_ASM=1)
    set_target_properties(libzstd_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(libzstd_static PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/zstd/lib")
    message(STATUS "zstd build/cmake missing - using External/zstd/lib fallback (libzstd_static)")
else()
    message(WARNING "zstd not found - using system zstd")
endif()

# ENet (Networking)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/enet/CMakeLists.txt)
    add_subdirectory(enet)
else()
    message(WARNING "ENet not found in External/enet - Networking will not be available")
endif()

# mbedTLS (Encryption)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/CMakeLists.txt")
    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    # Üçüncü taraf kodda derleyiciye özgü yeni uyarılar derlemeyi kırmasın (-Werror kapalı)
    set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
    
    # GCC 15 support: Disable false positive warnings on unterminated strings & zero-init union warnings
    # (yalnızca GCC bu bayrakları tanır; Clang/MinGW-Clang hata verir)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unterminated-string-initialization")
        if(CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 15)
            set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fzero-init-padding-bits=unions")
        endif()
    elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
        include(CheckCCompilerFlag)
        check_c_compiler_flag(-Wunterminated-string-initialization CQ_CLANG_HAS_UNTERMINATED_WARNING)
        if(CQ_CLANG_HAS_UNTERMINATED_WARNING)
            set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unterminated-string-initialization")
        endif()
    endif()
    
    add_subdirectory("mbedtls")
else()
    message(WARNING "mbedTLS not found in External/mbedtls - Encryption will not be available")
endif()

# miniupnpc (NAT Traversal)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/miniupnpc/CMakeLists.txt)
    set(UPNPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(UPNPC_BUILD_SAMPLE OFF CACHE BOOL "" FORCE)
    set(UPNPC_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(UPNPC_BUILD_STATIC ON CACHE BOOL "" FORCE)
    add_subdirectory(miniupnpc)
else()
    message(WARNING "miniupnpc not found in External/miniupnpc - NAT traversal will not be available")
endif()

# Luau (Roblox Lua lehçesi): betik dili VM + derleyici + yerel kod üretici
set(LUAU_BUILD_CLI OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_WEB OFF CACHE BOOL "" FORCE)
set(LUAU_WERROR OFF CACHE BOOL "" FORCE)
set(LUAU_BUILD_SHARED OFF CACHE BOOL "" FORCE)
add_subdirectory(luau)

# Crunch (crnlib): Use Crunch Compression'ın gerçek kodlayıcı / çözücüsü
add_subdirectory(crunch)
foreach(luau_target Luau.Common Luau.Ast Luau.Bytecode Luau.Inliner Luau.Compiler Luau.Config Luau.Analysis Luau.CodeGen Luau.VM Luau.Require Luau.CLI.lib isocline)
    if(TARGET ${luau_target})
        set_target_properties(${luau_target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
endforeach()

