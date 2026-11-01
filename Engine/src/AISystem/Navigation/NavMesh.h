#pragma once

#include "Core/Base/Base.h"
#include "AISystem/Components/NavComponents.h"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <vector>

// Gezinme ağı (Recast benzeri): sahne geometrisi voksel yükseklik alanına rasterleştirilir; eğim, basamak,
// tavan boşluğu ve ajan yarıçapına göre yürünebilir "zemin hücreleri" çıkarılır (çok katlı yapılar dahil).
// Yol bulma hücre grafiğinde A* (alan maliyetleri, NavMesh Link bağlantıları) + görüş hattı yumuşatma.
namespace Conqueror
{
    class Scene;

    enum NavAreaId : uint8_t { NavArea_Walkable = 0, NavArea_NotWalkable = 1, NavArea_Jump = 2 };
    constexpr int kNavMaxAreas = 32;

    struct NavBuildSettings
    {
        float CellSize = 0.166f;
        float CellHeight = 0.1f;
        float AgentRadius = 0.5f;
        float AgentHeight = 2.0f;
        float MaxSlope = 45.0f;
        float StepHeight = 0.4f;
    };

    // Uyumluluk: eski üçgen tabanlı ağın veri yapısı (hata ayıklama / dışa aktarma)
    struct NavTriangle
    {
        glm::vec3 Vertices[3]{};
        glm::vec3 Normal{};
        glm::vec3 Center{};
        int Neighbors[3] = { -1, -1, -1 };
        float Area{};
        int AreaType = 0;
    };

    class CQ_API NavMesh
    {
    public:
        struct Cell
        {
            float Y = 0.0f;                 // zemin yüksekliği
            float Clearance = 0.0f;         // üstteki boşluk
            uint8_t Area = NavArea_Walkable;
            int32_t Neighbors[4] = { -1, -1, -1, -1 };   // +x, +z, -x, -z
            int32_t Column = 0;
        };

        struct Link
        {
            glm::vec3 Start{ 0.0f }, End{ 0.0f };
            int StartCell = -1, EndCell = -1;
            bool Bidirectional = true;
            float Cost = 1.0f;
            uint8_t Area = NavArea_Jump;
        };

        NavMesh();

        // ---- Sorgular
        bool IsBaked() const { return !m_Cells.empty(); }
        int FindNearestCell(const glm::vec3& position, float maxDistance = 3.0f) const;
        bool SamplePosition(const glm::vec3& position, float maxDistance, glm::vec3& outPosition) const;
        // Yol: başlangıç ve hedef ağa en yakın noktalara oturtulur; hedefe varılamazsa en yakın noktaya gider (complete=false)
        bool FindPath(const glm::vec3& start, const glm::vec3& end, std::vector<NavPathPoint>& outPath, uint32_t areaMask = 0xFFFFFFFFu,
                      bool* outComplete = nullptr) const;
        // Ağ üzerinde düz hat: engel / kenar varsa false ve çarpma noktası
        bool Raycast(const glm::vec3& from, const glm::vec3& to, glm::vec3& outHit, uint32_t areaMask = 0xFFFFFFFFu) const;
        bool GetHeight(const glm::vec3& position, float& outY) const;
        glm::vec3 CellPosition(int cell) const;
        const Cell& GetCell(int i) const { return m_Cells[(size_t)i]; }
        size_t GetCellCount() const { return m_Cells.size(); }
        const std::vector<Link>& GetLinks() const { return m_Links; }
        const NavBuildSettings& GetSettings() const { return m_Settings; }

        // ---- Alan maliyetleri (Unity Navigation Areas)
        void SetAreaCost(int area, float cost) { if (area >= 0 && area < kNavMaxAreas) m_AreaCost[(size_t)area] = cost; }
        float GetAreaCost(int area) const { return area >= 0 && area < kNavMaxAreas ? m_AreaCost[(size_t)area] : 1.0f; }

        // ---- Dinamik engeller (Carve): hücre engeli
        void ClearBlocked();
        void BlockBox(const glm::mat4& transform, const glm::vec3& center, const glm::vec3& size, float expand);
        bool IsBlocked(int cell) const { return !m_Blocked.empty() && m_Blocked[(size_t)cell] != 0; }
        uint64_t GetBlockedVersion() const { return m_BlockedVersion; }

        // ---- Pişirme sonucu (NavMeshBuilder doldurur)
        void Clear();
        void SetGrid(const glm::vec3& origin, int width, int depth, const NavBuildSettings& settings);
        int AddCell(int x, int z, const Cell& cell);
        void FinishColumns();
        std::vector<Cell>& MutableCells() { return m_Cells; }
        std::vector<Link>& MutableLinks() { return m_Links; }
        int ColumnStart(int x, int z) const;
        int ColumnCount(int x, int z) const;
        uint64_t GetVersion() const { return m_Version; }

        // ---- Eski API (üçgen listesi): hata ayıklama için hücre dörtgenleri
        void AddTriangle(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, int areaType = 0);
        void BuildConnectivity() {}
        const std::vector<NavTriangle>& GetTriangles() const { return m_LegacyTriangles; }
        int FindClosestTriangle(const glm::vec3& position) const;

        glm::vec3 GetOrigin() const { return m_Origin; }
        int GetWidth() const { return m_Width; }
        int GetDepth() const { return m_Depth; }

    private:
        bool LineOfSight(int fromCell, int toCell, uint32_t areaMask) const;
        int NeighborDiagonal(int cell, int dirA, int dirB) const;

        glm::vec3 m_Origin{ 0.0f };
        int m_Width = 0, m_Depth = 0;
        NavBuildSettings m_Settings;
        std::vector<Cell> m_Cells;
        std::vector<std::vector<int>> m_ColumnCells;   // pişirme sırasında
        std::vector<int32_t> m_ColumnStart;            // Width*Depth+1
        std::vector<Link> m_Links;
        std::vector<std::vector<int>> m_LinksFrom;      // hücre -> link
        std::vector<uint8_t> m_Blocked;
        std::array<float, kNavMaxAreas> m_AreaCost{};
        uint64_t m_Version = 0, m_BlockedVersion = 0;
        std::vector<NavTriangle> m_LegacyTriangles;

        friend class NavMeshBuilder;
    };

    class CQ_API NavMeshBuilder
    {
    public:
        // Sahnedeki NavMesh Surface'lerin kapsadığı geometriyi pişirir (Modifier / Modifier Volume / Link dahil)
        static void Bake(Scene* scene, NavMesh& outNavMesh);
        // Sahne geometrisi son pişirmeden beri değişti mi (oyun başında yeniden pişirme kararı)
        static uint64_t ComputeSceneSignature(Scene* scene);
    };
}
