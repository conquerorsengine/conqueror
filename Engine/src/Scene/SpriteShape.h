#pragma once

#include "Core/Base/Base.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

// Unity 2D SpriteShape karşılığı: noktalarla çizilen şekil (Sprite Shape Controller) + çizimi (Sprite Shape Renderer).
// Sprite Shape Profile (.cqspriteshape): kapalı şeklin içini döşeyen dolgu dokusu ve kenar boyunca tekrarlanan kenar sprite'ları;
// açı aralıkları kenarın yönüne göre farklı sprite seçer (üstte çimen, yanda kaya). İsteğe bağlı olarak aynı nesnedeki
// Edge Collider 2D / Polygon Collider 2D şeklin hattına güncellenir.
namespace Conqueror
{
    class Scene;

    struct SpriteShapeControllerComponent
    {
        std::string Profile;                  // .cqspriteshape
        std::vector<glm::vec2> Points{ { -2.0f, -1.0f }, { 2.0f, -1.0f }, { 2.0f, 1.0f }, { -2.0f, 1.0f } };
        bool Closed = true;                   // kapalı şekil (dolgu); açıksa yalnız kenar
        bool Smooth = false;                  // noktalar arası eğri (Catmull-Rom)
        int Detail = 8;                       // eğride nokta arası parça sayısı
        bool UpdateCollider = true;           // Edge / Polygon Collider 2D noktalarını şekle uydur

        // Çalışma zamanı önbelleği
        size_t CacheHash = 0;
        size_t ColliderHash = 0;
        std::vector<glm::vec2> Outline;       // yerel hat (eğri örneklenmiş)
        std::vector<uint32_t> FillTriangles;
        float Area = 0.0f;                    // işaretli alan (> 0: saat yönünün tersi)
    };

    struct SpriteShapeRendererComponent
    {
        glm::vec4 Color{ 1.0f };
    };

    namespace SpriteShapes
    {
        // Çarpıştırıcı eşitleme (düzenleyicide ve oyunda)
        CQ_API void Update(Scene& scene);
        CQ_API void Draw(Scene& scene, entt::entity entity);
        CQ_API void DrawGizmo(Scene& scene, uint32_t entity);
        // Hattı (gerekiyorsa yeniden) hesaplar
        CQ_API const std::vector<glm::vec2>& Outline(SpriteShapeControllerComponent& shape);
    }

    void RegisterSpriteShapeComponents();
}
