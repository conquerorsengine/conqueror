#pragma once
#include "Core/Base/Base.h"
#include "Complex.h"
#include <vector>

namespace Conqueror::Math {
    class CQ_API SignalProcessing {
    public:
        // Fourier Dönüşümleri
        static void DFT(const std::vector<CQComplex>& input, std::vector<CQComplex>& output);
        static void IDFT(const std::vector<CQComplex>& input, std::vector<CQComplex>& output);
        
        // Hızlı Fourier Dönüşümü (Girdi boyutu 2'nin kuvveti olmalıdır)
        static void FFT(std::vector<CQComplex>& data);
        static void IFFT(std::vector<CQComplex>& data);

        // Ayrık Kosinüs Dönüşümü (Discrete Cosine Transform)
        static std::vector<double> DCT(const std::vector<double>& input);
        static std::vector<double> IDCT(const std::vector<double>& input);

        // Haar Wavelet Transform
        static void Haar1D(std::vector<double>& data);
        static void InverseHaar1D(std::vector<double>& data);

        // Pencereleme Fonksiyonları
        static std::vector<double> WindowHamming(int size);
        static std::vector<double> WindowHanning(int size);
        static std::vector<double> WindowBlackman(int size);
        static std::vector<double> WindowBartlett(int size);

        // Filtreler
        static std::vector<double> FIRFilter(const std::vector<double>& input, const std::vector<double>& coefficients);
    };
}
