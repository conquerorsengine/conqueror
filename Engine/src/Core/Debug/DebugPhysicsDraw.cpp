#include "DebugPhysicsDraw.h"
#include "DebugDraw.h"
#include "DebugPalette.h"
#include "DebugTimer.h"
#include "DebugSettings.h"
#include "DebugShapeDraw.h"
#include "DebugGizmoDraw.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

namespace Conqueror
{
    void DebugPhysicsDraw::AABB(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color, float duration, bool depthTested)
    {
        if (!DebugSettings::Get().IsCategoryActive(DebugCategory::Physics))
            return;

        glm::vec3 center = (min + max) * 0.5f;
        glm::vec3 size = max - min;

        DebugTimer::Submit(duration, depthTested, DebugCategory::Physics, [=]()
        {
            DebugDraw::Box(center, size, color, 0.0f, depthTested);
        });
    }

    void DebugPhysicsDraw::AABB(const glm::vec3& center, const glm::vec3& halfExtents, bool centerMode, const glm::vec4& color, float duration, bool depthTested)
    {
        if (centerMode)
            AABB(center - halfExtents, center + halfExtents, color, duration, depthTested);
        else
            AABB(center, center + halfExtents * 2.0f, color, duration, depthTested);
    }

    void DebugPhysicsDraw::OBB(const glm::mat4& transform, const glm::vec3& halfExtents, const glm::vec4& color, float duration, bool depthTested)
    {
        if (!DebugSettings::Get().IsCategoryActive(DebugCategory::Physics))
            return;

        glm::mat4 scaled = transform * glm::scale(glm::mat4(1.0f), halfExtents * 2.0f);
        DebugTimer::Submit(duration, depthTested, DebugCategory::Physics, [=]()
        {
            DebugDraw::Box(scaled, color, 0.0f, depthTested);
        });
    }

    void DebugPhysicsDraw::ContactPoint(const glm::vec3& point, const glm::vec3& normal, float size, const glm::vec4& color, float duration, bool depthTested)
    {
        if (!DebugSettings::Get().IsCategoryActive(DebugCategory::Physics))
            return;

        DebugTimer::Submit(duration, depthTested, DebugCategory::Physics, [=]()
        {
            DebugShapeDraw::Point(point, size * 0.5f, color, 0.0f, depthTested);
            DebugDraw::Ray(point, normal, size, DebugPalette::NormalColor, 0.0f, depthTested);
        });
    }

    void DebugPhysicsDraw::ContactManifold(const std::vector<glm::vec3>& points, const glm::vec3& normal, const glm::vec4& color, float duration, bool depthTested)
    {
        if (!DebugSettings::Get().IsCategoryActive(DebugCategory::Physics))
            return;

        std::vector<glm::vec3> copied = points;
        DebugTimer::Submit(duration, depthTested, DebugCategory::Physics, [=]()
        {
            for (const auto& point : copied)
                DebugShapeDraw::Point(point, 0.05f, color, 0.0f, depthTested);

            if (!copied.empty())
            {
                glm::vec3 center(0.0f);
                for (const auto& point : copied)
                    center += point;
                center /= static_cast<float>(copied.size());
                DebugDraw::Ray(center, normal, 0.5f, DebugPalette::NormalColor, 0.0f, depthTested);
            }
        });
    }

    void DebugPhysicsDraw::Velocity(const glm::vec3& origin, const glm::vec3& velocity, float scale, const glm::vec4& color, float duration, bool depthTested)
    {
        if (!DebugSettings::Get().IsCategoryActive(DebugCategory::Physics))
            return;

        DebugTimer::Submit(duration, depthTested, DebugCategory::Physics, [=]()
        {
            DebugDraw::Ray(origin, velocity, glm::length(velocity) * scale, color, 0.0f, depthTested);
        });
    }

    void DebugPhysicsDraw::Force(const glm::vec3& origin, const glm::vec3& force, float scale, const glm::vec4& color, float duration, bool depthTested)
    {
        Velocity(origin, force, scale, color, duration, depthTested);
    }

    void DebugPhysicsDraw::AngularVelocity(const glm::vec3& center, const glm::vec3& axis, float magnitude, float radius, const glm::vec4& color, float duration, bool depthTested)
    {
        if (!DebugSettings::Get().IsCategoryActive(DebugCategory::Physics))
            return;

        DebugTimer::Submit(duration, depthTested, DebugCategory::Physics, [=]()
        {
            DebugGizmoDraw::RotationRing(center, axis, radius, color, 0.0f, depthTested);
            DebugDraw::Ray(center, axis, magnitude, color, 0.0f, depthTested);
        });
    }

    void DebugPhysicsDraw::CenterOfMass(const glm::vec3& position, float radius, const glm::vec4& color, float duration, bool depthTested)
    {
        if (!DebugSettings::Get().IsCategoryActive(DebugCategory::Physics))
            return;

        DebugTimer::Submit(duration, depthTested, DebugCategory::Physics, [=]()
        {
            DebugDraw::Sphere(position, radius, color, 0.0f, depthTested);
            DebugGizmoDraw::Cross(position, radius * 2.0f, color, 0.0f, depthTested);
        });
    }

    void DebugPhysicsDraw::InertiaTensor(const glm::mat4& transform, const glm::vec3& diagonal, const glm::vec4& color, float duration, bool depthTested)
    {
        if (!DebugSettings::Get().IsCategoryActive(DebugCategory::Physics))
            return;

        glm::mat4 scaled = transform * glm::scale(glm::mat4(1.0f), diagonal);
        DebugTimer::Submit(duration, depthTested, DebugCategory::Physics, [=]()
        {
            DebugDraw::Box(scaled, DebugPalette::WithAlpha(color, 0.65f), 0.0f, depthTested);
            DebugGizmoDraw::CoordinateAxes(transform, std::max({ diagonal.x, diagonal.y, diagonal.z }) * 0.5f, 0.0f, depthTested);
        });
    }

    void DebugPhysicsDraw::SweepAABB(const glm::vec3& min, const glm::vec3& max, const glm::vec3& displacement, const glm::vec4& color, float duration, bool depthTested)
    {
        if (!DebugSettings::Get().IsCategoryActive(DebugCategory::Physics))
            return;

        DebugTimer::Submit(duration, depthTested, DebugCategory::Physics, [=]()
        {
            AABB(min, max, color, 0.0f, depthTested);
            AABB(min + displacement, max + displacement, DebugPalette::WithAlpha(color, 0.45f), 0.0f, depthTested);

            glm::vec3 corners[4] = {
                min,
                { max.x, min.y, min.z },
                { min.x, max.y, min.z },
                { min.x, min.y, max.z }
            };

            for (int i = 0; i < 4; ++i)
                DebugDraw::Line(corners[i], corners[i] + displacement, DebugPalette::WithAlpha(color, 0.35f), 0.0f, depthTested);
        });
    }
}
