#pragma once
#include "Core/Base/Base.h"
#include "VecMath.h"
#include <vector>

namespace Conqueror::Math {
    // Spherical Harmonics L=3 (9 coefficients) for advanced GI / Ambient Lighting
    class CQ_API SphericalHarmonics {
    public:
        static void EvaluateBasisL3(const CQVec3& dir, float* outBasis);
        static CQVec3 EvaluateLightL3(const CQVec3& dir, const CQVec3* coeffs);
        static void ProjectDirectionalLightL3(const CQVec3& dir, const CQVec3& color, float intensity, CQVec3* outCoeffs);
        static void AddAmbientLightL3(const CQVec3& color, CQVec3* outCoeffs);
    };
}
