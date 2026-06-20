#pragma once

#include "Core/Base/Base.h"
#include <glm/glm.hpp>

namespace Conqueror
{
    class CQ_API DebugGridDraw
    {
    public:
        static void XY(float size, float step, const glm::vec4& minorColor, const glm::vec4& majorColor, int majorEvery = 5, float duration = 0.0f, bool depthTested = true);
        static void XZ(float size, float step, const glm::vec4& minorColor, const glm::vec4& majorColor, int majorEvery = 5, float duration = 0.0f, bool depthTested = true);
        static void YZ(float size, float step, const glm::vec4& minorColor, const glm::vec4& majorColor, int majorEvery = 5, float duration = 0.0f, bool depthTested = true);

        static void InfiniteXZ(const glm::vec3& origin, float step, int lineCount, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void InfiniteXY(const glm::vec3& origin, float step, int lineCount, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void PolarXZ(const glm::vec3& center, float maxRadius, float radiusStep, int segments, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
    };
}
