#pragma once

#include "Core/Base/Base.h"
#include <glm/glm.hpp>

namespace Conqueror
{
    namespace DebugDrawInternal
    {
        void SubmitLine(const glm::vec3& start, const glm::vec3& end, const glm::vec4& color, bool depthTested);
        void SubmitLineImmediate(const glm::vec3& start, const glm::vec3& end, const glm::vec4& color, bool depthTested);

        uint32_t GetLineVertexCount();
        uint32_t GetLineCount();
    };
}
