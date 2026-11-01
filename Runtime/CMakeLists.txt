project(ConquerorRuntime)

# Collect all source files
file(GLOB_RECURSE RUNTIME_SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
)

if(ANDROID)
    # Android: oyuncu NativeActivity'nin yüklediği paylaşımlı kitaplıktır (libconqueror.so, android:lib_name="conqueror")
    add_library(conqueror SHARED
        ${RUNTIME_SOURCES}
        ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
    target_include_directories(conqueror PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../Engine/src
        ${ANDROID_NDK}/sources/android/native_app_glue)
    target_link_libraries(conqueror PRIVATE Engine)
    # Yapıştırıcı kodun giriş noktası başka hiçbir yerden çağrılmaz: bağlayıcı atmasın
    target_link_options(conqueror PRIVATE "-u" "ANativeActivity_onCreate")
    # APK düzeni: lib/<abi>/libconqueror.so (yapılandırma başına çıktı klasörleri de aynı yere)
    set_target_properties(conqueror PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib/${ANDROID_ABI})
    foreach(_cfg RELEASE DEBUG RELWITHDEBINFO MINSIZEREL)
        set_target_properties(conqueror PROPERTIES LIBRARY_OUTPUT_DIRECTORY_${_cfg} ${CMAKE_BINARY_DIR}/lib/${ANDROID_ABI})
    endforeach()
elseif(RUNTIME_SOURCES)
    add_executable(${PROJECT_NAME} ${RUNTIME_SOURCES})
    if(WIN32 AND CQ_APP_ICON_RC)
        target_sources(${PROJECT_NAME} PRIVATE ${CQ_APP_ICON_RC})
    endif()

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
    # Python modülü (varsa) oyuncuyla birlikte derlensin
    if(TARGET CQPython)
        add_dependencies(${PROJECT_NAME} CQPython)
    endif()

    # Derlenmiş oyun taşınabilir olmalı: libEngine oyunun yanındaki lib/ klasöründen yüklenir
    # (build ağacındaki ../lib de çalışsın diye ikisi birden)
    if(UNIX AND NOT APPLE)
        set_target_properties(${PROJECT_NAME} PROPERTIES
            BUILD_RPATH "\$ORIGIN/lib;\$ORIGIN/../lib"
            INSTALL_RPATH "\$ORIGIN/lib")
    elseif(APPLE)
        set_target_properties(${PROJECT_NAME} PROPERTIES
            BUILD_RPATH "@executable_path/lib;@executable_path/../lib"
            INSTALL_RPATH "@executable_path/lib")
    endif()
else()
    message(STATUS "Runtime sources not found, skipping...")
endif()
