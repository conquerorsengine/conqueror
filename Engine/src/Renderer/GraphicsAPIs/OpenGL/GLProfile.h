#pragma once

#include "Core/Base/Base.h"

#include <string>

// OpenGL profil seçimi ve uyumluluk katmanı. Motor OpenGL 4.5 (DSA) API'siyle yazılmıştır; daha eski masaüstü
// bağlamlarında (3.3 / 4.1 core, macOS) ve OpenGL ES 3.x'te (Android, AGL, WebGL2'nin temeli) eksik işlevler
// bağla-düzenle (bind-to-edit) taklitleriyle doldurulur, gölgelendiriciler hedef GLSL sürümüne çevrilir.
//   Seçim: --gl=4.6 | 4.5 | 4.1 | 3.3 | es | es3.2 | es3.1 | es3.0 | webgl2   ya da CQ_GL_PROFILE ortam değişkeni
namespace Conqueror::GLProfile
{
    enum class Kind : int { Auto = 0, GL46, GL45, GL41, GL33, GLES32, GLES31, GLES30 };

    struct Info
    {
        bool ES = false;
        int Major = 4, Minor = 6;
        bool HasDSA = true;                 // 4.5+ masaüstü
        int GLSLVersion = 450;              // gölgelendirici hedefi
        bool GLSLES = false;
        bool UniformBindingLayouts = true;  // layout(binding=N) yazılabilir (420+, ES 3.1+, ya da 420pack)
        bool Use420Pack = false;            // 3.3 / 4.1'de GL_ARB_shading_language_420pack ile
        bool UniformInitializers = true;    // ES'de uniform başlangıç değeri yazılamaz
        bool MultisampleTextures = true;    // ES 3.0'da yok
        bool PolygonMode = true;            // ES'de tel kafes yok
        bool TimerQueries = true;
        std::string Description;            // "OpenGL ES 3.2 (Mesa ...)"
    };

    CQ_API void SetRequested(Kind kind);
    CQ_API Kind GetRequested();
    // "4.1", "es3.2", "webgl2" ...
    CQ_API bool Parse(const std::string& text, Kind& out);
    CQ_API const char* KindName(Kind kind);
    // Komut satırı (--gl=X / --gl X) > CQ_GL_PROFILE > Auto
    CQ_API Kind Resolve(int argc, char** argv);

    // Pencere oluşturulmadan önce GLFW ipuçları (glfwDefaultWindowHints sonrasında)
    CQ_API void ApplyWindowHints();
    // Bağlam güncel + glad yüklendikten sonra: sürümü algıla, eksik işlevleri yükle / taklit et
    CQ_API void OnContextCreated(void* (*getProc)(const char* name));
    CQ_API const Info& Current();
    // ImGui OpenGL3 arka ucu için "#version ..." dizesi
    CQ_API const char* ImGuiGLSLVersion();
}
