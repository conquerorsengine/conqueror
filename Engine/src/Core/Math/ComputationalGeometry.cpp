#include "ComputationalGeometry.h"
#include <cmath>

namespace Conqueror::Math {
    static bool Inside(const CQVec2& p, const CQVec2& p1, const CQVec2& p2) {
        return (p2.x - p1.x) * (p.y - p1.y) - (p2.y - p1.y) * (p.x - p1.x) >= 0;
    }

    static CQVec2 Intersect(const CQVec2& p1, const CQVec2& p2, const CQVec2& p3, const CQVec2& p4) {
        float num = (p1.x*p2.y - p1.y*p2.x)*(p3.x - p4.x) - (p1.x - p2.x)*(p3.x*p4.y - p3.y*p4.x);
        float den = (p1.x - p2.x)*(p3.y - p4.y) - (p1.y - p2.y)*(p3.x - p4.x);
        float numY = (p1.x*p2.y - p1.y*p2.x)*(p3.y - p4.y) - (p1.y - p2.y)*(p3.x*p4.y - p3.y*p4.x);
        return CQVec2(num / den, numY / den);
    }

    std::vector<CQVec2> ComputationalGeometry::ClipPolygonSutherlandHodgman(const std::vector<CQVec2>& subjectPoly, const std::vector<CQVec2>& clipPoly) {
        std::vector<CQVec2> outputList = subjectPoly;
        for (size_t i = 0; i < clipPoly.size(); i++) {
            CQVec2 clipEdge1 = clipPoly[i];
            CQVec2 clipEdge2 = clipPoly[(i + 1) % clipPoly.size()];
            std::vector<CQVec2> inputList = outputList;
            outputList.clear();
            if (inputList.empty()) break;
            CQVec2 S = inputList.back();
            for (const auto& E : inputList) {
                if (Inside(E, clipEdge1, clipEdge2)) {
                    if (!Inside(S, clipEdge1, clipEdge2)) {
                        outputList.push_back(Intersect(S, E, clipEdge1, clipEdge2));
                    }
                    outputList.push_back(E);
                } else if (Inside(S, clipEdge1, clipEdge2)) {
                    outputList.push_back(Intersect(S, E, clipEdge1, clipEdge2));
                }
                S = E;
            }
        }
        return outputList;
    }

    float ComputationalGeometry::PolygonArea3D(const std::vector<CQVec3>& polygon) {
        if (polygon.size() < 3) return 0.0f;
        CQVec3 sum(0.0f, 0.0f, 0.0f);
        for (size_t i = 0; i < polygon.size(); ++i) {
            CQVec3 p1 = polygon[i];
            CQVec3 p2 = polygon[(i + 1) % polygon.size()];
            sum.x += (p1.y - p2.y) * (p1.z + p2.z);
            sum.y += (p1.z - p2.z) * (p1.x + p2.x);
            sum.z += (p1.x - p2.x) * (p1.y + p2.y);
        }
        return Vec3Length(sum) * 0.5f;
    }

    CQVec3 ComputationalGeometry::PolygonCentroid3D(const std::vector<CQVec3>& polygon) {
        if (polygon.empty()) return CQVec3(0,0,0);
        CQVec3 centroid(0,0,0);
        for (const auto& p : polygon) {
            centroid = Vec3Add(centroid, p);
        }
        return Vec3Div(centroid, polygon.size());
    }

    bool ComputationalGeometry::ArePointsCoplanar(const std::vector<CQVec3>& points, float epsilon) {
        if (points.size() <= 3) return true;
        CQVec3 v1 = Vec3Sub(points[1], points[0]);
        CQVec3 v2 = Vec3Sub(points[2], points[0]);
        CQVec3 normal = Vec3Cross(v1, v2);
        if (Vec3Length(normal) < epsilon) return true;
        normal = Vec3Normalize(normal);
        for (size_t i = 3; i < points.size(); ++i) {
            CQVec3 v = Vec3Sub(points[i], points[0]);
            if (std::abs(Vec3Dot(normal, v)) > epsilon) return false;
        }
        return true;
    }

    std::vector<CQVec3> ComputationalGeometry::GenerateSphereVertices(float radius, int sectors, int stacks) {
        std::vector<CQVec3> vertices;
        float sectorStep = 2 * 3.14159265359f / sectors;
        float stackStep = 3.14159265359f / stacks;
        for(int i = 0; i <= stacks; ++i) {
            float stackAngle = 3.14159265359f / 2 - i * stackStep;
            float xy = radius * std::cos(stackAngle);
            float z = radius * std::sin(stackAngle);
            for(int j = 0; j <= sectors; ++j) {
                float sectorAngle = j * sectorStep;
                float x = xy * std::cos(sectorAngle);
                float y = xy * std::sin(sectorAngle);
                vertices.push_back(CQVec3(x, y, z));
            }
        }
        return vertices;
    }

    std::vector<CQVec3> ComputationalGeometry::GenerateCylinderVertices(float radius, float height, int sectors) {
        std::vector<CQVec3> vertices;
        float sectorStep = 2 * 3.14159265359f / sectors;
        for(int j = 0; j <= sectors; ++j) {
            float sectorAngle = j * sectorStep;
            float x = radius * std::cos(sectorAngle);
            float y = radius * std::sin(sectorAngle);
            vertices.push_back(CQVec3(x, y, height / 2.0f));
            vertices.push_back(CQVec3(x, y, -height / 2.0f));
        }
        return vertices;
    }
}
