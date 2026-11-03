#pragma once

#include "Core/Base/Base.h"
#include "Core/Time/Timestep.h"
#include "Core/PhysicsSystem/Physics3D/PhysicsBackend3D.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Conqueror
{
    class Entity;
    class Scene;

    // Sahne ile fizik arka ucu (Jolt / MuJoCo) arasındaki cephe.
    // - Oyun başlarken sahnedeki tüm gövde/eklem/karakterleri kurar, değişiklikleri izler
    // - Sabit zaman adımıyla ilerler, görsel dönüşümleri ara değerler (interpolation)
    // - Unity kuralları: Rigidbody'siz çarpıştırıcı = statik; çocuk çarpıştırıcılar ata Rigidbody'ye eklenir
    class CQ_API PhysicsWorld3D
    {
    public:
        PhysicsWorld3D();
        ~PhysicsWorld3D();

        void Initialize();
        void Shutdown();
        static void InitGlobals() {}
        static void ShutdownGlobals() {}

        // ── Yaşam döngüsü ──
        void OnRuntimeStart();
        void OnRuntimeStop();
        bool IsRunning() const { return (bool)m_Backend; }
        void Step(Timestep ts);
        // Her sabit adımdan önce (betiklerin FixedUpdate'i)
        std::function<void(float)> OnFixedUpdate;
        // Eklem kırıldığında (varlık, kuvvet)
        std::function<void(uint32_t, float)> OnJointBreak;

        // ── Sahne bildirimleri ──
        void MarkDirty(Entity entity);
        void OnEntityDestroyed(Entity entity);

        // Eski API (betik köprüsü / Physics yardımcıları)
        void CreateBody(Entity entity);
        void DestroyBody(Entity entity);
        void CreateTerrainBody(Entity entity);
        void DestroyTerrainBody(Entity entity);

        void SetGravity(const glm::vec3& gravity);
        glm::vec3 GetGravity() const;

        void ApplyForce(Entity entity, const glm::vec3& force);
        void ApplyImpulse(Entity entity, const glm::vec3& impulse);
        void ApplyTorque(Entity entity, const glm::vec3& torque);
        void ApplyTorqueImpulse(Entity entity, const glm::vec3& torqueImpulse);
        void AddForceAtPosition(Entity entity, const glm::vec3& force, const glm::vec3& worldPoint);
        void AddImpulseAtPosition(Entity entity, const glm::vec3& impulse, const glm::vec3& worldPoint);
        void AddExplosionForce(float force, const glm::vec3& center, float radius, float upwardsModifier);

        void SetLinearVelocity(Entity entity, const glm::vec3& velocity);
        glm::vec3 GetLinearVelocity(Entity entity) const;
        void SetAngularVelocity(Entity entity, const glm::vec3& velocity);
        glm::vec3 GetAngularVelocity(Entity entity) const;
        float GetMass(Entity entity) const;
        bool IsSleeping(Entity entity) const;
        void WakeUp(Entity entity);
        // Gövdeyi (ve dönüşümü) anında taşı
        void Teleport(Entity entity, const glm::vec3& position, const glm::quat& rotation);

        // ── Karakter denetleyicisi ──
        bool HasCharacter(Entity entity) const;
        Physics3D::CharacterState MoveCharacter(Entity entity, const glm::vec3& displacement);
        // Kare başı (betiklerden önce): bu karenin süresi ve karakter hareket istekleri sıfırlanır
        void BeginFrame(Timestep ts);
        // Unity SimpleMove: yatay hız + otomatik yerçekimi
        Physics3D::CharacterState SimpleMoveCharacter(Entity entity, const glm::vec3& velocity);
        Physics3D::CharacterState GetCharacterState(Entity entity) const;

        // ── Eklemler ──
        void SetJointMotor(Entity entity, bool enabled, float targetVelocity, float maxForce);

        // ── Sorgular ──
        bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, Physics3D::RaycastHit& hit,
                     uint32_t layerMask = 0xFFFFFFFFu, uint32_t ignoreEntity = Physics3D::NoEntity);
        void RaycastAll(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, std::vector<Physics3D::RaycastHit>& hits,
                        uint32_t layerMask = 0xFFFFFFFFu);
        bool SphereCast(const glm::vec3& origin, float radius, const glm::vec3& direction, float maxDistance, Physics3D::RaycastHit& hit,
                        uint32_t layerMask = 0xFFFFFFFFu, uint32_t ignoreEntity = Physics3D::NoEntity);
        void OverlapSphere(const glm::vec3& center, float radius, std::vector<uint32_t>& entities, uint32_t layerMask = 0xFFFFFFFFu);
        void OverlapBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation, std::vector<uint32_t>& entities,
                        uint32_t layerMask = 0xFFFFFFFFu);

        // ── Olaylar (Scene betiklere dağıtır) ──
        std::vector<Physics3D::ContactEvent>& GetPendingEvents() { return m_PendingEvents; }

        // ── Bilgi ──
        const char* GetBackendName() const { return m_Backend ? m_Backend->GetName() : "None"; }
        const std::string& GetBackendError() const { return m_BackendError; }
        int GetBodyCount() const { return m_Backend ? m_Backend->GetBodyCount() : 0; }
        int GetActiveBodyCount() const { return m_Backend ? m_Backend->GetActiveBodyCount() : 0; }
        int GetLastStepCount() const { return m_LastStepCount; }
        float GetLastStepMs() const { return m_LastStepMs; }
        int GetFixedStepCounter() const { return m_FixedStepCounter; }

        // Eski: doğrudan arka uç erişimi (testler / gelişmiş kullanım)
        Physics3D::Backend* GetBackend() const { return m_Backend.get(); }

    private:
        struct BodyRecord
        {
            Physics3D::Handle Body = 0;
            Physics3D::Handle Sensor = 0;     // karma (katı + tetikleyici) gövdelerde tetikleyici şekiller
            Physics3D::MotionType Motion = Physics3D::MotionType::Static;
            bool Interpolate = false;
            glm::vec3 PrevPos{ 0.0f }, CurrPos{ 0.0f };
            glm::quat PrevRot{ 1, 0, 0, 0 }, CurrRot{ 1, 0, 0, 0 };
            glm::vec3 LastWrittenPos{ 0.0f };
            glm::quat LastWrittenRot{ 1, 0, 0, 0 };
            size_t Signature = 0;
            std::vector<uint32_t> ShapeEntities;
        };

        struct JointRecord
        {
            Physics3D::Handle Joint = 0;
            Physics3D::JointType Type = Physics3D::JointType::Fixed;
            float BreakForce = 0.0f, BreakTorque = 0.0f;
            size_t Signature = 0;
            // Son uygulanan motor durumu (bileşen birimleri: menteşe derece/s, kaydırıcı m/s)
            bool MotorOn = false;
            float MotorVelocity = 0.0f, MotorForce = 0.0f;
            void SetMotorState(bool on, float velocity, float force) { MotorOn = on; MotorVelocity = velocity; MotorForce = force; }
        };

        struct CharacterRecord
        {
            Physics3D::Handle Character = 0;
            glm::vec3 LastWrittenPos{ 0.0f };
            float VerticalVelocity = 0.0f;
            size_t Signature = 0;
            // Karakter fizik adımlarında hareket eder (Jolt önerisi): betiğin bu karede istediği hız, adımlar içinde uygulanır;
            // ekrandaki konum son iki adım arasında ara değerlenir. Karakter kutularla aynı adımda ilerler, aradaki karelerde
            // henüz kıpırdamamış kutuya takılıp kamerayı sarsmaz
            glm::vec3 RequestedVelocity{ 0.0f };
            bool Requested = false;
            uint64_t RequestFrame = 0;   // isteğin yapıldığı kare (betikler fizikten sonra çalışır: sonraki karenin adımları uygular)
            glm::vec3 PrevPos{ 0.0f }, CurrPos{ 0.0f };
            bool HasPose = false;
        };
        void SyncCharacterTeleport(uint32_t entity, CharacterRecord& rec);
        void MoveCharactersFixed(float dt);
        void WriteCharacterTransforms(float alpha);

        void CreateBackend();
        void ProcessDirty();
        void RebuildOwner(uint32_t owner, bool preserveVelocity);
        void RemoveOwner(uint32_t owner);
        void RebuildJoints(uint32_t entity);
        void RemoveJoints(uint32_t entity);
        void RebuildCharacter(uint32_t entity);
        void RemoveCharacter(uint32_t entity);
        uint32_t FindOwner(uint32_t entity) const;
        bool HasPhysicsShape(uint32_t entity) const;
        bool BuildBodyDesc(uint32_t owner, Physics3D::BodyDesc& desc, std::vector<uint32_t>& shapeEntities) const;
        void GatherShapes(uint32_t owner, uint32_t entity, const glm::mat4& ownerInv, std::vector<Physics3D::ShapeDesc>& shapes,
                          std::vector<uint32_t>& shapeEntities, bool isRoot) const;
        size_t ComputeSignature(uint32_t owner) const;
        size_t ComputeJointSignature(uint32_t entity) const;
        size_t ComputeCharacterSignature(uint32_t entity) const;

        void PreStep(float dt);
        void PostStep(float dt);
        // Wheel Collider: süspansiyon + lastik kuvvetleri (her sabit adım) ve görsel tekerlek pozu (her kare)
        void UpdateWheels(float dt);
        void UpdateWheelVisuals(float dt);
        uint32_t WheelOwner(uint32_t entity) const;
        void WriteTransforms(float alpha);
        void SyncExternalChanges(float dt);
        // Cloth: yumuşak gövdeler (oyun başında kurulur; sabitlenmiş köşeler varlığı izler; mesh her kare)
        void CreateCloths();
        void DriveCloths(float dt);
        void UpdateClothMeshes();

        glm::mat4 WorldMatrix(uint32_t entity) const;
        void DecomposeWorld(uint32_t entity, glm::vec3& position, glm::quat& rotation, glm::vec3& scale) const;
        void SetWorldPose(uint32_t entity, const glm::vec3& position, const glm::quat& rotation);
        Physics3D::Handle BodyOf(uint32_t entity) const;
        bool Valid(uint32_t entity) const;

        Scene* m_Scene = nullptr;
        std::unique_ptr<Physics3D::Backend> m_Backend;
        std::string m_BackendError;
        glm::vec3 m_Gravity{ 0.0f, -9.81f, 0.0f };
        bool m_Initialized = false;

        std::unordered_map<uint32_t, BodyRecord> m_Bodies;           // sahip varlık -> gövde
        std::unordered_map<uint32_t, uint32_t> m_ShapeOwner;         // çarpıştırıcı varlığı -> sahip
        std::unordered_map<uint32_t, std::vector<std::pair<int, JointRecord>>> m_Joints; // varlık -> (bileşen türü, eklem)
        std::unordered_map<uint32_t, size_t> m_JointSignatures;     // eklem bileşeni olan varlıklar -> imza
        // Eklemin ilk kurulumdaki dinlenme çerçevesi (bağlı gövdenin yerel uzayında; bağlı gövde yoksa dünya). Yeniden
        // kurulumlarda geri kullanılır: eklem o anki pozdan değil ilk pozdan kurulur (zincir açılmaz, limit kaymaz).
        struct JointRest
        {
            size_t Geometry = 0;             // çapa / eksen / bağlı gövde değişince çerçeve yeniden alınır
            bool Connected = false;
            glm::vec3 Point{ 0.0f }, Axis{ 1, 0, 0 }, Normal{ 0, 1, 0 }, Secondary{ 0, 0, 1 };
            float RestLength = -1.0f;
        };
        std::unordered_map<uint64_t, JointRest> m_JointRest;       // (varlık << 3 | eklem türü) -> çerçeve
        std::unordered_map<uint32_t, CharacterRecord> m_Characters;
        struct ClothRecord
        {
            Physics3D::Handle Body = 0;
            std::vector<glm::vec3> RestLocal;
            std::vector<uint32_t> Triangles;
            std::vector<uint32_t> Pinned;
            std::vector<glm::vec3> PinnedWorld;
            std::vector<glm::vec3> World;
            std::vector<glm::vec3> Local;
            float WindTime = 0.0f;
        };
        std::unordered_map<uint32_t, ClothRecord> m_Cloths;
        std::unordered_set<uint32_t> m_Dirty;
        std::vector<Physics3D::ContactEvent> m_PendingEvents;

        float m_Accumulator = 0.0f;
        float m_FixedDt = 1.0f / 60.0f;
        float m_FrameDt = 1.0f / 60.0f;
        float m_ScriptDt = 1.0f / 60.0f;   // bu karenin süresi (betikler yer değiştirmeyi bununla hesaplar)
        uint64_t m_FrameIndex = 0;
        int m_LastStepCount = 0;
        float m_LastStepMs = 0.0f;
        int m_FixedStepCounter = 0;
        int m_SignatureCursor = 0;

        friend class Scene;
    };
}
