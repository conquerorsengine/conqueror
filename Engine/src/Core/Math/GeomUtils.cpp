#include "GeomUtils.h"
#include "Core/Math/Curve.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Conqueror::Math {
    double GeomUtils::CrossProduct(const CQVec2& o, const CQVec2& a, const CQVec2& b) {
        return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    }

    std::vector<CQVec2> GeomUtils::ConvexHull2D(const std::vector<CQVec2>& points) {
        if (points.size() <= 3) return points;
        
        std::vector<CQVec2> pts = points;
        std::sort(pts.begin(), pts.end(), [](const CQVec2& a, const CQVec2& b) {
            return a.x < b.x || (a.x == b.x && a.y < b.y);
        });

        std::vector<CQVec2> hull;
        // Lower
        for (const auto& p : pts) {
            while (hull.size() >= 2 && CrossProduct(hull[hull.size()-2], hull.back(), p) <= 0) hull.pop_back();
            hull.push_back(p);
        }
        // Upper
        for (int i = (int)pts.size() - 2, t = hull.size() + 1; i >= 0; i--) {
            while (hull.size() >= t && CrossProduct(hull[hull.size()-2], hull.back(), pts[i]) <= 0) hull.pop_back();
            hull.push_back(pts[i]);
        }
        hull.pop_back();
        return hull;
    }

    std::vector<CQVec2> GeomUtils::EarClipTriangulate(const std::vector<CQVec2>& polygon) {
        std::vector<CQVec2> out;
        if (polygon.size() < 3) return out;
        
        std::vector<int> V(polygon.size());
        for (size_t i=0; i<polygon.size(); i++) V[i] = i;

        int n = polygon.size();
        int count = 2 * n; 
        for (int m = n, v = n - 1; m > 2; ) {
            if (0 >= (count--)) break;
            int u = v; if (m <= u) u = 0; v = u + 1; if (m <= v) v = 0; int w = v + 1; if (m <= w) w = 0;
            
            const CQVec2& A = polygon[V[u]];
            const CQVec2& B = polygon[V[v]];
            const CQVec2& C = polygon[V[w]];

            if (CrossProduct(A, B, C) < 0.000000001) continue;

            bool snip = true;
            for (int p = 0; p < m; p++) {
                if ((p == u) || (p == v) || (p == w)) continue;
                const CQVec2& P = polygon[V[p]];
                
                double a = ((B.y - C.y)*(P.x - C.x) + (C.x - B.x)*(P.y - C.y)) / ((B.y - C.y)*(A.x - C.x) + (C.x - B.x)*(A.y - C.y));
                double b = ((C.y - A.y)*(P.x - C.x) + (A.x - C.x)*(P.y - C.y)) / ((B.y - C.y)*(A.x - C.x) + (C.x - B.x)*(A.y - C.y));
                double c = 1.0 - a - b;
                if (a >= 0 && a <= 1 && b >= 0 && b <= 1 && c >= 0 && c <= 1) { snip = false; break; }
            }
            if (snip) {
                out.push_back(polygon[V[u]]);
                out.push_back(polygon[V[v]]);
                out.push_back(polygon[V[w]]);
                for (int s = v, t = v + 1; t < m; s++, t++) V[s] = V[t];
                m--;
                count = 2 * m;
            }
        }
        return out;
    }

    CQVec2 GeomUtils::LineIntersection2D(const CQVec2& p1, const CQVec2& p2, const CQVec2& p3, const CQVec2& p4) {
        double denom = (p1.x - p2.x) * (p3.y - p4.y) - (p1.y - p2.y) * (p3.x - p4.x);
        if (std::abs(denom) < 1e-9) return { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN() };

        double px = ((p1.x * p2.y - p1.y * p2.x) * (p3.x - p4.x) - (p1.x - p2.x) * (p3.x * p4.y - p3.y * p4.x)) / denom;
        double py = ((p1.x * p2.y - p1.y * p2.x) * (p3.y - p4.y) - (p1.y - p2.y) * (p3.x * p4.y - p3.y * p4.x)) / denom;
        
        return { (float)px, (float)py };
    }

    CQVec3 GeomUtils::BezierPoint3D(const CQVec3& p0, const CQVec3& p1, const CQVec3& p2, const CQVec3& p3, float t) {
        glm::vec3 gp0(p0.x, p0.y, p0.z);
        glm::vec3 gp1(p1.x, p1.y, p1.z);
        glm::vec3 gp2(p2.x, p2.y, p2.z);
        glm::vec3 gp3(p3.x, p3.y, p3.z);
        glm::vec3 res = Math::BezierCurve::Cubic(gp0, gp1, gp2, gp3, t);
        CQVec3 out;
        out.x = res.x; out.y = res.y; out.z = res.z;
        return out;
    }

    CQVec3 GeomUtils::CatmullRomPoint3D(const CQVec3& p0, const CQVec3& p1, const CQVec3& p2, const CQVec3& p3, float t) {
        glm::vec3 gp0(p0.x, p0.y, p0.z);
        glm::vec3 gp1(p1.x, p1.y, p1.z);
        glm::vec3 gp2(p2.x, p2.y, p2.z);
        glm::vec3 gp3(p3.x, p3.y, p3.z);
        glm::vec3 res = Math::CatmullRomSpline::Interpolate(gp0, gp1, gp2, gp3, t);
        CQVec3 out;
        out.x = res.x; out.y = res.y; out.z = res.z;
        return out;
    }

    bool GeomUtils::PointInPolygon2D(const CQVec2& point, const std::vector<CQVec2>& polygon) {
        bool inside = false;
        for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
            if (((polygon[i].y > point.y) != (polygon[j].y > point.y)) &&
                (point.x < (polygon[j].x - polygon[i].x) * (point.y - polygon[i].y) / (polygon[j].y - polygon[i].y) + polygon[i].x))
                inside = !inside;
        }
        return inside;
    }

    bool GeomUtils::AABBIntersect2D(const CQVec2& min1, const CQVec2& max1, const CQVec2& min2, const CQVec2& max2) {
        return min1.x <= max2.x && max1.x >= min2.x && min1.y <= max2.y && max1.y >= min2.y;
    }

    bool GeomUtils::RayCircleIntersect2D(const CQVec2& rayOrigin, const CQVec2& rayDir, const CQVec2& circleCenter, float radius, float& t1, float& t2) {
        glm::vec2 o(rayOrigin.x, rayOrigin.y);
        glm::vec2 d(rayDir.x, rayDir.y);
        glm::vec2 c(circleCenter.x, circleCenter.y);
        
        glm::vec2 oc = o - c;
        float a = glm::dot(d, d);
        float b = 2.0f * glm::dot(oc, d);
        float c_val = glm::dot(oc, oc) - radius * radius;
        float discriminant = b * b - 4.0f * a * c_val;

        if (discriminant < 0) return false;
        
        discriminant = std::sqrt(discriminant);
        t1 = (-b - discriminant) / (2.0f * a);
        t2 = (-b + discriminant) / (2.0f * a);
        return true;
    }

    CQVec2 GeomUtils::PolygonCentroid2D(const std::vector<CQVec2>& polygon) {
        if (polygon.empty()) return {0.0f, 0.0f};
        if (polygon.size() == 1) return polygon[0];
        if (polygon.size() == 2) return {(polygon[0].x + polygon[1].x) * 0.5f, (polygon[0].y + polygon[1].y) * 0.5f};

        float area = 0.0f;
        float cx = 0.0f;
        float cy = 0.0f;

        for (size_t i = 0; i < polygon.size(); ++i) {
            size_t j = (i + 1) % polygon.size();
            float cross = polygon[i].x * polygon[j].y - polygon[j].x * polygon[i].y;
            area += cross;
            cx += (polygon[i].x + polygon[j].x) * cross;
            cy += (polygon[i].y + polygon[j].y) * cross;
        }

        area *= 0.5f;
        if (area == 0.0f) return polygon[0]; // Degenerate polygon fallback

        cx /= (6.0f * area);
        cy /= (6.0f * area);

        return {cx, cy};
    }

    CQVec2 GeomUtils::ClosestPointOnLineSegment2D(const CQVec2& p, const CQVec2& a, const CQVec2& b) {
        glm::vec2 gp(p.x, p.y);
        glm::vec2 ga(a.x, a.y);
        glm::vec2 gb(b.x, b.y);

        glm::vec2 ab = gb - ga;
        float t = glm::dot(gp - ga, ab) / glm::dot(ab, ab);
        t = std::clamp(t, 0.0f, 1.0f);
        
        glm::vec2 closest = ga + t * ab;
        return {closest.x, closest.y};
    }

    bool GeomUtils::TriangleTriangleIntersect3D(const CQVec3& t1v0, const CQVec3& t1v1, const CQVec3& t1v2,
                                                const CQVec3& t2v0, const CQVec3& t2v1, const CQVec3& t2v2) {
        glm::vec3 min1 = glm::min(glm::min(glm::vec3(t1v0.x, t1v0.y, t1v0.z), glm::vec3(t1v1.x, t1v1.y, t1v1.z)), glm::vec3(t1v2.x, t1v2.y, t1v2.z));
        glm::vec3 max1 = glm::max(glm::max(glm::vec3(t1v0.x, t1v0.y, t1v0.z), glm::vec3(t1v1.x, t1v1.y, t1v1.z)), glm::vec3(t1v2.x, t1v2.y, t1v2.z));
        glm::vec3 min2 = glm::min(glm::min(glm::vec3(t2v0.x, t2v0.y, t2v0.z), glm::vec3(t2v1.x, t2v1.y, t2v1.z)), glm::vec3(t2v2.x, t2v2.y, t2v2.z));
        glm::vec3 max2 = glm::max(glm::max(glm::vec3(t2v0.x, t2v0.y, t2v0.z), glm::vec3(t2v1.x, t2v1.y, t2v1.z)), glm::vec3(t2v2.x, t2v2.y, t2v2.z));
        
        if (min1.x > max2.x || max1.x < min2.x || min1.y > max2.y || max1.y < min2.y || min1.z > max2.z || max1.z < min2.z)
            return false;

        auto projectTriangle = [](const glm::vec3& axis, const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, float& minOut, float& maxOut) {
            float p0 = glm::dot(axis, v0);
            float p1 = glm::dot(axis, v1);
            float p2 = glm::dot(axis, v2);
            minOut = std::min({p0, p1, p2});
            maxOut = std::max({p0, p1, p2});
        };

        glm::vec3 u0 = glm::vec3(t1v0.x, t1v0.y, t1v0.z);
        glm::vec3 u1 = glm::vec3(t1v1.x, t1v1.y, t1v1.z);
        glm::vec3 u2 = glm::vec3(t1v2.x, t1v2.y, t1v2.z);
        glm::vec3 v0 = glm::vec3(t2v0.x, t2v0.y, t2v0.z);
        glm::vec3 v1 = glm::vec3(t2v1.x, t2v1.y, t2v1.z);
        glm::vec3 v2 = glm::vec3(t2v2.x, t2v2.y, t2v2.z);

        glm::vec3 e1[3] = { u1 - u0, u2 - u1, u0 - u2 };
        glm::vec3 e2[3] = { v1 - v0, v2 - v1, v0 - v2 };
        glm::vec3 n1 = glm::cross(e1[0], -e1[2]);
        glm::vec3 n2 = glm::cross(e2[0], -e2[2]);

        std::vector<glm::vec3> axes = { n1, n2 };
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                glm::vec3 axis = glm::cross(e1[i], e2[j]);
                if (glm::length(axis) > 1e-6f) axes.push_back(glm::normalize(axis));
            }
        }

        for (const auto& axis : axes) {
            float m1, mx1, m2, mx2;
            projectTriangle(axis, u0, u1, u2, m1, mx1);
            projectTriangle(axis, v0, v1, v2, m2, mx2);
            if (mx1 < m2 || mx2 < m1) return false;
        }
        return true;
    }
}
