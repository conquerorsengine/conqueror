#pragma once
#include "Core/Base/Base.h"
#include <vector>
#include <functional>

namespace Conqueror::Math {
    class CQ_API Optimization {
    public:
        // Simulated Annealing (Tavlama Benzetimi)
        static std::vector<float> SimulatedAnnealing(
            std::function<float(const std::vector<float>&)> objective, 
            std::vector<float> current, 
            float temp, float coolingRate, int iterations);
            
        // Gradient Descent with Momentum
        static std::vector<float> GradientDescentMomentum(
            std::function<std::vector<float>(const std::vector<float>&)> gradient, 
            std::vector<float> start, 
            float lr, float momentum, int iterations);
    };
}
