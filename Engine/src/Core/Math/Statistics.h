#pragma once
#include "Core/Base/Base.h"
#include <vector>

namespace Conqueror::Math {
    class CQ_API Statistics {
    public:
        // Olasılık Yoğunluk Fonksiyonları (PDF)
        static double NormalPDF(double x, double mean, double stddev);
        static double PoissonPMF(int k, double lambda);
        static double BinomialPMF(int k, int n, double p);

        // Kümülatif Dağılım Fonksiyonları (CDF) - Hata Fonksiyonu (erf) bazlı
        static double NormalCDF(double x, double mean, double stddev);

        // Tanımlayıcı İstatistikler
        static double Mean(const std::vector<double>& data);
        static double Variance(const std::vector<double>& data, bool sample = true);
        static double StandardDeviation(const std::vector<double>& data, bool sample = true);
        static double Covariance(const std::vector<double>& dataX, const std::vector<double>& dataY, bool sample = true);
        static double CorrelationPearson(const std::vector<double>& dataX, const std::vector<double>& dataY);

        // Hipotez Testleri (P-Value ve Z-Score)
        static double ZTest(double sampleMean, double popMean, double popStdDev, int n);
        static double TTestOneSample(const std::vector<double>& sample, double popMean);
    };
}
