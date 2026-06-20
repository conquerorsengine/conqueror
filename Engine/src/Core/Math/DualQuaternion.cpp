#include "DualQuaternion.h"

namespace Conqueror::Math
{
    CQDualQuat DualQuaternionMath::FromTranslationRotation(const CQVec3& translation, const CQQuat& rotation)
    {
        CQQuat r = QuatNormalize(rotation);
        CQQuat t{translation.x, translation.y, translation.z, 0.0f};
        CQQuat d = QuatMultiply(t, r);
        d[0] *= 0.5f; d[1] *= 0.5f; d[2] *= 0.5f; d[3] *= 0.5f;
        return CQDualQuat(r, d);
    }

    CQDualQuat DualQuaternionMath::Add(const CQDualQuat& a, const CQDualQuat& b)
    {
        return CQDualQuat(
            CQQuat{a.real[0] + b.real[0], a.real[1] + b.real[1], a.real[2] + b.real[2], a.real[3] + b.real[3]},
            CQQuat{a.dual[0] + b.dual[0], a.dual[1] + b.dual[1], a.dual[2] + b.dual[2], a.dual[3] + b.dual[3]}
        );
    }

    CQDualQuat DualQuaternionMath::Mul(const CQDualQuat& a, const CQDualQuat& b)
    {
        CQQuat r = QuatMultiply(a.real, b.real);
        CQQuat d1 = QuatMultiply(a.real, b.dual);
        CQQuat d2 = QuatMultiply(a.dual, b.real);
        CQQuat d{d1[0] + d2[0], d1[1] + d2[1], d1[2] + d2[2], d1[3] + d2[3]};
        return CQDualQuat(r, d);
    }

    CQDualQuat DualQuaternionMath::Normalize(const CQDualQuat& dq)
    {
        float mag = QuatLength(dq.real);
        if (mag < 0.000001f) return dq;
        float invMag = 1.0f / mag;
        
        CQDualQuat result;
        result.real = CQQuat{dq.real[0] * invMag, dq.real[1] * invMag, dq.real[2] * invMag, dq.real[3] * invMag};
        result.dual = CQQuat{dq.dual[0] * invMag, dq.dual[1] * invMag, dq.dual[2] * invMag, dq.dual[3] * invMag};
        
        // Remove scalar part of dual to ensure valid rigid transformation
        float dDotR = result.dual[0] * result.real[0] + result.dual[1] * result.real[1] + result.dual[2] * result.real[2] + result.dual[3] * result.real[3];
        result.dual[0] -= result.real[0] * dDotR;
        result.dual[1] -= result.real[1] * dDotR;
        result.dual[2] -= result.real[2] * dDotR;
        result.dual[3] -= result.real[3] * dDotR;
        
        return result;
    }

    CQDualQuat DualQuaternionMath::Conjugate(const CQDualQuat& dq)
    {
        return CQDualQuat(QuatConjugate(dq.real), QuatConjugate(dq.dual));
    }

    CQDualQuat DualQuaternionMath::DLerp(const CQDualQuat& a, const CQDualQuat& b, float t)
    {
        float dot = a.real[0] * b.real[0] + a.real[1] * b.real[1] + a.real[2] * b.real[2] + a.real[3] * b.real[3];
        float sign = (dot < 0.0f) ? -1.0f : 1.0f;
        
        CQDualQuat result;
        result.real[0] = a.real[0] * (1.0f - t) + b.real[0] * t * sign;
        result.real[1] = a.real[1] * (1.0f - t) + b.real[1] * t * sign;
        result.real[2] = a.real[2] * (1.0f - t) + b.real[2] * t * sign;
        result.real[3] = a.real[3] * (1.0f - t) + b.real[3] * t * sign;
        
        result.dual[0] = a.dual[0] * (1.0f - t) + b.dual[0] * t * sign;
        result.dual[1] = a.dual[1] * (1.0f - t) + b.dual[1] * t * sign;
        result.dual[2] = a.dual[2] * (1.0f - t) + b.dual[2] * t * sign;
        result.dual[3] = a.dual[3] * (1.0f - t) + b.dual[3] * t * sign;
        
        return Normalize(result);
    }

    CQDualQuat DualQuaternionMath::Sklerp(const CQDualQuat& a, const CQDualQuat& b, float t)
    {
        // For skinning, DQSlerp is often implemented via DLerp and normalize due to performance.
        // True screw linear interpolation is DLerp + Normalize (which we already implemented as DLerp).
        // Let's wrap it here for explicit naming conventions.
        return DLerp(a, b, t);
    }

    CQVec3 DualQuaternionMath::TransformPoint(const CQDualQuat& dq, const CQVec3& p)
    {
        // Extract translation: t = 2 * dual * conjugate(real)
        CQQuat conj = QuatConjugate(dq.real);
        CQQuat tQuat = QuatMultiply(dq.dual, conj);
        CQVec3 translation(tQuat[0] * 2.0f, tQuat[1] * 2.0f, tQuat[2] * 2.0f);
        
        // Extract rotation and apply
        CQVec3 rotP = QuatMultiplyVec3(dq.real, p);
        
        return Vec3Add(rotP, translation);
    }

    CQVec3 DualQuaternionMath::TransformVector(const CQDualQuat& dq, const CQVec3& v)
    {
        // Only apply rotation
        return QuatMultiplyVec3(dq.real, v);
    }
}