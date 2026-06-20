#pragma once
#include "Core/Base/Base.h"
#include <vector>
#include <functional>

namespace Conqueror::Math {
    class CQ_API NumericalAnalysis {
    public:
        // Optimizasyon: Gradient Descent
        // Fonksiyon gradyanını yaklaşık olarak hesaplar
        static std::vector<double> GradientDescent(
            const std::function<double(const std::vector<double>&)>& func,
            std::vector<double> startPoint,
            double learningRate = 0.01,
            int maxIterations = 1000,
            double tolerance = 1e-6
        );

        // Optimizasyon: Newton-Raphson Metodu (Tek Değişkenli)
        static double NewtonRaphson(
            const std::function<double(double)>& func,
            const std::function<double(double)>& deriv,
            double startPoint,
            int maxIterations = 100,
            double tolerance = 1e-6
        );

        // İntegral: Romberg Metodu (Yüksek doğruluklu sayısal integral)
        static double RombergIntegration(
            const std::function<double(double)>& func,
            double a, double b,
            int maxSteps = 10,
            double tolerance = 1e-6
        );

        // Diferansiyel Denklem Çözümü: Runge-Kutta-Fehlberg (RKF45)
        // Adaptif adım aralıklı, 4. ve 5. derece RK
        static void RKF45(
            const std::function<double(double, double)>& func,
            double t0, double y0, double tEnd,
            double& tOut, double& yOut,
            double initialStepSize = 0.01,
            double tolerance = 1e-6
        );
    };
}
