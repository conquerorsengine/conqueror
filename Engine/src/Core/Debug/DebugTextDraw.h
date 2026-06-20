#pragma once

#include "Core/Base/Base.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Conqueror
{
    struct DebugTextEntry
    {
        glm::vec3 Position;
        std::string Text;
        glm::vec4 Color;
        float Scale = 1.0f;
        bool DepthTested = false;
        bool Billboard = true;
    };

    class CQ_API DebugTextDraw
    {
    public:
        static void Init();
        static void Shutdown();

        static void Draw(const glm::vec3& position, const std::string& text, const glm::vec4& color, float scale, bool depthTested, bool billboard = true);
        static void DrawFormatted(const glm::vec3& position, const glm::vec4& color, float scale, bool depthTested, const char* format, ...);

        static void Render(const glm::mat4& viewProjection, const glm::vec3& cameraPosition);
        static void Clear();

        static uint32_t GetEntryCount();
    };
}
