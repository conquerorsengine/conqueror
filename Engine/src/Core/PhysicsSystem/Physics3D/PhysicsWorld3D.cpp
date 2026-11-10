#include "Core/PhysicsSystem/PhysicsMaterial.h"
#include "PhysicsWorld3D.h"
#include "Core/Logging/Log.h"
#include "Core/PhysicsSystem/PhysicsSettings.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Terrain/Terrain.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Renderer/Utilities/Renderer3D/Mesh.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "ClothMesh.h"
#include "Scene/GameplayComponents.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>

namespace Conqueror
{
    using namespace Physics3D;

    namespace
    {
        inline void HashCombine(size_t& seed, size_t v) { seed ^= v + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2); }
        inline void HashF(size_t& seed, float f) { HashCombine(seed, std::hash<int>()((int)std::lround(f * 1000.0f))); }
        inline void HashV(size_t& seed, const glm::vec3& v) { HashF(seed, v.x); HashF(seed, v.y); HashF(seed, v.z); }

        void Decompose(const glm::mat4& m, glm::vec3& position, glm::quat& rotation, glm::vec3& scale)
        {
            position = glm::vec3(m[3]);
            glm::vec3 c0(m[0]), c1(m[1]), c2(m[2]);
            scale = glm::vec3(glm::length(c0), glm::length(c1), glm::length(c2));
            if (glm::dot(glm::cross(c0, c1), c2) < 0.0f)
                scale.x = -scale.x;
            glm::mat3 r(scale.x != 0.0f ? c0 / scale.x : glm::vec3(1, 0, 0),
                        scale.y != 0.0f ? c1 / scale.y : glm::vec3(0, 1, 0),
                        scale.z != 0.0f ? c2 / scale.z : glm::vec3(0, 0, 1));
            rotation = glm::normalize(glm::quat_cast(r));
        }

        // Kapsül ekseni (Y varsayılan) -> X / Z
        glm::quat AxisRotation(int direction)
        {
            if (direction == 0) return glm::angleAxis(glm::radians(-90.0f), glm::vec3(0, 0, 1));
            if (direction == 2) return glm::angleAxis(glm::radians(90.0f), glm::vec3(1, 0, 0));
            return glm::quat(1, 0, 0, 0);
        }

        std::shared_ptr<Mesh> PrimitiveMesh(MeshType type)
        {
            switch (type)
            {
                case MeshType::Cube: return Renderer3D::GetCubeMesh();
                case MeshType::Sphere: return Renderer3D::GetSphereMesh();
                case MeshType::Plane: return Renderer3D::GetPlaneMesh();
                case MeshType::Cylinder: return Renderer3D::GetCylinderMesh();
            }
            return nullptr;
        }

        int LayerOf(Entity e)
        {
            return e.HasComponent<LayerComponent>() ? std::clamp(e.GetComponent<LayerComponent>().Layer, 0, 31) : 0;
        }
    }

    PhysicsWorld3D::PhysicsWorld3D() = default;

    PhysicsWorld3D::~PhysicsWorld3D()
    {
        Shutdown();
    }

    void PhysicsWorld3D::Initialize()
    {
        m_Initialized = true;
        m_Gravity = PhysicsSettings::Get().Gravity;
    }

    void PhysicsWorld3D::Shutdown()
    {
        OnRuntimeStop();
        m_Initialized = false;
    }

    // ════════════════════════════════════════
    //  Yaşam döngüsü
    // ════════════════════════════════════════
    void PhysicsWorld3D::CreateBackend()
    {
        const PhysicsSettings& settings = PhysicsSettings::Get();
        m_BackendError.clear();
        if (settings.Backend == PhysicsBackend3D::MuJoCo)
        {
            auto mj = CreateMuJoCoBackend();
            std::string error;
            if (mj->Init(settings, &error))
                m_Backend = std::move(mj);
            else
            {
                m_BackendError = error;
                CQ_CORE_ERROR("[Physics] MuJoCo unavailable, falling back to Jolt: {0}", error);
            }
        }
        if (!m_Backend)
        {
            m_Backend = CreateJoltBackend();
            if (!m_Backend)
            {
                CQ_CORE_WARN("[Physics] This build has no 3D physics backend (JoltPhysics disabled)");
                return;
            }
            std::string error;
            if (!m_Backend->Init(settings, &error))
            {
                CQ_CORE_ERROR("[Physics] Jolt init failed: {0}", error);
                m_Backend.reset();
                return;
            }
        }
        m_Gravity = settings.Gravity;
        m_Backend->SetGravity(m_Gravity);
    }

    void PhysicsWorld3D::OnRuntimeStart()
    {
        if (!m_Scene)
            return;
        OnRuntimeStop();
        CreateBackend();
        if (!m_Backend)
            return;
        m_Accumulator = 0.0f;
        m_FixedStepCounter = 0;
        auto& reg = m_Scene->m_Registry;
        auto mark = [&](auto view) { for (auto e : view) m_Dirty.insert((uint32_t)e); };
        mark(reg.view<RigidbodyComponent>());
        mark(reg.view<BoxColliderComponent>());
        mark(reg.view<SphereColliderComponent>());
        mark(reg.view<CapsuleColliderComponent>());
        mark(reg.view<MeshColliderComponent>());
        mark(reg.view<TerrainComponent>());
        mark(reg.view<CharacterControllerComponent>());
        mark(reg.view<FixedJointComponent>());
        mark(reg.view<HingeJointComponent>());
        mark(reg.view<SliderJointComponent>());
        mark(reg.view<SpringJointComponent>());
        mark(reg.view<CharacterJointComponent>());
        mark(reg.view<BallJointComponent>());
        mark(reg.view<ConfigurableJointComponent>());
        mark(reg.view<WheelColliderComponent>());
        ProcessDirty();
        CreateCloths();
        CQ_CORE_INFO("[Physics] {0} started: {1} bodies, {2} characters", m_Backend->GetName(), m_Bodies.size(), m_Characters.size());
    }

    void PhysicsWorld3D::CreateCloths()
    {
        m_Cloths.clear();
        if (!m_Backend || !m_Scene)
            return;
        auto view = m_Scene->m_Registry.view<ClothComponent, TransformComponent>();
        for (auto e : view)
        {
            if (!m_Scene->IsEntityActive(e))
                continue;
            const auto& c = view.get<ClothComponent>(e);
            ClothRecord rec;
            std::vector<glm::vec2> uvs;
            ClothMesh::BuildGrid(c, rec.RestLocal, rec.Triangles, uvs, rec.Pinned);
            const glm::mat4 world = WorldMatrix((uint32_t)e);
            Physics3D::SoftBodyDesc desc;
            desc.Vertices.reserve(rec.RestLocal.size());
            for (auto& p : rec.RestLocal)
                desc.Vertices.push_back(glm::vec3(world * glm::vec4(p, 1.0f)));
            desc.Triangles = rec.Triangles;
            desc.InvMass.assign(rec.RestLocal.size(), 1.0f);
            for (uint32_t i : rec.Pinned)
                desc.InvMass[i] = 0.0f;
            const float stretch = 1.0f - std::clamp(c.StretchingStiffness, 0.0f, 1.0f);
            const float bend = 1.0f - std::clamp(c.BendingStiffness, 0.0f, 1.0f);
            desc.Compliance = stretch * stretch * 0.01f;
            desc.BendCompliance = 1e-4f + bend * bend * 1.0f;
            desc.Damping = std::max(c.Damping, 0.0f);
            desc.Friction = c.Friction;
            desc.GravityFactor = c.GravityScale;
            desc.VertexRadius = std::max(c.Thickness, 0.0f);
            desc.Iterations = c.SolverIterations;
            desc.Layer = m_Scene->m_Registry.all_of<LayerComponent>(e) ? m_Scene->m_Registry.get<LayerComponent>(e).Layer : 0;
            desc.Entity = (uint32_t)e;
            rec.Body = m_Backend->CreateSoftBody(desc);
            if (!rec.Body)
                continue;
            // Görüntü mesh'i: kumaşın kendisi
            if (auto* mr = m_Scene->m_Registry.try_get<MeshRendererComponent>(e))
            {
                mr->CustomMesh = ClothMesh::CreateMesh(rec.RestLocal, rec.Triangles, uvs);
                auto& cc = m_Scene->m_Registry.get<ClothComponent>(e);
                cc.BuiltSignature = ClothMesh::Signature(cc);
            }
            m_Cloths[(uint32_t)e] = std::move(rec);
        }
    }

    void PhysicsWorld3D::DriveCloths(float dt)
    {
        if (m_Cloths.empty())
            return;
        auto& reg = m_Scene->m_Registry;
        // Rüzgâr: yönlü Wind Zone'lar (ana güç + türbülans)
        glm::vec3 wind(0.0f);
        float turbulence = 0.0f;
        for (auto w : reg.view<WindZoneComponent, TransformComponent>())
        {
            if (!m_Scene->IsEntityActive(w))
                continue;
            const auto& wz = reg.get<WindZoneComponent>(w);
            if (wz.Mode != WindZoneMode::Directional)
                continue;
            glm::vec3 fwd = glm::normalize(glm::vec3(WorldMatrix((uint32_t)w) * glm::vec4(0, 0, 1, 0)));
            wind += fwd * wz.Main;
            turbulence += wz.Turbulence;
        }
        for (auto& [entity, rec] : m_Cloths)
        {
            entt::entity e = (entt::entity)entity;
            if (!reg.valid(e) || !reg.all_of<ClothComponent>(e))
                continue;
            const auto& c = reg.get<ClothComponent>(e);
            const glm::mat4 world = WorldMatrix(entity);
            rec.PinnedWorld.resize(rec.Pinned.size());
            for (size_t k = 0; k < rec.Pinned.size(); ++k)
                rec.PinnedWorld[k] = glm::vec3(world * glm::vec4(rec.RestLocal[rec.Pinned[k]], 1.0f));
            rec.WindTime += dt;
            glm::vec3 gust = wind * (1.0f + 0.35f * turbulence * std::sin(rec.WindTime * 2.3f) * std::sin(rec.WindTime * 0.7f + 1.3f));
            const glm::vec3 addVel = gust * std::max(c.WindInfluence, 0.0f) * dt;
            m_Backend->DriveSoftBody(rec.Body, rec.Pinned, rec.PinnedWorld, addVel, dt);
        }
    }

    void PhysicsWorld3D::UpdateClothMeshes()
    {
        auto& reg = m_Scene->m_Registry;
        for (auto& [entity, rec] : m_Cloths)
        {
            entt::entity e = (entt::entity)entity;
            auto* mr = reg.valid(e) ? reg.try_get<MeshRendererComponent>(e) : nullptr;
            if (!mr || !mr->CustomMesh || !m_Backend->GetSoftBodyVertices(rec.Body, rec.World))
                continue;
            const glm::mat4 inv = glm::inverse(WorldMatrix(entity));
            rec.Local.resize(rec.World.size());
            for (size_t i = 0; i < rec.World.size(); ++i)
                rec.Local[i] = glm::vec3(inv * glm::vec4(rec.World[i], 1.0f));
            ClothMesh::UpdateMesh(*mr->CustomMesh, rec.Local, rec.Triangles);
        }
    }

    void PhysicsWorld3D::OnRuntimeStop()
    {
        if (m_Backend)
            m_Backend->Shutdown();
        m_Backend.reset();
        if (m_Scene)
        {
            auto view = m_Scene->m_Registry.view<RigidbodyComponent>();
            for (auto e : view)
                view.get<RigidbodyComponent>(e).RuntimeBody = nullptr;
        }
        m_Bodies.clear();
        m_ShapeOwner.clear();
        m_Joints.clear();
        m_JointSignatures.clear();
        m_JointRest.clear();
        m_Characters.clear();
        m_Cloths.clear();
        m_Dirty.clear();
        m_PendingEvents.clear();
        m_Accumulator = 0.0f;
    }

    void PhysicsWorld3D::Step(Timestep ts)
    {
        if (!m_Backend || !m_Scene)
            return;
        const PhysicsSettings& settings = PhysicsSettings::Get();
        m_FixedDt = std::clamp(settings.FixedTimestep, 1.0f / 1000.0f, 0.25f);
        float dt = std::clamp(ts.GetSeconds(), 0.0f, 0.25f);
        m_FrameDt = dt > 0.0f ? dt : m_FixedDt;
        auto start = std::chrono::steady_clock::now();

        ProcessDirty();
        SyncExternalChanges(dt);

        m_Accumulator += dt;
        int steps = 0;
        while (m_Accumulator >= m_FixedDt && steps < settings.MaxSubSteps)
        {
            PreStep(m_FixedDt);
            MoveCharactersFixed(m_FixedDt);   // karakterler gövdelerden önce (Jolt önerisi), aynı adımda
            m_Backend->Step(m_FixedDt);
            PostStep(m_FixedDt);
            m_Accumulator -= m_FixedDt;
            ++steps;
            ++m_FixedStepCounter;
        }
        // Yavaş karelerde geride kalmayı biriktirme (ölüm sarmalı)
        if (steps >= settings.MaxSubSteps && m_Accumulator > m_FixedDt)
            m_Accumulator = 0.0f;
        WriteTransforms(std::clamp(m_Accumulator / m_FixedDt, 0.0f, 1.0f));
        WriteCharacterTransforms(std::clamp(m_Accumulator / m_FixedDt, 0.0f, 1.0f));
        if (steps > 0)
            UpdateClothMeshes();
        UpdateWheelVisuals(dt);

        // Bileşen değişikliklerini (denetçi / betik) birkaç gövdede bir dene
        if (!m_Bodies.empty())
        {
            int checks = std::min<int>(32, (int)m_Bodies.size());
            auto it = m_Bodies.begin();
            std::advance(it, m_SignatureCursor % (int)m_Bodies.size());
            std::vector<uint32_t> changed;
            for (int i = 0; i < checks; ++i)
            {
                if (it == m_Bodies.end()) it = m_Bodies.begin();
                if (Valid(it->first) && ComputeSignature(it->first) != it->second.Signature)
                    changed.push_back(it->first);
                ++it;
            }
            m_SignatureCursor = (m_SignatureCursor + checks) % std::max<int>(1, (int)m_Bodies.size());
            for (uint32_t e : changed)
                m_Dirty.insert(e);
        }
        for (auto& [e, signature] : m_JointSignatures)
            if (Valid(e) && ComputeJointSignature(e) != signature)
                m_Dirty.insert(e);
        for (auto& [e, rec] : m_Characters)
            if (Valid(e) && ComputeCharacterSignature(e) != rec.Signature)
                m_Dirty.insert(e);

        m_LastStepCount = steps;
        m_LastStepMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
    }

    // ════════════════════════════════════════
    //  Kurulum
    // ════════════════════════════════════════
    bool PhysicsWorld3D::Valid(uint32_t entity) const
    {
        return m_Scene && entity != NoEntity && m_Scene->m_Registry.valid((entt::entity)entity);
    }

    glm::mat4 PhysicsWorld3D::WorldMatrix(uint32_t entity) const
    {
        glm::mat4 m(1.0f);
        Entity e{ (entt::entity)entity, m_Scene };
        // Köke kadar yerel dönüşümleri çarp (önbelleğe alınmış WorldTransform bayat olabilir)
        std::vector<glm::mat4> chain;
        for (int guard = 0; e && guard < 256; ++guard)
        {
            if (!m_Scene->m_Registry.valid((entt::entity)e) || !e.HasComponent<TransformComponent>())
                break;
            chain.push_back(e.GetComponent<TransformComponent>().GetTransform());
            e = e.GetParent();
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it)
            m = m * (*it);
        return m;
    }

    void PhysicsWorld3D::DecomposeWorld(uint32_t entity, glm::vec3& position, glm::quat& rotation, glm::vec3& scale) const
    {
        Decompose(WorldMatrix(entity), position, rotation, scale);
    }

    void PhysicsWorld3D::SetWorldPose(uint32_t entity, const glm::vec3& position, const glm::quat& rotation)
    {
        Entity e{ (entt::entity)entity, m_Scene };
        if (!Valid(entity) || !e.HasComponent<TransformComponent>())
            return;
        auto& tc = e.GetComponent<TransformComponent>();
        Entity parent = e.GetParent();
        glm::vec3 localPos = position;
        glm::quat localRot = rotation;
        glm::mat4 parentWorld(1.0f);
        if (parent)
        {
            parentWorld = WorldMatrix((uint32_t)parent);
            glm::vec3 pp, ps;
            glm::quat pr;
            Decompose(parentWorld, pp, pr, ps);
            localPos = glm::vec3(glm::inverse(parentWorld) * glm::vec4(position, 1.0f));
            localRot = glm::inverse(pr) * rotation;
        }
        tc.Position = localPos;
        tc.Rotation = glm::eulerAngles(glm::normalize(localRot));
        tc.WorldTransform = parentWorld * tc.GetTransform();
    }

    bool PhysicsWorld3D::HasPhysicsShape(uint32_t entity) const
    {
        Entity e{ (entt::entity)entity, m_Scene };
        if (e.HasComponent<BoxColliderComponent>() || e.HasComponent<SphereColliderComponent>() ||
            e.HasComponent<CapsuleColliderComponent>() || e.HasComponent<MeshColliderComponent>())
            return true;
        return e.HasComponent<TerrainComponent>() && e.HasComponent<TerrainColliderComponent>();
    }

    uint32_t PhysicsWorld3D::FindOwner(uint32_t entity) const
    {
        if (!Valid(entity) || !m_Scene->IsEntityActive((entt::entity)entity))
            return NoEntity;
        Entity e{ (entt::entity)entity, m_Scene };
        if (e.HasComponent<CharacterControllerComponent>())
            return NoEntity;
        if (e.HasComponent<RigidbodyComponent>())
            return entity;
        // Ata Rigidbody'ye bağlı çocuk çarpıştırıcı (bileşik gövde)
        if (HasPhysicsShape(entity))
        {
            Entity p = e.GetParent();
            for (int guard = 0; p && guard < 256; ++guard)
            {
                if (p.HasComponent<CharacterControllerComponent>())
                    break;
                if (p.HasComponent<RigidbodyComponent>())
                    return (uint32_t)p;
                p = p.GetParent();
            }
            return entity;
        }
        return NoEntity;
    }

    Handle PhysicsWorld3D::BodyOf(uint32_t entity) const
    {
        auto it = m_Bodies.find(entity);
        if (it != m_Bodies.end())
            return it->second.Body;
        auto so = m_ShapeOwner.find(entity);
        if (so != m_ShapeOwner.end())
        {
            auto ot = m_Bodies.find(so->second);
            if (ot != m_Bodies.end())
                return ot->second.Body;
        }
        return 0;
    }

    void PhysicsWorld3D::GatherShapes(uint32_t owner, uint32_t entity, const glm::mat4& ownerInv, std::vector<ShapeDesc>& shapes,
                                      std::vector<uint32_t>& shapeEntities, bool isRoot) const
    {
        Entity e{ (entt::entity)entity, m_Scene };
        if (!Valid(entity) || !m_Scene->IsEntityActive((entt::entity)entity))
            return;
        if (!isRoot && (e.HasComponent<RigidbodyComponent>() || e.HasComponent<CharacterControllerComponent>()))
            return;

        glm::vec3 t, s;
        glm::quat q;
        Decompose(ownerInv * WorldMatrix(entity), t, q, s);
        const glm::vec3 as = glm::abs(s);
        size_t before = shapes.size();

        if (e.HasComponent<BoxColliderComponent>())
        {
            auto& c = e.GetComponent<BoxColliderComponent>();
            ShapeDesc d;
            d.Type = ShapeType::Box;
            d.HalfExtents = glm::abs(c.Size * as) * 0.5f;
            d.Offset = t + q * (c.Offset * s);
            d.Rotation = q;
            d.IsTrigger = c.IsTrigger;
            d.SourceEntity = entity;
            shapes.push_back(std::move(d));
        }
        if (e.HasComponent<SphereColliderComponent>())
        {
            auto& c = e.GetComponent<SphereColliderComponent>();
            ShapeDesc d;
            d.Type = ShapeType::Sphere;
            d.Radius = std::fabs(c.Radius) * std::max(as.x, std::max(as.y, as.z));
            d.Offset = t + q * (c.Offset * s);
            d.Rotation = q;
            d.IsTrigger = c.IsTrigger;
            d.SourceEntity = entity;
            shapes.push_back(std::move(d));
        }
        if (e.HasComponent<CapsuleColliderComponent>())
        {
            auto& c = e.GetComponent<CapsuleColliderComponent>();
            int dir = std::clamp(c.Direction, 0, 2);
            float radialScale = dir == 0 ? std::max(as.y, as.z) : dir == 1 ? std::max(as.x, as.z) : std::max(as.x, as.y);
            float radius = std::fabs(c.Radius) * radialScale;
            float height = std::fabs(c.Height) * as[dir];
            ShapeDesc d;
            d.Type = ShapeType::Capsule;
            d.Radius = radius;
            d.HalfHeight = std::max(0.0f, height * 0.5f - radius);
            d.Offset = t + q * (c.Offset * s);
            d.Rotation = q * AxisRotation(dir);
            d.IsTrigger = c.IsTrigger;
            d.SourceEntity = entity;
            shapes.push_back(std::move(d));
        }
        if (e.HasComponent<MeshColliderComponent>())
        {
            auto& c = e.GetComponent<MeshColliderComponent>();
            ShapeDesc d;
            d.Type = c.IsConvex ? ShapeType::ConvexHull : ShapeType::TriangleMesh;
            d.IsTrigger = c.IsTrigger;
            d.SourceEntity = entity;
            auto append = [&](const Mesh& mesh) {
                uint32_t base = (uint32_t)d.Vertices.size();
                for (const auto& v : mesh.GetVertices())
                    d.Vertices.push_back(t + q * (v.Position * s));
                if (!c.IsConvex)
                    for (uint32_t idx : mesh.GetIndices())
                        d.Indices.push_back(base + idx);
            };
            if (e.HasComponent<MeshRendererComponent>() && e.GetComponent<MeshRendererComponent>().ModelData)
            {
                for (auto& m : e.GetComponent<MeshRendererComponent>().ModelData->Meshes)
                    if (m) append(*m);
            }
            else if (e.HasComponent<MeshRendererComponent>() && !e.GetComponent<MeshRendererComponent>().IsModel())
            {
                // Özel mesh (kırılma parçası, prosedürel) varsa o; yoksa ilkel şekil
                const auto& mr = e.GetComponent<MeshRendererComponent>();
                if (mr.CustomMesh)
                    append(*mr.CustomMesh);
                else if (auto mesh = PrimitiveMesh(mr.Type))
                    append(*mesh);
            }
            if (d.Vertices.size() >= 4)
                shapes.push_back(std::move(d));
            else
                CQ_CORE_WARN("[Physics] Mesh Collider on '{0}' has no mesh (add a Mesh Renderer).", e.GetComponent<TagComponent>().Tag);
        }
        if (e.HasComponent<TerrainComponent>() && e.HasComponent<TerrainColliderComponent>())
        {
            auto& terrain = e.GetComponent<TerrainComponent>();
            TerrainSystem::EnsureData(terrain);
            ShapeDesc d;
            d.Type = ShapeType::HeightField;
            d.HFRows = d.HFCols = terrain.Resolution;
            d.HFHeights = terrain.Heights;
            d.HFSize = glm::vec3(terrain.Width * as.x, terrain.Height * as.y, terrain.Length * as.z);
            d.Offset = t;
            d.Rotation = q;
            d.SourceEntity = entity;
            if (d.HFRows >= 2 && (int)d.HFHeights.size() >= d.HFRows * d.HFCols)
                shapes.push_back(std::move(d));
        }
        if (shapes.size() > before)
            shapeEntities.push_back(entity);

        // Yalnızca Rigidbody sahipleri çocuk çarpıştırıcıları toplar (statikler kendi gövdesini alır)
        Entity ownerEntity{ (entt::entity)owner, m_Scene };
        if (ownerEntity.HasComponent<RigidbodyComponent>())
            for (Entity child : e.GetChildren())
                GatherShapes(owner, (uint32_t)child, ownerInv, shapes, shapeEntities, false);
    }

    bool PhysicsWorld3D::BuildBodyDesc(uint32_t owner, BodyDesc& desc, std::vector<uint32_t>& shapeEntities) const
    {
        Entity o{ (entt::entity)owner, m_Scene };
        const PhysicsSettings& settings = PhysicsSettings::Get();
        glm::vec3 p, s;
        glm::quat r;
        DecomposeWorld(owner, p, r, s);
        desc = BodyDesc{};
        desc.Entity = owner;
        desc.Position = p;
        desc.Rotation = r;
        desc.Layer = LayerOf(o);
        glm::mat4 ownerInv = glm::inverse(glm::translate(glm::mat4(1.0f), p) * glm::toMat4(r));
        GatherShapes(owner, owner, ownerInv, desc.Shapes, shapeEntities, true);
        if (desc.Shapes.empty())
            return false;
        if (o.HasComponent<RigidbodyComponent>())
        {
            auto& rb = o.GetComponent<RigidbodyComponent>();
            desc.Motion = rb.Type == RigidbodyComponent::BodyType::Static ? MotionType::Static
                        : rb.Type == RigidbodyComponent::BodyType::Kinematic ? MotionType::Kinematic : MotionType::Dynamic;
            desc.Mass = std::max(0.0001f, rb.Mass);
            desc.LinearDamping = std::max(0.0f, rb.LinearDrag);
            desc.AngularDamping = std::max(0.0f, rb.AngularDrag);
            desc.GravityFactor = rb.UseGravity ? rb.GravityScale : 0.0f;
            desc.Friction = std::max(0.0f, rb.Friction);
            desc.Restitution = std::clamp(rb.Restitution, 0.0f, 1.0f);
            desc.Continuous = rb.CollisionDetection == RigidbodyComponent::CollisionDetectionMode::Continuous;
            desc.MaxAngularVelocity = rb.MaxAngularVelocity;
            desc.AllowSleep = rb.AllowSleep;
            uint32_t locks = 0;
            if (rb.FreezePositionX) locks |= LockPosX;
            if (rb.FreezePositionY) locks |= LockPosY;
            if (rb.FreezePositionZ) locks |= LockPosZ;
            if (rb.FreezeRotation || rb.FreezeRotationX) locks |= LockRotX;
            if (rb.FreezeRotation || rb.FreezeRotationY) locks |= LockRotY;
            if (rb.FreezeRotation || rb.FreezeRotationZ) locks |= LockRotZ;
            desc.Locks = locks;
        }
        else
        {
            desc.Motion = MotionType::Static;
            desc.Friction = settings.DefaultFriction;
            desc.Restitution = settings.DefaultRestitution;
        }
        // Çarpıştırıcının fizik malzemesi (Unity Collider.material) Rigidbody / varsayılan değerlerin yerine geçer
        {
            std::string pm;
            if (o.HasComponent<BoxColliderComponent>()) pm = o.GetComponent<BoxColliderComponent>().PhysicsMaterial;
            if (pm.empty() && o.HasComponent<SphereColliderComponent>()) pm = o.GetComponent<SphereColliderComponent>().PhysicsMaterial;
            if (pm.empty() && o.HasComponent<CapsuleColliderComponent>()) pm = o.GetComponent<CapsuleColliderComponent>().PhysicsMaterial;
            if (pm.empty() && o.HasComponent<MeshColliderComponent>()) pm = o.GetComponent<MeshColliderComponent>().PhysicsMaterial;
            if (pm.empty() && o.HasComponent<TerrainColliderComponent>()) pm = o.GetComponent<TerrainColliderComponent>().PhysicsMaterial;
            PhysicsMaterialAsset mat;
            if (!pm.empty() && PhysicsMaterials::Load(pm, mat))
            {
                desc.Friction = mat.Friction;
                desc.Restitution = mat.Bounciness;
            }
        }
        return true;
    }

    size_t PhysicsWorld3D::ComputeSignature(uint32_t owner) const
    {
        size_t h = 0;
        Entity o{ (entt::entity)owner, m_Scene };
        HashCombine(h, (size_t)LayerOf(o));
        HashCombine(h, (size_t)m_Scene->IsEntityActive((entt::entity)owner));
        if (o.HasComponent<RigidbodyComponent>())
        {
            auto& rb = o.GetComponent<RigidbodyComponent>();
            HashCombine(h, (size_t)rb.Type + 7);
            HashF(h, rb.Mass); HashF(h, rb.LinearDrag); HashF(h, rb.AngularDrag); HashF(h, rb.GravityScale);
            HashF(h, rb.Friction); HashF(h, rb.Restitution); HashF(h, rb.MaxAngularVelocity);
            HashCombine(h, (size_t)rb.UseGravity | ((size_t)rb.FreezeRotation << 1) | ((size_t)rb.FreezePositionX << 2) | ((size_t)rb.FreezePositionY << 3)
                           | ((size_t)rb.FreezePositionZ << 4) | ((size_t)rb.FreezeRotationX << 5) | ((size_t)rb.FreezeRotationY << 6)
                           | ((size_t)rb.FreezeRotationZ << 7) | ((size_t)rb.CollisionDetection << 8) | ((size_t)rb.AllowSleep << 9) | ((size_t)rb.Interpolate << 10));
        }
        // Fizik malzemesi: atanınca / değişince gövde yeniden kurulur
        {
            PhysicsMaterialAsset pm;
            std::string path;
            if (o.HasComponent<BoxColliderComponent>()) path = o.GetComponent<BoxColliderComponent>().PhysicsMaterial;
            if (path.empty() && o.HasComponent<SphereColliderComponent>()) path = o.GetComponent<SphereColliderComponent>().PhysicsMaterial;
            if (path.empty() && o.HasComponent<CapsuleColliderComponent>()) path = o.GetComponent<CapsuleColliderComponent>().PhysicsMaterial;
            if (path.empty() && o.HasComponent<MeshColliderComponent>()) path = o.GetComponent<MeshColliderComponent>().PhysicsMaterial;
            if (path.empty() && o.HasComponent<TerrainColliderComponent>()) path = o.GetComponent<TerrainColliderComponent>().PhysicsMaterial;
            if (!path.empty())
            {
                HashCombine(h, std::hash<std::string>()(path));
                if (PhysicsMaterials::Load(path, pm)) { HashF(h, pm.Friction); HashF(h, pm.Bounciness); }
            }
        }
        // Şekil katkısı olan varlıklar (sahip + Rigidbody'li ise çocuklar)
        glm::vec3 p, s;
        glm::quat r;
        DecomposeWorld(owner, p, r, s);
        glm::mat4 ownerInv = glm::inverse(glm::translate(glm::mat4(1.0f), p) * glm::toMat4(r));
        std::function<void(uint32_t, bool)> walk = [&](uint32_t entity, bool root) {
            Entity e{ (entt::entity)entity, m_Scene };
            if (!Valid(entity)) return;
            if (!root && (e.HasComponent<RigidbodyComponent>() || e.HasComponent<CharacterControllerComponent>())) return;
            HashCombine(h, (size_t)m_Scene->IsEntityActive((entt::entity)entity));
            bool any = false;
            if (e.HasComponent<BoxColliderComponent>()) { auto& c = e.GetComponent<BoxColliderComponent>(); HashV(h, c.Size); HashV(h, c.Offset); HashCombine(h, c.IsTrigger + 11); any = true; }
            if (e.HasComponent<SphereColliderComponent>()) { auto& c = e.GetComponent<SphereColliderComponent>(); HashF(h, c.Radius); HashV(h, c.Offset); HashCombine(h, c.IsTrigger + 13); any = true; }
            if (e.HasComponent<CapsuleColliderComponent>()) { auto& c = e.GetComponent<CapsuleColliderComponent>(); HashF(h, c.Radius); HashF(h, c.Height); HashV(h, c.Offset); HashCombine(h, (size_t)c.Direction * 3 + c.IsTrigger + 17); any = true; }
            if (e.HasComponent<MeshColliderComponent>())
            {
                auto& c = e.GetComponent<MeshColliderComponent>();
                HashCombine(h, c.IsConvex * 2 + c.IsTrigger + 19);
                if (e.HasComponent<MeshRendererComponent>())
                {
                    HashCombine(h, (size_t)e.GetComponent<MeshRendererComponent>().Type + 23);
                    HashCombine(h, std::hash<const void*>()(e.GetComponent<MeshRendererComponent>().CustomMesh.get()));
                    HashCombine(h, std::hash<const void*>()(e.GetComponent<MeshRendererComponent>().ModelData.get()));
                }
                any = true;
            }
            if (e.HasComponent<TerrainComponent>())
            {
                auto& tc = e.GetComponent<TerrainComponent>();
                HashCombine(h, (size_t)e.HasComponent<TerrainColliderComponent>() + 29);
                HashF(h, tc.Width); HashF(h, tc.Length); HashF(h, tc.Height); HashCombine(h, (size_t)tc.Resolution);
                any = true;
            }
            if (any && !root)
            {
                glm::vec3 t, sc;
                glm::quat q;
                Decompose(ownerInv * WorldMatrix(entity), t, q, sc);
                // Dönüş taban vektörleriyle (q ile -q aynı dönüş: 180 derecede işaret gürültüyle değişip gövdeyi her adımda
                // yeniden kurduruyordu, ör. dönen pervanenin kanatları)
                HashV(h, t); HashV(h, sc); HashV(h, q * glm::vec3(1, 0, 0)); HashV(h, q * glm::vec3(0, 1, 0));
            }
            if (root)
                HashV(h, s); // sahip ölçeği şekil boyutlarını etkiler
            if (o.HasComponent<RigidbodyComponent>())
                for (Entity child : e.GetChildren())
                    walk((uint32_t)child, false);
        };
        walk(owner, true);
        return h;
    }

    void PhysicsWorld3D::RemoveOwner(uint32_t owner)
    {
        auto it = m_Bodies.find(owner);
        if (it == m_Bodies.end())
            return;
        if (m_Backend)
        {
            if (it->second.Body) m_Backend->DestroyBody(it->second.Body);
            if (it->second.Sensor) m_Backend->DestroyBody(it->second.Sensor);
        }
        for (auto so = m_ShapeOwner.begin(); so != m_ShapeOwner.end();)
        {
            if (so->second == owner) so = m_ShapeOwner.erase(so);
            else ++so;
        }
        if (Valid(owner))
        {
            Entity o{ (entt::entity)owner, m_Scene };
            if (o.HasComponent<RigidbodyComponent>())
                o.GetComponent<RigidbodyComponent>().RuntimeBody = nullptr;
            if (o.HasComponent<TerrainComponent>())
                o.GetComponent<TerrainComponent>().RuntimeBody = nullptr;
        }
        m_Bodies.erase(it);
    }

    void PhysicsWorld3D::RebuildOwner(uint32_t owner, bool preserveVelocity)
    {
        glm::vec3 lin(0.0f), ang(0.0f);
        auto old = m_Bodies.find(owner);
        if (old != m_Bodies.end() && preserveVelocity && old->second.Motion == MotionType::Dynamic)
        {
            lin = m_Backend->GetLinearVelocity(old->second.Body);
            ang = m_Backend->GetAngularVelocity(old->second.Body);
        }
        RemoveOwner(owner);
        if (!Valid(owner))
            return;
        BodyDesc desc;
        std::vector<uint32_t> shapeEntities;
        if (!BuildBodyDesc(owner, desc, shapeEntities))
            return;
        desc.LinearVelocity = lin;
        desc.AngularVelocity = ang;

        // Katı + tetikleyici karışıksa tetikleyiciler ayrı kinematik algılayıcıya
        BodyDesc sensorDesc;
        bool hasSolid = false, hasTrigger = false;
        for (auto& s : desc.Shapes) (s.IsTrigger ? hasTrigger : hasSolid) = true;
        if (hasSolid && hasTrigger)
        {
            sensorDesc = desc;
            sensorDesc.Shapes.clear();
            std::vector<ShapeDesc> solid;
            for (auto& s : desc.Shapes)
                (s.IsTrigger ? sensorDesc.Shapes : solid).push_back(s);
            desc.Shapes = std::move(solid);
            sensorDesc.Motion = MotionType::Kinematic;
            sensorDesc.LinearVelocity = sensorDesc.AngularVelocity = glm::vec3(0.0f);
        }

        BodyRecord rec;
        rec.Body = m_Backend->CreateBody(desc);
        if (!rec.Body)
            return;
        if (!sensorDesc.Shapes.empty())
            rec.Sensor = m_Backend->CreateBody(sensorDesc);
        rec.Motion = desc.Motion;
        Entity o{ (entt::entity)owner, m_Scene };
        rec.Interpolate = desc.Motion == MotionType::Dynamic && PhysicsSettings::Get().Interpolation &&
                          (!o.HasComponent<RigidbodyComponent>() || o.GetComponent<RigidbodyComponent>().Interpolate);
        rec.PrevPos = rec.CurrPos = rec.LastWrittenPos = desc.Position;
        rec.PrevRot = rec.CurrRot = rec.LastWrittenRot = desc.Rotation;
        rec.ShapeEntities = shapeEntities;
        rec.Signature = ComputeSignature(owner);
        for (uint32_t se : shapeEntities)
        {
            m_ShapeOwner[se] = owner;
            if (se != owner && m_Bodies.count(se))
                RemoveOwner(se);
            m_ShapeOwner[se] = owner;
        }
        if (o.HasComponent<RigidbodyComponent>())
            o.GetComponent<RigidbodyComponent>().RuntimeBody = reinterpret_cast<void*>((uintptr_t)rec.Body);
        if (o.HasComponent<TerrainComponent>())
            o.GetComponent<TerrainComponent>().RuntimeBody = reinterpret_cast<void*>((uintptr_t)rec.Body);
        m_Bodies[owner] = std::move(rec);
    }

    // ── Karakterler ──
    size_t PhysicsWorld3D::ComputeCharacterSignature(uint32_t entity) const
    {
        Entity e{ (entt::entity)entity, m_Scene };
        if (!e.HasComponent<CharacterControllerComponent>())
            return 0;
        auto& c = e.GetComponent<CharacterControllerComponent>();
        size_t h = 1;
        HashF(h, c.SlopeLimit); HashF(h, c.StepOffset); HashF(h, c.SkinWidth); HashV(h, c.Center);
        HashF(h, c.Radius); HashF(h, c.Height); HashF(h, c.Mass); HashCombine(h, c.PushRigidbodies);
        HashCombine(h, (size_t)LayerOf(e));
        glm::vec3 p, s;
        glm::quat r;
        DecomposeWorld(entity, p, r, s);
        HashV(h, s);
        return h;
    }

    void PhysicsWorld3D::RemoveCharacter(uint32_t entity)
    {
        auto it = m_Characters.find(entity);
        if (it == m_Characters.end())
            return;
        if (m_Backend)
            m_Backend->DestroyCharacter(it->second.Character);
        m_Characters.erase(it);
    }

    void PhysicsWorld3D::RebuildCharacter(uint32_t entity)
    {
        RemoveCharacter(entity);
        Entity e{ (entt::entity)entity, m_Scene };
        if (!Valid(entity) || !e.HasComponent<CharacterControllerComponent>() || !m_Scene->IsEntityActive((entt::entity)entity))
            return;
        auto& c = e.GetComponent<CharacterControllerComponent>();
        glm::vec3 p, s;
        glm::quat r;
        DecomposeWorld(entity, p, r, s);
        CharacterDesc d;
        d.Entity = entity;
        d.Position = p;
        d.Rotation = glm::quat(1, 0, 0, 0);
        float radial = std::max(std::fabs(s.x), std::fabs(s.z));
        d.Center = c.Center * s;
        d.Radius = std::max(0.01f, c.Radius * radial);
        d.Height = std::max(d.Radius * 2.0f, c.Height * std::fabs(s.y));
        d.SlopeLimit = glm::radians(std::clamp(c.SlopeLimit, 0.0f, 89.0f));
        d.StepOffset = std::max(0.0f, c.StepOffset);
        d.SkinWidth = std::max(0.001f, c.SkinWidth);
        d.Mass = c.Mass;
        d.PushRigidbodies = c.PushRigidbodies;
        d.Layer = LayerOf(e);
        CharacterRecord rec;
        rec.Character = m_Backend->CreateCharacter(d);
        if (!rec.Character)
            return;
        rec.LastWrittenPos = p;
        rec.Signature = ComputeCharacterSignature(entity);
        m_Characters[entity] = rec;
    }

    // ── Eklemler ──
    namespace
    {
        constexpr int kJointFixed = 0, kJointHinge = 1, kJointSlider = 2, kJointSpring = 3, kJointCharacter = 4, kJointBall = 5, kJointConfigurable = 6;

        bool HasAnyJoint(Entity e)
        {
            return e.HasComponent<FixedJointComponent>() || e.HasComponent<HingeJointComponent>() || e.HasComponent<SliderJointComponent>() ||
                   e.HasComponent<SpringJointComponent>() || e.HasComponent<CharacterJointComponent>() || e.HasComponent<BallJointComponent>() ||
                   e.HasComponent<ConfigurableJointComponent>();
        }

        // Eklemin geometrisi (dinlenme çerçevesini belirleyen alanlar): bağlı gövde, çapa, eksenler. Limit / yay / motor
        // değişiklikleri çerçeveyi yeniden almaz (sıfır açı / konum ilk kurulumdaki gibi kalır).
        size_t JointGeometrySignature(Entity e, int kind)
        {
            size_t h = 17 + (size_t)kind;
            auto common = [&](const JointCommon& j) { HashCombine(h, std::hash<uint64_t>()(j.ConnectedBody)); HashV(h, j.Anchor); };
            switch (kind)
            {
                case kJointFixed: common(e.GetComponent<FixedJointComponent>()); break;
                case kJointHinge: { auto& j = e.GetComponent<HingeJointComponent>(); common(j); HashV(h, j.Axis); break; }
                case kJointSlider: { auto& j = e.GetComponent<SliderJointComponent>(); common(j); HashV(h, j.Axis); break; }
                case kJointSpring:
                {
                    auto& j = e.GetComponent<SpringJointComponent>();
                    common(j); HashV(h, j.ConnectedAnchor); HashCombine(h, (size_t)j.AutoConfigureConnectedAnchor + 3);
                    HashF(h, j.MaxDistance);
                    break;
                }
                case kJointCharacter: { auto& j = e.GetComponent<CharacterJointComponent>(); common(j); HashV(h, j.Axis); HashV(h, j.SwingAxis); break; }
                case kJointBall: common(e.GetComponent<BallJointComponent>()); break;
                case kJointConfigurable: { auto& j = e.GetComponent<ConfigurableJointComponent>(); common(j); HashV(h, j.Axis); HashV(h, j.SecondaryAxis); break; }
                default: break;
            }
            return h;
        }

        // Varlığın eklemlerinin bağlı olduğu varlıklar (UUID -> varlık)
        template<typename F>
        void ForEachConnected(Scene* scene, Entity e, F&& fn)
        {
            auto visit = [&](const JointCommon& j) {
                if (!j.ConnectedBody) return;
                if (Entity other = scene->FindEntityByUUID(j.ConnectedBody)) fn((uint32_t)other);
            };
            if (e.HasComponent<FixedJointComponent>()) visit(e.GetComponent<FixedJointComponent>());
            if (e.HasComponent<HingeJointComponent>()) visit(e.GetComponent<HingeJointComponent>());
            if (e.HasComponent<SliderJointComponent>()) visit(e.GetComponent<SliderJointComponent>());
            if (e.HasComponent<SpringJointComponent>()) visit(e.GetComponent<SpringJointComponent>());
            if (e.HasComponent<CharacterJointComponent>()) visit(e.GetComponent<CharacterJointComponent>());
            if (e.HasComponent<BallJointComponent>()) visit(e.GetComponent<BallJointComponent>());
            if (e.HasComponent<ConfigurableJointComponent>()) visit(e.GetComponent<ConfigurableJointComponent>());
        }

        AxisMotion ToAxisMotion(JointMotion m)
        {
            return m == JointMotion::Free ? AxisMotion::Free : (m == JointMotion::Limited ? AxisMotion::Limited : AxisMotion::Locked);
        }
    }

    size_t PhysicsWorld3D::ComputeJointSignature(uint32_t entity) const
    {
        Entity e{ (entt::entity)entity, m_Scene };
        size_t h = 3;
        auto common = [&](const JointCommon& j) {
            HashCombine(h, std::hash<uint64_t>()(j.ConnectedBody));
            HashV(h, j.Anchor); HashF(h, j.BreakForce); HashF(h, j.BreakTorque);
            HashCombine(h, j.EnableCollision * 2 + j.Broken);
            Entity other = j.ConnectedBody ? m_Scene->FindEntityByUUID(j.ConnectedBody) : Entity{};
            HashCombine(h, (size_t)BodyOf(other ? (uint32_t)other : NoEntity));
        };
        if (e.HasComponent<FixedJointComponent>()) { common(e.GetComponent<FixedJointComponent>()); HashCombine(h, 101); }
        if (e.HasComponent<HingeJointComponent>())
        {
            auto& j = e.GetComponent<HingeJointComponent>();
            // Motor alanları imzada yok (yaylı menteşe hariç): oyun sırasında değişince eklem yeniden kurulmaz, her adımda
            // canlı uygulanır (SyncJointMotors). Yeniden kurulum eklemi o anki pozdan başlatır: limitler / referans açı kayardı.
            common(j); HashV(h, j.Axis); HashCombine(h, j.UseLimits + j.UseSpring * 2 + (j.UseSpring ? j.UseMotor * 4 : 0) + 103);
            HashF(h, j.MinAngle); HashF(h, j.MaxAngle); HashF(h, j.Spring); HashF(h, j.Damper); HashF(h, j.TargetAngle);
        }
        if (e.HasComponent<SliderJointComponent>())
        {
            auto& j = e.GetComponent<SliderJointComponent>();
            common(j); HashV(h, j.Axis); HashCombine(h, j.UseLimits + 107);
            HashF(h, j.MinDistance); HashF(h, j.MaxDistance);
        }
        if (e.HasComponent<SpringJointComponent>())
        {
            auto& j = e.GetComponent<SpringJointComponent>();
            common(j); HashV(h, j.ConnectedAnchor); HashCombine(h, j.AutoConfigureConnectedAnchor + 109);
            HashF(h, j.Spring); HashF(h, j.Damper); HashF(h, j.MinDistance); HashF(h, j.MaxDistance);
        }
        if (e.HasComponent<CharacterJointComponent>())
        {
            auto& j = e.GetComponent<CharacterJointComponent>();
            common(j); HashV(h, j.Axis); HashV(h, j.SwingAxis); HashCombine(h, 113);
            HashF(h, j.LowTwistLimit); HashF(h, j.HighTwistLimit); HashF(h, j.Swing1Limit); HashF(h, j.Swing2Limit);
        }
        if (e.HasComponent<BallJointComponent>()) { common(e.GetComponent<BallJointComponent>()); HashCombine(h, 127); }
        if (e.HasComponent<ConfigurableJointComponent>())
        {
            // Sürücü hedefleri imzada yok: her adımda SetJointTargets ile güncellenir (eklem yeniden kurulmaz)
            auto& j = e.GetComponent<ConfigurableJointComponent>();
            common(j); HashV(h, j.Axis); HashV(h, j.SecondaryAxis); HashCombine(h, 131);
            HashCombine(h, (size_t)j.XMotion + 3 * (size_t)j.YMotion + 9 * (size_t)j.ZMotion + 27 * (size_t)j.AngularXMotion
                         + 81 * (size_t)j.AngularYMotion + 243 * (size_t)j.AngularZMotion);
            HashF(h, j.LinearLimit); HashF(h, j.LinearLimitSpring); HashF(h, j.LinearLimitDamper);
            HashF(h, j.LowAngularXLimit); HashF(h, j.HighAngularXLimit); HashF(h, j.AngularYLimit); HashF(h, j.AngularZLimit);
            HashF(h, j.XDriveSpring); HashF(h, j.XDriveDamper); HashF(h, j.XDriveMaxForce);
            HashF(h, j.YDriveSpring); HashF(h, j.YDriveDamper); HashF(h, j.YDriveMaxForce);
            HashF(h, j.ZDriveSpring); HashF(h, j.ZDriveDamper); HashF(h, j.ZDriveMaxForce);
            HashF(h, j.AngularXDriveSpring); HashF(h, j.AngularXDriveDamper); HashF(h, j.AngularXDriveMaxForce);
            HashF(h, j.AngularYZDriveSpring); HashF(h, j.AngularYZDriveDamper); HashF(h, j.AngularYZDriveMaxForce);
        }
        HashCombine(h, (size_t)BodyOf(entity));
        return h;
    }

    void PhysicsWorld3D::RemoveJoints(uint32_t entity)
    {
        auto it = m_Joints.find(entity);
        if (it == m_Joints.end())
            return;
        if (m_Backend)
            for (auto& [kind, rec] : it->second)
                m_Backend->DestroyJoint(rec.Joint);
        m_Joints.erase(it);
    }

    void PhysicsWorld3D::RebuildJoints(uint32_t entity)
    {
        RemoveJoints(entity);
        if (!Valid(entity))
        {
            m_JointSignatures.erase(entity);
            return;
        }
        Entity e{ (entt::entity)entity, m_Scene };
        if (!HasAnyJoint(e))
        {
            m_JointSignatures.erase(entity);
            for (uint64_t k = 0; k < 8; ++k)
                m_JointRest.erase(((uint64_t)entity << 3) | k);
            return;
        }
        Handle bodyA = BodyOf(entity);
        const size_t signature = ComputeJointSignature(entity);
        // İmza her durumda izlenir: gövde sonradan oluşunca eklem kurulur
        m_JointSignatures[entity] = signature;
        if (!bodyA)
            return;
        glm::mat4 world = WorldMatrix(entity);
        glm::vec3 ep, es;
        glm::quat er;
        Decompose(world, ep, er, es);
        // Gövdenin gerçek fizik pozu (varlık dönüşümü enterpolasyon yüzünden bir adım geride olabilir; çapa kayardı)
        m_Backend->GetBodyTransform(bodyA, ep, er);
        world = glm::translate(glm::mat4(1.0f), ep) * glm::toMat4(er) * glm::scale(glm::mat4(1.0f), es);
        std::vector<std::pair<int, JointRecord>> records;

        auto setup = [&](const JointCommon& j, JointDesc& d) -> bool {
            if (j.Broken)
                return false;
            d.BodyA = bodyA;
            d.AnchorWorld = glm::vec3(world * glm::vec4(j.Anchor, 1.0f));
            d.EnableCollision = j.EnableCollision;
            d.BreakForce = j.BreakForce;
            d.BreakTorque = j.BreakTorque;
            if (j.ConnectedBody)
            {
                Entity other = m_Scene->FindEntityByUUID(j.ConnectedBody);
                d.BodyB = other ? BodyOf((uint32_t)other) : 0;
                if (other && !d.BodyB)
                    CQ_CORE_WARN("[Physics] Joint on '{0}': connected entity has no Rigidbody; attaching to the world.", e.GetComponent<TagComponent>().Tag);
            }
            return true;
        };
        auto add = [&](int kind, JointDesc d) {
            // Dinlenme çerçevesi: ilk kurulumda bağlı gövdenin (B) yerel uzayında saklanır, sonraki kurulumlarda geri
            // kullanılır. Böylece eklem, başka gövdeler yeniden kurulunca ya da ışınlamadan sonra o anki pozdan değil ilk
            // pozdan kurulur.
            const uint64_t key = ((uint64_t)entity << 3) | (uint64_t)kind;
            const size_t geometry = JointGeometrySignature(e, kind);
            glm::vec3 pB(0.0f);
            glm::quat rB(1.0f, 0.0f, 0.0f, 0.0f);
            if (d.BodyB)
                m_Backend->GetBodyTransform(d.BodyB, pB, rB);
            auto restIt = m_JointRest.find(key);
            if (restIt == m_JointRest.end() || restIt->second.Geometry != geometry || restIt->second.Connected != (d.BodyB != 0))
            {
                JointRest rest;
                rest.Geometry = geometry;
                rest.Connected = d.BodyB != 0;
                const glm::quat invB = glm::inverse(rB);
                const glm::vec3 point = d.Type == JointType::Spring ? d.ConnectedAnchorWorld : d.AnchorWorld;
                rest.Point = invB * (point - pB);
                rest.Axis = invB * d.AxisWorld;
                rest.Normal = invB * d.NormalWorld;
                rest.Secondary = invB * d.SixDOF.SecondaryAxisWorld;
                rest.RestLength = d.Type == JointType::Spring ? glm::distance(d.AnchorWorld, d.ConnectedAnchorWorld) : -1.0f;
                restIt = m_JointRest.insert_or_assign(key, rest).first;
            }
            const JointRest& rest = restIt->second;
            d.UseFrameB = true;
            d.AnchorWorldB = pB + rB * rest.Point;
            d.AxisWorldB = rB * rest.Axis;
            d.NormalWorldB = rB * rest.Normal;
            d.SecondaryAxisWorldB = rB * rest.Secondary;
            d.RestLength = rest.RestLength;
            if (d.Type == JointType::Spring)
                d.ConnectedAnchorWorld = d.AnchorWorldB;
            JointRecord rec;
            rec.Joint = m_Backend->CreateJoint(d);
            rec.Type = d.Type;
            rec.BreakForce = d.BreakForce;
            rec.BreakTorque = d.BreakTorque;
            rec.Signature = signature;
            if (rec.Joint)
                records.emplace_back(kind, rec);
        };

        if (e.HasComponent<FixedJointComponent>())
        {
            JointDesc d;
            d.Type = JointType::Fixed;
            d.AxisWorld = er * glm::vec3(1, 0, 0);
            d.NormalWorld = er * glm::vec3(0, 1, 0);
            if (setup(e.GetComponent<FixedJointComponent>(), d)) add(kJointFixed, d);
        }
        if (e.HasComponent<HingeJointComponent>())
        {
            auto& j = e.GetComponent<HingeJointComponent>();
            JointDesc d;
            d.Type = JointType::Hinge;
            if (setup(j, d))
            {
                d.AxisWorld = er * (glm::length(j.Axis) > 1e-6f ? glm::normalize(j.Axis) : glm::vec3(1, 0, 0));
                d.NormalWorld = er * glm::vec3(0, 1, 0);
                d.UseLimits = j.UseLimits;
                d.LimitMin = glm::radians(j.MinAngle);
                d.LimitMax = glm::radians(j.MaxAngle);
                d.UseSpring = j.UseSpring;
                d.SpringStiffness = j.Spring;
                d.SpringDamping = j.Damper;
                d.SpringTarget = glm::radians(j.TargetAngle);
                d.UseMotor = j.UseMotor;
                d.MotorTargetVelocity = glm::radians(j.MotorTargetVelocity);
                d.MotorMaxForce = j.MotorForce;
                add(kJointHinge, d);
                if (!records.empty() && records.back().first == kJointHinge)
                    records.back().second.SetMotorState(j.UseMotor, j.MotorTargetVelocity, j.MotorForce);
            }
        }
        if (e.HasComponent<SliderJointComponent>())
        {
            auto& j = e.GetComponent<SliderJointComponent>();
            JointDesc d;
            d.Type = JointType::Slider;
            if (setup(j, d))
            {
                d.AxisWorld = er * (glm::length(j.Axis) > 1e-6f ? glm::normalize(j.Axis) : glm::vec3(1, 0, 0));
                d.NormalWorld = er * glm::vec3(0, 1, 0);
                d.UseLimits = j.UseLimits;
                d.LimitMin = j.MinDistance;
                d.LimitMax = j.MaxDistance;
                d.UseMotor = j.UseMotor;
                d.MotorTargetVelocity = j.MotorTargetSpeed;
                d.MotorMaxForce = j.MotorForce;
                add(kJointSlider, d);
                if (!records.empty() && records.back().first == kJointSlider)
                    records.back().second.SetMotorState(j.UseMotor, j.MotorTargetSpeed, j.MotorForce);
            }
        }
        if (e.HasComponent<SpringJointComponent>())
        {
            auto& j = e.GetComponent<SpringJointComponent>();
            JointDesc d;
            d.Type = JointType::Spring;
            if (setup(j, d))
            {
                if (j.AutoConfigureConnectedAnchor)
                {
                    // Unity: bağlı nokta başlangıçta çapanın dünya konumuna eşlenir. Bağlı gövde varsa onun kökeni (ip etkisi)
                    Entity other = j.ConnectedBody ? m_Scene->FindEntityByUUID(j.ConnectedBody) : Entity{};
                    d.ConnectedAnchorWorld = other ? glm::vec3(WorldMatrix((uint32_t)other)[3]) : d.AnchorWorld;
                }
                else
                {
                    Entity other = j.ConnectedBody ? m_Scene->FindEntityByUUID(j.ConnectedBody) : Entity{};
                    d.ConnectedAnchorWorld = other ? glm::vec3(WorldMatrix((uint32_t)other) * glm::vec4(j.ConnectedAnchor, 1.0f)) : j.ConnectedAnchor;
                }
                d.SpringStiffness = j.Spring;
                d.SpringDamping = j.Damper;
                d.MinDistance = j.MinDistance;
                d.MaxDistance = j.MaxDistance;
                d.UseSpring = true;
                add(kJointSpring, d);
            }
        }
        if (e.HasComponent<CharacterJointComponent>())
        {
            auto& j = e.GetComponent<CharacterJointComponent>();
            JointDesc d;
            d.Type = JointType::Character;
            if (setup(j, d))
            {
                d.AxisWorld = er * (glm::length(j.Axis) > 1e-6f ? glm::normalize(j.Axis) : glm::vec3(1, 0, 0));
                d.NormalWorld = er * (glm::length(j.SwingAxis) > 1e-6f ? glm::normalize(j.SwingAxis) : glm::vec3(0, 1, 0));
                d.TwistMin = glm::radians(j.LowTwistLimit);
                d.TwistMax = glm::radians(j.HighTwistLimit);
                d.Swing1 = glm::radians(j.Swing1Limit);
                d.Swing2 = glm::radians(j.Swing2Limit);
                add(kJointCharacter, d);
            }
        }
        if (e.HasComponent<BallJointComponent>())
        {
            JointDesc d;
            d.Type = JointType::Ball;
            if (setup(e.GetComponent<BallJointComponent>(), d)) add(kJointBall, d);
        }
        if (e.HasComponent<ConfigurableJointComponent>())
        {
            auto& j = e.GetComponent<ConfigurableJointComponent>();
            JointDesc d;
            d.Type = JointType::SixDOF;
            if (setup(j, d))
            {
                d.AxisWorld = er * (glm::length(j.Axis) > 1e-6f ? glm::normalize(j.Axis) : glm::vec3(1, 0, 0));
                d.NormalWorld = er * glm::vec3(0, 1, 0);
                SixDOFDesc& x = d.SixDOF;
                x.SecondaryAxisWorld = er * (glm::length(j.SecondaryAxis) > 1e-6f ? glm::normalize(j.SecondaryAxis) : glm::vec3(0, 1, 0));
                x.Linear[0] = ToAxisMotion(j.XMotion); x.Linear[1] = ToAxisMotion(j.YMotion); x.Linear[2] = ToAxisMotion(j.ZMotion);
                x.Angular[0] = ToAxisMotion(j.AngularXMotion); x.Angular[1] = ToAxisMotion(j.AngularYMotion); x.Angular[2] = ToAxisMotion(j.AngularZMotion);
                x.LinearLimit = j.LinearLimit;
                x.LinearLimitSpring = j.LinearLimitSpring;
                x.LinearLimitDamper = j.LinearLimitDamper;
                x.AngularXMin = glm::radians(j.LowAngularXLimit);
                x.AngularXMax = glm::radians(j.HighAngularXLimit);
                x.AngularYLimit = glm::radians(j.AngularYLimit);
                x.AngularZLimit = glm::radians(j.AngularZLimit);
                const float ls[3] = { j.XDriveSpring, j.YDriveSpring, j.ZDriveSpring };
                const float ld[3] = { j.XDriveDamper, j.YDriveDamper, j.ZDriveDamper };
                const float lf[3] = { j.XDriveMaxForce, j.YDriveMaxForce, j.ZDriveMaxForce };
                for (int i = 0; i < 3; ++i)
                {
                    x.LinearDriveSpring[i] = ls[i];
                    x.LinearDriveDamper[i] = ld[i];
                    x.LinearDriveMaxForce[i] = lf[i];
                    // Unity: X sürücüsü dönme X'i, YZ sürücüsü dönme Y ve Z'yi çeker
                    x.AngularDriveSpring[i] = i == 0 ? j.AngularXDriveSpring : j.AngularYZDriveSpring;
                    x.AngularDriveDamper[i] = i == 0 ? j.AngularXDriveDamper : j.AngularYZDriveDamper;
                    x.AngularDriveMaxForce[i] = i == 0 ? j.AngularXDriveMaxForce : j.AngularYZDriveMaxForce;
                }
                add(kJointConfigurable, d);
            }
        }
        if (!records.empty())
            m_Joints[entity] = std::move(records);
    }

    void PhysicsWorld3D::ProcessDirty()
    {
        if (!m_Backend || m_Dirty.empty())
            return;
        std::unordered_set<uint32_t> owners;
        std::vector<uint32_t> dirty(m_Dirty.begin(), m_Dirty.end());
        m_Dirty.clear();
        bool anyJointEntity = false;
        for (uint32_t e : dirty)
        {
            if (!Valid(e))
            {
                RemoveCharacter(e);
                RemoveJoints(e);
                auto so = m_ShapeOwner.find(e);
                if (so != m_ShapeOwner.end() && so->second != e)
                    owners.insert(so->second);
                RemoveOwner(e);
                continue;
            }
            Entity ent{ (entt::entity)e, m_Scene };
            const bool active = m_Scene->IsEntityActive((entt::entity)e);
            if (ent.HasComponent<CharacterControllerComponent>() && active)
                RebuildCharacter(e);
            else
                RemoveCharacter(e);

            uint32_t owner = FindOwner(e);
            if (owner != NoEntity)
                owners.insert(owner);
            if (m_Bodies.count(e) && owner != e)
                RemoveOwner(e);
            auto so = m_ShapeOwner.find(e);
            if (so != m_ShapeOwner.end() && so->second != owner && so->second != e)
                owners.insert(so->second);
            // Yeni Rigidbody: çocuk statik çarpıştırıcılar bu gövdeye geçer
            if (ent.HasComponent<RigidbodyComponent>())
                for (Entity child : ent.GetChildren())
                    if (m_Bodies.count((uint32_t)child) && FindOwner((uint32_t)child) != (uint32_t)child)
                        RemoveOwner((uint32_t)child);
            if (HasAnyJoint(ent) || m_Joints.count(e))
                anyJointEntity = true;
        }
        for (uint32_t owner : owners)
            RebuildOwner(owner, true);
        // Gövdeler yeniden kurulduysa (eklem tutamaçları geçersiz) tüm eklemleri yeniden kur
        if (!owners.empty() || anyJointEntity)
        {
            std::unordered_set<uint32_t> jointEntities;
            auto& reg = m_Scene->m_Registry;
            auto collect = [&](auto view) { for (auto e : view) jointEntities.insert((uint32_t)e); };
            collect(reg.view<FixedJointComponent>());
            collect(reg.view<HingeJointComponent>());
            collect(reg.view<SliderJointComponent>());
            collect(reg.view<SpringJointComponent>());
            collect(reg.view<CharacterJointComponent>());
            collect(reg.view<BallJointComponent>());
            collect(reg.view<ConfigurableJointComponent>());
            for (auto& [e, list] : m_Joints)
                jointEntities.insert(e);
            for (auto& [e, sig] : m_JointSignatures)
                jointEntities.insert(e);
            // Yalnızca etkilenen eklemler: varlığı kirli olanlar, kendi ya da bağlı gövdesi yeniden kurulanlar (eski gövdeyle
            // birlikte eklemi de söküldü), imzası değişenler. Sahnedeki tüm eklemleri yeniden kurmak (ör. bir kırılma parçası
            // ya da ragdoll oluşunca) zincirleri ve limitleri o anki pozdan yeniden başlatıyordu.
            std::unordered_set<uint32_t> dirtySet(dirty.begin(), dirty.end());
            for (uint32_t e : jointEntities)
            {
                auto sig = m_JointSignatures.find(e);
                bool affected = !Valid(e) || dirtySet.count(e) || sig == m_JointSignatures.end() || owners.count(FindOwner(e));
                if (!affected)
                    ForEachConnected(m_Scene, Entity{ (entt::entity)e, m_Scene }, [&](uint32_t other) {
                        if (owners.count(FindOwner(other)))
                            affected = true;
                    });
                if (!affected && ComputeJointSignature(e) != sig->second)
                    affected = true;
                if (affected)
                    RebuildJoints(e);
            }
        }
    }

    // ════════════════════════════════════════
    //  Adım
    // ════════════════════════════════════════
    void PhysicsWorld3D::SyncExternalChanges(float dt)
    {
        (void)dt;
        if (!PhysicsSettings::Get().AutoSyncTransforms)
            return;
        for (auto& [owner, rec] : m_Bodies)
        {
            if (rec.Motion == MotionType::Kinematic || !Valid(owner))
                continue;
            glm::vec3 p, s;
            glm::quat r;
            DecomposeWorld(owner, p, r, s);
            const bool posChanged = glm::distance(p, rec.LastWrittenPos) > 1e-4f;
            const bool rotChanged = std::fabs(glm::dot(r, rec.LastWrittenRot)) < 0.99999f;
            if (posChanged || rotChanged)
            {
                // Betik / denetçi dönüşümü değiştirdi: gövdeyi ışınla. Yalnızca dönüş değiştiyse (ör. Rotator)
                // gerçek simülasyon konumu korunur; enterpolasyonlu (geride kalan) konuma ışınlamak düşüşü yavaşlatır.
                const glm::vec3 target = posChanged ? p : rec.CurrPos;
                m_Backend->SetBodyTransform(rec.Body, target, r, false);
                if (rec.Sensor) m_Backend->SetBodyTransform(rec.Sensor, target, r, false);
                rec.PrevPos = rec.CurrPos = target;
                rec.LastWrittenPos = p;
                rec.PrevRot = rec.CurrRot = rec.LastWrittenRot = r;
            }
        }
        for (auto& [entity, rec] : m_Characters)
        {
            if (!Valid(entity)) continue;
            glm::vec3 p, s;
            glm::quat r;
            DecomposeWorld(entity, p, r, s);
            if (glm::distance(p, rec.LastWrittenPos) > 1e-4f)
            {
                m_Backend->SetCharacterPosition(rec.Character, p);
                rec.LastWrittenPos = p;
            }
        }
    }

    void PhysicsWorld3D::PreStep(float dt)
    {
        if (OnFixedUpdate)
            OnFixedUpdate(dt);
        ProcessDirty();
        DriveCloths(dt);
        // Kinematik gövdeler dönüşümü izler
        for (auto& [owner, rec] : m_Bodies)
        {
            if (!Valid(owner))
                continue;
            if (rec.Motion == MotionType::Kinematic)
            {
                glm::vec3 p, s;
                glm::quat r;
                DecomposeWorld(owner, p, r, s);
                m_Backend->MoveKinematic(rec.Body, p, r, dt);
                rec.PrevPos = rec.CurrPos;
                rec.PrevRot = rec.CurrRot;
                rec.CurrPos = rec.LastWrittenPos = p;
                rec.CurrRot = rec.LastWrittenRot = r;
            }
            if (rec.Sensor)
            {
                glm::vec3 p;
                glm::quat r;
                if (rec.Motion == MotionType::Dynamic)
                    m_Backend->GetBodyTransform(rec.Body, p, r);
                else
                {
                    glm::vec3 s;
                    DecomposeWorld(owner, p, r, s);
                }
                m_Backend->MoveKinematic(rec.Sensor, p, r, dt);
            }
        }
        // Constant Force
        auto view = m_Scene->m_Registry.view<ConstantForceComponent>();
        for (auto e : view)
        {
            auto it = m_Bodies.find((uint32_t)e);
            if (it == m_Bodies.end() || it->second.Motion != MotionType::Dynamic)
                continue;
            auto& cf = view.get<ConstantForceComponent>(e);
            glm::vec3 p;
            glm::quat r;
            m_Backend->GetBodyTransform(it->second.Body, p, r);
            glm::vec3 force = cf.Force + r * cf.RelativeForce;
            glm::vec3 torque = cf.Torque + r * cf.RelativeTorque;
            if (glm::dot(force, force) > 0.0f) m_Backend->AddForce(it->second.Body, force);
            if (glm::dot(torque, torque) > 0.0f) m_Backend->AddTorque(it->second.Body, torque);
        }
        UpdateWheels(dt);

        // Hinge / Slider motorları (JointSetMotor, denetçi): değişen hız / kuvvet / açık-kapalı canlı uygulanır
        for (auto& [entity, list] : m_Joints)
        {
            if (!Valid(entity))
                continue;
            Entity e{ (entt::entity)entity, m_Scene };
            for (auto& [kind, rec] : list)
            {
                bool on = false;
                float velocity = 0.0f, force = 0.0f;
                if (kind == kJointHinge && e.HasComponent<HingeJointComponent>())
                {
                    const auto& j = e.GetComponent<HingeJointComponent>();
                    if (j.UseSpring)
                        continue;   // yaylı menteşede motor konum (yay) kipinde kurulur; değişiklik yeniden kurulumla gelir
                    on = j.UseMotor; velocity = j.MotorTargetVelocity; force = j.MotorForce;
                }
                else if (kind == kJointSlider && e.HasComponent<SliderJointComponent>())
                {
                    const auto& j = e.GetComponent<SliderJointComponent>();
                    on = j.UseMotor; velocity = j.MotorTargetSpeed; force = j.MotorForce;
                }
                else
                    continue;
                if (!rec.Joint || (on == rec.MotorOn && velocity == rec.MotorVelocity && force == rec.MotorForce))
                    continue;
                m_Backend->SetJointMotor(rec.Joint, on, kind == kJointHinge ? glm::radians(velocity) : velocity, force);
                rec.SetMotorState(on, velocity, force);
            }
        }

        // Configurable Joint sürücü hedefleri (betikten her karede değişebilir; eklem yeniden kurulmaz)
        for (auto& [entity, list] : m_Joints)
        {
            Entity e{ (entt::entity)entity, m_Scene };
            if (!Valid(entity) || !e.HasComponent<ConfigurableJointComponent>())
                continue;
            const auto& cj = e.GetComponent<ConfigurableJointComponent>();
            JointTargets t;
            t.Position = cj.TargetPosition;
            t.Velocity = cj.TargetVelocity;
            t.Rotation = glm::quat(glm::radians(cj.TargetRotation));
            t.AngularVelocity = glm::radians(cj.TargetAngularVelocity);
            for (auto& [kind, rec] : list)
                if (kind == kJointConfigurable)
                    m_Backend->SetJointTargets(rec.Joint, t);
        }
    }

    void PhysicsWorld3D::PostStep(float dt)
    {
        (void)dt;
        for (auto& [owner, rec] : m_Bodies)
        {
            if (rec.Motion != MotionType::Dynamic)
                continue;
            rec.PrevPos = rec.CurrPos;
            rec.PrevRot = rec.CurrRot;
            m_Backend->GetBodyTransform(rec.Body, rec.CurrPos, rec.CurrRot);
        }
        // Eklem kırılması ve çalışma zamanı değerleri
        std::vector<std::pair<uint32_t, float>> broken;
        for (auto& [entity, list] : m_Joints)
        {
            if (!Valid(entity))
                continue;
            Entity e{ (entt::entity)entity, m_Scene };
            for (auto it = list.begin(); it != list.end();)
            {
                auto& [kind, rec] = *it;
                if (!rec.Joint) { ++it; continue; }
                if (kind == kJointHinge && e.HasComponent<HingeJointComponent>())
                    e.GetComponent<HingeJointComponent>().CurrentAngle = glm::degrees(m_Backend->GetJointPosition(rec.Joint));
                if (kind == kJointSlider && e.HasComponent<SliderJointComponent>())
                    e.GetComponent<SliderJointComponent>().CurrentPosition = m_Backend->GetJointPosition(rec.Joint);
                if (kind == kJointConfigurable && e.HasComponent<ConfigurableJointComponent>())
                {
                    auto& cj = e.GetComponent<ConfigurableJointComponent>();
                    glm::vec3 t, r;
                    if (m_Backend->GetJointPose(rec.Joint, t, r))
                    {
                        cj.CurrentPosition = t;
                        cj.CurrentRotation = glm::degrees(r);
                    }
                }
                if (rec.BreakForce > 0.0f || rec.BreakTorque > 0.0f)
                {
                    float force = 0.0f, torque = 0.0f;
                    m_Backend->GetJointReaction(rec.Joint, force, torque);
                    if ((rec.BreakForce > 0.0f && force > rec.BreakForce) || (rec.BreakTorque > 0.0f && torque > rec.BreakTorque))
                    {
                        m_Backend->DestroyJoint(rec.Joint);
                        JointCommon* c = nullptr;
                        switch (kind)
                        {
                            case kJointFixed: c = e.HasComponent<FixedJointComponent>() ? &e.GetComponent<FixedJointComponent>() : nullptr; break;
                            case kJointHinge: c = e.HasComponent<HingeJointComponent>() ? &e.GetComponent<HingeJointComponent>() : nullptr; break;
                            case kJointSlider: c = e.HasComponent<SliderJointComponent>() ? &e.GetComponent<SliderJointComponent>() : nullptr; break;
                            case kJointSpring: c = e.HasComponent<SpringJointComponent>() ? &e.GetComponent<SpringJointComponent>() : nullptr; break;
                            case kJointCharacter: c = e.HasComponent<CharacterJointComponent>() ? &e.GetComponent<CharacterJointComponent>() : nullptr; break;
                            case kJointBall: c = e.HasComponent<BallJointComponent>() ? &e.GetComponent<BallJointComponent>() : nullptr; break;
                            case kJointConfigurable: c = e.HasComponent<ConfigurableJointComponent>() ? &e.GetComponent<ConfigurableJointComponent>() : nullptr; break;
                        }
                        if (c) c->Broken = true;
                        broken.emplace_back(entity, std::max(force, torque));
                        it = list.erase(it);
                        continue;
                    }
                }
                ++it;
            }
            // Kırılan eklem bileşen imzasını değiştirir (Broken); yeni imzayı kaydet ki yeniden kurulmasın
            if (!broken.empty() && broken.back().first == entity)
                m_JointSignatures[entity] = ComputeJointSignature(entity);
        }
        for (auto& [entity, force] : broken)
        {
            CQ_CORE_INFO("[Physics] Joint broke on entity {0} (force {1:.1f})", entity, force);
            if (OnJointBreak)
                OnJointBreak(entity, force);
        }
        m_Backend->DrainContactEvents(m_PendingEvents);
    }

    void PhysicsWorld3D::WriteTransforms(float alpha)
    {
        for (auto& [owner, rec] : m_Bodies)
        {
            if (rec.Motion != MotionType::Dynamic || !Valid(owner))
                continue;
            glm::vec3 p = rec.Interpolate ? glm::mix(rec.PrevPos, rec.CurrPos, alpha) : rec.CurrPos;
            glm::quat r = rec.Interpolate ? glm::slerp(rec.PrevRot, rec.CurrRot, alpha) : rec.CurrRot;
            SetWorldPose(owner, p, r);
            rec.LastWrittenPos = p;
            rec.LastWrittenRot = r;
        }
    }

    // ════════════════════════════════════════
    //  Bildirimler / eski API
    // ════════════════════════════════════════
    void PhysicsWorld3D::MarkDirty(Entity entity)
    {
        if (entity && m_Backend)
            m_Dirty.insert((uint32_t)entity);
    }

    void PhysicsWorld3D::OnEntityDestroyed(Entity entity)
    {
        if (!m_Backend || !entity)
            return;
        uint32_t e = (uint32_t)entity;
        RemoveCharacter(e);
        RemoveJoints(e);
        m_JointSignatures.erase(e);
        for (uint64_t k = 0; k < 8; ++k)
            m_JointRest.erase(((uint64_t)e << 3) | k);
        auto so = m_ShapeOwner.find(e);
        if (so != m_ShapeOwner.end() && so->second != e)
            m_Dirty.insert(so->second);
        RemoveOwner(e);
        // Bu varlığa bağlı eklemler yeniden değerlendirilir
        for (auto& [je, sig] : m_JointSignatures)
            m_Dirty.insert(je);
    }

    void PhysicsWorld3D::CreateBody(Entity entity) { MarkDirty(entity); }
    void PhysicsWorld3D::DestroyBody(Entity entity) { if (m_Backend && entity) RemoveOwner((uint32_t)entity); }
    void PhysicsWorld3D::CreateTerrainBody(Entity entity) { MarkDirty(entity); }
    void PhysicsWorld3D::DestroyTerrainBody(Entity entity) { DestroyBody(entity); }

    void PhysicsWorld3D::SetGravity(const glm::vec3& gravity)
    {
        m_Gravity = gravity;
        if (m_Backend) m_Backend->SetGravity(gravity);
    }

    glm::vec3 PhysicsWorld3D::GetGravity() const { return m_Gravity; }

    void PhysicsWorld3D::ApplyForce(Entity e, const glm::vec3& f) { ProcessDirty(); if (Handle h = BodyOf((uint32_t)e)) m_Backend->AddForce(h, f); }
    void PhysicsWorld3D::ApplyImpulse(Entity e, const glm::vec3& i) { ProcessDirty(); if (Handle h = BodyOf((uint32_t)e)) m_Backend->AddImpulse(h, i); }
    void PhysicsWorld3D::ApplyTorque(Entity e, const glm::vec3& t) { ProcessDirty(); if (Handle h = BodyOf((uint32_t)e)) m_Backend->AddTorque(h, t); }
    void PhysicsWorld3D::ApplyTorqueImpulse(Entity e, const glm::vec3& t) { ProcessDirty(); if (Handle h = BodyOf((uint32_t)e)) m_Backend->AddAngularImpulse(h, t); }
    void PhysicsWorld3D::AddForceAtPosition(Entity e, const glm::vec3& f, const glm::vec3& p) { ProcessDirty(); if (Handle h = BodyOf((uint32_t)e)) m_Backend->AddForceAtPoint(h, f, p); }
    void PhysicsWorld3D::AddImpulseAtPosition(Entity e, const glm::vec3& i, const glm::vec3& p) { ProcessDirty(); if (Handle h = BodyOf((uint32_t)e)) m_Backend->AddImpulseAtPoint(h, i, p); }

    void PhysicsWorld3D::AddExplosionForce(float force, const glm::vec3& center, float radius, float upwardsModifier)
    {
        if (!m_Backend || radius <= 0.0f)
            return;
        ProcessDirty();
        for (auto& [owner, rec] : m_Bodies)
        {
            if (rec.Motion != MotionType::Dynamic)
                continue;
            glm::vec3 p;
            glm::quat r;
            m_Backend->GetBodyTransform(rec.Body, p, r);
            glm::vec3 from = center - glm::vec3(0.0f, upwardsModifier, 0.0f);
            glm::vec3 dir = p - from;
            float dist = glm::length(p - center);
            if (dist > radius)
                continue;
            float falloff = 1.0f - dist / radius;
            glm::vec3 n = glm::length(dir) > 1e-5f ? glm::normalize(dir) : glm::vec3(0, 1, 0);
            m_Backend->AddImpulse(rec.Body, n * force * falloff);
            m_Backend->WakeBody(rec.Body);
        }
    }

    void PhysicsWorld3D::SetLinearVelocity(Entity e, const glm::vec3& v) { ProcessDirty(); if (Handle h = BodyOf((uint32_t)e)) m_Backend->SetLinearVelocity(h, v); }
    glm::vec3 PhysicsWorld3D::GetLinearVelocity(Entity e) const
    {
        if (!m_Backend) return glm::vec3(0.0f);
        if (auto it = m_Characters.find((uint32_t)e); it != m_Characters.end())
            return m_Backend->GetCharacterState(it->second.Character).Velocity;
        Handle h = BodyOf((uint32_t)e);
        return h ? m_Backend->GetLinearVelocity(h) : glm::vec3(0.0f);
    }
    void PhysicsWorld3D::SetAngularVelocity(Entity e, const glm::vec3& v) { ProcessDirty(); if (Handle h = BodyOf((uint32_t)e)) m_Backend->SetAngularVelocity(h, v); }
    glm::vec3 PhysicsWorld3D::GetAngularVelocity(Entity e) const
    {
        Handle h = m_Backend ? BodyOf((uint32_t)e) : 0;
        return h ? m_Backend->GetAngularVelocity(h) : glm::vec3(0.0f);
    }
    float PhysicsWorld3D::GetMass(Entity e) const
    {
        Handle h = m_Backend ? BodyOf((uint32_t)e) : 0;
        return h ? m_Backend->GetMass(h) : 0.0f;
    }
    bool PhysicsWorld3D::IsSleeping(Entity e) const
    {
        Handle h = m_Backend ? BodyOf((uint32_t)e) : 0;
        return h ? m_Backend->IsBodySleeping(h) : false;
    }
    void PhysicsWorld3D::WakeUp(Entity e) { if (Handle h = m_Backend ? BodyOf((uint32_t)e) : 0) m_Backend->WakeBody(h); }

    void PhysicsWorld3D::Teleport(Entity entity, const glm::vec3& position, const glm::quat& rotation)
    {
        uint32_t e = (uint32_t)entity;
        SetWorldPose(e, position, rotation);
        if (!m_Backend)
            return;
        ProcessDirty();
        if (auto it = m_Bodies.find(e); it != m_Bodies.end())
        {
            m_Backend->SetBodyTransform(it->second.Body, position, rotation, false);
            if (it->second.Sensor) m_Backend->SetBodyTransform(it->second.Sensor, position, rotation, false);
            it->second.PrevPos = it->second.CurrPos = it->second.LastWrittenPos = position;
            it->second.PrevRot = it->second.CurrRot = it->second.LastWrittenRot = rotation;
        }
        if (auto it = m_Characters.find(e); it != m_Characters.end())
        {
            m_Backend->SetCharacterPosition(it->second.Character, position);
            it->second.LastWrittenPos = position;
        }
    }

    // ── Karakter ──
    bool PhysicsWorld3D::HasCharacter(Entity entity) const
    {
        return m_Characters.count((uint32_t)entity) != 0;
    }

    void PhysicsWorld3D::BeginFrame(Timestep ts)
    {
        const float dt = std::clamp(ts.GetSeconds(), 0.0f, 0.25f);
        m_ScriptDt = dt > 0.0f ? dt : m_FixedDt;
        ++m_FrameIndex;
    }

    void PhysicsWorld3D::SyncCharacterTeleport(uint32_t e, CharacterRecord& rec)
    {
        // Dışarıdan taşındıysa (betik / Inspector / yeniden doğma) fizik konumu ve ara değerleme o noktaya atlar
        glm::vec3 p, s;
        glm::quat r;
        DecomposeWorld(e, p, r, s);
        if (glm::distance(p, rec.LastWrittenPos) > 1e-4f)
        {
            m_Backend->SetCharacterPosition(rec.Character, p);
            rec.PrevPos = rec.CurrPos = p;
            rec.LastWrittenPos = p;
            rec.HasPose = true;
        }
    }

    CharacterState PhysicsWorld3D::MoveCharacter(Entity entity, const glm::vec3& displacement)
    {
        if (!m_Backend)
            return {};
        ProcessDirty();
        uint32_t e = (uint32_t)entity;
        auto it = m_Characters.find(e);
        if (it == m_Characters.end())
            return {};
        Entity ent{ (entt::entity)e, m_Scene };
        auto& cc = ent.GetComponent<CharacterControllerComponent>();
        SyncCharacterTeleport(e, it->second);
        // İstek bu karenin hızına çevrilir; hareket fizik adımlarında yapılır (aynı karede birden çok çağrı toplanır)
        if (glm::length(displacement) >= cc.MinMoveDistance)
        {
            if (it->second.RequestFrame != m_FrameIndex)
                it->second.RequestedVelocity = glm::vec3(0.0f);   // yeni karenin ilk isteği
            it->second.RequestFrame = m_FrameIndex;
            it->second.RequestedVelocity += displacement / std::max(m_ScriptDt, 1e-4f);
            it->second.Requested = true;
        }
        CharacterState st = m_Backend->GetCharacterState(it->second.Character);
        cc.IsGrounded = st.Grounded;
        cc.Velocity = st.Velocity;
        cc.CollisionFlags = (int)st.Flags;
        return st;
    }

    void PhysicsWorld3D::MoveCharactersFixed(float dt)
    {
        for (auto& [e, rec] : m_Characters)
        {
            if (!Valid(e) || !rec.Character)
                continue;
            SyncCharacterTeleport(e, rec);
            if (!rec.HasPose)
            {
                const CharacterState now = m_Backend->GetCharacterState(rec.Character);
                rec.PrevPos = rec.CurrPos = now.Position;
                rec.HasPose = true;
            }
            // Betik önceki karede (ya da bu karede) hareket istediyse; istemediyse karakter durur
            const bool active = rec.Requested && rec.RequestFrame + 1 >= m_FrameIndex;
            const CharacterState st = active ? m_Backend->MoveCharacter(rec.Character, rec.RequestedVelocity * dt, dt)
                                             : m_Backend->GetCharacterState(rec.Character);
            rec.PrevPos = rec.CurrPos;
            rec.CurrPos = st.Position;
            Entity ent{ (entt::entity)e, m_Scene };
            auto& cc = ent.GetComponent<CharacterControllerComponent>();
            cc.IsGrounded = st.Grounded;
            cc.Velocity = st.Velocity;
            cc.CollisionFlags = (int)st.Flags;
        }
    }

    void PhysicsWorld3D::WriteCharacterTransforms(float alpha)
    {
        const bool interpolate = PhysicsSettings::Get().Interpolation;
        for (auto& [e, rec] : m_Characters)
        {
            if (!Valid(e) || !rec.HasPose)
                continue;
            glm::vec3 p, s;
            glm::quat r;
            DecomposeWorld(e, p, r, s);
            const glm::vec3 pos = interpolate ? glm::mix(rec.PrevPos, rec.CurrPos, alpha) : rec.CurrPos;
            SetWorldPose(e, pos, r);
            rec.LastWrittenPos = pos;
        }
    }

    CharacterState PhysicsWorld3D::SimpleMoveCharacter(Entity entity, const glm::vec3& velocity)
    {
        auto it = m_Characters.find((uint32_t)entity);
        if (!m_Backend || it == m_Characters.end())
            return {};
        CharacterState prev = m_Backend->GetCharacterState(it->second.Character);
        float& vy = it->second.VerticalVelocity;
        vy = prev.Grounded ? -1.0f : vy + m_Gravity.y * m_ScriptDt;
        glm::vec3 disp(velocity.x * m_ScriptDt, vy * m_ScriptDt, velocity.z * m_ScriptDt);
        CharacterState st = MoveCharacter(entity, disp);
        if (st.Grounded) vy = -1.0f;
        if (st.Flags & CollidedAbove) vy = std::min(vy, 0.0f);
        return st;
    }

    CharacterState PhysicsWorld3D::GetCharacterState(Entity entity) const
    {
        auto it = m_Characters.find((uint32_t)entity);
        return (!m_Backend || it == m_Characters.end()) ? CharacterState{} : m_Backend->GetCharacterState(it->second.Character);
    }

    void PhysicsWorld3D::SetJointMotor(Entity entity, bool enabled, float targetVelocity, float maxForce)
    {
        auto it = m_Joints.find((uint32_t)entity);
        if (!m_Backend || it == m_Joints.end())
            return;
        for (auto& [kind, rec] : it->second)
            if ((kind == kJointHinge || kind == kJointSlider) && rec.Joint)
                m_Backend->SetJointMotor(rec.Joint, enabled, kind == kJointHinge ? glm::radians(targetVelocity) : targetVelocity, maxForce);
    }

    // ── Sorgular ──
    bool PhysicsWorld3D::Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, RaycastHit& hit, uint32_t layerMask, uint32_t ignoreEntity)
    {
        if (!m_Backend) return false;
        ProcessDirty();
        QueryFilter f;
        f.LayerMask = layerMask;
        f.HitTriggers = PhysicsSettings::Get().QueriesHitTriggers;
        f.IgnoreEntity = ignoreEntity;
        return m_Backend->Raycast(origin, direction, maxDistance, f, hit);
    }

    void PhysicsWorld3D::RaycastAll(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, std::vector<RaycastHit>& hits, uint32_t layerMask)
    {
        hits.clear();
        if (!m_Backend) return;
        ProcessDirty();
        QueryFilter f;
        f.LayerMask = layerMask;
        f.HitTriggers = PhysicsSettings::Get().QueriesHitTriggers;
        m_Backend->RaycastAll(origin, direction, maxDistance, f, hits);
    }

    bool PhysicsWorld3D::SphereCast(const glm::vec3& origin, float radius, const glm::vec3& direction, float maxDistance, RaycastHit& hit, uint32_t layerMask, uint32_t ignoreEntity)
    {
        if (!m_Backend) return false;
        ProcessDirty();
        QueryFilter f;
        f.LayerMask = layerMask;
        f.HitTriggers = PhysicsSettings::Get().QueriesHitTriggers;
        f.IgnoreEntity = ignoreEntity;
        return m_Backend->SphereCast(origin, radius, direction, maxDistance, f, hit);
    }

    void PhysicsWorld3D::OverlapSphere(const glm::vec3& center, float radius, std::vector<uint32_t>& entities, uint32_t layerMask)
    {
        entities.clear();
        if (!m_Backend) return;
        ProcessDirty();
        QueryFilter f;
        f.LayerMask = layerMask;
        f.HitTriggers = PhysicsSettings::Get().QueriesHitTriggers;
        m_Backend->OverlapSphere(center, radius, f, entities);
    }

    void PhysicsWorld3D::OverlapBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::quat& rotation, std::vector<uint32_t>& entities, uint32_t layerMask)
    {
        entities.clear();
        if (!m_Backend) return;
        ProcessDirty();
        QueryFilter f;
        f.LayerMask = layerMask;
        f.HitTriggers = PhysicsSettings::Get().QueriesHitTriggers;
        m_Backend->OverlapBox(center, halfExtents, rotation, f, entities);
    }

    // ════════════════════════════════════════
    //  Wheel Collider (Unity): ışınla süspansiyon + lastik sürtünmesi
    // ════════════════════════════════════════
    namespace
    {
        // Unity WheelFrictionCurve: (0,0) -> uç nokta (eğim 0) -> asimptot -> sabit
        float EvaluateWheelFriction(float slip, float exSlip, float exValue, float asSlip, float asValue)
        {
            slip = std::fabs(slip);
            if (slip <= exSlip)
            {
                float t = slip / std::max(exSlip, 1e-4f);
                return exValue * t * (2.0f - t);
            }
            if (slip < asSlip)
            {
                float t = (slip - exSlip) / std::max(asSlip - exSlip, 1e-4f);
                return exValue + (asValue - exValue) * t * t * (3.0f - 2.0f * t);
            }
            return asValue;
        }
    }

    uint32_t PhysicsWorld3D::WheelOwner(uint32_t entity) const
    {
        // Tekerlek çarpışma şekli değildir: kendisi ya da en yakın Rigidbody'li ata
        Entity e{ (entt::entity)entity, m_Scene };
        for (int guard = 0; e && guard < 256; ++guard)
        {
            auto it = m_Bodies.find((uint32_t)e);
            if (it != m_Bodies.end())
                return it->second.Motion == MotionType::Dynamic ? (uint32_t)e : NoEntity;
            e = e.GetParent();
        }
        return NoEntity;
    }

    void PhysicsWorld3D::UpdateWheels(float dt)
    {
        auto view = m_Scene->m_Registry.view<WheelColliderComponent>();
        if (view.empty() || dt <= 0.0f)
            return;

        std::unordered_map<uint32_t, int> wheelsPerBody;
        std::vector<std::pair<entt::entity, uint32_t>> wheels;
        for (auto e : view)
        {
            auto& w = view.get<WheelColliderComponent>(e);
            uint32_t owner = m_Scene->IsEntityActive(e) ? WheelOwner((uint32_t)e) : NoEntity;
            if (owner == NoEntity)
            {
                w.IsGrounded = false;
                w.SuspensionForce = 0.0f;
                continue;
            }
            wheelsPerBody[owner]++;
            wheels.push_back({ e, owner });
        }

        for (auto [e, owner] : wheels)
        {
            auto& w = view.get<WheelColliderComponent>(e);
            const Handle body = m_Bodies[owner].Body;

            // Fiziksel gövde pozu + tekerleğin gövdeye göre yerel dönüşümü (görsel ara değerlemeden bağımsız)
            glm::vec3 bp, op, os;
            glm::quat br, orot;
            m_Backend->GetBodyTransform(body, bp, br);
            glm::mat4 ownerWorld = WorldMatrix(owner);
            Decompose(ownerWorld, op, orot, os);
            glm::mat4 local = glm::inverse(ownerWorld) * WorldMatrix((uint32_t)e);
            glm::mat4 wheelM = glm::translate(glm::mat4(1.0f), bp) * glm::mat4_cast(br) * glm::scale(glm::mat4(1.0f), os) * local;

            glm::vec3 top = glm::vec3(wheelM * glm::vec4(w.Center, 1.0f));
            glm::vec3 up = glm::normalize(glm::vec3(wheelM[1]));
            glm::vec3 fwd0 = glm::normalize(-glm::vec3(wheelM[2]));
            // Unity: pozitif direksiyon açısı sağa döner
            glm::vec3 fwd = glm::angleAxis(glm::radians(-w.SteerAngle), up) * fwd0;

            const float travel = std::max(w.SuspensionDistance, 0.0f);
            const float radius = std::max(w.Radius, 0.001f);
            const float inertia = std::max(0.5f * w.Mass * radius * radius, 1e-4f);

            QueryFilter filter;
            filter.IgnoreBody = body;
            filter.HitTriggers = false;
            RaycastHit hit;
            bool grounded = m_Backend->Raycast(top, -up, travel + radius, filter, hit);

            // Motor ve serbest dönme sönümü (fren temas çözümünde: kilitli tekerlek aracı tutar)
            w.AngularVelocity += w.MotorTorque / inertia * dt;
            w.AngularVelocity -= w.AngularVelocity * std::min(1.0f, w.WheelDampingRate / inertia * dt);
            const float brakeTorque = std::max(w.BrakeTorque, 0.0f);
            auto applyBrake = [&]() {
                float b = brakeTorque / inertia * dt;
                w.AngularVelocity = std::fabs(w.AngularVelocity) <= b ? 0.0f : w.AngularVelocity - std::copysign(b, w.AngularVelocity);
            };

            w.IsGrounded = grounded;
            if (!grounded)
            {
                applyBrake();
                w.Compression = 0.0f;
                w.LastCompression = -1.0f;
                w.SuspensionForce = 0.0f;
                w.ForwardSlip = w.SidewaysSlip = 0.0f;
                w.HitEntity = 0;
                w.RPM = w.AngularVelocity * 60.0f / 6.2831853f;
                continue;
            }

            // Süspansiyon: yay (hedef konuma göre) + sönüm; sıkışma sınırında sert tampon
            float x = std::clamp(hit.Distance - radius, 0.0f, travel);
            float compression = travel - x;
            float compressionVel = w.LastCompression >= 0.0f ? (compression - w.LastCompression) / dt : 0.0f;
            w.LastCompression = compression;
            float load = w.Spring * (compression - w.TargetPosition * travel) + w.Damper * compressionVel;
            if (hit.Distance < radius)
                load += w.Spring * 4.0f * (radius - hit.Distance);
            load = std::max(load, 0.0f);

            // Temas noktasının zemine göre hızı (hareketli zemin desteklenir)
            Handle groundBody = BodyOf(hit.Entity);
            glm::vec3 vel = m_Backend->GetPointVelocity(body, hit.Point);
            bool groundDynamic = false;
            {
                auto it = m_Bodies.find(hit.Entity);
                if (it == m_Bodies.end())
                {
                    auto so = m_ShapeOwner.find(hit.Entity);
                    if (so != m_ShapeOwner.end())
                        it = m_Bodies.find(so->second);
                }
                groundDynamic = it != m_Bodies.end() && it->second.Motion == MotionType::Dynamic;
            }
            if (groundBody)
                vel -= m_Backend->GetPointVelocity(groundBody, hit.Point);

            glm::vec3 n = glm::dot(hit.Normal, hit.Normal) > 1e-6f ? glm::normalize(hit.Normal) : up;
            glm::vec3 fwdG = fwd - n * glm::dot(fwd, n);
            fwdG = glm::dot(fwdG, fwdG) > 1e-8f ? glm::normalize(fwdG) : fwd;
            glm::vec3 sideG = glm::normalize(glm::cross(fwdG, n));
            float vLong = glm::dot(vel, fwdG);
            float vLat = glm::dot(vel, sideG);

            float bodyMass = std::max(m_Backend->GetMass(body), 1e-3f);
            float massShare = bodyMass / (float)std::max(1, wheelsPerBody[owner]);

            // Boyuna: kaymayı bu adımda gideren kuvvet uç değer sürtünmesini aşmıyorsa tekerlek tutunur (yuvarlanır);
            // aşıyorsa kayar ve kuvvet sürtünme eğrisinden gelir (patinaj, kilitli tekerlek kızaklaması).
            // Fren tekerleği durdurabiliyorsa tekerlek kilitlidir ve aracın kütle payını doğrudan taşır.
            const float peakLong = std::max(w.FwdExtremumValue, w.FwdAsymptoteValue) * w.FwdStiffness * load;
            bool locked = false;
            if (brakeTorque > 0.0f && std::fabs(w.AngularVelocity) <= brakeTorque / inertia * dt)
            {
                w.AngularVelocity = 0.0f;
                locked = true;
            }
            else if (brakeTorque > 0.0f)
                applyBrake();
            float slipVel = w.AngularVelocity * radius - vLong;
            float slipRatio = slipVel / std::max(std::fabs(vLong), 1.0f);
            float fx = 0.0f;
            if (locked)
            {
                fx = massShare * slipVel / dt;
                if (std::fabs(fx) > peakLong)
                    fx = std::copysign(EvaluateWheelFriction(slipRatio, w.FwdExtremumSlip, w.FwdExtremumValue, w.FwdAsymptoteSlip, w.FwdAsymptoteValue) * w.FwdStiffness * load, slipVel);
                // Fren tutamayacağı kadar tork gelirse tekerlek döner
                float excess = std::fabs(fx) * radius - brakeTorque;
                if (excess > 0.0f)
                    w.AngularVelocity -= std::copysign(excess, fx) / inertia * dt;
            }
            else
            {
                float effMass = 1.0f / (1.0f / massShare + radius * radius / inertia);
                fx = effMass * slipVel / dt;
                if (std::fabs(fx) > peakLong)
                    fx = std::copysign(EvaluateWheelFriction(slipRatio, w.FwdExtremumSlip, w.FwdExtremumValue, w.FwdAsymptoteSlip, w.FwdAsymptoteValue) * w.FwdStiffness * load, slipVel);
                w.AngularVelocity -= fx * radius / inertia * dt;
            }

            // Yanal: yanal hızı söndüren kuvvet; uç değeri aşınca kayma açısına göre sürtünme eğrisi (savrulma)
            float slipAngle = std::atan2(vLat, std::max(std::fabs(vLong), 0.5f));
            const float peakLat = std::max(w.SideExtremumValue, w.SideAsymptoteValue) * w.SideStiffness * load;
            float fy = -0.5f * massShare * vLat / dt;
            if (std::fabs(fy) > peakLat)
                fy = std::copysign(EvaluateWheelFriction(slipAngle, w.SideExtremumSlip, w.SideExtremumValue, w.SideAsymptoteSlip, w.SideAsymptoteValue) * w.SideStiffness * load, fy);

            // Sürtünme çemberi: birleşik kuvvet en büyük tutunmayı aşamaz
            float limit = std::max(peakLong, peakLat);
            float mag = std::sqrt(fx * fx + fy * fy);
            if (mag > limit && mag > 1e-6f)
            {
                fx *= limit / mag;
                fy *= limit / mag;
            }

            glm::vec3 force = up * load + fwdG * fx + sideG * fy;
            glm::vec3 at = hit.Point + up * w.ForceAppPointDistance;
            m_Backend->AddForceAtPoint(body, force, at);
            if (groundDynamic)
                m_Backend->AddForceAtPoint(groundBody, -force, hit.Point);

            w.Compression = travel > 0.0f ? compression / travel : 1.0f;
            w.SuspensionForce = load;
            w.ForwardSlip = slipRatio;
            w.SidewaysSlip = slipAngle;
            w.HitPoint = hit.Point;
            w.HitNormal = n;
            Entity hitEntity{ (entt::entity)hit.Entity, m_Scene };
            w.HitEntity = Valid(hit.Entity) && hitEntity.HasComponent<IDComponent>() ? (uint64_t)hitEntity.GetComponent<IDComponent>().ID : 0;
            w.RPM = w.AngularVelocity * 60.0f / 6.2831853f;
        }
    }

    void PhysicsWorld3D::UpdateWheelVisuals(float dt)
    {
        auto view = m_Scene->m_Registry.view<WheelColliderComponent>();
        for (auto e : view)
        {
            auto& w = view.get<WheelColliderComponent>(e);
            w.SpinAngle = std::fmod(w.SpinAngle + w.AngularVelocity * dt, 6.2831853f);
            if (!w.WheelModel)
                continue;
            Entity model = m_Scene->FindEntityByUUID(w.WheelModel);
            if (!model || (entt::entity)model == e)
                continue;

            glm::vec3 wp, ws;
            glm::quat wr;
            glm::mat4 wheelWorld = WorldMatrix((uint32_t)e);
            Decompose(wheelWorld, wp, wr, ws);
            glm::vec3 up = glm::normalize(glm::vec3(wheelWorld[1]));
            glm::vec3 top = glm::vec3(wheelWorld * glm::vec4(w.Center, 1.0f));
            float travel = std::max(w.SuspensionDistance, 0.0f);
            glm::vec3 center = top - up * (travel * (1.0f - std::clamp(w.Compression, 0.0f, 1.0f)));

            // Modelin tekerleğe göre ilk dönüşü korunur (ör. Y eksenli silindir 90° yatırılmış)
            if (!w.ModelOffsetCaptured)
            {
                glm::vec3 mp, ms;
                glm::quat mr;
                Decompose(WorldMatrix((uint32_t)model), mp, mr, ms);
                w.ModelRotOffset = glm::inverse(wr) * mr;
                w.ModelOffsetCaptured = true;
            }
            glm::quat steer = glm::angleAxis(glm::radians(-w.SteerAngle), glm::vec3(0.0f, 1.0f, 0.0f));
            glm::quat spin = glm::angleAxis(-w.SpinAngle, glm::vec3(1.0f, 0.0f, 0.0f));
            SetWorldPose((uint32_t)model, center, wr * steer * spin * w.ModelRotOffset);
        }
    }
}
