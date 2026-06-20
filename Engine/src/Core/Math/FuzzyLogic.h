#pragma once
#include "Core/Base/Base.h"
#include <functional>

namespace Conqueror::Math {
    class CQ_API FuzzyLogic {
    public:
        // Membership Functions
        static float Triangle(float x, float a, float b, float c);
        static float Trapezoid(float x, float a, float b, float c, float d);
        static float Gaussian(float x, float c, float sigma);
        
        // Defuzzification (Centroid method)
        static float DefuzzifyCentroid(float min, float max, int steps, std::function<float(float)> aggregatedSet);
    };
}
