#include "LightProbeGroup.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Core/Debug/DebugDraw.h"

#include <IconsFontAwesome6.h>

#include <algorithm>

namespace Conqueror
{
    namespace LightProbeGroups
    {
        void CollectWorldProbes(Scene& scene, std::vector<glm::vec3>& out)
        {
            auto view = scene.m_Registry.view<TransformComponent, LightProbeGroupComponent>();
            for (auto e : view)
            {
                if (!scene.IsEntityActive(e))
                    continue;
                const glm::mat4 w = scene.ComputeWorldMatrix(e);
                for (const glm::vec3& p : view.get<LightProbeGroupComponent>(e).Probes)
                    out.push_back(glm::vec3(w * glm::vec4(p, 1.0f)));
            }
        }

        bool Any(Scene& scene)
        {
            auto view = scene.m_Registry.view<LightProbeGroupComponent>();
            for (auto e : view)
                if (scene.IsEntityActive(e) && !view.get<LightProbeGroupComponent>(e).Probes.empty())
                    return true;
            return false;
        }

        void DrawGizmo(Scene& scene, uint32_t entity)
        {
            const entt::entity e = (entt::entity)entity;
            auto& reg = scene.m_Registry;
            if (!reg.valid(e) || !reg.all_of<LightProbeGroupComponent, TransformComponent>(e))
                return;
            const glm::mat4 w = scene.ComputeWorldMatrix(e);
            const auto& probes = reg.get<LightProbeGroupComponent>(e).Probes;
            std::vector<glm::vec3> wp;
            wp.reserve(probes.size());
            for (const glm::vec3& p : probes)
                wp.push_back(glm::vec3(w * glm::vec4(p, 1.0f)));
            const glm::vec4 probeCol(1.0f, 0.86f, 0.25f, 1.0f), linkCol(1.0f, 0.86f, 0.25f, 0.45f);
            for (size_t i = 0; i < wp.size(); ++i)
            {
                DebugDraw::Sphere(wp[i], 0.08f, probeCol, 0.0f, false);
                // En yakın 3 komşuya çizgi (Unity'deki prob ağı görünümü)
                std::vector<std::pair<float, size_t>> near;
                for (size_t j = 0; j < wp.size(); ++j)
                    if (j != i)
                        near.push_back({ glm::distance(wp[i], wp[j]), j });
                const size_t k = std::min<size_t>(3, near.size());
                std::partial_sort(near.begin(), near.begin() + k, near.end());
                for (size_t n = 0; n < k; ++n)
                    if (near[n].second > i)
                        DebugDraw::Line(wp[i], wp[near[n].second], linkCol, 0.0f, false);
            }
        }
    }

    template<> void Scene::OnComponentAdded<LightProbeGroupComponent>(Entity, LightProbeGroupComponent&) {}

    void RegisterLightProbeGroupComponents()
    {
        using L = LightProbeGroupComponent;
        ComponentDef<L>("Light Probe Group", "LightProbeGroupComponent", "Lighting", ICON_FA_CIRCLE_NODES).Color({ 1.0f, 0.86f, 0.25f, 1.0f })
            .Describe("Hand-placed light probes. Generate Lighting bakes the indirect light at each probe; moving objects take their ambient "
                      "light from the nearest probes. Place probes where the lighting changes (doorways, shadows, under bridges).")
            .Field("Probes", "Probe Positions", &L::Probes).Speed(0.05f).Tooltip("Local positions of the probes.")
            .Action("Reset To Cube", [](Entity, L& c) { c = L{}; });
    }
}
