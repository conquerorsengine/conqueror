#pragma once
#include "Core/Base/Base.h"
#include "VecMath.h"
#include "MatrixMath.h"
#include <array>

namespace Conqueror::Math
{
    // Native CQQuat struct
    using CQQuat = std::array<float, 4>; // x, y, z, w

    CQ_API CQQuat QuatIdentity();
    CQ_API CQQuat QuatFromEuler(const CQVec3& eulerAngles);
    CQ_API CQVec3 QuatToEuler(const CQQuat& q);
    CQ_API CQQuat QuatFromAxisAngle(const CQVec3& axis, float angle);
    CQ_API void QuatToAxisAngle(const CQQuat& q, CQVec3& axis, float& angle);
    
    CQ_API CQQuat QuatMultiply(const CQQuat& q1, const CQQuat& q2);
    CQ_API CQVec3 QuatMultiplyVec3(const CQQuat& q, const CQVec3& v);
    
    CQ_API float QuatDot(const CQQuat& q1, const CQQuat& q2);
    CQ_API float QuatLength(const CQQuat& q);
    CQ_API CQQuat QuatNormalize(const CQQuat& q);
    CQ_API CQQuat QuatInverse(const CQQuat& q);
    CQ_API CQQuat QuatConjugate(const CQQuat& q);
    
    CQ_API CQQuat QuatSlerp(const CQQuat& q1, const CQQuat& q2, float t);
    CQ_API CQQuat QuatNlerp(const CQQuat& q1, const CQQuat& q2, float t);
    
    CQ_API CQMat4 QuatToMat4(const CQQuat& q);
    CQ_API CQQuat Mat4ToQuat(const CQMat4& m);
    
    CQ_API CQQuat QuatLookRotation(const CQVec3& forward, const CQVec3& up);
}