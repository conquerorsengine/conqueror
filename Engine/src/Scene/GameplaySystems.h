#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <unordered_set>

namespace Conqueror
{
    class Scene;
    struct RenderCamera;

    // Bileşen tabanlı oyun sistemleri: Rotator, Projectile Movement, kısıtlar (Look At / Aim / Position /
    // Rotation / Scale / Parent), Camera Follow, Trail kayıtları, rüzgâr bölgeleri ve LOD grupları.
    namespace GameplaySystems
    {
        // runtime=false: yalnızca düzenleyicide de çalışanlar (kısıtlar, RunInEditor döndürücüler)
        CQ_API void Update(Scene& scene, float dt, bool runtime);
        // Kare sonu (betiklerin LateUpdate'inden sonra): kamera takibi
        CQ_API void LateUpdate(Scene& scene, float dt, bool runtime);
        // Saydam geçiş: Line Renderer + Trail Renderer
        CQ_API void RenderRibbons(Scene& scene);
        // Kameraya göre LOD seçimi; gizlenecek varlıklar 'hidden' kümesine yazılır
        CQ_API void UpdateLOD(Scene& scene, const RenderCamera& camera, std::unordered_set<uint32_t>& hidden);
        // Etkin rüzgâr bölgelerinin bu noktadaki rüzgârı (ivme, m/s²)
        CQ_API bool HasWind();
        CQ_API glm::vec3 SampleWind(const glm::vec3& position);
        // Particle System Force Field: dünya konumundaki parçacığın hızını günceller
        CQ_API bool HasForceFields();
        CQ_API void ApplyForceFields(const glm::vec3& position, glm::vec3& velocity, float dt);
        // Oyun modu bitince çalışma zamanı durumunu at (izler, mermiler)
        CQ_API void ResetRuntime(Scene& scene);
        CQ_API void ForgetScene(Scene* scene);
    }
}
