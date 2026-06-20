#pragma once

#include "AISystem/Navigation/NavMesh.h"
#include <vector>
#include <glm/glm.hpp>

namespace Conqueror
{
    class NavDebug
    {
    public:
        // NavMesh poligonlarını render etmek için kullanılacak
        static void DrawNavMesh(const NavMesh& navMesh);
        
        // A* Pathfinding'den dönen yolu çizdirmek için kullanılacak
        static void DrawPath(const std::vector<glm::vec3>& path);
        
        // Karakterin görüş açısını veya steering hedefini çizdirmek için
        static void DrawAgentGizmos(const glm::vec3& position, const glm::vec3& velocity, const glm::vec3& destination);
    };
}
