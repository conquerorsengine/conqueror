#pragma once
#include "Core/Base/Base.h"
#include <vector>

namespace Conqueror::Math {
    class CQ_API MarkovChains {
    public:
        // Predict the next state based on the current state and transition matrix
        static int PredictNextState(int currentState, const std::vector<std::vector<float>>& transitionMatrix, float randomValue0To1);
        
        // Compute N-step transition probability matrix
        static std::vector<std::vector<float>> ComputeNStepTransition(const std::vector<std::vector<float>>& matrix, int steps);
    };
}
