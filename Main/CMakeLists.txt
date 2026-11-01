project(ConquerorEngine)

# Create executable
add_executable(${PROJECT_NAME}
    Main.cpp
)
if(WIN32 AND CQ_APP_ICON_RC)
    target_sources(${PROJECT_NAME} PRIVATE ${CQ_APP_ICON_RC})
endif()

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

# Set runtime library to dynamic (/MD) for all configs
set_target_properties(${PROJECT_NAME} PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)

# Set as startup project in Visual Studio
set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${PROJECT_NAME})

# Kaynakları çıktı klasörüne eşitle: her derlemede çalışır (yalnızca değişen dosyalar kopyalanır),
# böylece yalnızca Resources değiştiğinde de (ör. C# barındırıcı kaynakları) güncel kalır.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.26)
    set(CQ_COPY_DIR_CMD copy_directory_if_different)
else()
    set(CQ_COPY_DIR_CMD copy_directory)
endif()
add_custom_target(SyncResources ALL
    # Proje şablonlarından silinen dosyalar çıktıdan da silinir (yoksa yeni projelere kopyalanırlardı)
    COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR}/Resources/Templates/Projects
    -DDST=$<TARGET_FILE_DIR:${PROJECT_NAME}>/Resources/Templates/Projects
    -P ${CMAKE_SOURCE_DIR}/Tools/Templates/PruneStale.cmake
    COMMAND ${CMAKE_COMMAND} -E ${CQ_COPY_DIR_CMD}
    ${CMAKE_SOURCE_DIR}/Resources
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/Resources
    COMMAND ${CMAKE_COMMAND} -E ${CQ_COPY_DIR_CMD}
    ${CMAKE_SOURCE_DIR}/Engine/src/Shaders
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/Engine/src/Shaders
    # Varsayılan giriş eşlemeleri: editör kaynak ağacı dışında (kurulum / dağıtım) da bulsun
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${PROJECT_NAME}>/Engine/src/Core/Input
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/Engine/src/Core/Input/default.cqinput
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/Engine/src/Core/Input/default.cqinput
    VERBATIM
)
add_dependencies(${PROJECT_NAME} SyncResources)

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

cq_apply_sanitizers(${PROJECT_NAME})
