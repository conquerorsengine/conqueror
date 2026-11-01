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
        static void Shutdown();
    };
}
