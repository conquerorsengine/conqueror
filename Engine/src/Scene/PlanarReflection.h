#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>

// Planar Reflection (Unreal): bu nesnenin yukarı (+Y) yönüne dik düzlemde ayna. Her karede sahne düzleme göre aynalanmış kameradan
// ayrı bir dokuya çizilir; düzleme yakın ve ona bakan yüzeyler (zemin, su birikintisi, ayna) yansımayı ekrandan örnekler.
namespace Conqueror
{
    class Scene;
    struct RenderCamera;

    struct PlanarReflectionComponent
    {
        float ScreenPercentage = 50.0f;       // yansıma dokusunun çözünürlüğü (görünümün yüzdesi)
        float DistanceFromPlaneFadeEnd = 1.0f;   // m: düzlemden bu kadar uzak yüzeyler yansıma almaz
        float NormalDistortionStrength = 0.03f;
        float MaxRoughness = 0.6f;            // daha pürüzlü yüzeyler gökyüzü yansımasında kalır
        // Bilgi
        int Renders = 0;
    };

    namespace PlanarReflections
    {
        // Ana görünümden önce: en yakın etkin düzlem için aynalanmış kameradan çizim (draw = sahnenin çizim fonksiyonu)
        CQ_API bool BuildMirroredCamera(const RenderCamera& camera, const glm::vec3& planePoint, const glm::vec3& planeNormal,
                                        RenderCamera& out);
    }

    void RegisterPlanarReflectionComponents();
}
