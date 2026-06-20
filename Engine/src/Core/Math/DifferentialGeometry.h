#pragma once
#include "Core/Base/Base.h"
#include "VecMath.h"

namespace Conqueror::Math {
    class CQ_API DifferentialGeometry {
    public:
        // Frenet-Serret Reference Frame for Curves
        static void FrenetSerretFrame(const CQVec3& dp, const CQVec3& ddp, CQVec3& outT, CQVec3& outN, CQVec3& outB);
        
        // Curve Curvature
        static float Curvature(const CQVec3& dp, const CQVec3& ddp);
        
        // Curve Torsion
        static float Torsion(const CQVec3& dp, const CQVec3& ddp, const CQVec3& dddp);
        
        // Surface Gaussian Curvature (using First and Second Fundamental Form coefficients)
        static float GaussianCurvature(float E, float F, float G, float L, float M, float N);
        
        // Surface Mean Curvature
        static float MeanCurvature(float E, float F, float G, float L, float M, float N);
    };
}
