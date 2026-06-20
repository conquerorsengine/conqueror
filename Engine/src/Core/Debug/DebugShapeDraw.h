#pragma once

#include "Core/Base/Base.h"
#include <glm/glm.hpp>
#include <vector>

namespace Conqueror
{
    class CQ_API DebugShapeDraw
    {
    public:
        static void Circle(const glm::vec3& center, const glm::vec3& normal, float radius, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void Arc(const glm::vec3& center, const glm::vec3& normal, float radius, float startAngle, float endAngle, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);

        static void Cylinder(const glm::vec3& start, const glm::vec3& end, float radius, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void Cone(const glm::vec3& tip, const glm::vec3& direction, float height, float radius, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);

        static void Triangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void Quad(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void Polygon(const std::vector<glm::vec3>& points, const glm::vec4& color, bool closed = true, float duration = 0.0f, bool depthTested = true);

        static void Path(const std::vector<glm::vec3>& points, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void Bezier(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, const glm::vec4& color, int segments = 24, float duration = 0.0f, bool depthTested = true);
        static void BezierChain(const std::vector<glm::vec3>& controlPoints, const glm::vec4& color, int segmentsPerCurve = 16, float duration = 0.0f, bool depthTested = true);

        static void Point(const glm::vec3& position, float size, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void Spiral(const glm::vec3& center, const glm::vec3& axis, float radius, float height, int turns, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void Torus(const glm::vec3& center, const glm::vec3& normal, float majorRadius, float minorRadius, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
    };
}
