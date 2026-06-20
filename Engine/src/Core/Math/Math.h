#pragma once

#include "Core/Base/Base.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

namespace Conqueror::Math
{
    // Geometric primitives
    struct CQ_API Ray
    {
        glm::vec3 Origin;
        glm::vec3 Direction;
        
        glm::vec3 GetPoint(float t) const { return Origin + Direction * t; }
    };

    struct CQ_API AABB
    {
        glm::vec3 Min;
        glm::vec3 Max;
        
        glm::vec3 GetCenter() const { return (Min + Max) * 0.5f; }
        glm::vec3 GetExtents() const { return (Max - Min) * 0.5f; }
        glm::vec3 GetSize() const { return Max - Min; }
        float GetVolume() const { glm::vec3 size = GetSize(); return size.x * size.y * size.z; }
        
        void Expand(const glm::vec3& point);
        void Expand(const AABB& other);
        AABB Transform(const glm::mat4& matrix) const;
    };

    struct CQ_API Sphere
    {
        glm::vec3 Center;
        float Radius;
        
        float GetVolume() const { return 4.18879f * Radius * Radius * Radius; }
        float GetSurfaceArea() const { return 12.56637f * Radius * Radius; }
    };

    struct CQ_API Plane
    {
        glm::vec3 Normal;
        float Distance;
        
        Plane() : Normal(0, 1, 0), Distance(0) {}
        Plane(const glm::vec3& normal, float distance) : Normal(normal), Distance(distance) {}
        Plane(const glm::vec3& normal, const glm::vec3& point) : Normal(normal), Distance(glm::dot(normal, point)) {}
        
        float DistanceToPoint(const glm::vec3& point) const { return glm::dot(Normal, point) - Distance; }
        glm::vec3 ClosestPoint(const glm::vec3& point) const { return point - Normal * DistanceToPoint(point); }
    };

    struct CQ_API Triangle
    {
        glm::vec3 V0, V1, V2;
        
        glm::vec3 GetNormal() const { return glm::normalize(glm::cross(V1 - V0, V2 - V0)); }
        glm::vec3 GetCenter() const { return (V0 + V1 + V2) / 3.0f; }
        float GetArea() const { return glm::length(glm::cross(V1 - V0, V2 - V0)) * 0.5f; }
    };

    struct CQ_API Capsule
    {
        glm::vec3 Start;
        glm::vec3 End;
        float Radius;
    };

    struct CQ_API OBB // Oriented Bounding Box
    {
        glm::vec3 Center;
        glm::vec3 Extents;
        glm::mat3 Rotation;
    };

    // Frustum for culling
    class CQ_API Frustum
    {
    public:
        Frustum() = default;
        Frustum(const glm::mat4& viewProjection);
        
        void Update(const glm::mat4& viewProjection);
        
        bool ContainsPoint(const glm::vec3& point) const;
        bool ContainsSphere(const Sphere& sphere) const;
        bool ContainsAABB(const AABB& aabb) const;
        bool ContainsOBB(const OBB& obb) const;
        
        const Plane& GetPlane(int index) const { return m_Planes[index]; }
        
        enum PlaneIndex { Left = 0, Right, Bottom, Top, Near, Far };
        
    private:
        Plane m_Planes[6];
    };

    // Intersection tests
    CQ_API bool RayAABBIntersection(const Ray& ray, const AABB& aabb, float& tMin, float& tMax);
    CQ_API bool RayAABBIntersection(const Ray& ray, const AABB& aabb);
    CQ_API bool RaySphereIntersection(const Ray& ray, const Sphere& sphere, float& t);
    CQ_API bool RaySphereIntersection(const Ray& ray, const Sphere& sphere);
    CQ_API bool RayPlaneIntersection(const Ray& ray, const Plane& plane, float& t);
    CQ_API bool RayTriangleIntersection(const Ray& ray, const Triangle& triangle, float& t, glm::vec2& uv);
    CQ_API bool RayCapsuleIntersection(const Ray& ray, const Capsule& capsule, float& t);
    
    CQ_API bool SphereSphereIntersection(const Sphere& a, const Sphere& b);
    CQ_API bool SphereAABBIntersection(const Sphere& sphere, const AABB& aabb);
    CQ_API bool SpherePlaneIntersection(const Sphere& sphere, const Plane& plane);
    
    CQ_API bool AABBAABBIntersection(const AABB& a, const AABB& b);
    CQ_API bool AABBPlaneIntersection(const AABB& aabb, const Plane& plane);
    
    CQ_API bool PointInAABB(const glm::vec3& point, const AABB& aabb);
    CQ_API bool PointInSphere(const glm::vec3& point, const Sphere& sphere);
    CQ_API bool PointInTriangle(const glm::vec3& point, const Triangle& triangle);
    
    // Closest point queries
    CQ_API glm::vec3 ClosestPointOnAABB(const glm::vec3& point, const AABB& aabb);
    CQ_API glm::vec3 ClosestPointOnSphere(const glm::vec3& point, const Sphere& sphere);
    CQ_API glm::vec3 ClosestPointOnPlane(const glm::vec3& point, const Plane& plane);
    CQ_API glm::vec3 ClosestPointOnTriangle(const glm::vec3& point, const Triangle& triangle);
    CQ_API glm::vec3 ClosestPointOnLine(const glm::vec3& point, const glm::vec3& lineStart, const glm::vec3& lineEnd);
    
    // Bounding volume generation
    CQ_API AABB ComputeAABB(const std::vector<glm::vec3>& points);
    CQ_API Sphere ComputeBoundingSphere(const std::vector<glm::vec3>& points);
    CQ_API Sphere ComputeMinimalBoundingSphere(const std::vector<glm::vec3>& points);
    
    // Transform utilities
    CQ_API bool DecomposeTransform(const glm::mat4& transform, glm::vec3& translation, glm::vec3& rotation, glm::vec3& scale);
    CQ_API glm::mat4 ComposeTransform(const glm::vec3& translation, const glm::vec3& rotation, const glm::vec3& scale);
    CQ_API glm::mat4 ComposeTransform(const glm::vec3& translation, const glm::quat& rotation, const glm::vec3& scale);
    
    // Matrix utilities
    CQ_API glm::mat4 LookAt(const glm::vec3& eye, const glm::vec3& center, const glm::vec3& up);
    CQ_API glm::mat4 Perspective(float fov, float aspect, float near, float far);
    CQ_API glm::mat4 Orthographic(float left, float right, float bottom, float top, float near, float far);
    CQ_API glm::mat4 InverseTransform(const glm::mat4& transform);
    
    // Quaternion utilities
    CQ_API glm::quat QuaternionFromEuler(const glm::vec3& euler);
    CQ_API glm::vec3 EulerFromQuaternion(const glm::quat& quat);
    CQ_API glm::quat QuaternionFromAxisAngle(const glm::vec3& axis, float angle);
    CQ_API void QuaternionToAxisAngle(const glm::quat& quat, glm::vec3& axis, float& angle);
    CQ_API glm::quat QuaternionLookRotation(const glm::vec3& forward, const glm::vec3& up = glm::vec3(0, 1, 0));
    CQ_API glm::quat QuatMultiply(const glm::quat& a, const glm::quat& b);
    CQ_API glm::quat QuatNormalize(const glm::quat& q);
    CQ_API glm::quat QuatConjugate(const glm::quat& q);
    CQ_API glm::quat QuatInverse(const glm::quat& q);
    CQ_API float QuatDot(const glm::quat& a, const glm::quat& b);
    CQ_API float QuatLength(const glm::quat& q);
    CQ_API glm::vec3 QuatRotateVec3(const glm::quat& q, const glm::vec3& v);
    
    // Interpolation
    CQ_API glm::vec3 Lerp(const glm::vec3& a, const glm::vec3& b, float t);
    CQ_API glm::quat Slerp(const glm::quat& a, const glm::quat& b, float t);
    CQ_API glm::quat Nlerp(const glm::quat& a, const glm::quat& b, float t);
    CQ_API float SmoothDamp(float current, float target, float& velocity, float smoothTime, float deltaTime, float maxSpeed = INFINITY);
    CQ_API glm::vec3 SmoothDamp(const glm::vec3& current, const glm::vec3& target, glm::vec3& velocity, float smoothTime, float deltaTime, float maxSpeed = INFINITY);
    
    // Clamping
    CQ_API float Clamp(float value, float min, float max);
    CQ_API glm::vec3 Clamp(const glm::vec3& value, const glm::vec3& min, const glm::vec3& max);
    CQ_API float Clamp01(float value);
    
    // Angle utilities
    CQ_API float DegToRad(float degrees);
    CQ_API float RadToDeg(float radians);
    CQ_API float NormalizeAngle(float angle); // [-180, 180]
    CQ_API float DeltaAngle(float current, float target); // Shortest angle difference
    CQ_API float LerpAngle(float a, float b, float t);
    
    // Color space conversions
    CQ_API glm::vec3 RGBToHSV(const glm::vec3& rgb);
    CQ_API glm::vec3 HSVToRGB(const glm::vec3& hsv);
    CQ_API glm::vec3 RGBToHSL(const glm::vec3& rgb);
    CQ_API glm::vec3 HSLToRGB(const glm::vec3& hsl);
    CQ_API glm::vec3 LinearToSRGB(const glm::vec3& linear);
    CQ_API glm::vec3 SRGBToLinear(const glm::vec3& srgb);
    
    // Misc utilities
    CQ_API float Remap(float value, float fromMin, float fromMax, float toMin, float toMax);
    CQ_API float InverseLerp(float a, float b, float value);
    CQ_API float Repeat(float t, float length);
    CQ_API float PingPong(float t, float length);
    CQ_API int Sign(float value);
    CQ_API bool Approximately(float a, float b, float epsilon = 0.0001f);
}
