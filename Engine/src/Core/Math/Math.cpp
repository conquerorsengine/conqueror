#include "Math.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <random>

namespace Conqueror::Math
{
    void AABB::Expand(const glm::vec3& point)
    {
        Min = glm::min(Min, point);
        Max = glm::max(Max, point);
    }
    
    void AABB::Expand(const AABB& other)
    {
        Min = glm::min(Min, other.Min);
        Max = glm::max(Max, other.Max);
    }
    
    AABB AABB::Transform(const glm::mat4& matrix) const
    {
        glm::vec3 corners[8] = {
            glm::vec3(Min.x, Min.y, Min.z), glm::vec3(Max.x, Min.y, Min.z),
            glm::vec3(Min.x, Max.y, Min.z), glm::vec3(Max.x, Max.y, Min.z),
            glm::vec3(Min.x, Min.y, Max.z), glm::vec3(Max.x, Min.y, Max.z),
            glm::vec3(Min.x, Max.y, Max.z), glm::vec3(Max.x, Max.y, Max.z)
        };
        AABB result{glm::vec3(1e30f), glm::vec3(-1e30f)};
        for (int i = 0; i < 8; ++i) {
            result.Expand(glm::vec3(matrix * glm::vec4(corners[i], 1.0f)));
        }
        return result;
    }

    Frustum::Frustum(const glm::mat4& viewProjection) { Update(viewProjection); }
    
    void Frustum::Update(const glm::mat4& vp)
    {
        m_Planes[Left].Normal = glm::vec3(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0], vp[2][3] + vp[2][0]);
        m_Planes[Left].Distance = vp[3][3] + vp[3][0];
        m_Planes[Right].Normal = glm::vec3(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0], vp[2][3] - vp[2][0]);
        m_Planes[Right].Distance = vp[3][3] - vp[3][0];
        m_Planes[Bottom].Normal = glm::vec3(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1], vp[2][3] + vp[2][1]);
        m_Planes[Bottom].Distance = vp[3][3] + vp[3][1];
        m_Planes[Top].Normal = glm::vec3(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1], vp[2][3] - vp[2][1]);
        m_Planes[Top].Distance = vp[3][3] - vp[3][1];
        m_Planes[Near].Normal = glm::vec3(vp[0][3] + vp[0][2], vp[1][3] + vp[1][2], vp[2][3] + vp[2][2]);
        m_Planes[Near].Distance = vp[3][3] + vp[3][2];
        m_Planes[Far].Normal = glm::vec3(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2], vp[2][3] - vp[2][2]);
        m_Planes[Far].Distance = vp[3][3] - vp[3][2];

        for (int i = 0; i < 6; i++) {
            float length = glm::length(m_Planes[i].Normal);
            m_Planes[i].Normal /= length;
            m_Planes[i].Distance /= length;
        }
    }

    bool Frustum::ContainsPoint(const glm::vec3& point) const
    {
        for (int i = 0; i < 6; i++) if (m_Planes[i].DistanceToPoint(point) < 0) return false;
        return true;
    }

    bool Frustum::ContainsSphere(const Sphere& sphere) const
    {
        for (int i = 0; i < 6; i++) if (m_Planes[i].DistanceToPoint(sphere.Center) < -sphere.Radius) return false;
        return true;
    }

    bool Frustum::ContainsAABB(const AABB& aabb) const
    {
        for (int i = 0; i < 6; i++) {
            glm::vec3 p = aabb.Min;
            if (m_Planes[i].Normal.x >= 0) p.x = aabb.Max.x;
            if (m_Planes[i].Normal.y >= 0) p.y = aabb.Max.y;
            if (m_Planes[i].Normal.z >= 0) p.z = aabb.Max.z;
            if (m_Planes[i].DistanceToPoint(p) < 0) return false;
        }
        return true;
    }

    bool Frustum::ContainsOBB(const OBB& obb) const
    {
        for (int i = 0; i < 6; i++) {
            float r = obb.Extents.x * std::abs(glm::dot(m_Planes[i].Normal, obb.Rotation[0])) +
                      obb.Extents.y * std::abs(glm::dot(m_Planes[i].Normal, obb.Rotation[1])) +
                      obb.Extents.z * std::abs(glm::dot(m_Planes[i].Normal, obb.Rotation[2]));
            if (m_Planes[i].DistanceToPoint(obb.Center) < -r) return false;
        }
        return true;
    }

    bool RayAABBIntersection(const Ray& ray, const AABB& aabb, float& tMin, float& tMax)
    {
        glm::vec3 invDir = 1.0f / ray.Direction;
        glm::vec3 t0 = (aabb.Min - ray.Origin) * invDir;
        glm::vec3 t1 = (aabb.Max - ray.Origin) * invDir;

        glm::vec3 tSmaller = glm::min(t0, t1);
        glm::vec3 tBigger = glm::max(t0, t1);

        tMin = glm::max(tSmaller.x, glm::max(tSmaller.y, tSmaller.z));
        tMax = glm::min(tBigger.x, glm::min(tBigger.y, tBigger.z));

        return tMax >= tMin && tMax >= 0.0f;
    }

    bool RayAABBIntersection(const Ray& ray, const AABB& aabb)
    {
        float tMin, tMax;
        return RayAABBIntersection(ray, aabb, tMin, tMax);
    }

    bool RaySphereIntersection(const Ray& ray, const Sphere& sphere, float& t)
    {
        glm::vec3 oc = ray.Origin - sphere.Center;
        float a = glm::dot(ray.Direction, ray.Direction);
        float b = 2.0f * glm::dot(oc, ray.Direction);
        float c = glm::dot(oc, oc) - sphere.Radius * sphere.Radius;
        float discriminant = b * b - 4 * a * c;

        if (discriminant < 0.0f) return false;

        t = (-b - sqrt(discriminant)) / (2.0f * a);
        return t >= 0.0f;
    }

    bool RaySphereIntersection(const Ray& ray, const Sphere& sphere)
    {
        float t;
        return RaySphereIntersection(ray, sphere, t);
    }

    bool RayPlaneIntersection(const Ray& ray, const Plane& plane, float& t)
    {
        float denom = glm::dot(plane.Normal, ray.Direction);
        if (std::abs(denom) > 1e-6) {
            glm::vec3 p0 = plane.Normal * plane.Distance;
            t = glm::dot(p0 - ray.Origin, plane.Normal) / denom;
            return t >= 0;
        }
        return false;
    }

    bool RayTriangleIntersection(const Ray& ray, const Triangle& triangle, float& t, glm::vec2& uv)
    {
        glm::vec3 edge1 = triangle.V1 - triangle.V0;
        glm::vec3 edge2 = triangle.V2 - triangle.V0;
        glm::vec3 h = glm::cross(ray.Direction, edge2);
        float a = glm::dot(edge1, h);
        if (a > -1e-6 && a < 1e-6) return false;
        float f = 1.0f / a;
        glm::vec3 s = ray.Origin - triangle.V0;
        uv.x = f * glm::dot(s, h);
        if (uv.x < 0.0f || uv.x > 1.0f) return false;
        glm::vec3 q = glm::cross(s, edge1);
        uv.y = f * glm::dot(ray.Direction, q);
        if (uv.y < 0.0f || uv.x + uv.y > 1.0f) return false;
        t = f * glm::dot(edge2, q);
        return t > 1e-6;
    }

    bool RayCapsuleIntersection(const Ray& ray, const Capsule& capsule, float& t)
    {
        glm::vec3 ba = capsule.End - capsule.Start;
        glm::vec3 oa = ray.Origin - capsule.Start;

        float baba = glm::dot(ba, ba);
        float bard = glm::dot(ba, ray.Direction);
        float baoa = glm::dot(ba, oa);
        float rdoa = glm::dot(ray.Direction, oa);
        float oaoa = glm::dot(oa, oa);

        float a = baba - bard * bard;
        float b = baba * rdoa - baoa * bard;
        float c = baba * oaoa - baoa * baoa - capsule.Radius * capsule.Radius * baba;

        float h = b * b - a * c;

        if (h >= 0.0f)
        {
            float t_cyl = (-b - std::sqrt(h)) / a;
            float y = baoa + t_cyl * bard;

            if (y > 0.0f && y < baba)
            {
                t = t_cyl;
                return t >= 0.0f;
            }

            float oc1_sq = oaoa;
            float b1 = rdoa;
            float c1 = oc1_sq - capsule.Radius * capsule.Radius;
            float h1 = b1 * b1 - c1;

            float t1 = -1.0f;
            if (h1 > 0.0f) t1 = -b1 - std::sqrt(h1);

            glm::vec3 oc2 = ray.Origin - capsule.End;
            float oc2_sq = glm::dot(oc2, oc2);
            float b2 = glm::dot(ray.Direction, oc2);
            float c2 = oc2_sq - capsule.Radius * capsule.Radius;
            float h2 = b2 * b2 - c2;

            float t2 = -1.0f;
            if (h2 > 0.0f) t2 = -b2 - std::sqrt(h2);

            if (t1 >= 0.0f && (t2 < 0.0f || t1 < t2)) {
                t = t1;
                return true;
            } else if (t2 >= 0.0f) {
                t = t2;
                return true;
            }
        }
        return false;
    }

    bool SphereSphereIntersection(const Sphere& a, const Sphere& b)
    {
        float distance = glm::length(a.Center - b.Center);
        return distance < (a.Radius + b.Radius);
    }

    bool SphereAABBIntersection(const Sphere& sphere, const AABB& aabb)
    {
        glm::vec3 closest = ClosestPointOnAABB(sphere.Center, aabb);
        float distance = glm::length(closest - sphere.Center);
        return distance <= sphere.Radius;
    }

    bool SpherePlaneIntersection(const Sphere& sphere, const Plane& plane)
    {
        return std::abs(plane.DistanceToPoint(sphere.Center)) <= sphere.Radius;
    }

    bool AABBAABBIntersection(const AABB& a, const AABB& b)
    {
        return (a.Min.x <= b.Max.x && a.Max.x >= b.Min.x) &&
               (a.Min.y <= b.Max.y && a.Max.y >= b.Min.y) &&
               (a.Min.z <= b.Max.z && a.Max.z >= b.Min.z);
    }

    bool AABBPlaneIntersection(const AABB& aabb, const Plane& plane)
    {
        glm::vec3 center = aabb.GetCenter();
        glm::vec3 extents = aabb.GetExtents();
        float r = extents.x * std::abs(plane.Normal.x) + extents.y * std::abs(plane.Normal.y) + extents.z * std::abs(plane.Normal.z);
        return std::abs(plane.DistanceToPoint(center)) <= r;
    }

    bool PointInAABB(const glm::vec3& point, const AABB& aabb)
    {
        return (point.x >= aabb.Min.x && point.x <= aabb.Max.x) &&
               (point.y >= aabb.Min.y && point.y <= aabb.Max.y) &&
               (point.z >= aabb.Min.z && point.z <= aabb.Max.z);
    }

    bool PointInSphere(const glm::vec3& point, const Sphere& sphere)
    {
        float distance = glm::length(point - sphere.Center);
        return distance <= sphere.Radius;
    }

    bool PointInTriangle(const glm::vec3& point, const Triangle& triangle)
    {
        glm::vec3 v0 = triangle.V2 - triangle.V0;
        glm::vec3 v1 = triangle.V1 - triangle.V0;
        glm::vec3 v2 = point - triangle.V0;

        float dot00 = glm::dot(v0, v0);
        float dot01 = glm::dot(v0, v1);
        float dot02 = glm::dot(v0, v2);
        float dot11 = glm::dot(v1, v1);
        float dot12 = glm::dot(v1, v2);

        float invDenom = 1.0f / (dot00 * dot11 - dot01 * dot01);
        float u = (dot11 * dot02 - dot01 * dot12) * invDenom;
        float v = (dot00 * dot12 - dot01 * dot02) * invDenom;

        return (u >= 0.0f) && (v >= 0.0f) && (u + v < 1.0f);
    }

    glm::vec3 ClosestPointOnAABB(const glm::vec3& point, const AABB& aabb)
    {
        return glm::clamp(point, aabb.Min, aabb.Max);
    }

    glm::vec3 ClosestPointOnSphere(const glm::vec3& point, const Sphere& sphere)
    {
        glm::vec3 diff = point - sphere.Center;
        float len = glm::length(diff);
        if (len <= sphere.Radius) return point;
        return sphere.Center + (diff / len) * sphere.Radius;
    }

    glm::vec3 ClosestPointOnPlane(const glm::vec3& point, const Plane& plane)
    {
        return point - plane.Normal * plane.DistanceToPoint(point);
    }

    glm::vec3 ClosestPointOnTriangle(const glm::vec3& point, const Triangle& triangle)
    {
        Plane plane(triangle.GetNormal(), triangle.V0);
        glm::vec3 p = ClosestPointOnPlane(point, plane);
        if (PointInTriangle(p, triangle)) return p;
        return p; 
    }

    glm::vec3 ClosestPointOnLine(const glm::vec3& point, const glm::vec3& lineStart, const glm::vec3& lineEnd)
    {
        glm::vec3 lineDir = lineEnd - lineStart;
        float lengthSq = glm::dot(lineDir, lineDir);
        if (lengthSq == 0.0f) return lineStart;
        float t = glm::max(0.0f, glm::min(1.0f, glm::dot(point - lineStart, lineDir) / lengthSq));
        return lineStart + t * lineDir;
    }

    AABB ComputeAABB(const std::vector<glm::vec3>& points)
    {
        AABB aabb{glm::vec3(1e30f), glm::vec3(-1e30f)};
        for (const auto& p : points) aabb.Expand(p);
        return aabb;
    }

    Sphere ComputeBoundingSphere(const std::vector<glm::vec3>& points)
    {
        AABB aabb = ComputeAABB(points);
        Sphere sphere;
        sphere.Center = aabb.GetCenter();
        sphere.Radius = 0;
        for (const auto& p : points) {
            sphere.Radius = glm::max(sphere.Radius, glm::length(p - sphere.Center));
        }
        return sphere;
    }

    namespace Internal
    {
        Sphere SphereFrom2(const glm::vec3& a, const glm::vec3& b)
        {
            return {(a + b) * 0.5f, glm::length(a - b) * 0.5f};
        }

        Sphere SphereFrom3(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
        {
            glm::vec3 ac = c - a;
            glm::vec3 ab = b - a;
            glm::vec3 cross_ab_ac = glm::cross(ab, ac);
            
            glm::vec3 toCenter = (glm::cross(cross_ab_ac, ab) * glm::dot(ac, ac) + 
                                  glm::cross(ac, cross_ab_ac) * glm::dot(ab, ab)) / 
                                 (2.0f * glm::dot(cross_ab_ac, cross_ab_ac));
            
            return {a + toCenter, glm::length(toCenter)};
        }

        Sphere SphereFrom4(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d)
        {
            glm::vec3 ba = b - a;
            glm::vec3 ca = c - a;
            glm::vec3 da = d - a;

            glm::mat3 E(
                ba.x, ca.x, da.x,
                ba.y, ca.y, da.y,
                ba.z, ca.z, da.z
            );
            
            float det = glm::determinant(E);
            if (std::abs(det) < 1e-6f) return SphereFrom3(a, b, c);

            glm::vec3 den = glm::vec3(
                glm::dot(ba, ba),
                glm::dot(ca, ca),
                glm::dot(da, da)
            );
            
            glm::mat3 m_inv = glm::inverse(E);
            glm::vec3 u = m_inv * den * 0.5f;
            
            return {a + u, glm::length(u)};
        }

        Sphere Welzl(std::vector<glm::vec3>& P, std::vector<glm::vec3> R, size_t n)
        {
            if (n == 0 || R.size() == 4)
            {
                if (R.size() == 0) return {glm::vec3(0), 0};
                if (R.size() == 1) return {R[0], 0};
                if (R.size() == 2) return SphereFrom2(R[0], R[1]);
                if (R.size() == 3) return SphereFrom3(R[0], R[1], R[2]);
                return SphereFrom4(R[0], R[1], R[2], R[3]);
            }

            glm::vec3 p = P[n - 1];
            Sphere s = Welzl(P, R, n - 1);

            if (glm::length(p - s.Center) <= s.Radius + 1e-4f)
                return s;

            R.push_back(p);
            return Welzl(P, R, n - 1);
        }
    }

    Sphere ComputeMinimalBoundingSphere(const std::vector<glm::vec3>& points)
    {
        if (points.empty()) return {glm::vec3(0), 0};
        
        std::vector<glm::vec3> p = points;
        std::vector<glm::vec3> r;
        
        std::mt19937 gen(1337);
        std::shuffle(p.begin(), p.end(), gen);
        
        return Internal::Welzl(p, r, p.size());
    }

    bool DecomposeTransform(const glm::mat4& transform, glm::vec3& translation, glm::vec3& rotation, glm::vec3& scale)
    {
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::quat orientation;

        if (!glm::decompose(transform, scale, orientation, translation, skew, perspective))
            return false;

        rotation = glm::eulerAngles(orientation);
        return true;
    }

    glm::mat4 ComposeTransform(const glm::vec3& translation, const glm::vec3& rotation, const glm::vec3& scale)
    {
        glm::mat4 t = glm::translate(glm::mat4(1.0f), translation);
        glm::quat q = QuaternionFromEuler(rotation);
        glm::mat4 r = glm::mat4_cast(q);
        glm::mat4 s = glm::scale(glm::mat4(1.0f), scale);
        return t * r * s;
    }

    glm::mat4 ComposeTransform(const glm::vec3& translation, const glm::quat& rotation, const glm::vec3& scale)
    {
        glm::mat4 t = glm::translate(glm::mat4(1.0f), translation);
        glm::mat4 r = glm::mat4_cast(rotation);
        glm::mat4 s = glm::scale(glm::mat4(1.0f), scale);
        return t * r * s;
    }

    glm::mat4 LookAt(const glm::vec3& eye, const glm::vec3& center, const glm::vec3& up) { return glm::lookAt(eye, center, up); }
    glm::mat4 Perspective(float fov, float aspect, float near, float far) { return glm::perspective(fov, aspect, near, far); }
    glm::mat4 Orthographic(float left, float right, float bottom, float top, float near, float far) { return glm::ortho(left, right, bottom, top, near, far); }
    glm::mat4 InverseTransform(const glm::mat4& transform) { return glm::inverse(transform); }

    glm::quat QuaternionFromEuler(const glm::vec3& euler) { return glm::quat(euler); }
    glm::vec3 EulerFromQuaternion(const glm::quat& quat) { return glm::eulerAngles(quat); }
    glm::quat QuaternionFromAxisAngle(const glm::vec3& axis, float angle) { return glm::angleAxis(angle, glm::normalize(axis)); }
    void QuaternionToAxisAngle(const glm::quat& quat, glm::vec3& axis, float& angle) { axis = glm::axis(quat); angle = glm::angle(quat); }
    
    glm::quat QuaternionLookRotation(const glm::vec3& forward, const glm::vec3& up)
    {
        if (glm::length(forward) < 0.0001f) return glm::quat(1,0,0,0);
        glm::vec3 f = glm::normalize(forward);
        glm::vec3 r = glm::normalize(glm::cross(up, f));
        glm::vec3 u = glm::cross(f, r);
        glm::mat3 m(r, u, f);
        return glm::quat_cast(m);
    }

    glm::quat QuatMultiply(const glm::quat& a, const glm::quat& b) { return a * b; }
    glm::quat QuatNormalize(const glm::quat& q) { return glm::normalize(q); }
    glm::quat QuatConjugate(const glm::quat& q) { return glm::conjugate(q); }
    glm::quat QuatInverse(const glm::quat& q) { return glm::inverse(q); }
    float QuatDot(const glm::quat& a, const glm::quat& b) { return glm::dot(a, b); }
    float QuatLength(const glm::quat& q) { return glm::length(q); }
    glm::vec3 QuatRotateVec3(const glm::quat& q, const glm::vec3& v) { return q * v; }

    glm::vec3 Lerp(const glm::vec3& a, const glm::vec3& b, float t) { return glm::mix(a, b, t); }
    glm::quat Slerp(const glm::quat& a, const glm::quat& b, float t) { return glm::slerp(a, b, t); }
    glm::quat Nlerp(const glm::quat& a, const glm::quat& b, float t) { return glm::normalize(glm::lerp(a, b, t)); }

    float SmoothDamp(float current, float target, float& velocity, float smoothTime, float deltaTime, float maxSpeed)
    {
        smoothTime = glm::max(0.0001f, smoothTime);
        float omega = 2.0f / smoothTime;
        float x = omega * deltaTime;
        float exp = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
        float change = current - target;
        float originalTo = target;
        float maxChange = maxSpeed * smoothTime;
        change = glm::clamp(change, -maxChange, maxChange);
        target = current - change;
        float temp = (velocity + omega * change) * deltaTime;
        velocity = (velocity - omega * temp) * exp;
        float output = target + (change + temp) * exp;
        if (originalTo - current > 0.0f == output > originalTo) {
            output = originalTo;
            velocity = (output - originalTo) / deltaTime;
        }
        return output;
    }

    glm::vec3 SmoothDamp(const glm::vec3& current, const glm::vec3& target, glm::vec3& velocity, float smoothTime, float deltaTime, float maxSpeed)
    {
        float vx = velocity.x, vy = velocity.y, vz = velocity.z;
        float x = SmoothDamp(current.x, target.x, vx, smoothTime, deltaTime, maxSpeed);
        float y = SmoothDamp(current.y, target.y, vy, smoothTime, deltaTime, maxSpeed);
        float z = SmoothDamp(current.z, target.z, vz, smoothTime, deltaTime, maxSpeed);
        velocity = glm::vec3(vx, vy, vz);
        return glm::vec3(x, y, z);
    }

    float Clamp(float value, float min, float max) { return std::clamp(value, min, max); }
    glm::vec3 Clamp(const glm::vec3& value, const glm::vec3& min, const glm::vec3& max) { return glm::clamp(value, min, max); }
    float Clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }
    
    float DegToRad(float degrees) { return glm::radians(degrees); }
    float RadToDeg(float radians) { return glm::degrees(radians); }
    
    float NormalizeAngle(float angle)
    {
        angle = std::fmod(angle + 180.0f, 360.0f);
        if (angle < 0) angle += 360.0f;
        return angle - 180.0f;
    }
    
    float DeltaAngle(float current, float target)
    {
        float delta = std::fmod(target - current, 360.0f);
        if (delta > 180.0f) delta -= 360.0f;
        if (delta < -180.0f) delta += 360.0f;
        return delta;
    }
    
    float LerpAngle(float a, float b, float t)
    {
        float delta = DeltaAngle(a, b);
        return a + delta * Clamp01(t);
    }

    glm::vec3 RGBToHSV(const glm::vec3& rgb)
    {
        float cmax = glm::max(glm::max(rgb.r, rgb.g), rgb.b);
        float cmin = glm::min(glm::min(rgb.r, rgb.g), rgb.b);
        float delta = cmax - cmin;
        glm::vec3 hsv(0, 0, cmax);
        if (cmax > 0) hsv.y = delta / cmax;
        if (delta > 0) {
            if (cmax == rgb.r) hsv.x = 60.0f * std::fmod((rgb.g - rgb.b) / delta, 6.0f);
            else if (cmax == rgb.g) hsv.x = 60.0f * (((rgb.b - rgb.r) / delta) + 2.0f);
            else hsv.x = 60.0f * (((rgb.r - rgb.g) / delta) + 4.0f);
            if (hsv.x < 0) hsv.x += 360.0f;
        }
        return hsv;
    }

    glm::vec3 HSVToRGB(const glm::vec3& hsv)
    {
        float c = hsv.z * hsv.y;
        float x = c * (1.0f - std::abs(std::fmod(hsv.x / 60.0f, 2.0f) - 1.0f));
        float m = hsv.z - c;
        glm::vec3 rgb(0);
        if (hsv.x >= 0 && hsv.x < 60) rgb = {c, x, 0};
        else if (hsv.x >= 60 && hsv.x < 120) rgb = {x, c, 0};
        else if (hsv.x >= 120 && hsv.x < 180) rgb = {0, c, x};
        else if (hsv.x >= 180 && hsv.x < 240) rgb = {0, x, c};
        else if (hsv.x >= 240 && hsv.x < 300) rgb = {x, 0, c};
        else rgb = {c, 0, x};
        return rgb + glm::vec3(m);
    }

    glm::vec3 RGBToHSL(const glm::vec3& rgb)
    {
        float cmax = glm::max(glm::max(rgb.r, rgb.g), rgb.b);
        float cmin = glm::min(glm::min(rgb.r, rgb.g), rgb.b);
        float delta = cmax - cmin;
        
        glm::vec3 hsl(0.0f, 0.0f, (cmax + cmin) * 0.5f);
        
        if (delta > 0.0f)
        {
            hsl.y = hsl.z < 0.5f ? (delta / (cmax + cmin)) : (delta / (2.0f - cmax - cmin));
            
            if (cmax == rgb.r) hsl.x = 60.0f * std::fmod((rgb.g - rgb.b) / delta, 6.0f);
            else if (cmax == rgb.g) hsl.x = 60.0f * (((rgb.b - rgb.r) / delta) + 2.0f);
            else hsl.x = 60.0f * (((rgb.r - rgb.g) / delta) + 4.0f);
            
            if (hsl.x < 0.0f) hsl.x += 360.0f;
        }
        return hsl;
    }

    static float HueToRGB(float p, float q, float t)
    {
        if (t < 0.0f) t += 1.0f;
        if (t > 1.0f) t -= 1.0f;
        if (t < 1.0f/6.0f) return p + (q - p) * 6.0f * t;
        if (t < 1.0f/2.0f) return q;
        if (t < 2.0f/3.0f) return p + (q - p) * (2.0f/3.0f - t) * 6.0f;
        return p;
    }

    glm::vec3 HSLToRGB(const glm::vec3& hsl)
    {
        glm::vec3 rgb;
        if (hsl.y == 0.0f)
        {
            rgb = glm::vec3(hsl.z);
        }
        else
        {
            float q = hsl.z < 0.5f ? hsl.z * (1.0f + hsl.y) : hsl.z + hsl.y - hsl.z * hsl.y;
            float p = 2.0f * hsl.z - q;
            float hk = hsl.x / 360.0f;
            
            rgb.r = HueToRGB(p, q, hk + 1.0f/3.0f);
            rgb.g = HueToRGB(p, q, hk);
            rgb.b = HueToRGB(p, q, hk - 1.0f/3.0f);
        }
        return rgb;
    }
    glm::vec3 LinearToSRGB(const glm::vec3& linear) { return glm::pow(linear, glm::vec3(1.0f/2.2f)); }
    glm::vec3 SRGBToLinear(const glm::vec3& srgb) { return glm::pow(srgb, glm::vec3(2.2f)); }

    float Remap(float value, float fromMin, float fromMax, float toMin, float toMax)
    {
        float t = InverseLerp(fromMin, fromMax, value);
        return toMin + (toMax - toMin) * t;
    }

    float InverseLerp(float a, float b, float value)
    {
        if (a != b) return Clamp01((value - a) / (b - a));
        return 0.0f;
    }

    float Repeat(float t, float length) { return Clamp(t - glm::floor(t / length) * length, 0.0f, length); }
    float PingPong(float t, float length) { t = Repeat(t, length * 2.0f); return length - std::abs(t - length); }
    int Sign(float value) { return (value > 0.0f) - (value < 0.0f); }
    bool Approximately(float a, float b, float epsilon) { return std::abs(b - a) < glm::max(0.000001f * glm::max(std::abs(a), std::abs(b)), epsilon); }
}
