#pragma once
#include "Core/Base/Base.h"
#include "VecMath.h"
#include <vector>

namespace Conqueror::Math {
    struct Edge2D {
        int p1, p2;
        bool operator==(const Edge2D& other) const {
            return (p1 == other.p1 && p2 == other.p2) || (p1 == other.p2 && p2 == other.p1);
        }
    };

    struct Triangle2D {
        int p1, p2, p3;
        bool ContainsEdge(const Edge2D& edge) const {
            return (edge.p1 == p1 || edge.p1 == p2 || edge.p1 == p3) &&
                   (edge.p2 == p1 || edge.p2 == p2 || edge.p2 == p3);
        }
    };

    class CQ_API Delaunay {
    public:
        // Bowyer-Watson Algorithm for 2D Delaunay Triangulation
        static std::vector<Triangle2D> Triangulate(const std::vector<CQVec2>& points);
        static bool CircumCircleContains(const CQVec2& a, const CQVec2& b, const CQVec2& c, const CQVec2& point);
    };
}
