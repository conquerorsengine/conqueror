#include "SpatialAlgebra.h"

namespace Conqueror::Math {
    SpatialVector SpatialAlgebra::Add(const SpatialVector& a, const SpatialVector& b) {
        return SpatialVector(Vec3Add(a.w, b.w), Vec3Add(a.v, b.v));
    }

    SpatialVector SpatialAlgebra::Sub(const SpatialVector& a, const SpatialVector& b) {
        return SpatialVector(Vec3Sub(a.w, b.w), Vec3Sub(a.v, b.v));
    }

    SpatialVector SpatialAlgebra::Mul(const SpatialVector& a, float scalar) {
        return SpatialVector(Vec3Mul(a.w, scalar), Vec3Mul(a.v, scalar));
    }

    SpatialVector SpatialAlgebra::CrossMotion(const SpatialVector& v1, const SpatialVector& v2) {
        CQVec3 w = Vec3Cross(v1.w, v2.w);
        CQVec3 v = Vec3Add(Vec3Cross(v1.w, v2.v), Vec3Cross(v1.v, v2.w));
        return SpatialVector(w, v);
    }

    SpatialVector SpatialAlgebra::CrossForce(const SpatialVector& v, const SpatialVector& f) {
        CQVec3 w = Vec3Add(Vec3Cross(v.w, f.w), Vec3Cross(v.v, f.v));
        CQVec3 vf = Vec3Cross(v.w, f.v);
        return SpatialVector(w, vf);
    }

    float SpatialAlgebra::Dot(const SpatialVector& a, const SpatialVector& b) {
        return Vec3Dot(a.w, b.w) + Vec3Dot(a.v, b.v);
    }
}
