#include "SkinnedMesh.h"
#include "Renderer/Renderer.h"

namespace Conqueror
{
    SkinnedMesh::SkinnedMesh(const std::vector<SkinnedVertex>& vertices, const std::vector<uint32_t>& indices)
        : m_Vertices(vertices), m_Indices(indices), m_IndexCount(static_cast<uint32_t>(indices.size()))
    {
        SetupMesh();

        float memoryMB = static_cast<float>(m_Vertices.size() * sizeof(SkinnedVertex) + m_Indices.size() * sizeof(uint32_t))
            / (1024.0f * 1024.0f);
        Renderer::GetStats().MeshMemory += memoryMB;
    }

    void SkinnedMesh::SetupMesh()
    {
        m_VertexArray = VertexArray::Create();

        m_VertexBuffer = VertexBuffer::Create((float*)m_Vertices.data(),
            static_cast<uint32_t>(m_Vertices.size() * sizeof(SkinnedVertex)));

        BufferLayout layout = {
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Normal" },
            { ShaderDataType::Float3, "a_Tangent" },
            { ShaderDataType::Float2, "a_TexCoord" },
            { ShaderDataType::Int4, "a_BoneIDs" },
            { ShaderDataType::Float4, "a_BoneWeights" },
        };
        m_VertexBuffer->SetLayout(layout);
        m_VertexArray->AddVertexBuffer(m_VertexBuffer);

        m_IndexBuffer = IndexBuffer::Create(m_Indices.data(), m_IndexCount);
        m_VertexArray->SetIndexBuffer(m_IndexBuffer);
    }
}
