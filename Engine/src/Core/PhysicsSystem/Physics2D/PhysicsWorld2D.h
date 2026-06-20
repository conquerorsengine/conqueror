#pragma once

#include "Core/Base/Base.h"
#include "Core/Time/Timestep.h"

#include <glm/glm.hpp>
#include <memory>
#include <box2d/box2d.h>

namespace Conqueror
{
    class Entity;

    class CQ_API PhysicsWorld2D
    {
    public:
        PhysicsWorld2D();
        ~PhysicsWorld2D();

        void Initialize();
        void Shutdown();

        void Step(Timestep ts);

        // Gravity
        void SetGravity(const glm::vec2& gravity);
        glm::vec2 GetGravity() const;

        // Body oluşturma/silme
        void CreateBody(Entity entity);
        void DestroyBody(Entity entity);

        // Kuvvet uygulama
        void ApplyForce(Entity entity, const glm::vec2& force, const glm::vec2& point);
        void ApplyForceToCenter(Entity entity, const glm::vec2& force);
        void ApplyImpulse(Entity entity, const glm::vec2& impulse, const glm::vec2& point);
        void ApplyImpulseToCenter(Entity entity, const glm::vec2& impulse);
        void ApplyTorque(Entity entity, float torque);
        void ApplyAngularImpulse(Entity entity, float impulse);

        // Velocity
        void SetLinearVelocity(Entity entity, const glm::vec2& velocity);
        glm::vec2 GetLinearVelocity(Entity entity) const;
        void SetAngularVelocity(Entity entity, float velocity);
        float GetAngularVelocity(Entity entity) const;

        b2WorldId GetWorldId() const { return m_WorldId; }

    private:
        void SyncTransformsFromPhysics();
        void SyncTransformsToPhysics();

    private:
        b2WorldId m_WorldId;
        glm::vec2 m_Gravity = glm::vec2(0.0f, -9.81f);
        bool m_Initialized = false;

        class Scene* m_Scene = nullptr; // Entity'lere erişim için

        friend class Scene;
    };
}
