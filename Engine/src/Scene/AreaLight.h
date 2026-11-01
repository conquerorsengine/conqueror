#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

// Alan ışığı (Unity HDRP / Unreal Rect Light): dikdörtgen ya da disk biçimli yumuşak ışık kaynağı.
// Gerçek zamanlı: difüz için kesin çokgen form faktörü, yansıma için temsilci nokta + enerji normalizasyonu.
// Işık varlığın yerel +Z yönüne yayar (pencere, tavan paneli, TV ekranı, neon tabela).
namespace Conqueror
{
    class Scene;

    enum class AreaLightShape : int { Rectangle = 0, Disc };

    struct AreaLightComponent
    {
        AreaLightShape Shape = AreaLightShape::Rectangle;
        glm::vec3 Color{ 1.0f };
        float Intensity = 5.0f;           // yüzey parlaklığı (doğrusal)
        glm::vec2 Size{ 1.0f, 1.0f };     // genişlik, yükseklik (disk: çaplar); ölçekle çarpılır
        float Range = 15.0f;
        bool TwoSided = false;
    };

    namespace AreaLights
    {
        CQ_API void Submit(Scene& scene);
        CQ_API void DrawGizmo(Scene& scene, uint32_t entity);
    }

    void RegisterAreaLightComponents();
}
