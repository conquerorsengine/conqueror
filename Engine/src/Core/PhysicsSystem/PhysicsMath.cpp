#include "PhysicsMath.h"
#include <cmath>

namespace Conqueror::PhysicsMath
{
    double GravityForce(double m1, double m2, double r) {
        if (r == 0) return 0.0;
        return 6.67430e-11 * (m1 * m2) / (r * r);
    }
    
    double KineticEnergy(double m, double v) { 
        return 0.5 * m * v * v; 
    }
    
    double ProjectileRange(double v, double angle) { 
        return (v * v * std::sin(2 * angle)) / 9.81; 
    }
    
    double TerminalVelocity(double m, double cd, double a) {
        if (cd * a == 0) return 0.0;
        return std::sqrt((2 * m * 9.81) / (1.225 * cd * a));
    }
    
    double SpringForce(double k, double x) { 
        return -k * x; 
    }
    
    double Momentum(double m, double v) { 
        return m * v; 
    }
}
