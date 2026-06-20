#pragma once
#include "Core/Base/Base.h"
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

namespace Conqueror::Math
{
    CQ_API double Variance(const std::vector<double>& values);
    CQ_API double StandardDeviation(const std::vector<double>& values);
    CQ_API double Median(std::vector<double> values);
    CQ_API double Mode(const std::vector<double>& values);
    CQ_API double Range(const std::vector<double>& values);
    CQ_API double Mean(const std::vector<double>& values);
    CQ_API double GeometricMean(const std::vector<double>& values);
    CQ_API double HarmonicMean(const std::vector<double>& values);
    CQ_API double RootMeanSquare(const std::vector<double>& values);
    CQ_API double SumOfSquares(const std::vector<double>& values);
    
    CQ_API int64_t Factorial(int64_t n);
    CQ_API bool IsPrime(int64_t n);
    CQ_API std::vector<int64_t> PrimeFactors(int64_t n);
    CQ_API int64_t GCD(int64_t a, int64_t b);
    CQ_API int64_t LCM(int64_t a, int64_t b);

    // Signal Processing
    CQ_API std::vector<double> Convolve1D(const std::vector<double>& signal, const std::vector<double>& kernel);

    // Wave Utils
    CQ_API double WaveSine(double t, double freq);
    CQ_API double WaveSquare(double t, double freq);
    CQ_API double WaveSawtooth(double t, double freq);
    CQ_API double WaveTriangle(double t, double freq);
    CQ_API double WavePulse(double t, double freq, double duty);

    // Color Utils
    CQ_API void HexToRGB(const std::string& hex, float& r, float& g, float& b);
    CQ_API std::string RGBToHex(float r, float g, float b);
    CQ_API float Brightness(float r, float g, float b);
    CQ_API void ColorBlend(float r1, float g1, float b1, float r2, float g2, float b2, float t, float& rOut, float& gOut, float& bOut);
    CQ_API float ColorGrayscale(float r, float g, float b);
    CQ_API void ColorToCMYK(float r, float g, float b, float& cOut, float& mOut, float& yOut, float& kOut);
    CQ_API void ColorFromCMYK(float c, float m, float y, float k, float& rOut, float& gOut, float& bOut);
    CQ_API void ColorToHSL(float r, float g, float b, float& hOut, float& sOut, float& lOut);
    CQ_API void ColorFromHSL(float h, float s, float l, float& rOut, float& gOut, float& bOut);
    CQ_API void ColorDarken(float r, float g, float b, float amount, float& rOut, float& gOut, float& bOut);
    CQ_API void ColorLighten(float r, float g, float b, float amount, float& rOut, float& gOut, float& bOut);
    CQ_API void ColorDesaturate(float r, float g, float b, float amount, float& rOut, float& gOut, float& bOut);
    CQ_API void ColorSaturate(float r, float g, float b, float amount, float& rOut, float& gOut, float& bOut);
    CQ_API void ColorBlendMultiply(float r1, float g1, float b1, float r2, float g2, float b2, float& rOut, float& gOut, float& bOut);
    CQ_API void ColorBlendScreen(float r1, float g1, float b1, float r2, float g2, float b2, float& rOut, float& gOut, float& bOut);
    CQ_API void ColorBlendOverlay(float r1, float g1, float b1, float r2, float g2, float b2, float& rOut, float& gOut, float& bOut);
    CQ_API void ColorInvert(float r, float g, float b, float& rOut, float& gOut, float& bOut);

    // Geometry Utils
    CQ_API double Distance2D(double x1, double y1, double x2, double y2);
    CQ_API bool CircleIntersect(double x1, double y1, double r1, double x2, double y2, double r2);
    CQ_API bool AABBIntersect(double ax, double ay, double aw, double ah, double bx, double by, double bw, double bh);
    CQ_API bool PointInRect(double px, double py, double rx, double ry, double rw, double rh);
    CQ_API double AngleBetween(double x1, double y1, double x2, double y2);
    CQ_API double PolygonArea2D(const std::vector<glm::vec2>& points);

    // Vector Directions
    CQ_API glm::vec3 GetForwardVector(const glm::vec3& eulerAngles);
    CQ_API glm::vec3 GetRightVector(const glm::vec3& eulerAngles);
    CQ_API glm::vec3 GetUpVector(const glm::vec3& eulerAngles);
    
    // Easing Utils
    CQ_API double Remap(double v, double iMin, double iMax, double oMin, double oMax);
    CQ_API double Wrap(double v, double min, double max);
    CQ_API double BezierQuad(double t, double p0, double p1, double p2);
    CQ_API double BezierCubic(double t, double p0, double p1, double p2, double p3);
    CQ_API double CatmullRom(double t, double p0, double p1, double p2, double p3);
    CQ_API double Hermite(double t, double p0, double p1, double m0, double m1);
    
    CQ_API double EaseInQuad(double t);
    CQ_API double EaseOutQuad(double t);
    CQ_API double EaseInOutQuad(double t);
    CQ_API double EaseInCubic(double t);
    CQ_API double EaseOutCubic(double t);
    CQ_API double EaseInOutCubic(double t);
    CQ_API double EaseInSine(double t);
    CQ_API double EaseOutSine(double t);
    CQ_API double EaseInOutSine(double t);
    CQ_API double EaseInExpo(double t);
    CQ_API double EaseOutExpo(double t);
    CQ_API double EaseInOutExpo(double t);
    CQ_API double EaseInElastic(double t);
    CQ_API double EaseOutElastic(double t);
    
    // Noise Utils
    CQ_API double NoiseHash(double n);
    CQ_API double NoiseFade(double t);
    CQ_API double NoiseGrad1D(int hash, double x);
    CQ_API double NoisePerlin1D(double x);
    CQ_API double NoiseFBM1D(double x, int octaves, double pers);

    // Bitwise Utilities
    CQ_API int BitCount(int64_t n);
    CQ_API int64_t BitToggle(int64_t n, int64_t pos);
    CQ_API bool BitCheck(int64_t n, int64_t pos);
    CQ_API int64_t BitAnd(int64_t a, int64_t b);
    CQ_API int64_t BitOr(int64_t a, int64_t b);
    CQ_API int64_t BitXor(int64_t a, int64_t b);
    CQ_API int64_t BitNot(int64_t a);
    CQ_API int64_t BitShiftLeft(int64_t a, int64_t b);
    CQ_API int64_t BitShiftRight(int64_t a, int64_t b);
}
