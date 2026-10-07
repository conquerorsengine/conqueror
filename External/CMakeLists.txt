# External Dependencies

# Tüm external library'leri -fPIC ile derle (shared library desteği için)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# GLFW
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(glfw)

# GLAD
add_library(glad STATIC
    glad/src/glad.c
)
target_include_directories(glad PUBLIC glad/include)

# GLM
add_subdirectory(glm)

# spdlog
add_subdirectory(spdlog)

# ImGui
# Windows'ta Engine.dll + exe aynı ImGui context'ini (GImGui) paylaşmalı;
# yoksa exe'deki statik kopyanın context'i NULL kalır ve ilk SetNextWindow*'da AV verir.
# Bu yüzden Windows'ta imgui paylaşılan DLL, Linux'ta eskisi gibi statik.
if(WIN32)
    add_library(imgui SHARED
        imgui/imgui.cpp
        imgui/imgui_demo.cpp
        imgui/imgui_draw.cpp
        imgui/imgui_tables.cpp
        imgui/imgui_widgets.cpp
    )
    # NOT: target_compile_definitions parantezli değeri VS vcxproj'da bölüyor;
    # /D bayrağı compile_options ile verbatim geçilir.
    target_compile_options(imgui PRIVATE "/DIMGUI_API=__declspec(dllexport)")
    target_compile_options(imgui INTERFACE "/DIMGUI_API=__declspec(dllimport)")
else()
    add_library(imgui STATIC
        imgui/imgui.cpp
        imgui/imgui_demo.cpp
        imgui/imgui_draw.cpp
        imgui/imgui_tables.cpp
        imgui/imgui_widgets.cpp
    )
endif()
target_include_directories(imgui PUBLIC imgui)

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
if(WIN32)
    add_library(imguizmo SHARED
        ImGuizmo/ImGuizmo.cpp
    )
    target_compile_options(imguizmo PRIVATE "/DIMGUZMO_API=__declspec(dllexport)")
    target_compile_options(imguizmo INTERFACE "/DIMGUZMO_API=__declspec(dllimport)")
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

# NFD (Native File Dialog)
set(NFD_PORTAL OFF CACHE BOOL "" FORCE)
add_subdirectory(nativefiledialog-extended)

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
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/JoltPhysics/Build/CMakeLists.txt)
    set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
    set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
    set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
    set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
    set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
    # Projenin geri kalanı /MD (dinamik CRT) kullanır; Jolt varsayılan /MT ile LNK2038 verir
    set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
    add_subdirectory(JoltPhysics/Build)
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
    
    # GCC 15 support: Disable false positive warnings on unterminated strings & zero-init union warnings
    if(NOT MSVC)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unterminated-string-initialization -fzero-init-padding-bits=unions")
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
