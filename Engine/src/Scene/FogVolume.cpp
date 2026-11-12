#include "FogVolume.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"

#include <IconsFontAwesome6.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

namespace Conqueror
{
    namespace
    {
        std::vector<FogVolumes::GPUVolume>& Frame()
        {
            static auto& v = *new std::vector<FogVolumes::GPUVolume>();
            return v;
        }
    }

    namespace FogVolumes
    {
        const std::vector<GPUVolume>& GetFrameVolumes() { return Frame(); }

        void Collect(Scene& scene)
        {
            auto& out = Frame();
            out.clear();
            auto view = scene.m_Registry.view<TransformComponent, FogVolumeComponent>();
            for (auto e : view)
            {
                if ((int)out.size() >= MaxVolumes)
                    break;
                if (!scene.IsEntityActive(e))
                    continue;
                const auto& fv = view.get<FogVolumeComponent>(e);
                if (std::abs(fv.Density) < 1e-6f && glm::dot(fv.Emission, fv.Emission) <= 0.0f)
                    continue;
                // Birim küpe (yarım boyut 1) eşleyen ters dönüşüm
                const glm::vec3 half = glm::max(glm::abs(fv.Size) * 0.5f, glm::vec3(1e-3f));
                const glm::mat4 world = scene.ComputeWorldMatrix(e) * glm::scale(glm::mat4(1.0f), half);
                GPUVolume g;
                g.WorldToUnit = glm::inverse(world);
                g.Params = glm::vec4((float)fv.Shape, fv.Density, std::max(fv.HeightFalloff, 0.0f) * half.y, std::clamp(fv.EdgeFade, 0.0f, 1.0f));
                g.Albedo = glm::vec4(glm::max(fv.Albedo, glm::vec3(0.0f)), 1.0f);
                g.Emission = glm::vec4(glm::max(fv.Emission, glm::vec3(0.0f)), 0.0f);
                out.push_back(g);
            }
        }
    }

    void RegisterFogVolumeComponents()
    {
        using C = FogVolumeComponent;
        ComponentDef<C>("Fog Volume", "FogVolumeComponent", "Rendering", ICON_FA_SMOG)
            .Describe("Godot FogVolume / Unreal Local Fog Volume: adds local density to the volumetric fog (Lighting > Post > Volumetric Fog) "
                      "inside a box, ellipsoid, cylinder or cone, or everywhere. Negative density clears the fog (caves, interiors). Edge "
                      "Fade softens the border, Height Falloff thins it upwards, Albedo tints it and Emission makes it glow. Set the global "
                      "fog density to 0 to see only the volumes.")
            .Field("Shape", "Shape", &C::Shape).Options({ "Box", "Ellipsoid", "Cylinder", "Cone", "World" })
            .Field("Size", "Size", &C::Size).VisibleIf([](const C& c) { return c.Shape != FogVolumeShape::World; })
            .Field("Density", "Density", &C::Density).Range(-10.0f, 10.0f, 0.01f).Tooltip("Per unit; negative removes fog")
            .Field("Albedo", "Albedo", &C::Albedo, FieldKind::Color3)
            .Field("Emission", "Emission", &C::Emission, FieldKind::Color3)
            .Field("HeightFalloff", "Height Falloff", &C::HeightFalloff).Range(0.0f, 100.0f, 0.01f)
            .Field("EdgeFade", "Edge Fade", &C::EdgeFade).Range(0.0f, 1.0f, 0.01f);
    }
}
