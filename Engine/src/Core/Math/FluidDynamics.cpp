#include "FluidDynamics.h"
#include <cmath>

namespace Conqueror::Math {
    const float PI = 3.14159265358979323846f;

    float FluidDynamics::SPH_Poly6(const CQVec3& r, float h) {
        float rLenSq = r.x*r.x + r.y*r.y + r.z*r.z;
        float hSq = h*h;
        if (rLenSq >= hSq || rLenSq == 0.0f) return 0.0f;
        
        float diff = hSq - rLenSq;
        float coeff = 315.0f / (64.0f * PI * std::pow(h, 9.0f));
        return coeff * diff * diff * diff;
    }

    CQVec3 FluidDynamics::SPH_SpikyGradient(const CQVec3& r, float h) {
        float rLen = std::sqrt(r.x*r.x + r.y*r.y + r.z*r.z);
        if (rLen >= h || rLen == 0.0f) return CQVec3(0,0,0);
        
        float diff = h - rLen;
        float coeff = -45.0f / (PI * std::pow(h, 6.0f));
        float magnitude = coeff * diff * diff;
        
        return CQVec3((r.x / rLen) * magnitude, (r.y / rLen) * magnitude, (r.z / rLen) * magnitude);
    }

    float FluidDynamics::SPH_ViscosityLaplacian(const CQVec3& r, float h) {
        float rLen = std::sqrt(r.x*r.x + r.y*r.y + r.z*r.z);
        if (rLen >= h || rLen == 0.0f) return 0.0f;
        
        float coeff = 45.0f / (PI * std::pow(h, 6.0f));
        return coeff * (h - rLen);
    }

    float FluidDynamics::CalculatePressure(float density, float restDensity, float stiffness) {
        return stiffness * (density - restDensity);
    }
}
