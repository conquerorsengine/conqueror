#include "Topology.h"
#include <map>
#include <set>

namespace Conqueror::Math {
    int Topology::CalculateEulerCharacteristic(int vertexCount, int edgeCount, int faceCount) {
        return vertexCount - edgeCount + faceCount;
    }

    float Topology::CalculateFaceArea(const std::vector<CQVec3>& vertices, const Face& face) {
        if (face.vertexIndices.size() < 3) return 0.0f;
        CQVec3 sum(0, 0, 0);
        for (size_t i = 0; i < face.vertexIndices.size(); ++i) {
            const CQVec3& v1 = vertices[face.vertexIndices[i]];
            const CQVec3& v2 = vertices[face.vertexIndices[(i + 1) % face.vertexIndices.size()]];
            sum.x += (v1.y - v2.y) * (v1.z + v2.z);
            sum.y += (v1.z - v2.z) * (v1.x + v2.x);
            sum.z += (v1.x - v2.x) * (v1.y + v2.y);
        }
        return Vec3Length(sum) * 0.5f;
    }

    // Edge hash helper
    struct Edge {
        int v1, v2;
        bool operator<(const Edge& o) const {
            if (v1 != o.v1) return v1 < o.v1;
            return v2 < o.v2;
        }
    };

    static Edge MakeEdge(int a, int b) {
        return {std::min(a, b), std::max(a, b)};
    }

    void Topology::CatmullClarkSubdivision(
        const std::vector<CQVec3>& inVertices, 
        const std::vector<Face>& inFaces,
        std::vector<CQVec3>& outVertices,
        std::vector<Face>& outFaces) 
    {
        outVertices.clear();
        outFaces.clear();

        if (inVertices.empty() || inFaces.empty()) return;

        std::vector<CQVec3> facePoints(inFaces.size());
        std::map<Edge, CQVec3> edgePoints;
        std::map<Edge, int> edgePointIndices;

        outVertices = inVertices; // Start with original points (will be updated later)

        // 1. Face Points
        for (size_t f = 0; f < inFaces.size(); ++f) {
            CQVec3 center(0, 0, 0);
            for (int idx : inFaces[f].vertexIndices) {
                center = Vec3Add(center, inVertices[idx]);
            }
            center = Vec3Div(center, inFaces[f].vertexIndices.size());
            facePoints[f] = center;
            outVertices.push_back(center);
        }
        int facePointStartIndex = inVertices.size();

        // 2. Edge Points
        std::map<Edge, std::vector<int>> edgeToFaces;
        for (size_t f = 0; f < inFaces.size(); ++f) {
            const auto& face = inFaces[f];
            for (size_t i = 0; i < face.vertexIndices.size(); ++i) {
                Edge e = MakeEdge(face.vertexIndices[i], face.vertexIndices[(i + 1) % face.vertexIndices.size()]);
                edgeToFaces[e].push_back(f);
            }
        }

        for (const auto& pair : edgeToFaces) {
            Edge e = pair.first;
            CQVec3 edgePt = Vec3Add(inVertices[e.v1], inVertices[e.v2]);
            int count = 2;
            for (int fIdx : pair.second) {
                edgePt = Vec3Add(edgePt, facePoints[fIdx]);
                count++;
            }
            edgePt = Vec3Div(edgePt, count);
            edgePoints[e] = edgePt;
            edgePointIndices[e] = outVertices.size();
            outVertices.push_back(edgePt);
        }

        // 3. Move Original Points
        std::vector<std::vector<int>> vertexToFaces(inVertices.size());
        std::vector<std::vector<Edge>> vertexToEdges(inVertices.size());
        for (size_t f = 0; f < inFaces.size(); ++f) {
            const auto& face = inFaces[f];
            for (size_t i = 0; i < face.vertexIndices.size(); ++i) {
                int v = face.vertexIndices[i];
                vertexToFaces[v].push_back(f);
                Edge e = MakeEdge(face.vertexIndices[i], face.vertexIndices[(i + 1) % face.vertexIndices.size()]);
                vertexToEdges[v].push_back(e);
            }
        }

        for (size_t v = 0; v < inVertices.size(); ++v) {
            int n = vertexToFaces[v].size();
            if (n == 0) continue;

            CQVec3 avgFacePt(0, 0, 0);
            for (int fIdx : vertexToFaces[v]) avgFacePt = Vec3Add(avgFacePt, facePoints[fIdx]);
            avgFacePt = Vec3Div(avgFacePt, n);

            CQVec3 avgEdgeMid(0, 0, 0);
            std::set<Edge> uniqueEdges;
            for (Edge e : vertexToEdges[v]) uniqueEdges.insert(e);
            for (Edge e : uniqueEdges) {
                CQVec3 mid = Vec3Div(Vec3Add(inVertices[e.v1], inVertices[e.v2]), 2.0f);
                avgEdgeMid = Vec3Add(avgEdgeMid, mid);
            }
            avgEdgeMid = Vec3Div(avgEdgeMid, uniqueEdges.size());

            CQVec3 p1 = Vec3Div(avgFacePt, n);
            CQVec3 p2 = Vec3Div(Vec3Mul(avgEdgeMid, 2.0f), n);
            CQVec3 p3 = Vec3Mul(inVertices[v], (n - 3.0f) / n);
            outVertices[v] = Vec3Add(Vec3Add(p1, p2), p3);
        }

        // 4. Create New Faces
        for (size_t f = 0; f < inFaces.size(); ++f) {
            const auto& face = inFaces[f];
            int n = face.vertexIndices.size();
            for (int i = 0; i < n; ++i) {
                int v1 = face.vertexIndices[i];
                int v2 = face.vertexIndices[(i + 1) % n];
                int v0 = face.vertexIndices[(i - 1 + n) % n];

                Edge e1 = MakeEdge(v1, v2);
                Edge e0 = MakeEdge(v0, v1);

                Face newFace;
                newFace.vertexIndices.push_back(v1);
                newFace.vertexIndices.push_back(edgePointIndices[e1]);
                newFace.vertexIndices.push_back(facePointStartIndex + f);
                newFace.vertexIndices.push_back(edgePointIndices[e0]);
                outFaces.push_back(newFace);
            }
        }
    }
}
