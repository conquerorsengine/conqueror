#pragma once

#include "Core/Base/Base.h"
#include "Mesh.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

// Sanal geometri (Unreal Nanite benzeri, CPU seçimli): mesh ~128 üçgenlik kümelere bölünür; komşu kümeler
// gruplanıp sınırları kilitli sadeleştirilerek (MeshSimplifier, LockBorders) üst seviye kümeler üretilir ve bir
// küme DAG'ı oluşur. Her karede her küme için ekran uzayı hatası hesaplanır; hatası eşiğin altında ama üst grubunun
// hatası eşiğin üstünde olan kümeler çizilir (çatlaksız sürekli LOD). Görünüm dışı kümeler ayıklanır, seçilenler
// tek glMultiDrawElements ile çizilir.
namespace Conqueror
{
    class VertexArray;

    struct ClusterInfo
    {
        uint32_t IndexOffset = 0;         // birleşik indeks tamponunda (indeks sayısı)
        uint32_t IndexCount = 0;
        glm::vec3 Center{ 0.0f };         // görünürlük ayıklama küresi
        float Radius = 0.0f;
        glm::vec3 LodCenter{ 0.0f };      // kendi hatasının ölçüldüğü küre (oluştuğu grup)
        float LodRadius = 0.0f;
        float Error = 0.0f;               // nesne uzayı (yaprak = 0)
        glm::vec3 ParentLodCenter{ 0.0f };
        float ParentLodRadius = 0.0f;
        float ParentError = 1e30f;        // ait olduğu grubun hatası (kök = sonsuz)
        int Level = 0;
    };

    class CQ_API ClusterMesh
    {
    public:
        // Yapı (pahalı; arka plan iş parçacığında çağrılabilir): GPU tamponları oluşturmaz
        static std::shared_ptr<ClusterMesh> Build(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices,
                                                  uint32_t clusterSize = 128);
        // GPU tamponlarını kur (ana iş parçacığı)
        void Upload();
        bool IsUploaded() const { return (bool)m_VertexArray; }

        // Seçim: nesne uzayındaki kamera konumu, dünya ölçeği, izdüşüm ölçeği (P[1][1] * ekran yüksekliği / 2) ve piksel eşiği.
        // frustum: dünya uzayı düzlemleri (nokta·xyz + w >= -r görünür); world: nesnenin dünya matrisi
        void Select(const glm::vec3& cameraLocal, float worldScale, float pixelScale, float thresholdPixels, bool orthographic,
                    const glm::mat4& world, const glm::vec4* frustum, std::vector<uint32_t>& outClusters) const;
        // Seçili kümeleri çiz (bağlı shader ile); çizilen üçgen sayısı
        uint32_t Draw(const std::vector<uint32_t>& clusters) const;

        const std::vector<ClusterInfo>& GetClusters() const { return m_Clusters; }
        uint32_t GetLeafTriangleCount() const { return m_LeafTriangles; }
        int GetLevelCount() const { return m_Levels; }
        const std::shared_ptr<VertexArray>& GetVertexArray() const { return m_VertexArray; }

    private:
        std::vector<Vertex> m_Vertices;
        std::vector<uint32_t> m_Indices;
        std::vector<ClusterInfo> m_Clusters;
        uint32_t m_LeafTriangles = 0;
        int m_Levels = 0;
        std::shared_ptr<VertexArray> m_VertexArray;
    };
}
