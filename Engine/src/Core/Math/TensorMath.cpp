#include "TensorMath.h"
#include <cmath>

namespace Conqueror::Math {
    TensorMath::Rank2Tensor TensorMath::OuterProduct(const CQVec3& a, const CQVec3& b) {
        Rank2Tensor r;
        r.m[0][0] = a.x * b.x; r.m[0][1] = a.x * b.y; r.m[0][2] = a.x * b.z;
        r.m[1][0] = a.y * b.x; r.m[1][1] = a.y * b.y; r.m[1][2] = a.y * b.z;
        r.m[2][0] = a.z * b.x; r.m[2][1] = a.z * b.y; r.m[2][2] = a.z * b.z;
        return r;
    }

    CQVec3 TensorMath::Contract(const Rank2Tensor& t, const CQVec3& v) {
        return CQVec3(
            t.m[0][0]*v.x + t.m[0][1]*v.y + t.m[0][2]*v.z,
            t.m[1][0]*v.x + t.m[1][1]*v.y + t.m[1][2]*v.z,
            t.m[2][0]*v.x + t.m[2][1]*v.y + t.m[2][2]*v.z
        );
    }

    float TensorMath::Trace(const Rank2Tensor& t) {
        return t.m[0][0] + t.m[1][1] + t.m[2][2];
    }

    TensorMath::Rank2Tensor TensorMath::Add(const Rank2Tensor& a, const Rank2Tensor& b) {
        Rank2Tensor r;
        for(int i=0; i<3; ++i)
            for(int j=0; j<3; ++j)
                r.m[i][j] = a.m[i][j] + b.m[i][j];
        return r;
    }

    float TensorMath::InvariantJ2(const Rank2Tensor& t) {
        float tr = Trace(t) / 3.0f;
        Rank2Tensor s = t;
        s.m[0][0] -= tr; s.m[1][1] -= tr; s.m[2][2] -= tr;
        
        float j2 = 0.0f;
        for(int i=0; i<3; ++i)
            for(int j=0; j<3; ++j)
                j2 += s.m[i][j] * s.m[j][i];
        return 0.5f * j2;
    }
}
