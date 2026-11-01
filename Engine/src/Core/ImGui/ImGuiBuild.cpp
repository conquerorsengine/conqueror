// Unity build for ImGui
// GL işlevleri glad üzerinden: Vulkan arka ucunda glad göstericileri GL->Vulkan katmanına (VkGL) bağlıdır
#ifdef __ANDROID__
    // Android: ImGui'nin OpenGL ES 3 yolu (GLES3/gl3.h, libGLESv3). Motorun glad göstericileri de aynı sürücüden yüklenir.
#else
    #define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
    #include <glad/glad.h>
#endif
// X11 başlıkları olmayan sistemler (Android, yalnızca Wayland'li AGL / Yocto görüntüleri)
// (CMake, GLFW_BUILD_X11=OFF / GLFW_BUILD_WAYLAND=OFF derlemelerinde aynı tanımları verir)
#if !defined(IMGUI_IMPL_GLFW_DISABLE_X11) && (defined(__ANDROID__) || (defined(__linux__) && !__has_include(<X11/Xatom.h>)))
    #define IMGUI_IMPL_GLFW_DISABLE_X11
#endif
#if !defined(IMGUI_IMPL_GLFW_DISABLE_WAYLAND) && defined(__ANDROID__)
    #define IMGUI_IMPL_GLFW_DISABLE_WAYLAND
#endif
#include <backends/imgui_impl_opengl3.cpp>
#include <backends/imgui_impl_glfw.cpp>
