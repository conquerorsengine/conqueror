#include "Core/PhysicsSystem/PhysicsMaterial.h"
#include "PhysicsWorld2D.h"
#include "Core/Logging/Log.h"
#include "Core/PhysicsSystem/PhysicsSettings.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Renderer/Renderer2D.h"
#include "Grid/GridSystem.h"
#include "PolygonUnion.h"
#include "Core/Math/Triangulate2D.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <functional>

namespace Conqueror
{
    namespace
    {
        inline void HashCombine(size_t& seed, size_t v) { seed ^= v + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2); }
        inline void HashF(size_t& seed, float f) { HashCombine(seed, std::hash<int>()((int)std::lround(f * 1000.0f))); }
        inline void HashV(size_t& seed, const glm::vec2& v) { HashF(seed, v.x); HashF(seed, v.y); }

        inline b2Vec2 B(const glm::vec2& v) { return { v.x, v.y }; }
        inline glm::vec2 G(b2Vec2 v) { return { v.x, v.y }; }
        inline void* EncodeEntity(uint32_t e) { return reinterpret_cast<void*>((uintptr_t)e + 1); }
        inline uint32_t DecodeEntity(void* p) { return p ? (uint32_t)((uintptr_t)p - 1) : PhysicsWorld2D::NoEntity; }
        inline uint64_t PairKey(uint32_t a, uint32_t b) { if (a > b) std::swap(a, b); return ((uint64_t)a << 32) | b; }

        int LayerOf(Entity e)
        {
            return e.HasComponent<LayerComponent>() ? std::clamp(e.GetComponent<LayerComponent>().Layer, 0, 31) : 0;
        }

        b2Filter MakeFilter(int layer)
        {
            b2Filter f = b2DefaultFilter();
            f.categoryBits = (uint64_t)1 << layer;
            f.maskBits = PhysicsSettings::Get().CollisionMatrix[(size_t)layer];
            return f;
        }

        // 2D dışbükey kabuk (Andrew monotone chain)
        std::vector<glm::vec2> ConvexHull(std::vector<glm::vec2> pts)
        {
            std::sort(pts.begin(), pts.end(), [](const glm::vec2& a, const glm::vec2& b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
            pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
            if (pts.size() < 3) return pts;
            auto cross = [](const glm::vec2& o, const glm::vec2& a, const glm::vec2& b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); };
            std::vector<glm::vec2> h(pts.size() * 2);
            size_t k = 0;
            for (size_t i = 0; i < pts.size(); ++i)
            {
                while (k >= 2 && cross(h[k - 2], h[k - 1], pts[i]) <= 0) --k;
                h[k++] = pts[i];
            }
            for (size_t i = pts.size() - 1, t = k + 1; i > 0; --i)
            {
                while (k >= t && cross(h[k - 2], h[k - 1], pts[i - 1]) <= 0) --k;
                h[k++] = pts[i - 1];
            }
            h.resize(k - 1);
            return h;
        }

        // Tek yönlü platform: ön çözüm (Box2D iş parçacıklarından; yalnızca salt okunur veri)
        struct PreSolveContext
        {
            const std::unordered_map<uint64_t, PhysicsWorld2D::PlatformInfo>* Platforms = nullptr;
            const std::unordered_map<uint64_t, glm::vec2>* Velocities = nullptr;
        };

        bool PlatformPreSolve(b2ShapeId shapeA, b2ShapeId shapeB, b2Vec2 point, b2Vec2 normal, void* context)
        {
            (void)point;
            auto* ctx = static_cast<PreSolveContext*>(context);
            if (!ctx || !ctx->Platforms)
                return true;
            uint64_t ka = b2StoreShapeId(shapeA), kb = b2StoreShapeId(shapeB);
            auto ia = ctx->Platforms->find(ka);
            auto ib = ctx->Platforms->find(kb);
            if (ia == ctx->Platforms->end() && ib == ctx->Platforms->end())
                return true;
            const bool platformIsA = ia != ctx->Platforms->end();
            const PhysicsWorld2D::PlatformInfo& info = platformIsA ? ia->second : ib->second;
            b2ShapeId platformShape = platformIsA ? shapeA : shapeB;
            b2ShapeId other = platformIsA ? shapeB : shapeA;
            // Platformun "yukarı"sı: gövde dönüşü + ofset
            b2Rot rot = b2Body_GetRotation(b2Shape_GetBody(platformShape));
            float angle = b2Rot_GetAngle(rot) + glm::radians(info.RotationalOffset);
            glm::vec2 up(-std::sin(angle), std::cos(angle));
            // Normal A'dan B'ye; platformdan diğerine doğru olmalı
            glm::vec2 n = platformIsA ? G(normal) : -G(normal);
            float cosHalfArc = std::cos(glm::radians(std::clamp(info.SurfaceArc, 1.0f, 360.0f)) * 0.5f);
            if (glm::dot(n, up) < cosHalfArc - 1e-4f)
                return false;
            // Aşağıdan yukarı çıkan cisim: temas yok
            if (ctx->Velocities)
            {
                auto v = ctx->Velocities->find(b2StoreBodyId(b2Shape_GetBody(other)));
                if (v != ctx->Velocities->end() && glm::dot(v->second, up) > 0.5f)
                    return false;
            }
            return true;
        }

        PreSolveContext& PreSolveState()
        {
            static PreSolveContext s_Context;
            return s_Context;
        }
        std::unordered_map<uint64_t, glm::vec2>& VelocitySnapshot()
        {
            static std::unordered_map<uint64_t, glm::vec2> s_Snapshot;
            return s_Snapshot;
        }

        constexpr int kDistance = 0, kSpring = 1, kHinge = 2, kSlider = 3, kWheel = 4, kFixed = 5, kFriction = 6, kRelative = 7, kTarget = 8;

        bool HasJoint2D(Entity e)
        {
            return e.HasComponent<DistanceJoint2DComponent>() || e.HasComponent<SpringJoint2DComponent>() || e.HasComponent<HingeJoint2DComponent>() ||
                   e.HasComponent<SliderJoint2DComponent>() || e.HasComponent<WheelJoint2DComponent>() || e.HasComponent<FixedJoint2DComponent>() ||
                   e.HasComponent<FrictionJoint2DComponent>() || e.HasComponent<RelativeJoint2DComponent>() || e.HasComponent<TargetJoint2DComponent>();
        }
    }

    PhysicsWorld2D::PhysicsWorld2D() = default;

    PhysicsWorld2D::~PhysicsWorld2D()
    {
        Shutdown();
    }

    void PhysicsWorld2D::Initialize()
    {
        m_Initialized = true;
        m_Gravity = PhysicsSettings::Get().Gravity2D;
    }

    void PhysicsWorld2D::Shutdown()
    {
        OnRuntimeStop();
        m_Initialized = false;
    }

    bool PhysicsWorld2D::Valid(uint32_t entity) const
    {
        return m_Scene && entity != NoEntity && m_Scene->m_Registry.valid((entt::entity)entity);
    }

    // ════════════════════════════════════════
    //  Yaşam döngüsü
    // ════════════════════════════════════════
    void PhysicsWorld2D::OnRuntimeStart()
    {
        if (!m_Scene)
            return;
        OnRuntimeStop();
        const auto& settings = PhysicsSettings::Get();
        b2WorldDef def = b2DefaultWorldDef();
        m_Gravity = settings.Gravity2D;
        def.gravity = B(m_Gravity);
        def.contactHertz = 30.0f;
        m_WorldId = b2CreateWorld(&def);
        auto& ctx = PreSolveState();
        ctx.Platforms = &m_Platforms;
        ctx.Velocities = &VelocitySnapshot();
        b2World_SetPreSolveCallback(m_WorldId, PlatformPreSolve, &ctx);
        m_Accumulator = 0.0f;
        auto& reg = m_Scene->m_Registry;
        auto mark = [&](auto view) { for (auto e : view) m_Dirty.insert((uint32_t)e); };
        mark(reg.view<RigidBody2DComponent>());
        mark(reg.view<BoxCollider2DComponent>());
        mark(reg.view<CircleCollider2DComponent>());
        mark(reg.view<CapsuleCollider2DComponent>());
        mark(reg.view<PolygonCollider2DComponent>());
        mark(reg.view<EdgeCollider2DComponent>());
        mark(reg.view<TilemapCollider2DComponent>());
        mark(reg.view<DistanceJoint2DComponent>());
        mark(reg.view<SpringJoint2DComponent>());
        mark(reg.view<HingeJoint2DComponent>());
        mark(reg.view<SliderJoint2DComponent>());
        mark(reg.view<WheelJoint2DComponent>());
        mark(reg.view<FixedJoint2DComponent>());
        mark(reg.view<FrictionJoint2DComponent>());
        mark(reg.view<RelativeJoint2DComponent>());
        mark(reg.view<TargetJoint2DComponent>());
        ProcessDirty();
        if (!m_Bodies.empty())
            CQ_CORE_INFO("[Physics2D] Box2D started: {0} bodies", m_Bodies.size());
    }

    void PhysicsWorld2D::OnRuntimeStop()
    {
        if (B2_IS_NON_NULL(m_WorldId))
        {
            b2DestroyWorld(m_WorldId);
            m_WorldId = b2_nullWorldId;
        }
        if (m_Scene)
        {
            auto view = m_Scene->m_Registry.view<RigidBody2DComponent>();
            for (auto e : view)
            {
                auto& rb = view.get<RigidBody2DComponent>(e);
                delete static_cast<b2BodyId*>(rb.RuntimeBody);
                rb.RuntimeBody = nullptr;
            }
        }
        m_Bodies.clear();
        m_ShapeOwner.clear();
        m_Joints.clear();
        m_JointSignatures.clear();
        m_Dirty.clear();
        m_PendingEvents.clear();
        m_ActivePairs.clear();
        m_SensorVisitors.clear();
        m_SensorPairCounts.clear();
        m_Platforms.clear();
        VelocitySnapshot().clear();
    }

    void PhysicsWorld2D::Step(Timestep ts)
    {
        if (B2_IS_NULL(m_WorldId) || !m_Scene)
            return;
        const auto& settings = PhysicsSettings::Get();
        m_FixedDt = std::clamp(settings.FixedTimestep, 1.0f / 1000.0f, 0.25f);
        float dt = std::clamp(ts.GetSeconds(), 0.0f, 0.25f);
        ProcessDirty();
        SyncExternalChanges();
        m_Accumulator += dt;
        int steps = 0;
        while (m_Accumulator >= m_FixedDt && steps < settings.MaxSubSteps)
        {
            PreStep(m_FixedDt);
            b2World_Step(m_WorldId, m_FixedDt, 4);
            PostStep(m_FixedDt);
            m_Accumulator -= m_FixedDt;
            ++steps;
        }
        if (steps >= settings.MaxSubSteps && m_Accumulator > m_FixedDt)
            m_Accumulator = 0.0f;
        WriteTransforms(std::clamp(m_Accumulator / m_FixedDt, 0.0f, 1.0f));

        // Bileşen değişiklikleri (denetçi / betik)
        if (!m_Bodies.empty())
        {
            int checks = std::min<int>(32, (int)m_Bodies.size());
            auto it = m_Bodies.begin();
            std::advance(it, m_SignatureCursor % (int)m_Bodies.size());
            for (int i = 0; i < checks; ++i)
            {
                if (it == m_Bodies.end()) it = m_Bodies.begin();
                if (Valid(it->first) && ComputeSignature(it->first) != it->second.Signature)
                    m_Dirty.insert(it->first);
                ++it;
            }
            m_SignatureCursor = (m_SignatureCursor + checks) % std::max<int>(1, (int)m_Bodies.size());
        }
        for (auto& [e, sig] : m_JointSignatures)
            if (Valid(e) && ComputeJointSignature(e) != sig)
                m_Dirty.insert(e);
    }

    // ════════════════════════════════════════
    //  Dönüşümler
    // ════════════════════════════════════════
    glm::mat4 PhysicsWorld2D::WorldMatrix(uint32_t entity) const
    {
        glm::mat4 m(1.0f);
        std::vector<glm::mat4> chain;
        Entity e{ (entt::entity)entity, m_Scene };
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

    void PhysicsWorld2D::WorldPose(uint32_t entity, glm::vec2& position, float& angle, glm::vec2& scale) const
    {
        glm::mat4 m = WorldMatrix(entity);
        position = glm::vec2(m[3]);
        angle = std::atan2(m[0].y, m[0].x);
        scale = glm::vec2(glm::length(glm::vec2(m[0])), glm::length(glm::vec2(m[1])));
    }

    void PhysicsWorld2D::SetWorldPose(uint32_t entity, const glm::vec2& position, float angle)
    {
        Entity e{ (entt::entity)entity, m_Scene };
        if (!Valid(entity) || !e.HasComponent<TransformComponent>())
            return;
        auto& tc = e.GetComponent<TransformComponent>();
        Entity parent = e.GetParent();
        if (parent)
        {
            glm::mat4 pw = WorldMatrix((uint32_t)parent);
            glm::vec3 local = glm::vec3(glm::inverse(pw) * glm::vec4(position, glm::vec3(pw * glm::vec4(tc.Position, 1.0f)).z, 1.0f));
            tc.Position.x = local.x;
            tc.Position.y = local.y;
            tc.Rotation.z = angle - std::atan2(pw[0].y, pw[0].x);
        }
        else
        {
            tc.Position.x = position.x;
            tc.Position.y = position.y;
            tc.Rotation.z = angle;
        }
    }

    // ════════════════════════════════════════
    //  Kurulum
    // ════════════════════════════════════════
    bool PhysicsWorld2D::HasCollider(uint32_t entity) const
    {
        Entity e{ (entt::entity)entity, m_Scene };
        return e.HasComponent<BoxCollider2DComponent>() || e.HasComponent<CircleCollider2DComponent>() || e.HasComponent<CapsuleCollider2DComponent>() ||
               e.HasComponent<PolygonCollider2DComponent>() || e.HasComponent<EdgeCollider2DComponent>() ||
               (e.HasComponent<TilemapCollider2DComponent>() && e.HasComponent<TilemapComponent>()) || e.HasComponent<CompositeCollider2DComponent>();
    }

    uint32_t PhysicsWorld2D::FindOwner(uint32_t entity) const
    {
        if (!Valid(entity) || !m_Scene->IsEntityActive((entt::entity)entity))
            return NoEntity;
        Entity e{ (entt::entity)entity, m_Scene };
        if (e.HasComponent<RigidBody2DComponent>())
            return entity;
        if (!HasCollider(entity))
            return NoEntity;
        // Üstte Rigidbody 2D yoksa en yakın Composite Collider 2D sahiplenir (statik birleşik şekil)
        uint32_t composite = NoEntity;
        for (Entity p = e.GetParent(); p; p = p.GetParent())
        {
            if (p.HasComponent<RigidBody2DComponent>())
                return (uint32_t)p;
            if (composite == NoEntity && p.HasComponent<CompositeCollider2DComponent>() && m_Scene->IsEntityActive((entt::entity)p))
                composite = (uint32_t)p;
        }
        return composite != NoEntity ? composite : entity;
    }

    b2BodyId PhysicsWorld2D::BodyOf(uint32_t entity) const
    {
        auto it = m_Bodies.find(entity);
        if (it != m_Bodies.end()) return it->second.Body;
        auto so = m_ShapeOwner.find(entity);
        if (so != m_ShapeOwner.end())
        {
            auto ot = m_Bodies.find(so->second);
            if (ot != m_Bodies.end()) return ot->second.Body;
        }
        return b2_nullBodyId;
    }

    void PhysicsWorld2D::AddShapes(uint32_t owner, uint32_t entity, b2BodyId body, const glm::mat4& ownerInv, std::vector<uint32_t>& shapeEntities, bool isRoot)
    {
        Entity e{ (entt::entity)entity, m_Scene };
        if (!Valid(entity) || !m_Scene->IsEntityActive((entt::entity)entity))
            return;
        if (!isRoot && e.HasComponent<RigidBody2DComponent>())
            return;
        glm::mat4 rel = ownerInv * WorldMatrix(entity);
        const glm::vec2 t(rel[3]);
        const float phi = std::atan2(rel[0].y, rel[0].x);
        const glm::vec2 s(glm::length(glm::vec2(rel[0])), glm::length(glm::vec2(rel[1])));
        const float c = std::cos(phi), sn = std::sin(phi);
        auto xf = [&](const glm::vec2& p) { glm::vec2 q = p * s; return t + glm::vec2(c * q.x - sn * q.y, sn * q.x + c * q.y); };
        const int layer = LayerOf(e);
        bool any = false;

        // Physics Material 2D: çarpıştırıcının kendi malzemesi, yoksa gövdenin (Rigidbody 2D) malzemesi
        auto applyMaterial = [&](const std::string& own, float& friction, float& restitution) {
            std::string path = own;
            if (path.empty())
            {
                Entity ow{ (entt::entity)owner, m_Scene };
                if (ow.HasComponent<RigidBody2DComponent>())
                    path = ow.GetComponent<RigidBody2DComponent>().PhysicsMaterial;
            }
            PhysicsMaterialAsset m;
            if (!path.empty() && PhysicsMaterials::Load(path, m))
            {
                friction = m.Friction;
                restitution = m.Bounciness;
            }
        };
        auto baseDef = [&](float density, float friction, float restitution, bool trigger, const std::string& material) {
            applyMaterial(material, friction, restitution);
            b2ShapeDef d = b2DefaultShapeDef();
            d.density = density;
            d.material.friction = friction;
            d.material.restitution = restitution;
            d.isSensor = trigger;
            d.enableSensorEvents = true;
            d.enableContactEvents = true;
            d.enableHitEvents = false;
            d.filter = MakeFilter(layer);
            d.userData = EncodeEntity(entity);
            if (e.HasComponent<SurfaceEffector2DComponent>())
                d.material.tangentSpeed = e.GetComponent<SurfaceEffector2DComponent>().Speed;
            if (e.HasComponent<PlatformEffector2DComponent>() && e.GetComponent<PlatformEffector2DComponent>().UseOneWay && !trigger)
                d.enablePreSolveEvents = true;
            return d;
        };
        auto registerShape = [&](b2ShapeId id, const b2ShapeDef& d) {
            if (d.enablePreSolveEvents)
            {
                auto& pe = e.GetComponent<PlatformEffector2DComponent>();
                m_Platforms[b2StoreShapeId(id)] = { pe.SurfaceArc, pe.RotationalOffset };
            }
            any = true;
        };

        // Composite Collider 2D: bu alt ağaçtaki çarpıştırıcılar tek birleşik şekil olur
        if (e.HasComponent<CompositeCollider2DComponent>())
        {
            auto& comp = e.GetComponent<CompositeCollider2DComponent>();
            std::vector<std::vector<glm::vec2>> sources;
            std::function<void(Entity, bool)> collect = [&](Entity sub, bool subRoot) {
                if (!Valid((uint32_t)sub) || !m_Scene->IsEntityActive((entt::entity)sub))
                    return;
                if (!subRoot && sub.HasComponent<RigidBody2DComponent>())
                    return;
                glm::mat4 r = ownerInv * WorldMatrix((uint32_t)sub);
                const glm::vec2 st(r[3]);
                const float sphi = std::atan2(r[0].y, r[0].x);
                const glm::vec2 ss(glm::length(glm::vec2(r[0])), glm::length(glm::vec2(r[1])));
                const float sc = std::cos(sphi), ssn = std::sin(sphi);
                auto sxf = [&](const glm::vec2& p) { glm::vec2 q = p * ss; return st + glm::vec2(sc * q.x - ssn * q.y, ssn * q.x + sc * q.y); };
                if (sub.HasComponent<BoxCollider2DComponent>())
                {
                    auto& bc = sub.GetComponent<BoxCollider2DComponent>();
                    glm::vec2 h = glm::abs(bc.Size) * 0.5f;
                    sources.push_back({ sxf(bc.Offset + glm::vec2(-h.x, -h.y)), sxf(bc.Offset + glm::vec2(h.x, -h.y)), sxf(bc.Offset + h), sxf(bc.Offset + glm::vec2(-h.x, h.y)) });
                }
                if (sub.HasComponent<CircleCollider2DComponent>())
                {
                    auto& cc = sub.GetComponent<CircleCollider2DComponent>();
                    std::vector<glm::vec2> poly;
                    for (int i = 0; i < 24; ++i)
                    {
                        float a = (float)i / 24.0f * 6.2831853f;
                        poly.push_back(sxf(cc.Offset + glm::vec2(std::cos(a), std::sin(a)) * std::fabs(cc.Radius)));
                    }
                    sources.push_back(std::move(poly));
                }
                if (sub.HasComponent<CapsuleCollider2DComponent>())
                {
                    auto& cc = sub.GetComponent<CapsuleCollider2DComponent>();
                    const bool vertical = cc.Direction == 0;
                    const glm::vec2 size = glm::abs(cc.Size);
                    const float rad = (vertical ? size.x : size.y) * 0.5f;
                    const float half = std::max(0.0f, (vertical ? size.y : size.x) * 0.5f - rad);
                    const glm::vec2 axis = vertical ? glm::vec2(0, 1) : glm::vec2(1, 0);
                    const float base = vertical ? 0.0f : -1.5707963f;
                    std::vector<glm::vec2> poly;
                    for (int i = 0; i <= 12; ++i) { float a = base + (float)i / 12.0f * 3.1415926f; poly.push_back(sxf(cc.Offset + axis * half + glm::vec2(std::cos(a), std::sin(a)) * rad)); }
                    for (int i = 0; i <= 12; ++i) { float a = base + 3.1415926f + (float)i / 12.0f * 3.1415926f; poly.push_back(sxf(cc.Offset - axis * half + glm::vec2(std::cos(a), std::sin(a)) * rad)); }
                    sources.push_back(std::move(poly));
                }
                if (sub.HasComponent<PolygonCollider2DComponent>())
                {
                    auto& pc = sub.GetComponent<PolygonCollider2DComponent>();
                    std::vector<glm::vec2> poly;
                    for (auto& p : pc.Points) poly.push_back(sxf(p + pc.Offset));
                    if (poly.size() >= 3) sources.push_back(std::move(poly));
                }
                if (sub.HasComponent<TilemapCollider2DComponent>() && sub.HasComponent<TilemapComponent>())
                {
                    auto& map = sub.GetComponent<TilemapComponent>();
                    const GridComponent& grid = GridSystem::GridOf(sub);
                    std::vector<glm::vec2> corners;
                    for (const auto& [cellKey, cell] : map.Cells)
                    {
                        if (cell.Tile < 0 || cell.Tile >= (int)map.Palette.size() || !map.Palette[(size_t)cell.Tile].Solid)
                            continue;
                        GridSystem::CellCorners(grid, GridKeyToCell(cellKey), corners);
                        std::vector<glm::vec2> poly;
                        for (auto& c : corners) poly.push_back(sxf(glm::vec2(GridSystem::PlaneToLocal(grid, c, 0.0f))));
                        sources.push_back(std::move(poly));
                    }
                }
                for (Entity child : sub.GetChildren())
                    collect(child, false);
            };
            collect(e, true);

            const bool boxes = comp.GeometryType == CompositeGeometryType::Polygons || comp.IsTrigger;
            PolygonUnion::Result merged = PolygonUnion::Build(sources, comp.VertexDistance, boxes);
            comp.OutlineCount = (int)merged.Loops.size();
            comp.ShapeCount = 0;
            if (boxes)
            {
                b2ShapeDef d = baseDef(comp.Density, comp.Friction, comp.Restitution, comp.IsTrigger, comp.PhysicsMaterial);
                for (const glm::vec4& r : merged.Boxes)
                {
                    glm::vec2 half = glm::max((glm::vec2(r.z, r.w) - glm::vec2(r.x, r.y)) * 0.5f, glm::vec2(0.0005f));
                    glm::vec2 center = (glm::vec2(r.x, r.y) + glm::vec2(r.z, r.w)) * 0.5f;
                    b2Polygon box = b2MakeOffsetBox(half.x, half.y, B(center), b2MakeRot(0.0f));
                    registerShape(b2CreatePolygonShape(body, &d, &box), d);
                    ++comp.ShapeCount;
                }
            }
            else
            {
                b2SurfaceMaterial material = b2DefaultSurfaceMaterial();
                material.friction = comp.Friction;
                material.restitution = comp.Restitution;
                applyMaterial(comp.PhysicsMaterial, material.friction, material.restitution);
                for (auto& loop : merged.Loops)
                {
                    std::vector<b2Vec2> pts;
                    for (auto& p : loop) pts.push_back(B(p));
                    // Zincir en az 4 nokta ister: üçgende en uzun kenarın ortasına nokta eklenir
                    if (pts.size() == 3)
                    {
                        size_t longest = 0;
                        float best = -1.0f;
                        for (size_t i = 0; i < 3; ++i)
                        {
                            b2Vec2 a = pts[i], b = pts[(i + 1) % 3];
                            float l = (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y);
                            if (l > best) { best = l; longest = i; }
                        }
                        b2Vec2 a = pts[longest], b = pts[(longest + 1) % 3];
                        pts.insert(pts.begin() + (long)longest + 1, b2Vec2{ (a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f });
                    }
                    b2ChainDef cd = b2DefaultChainDef();
                    cd.points = pts.data();
                    cd.count = (int)pts.size();
                    cd.materials = &material;
                    cd.materialCount = 1;
                    cd.filter = MakeFilter(layer);
                    cd.userData = EncodeEntity(entity);
                    cd.isLoop = true;
                    b2CreateChain(body, &cd);
                    comp.ShapeCount += (int)pts.size();
                    any = true;
                }
            }
            if (any)
                shapeEntities.push_back(entity);
            return;
        }

        if (e.HasComponent<BoxCollider2DComponent>())
        {
            auto& bc = e.GetComponent<BoxCollider2DComponent>();
            b2ShapeDef d = baseDef(bc.Density, bc.Friction, bc.Restitution, bc.IsTrigger, bc.PhysicsMaterial);
            glm::vec2 half = glm::max(glm::abs(bc.Size * s) * 0.5f, glm::vec2(0.001f));
            b2Polygon box = bc.EdgeRadius > 0.0f ? b2MakeOffsetRoundedBox(half.x, half.y, B(xf(bc.Offset)), b2MakeRot(phi), bc.EdgeRadius)
                                                 : b2MakeOffsetBox(half.x, half.y, B(xf(bc.Offset)), b2MakeRot(phi));
            registerShape(b2CreatePolygonShape(body, &d, &box), d);
        }
        if (e.HasComponent<CircleCollider2DComponent>())
        {
            auto& cc = e.GetComponent<CircleCollider2DComponent>();
            b2ShapeDef d = baseDef(cc.Density, cc.Friction, cc.Restitution, cc.IsTrigger, cc.PhysicsMaterial);
            b2Circle circle{ B(xf(cc.Offset)), std::max(0.001f, std::fabs(cc.Radius) * std::max(s.x, s.y)) };
            registerShape(b2CreateCircleShape(body, &d, &circle), d);
        }
        if (e.HasComponent<CapsuleCollider2DComponent>())
        {
            auto& cc = e.GetComponent<CapsuleCollider2DComponent>();
            b2ShapeDef d = baseDef(cc.Density, cc.Friction, cc.Restitution, cc.IsTrigger, cc.PhysicsMaterial);
            glm::vec2 size = glm::abs(cc.Size * s);
            bool vertical = cc.Direction == 0;
            float radius = std::max(0.001f, (vertical ? size.x : size.y) * 0.5f);
            float half = std::max(0.0f, (vertical ? size.y : size.x) * 0.5f - radius);
            glm::vec2 axisLocal = vertical ? glm::vec2(0, 1) : glm::vec2(1, 0);
            glm::vec2 center = xf(cc.Offset);
            glm::vec2 axis(c * axisLocal.x - sn * axisLocal.y, sn * axisLocal.x + c * axisLocal.y);
            if (half <= 1e-4f)
            {
                b2Circle circle{ B(center), radius };
                registerShape(b2CreateCircleShape(body, &d, &circle), d);
            }
            else
            {
                b2Capsule cap{ B(center - axis * half), B(center + axis * half), radius };
                registerShape(b2CreateCapsuleShape(body, &d, &cap), d);
            }
        }
        if (e.HasComponent<PolygonCollider2DComponent>())
        {
            auto& pc = e.GetComponent<PolygonCollider2DComponent>();
            std::vector<glm::vec2> pts;
            for (auto& p : pc.Points) pts.push_back(xf(p + pc.Offset));
            // İçbükey ya da 8'den çok köşeli çokgen (Unity gibi): üçgenlere bölünüp her üçgen ayrı dışbükey şekil olur
            std::vector<uint32_t> tris;
            if (pts.size() >= 3 && (!Triangulate2D::IsConvex(pts) || pts.size() > B2_MAX_POLYGON_VERTICES) && Triangulate2D::Triangulate(pts, tris))
            {
                b2ShapeDef d = baseDef(pc.Density, pc.Friction, pc.Restitution, pc.IsTrigger, pc.PhysicsMaterial);
                for (size_t t = 0; t + 2 < tris.size(); t += 3)
                {
                    const b2Vec2 tri[3] = { B(pts[tris[t]]), B(pts[tris[t + 1]]), B(pts[tris[t + 2]]) };
                    b2Hull hull = b2ComputeHull(tri, 3);
                    if (hull.count < 3)
                        continue;   // dejenere (alanı sıfır) üçgen
                    b2Polygon poly = b2MakePolygon(&hull, 0.0f);
                    registerShape(b2CreatePolygonShape(body, &d, &poly), d);
                }
                pts.clear();
            }
            else
                pts = ConvexHull(pts);
            if (pts.size() > B2_MAX_POLYGON_VERTICES)
            {
                // Eşit aralıklı 8 kabuk noktası
                std::vector<glm::vec2> reduced;
                for (int i = 0; i < B2_MAX_POLYGON_VERTICES; ++i)
                    reduced.push_back(pts[(size_t)i * pts.size() / B2_MAX_POLYGON_VERTICES]);
                pts = reduced;
            }
            if (pts.size() >= 3)
            {
                std::vector<b2Vec2> bp;
                for (auto& p : pts) bp.push_back(B(p));
                b2Hull hull = b2ComputeHull(bp.data(), (int)bp.size());
                if (hull.count >= 3)
                {
                    b2ShapeDef d = baseDef(pc.Density, pc.Friction, pc.Restitution, pc.IsTrigger, pc.PhysicsMaterial);
                    b2Polygon poly = b2MakePolygon(&hull, 0.0f);
                    registerShape(b2CreatePolygonShape(body, &d, &poly), d);
                }
            }
        }
        if (e.HasComponent<EdgeCollider2DComponent>())
        {
            auto& ec = e.GetComponent<EdgeCollider2DComponent>();
            b2ShapeDef d = baseDef(0.0f, ec.Friction, ec.Restitution, false, ec.PhysicsMaterial);
            const size_t n = ec.Points.size();
            for (size_t i = 0; i + 1 < n + (ec.Loop && n > 2 ? 1 : 0); ++i)
            {
                glm::vec2 a = xf(ec.Points[i] + ec.Offset), b = xf(ec.Points[(i + 1) % n] + ec.Offset);
                if (glm::distance(a, b) < 1e-5f) continue;
                if (ec.EdgeRadius > 0.0f)
                {
                    b2Capsule cap{ B(a), B(b), ec.EdgeRadius };
                    registerShape(b2CreateCapsuleShape(body, &d, &cap), d);
                }
                else
                {
                    b2Segment seg{ B(a), B(b) };
                    registerShape(b2CreateSegmentShape(body, &d, &seg), d);
                }
            }
        }
        if (e.HasComponent<TilemapCollider2DComponent>() && e.HasComponent<TilemapComponent>())
        {
            auto& tc = e.GetComponent<TilemapCollider2DComponent>();
            auto& map = e.GetComponent<TilemapComponent>();
            const GridComponent& grid = GridSystem::GridOf(e);
            std::vector<glm::vec2> corners;
            for (const auto& [key, cell] : map.Cells)
            {
                if (cell.Tile < 0 || cell.Tile >= (int)map.Palette.size() || !map.Palette[(size_t)cell.Tile].Solid)
                    continue;
                GridSystem::CellCorners(grid, GridKeyToCell(key), corners);
                std::vector<b2Vec2> bp;
                for (auto& c : corners)
                {
                    glm::vec3 local = GridSystem::PlaneToLocal(grid, c, 0.0f);
                    bp.push_back(B(xf(glm::vec2(local))));
                }
                b2Hull hull = b2ComputeHull(bp.data(), (int)bp.size());
                if (hull.count < 3)
                    continue;
                b2ShapeDef d = baseDef(0.0f, tc.Friction, tc.Restitution, tc.IsTrigger, tc.PhysicsMaterial);
                b2Polygon poly = b2MakePolygon(&hull, 0.0f);
                registerShape(b2CreatePolygonShape(body, &d, &poly), d);
            }
        }
        if (any)
            shapeEntities.push_back(entity);

        Entity ownerEntity{ (entt::entity)owner, m_Scene };
        if (ownerEntity.HasComponent<RigidBody2DComponent>())
            for (Entity child : e.GetChildren())
                AddShapes(owner, (uint32_t)child, body, ownerInv, shapeEntities, false);
    }

    size_t PhysicsWorld2D::ComputeSignature(uint32_t owner) const
    {
        size_t h = 0;
        Entity o{ (entt::entity)owner, m_Scene };
        HashCombine(h, (size_t)LayerOf(o));
        if (o.HasComponent<RigidBody2DComponent>())
        {
            auto& rb = o.GetComponent<RigidBody2DComponent>();
            HashCombine(h, (size_t)rb.Type + 3);
            HashF(h, rb.Mass); HashF(h, rb.LinearDrag); HashF(h, rb.AngularDrag); HashF(h, rb.GravityScale);
            HashCombine(h, rb.FixedRotation | (rb.UseAutoMass << 1) | (rb.ContinuousCollision << 2) | (rb.AllowSleep << 3) | (rb.Interpolate << 4));
        }
        glm::vec2 p, s;
        float a;
        WorldPose(owner, p, a, s);
        glm::mat4 ownerInv = glm::inverse(glm::translate(glm::mat4(1.0f), glm::vec3(p, 0.0f)) * glm::rotate(glm::mat4(1.0f), a, glm::vec3(0, 0, 1)));
        std::function<void(uint32_t, bool)> walk = [&](uint32_t entity, bool root) {
            Entity e{ (entt::entity)entity, m_Scene };
            if (!Valid(entity)) return;
            if (!root && e.HasComponent<RigidBody2DComponent>()) return;
            HashCombine(h, (size_t)m_Scene->IsEntityActive((entt::entity)entity) + 5);
            bool any = false;
            if (e.HasComponent<BoxCollider2DComponent>()) { auto& c = e.GetComponent<BoxCollider2DComponent>(); HashV(h, c.Size); HashV(h, c.Offset); HashF(h, c.Density); HashF(h, c.Friction); HashF(h, c.Restitution); HashF(h, c.EdgeRadius); HashCombine(h, c.IsTrigger + 11); any = true; }
            if (e.HasComponent<CircleCollider2DComponent>()) { auto& c = e.GetComponent<CircleCollider2DComponent>(); HashF(h, c.Radius); HashV(h, c.Offset); HashF(h, c.Density); HashF(h, c.Friction); HashF(h, c.Restitution); HashCombine(h, c.IsTrigger + 13); any = true; }
            if (e.HasComponent<CapsuleCollider2DComponent>()) { auto& c = e.GetComponent<CapsuleCollider2DComponent>(); HashV(h, c.Size); HashV(h, c.Offset); HashF(h, c.Density); HashF(h, c.Friction); HashCombine(h, c.Direction * 2 + c.IsTrigger + 17); any = true; }
            if (e.HasComponent<PolygonCollider2DComponent>()) { auto& c = e.GetComponent<PolygonCollider2DComponent>(); for (auto& pt : c.Points) HashV(h, pt); HashV(h, c.Offset); HashF(h, c.Density); HashCombine(h, c.IsTrigger + 19); any = true; }
            if (e.HasComponent<EdgeCollider2DComponent>()) { auto& c = e.GetComponent<EdgeCollider2DComponent>(); for (auto& pt : c.Points) HashV(h, pt); HashV(h, c.Offset); HashF(h, c.EdgeRadius); HashCombine(h, c.Loop + 23); any = true; }
            if (e.HasComponent<SurfaceEffector2DComponent>()) HashF(h, e.GetComponent<SurfaceEffector2DComponent>().Speed);
            // Physics Material 2D (dosya değişince de yeniden kurulur)
            auto hashMat = [&](const std::string& path) {
                if (path.empty()) return;
                HashCombine(h, std::hash<std::string>{}(path));
                PhysicsMaterialAsset m;
                if (PhysicsMaterials::Load(path, m)) { HashF(h, m.Friction); HashF(h, m.Bounciness); }
            };
            if (e.HasComponent<BoxCollider2DComponent>()) hashMat(e.GetComponent<BoxCollider2DComponent>().PhysicsMaterial);
            if (e.HasComponent<CircleCollider2DComponent>()) hashMat(e.GetComponent<CircleCollider2DComponent>().PhysicsMaterial);
            if (e.HasComponent<CapsuleCollider2DComponent>()) hashMat(e.GetComponent<CapsuleCollider2DComponent>().PhysicsMaterial);
            if (e.HasComponent<PolygonCollider2DComponent>()) hashMat(e.GetComponent<PolygonCollider2DComponent>().PhysicsMaterial);
            if (e.HasComponent<EdgeCollider2DComponent>()) hashMat(e.GetComponent<EdgeCollider2DComponent>().PhysicsMaterial);
            if (e.HasComponent<CompositeCollider2DComponent>()) hashMat(e.GetComponent<CompositeCollider2DComponent>().PhysicsMaterial);
            if (e.HasComponent<TilemapCollider2DComponent>()) hashMat(e.GetComponent<TilemapCollider2DComponent>().PhysicsMaterial);
            if (root && e.HasComponent<RigidBody2DComponent>()) hashMat(e.GetComponent<RigidBody2DComponent>().PhysicsMaterial);
            if (e.HasComponent<CompositeCollider2DComponent>())
            {
                auto& cc = e.GetComponent<CompositeCollider2DComponent>();
                HashCombine(h, (size_t)cc.GeometryType * 2 + cc.IsTrigger + 41);
                HashF(h, cc.VertexDistance); HashF(h, cc.Density); HashF(h, cc.Friction); HashF(h, cc.Restitution);
                any = true;
            }
            if (e.HasComponent<PlatformEffector2DComponent>()) { auto& pe = e.GetComponent<PlatformEffector2DComponent>(); HashCombine(h, pe.UseOneWay + 29); HashF(h, pe.SurfaceArc); HashF(h, pe.RotationalOffset); }
            if (e.HasComponent<TilemapCollider2DComponent>() && e.HasComponent<TilemapComponent>())
            {
                auto& tc = e.GetComponent<TilemapCollider2DComponent>();
                HashCombine(h, (size_t)e.GetComponent<TilemapComponent>().Version * 31 + tc.IsTrigger);
                HashF(h, tc.Friction); HashF(h, tc.Restitution);
                const GridComponent& grid = GridSystem::GridOf(e);
                HashCombine(h, (size_t)grid.Layout + 37); HashV(h, grid.CellSize); HashV(h, grid.CellGap);
                any = true;
            }
            if (any)
            {
                glm::mat4 rel = ownerInv * WorldMatrix(entity);
                HashV(h, glm::vec2(rel[3]));
                HashF(h, std::atan2(rel[0].y, rel[0].x));
                HashV(h, glm::vec2(glm::length(glm::vec2(rel[0])), glm::length(glm::vec2(rel[1]))));
            }
            if (o.HasComponent<RigidBody2DComponent>() || o.HasComponent<CompositeCollider2DComponent>())
                for (Entity child : e.GetChildren())
                    walk((uint32_t)child, false);
        };
        walk(owner, true);
        return h;
    }

    void PhysicsWorld2D::RemoveOwner(uint32_t owner)
    {
        auto it = m_Bodies.find(owner);
        if (it == m_Bodies.end())
            return;
        if (B2_IS_NON_NULL(m_WorldId) && b2Body_IsValid(it->second.Body))
        {
            // Platform kayıtları ve sensör örtüşmeleri bu gövdenin şekilleriyle gider
            int count = b2Body_GetShapeCount(it->second.Body);
            std::vector<b2ShapeId> shapes((size_t)std::max(0, count));
            if (count > 0) b2Body_GetShapes(it->second.Body, shapes.data(), count);
            for (auto& s : shapes) m_Platforms.erase(b2StoreShapeId(s));
            b2DestroyBody(it->second.Body);
        }
        // Bu gövdeyi içeren temas/tetikleyici çiftleri için Exit
        for (auto pt = m_ActivePairs.begin(); pt != m_ActivePairs.end();)
        {
            if (pt->second.A == owner || pt->second.B == owner)
            {
                ContactEvent2D ev;
                ev.Type = ContactEvent2D::Exit;
                ev.EntityA = pt->second.A;
                ev.EntityB = pt->second.B;
                ev.Trigger = pt->second.Trigger;
                m_PendingEvents.push_back(ev);
                pt = m_ActivePairs.erase(pt);
            }
            else
                ++pt;
        }
        for (auto& [sensor, visitors] : m_SensorVisitors)
            visitors.erase(owner);
        for (auto so = m_ShapeOwner.begin(); so != m_ShapeOwner.end();)
        {
            if (so->second == owner) so = m_ShapeOwner.erase(so);
            else ++so;
        }
        if (Valid(owner))
        {
            Entity o{ (entt::entity)owner, m_Scene };
            if (o.HasComponent<RigidBody2DComponent>())
            {
                auto& rb = o.GetComponent<RigidBody2DComponent>();
                delete static_cast<b2BodyId*>(rb.RuntimeBody);
                rb.RuntimeBody = nullptr;
            }
        }
        m_Bodies.erase(it);
    }

    void PhysicsWorld2D::RebuildOwner(uint32_t owner)
    {
        glm::vec2 lin(0.0f);
        float ang = 0.0f;
        if (auto old = m_Bodies.find(owner); old != m_Bodies.end() && b2Body_IsValid(old->second.Body))
        {
            lin = G(b2Body_GetLinearVelocity(old->second.Body));
            ang = b2Body_GetAngularVelocity(old->second.Body);
        }
        RemoveOwner(owner);
        if (!Valid(owner))
            return;
        Entity o{ (entt::entity)owner, m_Scene };
        glm::vec2 pos, scale;
        float angle;
        WorldPose(owner, pos, angle, scale);

        b2BodyDef bd = b2DefaultBodyDef();
        bd.position = B(pos);
        bd.rotation = b2MakeRot(angle);
        bd.userData = EncodeEntity(owner);
        bd.linearVelocity = B(lin);
        bd.angularVelocity = ang;
        bd.type = b2_staticBody;
        RigidBody2DComponent* rb = o.HasComponent<RigidBody2DComponent>() ? &o.GetComponent<RigidBody2DComponent>() : nullptr;
        if (rb)
        {
            bd.type = rb->Type == RigidBody2DComponent::BodyType::Dynamic ? b2_dynamicBody
                    : rb->Type == RigidBody2DComponent::BodyType::Kinematic ? b2_kinematicBody : b2_staticBody;
            bd.gravityScale = rb->GravityScale;
            bd.linearDamping = std::max(0.0f, rb->LinearDrag);
            bd.angularDamping = std::max(0.0f, rb->AngularDrag);
            bd.motionLocks.angularZ = rb->FixedRotation;
            bd.isBullet = rb->ContinuousCollision;
            bd.enableSleep = rb->AllowSleep;
        }
        b2BodyId body = b2CreateBody(m_WorldId, &bd);
        glm::mat4 ownerInv = glm::inverse(glm::translate(glm::mat4(1.0f), glm::vec3(pos, 0.0f)) * glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0, 0, 1)));
        std::vector<uint32_t> shapeEntities;
        AddShapes(owner, owner, body, ownerInv, shapeEntities, true);
        if (shapeEntities.empty() && !rb)
        {
            b2DestroyBody(body);
            return;
        }
        // Belirtilen kütle (Unity: Use Auto Mass kapalı)
        if (rb && bd.type == b2_dynamicBody && !rb->UseAutoMass && rb->Mass > 0.0f)
        {
            float autoMass = b2Body_GetMass(body);
            b2MassData md;
            md.mass = rb->Mass;
            md.center = b2Body_GetLocalCenterOfMass(body);
            float inertia = b2Body_GetRotationalInertia(body);
            md.rotationalInertia = autoMass > 1e-6f ? inertia * (rb->Mass / autoMass) : rb->Mass * 0.1f;
            b2Body_SetMassData(body, md);
        }
        BodyRecord rec;
        rec.Body = body;
        rec.Type = bd.type;
        rec.Interpolate = bd.type == b2_dynamicBody && PhysicsSettings::Get().Interpolation && (!rb || rb->Interpolate);
        rec.PrevPos = rec.CurrPos = rec.LastWrittenPos = pos;
        rec.PrevAngle = rec.CurrAngle = rec.LastWrittenAngle = angle;
        rec.ShapeEntities = shapeEntities;
        rec.Signature = ComputeSignature(owner);
        for (uint32_t se : shapeEntities)
        {
            if (se != owner && m_Bodies.count(se))
                RemoveOwner(se);
            m_ShapeOwner[se] = owner;
        }
        if (rb)
            rb->RuntimeBody = new b2BodyId(body);
        m_Bodies[owner] = rec;
    }

    // ── Eklemler ──
    size_t PhysicsWorld2D::ComputeJointSignature(uint32_t entity) const
    {
        Entity e{ (entt::entity)entity, m_Scene };
        size_t h = 7;
        auto common = [&](const Joint2DCommon& j) {
            HashCombine(h, std::hash<uint64_t>()(j.ConnectedBody));
            HashV(h, j.Anchor); HashV(h, j.ConnectedAnchor);
            HashCombine(h, j.AutoConfigureConnectedAnchor + j.EnableCollision * 2 + j.Broken * 4);
            HashF(h, j.BreakForce); HashF(h, j.BreakTorque);
            Entity other = j.ConnectedBody ? m_Scene->FindEntityByUUID(j.ConnectedBody) : Entity{};
            b2BodyId ob = other ? BodyOf((uint32_t)other) : b2_nullBodyId;
            HashCombine(h, std::hash<uint64_t>()(B2_IS_NON_NULL(ob) ? b2StoreBodyId(ob) : 0));
        };
        if (e.HasComponent<DistanceJoint2DComponent>()) { auto& j = e.GetComponent<DistanceJoint2DComponent>(); common(j); HashCombine(h, j.AutoConfigureDistance + j.MaxDistanceOnly * 2 + 31); HashF(h, j.Distance); }
        if (e.HasComponent<SpringJoint2DComponent>()) { auto& j = e.GetComponent<SpringJoint2DComponent>(); common(j); HashCombine(h, j.AutoConfigureDistance + 37); HashF(h, j.Distance); HashF(h, j.Frequency); HashF(h, j.DampingRatio); }
        if (e.HasComponent<HingeJoint2DComponent>()) { auto& j = e.GetComponent<HingeJoint2DComponent>(); common(j); HashCombine(h, j.UseMotor + j.UseLimits * 2 + 41); HashF(h, j.MotorSpeed); HashF(h, j.MaxMotorTorque); HashF(h, j.LowerAngle); HashF(h, j.UpperAngle); }
        if (e.HasComponent<SliderJoint2DComponent>()) { auto& j = e.GetComponent<SliderJoint2DComponent>(); common(j); HashCombine(h, j.UseMotor + j.UseLimits * 2 + 43); HashF(h, j.Angle); HashF(h, j.MotorSpeed); HashF(h, j.MaxMotorForce); HashF(h, j.LowerTranslation); HashF(h, j.UpperTranslation); }
        if (e.HasComponent<WheelJoint2DComponent>()) { auto& j = e.GetComponent<WheelJoint2DComponent>(); common(j); HashCombine(h, j.UseMotor + 47); HashF(h, j.Angle); HashF(h, j.SuspensionFrequency); HashF(h, j.SuspensionDamping); HashF(h, j.MotorSpeed); HashF(h, j.MaxMotorTorque); }
        if (e.HasComponent<FixedJoint2DComponent>()) { auto& j = e.GetComponent<FixedJoint2DComponent>(); common(j); HashCombine(h, 53); HashF(h, j.Frequency); HashF(h, j.DampingRatio); }
        if (e.HasComponent<FrictionJoint2DComponent>()) { auto& j = e.GetComponent<FrictionJoint2DComponent>(); common(j); HashCombine(h, 59); HashF(h, j.MaxForce); HashF(h, j.MaxTorque); }
        if (e.HasComponent<RelativeJoint2DComponent>())
        {
            // Otomatik ofset kurulumda yazılır; imzaya girerse eklem sürekli yeniden kurulurdu
            auto& j = e.GetComponent<RelativeJoint2DComponent>(); common(j); HashCombine(h, 61 + j.AutoConfigureOffset);
            HashF(h, j.MaxForce); HashF(h, j.MaxTorque); HashF(h, j.CorrectionScale);
            if (!j.AutoConfigureOffset) { HashV(h, j.LinearOffset); HashF(h, j.AngularOffset); }
        }
        if (e.HasComponent<TargetJoint2DComponent>())
        {
            // Hedef imzada yok: her adımda güncellenir (sürüklerken eklem yeniden kurulmaz)
            auto& j = e.GetComponent<TargetJoint2DComponent>(); HashCombine(h, 67 + j.AutoConfigureTarget * 2 + j.Broken * 4);
            HashV(h, j.Anchor); HashF(h, j.MaxForce); HashF(h, j.DampingRatio); HashF(h, j.Frequency); HashF(h, j.BreakForce);
        }
        b2BodyId own = BodyOf(entity);
        HashCombine(h, std::hash<uint64_t>()(B2_IS_NON_NULL(own) ? b2StoreBodyId(own) : 0));
        return h;
    }

    void PhysicsWorld2D::RemoveJoints(uint32_t entity)
    {
        auto it = m_Joints.find(entity);
        if (it == m_Joints.end())
            return;
        for (auto& j : it->second)
            if (B2_IS_NON_NULL(m_WorldId) && b2Joint_IsValid(j.Joint))
                b2DestroyJoint(j.Joint, true);
        m_Joints.erase(it);
    }

    namespace
    {
        // Dışa aktarılan (dllexport) sınıfın üye işlevinde thread_local yerel olamaz (MSVC/MinGW): dosya kapsamında
        thread_local b2WorldId s_JointGroundWorld = b2_nullWorldId;
        thread_local b2BodyId s_JointGroundBody = b2_nullBodyId;
    }

    void PhysicsWorld2D::RebuildJoints(uint32_t entity)
    {
        RemoveJoints(entity);
        Entity e{ (entt::entity)entity, m_Scene };
        if (!Valid(entity) || !HasJoint2D(e))
        {
            m_JointSignatures.erase(entity);
            return;
        }
        m_JointSignatures[entity] = ComputeJointSignature(entity);
        b2BodyId own = BodyOf(entity);
        if (B2_IS_NULL(own) || b2Body_GetType(own) == b2_staticBody)
            return;
        glm::mat4 world = WorldMatrix(entity);
        glm::vec2 ownScale(glm::length(glm::vec2(world[0])), glm::length(glm::vec2(world[1])));
        std::vector<JointRecord> records;

        // Bağlı gövde: belirtilen varlık ya da dünya için kalıcı statik "zemin" gövdesi
        auto ground = [&]() {
            b2WorldId& s_World = s_JointGroundWorld;
            b2BodyId& s_Ground = s_JointGroundBody;
            const bool sameWorld = s_World.index1 == m_WorldId.index1 && s_World.generation == m_WorldId.generation;
            if (!sameWorld || !b2Body_IsValid(s_Ground))
            {
                b2BodyDef bd = b2DefaultBodyDef();
                s_Ground = b2CreateBody(m_WorldId, &bd);
                s_World = m_WorldId;
            }
            return s_Ground;
        };

        auto setup = [&](const Joint2DCommon& j, b2JointDef& base, glm::vec2& anchorWorld, glm::vec2& connectedWorld) -> bool {
            if (j.Broken)
                return false;
            Entity other = j.ConnectedBody ? m_Scene->FindEntityByUUID(j.ConnectedBody) : Entity{};
            b2BodyId connected = other ? BodyOf((uint32_t)other) : b2_nullBodyId;
            if (B2_IS_NULL(connected))
                connected = ground();
            anchorWorld = glm::vec2(world * glm::vec4(j.Anchor, 0.0f, 1.0f));
            if (j.AutoConfigureConnectedAnchor)
                connectedWorld = anchorWorld;
            else if (other)
                connectedWorld = glm::vec2(WorldMatrix((uint32_t)other) * glm::vec4(j.ConnectedAnchor, 0.0f, 1.0f));
            else
                connectedWorld = j.ConnectedAnchor;
            // Gövde A = bağlı (zemin), gövde B = sahip: açı/konum sahibin bağlıya göre değeri (Unity)
            base.bodyIdA = connected;
            base.bodyIdB = own;
            base.localFrameA.p = b2Body_GetLocalPoint(connected, B(connectedWorld));
            base.localFrameB.p = b2Body_GetLocalPoint(own, B(anchorWorld));
            base.localFrameA.q = b2InvMulRot(b2Body_GetRotation(connected), b2Body_GetRotation(own));
            base.localFrameB.q = b2Rot_identity;
            base.collideConnected = j.EnableCollision;
            base.userData = EncodeEntity(entity);
            return true;
        };
        auto setAxis = [&](b2JointDef& base, float angleDeg) {
            // Eksen = yerel çerçevenin x ekseni; sahibin yerel uzayında açı
            b2Rot axisLocal = b2MakeRot(glm::radians(angleDeg));
            base.localFrameB.q = axisLocal;
            base.localFrameA.q = b2MulRot(b2InvMulRot(b2Body_GetRotation(base.bodyIdA), b2Body_GetRotation(own)), axisLocal);
        };
        auto add = [&](int kind, b2JointId id, const Joint2DCommon& j) {
            if (B2_IS_NON_NULL(id))
                records.push_back({ id, kind, j.BreakForce, j.BreakTorque });
        };

        glm::vec2 aw, cw;
        if (e.HasComponent<DistanceJoint2DComponent>())
        {
            auto& j = e.GetComponent<DistanceJoint2DComponent>();
            b2DistanceJointDef d = b2DefaultDistanceJointDef();
            if (setup(j, d.base, aw, cw))
            {
                d.base.localFrameA.q = d.base.localFrameB.q = b2Rot_identity;
                if (j.AutoConfigureConnectedAnchor)
                {
                    // Bağlı gövdenin kökeni (ip/çubuk etkisi)
                    Entity other = j.ConnectedBody ? m_Scene->FindEntityByUUID(j.ConnectedBody) : Entity{};
                    cw = other ? glm::vec2(WorldMatrix((uint32_t)other)[3]) : aw + glm::vec2(0, 1);
                    d.base.localFrameA.p = b2Body_GetLocalPoint(d.base.bodyIdA, B(cw));
                }
                float length = j.AutoConfigureDistance ? glm::distance(aw, cw) : j.Distance;
                d.length = std::max(0.005f, length);
                if (j.MaxDistanceOnly)
                {
                    d.enableSpring = true;
                    d.hertz = 0.0f;
                    d.enableLimit = true;
                    d.minLength = 0.0f;
                    d.maxLength = d.length;
                }
                add(kDistance, b2CreateDistanceJoint(m_WorldId, &d), j);
            }
        }
        if (e.HasComponent<SpringJoint2DComponent>())
        {
            auto& j = e.GetComponent<SpringJoint2DComponent>();
            b2DistanceJointDef d = b2DefaultDistanceJointDef();
            if (setup(j, d.base, aw, cw))
            {
                d.base.localFrameA.q = d.base.localFrameB.q = b2Rot_identity;
                if (j.AutoConfigureConnectedAnchor)
                {
                    Entity other = j.ConnectedBody ? m_Scene->FindEntityByUUID(j.ConnectedBody) : Entity{};
                    cw = other ? glm::vec2(WorldMatrix((uint32_t)other)[3]) : aw + glm::vec2(0, 1);
                    d.base.localFrameA.p = b2Body_GetLocalPoint(d.base.bodyIdA, B(cw));
                }
                d.length = std::max(0.005f, j.AutoConfigureDistance ? glm::distance(aw, cw) : j.Distance);
                d.enableSpring = true;
                d.hertz = std::max(0.0f, j.Frequency);
                d.dampingRatio = std::max(0.0f, j.DampingRatio);
                add(kSpring, b2CreateDistanceJoint(m_WorldId, &d), j);
            }
        }
        if (e.HasComponent<HingeJoint2DComponent>())
        {
            auto& j = e.GetComponent<HingeJoint2DComponent>();
            b2RevoluteJointDef d = b2DefaultRevoluteJointDef();
            if (setup(j, d.base, aw, cw))
            {
                d.enableMotor = j.UseMotor;
                d.motorSpeed = glm::radians(j.MotorSpeed);
                d.maxMotorTorque = std::max(0.0f, j.MaxMotorTorque);
                d.enableLimit = j.UseLimits;
                const float lim = 0.99f * glm::pi<float>();
                d.lowerAngle = std::clamp(glm::radians(std::min(j.LowerAngle, j.UpperAngle)), -lim, lim);
                d.upperAngle = std::clamp(glm::radians(std::max(j.LowerAngle, j.UpperAngle)), -lim, lim);
                add(kHinge, b2CreateRevoluteJoint(m_WorldId, &d), j);
            }
        }
        if (e.HasComponent<SliderJoint2DComponent>())
        {
            auto& j = e.GetComponent<SliderJoint2DComponent>();
            b2PrismaticJointDef d = b2DefaultPrismaticJointDef();
            if (setup(j, d.base, aw, cw))
            {
                setAxis(d.base, j.Angle);
                d.enableMotor = j.UseMotor;
                d.motorSpeed = j.MotorSpeed;
                d.maxMotorForce = std::max(0.0f, j.MaxMotorForce);
                d.enableLimit = j.UseLimits;
                d.lowerTranslation = std::min(j.LowerTranslation, j.UpperTranslation);
                d.upperTranslation = std::max(j.LowerTranslation, j.UpperTranslation);
                add(kSlider, b2CreatePrismaticJoint(m_WorldId, &d), j);
            }
        }
        if (e.HasComponent<WheelJoint2DComponent>())
        {
            auto& j = e.GetComponent<WheelJoint2DComponent>();
            b2WheelJointDef d = b2DefaultWheelJointDef();
            if (setup(j, d.base, aw, cw))
            {
                setAxis(d.base, j.Angle);
                d.enableSpring = true;
                d.hertz = std::max(0.0f, j.SuspensionFrequency);
                d.dampingRatio = std::max(0.0f, j.SuspensionDamping);
                d.enableMotor = j.UseMotor;
                d.motorSpeed = glm::radians(j.MotorSpeed);
                d.maxMotorTorque = std::max(0.0f, j.MaxMotorTorque);
                add(kWheel, b2CreateWheelJoint(m_WorldId, &d), j);
            }
        }
        if (e.HasComponent<FixedJoint2DComponent>())
        {
            auto& j = e.GetComponent<FixedJoint2DComponent>();
            b2WeldJointDef d = b2DefaultWeldJointDef();
            if (setup(j, d.base, aw, cw))
            {
                d.linearHertz = d.angularHertz = std::max(0.0f, j.Frequency);
                d.linearDampingRatio = d.angularDampingRatio = std::max(0.0f, j.DampingRatio);
                add(kFixed, b2CreateWeldJoint(m_WorldId, &d), j);
            }
        }
        if (e.HasComponent<FrictionJoint2DComponent>())
        {
            // Motor eklemi: hedef göreli hız 0, kuvvet/tork sınırlı => sürtünme (konum yayı yok)
            auto& j = e.GetComponent<FrictionJoint2DComponent>();
            b2MotorJointDef d = b2DefaultMotorJointDef();
            if (setup(j, d.base, aw, cw))
            {
                d.linearVelocity = { 0.0f, 0.0f };
                d.angularVelocity = 0.0f;
                d.maxVelocityForce = std::max(0.0f, j.MaxForce);
                d.maxVelocityTorque = std::max(0.0f, j.MaxTorque);
                d.linearHertz = d.angularHertz = 0.0f;
                d.maxSpringForce = d.maxSpringTorque = 0.0f;
                add(kFriction, b2CreateMotorJoint(m_WorldId, &d), j);
            }
        }
        if (e.HasComponent<RelativeJoint2DComponent>())
        {
            // Motor eklemi: konum yayı sahibi bağlı gövdeye göre ofset çerçevesine çeker
            auto& j = e.GetComponent<RelativeJoint2DComponent>();
            b2MotorJointDef d = b2DefaultMotorJointDef();
            if (setup(j, d.base, aw, cw))
            {
                b2BodyId a = d.base.bodyIdA;
                d.base.localFrameB = b2Transform_identity;
                if (j.AutoConfigureOffset)
                {
                    d.base.localFrameA.p = b2Body_GetLocalPoint(a, b2Body_GetPosition(own));
                    d.base.localFrameA.q = b2InvMulRot(b2Body_GetRotation(a), b2Body_GetRotation(own));
                    j.LinearOffset = G(d.base.localFrameA.p);
                    j.AngularOffset = glm::degrees(b2Rot_GetAngle(d.base.localFrameA.q));
                }
                else
                {
                    d.base.localFrameA.p = B(j.LinearOffset);
                    d.base.localFrameA.q = b2MakeRot(glm::radians(j.AngularOffset));
                }
                const float hz = 1.0f + 19.0f * std::clamp(j.CorrectionScale, 0.0f, 1.0f);
                d.linearHertz = d.angularHertz = hz;
                d.linearDampingRatio = d.angularDampingRatio = 1.0f;
                d.maxSpringForce = std::max(0.0f, j.MaxForce);
                d.maxSpringTorque = std::max(0.0f, j.MaxTorque);
                d.maxVelocityForce = d.maxVelocityTorque = 0.0f;
                add(kRelative, b2CreateMotorJoint(m_WorldId, &d), j);
            }
        }
        if (e.HasComponent<TargetJoint2DComponent>())
        {
            // Dünyaya (sabit zemin gövdesi, kökende) bağlı motor eklemi: zeminin çerçevesi = hedef nokta
            auto& j = e.GetComponent<TargetJoint2DComponent>();
            if (!j.Broken)
            {
                glm::vec2 anchorWorld = glm::vec2(world * glm::vec4(j.Anchor, 0.0f, 1.0f));
                if (j.AutoConfigureTarget)
                    j.Target = anchorWorld;
                b2MotorJointDef d = b2DefaultMotorJointDef();
                d.base.bodyIdA = ground();
                d.base.bodyIdB = own;
                d.base.localFrameA.p = b2Body_GetLocalPoint(d.base.bodyIdA, B(j.Target));
                d.base.localFrameA.q = b2Body_GetRotation(own);
                d.base.localFrameB.p = b2Body_GetLocalPoint(own, B(anchorWorld));
                d.base.localFrameB.q = b2Rot_identity;
                d.base.collideConnected = true;
                d.base.userData = EncodeEntity(entity);
                d.linearHertz = std::max(0.0f, j.Frequency);
                d.linearDampingRatio = std::max(0.0f, j.DampingRatio);
                d.maxSpringForce = std::max(0.0f, j.MaxForce);
                d.angularHertz = 0.0f;
                d.maxSpringTorque = 0.0f;
                d.maxVelocityForce = d.maxVelocityTorque = 0.0f;
                b2JointId id = b2CreateMotorJoint(m_WorldId, &d);
                if (B2_IS_NON_NULL(id))
                    records.push_back({ id, kTarget, j.BreakForce, 0.0f });
            }
        }
        (void)ownScale;
        if (!records.empty())
            m_Joints[entity] = std::move(records);
    }

    void PhysicsWorld2D::ProcessDirty()
    {
        if (B2_IS_NULL(m_WorldId) || m_Dirty.empty())
            return;
        std::vector<uint32_t> dirty(m_Dirty.begin(), m_Dirty.end());
        m_Dirty.clear();
        std::unordered_set<uint32_t> owners;
        bool anyJoint = false;
        for (uint32_t e : dirty)
        {
            if (!Valid(e))
            {
                RemoveJoints(e);
                m_JointSignatures.erase(e);
                auto so = m_ShapeOwner.find(e);
                if (so != m_ShapeOwner.end() && so->second != e) owners.insert(so->second);
                RemoveOwner(e);
                continue;
            }
            uint32_t owner = FindOwner(e);
            if (owner != NoEntity) owners.insert(owner);
            if (m_Bodies.count(e) && owner != e) RemoveOwner(e);
            auto so = m_ShapeOwner.find(e);
            if (so != m_ShapeOwner.end() && so->second != owner && so->second != e) owners.insert(so->second);
            Entity ent{ (entt::entity)e, m_Scene };
            if (ent.HasComponent<RigidBody2DComponent>())
                for (Entity child : ent.GetChildren())
                    if (m_Bodies.count((uint32_t)child) && FindOwner((uint32_t)child) != (uint32_t)child)
                        RemoveOwner((uint32_t)child);
            anyJoint |= m_JointSignatures.count(e) || HasJoint2D(ent);
        }
        for (uint32_t owner : owners)
            RebuildOwner(owner);
        if (!owners.empty() || anyJoint)
        {
            std::unordered_set<uint32_t> jointEntities;
            auto& reg = m_Scene->m_Registry;
            auto collect = [&](auto view) { for (auto e : view) jointEntities.insert((uint32_t)e); };
            collect(reg.view<DistanceJoint2DComponent>());
            collect(reg.view<SpringJoint2DComponent>());
            collect(reg.view<HingeJoint2DComponent>());
            collect(reg.view<SliderJoint2DComponent>());
            collect(reg.view<WheelJoint2DComponent>());
            collect(reg.view<FixedJoint2DComponent>());
            collect(reg.view<FrictionJoint2DComponent>());
            collect(reg.view<RelativeJoint2DComponent>());
            collect(reg.view<TargetJoint2DComponent>());
            for (auto& [e, sig] : m_JointSignatures) jointEntities.insert(e);
            for (uint32_t e : jointEntities) RebuildJoints(e);
        }
    }

    // ════════════════════════════════════════
    //  Adım
    // ════════════════════════════════════════
    void PhysicsWorld2D::SyncExternalChanges()
    {
        if (!PhysicsSettings::Get().AutoSyncTransforms)
            return;
        for (auto& [owner, rec] : m_Bodies)
        {
            if (rec.Type == b2_kinematicBody || !Valid(owner))
                continue;
            glm::vec2 p, s;
            float a;
            WorldPose(owner, p, a, s);
            float da = std::fabs(std::remainder(a - rec.LastWrittenAngle, glm::two_pi<float>()));
            if (glm::distance(p, rec.LastWrittenPos) > 1e-4f || da > 1e-4f)
            {
                b2Body_SetTransform(rec.Body, B(p), b2MakeRot(a));
                rec.PrevPos = rec.CurrPos = rec.LastWrittenPos = p;
                rec.PrevAngle = rec.CurrAngle = rec.LastWrittenAngle = a;
            }
        }
    }

    void PhysicsWorld2D::PreStep(float dt)
    {
        ProcessDirty();
        auto& velocities = VelocitySnapshot();
        velocities.clear();
        for (auto& [owner, rec] : m_Bodies)
        {
            if (!Valid(owner))
                continue;
            if (rec.Type == b2_kinematicBody)
            {
                glm::vec2 p, s;
                float a;
                WorldPose(owner, p, a, s);
                b2Body_SetTargetTransform(rec.Body, { B(p), b2MakeRot(a) }, dt, true);
                rec.PrevPos = rec.CurrPos;
                rec.PrevAngle = rec.CurrAngle;
                rec.CurrPos = rec.LastWrittenPos = p;
                rec.CurrAngle = rec.LastWrittenAngle = a;
            }
            if (rec.Type != b2_staticBody)
                velocities[b2StoreBodyId(rec.Body)] = G(b2Body_GetLinearVelocity(rec.Body));
        }
        ApplyEffectors();

        auto& reg = m_Scene->m_Registry;
        // Constant Force 2D
        for (auto e : reg.view<ConstantForce2DComponent>())
        {
            auto it = m_Bodies.find((uint32_t)e);
            if (it == m_Bodies.end() || it->second.Type != b2_dynamicBody || !b2Body_IsValid(it->second.Body))
                continue;
            const auto& cf = reg.get<ConstantForce2DComponent>(e);
            b2Vec2 rel = b2RotateVector(b2Body_GetRotation(it->second.Body), B(cf.RelativeForce));
            b2Vec2 f = { cf.Force.x + rel.x, cf.Force.y + rel.y };
            if (f.x != 0.0f || f.y != 0.0f)
                b2Body_ApplyForceToCenter(it->second.Body, f, true);
            if (cf.Torque != 0.0f)
                b2Body_ApplyTorque(it->second.Body, cf.Torque, true);
        }
        // Target Joint 2D: hedef her adımda (eklem yeniden kurulmadan) güncellenir
        for (auto& [entity, list] : m_Joints)
        {
            Entity e{ (entt::entity)entity, m_Scene };
            if (!Valid(entity) || !e.HasComponent<TargetJoint2DComponent>())
                continue;
            const auto& tj = e.GetComponent<TargetJoint2DComponent>();
            for (auto& rec : list)
            {
                if (rec.Kind != kTarget || !b2Joint_IsValid(rec.Joint))
                    continue;
                b2Transform frame = b2Joint_GetLocalFrameA(rec.Joint);
                b2Vec2 target = b2Body_GetLocalPoint(b2Joint_GetBodyA(rec.Joint), B(tj.Target));
                if (frame.p.x != target.x || frame.p.y != target.y)
                {
                    frame.p = target;
                    b2Joint_SetLocalFrameA(rec.Joint, frame);
                    b2Body_SetAwake(b2Joint_GetBodyB(rec.Joint), true);
                }
            }
        }
    }

    void PhysicsWorld2D::ApplyEffectors()
    {
        auto& reg = m_Scene->m_Registry;
        auto bodiesIn = [&](uint32_t effector) -> std::vector<b2BodyId> {
            std::vector<b2BodyId> out;
            auto it = m_SensorVisitors.find(effector);
            if (it == m_SensorVisitors.end()) return out;
            for (uint32_t v : it->second)
            {
                auto b = m_Bodies.find(v);
                if (b != m_Bodies.end() && b->second.Type == b2_dynamicBody && b2Body_IsValid(b->second.Body))
                    out.push_back(b->second.Body);
            }
            return out;
        };
        for (auto e : reg.view<AreaEffector2DComponent>())
        {
            auto& fx = reg.get<AreaEffector2DComponent>(e);
            glm::vec2 p, s;
            float a;
            WorldPose((uint32_t)e, p, a, s);
            float angle = glm::radians(fx.ForceAngle) + (fx.UseGlobalAngle ? 0.0f : a);
            glm::vec2 dir(std::cos(angle), std::sin(angle));
            for (b2BodyId b : bodiesIn((uint32_t)e))
            {
                float mass = b2Body_GetMass(b);
                glm::vec2 v = G(b2Body_GetLinearVelocity(b));
                glm::vec2 f = dir * fx.ForceMagnitude - v * fx.Drag * mass;
                b2Body_ApplyForceToCenter(b, B(f), true);
                if (fx.AngularDrag > 0.0f)
                    b2Body_ApplyTorque(b, -b2Body_GetAngularVelocity(b) * fx.AngularDrag * b2Body_GetRotationalInertia(b), true);
            }
        }
        for (auto e : reg.view<PointEffector2DComponent>())
        {
            auto& fx = reg.get<PointEffector2DComponent>(e);
            glm::vec2 center, s;
            float a;
            WorldPose((uint32_t)e, center, a, s);
            for (b2BodyId b : bodiesIn((uint32_t)e))
            {
                glm::vec2 bp = G(b2Body_GetWorldCenterOfMass(b));
                glm::vec2 d = bp - center;
                float dist = std::max(0.05f, glm::length(d) * std::max(0.001f, fx.DistanceScale));
                glm::vec2 n = glm::length(d) > 1e-5f ? glm::normalize(d) : glm::vec2(0, 1);
                float mag = fx.ForceMagnitude;
                if (fx.ForceMode == 1) mag /= dist;
                else if (fx.ForceMode == 2) mag /= dist * dist;
                glm::vec2 v = G(b2Body_GetLinearVelocity(b));
                b2Body_ApplyForceToCenter(b, B(n * mag - v * fx.Drag * b2Body_GetMass(b)), true);
                if (fx.AngularDrag > 0.0f)
                    b2Body_ApplyTorque(b, -b2Body_GetAngularVelocity(b) * fx.AngularDrag * b2Body_GetRotationalInertia(b), true);
            }
        }
        for (auto e : reg.view<BuoyancyEffector2DComponent>())
        {
            auto& fx = reg.get<BuoyancyEffector2DComponent>(e);
            glm::mat4 w = WorldMatrix((uint32_t)e);
            float surfaceY = glm::vec3(w * glm::vec4(0.0f, fx.SurfaceLevel, 0.0f, 1.0f)).y;
            for (b2BodyId b : bodiesIn((uint32_t)e))
            {
                b2AABB box = b2Body_ComputeAABB(b);
                float height = std::max(1e-4f, box.upperBound.y - box.lowerBound.y);
                float width = std::max(1e-4f, box.upperBound.x - box.lowerBound.x);
                float submerged = std::clamp((surfaceY - box.lowerBound.y) / height, 0.0f, 1.0f);
                if (submerged <= 0.0f)
                    continue;
                // Kaldırma: yoğunluk * batan alan * |g| (AABB alanının dolu kısmı ~ %70 varsayımı)
                float area = width * height * submerged * 0.7f;
                glm::vec2 buoyancy = -m_Gravity * fx.Density * area;
                glm::vec2 v = G(b2Body_GetLinearVelocity(b));
                float mass = b2Body_GetMass(b);
                glm::vec2 drag = -v * fx.LinearDrag * submerged * mass;
                float flowAngle = glm::radians(fx.FlowAngle);
                glm::vec2 flow = glm::vec2(std::cos(flowAngle), std::sin(flowAngle)) * fx.FlowMagnitude * submerged;
                b2Body_ApplyForceToCenter(b, B(buoyancy + drag + flow), true);
                b2Body_ApplyTorque(b, -b2Body_GetAngularVelocity(b) * fx.AngularDrag * submerged * b2Body_GetRotationalInertia(b), true);
            }
        }
    }

    void PhysicsWorld2D::PostStep(float dt)
    {
        (void)dt;
        for (auto& [owner, rec] : m_Bodies)
        {
            if (rec.Type != b2_dynamicBody || !b2Body_IsValid(rec.Body))
                continue;
            rec.PrevPos = rec.CurrPos;
            rec.PrevAngle = rec.CurrAngle;
            rec.CurrPos = G(b2Body_GetPosition(rec.Body));
            rec.CurrAngle = b2Rot_GetAngle(b2Body_GetRotation(rec.Body));
        }
        CollectEvents();
        // Eklem kırılması ve çalışma zamanı değerleri
        std::vector<std::pair<uint32_t, float>> broken;
        for (auto& [entity, list] : m_Joints)
        {
            if (!Valid(entity)) continue;
            Entity e{ (entt::entity)entity, m_Scene };
            for (auto it = list.begin(); it != list.end();)
            {
                if (!b2Joint_IsValid(it->Joint)) { it = list.erase(it); continue; }
                if (it->Kind == kHinge && e.HasComponent<HingeJoint2DComponent>())
                    e.GetComponent<HingeJoint2DComponent>().CurrentAngle = glm::degrees(b2RevoluteJoint_GetAngle(it->Joint));
                if (it->Kind == kSlider && e.HasComponent<SliderJoint2DComponent>())
                    e.GetComponent<SliderJoint2DComponent>().CurrentTranslation = b2PrismaticJoint_GetTranslation(it->Joint);
                if (it->BreakForce > 0.0f || it->BreakTorque > 0.0f)
                {
                    float force = glm::length(G(b2Joint_GetConstraintForce(it->Joint)));
                    float torque = std::fabs(b2Joint_GetConstraintTorque(it->Joint));
                    if ((it->BreakForce > 0.0f && force > it->BreakForce) || (it->BreakTorque > 0.0f && torque > it->BreakTorque))
                    {
                        b2DestroyJoint(it->Joint, true);
                        Joint2DCommon* c = nullptr;
                        switch (it->Kind)
                        {
                            case kDistance: c = e.HasComponent<DistanceJoint2DComponent>() ? &e.GetComponent<DistanceJoint2DComponent>() : nullptr; break;
                            case kSpring: c = e.HasComponent<SpringJoint2DComponent>() ? &e.GetComponent<SpringJoint2DComponent>() : nullptr; break;
                            case kHinge: c = e.HasComponent<HingeJoint2DComponent>() ? &e.GetComponent<HingeJoint2DComponent>() : nullptr; break;
                            case kSlider: c = e.HasComponent<SliderJoint2DComponent>() ? &e.GetComponent<SliderJoint2DComponent>() : nullptr; break;
                            case kWheel: c = e.HasComponent<WheelJoint2DComponent>() ? &e.GetComponent<WheelJoint2DComponent>() : nullptr; break;
                            case kFixed: c = e.HasComponent<FixedJoint2DComponent>() ? &e.GetComponent<FixedJoint2DComponent>() : nullptr; break;
                            case kFriction: c = e.HasComponent<FrictionJoint2DComponent>() ? &e.GetComponent<FrictionJoint2DComponent>() : nullptr; break;
                            case kRelative: c = e.HasComponent<RelativeJoint2DComponent>() ? &e.GetComponent<RelativeJoint2DComponent>() : nullptr; break;
                            case kTarget:
                                if (e.HasComponent<TargetJoint2DComponent>()) e.GetComponent<TargetJoint2DComponent>().Broken = true;
                                break;
                        }
                        if (c) c->Broken = true;
                        broken.emplace_back(entity, std::max(force, torque));
                        it = list.erase(it);
                        continue;
                    }
                }
                ++it;
            }
            if (!broken.empty() && broken.back().first == entity)
                m_JointSignatures[entity] = ComputeJointSignature(entity);
        }
        for (auto& [entity, force] : broken)
            if (OnJointBreak) OnJointBreak(entity, force);
    }

    void PhysicsWorld2D::CollectEvents()
    {
        auto ownerOfShape = [&](b2ShapeId s) -> uint32_t {
            if (!b2Shape_IsValid(s)) return NoEntity;
            uint32_t shapeEntity = DecodeEntity(b2Shape_GetUserData(s));
            auto so = m_ShapeOwner.find(shapeEntity);
            return so != m_ShapeOwner.end() ? so->second : shapeEntity;
        };
        std::unordered_map<uint64_t, ContactEvent2D> entered;
        b2ContactEvents ce = b2World_GetContactEvents(m_WorldId);
        for (int i = 0; i < ce.beginCount; ++i)
        {
            const auto& ev = ce.beginEvents[i];
            uint32_t a = ownerOfShape(ev.shapeIdA), b = ownerOfShape(ev.shapeIdB);
            if (a == NoEntity || b == NoEntity || a == b) continue;
            uint64_t key = PairKey(a, b);
            if (m_ActivePairs.count(key)) continue;
            ContactEvent2D out;
            out.Type = ContactEvent2D::Enter;
            out.EntityA = a;
            out.EntityB = b;
            if (b2Contact_IsValid(ev.contactId))
            {
                b2ContactData data = b2Contact_GetData(ev.contactId);
                out.Normal = G(data.manifold.normal);
                if (data.manifold.pointCount > 0)
                {
                    out.Point = G(data.manifold.points[0].clipPoint);
                    out.Impulse = data.manifold.points[0].normalImpulse;
                }
            }
            m_ActivePairs[key] = { a, b, false };
            m_PendingEvents.push_back(out);
        }
        for (int i = 0; i < ce.endCount; ++i)
        {
            const auto& ev = ce.endEvents[i];
            uint32_t a = ownerOfShape(ev.shapeIdA), b = ownerOfShape(ev.shapeIdB);
            if (a == NoEntity || b == NoEntity) continue;
            uint64_t key = PairKey(a, b);
            auto it = m_ActivePairs.find(key);
            if (it == m_ActivePairs.end() || it->second.Trigger) continue;
            // Aynı çiftin başka şekilleri hâlâ temas ediyor olabilir: gövdeler gerçekten ayrıldı mı?
            bool stillTouching = false;
            b2BodyId ba = BodyOf(a), bb = BodyOf(b);
            if (B2_IS_NON_NULL(ba) && B2_IS_NON_NULL(bb))
            {
                int cap = b2Body_GetContactCapacity(ba);
                std::vector<b2ContactData> contacts((size_t)std::max(1, cap));
                int n = b2Body_GetContactData(ba, contacts.data(), cap);
                for (int k = 0; k < n && !stillTouching; ++k)
                {
                    b2BodyId x = b2Shape_GetBody(contacts[(size_t)k].shapeIdA), y = b2Shape_GetBody(contacts[(size_t)k].shapeIdB);
                    if ((B2_ID_EQUALS(x, bb) || B2_ID_EQUALS(y, bb)) && contacts[(size_t)k].manifold.pointCount > 0)
                        stillTouching = true;
                }
            }
            if (stillTouching) continue;
            ContactEvent2D out;
            out.Type = ContactEvent2D::Exit;
            out.EntityA = it->second.A;
            out.EntityB = it->second.B;
            m_PendingEvents.push_back(out);
            m_ActivePairs.erase(it);
        }
        // Tetikleyiciler
        b2SensorEvents se = b2World_GetSensorEvents(m_WorldId);
        for (int i = 0; i < se.beginCount; ++i)
        {
            const auto& ev = se.beginEvents[i];
            if (!b2Shape_IsValid(ev.sensorShapeId)) continue;
            uint32_t sensorEntity = DecodeEntity(b2Shape_GetUserData(ev.sensorShapeId));
            uint32_t sensorOwner = ownerOfShape(ev.sensorShapeId);
            uint32_t visitor = ownerOfShape(ev.visitorShapeId);
            if (visitor == NoEntity || sensorOwner == NoEntity || visitor == sensorOwner) continue;
            if (m_SensorPairCounts[PairKey(sensorEntity, visitor) ^ 0x8000000000000000ULL]++ == 0)
                m_SensorVisitors[sensorEntity].insert(visitor);
            uint64_t key = PairKey(sensorOwner, visitor);
            if (m_ActivePairs.count(key)) continue;
            ContactEvent2D out;
            out.Type = ContactEvent2D::Enter;
            out.EntityA = sensorOwner;
            out.EntityB = visitor;
            out.Trigger = true;
            m_ActivePairs[key] = { sensorOwner, visitor, true };
            m_PendingEvents.push_back(out);
        }
        for (int i = 0; i < se.endCount; ++i)
        {
            const auto& ev = se.endEvents[i];
            uint32_t sensorEntity = b2Shape_IsValid(ev.sensorShapeId) ? DecodeEntity(b2Shape_GetUserData(ev.sensorShapeId)) : NoEntity;
            uint32_t sensorOwner = ownerOfShape(ev.sensorShapeId);
            uint32_t visitor = ownerOfShape(ev.visitorShapeId);
            if (visitor == NoEntity || sensorOwner == NoEntity) continue;
            auto pc = m_SensorPairCounts.find(PairKey(sensorEntity, visitor) ^ 0x8000000000000000ULL);
            if (pc != m_SensorPairCounts.end() && --pc->second <= 0)
            {
                m_SensorPairCounts.erase(pc);
                m_SensorVisitors[sensorEntity].erase(visitor);
            }
            uint64_t key = PairKey(sensorOwner, visitor);
            auto it = m_ActivePairs.find(key);
            if (it == m_ActivePairs.end() || !it->second.Trigger) continue;
            if (m_SensorVisitors[sensorEntity].count(visitor)) continue;
            ContactEvent2D out;
            out.Type = ContactEvent2D::Exit;
            out.EntityA = it->second.A;
            out.EntityB = it->second.B;
            out.Trigger = true;
            m_PendingEvents.push_back(out);
            m_ActivePairs.erase(it);
        }
        // Stay: bu adımda başlamayan tüm etkin çiftler
        for (auto& [key, pair] : m_ActivePairs)
        {
            bool justEntered = false;
            for (auto& ev : m_PendingEvents)
                if (ev.Type == ContactEvent2D::Enter && PairKey(ev.EntityA, ev.EntityB) == key) { justEntered = true; break; }
            if (justEntered) continue;
            ContactEvent2D out;
            out.Type = ContactEvent2D::Stay;
            out.EntityA = pair.A;
            out.EntityB = pair.B;
            out.Trigger = pair.Trigger;
            m_PendingEvents.push_back(out);
        }
        (void)entered;
    }

    void PhysicsWorld2D::WriteTransforms(float alpha)
    {
        for (auto& [owner, rec] : m_Bodies)
        {
            if (rec.Type != b2_dynamicBody || !Valid(owner))
                continue;
            glm::vec2 p = rec.Interpolate ? glm::mix(rec.PrevPos, rec.CurrPos, alpha) : rec.CurrPos;
            float da = std::remainder(rec.CurrAngle - rec.PrevAngle, glm::two_pi<float>());
            float a = rec.Interpolate ? rec.PrevAngle + da * alpha : rec.CurrAngle;
            SetWorldPose(owner, p, a);
            rec.LastWrittenPos = p;
            rec.LastWrittenAngle = a;
        }
    }

    // ════════════════════════════════════════
    //  API
    // ════════════════════════════════════════
    void PhysicsWorld2D::MarkDirty(Entity entity)
    {
        if (entity && B2_IS_NON_NULL(m_WorldId))
            m_Dirty.insert((uint32_t)entity);
    }

    void PhysicsWorld2D::OnEntityDestroyed(Entity entity)
    {
        if (!entity || B2_IS_NULL(m_WorldId))
            return;
        uint32_t e = (uint32_t)entity;
        RemoveJoints(e);
        m_JointSignatures.erase(e);
        auto so = m_ShapeOwner.find(e);
        if (so != m_ShapeOwner.end() && so->second != e)
            m_Dirty.insert(so->second);
        RemoveOwner(e);
        m_SensorVisitors.erase(e);
        for (auto& [je, sig] : m_JointSignatures)
            m_Dirty.insert(je);
    }

    void PhysicsWorld2D::CreateBody(Entity entity) { MarkDirty(entity); }
    void PhysicsWorld2D::DestroyBody(Entity entity) { if (entity && B2_IS_NON_NULL(m_WorldId)) RemoveOwner((uint32_t)entity); }

    void PhysicsWorld2D::SetGravity(const glm::vec2& gravity)
    {
        m_Gravity = gravity;
        if (B2_IS_NON_NULL(m_WorldId))
            b2World_SetGravity(m_WorldId, B(gravity));
    }

    glm::vec2 PhysicsWorld2D::GetGravity() const { return m_Gravity; }

    namespace
    {
        bool Dyn(b2BodyId b) { return B2_IS_NON_NULL(b) && b2Body_IsValid(b) && b2Body_GetType(b) == b2_dynamicBody; }
    }

    void PhysicsWorld2D::ApplyForce(Entity e, const glm::vec2& f, const glm::vec2& p) { ProcessDirty(); if (b2BodyId b = BodyOf((uint32_t)e); Dyn(b)) b2Body_ApplyForce(b, B(f), B(p), true); }
    void PhysicsWorld2D::ApplyForceToCenter(Entity e, const glm::vec2& f) { ProcessDirty(); if (b2BodyId b = BodyOf((uint32_t)e); Dyn(b)) b2Body_ApplyForceToCenter(b, B(f), true); }
    void PhysicsWorld2D::ApplyImpulse(Entity e, const glm::vec2& i, const glm::vec2& p) { ProcessDirty(); if (b2BodyId b = BodyOf((uint32_t)e); Dyn(b)) b2Body_ApplyLinearImpulse(b, B(i), B(p), true); }
    void PhysicsWorld2D::ApplyImpulseToCenter(Entity e, const glm::vec2& i) { ProcessDirty(); if (b2BodyId b = BodyOf((uint32_t)e); Dyn(b)) b2Body_ApplyLinearImpulseToCenter(b, B(i), true); }
    void PhysicsWorld2D::ApplyTorque(Entity e, float t) { ProcessDirty(); if (b2BodyId b = BodyOf((uint32_t)e); Dyn(b)) b2Body_ApplyTorque(b, t, true); }
    void PhysicsWorld2D::ApplyAngularImpulse(Entity e, float i) { ProcessDirty(); if (b2BodyId b = BodyOf((uint32_t)e); Dyn(b)) b2Body_ApplyAngularImpulse(b, i, true); }

    void PhysicsWorld2D::SetLinearVelocity(Entity e, const glm::vec2& v)
    {
        ProcessDirty();
        b2BodyId b = BodyOf((uint32_t)e);
        if (B2_IS_NON_NULL(b) && b2Body_IsValid(b) && b2Body_GetType(b) != b2_staticBody)
            b2Body_SetLinearVelocity(b, B(v));
    }
    glm::vec2 PhysicsWorld2D::GetLinearVelocity(Entity e) const
    {
        b2BodyId b = BodyOf((uint32_t)e);
        return B2_IS_NON_NULL(b) && b2Body_IsValid(b) ? G(b2Body_GetLinearVelocity(b)) : glm::vec2(0.0f);
    }
    void PhysicsWorld2D::SetAngularVelocity(Entity e, float w)
    {
        ProcessDirty();
        b2BodyId b = BodyOf((uint32_t)e);
        if (B2_IS_NON_NULL(b) && b2Body_IsValid(b) && b2Body_GetType(b) != b2_staticBody)
            b2Body_SetAngularVelocity(b, w);
    }
    float PhysicsWorld2D::GetAngularVelocity(Entity e) const
    {
        b2BodyId b = BodyOf((uint32_t)e);
        return B2_IS_NON_NULL(b) && b2Body_IsValid(b) ? b2Body_GetAngularVelocity(b) : 0.0f;
    }
    float PhysicsWorld2D::GetMass(Entity e) const
    {
        b2BodyId b = BodyOf((uint32_t)e);
        return Dyn(b) ? b2Body_GetMass(b) : 0.0f;
    }

    void PhysicsWorld2D::Teleport(Entity entity, const glm::vec2& position, float angle)
    {
        uint32_t e = (uint32_t)entity;
        SetWorldPose(e, position, angle);
        ProcessDirty();
        if (auto it = m_Bodies.find(e); it != m_Bodies.end() && b2Body_IsValid(it->second.Body))
        {
            b2Body_SetTransform(it->second.Body, B(position), b2MakeRot(angle));
            it->second.PrevPos = it->second.CurrPos = it->second.LastWrittenPos = position;
            it->second.PrevAngle = it->second.CurrAngle = it->second.LastWrittenAngle = angle;
        }
    }

    // ── Sorgular ──
    namespace
    {
        struct RayContext
        {
            uint32_t Ignore{};
            bool HitTriggers{};
            const std::unordered_map<uint32_t, uint32_t>* Owners{};
            std::vector<std::pair<float, RaycastHit2D>>* All{};
            float BestFraction = 1.0f;
            RaycastHit2D Best;
            bool Any = false;
            bool Closest = true;
        };

        float RayCallback(b2ShapeId shape, b2Vec2 point, b2Vec2 normal, float fraction, void* context)
        {
            auto* ctx = static_cast<RayContext*>(context);
            if (b2Shape_IsSensor(shape) && !ctx->HitTriggers)
                return -1.0f;
            uint32_t se = DecodeEntity(b2Shape_GetUserData(shape));
            auto it = ctx->Owners->find(se);
            uint32_t owner = it != ctx->Owners->end() ? it->second : se;
            if (owner == ctx->Ignore || se == ctx->Ignore)
                return -1.0f;
            RaycastHit2D h;
            h.Entity = owner;
            h.Point = G(point);
            h.Normal = G(normal);
            h.IsTrigger = b2Shape_IsSensor(shape);
            if (!ctx->Closest)
            {
                ctx->All->push_back({ fraction, h });
                return 1.0f;
            }
            if (fraction < ctx->BestFraction)
            {
                ctx->BestFraction = fraction;
                ctx->Best = h;
                ctx->Any = true;
            }
            return fraction;
        }

        struct OverlapContext
        {
            const std::unordered_map<uint32_t, uint32_t>* Owners;
            bool HitTriggers;
            std::set<uint32_t> Found;
        };

        bool OverlapCallback(b2ShapeId shape, void* context)
        {
            auto* ctx = static_cast<OverlapContext*>(context);
            if (b2Shape_IsSensor(shape) && !ctx->HitTriggers)
                return true;
            uint32_t se = DecodeEntity(b2Shape_GetUserData(shape));
            auto it = ctx->Owners->find(se);
            ctx->Found.insert(it != ctx->Owners->end() ? it->second : se);
            return true;
        }
    }

    bool PhysicsWorld2D::Raycast(const glm::vec2& origin, const glm::vec2& direction, float maxDistance, RaycastHit2D& hit, uint32_t layerMask, uint32_t ignoreEntity)
    {
        if (B2_IS_NULL(m_WorldId) || glm::length(direction) < 1e-8f)
            return false;
        ProcessDirty();
        glm::vec2 d = glm::normalize(direction);
        maxDistance = std::min(maxDistance, 1.0e6f);
        b2QueryFilter filter = b2DefaultQueryFilter();
        filter.maskBits = layerMask;
        RayContext ctx{ ignoreEntity, PhysicsSettings::Get().QueriesHitTriggers, &m_ShapeOwner, nullptr };
        b2World_CastRay(m_WorldId, B(origin), B(d * maxDistance), filter, RayCallback, &ctx);
        if (!ctx.Any)
            return false;
        hit = ctx.Best;
        hit.Distance = ctx.BestFraction * maxDistance;
        return true;
    }

    void PhysicsWorld2D::RaycastAll(const glm::vec2& origin, const glm::vec2& direction, float maxDistance, std::vector<RaycastHit2D>& hits, uint32_t layerMask)
    {
        hits.clear();
        if (B2_IS_NULL(m_WorldId) || glm::length(direction) < 1e-8f)
            return;
        ProcessDirty();
        glm::vec2 d = glm::normalize(direction);
        maxDistance = std::min(maxDistance, 1.0e6f);
        b2QueryFilter filter = b2DefaultQueryFilter();
        filter.maskBits = layerMask;
        std::vector<std::pair<float, RaycastHit2D>> all;
        RayContext ctx{ NoEntity, PhysicsSettings::Get().QueriesHitTriggers, &m_ShapeOwner, &all };
        ctx.Closest = false;
        b2World_CastRay(m_WorldId, B(origin), B(d * maxDistance), filter, RayCallback, &ctx);
        std::sort(all.begin(), all.end(), [](auto& a, auto& b) { return a.first < b.first; });
        for (auto& [f, h] : all)
        {
            h.Distance = f * maxDistance;
            hits.push_back(h);
        }
    }

    void PhysicsWorld2D::OverlapCircle(const glm::vec2& center, float radius, std::vector<uint32_t>& entities, uint32_t layerMask)
    {
        entities.clear();
        if (B2_IS_NULL(m_WorldId)) return;
        ProcessDirty();
        b2Vec2 p = B(center);
        b2ShapeProxy proxy = b2MakeProxy(&p, 1, std::max(0.001f, radius));
        b2QueryFilter filter = b2DefaultQueryFilter();
        filter.maskBits = layerMask;
        OverlapContext ctx{ &m_ShapeOwner, PhysicsSettings::Get().QueriesHitTriggers, {} };
        b2World_OverlapShape(m_WorldId, &proxy, filter, OverlapCallback, &ctx);
        entities.assign(ctx.Found.begin(), ctx.Found.end());
    }

    void PhysicsWorld2D::OverlapBox(const glm::vec2& center, const glm::vec2& halfExtents, float angle, std::vector<uint32_t>& entities, uint32_t layerMask)
    {
        entities.clear();
        if (B2_IS_NULL(m_WorldId)) return;
        ProcessDirty();
        float c = std::cos(angle), s = std::sin(angle);
        glm::vec2 he = glm::abs(halfExtents);
        b2Vec2 pts[4];
        const glm::vec2 corners[4] = { { -he.x, -he.y }, { he.x, -he.y }, { he.x, he.y }, { -he.x, he.y } };
        for (int i = 0; i < 4; ++i)
            pts[i] = B(center + glm::vec2(c * corners[i].x - s * corners[i].y, s * corners[i].x + c * corners[i].y));
        b2ShapeProxy proxy = b2MakeProxy(pts, 4, 0.0f);
        b2QueryFilter filter = b2DefaultQueryFilter();
        filter.maskBits = layerMask;
        OverlapContext ctx{ &m_ShapeOwner, PhysicsSettings::Get().QueriesHitTriggers, {} };
        b2World_OverlapShape(m_WorldId, &proxy, filter, OverlapCallback, &ctx);
        entities.assign(ctx.Found.begin(), ctx.Found.end());
    }
}
