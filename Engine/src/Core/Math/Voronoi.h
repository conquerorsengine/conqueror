#pragma once
#include "Core/Base/Base.h"
#include "VecMath.h"
#include <vector>

namespace Conqueror::Math {
    class CQ_API Voronoi {
    public:
        // Lloyd's Relaxation algorithm to distribute points evenly inside a bounding box
        static std::vector<CQVec2> LloydRelaxation(const std::vector<CQVec2>& points, int iterations, const CQVec2& minBounds, const CQVec2& maxBounds);
    };
}
