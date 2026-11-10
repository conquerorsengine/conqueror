#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>

// NavMesh Agent hareketi (Unity NavMeshAgent): yol takibi, ivme / frenleme, açısal hız, zemine oturma,
// NavMesh Link geçişi (atlama yayı), ajanlar arası kaçınma, Carve engeller ve yeniden yol hesabı.
namespace Conqueror
{
    class Scene;
    class NavMesh;

    namespace NavAgents
    {
        // Oyun başında: NavMesh Surface "Bake On Play" ise (sahne değiştiyse) pişir; aynı sahne için önbellekten
        CQ_API void OnRuntimeStart(Scene& scene);
        // Editör "Bake" düğmesi / betik: pişir ve önbelleğe al
        CQ_API void Bake(Scene& scene);
        // Sahne geometrisi son pişirmeden beri değişmediyse önbellekteki ağı kullanır, değiştiyse pişirir (betikten NavBake: her
        // Play'de ~60 ms yeniden pişirme yapılmaz)
        CQ_API void BakeCached(Scene& scene);
        // Editörde sahne açılınca / kaydedilince: "Bake On Play" ağı önbelleğe önceden pişirilir (sahnenin kendi ağına dokunulmaz);
        // ilk Play'de pişirme takılması olmaz (aynı geometri)
        CQ_API void Prebake(Scene& scene);
        CQ_API void Update(Scene& scene, float dt);
        CQ_API bool SetDestination(Scene& scene, uint32_t agent, const glm::vec3& destination);
        CQ_API void Warp(Scene& scene, uint32_t agent, const glm::vec3& position);
        CQ_API void Stop(Scene& scene, uint32_t agent, bool stopped);
        // Editör: pişmiş ağı (hücreleri) yarı saydam çiz
        CQ_API void DrawDebug(const NavMesh& nav, const glm::mat4& viewProjection);
        CQ_API void Shutdown();
    }
}
