#include "ActorComponents.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/GameplayComponents.h"
#include "Scene/PhysicsComponents.h"
#include "Scene/ComponentRegistry.h"
#include "Core/Input/GameInput.h"
#include "Core/KeyCodes.h"
#include "Core/PhysicsSystem/Physics3D/PhysicsWorld3D.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Scripting/ConquerorScript/CQSValue.h"
#include "Scripting/NativeRegistry.h"
#include "Scripting/ScriptContext.h"

#include <IconsFontAwesome6.h>

#include <algorithm>
#include <cmath>

namespace Conqueror
{
    namespace
    {
        // Dünya pozuna taşı (ebeveyn varsa yerel uzaya çevirerek)
        void MoveTo(Scene& scene, Entity e, const glm::vec3& worldPos)
        {
            auto& tc = e.GetComponent<TransformComponent>();
            glm::vec3 local = worldPos;
            if (Entity p = e.GetParent())
                local = glm::vec3(glm::inverse(scene.ComputeWorldMatrix((entt::entity)p)) * glm::vec4(worldPos, 1.0f));
            tc.Position = local;
        }

        glm::vec3 PathPoint(const std::vector<glm::vec3>& pts, float t)
        {
            if (pts.empty()) return glm::vec3(0.0f);
            if (pts.size() == 1) return pts[0];
            // Uzunluğa göre eşit hız (Unreal: her parçanın payı boyuna göre)
            float total = 0.0f;
            for (size_t i = 1; i < pts.size(); ++i) total += glm::distance(pts[i - 1], pts[i]);
            if (total < 1e-6f) return pts[0];
            float d = std::clamp(t, 0.0f, 1.0f) * total;
            for (size_t i = 1; i < pts.size(); ++i)
            {
                const float seg = glm::distance(pts[i - 1], pts[i]);
                if (d <= seg || i + 1 == pts.size())
                    return glm::mix(pts[i - 1], pts[i], seg > 1e-6f ? std::clamp(d / seg, 0.0f, 1.0f) : 1.0f);
                d -= seg;
            }
            return pts.back();
        }

        void UpdateCable(Scene& scene, entt::entity e, CableComponent& c, float dt)
        {
            const int n = std::clamp(c.NumSegments, 1, 256);
            const glm::mat4 w = scene.ComputeWorldMatrix(e);
            const glm::vec3 a = glm::vec3(w[3]);
            glm::vec3 b = glm::vec3(w * glm::vec4(c.EndLocation, 1.0f));
            if (c.AttachEndTo)
                if (Entity t = scene.FindEntityByUUID(c.AttachEndTo))
                    b = glm::vec3(scene.ComputeWorldMatrix((entt::entity)t) * glm::vec4(c.EndLocation, 1.0f));
            if ((int)c.Particles.size() != n + 1)
            {
                c.Particles.resize((size_t)n + 1);
                for (int i = 0; i <= n; ++i)
                    c.Particles[(size_t)i] = glm::mix(a, b, (float)i / (float)n);
                c.Previous = c.Particles;
            }
            // Verlet: yerçekimi + sönüm, sonra parça boyu kısıtları (uçlar sabit)
            const float step = std::min(dt, 1.0f / 30.0f);
            const glm::vec3 g(0.0f, -9.81f * c.GravityScale, 0.0f);
            for (int i = 1; i < n; ++i)
            {
                glm::vec3& p = c.Particles[(size_t)i];
                const glm::vec3 vel = (p - c.Previous[(size_t)i]) * 0.99f;
                c.Previous[(size_t)i] = p;
                p += vel + g * step * step;
            }
            c.Particles.front() = c.Previous.front() = a;
            c.Particles.back() = c.Previous.back() = b;
            const float seg = std::max(c.CableLength, 1e-3f) / (float)n;
            for (int it = 0; it < std::clamp(c.SolverIterations, 1, 32); ++it)
                for (int i = 0; i < n; ++i)
                {
                    glm::vec3& p0 = c.Particles[(size_t)i];
                    glm::vec3& p1 = c.Particles[(size_t)i + 1];
                    const glm::vec3 d = p1 - p0;
                    const float len = glm::length(d);
                    if (len < 1e-6f) continue;
                    const glm::vec3 corr = d * ((len - seg) / len);
                    const bool fix0 = i == 0, fix1 = i + 1 == n;
                    if (fix0 && fix1) continue;
                    if (fix0) p1 -= corr;
                    else if (fix1) p0 += corr;
                    else { p0 += corr * 0.5f; p1 -= corr * 0.5f; }
                }
            // Çizim: dünya uzayında çizgi (Line Renderer)
            Entity ent{ e, &scene };
            if (!ent.HasComponent<LineRendererComponent>())
                ent.AddComponent<LineRendererComponent>();
            auto& lr = ent.GetComponent<LineRendererComponent>();
            lr.UseWorldSpace = true;
            lr.Positions = c.Particles;
            lr.StartWidth = lr.EndWidth = c.CableWidth;
            lr.StartColor = lr.EndColor = c.Color;
        }
    }

    namespace ActorComponents
    {
        void AddMovementInput(Scene& scene, uint32_t entity, const glm::vec3& dir, float scale)
        {
            if (auto* fp = scene.m_Registry.try_get<FloatingPawnMovementComponent>((entt::entity)entity))
                fp->PendingInput += dir * scale;
        }

        void Update(Scene& scene, float dt, bool runtime)
        {
            auto& reg = scene.m_Registry;
            // Cable: düzenleyicide de sarkar (Unreal gibi)
            for (auto e : reg.view<TransformComponent, CableComponent>())
                if (scene.IsEntityActive(e))
                    UpdateCable(scene, e, reg.get<CableComponent>(e), dt);
            // Cine Camera: sensör + odak uzaklığından görüş açısı
            for (auto e : reg.view<CameraComponent, CineCameraComponent>())
            {
                auto& cc = reg.get<CineCameraComponent>(e);
                const float fov = 2.0f * std::atan(std::max(cc.SensorHeight, 0.01f) / (2.0f * std::max(cc.FocalLength, 0.1f)));
                cc.FieldOfView = glm::degrees(fov);
                auto& cam = reg.get<CameraComponent>(e).Camera;
                if (std::abs(cam.GetPerspectiveVerticalFOV() - fov) > 1e-5f)
                    cam.SetPerspectiveVerticalFOV(fov);
                cc.CurrentFocusDistance = cc.ManualFocusDistance;
                if (cc.FocusMethod == CineFocusMethod::Tracking && cc.TrackingTarget)
                    if (Entity t = scene.FindEntityByUUID(cc.TrackingTarget))
                        cc.CurrentFocusDistance = glm::distance(glm::vec3(scene.ComputeWorldMatrix(e)[3]), glm::vec3(scene.ComputeWorldMatrix((entt::entity)t)[3]));
            }
            if (!runtime)
                return;
            PhysicsWorld3D* physics = scene.GetPhysicsWorld3D();
            // Floating Pawn Movement
            for (auto e : reg.view<TransformComponent, FloatingPawnMovementComponent>())
            {
                if (!scene.IsEntityActive(e)) continue;
                auto& m = reg.get<FloatingPawnMovementComponent>(e);
                Entity ent{ e, &scene };
                glm::vec3 input = m.PendingInput;
                m.PendingInput = glm::vec3(0.0f);
                if (m.UseDefaultInput)
                {
                    const glm::mat4 w = scene.ComputeWorldMatrix(e);
                    const glm::vec3 fwd = -glm::normalize(glm::vec3(w[2])), right = glm::normalize(glm::vec3(w[0]));
                    if (GameInput::IsKeyHeld(Key::W)) input += fwd;
                    if (GameInput::IsKeyHeld(Key::S)) input -= fwd;
                    if (GameInput::IsKeyHeld(Key::D)) input += right;
                    if (GameInput::IsKeyHeld(Key::A)) input -= right;
                    if (GameInput::IsKeyHeld(Key::E)) input.y += 1.0f;
                    if (GameInput::IsKeyHeld(Key::Q)) input.y -= 1.0f;
                }
                if (glm::length(input) > 1.0f)
                    input = glm::normalize(input);
                if (glm::length(input) > 1e-4f)
                {
                    // Dönüş takviyesi: hız yönü istenen yöne çabuk döner (Unreal TurningBoost)
                    const float speed = glm::length(m.Velocity);
                    m.Velocity -= (m.Velocity - glm::normalize(input) * speed) * std::min(dt * m.TurningBoost, 1.0f);
                    m.Velocity += input * m.Acceleration * dt;
                    const float sp = glm::length(m.Velocity), cap = m.MaxSpeed * glm::length(input);
                    if (sp > cap && sp > 1e-5f) m.Velocity *= cap / sp;
                }
                else
                {
                    const float sp = glm::length(m.Velocity);
                    const float ns = std::max(0.0f, sp - m.Deceleration * dt);
                    m.Velocity = sp > 1e-5f ? m.Velocity * (ns / sp) : glm::vec3(0.0f);
                }
                if (physics && physics->HasCharacter(ent))
                    physics->MoveCharacter(ent, m.Velocity * dt);
                else if (physics && reg.all_of<RigidbodyComponent>(e) && reg.get<RigidbodyComponent>(e).Type == RigidbodyComponent::BodyType::Dynamic)
                    physics->SetLinearVelocity(ent, m.Velocity);
                else
                    MoveTo(scene, ent, glm::vec3(scene.ComputeWorldMatrix(e)[3]) + m.Velocity * dt);
            }
            // Interp To Movement
            for (auto e : reg.view<TransformComponent, InterpToMovementComponent>())
            {
                if (!scene.IsEntityActive(e)) continue;
                auto& m = reg.get<InterpToMovementComponent>(e);
                Entity ent{ e, &scene };
                if (!m.HasStart)
                {
                    m.Start = glm::vec3(scene.ComputeWorldMatrix(e)[3]);
                    m.HasStart = true;
                    m.Active = m.AutoActivate;
                    m.Time = m.Behaviour == InterpBehaviour::OneShotReverse ? 1.0f : 0.0f;
                    m.Direction = m.Behaviour == InterpBehaviour::OneShotReverse ? -1 : 1;
                }
                if (!m.Active) continue;
                m.Time += dt / std::max(m.Duration, 1e-3f) * (float)m.Direction;
                if (m.Time >= 1.0f || m.Time <= 0.0f)
                {
                    switch (m.Behaviour)
                    {
                        case InterpBehaviour::OneShot:
                        case InterpBehaviour::OneShotReverse: m.Time = std::clamp(m.Time, 0.0f, 1.0f); m.Active = false; break;
                        case InterpBehaviour::LoopReset: m.Time = m.Time - std::floor(m.Time); break;
                        case InterpBehaviour::PingPong: m.Time = std::clamp(m.Time, 0.0f, 1.0f); m.Direction = -m.Direction; break;
                    }
                }
                std::vector<glm::vec3> pts;
                for (const auto& p : m.ControlPoints) pts.push_back(m.Start + p);
                const glm::vec3 target = PathPoint(pts, m.Time);
                if (physics && reg.all_of<RigidbodyComponent>(e))
                    physics->Teleport(ent, target, glm::quat(reg.get<TransformComponent>(e).Rotation));
                MoveTo(scene, ent, target);
            }
        }

        void ApplyEnvironment(const Scene& scene, SceneEnvironment& env)
        {
            Scene& s = const_cast<Scene&>(scene);
            Entity cam = s.GetPrimaryCameraEntity();
            if (!cam || !cam.HasComponent<CineCameraComponent>())
                return;
            const auto& cc = cam.GetComponent<CineCameraComponent>();
            auto& post = env.Settings.Post;
            if (cc.FocusMethod == CineFocusMethod::Disable)
                post.DepthOfField = false;
            else if (cc.FocusMethod != CineFocusMethod::DoNotOverride)
            {
                post.DepthOfField = true;
                post.FocusDistance = std::max(cc.CurrentFocusDistance, 0.01f);
                post.Aperture = cc.Aperture;
                post.FocalLength = cc.FocalLength;
            }
        }

        void RegisterNatives(Scripting::NativeRegistry* vm)
        {
            using CQS::Value;
            // AddMovementInput(varlık, x, y, z [, ölçek]): Floating Pawn Movement'a bu karenin hareket isteği (dünya yönü)
            vm->DefineNative("AddMovementInput", [](int argc, Value* args) -> Value {
                Scene* scene = ScriptContext::GetActiveScene();
                if (!scene || argc < 4 || !args[0].IsNumber())
                    return Value::MakeBool(false);
                const uint32_t e = (uint32_t)(int64_t)args[0].ToNumber();
                AddMovementInput(*scene, e, glm::vec3((float)args[1].ToNumber(), (float)args[2].ToNumber(), (float)args[3].ToNumber()),
                                 argc > 4 ? (float)args[4].ToNumber() : 1.0f);
                return Value::MakeBool(true);
            }, -1);
        }
    }

    template<> void Scene::OnComponentAdded<FloatingPawnMovementComponent>(Entity, FloatingPawnMovementComponent&) {}
    template<> void Scene::OnComponentAdded<InterpToMovementComponent>(Entity, InterpToMovementComponent&) {}
    template<> void Scene::OnComponentAdded<CableComponent>(Entity, CableComponent&) {}
    template<> void Scene::OnComponentAdded<CineCameraComponent>(Entity, CineCameraComponent&) {}

    void RegisterActorComponents()
    {
        const glm::vec4 kMove{ 0.45f, 0.85f, 0.55f, 1.0f };
        {
            using C = FloatingPawnMovementComponent;
            ComponentDef<C>("Floating Pawn Movement", "FloatingPawnMovementComponent", "Gameplay", ICON_FA_PLANE).Color(kMove)
                .Describe("Flying movement without gravity (Unreal Floating Pawn Movement): accelerates towards the input, slows down when it stops. "
                          "Default input: WASD and E / Q; scripts call AddMovementInput(entity, x, y, z).")
                .Field("MaxSpeed", "Max Speed", &C::MaxSpeed).Range(0.0f, 1000.0f, 0.1f)
                .Field("Acceleration", "Acceleration", &C::Acceleration).Range(0.0f, 10000.0f, 0.5f)
                .Field("Deceleration", "Deceleration", &C::Deceleration).Range(0.0f, 10000.0f, 0.5f)
                .Field("TurningBoost", "Turning Boost", &C::TurningBoost).Range(0.0f, 100.0f, 0.1f)
                .Field("UseDefaultInput", "Use Default Input", &C::UseDefaultInput);
        }
        {
            using C = InterpToMovementComponent;
            ComponentDef<C>("Interp To Movement", "InterpToMovementComponent", "Gameplay", ICON_FA_ROUTE).Color(kMove)
                .Describe("Moves the object through Control Points (relative to where it starts) in Duration seconds: platforms, doors, elevators.")
                .Field("Duration", "Duration", &C::Duration).Range(0.01f, 10000.0f, 0.05f)
                .Field("Behaviour", "Behaviour Type", &C::Behaviour).Options({ "One Shot", "One Shot Reverse", "Loop Reset", "Ping Pong" })
                .Field("ControlPoints", "Control Points", &C::ControlPoints).Speed(0.05f)
                .Field("AutoActivate", "Auto Activate", &C::AutoActivate);
        }
        {
            using C = CableComponent;
            ComponentDef<C>("Cable", "CableComponent", "Rendering", ICON_FA_LINK).Color({ 0.55f, 0.8f, 1.0f, 1.0f })
                .Describe("A hanging cable or rope from this object to Attach End To (or End Location): simulated with gravity, also in the editor.")
                .Field("AttachEndTo", "Attach End To", &C::AttachEndTo)
                .Field("EndLocation", "End Location", &C::EndLocation).Speed(0.05f)
                .Field("CableLength", "Cable Length", &C::CableLength).Range(0.01f, 10000.0f, 0.05f)
                .Field("NumSegments", "Num Segments", &C::NumSegments).Range(1.0f, 256.0f, 1.0f)
                .Field("SolverIterations", "Solver Iterations", &C::SolverIterations).Range(1.0f, 32.0f, 1.0f)
                .Field("CableWidth", "Cable Width", &C::CableWidth).Range(0.001f, 2.0f, 0.001f)
                .Field("GravityScale", "Cable Gravity Scale", &C::GravityScale).Range(-10.0f, 10.0f, 0.05f)
                .Field("Color", "Color", &C::Color, FieldKind::Color4);
        }
        {
            using C = CineCameraComponent;
            ComponentDef<C>("Cine Camera", "CineCameraComponent", "Cameras", ICON_FA_FILM).Color({ 0.55f, 0.8f, 1.0f, 1.0f })
                .Requires("CameraComponent")
                .Describe("Film camera settings (Unreal Cine Camera): the field of view comes from the sensor size and focal length, and the "
                          "depth of field from the aperture and focus (manual distance or tracking an object).")
                .Field("SensorWidth", "Sensor Width (mm)", &C::SensorWidth).Range(1.0f, 100.0f, 0.01f).Header("Filmback")
                .Field("SensorHeight", "Sensor Height (mm)", &C::SensorHeight).Range(1.0f, 100.0f, 0.01f)
                .Field("FocalLength", "Focal Length (mm)", &C::FocalLength).Range(4.0f, 1000.0f, 0.1f).Header("Lens")
                .Field("Aperture", "Aperture (f-stop)", &C::Aperture).Range(1.0f, 32.0f, 0.1f)
                .Field("FocusMethod", "Focus Method", &C::FocusMethod).Options({ "Do Not Override", "Manual", "Tracking", "Disable" }).Header("Focus")
                .Field("ManualFocusDistance", "Manual Focus Distance", &C::ManualFocusDistance).Range(0.01f, 100000.0f, 0.05f)
                .Field("TrackingTarget", "Actor To Track", &C::TrackingTarget)
                .Field("FieldOfView", "Field Of View", &C::FieldOfView).ReadOnly().Header("Info")
                .Field("CurrentFocusDistance", "Current Focus Distance", &C::CurrentFocusDistance).ReadOnly();
        }
    }
}
