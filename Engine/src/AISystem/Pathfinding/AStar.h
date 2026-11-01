#pragma once

#include "AISystem/Navigation/NavMesh.h"
#include <vector>
#include <glm/glm.hpp>

namespace Conqueror
{
    // Eski API: NavMesh::FindPath üzerine ince sarmalayıcı (köşe noktaları)
    class AStar
    {
    public:
        static bool FindPath(const NavMesh& navMesh, const glm::vec3& startPos, const glm::vec3& endPos, std::vector<glm::vec3>& outPath);
    };
}
