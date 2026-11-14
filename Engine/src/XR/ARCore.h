#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <vector>

// ARCore (Unity AR Foundation + Google ARCore XR Plugin, Unreal Google ARCore, Godot ARCore eklentisi): Android telefon ve
// tabletlerde gerçek AR. Google'ın ARCore SDK'sı (libarcore_sdk_c.so, "ARCore Additional Terms of Service") depoda değildir:
// APK paketleyici AR projelerinde Google Maven'dan indirir, motor çalışma zamanında dlopen ile açar. Kamera görüntüsü sahnenin
// arka planı olur, AR kamerası cihazın pozunu ve görüş açısını alır, düzlemler ve isabet testi betiklere açılır.
// Masaüstünde yoktur: AR şablonu orada kendi simülasyonunu (AR Simulation) kullanır.
namespace Conqueror
{
    namespace ARCore
    {
        struct Plane
        {
            uint64_t Id = 0;
            glm::vec3 Position{ 0.0f };          // merkez (ARCore dünya uzayı, metre)
            glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };   // yerel +Y = düzlem normali
            float ExtentX = 0.0f, ExtentZ = 0.0f;
            int Type = 0;                         // 0 yatay yukarı (zemin, masa), 1 yatay aşağı (tavan), 2 dikey (duvar)
            std::vector<glm::vec2> Polygon;       // yerel XZ sınırı
        };

        struct HitResult
        {
            glm::vec3 Point{ 0.0f };
            glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
            uint64_t PlaneId = 0;
            float Distance = 0.0f;
        };

        // Proje ayarı (XR Plug-in Management > ARCore) ve Android
        CQ_API bool WantedAtStartup();
        CQ_API bool Start();
        CQ_API void Stop();
        // Her kare, betiklerden önce (GL bağlamında): kare güncelleme, kamera görüntüsü, poz, düzlemler
        CQ_API void Update();
        CQ_API bool IsRunning();
        // "Unavailable", "Installing", "Waiting for camera permission", "Initializing", "Tracking", "Limited"
        CQ_API const std::string& GetTrackingState();
        CQ_API const std::string& GetError();
        CQ_API bool CameraPose(glm::vec3& position, glm::quat& rotation);
        CQ_API float CameraFovY();               // derece (fiziksel kameranın dikey görüş açısı)
        CQ_API const std::vector<Plane>& GetPlanes();
        CQ_API bool Raycast(float pixelX, float pixelY, HitResult& out);   // ekran pikseli, sol üst köken
        // Kamera görüntüsü (sRGB RGBA8, ekranın en-boy oranında kırpılmış): sahnenin arka planı; yoksa 0
        CQ_API uint32_t BackgroundTexture();
        CQ_API uint64_t FramesUpdated();
    }
}
