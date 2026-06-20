#pragma once

#include "AISystem/Navigation/NavMesh.h"
#include <vector>
#include <glm/glm.hpp>

namespace Conqueror
{
    class AStar
    {
    public:
        // Başlangıç ve bitiş noktalarına göre NavMesh üzerinde en kısa yolu (A*) bulur.
        static bool FindPath(const NavMesh& navMesh, const glm::vec3& startPos, const glm::vec3& endPos, std::vector<glm::vec3>& outPath);
    
    private:
        // A* implementasyonu için yardımcı node yapısı
        struct PathNode
        {
            int TriangleIndex;
            float G_Cost; // Başlangıçtan bu node'a kadar olan maliyet
            float H_Cost; // Bu node'dan hedefe tahmini maliyet (Heuristic)
            float F_Cost() const { return G_Cost + H_Cost; }
            int ParentIndex; // Geldiğimiz bir önceki node

            PathNode(int index, float g, float h, int parent)
                : TriangleIndex(index), G_Cost(g), H_Cost(h), ParentIndex(parent) {}
        };
        
        static float Heuristic(const glm::vec3& a, const glm::vec3& b);
        static void SmoothPath(const NavMesh& navMesh, const std::vector<int>& trianglePath, const glm::vec3& start, const glm::vec3& end, std::vector<glm::vec3>& outSmoothedPath);
    };
}
