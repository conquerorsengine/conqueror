#include "GeometricAlgebra.h"

namespace Conqueror::Math {
    Multivector GeometricAlgebra::GeometricProduct(const Multivector& a, const Multivector& b) {
        Multivector r;
        // Scalar part
        r.scalar = a.scalar*b.scalar + a.e1*b.e1 + a.e2*b.e2 + a.e3*b.e3 
                 - a.e12*b.e12 - a.e23*b.e23 - a.e31*b.e31 - a.e123*b.e123;
                 
        // Vector part (e1, e2, e3)
        r.e1 = a.scalar*b.e1 + a.e1*b.scalar - a.e2*b.e12 + a.e3*b.e31 
             + a.e12*b.e2 - a.e31*b.e3 - a.e23*b.e123 - a.e123*b.e23;
             
        r.e2 = a.scalar*b.e2 + a.e2*b.scalar - a.e3*b.e23 + a.e1*b.e12 
             + a.e23*b.e3 - a.e12*b.e1 - a.e31*b.e123 - a.e123*b.e31;
             
        r.e3 = a.scalar*b.e3 + a.e3*b.scalar - a.e1*b.e31 + a.e2*b.e23 
             + a.e31*b.e1 - a.e23*b.e2 - a.e12*b.e123 - a.e123*b.e12;
             
        // Bivector part (e12, e23, e31)
        r.e12 = a.scalar*b.e12 + a.e12*b.scalar + a.e1*b.e2 - a.e2*b.e1 
              - a.e3*b.e123 - a.e123*b.e3 - a.e23*b.e31 + a.e31*b.e23;
              
        r.e23 = a.scalar*b.e23 + a.e23*b.scalar + a.e2*b.e3 - a.e3*b.e2 
              - a.e1*b.e123 - a.e123*b.e1 - a.e31*b.e12 + a.e12*b.e31;
              
        r.e31 = a.scalar*b.e31 + a.e31*b.scalar + a.e3*b.e1 - a.e1*b.e3 
              - a.e2*b.e123 - a.e123*b.e2 - a.e12*b.e23 + a.e23*b.e12;
              
        // Trivector part (e123)
        r.e123 = a.scalar*b.e123 + a.e123*b.scalar + a.e1*b.e23 + a.e2*b.e31 + a.e3*b.e12 
               + a.e12*b.e3 + a.e23*b.e1 + a.e31*b.e2;
               
        return r;
    }

    Multivector GeometricAlgebra::WedgeProduct(const Multivector& a, const Multivector& b) {
        Multivector r;
        r.scalar = a.scalar * b.scalar;
        
        r.e1 = a.scalar*b.e1 + a.e1*b.scalar;
        r.e2 = a.scalar*b.e2 + a.e2*b.scalar;
        r.e3 = a.scalar*b.e3 + a.e3*b.scalar;
        
        r.e12 = a.scalar*b.e12 + a.e12*b.scalar + a.e1*b.e2 - a.e2*b.e1;
        r.e23 = a.scalar*b.e23 + a.e23*b.scalar + a.e2*b.e3 - a.e3*b.e2;
        r.e31 = a.scalar*b.e31 + a.e31*b.scalar + a.e3*b.e1 - a.e1*b.e3;
        
        r.e123 = a.scalar*b.e123 + a.e123*b.scalar 
               + a.e1*b.e23 + a.e2*b.e31 + a.e3*b.e12 
               + a.e12*b.e3 + a.e23*b.e1 + a.e31*b.e2;
               
        return r;
    }

    Multivector GeometricAlgebra::InnerProduct(const Multivector& a, const Multivector& b) {
        Multivector r;
        r.scalar = a.scalar*b.scalar + a.e1*b.e1 + a.e2*b.e2 + a.e3*b.e3 
                 - a.e12*b.e12 - a.e23*b.e23 - a.e31*b.e31 - a.e123*b.e123;
        return r;
    }

    Multivector GeometricAlgebra::Reverse(const Multivector& a) {
        Multivector r = a;
        r.e12 = -a.e12;
        r.e23 = -a.e23;
        r.e31 = -a.e31;
        r.e123 = -a.e123;
        return r;
    }
}
