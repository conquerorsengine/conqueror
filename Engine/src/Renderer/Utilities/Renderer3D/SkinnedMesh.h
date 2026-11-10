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
        const std::vector<SkinnedVertex>& GetVertices() const { return m_Vertices; }
        const std::vector<uint32_t>& GetIndices() const { return m_Indices; }

        // Kemik başına bağlama pozu kutusu: o kemiğin etkilediği köşelerin yerel kutusu (Bone = -1: ağırlıksız köşeler,
        // shader onları bağlama pozunda bırakır). Animasyonlu pozun tutucu sınırı bunlardan hesaplanır (görüş kırpması).
        struct BoneBox
        {
            int Bone = -1;
            glm::vec3 Center{ 0.0f };
            glm::vec3 Extents{ 0.0f };
        };
        const std::vector<BoneBox>& GetBoneBoxes() const { return m_BoneBoxes; }

        // Blend shape (morph target): yalnız değişen köşeler (seyrek), konum ve normal farkı; Channel = Skeleton::MorphNames dizini
        struct MorphTarget
        {
            int Channel = -1;
            std::vector<uint32_t> Vertices;
            std::vector<glm::vec3> DeltaPositions;
            std::vector<glm::vec3> DeltaNormals;
        };
        const std::vector<MorphTarget>& GetMorphTargets() const { return m_MorphTargets; }
        void SetMorphTargets(std::vector<MorphTarget> targets) { m_MorphTargets = std::move(targets); }

    private:
        void SetupMesh();
        void ComputeBoneBoxes();

        std::vector<SkinnedVertex> m_Vertices;
        std::vector<uint32_t> m_Indices;
        uint32_t m_IndexCount = 0;
        std::vector<BoneBox> m_BoneBoxes;
        std::vector<MorphTarget> m_MorphTargets;

        std::shared_ptr<VertexArray> m_VertexArray;
        std::shared_ptr<VertexBuffer> m_VertexBuffer;
        std::shared_ptr<IndexBuffer> m_IndexBuffer;
    };
}
