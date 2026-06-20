#pragma once

#include "Core/Base/Base.h"
#include <glm/glm.hpp>
#include <vector>

namespace Conqueror
{
    class CQ_API DebugPhysicsDraw
    {
    public:
        static void AABB(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void AABB(const glm::vec3& center, const glm::vec3& halfExtents, bool centerMode, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);

        static void OBB(const glm::mat4& transform, const glm::vec3& halfExtents, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void ContactPoint(const glm::vec3& point, const glm::vec3& normal, float size, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void ContactManifold(const std::vector<glm::vec3>& points, const glm::vec3& normal, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);

        static void Velocity(const glm::vec3& origin, const glm::vec3& velocity, float scale, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void Force(const glm::vec3& origin, const glm::vec3& force, float scale, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void AngularVelocity(const glm::vec3& center, const glm::vec3& axis, float magnitude, float radius, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);

        static void CenterOfMass(const glm::vec3& position, float radius, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void InertiaTensor(const glm::mat4& transform, const glm::vec3& diagonal, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
        static void SweepAABB(const glm::vec3& min, const glm::vec3& max, const glm::vec3& displacement, const glm::vec4& color, float duration = 0.0f, bool depthTested = true);
    };
}
