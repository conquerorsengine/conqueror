#include "Statistics.h"
#include <cmath>
#include <numeric>

namespace Conqueror::Math {

    const double PI = 3.14159265358979323846;

    // Faktöriyel (Yardımcı)
    static double Factorial(int n) {
        if (n <= 1) return 1.0;
        double res = 1.0;
        for (int i = 2; i <= n; ++i) res *= i;
        return res;
    }

    double Statistics::NormalPDF(double x, double mean, double stddev) {
        double expPart = std::exp(-0.5 * std::pow((x - mean) / stddev, 2));
        return (1.0 / (stddev * std::sqrt(2 * PI))) * expPart;
    }

    double Statistics::PoissonPMF(int k, double lambda) {
        if (k < 0) return 0.0;
        return (std::pow(lambda, k) * std::exp(-lambda)) / Factorial(k);
    }

    double Statistics::BinomialPMF(int k, int n, double p) {
        if (k < 0 || k > n) return 0.0;
        double comb = Factorial(n) / (Factorial(k) * Factorial(n - k));
        return comb * std::pow(p, k) * std::pow(1.0 - p, n - k);
    }

    double Statistics::NormalCDF(double x, double mean, double stddev) {
        return 0.5 * (1.0 + std::erf((x - mean) / (stddev * std::sqrt(2.0))));
    }

    double Statistics::Mean(const std::vector<double>& data) {
        if (data.empty()) return 0.0;
        double sum = std::accumulate(data.begin(), data.end(), 0.0);
        return sum / data.size();
    }

    double Statistics::Variance(const std::vector<double>& data, bool sample) {
        if (data.size() <= (sample ? 1 : 0)) return 0.0;
        double mean = Mean(data);
        double sumSq = 0.0;
        for (double x : data) {
            sumSq += (x - mean) * (x - mean);
        }
        return sumSq / (data.size() - (sample ? 1 : 0));
    }

    double Statistics::StandardDeviation(const std::vector<double>& data, bool sample) {
        return std::sqrt(Variance(data, sample));
    }

    double Statistics::Covariance(const std::vector<double>& dataX, const std::vector<double>& dataY, bool sample) {
        if (dataX.size() != dataY.size() || dataX.size() <= (sample ? 1 : 0)) return 0.0;
        double meanX = Mean(dataX);
        double meanY = Mean(dataY);
        double sum = 0.0;
        for (size_t i = 0; i < dataX.size(); ++i) {
            sum += (dataX[i] - meanX) * (dataY[i] - meanY);
        }
        return sum / (dataX.size() - (sample ? 1 : 0));
    }

    double Statistics::CorrelationPearson(const std::vector<double>& dataX, const std::vector<double>& dataY) {
        double cov = Covariance(dataX, dataY, true);
        double stdX = StandardDeviation(dataX, true);
        double stdY = StandardDeviation(dataY, true);
        if (stdX == 0 || stdY == 0) return 0.0;
        return cov / (stdX * stdY);
    }

    double Statistics::ZTest(double sampleMean, double popMean, double popStdDev, int n) {
        if (n <= 0 || popStdDev <= 0) return 0.0;
        return (sampleMean - popMean) / (popStdDev / std::sqrt(n));
    }

    double Statistics::TTestOneSample(const std::vector<double>& sample, double popMean) {
        if (sample.size() <= 1) return 0.0;
        double mean = Mean(sample);
        double stddev = StandardDeviation(sample, true);
        return (mean - popMean) / (stddev / std::sqrt(sample.size()));
    }
}
