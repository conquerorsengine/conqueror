#include "SkinnedMesh.h"
#include "Renderer/Renderer.h"

#include <limits>

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

    void SkinnedMesh::ComputeBoneBoxes()
    {
        // PBRSkinned.cqsh ile aynı kural: ağırlığı > 0 ve 0 <= kimlik < 128 olan kemikler; hiçbiri yoksa köşe kıpırdamaz
        constexpr int kMaxBones = 128;
        std::vector<glm::vec3> mn(kMaxBones + 1, glm::vec3(std::numeric_limits<float>::max()));
        std::vector<glm::vec3> mx(kMaxBones + 1, glm::vec3(-std::numeric_limits<float>::max()));
        std::vector<char> used(kMaxBones + 1, 0);
        auto add = [&](int slot, const glm::vec3& p) {
            mn[(size_t)slot] = glm::min(mn[(size_t)slot], p);
            mx[(size_t)slot] = glm::max(mx[(size_t)slot], p);
            used[(size_t)slot] = 1;
        };
        for (const auto& v : m_Vertices)
        {
            bool any = false;
            for (int k = 0; k < 4; ++k)
            {
                const int id = v.BoneIDs[k];
                if (v.BoneWeights[k] > 0.0f && id >= 0 && id < kMaxBones)
                {
                    add(id, v.Position);
                    any = true;
                }
            }
            if (!any)
                add(kMaxBones, v.Position);
        }
        m_BoneBoxes.clear();
        for (int slot = 0; slot <= kMaxBones; ++slot)
            if (used[(size_t)slot])
                m_BoneBoxes.push_back({ slot == kMaxBones ? -1 : slot, (mn[(size_t)slot] + mx[(size_t)slot]) * 0.5f,
                                        (mx[(size_t)slot] - mn[(size_t)slot]) * 0.5f });
    }

    void SkinnedMesh::SetupMesh()
    {
        ComputeBoneBoxes();

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
