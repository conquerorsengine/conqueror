#include "Delaunay.h"
#include <cmath>
#include <algorithm>

namespace Conqueror::Math {
    bool Delaunay::CircumCircleContains(const CQVec2& a, const CQVec2& b, const CQVec2& c, const CQVec2& point) {
        float ab = a.x * a.x + a.y * a.y;
        float cd = b.x * b.x + b.y * b.y;
        float ef = c.x * c.x + c.y * c.y;

        float circumX = (ab * (c.y - b.y) + cd * (a.y - c.y) + ef * (b.y - a.y)) / (a.x * (c.y - b.y) + b.x * (a.y - c.y) + c.x * (b.y - a.y)) / 2.0f;
        float circumY = (ab * (c.x - b.x) + cd * (a.x - c.x) + ef * (b.x - a.x)) / (a.y * (c.x - b.x) + b.y * (a.x - c.x) + c.y * (b.x - a.x)) / 2.0f;
        float circumRadiusSq = (a.x - circumX) * (a.x - circumX) + (a.y - circumY) * (a.y - circumY);

        float distSq = (point.x - circumX) * (point.x - circumX) + (point.y - circumY) * (point.y - circumY);
        return distSq <= circumRadiusSq;
    }

    std::vector<Triangle2D> Delaunay::Triangulate(const std::vector<CQVec2>& points) {
        std::vector<Triangle2D> triangles;
        if (points.size() < 3) return triangles;

        // Create super triangle that encompasses all points
        float minX = points[0].x, minY = points[0].y;
        float maxX = minX, maxY = minY;
        for (const auto& p : points) {
            if (p.x < minX) minX = p.x;
            if (p.y < minY) minY = p.y;
            if (p.x > maxX) maxX = p.x;
            if (p.y > maxY) maxY = p.y;
        }
        float dx = maxX - minX;
        float dy = maxY - minY;
        float deltaMax = std::max(dx, dy);
        float midx = (minX + maxX) / 2.0f;
        float midy = (minY + maxY) / 2.0f;

        CQVec2 p1(midx - 20 * deltaMax, midy - deltaMax);
        CQVec2 p2(midx, midy + 20 * deltaMax);
        CQVec2 p3(midx + 20 * deltaMax, midy - deltaMax);

        std::vector<CQVec2> pts = points;
        pts.push_back(p1);
        pts.push_back(p2);
        pts.push_back(p3);

        int n = pts.size();
        triangles.push_back({n - 3, n - 2, n - 1});

        for (int i = 0; i < n - 3; ++i) {
            std::vector<Triangle2D> badTriangles;
            std::vector<Edge2D> polygon;

            for (const auto& t : triangles) {
                if (CircumCircleContains(pts[t.p1], pts[t.p2], pts[t.p3], pts[i])) {
                    badTriangles.push_back(t);
                    polygon.push_back({t.p1, t.p2});
                    polygon.push_back({t.p2, t.p3});
                    polygon.push_back({t.p3, t.p1});
                }
            }

            auto isShared = [&](const Edge2D& edge) {
                int count = 0;
                for (const auto& e : polygon) {
                    if (e == edge) count++;
                }
                return count > 1;
            };

            std::vector<Edge2D> uniqueEdges;
            for (const auto& e : polygon) {
                if (!isShared(e)) uniqueEdges.push_back(e);
            }

            triangles.erase(std::remove_if(triangles.begin(), triangles.end(), [&](const Triangle2D& t) {
                for (const auto& bt : badTriangles) {
                    if (t.p1 == bt.p1 && t.p2 == bt.p2 && t.p3 == bt.p3) return true;
                }
                return false;
            }), triangles.end());

            for (const auto& e : uniqueEdges) {
                triangles.push_back({e.p1, e.p2, i});
            }
        }

        triangles.erase(std::remove_if(triangles.begin(), triangles.end(), [&](const Triangle2D& t) {
            bool hasSuper = (t.p1 >= n - 3 || t.p2 >= n - 3 || t.p3 >= n - 3);
            return hasSuper;
        }), triangles.end());

        return triangles;
    }
}
