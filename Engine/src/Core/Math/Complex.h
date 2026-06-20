#pragma once
#include "Core/Base/Base.h"

namespace Conqueror::Math
{
    struct CQ_API CQComplex
    {
        float real;
        float imag;
        CQComplex(float r = 0.0f, float i = 0.0f) : real(r), imag(i) {}
    };

    class CQ_API ComplexMath
    {
    public:
        static CQComplex Add(const CQComplex& a, const CQComplex& b);
        static CQComplex Sub(const CQComplex& a, const CQComplex& b);
        static CQComplex Mul(const CQComplex& a, const CQComplex& b);
        static CQComplex Div(const CQComplex& a, const CQComplex& b);
        
        static float Abs(const CQComplex& c);
        static float Arg(const CQComplex& c);
        
        static CQComplex Exp(const CQComplex& c);
        static CQComplex Log(const CQComplex& c);
        static CQComplex Pow(const CQComplex& base, const CQComplex& exp);
        static CQComplex Pow(const CQComplex& base, float exp);
        
        static CQComplex Sin(const CQComplex& c);
        static CQComplex Cos(const CQComplex& c);
        static CQComplex Sinh(const CQComplex& c);
        static CQComplex Cosh(const CQComplex& c);
        static CQComplex Tanh(const CQComplex& c);
    };
}