project(ConquerorLauncher)

# Collect all source files
file(GLOB_RECURSE LAUNCHER_SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
)

# Create executable
add_executable(${PROJECT_NAME} ${LAUNCHER_SOURCES})
if(WIN32 AND CQ_APP_ICON_RC)
    target_sources(${PROJECT_NAME} PRIVATE ${CQ_APP_ICON_RC})
endif()

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../Engine/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../Editor/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../External/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/../External/IconFontCppHeaders
    ${CMAKE_CURRENT_SOURCE_DIR}/../External/glm
    ${CMAKE_CURRENT_SOURCE_DIR}/../External/yaml-cpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../External/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/../External/glad/include
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    Engine
    imgui
    nfd
)

if(UNIX AND NOT APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        BUILD_RPATH "$ORIGIN/../lib"
        INSTALL_RPATH "$ORIGIN/../lib")
endif()

# Copy resources to output directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/Resources
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/Resources
    VERBATIM
)

cq_apply_sanitizers(${PROJECT_NAME})
