#pragma once
#include "Core/Base/Base.h"
#include "Complex.h"
#include <vector>

namespace Conqueror::Math {
    class CQ_API FourierTransforms {
    public:
        // Radix-2 Cooley-Tukey Fast Fourier Transform
        // Size of the array must be a power of 2.
        static void FFT(std::vector<CQComplex>& data, bool invert);
        
        // Fast polynomial multiplication / Convolution using FFT
        static std::vector<CQComplex> Convolve(const std::vector<CQComplex>& a, const std::vector<CQComplex>& b);
    };
}
