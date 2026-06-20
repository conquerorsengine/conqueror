#pragma once
#include "Core/Base/Base.h"
#include "VecMath.h"
#include <vector>

namespace Conqueror::Math {
    class CQ_API ComputationalGeometry {
    public:
        // 2D Polygon Kırpma (Sutherland-Hodgman)
        static std::vector<CQVec2> ClipPolygonSutherlandHodgman(const std::vector<CQVec2>& subjectPoly, const std::vector<CQVec2>& clipPoly);

        // 3D Polygon Operasyonları
        static float PolygonArea3D(const std::vector<CQVec3>& polygon);
        static CQVec3 PolygonCentroid3D(const std::vector<CQVec3>& polygon);
        static bool ArePointsCoplanar(const std::vector<CQVec3>& points, float epsilon = 1e-5f);

        // 3D Şekil Üretimi
        static std::vector<CQVec3> GenerateSphereVertices(float radius, int sectors, int stacks);
        static std::vector<CQVec3> GenerateCylinderVertices(float radius, float height, int sectors);
    };
}
