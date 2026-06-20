#include "DebugShapeDraw.h"
#include "DebugDraw.h"
#include "DebugTimer.h"
#include "DebugSettings.h"

#include <cmath>

namespace Conqueror
{
    static void BuildCircleBasis(const glm::vec3& normal, glm::vec3& tangent, glm::vec3& bitangent)
    {
        glm::vec3 n = glm::normalize(normal);
        tangent = glm::abs(n.y) < 0.99f ? glm::cross(n, glm::vec3(0, 1, 0)) : glm::cross(n, glm::vec3(1, 0, 0));
        tangent = glm::normalize(tangent);
        bitangent = glm::normalize(glm::cross(n, tangent));
    }

    void DebugShapeDraw::Circle(const glm::vec3& center, const glm::vec3& normal, float radius, const glm::vec4& color, float duration, bool depthTested)
    {
        if (!DebugSettings::Get().IsCategoryActive(DebugCategory::Path))
            return;

        DebugTimer::Submit(duration, depthTested, DebugCategory::Path, [=]()
        {
            glm::vec3 tangent, bitangent;
            BuildCircleBasis(normal, tangent, bitangent);

            int segments = static_cast<int>(DebugSettings::Get().CircleSegments);
            float step = 2.0f * 3.14159265359f / static_cast<float>(segments);
            glm::vec3 prev = center + tangent * radius;

            for (int i = 1; i <= segments; ++i)
            {
                float angle = step * static_cast<float>(i);
                glm::vec3 point = center + (tangent * cosf(angle) + bitangent * sinf(angle)) * radius;
                DebugDraw::Line(prev, point, color, 0.0f, depthTested);
                prev = point;
            }
        });
    }

    void DebugShapeDraw::Arc(const glm::vec3& center, const glm::vec3& normal, float radius, float startAngle, float endAngle, const glm::vec4& color, float duration, bool depthTested)
    {
        if (!DebugSettings::Get().IsCategoryActive(DebugCategory::Path))
            return;

        DebugTimer::Submit(duration, depthTested, DebugCategory::Path, [=]()
        {
            glm::vec3 tangent, bitangent;
            BuildCircleBasis(normal, tangent, bitangent);

            int segments = static_cast<int>(DebugSettings::Get().CircleSegments);
            float span = endAngle - startAngle;
            float step = span / static_cast<float>(segments);
            glm::vec3 prev = center + (tangent * cosf(startAngle) + bitangent * sinf(startAngle)) * radius;

            for (int i = 1; i <= segments; ++i)
            {
                float angle = startAngle + step * static_cast<float>(i);
                glm::vec3 point = center + (tangent * cosf(angle) + bitangent * sinf(angle)) * radius;
                DebugDraw::Line(prev, point, color, 0.0f, depthTested);
                prev = point;
            }
        });
    }

    void DebugShapeDraw::Cylinder(const glm::vec3& start, const glm::vec3& end, float radius, const glm::vec4& color, float duration, bool depthTested)
    {
        if (!DebugSettings::Get().IsCategoryActive(DebugCategory::Wireframe))
            return;

        DebugTimer::Submit(duration, depthTested, DebugCategory::Wireframe, [=]()
        {
            glm::vec3 axis = end - start;
            float height = glm::length(axis);
            if (height < 0.0001f)
                return;

            glm::vec3 normal = axis / height;
            glm::vec3 tangent, bitangent;
            BuildCircleBasis(normal, tangent, bitangent);

            int segments = static_cast<int>(DebugSettings::Get().CircleSegments);
            float step = 2.0f * 3.14159265359f / static_cast<float>(segments);

            glm::vec3 prevStart = start + tangent * radius;
            glm::vec3 prevEnd = end + tangent * radius;

            for (int i = 1; i <= segments; ++i)
            {
                float angle = step * static_cast<float>(i);
                glm::vec3 offset = (tangent * cosf(angle) + bitangent * sinf(angle)) * radius;
                glm::vec3 currentStart = start + offset;
                glm::vec3 currentEnd = end + offset;

                DebugDraw::Line(prevStart, currentStart, color, 0.0f, depthTested);
                DebugDraw::Line(currentStart, currentEnd, color, 0.0f, depthTested);
                prevStart = currentStart;
                prevEnd = currentEnd;
                (void)prevEnd;
            }
        });
    }

    void DebugShapeDraw::Cone(const glm::vec3& tip, const glm::vec3& direction, float height, float radius, const glm::vec4& color, float duration, bool depthTested)
    {
        if (!DebugSettings::Get().IsCategoryActive(DebugCategory::Wireframe))
            return;

        DebugTimer::Submit(duration, depthTested, DebugCategory::Wireframe, [=]()
        {
            glm::vec3 dir = glm::normalize(direction);
            glm::vec3 baseCenter = tip - dir * height;
            glm::vec3 tangent, bitangent;
            BuildCircleBasis(dir, tangent, bitangent);

            int segments = static_cast<int>(DebugSettings::Get().CircleSegments);
            float step = 2.0f * 3.14159265359f / static_cast<float>(segments);
            glm::vec3 prev = baseCenter + tangent * radius;

            for (int i = 1; i <= segments; ++i)
            {
                float angle = step * static_cast<float>(i);
                glm::vec3 point = baseCenter + (tangent * cosf(angle) + bitangent * sinf(angle)) * radius;
                DebugDraw::Line(tip, point, color, 0.0f, depthTested);
                DebugDraw::Line(prev, point, color, 0.0f, depthTested);
                prev = point;
            }
        });
    }

    void DebugShapeDraw::Triangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec4& color, float duration, bool depthTested)
    {
        DebugTimer::Submit(duration, depthTested, DebugCategory::Wireframe, [=]()
        {
            DebugDraw::Line(a, b, color, 0.0f, depthTested);
            DebugDraw::Line(b, c, color, 0.0f, depthTested);
            DebugDraw::Line(c, a, color, 0.0f, depthTested);
        });
    }

    void DebugShapeDraw::Quad(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d, const glm::vec4& color, float duration, bool depthTested)
    {
        DebugTimer::Submit(duration, depthTested, DebugCategory::Wireframe, [=]()
        {
            DebugDraw::Line(a, b, color, 0.0f, depthTested);
            DebugDraw::Line(b, c, color, 0.0f, depthTested);
            DebugDraw::Line(c, d, color, 0.0f, depthTested);
            DebugDraw::Line(d, a, color, 0.0f, depthTested);
        });
    }

    void DebugShapeDraw::Polygon(const std::vector<glm::vec3>& points, const glm::vec4& color, bool closed, float duration, bool depthTested)
    {
        if (points.size() < 2)
            return;

        std::vector<glm::vec3> copied = points;
        DebugTimer::Submit(duration, depthTested, DebugCategory::Path, [=]()
        {
            for (size_t i = 1; i < copied.size(); ++i)
                DebugDraw::Line(copied[i - 1], copied[i], color, 0.0f, depthTested);
            if (closed && copied.size() > 2)
                DebugDraw::Line(copied.back(), copied.front(), color, 0.0f, depthTested);
        });
    }

    void DebugShapeDraw::Path(const std::vector<glm::vec3>& points, const glm::vec4& color, float duration, bool depthTested)
    {
        Polygon(points, color, false, duration, depthTested);
    }

    void DebugShapeDraw::Bezier(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, const glm::vec4& color, int segments, float duration, bool depthTested)
    {
        if (!DebugSettings::Get().IsCategoryActive(DebugCategory::Path))
            return;

        DebugTimer::Submit(duration, depthTested, DebugCategory::Path, [=]()
        {
            glm::vec3 prev = p0;
            for (int i = 1; i <= segments; ++i)
            {
                float t = static_cast<float>(i) / static_cast<float>(segments);
                float u = 1.0f - t;
                glm::vec3 point =
                    u * u * u * p0 +
                    3.0f * u * u * t * p1 +
                    3.0f * u * t * t * p2 +
                    t * t * t * p3;
                DebugDraw::Line(prev, point, color, 0.0f, depthTested);
                prev = point;
            }
        });
    }

    void DebugShapeDraw::BezierChain(const std::vector<glm::vec3>& controlPoints, const glm::vec4& color, int segmentsPerCurve, float duration, bool depthTested)
    {
        if (controlPoints.size() < 4)
            return;

        for (size_t i = 0; i + 3 < controlPoints.size(); i += 3)
        {
            Bezier(controlPoints[i], controlPoints[i + 1], controlPoints[i + 2], controlPoints[i + 3], color, segmentsPerCurve, duration, depthTested);
        }
    }

    void DebugShapeDraw::Point(const glm::vec3& position, float size, const glm::vec4& color, float duration, bool depthTested)
    {
        DebugTimer::Submit(duration, depthTested, DebugCategory::Wireframe, [=]()
        {
            DebugDraw::Line(position - glm::vec3(size, 0, 0), position + glm::vec3(size, 0, 0), color, 0.0f, depthTested);
            DebugDraw::Line(position - glm::vec3(0, size, 0), position + glm::vec3(0, size, 0), color, 0.0f, depthTested);
            DebugDraw::Line(position - glm::vec3(0, 0, size), position + glm::vec3(0, 0, size), color, 0.0f, depthTested);
        });
    }

    void DebugShapeDraw::Spiral(const glm::vec3& center, const glm::vec3& axis, float radius, float height, int turns, const glm::vec4& color, float duration, bool depthTested)
    {
        if (!DebugSettings::Get().IsCategoryActive(DebugCategory::Path))
            return;

        DebugTimer::Submit(duration, depthTested, DebugCategory::Path, [=]()
        {
            glm::vec3 tangent, bitangent;
            BuildCircleBasis(axis, tangent, bitangent);

            int segments = turns * static_cast<int>(DebugSettings::Get().CircleSegments);
            glm::vec3 prev = center + tangent * radius;

            for (int i = 1; i <= segments; ++i)
            {
                float t = static_cast<float>(i) / static_cast<float>(segments);
                float angle = t * static_cast<float>(turns) * 2.0f * 3.14159265359f;
                glm::vec3 point = center
                    + (tangent * cosf(angle) + bitangent * sinf(angle)) * radius
                    + glm::normalize(axis) * (height * t);
                DebugDraw::Line(prev, point, color, 0.0f, depthTested);
                prev = point;
            }
        });
    }

    void DebugShapeDraw::Torus(const glm::vec3& center, const glm::vec3& normal, float majorRadius, float minorRadius, const glm::vec4& color, float duration, bool depthTested)
    {
        if (!DebugSettings::Get().IsCategoryActive(DebugCategory::Wireframe))
            return;

        DebugTimer::Submit(duration, depthTested, DebugCategory::Wireframe, [=]()
        {
            glm::vec3 tangent, bitangent;
            BuildCircleBasis(normal, tangent, bitangent);

            int majorSegments = static_cast<int>(DebugSettings::Get().CircleSegments);
            int minorSegments = majorSegments / 2;
            float majorStep = 2.0f * 3.14159265359f / static_cast<float>(majorSegments);
            float minorStep = 2.0f * 3.14159265359f / static_cast<float>(minorSegments);

            for (int i = 0; i < majorSegments; ++i)
            {
                float majorAngle = majorStep * static_cast<float>(i);
                glm::vec3 ringCenter = center + (tangent * cosf(majorAngle) + bitangent * sinf(majorAngle)) * majorRadius;
                glm::vec3 ringNormal = glm::normalize(tangent * cosf(majorAngle) + bitangent * sinf(majorAngle));

                glm::vec3 ringTangent, ringBitangent;
                BuildCircleBasis(ringNormal, ringTangent, ringBitangent);

                glm::vec3 prev = ringCenter + ringTangent * minorRadius;
                for (int j = 1; j <= minorSegments; ++j)
                {
                    float minorAngle = minorStep * static_cast<float>(j);
                    glm::vec3 point = ringCenter + (ringTangent * cosf(minorAngle) + ringBitangent * sinf(minorAngle)) * minorRadius;
                    DebugDraw::Line(prev, point, color, 0.0f, depthTested);
                    prev = point;
                }
            }
        });
    }
}
