#pragma once

#include "Core/Base/Base.h"

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <string>

// Visual Effect (Unity VFX Graph): .cqvfx düğüm grafiği (Spawn -> Initialize Particle -> Update Particle -> Output Particle)
// GPU'da compute shader'larla simüle edilir; yüz binlerce parçacık. Çizim Particle System'in gölgelendiricisiyle (yumuşak parçacık,
// TAA tepki maskesi). Düzenleyicide de oynar.
namespace Conqueror
{
    class Scene;

    struct VisualEffectComponent
    {
        std::string Asset;                    // .cqvfx
        bool Playing = true;                  // Play / Stop (doğurma durur, var olanlar ömrünü tamamlar)
        float PlayRate = 1.0f;
        int Seed = 0;
        // Bilgi
        int Systems = 0;
        int Capacity = 0;
    };

    namespace VisualEffects
    {
        constexpr const char* Kind = "VFXGraph";
        constexpr const char* Extension = ".cqvfx";
        // Simülasyon (düzenleyicide ve oyunda her kare)
        CQ_API void Update(Scene& scene, float dt);
        CQ_API void Render(Scene& scene);
        // Parçacıkları sil, baştan başlat
        CQ_API void Reinit(Scene& scene, entt::entity entity);
        // Bu karede doğan parçacık sayısı (bilgi / test)
        CQ_API uint64_t SpawnedTotal(Scene& scene, entt::entity entity);
    }

    void RegisterVisualEffectComponents();
}
