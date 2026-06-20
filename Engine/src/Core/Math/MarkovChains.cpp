#include "MarkovChains.h"

namespace Conqueror::Math {
    int MarkovChains::PredictNextState(int currentState, const std::vector<std::vector<float>>& transitionMatrix, float randomValue0To1) {
        if (currentState < 0 || static_cast<size_t>(currentState) >= transitionMatrix.size()) return currentState;
        
        const auto& probabilities = transitionMatrix[currentState];
        float cumulative = 0.0f;
        
        for (size_t i = 0; i < probabilities.size(); ++i) {
            cumulative += probabilities[i];
            if (randomValue0To1 <= cumulative) {
                return i;
            }
        }
        return probabilities.size() - 1;
    }

    std::vector<std::vector<float>> MarkovChains::ComputeNStepTransition(const std::vector<std::vector<float>>& matrix, int steps) {
        int n = matrix.size();
        std::vector<std::vector<float>> result = matrix;
        std::vector<std::vector<float>> temp(n, std::vector<float>(n, 0.0f));

        for (int step = 1; step < steps; ++step) {
            for (int i = 0; i < n; ++i) {
                for (int j = 0; j < n; ++j) {
                    temp[i][j] = 0.0f;
                    for (int k = 0; k < n; ++k) {
                        temp[i][j] += result[i][k] * matrix[k][j];
                    }
                }
            }
            result = temp;
        }
        return result;
    }
}
