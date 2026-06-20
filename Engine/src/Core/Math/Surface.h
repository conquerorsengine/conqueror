#pragma once
#include "Core/Base/Base.h"
#include "VecMath.h"
#include <vector>

namespace Conqueror::Math {
    class CQ_API Surface {
    public:
        // Interpolasyon (Bilinear & Bicubic)
        static CQVec3 BilinearInterpolation(const CQVec3& p00, const CQVec3& p10, const CQVec3& p01, const CQVec3& p11, float u, float v);
        
        // Parametrik Bezier Yüzeyi (u,v -> [0, 1])
        static CQVec3 BezierSurface(const std::vector<std::vector<CQVec3>>& controlPoints, float u, float v);
        
        // Bezier Yüzey Normali Hesaplama
        static CQVec3 BezierSurfaceNormal(const std::vector<std::vector<CQVec3>>& controlPoints, float u, float v);
    };
}
