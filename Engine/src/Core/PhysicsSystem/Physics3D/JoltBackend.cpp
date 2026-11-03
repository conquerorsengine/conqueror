#include "PhysicsBackend3D.h"
#include "Core/Logging/Log.h"

#include <unordered_set>

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/SoftBody/SoftBodyCreationSettings.h>
#include <Jolt/Physics/SoftBody/SoftBodySharedSettings.h>
#include <Jolt/Physics/SoftBody/SoftBodyMotionProperties.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/SixDOFConstraint.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Body/BodyFilter.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>

namespace Conqueror::Physics3D
{
    namespace
    {
        // ── Küresel Jolt durumu (tüm dünyalar paylaşır) ──
        JPH::TempAllocator* s_TempAllocator = nullptr;
        JPH::JobSystem* s_JobSystem = nullptr;
        int s_GlobalRefs = 0;
        std::mutex s_GlobalMutex;

        void AcquireGlobals()
        {
            std::lock_guard lock(s_GlobalMutex);
            if (s_GlobalRefs++ > 0)
                return;
            if (!JPH::Factory::sInstance)
            {
                JPH::RegisterDefaultAllocator();
                JPH::Factory::sInstance = new JPH::Factory();
                JPH::RegisterTypes();
            }
            s_TempAllocator = new JPH::TempAllocatorImpl(32 * 1024 * 1024);
            unsigned threads = std::max(1u, std::thread::hardware_concurrency()) - 1;
            s_JobSystem = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, (int)std::max(1u, threads));
        }

        void ReleaseGlobals()
        {
            std::lock_guard lock(s_GlobalMutex);
            if (--s_GlobalRefs > 0)
                return;
            delete s_JobSystem;
            s_JobSystem = nullptr;
            delete s_TempAllocator;
            s_TempAllocator = nullptr;
            // Factory/türler süreç boyunca kalır (yeniden kayıt maliyetli ve güvenli değil)
        }

        inline JPH::Vec3 ToJ(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
        inline JPH::RVec3 ToJR(const glm::vec3& v) { return JPH::RVec3(v.x, v.y, v.z); }
        inline JPH::Quat ToJ(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w).Normalized(); }
        inline glm::vec3 ToG(JPH::Vec3Arg v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }
#ifdef JPH_DOUBLE_PRECISION
        inline glm::vec3 ToG(JPH::RVec3Arg v) { return glm::vec3((float)v.GetX(), (float)v.GetY(), (float)v.GetZ()); }
#endif
        inline glm::quat ToG(JPH::QuatArg q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }

        // Nesne katmanı = (kullanıcı katmanı << 1) | hareketli
        inline JPH::ObjectLayer MakeLayer(int layer, bool moving)
        {
            return (JPH::ObjectLayer)(((std::clamp(layer, 0, 31)) << 1) | (moving ? 1 : 0));
        }
        inline int UserLayer(JPH::ObjectLayer l) { return (int)(l >> 1); }
        inline bool IsMovingLayer(JPH::ObjectLayer l) { return (l & 1) != 0; }

        namespace BP
        {
            constexpr JPH::BroadPhaseLayer NonMoving(0);
            constexpr JPH::BroadPhaseLayer Moving(1);
        }

        class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface
        {
        public:
            JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
            JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override { return IsMovingLayer(layer) ? BP::Moving : BP::NonMoving; }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override { return layer == BP::Moving ? "MOVING" : "NON_MOVING"; }
#endif
        };

        class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter
        {
        public:
            bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bp) const override
            {
                return IsMovingLayer(layer) ? true : bp == BP::Moving;
            }
        };

        class LayerPairs final : public JPH::ObjectLayerPairFilter
        {
        public:
            const PhysicsSettings* Settings = nullptr;
            bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
            {
                if (!IsMovingLayer(a) && !IsMovingLayer(b))
                    return false;
                return !Settings || Settings->LayersCollide(UserLayer(a), UserLayer(b));
            }
        };

        // Sorgularda katman maskesi
        class MaskLayerFilter final : public JPH::ObjectLayerFilter
        {
        public:
            uint32_t Mask = 0xFFFFFFFFu;
            bool ShouldCollide(JPH::ObjectLayer layer) const override { return (Mask >> UserLayer(layer)) & 1u; }
        };

        // Sorgularda tetikleyici / yok sayılan varlık filtresi
        class QueryBodyFilter final : public JPH::BodyFilter
        {
        public:
            bool HitTriggers = true;
            uint32_t IgnoreEntity = NoEntity;
            uint32_t IgnoreBodyId = 0xFFFFFFFFu;
            bool ShouldCollideLocked(const JPH::Body& body) const override
            {
                if (!HitTriggers && body.IsSensor())
                    return false;
                if (IgnoreEntity != NoEntity && (uint32_t)body.GetUserData() == IgnoreEntity)
                    return false;
                if (body.GetID().GetIndexAndSequenceNumber() == IgnoreBodyId)
                    return false;
                return true;
            }
        };

        uint64_t PairKey(JPH::BodyID a, JPH::BodyID b)
        {
            uint32_t x = a.GetIndexAndSequenceNumber(), y = b.GetIndexAndSequenceNumber();
            if (x > y) std::swap(x, y);
            return ((uint64_t)x << 32) | y;
        }

        // Temas dinleyicisi: iş parçacıklarından çağrılır, olaylar kilitle biriktirilir
        class ContactCollector final : public JPH::ContactListener
        {
        public:
            struct PairState
            {
                int Count = 0;
                uint32_t EntityA = NoEntity, EntityB = NoEntity;
                bool Trigger = false;
            };

            // Eklemle bağlı ve çarpışması kapatılmış gövde çiftleri (adım sırasında yalnızca okunur)
            const std::set<uint64_t>* NoCollide = nullptr;

            JPH::ValidateResult OnContactValidate(const JPH::Body& b1, const JPH::Body& b2, JPH::RVec3Arg, const JPH::CollideShapeResult&) override
            {
                if (NoCollide && !NoCollide->empty() && NoCollide->count(PairKey(b1.GetID(), b2.GetID())))
                    return JPH::ValidateResult::RejectAllContactsForThisBodyPair;
                return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
            }

            void OnContactAdded(const JPH::Body& b1, const JPH::Body& b2, const JPH::ContactManifold& manifold, JPH::ContactSettings&) override
            {
                std::lock_guard lock(Mutex);
                uint64_t key = PairKey(b1.GetID(), b2.GetID());
                PairState& st = Pairs[key];
                if (st.Count++ == 0)
                {
                    st.EntityA = (uint32_t)b1.GetUserData();
                    st.EntityB = (uint32_t)b2.GetUserData();
                    st.Trigger = b1.IsSensor() || b2.IsSensor();
                    ContactEvent ev;
                    ev.Type = ContactEvent::Enter;
                    ev.EntityA = st.EntityA;
                    ev.EntityB = st.EntityB;
                    ev.Trigger = st.Trigger;
                    Fill(ev, b1, b2, manifold);
                    Events.push_back(ev);
                }
            }

            void OnContactPersisted(const JPH::Body& b1, const JPH::Body& b2, const JPH::ContactManifold& manifold, JPH::ContactSettings&) override
            {
                std::lock_guard lock(Mutex);
                uint64_t key = PairKey(b1.GetID(), b2.GetID());
                if (!StayedThisStep.insert(key).second)
                    return;
                ContactEvent ev;
                ev.Type = ContactEvent::Stay;
                ev.EntityA = (uint32_t)b1.GetUserData();
                ev.EntityB = (uint32_t)b2.GetUserData();
                ev.Trigger = b1.IsSensor() || b2.IsSensor();
                Fill(ev, b1, b2, manifold);
                Events.push_back(ev);
            }

            void OnContactRemoved(const JPH::SubShapeIDPair& pair) override
            {
                std::lock_guard lock(Mutex);
                uint64_t key = PairKey(pair.GetBody1ID(), pair.GetBody2ID());
                auto it = Pairs.find(key);
                if (it == Pairs.end())
                    return;
                if (--it->second.Count <= 0)
                {
                    ContactEvent ev;
                    ev.Type = ContactEvent::Exit;
                    ev.EntityA = it->second.EntityA;
                    ev.EntityB = it->second.EntityB;
                    ev.Trigger = it->second.Trigger;
                    Events.push_back(ev);
                    Pairs.erase(it);
                }
            }

            // Gövde silinince ilgili çiftler için Exit üret (Jolt silinen gövdeler için Removed çağırmaz)
            void ForgetBody(JPH::BodyID id)
            {
                std::lock_guard lock(Mutex);
                uint32_t idx = id.GetIndexAndSequenceNumber();
                for (auto it = Pairs.begin(); it != Pairs.end();)
                {
                    uint32_t x = (uint32_t)(it->first >> 32), y = (uint32_t)it->first;
                    if (x == idx || y == idx)
                    {
                        ContactEvent ev;
                        ev.Type = ContactEvent::Exit;
                        ev.EntityA = it->second.EntityA;
                        ev.EntityB = it->second.EntityB;
                        ev.Trigger = it->second.Trigger;
                        Events.push_back(ev);
                        it = Pairs.erase(it);
                    }
                    else
                        ++it;
                }
            }

            static void Fill(ContactEvent& ev, const JPH::Body& b1, const JPH::Body& b2, const JPH::ContactManifold& m)
            {
                if (m.mRelativeContactPointsOn1.size() > 0)
                    ev.Point = ToG(JPH::Vec3(m.GetWorldSpaceContactPointOn1(0)));
                ev.Normal = ToG(m.mWorldSpaceNormal);
                ev.RelativeVelocity = ToG(b2.GetLinearVelocity() - b1.GetLinearVelocity());
                // Yaklaşma hızından tahmini darbe (gerçek darbe çözücüden sonra bilinir)
                float approach = std::fabs(glm::dot(ev.RelativeVelocity, ev.Normal));
                float m1 = b1.IsDynamic() ? 1.0f / std::max(1e-6f, b1.GetMotionProperties()->GetInverseMass()) : 0.0f;
                float m2 = b2.IsDynamic() ? 1.0f / std::max(1e-6f, b2.GetMotionProperties()->GetInverseMass()) : 0.0f;
                float mass = (m1 > 0 && m2 > 0) ? (m1 * m2) / (m1 + m2) : std::max(m1, m2);
                ev.Impulse = approach * mass;
            }

            std::mutex Mutex;
            std::vector<ContactEvent> Events;
            std::unordered_map<uint64_t, PairState> Pairs;
            std::set<uint64_t> StayedThisStep;
        };

        // Karakterin temasları (itme / tetikleyiciler) — CharacterVirtual iç gövdesiyle tetikleyicilere girer
        class CharacterListener final : public JPH::CharacterContactListener
        {
        public:
            bool PushBodies = true;
            void OnContactAdded(const JPH::CharacterVirtual*, const JPH::CharacterContact& contact, JPH::CharacterContactSettings& settings) override
            {
                // Hareketli platformlar (kinematik) karakteri taşır / iter; dinamik nesneler yalnız itilir (Unreal karakteri
                // gibi): kutu karakteri geri itince her fizik adımında bir duraklayıp atlayan titreme oluşuyordu
                settings.mCanPushCharacter = contact.mMotionTypeB == JPH::EMotionType::Kinematic;
                settings.mCanReceiveImpulses = PushBodies;
            }
        };

        struct JointRecord
        {
            JPH::Ref<JPH::Constraint> Constraint;
            JointType Type = JointType::Fixed;
        };

        struct CharacterRecord
        {
            JPH::Ref<JPH::CharacterVirtual> Character;
            std::unique_ptr<CharacterListener> Listener;
            glm::vec3 Center{ 0.0f };
            float StepOffset = 0.3f;
            int Layer = 0;
            uint32_t Entity = NoEntity;
            CharacterState LastState;
        };

        JPH::ShapeRefC MakeLeafShape(const ShapeDesc& s, bool dynamic, std::string& warning)
        {
            JPH::ShapeSettings::ShapeResult result;
            switch (s.Type)
            {
                case ShapeType::Box:
                {
                    glm::vec3 he = glm::max(glm::abs(s.HalfExtents), glm::vec3(0.001f));
                    float convex = std::min(JPH::cDefaultConvexRadius, 0.5f * std::min(he.x, std::min(he.y, he.z)));
                    result = JPH::BoxShapeSettings(ToJ(he), convex).Create();
                    break;
                }
                case ShapeType::Sphere:
                    result = JPH::SphereShapeSettings(std::max(0.001f, s.Radius)).Create();
                    break;
                case ShapeType::Capsule:
                    if (s.HalfHeight <= 1e-4f)
                        result = JPH::SphereShapeSettings(std::max(0.001f, s.Radius)).Create();
                    else
                        result = JPH::CapsuleShapeSettings(s.HalfHeight, std::max(0.001f, s.Radius)).Create();
                    break;
                case ShapeType::Cylinder:
                {
                    float hh = std::max(0.001f, s.HalfHeight), r = std::max(0.001f, s.Radius);
                    result = JPH::CylinderShapeSettings(hh, r, std::min(JPH::cDefaultConvexRadius, 0.5f * std::min(hh, r))).Create();
                    break;
                }
                case ShapeType::ConvexHull:
                {
                    JPH::Array<JPH::Vec3> points;
                    points.reserve(s.Vertices.size());
                    for (auto& v : s.Vertices) points.push_back(ToJ(v));
                    result = JPH::ConvexHullShapeSettings(points).Create();
                    break;
                }
                case ShapeType::TriangleMesh:
                {
                    if (dynamic)
                    {
                        // Jolt üçgen ağları yalnızca statik/kinematik gövdelerde çalışır (Unity: dışbükey olmayan MeshCollider kuralı)
                        warning = "Non-convex Mesh Collider on a dynamic Rigidbody; using its convex hull. Enable 'Convex' or make the body kinematic.";
                        JPH::Array<JPH::Vec3> points;
                        for (auto& v : s.Vertices) points.push_back(ToJ(v));
                        result = JPH::ConvexHullShapeSettings(points).Create();
                        break;
                    }
                    JPH::VertexList verts;
                    verts.reserve(s.Vertices.size());
                    for (auto& v : s.Vertices) verts.push_back(JPH::Float3(v.x, v.y, v.z));
                    JPH::IndexedTriangleList tris;
                    tris.reserve(s.Indices.size() / 3);
                    for (size_t i = 0; i + 2 < s.Indices.size(); i += 3)
                        tris.push_back(JPH::IndexedTriangle(s.Indices[i], s.Indices[i + 1], s.Indices[i + 2], 0));
                    result = JPH::MeshShapeSettings(std::move(verts), std::move(tris)).Create();
                    break;
                }
                case ShapeType::HeightField:
                {
                    // Jolt'ta üçgen ağına çevrilir (render ağıyla birebir)
                    const int rows = s.HFRows, cols = s.HFCols;
                    if (rows < 2 || cols < 2 || (int)s.HFHeights.size() < rows * cols)
                    {
                        warning = "Invalid height field";
                        return nullptr;
                    }
                    const float sx = s.HFSize.x / (cols - 1), sz = s.HFSize.z / (rows - 1);
                    JPH::VertexList verts;
                    verts.reserve((size_t)rows * cols);
                    for (int r = 0; r < rows; ++r)
                        for (int c = 0; c < cols; ++c)
                            verts.push_back(JPH::Float3(c * sx, s.HFHeights[(size_t)r * cols + c] * s.HFSize.y, r * sz));
                    JPH::IndexedTriangleList tris;
                    tris.reserve((size_t)(rows - 1) * (cols - 1) * 2);
                    for (int r = 0; r < rows - 1; ++r)
                        for (int c = 0; c < cols - 1; ++c)
                        {
                            uint32_t i = (uint32_t)(r * cols + c);
                            tris.push_back(JPH::IndexedTriangle(i, i + (uint32_t)cols, i + 1, 0));
                            tris.push_back(JPH::IndexedTriangle(i + 1, i + (uint32_t)cols, i + (uint32_t)cols + 1, 0));
                        }
                    result = JPH::MeshShapeSettings(std::move(verts), std::move(tris)).Create();
                    break;
                }
            }
            if (result.HasError())
            {
                warning = std::string("Shape creation failed: ") + result.GetError().c_str();
                return nullptr;
            }
            JPH::ShapeRefC shape = result.Get();
            const_cast<JPH::Shape*>(shape.GetPtr())->SetUserData(s.SourceEntity);
            return shape;
        }
    }

    class JoltBackend final : public Backend
    {
    public:
        ~JoltBackend() override { Shutdown(); }

        const char* GetName() const override { return "Jolt"; }

        bool Init(const PhysicsSettings& settings, std::string* error) override
        {
            (void)error;
            AcquireGlobals();
            m_Settings = settings;
            m_LayerPairs.Settings = &m_Settings;
            m_System = std::make_unique<JPH::PhysicsSystem>();
            m_System->Init(65536, 0, 65536, 20480, m_BroadPhase, m_ObjVsBP, m_LayerPairs);
            JPH::PhysicsSettings ps;
            ps.mNumVelocitySteps = (JPH::uint)settings.VelocityIterations;
            ps.mNumPositionSteps = (JPH::uint)settings.PositionIterations;
            ps.mPointVelocitySleepThreshold = settings.SleepThreshold;
            m_System->SetPhysicsSettings(ps);
            m_System->SetGravity(ToJ(settings.Gravity));
            m_Contacts.NoCollide = &m_NoCollidePairs;
            m_System->SetContactListener(&m_Contacts);
            m_Initialized = true;
            return true;
        }

        void Shutdown() override
        {
            if (!m_Initialized)
                return;
            for (auto& [h, c] : m_Characters)
                m_CharVsChar.Remove(c.Character.GetPtr());
            m_Characters.clear();
            for (auto& [h, j] : m_Joints)
                m_System->RemoveConstraint(j.Constraint);
            m_Joints.clear();
            JPH::BodyInterface& bi = m_System->GetBodyInterface();
            for (Handle h : m_SoftBodies)
            {
                bi.RemoveBody(ToId(h));
                bi.DestroyBody(ToId(h));
            }
            m_SoftBodies.clear();
            for (auto& [h, info] : m_Bodies)
            {
                JPH::BodyID id = ToId(h);
                bi.RemoveBody(id);
                bi.DestroyBody(id);
            }
            m_Bodies.clear();
            m_System->SetContactListener(nullptr);
            m_System.reset();
            m_Initialized = false;
            ReleaseGlobals();
        }

        void SetGravity(const glm::vec3& gravity) override
        {
            m_Settings.Gravity = gravity;
            if (m_System) m_System->SetGravity(ToJ(gravity));
        }

        void Step(float dt) override
        {
            if (!m_Initialized || dt <= 0.0f)
                return;
            {
                std::lock_guard lock(m_Contacts.Mutex);
                m_Contacts.StayedThisStep.clear();
            }
            m_LastDt = dt;
            m_System->Update(dt, std::max(1, m_Settings.CollisionSteps), s_TempAllocator, s_JobSystem);
        }

        // ── Gövdeler ──
        Handle CreateBody(const BodyDesc& desc) override
        {
            if (!m_Initialized || desc.Shapes.empty())
                return 0;
            const bool dynamic = desc.Motion == MotionType::Dynamic;
            std::string warning;
            JPH::ShapeRefC shape;
            bool allTriggers = true;
            for (auto& s : desc.Shapes) allTriggers &= s.IsTrigger;
            if (desc.Shapes.size() == 1 && desc.Shapes[0].Offset == glm::vec3(0.0f) && desc.Shapes[0].Rotation == glm::quat(1, 0, 0, 0))
                shape = MakeLeafShape(desc.Shapes[0], dynamic, warning);
            else
            {
                JPH::StaticCompoundShapeSettings compound;
                for (auto& s : desc.Shapes)
                {
                    JPH::ShapeRefC leaf = MakeLeafShape(s, dynamic, warning);
                    if (leaf)
                        compound.AddShape(ToJ(s.Offset), ToJ(s.Rotation), leaf, s.SourceEntity);
                }
                auto result = compound.Create();
                if (result.HasError())
                    warning = std::string("Compound shape failed: ") + result.GetError().c_str();
                else
                    shape = result.Get();
            }
            if (!warning.empty())
                CQ_CORE_WARN("[Physics] {0}", warning);
            if (!shape)
                return 0;

            JPH::EMotionType motion = desc.Motion == MotionType::Static ? JPH::EMotionType::Static
                                    : desc.Motion == MotionType::Kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic;
            // Tetikleyiciler: kinematik algılayıcı (statik/kinematik/dinamik her şeyi algılar)
            if (allTriggers && motion == JPH::EMotionType::Static)
                motion = JPH::EMotionType::Kinematic;
            const bool moving = motion != JPH::EMotionType::Static;
            JPH::BodyCreationSettings bs(shape, ToJR(desc.Position), ToJ(desc.Rotation), motion, MakeLayer(desc.Layer, moving));
            bs.mUserData = desc.Entity;
            bs.mFriction = desc.Friction;
            bs.mRestitution = desc.Restitution;
            bs.mLinearDamping = desc.LinearDamping;
            bs.mAngularDamping = desc.AngularDamping;
            bs.mGravityFactor = desc.GravityFactor;
            bs.mMaxAngularVelocity = std::max(0.1f, desc.MaxAngularVelocity);
            bs.mAllowSleeping = desc.AllowSleep && !allTriggers;
            bs.mIsSensor = allTriggers;
            bs.mCollideKinematicVsNonDynamic = allTriggers;
            bs.mMotionQuality = desc.Continuous ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
            bs.mLinearVelocity = ToJ(desc.LinearVelocity);
            bs.mAngularVelocity = ToJ(desc.AngularVelocity);
            if (dynamic)
            {
                bs.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                bs.mMassPropertiesOverride.mMass = std::max(0.0001f, desc.Mass);
                JPH::EAllowedDOFs dofs = JPH::EAllowedDOFs::All;
                if (desc.Locks & LockPosX) dofs &= ~JPH::EAllowedDOFs::TranslationX;
                if (desc.Locks & LockPosY) dofs &= ~JPH::EAllowedDOFs::TranslationY;
                if (desc.Locks & LockPosZ) dofs &= ~JPH::EAllowedDOFs::TranslationZ;
                if (desc.Locks & LockRotX) dofs &= ~JPH::EAllowedDOFs::RotationX;
                if (desc.Locks & LockRotY) dofs &= ~JPH::EAllowedDOFs::RotationY;
                if (desc.Locks & LockRotZ) dofs &= ~JPH::EAllowedDOFs::RotationZ;
                if (dofs == JPH::EAllowedDOFs::None)
                    dofs = JPH::EAllowedDOFs::TranslationY; // en az bir serbestlik gerekir; tamamen kilitli = kinematik gibi davranır
                bs.mAllowedDOFs = dofs;
            }
            JPH::BodyInterface& bi = m_System->GetBodyInterface();
            JPH::Body* body = bi.CreateBody(bs);
            if (!body)
            {
                CQ_CORE_ERROR("[Physics] Body limit reached; cannot create body for entity {0}", desc.Entity);
                return 0;
            }
            bi.AddBody(body->GetID(), moving ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
            Handle h = ToHandle(body->GetID());
            m_Bodies[h] = { desc.Entity, allTriggers };
            return h;
        }

        Handle CreateSoftBody(const SoftBodyDesc& desc) override
        {
            if (desc.Vertices.size() < 3 || desc.Triangles.size() < 3)
                return 0;
            JPH::Ref<JPH::SoftBodySharedSettings> shared = new JPH::SoftBodySharedSettings();
            for (size_t i = 0; i < desc.Vertices.size(); ++i)
            {
                const glm::vec3& v = desc.Vertices[i];
                const float inv = i < desc.InvMass.size() ? desc.InvMass[i] : 1.0f;
                shared->mVertices.push_back(JPH::SoftBodySharedSettings::Vertex(JPH::Float3(v.x, v.y, v.z), JPH::Float3(0, 0, 0), inv));
            }
            for (size_t i = 0; i + 2 < desc.Triangles.size(); i += 3)
            {
                JPH::SoftBodySharedSettings::Face f(desc.Triangles[i], desc.Triangles[i + 1], desc.Triangles[i + 2]);
                if (!f.IsDegenerate())
                    shared->AddFace(f);
            }
            JPH::SoftBodySharedSettings::VertexAttributes attr(desc.Compliance, desc.Compliance, desc.BendCompliance);
            shared->CreateConstraints(&attr, 1, JPH::SoftBodySharedSettings::EBendType::Distance);
            shared->Optimize();
            JPH::SoftBodyCreationSettings sc(shared, JPH::RVec3::sZero(), JPH::Quat::sIdentity(), MakeLayer(desc.Layer, true));
            sc.mNumIterations = (JPH::uint32)std::clamp(desc.Iterations, 1, 64);
            sc.mLinearDamping = std::max(desc.Damping, 0.0f);
            sc.mFriction = std::clamp(desc.Friction, 0.0f, 1.0f);
            sc.mGravityFactor = desc.GravityFactor;
            sc.mVertexRadius = std::max(desc.VertexRadius, 0.0f);
            sc.mFacesDoubleSided = true;
            sc.mAllowSleeping = false;
            sc.mUserData = desc.Entity;
            JPH::BodyInterface& bi = m_System->GetBodyInterface();
            JPH::Body* body = bi.CreateSoftBody(sc);
            if (!body)
                return 0;
            bi.AddBody(body->GetID(), JPH::EActivation::Activate);
            const Handle h = ToHandle(body->GetID());
            m_SoftBodies.insert(h);
            return h;
        }

        void DestroySoftBody(Handle h) override
        {
            if (!m_SoftBodies.erase(h))
                return;
            JPH::BodyInterface& bi = m_System->GetBodyInterface();
            bi.RemoveBody(ToId(h));
            bi.DestroyBody(ToId(h));
        }

        bool GetSoftBodyVertices(Handle h, std::vector<glm::vec3>& out) const override
        {
            if (!m_SoftBodies.count(h))
                return false;
            JPH::BodyLockRead lock(m_System->GetBodyLockInterface(), ToId(h));
            if (!lock.Succeeded())
                return false;
            const JPH::Body& body = lock.GetBody();
            const auto* mp = static_cast<const JPH::SoftBodyMotionProperties*>(body.GetMotionProperties());
            const JPH::RMat44 com = body.GetCenterOfMassTransform();
            const auto& verts = mp->GetVertices();
            out.resize(verts.size());
            for (size_t i = 0; i < verts.size(); ++i)
            {
                JPH::RVec3 p = com * verts[i].mPosition;
                out[i] = glm::vec3((float)p.GetX(), (float)p.GetY(), (float)p.GetZ());
            }
            return true;
        }

        void DriveSoftBody(Handle h, const std::vector<uint32_t>& pinned, const std::vector<glm::vec3>& pinnedWorld, const glm::vec3& addVelocity, float dt) override
        {
            if (!m_SoftBodies.count(h))
                return;
            JPH::BodyLockWrite lock(m_System->GetBodyLockInterface(), ToId(h));
            if (!lock.Succeeded())
                return;
            JPH::Body& body = lock.GetBody();
            auto* mp = static_cast<JPH::SoftBodyMotionProperties*>(body.GetMotionProperties());
            const JPH::RMat44 invCom = body.GetCenterOfMassTransform().InversedRotationTranslation();
            auto& verts = mp->GetVertices();
            for (size_t k = 0; k < pinned.size() && k < pinnedWorld.size(); ++k)
            {
                if (pinned[k] >= verts.size())
                    continue;
                auto& v = verts[pinned[k]];
                const JPH::Vec3 local = JPH::Vec3(invCom * JPH::RVec3(pinnedWorld[k].x, pinnedWorld[k].y, pinnedWorld[k].z));
                v.mVelocity = dt > 0.0f ? (local - v.mPosition) / dt : JPH::Vec3::sZero();
                v.mPosition = local;
            }
            if (glm::dot(addVelocity, addVelocity) > 0.0f)
            {
                const JPH::Vec3 dv(addVelocity.x, addVelocity.y, addVelocity.z);
                for (auto& v : verts)
                    if (v.mInvMass > 0.0f)
                        v.mVelocity += dv;
            }
            m_System->GetBodyInterfaceNoLock().ActivateBody(ToId(h));
        }

        void DestroyBody(Handle h) override
        {
            auto it = m_Bodies.find(h);
            if (it == m_Bodies.end())
                return;
            JPH::BodyID id = ToId(h);
            // Bu gövdeye bağlı eklemleri sök
            for (auto jt = m_Joints.begin(); jt != m_Joints.end();)
            {
                auto* two = static_cast<JPH::TwoBodyConstraint*>(jt->second.Constraint.GetPtr());
                if (two->GetBody1()->GetID() == id || two->GetBody2()->GetID() == id)
                {
                    m_System->RemoveConstraint(jt->second.Constraint);
                    jt = m_Joints.erase(jt);
                }
                else
                    ++jt;
            }
            m_Contacts.ForgetBody(id);
            JPH::BodyInterface& bi = m_System->GetBodyInterface();
            bi.RemoveBody(id);
            bi.DestroyBody(id);
            m_Bodies.erase(it);
        }

        void SetBodyTransform(Handle h, const glm::vec3& position, const glm::quat& rotation, bool resetVelocity) override
        {
            if (!Valid(h)) return;
            JPH::BodyInterface& bi = m_System->GetBodyInterface();
            JPH::BodyID id = ToId(h);
            bi.SetPositionAndRotation(id, ToJR(position), ToJ(rotation),
                bi.GetMotionType(id) == JPH::EMotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
            if (resetVelocity && bi.GetMotionType(id) != JPH::EMotionType::Static)
            {
                bi.SetLinearVelocity(id, JPH::Vec3::sZero());
                bi.SetAngularVelocity(id, JPH::Vec3::sZero());
            }
        }

        void MoveKinematic(Handle h, const glm::vec3& position, const glm::quat& rotation, float dt) override
        {
            if (!Valid(h)) return;
            JPH::BodyInterface& bi = m_System->GetBodyInterface();
            JPH::BodyID id = ToId(h);
            if (bi.GetMotionType(id) == JPH::EMotionType::Kinematic)
                bi.MoveKinematic(id, ToJR(position), ToJ(rotation), std::max(1e-5f, dt));
            else
                bi.SetPositionAndRotation(id, ToJR(position), ToJ(rotation), JPH::EActivation::DontActivate);
        }

        void GetBodyTransform(Handle h, glm::vec3& position, glm::quat& rotation) const override
        {
            if (!Valid(h)) return;
            JPH::RVec3 p;
            JPH::Quat q;
            m_System->GetBodyInterface().GetPositionAndRotation(ToId(h), p, q);
            position = glm::vec3((float)p.GetX(), (float)p.GetY(), (float)p.GetZ());
            rotation = ToG(q);
        }

        bool IsBodySleeping(Handle h) const override { return Valid(h) && !m_System->GetBodyInterface().IsActive(ToId(h)); }
        void WakeBody(Handle h) override { if (Valid(h)) m_System->GetBodyInterface().ActivateBody(ToId(h)); }

        void AddForce(Handle h, const glm::vec3& f) override { if (Dynamic(h)) m_System->GetBodyInterface().AddForce(ToId(h), ToJ(f)); }
        void AddForceAtPoint(Handle h, const glm::vec3& f, const glm::vec3& p) override { if (Dynamic(h)) m_System->GetBodyInterface().AddForce(ToId(h), ToJ(f), ToJR(p)); }
        void AddImpulse(Handle h, const glm::vec3& i) override { if (Dynamic(h)) m_System->GetBodyInterface().AddImpulse(ToId(h), ToJ(i)); }
        void AddImpulseAtPoint(Handle h, const glm::vec3& i, const glm::vec3& p) override { if (Dynamic(h)) m_System->GetBodyInterface().AddImpulse(ToId(h), ToJ(i), ToJR(p)); }
        void AddTorque(Handle h, const glm::vec3& t) override { if (Dynamic(h)) m_System->GetBodyInterface().AddTorque(ToId(h), ToJ(t)); }
        void AddAngularImpulse(Handle h, const glm::vec3& i) override { if (Dynamic(h)) m_System->GetBodyInterface().AddAngularImpulse(ToId(h), ToJ(i)); }

        void SetLinearVelocity(Handle h, const glm::vec3& v) override
        {
            if (!Valid(h)) return;
            JPH::BodyInterface& bi = m_System->GetBodyInterface();
            if (bi.GetMotionType(ToId(h)) == JPH::EMotionType::Static) return;
            bi.SetLinearVelocity(ToId(h), ToJ(v));
            bi.ActivateBody(ToId(h));
        }
        glm::vec3 GetLinearVelocity(Handle h) const override { return Valid(h) ? ToG(m_System->GetBodyInterface().GetLinearVelocity(ToId(h))) : glm::vec3(0.0f); }
        void SetAngularVelocity(Handle h, const glm::vec3& v) override
        {
            if (!Valid(h)) return;
            JPH::BodyInterface& bi = m_System->GetBodyInterface();
            if (bi.GetMotionType(ToId(h)) == JPH::EMotionType::Static) return;
            bi.SetAngularVelocity(ToId(h), ToJ(v));
            bi.ActivateBody(ToId(h));
        }
        glm::vec3 GetAngularVelocity(Handle h) const override { return Valid(h) ? ToG(m_System->GetBodyInterface().GetAngularVelocity(ToId(h))) : glm::vec3(0.0f); }

        glm::vec3 GetPointVelocity(Handle h, const glm::vec3& worldPoint) const override
        {
            if (!Valid(h)) return glm::vec3(0.0f);
            return ToG(m_System->GetBodyInterface().GetPointVelocity(ToId(h), ToJR(worldPoint)));
        }

        float GetMass(Handle h) const override
        {
            if (!Valid(h)) return 0.0f;
            JPH::BodyLockRead lock(m_System->GetBodyLockInterface(), ToId(h));
            if (!lock.Succeeded() || !lock.GetBody().IsDynamic()) return 0.0f;
            float inv = lock.GetBody().GetMotionProperties()->GetInverseMass();
            return inv > 0.0f ? 1.0f / inv : 0.0f;
        }

        // ── Eklemler ──
        Handle CreateJoint(const JointDesc& d) override
        {
            if (!m_Initialized || !Valid(d.BodyA))
                return 0;
            const JPH::BodyLockInterface& locks = m_System->GetBodyLockInterfaceNoLock();
            JPH::Body* a = locks.TryGetBody(ToId(d.BodyA));
            JPH::Body* b = d.BodyB && Valid(d.BodyB) ? locks.TryGetBody(ToId(d.BodyB)) : &JPH::Body::sFixedToWorld;
            if (!a || !b)
                return 0;
            glm::vec3 axis = glm::length(d.AxisWorld) > 1e-6f ? glm::normalize(d.AxisWorld) : glm::vec3(1, 0, 0);
            glm::vec3 normal = d.NormalWorld - axis * glm::dot(d.NormalWorld, axis);
            if (glm::length(normal) < 1e-4f)
                normal = std::fabs(axis.y) < 0.9f ? glm::cross(axis, glm::vec3(0, 1, 0)) : glm::cross(axis, glm::vec3(1, 0, 0));
            normal = glm::normalize(normal);
            // B (bağlı gövde, Jolt'ta gövde 1) tarafı: saklanan dinlenme çerçevesi ya da A ile aynı
            const glm::vec3 pointB = d.UseFrameB ? d.AnchorWorldB : d.AnchorWorld;
            glm::vec3 axisB = axis, normalB = normal;
            if (d.UseFrameB)
            {
                axisB = glm::length(d.AxisWorldB) > 1e-6f ? glm::normalize(d.AxisWorldB) : axis;
                normalB = d.NormalWorldB - axisB * glm::dot(d.NormalWorldB, axisB);
                normalB = glm::length(normalB) > 1e-4f ? glm::normalize(normalB) : normal;
            }

            JPH::Ref<JPH::TwoBodyConstraintSettings> settings;
            switch (d.Type)
            {
                case JointType::Fixed:
                {
                    auto* s = new JPH::FixedConstraintSettings();
                    s->mSpace = JPH::EConstraintSpace::WorldSpace;
                    if (d.UseFrameB)
                    {
                        // İlk kurulumdaki göreli poz korunur (gövde 1 = bağlı, gövde 2 = sahip)
                        s->mAutoDetectPoint = false;
                        s->mPoint1 = ToJR(pointB);
                        s->mAxisX1 = ToJ(axisB);
                        s->mAxisY1 = ToJ(normalB);
                        s->mPoint2 = ToJR(d.AnchorWorld);
                        s->mAxisX2 = ToJ(axis);
                        s->mAxisY2 = ToJ(normal);
                    }
                    else
                        s->mAutoDetectPoint = true;
                    settings = s;
                    break;
                }
                case JointType::Hinge:
                {
                    auto* s = new JPH::HingeConstraintSettings();
                    s->mSpace = JPH::EConstraintSpace::WorldSpace;
                    s->mPoint1 = ToJR(pointB);
                    s->mPoint2 = ToJR(d.AnchorWorld);
                    s->mHingeAxis1 = ToJ(axisB);
                    s->mHingeAxis2 = ToJ(axis);
                    s->mNormalAxis1 = ToJ(normalB);
                    s->mNormalAxis2 = ToJ(normal);
                    if (d.UseLimits)
                    {
                        s->mLimitsMin = std::clamp(d.LimitMin, -JPH::JPH_PI, 0.0f);
                        s->mLimitsMax = std::clamp(d.LimitMax, 0.0f, JPH::JPH_PI);
                    }
                    s->mMotorSettings = JPH::MotorSettings(2.0f, 1.0f);
                    s->mMotorSettings.SetTorqueLimit(std::max(0.0f, d.MotorMaxForce));
                    if (d.UseSpring)
                    {
                        s->mMotorSettings.mSpringSettings = JPH::SpringSettings(JPH::ESpringMode::StiffnessAndDamping, d.SpringStiffness, d.SpringDamping);
                        s->mMotorSettings.SetTorqueLimit(FLT_MAX);
                    }
                    settings = s;
                    break;
                }
                case JointType::Slider:
                {
                    auto* s = new JPH::SliderConstraintSettings();
                    s->mSpace = JPH::EConstraintSpace::WorldSpace;
                    s->mPoint1 = ToJR(pointB);
                    s->mPoint2 = ToJR(d.AnchorWorld);
                    s->mSliderAxis1 = ToJ(axisB);
                    s->mSliderAxis2 = ToJ(axis);
                    s->mNormalAxis1 = ToJ(normalB);
                    s->mNormalAxis2 = ToJ(normal);
                    if (d.UseLimits)
                    {
                        s->mLimitsMin = std::min(d.LimitMin, 0.0f);
                        s->mLimitsMax = std::max(d.LimitMax, 0.0f);
                    }
                    s->mMotorSettings = JPH::MotorSettings(2.0f, 1.0f);
                    s->mMotorSettings.SetForceLimit(std::max(0.0f, d.MotorMaxForce));
                    settings = s;
                    break;
                }
                case JointType::Spring:
                {
                    auto* s = new JPH::DistanceConstraintSettings();
                    s->mSpace = JPH::EConstraintSpace::WorldSpace;
                    // Gövde 1 = bağlı gövde, gövde 2 = eklemin sahibi (aşağıdaki Create sırası)
                    s->mPoint1 = ToJR(d.ConnectedAnchorWorld);
                    s->mPoint2 = ToJR(d.AnchorWorld);
                    float rest = d.RestLength >= 0.0f ? d.RestLength : glm::distance(d.AnchorWorld, d.ConnectedAnchorWorld);
                    float minD = d.MinDistance, maxD = d.MaxDistance > 0.0f ? d.MaxDistance : rest;
                    if (minD > maxD) std::swap(minD, maxD);
                    s->mMinDistance = minD;
                    s->mMaxDistance = maxD;
                    s->mLimitsSpringSettings = JPH::SpringSettings(JPH::ESpringMode::StiffnessAndDamping, std::max(0.0f, d.SpringStiffness), std::max(0.0f, d.SpringDamping));
                    settings = s;
                    break;
                }
                case JointType::Character:
                {
                    auto* s = new JPH::SwingTwistConstraintSettings();
                    s->mSpace = JPH::EConstraintSpace::WorldSpace;
                    s->mPosition1 = ToJR(pointB);
                    s->mPosition2 = ToJR(d.AnchorWorld);
                    s->mTwistAxis1 = ToJ(axisB);
                    s->mTwistAxis2 = ToJ(axis);
                    s->mPlaneAxis1 = ToJ(normalB);
                    s->mPlaneAxis2 = ToJ(normal);
                    s->mNormalHalfConeAngle = std::clamp(d.Swing1, 0.0f, JPH::JPH_PI);
                    s->mPlaneHalfConeAngle = std::clamp(d.Swing2, 0.0f, JPH::JPH_PI);
                    s->mTwistMinAngle = std::clamp(std::min(d.TwistMin, d.TwistMax), -JPH::JPH_PI, JPH::JPH_PI);
                    s->mTwistMaxAngle = std::clamp(std::max(d.TwistMin, d.TwistMax), -JPH::JPH_PI, JPH::JPH_PI);
                    settings = s;
                    break;
                }
                case JointType::Ball:
                {
                    auto* s = new JPH::PointConstraintSettings();
                    s->mSpace = JPH::EConstraintSpace::WorldSpace;
                    s->mPoint1 = ToJR(pointB);
                    s->mPoint2 = ToJR(d.AnchorWorld);
                    settings = s;
                    break;
                }
                case JointType::SixDOF:
                {
                    // Unity ConfigurableJoint: X = birincil eksen, Y = ikincil eksen; her eksen Locked / Limited / Free
                    using A = JPH::SixDOFConstraintSettings::EAxis;
                    const SixDOFDesc& x = d.SixDOF;
                    glm::vec3 sec = x.SecondaryAxisWorld - axis * glm::dot(x.SecondaryAxisWorld, axis);
                    sec = glm::length(sec) > 1e-4f ? glm::normalize(sec) : normal;
                    glm::vec3 secB = sec;
                    if (d.UseFrameB)
                    {
                        secB = d.SecondaryAxisWorldB - axisB * glm::dot(d.SecondaryAxisWorldB, axisB);
                        secB = glm::length(secB) > 1e-4f ? glm::normalize(secB) : normalB;
                    }
                    auto* s = new JPH::SixDOFConstraintSettings();
                    s->mSpace = JPH::EConstraintSpace::WorldSpace;
                    s->mPosition1 = ToJR(pointB);
                    s->mPosition2 = ToJR(d.AnchorWorld);
                    s->mAxisX1 = ToJ(axisB);
                    s->mAxisX2 = ToJ(axis);
                    s->mAxisY1 = ToJ(secB);
                    s->mAxisY2 = ToJ(sec);
                    s->mSwingType = JPH::ESwingType::Pyramid;
                    for (int i = 0; i < 3; ++i)
                    {
                        A ax = (A)((int)A::TranslationX + i);
                        if (x.Linear[i] == AxisMotion::Locked) s->MakeFixedAxis(ax);
                        else if (x.Linear[i] == AxisMotion::Free) s->MakeFreeAxis(ax);
                        else
                        {
                            float lim = std::max(0.0f, x.LinearLimit);
                            s->SetLimitedAxis(ax, -lim, lim);
                            if (x.LinearLimitSpring > 0.0f)
                                s->mLimitsSpringSettings[i] = JPH::SpringSettings(JPH::ESpringMode::StiffnessAndDamping, x.LinearLimitSpring, std::max(0.0f, x.LinearLimitDamper));
                        }
                        // Sürücü: yay (konum hedefi) + sönüm
                        auto& m = s->mMotorSettings[(int)ax];
                        m.mSpringSettings = JPH::SpringSettings(JPH::ESpringMode::StiffnessAndDamping, std::max(0.0f, x.LinearDriveSpring[i]), std::max(0.0f, x.LinearDriveDamper[i]));
                        m.SetForceLimit(std::max(0.0f, x.LinearDriveMaxForce[i]));
                    }
                    const float pi = JPH::JPH_PI;
                    for (int i = 0; i < 3; ++i)
                    {
                        A ax = (A)((int)A::RotationX + i);
                        if (x.Angular[i] == AxisMotion::Locked) s->MakeFixedAxis(ax);
                        else if (x.Angular[i] == AxisMotion::Free) s->MakeFreeAxis(ax);
                        else if (i == 0)
                            s->SetLimitedAxis(ax, std::clamp(std::min(x.AngularXMin, x.AngularXMax), -pi, pi), std::clamp(std::max(x.AngularXMin, x.AngularXMax), -pi, pi));
                        else
                        {
                            // Salınım sınırları simetrik olmalı
                            float lim = std::clamp(i == 1 ? x.AngularYLimit : x.AngularZLimit, 0.0f, pi * 0.5f);
                            s->SetLimitedAxis(ax, -lim, lim);
                        }
                        auto& m = s->mMotorSettings[(int)ax];
                        m.mSpringSettings = JPH::SpringSettings(JPH::ESpringMode::StiffnessAndDamping, std::max(0.0f, x.AngularDriveSpring[i]), std::max(0.0f, x.AngularDriveDamper[i]));
                        m.SetTorqueLimit(std::max(0.0f, x.AngularDriveMaxForce[i]));
                    }
                    settings = s;
                    break;
                }
            }
            if (!settings)
                return 0;
            // Unity anlamı: açı/konum = sahibin bağlı gövdeye göre durumu => Jolt'ta gövde 1 = bağlı, gövde 2 = sahip
            JPH::TwoBodyConstraint* c = settings->Create(*b, *a);
            if (!c)
                return 0;
            c->SetEnabled(true);
            m_System->AddConstraint(c);
            // Bağlı gövdeler arasında çarpışma kapalıysa grup filtresi yerine temas doğrulamasıyla engelle
            if (!d.EnableCollision && d.BodyB)
                m_NoCollidePairs.insert(PairKey(ToId(d.BodyA), ToId(d.BodyB)));
            // Motor / yay hedefleri
            if (d.Type == JointType::Hinge)
            {
                auto* hinge = static_cast<JPH::HingeConstraint*>(c);
                if (d.UseSpring)
                {
                    hinge->SetMotorState(JPH::EMotorState::Position);
                    hinge->SetTargetAngle(d.SpringTarget);
                }
                else if (d.UseMotor)
                {
                    hinge->SetMotorState(JPH::EMotorState::Velocity);
                    hinge->SetTargetAngularVelocity(d.MotorTargetVelocity);
                }
            }
            else if (d.Type == JointType::Slider && d.UseMotor)
            {
                auto* slider = static_cast<JPH::SliderConstraint*>(c);
                slider->SetMotorState(JPH::EMotorState::Velocity);
                slider->SetTargetVelocity(d.MotorTargetVelocity);
            }
            else if (d.Type == JointType::SixDOF)
            {
                // Yaylı sürücü = konum motoru (yay sertliği + sönüm), yalnızca sönüm = hız motoru
                using A = JPH::SixDOFConstraintSettings::EAxis;
                auto* six = static_cast<JPH::SixDOFConstraint*>(c);
                const SixDOFDesc& x = d.SixDOF;
                for (int i = 0; i < 6; ++i)
                {
                    const bool linear = i < 3;
                    const float spring = linear ? x.LinearDriveSpring[i] : x.AngularDriveSpring[i - 3];
                    const float damper = linear ? x.LinearDriveDamper[i] : x.AngularDriveDamper[i - 3];
                    const AxisMotion motion = linear ? x.Linear[i] : x.Angular[i - 3];
                    if (motion == AxisMotion::Locked)
                        continue;
                    six->SetMotorState((A)i, spring > 0.0f ? JPH::EMotorState::Position : (damper > 0.0f ? JPH::EMotorState::Velocity : JPH::EMotorState::Off));
                }
            }
            Handle h = ++m_NextJoint;
            m_Joints[h] = { c, d.Type };
            // Eklem eklenince gövdeleri uyandır
            m_System->GetBodyInterface().ActivateBody(ToId(d.BodyA));
            if (d.BodyB) m_System->GetBodyInterface().ActivateBody(ToId(d.BodyB));
            return h;
        }

        void SetJointTargets(Handle h, const JointTargets& t) override
        {
            auto it = m_Joints.find(h);
            if (it == m_Joints.end() || it->second.Type != JointType::SixDOF)
                return;
            auto* six = static_cast<JPH::SixDOFConstraint*>(it->second.Constraint.GetPtr());
            six->SetTargetPositionCS(ToJ(t.Position));
            six->SetTargetVelocityCS(ToJ(t.Velocity));
            six->SetTargetOrientationCS(JPH::Quat(t.Rotation.x, t.Rotation.y, t.Rotation.z, t.Rotation.w).Normalized());
            six->SetTargetAngularVelocityCS(ToJ(t.AngularVelocity));
            auto* two = static_cast<JPH::TwoBodyConstraint*>(six);
            m_System->GetBodyInterface().ActivateBody(two->GetBody2()->GetID());
            if (!two->GetBody1()->IsStatic())
                m_System->GetBodyInterface().ActivateBody(two->GetBody1()->GetID());
        }

        bool GetJointPose(Handle h, glm::vec3& translation, glm::vec3& euler) const override
        {
            translation = euler = glm::vec3(0.0f);
            auto it = m_Joints.find(h);
            if (it == m_Joints.end() || it->second.Type != JointType::SixDOF)
                return false;
            auto* two = static_cast<JPH::TwoBodyConstraint*>(it->second.Constraint.GetPtr());
            // Bağlı gövdenin (1) eklem çerçevesinde sahibin (2) eklem çerçevesi
            JPH::RMat44 f1 = two->GetBody1()->GetCenterOfMassTransform() * two->GetConstraintToBody1Matrix();
            JPH::RMat44 f2 = two->GetBody2()->GetCenterOfMassTransform() * two->GetConstraintToBody2Matrix();
            JPH::Vec3 local = JPH::Vec3(f1.InversedRotationTranslation() * f2.GetTranslation());
            translation = ToG(local);
            JPH::Quat q = static_cast<JPH::SixDOFConstraint*>(two)->GetRotationInConstraintSpace();
            euler = glm::eulerAngles(glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()));
            return true;
        }

        void DestroyJoint(Handle h) override
        {
            auto it = m_Joints.find(h);
            if (it == m_Joints.end())
                return;
            auto* two = static_cast<JPH::TwoBodyConstraint*>(it->second.Constraint.GetPtr());
            m_NoCollidePairs.erase(PairKey(two->GetBody1()->GetID(), two->GetBody2()->GetID()));
            JPH::BodyID b1 = two->GetBody1()->GetID(), b2 = two->GetBody2()->GetID();
            m_System->RemoveConstraint(it->second.Constraint);
            m_Joints.erase(it);
            if (!b1.IsInvalid()) m_System->GetBodyInterface().ActivateBody(b1);
            if (!b2.IsInvalid()) m_System->GetBodyInterface().ActivateBody(b2);
        }

        void GetJointReaction(Handle h, float& force, float& torque) const override
        {
            force = torque = 0.0f;
            auto it = m_Joints.find(h);
            if (it == m_Joints.end())
                return;
            const float inv = 1.0f / std::max(1e-6f, m_LastDt / (float)std::max(1, m_Settings.CollisionSteps));
            JPH::Constraint* c = it->second.Constraint.GetPtr();
            switch (it->second.Type)
            {
                case JointType::Fixed:
                {
                    auto* f = static_cast<JPH::FixedConstraint*>(c);
                    force = f->GetTotalLambdaPosition().Length() * inv;
                    torque = f->GetTotalLambdaRotation().Length() * inv;
                    break;
                }
                case JointType::Hinge:
                {
                    auto* f = static_cast<JPH::HingeConstraint*>(c);
                    force = f->GetTotalLambdaPosition().Length() * inv;
                    auto r = f->GetTotalLambdaRotation();
                    torque = std::sqrt(r[0] * r[0] + r[1] * r[1]) * inv;
                    break;
                }
                case JointType::Slider:
                {
                    auto* f = static_cast<JPH::SliderConstraint*>(c);
                    auto p = f->GetTotalLambdaPosition();
                    force = std::sqrt(p[0] * p[0] + p[1] * p[1]) * inv;
                    torque = f->GetTotalLambdaRotation().Length() * inv;
                    break;
                }
                case JointType::Spring:
                    force = std::fabs(static_cast<JPH::DistanceConstraint*>(c)->GetTotalLambdaPosition()) * inv;
                    break;
                case JointType::Character:
                {
                    auto* f = static_cast<JPH::SwingTwistConstraint*>(c);
                    force = f->GetTotalLambdaPosition().Length() * inv;
                    torque = std::sqrt(f->GetTotalLambdaTwist() * f->GetTotalLambdaTwist() + f->GetTotalLambdaSwingY() * f->GetTotalLambdaSwingY()
                                       + f->GetTotalLambdaSwingZ() * f->GetTotalLambdaSwingZ()) * inv;
                    break;
                }
                case JointType::Ball:
                    force = static_cast<JPH::PointConstraint*>(c)->GetTotalLambdaPosition().Length() * inv;
                    break;
                case JointType::SixDOF:
                {
                    auto* f = static_cast<JPH::SixDOFConstraint*>(c);
                    force = f->GetTotalLambdaPosition().Length() * inv;
                    torque = f->GetTotalLambdaRotation().Length() * inv;
                    break;
                }
            }
        }

        void SetJointMotor(Handle h, bool enabled, float targetVelocity, float maxForce) override
        {
            auto it = m_Joints.find(h);
            if (it == m_Joints.end())
                return;
            JPH::Constraint* c = it->second.Constraint.GetPtr();
            if (it->second.Type == JointType::Hinge)
            {
                auto* hinge = static_cast<JPH::HingeConstraint*>(c);
                hinge->GetMotorSettings().SetTorqueLimit(maxForce);
                hinge->SetMotorState(enabled ? JPH::EMotorState::Velocity : JPH::EMotorState::Off);
                hinge->SetTargetAngularVelocity(targetVelocity);
            }
            else if (it->second.Type == JointType::Slider)
            {
                auto* slider = static_cast<JPH::SliderConstraint*>(c);
                slider->GetMotorSettings().SetForceLimit(maxForce);
                slider->SetMotorState(enabled ? JPH::EMotorState::Velocity : JPH::EMotorState::Off);
                slider->SetTargetVelocity(targetVelocity);
            }
            auto* two = static_cast<JPH::TwoBodyConstraint*>(c);
            if (!two->GetBody1()->GetID().IsInvalid()) m_System->GetBodyInterface().ActivateBody(two->GetBody1()->GetID());
            if (!two->GetBody2()->GetID().IsInvalid()) m_System->GetBodyInterface().ActivateBody(two->GetBody2()->GetID());
        }

        float GetJointPosition(Handle h) const override
        {
            auto it = m_Joints.find(h);
            if (it == m_Joints.end())
                return 0.0f;
            if (it->second.Type == JointType::Hinge)
                return static_cast<JPH::HingeConstraint*>(it->second.Constraint.GetPtr())->GetCurrentAngle();
            if (it->second.Type == JointType::Slider)
                return static_cast<JPH::SliderConstraint*>(it->second.Constraint.GetPtr())->GetCurrentPosition();
            return 0.0f;
        }

        // ── Karakterler ──
        Handle CreateCharacter(const CharacterDesc& d) override
        {
            if (!m_Initialized)
                return 0;
            float radius = std::max(0.01f, d.Radius);
            float halfCyl = std::max(0.0f, d.Height * 0.5f - radius);
            JPH::RefConst<JPH::Shape> capsule = halfCyl > 1e-4f ? JPH::RefConst<JPH::Shape>(new JPH::CapsuleShape(halfCyl, radius))
                                                                : JPH::RefConst<JPH::Shape>(new JPH::SphereShape(radius));
            JPH::Ref<JPH::CharacterVirtualSettings> s = new JPH::CharacterVirtualSettings();
            s->mShape = JPH::RotatedTranslatedShapeSettings(ToJ(d.Center), JPH::Quat::sIdentity(), capsule).Create().Get();
            s->mMaxSlopeAngle = std::clamp(d.SlopeLimit, 0.0f, JPH::DegreesToRadians(89.0f));
            s->mMass = std::max(1.0f, d.Mass);
            s->mMaxStrength = d.PushRigidbodies ? 100.0f + d.Mass * 10.0f : 0.0f;
            s->mCharacterPadding = std::clamp(d.SkinWidth, 0.001f, 0.2f);
            s->mPenetrationRecoverySpeed = 1.0f;
            s->mPredictiveContactDistance = 0.1f;
            // Ayak düzlemi: kapsülün alt yarımküresi
            s->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -(d.Center.y - halfCyl));
            // İç gövde: tetikleyicilere girer, ışın sorgularına görünür
            float innerRadius = std::max(0.01f, radius * 0.9f);
            // Sorgular (ışın / çakışma) varlığı yaprak şeklin kullanıcı verisinden okur: karakterin kendisi olmalı
            JPH::Ref<JPH::Shape> innerLeaf = halfCyl > 1e-4f ? JPH::Ref<JPH::Shape>(new JPH::CapsuleShape(halfCyl, innerRadius))
                                                             : JPH::Ref<JPH::Shape>(new JPH::SphereShape(innerRadius));
            innerLeaf->SetUserData(d.Entity);
            s->mInnerBodyShape = JPH::RotatedTranslatedShapeSettings(ToJ(d.Center), JPH::Quat::sIdentity(), innerLeaf.GetPtr()).Create().Get();
            s->mInnerBodyLayer = MakeLayer(d.Layer, true);
            JPH::Ref<JPH::CharacterVirtual> ch = new JPH::CharacterVirtual(s, ToJR(d.Position), ToJ(d.Rotation), d.Entity, m_System.get());
            ch->SetCharacterVsCharacterCollision(&m_CharVsChar);
            m_CharVsChar.Add(ch.GetPtr());
            Handle h = ++m_NextCharacter;
            CharacterRecord& rec = m_Characters[h];
            rec.Character = ch;
            rec.Listener = std::make_unique<CharacterListener>();
            rec.Listener->PushBodies = d.PushRigidbodies;
            ch->SetListener(rec.Listener.get());
            rec.Center = d.Center;
            rec.StepOffset = std::max(0.0f, d.StepOffset);
            rec.Layer = d.Layer;
            rec.Entity = d.Entity;
            // İç gövdenin kullanıcı verisi = varlık (temas olayları)
            if (!ch->GetInnerBodyID().IsInvalid())
            {
                m_System->GetBodyInterface().SetUserData(ch->GetInnerBodyID(), d.Entity);
                m_CharacterInnerBodies.insert(ch->GetInnerBodyID().GetIndexAndSequenceNumber());
            }
            rec.LastState.Position = d.Position;
            RefreshCharacter(rec);
            return h;
        }

        void DestroyCharacter(Handle h) override
        {
            auto it = m_Characters.find(h);
            if (it == m_Characters.end())
                return;
            if (!it->second.Character->GetInnerBodyID().IsInvalid())
            {
                m_CharacterInnerBodies.erase(it->second.Character->GetInnerBodyID().GetIndexAndSequenceNumber());
                m_Contacts.ForgetBody(it->second.Character->GetInnerBodyID());
            }
            m_CharVsChar.Remove(it->second.Character.GetPtr());
            m_Characters.erase(it);
        }

        CharacterState MoveCharacter(Handle h, const glm::vec3& displacement, float dt) override
        {
            auto it = m_Characters.find(h);
            if (it == m_Characters.end())
                return {};
            CharacterRecord& rec = it->second;
            JPH::CharacterVirtual* ch = rec.Character.GetPtr();
            dt = std::max(dt, 1e-4f);
            glm::vec3 before = ToG(JPH::Vec3(ch->GetPosition()));
            ch->SetLinearVelocity(ToJ(displacement / dt));
            JPH::CharacterVirtual::ExtendedUpdateSettings ext;
            const bool wasGrounded = ch->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
            ext.mStickToFloorStepDown = (wasGrounded && displacement.y <= 1e-5f) ? JPH::Vec3(0, -std::max(0.05f, rec.StepOffset), 0) : JPH::Vec3::sZero();
            ext.mWalkStairsStepUp = JPH::Vec3(0, rec.StepOffset, 0);
            MaskLayerFilter layerFilter;
            layerFilter.Mask = 0xFFFFFFFFu;
            CharacterLayerFilter objFilter(rec.Layer, m_Settings);
            JPH::IgnoreSingleBodyFilter bodyFilter(ch->GetInnerBodyID());
            ch->ExtendedUpdate(dt, JPH::Vec3::sZero(), ext, m_System->GetDefaultBroadPhaseLayerFilter(MakeLayer(rec.Layer, true)),
                               objFilter, bodyFilter, {}, *s_TempAllocator);
            glm::vec3 after = ToG(JPH::Vec3(ch->GetPosition()));
            // Kutuları Jolt'un karakter itmesi iter (eski hız); karakter artık fizik adımında hareket ettiği için kutuyla aynı
            // adımda ilerler, aradaki karelerde takılıp sarsılmaz
            RefreshCharacter(rec);
            rec.LastState.Velocity = (after - before) / dt;
            return rec.LastState;
        }


        void SetCharacterPosition(Handle h, const glm::vec3& position) override
        {
            auto it = m_Characters.find(h);
            if (it == m_Characters.end())
                return;
            JPH::CharacterVirtual* ch = it->second.Character.GetPtr();
            ch->SetPosition(ToJR(position));
            ch->SetLinearVelocity(JPH::Vec3::sZero());
            CharacterLayerFilter objFilter(it->second.Layer, m_Settings);
            JPH::IgnoreSingleBodyFilter bodyFilter(ch->GetInnerBodyID());
            ch->RefreshContacts(m_System->GetDefaultBroadPhaseLayerFilter(MakeLayer(it->second.Layer, true)), objFilter, bodyFilter, {}, *s_TempAllocator);
            RefreshCharacter(it->second);
        }

        CharacterState GetCharacterState(Handle h) const override
        {
            auto it = m_Characters.find(h);
            return it == m_Characters.end() ? CharacterState{} : it->second.LastState;
        }

        // ── Sorgular ──
        bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, const QueryFilter& filter, RaycastHit& hit) const override
        {
            if (!m_Initialized || glm::length(direction) < 1e-8f)
                return false;
            glm::vec3 dir = glm::normalize(direction);
            maxDistance = std::min(maxDistance, 1.0e6f);
            JPH::RRayCast ray(ToJR(origin), ToJ(dir * maxDistance));
            JPH::RayCastSettings rs;
            rs.SetBackFaceMode(JPH::EBackFaceMode::IgnoreBackFaces);
            rs.mTreatConvexAsSolid = true;
            JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> collector;
            MaskLayerFilter lf;
            lf.Mask = filter.LayerMask;
            QueryBodyFilter bf;
            bf.HitTriggers = filter.HitTriggers;
            bf.IgnoreEntity = filter.IgnoreEntity;
            if (filter.IgnoreBody) bf.IgnoreBodyId = (uint32_t)(filter.IgnoreBody - 1);
            m_System->GetNarrowPhaseQuery().CastRay(ray, rs, collector, {}, lf, bf);
            if (!collector.HadHit())
                return false;
            FillRayHit(ray, collector.mHit, maxDistance, hit);
            return true;
        }

        void RaycastAll(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, const QueryFilter& filter, std::vector<RaycastHit>& hits) const override
        {
            hits.clear();
            if (!m_Initialized || glm::length(direction) < 1e-8f)
                return;
            glm::vec3 dir = glm::normalize(direction);
            maxDistance = std::min(maxDistance, 1.0e6f);
            JPH::RRayCast ray(ToJR(origin), ToJ(dir * maxDistance));
            JPH::RayCastSettings rs;
            rs.SetBackFaceMode(JPH::EBackFaceMode::IgnoreBackFaces);
            JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
            MaskLayerFilter lf;
            lf.Mask = filter.LayerMask;
            QueryBodyFilter bf;
            bf.HitTriggers = filter.HitTriggers;
            bf.IgnoreEntity = filter.IgnoreEntity;
            if (filter.IgnoreBody) bf.IgnoreBodyId = (uint32_t)(filter.IgnoreBody - 1);
            m_System->GetNarrowPhaseQuery().CastRay(ray, rs, collector, {}, lf, bf);
            collector.Sort();
            for (auto& r : collector.mHits)
            {
                RaycastHit h;
                FillRayHit(ray, r, maxDistance, h);
                hits.push_back(h);
            }
        }

        bool SphereCast(const glm::vec3& origin, float radius, const glm::vec3& direction, float maxDistance, const QueryFilter& filter, RaycastHit& hit) const override
        {
            if (!m_Initialized || glm::length(direction) < 1e-8f)
                return false;
            glm::vec3 dir = glm::normalize(direction);
            maxDistance = std::min(maxDistance, 1.0e6f);
            JPH::SphereShape sphere(std::max(0.001f, radius));
            sphere.SetEmbedded();
            JPH::RShapeCast cast(&sphere, JPH::Vec3::sOne(), JPH::RMat44::sTranslation(ToJR(origin)), ToJ(dir * maxDistance));
            JPH::ShapeCastSettings ss;
            ss.mBackFaceModeTriangles = JPH::EBackFaceMode::IgnoreBackFaces;
            JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
            MaskLayerFilter lf;
            lf.Mask = filter.LayerMask;
            QueryBodyFilter bf;
            bf.HitTriggers = filter.HitTriggers;
            bf.IgnoreEntity = filter.IgnoreEntity;
            if (filter.IgnoreBody) bf.IgnoreBodyId = (uint32_t)(filter.IgnoreBody - 1);
            m_System->GetNarrowPhaseQuery().CastShape(cast, ss, JPH::RVec3::sZero(), collector, {}, lf, bf);
            if (!collector.HadHit())
                return false;
            const JPH::ShapeCastResult& r = collector.mHit;
            hit.Entity = EntityOf(r.mBodyID2, r.mSubShapeID2);
            hit.Distance = r.mFraction * maxDistance;
            hit.Point = ToG(JPH::Vec3(r.mContactPointOn2));
            JPH::Vec3 n = -r.mPenetrationAxis;
            hit.Normal = n.LengthSq() > 1e-12f ? ToG(n.Normalized()) : -dir;
            hit.IsTrigger = IsSensor(r.mBodyID2);
            return true;
        }

        void OverlapSphere(const glm::vec3& center, float radius, const QueryFilter& filter, std::vector<uint32_t>& entities) const override
        {
            JPH::SphereShape sphere(std::max(0.001f, radius));
            sphere.SetEmbedded();
            Overlap(&sphere, JPH::RMat44::sTranslation(ToJR(center)), filter, entities);
        }

        void OverlapBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation, const QueryFilter& filter, std::vector<uint32_t>& entities) const override
        {
            glm::vec3 he = glm::max(glm::abs(halfExtents), glm::vec3(0.001f));
            JPH::BoxShape box(ToJ(he), std::min(JPH::cDefaultConvexRadius, 0.5f * std::min(he.x, std::min(he.y, he.z))));
            box.SetEmbedded();
            Overlap(&box, JPH::RMat44::sRotationTranslation(ToJ(rotation), ToJR(center)), filter, entities);
        }

        void DrainContactEvents(std::vector<ContactEvent>& events) override
        {
            std::lock_guard lock(m_Contacts.Mutex);
            auto& src = m_Contacts.Events;
            // Bağlı (çarpışması kapalı) eklem çiftleri ve karakterin kendi iç gövdesi filtrelenir
            for (auto& ev : src)
            {
                if (ev.EntityA == ev.EntityB)
                    continue;
                events.push_back(ev);
            }
            src.clear();
        }

        int GetBodyCount() const override { return m_Initialized ? (int)m_System->GetNumBodies() : 0; }
        int GetActiveBodyCount() const override { return m_Initialized ? (int)m_System->GetNumActiveBodies(JPH::EBodyType::RigidBody) : 0; }

    private:
        // Karakter: kendi katmanına göre çarpışan nesne katmanları
        class CharacterLayerFilter final : public JPH::ObjectLayerFilter
        {
        public:
            CharacterLayerFilter(int layer, const PhysicsSettings& s) : m_Layer(layer), m_Settings(s) {}
            bool ShouldCollide(JPH::ObjectLayer layer) const override { return m_Settings.LayersCollide(m_Layer, UserLayer(layer)); }

        private:
            int m_Layer;
            const PhysicsSettings& m_Settings;
        };

        struct BodyInfo
        {
            uint32_t Entity = NoEntity;
            bool Sensor = false;
        };

        static JPH::BodyID ToId(Handle h) { return JPH::BodyID((uint32_t)(h - 1)); }
        static Handle ToHandle(JPH::BodyID id) { return (Handle)id.GetIndexAndSequenceNumber() + 1; }
        bool Valid(Handle h) const { return m_Initialized && h && m_Bodies.count(h); }
        bool Dynamic(Handle h) const
        {
            if (!Valid(h)) return false;
            return m_System->GetBodyInterface().GetMotionType(ToId(h)) == JPH::EMotionType::Dynamic;
        }

        bool IsSensor(JPH::BodyID id) const
        {
            auto it = m_Bodies.find(ToHandle(id));
            return it != m_Bodies.end() && it->second.Sensor;
        }

        uint32_t EntityOf(JPH::BodyID id, const JPH::SubShapeID& sub) const
        {
            JPH::BodyLockRead lock(m_System->GetBodyLockInterface(), id);
            if (!lock.Succeeded())
                return NoEntity;
            const JPH::Body& body = lock.GetBody();
            // Yaprak şekiller kaynak varlığı taşır (çocuk çarpıştırıcılar); yoksa gövdenin varlığı
            uint32_t shapeEntity = (uint32_t)body.GetShape()->GetSubShapeUserData(sub);
            return shapeEntity != NoEntity ? shapeEntity : (uint32_t)body.GetUserData();
        }

        void FillRayHit(const JPH::RRayCast& ray, const JPH::RayCastResult& r, float maxDistance, RaycastHit& hit) const
        {
            hit.Distance = r.mFraction * maxDistance;
            JPH::RVec3 p = ray.GetPointOnRay(r.mFraction);
            hit.Point = glm::vec3((float)p.GetX(), (float)p.GetY(), (float)p.GetZ());
            hit.Entity = EntityOf(r.mBodyID, r.mSubShapeID2);
            hit.IsTrigger = IsSensor(r.mBodyID);
            JPH::BodyLockRead lock(m_System->GetBodyLockInterface(), r.mBodyID);
            if (lock.Succeeded())
                hit.Normal = ToG(lock.GetBody().GetWorldSpaceSurfaceNormal(r.mSubShapeID2, p));
        }

        void Overlap(const JPH::Shape* shape, JPH::RMat44Arg transform, const QueryFilter& filter, std::vector<uint32_t>& entities) const
        {
            entities.clear();
            if (!m_Initialized)
                return;
            JPH::CollideShapeSettings cs;
            JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
            MaskLayerFilter lf;
            lf.Mask = filter.LayerMask;
            QueryBodyFilter bf;
            bf.HitTriggers = filter.HitTriggers;
            bf.IgnoreEntity = filter.IgnoreEntity;
            if (filter.IgnoreBody) bf.IgnoreBodyId = (uint32_t)(filter.IgnoreBody - 1);
            m_System->GetNarrowPhaseQuery().CollideShape(shape, JPH::Vec3::sOne(), transform, cs, JPH::RVec3::sZero(), collector, {}, lf, bf);
            std::set<uint32_t> unique;
            for (auto& r : collector.mHits)
            {
                uint32_t e = EntityOf(r.mBodyID2, r.mSubShapeID2);
                if (e != NoEntity && unique.insert(e).second)
                    entities.push_back(e);
            }
        }

        void RefreshCharacter(CharacterRecord& rec)
        {
            JPH::CharacterVirtual* ch = rec.Character.GetPtr();
            CharacterState& st = rec.LastState;
            st.Position = ToG(JPH::Vec3(ch->GetPosition()));
            auto ground = ch->GetGroundState();
            st.Grounded = ground == JPH::CharacterBase::EGroundState::OnGround;
            st.GroundNormal = ToG(ch->GetGroundNormal());
            st.GroundEntity = ground == JPH::CharacterBase::EGroundState::InAir ? NoEntity : (uint32_t)ch->GetGroundUserData();
            st.Flags = CollidedNone;
            if (ground != JPH::CharacterBase::EGroundState::InAir)
                st.Flags |= CollidedBelow;
            for (const auto& c : ch->GetActiveContacts())
            {
                if (!c.mHadCollision)
                    continue;
                float up = c.mContactNormal.GetY();
                if (up < -0.5f) st.Flags |= CollidedAbove;
                else if (up < 0.5f) st.Flags |= CollidedSides;
            }
        }

        PhysicsSettings m_Settings;
        BroadPhaseLayers m_BroadPhase;
        ObjectVsBroadPhase m_ObjVsBP;
        LayerPairs m_LayerPairs;
        std::unique_ptr<JPH::PhysicsSystem> m_System;
        ContactCollector m_Contacts;
        JPH::CharacterVsCharacterCollisionSimple m_CharVsChar;
        std::unordered_map<Handle, BodyInfo> m_Bodies;
        std::unordered_set<Handle> m_SoftBodies;
        std::map<Handle, JointRecord> m_Joints;
        std::unordered_map<Handle, CharacterRecord> m_Characters;
        std::set<uint64_t> m_NoCollidePairs;
        std::set<uint32_t> m_CharacterInnerBodies;
        Handle m_NextJoint = 0;
        Handle m_NextCharacter = 0;
        float m_LastDt = 1.0f / 60.0f;
        bool m_Initialized = false;
    };

    std::unique_ptr<Backend> CreateJoltBackend()
    {
        return std::make_unique<JoltBackend>();
    }
}
