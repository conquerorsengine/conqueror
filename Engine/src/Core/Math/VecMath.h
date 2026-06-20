#pragma once
#include "Core/Base/Base.h"

namespace Conqueror::Math
{
    struct CQ_API CQVec2
    {
        float x = 0.0f, y = 0.0f;
        CQVec2() = default;
        CQVec2(float x, float y) : x(x), y(y) {}
    };

    // Motor seviyesinde saf vektör tipi (glm bağımsız)
    struct CQ_API CQVec3
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        CQVec3() = default;
        CQVec3(float x, float y, float z) : x(x), y(y), z(z) {}
    };

    CQ_API CQVec3 Vec3Add(const CQVec3& a, const CQVec3& b);
    CQ_API CQVec3 Vec3Sub(const CQVec3& a, const CQVec3& b);
    CQ_API CQVec3 Vec3Mul(const CQVec3& a, float scalar);
    CQ_API CQVec3 Vec3Div(const CQVec3& a, float scalar);
    CQ_API CQVec3 Vec3Lerp(const CQVec3& a, const CQVec3& b, float t);
    CQ_API CQVec3 Vec3Reflect(const CQVec3& v, const CQVec3& n);
    CQ_API CQVec3 Vec3InUnitSphere();
    CQ_API CQVec3 Vec3Cross(const CQVec3& a, const CQVec3& b);
    CQ_API float Vec3Dot(const CQVec3& a, const CQVec3& b);
    CQ_API float Vec3Length(const CQVec3& a);
    CQ_API CQVec3 Vec3Normalize(const CQVec3& a);
    CQ_API float Vec3Distance(const CQVec3& a, const CQVec3& b);
    CQ_API float Vec3DistanceSquared(const CQVec3& a, const CQVec3& b);
    CQ_API float Vec3Angle(const CQVec3& a, const CQVec3& b);
    CQ_API CQVec3 Vec3Project(const CQVec3& v, const CQVec3& onto);

    struct CQ_API CQVec4
    {
        float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
        CQVec4() = default;
        CQVec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    };

    CQ_API CQVec3 Vec3Refract(const CQVec3& v, const CQVec3& n, float eta);
    CQ_API CQVec3 Vec3MoveTowards(const CQVec3& current, const CQVec3& target, float maxDistanceDelta);
    CQ_API CQVec3 Vec3Slerp(const CQVec3& a, const CQVec3& b, float t);
    CQ_API CQVec3 Vec3Orthogonalize(const CQVec3& v, const CQVec3& n);

    // CQVec4 operations
    CQ_API CQVec4 Vec4Add(const CQVec4& a, const CQVec4& b);
    CQ_API CQVec4 Vec4Sub(const CQVec4& a, const CQVec4& b);
    CQ_API CQVec4 Vec4Mul(const CQVec4& a, float scalar);
    CQ_API CQVec4 Vec4Div(const CQVec4& a, float scalar);
    CQ_API float Vec4Dot(const CQVec4& a, const CQVec4& b);
    CQ_API CQVec4 Vec4Normalize(const CQVec4& a);

    // CQVec2 operations
    CQ_API CQVec2 Vec2Add(const CQVec2& a, const CQVec2& b);
    CQ_API CQVec2 Vec2Sub(const CQVec2& a, const CQVec2& b);
    CQ_API CQVec2 Vec2Mul(const CQVec2& a, float scalar);
    CQ_API float Vec2Dot(const CQVec2& a, const CQVec2& b);
    CQ_API float Vec2Distance(const CQVec2& a, const CQVec2& b);
    CQ_API float Vec2Angle(const CQVec2& a, const CQVec2& b);
    CQ_API CQVec2 Vec2Normalize(const CQVec2& a);
    CQ_API float Vec2Length(const CQVec2& a);
}
