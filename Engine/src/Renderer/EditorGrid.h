#pragma once

#include "Core/Base/Base.h"
#include <glm/glm.hpp>

namespace Conqueror
{
    struct RenderCamera;

    struct EditorGridSettings
    {
        bool Visible = true;
        float BaseSize = 1.0f;      // en küçük hücre (snap ile eşleştirilebilir)
        float Opacity = 1.0f;
        float FadeDistance = 0.0f;  // 0 = kamera yüksekliğine göre otomatik
        // Açık renkli çizgiler: varsayılan gökyüzünün gri zemini (~0.56) üzerinde de seçilsin
        // (eski 0.55 ince çizgi zeminle aynı tondaydı ve görünmüyordu)
        glm::vec3 MinorColor = glm::vec3(0.86f);
        glm::vec3 MajorColor = glm::vec3(1.0f);
    };

    // Editör görünümleri için shader tabanlı sonsuz ızgara
    class CQ_API EditorGrid
    {
    public:
        static EditorGridSettings& Settings();
        // plane2D: XY düzlemi (2D görünüm); aksi halde XZ zemin düzlemi.
        // background: ızgara en arkada (gökyüzünün hemen önünde) çizilir; 2D görünümde sprite ve
        // modeller her zaman ızgaranın önünde görünür
        static void Render(const RenderCamera& camera, bool plane2D, bool linearOutput = true, bool background = false);
        // Zamansal kenar yumuşatmalı (TAA, Temporal Stability, zamansal yükseltici) görünüm: 3D ızgara sahneye çizilmez, kenar yumuşatma
        // ve ton eşlemeden sonra son görüntünün üstüne çizilmek üzere ertelenir (Unity Scene ızgarası / Unreal editör ilkelleri gibi).
        // Sahne hedefine çizilirken çağrılır. Zamansal yöntem yoksa false döner, ızgara eskisi gibi sahneye çizilmelidir
        static bool DeferToComposite(const RenderCamera& camera);
        // Ertelenen ızgarayı son görüntüye (dokusu colorTexture, sRGB) çizer; ertelenen yoksa bir şey yapmaz. sceneDepth: görünümün
        // (çok örneklide çözülmüş) sahne derinliği (PostProcessStack::GetSceneDepthTexture)
        static void RenderDeferred(uint32_t colorTexture, uint32_t width, uint32_t height, uint32_t sceneDepth);
        static void Shutdown();
    };
}
