#pragma once
#include "Core/Base/Base.h"
#include "VecMath.h"

namespace Conqueror::Math {
    // 6D Spatial Vector for Rigid Body Dynamics (Featherstone's algorithm)
    struct CQ_API SpatialVector {
        CQVec3 w; // angular
        CQVec3 v; // linear
        
        SpatialVector() : w(0,0,0), v(0,0,0) {}
        SpatialVector(const CQVec3& angular, const CQVec3& linear) : w(angular), v(linear) {}
    };

    class CQ_API SpatialAlgebra {
    public:
        static SpatialVector Add(const SpatialVector& a, const SpatialVector& b);
        static SpatialVector Sub(const SpatialVector& a, const SpatialVector& b);
        static SpatialVector Mul(const SpatialVector& a, float scalar);
        
        // Spatial cross product for motion vectors
        static SpatialVector CrossMotion(const SpatialVector& v1, const SpatialVector& v2);
        
        // Spatial cross product for forces
        static SpatialVector CrossForce(const SpatialVector& v, const SpatialVector& f);
        
        static float Dot(const SpatialVector& a, const SpatialVector& b);
    };
}
