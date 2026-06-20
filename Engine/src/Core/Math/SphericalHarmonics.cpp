#include "SphericalHarmonics.h"
#include <cmath>

namespace Conqueror::Math {
    void SphericalHarmonics::EvaluateBasisL3(const CQVec3& dir, float* outBasis) {
        float x = dir.x, y = dir.y, z = dir.z;
        float x2 = x*x, y2 = y*y, z2 = z*z;
        
        outBasis[0] = 0.282095f;
        outBasis[1] = 0.488603f * y;
        outBasis[2] = 0.488603f * z;
        outBasis[3] = 0.488603f * x;
        outBasis[4] = 1.092548f * x * y;
        outBasis[5] = 1.092548f * y * z;
        outBasis[6] = 0.315392f * (3.0f * z2 - 1.0f);
        outBasis[7] = 1.092548f * x * z;
        outBasis[8] = 0.546274f * (x2 - y2);
    }

    CQVec3 SphericalHarmonics::EvaluateLightL3(const CQVec3& dir, const CQVec3* coeffs) {
        float basis[9];
        EvaluateBasisL3(dir, basis);
        
        CQVec3 color(0,0,0);
        for(int i = 0; i < 9; ++i) {
            color = Vec3Add(color, Vec3Mul(coeffs[i], basis[i]));
        }
        // Clamp to zero to avoid negative light
        if (color.x < 0) color.x = 0;
        if (color.y < 0) color.y = 0;
        if (color.z < 0) color.z = 0;
        return color;
    }

    void SphericalHarmonics::ProjectDirectionalLightL3(const CQVec3& dir, const CQVec3& color, float intensity, CQVec3* outCoeffs) {
        float basis[9];
        EvaluateBasisL3(Vec3Normalize(dir), basis);
        
        // PI constant factor for directional light SH projection
        const float PI = 3.1415926535f;
        float factors[9] = {
            PI, 
            2.0f * PI / 3.0f, 2.0f * PI / 3.0f, 2.0f * PI / 3.0f,
            PI / 4.0f, PI / 4.0f, PI / 4.0f, PI / 4.0f, PI / 4.0f
        };
        
        for(int i = 0; i < 9; ++i) {
            CQVec3 projected = Vec3Mul(color, intensity * basis[i] * factors[i]);
            outCoeffs[i] = Vec3Add(outCoeffs[i], projected);
        }
    }

    void SphericalHarmonics::AddAmbientLightL3(const CQVec3& color, CQVec3* outCoeffs) {
        // Ambient light only affects the DC term (index 0)
        const float PI = 3.1415926535f;
        float factor = 2.0f * PI * 0.282095f; 
        outCoeffs[0] = Vec3Add(outCoeffs[0], Vec3Mul(color, factor));
    }
}
