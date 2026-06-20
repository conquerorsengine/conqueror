#pragma once
#include "Core/Base/Base.h"

namespace Conqueror::Math {
    // 3D Projective Geometric Algebra (PGA) Multivector (8 components for basic 3D operations)
    // Representing scalars, vectors, bivectors, and trivectors.
    struct CQ_API Multivector {
        float scalar;
        float e1, e2, e3;       // Vectors
        float e12, e23, e31;    // Bivectors
        float e123;             // Trivector (Pseudoscalar)
        
        Multivector() : scalar(0), e1(0), e2(0), e3(0), e12(0), e23(0), e31(0), e123(0) {}
    };

    class CQ_API GeometricAlgebra {
    public:
        // Geometric Product: Combines dot and wedge products
        static Multivector GeometricProduct(const Multivector& a, const Multivector& b);
        
        // Wedge (Outer) Product: Creates higher dimensional entities
        static Multivector WedgeProduct(const Multivector& a, const Multivector& b);
        
        // Inner (Dot) Product
        static Multivector InnerProduct(const Multivector& a, const Multivector& b);
        
        // Reverse operation (useful for rotors)
        static Multivector Reverse(const Multivector& a);
    };
}
