#pragma once

#include "Core/Base/Base.h"
#include <glm/glm.hpp>

namespace Conqueror
{
    class CQ_API DebugCameraDraw
    {
    public:
        static void Frustum(const glm::mat4& inverseViewProjection, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void FrustumFromMatrices(const glm::mat4& view, const glm::mat4& projection, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);

        static void OrthographicBounds(const glm::vec3& center, float width, float height, float depth, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void ViewportRect(float x, float y, float width, float height, float depth, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);

        static void LookDirection(const glm::vec3& origin, const glm::vec3& direction, float length, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void CameraPath(const glm::vec3* points, uint32_t count, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
    };
}
