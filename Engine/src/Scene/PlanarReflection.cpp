#include "PlanarReflection.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/ComponentRegistry.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"

#include <IconsFontAwesome6.h>

#include <glm/gtc/matrix_inverse.hpp>

#include <cmath>

namespace Conqueror
{
    namespace PlanarReflections
    {
        bool BuildMirroredCamera(const RenderCamera& cam, const glm::vec3& p0, const glm::vec3& nIn, RenderCamera& out)
        {
            const glm::vec3 n = glm::normalize(nIn);
            const float d = -glm::dot(n, p0);
            // Kamera düzlemin altındaysa yansıma yok
            if (glm::dot(n, cam.Position) + d <= 0.01f || cam.Orthographic)
                return false;
            // Yansıma matrisi: x' = x - 2 (n.x + d) n
            glm::mat4 m(1.0f);
            m[0] = glm::vec4(1.0f - 2.0f * n.x * n.x, -2.0f * n.x * n.y, -2.0f * n.x * n.z, 0.0f);
            m[1] = glm::vec4(-2.0f * n.y * n.x, 1.0f - 2.0f * n.y * n.y, -2.0f * n.y * n.z, 0.0f);
            m[2] = glm::vec4(-2.0f * n.z * n.x, -2.0f * n.z * n.y, 1.0f - 2.0f * n.z * n.z, 0.0f);
            m[3] = glm::vec4(-2.0f * d * n.x, -2.0f * d * n.y, -2.0f * d * n.z, 1.0f);
            out = cam;
            out.View = cam.View * m;
            out.Position = glm::vec3(m * glm::vec4(cam.Position, 1.0f));
            // Eğik yakın düzlem (Lengyel): düzlemin altında kalan geometri kırpılır
            glm::vec4 c = glm::inverseTranspose(out.View) * glm::vec4(n, d);
            glm::mat4 p = cam.Projection;
            const glm::vec4 q = glm::inverse(p) * glm::vec4(c.x > 0.0f ? 1.0f : -1.0f, c.y > 0.0f ? 1.0f : -1.0f, 1.0f, 1.0f);
            const float dq = glm::dot(c, q);
            if (std::abs(dq) < 1e-8f)
                return false;
            c *= 2.0f / dq;
            p[0][2] = c.x - p[0][3];
            p[1][2] = c.y - p[1][3];
            p[2][2] = c.z - p[2][3];
            p[3][2] = c.w - p[3][3];
            out.Projection = p;
            return true;
        }
    }

    template<> void Scene::OnComponentAdded<PlanarReflectionComponent>(Entity, PlanarReflectionComponent&) {}

    void RegisterPlanarReflectionComponents()
    {
        using C = PlanarReflectionComponent;
        ComponentDef<C>("Planar Reflection", "PlanarReflectionComponent", "Rendering", ICON_FA_CLONE).Color({ 0.55f, 0.8f, 1.0f, 1.0f })
            .Describe("A mirror plane through this object, facing its +Y (Unreal Planar Reflection): floors, puddles and mirrors near the plane "
                      "reflect the scene sharply. Costs one extra scene render per frame at Screen Percentage.")
            .Field("ScreenPercentage", "Screen Percentage", &C::ScreenPercentage).Range(10.0f, 100.0f, 1.0f)
            .Field("DistanceFromPlaneFadeEnd", "Distance From Plane Fade End", &C::DistanceFromPlaneFadeEnd).Range(0.01f, 100.0f, 0.01f)
            .Field("NormalDistortionStrength", "Normal Distortion Strength", &C::NormalDistortionStrength).Range(0.0f, 1.0f, 0.001f)
            .Field("MaxRoughness", "Max Roughness", &C::MaxRoughness).Range(0.01f, 1.0f, 0.01f)
            .Field("Renders", "Renders", &C::Renders).ReadOnly().Header("Info");
    }
}
