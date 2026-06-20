#pragma once
#include "Core/Base/Base.h"
#include "QuaternionMath.h"
#include "VecMath.h"

namespace Conqueror::Math
{
    struct CQ_API CQDualQuat
    {
        CQQuat real;
        CQQuat dual;
        CQDualQuat() : real{0,0,0,1}, dual{0,0,0,0} {}
        CQDualQuat(const CQQuat& r, const CQQuat& d) : real(r), dual(d) {}
    };

    class CQ_API DualQuaternionMath
    {
    public:
        static CQDualQuat FromTranslationRotation(const CQVec3& translation, const CQQuat& rotation);
        static CQDualQuat Add(const CQDualQuat& a, const CQDualQuat& b);
        static CQDualQuat Mul(const CQDualQuat& a, const CQDualQuat& b);
        static CQDualQuat Normalize(const CQDualQuat& dq);
        static CQDualQuat Conjugate(const CQDualQuat& dq);
        
        // Linear blend for Dual Quaternions (often used with normalize for DQS)
        static CQDualQuat DLerp(const CQDualQuat& a, const CQDualQuat& b, float t);
        
        // Dual Quaternion Spherical Linear Interpolation (Screw Linear Interpolation)
        static CQDualQuat Sklerp(const CQDualQuat& a, const CQDualQuat& b, float t);
        
        static CQVec3 TransformPoint(const CQDualQuat& dq, const CQVec3& p);
        static CQVec3 TransformVector(const CQDualQuat& dq, const CQVec3& v);
    };
}