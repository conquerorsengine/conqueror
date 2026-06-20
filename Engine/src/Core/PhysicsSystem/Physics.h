#pragma once

#include "Core/Base/Base.h"
#include "Physics2D/PhysicsWorld2D.h"
#include "Physics3D/PhysicsWorld3D.h"

namespace Conqueror
{
    class Entity;

    // Physics API - Script'lerden kullanılabilir
    class CQ_API Physics
    {
    public:
        // 2D Physics
        static void ApplyForce2D(Entity entity, const glm::vec2& force, const glm::vec2& point);
        static void ApplyForceToCenter2D(Entity entity, const glm::vec2& force);
        static void ApplyImpulse2D(Entity entity, const glm::vec2& impulse, const glm::vec2& point);
        static void ApplyImpulseToCenter2D(Entity entity, const glm::vec2& impulse);
        static void SetLinearVelocity2D(Entity entity, const glm::vec2& velocity);
        static glm::vec2 GetLinearVelocity2D(Entity entity);
        static void SetAngularVelocity2D(Entity entity, float velocity);
        static float GetAngularVelocity2D(Entity entity);

        // 3D Physics
        static void ApplyForce(Entity entity, const glm::vec3& force);
        static void ApplyImpulse(Entity entity, const glm::vec3& impulse);
        static void ApplyTorque(Entity entity, const glm::vec3& torque);
        static void ApplyTorqueImpulse(Entity entity, const glm::vec3& torqueImpulse);
        static void SetLinearVelocity(Entity entity, const glm::vec3& velocity);
        static glm::vec3 GetLinearVelocity(Entity entity);
        static void SetAngularVelocity(Entity entity, const glm::vec3& velocity);
        static glm::vec3 GetAngularVelocity(Entity entity);
    };
}
