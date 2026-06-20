project(ConquerorEngine)

# Create executable
add_executable(${PROJECT_NAME}
    Main.cpp
)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../Engine/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../Editor/src
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    Engine
    ConquerorEditor
)

# Prefer Engine from this build tree (Linux often picks up another libEngine.so otherwise).
if(UNIX AND NOT APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        BUILD_RPATH "$ORIGIN/../lib"
        INSTALL_RPATH "$ORIGIN/../lib")
endif()

# Set runtime library to MultiThreadedDebugDLL for Debug
set_target_properties(${PROJECT_NAME} PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreadedDebug$<$<CONFIG:Debug>:DLL>"
)

# Set as startup project in Visual Studio
set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})

# Copy resources to output directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/Resources
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/Resources
    VERBATIM
)

# Copy shaders to output directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${PROJECT_NAME}>/Engine/src
    VERBATIM
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/Engine/src/Shaders
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/Engine/src/Shaders
    VERBATIM
)
