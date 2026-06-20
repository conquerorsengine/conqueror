#include "DebugCameraDraw.h"
#include "DebugDraw.h"
#include "DebugPalette.h"
#include "DebugTimer.h"
#include "DebugSettings.h"

#include <glm/gtc/matrix_transform.hpp>
#include <vector>

namespace Conqueror
{
    static glm::vec3 UnprojectPoint(const glm::mat4& invViewProjection, const glm::vec3& ndc)
    {
        glm::vec4 world = invViewProjection * glm::vec4(ndc, 1.0f);
        return glm::vec3(world) / world.w;
    }

    void DebugCameraDraw::Frustum(const glm::mat4& inverseViewProjection, const glm::vec4& color, float duration, bool depthTested)
    {
        if (!DebugSettings::Get().IsCategoryActive(DebugCategory::Camera))
            return;

        DebugTimer::Submit(duration, depthTested, DebugCategory::Camera, [=]()
        {
            glm::vec3 ntl = UnprojectPoint(inverseViewProjection, { -1.0f,  1.0f, -1.0f });
            glm::vec3 ntr = UnprojectPoint(inverseViewProjection, {  1.0f,  1.0f, -1.0f });
            glm::vec3 nbl = UnprojectPoint(inverseViewProjection, { -1.0f, -1.0f, -1.0f });
            glm::vec3 nbr = UnprojectPoint(inverseViewProjection, {  1.0f, -1.0f, -1.0f });
            glm::vec3 ftl = UnprojectPoint(inverseViewProjection, { -1.0f,  1.0f,  1.0f });
            glm::vec3 ftr = UnprojectPoint(inverseViewProjection, {  1.0f,  1.0f,  1.0f });
            glm::vec3 fbl = UnprojectPoint(inverseViewProjection, { -1.0f, -1.0f,  1.0f });
            glm::vec3 fbr = UnprojectPoint(inverseViewProjection, {  1.0f, -1.0f,  1.0f });

            DebugDraw::Line(ntl, ntr, color, 0.0f, depthTested);
            DebugDraw::Line(ntr, nbr, color, 0.0f, depthTested);
            DebugDraw::Line(nbr, nbl, color, 0.0f, depthTested);
            DebugDraw::Line(nbl, ntl, color, 0.0f, depthTested);

            DebugDraw::Line(ftl, ftr, color, 0.0f, depthTested);
            DebugDraw::Line(ftr, fbr, color, 0.0f, depthTested);
            DebugDraw::Line(fbr, fbl, color, 0.0f, depthTested);
            DebugDraw::Line(fbl, ftl, color, 0.0f, depthTested);

            DebugDraw::Line(ntl, ftl, color, 0.0f, depthTested);
            DebugDraw::Line(ntr, ftr, color, 0.0f, depthTested);
            DebugDraw::Line(nbl, fbl, color, 0.0f, depthTested);
            DebugDraw::Line(nbr, fbr, color, 0.0f, depthTested);
        });
    }

    void DebugCameraDraw::FrustumFromMatrices(const glm::mat4& view, const glm::mat4& projection, const glm::vec4& color, float duration, bool depthTested)
    {
        glm::mat4 viewProjection = projection * view;
        Frustum(glm::inverse(viewProjection), color, duration, depthTested);
    }

    void DebugCameraDraw::OrthographicBounds(const glm::vec3& center, float width, float height, float depth, const glm::vec4& color, float duration, bool depthTested)
    {
        if (!DebugSettings::Get().IsCategoryActive(DebugCategory::Camera))
            return;

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), center);
        transform = transform * glm::scale(glm::mat4(1.0f), glm::vec3(width, height, depth));

        DebugTimer::Submit(duration, depthTested, DebugCategory::Camera, [=]()
        {
            DebugDraw::Box(transform, color, 0.0f, depthTested);
        });
    }

    void DebugCameraDraw::ViewportRect(float x, float y, float width, float height, float depth, const glm::vec4& color, float duration, bool depthTested)
    {
        if (!DebugSettings::Get().IsCategoryActive(DebugCategory::Camera))
            return;

        DebugTimer::Submit(duration, depthTested, DebugCategory::Camera, [=]()
        {
            glm::vec3 a(x, y, depth);
            glm::vec3 b(x + width, y, depth);
            glm::vec3 c(x + width, y + height, depth);
            glm::vec3 d(x, y + height, depth);
            DebugDraw::Line(a, b, color, 0.0f, depthTested);
            DebugDraw::Line(b, c, color, 0.0f, depthTested);
            DebugDraw::Line(c, d, color, 0.0f, depthTested);
            DebugDraw::Line(d, a, color, 0.0f, depthTested);
        });
    }

    void DebugCameraDraw::LookDirection(const glm::vec3& origin, const glm::vec3& direction, float length, const glm::vec4& color, float duration, bool depthTested)
    {
        if (!DebugSettings::Get().IsCategoryActive(DebugCategory::Camera))
            return;

        DebugTimer::Submit(duration, depthTested, DebugCategory::Camera, [=]()
        {
            DebugDraw::Ray(origin, direction, length, color, 0.0f, depthTested);
        });
    }

    void DebugCameraDraw::CameraPath(const glm::vec3* points, uint32_t count, const glm::vec4& color, float duration, bool depthTested)
    {
        if (!points || count < 2 || !DebugSettings::Get().IsCategoryActive(DebugCategory::Camera))
            return;

        std::vector<glm::vec3> copied(points, points + count);
        DebugTimer::Submit(duration, depthTested, DebugCategory::Camera, [=]()
        {
            for (uint32_t i = 1; i < copied.size(); ++i)
                DebugDraw::Line(copied[i - 1], copied[i], color, 0.0f, depthTested);
        });
    }
}
