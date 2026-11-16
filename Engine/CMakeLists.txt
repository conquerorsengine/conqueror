project(Engine)

# Collect all source files
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
    "src/Core/*.cpp"
    "src/Platform/*.cpp"
    "src/Renderer/*.cpp"
    "src/Scene/*.cpp"
    "src/Terrain/*.cpp"
    "src/Particles/*.cpp"
    "src/Grid/*.cpp"
    "src/Timeline/*.cpp"
    "src/Addons/*.cpp"
    "src/UI/*.cpp"
    "src/Scripting/*.cpp"
    "src/XR/*.cpp"
    "src/AISystem/*.cpp"
    "src/AISystem/**/*.cpp"
    "src/Networking/*.cpp"
    "src/Networking/Encryption/*.cpp"
    "src/Networking/NAT/*.cpp"
    "src/Networking/Replication/*.cpp"
    "src/Networking/RPC/*.cpp"
)

# 3B fizik: Jolt yoksa (CQ_ENABLE_JOLT=OFF, QNX) arka uç yerine boş uygulama derlenir
if(TARGET Jolt)
    list(FILTER ENGINE_SOURCES EXCLUDE REGEX ".*/Physics3D/JoltBackendUnavailable\\.cpp$")
else()
    list(FILTER ENGINE_SOURCES EXCLUDE REGEX ".*/Physics3D/JoltBackend\\.cpp$")
endif()

# Tint köprüsü ayrı kitaplıkta (Tint'in içerme yollarıyla) derlenir; motor kaynaklarına girmez
list(FILTER ENGINE_SOURCES EXCLUDE REGEX ".*/WebGPU/TintBridge\\.cpp$")

# Platforma özel kaynakları ayıkla (işlev kaldırılmadı, sadece yanlış platformda derlenmesin)
# NOT: Platform/Windows altındaki dosyalar portable GLFW kodudur, Linux'ta da derlenir;
# yalnızca dlfcn.h isteyen Linux/ klasörü Windows'ta hariç tutulur.
if(WIN32)
    list(FILTER ENGINE_SOURCES EXCLUDE REGEX "src/Platform/Linux/.*")
endif()

# Linux: shared libEngine.so is easy to shadow with another copy on LD_LIBRARY_PATH / ld.so cache.
# Static linking ships all renderer code inside the executable so you always run what you compiled.
if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "QNX" AND NOT EMSCRIPTEN AND NOT CMAKE_SYSTEM_NAME STREQUAL "OHOS")
    option(CQ_LINUX_ENGINE_CORE_STATIC "Build Engine as static on Linux (avoids wrong libEngine.so at runtime)" OFF)
else()
    set(CQ_LINUX_ENGINE_CORE_STATIC OFF)
endif()

# Windows/macOS: motor çekirdeği statik. C++ betik DLL'leri motorla yalnızca C ABI üzerinden konuşur (CQHostApi),
# bu yüzden DLL dışa aktarım listesine (her sınıfa CQ_API) gerek kalmaz. HarmonyOS: editör / oyuncu .so'su motoru içinde taşır.
if(CQ_LINUX_ENGINE_CORE_STATIC OR WIN32 OR APPLE OR ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "QNX" OR EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "OHOS")
    add_library(${PROJECT_NAME} STATIC ${ENGINE_SOURCES})
else()
    add_library(${PROJECT_NAME} SHARED ${ENGINE_SOURCES})
    target_compile_definitions(${PROJECT_NAME} PRIVATE CQ_BUILD_DLL)
    target_compile_definitions(${PROJECT_NAME} PUBLIC CQ_DYNAMIC_LINK)
endif()

# sljit: ConquerorScript JIT arka ucu (x86-32/64, ARM32/64, RISC-V, PPC, s390x, LoongArch, MIPS)
set(CQ_SLJIT_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/../External/sljit/sljit_src/sljitLir.c)
# Web: WebAssembly makine kodu üretemez; iOS çalıştırılabilir bellek vermez: JIT derlenmez (CQSJit.cpp yorumlayıcıya bırakır)
if(NOT EMSCRIPTEN AND NOT IOS)
    target_sources(${PROJECT_NAME} PRIVATE ${CQ_SLJIT_SOURCE})
endif()
set_source_files_properties(${CQ_SLJIT_SOURCE} PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON
    COMPILE_OPTIONS "$<IF:$<C_COMPILER_ID:MSVC>,/w,-w>")
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../External/sljit/sljit_src)
# OpenXR başlıkları (yükleyici çalışma zamanında dlopen ile: libopenxr_loader.so.1 / openxr_loader.dll)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../External/openxr/include)
target_compile_definitions(${PROJECT_NAME} PRIVATE SLJIT_CONFIG_AUTO=1)

# AMD FidelityFX Super Resolution 2 SDK çekirdeği (MIT): OpenGL arka ucu src/Renderer/FSR2/FSR2GL.cpp
set(CQ_FSR2_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../External/FidelityFX-FSR2/src/ffx-fsr2-api)
set(CQ_FSR2_SOURCES ${CQ_FSR2_DIR}/ffx_fsr2.cpp ${CQ_FSR2_DIR}/ffx_assert.cpp)
target_sources(${PROJECT_NAME} PRIVATE ${CQ_FSR2_SOURCES})
set_source_files_properties(${CQ_FSR2_SOURCES} PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON
    COMPILE_DEFINITIONS "FFX_GCC=1"
    COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w;-include;${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/FSR2/fsr2_compat.h>")
target_compile_definitions(${PROJECT_NAME} PRIVATE FFX_GCC=1)

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
    ${CMAKE_CURRENT_SOURCE_DIR}/../External/IconFontCppHeaders
    ${CMAKE_CURRENT_SOURCE_DIR}/../External/mujoco/include
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
    glslang
    glslang-default-resource-limits
    spirv-cross-core
    spirv-cross-glsl
    spirv-cross-hlsl
    spirv-cross-msl
    spirv-cross-reflect
)


# Compression libraries
if(TARGET zlibstatic)
    target_link_libraries(${PROJECT_NAME} PUBLIC zlibstatic)
else()
    target_link_libraries(${PROJECT_NAME} PUBLIC z)
endif()

# ── Betik dilleri ──
# Luau (gömülü, her platformda)
target_link_libraries(${PROJECT_NAME} PRIVATE Luau.Compiler Luau.VM Luau.CodeGen)
# Crunch (crnlib): .crn dokularını çözme (motor) ve kodlama (editör derlemesi)
target_link_libraries(${PROJECT_NAME} PUBLIC crnlib)
target_compile_definitions(${PROJECT_NAME} PRIVATE CQ_LUAU_ENABLED)

# ── Vulkan arka ucu ── (GL -> Vulkan 1.2/1.3 çeviri katmanı; --vulkan / CQ_RENDERER=vulkan / Player Settings ile seçilir)
# Başlıklar External/Vulkan-Headers'da; yükleyici çalışma anında açılır (VulkanLoader): Vulkan SDK gerekmez ve
# yürütülebilir dosya vulkan-1.dll / libvulkan olmadan da açılır (OpenGL / D3D11'e düşer).
if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "QNX" OR EMSCRIPTEN OR IOS OR CMAKE_SYSTEM_NAME STREQUAL "OHOS")
    # Android / QNX / HarmonyOS: OpenGL ES 3.x (EGL). Vulkan arka ucu GLFW yüzeyi kullandığı için şimdilik kapalı. Web: WebGL 2 / WebGPU. iOS: Metal
    option(CQ_ENABLE_VULKAN "Build the Vulkan backend" OFF)
else()
    option(CQ_ENABLE_VULKAN "Build the Vulkan backend" ON)
endif()
if(CQ_ENABLE_VULKAN)
    target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../External/Vulkan-Headers/include)
    target_compile_definitions(${PROJECT_NAME} PUBLIC CQ_VULKAN_ENABLED)
    message(STATUS "Conqueror: Vulkan backend enabled (runtime-loaded)")
endif()

# Intel XeSS: başlıklar External/xess (MIT); libxess.dll (yalnız Windows) çalışma anında açılır (Upscalers/XeSSUpscaler.cpp)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../External/xess/include)

# ── NVIDIA DLSS (isteğe bağlı eklenti) ── NVIDIA'nın DLSS SDK'sı (NVIDIA RTX SDKs lisansı: açık kaynak dağıtıma girmez) depoda değil:
# Tools/DLSS/fetch-dlss-sdk.sh <klasör> --accept-nvidia-license ile indirilir, -DCQ_DLSS_SDK_DIR=<klasör> (ya da aynı adlı ortam
# değişkeni) verilince Upscalers/DLSSUpscaler.cpp NGX ile derlenir ve DLSS çalışma zamanı kitaplığı oyuncunun yanına kopyalanır.
# Motor OpenGL ile çizer; DLSS NGX'in Vulkan yolundan, OpenGL <-> Vulkan görüntü paylaşımıyla (VkGLInterop) çalışır: Vulkan gerekir.
set(CQ_DLSS_SDK_DIR "$ENV{CQ_DLSS_SDK_DIR}" CACHE PATH "NVIDIA DLSS SDK folder (Tools/DLSS/fetch-dlss-sdk.sh); empty = build without DLSS")
if(CQ_DLSS_SDK_DIR AND EXISTS "${CQ_DLSS_SDK_DIR}/include/nvsdk_ngx.h" AND CQ_ENABLE_VULKAN
   AND (WIN32 OR (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")))
    # Yalnız DLSS dosyası NGX başlıklarını görür (motorun geri kalanı SDK'dan bağımsız; tanım değişince her şey yeniden derlenmesin)
    set_source_files_properties(src/Renderer/Upscalers/DLSSUpscaler.cpp PROPERTIES
        COMPILE_DEFINITIONS CQ_DLSS_ENABLED INCLUDE_DIRECTORIES "${CQ_DLSS_SDK_DIR}/include" SKIP_PRECOMPILE_HEADERS ON)
    if(WIN32)
        target_link_libraries(${PROJECT_NAME} PUBLIC "${CQ_DLSS_SDK_DIR}/lib/Windows_x86_64/x64/nvsdk_ngx_d.lib")
        set(_cq_dlss_runtime "${CQ_DLSS_SDK_DIR}/lib/Windows_x86_64/rel/nvngx_dlss.dll")
    else()
        target_link_libraries(${PROJECT_NAME} PUBLIC "${CQ_DLSS_SDK_DIR}/lib/Linux_x86_64/libnvsdk_ngx.a" ${CMAKE_DL_LIBS})
        file(GLOB _cq_dlss_runtime "${CQ_DLSS_SDK_DIR}/lib/Linux_x86_64/rel/libnvidia-ngx-dlss.so.*")
    endif()
    # NGX DLSS kitaplığını oyunun yanında arar: editörün / oyuncunun derleme klasörüne (derlenen oyunlara Build Settings kopyalar)
    if(_cq_dlss_runtime)
        file(COPY ${_cq_dlss_runtime} DESTINATION ${CMAKE_BINARY_DIR}/bin)
        # Linux: derlenen oyunlar lib/ klasörünü alır (NGX oyunun yanına ve lib/'e bakar)
        if(NOT WIN32)
            file(COPY ${_cq_dlss_runtime} DESTINATION ${CMAKE_BINARY_DIR}/lib)
        endif()
    endif()
    message(STATUS "Conqueror: NVIDIA DLSS plugin enabled (${CQ_DLSS_SDK_DIR})")
elseif(CQ_DLSS_SDK_DIR)
    message(WARNING "CQ_DLSS_SDK_DIR is set but no usable DLSS SDK was found there (or this platform has no DLSS): building without DLSS")
endif()

# ── WebGPU arka ucu ── (GL -> WebGPU çeviri katmanı, Metal / Vulkan katmanlarıyla aynı ön yüz; Player Settings > Graphics API,
# --webgpu ya da CQ_RENDERER=webgpu). Gölgelendiriciler SPIR-V -> WGSL Google Tint ile: Dawn kaynak ağacı gerekir
# (Tools/WebGPU/fetch-dawn.sh <klasör>, CQ_DAWN_DIR). Masaüstünde WebGPU uygulaması (Dawn / wgpu-native) çalışma anında açılır;
# Web'de tarayıcının WebGPU'su (emdawnwebgpu). Dawn yoksa arka uç derlenmez, istek OpenGL'e düşer.
set(CQ_DAWN_DIR "$ENV{CQ_DAWN_DIR}" CACHE PATH "Dawn source tree for the WebGPU backend's shader translator (Tools/WebGPU/fetch-dawn.sh)")
if(CQ_DAWN_DIR AND EXISTS "${CQ_DAWN_DIR}/src/tint" AND NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "QNX" AND NOT CMAKE_SYSTEM_NAME STREQUAL "OHOS")
    foreach(_opt DAWN_BUILD_SAMPLES DAWN_BUILD_TESTS DAWN_USE_GLFW DAWN_ENABLE_VULKAN DAWN_ENABLE_DESKTOP_GL DAWN_ENABLE_OPENGLES
                 DAWN_ENABLE_NULL DAWN_ENABLE_D3D11 DAWN_ENABLE_D3D12 DAWN_ENABLE_METAL DAWN_ENABLE_SPIRV_VALIDATION DAWN_USE_X11
                 DAWN_USE_WAYLAND TINT_BUILD_GLSL_WRITER TINT_BUILD_GLSL_VALIDATOR TINT_BUILD_HLSL_WRITER TINT_BUILD_MSL_WRITER
                 TINT_BUILD_SPV_WRITER TINT_BUILD_TESTS TINT_BUILD_CMD_TOOLS TINT_BUILD_BENCHMARKS TINT_BUILD_FUZZERS DAWN_FETCH_DEPENDENCIES)
        set(${_opt} OFF CACHE BOOL "" FORCE)
    endforeach()
    foreach(_opt TINT_BUILD_SPV_READER TINT_BUILD_WGSL_WRITER TINT_BUILD_WGSL_READER)
        set(${_opt} ON CACHE BOOL "" FORCE)
    endforeach()
    # Tint'in SPIR-V okuyucusu SPIRV-Tools ister; Dawn'ın üçüncü taraf listesi Emscripten'de onu eklemeden döner: önce biz ekleriz
    if(NOT TARGET SPIRV-Headers)
        set(SPIRV_HEADERS_SKIP_EXAMPLES ON CACHE BOOL "" FORCE)
        set(SPIRV_HEADERS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
        add_subdirectory(${CQ_DAWN_DIR}/third_party/spirv-headers/src ${CMAKE_BINARY_DIR}/dawn-spirv-headers EXCLUDE_FROM_ALL)
    endif()
    if(NOT TARGET SPIRV-Tools)
        set(SPIRV-Headers_SOURCE_DIR ${CQ_DAWN_DIR}/third_party/spirv-headers/src)
        foreach(_opt SPIRV_SKIP_TESTS SPIRV_SKIP_EXECUTABLES SKIP_SPIRV_TOOLS_INSTALL)
            set(${_opt} ON CACHE BOOL "" FORCE)
        endforeach()
        set(SPIRV_WERROR OFF CACHE BOOL "" FORCE)
        add_subdirectory(${CQ_DAWN_DIR}/third_party/spirv-tools/src ${CMAKE_BINARY_DIR}/dawn-spirv-tools EXCLUDE_FROM_ALL)
    endif()
    add_subdirectory(${CQ_DAWN_DIR} ${CMAKE_BINARY_DIR}/dawn EXCLUDE_FROM_ALL)
    add_library(CQTintBridge STATIC src/Renderer/GraphicsAPIs/WebGPU/TintBridge.cpp)
    target_include_directories(CQTintBridge PRIVATE ${CQ_DAWN_DIR} ${CQ_DAWN_DIR}/include ${CMAKE_BINARY_DIR}/dawn/gen/include)
    target_link_libraries(CQTintBridge PRIVATE tint_api tint_lang_spirv_reader tint_lang_wgsl_writer tint_lang_core_ir dawn_utils_config)
    # Tint'in SPIR-V ayrıştırıcısı SPIRV-Tools'un üretilen tablolarını içerir; o hedef bu sırayı kendisi bildirmiyor
    if(TARGET tint_lang_spirv_reader_parser AND TARGET core_tables)
        add_dependencies(tint_lang_spirv_reader_parser core_tables)
    endif()
    target_compile_definitions(CQTintBridge PRIVATE TINT_BUILD_SPV_READER=1 TINT_BUILD_WGSL_WRITER=1)
    set_target_properties(CQTintBridge PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_STANDARD 20)
    target_link_libraries(${PROJECT_NAME} PRIVATE CQTintBridge)
    target_compile_definitions(${PROJECT_NAME} PUBLIC CQ_WEBGPU_ENABLED)
    if(EMSCRIPTEN)
        target_compile_options(${PROJECT_NAME} PUBLIC --use-port=emdawnwebgpu)
        target_link_options(${PROJECT_NAME} PUBLIC --use-port=emdawnwebgpu)
    else()
        target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../External/webgpu-headers/include)
    endif()
    message(STATUS "Conqueror: WebGPU backend enabled (Tint from ${CQ_DAWN_DIR})")
else()
    message(STATUS "Conqueror: WebGPU backend disabled (set CQ_DAWN_DIR to a Dawn source tree, see Tools/WebGPU/fetch-dawn.sh)")
endif()

# ── Direct3D 11 arka ucu ── (Windows varsayılanı; GL -> D3D11 çeviri katmanı, HLSL SPIRV-Cross ile)
if(WIN32)
    if(MSVC)
        target_link_libraries(${PROJECT_NAME} PUBLIC d3d11 dxgi d3dcompiler dxguid)
    else()
        target_link_libraries(${PROJECT_NAME} PUBLIC d3d11 dxgi d3dcompiler_47 dxguid)
    endif()
    target_compile_definitions(${PROJECT_NAME} PUBLIC CQ_D3D11_ENABLED)
    message(STATUS "Conqueror: Direct3D 11 backend enabled")
    # Direct3D 12: d3d12.dll çalışma anında açılır (Windows 10 öncesinde D3D11 / OpenGL'e düşer)
    target_compile_definitions(${PROJECT_NAME} PUBLIC CQ_D3D12_ENABLED)
    message(STATUS "Conqueror: Direct3D 12 backend enabled (runtime-loaded)")
endif()

# ── Metal arka ucu ── (macOS varsayılanı; GL -> Metal çeviri katmanı, MSL SPIRV-Cross ile; Objective-C++ / ARC)
if(APPLE)
    enable_language(OBJCXX)
    file(GLOB CQ_METAL_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/Renderer/GraphicsAPIs/Metal/*.mm")
    target_sources(${PROJECT_NAME} PRIVATE ${CQ_METAL_SOURCES})
    # Önderlenmiş başlık ARC olmadan C++ için derlenir; ARC açık Objective-C++ dosyaları onu kullanamaz
    set_source_files_properties(${CQ_METAL_SOURCES} PROPERTIES COMPILE_FLAGS "-fobjc-arc" SKIP_PRECOMPILE_HEADERS ON)
    if(IOS)
        # iOS / iPadOS: UIKit penceresi + girdi (GLFW işlevleri), oyun kumandaları; ses (miniaudio) AVAudioSession ile
        file(GLOB CQ_IOS_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/Platform/iOS/*.mm")
        target_sources(${PROJECT_NAME} PRIVATE ${CQ_IOS_SOURCES})
        set_source_files_properties(${CQ_IOS_SOURCES} PROPERTIES COMPILE_FLAGS "-fobjc-arc" SKIP_PRECOMPILE_HEADERS ON)
        # miniaudio iOS'ta AVFoundation'ı (Objective-C başlığı) içerir: ses motoru Objective-C++ olarak derlenir
        set_source_files_properties(src/Core/Audio/AudioEngine.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++" SKIP_PRECOMPILE_HEADERS ON)
        target_link_libraries(${PROJECT_NAME} PUBLIC "-framework Metal" "-framework QuartzCore" "-framework UIKit" "-framework Foundation"
                              "-framework GameController" "-framework AVFoundation" "-framework AudioToolbox" "-framework CoreAudio"
                              "-framework CoreFoundation")
    else()
        target_link_libraries(${PROJECT_NAME} PUBLIC "-framework Metal" "-framework QuartzCore" "-framework Cocoa")
    endif()
    # MetalFX (Upscaling > MetalFX): macOS 13 / iOS 16 ve sonrası; zayıf bağlantı, eski sistemde oyun açılır, MetalFX gri kalır
    # iOS Simülatörü SDK'sında MetalFX yok (MetalFXUpscaler.mm orada yer tutucu derler)
    if(NOT CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
        target_link_libraries(${PROJECT_NAME} PUBLIC "-weak_framework MetalFX")
    endif()
    target_compile_definitions(${PROJECT_NAME} PUBLIC CQ_METAL_ENABLED)
    message(STATUS "Conqueror: Metal backend enabled")
endif()

# GLFW yalnızca Wayland (AGL / Yocto: GLFW_BUILD_X11=OFF) ya da yalnızca X11 ile derlendiyse ImGui'nin GLFW arka ucu
# olmayan platformun işlevlerini (glfwGetX11Display ...) çağırmasın
if(DEFINED GLFW_BUILD_X11 AND NOT GLFW_BUILD_X11)
    target_compile_definitions(${PROJECT_NAME} PRIVATE IMGUI_IMPL_GLFW_DISABLE_X11)
endif()
if(DEFINED GLFW_BUILD_WAYLAND AND NOT GLFW_BUILD_WAYLAND)
    target_compile_definitions(${PROJECT_NAME} PRIVATE IMGUI_IMPL_GLFW_DISABLE_WAYLAND)
endif()

# Varsayılan OpenGL profili (--gl / CQ_GL_PROFILE verilmezse): gömülü / otomotiv Linux görüntüleri için, ör. "es3.2"
set(CQ_DEFAULT_GL_PROFILE "" CACHE STRING "Default GL profile when none is requested (4.6, 4.1, 3.3, es3.2, es3.0 ...)")
if(CQ_DEFAULT_GL_PROFILE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE CQ_DEFAULT_GL_PROFILE="${CQ_DEFAULT_GL_PROFILE}")
endif()

# Python (CPython gömme): geliştirme dosyaları bulunursa (Android / HarmonyOS'ta ana makinenin Python'u kullanılamaz)
if(NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "QNX" AND NOT EMSCRIPTEN AND NOT IOS AND NOT CMAKE_SYSTEM_NAME STREQUAL "OHOS")
    find_package(Python3 COMPONENTS Development.Embed QUIET)
endif()
if(Python3_Development.Embed_FOUND AND NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "QNX" AND NOT IOS AND NOT CMAKE_SYSTEM_NAME STREQUAL "OHOS")
    get_target_property(_cq_engine_type ${PROJECT_NAME} TYPE)
    if(_cq_engine_type STREQUAL "SHARED_LIBRARY" AND NOT WIN32)
        # Python ayrı modül (libCQPython.so): motor onu gerektiğinde açar. Derlenen oyun Python kurulu olmayan
        # makinede de açılır; yalnızca Python betikleri devre dışı kalır.
        get_target_property(_cq_engine_sources ${PROJECT_NAME} SOURCES)
        list(FILTER _cq_engine_sources EXCLUDE REGEX ".*/Scripting/Python/PythonRuntime\\.cpp$")
        set_target_properties(${PROJECT_NAME} PROPERTIES SOURCES "${_cq_engine_sources}")
        target_compile_definitions(${PROJECT_NAME} PRIVATE CQ_PYTHON_MODULE)
        add_library(CQPython SHARED src/Scripting/Python/PythonRuntime.cpp)
        target_link_libraries(CQPython PRIVATE ${PROJECT_NAME} Python3::Python)
        # BSD'lerde Python'un bağlantı bayrakları (-lintl ...) /usr/local/lib'deki paketlere işaret eder; bağlayıcı orayı kendiliğinden aramaz
        if(CMAKE_SYSTEM_NAME MATCHES "BSD|DragonFly")
            target_link_directories(CQPython PRIVATE /usr/local/lib)
        endif()
        target_compile_definitions(CQPython PRIVATE CQ_PYTHON_ENABLED CQ_PYTHON_MODULE)
        set_target_properties(CQPython PROPERTIES POSITION_INDEPENDENT_CODE ON)
        message(STATUS "ConquerorEngine: Python scripting enabled as module (${Python3_VERSION})")
    else()
        target_link_libraries(${PROJECT_NAME} PRIVATE Python3::Python)
        target_compile_definitions(${PROJECT_NAME} PRIVATE CQ_PYTHON_ENABLED)
        message(STATUS "ConquerorEngine: Python scripting enabled (${Python3_VERSION})")
    endif()
endif()

# Java (JNI): yalnızca başlıklar; libjvm çalışma anında yüklenir (JAVA_HOME). HarmonyOS'ta JVM yok
if(NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "QNX" AND NOT EMSCRIPTEN AND NOT IOS AND NOT CMAKE_SYSTEM_NAME STREQUAL "OHOS")
    find_package(JNI QUIET COMPONENTS JVM)
endif()
if(JNI_INCLUDE_DIRS AND NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "QNX" AND NOT IOS AND NOT CMAKE_SYSTEM_NAME STREQUAL "OHOS")
    target_include_directories(${PROJECT_NAME} PRIVATE ${JNI_INCLUDE_DIRS})
    target_compile_definitions(${PROJECT_NAME} PRIVATE CQ_JAVA_ENABLED)
    message(STATUS "ConquerorEngine: Java scripting enabled")
endif()

if(TARGET libzstd_static)
    target_link_libraries(${PROJECT_NAME} PUBLIC libzstd_static)
    target_include_directories(${PROJECT_NAME} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../External/zstd/lib
    )
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
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../External/mbedtls/include")
    target_link_libraries(${PROJECT_NAME} PUBLIC mbedtls mbedcrypto mbedx509)
    target_compile_definitions(${PROJECT_NAME} PUBLIC CQ_ENCRYPTION_ENABLED)
    target_include_directories(${PROJECT_NAME} PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../External/mbedtls/include"
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
# GL işlevleri ve türleri glad'dan gelir: glfw3.h sistemin GL/gl.h başlığını aramasın (PUBLIC: oyuncu, editör ve
# başlatıcı da). AGL / Yocto, BSD gibi yalnız GLES / Wayland kurulu sistemlerde o başlık yoktur (CI linux-embedded)
target_compile_definitions(${PROJECT_NAME} PUBLIC GLFW_INCLUDE_NONE)

if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PUBLIC CQ_PLATFORM_WINDOWS)
    # ImGui arka uçları (ImGuiBuild.cpp) motorun içinde derlenir; bildirimleri imgui.dll içe aktarımı sanmasın
    target_compile_definitions(${PROJECT_NAME} PUBLIC IMGUI_IMPL_API=)
    target_link_libraries(${PROJECT_NAME} PUBLIC ws2_32 winmm)
elseif(ANDROID)
    # NativeActivity + EGL + OpenGL ES 3; android_native_app_glue.h başlığı GLFWAndroid.cpp için
    target_include_directories(${PROJECT_NAME} PUBLIC ${ANDROID_NDK}/sources/android/native_app_glue)
    # glfw3.h masaüstü GL/gl.h başlığını aramasın (GL işlevleri glad üzerinden)
    target_compile_definitions(${PROJECT_NAME} PUBLIC GLFW_INCLUDE_NONE)
    target_link_libraries(${PROJECT_NAME} PUBLIC android log EGL GLESv3 dl)
elseif(CMAKE_SYSTEM_NAME STREQUAL "QNX")
    # QNX: Screen (pencere / girdi) + EGL + OpenGL ES; soketler libsocket'te, dlopen libc'de (libdl yok)
    target_compile_definitions(${PROJECT_NAME} PUBLIC GLFW_INCLUDE_NONE _QNX_SOURCE)
    target_link_libraries(${PROJECT_NAME} PUBLIC screen EGL GLESv2 socket m)
elseif(CMAKE_SYSTEM_NAME STREQUAL "OHOS")
    # HarmonyOS (OpenHarmony NDK, API 12+): XComponent yüzeyi (ace_ndk) + OHNativeWindow + EGL + OpenGL ES 3.x, NAPI modülü
    # (ace_napi), hilog günlüğü, HAP içi dosyalar (rawfile), ekran yoğunluğu (native_display_manager). API 13+ işlevleri
    # (pano) çalışma anında dlopen ile açılır: uygulama HarmonyOS 5.0 (API 12) cihazlarda da yüklenir
    target_compile_definitions(${PROJECT_NAME} PUBLIC GLFW_INCLUDE_NONE)
    target_link_libraries(${PROJECT_NAME} PUBLIC ace_napi.z ace_ndk.z native_window EGL GLESv3 hilog_ndk.z rawfile.z
                          native_display_manager dl m)
elseif(EMSCRIPTEN)
    # Web: GLFW (contrib.glfw3: tarayıcı tuvali, HiDPI, oyun kumandası), WebGL 2 (OpenGL ES 3.0; glad işlevleri
    # glfwGetProcAddress ile), büyüyebilen bellek, dosya sistemi (oyun dosyaları açılışta belleğe yazılır)
    target_compile_options(${PROJECT_NAME} PUBLIC --use-port=contrib.glfw3)
    target_link_options(${PROJECT_NAME} PUBLIC
        --use-port=contrib.glfw3
        -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sFULL_ES3=1 -sGL_ENABLE_GET_PROC_ADDRESS=1
        -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 -sMAXIMUM_MEMORY=4294967296 -sSTACK_SIZE=8388608
        -sFORCE_FILESYSTEM=1 -lidbfs.js)
elseif(IOS)
    # iOS / iPadOS: çerçeveler Metal bölümünde; X11 / D-Bus yok
    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} PUBLIC Threads::Threads)
else()
    if(CMAKE_SYSTEM_NAME MATCHES "BSD|DragonFly")
        # BSD: GL işlevleri glad'dan; glfw3.h sistemde olmayabilecek GL/gl.h başlığını aramasın
        target_compile_definitions(${PROJECT_NAME} PUBLIC GLFW_INCLUDE_NONE)
    endif()
    # ImGui GLFW arka ucu X11 başlıklarını (X11/Xatom.h) kullanır; BSD'lerde paketler /usr/local/include altındadır ve
    # derleyici orada kendiliğinden aramaz (CI FreeBSD işi bu yüzden düşüyordu)
    if(NOT (DEFINED GLFW_BUILD_X11 AND NOT GLFW_BUILD_X11))
        find_package(X11 QUIET)
        if(X11_FOUND AND X11_INCLUDE_DIR)
            target_include_directories(${PROJECT_NAME} PRIVATE ${X11_INCLUDE_DIR})
        elseif(CMAKE_SYSTEM_NAME MATCHES "BSD|DragonFly")
            target_include_directories(${PROJECT_NAME} PRIVATE /usr/local/include)
        endif()
    endif()
    # Status Indicator (Godot): sistem tepsisi simgesi D-Bus StatusNotifierItem ile. libdbus çalışma zamanında dlopen ile yüklenir
    # (yoksa özellik kapanır); derlemede yalnız başlıkları gerekir
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(CQ_DBUS QUIET dbus-1)
    endif()
    if(CQ_DBUS_FOUND)
        target_include_directories(${PROJECT_NAME} PRIVATE ${CQ_DBUS_INCLUDE_DIRS})
        target_compile_definitions(${PROJECT_NAME} PRIVATE CQ_HAS_DBUS_HEADERS=1)
    endif()
    # dlopen: Linux'ta libdl, BSD'lerde libc içinde (CMAKE_DL_LIBS boş); std::thread için pthread
    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} PUBLIC ${CMAKE_DL_LIBS} Threads::Threads)
    # 32 bit x86: 8 / 16 baytlık std::atomic işlemleri libatomic'te (__atomic_load ...)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(i[3-6]86|x86)$")
        target_link_libraries(${PROJECT_NAME} PUBLIC atomic)
    endif()
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

# Copy AI .env file to binary output directory
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/Core/AI/.env")
    file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/src/Core/AI/.env" DESTINATION "${CMAKE_BINARY_DIR}/bin")
endif()

cq_apply_sanitizers(${PROJECT_NAME})
