#include "Physics.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"

namespace Conqueror
{
    // 2D Physics
    void Physics::ApplyForce2D(Entity entity, const glm::vec2& force, const glm::vec2& point)
    {
        if (auto* world = entity.GetScene()->GetPhysicsWorld2D())
            world->ApplyForce(entity, force, point);
    }

    void Physics::ApplyForceToCenter2D(Entity entity, const glm::vec2& force)
    {
        if (auto* world = entity.GetScene()->GetPhysicsWorld2D())
            world->ApplyForceToCenter(entity, force);
    }

    void Physics::ApplyImpulse2D(Entity entity, const glm::vec2& impulse, const glm::vec2& point)
    {
        if (auto* world = entity.GetScene()->GetPhysicsWorld2D())
            world->ApplyImpulse(entity, impulse, point);
    }

    void Physics::ApplyImpulseToCenter2D(Entity entity, const glm::vec2& impulse)
    {
        if (auto* world = entity.GetScene()->GetPhysicsWorld2D())
            world->ApplyImpulseToCenter(entity, impulse);
    }

    void Physics::SetLinearVelocity2D(Entity entity, const glm::vec2& velocity)
    {
        if (auto* world = entity.GetScene()->GetPhysicsWorld2D())
            world->SetLinearVelocity(entity, velocity);
    }

    glm::vec2 Physics::GetLinearVelocity2D(Entity entity)
    {
        if (auto* world = entity.GetScene()->GetPhysicsWorld2D())
            return world->GetLinearVelocity(entity);
        return glm::vec2(0.0f);
    }

    void Physics::SetAngularVelocity2D(Entity entity, float velocity)
    {
        if (auto* world = entity.GetScene()->GetPhysicsWorld2D())
            world->SetAngularVelocity(entity, velocity);
    }

    float Physics::GetAngularVelocity2D(Entity entity)
    {
        if (auto* world = entity.GetScene()->GetPhysicsWorld2D())
            return world->GetAngularVelocity(entity);
        return 0.0f;
    }

    // 3D Physics
    void Physics::ApplyForce(Entity entity, const glm::vec3& force)
    {
        if (auto* world = entity.GetScene()->GetPhysicsWorld3D())
            world->ApplyForce(entity, force);
    }

    void Physics::ApplyImpulse(Entity entity, const glm::vec3& impulse)
    {
        if (auto* world = entity.GetScene()->GetPhysicsWorld3D())
            world->ApplyImpulse(entity, impulse);
    }

    void Physics::ApplyTorque(Entity entity, const glm::vec3& torque)
    {
        if (auto* world = entity.GetScene()->GetPhysicsWorld3D())
            world->ApplyTorque(entity, torque);
    }

    void Physics::ApplyTorqueImpulse(Entity entity, const glm::vec3& torqueImpulse)
    {
        if (auto* world = entity.GetScene()->GetPhysicsWorld3D())
            world->ApplyTorqueImpulse(entity, torqueImpulse);
    }

    void Physics::SetLinearVelocity(Entity entity, const glm::vec3& velocity)
    {
        if (auto* world = entity.GetScene()->GetPhysicsWorld3D())
            world->SetLinearVelocity(entity, velocity);
    }

    glm::vec3 Physics::GetLinearVelocity(Entity entity)
    {
        if (auto* world = entity.GetScene()->GetPhysicsWorld3D())
            return world->GetLinearVelocity(entity);
        return glm::vec3(0.0f);
    }

    void Physics::SetAngularVelocity(Entity entity, const glm::vec3& velocity)
    {
        if (auto* world = entity.GetScene()->GetPhysicsWorld3D())
            world->SetAngularVelocity(entity, velocity);
    }

    glm::vec3 Physics::GetAngularVelocity(Entity entity)
    {
        if (auto* world = entity.GetScene()->GetPhysicsWorld3D())
            return world->GetAngularVelocity(entity);
        return glm::vec3(0.0f);
    }
}
