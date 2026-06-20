#pragma once
#include "Core/Base/Base.h"
#include <vector>

namespace Conqueror::Math {
    class CQ_API MLUtils {
    public:
        static double CalculateMean(const std::vector<double>& data);
        static double CalculateVariance(const std::vector<double>& data);
        static double CalculateStandardDeviation(const std::vector<double>& data);

        struct RegressionResult {
            double slope;
            double intercept;
        };
        static RegressionResult LinearRegression(const std::vector<double>& x, const std::vector<double>& y);

        // K-Means clustering for 1D data. Returns a vector of cluster indices for each data point.
        static std::vector<int> KMeansCluster1D(const std::vector<double>& data, int k, int maxIterations = 100);
    };
}
