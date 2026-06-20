#include "Calculus.h"
#include <cmath>
#include <stdexcept>

namespace Conqueror::Math {
    double Calculus::DerivativeForward(const std::function<double(double)>& f, double x, double h) {
        return (f(x + h) - f(x)) / h;
    }

    double Calculus::DerivativeCentral(const std::function<double(double)>& f, double x, double h) {
        return (f(x + h) - f(x - h)) / (2.0 * h);
    }

    double Calculus::DerivativeBackward(const std::function<double(double)>& f, double x, double h) {
        return (f(x) - f(x - h)) / h;
    }

    double Calculus::SecondDerivative(const std::function<double(double)>& f, double x, double h) {
        return (f(x + h) - 2.0 * f(x) + f(x - h)) / (h * h);
    }

    double Calculus::IntegralTrapezoidal(const std::function<double(double)>& f, double a, double b, int n) {
        double h = (b - a) / n;
        double sum = 0.5 * (f(a) + f(b));
        for (int i = 1; i < n; ++i) {
            sum += f(a + i * h);
        }
        return sum * h;
    }

    double Calculus::IntegralSimpson13(const std::function<double(double)>& f, double a, double b, int n) {
        if (n % 2 != 0) n++; // Çift olmalı
        double h = (b - a) / n;
        double sum = f(a) + f(b);
        for (int i = 1; i < n; i += 2) sum += 4.0 * f(a + i * h);
        for (int i = 2; i < n - 1; i += 2) sum += 2.0 * f(a + i * h);
        return sum * h / 3.0;
    }

    double Calculus::IntegralSimpson38(const std::function<double(double)>& f, double a, double b, int n) {
        if (n % 3 != 0) n += 3 - (n % 3); // 3'ün katı olmalı
        double h = (b - a) / n;
        double sum = f(a) + f(b);
        for (int i = 1; i < n; ++i) {
            if (i % 3 == 0) sum += 2.0 * f(a + i * h);
            else sum += 3.0 * f(a + i * h);
        }
        return sum * 3.0 * h / 8.0;
    }

    double Calculus::IntegralGaussianQuadrature(const std::function<double(double)>& f, double a, double b) {
        // 5 noktalı Gaussian Quadrature
        double x[] = {0.0, 0.5384693, -0.5384693, 0.9061798, -0.9061798};
        double w[] = {0.5688889, 0.4786287, 0.4786287, 0.2369269, 0.2369269};
        double sum = 0.0;
        double mid = 0.5 * (b + a);
        double half_diff = 0.5 * (b - a);
        for (int i = 0; i < 5; ++i) {
            sum += w[i] * f(mid + half_diff * x[i]);
        }
        return sum * half_diff;
    }

    double Calculus::RootBisection(const std::function<double(double)>& f, double a, double b, double tol, int maxIter) {
        if (f(a) * f(b) >= 0) return a;
        double c = a;
        for (int i = 0; i < maxIter; ++i) {
            c = (a + b) / 2.0;
            if (std::abs(f(c)) < tol || (b - a) / 2.0 < tol) return c;
            if (f(c) * f(a) < 0) b = c;
            else a = c;
        }
        return c;
    }

    double Calculus::RootNewtonRaphson(const std::function<double(double)>& f, const std::function<double(double)>& df, double guess, double tol, int maxIter) {
        double x = guess;
        for (int i = 0; i < maxIter; ++i) {
            double fx = f(x);
            if (std::abs(fx) < tol) return x;
            double dfx = df(x);
            if (std::abs(dfx) < 1e-14) break;
            x = x - fx / dfx;
        }
        return x;
    }

    double Calculus::RootSecant(const std::function<double(double)>& f, double x0, double x1, double tol, int maxIter) {
        for (int i = 0; i < maxIter; ++i) {
            double f0 = f(x0);
            double f1 = f(x1);
            if (std::abs(f1 - f0) < 1e-14) break;
            double x2 = x1 - f1 * (x1 - x0) / (f1 - f0);
            if (std::abs(x2 - x1) < tol) return x2;
            x0 = x1;
            x1 = x2;
        }
        return x1;
    }
}
