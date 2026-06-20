#include "QuaternionMath.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Conqueror::Math
{
    static glm::quat ToGLMQuat(const CQQuat& q) { return glm::quat(q[3], q[0], q[1], q[2]); }
    static CQQuat FromGLMQuat(const glm::quat& q) { return {q.x, q.y, q.z, q.w}; }
    static glm::vec3 ToGLMVec3(const CQVec3& v) { return glm::vec3(v.x, v.y, v.z); }
    static CQVec3 FromGLMVec3(const glm::vec3& v) { return {v.x, v.y, v.z}; }
    static glm::mat4 ToGLMMat4(const CQMat4& m) { return glm::make_mat4(m.data()); }
    static CQMat4 FromGLMMat4(const glm::mat4& m) { 
        CQMat4 result;
        const float* ptr = glm::value_ptr(m);
        for(int i=0; i<16; ++i) result[i] = ptr[i];
        return result; 
    }

    CQQuat QuatIdentity()
    {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }

    CQQuat QuatFromEuler(const CQVec3& eulerAngles)
    {
        glm::quat q = glm::quat(ToGLMVec3(eulerAngles));
        return FromGLMQuat(q);
    }

    CQVec3 QuatToEuler(const CQQuat& q)
    {
        return FromGLMVec3(glm::eulerAngles(ToGLMQuat(q)));
    }

    CQQuat QuatFromAxisAngle(const CQVec3& axis, float angle)
    {
        return FromGLMQuat(glm::angleAxis(angle, glm::normalize(ToGLMVec3(axis))));
    }

    void QuatToAxisAngle(const CQQuat& q, CQVec3& axis, float& angle)
    {
        glm::quat gq = ToGLMQuat(q);
        axis = FromGLMVec3(glm::axis(gq));
        angle = glm::angle(gq);
    }

    CQQuat QuatMultiply(const CQQuat& q1, const CQQuat& q2)
    {
        return FromGLMQuat(ToGLMQuat(q1) * ToGLMQuat(q2));
    }

    CQVec3 QuatMultiplyVec3(const CQQuat& q, const CQVec3& v)
    {
        return FromGLMVec3(ToGLMQuat(q) * ToGLMVec3(v));
    }

    float QuatDot(const CQQuat& q1, const CQQuat& q2)
    {
        return glm::dot(ToGLMQuat(q1), ToGLMQuat(q2));
    }

    float QuatLength(const CQQuat& q)
    {
        return glm::length(ToGLMQuat(q));
    }

    CQQuat QuatNormalize(const CQQuat& q)
    {
        return FromGLMQuat(glm::normalize(ToGLMQuat(q)));
    }

    CQQuat QuatInverse(const CQQuat& q)
    {
        return FromGLMQuat(glm::inverse(ToGLMQuat(q)));
    }

    CQQuat QuatConjugate(const CQQuat& q)
    {
        return FromGLMQuat(glm::conjugate(ToGLMQuat(q)));
    }

    CQQuat QuatSlerp(const CQQuat& q1, const CQQuat& q2, float t)
    {
        return FromGLMQuat(glm::slerp(ToGLMQuat(q1), ToGLMQuat(q2), t));
    }

    CQQuat QuatNlerp(const CQQuat& q1, const CQQuat& q2, float t)
    {
        return FromGLMQuat(glm::normalize(glm::lerp(ToGLMQuat(q1), ToGLMQuat(q2), t)));
    }

    CQMat4 QuatToMat4(const CQQuat& q)
    {
        return FromGLMMat4(glm::mat4_cast(ToGLMQuat(q)));
    }

    CQQuat Mat4ToQuat(const CQMat4& m)
    {
        return FromGLMQuat(glm::quat_cast(ToGLMMat4(m)));
    }

    CQQuat QuatLookRotation(const CQVec3& forward, const CQVec3& up)
    {
        glm::vec3 f = glm::normalize(ToGLMVec3(forward));
        glm::vec3 u = glm::normalize(ToGLMVec3(up));
        glm::vec3 r = glm::normalize(glm::cross(u, f));
        u = glm::cross(f, r);
        glm::mat3 m(r, u, f);
        return FromGLMQuat(glm::quat_cast(m));
    }
}