#pragma once
#include "Core/Base/Base.h"
#include "VecMath.h"

namespace Conqueror::Math {
    // Smoothed Particle Hydrodynamics (SPH) Math Kernels
    class CQ_API FluidDynamics {
    public:
        static float SPH_Poly6(const CQVec3& r, float h);
        static CQVec3 SPH_SpikyGradient(const CQVec3& r, float h);
        static float SPH_ViscosityLaplacian(const CQVec3& r, float h);
        static float CalculatePressure(float density, float restDensity, float stiffness);
    };
}
