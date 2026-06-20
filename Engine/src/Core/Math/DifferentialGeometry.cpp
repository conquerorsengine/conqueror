#include "DifferentialGeometry.h"

namespace Conqueror::Math {
    void DifferentialGeometry::FrenetSerretFrame(const CQVec3& dp, const CQVec3& ddp, CQVec3& outT, CQVec3& outN, CQVec3& outB) {
        outT = Vec3Normalize(dp);
        CQVec3 crossDpDdp = Vec3Cross(dp, ddp);
        outB = Vec3Normalize(crossDpDdp);
        outN = Vec3Cross(outB, outT);
    }

    float DifferentialGeometry::Curvature(const CQVec3& dp, const CQVec3& ddp) {
        CQVec3 crossP = Vec3Cross(dp, ddp);
        float num = Vec3Length(crossP);
        float den = Vec3Length(dp);
        if (den == 0.0f) return 0.0f;
        return num / (den * den * den);
    }

    float DifferentialGeometry::Torsion(const CQVec3& dp, const CQVec3& ddp, const CQVec3& dddp) {
        CQVec3 crossP = Vec3Cross(dp, ddp);
        float num = Vec3Dot(crossP, dddp);
        float den = Vec3Length(crossP);
        den = den * den;
        if (den == 0.0f) return 0.0f;
        return num / den;
    }

    float DifferentialGeometry::GaussianCurvature(float E, float F, float G, float L, float M, float N) {
        float den = E * G - F * F;
        if (den == 0.0f) return 0.0f;
        return (L * N - M * M) / den;
    }

    float DifferentialGeometry::MeanCurvature(float E, float F, float G, float L, float M, float N) {
        float den = 2.0f * (E * G - F * F);
        if (den == 0.0f) return 0.0f;
        return (E * N - 2.0f * F * M + G * L) / den;
    }
}
