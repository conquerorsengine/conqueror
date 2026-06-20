#pragma once
#include "Core/Base/Base.h"
#include "VecMath.h"
#include <vector>

namespace Conqueror::Math
{
    class CQ_API Sampling
    {
    public:
        // Poisson Disk Sampling
        // Returns a list of points randomly distributed but never closer than 'radius' to each other.
        static std::vector<CQVec2> PoissonDiskSampling2D(float radius, const CQVec2& sampleRegionSize, int numSamplesBeforeRejection = 30);
        
        // Low Discrepancy Sequences for Monte Carlo / Ray Tracing
        static float HaltonSequence(int index, int base);
        static CQVec2 HammersleySet2D(int i, int N);
        
        // Hemisphere Sampling for Lighting
        static CQVec3 UniformSampleHemisphere(float u1, float u2);
        static CQVec3 CosineSampleHemisphere(float u1, float u2);
    };
}