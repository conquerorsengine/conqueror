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
elseif(IOS)
    # iOS / iPadOS: UIKit uygulama paketi (conqueror.app). Oyun dosyaları, adı, paket kimliği ve imza Tools/iOS/package_ios.py ile
    add_executable(${PROJECT_NAME} MACOSX_BUNDLE ${RUNTIME_SOURCES} src/iOSMain.mm)
    set_source_files_properties(src/iOSMain.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc" SKIP_PRECOMPILE_HEADERS ON)
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../Engine/src)
    target_link_libraries(${PROJECT_NAME} PRIVATE Engine)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        OUTPUT_NAME conqueror
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/../Tools/iOS/Info.plist.in
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.conqueror.player"
        MACOSX_BUNDLE_BUNDLE_NAME "Conqueror"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${CMAKE_PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "1")
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
    if(EMSCRIPTEN)
        # Web oyuncusu: conqueror.js + conqueror.wasm (Tools/Web/package_web.py oyun dosyalarını ve index.html'i ekler).
        # Modül fabrikası createConqueror(): sayfa tuvali, oyun verisinin yüklenmesi ve ilerleme çubuğu sayfadadır
        set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME conqueror SUFFIX ".js")
        target_link_options(${PROJECT_NAME} PRIVATE
            -sMODULARIZE=1 -sEXPORT_NAME=createConqueror -sENVIRONMENT=web,worker
            -sEXPORTED_RUNTIME_METHODS=FS,ENV,IDBFS,callMain,addRunDependency,removeRunDependency
            -sEXIT_RUNTIME=0)
        if(CQ_WEB_THREADS)
            target_link_options(${PROJECT_NAME} PRIVATE -sPTHREAD_POOL_SIZE=16 -sPTHREAD_POOL_SIZE_STRICT=0)
        endif()
    elseif(UNIX AND NOT APPLE)
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
