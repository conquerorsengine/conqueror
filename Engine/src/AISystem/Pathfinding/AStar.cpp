#include "AStar.h"

namespace Conqueror
{
    bool AStar::FindPath(const NavMesh& navMesh, const glm::vec3& startPos, const glm::vec3& endPos, std::vector<glm::vec3>& outPath)
    {
        outPath.clear();
        std::vector<NavPathPoint> points;
        if (!navMesh.FindPath(startPos, endPos, points))
            return false;
        for (auto& p : points)
            outPath.push_back(p.Position);
        return !outPath.empty();
    }
}
