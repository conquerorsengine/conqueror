#pragma once

#include "Core/Base/Base.h"
#include "Core/Time/Timestep.h"

#include <glm/glm.hpp>
#include <memory>

namespace JPH
{
    class PhysicsSystem;
    class TempAllocator;
    class JobSystem;
    class BroadPhaseLayerInterface;
    class ObjectVsBroadPhaseLayerFilter;
    class ObjectLayerPairFilter;
    class BodyInterface;
}

namespace Conqueror
{
    class Entity;

    class CQ_API PhysicsWorld3D
    {
    public:
        PhysicsWorld3D();
        ~PhysicsWorld3D();

        void Initialize();
        void Shutdown();

        void Step(Timestep ts);

        // Gravity
        void SetGravity(const glm::vec3& gravity);
        glm::vec3 GetGravity() const;

        // Body oluşturma/silme
        void CreateBody(Entity entity);
        void DestroyBody(Entity entity);

        // Kuvvet uygulama
        void ApplyForce(Entity entity, const glm::vec3& force);
        void ApplyImpulse(Entity entity, const glm::vec3& impulse);
        void ApplyTorque(Entity entity, const glm::vec3& torque);
        void ApplyTorqueImpulse(Entity entity, const glm::vec3& torqueImpulse);

        // Velocity
        void SetLinearVelocity(Entity entity, const glm::vec3& velocity);
        glm::vec3 GetLinearVelocity(Entity entity) const;
        void SetAngularVelocity(Entity entity, const glm::vec3& velocity);
        glm::vec3 GetAngularVelocity(Entity entity) const;

        JPH::BodyInterface* GetBodyInterface() const;

    private:
        void SyncTransformsFromPhysics();
        void SyncTransformsToPhysics();

    private:
        JPH::PhysicsSystem* m_PhysicsSystem = nullptr;
        JPH::TempAllocator* m_TempAllocator = nullptr;
        JPH::JobSystem* m_JobSystem = nullptr;
        JPH::BroadPhaseLayerInterface* m_BroadPhaseLayerInterface = nullptr;
        JPH::ObjectVsBroadPhaseLayerFilter* m_ObjectVsBroadPhaseLayerFilter = nullptr;
        JPH::ObjectLayerPairFilter* m_ObjectLayerPairFilter = nullptr;

        glm::vec3 m_Gravity = glm::vec3(0.0f, -9.81f, 0.0f);
        bool m_Initialized = false;

        class Scene* m_Scene = nullptr; // Entity'lere erişim için

        friend class Scene;
    };
}
