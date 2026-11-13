#include "XRInteraction.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/GameplayComponents.h"
#include "Scene/ComponentRegistry.h"
#include "Core/Input/GameInput.h"
#include "Core/Input/Input.h"
#include "Core/KeyCodes.h"
#include "Core/MouseCodes.h"
#include "Core/Math/Math.h"
#include "Core/PhysicsSystem/Physics3D/PhysicsWorld3D.h"
#include "XR/OpenXR.h"

#include <IconsFontAwesome6.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <functional>

namespace Conqueror
{
    namespace
    {
        struct ForcedButtons { int Grip = -1, Trigger = -1, Teleport = -1; };
        ForcedButtons s_Forced[2];

        Entity FindCamera(Entity root)
        {
            std::function<Entity(Entity)> walk = [&](Entity e) -> Entity {
                if (e.HasComponent<CameraComponent>())
                    return e;
                for (Entity c : e.GetChildren())
                    if (Entity f = walk(c))
                        return f;
                return {};
            };
            for (Entity c : root.GetChildren())
                if (Entity f = walk(c))
                    return f;
            return {};
        }

        // Dünya pozunu yerel dönüşüme yaz (ölçek korunur)
        void SetWorldPose(Scene& scene, Entity e, const glm::vec3& pos, const glm::quat& rot)
        {
            auto& tc = e.GetComponent<TransformComponent>();
            glm::mat4 parentWorld(1.0f);
            if (Entity p = e.GetParent())
                parentWorld = scene.ComputeWorldMatrix((entt::entity)p);
            const glm::mat4 local = glm::inverse(parentWorld) * (glm::translate(glm::mat4(1.0f), pos) * glm::toMat4(rot));
            glm::vec3 t, r, s;
            Math::DecomposeTransform(local, t, r, s);
            tc.Position = t;
            tc.Rotation = r;
        }

        template<typename T>
        Entity Owner(Scene& scene, entt::entity hit)
        {
            for (Entity e(hit, &scene); e; e = e.GetParent())
                if (e.HasComponent<T>())
                    return e;
            return {};
        }

        void SetLine(Entity c, const std::vector<glm::vec3>& points, const glm::vec4& color, float width)
        {
            if (!c.HasComponent<LineRendererComponent>())
                c.AddComponent<LineRendererComponent>();
            auto& lr = c.GetComponent<LineRendererComponent>();
            lr.UseWorldSpace = true;
            lr.Positions = points;
            lr.StartColor = lr.EndColor = color;
            lr.StartWidth = lr.EndWidth = width;
        }

        uint64_t UUIDOf(Entity e) { return e && e.HasComponent<IDComponent>() ? (uint64_t)e.GetComponent<IDComponent>().ID : 0; }

        void Invoke(Scene& scene, Entity e, const char* method)
        {
            if (e)
                scene.InvokeScriptMethod((uint32_t)(entt::entity)e, method, nullptr, 0);
        }
    }

    namespace XRInteraction
    {
        void SetSimulatedButtons(int hand, int grip, int trigger, int teleport)
        {
            if (hand < 0 || hand > 1)
                return;
            s_Forced[hand] = { grip, trigger, teleport };
        }

        void Stop(Scene&)
        {
            s_Forced[0] = s_Forced[1] = {};
        }

        void Update(Scene& scene, float dt)
        {
            auto& reg = scene.m_Registry;
            Entity origin;
            for (auto e : reg.view<XROriginComponent>())
                if (scene.IsEntityActive(e)) { origin = Entity{ e, &scene }; break; }
            if (!origin)
                return;
            auto& xo = origin.GetComponent<XROriginComponent>();
            auto& ot = origin.GetComponent<TransformComponent>();

            // OpenXR gözlüğü varsa baş, eller ve kumanda düğmeleri ondan; yoksa XR Device Simulator (fare baş, tuşlar eller)
            glm::vec3 xrHeadPos(0.0f);
            glm::quat xrHeadRot(1.0f, 0.0f, 0.0f, 0.0f);
            const bool hmd = OpenXR::IsRunning() && OpenXR::HeadPose(xrHeadPos, xrHeadRot);
            const OpenXR::HandInput leftPad = OpenXR::Hand(0), rightPad = OpenXR::Hand(1);
            if (!hmd)
            {
                if (!xo.CursorLocked)
                {
                    Input::SetCursorMode(2);
                    xo.CursorLocked = true;
                }
                const glm::vec2 md = GameInput::GetMouseDelta();
                xo.HeadYaw -= md.x * xo.LookSensitivity;
                xo.HeadPitch = std::clamp(xo.HeadPitch - md.y * xo.LookSensitivity, -85.0f, 85.0f);
            }
            else
            {
                // Gözlük başı: sapma açısı yürüme yönü için (Unity XRI: baş yönüne göre hareket)
                const glm::vec3 f = xrHeadRot * glm::vec3(0.0f, 0.0f, -1.0f);
                xo.HeadYaw = glm::degrees(std::atan2(-f.x, -f.z));
                xo.HeadPitch = glm::degrees(std::asin(std::clamp(f.y, -1.0f, 1.0f)));
            }
            if (origin.HasComponent<SnapTurnProviderComponent>())
            {
                const float amount = glm::radians(origin.GetComponent<SnapTurnProviderComponent>().TurnAmount);
                if (GameInput::IsKeyDown(Key::Q)) ot.Rotation.y += amount;
                if (GameInput::IsKeyDown(Key::E)) ot.Rotation.y -= amount;
                // Sağ çubuk sola / sağa: bir dönüş, çubuk ortaya dönünce yeniden (Unity XRI Snap Turn Provider)
                static bool s_SnapArmed = true;
                if (std::abs(rightPad.Stick.x) < 0.3f)
                    s_SnapArmed = true;
                else if (s_SnapArmed && std::abs(rightPad.Stick.x) > 0.75f)
                {
                    ot.Rotation.y += rightPad.Stick.x < 0.0f ? amount : -amount;
                    s_SnapArmed = false;
                }
            }
            const float bodyYaw = glm::degrees(ot.Rotation.y) + xo.HeadYaw;
            if (origin.HasComponent<ContinuousMoveProviderComponent>())
            {
                const auto& mp = origin.GetComponent<ContinuousMoveProviderComponent>();
                glm::vec2 in(0.0f);
                if (GameInput::IsKeyHeld(Key::W)) in.y += 1.0f;
                if (GameInput::IsKeyHeld(Key::S)) in.y -= 1.0f;
                if (mp.EnableStrafe && GameInput::IsKeyHeld(Key::D)) in.x += 1.0f;
                if (mp.EnableStrafe && GameInput::IsKeyHeld(Key::A)) in.x -= 1.0f;
                // Sol çubuk (ölü bölge 0.15)
                if (glm::length(leftPad.Stick) > 0.15f)
                    in += glm::vec2(mp.EnableStrafe ? leftPad.Stick.x : 0.0f, leftPad.Stick.y);
                if (glm::length(in) > 0.0f)
                {
                    in = glm::length(in) > 1.0f ? glm::normalize(in) : in;
                    const float y = glm::radians(bodyYaw);
                    const glm::vec3 fwd(-std::sin(y), 0.0f, -std::cos(y)), right(std::cos(y), 0.0f, -std::sin(y));
                    ot.Position += (fwd * in.y + right * in.x) * mp.MoveSpeed * dt;
                }
            }
            // Baş (kamera) ve eller: XR Origin'in yerel uzayında
            // Gözlük: Stage uzayı zemin kökenli (boy gerçek); simülatör: Camera Y Offset
            const glm::quat headRot = hmd ? xrHeadRot : glm::quat(glm::vec3(glm::radians(xo.HeadPitch), glm::radians(xo.HeadYaw), 0.0f));
            const glm::vec3 headPos = hmd ? xrHeadPos : glm::vec3(0.0f, xo.CameraYOffset, 0.0f);
            Entity cam = FindCamera(origin);
            if (cam)
            {
                auto& ct = cam.GetComponent<TransformComponent>();
                if (cam.GetParent() == origin)
                {
                    ct.Position = headPos;
                    ct.Rotation = glm::eulerAngles(headRot);
                }
            }
            scene.UpdateWorldTransforms();
            const glm::mat4 originWorld = scene.ComputeWorldMatrix((entt::entity)origin);
            for (auto e : reg.view<XRControllerComponent>())
            {
                if (!scene.IsEntityActive(e))
                    continue;
                Entity c{ e, &scene };
                auto& xc = c.GetComponent<XRControllerComponent>();
                const int hand = xc.Hand == XRHand::Right ? 1 : 0;
                glm::vec3 local = headPos + headRot * glm::vec3(hand ? 0.22f : -0.22f, -0.25f, -0.45f);
                glm::quat localRot = headRot;
                const bool tracked = hmd && OpenXR::HandPose(hand, local, localRot);
                const glm::vec3 wp = glm::vec3(originWorld * glm::vec4(local, 1.0f));
                const glm::quat wr = glm::quat_cast(glm::mat3(originWorld)) * localRot;
                SetWorldPose(scene, c, wp, wr);
                if (hmd)
                {
                    // Kumanda: tutma = yan tetik (grip), tetik = işaret parmağı, ışınlanma = sağ çubuk ileri (Unity XRI varsayılanı)
                    const OpenXR::HandInput& pad = hand ? rightPad : leftPad;
                    xc.Grip = tracked && pad.Grip > 0.5f;
                    xc.Trigger = tracked && pad.Trigger > 0.5f;
                    xc.Teleport = tracked && hand == 1 && pad.Stick.y > 0.6f;
                }
                else if (hand)
                {
                    xc.Grip = GameInput::IsMouseHeld(Mouse::Button1);
                    xc.Trigger = GameInput::IsMouseHeld(Mouse::Button0);
                    xc.Teleport = GameInput::IsKeyHeld(Key::Space);
                }
                else
                {
                    xc.Grip = GameInput::IsKeyHeld(Key::F);
                    xc.Trigger = GameInput::IsKeyHeld(Key::G);
                    xc.Teleport = false;
                }
                if (s_Forced[hand].Grip >= 0) xc.Grip = s_Forced[hand].Grip != 0;
                if (s_Forced[hand].Trigger >= 0) xc.Trigger = s_Forced[hand].Trigger != 0;
                if (s_Forced[hand].Teleport >= 0) xc.Teleport = s_Forced[hand].Teleport != 0;
            }
            scene.UpdateWorldTransforms();

            // ── Etkileşimciler ──
            PhysicsWorld3D* physics = scene.GetPhysicsWorld3D();
            for (auto e : reg.view<XRControllerComponent, XRRayInteractorComponent>())
            {
                if (!scene.IsEntityActive(e))
                    continue;
                Entity c{ e, &scene };
                auto& xc = c.GetComponent<XRControllerComponent>();
                auto& ri = c.GetComponent<XRRayInteractorComponent>();
                const glm::mat4 w = scene.ComputeWorldMatrix(e);
                const glm::vec3 o = glm::vec3(w[3]);
                const glm::vec3 dir = -glm::normalize(glm::vec3(w[2]));
                // El hızı (fırlatma): son 0.15 s
                xc.History.push_back({ 0.0f, o });
                for (auto& h : xc.History) h.first += dt;
                while (xc.History.size() > 2 && xc.History.front().first > 0.15f) xc.History.pop_front();
                xc.Velocity = xc.History.size() >= 2 && xc.History.front().first > 1e-4f
                                  ? (xc.History.back().second - xc.History.front().second) / xc.History.front().first : glm::vec3(0.0f);

                if (ri.LineType == XRLineType::Straight)
                {
                    Entity held = ri.Selected ? scene.FindEntityByUUID(ri.Selected) : Entity{};
                    if (held)
                    {
                        auto& gi = held.GetComponent<XRGrabInteractableComponent>();
                        const glm::mat4 target = w * ri.AttachOffset;
                        glm::vec3 tp, tr, ts;
                        Math::DecomposeTransform(target, tp, tr, ts);
                        const glm::quat tq = gi.TrackRotation ? glm::quat(tr) : glm::quat_cast(glm::mat3(held.GetComponent<TransformComponent>().WorldTransform));
                        const bool body = physics && held.HasComponent<RigidbodyComponent>();
                        if (body && gi.MovementType == XRMovementType::VelocityTracking && dt > 1e-5f)
                        {
                            const glm::mat4& cur = held.GetComponent<TransformComponent>().WorldTransform;
                            physics->SetLinearVelocity(held, (tp - glm::vec3(cur[3])) / dt);
                            if (gi.TrackRotation)
                            {
                                glm::quat dq = tq * glm::inverse(glm::quat_cast(glm::mat3(cur)));
                                if (dq.w < 0.0f) dq = -dq;
                                const float ang = 2.0f * std::acos(std::clamp(dq.w, -1.0f, 1.0f));
                                const glm::vec3 ax = std::abs(ang) > 1e-5f ? glm::normalize(glm::vec3(dq.x, dq.y, dq.z)) : glm::vec3(0.0f);
                                physics->SetAngularVelocity(held, ax * ang / dt);
                            }
                        }
                        else
                        {
                            SetWorldPose(scene, held, tp, tq);
                            if (body)
                            {
                                physics->Teleport(held, tp, tq);
                                physics->SetLinearVelocity(held, glm::vec3(0.0f));
                            }
                        }
                        if (xc.Trigger && !ri.PrevTrigger)
                            Invoke(scene, held, "OnActivated");   // tetik kenarı (Activate)
                        if (!xc.Grip)
                        {
                            // Bırak: fırlatma hızı
                            gi.IsSelected = false;
                            ri.Selected = 0;
                            if (body && gi.ThrowOnDetach)
                                physics->SetLinearVelocity(held, xc.Velocity * gi.ThrowVelocityScale);
                            Invoke(scene, held, "OnSelectExited");
                        }
                        SetLine(c, { o, glm::vec3(target[3]) }, ri.ValidColor, ri.LineWidth);
                        ri.PrevGrip = xc.Grip;
                        ri.PrevTrigger = xc.Trigger;
                        continue;
                    }
                    ri.Selected = 0;
                    Physics3D::RaycastHit hit;
                    Entity hover;
                    glm::vec3 end = o + dir * ri.MaxDistance;
                    if (physics && physics->Raycast(o, dir, ri.MaxDistance, hit))
                    {
                        end = hit.Point;
                        if (hit.Entity != Physics3D::NoEntity && reg.valid((entt::entity)hit.Entity))
                            hover = Owner<XRGrabInteractableComponent>(scene, (entt::entity)hit.Entity);
                    }
                    const uint64_t hoverId = UUIDOf(hover);
                    if (hoverId != ri.Hovered)
                    {
                        if (Entity old = ri.Hovered ? scene.FindEntityByUUID(ri.Hovered) : Entity{})
                        {
                            old.GetComponent<XRGrabInteractableComponent>().IsHovered = false;
                            Invoke(scene, old, "OnHoverExited");
                        }
                        if (hover)
                        {
                            hover.GetComponent<XRGrabInteractableComponent>().IsHovered = true;
                            Invoke(scene, hover, "OnHoverEntered");
                        }
                        ri.Hovered = hoverId;
                    }
                    // Tutma kenarı: ışının çarptığı nesne ele bağlanır (ele göre şimdiki konumu korunur)
                    if (hover && xc.Grip && !ri.PrevGrip)
                    {
                        auto& gi = hover.GetComponent<XRGrabInteractableComponent>();
                        if (!gi.IsSelected)
                        {
                            gi.IsSelected = true;
                            ri.Selected = hoverId;
                            ri.AttachOffset = glm::inverse(w) * hover.GetComponent<TransformComponent>().WorldTransform;
                            Invoke(scene, hover, "OnSelectEntered");
                        }
                    }
                    ri.PrevGrip = xc.Grip;
                    ri.PrevTrigger = xc.Trigger;
                    SetLine(c, { o, end }, hover ? ri.ValidColor : ri.InvalidColor, ri.LineWidth);
                }
                else
                {
                    // Işınlanma yayı: balistik eğri, ilk çarpılan Teleportation Area / Anchor hedef
                    if (xc.Teleport)
                    {
                        std::vector<glm::vec3> pts{ o };
                        glm::vec3 p = o, v = dir * ri.Velocity;
                        ri.HasTarget = false;
                        ri.TargetAnchor = 0;
                        for (int i = 0; i < 60 && physics; ++i)
                        {
                            const float step = 0.04f;
                            const glm::vec3 np = p + v * step + glm::vec3(0.0f, -9.81f, 0.0f) * (0.5f * step * step);
                            v += glm::vec3(0.0f, -9.81f, 0.0f) * step;
                            const glm::vec3 seg = np - p;
                            const float len = glm::length(seg);
                            Physics3D::RaycastHit hit;
                            if (len > 1e-5f && physics->Raycast(p, seg / len, len, hit))
                            {
                                pts.push_back(hit.Point);
                                if (hit.Entity != Physics3D::NoEntity && reg.valid((entt::entity)hit.Entity))
                                {
                                    if (Entity anchor = Owner<TeleportationAnchorComponent>(scene, (entt::entity)hit.Entity))
                                    {
                                        ri.HasTarget = true;
                                        ri.TargetAnchor = UUIDOf(anchor);
                                        ri.TargetPoint = glm::vec3(anchor.GetComponent<TransformComponent>().WorldTransform[3]);
                                    }
                                    else if (Owner<TeleportationAreaComponent>(scene, (entt::entity)hit.Entity))
                                    {
                                        ri.HasTarget = true;
                                        ri.TargetPoint = hit.Point;
                                    }
                                }
                                break;
                            }
                            pts.push_back(np);
                            p = np;
                        }
                        SetLine(c, pts, ri.HasTarget ? ri.ValidColor : ri.InvalidColor, ri.LineWidth);
                    }
                    else
                    {
                        if (ri.PrevTeleport && ri.HasTarget)
                        {
                            // Bırakınca ışınlan: kameranın zemindeki izi hedefe gelir
                            const glm::vec3 camPos = cam ? glm::vec3(scene.ComputeWorldMatrix((entt::entity)cam)[3]) : glm::vec3(originWorld[3]);
                            ot.Position += glm::vec3(ri.TargetPoint.x - camPos.x, 0.0f, ri.TargetPoint.z - camPos.z);
                            ot.Position.y = ri.TargetPoint.y;
                            if (Entity anchor = ri.TargetAnchor ? scene.FindEntityByUUID(ri.TargetAnchor) : Entity{})
                                if (anchor.GetComponent<TeleportationAnchorComponent>().MatchOrientation)
                                {
                                    const glm::mat4& aw = anchor.GetComponent<TransformComponent>().WorldTransform;
                                    const glm::vec3 f = -glm::vec3(aw[2]);
                                    const float anchorYaw = std::atan2(-f.x, -f.z);
                                    ot.Rotation.y = anchorYaw - glm::radians(xo.HeadYaw);
                                }
                            Invoke(scene, origin, "OnTeleported");
                        }
                        ri.HasTarget = false;
                        SetLine(c, {}, ri.InvalidColor, ri.LineWidth);
                    }
                    ri.PrevTeleport = xc.Teleport;
                }
            }
            scene.UpdateWorldTransforms();
        }
    }

    template<> void Scene::OnComponentAdded<XROriginComponent>(Entity, XROriginComponent&) {}
    template<> void Scene::OnComponentAdded<XRControllerComponent>(Entity, XRControllerComponent&) {}
    template<> void Scene::OnComponentAdded<XRRayInteractorComponent>(Entity, XRRayInteractorComponent&) {}
    template<> void Scene::OnComponentAdded<XRGrabInteractableComponent>(Entity, XRGrabInteractableComponent&) {}
    template<> void Scene::OnComponentAdded<TeleportationAreaComponent>(Entity, TeleportationAreaComponent&) {}
    template<> void Scene::OnComponentAdded<TeleportationAnchorComponent>(Entity, TeleportationAnchorComponent&) {}
    template<> void Scene::OnComponentAdded<SnapTurnProviderComponent>(Entity, SnapTurnProviderComponent&) {}
    template<> void Scene::OnComponentAdded<ContinuousMoveProviderComponent>(Entity, ContinuousMoveProviderComponent&) {}

    void RegisterXRComponents()
    {
        const glm::vec4 kColor{ 0.75f, 0.6f, 1.0f, 1.0f };
        {
            using C = XROriginComponent;
            ComponentDef<C>("XR Origin", "XROriginComponent", "XR", ICON_FA_VR_CARDBOARD).Color(kColor)
                .Describe("The player in VR: its child camera is the head and its XR Controller children are the hands. Without a headset the "
                          "XR Device Simulator runs: mouse looks, right click grabs, left click triggers, Space aims a teleport, Q / E turn, WASD walks.")
                .Field("CameraYOffset", "Camera Y Offset", &C::CameraYOffset).Range(0.0f, 3.0f, 0.01f).Tooltip("Eye height without a headset.")
                .Field("LookSensitivity", "Simulator Look Sensitivity", &C::LookSensitivity).Range(0.0f, 2.0f, 0.005f);
        }
        {
            using C = XRControllerComponent;
            ComponentDef<C>("XR Controller", "XRControllerComponent", "XR", ICON_FA_HAND).Color(kColor)
                .Describe("A hand: follows the left or right controller (simulated without a headset). Add an XR Ray Interactor to point, grab or teleport.")
                .Field("Hand", "Hand", &C::Hand).Options({ "Left", "Right" })
                .Field("Grip", "Grip", &C::Grip).ReadOnly().Header("Input")
                .Field("Trigger", "Trigger", &C::Trigger).ReadOnly();
        }
        {
            using C = XRRayInteractorComponent;
            ComponentDef<C>("XR Ray Interactor", "XRRayInteractorComponent", "XR", ICON_FA_WAND_MAGIC).Color(kColor)
                .Describe("A ray from the hand: Straight rays grab XR Grab Interactables with the grip; Projectile rays aim a teleport arc at "
                          "Teleportation Areas and Anchors (teleports on release).")
                .Field("LineType", "Line Type", &C::LineType).Options({ "Straight", "Projectile" })
                .Field("MaxDistance", "Max Raycast Distance", &C::MaxDistance).Range(0.1f, 1000.0f, 0.1f)
                .Field("Velocity", "Projectile Velocity", &C::Velocity).Range(0.5f, 50.0f, 0.1f)
                    .VisibleIf([](const C& c) { return c.LineType == XRLineType::Projectile; })
                .Field("LineWidth", "Line Width", &C::LineWidth).Range(0.001f, 0.5f, 0.001f)
                .Field("ValidColor", "Valid Color", &C::ValidColor, FieldKind::Color4)
                .Field("InvalidColor", "Invalid Color", &C::InvalidColor, FieldKind::Color4);
        }
        {
            using C = XRGrabInteractableComponent;
            ComponentDef<C>("XR Grab Interactable", "XRGrabInteractableComponent", "XR", ICON_FA_HAND_HOLDING).Color(kColor)
                .Describe("Can be grabbed with a ray and thrown. Scripts get OnHoverEntered / OnHoverExited / OnSelectEntered / OnSelectExited / OnActivated.")
                .Field("MovementType", "Movement Type", &C::MovementType).Options({ "Velocity Tracking", "Kinematic", "Instantaneous" })
                .Field("TrackRotation", "Track Rotation", &C::TrackRotation)
                .Field("ThrowOnDetach", "Throw On Detach", &C::ThrowOnDetach)
                .Field("ThrowVelocityScale", "Throw Velocity Scale", &C::ThrowVelocityScale).Range(0.0f, 10.0f, 0.05f)
                .Field("IsSelected", "Is Selected", &C::IsSelected).ReadOnly().Header("Runtime")
                .Field("IsHovered", "Is Hovered", &C::IsHovered).ReadOnly();
        }
        {
            using C = TeleportationAreaComponent;
            ComponentDef<C>("Teleportation Area", "TeleportationAreaComponent", "XR", ICON_FA_SHOE_PRINTS).Color(kColor)
                .Describe("Put it on a floor with a collider: a projectile ray can teleport the XR Origin anywhere on it.")
                .Field("MatchOrientation", "Match Orientation", &C::MatchOrientation);
        }
        {
            using C = TeleportationAnchorComponent;
            ComponentDef<C>("Teleportation Anchor", "TeleportationAnchorComponent", "XR", ICON_FA_LOCATION_DOT).Color(kColor)
                .Describe("A fixed teleport spot (needs a collider): the XR Origin lands on this object's position and can face its direction.")
                .Field("MatchOrientation", "Match Orientation", &C::MatchOrientation);
        }
        {
            using C = SnapTurnProviderComponent;
            ComponentDef<C>("Snap Turn Provider", "SnapTurnProviderComponent", "XR", ICON_FA_ROTATE).Color(kColor)
                .Describe("Put it on the XR Origin: turns the player in steps (Q / E in the simulator).")
                .Field("TurnAmount", "Turn Amount", &C::TurnAmount).Range(1.0f, 180.0f, 1.0f);
        }
        {
            using C = ContinuousMoveProviderComponent;
            ComponentDef<C>("Continuous Move Provider", "ContinuousMoveProviderComponent", "XR", ICON_FA_PERSON_WALKING).Color(kColor)
                .Describe("Put it on the XR Origin: walks where the head looks (WASD in the simulator).")
                .Field("MoveSpeed", "Move Speed", &C::MoveSpeed).Range(0.0f, 20.0f, 0.05f)
                .Field("EnableStrafe", "Enable Strafe", &C::EnableStrafe);
        }
    }
}
