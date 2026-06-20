#pragma once
#include "Core/Base/Base.h"
#include <functional>

namespace Conqueror::Math {
    class CQ_API Calculus {
    public:
        // Türev (Differentiation)
        static double DerivativeForward(const std::function<double(double)>& f, double x, double h = 1e-5);
        static double DerivativeCentral(const std::function<double(double)>& f, double x, double h = 1e-5);
        static double DerivativeBackward(const std::function<double(double)>& f, double x, double h = 1e-5);
        static double SecondDerivative(const std::function<double(double)>& f, double x, double h = 1e-4);

        // İntegral (Integration)
        static double IntegralTrapezoidal(const std::function<double(double)>& f, double a, double b, int n = 1000);
        static double IntegralSimpson13(const std::function<double(double)>& f, double a, double b, int n = 1000);
        static double IntegralSimpson38(const std::function<double(double)>& f, double a, double b, int n = 999);
        static double IntegralGaussianQuadrature(const std::function<double(double)>& f, double a, double b);

        // Kök Bulma (Root Finding)
        static double RootBisection(const std::function<double(double)>& f, double a, double b, double tol = 1e-7, int maxIter = 100);
        static double RootNewtonRaphson(const std::function<double(double)>& f, const std::function<double(double)>& df, double guess, double tol = 1e-7, int maxIter = 100);
        static double RootSecant(const std::function<double(double)>& f, double x0, double x1, double tol = 1e-7, int maxIter = 100);
    };
}
