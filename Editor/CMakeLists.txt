project(ConquerorEditor)

# Collect all source files
file(GLOB_RECURSE EDITOR_SOURCES
    "src/*.cpp"
)

# Create library
add_library(${PROJECT_NAME} STATIC ${EDITOR_SOURCES})

# Include directories
target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../Engine/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../External/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/../External/imnodes
    ${CMAKE_CURRENT_SOURCE_DIR}/../External/glm
    ${CMAKE_CURRENT_SOURCE_DIR}/../External/yaml-cpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../External/entt/single_include
    ${CMAKE_CURRENT_SOURCE_DIR}/../External/imguizmo
    ${CMAKE_CURRENT_SOURCE_DIR}/../External/ImGuiColorTextEdit
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PUBLIC
    Engine
    imgui
    imnodes
    imguizmo
    imguicolortextedit
    nfd
)
