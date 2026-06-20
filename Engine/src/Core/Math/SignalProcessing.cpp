#include "SignalProcessing.h"
#include <cmath>

namespace Conqueror::Math {
    const double PI = 3.14159265358979323846;

    void SignalProcessing::DFT(const std::vector<CQComplex>& input, std::vector<CQComplex>& output) {
        int n = input.size();
        output.assign(n, CQComplex(0, 0));
        for (int k = 0; k < n; ++k) {
            for (int t = 0; t < n; ++t) {
                double angle = -2.0 * PI * t * k / n;
                CQComplex w(std::cos(angle), std::sin(angle));
                output[k] = ComplexMath::Add(output[k], ComplexMath::Mul(input[t], w));
            }
        }
    }

    void SignalProcessing::IDFT(const std::vector<CQComplex>& input, std::vector<CQComplex>& output) {
        int n = input.size();
        output.assign(n, CQComplex(0, 0));
        for (int t = 0; t < n; ++t) {
            for (int k = 0; k < n; ++k) {
                double angle = 2.0 * PI * t * k / n;
                CQComplex w(std::cos(angle), std::sin(angle));
                output[t] = ComplexMath::Add(output[t], ComplexMath::Mul(input[k], w));
            }
            output[t].real /= n;
            output[t].imag /= n;
        }
    }

    static void FFTCompute(std::vector<CQComplex>& data, bool invert) {
        int n = data.size();
        if (n <= 1) return;

        // Bit-reversal permutation
        for (int i = 1, j = 0; i < n; ++i) {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(data[i], data[j]);
        }

        for (int len = 2; len <= n; len <<= 1) {
            double angle = 2.0 * PI / len * (invert ? 1 : -1);
            CQComplex wlen(std::cos(angle), std::sin(angle));
            for (int i = 0; i < n; i += len) {
                CQComplex w(1, 0);
                for (int j = 0; j < len / 2; ++j) {
                    CQComplex u = data[i + j];
                    CQComplex v = ComplexMath::Mul(data[i + j + len / 2], w);
                    data[i + j] = ComplexMath::Add(u, v);
                    data[i + j + len / 2] = ComplexMath::Sub(u, v);
                    w = ComplexMath::Mul(w, wlen);
                }
            }
        }

        if (invert) {
            for (CQComplex& x : data) {
                x.real /= n;
                x.imag /= n;
            }
        }
    }

    void SignalProcessing::FFT(std::vector<CQComplex>& data) {
        FFTCompute(data, false);
    }

    void SignalProcessing::IFFT(std::vector<CQComplex>& data) {
        FFTCompute(data, true);
    }

    std::vector<double> SignalProcessing::WindowHamming(int size) {
        std::vector<double> window(size);
        for (int i = 0; i < size; ++i) {
            window[i] = 0.54 - 0.46 * std::cos(2.0 * PI * i / (size - 1));
        }
        return window;
    }

    std::vector<double> SignalProcessing::WindowHanning(int size) {
        std::vector<double> window(size);
        for (int i = 0; i < size; ++i) {
            window[i] = 0.5 * (1.0 - std::cos(2.0 * PI * i / (size - 1)));
        }
        return window;
    }

    std::vector<double> SignalProcessing::WindowBlackman(int size) {
        std::vector<double> window(size);
        for (int i = 0; i < size; ++i) {
            window[i] = 0.42 - 0.5 * std::cos(2.0 * PI * i / (size - 1)) + 0.08 * std::cos(4.0 * PI * i / (size - 1));
        }
        return window;
    }

    std::vector<double> SignalProcessing::WindowBartlett(int size) {
        std::vector<double> window(size);
        for (int i = 0; i < size; ++i) {
            window[i] = 1.0 - std::abs((i - (size - 1) / 2.0) / ((size - 1) / 2.0));
        }
        return window;
    }

    std::vector<double> SignalProcessing::FIRFilter(const std::vector<double>& input, const std::vector<double>& coefficients) {
        std::vector<double> output(input.size(), 0.0);
        int m = coefficients.size();
        for (size_t i = 0; i < input.size(); ++i) {
            for (int j = 0; j < m; ++j) {
                if (i >= (size_t)j) output[i] += input[i - j] * coefficients[j];
            }
        }
        return output;
    }

    std::vector<double> SignalProcessing::DCT(const std::vector<double>& input) {
        int n = input.size();
        std::vector<double> output(n, 0.0);
        for (int k = 0; k < n; ++k) {
            double sum = 0.0;
            for (int i = 0; i < n; ++i) {
                sum += input[i] * std::cos(PI / n * (i + 0.5) * k);
            }
            output[k] = sum;
        }
        return output;
    }

    std::vector<double> SignalProcessing::IDCT(const std::vector<double>& input) {
        int n = input.size();
        std::vector<double> output(n, 0.0);
        for (int k = 0; k < n; ++k) {
            double sum = 0.5 * input[0];
            for (int i = 1; i < n; ++i) {
                sum += input[i] * std::cos(PI / n * (k + 0.5) * i);
            }
            output[k] = sum * (2.0 / n);
        }
        return output;
    }

    void SignalProcessing::Haar1D(std::vector<double>& data) {
        int n = data.size();
        std::vector<double> temp(n);
        while (n > 1) {
            n /= 2;
            for (int i = 0; i < n; ++i) {
                temp[i] = (data[2 * i] + data[2 * i + 1]) / std::sqrt(2.0);
                temp[n + i] = (data[2 * i] - data[2 * i + 1]) / std::sqrt(2.0);
            }
            for (int i = 0; i < n * 2; ++i) data[i] = temp[i];
        }
    }

    void SignalProcessing::InverseHaar1D(std::vector<double>& data) {
        int len = data.size();
        std::vector<double> temp(len);
        int n = 1;
        while (n < len) {
            for (int i = 0; i < n; ++i) {
                temp[2 * i] = (data[i] + data[n + i]) / std::sqrt(2.0);
                temp[2 * i + 1] = (data[i] - data[n + i]) / std::sqrt(2.0);
            }
            n *= 2;
            for (int i = 0; i < n; ++i) data[i] = temp[i];
        }
    }
}
