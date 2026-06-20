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
add_library(imgui STATIC
    imgui/imgui.cpp
    imgui/imgui_demo.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
)
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
add_library(imguizmo STATIC
    ImGuizmo/ImGuizmo.cpp
)
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
add_subdirectory(freetype)

# Assimp
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
    add_subdirectory(JoltPhysics/Build)
else()
    message(WARNING "JoltPhysics not found in External/JoltPhysics - 3D physics will not be available")
endif()

# zstd
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/zstd/build/cmake/CMakeLists.txt)
    set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(zstd/build/cmake)
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
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/CMakeLists.txt)
    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    
    # GCC 15 support: Disable false positive warnings on unterminated strings & zero-init union warnings
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-unterminated-string-initialization -fzero-init-padding-bits=unions")
    
    add_subdirectory(mbedtls)
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
