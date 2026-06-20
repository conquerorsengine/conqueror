#include "VecMath.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "Random.h"

namespace Conqueror::Math
{
    static glm::vec3 ToGLM(const CQVec3& v) { return glm::vec3(v.x, v.y, v.z); }
    static CQVec3 FromGLM(const glm::vec3& v) { return CQVec3(v.x, v.y, v.z); }

    CQVec3 Vec3Add(const CQVec3& a, const CQVec3& b)
    {
        return FromGLM(ToGLM(a) + ToGLM(b));
    }

    CQVec3 Vec3Sub(const CQVec3& a, const CQVec3& b)
    {
        return FromGLM(ToGLM(a) - ToGLM(b));
    }

    CQVec3 Vec3Mul(const CQVec3& a, float scalar)
    {
        return FromGLM(ToGLM(a) * scalar);
    }

    CQVec3 Vec3Div(const CQVec3& a, float scalar)
    {
        if (scalar == 0.0f) scalar = 0.000001f;
        return FromGLM(ToGLM(a) / scalar);
    }

    CQVec3 Vec3Lerp(const CQVec3& a, const CQVec3& b, float t)
    {
        return FromGLM(glm::mix(ToGLM(a), ToGLM(b), t));
    }

    CQVec3 Vec3Reflect(const CQVec3& v, const CQVec3& n)
    {
        return FromGLM(glm::reflect(ToGLM(v), ToGLM(n)));
    }

    CQVec3 Vec3InUnitSphere()
    {
        glm::vec3 v = Random::InUnitSphere();
        return FromGLM(v);
    }

    CQVec3 Vec3Cross(const CQVec3& a, const CQVec3& b)
    {
        return FromGLM(glm::cross(ToGLM(a), ToGLM(b)));
    }

    float Vec3Dot(const CQVec3& a, const CQVec3& b)
    {
        return glm::dot(ToGLM(a), ToGLM(b));
    }

    float Vec3Length(const CQVec3& a)
    {
        return glm::length(ToGLM(a));
    }

    CQVec3 Vec3Normalize(const CQVec3& a)
    {
        glm::vec3 g = ToGLM(a);
        float len = glm::length(g);
        if (len > 0.0f) g /= len;
        return FromGLM(g);
    }

    float Vec3Distance(const CQVec3& a, const CQVec3& b)
    {
        return glm::distance(ToGLM(a), ToGLM(b));
    }

    float Vec3DistanceSquared(const CQVec3& a, const CQVec3& b)
    {
        glm::vec3 d = ToGLM(a) - ToGLM(b);
        return glm::dot(d, d);
    }

    float Vec3Angle(const CQVec3& a, const CQVec3& b)
    {
        glm::vec3 ga = ToGLM(a), gb = ToGLM(b);
        float lenSq = glm::dot(ga, ga) * glm::dot(gb, gb);
        if (lenSq == 0.0f) return 0.0f;
        return std::acos(glm::clamp(glm::dot(ga, gb) / std::sqrt(lenSq), -1.0f, 1.0f));
    }

    CQVec3 Vec3Project(const CQVec3& v, const CQVec3& onto)
    {
        glm::vec3 gv = ToGLM(v), gn = ToGLM(onto);
        glm::vec3 res = gn * (glm::dot(gv, gn) / std::max(0.000001f, glm::dot(gn, gn)));
        return FromGLM(res);
    }

    CQVec3 Vec3Refract(const CQVec3& v, const CQVec3& n, float eta)
    {
        return FromGLM(glm::refract(ToGLM(v), ToGLM(n), eta));
    }

    CQVec3 Vec3MoveTowards(const CQVec3& current, const CQVec3& target, float maxDistanceDelta)
    {
        glm::vec3 c = ToGLM(current);
        glm::vec3 t = ToGLM(target);
        glm::vec3 dir = t - c;
        float dist = glm::length(dir);
        if (dist <= maxDistanceDelta || dist < 0.00001f)
            return target;
        return FromGLM(c + (dir / dist) * maxDistanceDelta);
    }

    CQVec3 Vec3Slerp(const CQVec3& a, const CQVec3& b, float t)
    {
        glm::vec3 ga = ToGLM(a);
        glm::vec3 gb = ToGLM(b);
        float dot = glm::dot(ga, gb);
        dot = glm::clamp(dot, -1.0f, 1.0f);
        float theta = std::acos(dot) * t;
        glm::vec3 relativeVec = glm::normalize(gb - ga * dot);
        return FromGLM(ga * std::cos(theta) + relativeVec * std::sin(theta));
    }

    CQVec3 Vec3Orthogonalize(const CQVec3& v, const CQVec3& n)
    {
        glm::vec3 gv = ToGLM(v);
        glm::vec3 gn = ToGLM(n);
        return FromGLM(glm::normalize(gv - gn * glm::dot(gv, gn)));
    }

    // ── CQVec4 Operations ──
    static glm::vec4 ToGLM4(const CQVec4& v) { return glm::vec4(v.x, v.y, v.z, v.w); }
    static CQVec4 FromGLM4(const glm::vec4& v) { return CQVec4(v.x, v.y, v.z, v.w); }

    CQVec4 Vec4Add(const CQVec4& a, const CQVec4& b) { return FromGLM4(ToGLM4(a) + ToGLM4(b)); }
    CQVec4 Vec4Sub(const CQVec4& a, const CQVec4& b) { return FromGLM4(ToGLM4(a) - ToGLM4(b)); }
    CQVec4 Vec4Mul(const CQVec4& a, float scalar) { return FromGLM4(ToGLM4(a) * scalar); }
    CQVec4 Vec4Div(const CQVec4& a, float scalar) { return FromGLM4(ToGLM4(a) / (scalar == 0.0f ? 0.000001f : scalar)); }
    float Vec4Dot(const CQVec4& a, const CQVec4& b) { return glm::dot(ToGLM4(a), ToGLM4(b)); }
    CQVec4 Vec4Normalize(const CQVec4& a)
    {
        glm::vec4 g = ToGLM4(a);
        float len = glm::length(g);
        if (len > 0.0f) g /= len;
        return FromGLM4(g);
    }

    // ── CQVec2 Operations ──
    static glm::vec2 ToGLM2(const CQVec2& v) { return glm::vec2(v.x, v.y); }
    static CQVec2 FromGLM2(const glm::vec2& v) { return CQVec2(v.x, v.y); }

    CQVec2 Vec2Add(const CQVec2& a, const CQVec2& b)
    {
        return FromGLM2(ToGLM2(a) + ToGLM2(b));
    }

    CQVec2 Vec2Sub(const CQVec2& a, const CQVec2& b)
    {
        return FromGLM2(ToGLM2(a) - ToGLM2(b));
    }

    CQVec2 Vec2Mul(const CQVec2& a, float scalar)
    {
        return FromGLM2(ToGLM2(a) * scalar);
    }

    float Vec2Dot(const CQVec2& a, const CQVec2& b)
    {
        return glm::dot(ToGLM2(a), ToGLM2(b));
    }

    float Vec2Distance(const CQVec2& a, const CQVec2& b)
    {
        return glm::distance(ToGLM2(a), ToGLM2(b));
    }

    float Vec2Angle(const CQVec2& a, const CQVec2& b)
    {
        glm::vec2 ga = ToGLM2(a), gb = ToGLM2(b);
        float lenSq = glm::dot(ga, ga) * glm::dot(gb, gb);
        if (lenSq == 0.0f) return 0.0f;
        return std::acos(glm::clamp(glm::dot(ga, gb) / std::sqrt(lenSq), -1.0f, 1.0f));
    }

    CQVec2 Vec2Normalize(const CQVec2& a)
    {
        glm::vec2 g = ToGLM2(a);
        float len = glm::length(g);
        if (len > 0.0f) g /= len;
        return FromGLM2(g);
    }

    float Vec2Length(const CQVec2& a)
    {
        return glm::length(ToGLM2(a));
    }
}
