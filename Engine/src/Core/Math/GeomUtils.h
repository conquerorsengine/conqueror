#pragma once
#include "Core/Base/Base.h"
#include "Core/Math/VecMath.h"
#include <vector>

namespace Conqueror::Math {
    class CQ_API GeomUtils {
    public:
        // Returns the convex hull of a set of 2D points using Monotone Chain algorithm
        static std::vector<CQVec2> ConvexHull2D(const std::vector<CQVec2>& points);
        
        // Triangulates a simple polygon using Ear Clipping algorithm
        static std::vector<CQVec2> EarClipTriangulate(const std::vector<CQVec2>& polygon);
        
        // Returns the intersection point of two line segments, or {NaN, NaN} if no intersection
        static CQVec2 LineIntersection2D(const CQVec2& p1, const CQVec2& p2, const CQVec2& p3, const CQVec2& p4);

        // Spline calculations using engine CQVec3
        static CQVec3 BezierPoint3D(const CQVec3& p0, const CQVec3& p1, const CQVec3& p2, const CQVec3& p3, float t);
        static CQVec3 CatmullRomPoint3D(const CQVec3& p0, const CQVec3& p1, const CQVec3& p2, const CQVec3& p3, float t);

        // 2D Geometry Tests
        static bool PointInPolygon2D(const CQVec2& point, const std::vector<CQVec2>& polygon);
        static bool AABBIntersect2D(const CQVec2& min1, const CQVec2& max1, const CQVec2& min2, const CQVec2& max2);
        static bool RayCircleIntersect2D(const CQVec2& rayOrigin, const CQVec2& rayDir, const CQVec2& circleCenter, float radius, float& t1, float& t2);
        
        // Polygon & Triangle Utils
        static CQVec2 PolygonCentroid2D(const std::vector<CQVec2>& polygon);
        static CQVec2 ClosestPointOnLineSegment2D(const CQVec2& p, const CQVec2& a, const CQVec2& b);
        
        // Advanced 3D
        static bool TriangleTriangleIntersect3D(const CQVec3& t1v0, const CQVec3& t1v1, const CQVec3& t1v2,
                                                const CQVec3& t2v0, const CQVec3& t2v1, const CQVec3& t2v2);

    private:
        static double CrossProduct(const CQVec2& o, const CQVec2& a, const CQVec2& b);
    };
}
