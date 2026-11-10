#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>
#include <vector>

// Light Probe Group (Unity): elle yerleştirilen ışık probları. Generate Lighting her probun konumunda dolaylı ışığı (L2 küresel harmonik)
// pişirir; hareketli nesneler ortam ışığını en yakın problardan alır (Adaptive Probe Volume problarıyla aynı örnekleme yolu).
// Pişirilen problar sahne dosyasına kaydedilir, oyunda yeniden pişirmeden kullanılır.
namespace Conqueror
{
    class Scene;

    struct LightProbeGroupComponent
    {
        // Yerel konumlar (Unity'deki gibi varsayılan: 1 m'lik küpün 8 köşesi)
        std::vector<glm::vec3> Probes = {
            { -0.5f, -0.5f, -0.5f }, { 0.5f, -0.5f, -0.5f }, { -0.5f, 0.5f, -0.5f }, { 0.5f, 0.5f, -0.5f },
            { -0.5f, -0.5f, 0.5f }, { 0.5f, -0.5f, 0.5f }, { -0.5f, 0.5f, 0.5f }, { 0.5f, 0.5f, 0.5f } };
    };

    namespace LightProbeGroups
    {
        // Etkin Light Probe Group'ların tüm problarının dünya konumları
        CQ_API void CollectWorldProbes(Scene& scene, std::vector<glm::vec3>& out);
        CQ_API bool Any(Scene& scene);
        // Seçiliyken sahne görünümünde problar (sarı küreler) ve komşu bağlantıları
        CQ_API void DrawGizmo(Scene& scene, uint32_t entity);
    }

    void RegisterLightProbeGroupComponents();
}
