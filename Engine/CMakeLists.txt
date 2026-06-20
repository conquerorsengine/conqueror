project(Engine)

# Collect all source files
file(GLOB_RECURSE ENGINE_SOURCES
    "src/Core/*.cpp"
    "src/Platform/*.cpp"
    "src/Renderer/*.cpp"
    "src/Scene/*.cpp"
    "src/Scripting/*.cpp"
    "src/AISystem/*.cpp"
    "src/AISystem/**/*.cpp"
    "src/Networking/*.cpp"
    "src/Networking/Encryption/*.cpp"
    "src/Networking/NAT/*.cpp"
    "src/Networking/Replication/*.cpp"
    "src/Networking/RPC/*.cpp"
)

# Linux: shared libEngine.so is easy to shadow with another copy on LD_LIBRARY_PATH / ld.so cache.
# Static linking ships all renderer code inside the executable so you always run what you compiled.
if(UNIX AND NOT APPLE)
    option(CQ_LINUX_ENGINE_CORE_STATIC "Build Engine as static on Linux (avoids wrong libEngine.so at runtime)" OFF)
else()
    set(CQ_LINUX_ENGINE_CORE_STATIC OFF)
endif()

if(CQ_LINUX_ENGINE_CORE_STATIC)
    add_library(${PROJECT_NAME} STATIC ${ENGINE_SOURCES})
else()
    add_library(${PROJECT_NAME} SHARED ${ENGINE_SOURCES})
    target_compile_definitions(${PROJECT_NAME} PRIVATE CQ_BUILD_DLL)
    target_compile_definitions(${PROJECT_NAME} PUBLIC CQ_DYNAMIC_LINK)
endif()

# Position Independent Code (shared library'ler için gerekli)
set_target_properties(${PROJECT_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Symbol visibility
set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN OFF
)

# Include directories
target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../External/glfw/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../External/glad/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../External/glm
    ${CMAKE_CURRENT_SOURCE_DIR}/../External/spdlog/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../External/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/../External/yaml-cpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../External/entt/single_include
    ${CMAKE_CURRENT_SOURCE_DIR}/../External/imguizmo
    ${CMAKE_CURRENT_SOURCE_DIR}/../External/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/../External/freetype/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../External/assimp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../External/miniaudio
    ${CMAKE_CURRENT_SOURCE_DIR}/../External/nativefiledialog-extended/src/include
    ${CMAKE_BINARY_DIR}/External/assimp/include
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PUBLIC
    glfw
    glad
    glm
    spdlog
    imgui
    yaml-cpp
    EnTT
    imguizmo
    stb
    freetype
    assimp
    nfd
)

# Compression libraries
if(TARGET zlibstatic)
    target_link_libraries(${PROJECT_NAME} PUBLIC zlibstatic)
else()
    target_link_libraries(${PROJECT_NAME} PUBLIC z)
endif()

if(TARGET libzstd_static)
    target_link_libraries(${PROJECT_NAME} PUBLIC libzstd_static)
else()
    target_link_libraries(${PROJECT_NAME} PUBLIC zstd)
endif()

# ENet (Networking)
if(TARGET enet)
    target_link_libraries(${PROJECT_NAME} PUBLIC enet)
    target_compile_definitions(${PROJECT_NAME} PUBLIC CQ_NETWORKING_ENABLED)
    target_include_directories(${PROJECT_NAME} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../External/enet/include
    )
endif()

# mbedTLS (Encryption)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../External/mbedtls/include)
    target_link_libraries(${PROJECT_NAME} PUBLIC mbedtls mbedcrypto mbedx509)
    target_compile_definitions(${PROJECT_NAME} PUBLIC CQ_ENCRYPTION_ENABLED)
    target_include_directories(${PROJECT_NAME} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../External/mbedtls/include
    )
endif()

# miniupnpc (NAT Traversal)
if(TARGET libminiupnpc-static)
    target_link_libraries(${PROJECT_NAME} PUBLIC libminiupnpc-static)
    target_compile_definitions(${PROJECT_NAME} PUBLIC CQ_NAT_ENABLED)
    target_include_directories(${PROJECT_NAME} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../External/miniupnpc/include
    )
endif()

# Physics libraries (optional)
if(TARGET box2d)
    target_link_libraries(${PROJECT_NAME} PUBLIC box2d)
    target_compile_definitions(${PROJECT_NAME} PUBLIC CQ_PHYSICS2D_ENABLED)
endif()

if(TARGET Jolt)
    target_link_libraries(${PROJECT_NAME} PUBLIC Jolt)
    target_compile_definitions(${PROJECT_NAME} PUBLIC CQ_PHYSICS3D_ENABLED)
    target_include_directories(${PROJECT_NAME} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../External/JoltPhysics
    )
endif()

# Platform specific
if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PUBLIC CQ_PLATFORM_WINDOWS)
else()
    target_link_libraries(${PROJECT_NAME} PUBLIC dl)
endif()

# Precompiled headers (optional)
target_precompile_headers(${PROJECT_NAME} PRIVATE
    <memory>
    <string>
    <vector>
    <unordered_map>
    <functional>
    <algorithm>
)
