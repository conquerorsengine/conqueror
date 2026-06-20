#include "FuzzyLogic.h"
#include <cmath>
#include <algorithm>

namespace Conqueror::Math {
    float FuzzyLogic::Triangle(float x, float a, float b, float c) {
        if (x <= a || x >= c) return 0.0f;
        if (x == b) return 1.0f;
        if (x < b) return (x - a) / (b - a);
        return (c - x) / (c - b);
    }

    float FuzzyLogic::Trapezoid(float x, float a, float b, float c, float d) {
        if (x <= a || x >= d) return 0.0f;
        if (x >= b && x <= c) return 1.0f;
        if (x < b) return (x - a) / (b - a);
        return (d - x) / (d - c);
    }

    float FuzzyLogic::Gaussian(float x, float c, float sigma) {
        if (sigma == 0.0f) return x == c ? 1.0f : 0.0f;
        float exp_val = -0.5f * std::pow((x - c) / sigma, 2.0f);
        return std::exp(exp_val);
    }

    float FuzzyLogic::DefuzzifyCentroid(float min, float max, int steps, std::function<float(float)> aggregatedSet) {
        if (steps <= 0) return min;
        float stepSize = (max - min) / steps;
        float num = 0.0f;
        float den = 0.0f;
        
        for (int i = 0; i <= steps; ++i) {
            float x = min + i * stepSize;
            float mu = aggregatedSet(x);
            num += x * mu;
            den += mu;
        }
        
        if (den == 0.0f) return (min + max) / 2.0f;
        return num / den;
    }
}
