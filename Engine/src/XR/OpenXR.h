#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>

// OpenXR (Unity XR Plug-in Management > OpenXR, Unreal OpenXR eklentisi, Godot OpenXR): gerçek gözlük. Yükleyici çalışma zamanında
// açılır (libopenxr_loader.so.1 / openxr_loader.dll); etkin çalışma zamanı (Monado, SteamVR, Meta Quest Link, WMR ...) gözlüğü
// sağlar. OpenGL bağlamı oturuma bağlanır (Wayland: EGL / XR_MNDX_egl_enable, X11: GLX / XR_KHR_opengl_enable, Windows: WGL),
// her göz için bir swapchain; sahne her göz için kendi pozu ve görüş açısıyla HDR çizilir, ton eşlenip gözlüğe gönderilir.
// Baş ve el pozları + düğmeler XR Origin / XR Controller'ı sürer (gözlük yokken XR Device Simulator çalışır).
namespace Conqueror
{
    class Scene;

    namespace OpenXR
    {
        struct HandInput
        {
            bool Active = false;            // kumanda izleniyor
            float Trigger = 0.0f, Grip = 0.0f;
            bool Primary = false, Secondary = false, Menu = false, StickClick = false;
            glm::vec2 Stick{ 0.0f };
        };

        struct Status
        {
            bool LoaderFound = false;
            bool InstanceCreated = false;
            bool SessionCreated = false;
            bool Running = false;           // oturum çalışıyor (kareler gözlüğe gidiyor)
            int SessionState = 0;           // XrSessionState
            std::string Runtime;            // çalışma zamanı adı ve sürümü
            std::string System;             // gözlüğün adı
            std::string Binding;            // EGL / GLX / WGL
            uint32_t EyeWidth = 0, EyeHeight = 0;
            uint64_t FramesSubmitted = 0;   // xrEndFrame başarılı ve katmanlı kareler
            std::string Error;
        };

        // Etkin OpenGL bağlamıyla oturum aç (oyun başlarken). Çalışma zamanı yoksa false: simülatör kalır
        CQ_API bool Start();
        CQ_API void Stop();
        CQ_API bool IsRunning();
        CQ_API const Status& GetStatus();
        // Her oyun karesi: olaylar, eylemler, kare bekleme, iki gözün çizimi ve gönderimi (GL durumu geri yüklenir)
        CQ_API void RenderFrame(Scene& scene);
        // Takip uzayında (Stage: zemin kökenli, metre)
        CQ_API bool HeadPose(glm::vec3& position, glm::quat& rotation);
        CQ_API bool HandPose(int hand, glm::vec3& position, glm::quat& rotation);   // 0 sol, 1 sağ (aim pozu)
        CQ_API HandInput Hand(int hand);
        CQ_API void Haptic(int hand, float amplitude, float seconds);
        // Proje ayarı ya da CQ_OPENXR=1 ile oyun başlarken açılsın mı
        CQ_API bool WantedAtStartup();
        // Test: bir gözün son çizilen HDR görüntüsünün ortalama doğrusal rengi
        CQ_API bool ProbeEyeMean(int eye, glm::vec3& mean);
    }
}
