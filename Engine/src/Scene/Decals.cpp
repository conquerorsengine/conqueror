#include "Decals.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Core/Debug/DebugDraw.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Renderer/RHI/Texture.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"

#include <IconsFontAwesome6.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <vector>

namespace Conqueror
{
    namespace Decals
    {
        void Render(Scene& scene)
        {
            auto view = scene.m_Registry.view<TransformComponent, DecalProjectorComponent>();
            if (view.begin() == view.end())
                return;
            std::vector<Renderer3D::DecalDraw> draws;
            for (auto h : view)
            {
                if (!scene.IsEntityActive(h))
                    continue;
                auto& d = view.get<DecalProjectorComponent>(h);
                if (d.LoadedPath != d.Texture)
                {
                    d.LoadedPath = d.Texture;
                    d.RuntimeTexture = d.Texture.empty() ? nullptr : Texture2D::Create(SceneSerializer::ResolveAssetPath(d.Texture));
                }
                Renderer3D::DecalDraw draw;
                // Birim küp -1..1: boyutun yarısı kadar ölçek
                draw.Transform = view.get<TransformComponent>(h).WorldTransform * glm::scale(glm::mat4(1.0f), glm::max(glm::abs(d.Size), glm::vec3(1e-3f)) * 0.5f);
                draw.Texture = d.RuntimeTexture;
                draw.Color = d.Color;
                draw.Tiling = d.Tiling;
                draw.Offset = d.Offset;
                draw.AngleFadeCos = std::cos(glm::radians(std::clamp(d.AngleFade, 0.0f, 90.0f)));
                draw.Emission = d.Emission;
                draws.push_back(draw);
            }
            Renderer3D::DrawDecals(draws);
        }

        void DrawGizmo(Scene& scene, uint32_t entity)
        {
            entt::entity e = (entt::entity)entity;
            if (!scene.m_Registry.valid(e) || !scene.m_Registry.all_of<DecalProjectorComponent>(e))
                return;
            const auto& d = scene.m_Registry.get<DecalProjectorComponent>(e);
            glm::mat4 world = scene.ComputeWorldMatrix(e);
            DebugDraw::Box(world * glm::scale(glm::mat4(1.0f), glm::abs(d.Size)), glm::vec4(1.0f, 0.75f, 0.3f, 1.0f), 0.0f, false);
            // Yansıtma yönü (-Z)
            glm::vec3 c = glm::vec3(world[3]);
            glm::vec3 dir = glm::normalize(glm::vec3(world * glm::vec4(0, 0, -1, 0)));
            DebugDraw::Line(c, c + dir * std::max(std::fabs(d.Size.z) * 0.5f, 0.25f), glm::vec4(1.0f, 0.75f, 0.3f, 1.0f), 0.0f, false);
        }
    }

    void RegisterDecalComponents()
    {
        using D = DecalProjectorComponent;
        ComponentDef<D>("Decal Projector", "DecalProjectorComponent", "Rendering", ICON_FA_STAMP).Color({ 0.62f, 0.72f, 1.0f, 1.0f })
            .Describe("Projects a texture onto surfaces inside its box along local -Z (bullet holes, stains, road markings, graffiti).")
            .Field("Texture", "Texture", &D::Texture, FieldKind::Asset).Filter(".png;.jpg;.jpeg;.tga")
            .Field("Color", "Color", &D::Color, FieldKind::Color4)
            .Field("Size", "Size", &D::Size).Speed(0.01f).Tooltip("Width, height and projection depth (metres).")
            .Field("Tiling", "Tiling", &D::Tiling).Speed(0.01f)
            .Field("Offset", "Offset", &D::Offset).Speed(0.01f)
            .Field("AngleFade", "Angle Fade", &D::AngleFade, FieldKind::Angle).Range(0.0f, 90.0f, 0.5f).Format("%.1f°")
                .Tooltip("Surfaces turned further than this from the projection direction fade out.")
            .Field("Emission", "Emission", &D::Emission).Range(0.0f, 100.0f, 0.05f);
    }
}
