#pragma once

#include "Core/Base/Base.h"
#include "Renderer/RHI/Buffer.h"
#include "Renderer/RHI/VertexArray.h"

#include <glm/glm.hpp>
#include <vector>
#include <memory>

namespace Conqueror
{
#pragma pack(push, 1)
    struct SkinnedVertex
    {
        glm::vec3 Position{};
        glm::vec3 Normal{};
        glm::vec3 Tangent{};
        glm::vec2 TexCoords{};
        glm::ivec4 BoneIDs{ -1, -1, -1, -1 };
        glm::vec4 BoneWeights{ 0.f, 0.f, 0.f, 0.f };
    };
#pragma pack(pop)


    class SkinnedMesh
    {
    public:
        SkinnedMesh(const std::vector<SkinnedVertex>& vertices, const std::vector<uint32_t>& indices);

        uint32_t GetIndexCount() const { return m_IndexCount; }
        uint32_t GetVertexCount() const { return static_cast<uint32_t>(m_Vertices.size()); }
        std::shared_ptr<VertexArray> GetVertexArray() const { return m_VertexArray; }

    private:
        void SetupMesh();

        std::vector<SkinnedVertex> m_Vertices;
        std::vector<uint32_t> m_Indices;
        uint32_t m_IndexCount = 0;

        std::shared_ptr<VertexArray> m_VertexArray;
        std::shared_ptr<VertexBuffer> m_VertexBuffer;
        std::shared_ptr<IndexBuffer> m_IndexBuffer;
    };
}
