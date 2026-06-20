#include "MathExtended.h"
#include <cmath>
#include <algorithm>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

namespace Conqueror::Math
{
    double Variance(const std::vector<double>& values)
    {
        if (values.empty()) return 0.0;
        double sum = 0.0;
        for (double v : values) sum += v;
        double mean = sum / values.size();
        double variance = 0.0;
        for (double v : values) {
            double d = v - mean;
            variance += d * d;
        }
        return variance / values.size();
    }

    double StandardDeviation(const std::vector<double>& values)
    {
        return std::sqrt(Variance(values));
    }

    double Median(std::vector<double> values)
    {
        if (values.empty()) return 0.0;
        std::sort(values.begin(), values.end());
        size_t n = values.size();
        return n % 2 == 0 ? (values[n/2 - 1] + values[n/2]) / 2.0 : values[n/2];
    }

    double Mode(const std::vector<double>& values)
    {
        if (values.empty()) return 0.0;
        std::map<double, int> counts;
        for (double val : values) counts[val]++;
        double mode = counts.begin()->first;
        int maxCount = counts.begin()->second;
        for (auto& pair : counts) {
            if (pair.second > maxCount) {
                maxCount = pair.second;
                mode = pair.first;
            }
        }
        return mode;
    }

    double Range(const std::vector<double>& values)
    {
        if (values.empty()) return 0.0;
        double mn = values[0];
        double mx = values[0];
        for (double v : values) {
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }
        return mx - mn;
    }

    double Mean(const std::vector<double>& values)
    {
        if (values.empty()) return 0.0;
        double sum = 0.0;
        for (double v : values) sum += v;
        return sum / values.size();
    }

    double GeometricMean(const std::vector<double>& values)
    {
        if (values.empty()) return 0.0;
        double prod = 1.0;
        for (double v : values) prod *= v;
        return std::pow(prod, 1.0 / values.size());
    }

    double HarmonicMean(const std::vector<double>& values)
    {
        if (values.empty()) return 0.0;
        double sum = 0.0;
        for (double v : values) {
            if (v != 0.0) sum += 1.0 / v;
        }
        return sum != 0.0 ? values.size() / sum : 0.0;
    }

    double RootMeanSquare(const std::vector<double>& values)
    {
        if (values.empty()) return 0.0;
        double sum = 0.0;
        for (double v : values) sum += v * v;
        return std::sqrt(sum / values.size());
    }

    double SumOfSquares(const std::vector<double>& values)
    {
        double sum = 0.0;
        for (double v : values) sum += v * v;
        return sum;
    }

    int64_t Factorial(int64_t n)
    {
        if (n < 0) return 0;
        int64_t result = 1;
        for (int64_t i = 2; i <= n; ++i) result *= i;
        return result;
    }

    bool IsPrime(int64_t n)
    {
        if (n < 2) return false;
        if (n < 4) return true;
        if (n % 2 == 0 || n % 3 == 0) return false;
        for (int64_t i = 5; i * i <= n; i += 6)
            if (n % i == 0 || n % (i + 2) == 0) return false;
        return true;
    }

    int64_t GCD(int64_t a, int64_t b)
    {
        a = std::abs(a);
        b = std::abs(b);
        while (b) { int64_t t = b; b = a % b; a = t; }
        return a;
    }

    int64_t LCM(int64_t a, int64_t b)
    {
        if (a == 0 || b == 0) return 0;
        return std::abs(a * b) / GCD(a, b);
    }

    std::vector<int64_t> PrimeFactors(int64_t n) {
        std::vector<int64_t> factors;
        n = std::abs(n);
        while (n % 2 == 0) { factors.push_back(2); n /= 2; }
        for (int64_t i = 3; i * i <= n; i += 2) {
            while (n % i == 0) {
                factors.push_back(i);
                n /= i;
            }
        }
        if (n > 2) factors.push_back(n);
        return factors;
    }

    std::vector<double> Convolve1D(const std::vector<double>& signal, const std::vector<double>& kernel) {
        std::vector<double> res;
        int n = (int)signal.size();
        int m = (int)kernel.size();
        if (n == 0 || m == 0) return res;
        
        for (int i = 0; i < n + m - 1; i++) {
            double sum = 0.0;
            for (int j = 0; j < m; j++) {
                if (i - j >= 0 && i - j < n) {
                    sum += signal[i - j] * kernel[j];
                }
            }
            res.push_back(sum);
        }
        return res;
    }

    double WaveSine(double t, double freq) { return std::sin(t * freq * 2.0 * 3.1415926535); }
    double WaveSquare(double t, double freq) { return std::sin(t * freq * 2.0 * 3.1415926535) >= 0 ? 1.0 : -1.0; }
    double WaveSawtooth(double t, double freq) { return 2.0 * std::fmod(t * freq, 1.0) - 1.0; }
    double WaveTriangle(double t, double freq) { return 2.0 * std::abs(2.0 * std::fmod(t * freq, 1.0) - 1.0) - 1.0; }
    double WavePulse(double t, double freq, double duty) { return std::fmod(t * freq, 1.0) < duty ? 1.0 : -1.0; }

    double GravityForce(double m1, double m2, double r) {
        if (r == 0) return 0.0;
        return 6.67430e-11 * (m1 * m2) / (r * r);
    }
    double KineticEnergy(double m, double v) { return 0.5 * m * v * v; }
    double ProjectileRange(double v, double angle) { return (v * v * std::sin(2 * angle)) / 9.81; }
    double TerminalVelocity(double m, double cd, double a) {
        if (cd * a == 0) return 0.0;
        return std::sqrt((2 * m * 9.81) / (1.225 * cd * a));
    }
    double SpringForce(double k, double x) { return -k * x; }
    double Momentum(double m, double v) { return m * v; }

    void HexToRGB(const std::string& hex, float& r, float& g, float& b)
    {
        std::string h = hex;
        if (!h.empty() && h[0] == '#') h = h.substr(1);
        if (h.length() < 6) { r = g = b = 0.0f; return; }
        int r_i = std::stoi(h.substr(0, 2), nullptr, 16);
        int g_i = std::stoi(h.substr(2, 2), nullptr, 16);
        int b_i = std::stoi(h.substr(4, 2), nullptr, 16);
        r = r_i / 255.0f;
        g = g_i / 255.0f;
        b = b_i / 255.0f;
    }

    std::string RGBToHex(float r, float g, float b)
    {
        int r_i = std::clamp((int)(r * 255.0f), 0, 255);
        int g_i = std::clamp((int)(g * 255.0f), 0, 255);
        int b_i = std::clamp((int)(b * 255.0f), 0, 255);
        char buf[8];
        snprintf(buf, sizeof(buf), "#%02X%02X%02X", r_i, g_i, b_i);
        return std::string(buf);
    }

    float Brightness(float r, float g, float b)
    {
        return 0.299f * r + 0.587f * g + 0.114f * b;
    }

    void ColorBlend(float r1, float g1, float b1, float r2, float g2, float b2, float t, float& rOut, float& gOut, float& bOut) {
        rOut = r1 + t * (r2 - r1);
        gOut = g1 + t * (g2 - g1);
        bOut = b1 + t * (b2 - b1);
    }
    
    float ColorGrayscale(float r, float g, float b) {
        return 0.299f * r + 0.587f * g + 0.114f * b;
    }

    void ColorToCMYK(float r, float g, float b, float& cOut, float& mOut, float& yOut, float& kOut) {
        float k = 1.0f - std::max({r, g, b});
        if (k == 1.0f) { cOut = mOut = yOut = 0; kOut = 1.0f; return; }
        cOut = (1.0f - r - k) / (1.0f - k);
        mOut = (1.0f - g - k) / (1.0f - k);
        yOut = (1.0f - b - k) / (1.0f - k);
        kOut = k;
    }

    void ColorFromCMYK(float c, float m, float y, float k, float& rOut, float& gOut, float& bOut) {
        rOut = (1.0f - c) * (1.0f - k);
        gOut = (1.0f - m) * (1.0f - k);
        bOut = (1.0f - y) * (1.0f - k);
    }

    void ColorToHSL(float r, float g, float b, float& hOut, float& sOut, float& lOut) {
        float min = std::min({r, g, b}), max = std::max({r, g, b});
        float h = 0, s = 0, l = (max + min) / 2.0f;
        if (max != min) {
            float d = max - min;
            s = l > 0.5f ? d / (2.0f - max - min) : d / (max + min);
            if (max == r) h = (g - b) / d + (g < b ? 6.0f : 0.0f);
            else if (max == g) h = (b - r) / d + 2.0f;
            else h = (r - g) / d + 4.0f;
            h /= 6.0f;
        }
        hOut = h; sOut = s; lOut = l;
    }

    void ColorFromHSL(float h, float s, float l, float& rOut, float& gOut, float& bOut) {
        if (s == 0) { rOut = gOut = bOut = l; return; }
        auto hue2rgb = [](float p, float q, float t) {
            if(t < 0) t += 1.0f;
            if(t > 1.0f) t -= 1.0f;
            if(t < 1.0f/6.0f) return p + (q - p) * 6.0f * t;
            if(t < 1.0f/2.0f) return q;
            if(t < 2.0f/3.0f) return p + (q - p) * (2.0f/3.0f - t) * 6.0f;
            return p;
        };
        float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
        float p = 2.0f * l - q;
        rOut = hue2rgb(p, q, h + 1.0f/3.0f);
        gOut = hue2rgb(p, q, h);
        bOut = hue2rgb(p, q, h - 1.0f/3.0f);
    }

    void ColorDarken(float r, float g, float b, float amount, float& rOut, float& gOut, float& bOut) {
        rOut = std::max(0.0f, r - amount);
        gOut = std::max(0.0f, g - amount);
        bOut = std::max(0.0f, b - amount);
    }

    void ColorLighten(float r, float g, float b, float amount, float& rOut, float& gOut, float& bOut) {
        rOut = std::min(1.0f, r + amount);
        gOut = std::min(1.0f, g + amount);
        bOut = std::min(1.0f, b + amount);
    }

    void ColorDesaturate(float r, float g, float b, float amount, float& rOut, float& gOut, float& bOut) {
        float l = ColorGrayscale(r, g, b);
        ColorBlend(r, g, b, l, l, l, amount, rOut, gOut, bOut);
    }

    void ColorSaturate(float r, float g, float b, float amount, float& rOut, float& gOut, float& bOut) {
        float l = ColorGrayscale(r, g, b);
        ColorBlend(r, g, b, l, l, l, -amount, rOut, gOut, bOut);
    }

    void ColorBlendMultiply(float r1, float g1, float b1, float r2, float g2, float b2, float& rOut, float& gOut, float& bOut) {
        rOut = r1 * r2;
        gOut = g1 * g2;
        bOut = b1 * b2;
    }

    void ColorBlendScreen(float r1, float g1, float b1, float r2, float g2, float b2, float& rOut, float& gOut, float& bOut) {
        rOut = 1.0f - (1.0f - r1) * (1.0f - r2);
        gOut = 1.0f - (1.0f - g1) * (1.0f - g2);
        bOut = 1.0f - (1.0f - b1) * (1.0f - b2);
    }

    void ColorBlendOverlay(float r1, float g1, float b1, float r2, float g2, float b2, float& rOut, float& gOut, float& bOut) {
        rOut = (r1 < 0.5f) ? (2.0f * r1 * r2) : (1.0f - 2.0f * (1.0f - r1) * (1.0f - r2));
        gOut = (g1 < 0.5f) ? (2.0f * g1 * g2) : (1.0f - 2.0f * (1.0f - g1) * (1.0f - g2));
        bOut = (b1 < 0.5f) ? (2.0f * b1 * b2) : (1.0f - 2.0f * (1.0f - b1) * (1.0f - b2));
    }

    void ColorInvert(float r, float g, float b, float& rOut, float& gOut, float& bOut) {
        rOut = 1.0f - r;
        gOut = 1.0f - g;
        bOut = 1.0f - b;
    }

    double Distance2D(double x1, double y1, double x2, double y2)
    {
        double dx = x2 - x1;
        double dy = y2 - y1;
        return std::sqrt(dx * dx + dy * dy);
    }

    bool CircleIntersect(double x1, double y1, double r1, double x2, double y2, double r2)
    {
        double dist = Distance2D(x1, y1, x2, y2);
        return dist <= (r1 + r2);
    }

    bool AABBIntersect(double ax, double ay, double aw, double ah, double bx, double by, double bw, double bh)
    {
        return (ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by);
    }

    bool PointInRect(double px, double py, double rx, double ry, double rw, double rh)
    {
        return (px >= rx && px <= rx + rw && py >= ry && py <= ry + rh);
    }

    double AngleBetween(double x1, double y1, double x2, double y2)
    {
        return std::atan2(y2 - y1, x2 - x1);
    }

    double PolygonArea2D(const std::vector<glm::vec2>& points)
    {
        size_t n = points.size();
        if (n < 3) return 0.0;
        double area = 0.0;
        for (size_t i = 0; i < n; ++i) {
            size_t j = (i + 1) % n;
            area += (double)points[i].x * (double)points[j].y - (double)points[j].x * (double)points[i].y;
        }
        return std::abs(area) / 2.0;
    }

    glm::vec3 GetForwardVector(const glm::vec3& eulerAngles)
    {
        glm::quat q(eulerAngles);
        return q * glm::vec3(0, 0, -1);
    }

    glm::vec3 GetRightVector(const glm::vec3& eulerAngles)
    {
        glm::quat q(eulerAngles);
        return q * glm::vec3(1, 0, 0);
    }

    glm::vec3 GetUpVector(const glm::vec3& eulerAngles)
    {
        glm::quat q(eulerAngles);
        return q * glm::vec3(0, 1, 0);
    }
    
    // Easing & Interpolation
    double Remap(double v, double iMin, double iMax, double oMin, double oMax) {
        return oMin + (v - iMin) * (oMax - oMin) / (iMax - iMin);
    }
    double Wrap(double v, double min, double max) {
        return v - (max - min) * std::floor((v - min) / (max - min));
    }
    double BezierQuad(double t, double p0, double p1, double p2) {
        double u = 1 - t;
        return u * u * p0 + 2 * u * t * p1 + t * t * p2;
    }
    double BezierCubic(double t, double p0, double p1, double p2, double p3) {
        double u = 1 - t;
        return u * u * u * p0 + 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t * p3;
    }
    double CatmullRom(double t, double p0, double p1, double p2, double p3) {
        double t2 = t * t, t3 = t2 * t;
        return 0.5 * (2*p1 + (-p0 + p2)*t + (2*p0 - 5*p1 + 4*p2 - p3)*t2 + (-p0 + 3*p1 - 3*p2 + p3)*t3);
    }
    double Hermite(double t, double p0, double p1, double m0, double m1) {
        double t2 = t * t, t3 = t2 * t;
        return (2*t3 - 3*t2 + 1)*p0 + (t3 - 2*t2 + t)*m0 + (-2*t3 + 3*t2)*p1 + (t3 - t2)*m1;
    }

    double EaseInQuad(double t) { return t * t; }
    double EaseOutQuad(double t) { return t * (2 - t); }
    double EaseInOutQuad(double t) { return t < .5 ? 2 * t * t : -1 + (4 - 2 * t) * t; }
    double EaseInCubic(double t) { return t * t * t; }
    double EaseOutCubic(double t) { t -= 1; return t * t * t + 1; }
    double EaseInOutCubic(double t) { return t < .5 ? 4 * t * t * t : (t - 1) * (2 * t - 2) * (2 * t - 2) + 1; }
    double EaseInSine(double t) { return 1.0 - std::cos(t * 3.1415926535 / 2.0); }
    double EaseOutSine(double t) { return std::sin(t * 3.1415926535 / 2.0); }
    double EaseInOutSine(double t) { return -(std::cos(3.1415926535 * t) - 1.0) / 2.0; }
    double EaseInExpo(double t) { return t == 0.0 ? 0.0 : std::pow(2.0, 10.0 * t - 10.0); }
    double EaseOutExpo(double t) { return t == 1.0 ? 1.0 : 1.0 - std::pow(2.0, -10.0 * t); }
    double EaseInOutExpo(double t) { 
        if (t == 0.0) return 0.0;
        if (t == 1.0) return 1.0;
        return t < 0.5 ? std::pow(2.0, 20.0 * t - 10.0) / 2.0 : (2.0 - std::pow(2.0, -20.0 * t + 10.0)) / 2.0; 
    }
    double EaseInElastic(double t) {
        if (t == 0.0) return 0.0;
        if (t == 1.0) return 1.0;
        return -std::pow(2.0, 10.0 * t - 10.0) * std::sin((t * 10.0 - 10.75) * ((2.0 * 3.1415926535) / 3.0));
    }
    double EaseOutElastic(double t) {
        if (t == 0.0) return 0.0;
        if (t == 1.0) return 1.0;
        return std::pow(2.0, -10.0 * t) * std::sin((t * 10.0 - 0.75) * ((2.0 * 3.1415926535) / 3.0)) + 1.0;
    }

    // Noise
    double NoiseHash(double n) { return std::fmod(std::sin(n) * 43758.5453123, 1.0); }
    double NoiseFade(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }
    double NoiseGrad1D(int hash, double x) { return (hash & 15) < 8 ? x : -x; }
    double NoisePerlin1D(double x) {
        int X = (int)std::floor(x) & 255;
        x -= std::floor(x);
        double u = NoiseFade(x);
        double g1 = NoiseGrad1D(X, x);
        double g2 = NoiseGrad1D(X+1, x-1.0);
        return g1 + u * (g2 - g1);
    }
    double NoiseFBM1D(double x, int octaves, double pers) {
        double t = 0.0, a = 1.0, f = 1.0;
        for (int i = 0; i < octaves; i++) {
            t += a * NoisePerlin1D(x * f);
            f *= 2.0; a *= pers;
        }
        return t;
    }

    // Bitwise Utilities
    int BitCount(int64_t n)
    {
        unsigned long long v = (unsigned long long)n;
        int count = 0;
        while (v) { count += v & 1; v >>= 1; }
        return count;
    }

    int64_t BitToggle(int64_t n, int64_t pos)
    {
        return n ^ (1LL << pos);
    }

    bool BitCheck(int64_t n, int64_t pos)
    {
        return (n & (1LL << pos)) != 0;
    }

    int64_t BitAnd(int64_t a, int64_t b) { return a & b; }
    int64_t BitOr(int64_t a, int64_t b) { return a | b; }
    int64_t BitXor(int64_t a, int64_t b) { return a ^ b; }
    int64_t BitNot(int64_t a) { return ~a; }
    int64_t BitShiftLeft(int64_t a, int64_t b) { return a << b; }
    int64_t BitShiftRight(int64_t a, int64_t b) { return a >> b; }
}
