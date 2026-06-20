#pragma once
#include "Core/Base/Base.h"

namespace Conqueror::PhysicsMath
{
    CQ_API double GravityForce(double m1, double m2, double r);
    CQ_API double KineticEnergy(double m, double v);
    CQ_API double ProjectileRange(double v, double angle);
    CQ_API double TerminalVelocity(double m, double cd, double a);
    CQ_API double SpringForce(double k, double x);
    CQ_API double Momentum(double m, double v);
}
