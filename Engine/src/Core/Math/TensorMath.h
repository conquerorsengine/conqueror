#pragma once
#include "Core/Base/Base.h"
#include "VecMath.h"

namespace Conqueror::Math {
    class CQ_API TensorMath {
    public:
        // Rank-2 Tensor (3x3 Matrix equivalent for physical stress/inertia)
        struct Rank2Tensor { float m[3][3]; };
        
        // Rank-3 Tensor (3x3x3)
        struct Rank3Tensor { float m[3][3][3]; };

        static Rank2Tensor OuterProduct(const CQVec3& a, const CQVec3& b);
        static CQVec3 Contract(const Rank2Tensor& t, const CQVec3& v);
        static float Trace(const Rank2Tensor& t);
        static Rank2Tensor Add(const Rank2Tensor& a, const Rank2Tensor& b);
        static float InvariantJ2(const Rank2Tensor& t);
    };
}
