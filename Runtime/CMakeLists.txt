project(ConquerorRuntime)

# Collect all source files
file(GLOB_RECURSE RUNTIME_SOURCES
    "src/*.cpp"
)

# Only create executable if there are sources
if(RUNTIME_SOURCES)
    add_executable(${PROJECT_NAME} ${RUNTIME_SOURCES})

    # Include directories
    target_include_directories(${PROJECT_NAME} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../Engine/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/spdlog/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/glm
    )

    # Link libraries
    target_link_libraries(${PROJECT_NAME} PRIVATE
        Engine
    )
else()
    message(STATUS "Runtime sources not found, skipping...")
endif()
