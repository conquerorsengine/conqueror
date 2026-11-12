#include "GodotNodes.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/Signals.h"
#include "Core/PhysicsSystem/PhysicsSettings.h"
#include "Scene/PhysicsComponents.h"
#include "Core/Time/TimeManager.h"
#include "Core/PhysicsSystem/Physics3D/PhysicsWorld3D.h"
#include "Core/PhysicsSystem/Physics2D/PhysicsWorld2D.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Renderer/Utilities/Renderer3D/Mesh.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"

#include <IconsFontAwesome6.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Conqueror
{
    namespace
    {
        struct TRS
        {
            glm::vec3 Position{ 0.0f };
            glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
            glm::vec3 Scale{ 1.0f };
        };

        TRS Decompose(const glm::mat4& m)
        {
            TRS t;
            glm::vec3 skew;
            glm::vec4 perspective;
            if (!glm::decompose(m, t.Scale, t.Rotation, t.Position, skew, perspective))
            {
                t = TRS{};
                t.Position = glm::vec3(m[3]);
            }
            return t;
        }

        glm::mat4 Compose(const TRS& t)
        {
            return glm::translate(glm::mat4(1.0f), t.Position) * glm::mat4_cast(t.Rotation) * glm::scale(glm::mat4(1.0f), t.Scale);
        }

        void UpdateRemoteTransforms(Scene& scene)
        {
            auto view = scene.m_Registry.view<TransformComponent, RemoteTransformComponent>();
            for (auto e : view)
            {
                auto& rt = view.get<RemoteTransformComponent>(e);
                if (!rt.Target || !scene.IsEntityActive(e))
                    continue;
                Entity target = scene.FindEntityByUUID(rt.Target);
                if (!target || (entt::entity)target == e)
                    continue;
                auto& dst = target.GetComponent<TransformComponent>();
                if (!rt.UseGlobalCoordinates)
                {
                    const auto& src = view.get<TransformComponent>(e);
                    if (rt.UpdatePosition) dst.Position = src.Position;
                    if (rt.UpdateRotation) dst.Rotation = src.Rotation;
                    if (rt.UpdateScale) dst.Scale = src.Scale;
                    continue;
                }
                // Dünya uzayında eşle, sonra hedefin atasına göre yerel dönüşüme çevir
                const TRS source = Decompose(scene.ComputeWorldMatrix(e));
                TRS desired = Decompose(scene.ComputeWorldMatrix((entt::entity)target));
                if (rt.UpdatePosition) desired.Position = source.Position;
                if (rt.UpdateRotation) desired.Rotation = source.Rotation;
                if (rt.UpdateScale) desired.Scale = source.Scale;
                glm::mat4 local = Compose(desired);
                if (Entity parent = target.GetParent())
                    local = glm::inverse(scene.ComputeWorldMatrix((entt::entity)parent)) * local;
                const TRS l = Decompose(local);
                if (rt.UpdatePosition) dst.Position = l.Position;
                if (rt.UpdateRotation) dst.Rotation = glm::eulerAngles(l.Rotation);
                if (rt.UpdateScale) dst.Scale = l.Scale;
            }
        }

        bool CastRay(Scene& scene, entt::entity e, RayCastComponent& rc)
        {
            rc.Colliding = false;
            rc.ColliderEntity = 0xFFFFFFFFu;
            rc.Collider.clear();
            const glm::mat4 world = scene.ComputeWorldMatrix(e);
            const glm::vec3 from = glm::vec3(world[3]);
            const glm::vec3 to = glm::vec3(world * glm::vec4(rc.TargetPosition, 1.0f));
            const glm::vec3 delta = to - from;
            const float length = glm::length(delta);
            if (length < 1e-6f)
                return false;
            const glm::vec3 dir = delta / length;
            uint32_t ignoreA = 0xFFFFFFFFu, ignoreB = 0xFFFFFFFFu;
            if (rc.ExcludeParent)
            {
                ignoreA = (uint32_t)e;
                if (Entity parent = Entity{ e, &scene }.GetParent())
                    ignoreB = (uint32_t)(entt::entity)parent;
            }
            bool found = false;
            float best = 1e30f;
            if (rc.Space == RayCastSpace::Physics2D)
            {
                PhysicsWorld2D* physics = scene.GetPhysicsWorld2D();
                if (!physics)
                    return false;
                static thread_local std::vector<RaycastHit2D> s_Hits;
                s_Hits.clear();
                const glm::vec2 dir2 = glm::vec2(delta);
                const float len2 = glm::length(dir2);
                if (len2 < 1e-6f)
                    return false;
                physics->RaycastAll(glm::vec2(from), dir2 / len2, len2, s_Hits, rc.CollisionMask);
                for (const auto& h : s_Hits)
                {
                    if (h.Entity == ignoreA || h.Entity == ignoreB || (h.IsTrigger && !rc.HitTriggers) || h.Distance >= best)
                        continue;
                    best = h.Distance;
                    found = true;
                    rc.CollisionPoint = glm::vec3(h.Point, from.z);
                    rc.CollisionNormal = glm::vec3(h.Normal, 0.0f);
                    rc.Distance = h.Distance;
                    rc.ColliderEntity = h.Entity;
                }
            }
            else
            {
                PhysicsWorld3D* physics = scene.GetPhysicsWorld3D();
                if (!physics)
                    return false;
                static thread_local std::vector<Physics3D::RaycastHit> s_Hits;
                s_Hits.clear();
                physics->RaycastAll(from, dir, length, s_Hits, rc.CollisionMask);
                for (const auto& h : s_Hits)
                {
                    if (h.Entity == ignoreA || h.Entity == ignoreB || (h.IsTrigger && !rc.HitTriggers) || h.Distance >= best)
                        continue;
                    best = h.Distance;
                    found = true;
                    rc.CollisionPoint = h.Point;
                    rc.CollisionNormal = h.Normal;
                    rc.Distance = h.Distance;
                    rc.ColliderEntity = h.Entity;
                }
            }
            rc.Colliding = found;
            if (found)
            {
                const auto hitEntity = (entt::entity)rc.ColliderEntity;
                if (scene.m_Registry.valid(hitEntity) && scene.m_Registry.all_of<TagComponent>(hitEntity))
                    rc.Collider = scene.m_Registry.get<TagComponent>(hitEntity).Tag;
            }
            return found;
        }

        bool CastShape(Scene& scene, entt::entity e, ShapeCastComponent& sc)
        {
            sc.Results.clear();
            sc.Colliding = false;
            sc.CollisionCount = 0;
            sc.SafeFraction = 1.0f;
            sc.Collider.clear();
            const glm::mat4 world = scene.ComputeWorldMatrix(e);
            const glm::vec3 from = glm::vec3(world[3]);
            const glm::vec3 to = glm::vec3(world * glm::vec4(sc.TargetPosition, 1.0f));
            const glm::vec3 delta = to - from;
            float length = glm::length(delta);
            // Hedef kendisiyse yerinde (yalnız iç içe olanlar): çok kısa bir süpürme
            glm::vec3 dir = length > 1e-6f ? delta / length : glm::vec3(0.0f, -1.0f, 0.0f);
            if (length < 1e-6f)
                length = 1e-4f;
            // Nesnenin dönüşü ve ölçeği şekle uygulanır
            glm::vec3 scale, translation, skew;
            glm::vec4 perspective;
            glm::quat rotation;
            glm::decompose(world, scale, rotation, translation, skew, perspective);
            scale = glm::abs(scale);
            uint32_t ignoreA = 0xFFFFFFFFu, ignoreB = 0xFFFFFFFFu;
            if (sc.ExcludeParent)
            {
                ignoreA = (uint32_t)e;
                if (Entity parent = Entity{ e, &scene }.GetParent())
                    ignoreB = (uint32_t)(entt::entity)parent;
            }
            const int maxResults = std::clamp(sc.MaxResults, 1, 256);
            if (sc.Space == RayCastSpace::Physics2D)
            {
                PhysicsWorld2D* physics = scene.GetPhysicsWorld2D();
                if (!physics)
                    return false;
                const float s2 = std::max(scale.x, scale.y);
                const float angle = glm::eulerAngles(rotation).z;
                const glm::vec2 c(from), ax(std::cos(angle), std::sin(angle)), ay(-std::sin(angle), std::cos(angle));
                glm::vec2 pts[4];
                int count = 1;
                float radius = sc.Radius * s2;
                if (sc.Shape == ShapeCastShape::Box)
                {
                    const glm::vec2 h = glm::vec2(sc.Size) * 0.5f * glm::vec2(scale);
                    pts[0] = c - ax * h.x - ay * h.y; pts[1] = c + ax * h.x - ay * h.y;
                    pts[2] = c + ax * h.x + ay * h.y; pts[3] = c - ax * h.x + ay * h.y;
                    count = 4;
                    radius = 0.0f;
                }
                else if (sc.Shape == ShapeCastShape::Capsule)
                {
                    const float half = std::max(sc.Height * 0.5f * scale.y - radius, 0.0f);
                    pts[0] = c - ay * half;
                    pts[1] = c + ay * half;
                    count = 2;
                }
                else
                    pts[0] = c;
                static thread_local std::vector<RaycastHit2D> s_Hits;
                physics->ShapeCastAll(pts, count, radius, glm::vec2(dir) * length, s_Hits, sc.CollisionMask, sc.HitTriggers, maxResults + 2);
                for (const auto& h : s_Hits)
                {
                    if (h.Entity == ignoreA || h.Entity == ignoreB || (int)sc.Results.size() >= maxResults)
                        continue;
                    sc.Results.push_back({ glm::vec3(h.Point, from.z), glm::vec3(h.Normal, 0.0f), h.Distance, h.Entity });
                }
            }
            else
            {
                PhysicsWorld3D* physics = scene.GetPhysicsWorld3D();
                if (!physics)
                    return false;
                const float sr = std::max({ scale.x, scale.y, scale.z });
                const auto type = sc.Shape == ShapeCastShape::Box ? Physics3D::Backend::CastShape::Box
                                : sc.Shape == ShapeCastShape::Capsule ? Physics3D::Backend::CastShape::Capsule : Physics3D::Backend::CastShape::Sphere;
                const float radius = sc.Radius * (sc.Shape == ShapeCastShape::Capsule ? std::max(scale.x, scale.z) : sr);
                const float halfHeight = std::max(sc.Height * 0.5f * scale.y - radius, 0.001f);
                static thread_local std::vector<Physics3D::RaycastHit> s_Hits;
                physics->ShapeCastAll(type, sc.Size * 0.5f * scale, radius, halfHeight, rotation, from, dir, length, s_Hits, sc.CollisionMask,
                                      sc.HitTriggers, maxResults + 2);
                for (const auto& h : s_Hits)
                {
                    if (h.Entity == ignoreA || h.Entity == ignoreB || (int)sc.Results.size() >= maxResults)
                        continue;
                    sc.Results.push_back({ h.Point, h.Normal, h.Distance, h.Entity });
                }
            }
            sc.CollisionCount = (int)sc.Results.size();
            sc.Colliding = sc.CollisionCount > 0;
            if (sc.Colliding)
            {
                sc.SafeFraction = std::clamp(sc.Results[0].Distance / std::max(length, 1e-6f), 0.0f, 1.0f);
                const auto hitEntity = (entt::entity)sc.Results[0].Entity;
                if (scene.m_Registry.valid(hitEntity) && scene.m_Registry.all_of<TagComponent>(hitEntity))
                    sc.Collider = scene.m_Registry.get<TagComponent>(hitEntity).Tag;
            }
            return sc.Colliding;
        }

        // Yerel sınır küresi (merkez, yarıçap)
        bool LocalBounds(Scene& scene, entt::entity e, const VisibleOnScreenNotifierComponent& n, glm::vec3& center, float& radius)
        {
            if (n.AutoBounds)
            {
                if (auto* mr = scene.m_Registry.try_get<MeshRendererComponent>(e); mr && !mr->IsModel())
                {
                    std::shared_ptr<Mesh> mesh = mr->CustomMesh;
                    if (!mesh)
                    {
                        switch (mr->Type)
                        {
                            case MeshType::Sphere:   mesh = Renderer3D::GetSphereMesh(); break;
                            case MeshType::Plane:    mesh = Renderer3D::GetPlaneMesh(); break;
                            case MeshType::Cylinder: mesh = Renderer3D::GetCylinderMesh(); break;
                            default:                 mesh = Renderer3D::GetCubeMesh(); break;
                        }
                    }
                    if (mesh)
                    {
                        center = mesh->GetBoundsCenter();
                        radius = mesh->GetBoundsRadius();
                        return true;
                    }
                }
                if (auto* mc = scene.m_Registry.try_get<MeshRendererComponent>(e); mc && mc->ModelData && !mc->ModelData->Meshes.empty())
                {
                    glm::vec3 mn(1e30f), mx(-1e30f);
                    for (const auto& m : mc->ModelData->Meshes)
                    {
                        if (!m) continue;
                        mn = glm::min(mn, m->GetBoundsCenter() - glm::vec3(m->GetBoundsRadius()));
                        mx = glm::max(mx, m->GetBoundsCenter() + glm::vec3(m->GetBoundsRadius()));
                    }
                    if (mn.x <= mx.x)
                    {
                        center = (mn + mx) * 0.5f;
                        radius = glm::length(mx - mn) * 0.5f;
                        return true;
                    }
                }
                if (scene.m_Registry.all_of<SpriteRendererComponent>(e))
                {
                    center = glm::vec3(0.0f);
                    radius = 0.7072f;   // birim dörtgenin köşegeni / 2
                    return true;
                }
            }
            center = n.BoundsCenter;
            radius = glm::length(n.BoundsSize) * 0.5f;
            return true;
        }

        void UpdateNotifiers(Scene& scene)
        {
            auto view = scene.m_Registry.view<TransformComponent, VisibleOnScreenNotifierComponent>();
            auto enablerView = scene.m_Registry.view<VisibleOnScreenEnablerComponent>();
            if (view.begin() == view.end() && enablerView.begin() == enablerView.end())
                return;
            Entity cameraEntity = scene.GetPrimaryCameraEntity();
            if (!cameraEntity || !cameraEntity.HasComponent<CameraComponent>())
                return;
            const glm::mat4 projection = cameraEntity.GetComponent<CameraComponent>().Camera.GetProjection();
            const glm::mat4 viewMatrix = glm::inverse(scene.ComputeWorldMatrix((entt::entity)cameraEntity));
            glm::vec4 planes[6];
            Renderer3D::ExtractFrustum(projection * viewMatrix, planes);

            std::vector<std::pair<uint32_t, bool>> changed;
            for (auto e : view)
            {
                auto& n = view.get<VisibleOnScreenNotifierComponent>(e);
                bool visible = false;
                if (scene.IsEntityActive(e))
                {
                    glm::vec3 center;
                    float radius = 0.5f;
                    LocalBounds(scene, e, n, center, radius);
                    const glm::mat4 world = scene.ComputeWorldMatrix(e);
                    const float scale = std::max({ glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])) });
                    const glm::vec3 c = glm::vec3(world * glm::vec4(center, 1.0f));
                    const float r = radius * scale;
                    visible = true;
                    for (int p = 0; p < 6 && visible; ++p)
                        if (glm::dot(glm::vec3(planes[p]), c) + planes[p].w < -r)
                            visible = false;
                }
                if (!n.Initialized || visible != n.OnScreen)
                {
                    // İlk karede yalnızca görünürse bildir (Godot: başlangıçta ekrandaysa screen_entered gelir)
                    if (n.Initialized || visible)
                        changed.push_back({ (uint32_t)e, visible });
                    n.Initialized = true;
                    n.OnScreen = visible;
                }
            }
            // Enabler: aynı görünürlük testi; ekran dışında hedef işlenmez
            auto enablers = scene.m_Registry.view<TransformComponent, VisibleOnScreenEnablerComponent>();
            for (auto e : enablers)
            {
                auto& en = enablers.get<VisibleOnScreenEnablerComponent>(e);
                uint32_t target = 0xFFFFFFFFu;
                if (en.Target)
                {
                    if (Entity t = scene.FindEntityByUUID(en.Target))
                        target = (uint32_t)(entt::entity)t;
                }
                else if (Entity parent = Entity{ e, &scene }.GetParent())
                    target = (uint32_t)(entt::entity)parent;
                if (target != en.AppliedTo && en.AppliedTo != 0xFFFFFFFFu)
                    scene.SetProcessDisabled(en.AppliedTo, false);   // hedef değişti: eskisi serbest
                bool visible = false;
                if (scene.IsEntityActive(e))
                {
                    VisibleOnScreenNotifierComponent bounds;
                    bounds.AutoBounds = en.AutoBounds;
                    bounds.BoundsCenter = en.BoundsCenter;
                    bounds.BoundsSize = en.BoundsSize;
                    glm::vec3 center;
                    float radius = 0.5f;
                    LocalBounds(scene, e, bounds, center, radius);
                    const glm::mat4 world = scene.ComputeWorldMatrix(e);
                    const float scale = std::max({ glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])) });
                    const glm::vec3 c = glm::vec3(world * glm::vec4(center, 1.0f));
                    const float r = radius * scale;
                    visible = true;
                    for (int p = 0; p < 6 && visible; ++p)
                        if (glm::dot(glm::vec3(planes[p]), c) + planes[p].w < -r)
                            visible = false;
                }
                en.OnScreen = visible;
                en.Initialized = true;
                en.AppliedTo = target;
                if (target != 0xFFFFFFFFu)
                    scene.SetProcessDisabled(target, !visible);
            }
            // Betikler varlık silebilir: bildirimler toplu yapılır
            for (const auto& [id, visible] : changed)
            {
                if (!scene.m_Registry.valid((entt::entity)id))
                    continue;
                Signals::Emit(scene, id, visible ? "screen_entered" : "screen_exited", nullptr, 0);
                if (scene.m_Registry.valid((entt::entity)id))
                    scene.InvokeScriptMethod(id, visible ? "OnBecameVisible" : "OnBecameInvisible", nullptr, 0);
            }
        }

        void UpdateParallax(Scene& scene, float dt)
        {
            auto view = scene.m_Registry.view<TransformComponent, Parallax2DComponent>();
            if (view.begin() == view.end())
                return;
            Entity cameraEntity = scene.GetPrimaryCameraEntity();
            if (!cameraEntity)
                return;
            const glm::vec2 camera = glm::vec2(scene.ComputeWorldMatrix((entt::entity)cameraEntity)[3]);
            for (auto e : view)
            {
                auto [tc, p] = view.get<TransformComponent, Parallax2DComponent>(e);
                if (!p.HasBase)
                {
                    p.HasBase = true;
                    p.BasePosition = tc.Position;
                    p.CameraStart = camera;
                    p.AutoOffset = glm::vec2(0.0f);
                }
                p.AutoOffset += p.Autoscroll * dt;
                // Kamera hareketinin (1 - ölçek) kadarı katmana eklenir: 0.5 => kameranın yarı hızında kayar
                glm::vec2 pos = glm::vec2(p.BasePosition) + (camera - p.CameraStart) * (glm::vec2(1.0f) - p.ScrollScale) + p.ScrollOffset + p.AutoOffset;
                for (int axis = 0; axis < 2; ++axis)
                {
                    const float size = p.RepeatSize[axis];
                    if (size > 0.0f)
                    {
                        // Katmanı kameraya en yakın tekrar konumuna kaydır (kopyalar RepeatSize aralığıyla dizilir)
                        const float rel = camera[axis] - pos[axis];
                        pos[axis] += std::round(rel / size) * size;
                    }
                }
                tc.Position.x = pos.x;
                tc.Position.y = pos.y;
            }
        }
    }

    namespace
    {
        // Karakterin kapsülü (dünya): nesnenin 2D çarpıştırıcısından; yoksa varsayılan kapsül
        void CharacterCapsule(Scene& scene, entt::entity e, glm::vec2& c1, glm::vec2& c2, float& radius, glm::vec2& center, float& angle)
        {
            const glm::mat4 world = scene.ComputeWorldMatrix(e);
            center = glm::vec2(world[3]);
            angle = std::atan2(world[0].y, world[0].x);
            const glm::vec2 sx(glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])));
            const glm::vec2 ax(std::cos(angle), std::sin(angle)), ay(-std::sin(angle), std::cos(angle));
            glm::vec2 offset(0.0f), axis(0.0f, 1.0f);
            float half = 0.4f;
            radius = 0.4f;
            auto& reg = scene.m_Registry;
            if (const auto* cap = reg.try_get<CapsuleCollider2DComponent>(e))
            {
                const glm::vec2 size = glm::abs(cap->Size) * sx;
                offset = cap->Offset * sx;
                if (cap->Direction == 0) { radius = size.x * 0.5f; half = std::max(size.y * 0.5f - radius, 0.0f); axis = glm::vec2(0.0f, 1.0f); }
                else { radius = size.y * 0.5f; half = std::max(size.x * 0.5f - radius, 0.0f); axis = glm::vec2(1.0f, 0.0f); }
            }
            else if (const auto* circle = reg.try_get<CircleCollider2DComponent>(e))
            {
                radius = std::abs(circle->Radius) * std::max(sx.x, sx.y);
                offset = circle->Offset * sx;
                half = 0.0f;
            }
            else if (const auto* box = reg.try_get<BoxCollider2DComponent>(e))
            {
                // Kutu: kısa kenarı çap olan kapsül (köşeler yuvarlanır: basamak / eğimde takılmaz)
                const glm::vec2 size = glm::abs(box->Size) * sx;
                offset = box->Offset * sx;
                if (size.y >= size.x) { radius = size.x * 0.5f; half = std::max(size.y * 0.5f - radius, 0.0f); axis = glm::vec2(0.0f, 1.0f); }
                else { radius = size.y * 0.5f; half = std::max(size.x * 0.5f - radius, 0.0f); axis = glm::vec2(1.0f, 0.0f); }
            }
            const glm::vec2 o = center + ax * offset.x + ay * offset.y;
            const glm::vec2 a = ax * axis.x + ay * axis.y;
            c1 = o - a * half;
            c2 = o + a * half;
            radius = std::max(radius, 0.01f);
        }

        // Zamanlayıcıları say; dolanlar toplu bildirilir (betik nesne silebilir)
        void UpdateTimers(Scene& scene, float dt, float unscaledDt, TimerProcess mode)
        {
            auto view = scene.m_Registry.view<TimerComponent>();
            if (view.begin() == view.end())
                return;
            std::vector<uint32_t> fired;
            for (auto e : view)
            {
                auto& t = view.get<TimerComponent>(e);
                if (t.ProcessCallback != mode || !scene.IsEntityActive(e) || scene.IsProcessDisabled(e))
                    continue;
                if (t.Autostart && !t.AutostartDone)
                {
                    t.AutostartDone = true;
                    t.Running = true;
                    t.TimeLeft = std::max(t.WaitTime, 0.0f);
                }
                if (!t.Running || t.Paused)
                    continue;
                t.TimeLeft -= t.IgnoreTimeScale ? unscaledDt : dt;
                if (t.TimeLeft > 0.0f)
                    continue;
                ++t.Timeouts;
                fired.push_back((uint32_t)e);
                if (t.OneShot || t.WaitTime <= 0.0f)
                {
                    t.Running = false;
                    t.TimeLeft = 0.0f;
                }
                else
                {
                    // Taşan süre bir sonraki tura sayılır (kare hızından bağımsız aralık); çok uzun karede tek bildirim
                    t.TimeLeft += t.WaitTime;
                    if (t.TimeLeft <= 0.0f)
                        t.TimeLeft = t.WaitTime;
                }
            }
            for (uint32_t id : fired)
            {
                if (!scene.m_Registry.valid((entt::entity)id))
                    continue;
                Signals::Emit(scene, id, "timeout", nullptr, 0);
                if (scene.m_Registry.valid((entt::entity)id))
                    scene.InvokeScriptMethod(id, "OnTimeout", nullptr, 0);
            }
        }
    }

    namespace GodotNodes
    {
        void FixedUpdate(Scene& scene, float dt)
        {
            const float scale = TimeManager::GetTimeScale();
            UpdateTimers(scene, dt, scale > 1e-6f ? dt / scale : dt, TimerProcess::Physics);
        }

        bool TimerStart(Scene& scene, uint32_t entity, float time)
        {
            const auto e = (entt::entity)entity;
            if (!scene.m_Registry.valid(e) || !scene.m_Registry.all_of<TimerComponent>(e))
                return false;
            auto& t = scene.m_Registry.get<TimerComponent>(e);
            if (time > 0.0f)
                t.WaitTime = time;
            t.TimeLeft = std::max(t.WaitTime, 0.0f);
            t.Running = true;
            t.AutostartDone = true;
            return true;
        }

        bool TimerStop(Scene& scene, uint32_t entity)
        {
            const auto e = (entt::entity)entity;
            if (!scene.m_Registry.valid(e) || !scene.m_Registry.all_of<TimerComponent>(e))
                return false;
            auto& t = scene.m_Registry.get<TimerComponent>(e);
            t.Running = false;
            t.TimeLeft = 0.0f;
            t.AutostartDone = true;
            return true;
        }

        void Update(Scene& scene, float dt, bool runtime)
        {
            UpdateRemoteTransforms(scene);
            if (!runtime)
                return;
            // Ekran bildirimleri ve Enabler önce: bu karede işlenmeyecek hedefler (Timer, betikler) baştan bilinsin
            UpdateNotifiers(scene);
            UpdateTimers(scene, dt, TimeManager::GetUnscaledDeltaTime(), TimerProcess::Idle);
            // Sabit adım yoksa (yalnız 2D sahne) Physics zamanlayıcıları da karede
            if (!scene.GetPhysicsWorld3D())
                UpdateTimers(scene, dt, TimeManager::GetUnscaledDeltaTime(), TimerProcess::Physics);
            auto rays = scene.m_Registry.view<RayCastComponent>();
            for (auto e : rays)
            {
                auto& rc = rays.get<RayCastComponent>(e);
                if (rc.Enabled && scene.IsEntityActive(e))
                    CastRay(scene, e, rc);
                else
                    rc.Colliding = false;
            }
            // Character Body 2D (Auto Move): betiksiz karakterler yerçekimi + Velocity ile ilerler
            {
                std::vector<uint32_t> movers;
                for (auto e : scene.m_Registry.view<CharacterBody2DComponent>())
                {
                    auto& cb = scene.m_Registry.get<CharacterBody2DComponent>(e);
                    if (!cb.AutoMove || !scene.IsEntityActive(e) || scene.IsProcessDisabled(e))
                        continue;
                    if (!cb.OnFloor || cb.MotionMode == CharacterMotionMode::Floating)
                        cb.Velocity += PhysicsSettings::Get().Gravity2D * cb.GravityScale * dt * (cb.MotionMode == CharacterMotionMode::Floating ? 0.0f : 1.0f);
                    movers.push_back((uint32_t)e);
                }
                for (uint32_t id : movers)
                    MoveAndSlide2D(scene, id, dt);
            }
            auto shapes = scene.m_Registry.view<ShapeCastComponent>();
            for (auto e : shapes)
            {
                auto& sc = shapes.get<ShapeCastComponent>(e);
                if (sc.Enabled && scene.IsEntityActive(e))
                    CastShape(scene, e, sc);
                else
                {
                    sc.Colliding = false;
                    sc.CollisionCount = 0;
                    sc.Results.clear();
                }
            }
            UpdateParallax(scene, dt);
        }

        bool MoveAndSlide2D(Scene& scene, uint32_t entity, float dt)
        {
            const auto e = (entt::entity)entity;
            if (!scene.m_Registry.valid(e) || !scene.m_Registry.all_of<CharacterBody2DComponent>(e))
                return false;
            auto& cb = scene.m_Registry.get<CharacterBody2DComponent>(e);
            PhysicsWorld2D* physics = scene.GetPhysicsWorld2D();
            if (dt <= 0.0f)
                dt = TimeManager::GetDeltaTime();
            if (!physics || dt <= 0.0f)
                return false;
            glm::vec2 c1, c2, center;
            float radius, angle;
            CharacterCapsule(scene, e, c1, c2, radius, center, angle);
            const glm::vec2 up = glm::length(cb.UpDirection) > 1e-6f ? glm::normalize(cb.UpDirection) : glm::vec2(0.0f, 1.0f);
            const float floorCos = std::cos(glm::radians(std::clamp(cb.FloorMaxAngle, 0.0f, 89.9f)));
            const bool wasOnFloor = cb.OnFloor;
            glm::vec2 motion = cb.Velocity * dt;
            if (cb.MotionMode == CharacterMotionMode::Grounded && wasOnFloor)
            {
                // Zeminde: aşağı (yerçekimi) bileşeni zemin taşır; yatay hareket zemin eğimine yatırılır (yokuşta temas kopmaz)
                const float vertical = glm::dot(motion, up);
                glm::vec2 lateral = motion - up * vertical;
                const glm::vec2 n = cb.FloorNormal;
                if (glm::dot(lateral, lateral) > 1e-12f)
                {
                    const float len = glm::length(lateral);
                    glm::vec2 along = lateral - n * glm::dot(lateral, n);
                    if (glm::dot(along, along) > 1e-12f)
                        lateral = glm::normalize(along) * len;
                }
                if (vertical > 0.0f)
                    motion = lateral + up * vertical;        // zıplama
                else if (cb.FloorStopOnSlope)
                    motion = lateral;                        // duruyorsa eğimden kaymaz
                else
                    motion = lateral + up * vertical;
            }
            std::vector<PhysicsWorld2D::MoverPlane2D> planes;
            glm::vec2 offset = physics->MoveCapsule(entity, c1, c2, radius, motion, cb.CollisionMask, planes, std::clamp(cb.MaxSlides, 1, 16) + 1);
            auto classify = [&](const std::vector<PhysicsWorld2D::MoverPlane2D>& list) {
                cb.OnFloor = cb.OnWall = cb.OnCeiling = false;
                cb.WallNormal = glm::vec2(0.0f);
                int touching = 0;
                for (const auto& p : list)
                {
                    if (p.Offset > 0.02f)
                        continue;
                    ++touching;
                    const float d = glm::dot(p.Normal, up);
                    if (cb.MotionMode == CharacterMotionMode::Grounded && d >= floorCos)
                    {
                        cb.OnFloor = true;
                        cb.FloorNormal = p.Normal;
                    }
                    else if (cb.MotionMode == CharacterMotionMode::Grounded && d <= -floorCos)
                        cb.OnCeiling = true;
                    else
                    {
                        cb.OnWall = true;
                        cb.WallNormal = p.Normal;
                    }
                }
                return touching;
            };
            int touching = classify(planes);
            // Zemine yapış: önceki karede zemindeydi, şimdi değil ve yukarı gitmiyor (yokuş / basamak aşağı)
            if (cb.MotionMode == CharacterMotionMode::Grounded && wasOnFloor && !cb.OnFloor && glm::dot(cb.Velocity, up) <= 0.0f && cb.FloorSnapLength > 0.0f)
            {
                std::vector<PhysicsWorld2D::MoverPlane2D> snapPlanes;
                const glm::vec2 snap = physics->MoveCapsule(entity, c1 + offset, c2 + offset, radius, -up * cb.FloorSnapLength, cb.CollisionMask, snapPlanes, 2);
                bool floorBelow = false;
                for (const auto& p : snapPlanes)
                    if (p.Offset <= 0.02f && glm::dot(p.Normal, up) >= floorCos)
                        floorBelow = true;
                if (floorBelow)
                {
                    offset += snap;
                    planes = snapPlanes;
                    touching = classify(planes);
                }
            }
            if (!cb.OnFloor && cb.MotionMode == CharacterMotionMode::Grounded)
                cb.FloorNormal = up;
            cb.Velocity = PhysicsWorld2D::ClipVelocity(cb.Velocity, planes);
            cb.RealVelocity = offset / dt;
            cb.SlideCollisionCount = touching;
            physics->Teleport(Entity{ e, &scene }, center + offset, angle);
            return touching > 0;
        }

        bool ForceShapeCast(Scene& scene, uint32_t entity)
        {
            const auto e = (entt::entity)entity;
            if (!scene.m_Registry.valid(e) || !scene.m_Registry.all_of<ShapeCastComponent>(e))
                return false;
            return CastShape(scene, e, scene.m_Registry.get<ShapeCastComponent>(e));
        }

        bool ForceRayCast(Scene& scene, uint32_t entity)
        {
            const auto e = (entt::entity)entity;
            if (!scene.m_Registry.valid(e) || !scene.m_Registry.all_of<RayCastComponent>(e))
                return false;
            return CastRay(scene, e, scene.m_Registry.get<RayCastComponent>(e));
        }
    }

    void RegisterGodotNodeComponents()
    {
        {
            using C = RemoteTransformComponent;
            ComponentDef<C>("Remote Transform", "RemoteTransformComponent", "Transform", ICON_FA_ARROWS_TO_DOT)
                .Describe("Godot RemoteTransform: pushes this object's position / rotation / scale to another object every frame without "
                          "parenting it (e.g. a camera that follows a moving platform but not its rotation).")
                .Field("Target", "Target", &C::Target)
                .Field("UseGlobalCoordinates", "Use Global Coordinates", &C::UseGlobalCoordinates)
                .Field("UpdatePosition", "Update Position", &C::UpdatePosition)
                .Field("UpdateRotation", "Update Rotation", &C::UpdateRotation)
                .Field("UpdateScale", "Update Scale", &C::UpdateScale);
        }
        {
            using C = RayCastComponent;
            ComponentDef<C>("Ray Cast", "RayCastComponent", "Physics", ICON_FA_LOCATION_CROSSHAIRS)
                .Describe("Godot RayCast3D / RayCast2D: a ray from this object to Target Position (local) re-cast every frame. Read "
                          "Colliding / Collision Point / Normal / Collider from scripts (RayCastIsColliding, RayCastGetPoint ...).")
                .Field("Enabled", "Enabled", &C::Enabled)
                .Field("Space", "Space", &C::Space).Options({ "3D Physics", "2D Physics" })
                .Field("TargetPosition", "Target Position", &C::TargetPosition)
                .Field("CollisionMask", "Collision Mask", &C::CollisionMask, FieldKind::LayerMask)
                .Field("ExcludeParent", "Exclude Parent", &C::ExcludeParent).Tooltip("Ignore this object's own collider and its parent's")
                .Field("HitTriggers", "Hit Triggers", &C::HitTriggers)
                .Field("Colliding", "Colliding", &C::Colliding).ReadOnly()
                .Field("CollisionPoint", "Collision Point", &C::CollisionPoint).ReadOnly()
                .Field("CollisionNormal", "Collision Normal", &C::CollisionNormal).ReadOnly()
                .Field("Distance", "Distance", &C::Distance).ReadOnly()
                .Field("Collider", "Collider", &C::Collider).ReadOnly();
        }
        {
            using C = ShapeCastComponent;
            ComponentDef<C>("Shape Cast", "ShapeCastComponent", "Physics", ICON_FA_CIRCLE_DOT)
                .Describe("Godot ShapeCast3D / ShapeCast2D: sweeps a sphere, box or capsule from this object to Target Position (local) every "
                          "frame and lists everything it touches, nearest first. Safe Fraction is how far the shape can move without touching. "
                          "Read the results from scripts (ShapeCastIsColliding, ShapeCastGetCollisionCount, ShapeCastGetPoint ...).")
                .Field("Enabled", "Enabled", &C::Enabled)
                .Field("Space", "Space", &C::Space).Options({ "3D Physics", "2D Physics" })
                .Field("Shape", "Shape", &C::Shape).Options({ "Sphere", "Box", "Capsule" })
                .Field("Radius", "Radius", &C::Radius).Range(0.001f, 1000.0f, 0.01f).VisibleIf([](const C& c) { return c.Shape != ShapeCastShape::Box; })
                .Field("Size", "Size", &C::Size).VisibleIf([](const C& c) { return c.Shape == ShapeCastShape::Box; })
                .Field("Height", "Height", &C::Height).Range(0.001f, 1000.0f, 0.01f).VisibleIf([](const C& c) { return c.Shape == ShapeCastShape::Capsule; })
                .Field("TargetPosition", "Target Position", &C::TargetPosition)
                .Field("MaxResults", "Max Results", &C::MaxResults).Range(1.0f, 256.0f, 1.0f)
                .Field("CollisionMask", "Collision Mask", &C::CollisionMask, FieldKind::LayerMask)
                .Field("ExcludeParent", "Exclude Parent", &C::ExcludeParent).Tooltip("Ignore this object's own collider and its parent's")
                .Field("HitTriggers", "Hit Triggers", &C::HitTriggers)
                .Field("Colliding", "Colliding", &C::Colliding).ReadOnly()
                .Field("CollisionCount", "Collision Count", &C::CollisionCount).ReadOnly()
                .Field("SafeFraction", "Safe Fraction", &C::SafeFraction).ReadOnly()
                .Field("Collider", "Collider", &C::Collider).ReadOnly();
        }
        {
            using C = VisibleOnScreenNotifierComponent;
            ComponentDef<C>("Visible On Screen Notifier", "VisibleOnScreenNotifierComponent", "Rendering", ICON_FA_EYE)
                .Describe("Godot VisibleOnScreenNotifier: emits screen_entered / screen_exited signals and calls OnBecameVisible / "
                          "OnBecameInvisible on this object's scripts when its bounds enter or leave the main camera's view.")
                .Field("AutoBounds", "Auto Bounds", &C::AutoBounds).Tooltip("Use the mesh / model / sprite bounds of this object")
                .Field("BoundsCenter", "Bounds Center", &C::BoundsCenter).VisibleIf([](const C& c) { return !c.AutoBounds; })
                .Field("BoundsSize", "Bounds Size", &C::BoundsSize).VisibleIf([](const C& c) { return !c.AutoBounds; })
                .Field("OnScreen", "On Screen", &C::OnScreen).ReadOnly();
        }
        {
            using C = TimerComponent;
            ComponentDef<C>("Timer", "TimerComponent", "Scripting", ICON_FA_STOPWATCH)
                .Describe("Godot Timer: counts down Wait Time seconds, then emits the timeout signal and calls OnTimeout on this object's "
                          "scripts. Restarts by itself unless One Shot. Start / stop from scripts with TimerStart / TimerStop.")
                .Field("WaitTime", "Wait Time", &C::WaitTime).Range(0.001f, 100000.0f, 0.01f).Tooltip("Seconds")
                .Field("OneShot", "One Shot", &C::OneShot).Tooltip("Stop after the first timeout")
                .Field("Autostart", "Autostart", &C::Autostart).Tooltip("Start when the game starts")
                .Field("Paused", "Paused", &C::Paused)
                .Field("IgnoreTimeScale", "Ignore Time Scale", &C::IgnoreTimeScale).Tooltip("Count real time (slow motion and pause do not slow it)")
                .Field("ProcessCallback", "Process Callback", &C::ProcessCallback).Options({ "Idle", "Physics" })
                    .Tooltip("Idle: every frame; Physics: every fixed physics step")
                .Field("TimeLeft", "Time Left", &C::TimeLeft).ReadOnly()
                .Field("Running", "Running", &C::Running).ReadOnly()
                .Field("Timeouts", "Timeouts", &C::Timeouts).ReadOnly();
        }
        {
            using C = CharacterBody2DComponent;
            ComponentDef<C>("Character Body 2D", "CharacterBody2DComponent", "Physics 2D", ICON_FA_PERSON_RUNNING)
                .Describe("Godot CharacterBody2D: a kinematic 2D character moved by scripts. Set Velocity and call CharacterBody2DMoveAndSlide: "
                          "it slides along walls and floors, walks up slopes up to Floor Max Angle, sticks to the floor going down slopes and "
                          "steps (Floor Snap Length) and stops the velocity going into walls and ceilings. Is On Floor / Wall / Ceiling tell "
                          "where it touches. The shape comes from this object's Capsule / Circle / Box Collider 2D. Auto Move moves it every "
                          "frame with Velocity and gravity, without a script.")
                .Field("Velocity", "Velocity", &C::Velocity)
                .Field("MotionMode", "Motion Mode", &C::MotionMode).Options({ "Grounded", "Floating" })
                    .Tooltip("Grounded: platformer (floor, walls, ceiling); Floating: top-down, every surface is a wall")
                .Field("UpDirection", "Up Direction", &C::UpDirection)
                .Field("FloorMaxAngle", "Floor Max Angle", &C::FloorMaxAngle).Range(0.0f, 89.0f, 0.5f).Tooltip("Degrees: steeper surfaces are walls")
                .Field("FloorSnapLength", "Floor Snap Length", &C::FloorSnapLength).Range(0.0f, 10.0f, 0.01f)
                .Field("FloorStopOnSlope", "Floor Stop On Slope", &C::FloorStopOnSlope).Tooltip("Standing still on a slope does not slide down")
                .Field("MaxSlides", "Max Slides", &C::MaxSlides).Range(1.0f, 16.0f, 1.0f)
                .Field("CollisionMask", "Collision Mask", &C::CollisionMask, FieldKind::LayerMask)
                .Field("AutoMove", "Auto Move", &C::AutoMove).Tooltip("Move every frame with Velocity and gravity (no script needed)")
                .Field("GravityScale", "Gravity Scale", &C::GravityScale).VisibleIf([](const C& c) { return c.AutoMove; })
                .Field("OnFloor", "Is On Floor", &C::OnFloor).ReadOnly()
                .Field("OnWall", "Is On Wall", &C::OnWall).ReadOnly()
                .Field("OnCeiling", "Is On Ceiling", &C::OnCeiling).ReadOnly()
                .Field("RealVelocity", "Real Velocity", &C::RealVelocity).ReadOnly();
        }
        {
            using C = VisibleOnScreenEnablerComponent;
            ComponentDef<C>("Visible On Screen Enabler", "VisibleOnScreenEnablerComponent", "Rendering", ICON_FA_EYE_SLASH)
                .Describe("Godot VisibleOnScreenEnabler: while these bounds are outside the main camera's view, the target (the parent when "
                          "empty) and its children stop processing - script Update / FixedUpdate, Animator, particles and Timers - but stay "
                          "drawn; they continue where they stopped when the bounds come back into view. Saves time on off-screen enemies.")
                .Field("Target", "Target", &C::Target).Tooltip("Object to pause (empty: the parent)")
                .Field("AutoBounds", "Auto Bounds", &C::AutoBounds).Tooltip("Use the mesh / model / sprite bounds of this object")
                .Field("BoundsCenter", "Bounds Center", &C::BoundsCenter).VisibleIf([](const C& c) { return !c.AutoBounds; })
                .Field("BoundsSize", "Bounds Size", &C::BoundsSize).VisibleIf([](const C& c) { return !c.AutoBounds; })
                .Field("OnScreen", "On Screen", &C::OnScreen).ReadOnly();
        }
        {
            using C = Parallax2DComponent;
            ComponentDef<C>("Parallax 2D", "Parallax2DComponent", "Rendering", ICON_FA_LAYER_GROUP)
                .Describe("Godot Parallax2D: the layer scrolls at Scroll Scale times the camera movement (0.5 = half speed, far away). "
                          "Autoscroll moves it on its own (clouds); Repeat Size keeps copies laid out at that spacing endlessly around the camera.")
                .Field("ScrollScale", "Scroll Scale", &C::ScrollScale)
                .Field("ScrollOffset", "Scroll Offset", &C::ScrollOffset)
                .Field("Autoscroll", "Autoscroll", &C::Autoscroll)
                .Field("RepeatSize", "Repeat Size", &C::RepeatSize);
        }
    }
}
