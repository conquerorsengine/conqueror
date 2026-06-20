#pragma once
#include "Core/Base/Base.h"
#include <vector>
#include <unordered_map>
#include <limits>

namespace Conqueror::Math {
    struct GraphEdge { int to; float weight; };

    class CQ_API GraphTheory {
    public:
        // Shortest Path using Dijkstra
        static std::vector<int> Dijkstra(const std::unordered_map<int, std::vector<GraphEdge>>& graph, int start, int end);
        
        // Shortest Path using A*
        static std::vector<int> AStar(const std::unordered_map<int, std::vector<GraphEdge>>& graph, int start, int end, const std::unordered_map<int, float>& heuristics);
        
        // Minimum Spanning Tree using Kruskal
        static float KruskalMSTWeight(int numVertices, const std::vector<std::pair<float, std::pair<int, int>>>& edges);
    };
}
