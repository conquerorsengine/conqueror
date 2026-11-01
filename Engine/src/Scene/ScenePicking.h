#pragma once

#include "Core/Base/Base.h"
#include "Entity.h"

#include <glm/glm.hpp>

namespace Conqueror
{
    class Scene;

    struct PickResult
    {
        Entity Hit;
        float Distance = 0.0f;
        glm::vec3 Point{ 0.0f };
    };

    // Görsel sınırlar üzerinden ışın testi (editör seçimi, odaklama, oyun içi fare sorguları).
    // Mesh/model için gerçek vertex sınırları (önbellekli), sprite için doku boyutu,
    // geometrisi olmayan nesneler (ışık, kamera, ses) için küçük seçim küresi kullanılır.
    class CQ_API ScenePicking
    {
    public:
        static bool Raycast(Scene& scene, const glm::vec3& origin, const glm::vec3& direction, PickResult& out,
                            bool includeIconOnlyEntities = true);
        // Varlığın yerel uzaydaki sınırları (yoksa false)
        static bool GetLocalBounds(Entity entity, glm::vec3& outMin, glm::vec3& outMax);
        // Dünya uzayında sınırlar (dönüşüm uygulanmış AABB)
        static bool GetWorldBounds(Entity entity, glm::vec3& outMin, glm::vec3& outMax);
    };
}
