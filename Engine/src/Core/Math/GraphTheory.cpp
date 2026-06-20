#include "GraphTheory.h"
#include <queue>
#include <algorithm>

namespace Conqueror::Math {
    std::vector<int> GraphTheory::Dijkstra(const std::unordered_map<int, std::vector<GraphEdge>>& graph, int start, int end) {
        std::unordered_map<int, float> dist;
        std::unordered_map<int, int> prev;
        for (const auto& pair : graph) {
            dist[pair.first] = std::numeric_limits<float>::max();
            for (const auto& edge : pair.second) dist[edge.to] = std::numeric_limits<float>::max();
        }
        dist[start] = 0.0f;

        using pif = std::pair<float, int>;
        std::priority_queue<pif, std::vector<pif>, std::greater<pif>> pq;
        pq.push({0.0f, start});

        while (!pq.empty()) {
            int u = pq.top().second;
            float d = pq.top().first;
            pq.pop();

            if (u == end) break;
            if (d > dist[u]) continue;

            auto it = graph.find(u);
            if (it != graph.end()) {
                for (const auto& edge : it->second) {
                    float newDist = dist[u] + edge.weight;
                    if (newDist < dist[edge.to]) {
                        dist[edge.to] = newDist;
                        prev[edge.to] = u;
                        pq.push({newDist, edge.to});
                    }
                }
            }
        }

        std::vector<int> path;
        if (dist[end] == std::numeric_limits<float>::max()) return path;

        for (int at = end; at != start; at = prev[at]) {
            path.push_back(at);
            if (prev.find(at) == prev.end()) return {}; // No path
        }
        path.push_back(start);
        std::reverse(path.begin(), path.end());
        return path;
    }

    std::vector<int> GraphTheory::AStar(const std::unordered_map<int, std::vector<GraphEdge>>& graph, int start, int end, const std::unordered_map<int, float>& heuristics) {
        std::unordered_map<int, float> gScore;
        std::unordered_map<int, int> prev;
        gScore[start] = 0.0f;

        using pif = std::pair<float, int>;
        std::priority_queue<pif, std::vector<pif>, std::greater<pif>> pq;
        pq.push({heuristics.count(start) ? heuristics.at(start) : 0.0f, start});

        while (!pq.empty()) {
            int u = pq.top().second;
            pq.pop();

            if (u == end) break;

            auto it = graph.find(u);
            if (it != graph.end()) {
                for (const auto& edge : it->second) {
                    float tentativeG = gScore[u] + edge.weight;
                    if (gScore.find(edge.to) == gScore.end() || tentativeG < gScore[edge.to]) {
                        prev[edge.to] = u;
                        gScore[edge.to] = tentativeG;
                        float fScore = tentativeG + (heuristics.count(edge.to) ? heuristics.at(edge.to) : 0.0f);
                        pq.push({fScore, edge.to});
                    }
                }
            }
        }

        std::vector<int> path;
        if (gScore.find(end) == gScore.end()) return path;

        for (int at = end; at != start; at = prev[at]) {
            path.push_back(at);
        }
        path.push_back(start);
        std::reverse(path.begin(), path.end());
        return path;
    }

    struct DSU {
        std::vector<int> parent;
        DSU(int n) {
            parent.resize(n);
            for(int i=0; i<n; ++i) parent[i] = i;
        }
        int find(int i) {
            if (parent[i] == i) return i;
            return parent[i] = find(parent[i]);
        }
        void unite(int i, int j) {
            int root_i = find(i);
            int root_j = find(j);
            if (root_i != root_j) parent[root_i] = root_j;
        }
    };

    float GraphTheory::KruskalMSTWeight(int numVertices, const std::vector<std::pair<float, std::pair<int, int>>>& edges) {
        std::vector<std::pair<float, std::pair<int, int>>> sortedEdges = edges;
        std::sort(sortedEdges.begin(), sortedEdges.end());
        
        DSU dsu(numVertices);
        float mstWeight = 0.0f;
        
        for(const auto& edge : sortedEdges) {
            int u = edge.second.first;
            int v = edge.second.second;
            if(dsu.find(u) != dsu.find(v)) {
                dsu.unite(u, v);
                mstWeight += edge.first;
            }
        }
        return mstWeight;
    }
}
