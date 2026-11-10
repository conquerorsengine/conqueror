#include "UIExtras.h"

#include "Scene/ComponentRegistry.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "UI/UIComponents.h"
#include "UI/UISystem.h"
#include "Core/Input/Input.h"
#include "Core/PhysicsSystem/Physics2D/PhysicsWorld2D.h"
#include "Core/PhysicsSystem/Physics3D/PhysicsWorld3D.h"

#include <glm/gtc/matrix_transform.hpp>

#include <IconsFontAwesome6.h>

namespace Conqueror
{
    template<> void Scene::OnComponentAdded<ToggleGroupComponent>(Entity, ToggleGroupComponent&) {}
    template<> void Scene::OnComponentAdded<PhysicsRaycasterComponent>(Entity, PhysicsRaycasterComponent&) {}
    template<> void Scene::OnComponentAdded<Physics2DRaycasterComponent>(Entity, Physics2DRaycasterComponent&) {}
    template<> void Scene::OnComponentAdded<OnScreenStickComponent>(Entity, OnScreenStickComponent&) {}
    template<> void Scene::OnComponentAdded<UISelectableComponent>(Entity, UISelectableComponent&) {}
    template<> void Scene::OnComponentAdded<OnScreenButtonComponent>(Entity, OnScreenButtonComponent&) {}

    namespace UIExtras
    {
        void RegisterComponents()
        {
            using G = ToggleGroupComponent;
            ComponentDef<G>("Toggle Group", "ToggleGroupComponent", "UI", ICON_FA_LIST_CHECK)
                .Describe("Makes the Toggles under this object (and on it) mutually exclusive: turning one on turns the others off. "
                          "With Allow Switch Off unchecked, clicking the toggle that is on keeps it on, so one option always stays selected. "
                          "A Toggle whose Group number is not 0 uses that numbered group instead.")
                .Field("AllowSwitchOff", "Allow Switch Off", &G::AllowSwitchOff);
            using R = PhysicsRaycasterComponent;
            ComponentDef<R>("Physics Raycaster", "PhysicsRaycasterComponent", "UI", ICON_FA_CROSSHAIRS)
                .Describe("Put it on the camera: when the pointer is not over UI, a ray from this camera finds the 3D collider under the pointer and "
                          "its object (or the nearest parent with an Event Trigger) gets Pointer Enter, Exit, Down, Up, Click, Drag and Scroll.")
                .Field("EventMask", "Event Mask", &R::EventMask)
                .Field("MaxRayDistance", "Max Ray Distance", &R::MaxRayDistance).Tooltip("0 uses the camera's far clip plane");
            using R2 = Physics2DRaycasterComponent;
            ComponentDef<R2>("Physics 2D Raycaster", "Physics2DRaycasterComponent", "UI", ICON_FA_CROSSHAIRS)
                .Describe("Put it on the camera: the 2D collider under the pointer gets the Event Trigger pointer events (when the pointer is not over UI).")
                .Field("EventMask", "Event Mask", &R2::EventMask);
            using SC = UISelectableComponent;
            ComponentDef<SC>("Selectable", "UISelectableComponent", "UI", ICON_FA_HAND_POINTER)
                .Describe("Makes this UI element interactive like a Button without a click action: hover, press and disabled colors (or scale), "
                          "and keyboard / gamepad navigation. Add an Event Trigger to react to pointer and navigation events.")
                .FieldVia("Interactable", "Interactable", [](SC& c) -> bool& { return c.Selectable.Interactable; })
                .FieldVia("Transition", "Transition", [](SC& c) -> UITransition& { return c.Selectable.Transition; }).Options({ "None", "Color Tint", "Scale" })
                .FieldVia("NormalColor", "Normal Color", [](SC& c) -> glm::vec4& { return c.Selectable.NormalColor; }, FieldKind::Color4)
                .FieldVia("HighlightedColor", "Highlighted Color", [](SC& c) -> glm::vec4& { return c.Selectable.HighlightedColor; }, FieldKind::Color4)
                .FieldVia("PressedColor", "Pressed Color", [](SC& c) -> glm::vec4& { return c.Selectable.PressedColor; }, FieldKind::Color4)
                .FieldVia("DisabledColor", "Disabled Color", [](SC& c) -> glm::vec4& { return c.Selectable.DisabledColor; }, FieldKind::Color4)
                .FieldVia("FadeDuration", "Fade Duration", [](SC& c) -> float& { return c.Selectable.FadeDuration; }).Range(0.0f, 2.0f, 0.01f);
            using OS = OnScreenStickComponent;
            ComponentDef<OS>("On-Screen Stick", "OnScreenStickComponent", "Input", ICON_FA_GAMEPAD)
                .Describe("Put it on a UI Image: press and drag it to move it within Movement Range and drive the virtual gamepad's left or right stick. "
                          "Player Input actions bound to the gamepad stick read it like a real controller.")
                .Field("MovementRange", "Movement Range", &OS::MovementRange).Range(1.0f, 1000.0f, 1.0f)
                .Field("Control", "Control", &OS::Control).Options({ "Left Stick", "Right Stick" });
            using OB = OnScreenButtonComponent;
            ComponentDef<OB>("On-Screen Button", "OnScreenButtonComponent", "Input", ICON_FA_CIRCLE_DOT)
                .Describe("Put it on a UI element: while it is pressed, the chosen virtual gamepad button is held down.")
                .Field("Button", "Button", &OB::Button).Options({ "A (South)", "B (East)", "X (West)", "Y (North)", "Left Shoulder", "Right Shoulder",
                                                                   "Select", "Start", "Guide", "Left Stick Press", "Right Stick Press",
                                                                   "D-Pad Up", "D-Pad Right", "D-Pad Down", "D-Pad Left" });
        }

        namespace
        {
            bool IsOver(Scene& scene, entt::entity hovered, entt::entity element)
            {
                for (Entity e(hovered, &scene); e; e = e.GetParent())
                    if ((entt::entity)e == element)
                        return true;
                return false;
            }
        }

        bool UpdateOnScreenControls(Scene& scene, const UIInputState& input, entt::entity hovered)
        {
            auto& reg = scene.m_Registry;
            auto sticks = reg.view<OnScreenStickComponent, RectTransformComponent>();
            auto buttons = reg.view<OnScreenButtonComponent>();
            if (sticks.begin() == sticks.end() && buttons.begin() == buttons.end())
                return false;
            bool anyHeld = false;
            if (!Input::IsVirtualGamepad())
                Input::SetVirtualGamepad(true, "On-Screen Controls");
            const bool hasHover = hovered != entt::null && reg.valid(hovered);
            for (auto e : sticks)
            {
                auto& st = sticks.get<OnScreenStickComponent>(e);
                auto& rt = sticks.get<RectTransformComponent>(e);
                const int ax = st.Control == OnScreenStickControl::RightStick ? 2 : 0, ay = ax + 1;
                if (!st.Held && input.MousePressed && input.Valid && hasHover && IsOver(scene, hovered, e))
                {
                    st.Held = true;
                    st.PressPos = input.MousePosition;
                    st.StartPos = rt.AnchoredPosition;
                }
                if (st.Held && !input.MouseDown)
                {
                    st.Held = false;
                    rt.AnchoredPosition = st.StartPos;
                    Input::SetVirtualGamepadAxis(ax, 0.0f);
                    Input::SetVirtualGamepadAxis(ay, 0.0f);
                }
                if (st.Held)
                {
                    // Ekran pikseli -> canvas birimi: öğenin yerel x biriminin canvas'taki uzunluğu
                    const float scale = std::max(glm::length(glm::vec3(rt.CanvasMatrix[0])), 1e-4f);
                    glm::vec2 delta = (input.MousePosition - st.PressPos) / scale;
                    const float range = std::max(st.MovementRange, 1e-3f), len = glm::length(delta);
                    if (len > range)
                        delta *= range / len;
                    rt.AnchoredPosition = st.StartPos + delta;
                    const glm::vec2 v = delta / range;
                    Input::SetVirtualGamepadAxis(ax, v.x);
                    Input::SetVirtualGamepadAxis(ay, -v.y);   // gamepad Y aşağı artı (GLFW)
                    anyHeld = true;
                }
            }
            for (auto e : buttons)
            {
                auto& b = buttons.get<OnScreenButtonComponent>(e);
                const bool was = b.Held;
                if (!b.Held && input.MousePressed && input.Valid && hasHover && IsOver(scene, hovered, e))
                    b.Held = true;
                if (b.Held && !input.MouseDown)
                    b.Held = false;
                const int button = (int)b.Button;
                if (b.Held != was && button >= 0 && button <= 14)
                    Input::SetVirtualGamepadButton(button, b.Held);
                anyHeld |= b.Held;
            }
            return anyHeld;
        }

        entt::entity WorldPointerTarget(Scene& scene, const glm::vec2& screenSize, const glm::vec2& mouse)
        {
            Entity cam = scene.GetPrimaryCameraEntity();
            if (!cam || !cam.HasComponent<CameraComponent>() || screenSize.x <= 0.0f || screenSize.y <= 0.0f)
                return entt::null;
            const bool has3D = cam.HasComponent<PhysicsRaycasterComponent>(), has2D = cam.HasComponent<Physics2DRaycasterComponent>();
            if (!has3D && !has2D)
                return entt::null;
            const auto& cc = cam.GetComponent<CameraComponent>();
            const glm::mat4 world = cam.GetComponent<TransformComponent>().WorldTransform;
            const glm::mat4 inv = glm::inverse(cc.Camera.GetProjection() * glm::inverse(world));
            const glm::vec2 ndc = mouse / screenSize * 2.0f - 1.0f;
            glm::vec4 n = inv * glm::vec4(ndc, -1.0f, 1.0f), f = inv * glm::vec4(ndc, 1.0f, 1.0f);
            n /= n.w;
            f /= f.w;
            const glm::vec3 origin = glm::vec3(n);
            const glm::vec3 dir = glm::normalize(glm::vec3(f) - origin);
            if (has3D)
            {
                if (PhysicsWorld3D* w3 = scene.GetPhysicsWorld3D())
                {
                    const auto& r = cam.GetComponent<PhysicsRaycasterComponent>();
                    const float maxDist = r.MaxRayDistance > 0.0f ? r.MaxRayDistance : glm::length(glm::vec3(f) - origin);
                    Physics3D::RaycastHit hit;
                    if (w3->Raycast(origin, dir, maxDist, hit, r.EventMask) && hit.Entity != Physics3D::NoEntity)
                        return (entt::entity)hit.Entity;
                }
            }
            if (has2D)
            {
                if (PhysicsWorld2D* w2 = scene.GetPhysicsWorld2D())
                {
                    // Işının z = 0 düzlemini kestiği nokta (ortografik 2B kamerada farenin dünya noktası)
                    glm::vec2 p(origin);
                    if (std::abs(dir.z) > 1e-6f)
                    {
                        const float t = -origin.z / dir.z;
                        if (t > 0.0f)
                            p = glm::vec2(origin + dir * t);
                    }
                    std::vector<uint32_t> hits;
                    w2->OverlapCircle(p, 0.001f, hits, cam.GetComponent<Physics2DRaycasterComponent>().EventMask);
                    if (!hits.empty())
                        return (entt::entity)hits.front();
                }
            }
            return entt::null;
        }

        entt::entity FindToggleGroup(Scene& scene, entt::entity toggle)
        {
            Entity e(toggle, &scene);
            for (int depth = 0; e && depth < 64; ++depth)
            {
                if (e.HasComponent<ToggleGroupComponent>())
                    return (entt::entity)e;
                e = e.GetParent();
            }
            return entt::null;
        }

        bool ClickToggle(Scene& scene, entt::entity toggle)
        {
            auto& reg = scene.m_Registry;
            if (!reg.valid(toggle) || !reg.all_of<UIToggleComponent>(toggle))
                return false;
            auto& tg = reg.get<UIToggleComponent>(toggle);
            // Numaralı grup (eski davranış): açık olan kapatılamaz, açılınca aynı numaralı diğerleri kapanır
            if (tg.Group != 0)
            {
                if (tg.IsOn)
                    return false;
                tg.IsOn = true;
                tg.ChangedThisFrame = true;
                for (auto o : reg.view<UIToggleComponent>())
                {
                    auto& other = reg.get<UIToggleComponent>(o);
                    if (o != toggle && other.Group == tg.Group && other.IsOn)
                    {
                        other.IsOn = false;
                        other.ChangedThisFrame = true;
                    }
                }
                return true;
            }
            const entt::entity group = FindToggleGroup(scene, toggle);
            if (group == entt::null)
            {
                tg.IsOn = !tg.IsOn;
                tg.ChangedThisFrame = true;
                return true;
            }
            const bool allowOff = reg.get<ToggleGroupComponent>(group).AllowSwitchOff;
            if (tg.IsOn && !allowOff)
                return false;
            tg.IsOn = !tg.IsOn;
            tg.ChangedThisFrame = true;
            if (tg.IsOn)
            {
                for (auto o : reg.view<UIToggleComponent>())
                {
                    if (o == toggle)
                        continue;
                    auto& other = reg.get<UIToggleComponent>(o);
                    if (other.Group == 0 && other.IsOn && FindToggleGroup(scene, o) == group)
                    {
                        other.IsOn = false;
                        other.ChangedThisFrame = true;
                    }
                }
            }
            return true;
        }
    }
}
