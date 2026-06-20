#pragma once

#include "Core/Base/Base.h"
#include <glm/glm.hpp>

namespace Conqueror
{
    class CQ_API DebugGizmoDraw
    {
    public:
        static void CoordinateAxes(const glm::vec3& origin, float length = 1.0f, float duration = 0.0f, bool depthTested = true);
        static void CoordinateAxes(const glm::mat4& transform, float length = 1.0f, float duration = 0.0f, bool depthTested = true);

        static void TransformGizmo(const glm::mat4& transform, float size = 1.0f, float duration = 0.0f, bool depthTested = true);
        static void RotationRing(const glm::vec3& center, const glm::vec3& axis, float radius, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void RotationRings(const glm::vec3& center, float radius, float duration = 0.0f, bool depthTested = true);

        static void LocalGrid(const glm::mat4& transform, float size, float step, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void Pivot(const glm::vec3& position, float size = 0.15f, const glm::vec4& color = glm::vec4(1.0f), float duration = 0.0f, bool depthTested = true);
        static void Cross(const glm::vec3& center, float size, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void Diamond(const glm::vec3& center, float size, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
    };
}
