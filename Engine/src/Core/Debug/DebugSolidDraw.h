#pragma once

#include "Core/Base/Base.h"
#include <glm/glm.hpp>
#include <vector>
#include <memory>

namespace Conqueror
{
    class VertexArray;
    class VertexBuffer;
    class IndexBuffer;
    class Shader;

    struct DebugSolidVertex
    {
        glm::vec3 Position;
        glm::vec4 Color;
    };

    class CQ_API DebugSolidDraw
    {
    public:
        static void Init();
        static void Shutdown();

        static void Box(const glm::mat4& transform, const glm::vec4& color, bool depthTested);
        static void Box(const glm::vec3& center, const glm::vec3& size, const glm::vec4& color, bool depthTested);
        static void Sphere(const glm::vec3& center, float radius, const glm::vec4& color, bool depthTested);
        static void Capsule(const glm::vec3& center, float radius, float height, const glm::vec4& color, bool depthTested);
        static void Triangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec4& color, bool depthTested);
        static void Quad(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d, const glm::vec4& color, bool depthTested);
        static void Plane(const glm::vec3& center, const glm::vec3& normal, float width, float height, const glm::vec4& color, bool depthTested);

        static void Render(const glm::mat4& viewProjection);
        static void Clear();

        static uint32_t GetVertexCount();
        static uint32_t GetTriangleCount();
    };
}
