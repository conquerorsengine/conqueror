#include "MLUtils.h"
#include <cmath>
#include <numeric>
#include <algorithm>
#include <limits>
#include <cstdlib>

namespace Conqueror::Math {
    double MLUtils::CalculateMean(const std::vector<double>& data) {
        if (data.empty()) return 0.0;
        double sum = std::accumulate(data.begin(), data.end(), 0.0);
        return sum / data.size();
    }

    double MLUtils::CalculateVariance(const std::vector<double>& data) {
        if (data.empty()) return 0.0;
        double mean = CalculateMean(data);
        double varSum = 0.0;
        for (double val : data) {
            varSum += (val - mean) * (val - mean);
        }
        return varSum / data.size();
    }

    double MLUtils::CalculateStandardDeviation(const std::vector<double>& data) {
        return std::sqrt(CalculateVariance(data));
    }

    MLUtils::RegressionResult MLUtils::LinearRegression(const std::vector<double>& x, const std::vector<double>& y) {
        RegressionResult result = {0.0, 0.0};
        if (x.empty() || x.size() != y.size()) return result;

        double sumX = 0, sumY = 0, sumXY = 0, sumXX = 0;
        int n = x.size();
        for (int i = 0; i < n; i++) {
            sumX += x[i];
            sumY += y[i];
            sumXY += x[i] * y[i];
            sumXX += x[i] * x[i];
        }

        double denominator = (n * sumXX - sumX * sumX);
        if (std::abs(denominator) < 1e-9) return result;

        result.slope = (n * sumXY - sumX * sumY) / denominator;
        result.intercept = (sumY - result.slope * sumX) / n;
        return result;
    }

    std::vector<int> MLUtils::KMeansCluster1D(const std::vector<double>& data, int k, int maxIterations) {
        std::vector<int> assignments(data.size(), 0);
        if (data.empty() || k <= 0) return assignments;
        if (k >= data.size()) {
            for (int i = 0; i < data.size(); i++) assignments[i] = i;
            return assignments;
        }

        std::vector<double> centroids(k);
        // Initialize centroids randomly from data points
        for (int i = 0; i < k; i++) {
            centroids[i] = data[rand() % data.size()];
        }

        bool changed = true;
        int iterations = 0;
        while (changed && iterations < maxIterations) {
            changed = false;
            iterations++;

            // Assign points to nearest centroid
            for (size_t i = 0; i < data.size(); i++) {
                int bestCluster = 0;
                double minDistance = std::numeric_limits<double>::max();
                for (int j = 0; j < k; j++) {
                    double dist = std::abs(data[i] - centroids[j]);
                    if (dist < minDistance) {
                        minDistance = dist;
                        bestCluster = j;
                    }
                }
                if (assignments[i] != bestCluster) {
                    assignments[i] = bestCluster;
                    changed = true;
                }
            }

            // Update centroids
            std::vector<double> newCentroids(k, 0.0);
            std::vector<int> counts(k, 0);
            for (size_t i = 0; i < data.size(); i++) {
                newCentroids[assignments[i]] += data[i];
                counts[assignments[i]]++;
            }
            for (int j = 0; j < k; j++) {
                if (counts[j] > 0) centroids[j] = newCentroids[j] / counts[j];
            }
        }
        return assignments;
    }
}
