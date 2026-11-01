#include "GameplaySystems.h"
#include "Core/Tiering/QualitySettings.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/GameplayComponents.h"
#include "Scene/Splines.h"
#include "Scene/CameraRig.h"
#include "Renderer/RibbonRenderer.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Core/PhysicsSystem/Physics3D/PhysicsWorld3D.h"
#include "Core/Input/Input.h"
#include "Core/Time/TimeManager.h"
#include "Scripting/ConquerorScript/CQSValue.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <deque>
#include <mutex>
#include <unordered_map>

namespace Conqueror::GameplaySystems
{
    namespace
    {
        struct TrailPoint
        {
            glm::vec3 Position;
            float Time;
        };

        struct SceneState
        {
            std::unordered_map<uint32_t, std::deque<TrailPoint>> Trails;
            float Time = 0.0f;
        };

        std::unordered_map<Scene*, SceneState>& States()
        {
            static std::unordered_map<Scene*, SceneState> s_States;
            return s_States;
        }

        struct WindSource
        {
            WindZoneMode Mode;
            glm::vec3 Position;
            glm::vec3 Direction;
            float Radius, Main, Turbulence, PulseMagnitude, PulseFrequency;
        };
        std::vector<WindSource> s_Wind;

        struct ForceFieldSource
        {
            ParticleForceFieldComponent Settings;
            glm::mat4 World{ 1.0f };
            glm::mat4 InvWorld{ 1.0f };
        };
        std::vector<ForceFieldSource> s_ForceFields;
        float s_WindTime = 0.0f;

        glm::mat4 WorldOf(Entity e)
        {
            glm::mat4 m = e.GetComponent<TransformComponent>().GetTransform();
            for (Entity p = e.GetParent(); p; p = p.GetParent())
                m = p.GetComponent<TransformComponent>().GetTransform() * m;
            return m;
        }

        void Decompose(const glm::mat4& m, glm::vec3& pos, glm::quat& rot, glm::vec3& scale)
        {
            pos = glm::vec3(m[3]);
            scale = glm::vec3(glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2])));
            glm::mat3 r(glm::vec3(m[0]) / std::max(scale.x, 1e-6f), glm::vec3(m[1]) / std::max(scale.y, 1e-6f), glm::vec3(m[2]) / std::max(scale.z, 1e-6f));
            rot = glm::normalize(glm::quat_cast(r));
        }

        glm::mat4 ParentWorld(Entity e)
        {
            Entity p = e.GetParent();
            return p ? WorldOf(p) : glm::mat4(1.0f);
        }

        // Dünya konumunu yerel Position'a çevir
        void SetWorldPosition(Entity e, const glm::vec3& world)
        {
            auto& tc = e.GetComponent<TransformComponent>();
            tc.Position = glm::vec3(glm::inverse(ParentWorld(e)) * glm::vec4(world, 1.0f));
        }

        void SetWorldRotation(Entity e, const glm::quat& world)
        {
            glm::vec3 pp, ps;
            glm::quat pr;
            Decompose(ParentWorld(e), pp, pr, ps);
            e.GetComponent<TransformComponent>().Rotation = glm::eulerAngles(glm::normalize(glm::inverse(pr) * world));
        }

        Entity SourceOf(Scene& scene, uint64_t uuid)
        {
            return uuid ? scene.FindEntityByUUID(uuid) : Entity{};
        }

        // -Z ileri (motor kuralı) için bakış dönüşü
        glm::quat LookRotation(const glm::vec3& forward, const glm::vec3& up)
        {
            glm::vec3 f = glm::normalize(forward);
            glm::vec3 u = std::fabs(glm::dot(f, glm::normalize(up))) > 0.999f ? glm::vec3(0, 0, 1) : up;
            // glm::quatLookAt: -Z ileri
            return glm::quatLookAt(f, glm::normalize(u));
        }

        float Damp(float lambdaSeconds, float dt)
        {
            if (lambdaSeconds <= 1e-4f)
                return 1.0f;
            return 1.0f - std::exp(-dt / lambdaSeconds);
        }
    }

    // ════════════════════════════════════════
    //  Güncelleme
    // ════════════════════════════════════════
    void Update(Scene& scene, float dt, bool runtime)
    {
        auto& reg = scene.m_Registry;
        SceneState& state = States()[&scene];
        state.Time += dt;

        // Eğriler: Spline Animate / Extrude / Instantiate
        Splines::Update(scene, dt, runtime);

        // ── Döndürücü (Unreal RotatingMovement) ──
        for (auto h : reg.view<TransformComponent, RotatorComponent>())
        {
            if (!scene.IsEntityActive(h)) continue;
            auto& rot = reg.get<RotatorComponent>(h);
            if (!runtime && !rot.RunInEditor) continue;
            auto& tc = reg.get<TransformComponent>(h);
            glm::quat q(tc.Rotation);
            glm::vec3 r = glm::radians(rot.RotationRate) * dt;
            glm::quat delta(r);
            q = rot.LocalSpace ? q * delta : delta * q;
            tc.Rotation = glm::eulerAngles(glm::normalize(q));
        }

        // ── Mermi hareketi (Unreal ProjectileMovement) ──
        if (runtime)
        {
            std::vector<entt::entity> expired;
            PhysicsWorld3D* physics = scene.GetPhysicsWorld3D();
            for (auto h : reg.view<TransformComponent, ProjectileMovementComponent>())
            {
                if (!scene.IsEntityActive(h)) continue;
                Entity e{ h, &scene };
                auto& pm = reg.get<ProjectileMovementComponent>(h);
                auto& tc = reg.get<TransformComponent>(h);
                glm::vec3 pos, scale;
                glm::quat rotation;
                Decompose(WorldOf(e), pos, rotation, scale);
                if (!pm.Initialized)
                {
                    pm.Initialized = true;
                    if (glm::length(pm.Velocity) < 1e-5f)
                        pm.Velocity = rotation * glm::vec3(0, 0, -1) * pm.InitialSpeed;
                }
                pm.Age += dt;
                if (pm.Lifetime > 0.0f && pm.Age >= pm.Lifetime)
                {
                    expired.push_back(h);
                    continue;
                }
                glm::vec3 gravity = physics ? physics->GetGravity() : glm::vec3(0, -9.81f, 0);
                pm.Velocity += gravity * pm.GravityScale * dt;
                float speed = glm::length(pm.Velocity);
                if (pm.MaxSpeed > 0.0f && speed > pm.MaxSpeed)
                    pm.Velocity *= pm.MaxSpeed / speed;
                glm::vec3 move = pm.Velocity * dt;
                float dist = glm::length(move);
                if (physics && physics->IsRunning() && dist > 1e-6f)
                {
                    Physics3D::RaycastHit hit;
                    if (physics->Raycast(pos, move, dist, hit, 0xFFFFFFFFu, (uint32_t)h) && !hit.IsTrigger)
                    {
                        if (pm.ShouldBounce)
                        {
                            pos = hit.Point + hit.Normal * 0.01f;
                            pm.Velocity = glm::reflect(pm.Velocity, hit.Normal) * pm.Bounciness;
                            move = glm::vec3(0.0f);
                        }
                        else
                        {
                            pos = hit.Point;
                            pm.Velocity = glm::vec3(0.0f);
                            move = glm::vec3(0.0f);
                            CQS::Value args[2] = { CQS::Value::MakeInt((int64_t)hit.Entity), CQS::Value::MakeString(
                                reg.valid((entt::entity)hit.Entity) && reg.all_of<TagComponent>((entt::entity)hit.Entity) ? reg.get<TagComponent>((entt::entity)hit.Entity).Tag : "") };
                            scene.InvokeScriptMethod((uint32_t)h, "OnProjectileStop", args, 2);
                        }
                    }
                }
                SetWorldPosition(e, pos + move);
                if (pm.RotationFollowsVelocity && glm::length(pm.Velocity) > 1e-4f)
                    SetWorldRotation(e, LookRotation(pm.Velocity, glm::vec3(0, 1, 0)));
                (void)tc;
            }
            for (auto h : expired)
                if (reg.valid(h))
                    scene.DestroyEntity(Entity{ h, &scene });
        }

        // ── Kısıtlar (düzenleyicide de çalışır, Unity gibi) ──
        auto blendPos = [](const glm::vec3& a, const glm::vec3& b, float w) { return glm::mix(a, b, std::clamp(w, 0.0f, 1.0f)); };
        for (auto h : reg.view<TransformComponent, PositionConstraintComponent>())
        {
            auto& c = reg.get<PositionConstraintComponent>(h);
            Entity src = SourceOf(scene, c.Source);
            if (!c.IsActive || !src) continue;
            Entity e{ h, &scene };
            glm::vec3 cur = glm::vec3(WorldOf(e)[3]);
            glm::vec3 target = glm::vec3(WorldOf(src)[3]) + c.Offset;
            if (c.FreezeX) target.x = cur.x;
            if (c.FreezeY) target.y = cur.y;
            if (c.FreezeZ) target.z = cur.z;
            SetWorldPosition(e, blendPos(cur, target, c.Weight));
        }
        for (auto h : reg.view<TransformComponent, RotationConstraintComponent>())
        {
            auto& c = reg.get<RotationConstraintComponent>(h);
            Entity src = SourceOf(scene, c.Source);
            if (!c.IsActive || !src) continue;
            Entity e{ h, &scene };
            glm::vec3 p, s, sp, ss;
            glm::quat cur, srcRot;
            Decompose(WorldOf(e), p, cur, s);
            Decompose(WorldOf(src), sp, srcRot, ss);
            glm::quat target = srcRot * glm::quat(glm::radians(c.Offset));
            glm::vec3 ce = glm::eulerAngles(cur), te = glm::eulerAngles(target);
            if (c.FreezeX) te.x = ce.x;
            if (c.FreezeY) te.y = ce.y;
            if (c.FreezeZ) te.z = ce.z;
            SetWorldRotation(e, glm::slerp(cur, glm::quat(te), std::clamp(c.Weight, 0.0f, 1.0f)));
        }
        for (auto h : reg.view<TransformComponent, ScaleConstraintComponent>())
        {
            auto& c = reg.get<ScaleConstraintComponent>(h);
            Entity src = SourceOf(scene, c.Source);
            if (!c.IsActive || !src) continue;
            auto& tc = reg.get<TransformComponent>(h);
            glm::vec3 sp, ss;
            glm::quat sr;
            Decompose(WorldOf(src), sp, sr, ss);
            tc.Scale = blendPos(tc.Scale, ss * c.Offset, c.Weight);
        }
        for (auto h : reg.view<TransformComponent, ParentConstraintComponent>())
        {
            auto& c = reg.get<ParentConstraintComponent>(h);
            Entity src = SourceOf(scene, c.Source);
            if (!c.IsActive || !src) continue;
            Entity e{ h, &scene };
            glm::vec3 p, s, sp, ss;
            glm::quat r, sr;
            Decompose(WorldOf(e), p, r, s);
            Decompose(WorldOf(src), sp, sr, ss);
            if (c.CaptureOffsetOnStart && !c.OffsetCaptured && runtime)
            {
                c.PositionOffset = glm::inverse(sr) * (p - sp);
                c.RotationOffset = glm::degrees(glm::eulerAngles(glm::inverse(sr) * r));
                c.OffsetCaptured = true;
            }
            glm::vec3 targetPos = sp + sr * c.PositionOffset;
            glm::quat targetRot = sr * glm::quat(glm::radians(c.RotationOffset));
            float w = std::clamp(c.Weight, 0.0f, 1.0f);
            SetWorldPosition(e, glm::mix(p, targetPos, w));
            SetWorldRotation(e, glm::slerp(r, targetRot, w));
        }
        for (auto h : reg.view<TransformComponent, LookAtConstraintComponent>())
        {
            auto& c = reg.get<LookAtConstraintComponent>(h);
            Entity src = SourceOf(scene, c.Source);
            if (!c.IsActive || !src) continue;
            Entity e{ h, &scene };
            glm::vec3 p, s;
            glm::quat cur;
            Decompose(WorldOf(e), p, cur, s);
            glm::vec3 dir = glm::vec3(WorldOf(src)[3]) - p;
            if (glm::length(dir) < 1e-5f) continue;
            glm::quat look = LookRotation(dir, c.UseWorldUp ? glm::vec3(0, 1, 0) : cur * glm::vec3(0, 1, 0));
            look = look * glm::angleAxis(glm::radians(c.Roll), glm::vec3(0, 0, -1)) * glm::quat(glm::radians(c.RotationOffset));
            SetWorldRotation(e, glm::slerp(cur, look, std::clamp(c.Weight, 0.0f, 1.0f)));
        }
        for (auto h : reg.view<TransformComponent, AimConstraintComponent>())
        {
            auto& c = reg.get<AimConstraintComponent>(h);
            Entity src = SourceOf(scene, c.Source);
            if (!c.IsActive || !src) continue;
            Entity e{ h, &scene };
            glm::vec3 p, s;
            glm::quat cur;
            Decompose(WorldOf(e), p, cur, s);
            glm::vec3 dir = glm::vec3(WorldOf(src)[3]) - p;
            if (glm::length(dir) < 1e-5f || glm::length(c.AimVector) < 1e-5f) continue;
            // Yerel Aim vektörünü hedef yönüne, Up vektörünü dünya yukarısına hizala
            glm::vec3 aimL = glm::normalize(c.AimVector);
            glm::vec3 upL = glm::length(c.UpVector) > 1e-5f ? glm::normalize(c.UpVector) : glm::vec3(0, 1, 0);
            glm::vec3 f = glm::normalize(dir);
            glm::vec3 wu = glm::length(c.WorldUpVector) > 1e-5f ? glm::normalize(c.WorldUpVector) : glm::vec3(0, 1, 0);
            glm::vec3 r = glm::cross(wu, f);
            if (glm::length(r) < 1e-5f) r = glm::cross(glm::vec3(1, 0, 0), f);
            r = glm::normalize(r);
            glm::vec3 u = glm::cross(f, r);
            glm::mat3 world(r, u, f);                                     // hedef çerçeve (sağ, yukarı, ileri)
            glm::vec3 rl = glm::normalize(glm::cross(upL, aimL));
            glm::vec3 ul = glm::cross(aimL, rl);
            glm::mat3 local(rl, ul, aimL);                                // yerel çerçeve
            glm::quat target = glm::quat_cast(world * glm::transpose(local)) * glm::quat(glm::radians(c.RotationOffset));
            SetWorldRotation(e, glm::slerp(cur, glm::normalize(target), std::clamp(c.Weight, 0.0f, 1.0f)));
        }

        // ── Trail kayıtları ──
        for (auto h : reg.view<TransformComponent, TrailRendererComponent>())
        {
            auto& tr = reg.get<TrailRendererComponent>(h);
            auto& points = state.Trails[(uint32_t)h];
            Entity e{ h, &scene };
            glm::vec3 pos = glm::vec3(WorldOf(e)[3]);
            while (!points.empty() && state.Time - points.front().Time > std::max(0.01f, tr.Time))
                points.pop_front();
            if (tr.Emitting && scene.IsEntityActive(h) && (points.empty() || glm::distance(points.back().Position, pos) >= std::max(0.001f, tr.MinVertexDistance)))
                points.push_back({ pos, state.Time });
            if (points.size() > 4096)
                points.pop_front();
        }

        // ── Parçacık kuvvet alanları ──
        s_ForceFields.clear();
        for (auto h : reg.view<TransformComponent, ParticleForceFieldComponent>())
        {
            if (!scene.IsEntityActive(h)) continue;
            Entity e{ h, &scene };
            glm::mat4 w = WorldOf(e);
            // Ölçeksiz çerçeve: menziller metre cinsinden
            glm::vec3 p, sc;
            glm::quat r;
            Decompose(w, p, r, sc);
            glm::mat4 frame = glm::translate(glm::mat4(1.0f), p) * glm::mat4_cast(r);
            s_ForceFields.push_back({ reg.get<ParticleForceFieldComponent>(h), frame, glm::inverse(frame) });
        }

        // ── Rüzgâr bölgeleri ──
        s_Wind.clear();
        s_WindTime += dt;
        for (auto h : reg.view<TransformComponent, WindZoneComponent>())
        {
            if (!scene.IsEntityActive(h)) continue;
            auto& wz = reg.get<WindZoneComponent>(h);
            Entity e{ h, &scene };
            glm::vec3 p, s;
            glm::quat r;
            Decompose(WorldOf(e), p, r, s);
            s_Wind.push_back({ wz.Mode, p, r * glm::vec3(0, 0, -1), wz.Radius, wz.Main, wz.Turbulence, wz.PulseMagnitude, wz.PulseFrequency });
            // İsteğe bağlı: dinamik gövdelere kuvvet
            if (runtime && wz.AffectRigidbodies)
                if (PhysicsWorld3D* physics = scene.GetPhysicsWorld3D(); physics && physics->IsRunning())
                    for (auto b : reg.view<TransformComponent, RigidbodyComponent>())
                    {
                        auto& rb = reg.get<RigidbodyComponent>(b);
                        if (rb.Type != RigidbodyComponent::BodyType::Dynamic || !rb.RuntimeBody) continue;
                        glm::vec3 bp = glm::vec3(WorldOf(Entity{ b, &scene })[3]);
                        glm::vec3 wind = SampleWind(bp);
                        if (glm::dot(wind, wind) > 0.0f)
                            physics->ApplyForce(Entity{ b, &scene }, wind * rb.Mass * wz.RigidbodyForceScale);
                    }
        }
    }

    bool HasWind() { return !s_Wind.empty(); }

    bool HasForceFields() { return !s_ForceFields.empty(); }

    void ApplyForceFields(const glm::vec3& position, glm::vec3& velocity, float dt)
    {
        for (const auto& f : s_ForceFields)
        {
            const auto& s = f.Settings;
            glm::vec3 local = glm::vec3(f.InvWorld * glm::vec4(position, 1.0f));
            // Şekil içinde mi? Etki merkezden uzaklığa göre (yarıçap boyunca sabit)
            float dist = 0.0f;
            glm::vec3 toCenter(0.0f);      // yerel uzayda, merkezden parçacığa
            switch (s.Shape)
            {
                case ForceFieldShape::Sphere:
                case ForceFieldShape::Hemisphere:
                    if (s.Shape == ForceFieldShape::Hemisphere && local.y < 0.0f) continue;
                    toCenter = local;
                    dist = glm::length(local);
                    break;
                case ForceFieldShape::Cylinder:
                    if (std::fabs(local.y) > s.Length * 0.5f) continue;
                    toCenter = glm::vec3(local.x, 0.0f, local.z);
                    dist = glm::length(toCenter);
                    break;
                case ForceFieldShape::Box:
                    if (std::fabs(local.x) > s.EndRange || std::fabs(local.y) > s.EndRange || std::fabs(local.z) > s.EndRange) continue;
                    toCenter = local;
                    dist = std::max({ std::fabs(local.x), std::fabs(local.y), std::fabs(local.z) });
                    break;
            }
            if (dist > s.EndRange || dist < s.StartRange)
                continue;
            glm::mat3 rot = glm::mat3(f.World);
            glm::vec3 accel = rot * s.Directional;
            if (s.Gravity != 0.0f && dist > 1e-4f)
            {
                // Odak: 0 merkeze, 1 dış yüzeye doğru çeker
                float targetDist = s.GravityFocus * s.EndRange;
                float sign = dist > targetDist ? -1.0f : 1.0f;
                accel += rot * (glm::normalize(toCenter) * sign) * s.Gravity;
            }
            velocity += accel * dt;
            if (s.RotationSpeed != 0.0f)
            {
                glm::vec3 axis = glm::normalize(rot * glm::vec3(0.0f, 1.0f, 0.0f));
                glm::vec3 rel = position - glm::vec3(f.World[3]);
                glm::vec3 radial = rel - axis * glm::dot(rel, axis);
                glm::vec3 swirl = glm::cross(axis, radial) * s.RotationSpeed;
                float k = std::clamp(s.RotationAttraction, 0.0f, 1.0f);
                // Girdap bileşenine doğru çek (eksen boyunca hız korunur)
                glm::vec3 alongAxis = axis * glm::dot(velocity, axis);
                glm::vec3 planar = velocity - alongAxis;
                planar = glm::mix(planar, swirl, std::min(1.0f, k * dt * 10.0f));
                velocity = planar + alongAxis;
            }
            if (s.Drag > 0.0f)
            {
                float d = s.Drag * dt * (s.MultiplyDragByVelocity ? glm::length(velocity) : 1.0f);
                velocity *= std::max(0.0f, 1.0f - d);
            }
        }
    }

    glm::vec3 SampleWind(const glm::vec3& position)
    {
        glm::vec3 total(0.0f);
        for (const auto& w : s_Wind)
        {
            float pulse = 1.0f + w.PulseMagnitude * std::sin(s_WindTime * w.PulseFrequency * 6.2831853f * 10.0f);
            // Basit, ucuz türbülans (konuma ve zamana bağlı)
            glm::vec3 turbulence(std::sin(position.x * 0.73f + s_WindTime * 1.9f), std::sin(position.y * 0.61f + s_WindTime * 2.3f),
                                 std::sin(position.z * 0.67f + s_WindTime * 1.7f));
            if (w.Mode == WindZoneMode::Directional)
                total += w.Direction * w.Main * pulse + turbulence * w.Turbulence;
            else
            {
                glm::vec3 d = position - w.Position;
                float dist = glm::length(d);
                if (dist >= w.Radius || dist < 1e-4f) continue;
                float falloff = 1.0f - dist / std::max(w.Radius, 1e-4f);
                total += (d / dist) * w.Main * pulse * falloff + turbulence * w.Turbulence * falloff;
            }
        }
        return total;
    }

    void LateUpdate(Scene& scene, float dt, bool runtime)
    {
        if (!runtime)
            return;
        auto& reg = scene.m_Registry;
        // Sanal kameralar + kamera beyni + sarsıntı (betiklerin LateUpdate'inden sonra)
        CameraRig::LateUpdate(scene, dt);
        for (auto h : reg.view<TransformComponent, CameraFollowComponent>())
        {
            if (!scene.IsEntityActive(h)) continue;
            auto& cf = reg.get<CameraFollowComponent>(h);
            Entity target = SourceOf(scene, cf.Target);
            if (!target) continue;
            Entity e{ h, &scene };
            glm::vec3 tp, ts;
            glm::quat tr;
            Decompose(WorldOf(target), tp, tr, ts);
            glm::vec3 cp, cs;
            glm::quat cr;
            Decompose(WorldOf(e), cp, cr, cs);
            glm::vec3 desired = cp;
            switch (cf.Mode)
            {
                case CameraFollowMode::Follow:
                    // Hedefin arkasında (hedefin yönelimine göre ofset)
                    desired = tp + tr * cf.Offset;
                    break;
                case CameraFollowMode::Orbit:
                {
                    if (cf.MouseOrbit && Input::IsMouseButtonDown(Mouse::ButtonRight))
                    {
                        glm::vec2 d = Input::GetMouseDelta();
                        cf.OrbitYaw -= d.x * cf.OrbitSensitivity;
                        cf.OrbitPitch = std::clamp(cf.OrbitPitch + d.y * cf.OrbitSensitivity, -85.0f, 85.0f);
                    }
                    float yaw = glm::radians(cf.OrbitYaw), pitch = glm::radians(cf.OrbitPitch);
                    glm::vec3 dir(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
                    desired = tp + cf.LookOffset + dir * cf.OrbitDistance;
                    break;
                }
                case CameraFollowMode::SideScroller:
                {
                    desired = cp;
                    glm::vec3 goal = tp + cf.Offset;
                    if (std::fabs(goal.x - cp.x) > cf.DeadZone.x) desired.x = goal.x - std::copysign(cf.DeadZone.x, goal.x - cp.x);
                    if (std::fabs(goal.y - cp.y) > cf.DeadZone.y) desired.y = goal.y - std::copysign(cf.DeadZone.y, goal.y - cp.y);
                    desired.z = goal.z;
                    break;
                }
                case CameraFollowMode::TopDown:
                    desired = tp + cf.Offset;
                    break;
            }
            glm::vec3 newPos = glm::mix(cp, desired, Damp(cf.PositionDamping, dt));
            SetWorldPosition(e, newPos);
            if (cf.LookAtTarget && cf.Mode != CameraFollowMode::SideScroller)
            {
                glm::vec3 dir = tp + cf.LookOffset - newPos;
                if (glm::length(dir) > 1e-4f)
                    SetWorldRotation(e, glm::slerp(cr, LookRotation(dir, glm::vec3(0, 1, 0)), Damp(cf.RotationDamping, dt)));
            }
        }
    }

    // ════════════════════════════════════════
    //  Çizim ve LOD
    // ════════════════════════════════════════
    void RenderRibbons(Scene& scene)
    {
        auto& reg = scene.m_Registry;
        SceneState& state = States()[&scene];
        for (auto h : reg.view<TransformComponent, LineRendererComponent>())
        {
            if (!scene.IsEntityActive(h)) continue;
            auto& lr = reg.get<LineRendererComponent>(h);
            if (lr.Positions.size() < 2) continue;
            Entity e{ h, &scene };
            glm::mat4 world = WorldOf(e);
            std::vector<RibbonRenderer::Point> pts;
            pts.reserve(lr.Positions.size());
            const size_t n = lr.Positions.size();
            for (size_t i = 0; i < n; ++i)
            {
                float t = n > 1 ? (float)i / (float)(n - 1) : 0.0f;
                glm::vec3 p = lr.UseWorldSpace ? lr.Positions[i] : glm::vec3(world * glm::vec4(lr.Positions[i], 1.0f));
                pts.push_back({ p, glm::mix(lr.StartWidth, lr.EndWidth, t), glm::mix(lr.StartColor, lr.EndColor, t) });
            }
            RibbonRenderer::Params params;
            params.AlignToView = lr.Alignment == RibbonAlignment::View;
            params.ViewNormal = glm::normalize(glm::vec3(world[2]) + glm::vec3(1e-6f));
            params.TileTexture = lr.TextureMode == RibbonTextureMode::Tile;
            params.Additive = lr.Blend == RibbonBlend::Additive;
            params.Loop = lr.Loop;
            params.Emissive = lr.Emissive;
            params.Texture = RibbonRenderer::LoadTexture(lr.Texture);
            RibbonRenderer::Draw(pts, params);
        }
        for (auto h : reg.view<TransformComponent, TrailRendererComponent>())
        {
            auto it = state.Trails.find((uint32_t)h);
            if (it == state.Trails.end() || it->second.size() < 1) continue;
            auto& tr = reg.get<TrailRendererComponent>(h);
            const auto& src = it->second;
            std::vector<RibbonRenderer::Point> pts;
            pts.reserve(src.size() + 1);
            // En eski -> en yeni; genişlik/renk yaşa göre (yeni = başlangıç)
            for (const auto& p : src)
            {
                float age = std::clamp((state.Time - p.Time) / std::max(0.01f, tr.Time), 0.0f, 1.0f);
                pts.push_back({ p.Position, glm::mix(tr.StartWidth, tr.EndWidth, age), glm::mix(tr.StartColor, tr.EndColor, age) });
            }
            // Baş: varlığın şu anki konumu
            Entity e{ h, &scene };
            glm::vec3 head = glm::vec3(WorldOf(e)[3]);
            if (scene.IsEntityActive(h) && glm::distance(head, src.back().Position) > 1e-4f)
                pts.push_back({ head, tr.StartWidth, tr.StartColor });
            if (pts.size() < 2) continue;
            RibbonRenderer::Params params;
            params.TileTexture = tr.TextureMode == RibbonTextureMode::Tile;
            params.Additive = tr.Blend == RibbonBlend::Additive;
            params.Emissive = tr.Emissive;
            params.Texture = RibbonRenderer::LoadTexture(tr.Texture);
            RibbonRenderer::Draw(pts, params);
        }
    }

    void UpdateLOD(Scene& scene, const RenderCamera& camera, std::unordered_set<uint32_t>& hidden)
    {
        hidden.clear();
        auto& reg = scene.m_Registry;
        auto view = reg.view<TransformComponent, LODGroupComponent>();
        if (view.begin() == view.end())
            return;
        const float projScale = camera.Projection[1][1]; // cot(fov/2)
        for (auto h : view)
        {
            auto& lod = reg.get<LODGroupComponent>(h);
            Entity e{ h, &scene };
            glm::mat4 world = WorldOf(e);
            glm::vec3 center = glm::vec3(world[3]);
            float scale = std::max(glm::length(glm::vec3(world[0])), std::max(glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2]))));
            float size = lod.Size * scale;
            float ratio;
            if (camera.Orthographic)
                ratio = size * projScale * 0.5f;
            else
            {
                float dist = std::max(glm::distance(center, camera.Position), 1e-3f);
                ratio = size * projScale * 0.5f / dist; // ekran yüksekliğine oran (0..1+)
            }
            // Kalite "LOD Bias": büyük değer ayrıntılı seviyeyi daha uzakta tutar
            float percent = ratio * 100.0f * std::max(QualitySettings::GetPreset().LODBias, 0.01f);
            const uint64_t levels[4] = { lod.LOD0, lod.LOD1, lod.LOD2, lod.LOD3 };
            const float thresholds[4] = { lod.Threshold0, lod.Threshold1, lod.Threshold2, lod.Threshold3 };
            int selected = -1;
            int last = -1;
            for (int i = 0; i < 4; ++i)
            {
                if (!levels[i]) continue;
                last = i;
                if (selected < 0 && percent >= thresholds[i])
                    selected = i;
            }
            if (selected < 0 && !lod.CullBelowLast)
                selected = last;
            lod.CurrentLOD = selected;
            for (int i = 0; i < 4; ++i)
            {
                if (!levels[i] || i == selected) continue;
                Entity child = scene.FindEntityByUUID(levels[i]);
                if (!child) continue;
                // Seviye ve tüm altı gizlenir
                std::vector<Entity> stack{ child };
                while (!stack.empty())
                {
                    Entity c = stack.back();
                    stack.pop_back();
                    hidden.insert((uint32_t)c);
                    for (Entity cc : c.GetChildren()) stack.push_back(cc);
                }
            }
        }
    }

    void ResetRuntime(Scene& scene)
    {
        auto it = States().find(&scene);
        if (it != States().end())
            it->second.Trails.clear();
    }

    void ForgetScene(Scene* scene)
    {
        States().erase(scene);
    }
}
