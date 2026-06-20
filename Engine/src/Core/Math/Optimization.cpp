#include "Optimization.h"
#include "Random.h"
#include <cmath>

namespace Conqueror::Math {
    std::vector<float> Optimization::SimulatedAnnealing(
        std::function<float(const std::vector<float>&)> objective, 
        std::vector<float> current, 
        float temp, float coolingRate, int iterations) 
    {
        float currentEnergy = objective(current);
        std::vector<float> best = current;
        float bestEnergy = currentEnergy;

        for (int i = 0; i < iterations; ++i) {
            std::vector<float> neighbor = current;
            for (size_t j = 0; j < neighbor.size(); ++j) {
                neighbor[j] += Random::Float(-1.0f, 1.0f);
            }

            float neighborEnergy = objective(neighbor);
            if (neighborEnergy < currentEnergy || std::exp((currentEnergy - neighborEnergy) / temp) > Random::Float()) {
                current = neighbor;
                currentEnergy = neighborEnergy;
            }

            if (currentEnergy < bestEnergy) {
                best = current;
                bestEnergy = currentEnergy;
            }

            temp *= coolingRate;
        }
        return best;
    }

    std::vector<float> Optimization::GradientDescentMomentum(
        std::function<std::vector<float>(const std::vector<float>&)> gradient, 
        std::vector<float> start, 
        float lr, float momentum, int iterations) 
    {
        std::vector<float> current = start;
        std::vector<float> velocity(start.size(), 0.0f);

        for (int i = 0; i < iterations; ++i) {
            std::vector<float> grad = gradient(current);
            for (size_t j = 0; j < current.size(); ++j) {
                velocity[j] = momentum * velocity[j] - lr * grad[j];
                current[j] += velocity[j];
            }
        }
        return current;
    }
}
