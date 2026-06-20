#include "AStar.h"
#include <queue>
#include <unordered_map>
#include <algorithm>

namespace Conqueror
{
    float AStar::Heuristic(const glm::vec3& a, const glm::vec3& b)
    {
        return glm::distance(a, b);
    }

    bool AStar::FindPath(const NavMesh& navMesh, const glm::vec3& startPos, const glm::vec3& endPos, std::vector<glm::vec3>& outPath)
    {
        outPath.clear();
        
        int startTri = navMesh.FindClosestTriangle(startPos);
        int endTri = navMesh.FindClosestTriangle(endPos);

        if (startTri == -1 || endTri == -1)
            return false;

        if (startTri == endTri)
        {
            outPath.push_back(startPos);
            outPath.push_back(endPos);
            return true;
        }

        const auto& triangles = navMesh.GetTriangles();

        auto cmp = [](const PathNode& left, const PathNode& right) { return left.F_Cost() > right.F_Cost(); };
        std::priority_queue<PathNode, std::vector<PathNode>, decltype(cmp)> openSet(cmp);
        
        std::unordered_map<int, PathNode> allNodes;

        PathNode startNode(startTri, 0.0f, Heuristic(triangles[startTri].Center, triangles[endTri].Center), -1);
        openSet.push(startNode);
        allNodes.insert({startTri, startNode});

        std::vector<int> closedSet;

        while (!openSet.empty())
        {
            PathNode current = openSet.top();
            openSet.pop();

            if (current.TriangleIndex == endTri)
            {
                // Yolu geri izle
                std::vector<int> pathIndices;
                int currIdx = current.TriangleIndex;
                while (currIdx != -1)
                {
                    pathIndices.push_back(currIdx);
                    currIdx = allNodes.at(currIdx).ParentIndex;
                }
                std::reverse(pathIndices.begin(), pathIndices.end());
                
                SmoothPath(navMesh, pathIndices, startPos, endPos, outPath);
                return true;
            }

            closedSet.push_back(current.TriangleIndex);

            for (int i = 0; i < 3; ++i)
            {
                int neighborIdx = triangles[current.TriangleIndex].Neighbors[i];
                if (neighborIdx == -1) continue;

                if (std::find(closedSet.begin(), closedSet.end(), neighborIdx) != closedSet.end())
                    continue;

                float tentativeG = current.G_Cost + glm::distance(triangles[current.TriangleIndex].Center, triangles[neighborIdx].Center);

                auto it = allNodes.find(neighborIdx);
                if (it == allNodes.end() || tentativeG < it->second.G_Cost)
                {
                    PathNode neighborNode(neighborIdx, tentativeG, Heuristic(triangles[neighborIdx].Center, triangles[endTri].Center), current.TriangleIndex);
                    
                    if (it != allNodes.end())
                        it->second = neighborNode;
                    else
                        allNodes.insert({neighborIdx, neighborNode});

                    openSet.push(neighborNode);
                }
            }
        }

        return false;
    }

    void AStar::SmoothPath(const NavMesh& navMesh, const std::vector<int>& trianglePath, const glm::vec3& start, const glm::vec3& end, std::vector<glm::vec3>& outSmoothedPath)
    {
        // Simple smoothing: Sadece merkezleri ekle.
        // Gelişmiş bir sistemde String Pulling (Funnel Algorithm) kullanılır.
        outSmoothedPath.push_back(start);
        const auto& triangles = navMesh.GetTriangles();
        
        for (size_t i = 1; i < trianglePath.size() - 1; ++i)
        {
            outSmoothedPath.push_back(triangles[trianglePath[i]].Center);
        }
        
        outSmoothedPath.push_back(end);
    }
}
