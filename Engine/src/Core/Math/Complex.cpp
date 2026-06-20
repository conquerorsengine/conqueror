#include "Complex.h"
#include <cmath>

namespace Conqueror::Math
{
    CQComplex ComplexMath::Add(const CQComplex& a, const CQComplex& b)
    {
        return CQComplex(a.real + b.real, a.imag + b.imag);
    }

    CQComplex ComplexMath::Sub(const CQComplex& a, const CQComplex& b)
    {
        return CQComplex(a.real - b.real, a.imag - b.imag);
    }

    CQComplex ComplexMath::Mul(const CQComplex& a, const CQComplex& b)
    {
        return CQComplex(a.real * b.real - a.imag * b.imag, a.real * b.imag + a.imag * b.real);
    }

    CQComplex ComplexMath::Div(const CQComplex& a, const CQComplex& b)
    {
        float denom = b.real * b.real + b.imag * b.imag;
        if (denom == 0.0f) return CQComplex(0.0f, 0.0f);
        return CQComplex((a.real * b.real + a.imag * b.imag) / denom, (a.imag * b.real - a.real * b.imag) / denom);
    }

    float ComplexMath::Abs(const CQComplex& c)
    {
        return std::sqrt(c.real * c.real + c.imag * c.imag);
    }

    float ComplexMath::Arg(const CQComplex& c)
    {
        return std::atan2(c.imag, c.real);
    }

    CQComplex ComplexMath::Exp(const CQComplex& c)
    {
        float e = std::exp(c.real);
        return CQComplex(e * std::cos(c.imag), e * std::sin(c.imag));
    }

    CQComplex ComplexMath::Log(const CQComplex& c)
    {
        return CQComplex(std::log(Abs(c)), Arg(c));
    }

    CQComplex ComplexMath::Pow(const CQComplex& base, const CQComplex& exp)
    {
        if (base.real == 0.0f && base.imag == 0.0f) return CQComplex(0.0f, 0.0f);
        return Exp(Mul(exp, Log(base)));
    }

    CQComplex ComplexMath::Pow(const CQComplex& base, float exp)
    {
        float r = Abs(base);
        float theta = Arg(base);
        float p = std::pow(r, exp);
        return CQComplex(p * std::cos(exp * theta), p * std::sin(exp * theta));
    }

    CQComplex ComplexMath::Sin(const CQComplex& c)
    {
        return CQComplex(std::sin(c.real) * std::cosh(c.imag), std::cos(c.real) * std::sinh(c.imag));
    }

    CQComplex ComplexMath::Cos(const CQComplex& c)
    {
        return CQComplex(std::cos(c.real) * std::cosh(c.imag), -std::sin(c.real) * std::sinh(c.imag));
    }

    CQComplex ComplexMath::Sinh(const CQComplex& c)
    {
        return CQComplex(std::sinh(c.real) * std::cos(c.imag), std::cosh(c.real) * std::sin(c.imag));
    }

    CQComplex ComplexMath::Cosh(const CQComplex& c)
    {
        return CQComplex(std::cosh(c.real) * std::cos(c.imag), std::sinh(c.real) * std::sin(c.imag));
    }

    CQComplex ComplexMath::Tanh(const CQComplex& c)
    {
        return Div(Sinh(c), Cosh(c));
    }
}