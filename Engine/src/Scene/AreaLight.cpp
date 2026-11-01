#include "AreaLight.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Core/Debug/DebugDraw.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"

#include <IconsFontAwesome6.h>

#include <cmath>

namespace Conqueror
{
    namespace AreaLights
    {
        void Submit(Scene& scene)
        {
            auto view = scene.m_Registry.view<AreaLightComponent, TransformComponent>();
            for (auto e : view)
            {
                if (!scene.IsEntityActive(e))
                    continue;
                const auto& l = view.get<AreaLightComponent>(e);
                const glm::mat4& w = view.get<TransformComponent>(e).WorldTransform;
                const glm::vec3 right = glm::vec3(w[0]) * (l.Size.x * 0.5f);
                const glm::vec3 up = glm::vec3(w[1]) * (l.Size.y * 0.5f);
                Renderer3D::AddAreaLight(glm::vec3(w[3]), right, up, l.Color * std::max(l.Intensity, 0.0f), l.Range, l.TwoSided,
                                         l.Shape == AreaLightShape::Disc);
            }
        }

        void DrawGizmo(Scene& scene, uint32_t entity)
        {
            entt::entity e = (entt::entity)entity;
            auto& reg = scene.m_Registry;
            if (!reg.valid(e) || !reg.all_of<AreaLightComponent, TransformComponent>(e))
                return;
            const auto& l = reg.get<AreaLightComponent>(e);
            const glm::mat4& w = reg.get<TransformComponent>(e).WorldTransform;
            const glm::vec3 c = glm::vec3(w[3]);
            const glm::vec3 x = glm::vec3(w[0]) * (l.Size.x * 0.5f), y = glm::vec3(w[1]) * (l.Size.y * 0.5f);
            const glm::vec4 col(1.0f, 0.92f, 0.5f, 1.0f);
            if (l.Shape == AreaLightShape::Disc)
            {
                glm::vec3 prev = c + x;
                for (int k = 1; k <= 32; ++k)
                {
                    float t = (float)k / 32.0f * 6.2831853f;
                    glm::vec3 cur = c + x * std::cos(t) + y * std::sin(t);
                    DebugDraw::Line(prev, cur, col, 0.0f, false);
                    prev = cur;
                }
            }
            else
            {
                const glm::vec3 p[4] = { c - x - y, c + x - y, c + x + y, c - x + y };
                for (int i = 0; i < 4; ++i)
                    DebugDraw::Line(p[i], p[(i + 1) % 4], col, 0.0f, false);
            }
            // Yayılma yönü (+Z)
            glm::vec3 n = glm::normalize(glm::cross(glm::vec3(w[0]), glm::vec3(w[1])));
            float len = std::max(0.25f, std::max(l.Size.x, l.Size.y) * 0.5f);
            DebugDraw::Line(c, c + n * len, col, 0.0f, false);
        }
    }

    void RegisterAreaLightComponents()
    {
        using A = AreaLightComponent;
        ComponentDef<A>("Area Light", "AreaLightComponent", "Lighting", ICON_FA_TABLET_SCREEN_BUTTON).Color({ 1.0f, 0.92f, 0.5f, 1.0f })
            .Describe("Soft rectangular or disc light emitting along local +Z: windows, ceiling panels, screens, neon signs. Real-time, up to 16 per view.")
            .Field("Shape", "Shape", &A::Shape).Options({ "Rectangle", "Disc" })
            .Field("Color", "Color", &A::Color, FieldKind::Color3)
            .Field("Intensity", "Intensity", &A::Intensity).Range(0.0f, 10000.0f, 0.05f)
            .Field("Size", "Size", &A::Size).Range(0.01f, 1000.0f, 0.01f).Tooltip("Width and height in metres (multiplied by the scale).")
            .Field("Range", "Range", &A::Range).Range(0.1f, 10000.0f, 0.1f)
            .Field("TwoSided", "Two Sided", &A::TwoSided);
    }
}
