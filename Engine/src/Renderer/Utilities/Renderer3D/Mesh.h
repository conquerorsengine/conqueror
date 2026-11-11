#pragma once

#include "Core/Base/Base.h"
#include "Renderer/RHI/Buffer.h"
#include "Renderer/RHI/VertexArray.h"

#include <glm/glm.hpp>
#include <vector>
#include <memory>

namespace Conqueror
{
    struct Vertex
    {
        glm::vec3 Position;
        glm::vec3 Normal;
        glm::vec3 Tangent;
        glm::vec2 TexCoords;
        glm::vec2 TexCoords2; // Lightmap UV
    };

    class Mesh
    {
    public:
        Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
        ~Mesh() = default;

        void Bind() const;
        void Unbind() const;

        uint32_t GetIndexCount() const { return m_IndexCount; }
        uint32_t GetVertexCount() const { return (uint32_t)m_Vertices.size(); }
        std::shared_ptr<VertexArray> GetVertexArray() const { return m_VertexArray; }

        const std::vector<Vertex>& GetVertices() const { return m_Vertices; }
        const std::vector<uint32_t>& GetIndices() const { return m_Indices; }

        // Lightmap UV2 guncelle + GPU'ya yukle
        void UpdateUV2(const std::vector<glm::vec2>& newUV2);

        // Vertex aralığını güncelle (CPU kopyası + GPU alt bölümü); terrain fırçaları için
        void UpdateVertices(const Vertex* vertices, uint32_t first, uint32_t count);
        // Köşe verisi her değiştiğinde artar (ışın izleme BLAS'ı yeniden dizilir)
        uint32_t GetVersion() const { return m_Version; }

        // Otomatik LOD (Unreal Static Mesh LOD'ları). Seviye 0 meshin kendisi; ekran boyutu (sınır küresi
        // yarıçapı * izdüşüm / uzaklık) bir seviyenin eşiğinin altına inince o seviye çizilir.
        struct LODLevel
        {
            std::shared_ptr<Mesh> LODMesh;
            float ScreenSize = 0.0f;
        };
        void SetLODs(std::vector<LODLevel> lods, uint64_t signature = 0) { m_LODs = std::move(lods); m_LODSignature = signature; }
        uint64_t GetLODSignature() const { return m_LODSignature; }
        const std::vector<LODLevel>& GetLODs() const { return m_LODs; }
        bool HasLODs() const { return !m_LODs.empty(); }
        int GetLODCount() const { return 1 + (int)m_LODs.size(); }
        // level 0 = kendisi; aralık dışı -> son seviye
        const Mesh* GetLOD(int level) const;
        int SelectLOD(float screenSize) const;
        // Yerel sınırlar: kutunun merkezi, yarı boyutu (görüş kırpması) ve bu merkeze göre sınır küresi (LOD)
        const glm::vec3& GetBoundsCenter() const { return m_BoundsCenter; }
        const glm::vec3& GetBoundsExtents() const { return m_BoundsExtents; }
        float GetBoundsRadius() const { return m_BoundsRadius; }

        // Cube mesh oluştur
        static std::shared_ptr<Mesh> CreateCube();
        
        // Sphere mesh oluştur (light gizmo için)
        static std::shared_ptr<Mesh> CreateSphere(float radius = 0.5f, uint32_t segments = 16);
        
        // Plane mesh oluştur
        static std::shared_ptr<Mesh> CreatePlane(float size = 1.0f, uint32_t subdivisions = 1);
        
        // Cylinder mesh oluştur
        static std::shared_ptr<Mesh> CreateCylinder(float radius = 0.5f, float height = 1.0f, uint32_t segments = 32);
        
        // Cone mesh oluştur (spot light gizmo için)
        static std::shared_ptr<Mesh> CreateCone(float radius = 0.5f, float height = 1.0f, uint32_t segments = 64);
        
        // Arrow mesh oluştur (directional light gizmo için)
        static std::shared_ptr<Mesh> CreateArrow(float shaftRadius = 0.05f, float shaftLength = 0.8f, 
                                                  float headRadius = 0.15f, float headLength = 0.3f, uint32_t segments = 32);

    private:
        void SetupMesh();

    private:
        std::vector<Vertex> m_Vertices;
        std::vector<uint32_t> m_Indices;
        uint32_t m_IndexCount;
        uint32_t m_Version = 0;
        std::vector<LODLevel> m_LODs;
        uint64_t m_LODSignature = 0;
        void ComputeBounds();

        glm::vec3 m_BoundsCenter{ 0.0f };
        glm::vec3 m_BoundsExtents{ 0.0f };
        float m_BoundsRadius = 0.0f;

        std::shared_ptr<VertexArray> m_VertexArray;
        std::shared_ptr<VertexBuffer> m_VertexBuffer;
        std::shared_ptr<IndexBuffer> m_IndexBuffer;
    };
}
