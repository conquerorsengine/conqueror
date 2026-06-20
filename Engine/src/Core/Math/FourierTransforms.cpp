#include "FourierTransforms.h"
#include <cmath>
#include <algorithm>

namespace Conqueror::Math {
    void FourierTransforms::FFT(std::vector<CQComplex>& a, bool invert) {
        int n = a.size();
        for (int i = 1, j = 0; i < n; i++) {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;

            if (i < j)
                std::swap(a[i], a[j]);
        }

        for (int len = 2; len <= n; len <<= 1) {
            float angle = 2.0f * 3.14159265358979323846f / len * (invert ? -1 : 1);
            CQComplex wlen(std::cos(angle), std::sin(angle));
            for (int i = 0; i < n; i += len) {
                CQComplex w(1.0f, 0.0f);
                for (int j = 0; j < len / 2; j++) {
                    CQComplex u = a[i+j];
                    CQComplex v = ComplexMath::Mul(a[i+j+len/2], w);
                    a[i+j] = ComplexMath::Add(u, v);
                    a[i+j+len/2] = ComplexMath::Sub(u, v);
                    w = ComplexMath::Mul(w, wlen);
                }
            }
        }

        if (invert) {
            for (CQComplex& x : a) {
                x.real /= n;
                x.imag /= n;
            }
        }
    }

    std::vector<CQComplex> FourierTransforms::Convolve(const std::vector<CQComplex>& a, const std::vector<CQComplex>& b) {
        std::vector<CQComplex> fa(a.begin(), a.end()), fb(b.begin(), b.end());
        int n = 1;
        while (n < a.size() + b.size()) 
            n <<= 1;
        
        fa.resize(n, {0,0});
        fb.resize(n, {0,0});

        FFT(fa, false);
        FFT(fb, false);

        for (int i = 0; i < n; i++) {
            fa[i] = ComplexMath::Mul(fa[i], fb[i]);
        }

        FFT(fa, true);
        return fa;
    }
}
