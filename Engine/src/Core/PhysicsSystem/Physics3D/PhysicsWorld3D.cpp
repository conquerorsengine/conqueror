#include "PhysicsWorld3D.h"
#include "Core/Logging/Log.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>

namespace Conqueror
{
    // Jolt layer sistemi
    namespace Layers
    {
        static constexpr JPH::ObjectLayer NON_MOVING = 0;
        static constexpr JPH::ObjectLayer MOVING = 1;
        static constexpr JPH::uint NUM_LAYERS = 2;
    };

    namespace BroadPhaseLayers
    {
        static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
        static constexpr JPH::BroadPhaseLayer MOVING(1);
        static constexpr JPH::uint NUM_LAYERS(2);
    };

    // BroadPhase layer interface
    class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
    {
    public:
        BPLayerInterfaceImpl()
        {
            m_ObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
            m_ObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
        }

        virtual JPH::uint GetNumBroadPhaseLayers() const override
        {
            return BroadPhaseLayers::NUM_LAYERS;
        }

        virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
        {
            return m_ObjectToBroadPhase[inLayer];
        }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
        {
            switch ((JPH::BroadPhaseLayer::Type)inLayer)
            {
                case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING: return "NON_MOVING";
                case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING: return "MOVING";
                default: return "INVALID";
            }
        }
#endif

    private:
        JPH::BroadPhaseLayer m_ObjectToBroadPhase[Layers::NUM_LAYERS];
    };

    // Object vs BroadPhase layer filter
    class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter
    {
    public:
        virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
        {
            switch (inLayer1)
            {
                case Layers::NON_MOVING:
                    return inLayer2 == BroadPhaseLayers::MOVING;
                case Layers::MOVING:
                    return true;
                default:
                    return false;
            }
        }
    };

    // Object layer pair filter
    class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter
    {
    public:
        virtual bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override
        {
            switch (inObject1)
            {
                case Layers::NON_MOVING:
                    return inObject2 == Layers::MOVING;
                case Layers::MOVING:
                    return true;
                default:
                    return false;
            }
        }
    };

    PhysicsWorld3D::PhysicsWorld3D()
    {
    }

    PhysicsWorld3D::~PhysicsWorld3D()
    {
        Shutdown();
    }

    void PhysicsWorld3D::Initialize()
    {
        if (m_Initialized)
            return;

        // Jolt'u başlat
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();

        // Temp allocator
        m_TempAllocator = new JPH::TempAllocatorImpl(10 * 1024 * 1024);

        // Job system
        m_JobSystem = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::thread::hardware_concurrency() - 1);

        // Layer interfaces
        m_BroadPhaseLayerInterface = new BPLayerInterfaceImpl();
        m_ObjectVsBroadPhaseLayerFilter = new ObjectVsBroadPhaseLayerFilterImpl();
        m_ObjectLayerPairFilter = new ObjectLayerPairFilterImpl();

        // Physics system
        const JPH::uint cMaxBodies = 10240;
        const JPH::uint cNumBodyMutexes = 0;
        const JPH::uint cMaxBodyPairs = 65536;
        const JPH::uint cMaxContactConstraints = 10240;

        m_PhysicsSystem = new JPH::PhysicsSystem();
        m_PhysicsSystem->Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints,
            *m_BroadPhaseLayerInterface, *m_ObjectVsBroadPhaseLayerFilter, *m_ObjectLayerPairFilter);

        m_PhysicsSystem->SetGravity(JPH::Vec3(m_Gravity.x, m_Gravity.y, m_Gravity.z));

        m_Initialized = true;
        CQ_CORE_INFO("Physics3D initialized");
    }

    void PhysicsWorld3D::Shutdown()
    {
        if (!m_Initialized)
            return;

        delete m_PhysicsSystem;
        delete m_ObjectLayerPairFilter;
        delete m_ObjectVsBroadPhaseLayerFilter;
        delete m_BroadPhaseLayerInterface;
        delete m_JobSystem;
        delete m_TempAllocator;
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;

        m_Initialized = false;
        CQ_CORE_INFO("Physics3D shutdown");
    }

    void PhysicsWorld3D::Step(Timestep ts)
    {
        if (!m_Initialized)
            return;

        const int cCollisionSteps = 1;
        m_PhysicsSystem->Update(ts.GetSeconds(), cCollisionSteps, m_TempAllocator, m_JobSystem);

        SyncTransformsFromPhysics();
    }

    void PhysicsWorld3D::SetGravity(const glm::vec3& gravity)
    {
        m_Gravity = gravity;
        if (m_Initialized)
            m_PhysicsSystem->SetGravity(JPH::Vec3(gravity.x, gravity.y, gravity.z));
    }

    glm::vec3 PhysicsWorld3D::GetGravity() const
    {
        return m_Gravity;
    }

    void PhysicsWorld3D::CreateBody(Entity entity)
    {
        if (!m_Initialized || !entity.HasComponent<RigidbodyComponent>())
            return;

        auto& rb = entity.GetComponent<RigidbodyComponent>();
        auto& transform = entity.GetComponent<TransformComponent>();

        // Shape oluştur
        JPH::ShapeRefC shape = nullptr;

        // MeshRendererComponent varsa collider'ı mesh boyutuna göre otomatik ayarla
        if (entity.HasComponent<MeshRendererComponent>())
        {
            auto& meshRenderer = entity.GetComponent<MeshRendererComponent>();

            if (entity.HasComponent<BoxColliderComponent>())
            {
                auto& collider = entity.GetComponent<BoxColliderComponent>();
                collider.Size = {2.0f, 2.0f, 2.0f};
                glm::vec3 halfExtents = collider.Size * transform.Scale * 0.5f;
                shape = new JPH::BoxShape(JPH::Vec3(halfExtents.x, halfExtents.y, halfExtents.z));
            }
            else if (entity.HasComponent<SphereColliderComponent>())
            {
                auto& collider = entity.GetComponent<SphereColliderComponent>();
                collider.Radius = 1.0f;
                float radius = collider.Radius * glm::max(glm::max(transform.Scale.x, transform.Scale.y), transform.Scale.z);
                shape = new JPH::SphereShape(radius);
            }
            else if (entity.HasComponent<CapsuleColliderComponent>())
            {
                auto& collider = entity.GetComponent<CapsuleColliderComponent>();
                collider.Radius = 1.0f;
                collider.Height = 2.0f;
                float radius = collider.Radius * glm::max(transform.Scale.x, transform.Scale.z);
                float halfHeight = collider.Height * transform.Scale.y * 0.5f;
                shape = new JPH::CapsuleShape(halfHeight, radius);
            }
        }
        else
        {
            if (entity.HasComponent<BoxColliderComponent>())
            {
                auto& collider = entity.GetComponent<BoxColliderComponent>();
                glm::vec3 halfExtents = collider.Size * transform.Scale * 0.5f;
                shape = new JPH::BoxShape(JPH::Vec3(halfExtents.x, halfExtents.y, halfExtents.z));
            }
            else if (entity.HasComponent<SphereColliderComponent>())
            {
                auto& collider = entity.GetComponent<SphereColliderComponent>();
                float radius = collider.Radius * glm::max(glm::max(transform.Scale.x, transform.Scale.y), transform.Scale.z);
                shape = new JPH::SphereShape(radius);
            }
            else if (entity.HasComponent<CapsuleColliderComponent>())
            {
                auto& collider = entity.GetComponent<CapsuleColliderComponent>();
                float radius = collider.Radius * glm::max(transform.Scale.x, transform.Scale.z);
                float halfHeight = collider.Height * transform.Scale.y * 0.5f;
                shape = new JPH::CapsuleShape(halfHeight, radius);
            }
        }

        if (!shape)
            return;

        // Body type
        JPH::EMotionType motionType;
        JPH::ObjectLayer layer;

        switch (rb.Type)
        {
            case RigidbodyComponent::BodyType::Static:
                motionType = JPH::EMotionType::Static;
                layer = Layers::NON_MOVING;
                break;
            case RigidbodyComponent::BodyType::Kinematic:
                motionType = JPH::EMotionType::Kinematic;
                layer = Layers::MOVING;
                break;
            case RigidbodyComponent::BodyType::Dynamic:
            default:
                motionType = JPH::EMotionType::Dynamic;
                layer = Layers::MOVING;
                break;
        }

        // Body oluştur
        JPH::BodyCreationSettings bodySettings(
            shape,
            JPH::RVec3(transform.Position.x, transform.Position.y, transform.Position.z),
            JPH::Quat::sEulerAngles(JPH::Vec3(transform.Rotation.x, transform.Rotation.y, transform.Rotation.z)),
            motionType,
            layer
        );

        bodySettings.mFriction = rb.Friction;
        bodySettings.mRestitution = rb.Restitution;
        bodySettings.mLinearDamping = rb.LinearDrag;
        bodySettings.mAngularDamping = rb.AngularDrag;
        bodySettings.mGravityFactor = rb.GravityScale;

        if (rb.Type == RigidbodyComponent::BodyType::Dynamic)
        {
            // Mass ve inertia'yı shape'den hesapla
            bodySettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateMassAndInertia;
            bodySettings.mMassPropertiesOverride.mMass = rb.Mass;
            
            CQ_CORE_INFO("PhysicsWorld3D::CreateBody - Creating Dynamic body with mass: {0}", rb.Mass);
        }

        JPH::Body* body = m_PhysicsSystem->GetBodyInterface().CreateBody(bodySettings);
        if (body)
        {
            m_PhysicsSystem->GetBodyInterface().AddBody(body->GetID(), JPH::EActivation::Activate);
            rb.RuntimeBody = reinterpret_cast<void*>(static_cast<uintptr_t>(body->GetID().GetIndexAndSequenceNumber()));
        }
    }

    void PhysicsWorld3D::DestroyBody(Entity entity)
    {
        if (!m_Initialized || !entity.HasComponent<RigidbodyComponent>())
            return;

        auto& rb = entity.GetComponent<RigidbodyComponent>();
        if (rb.RuntimeBody)
        {
            JPH::BodyID bodyID(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rb.RuntimeBody)));
            m_PhysicsSystem->GetBodyInterface().RemoveBody(bodyID);
            m_PhysicsSystem->GetBodyInterface().DestroyBody(bodyID);
            rb.RuntimeBody = nullptr;
        }
    }

    void PhysicsWorld3D::ApplyForce(Entity entity, const glm::vec3& force)
    {
        if (!m_Initialized)
        {
            CQ_CORE_ERROR("PhysicsWorld3D::ApplyForce - Physics world not initialized!");
            return;
        }

        if (!entity.HasComponent<RigidbodyComponent>())
        {
            CQ_CORE_ERROR("PhysicsWorld3D::ApplyForce - Entity has no RigidbodyComponent!");
            return;
        }

        auto& rb = entity.GetComponent<RigidbodyComponent>();
        if (!rb.RuntimeBody)
        {
            CQ_CORE_ERROR("PhysicsWorld3D::ApplyForce - RuntimeBody is null! Body not created yet?");
            return;
        }

        JPH::BodyID bodyID(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rb.RuntimeBody)));
        
        // Body'nin aktif olup olmadığını kontrol et
        JPH::BodyInterface& bodyInterface = m_PhysicsSystem->GetBodyInterface();
        
        if (!bodyInterface.IsActive(bodyID))
        {
            CQ_CORE_WARN("PhysicsWorld3D::ApplyForce - Body is not active! Activating...");
            bodyInterface.ActivateBody(bodyID);
        }
        
        // Force uygula
        bodyInterface.AddForce(bodyID, JPH::Vec3(force.x, force.y, force.z));
        
        // Velocity'yi kontrol et (debug)
        JPH::Vec3 velocity = bodyInterface.GetLinearVelocity(bodyID);
        CQ_CORE_INFO("PhysicsWorld3D::ApplyForce - Force: ({0}, {1}, {2}), Velocity after: ({3}, {4}, {5})", 
                     force.x, force.y, force.z, velocity.GetX(), velocity.GetY(), velocity.GetZ());
    }

    void PhysicsWorld3D::ApplyImpulse(Entity entity, const glm::vec3& impulse)
    {
        if (!m_Initialized || !entity.HasComponent<RigidbodyComponent>())
            return;

        auto& rb = entity.GetComponent<RigidbodyComponent>();
        if (rb.RuntimeBody)
        {
            JPH::BodyID bodyID(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rb.RuntimeBody)));
            m_PhysicsSystem->GetBodyInterface().AddImpulse(bodyID, JPH::Vec3(impulse.x, impulse.y, impulse.z));
        }
    }

    void PhysicsWorld3D::ApplyTorque(Entity entity, const glm::vec3& torque)
    {
        if (!m_Initialized || !entity.HasComponent<RigidbodyComponent>())
            return;

        auto& rb = entity.GetComponent<RigidbodyComponent>();
        if (rb.RuntimeBody)
        {
            JPH::BodyID bodyID(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rb.RuntimeBody)));
            m_PhysicsSystem->GetBodyInterface().AddTorque(bodyID, JPH::Vec3(torque.x, torque.y, torque.z));
        }
    }

    void PhysicsWorld3D::ApplyTorqueImpulse(Entity entity, const glm::vec3& torqueImpulse)
    {
        if (!m_Initialized || !entity.HasComponent<RigidbodyComponent>())
            return;

        auto& rb = entity.GetComponent<RigidbodyComponent>();
        if (rb.RuntimeBody)
        {
            JPH::BodyID bodyID(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rb.RuntimeBody)));
            m_PhysicsSystem->GetBodyInterface().AddAngularImpulse(bodyID, JPH::Vec3(torqueImpulse.x, torqueImpulse.y, torqueImpulse.z));
        }
    }

    void PhysicsWorld3D::SetLinearVelocity(Entity entity, const glm::vec3& velocity)
    {
        if (!m_Initialized || !entity.HasComponent<RigidbodyComponent>())
            return;

        auto& rb = entity.GetComponent<RigidbodyComponent>();
        if (rb.RuntimeBody)
        {
            JPH::BodyID bodyID(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rb.RuntimeBody)));
            m_PhysicsSystem->GetBodyInterface().SetLinearVelocity(bodyID, JPH::Vec3(velocity.x, velocity.y, velocity.z));
        }
    }

    glm::vec3 PhysicsWorld3D::GetLinearVelocity(Entity entity) const
    {
        if (!m_Initialized || !entity.HasComponent<RigidbodyComponent>())
            return glm::vec3(0.0f);

        auto& rb = entity.GetComponent<RigidbodyComponent>();
        if (rb.RuntimeBody)
        {
            JPH::BodyID bodyID(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rb.RuntimeBody)));
            JPH::Vec3 vel = m_PhysicsSystem->GetBodyInterface().GetLinearVelocity(bodyID);
            return glm::vec3(vel.GetX(), vel.GetY(), vel.GetZ());
        }
        return glm::vec3(0.0f);
    }

    void PhysicsWorld3D::SetAngularVelocity(Entity entity, const glm::vec3& velocity)
    {
        if (!m_Initialized || !entity.HasComponent<RigidbodyComponent>())
            return;

        auto& rb = entity.GetComponent<RigidbodyComponent>();
        if (rb.RuntimeBody)
        {
            JPH::BodyID bodyID(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rb.RuntimeBody)));
            m_PhysicsSystem->GetBodyInterface().SetAngularVelocity(bodyID, JPH::Vec3(velocity.x, velocity.y, velocity.z));
        }
    }

    glm::vec3 PhysicsWorld3D::GetAngularVelocity(Entity entity) const
    {
        if (!m_Initialized || !entity.HasComponent<RigidbodyComponent>())
            return glm::vec3(0.0f);

        auto& rb = entity.GetComponent<RigidbodyComponent>();
        if (rb.RuntimeBody)
        {
            JPH::BodyID bodyID(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rb.RuntimeBody)));
            JPH::Vec3 vel = m_PhysicsSystem->GetBodyInterface().GetAngularVelocity(bodyID);
            return glm::vec3(vel.GetX(), vel.GetY(), vel.GetZ());
        }
        return glm::vec3(0.0f);
    }

    JPH::BodyInterface* PhysicsWorld3D::GetBodyInterface() const
    {
        if (m_Initialized)
            return &m_PhysicsSystem->GetBodyInterface();
        return nullptr;
    }

    void PhysicsWorld3D::SyncTransformsFromPhysics()
    {
        // Physics'ten Transform'a senkronizasyon
        if (!m_Initialized || !m_Scene)
            return;

        // Tüm RigidbodyComponent'leri tara
        auto view = m_Scene->m_Registry.view<RigidbodyComponent, TransformComponent>();
        for (auto entity : view)
        {
            auto& rb = view.get<RigidbodyComponent>(entity);
            auto& transform = view.get<TransformComponent>(entity);

            if (rb.RuntimeBody && rb.Type == RigidbodyComponent::BodyType::Dynamic)
            {
                JPH::BodyID bodyID(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(rb.RuntimeBody)));
                
                // Pozisyon al
                JPH::RVec3 pos = m_PhysicsSystem->GetBodyInterface().GetPosition(bodyID);
                transform.Position = glm::vec3(pos.GetX(), pos.GetY(), pos.GetZ());
                
                // Rotasyon al
                JPH::Quat rot = m_PhysicsSystem->GetBodyInterface().GetRotation(bodyID);
                JPH::Vec3 euler = rot.GetEulerAngles();
                transform.Rotation = glm::vec3(euler.GetX(), euler.GetY(), euler.GetZ());
            }
        }
    }

    void PhysicsWorld3D::SyncTransformsToPhysics()
    {
        // Transform'dan Physics'e senkronizasyon
        // Scene tarafından çağrılacak
    }
}
