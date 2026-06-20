#include "DebugGizmoDraw.h"
#include "DebugDraw.h"
#include "DebugPalette.h"
#include "DebugTimer.h"
#include "DebugSettings.h"

#include <cmath>

namespace Conqueror
{

    static void GizmoLine(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color, float duration, bool depthTested)
    {
        DebugTimer::Submit(duration, depthTested, DebugCategory::Gizmo, [=]()
        {
            DebugDraw::Line(a, b, color, 0.0f, depthTested);
        });
    }

    void DebugGizmoDraw::CoordinateAxes(const glm::vec3& origin, float length, float duration, bool depthTested)
    {
        if (!DebugSettings::Get().IsCategoryActive(DebugCategory::Gizmo))
            return;

        GizmoLine(origin, origin + glm::vec3(length, 0.0f, 0.0f), DebugPalette::XAxis, duration, depthTested);
        GizmoLine(origin, origin + glm::vec3(0.0f, length, 0.0f), DebugPalette::YAxis, duration, depthTested);
        GizmoLine(origin, origin + glm::vec3(0.0f, 0.0f, length), DebugPalette::ZAxis, duration, depthTested);
    }

    void DebugGizmoDraw::CoordinateAxes(const glm::mat4& transform, float length, float duration, bool depthTested)
    {
        glm::vec3 origin = glm::vec3(transform[3]);
        glm::vec3 xAxis = glm::normalize(glm::vec3(transform[0])) * length;
        glm::vec3 yAxis = glm::normalize(glm::vec3(transform[1])) * length;
        glm::vec3 zAxis = glm::normalize(glm::vec3(transform[2])) * length;

        GizmoLine(origin, origin + xAxis, DebugPalette::XAxis, duration, depthTested);
        GizmoLine(origin, origin + yAxis, DebugPalette::YAxis, duration, depthTested);
        GizmoLine(origin, origin + zAxis, DebugPalette::ZAxis, duration, depthTested);
    }

    void DebugGizmoDraw::TransformGizmo(const glm::mat4& transform, float size, float duration, bool depthTested)
    {
        CoordinateAxes(transform, size, duration, depthTested);
        Pivot(glm::vec3(transform[3]), size * 0.12f, DebugPalette::White, duration, depthTested);
    }

    void DebugGizmoDraw::RotationRing(const glm::vec3& center, const glm::vec3& axis, float radius, const glm::vec4& color, float duration, bool depthTested)
    {
        if (!DebugSettings::Get().IsCategoryActive(DebugCategory::Gizmo))
            return;

        glm::vec3 n = glm::normalize(axis);
        glm::vec3 tangent = glm::abs(n.y) < 0.99f ? glm::cross(n, glm::vec3(0, 1, 0)) : glm::cross(n, glm::vec3(1, 0, 0));
        tangent = glm::normalize(tangent);
        glm::vec3 bitangent = glm::normalize(glm::cross(n, tangent));

        int segments = static_cast<int>(DebugSettings::Get().CircleSegments);
        float step = 2.0f * 3.14159265359f / static_cast<float>(segments);
        glm::vec3 prev = center + tangent * radius;

        DebugTimer::Submit(duration, depthTested, DebugCategory::Gizmo, [=]()
        {
            glm::vec3 previous = prev;
            for (int i = 1; i <= segments; ++i)
            {
                float angle = step * static_cast<float>(i);
                glm::vec3 point = center + (tangent * cosf(angle) + bitangent * sinf(angle)) * radius;
                DebugDraw::Line(previous, point, color, 0.0f, depthTested);
                previous = point;
            }
        });
    }

    void DebugGizmoDraw::RotationRings(const glm::vec3& center, float radius, float duration, bool depthTested)
    {
        RotationRing(center, glm::vec3(1, 0, 0), radius, DebugPalette::XAxis, duration, depthTested);
        RotationRing(center, glm::vec3(0, 1, 0), radius, DebugPalette::YAxis, duration, depthTested);
        RotationRing(center, glm::vec3(0, 0, 1), radius, DebugPalette::ZAxis, duration, depthTested);
    }

    void DebugGizmoDraw::LocalGrid(const glm::mat4& transform, float size, float step, const glm::vec4& color, float duration, bool depthTested)
    {
        if (!DebugSettings::Get().IsCategoryActive(DebugCategory::Gizmo))
            return;

        DebugTimer::Submit(duration, depthTested, DebugCategory::Gizmo, [=]()
        {
            for (float i = -size; i <= size; i += step)
            {
                glm::vec4 p1 = transform * glm::vec4(-size, 0.0f, i, 1.0f);
                glm::vec4 p2 = transform * glm::vec4(size, 0.0f, i, 1.0f);
                glm::vec4 p3 = transform * glm::vec4(i, 0.0f, -size, 1.0f);
                glm::vec4 p4 = transform * glm::vec4(i, 0.0f, size, 1.0f);
                DebugDraw::Line(glm::vec3(p1), glm::vec3(p2), color, 0.0f, depthTested);
                DebugDraw::Line(glm::vec3(p3), glm::vec3(p4), color, 0.0f, depthTested);
            }
        });
    }

    void DebugGizmoDraw::Pivot(const glm::vec3& position, float size, const glm::vec4& color, float duration, bool depthTested)
    {
        Cross(position, size, color, duration, depthTested);
    }

    void DebugGizmoDraw::Cross(const glm::vec3& center, float size, const glm::vec4& color, float duration, bool depthTested)
    {
        if (!DebugSettings::Get().IsCategoryActive(DebugCategory::Gizmo))
            return;

        DebugTimer::Submit(duration, depthTested, DebugCategory::Gizmo, [=]()
        {
            DebugDraw::Line(center - glm::vec3(size, 0, 0), center + glm::vec3(size, 0, 0), color, 0.0f, depthTested);
            DebugDraw::Line(center - glm::vec3(0, size, 0), center + glm::vec3(0, size, 0), color, 0.0f, depthTested);
            DebugDraw::Line(center - glm::vec3(0, 0, size), center + glm::vec3(0, 0, size), color, 0.0f, depthTested);
        });
    }

    void DebugGizmoDraw::Diamond(const glm::vec3& center, float size, const glm::vec4& color, float duration, bool depthTested)
    {
        if (!DebugSettings::Get().IsCategoryActive(DebugCategory::Gizmo))
            return;

        DebugTimer::Submit(duration, depthTested, DebugCategory::Gizmo, [=]()
        {
            glm::vec3 top = center + glm::vec3(0, size, 0);
            glm::vec3 bottom = center - glm::vec3(0, size, 0);
            glm::vec3 left = center - glm::vec3(size, 0, 0);
            glm::vec3 right = center + glm::vec3(size, 0, 0);
            glm::vec3 front = center + glm::vec3(0, 0, size);
            glm::vec3 back = center - glm::vec3(0, 0, size);

            DebugDraw::Line(top, left, color, 0.0f, depthTested);
            DebugDraw::Line(top, right, color, 0.0f, depthTested);
            DebugDraw::Line(top, front, color, 0.0f, depthTested);
            DebugDraw::Line(top, back, color, 0.0f, depthTested);
            DebugDraw::Line(bottom, left, color, 0.0f, depthTested);
            DebugDraw::Line(bottom, right, color, 0.0f, depthTested);
            DebugDraw::Line(bottom, front, color, 0.0f, depthTested);
            DebugDraw::Line(bottom, back, color, 0.0f, depthTested);
        });
    }
}
