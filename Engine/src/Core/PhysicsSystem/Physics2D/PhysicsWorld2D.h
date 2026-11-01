#pragma once

#include "Core/Base/Base.h"
#include "Core/Time/Timestep.h"

#include <glm/glm.hpp>
#include <box2d/box2d.h>

#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Conqueror
{
    class Entity;
    class Scene;

    struct ContactEvent2D
    {
        enum Kind : int { Enter = 0, Stay, Exit };
        Kind Type = Enter;
        uint32_t EntityA = 0xFFFFFFFFu, EntityB = 0xFFFFFFFFu;
        bool Trigger = false;
        glm::vec2 Point{ 0.0f };
        glm::vec2 Normal{ 0.0f, 1.0f };  // A'dan B'ye
        float Impulse = 0.0f;
    };

    struct RaycastHit2D
    {
        uint32_t Entity = 0xFFFFFFFFu;
        glm::vec2 Point{ 0.0f };
        glm::vec2 Normal{ 0.0f, 1.0f };
        float Distance = 0.0f;
        bool IsTrigger = false;
    };

    // Box2D 3 ile 2D fizik: Unity Physics2D kuralları (Rigidbody2D'siz çarpıştırıcı = statik,
    // çocuk çarpıştırıcılar ata gövdeye bağlanır), sabit adım + ara değer, olaylar, sorgular,
    // eklemler (Distance/Spring/Hinge/Slider/Wheel/Fixed) ve efektörler (Platform/Area/Point/Surface/Buoyancy).
    class CQ_API PhysicsWorld2D
    {
    public:
        static constexpr uint32_t NoEntity = 0xFFFFFFFFu;

        PhysicsWorld2D();
        ~PhysicsWorld2D();

        void Initialize();
        void Shutdown();

        void OnRuntimeStart();
        void OnRuntimeStop();
        bool IsRunning() const { return B2_IS_NON_NULL(m_WorldId); }
        void Step(Timestep ts);
        std::function<void(uint32_t, float)> OnJointBreak;

        void MarkDirty(Entity entity);
        void OnEntityDestroyed(Entity entity);

        // Eski API
        void CreateBody(Entity entity);
        void DestroyBody(Entity entity);

        void SetGravity(const glm::vec2& gravity);
        glm::vec2 GetGravity() const;

        void ApplyForce(Entity entity, const glm::vec2& force, const glm::vec2& point);
        void ApplyForceToCenter(Entity entity, const glm::vec2& force);
        void ApplyImpulse(Entity entity, const glm::vec2& impulse, const glm::vec2& point);
        void ApplyImpulseToCenter(Entity entity, const glm::vec2& impulse);
        void ApplyTorque(Entity entity, float torque);
        void ApplyAngularImpulse(Entity entity, float impulse);

        void SetLinearVelocity(Entity entity, const glm::vec2& velocity);
        glm::vec2 GetLinearVelocity(Entity entity) const;
        void SetAngularVelocity(Entity entity, float velocity);
        float GetAngularVelocity(Entity entity) const;
        float GetMass(Entity entity) const;
        void Teleport(Entity entity, const glm::vec2& position, float angle);

        // Sorgular
        bool Raycast(const glm::vec2& origin, const glm::vec2& direction, float maxDistance, RaycastHit2D& hit,
                     uint32_t layerMask = 0xFFFFFFFFu, uint32_t ignoreEntity = NoEntity);
        void RaycastAll(const glm::vec2& origin, const glm::vec2& direction, float maxDistance, std::vector<RaycastHit2D>& hits,
                        uint32_t layerMask = 0xFFFFFFFFu);
        void OverlapCircle(const glm::vec2& center, float radius, std::vector<uint32_t>& entities, uint32_t layerMask = 0xFFFFFFFFu);
        void OverlapBox(const glm::vec2& center, const glm::vec2& halfExtents, float angle, std::vector<uint32_t>& entities,
                        uint32_t layerMask = 0xFFFFFFFFu);

        std::vector<ContactEvent2D>& GetPendingEvents() { return m_PendingEvents; }
        int GetBodyCount() const { return (int)m_Bodies.size(); }
        b2WorldId GetWorldId() const { return m_WorldId; }

        // Tek yönlü platform çağrısı için (Box2D iş parçacıklarından okunur)
        struct PlatformInfo { float SurfaceArc; float RotationalOffset; };

    private:
        struct BodyRecord
        {
            b2BodyId Body = b2_nullBodyId;
            b2BodyType Type = b2_staticBody;
            bool Interpolate = false;
            glm::vec2 PrevPos{ 0.0f }, CurrPos{ 0.0f };
            float PrevAngle = 0.0f, CurrAngle = 0.0f;
            glm::vec2 LastWrittenPos{ 0.0f };
            float LastWrittenAngle = 0.0f;
            size_t Signature = 0;
            std::vector<uint32_t> ShapeEntities;
        };

        struct JointRecord
        {
            b2JointId Joint = b2_nullJointId;
            int Kind = 0;
            float BreakForce = 0.0f, BreakTorque = 0.0f;
        };

        void ProcessDirty();
        void RebuildOwner(uint32_t owner);
        void RemoveOwner(uint32_t owner);
        void RebuildJoints(uint32_t entity);
        void RemoveJoints(uint32_t entity);
        uint32_t FindOwner(uint32_t entity) const;
        bool HasCollider(uint32_t entity) const;
        void AddShapes(uint32_t owner, uint32_t entity, b2BodyId body, const glm::mat4& ownerInv, std::vector<uint32_t>& shapeEntities, bool isRoot);
        size_t ComputeSignature(uint32_t owner) const;
        size_t ComputeJointSignature(uint32_t entity) const;
        glm::mat4 WorldMatrix(uint32_t entity) const;
        void WorldPose(uint32_t entity, glm::vec2& position, float& angle, glm::vec2& scale) const;
        void SetWorldPose(uint32_t entity, const glm::vec2& position, float angle);
        b2BodyId BodyOf(uint32_t entity) const;
        bool Valid(uint32_t entity) const;
        void SyncExternalChanges();
        void PreStep(float dt);
        void PostStep(float dt);
        void CollectEvents();
        void ApplyEffectors();
        void WriteTransforms(float alpha);

        b2WorldId m_WorldId = b2_nullWorldId;
        glm::vec2 m_Gravity{ 0.0f, -9.81f };
        Scene* m_Scene = nullptr;
        bool m_Initialized = false;

        std::unordered_map<uint32_t, BodyRecord> m_Bodies;
        std::unordered_map<uint32_t, uint32_t> m_ShapeOwner;
        std::unordered_map<uint32_t, std::vector<JointRecord>> m_Joints;
        std::unordered_map<uint32_t, size_t> m_JointSignatures;
        std::unordered_set<uint32_t> m_Dirty;
        std::vector<ContactEvent2D> m_PendingEvents;

        // Aktif temas çiftleri (Stay üretimi) ve efektör örtüşmeleri
        struct PairInfo { uint32_t A, B; bool Trigger; };
        std::unordered_map<uint64_t, PairInfo> m_ActivePairs;
        std::unordered_map<uint32_t, std::set<uint32_t>> m_SensorVisitors; // tetikleyici varlık -> içindeki gövde varlıkları
        std::unordered_map<uint64_t, int> m_SensorPairCounts;              // (sensör şekil varlığı, ziyaretçi) şekil sayısı

        // Tek yönlü platform şekilleri: şekil kimliği -> ayarlar (ön çözüm çağrısı salt okur)
        std::unordered_map<uint64_t, PlatformInfo> m_Platforms;

        float m_Accumulator = 0.0f;
        float m_FixedDt = 1.0f / 60.0f;
        int m_SignatureCursor = 0;

        friend class Scene;
    };
}
