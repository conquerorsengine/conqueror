#include "DebugSolidDraw.h"
#include "DebugSettings.h"
#include "Renderer/RHI/Shader.h"
#include "Renderer/RHI/Buffer.h"
#include "Renderer/RHI/VertexArray.h"
#include "Renderer/RHI/RenderCommand.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cmath>

namespace Conqueror
{
    struct DebugSolidBatch
    {
        std::vector<DebugSolidVertex> Vertices;
        std::vector<uint32_t> Indices;
        bool DepthTested = true;
    };

    struct DebugSolidDrawData
    {
        std::vector<DebugSolidBatch> Batches;

        std::shared_ptr<VertexArray> VA;
        std::shared_ptr<VertexBuffer> VB;
        std::shared_ptr<IndexBuffer> IB;
        std::shared_ptr<Shader> SolidShader;

        uint32_t MaxVertices = 100000;
        uint32_t MaxIndices = 300000;
    };

    static DebugSolidDrawData* s_SolidData = nullptr;

    static DebugSolidBatch& GetBatch(bool depthTested)
    {
        for (auto& batch : s_SolidData->Batches)
        {
            if (batch.DepthTested == depthTested)
                return batch;
        }
        s_SolidData->Batches.push_back({});
        s_SolidData->Batches.back().DepthTested = depthTested;
        return s_SolidData->Batches.back();
    }

    static bool HasSolidCapacity(const DebugSolidBatch& batch, uint32_t extraVertices, uint32_t extraIndices)
    {
        return (batch.Vertices.size() + extraVertices <= s_SolidData->MaxVertices) &&
               (batch.Indices.size() + extraIndices <= s_SolidData->MaxIndices);
    }

    static void PushTriangle(DebugSolidBatch& batch, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec4& color)
    {
        if (!HasSolidCapacity(batch, 3, 3))
            return;

        uint32_t base = static_cast<uint32_t>(batch.Vertices.size());
        batch.Vertices.push_back({ a, color });
        batch.Vertices.push_back({ b, color });
        batch.Vertices.push_back({ c, color });
        batch.Indices.push_back(base);
        batch.Indices.push_back(base + 1);
        batch.Indices.push_back(base + 2);
    }

    static void PushQuad(DebugSolidBatch& batch, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d, const glm::vec4& color)
    {
        PushTriangle(batch, a, b, c, color);
        PushTriangle(batch, a, c, d, color);
    }

    static void BuildUnitSphere(std::vector<glm::vec3>& outVertices, std::vector<uint32_t>& outIndices, int segments)
    {
        outVertices.clear();
        outIndices.clear();

        for (int y = 0; y <= segments; ++y)
        {
            float v = static_cast<float>(y) / static_cast<float>(segments);
            float phi = v * 3.14159265359f;
            for (int x = 0; x <= segments; ++x)
            {
                float u = static_cast<float>(x) / static_cast<float>(segments);
                float theta = u * 2.0f * 3.14159265359f;
                glm::vec3 p;
                p.x = sin(phi) * cos(theta);
                p.y = cos(phi);
                p.z = sin(phi) * sin(theta);
                outVertices.push_back(p);
            }
        }

        for (int y = 0; y < segments; ++y)
        {
            for (int x = 0; x < segments; ++x)
            {
                uint32_t i0 = y * (segments + 1) + x;
                uint32_t i1 = i0 + 1;
                uint32_t i2 = i0 + static_cast<uint32_t>(segments + 1);
                uint32_t i3 = i2 + 1;
                outIndices.push_back(i0);
                outIndices.push_back(i2);
                outIndices.push_back(i1);
                outIndices.push_back(i1);
                outIndices.push_back(i2);
                outIndices.push_back(i3);
            }
        }
    }

    void DebugSolidDraw::Init()
    {
        s_SolidData = new DebugSolidDrawData();
        s_SolidData->MaxVertices = DebugSettings::Get().MaxSolidVertices;
        s_SolidData->MaxIndices = DebugSettings::Get().MaxSolidIndices;

        s_SolidData->VA = VertexArray::Create();
        s_SolidData->VB = VertexBuffer::Create(s_SolidData->MaxVertices * sizeof(DebugSolidVertex));
        s_SolidData->VB->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float4, "a_Color" }
        });
        s_SolidData->VA->AddVertexBuffer(s_SolidData->VB);
        s_SolidData->IB = IndexBuffer::Create(nullptr, s_SolidData->MaxIndices);
        s_SolidData->VA->SetIndexBuffer(s_SolidData->IB);

        const std::string vertexSrc = R"(
            #version 330 core
            layout(location = 0) in vec3 a_Position;
            layout(location = 1) in vec4 a_Color;
            uniform mat4 u_ViewProjection;
            out vec4 v_Color;
            void main()
            {
                v_Color = a_Color;
                gl_Position = u_ViewProjection * vec4(a_Position, 1.0);
            }
        )";

        const std::string fragmentSrc = R"(
            #version 330 core
            layout(location = 0) out vec4 color;
            in vec4 v_Color;
            void main()
            {
                color = v_Color;
            }
        )";

        s_SolidData->SolidShader = Shader::Create("DebugSolid", vertexSrc, fragmentSrc);
    }

    void DebugSolidDraw::Shutdown()
    {
        delete s_SolidData;
        s_SolidData = nullptr;
    }

    void DebugSolidDraw::Box(const glm::mat4& transform, const glm::vec4& color, bool depthTested)
    {
        if (!s_SolidData || !DebugSettings::Get().IsCategoryActive(DebugCategory::Solid))
            return;

        glm::vec3 corners[8] = {
            { -0.5f, -0.5f, -0.5f }, {  0.5f, -0.5f, -0.5f },
            {  0.5f, -0.5f,  0.5f }, { -0.5f, -0.5f,  0.5f },
            { -0.5f,  0.5f, -0.5f }, {  0.5f,  0.5f, -0.5f },
            {  0.5f,  0.5f,  0.5f }, { -0.5f,  0.5f,  0.5f }
        };

        for (int i = 0; i < 8; ++i)
            corners[i] = glm::vec3(transform * glm::vec4(corners[i], 1.0f));

        auto& batch = GetBatch(depthTested);
        PushQuad(batch, corners[0], corners[1], corners[2], corners[3], color);
        PushQuad(batch, corners[4], corners[5], corners[6], corners[7], color);
        PushQuad(batch, corners[0], corners[1], corners[5], corners[4], color);
        PushQuad(batch, corners[2], corners[3], corners[7], corners[6], color);
        PushQuad(batch, corners[0], corners[3], corners[7], corners[4], color);
        PushQuad(batch, corners[1], corners[2], corners[6], corners[5], color);
    }

    void DebugSolidDraw::Box(const glm::vec3& center, const glm::vec3& size, const glm::vec4& color, bool depthTested)
    {
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), center);
        transform = transform * glm::scale(glm::mat4(1.0f), size);
        Box(transform, color, depthTested);
    }

    void DebugSolidDraw::Sphere(const glm::vec3& center, float radius, const glm::vec4& color, bool depthTested)
    {
        if (!s_SolidData || !DebugSettings::Get().IsCategoryActive(DebugCategory::Solid))
            return;

        int segments = static_cast<int>(DebugSettings::Get().SphereSegments);
        std::vector<glm::vec3> unitVerts;
        std::vector<uint32_t> unitIndices;
        BuildUnitSphere(unitVerts, unitIndices, segments);

        auto& batch = GetBatch(depthTested);
        if (!HasSolidCapacity(batch, static_cast<uint32_t>(unitVerts.size()), static_cast<uint32_t>(unitIndices.size())))
            return;

        uint32_t base = static_cast<uint32_t>(batch.Vertices.size());
        for (const auto& v : unitVerts)
            batch.Vertices.push_back({ center + v * radius, color });

        for (uint32_t idx : unitIndices)
            batch.Indices.push_back(base + idx);
    }

    void DebugSolidDraw::Capsule(const glm::vec3& center, float radius, float height, const glm::vec4& color, bool depthTested)
    {
        float halfHeight = glm::max(0.0f, height * 0.5f - radius);
        Sphere(center + glm::vec3(0.0f, halfHeight, 0.0f), radius, color, depthTested);
        Sphere(center - glm::vec3(0.0f, halfHeight, 0.0f), radius, color, depthTested);

        auto& batch = GetBatch(depthTested);
        glm::vec3 minC = center - glm::vec3(radius, halfHeight, radius);
        glm::vec3 maxC = center + glm::vec3(radius, halfHeight, radius);
        PushQuad(batch,
            { minC.x, minC.y, minC.z }, { maxC.x, minC.y, minC.z },
            { maxC.x, minC.y, maxC.z }, { minC.x, minC.y, maxC.z }, color);
        PushQuad(batch,
            { minC.x, maxC.y, minC.z }, { maxC.x, maxC.y, minC.z },
            { maxC.x, maxC.y, maxC.z }, { minC.x, maxC.y, maxC.z }, color);
    }

    void DebugSolidDraw::Triangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec4& color, bool depthTested)
    {
        if (!s_SolidData || !DebugSettings::Get().IsCategoryActive(DebugCategory::Solid))
            return;
        PushTriangle(GetBatch(depthTested), a, b, c, color);
    }

    void DebugSolidDraw::Quad(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d, const glm::vec4& color, bool depthTested)
    {
        if (!s_SolidData || !DebugSettings::Get().IsCategoryActive(DebugCategory::Solid))
            return;
        PushQuad(GetBatch(depthTested), a, b, c, d, color);
    }

    void DebugSolidDraw::Plane(const glm::vec3& center, const glm::vec3& normal, float width, float height, const glm::vec4& color, bool depthTested)
    {
        glm::vec3 n = glm::normalize(normal);
        glm::vec3 tangent = glm::abs(n.y) < 0.99f ? glm::cross(n, glm::vec3(0, 1, 0)) : glm::cross(n, glm::vec3(1, 0, 0));
        tangent = glm::normalize(tangent);
        glm::vec3 bitangent = glm::normalize(glm::cross(n, tangent));

        glm::vec3 hw = tangent * (width * 0.5f);
        glm::vec3 hh = bitangent * (height * 0.5f);
        Quad(center - hw - hh, center + hw - hh, center + hw + hh, center - hw + hh, color, depthTested);
    }

    void DebugSolidDraw::Render(const glm::mat4& viewProjection)
    {
        if (!s_SolidData || !DebugSettings::Get().Enabled)
            return;

        s_SolidData->SolidShader->Bind();
        s_SolidData->SolidShader->SetMat4("u_ViewProjection", viewProjection);

        GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
        GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);

        if (!blendWasEnabled)
            glEnable(GL_BLEND);

        for (const auto& batch : s_SolidData->Batches)
        {
            if (batch.Vertices.empty() || batch.Indices.empty())
                continue;

            if (batch.DepthTested)
                glEnable(GL_DEPTH_TEST);
            else
                glDisable(GL_DEPTH_TEST);

            glDepthMask(GL_FALSE);

            uint32_t vertexCount = static_cast<uint32_t>(batch.Vertices.size());
            uint32_t indexCount = static_cast<uint32_t>(batch.Indices.size());
            if (vertexCount > s_SolidData->MaxVertices)
                vertexCount = s_SolidData->MaxVertices;
            if (indexCount > s_SolidData->MaxIndices)
                indexCount = s_SolidData->MaxIndices;

            s_SolidData->VB->SetData(batch.Vertices.data(), vertexCount * sizeof(DebugSolidVertex));
            s_SolidData->VA->Bind();
            s_SolidData->IB->SetData(batch.Indices.data(), indexCount);
            RenderCommand::DrawIndexed(s_SolidData->VA, indexCount);
        }

        glDepthMask(GL_TRUE);
        if (depthWasEnabled)
            glEnable(GL_DEPTH_TEST);
        else
            glDisable(GL_DEPTH_TEST);

        if (!blendWasEnabled)
            glDisable(GL_BLEND);
    }

    void DebugSolidDraw::Clear()
    {
        if (!s_SolidData)
            return;
        s_SolidData->Batches.clear();
    }

    uint32_t DebugSolidDraw::GetVertexCount()
    {
        if (!s_SolidData)
            return 0;
        uint32_t count = 0;
        for (const auto& batch : s_SolidData->Batches)
            count += static_cast<uint32_t>(batch.Vertices.size());
        return count;
    }

    uint32_t DebugSolidDraw::GetTriangleCount()
    {
        if (!s_SolidData)
            return 0;
        uint32_t count = 0;
        for (const auto& batch : s_SolidData->Batches)
            count += static_cast<uint32_t>(batch.Indices.size()) / 3;
        return count;
    }
}
