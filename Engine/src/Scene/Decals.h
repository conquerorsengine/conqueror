#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>

// Unity URP/HDRP Decal Projector: kutu hacmindeki yüzeylere doku yansıtır (kurşun izi, leke, yol çizgisi, grafiti).
namespace Conqueror
{
    class Scene;
    class Texture2D;

    struct DecalProjectorComponent
    {
        std::string Texture;                       // albedo + alfa
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f }; // çarpan; alfa = opaklık
        glm::vec3 Size{ 1.0f, 1.0f, 1.0f };        // genişlik, yükseklik, yansıtma derinliği (yerel -Z)
        glm::vec2 Tiling{ 1.0f, 1.0f };
        glm::vec2 Offset{ 0.0f, 0.0f };
        float AngleFade = 80.0f;                   // derece: daha dik yüzeylerde solar
        float Emission = 0.0f;
        // Çalışma zamanı
        std::shared_ptr<Texture2D> RuntimeTexture;
        std::string LoadedPath;
    };

    namespace Decals
    {
        // Opak geometriden sonra (RenderScene3D) çağrılır
        CQ_API void Render(Scene& scene);
        CQ_API void DrawGizmo(Scene& scene, uint32_t entity);
    }

    void RegisterDecalComponents();
}
