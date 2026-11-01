#include "CameraRig.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Core/Input/GameInput.h"
#include "Core/Input/Input.h"
#include "Core/Math/Math.h"
#include "Core/PhysicsSystem/Physics3D/PhysicsWorld3D.h"

#include <IconsFontAwesome6.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace Conqueror
{
    namespace
    {
        struct ActiveImpulse
        {
            glm::vec3 Position{ 0.0f };
            float Amplitude = 1.0f, Frequency = 12.0f, Duration = 0.4f, Radius = 30.0f;
            float Elapsed = 0.0f;
            float Phase = 0.0f;
        };

        std::unordered_map<const Scene*, std::vector<ActiveImpulse>>& Impulses()
        {
            static std::unordered_map<const Scene*, std::vector<ActiveImpulse>> s_Impulses;
            return s_Impulses;
        }

        // Ucuz, yumuşak 1B gürültü (değer gürültüsü + kosinüs ara değerleme)
        float Noise1(float x)
        {
            auto hash = [](int n) {
                n = (n << 13) ^ n;
                return 1.0f - (float)((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f;
            };
            int i = (int)std::floor(x);
            float f = x - (float)i;
            float u = (1.0f - std::cos(f * 3.14159265f)) * 0.5f;
            return hash(i) * (1.0f - u) + hash(i + 1) * u;
        }

        bool WorldPose(Scene& scene, uint64_t uuid, glm::vec3& position, glm::quat& rotation)
        {
            if (!uuid) return false;
            Entity e = scene.FindEntityByUUID(uuid);
            if (!e) return false;
            glm::vec3 s, r;
            Math::DecomposeTransform(scene.ComputeWorldMatrix((entt::entity)e), position, r, s);
            rotation = glm::quat(r);
            return true;
        }

        float Damp(float damping, float dt)
        {
            return damping <= 1e-4f ? 1.0f : 1.0f - std::exp(-dt / damping);
        }

        glm::vec3 ImpulseDisplacement(const Scene& scene, const glm::vec3& at, float gain)
        {
            glm::vec3 total(0.0f);
            auto it = Impulses().find(&scene);
            if (it == Impulses().end() || gain <= 0.0f)
                return total;
            for (const auto& im : it->second)
            {
                float d = glm::distance(at, im.Position);
                if (d >= im.Radius) continue;
                float falloff = 1.0f - d / std::max(im.Radius, 1e-3f);
                float env = 1.0f - std::clamp(im.Elapsed / std::max(im.Duration, 1e-3f), 0.0f, 1.0f);
                env *= env;
                float w = 6.2831853f * im.Frequency * im.Elapsed + im.Phase;
                total += glm::vec3(std::sin(w), std::sin(w * 1.31f + 1.7f), std::sin(w * 0.87f + 4.1f)) * im.Amplitude * env * falloff * gain;
            }
            return total;
        }
    }

    namespace CameraRig
    {
        void GenerateImpulseAt(Scene& scene, const glm::vec3& position, float amplitude, float frequency, float duration, float radius)
        {
            ActiveImpulse im;
            im.Position = position;
            im.Amplitude = amplitude;
            im.Frequency = frequency;
            im.Duration = std::max(duration, 0.01f);
            im.Radius = std::max(radius, 0.01f);
            im.Phase = (float)(Impulses()[&scene].size() * 1.618);
            Impulses()[&scene].push_back(im);
        }

        void GenerateImpulse(Scene& scene, uint32_t sourceEntity, float strength)
        {
            Entity e{ (entt::entity)sourceEntity, &scene };
            if (!scene.m_Registry.valid((entt::entity)sourceEntity) || !e.HasComponent<ImpulseSourceComponent>())
                return;
            const auto& src = e.GetComponent<ImpulseSourceComponent>();
            glm::vec3 p = glm::vec3(scene.ComputeWorldMatrix((entt::entity)sourceEntity)[3]);
            GenerateImpulseAt(scene, p, src.Amplitude * strength, src.Frequency, src.Duration, src.Radius);
        }

        void OnCollision(Scene& scene, uint32_t entity, float impulse)
        {
            Entity e{ (entt::entity)entity, &scene };
            if (!scene.m_Registry.valid((entt::entity)entity) || !e.HasComponent<ImpulseSourceComponent>())
                return;
            if (!e.GetComponent<ImpulseSourceComponent>().GenerateOnCollision)
                return;
            // Çarpışma darbesi (N·s) kütleye göre kabaca 0..1 güce çevrilir
            GenerateImpulse(scene, entity, std::clamp(impulse / 50.0f, 0.1f, 2.0f));
        }

        void Reset(Scene* scene)
        {
            if (scene) Impulses().erase(scene);
            else Impulses().clear();
        }

        void LateUpdate(Scene& scene, float dt)
        {
            auto& reg = scene.m_Registry;

            // Darbeler: süresi biten silinir; "başlangıçta üret" kaynakları ilk karede
            auto& impulses = Impulses()[&scene];
            for (auto& im : impulses) im.Elapsed += dt;
            impulses.erase(std::remove_if(impulses.begin(), impulses.end(), [](const ActiveImpulse& im) { return im.Elapsed >= im.Duration; }), impulses.end());
            for (auto h : reg.view<ImpulseSourceComponent>())
            {
                auto& src = reg.get<ImpulseSourceComponent>(h);
                if (src.GenerateOnStart && scene.IsEntityActive(h))
                {
                    src.GenerateOnStart = false;   // yalnızca bir kez (çalışma zamanı kopyasında)
                    GenerateImpulse(scene, (uint32_t)h, 1.0f);
                }
            }

            // ── Sanal kameralar ──
            uint32_t live = 0xFFFFFFFFu;
            int livePriority = -2147483647;
            const glm::vec2 mouseDelta = GameInput::GetMouseDelta();
            for (auto h : reg.view<TransformComponent, VirtualCameraComponent>())
            {
                auto& vc = reg.get<VirtualCameraComponent>(h);
                vc.Live = false;
                if (!scene.IsEntityActive(h))
                    continue;
                glm::vec3 selfPos, selfScale, selfEuler;
                Math::DecomposeTransform(scene.ComputeWorldMatrix(h), selfPos, selfEuler, selfScale);
                glm::quat selfRot(selfEuler);
                if (!vc.Initialized)
                {
                    vc.StatePosition = selfPos;
                    vc.StateRotation = selfRot;
                    vc.Initialized = true;
                }

                glm::vec3 tPos(0.0f);
                glm::quat tRot(1, 0, 0, 0);
                const bool hasFollow = WorldPose(scene, vc.Follow, tPos, tRot);

                // Gövde: istenen konum
                glm::vec3 desired = vc.StatePosition;
                glm::vec3 yawFwd = tRot * glm::vec3(0, 0, -1);
                yawFwd.y = 0.0f;
                glm::quat yawRot = glm::length(yawFwd) > 1e-5f ? glm::quatLookAt(glm::normalize(yawFwd), glm::vec3(0, 1, 0)) : glm::quat(1, 0, 0, 0);
                if (hasFollow)
                {
                    switch (vc.Body)
                    {
                        case VCamBody::Follow:
                        {
                            glm::vec3 off = vc.FollowOffset;
                            if (vc.Binding == VCamBinding::LockToTargetWithWorldUp) off = yawRot * off;
                            else if (vc.Binding == VCamBinding::LockToTarget) off = tRot * off;
                            desired = tPos + off;
                            break;
                        }
                        case VCamBody::ThirdPerson:
                        {
                            glm::vec3 pivot = tPos + tRot * vc.ShoulderOffset;
                            glm::vec3 back = tRot * glm::vec3(0, 0, 1);
                            desired = pivot + back * vc.CameraDistance;
                            if (vc.AvoidObstacles)
                                if (PhysicsWorld3D* physics = scene.GetPhysicsWorld3D(); physics && physics->IsRunning())
                                {
                                    Physics3D::RaycastHit hit;
                                    Entity target = scene.FindEntityByUUID(vc.Follow);
                                    uint32_t ignore = target ? (uint32_t)(entt::entity)target : Physics3D::NoEntity;
                                    if (physics->SphereCast(pivot, std::max(vc.CameraRadius, 0.01f), back, vc.CameraDistance, hit, 0xFFFFFFFFu, ignore))
                                        desired = pivot + back * std::max(hit.Distance - 0.05f, 0.1f);
                                }
                            break;
                        }
                        case VCamBody::Orbital:
                        {
                            if (vc.OrbitInput && (Input::IsMouseButtonPressed(Mouse::ButtonRight) || Input::GetCursorMode() != 0))
                            {
                                vc.OrbitYaw -= mouseDelta.x * vc.OrbitSpeed;
                                vc.OrbitPitch = std::clamp(vc.OrbitPitch + mouseDelta.y * vc.OrbitSpeed, -80.0f, 80.0f);
                            }
                            float yaw = glm::radians(vc.OrbitYaw), pitch = glm::radians(vc.OrbitPitch);
                            glm::vec3 dir(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
                            desired = tPos + glm::vec3(0, vc.OrbitHeight, 0) + dir * vc.OrbitRadius;
                            break;
                        }
                        case VCamBody::DoNothing:
                            desired = selfPos;
                            break;
                    }
                }
                else if (vc.Body == VCamBody::DoNothing || !vc.Follow)
                    desired = selfPos;

                vc.StatePosition.x += (desired.x - vc.StatePosition.x) * Damp(vc.PositionDamping.x, dt);
                vc.StatePosition.y += (desired.y - vc.StatePosition.y) * Damp(vc.PositionDamping.y, dt);
                vc.StatePosition.z += (desired.z - vc.StatePosition.z) * Damp(vc.PositionDamping.z, dt);

                // Nişan: istenen dönüş
                glm::quat aimRot = vc.StateRotation;
                glm::vec3 lPos;
                glm::quat lRot;
                const bool hasLook = WorldPose(scene, vc.LookAt ? vc.LookAt : vc.Follow, lPos, lRot);
                switch (vc.Aim)
                {
                    case VCamAim::Composer:
                    case VCamAim::HardLookAt:
                        if (hasLook)
                        {
                            glm::vec3 to = lPos + vc.LookAtOffset - vc.StatePosition;
                            if (glm::length(to) > 1e-4f)
                            {
                                glm::quat look = glm::quatLookAt(glm::normalize(to), glm::vec3(0, 1, 0));
                                aimRot = vc.Aim == VCamAim::HardLookAt ? look : glm::slerp(vc.StateRotation, look, Damp(vc.AimDamping, dt));
                            }
                        }
                        break;
                    case VCamAim::SameAsFollowTarget:
                        if (hasFollow) aimRot = tRot;
                        break;
                    case VCamAim::DoNothing:
                        aimRot = selfRot;
                        break;
                }
                vc.StateRotation = glm::normalize(aimRot);

                // Kendi dönüşümüne yaz (sahne görünümünde nereden baktığı görünsün)
                Entity self{ h, &scene };
                scene.SetEntityWorldPose(self, vc.StatePosition, &vc.StateRotation);

                if (vc.Priority >= livePriority)
                {
                    livePriority = vc.Priority;
                    live = (uint32_t)h;
                }
            }
            if (live != 0xFFFFFFFFu)
                reg.get<VirtualCameraComponent>((entt::entity)live).Live = true;

            // ── Beyin: canlı sanal kamerayı gerçek kameraya uygula, geçişlerde harmanla ──
            for (auto h : reg.view<TransformComponent, CameraBrainComponent, CameraComponent>())
            {
                if (!scene.IsEntityActive(h) || live == 0xFFFFFFFFu)
                    continue;
                auto& brain = reg.get<CameraBrainComponent>(h);
                auto& cam = reg.get<CameraComponent>(h);
                auto& vc = reg.get<VirtualCameraComponent>((entt::entity)live);
                Entity liveEntity{ (entt::entity)live, &scene };
                uint64_t liveId = liveEntity.HasComponent<IDComponent>() ? (uint64_t)liveEntity.GetComponent<IDComponent>().ID : 0;

                glm::vec3 curPos, curScale, curEuler;
                Math::DecomposeTransform(scene.ComputeWorldMatrix(h), curPos, curEuler, curScale);
                const float curFov = glm::degrees(cam.Camera.GetPerspectiveVerticalFOV());
                if (liveId != brain.ActiveCamera)
                {
                    brain.ActiveCamera = liveId;
                    if (brain.HasPose && brain.BlendCurve != CameraBlendCurve::Cut && brain.DefaultBlend > 1e-3f)
                    {
                        brain.BlendFromPosition = curPos;
                        brain.BlendFromRotation = glm::quat(curEuler);
                        brain.BlendFromFov = curFov;
                        brain.BlendProgress = 0.0f;
                    }
                    else
                        brain.BlendProgress = 1.0f;
                }
                brain.BlendProgress = std::min(1.0f, brain.BlendProgress + dt / std::max(brain.DefaultBlend, 1e-3f));
                float t = brain.BlendProgress;
                if (brain.BlendCurve == CameraBlendCurve::EaseInOut)
                    t = t * t * (3.0f - 2.0f * t);

                // Gürültü ve sarsıntı canlı kameranın üstüne eklenir (harmanlanmış pozda)
                vc.NoiseTime += dt * std::max(vc.NoiseFrequency, 0.0f);
                glm::vec3 shake = ImpulseDisplacement(scene, vc.StatePosition, vc.ImpulseGain);
                glm::vec3 noiseEuler(0.0f);
                if (vc.NoiseAmplitude > 0.0f)
                    noiseEuler = glm::radians(glm::vec3(Noise1(vc.NoiseTime * 2.1f), Noise1(vc.NoiseTime * 1.7f + 37.0f), Noise1(vc.NoiseTime * 1.3f + 71.0f) * 0.5f) * vc.NoiseAmplitude);
                glm::quat targetRot = vc.StateRotation * glm::quat(noiseEuler) * glm::angleAxis(glm::radians(vc.Dutch), glm::vec3(0, 0, 1));
                // Sarsıntının küçük bir kısmı açısal (daha inandırıcı)
                targetRot = targetRot * glm::quat(glm::vec3(shake.y, shake.x, 0.0f) * 0.02f);
                glm::vec3 targetPos = vc.StatePosition + shake;

                glm::vec3 pos = brain.BlendProgress >= 1.0f ? targetPos : glm::mix(brain.BlendFromPosition, targetPos, t);
                glm::quat rot = brain.BlendProgress >= 1.0f ? targetRot : glm::slerp(brain.BlendFromRotation, targetRot, t);
                scene.SetEntityWorldPose(Entity{ h, &scene }, pos, &rot);
                if (brain.ApplyFieldOfView && cam.Camera.GetProjectionType() == SceneCamera::ProjectionType::Perspective)
                {
                    float fov = brain.BlendProgress >= 1.0f ? vc.FieldOfView : glm::mix(brain.BlendFromFov, vc.FieldOfView, t);
                    cam.Camera.SetPerspectiveVerticalFOV(glm::radians(std::clamp(fov, 1.0f, 179.0f)));
                }
                brain.HasPose = true;
            }
        }
    }

    void RegisterCameraRigComponents()
    {
        const glm::vec4 kCamColor{ 0.95f, 0.45f, 0.35f, 1.0f };

        ComponentDef<VirtualCameraComponent>("Virtual Camera", "VirtualCameraComponent", "Cameras", ICON_FA_VIDEO).Color(kCamColor)
            .Describe("Cinemachine-style camera shot: follows and frames targets with damping. The highest-priority active one drives the Camera Brain.")
            .Field("Priority", "Priority", &VirtualCameraComponent::Priority).Range(-1000.0f, 1000.0f, 1.0f)
            .Field("Follow", "Follow", &VirtualCameraComponent::Follow)
            .Field("LookAt", "Look At", &VirtualCameraComponent::LookAt).Tooltip("Empty = the Follow target.")
            .Field("Body", "Body", &VirtualCameraComponent::Body).Options({ "Do Nothing", "Follow", "Third Person", "Orbital" }).Header("Body")
            .Field("Binding", "Binding Mode", &VirtualCameraComponent::Binding).Options({ "World", "Lock To Target (World Up)", "Lock To Target" })
                .VisibleIf([](const VirtualCameraComponent& c) { return c.Body == VCamBody::Follow; })
            .Field("FollowOffset", "Follow Offset", &VirtualCameraComponent::FollowOffset).Speed(0.05f)
                .VisibleIf([](const VirtualCameraComponent& c) { return c.Body == VCamBody::Follow; })
            .Field("ShoulderOffset", "Shoulder Offset", &VirtualCameraComponent::ShoulderOffset).Speed(0.02f)
                .VisibleIf([](const VirtualCameraComponent& c) { return c.Body == VCamBody::ThirdPerson; })
            .Field("CameraDistance", "Camera Distance", &VirtualCameraComponent::CameraDistance).Range(0.0f, 1000.0f, 0.05f)
                .VisibleIf([](const VirtualCameraComponent& c) { return c.Body == VCamBody::ThirdPerson; })
            .Field("AvoidObstacles", "Avoid Obstacles", &VirtualCameraComponent::AvoidObstacles)
                .VisibleIf([](const VirtualCameraComponent& c) { return c.Body == VCamBody::ThirdPerson; })
            .Field("CameraRadius", "Camera Radius", &VirtualCameraComponent::CameraRadius).Range(0.0f, 5.0f, 0.01f)
                .VisibleIf([](const VirtualCameraComponent& c) { return c.Body == VCamBody::ThirdPerson && c.AvoidObstacles; })
            .Field("OrbitRadius", "Orbit Radius", &VirtualCameraComponent::OrbitRadius).Range(0.0f, 10000.0f, 0.05f)
                .VisibleIf([](const VirtualCameraComponent& c) { return c.Body == VCamBody::Orbital; })
            .Field("OrbitHeight", "Orbit Height", &VirtualCameraComponent::OrbitHeight).Speed(0.05f)
                .VisibleIf([](const VirtualCameraComponent& c) { return c.Body == VCamBody::Orbital; })
            .Field("OrbitInput", "Mouse Orbit", &VirtualCameraComponent::OrbitInput)
                .Tooltip("Rotate with the mouse while the right button is held or the cursor is locked.")
                .VisibleIf([](const VirtualCameraComponent& c) { return c.Body == VCamBody::Orbital; })
            .Field("OrbitSpeed", "Orbit Speed", &VirtualCameraComponent::OrbitSpeed).Range(0.0f, 10.0f, 0.01f)
                .VisibleIf([](const VirtualCameraComponent& c) { return c.Body == VCamBody::Orbital; })
            .Field("OrbitYaw", "Orbit Yaw", &VirtualCameraComponent::OrbitYaw, FieldKind::Angle).Range(-360.0f, 360.0f, 0.5f).Format("%.1f°")
                .VisibleIf([](const VirtualCameraComponent& c) { return c.Body == VCamBody::Orbital; })
            .Field("OrbitPitch", "Orbit Pitch", &VirtualCameraComponent::OrbitPitch, FieldKind::Angle).Range(-80.0f, 80.0f, 0.5f).Format("%.1f°")
                .VisibleIf([](const VirtualCameraComponent& c) { return c.Body == VCamBody::Orbital; })
            .Field("PositionDamping", "Damping", &VirtualCameraComponent::PositionDamping).Speed(0.01f)
                .Tooltip("Seconds to catch up on each axis (0 = rigid).")
            .Field("Aim", "Aim", &VirtualCameraComponent::Aim).Options({ "Do Nothing", "Composer", "Hard Look At", "Same As Follow Target" }).Header("Aim")
            .Field("AimDamping", "Aim Damping", &VirtualCameraComponent::AimDamping).Range(0.0f, 10.0f, 0.01f)
                .VisibleIf([](const VirtualCameraComponent& c) { return c.Aim == VCamAim::Composer; })
            .Field("LookAtOffset", "Look At Offset", &VirtualCameraComponent::LookAtOffset).Speed(0.02f)
            .Field("FieldOfView", "Field Of View", &VirtualCameraComponent::FieldOfView, FieldKind::Angle).Range(1.0f, 179.0f, 0.5f).Format("%.1f°").Header("Lens")
            .Field("Dutch", "Dutch", &VirtualCameraComponent::Dutch, FieldKind::Angle).Range(-180.0f, 180.0f, 0.5f).Format("%.1f°")
            .Field("NoiseAmplitude", "Noise Amplitude", &VirtualCameraComponent::NoiseAmplitude).Range(0.0f, 45.0f, 0.05f).Format("%.2f°").Header("Noise")
                .Tooltip("Hand-held camera wobble.")
            .Field("NoiseFrequency", "Noise Frequency", &VirtualCameraComponent::NoiseFrequency).Range(0.0f, 50.0f, 0.05f)
            .Field("ImpulseGain", "Impulse Gain", &VirtualCameraComponent::ImpulseGain).Range(0.0f, 10.0f, 0.05f)
                .Tooltip("How strongly Impulse Sources shake this camera (0 = ignore).")
            .Field("Live", "Live", &VirtualCameraComponent::Live).ReadOnly().Header("Runtime");

        ComponentDef<CameraBrainComponent>("Camera Brain", "CameraBrainComponent", "Cameras", ICON_FA_BRAIN).Color(kCamColor)
            .Requires("CameraComponent")
            .Describe("Put on the real Camera: it shows the live Virtual Camera and blends smoothly when the shot changes.")
            .Field("DefaultBlend", "Default Blend", &CameraBrainComponent::DefaultBlend).Range(0.0f, 60.0f, 0.05f).Format("%.2f s")
            .Field("BlendCurve", "Blend Curve", &CameraBrainComponent::BlendCurve).Options({ "Ease In Out", "Linear", "Cut" })
            .Field("ApplyFieldOfView", "Apply Field Of View", &CameraBrainComponent::ApplyFieldOfView)
            .Field("BlendProgress", "Blend Progress", &CameraBrainComponent::BlendProgress).ReadOnly().Header("Runtime");

        ComponentDef<ImpulseSourceComponent>("Impulse Source", "ImpulseSourceComponent", "Cameras", ICON_FA_BURST).Color(kCamColor)
            .Describe("Camera shake emitter (explosions, landings). Trigger it with ImpulseGenerate(entity, strength), on start or on collision.")
            .Field("Amplitude", "Amplitude", &ImpulseSourceComponent::Amplitude).Range(0.0f, 100.0f, 0.01f)
            .Field("Frequency", "Frequency", &ImpulseSourceComponent::Frequency).Range(0.1f, 100.0f, 0.1f).Format("%.1f Hz")
            .Field("Duration", "Duration", &ImpulseSourceComponent::Duration).Range(0.01f, 30.0f, 0.01f).Format("%.2f s")
            .Field("Radius", "Radius", &ImpulseSourceComponent::Radius).Range(0.0f, 100000.0f, 0.1f)
            .Field("GenerateOnStart", "Generate On Start", &ImpulseSourceComponent::GenerateOnStart)
            .Field("GenerateOnCollision", "Generate On Collision", &ImpulseSourceComponent::GenerateOnCollision);
    }
}
