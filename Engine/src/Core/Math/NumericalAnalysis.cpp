#include "NumericalAnalysis.h"
#include <cmath>
#include <algorithm>

namespace Conqueror::Math {

    std::vector<double> NumericalAnalysis::GradientDescent(
        const std::function<double(const std::vector<double>&)>& func,
        std::vector<double> startPoint,
        double learningRate,
        int maxIterations,
        double tolerance)
    {
        std::vector<double> current = startPoint;
        int n = current.size();
        std::vector<double> grad(n);
        double h = 1e-5;

        for (int iter = 0; iter < maxIterations; ++iter) {
            double maxGrad = 0.0;
            for (int i = 0; i < n; ++i) {
                std::vector<double> p_plus = current;
                std::vector<double> p_minus = current;
                p_plus[i] += h;
                p_minus[i] -= h;
                grad[i] = (func(p_plus) - func(p_minus)) / (2.0 * h);
                maxGrad = std::max(maxGrad, std::abs(grad[i]));
            }

            if (maxGrad < tolerance) break;

            for (int i = 0; i < n; ++i) {
                current[i] -= learningRate * grad[i];
            }
        }
        return current;
    }

    double NumericalAnalysis::NewtonRaphson(
        const std::function<double(double)>& func,
        const std::function<double(double)>& deriv,
        double startPoint,
        int maxIterations,
        double tolerance)
    {
        double x = startPoint;
        for (int i = 0; i < maxIterations; ++i) {
            double fx = func(x);
            if (std::abs(fx) < tolerance) return x;
            double dfx = deriv(x);
            if (std::abs(dfx) < 1e-10) break; // Sıfıra bölme hatasını engelle
            x = x - fx / dfx;
        }
        return x;
    }

    double NumericalAnalysis::RombergIntegration(
        const std::function<double(double)>& func,
        double a, double b,
        int maxSteps,
        double tolerance)
    {
        std::vector<std::vector<double>> R(maxSteps, std::vector<double>(maxSteps, 0.0));
        
        R[0][0] = 0.5 * (b - a) * (func(a) + func(b));

        for (int i = 1; i < maxSteps; ++i) {
            double h = (b - a) / std::pow(2, i);
            double sum = 0.0;
            for (int k = 1; k <= std::pow(2, i - 1); ++k) {
                sum += func(a + (2 * k - 1) * h);
            }
            R[i][0] = 0.5 * R[i - 1][0] + sum * h;

            for (int j = 1; j <= i; ++j) {
                R[i][j] = R[i][j - 1] + (R[i][j - 1] - R[i - 1][j - 1]) / (std::pow(4, j) - 1.0);
            }

            if (std::abs(R[i][i] - R[i - 1][i - 1]) < tolerance) {
                return R[i][i];
            }
        }
        return R[maxSteps - 1][maxSteps - 1];
    }

    void NumericalAnalysis::RKF45(
        const std::function<double(double, double)>& func,
        double t0, double y0, double tEnd,
        double& tOut, double& yOut,
        double initialStepSize,
        double tolerance)
    {
        double t = t0;
        double y = y0;
        double h = initialStepSize;

        // Butcher tablosu (RKF45)
        const double c2 = 1.0/4.0, c3 = 3.0/8.0, c4 = 12.0/13.0, c5 = 1.0, c6 = 1.0/2.0;
        const double a21 = 1.0/4.0;
        const double a31 = 3.0/32.0, a32 = 9.0/32.0;
        const double a41 = 1932.0/2197.0, a42 = -7200.0/2197.0, a43 = 7296.0/2197.0;
        const double a51 = 439.0/216.0, a52 = -8.0, a53 = 3680.0/513.0, a54 = -845.0/4104.0;
        const double a61 = -8.0/27.0, a62 = 2.0, a63 = -3544.0/2565.0, a64 = 1859.0/4104.0, a65 = -11.0/40.0;
        const double b1 = 16.0/135.0, b3 = 6656.0/12825.0, b4 = 28561.0/56430.0, b5 = -9.0/50.0, b6 = 2.0/55.0;
        const double bs1 = 25.0/216.0, bs3 = 1408.0/2565.0, bs4 = 2197.0/4104.0, bs5 = -1.0/5.0;

        while (t < tEnd) {
            if (t + h > tEnd) h = tEnd - t;

            double k1 = h * func(t, y);
            double k2 = h * func(t + c2 * h, y + a21 * k1);
            double k3 = h * func(t + c3 * h, y + a31 * k1 + a32 * k2);
            double k4 = h * func(t + c4 * h, y + a41 * k1 + a42 * k2 + a43 * k3);
            double k5 = h * func(t + c5 * h, y + a51 * k1 + a52 * k2 + a53 * k3 + a54 * k4);
            double k6 = h * func(t + c6 * h, y + a61 * k1 + a62 * k2 + a63 * k3 + a64 * k4 + a65 * k5);

            double y4 = y + bs1 * k1 + bs3 * k3 + bs4 * k4 + bs5 * k5;
            double y5 = y + b1 * k1 + b3 * k3 + b4 * k4 + b5 * k5 + b6 * k6;

            double error = std::abs(y5 - y4);
            if (error <= tolerance) {
                t += h;
                y = y5;
            }
            
            double s = 0.84 * std::pow(tolerance * h / error, 0.25);
            h = h * std::clamp(s, 0.1, 4.0); // Adım boyutunu sınırla
        }

        tOut = t;
        yOut = y;
    }

}
