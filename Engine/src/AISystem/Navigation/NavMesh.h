#pragma once

#include <glm/glm.hpp>
#include <vector>

namespace Conqueror
{
    struct NavTriangle
    {
        glm::vec3 Vertices[3];
        glm::vec3 Normal;
        glm::vec3 Center;
        
        // Komşu üçgenlerin indeksleri (-1: komşu yok / duvar)
        int Neighbors[3] = { -1, -1, -1 };
        
        float Area;
        int AreaType = 0; // 0 = Walkable vb.
    };

    class NavMesh
    {
    public:
        NavMesh() = default;
        ~NavMesh() = default;

        void AddTriangle(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, int areaType = 0);
        void BuildConnectivity();
        
        const std::vector<NavTriangle>& GetTriangles() const { return m_Triangles; }
        
        // Verilen pozisyona en yakın üçgeni bulur
        int FindClosestTriangle(const glm::vec3& position) const;
        
        void Clear();

    private:
        std::vector<NavTriangle> m_Triangles;
    };

    class NavMeshBuilder
    {
    public:
        // Scene'deki NavMeshSurfaceComponent olan objeleri toplayıp NavMesh oluşturur
        static void Bake(class Scene* scene, NavMesh& outNavMesh);
    };
}
