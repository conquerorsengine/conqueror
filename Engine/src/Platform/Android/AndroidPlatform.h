#pragma once

// Android platform katmanı: NativeActivity (android_native_app_glue) + EGL üzerinde motorun kullandığı GLFW alt kümesi.
// Motor kodu (pencere, girdi, OpenGL bağlamı) masaüstündeki gibi GLFW çağırır; bu katman o çağrıları Android'e taşır.
#ifdef __ANDROID__

#include "Core/Base/Base.h"

#include <cstdint>
#include <string>

struct android_app;
struct AInputEvent;

namespace Conqueror::Android
{
    // android_main ilk iş olarak çağırır
    CQ_API void SetApp(android_app* app);
    CQ_API android_app* GetApp();
    // Uygulamanın yazılabilir dosya dizini (internalDataPath)
    CQ_API std::string FilesDirectory();
    // APK assets/ içeriğini (cq_files.txt listesi) hedef dizine çıkarır; sürüm değişmediyse atlar
    CQ_API bool ExtractAssets(const std::string& destination);
    // ImGui gibi ek girdi tüketicileri (ilk tüketen true döndürür)
    CQ_API void SetInputForwarder(int32_t (*forwarder)(const AInputEvent* event));
    // Kapanış istendi mi (APP_CMD_DESTROY)
    CQ_API bool DestroyRequested();
    // Sistem arayüzü (Android Automotive araç çubukları) yüzeyin bir kısmını örtüyorsa oyunun çizileceği alan, OpenGL
    // görünüm biçiminde (sol-alt köken). Örtme yoksa false (tüm yüzey).
    CQ_API bool OutputViewport(int& x, int& y, int& width, int& height);
}

#endif
