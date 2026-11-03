#include "PlayerInput.h"
#include "Core/Input/InputActionAssets.h"
#include "Core/Input/InputActionRuntime.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Core/Input/Input.h"
#include "Core/Input/GameInput.h"
#include "Core/Serialization/PrefabSerializer.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Scripting/ConquerorScript/CQSValue.h"

#include <IconsFontAwesome6.h>

#include <algorithm>
#include <cmath>

namespace Conqueror
{
    namespace
    {
        // GLFW standart gamepad: 0 A, 1 B, 2 X, 3 Y, 4 LB, 5 RB, 6 Back, 7 Start, ...; eksenler 0 LX, 1 LY, 2 RX, 3 RY
        float Deadzone(float v, float dz) { return std::fabs(v) < dz ? 0.0f : v; }

        float EvaluateAction(const InputAction& a, const PlayerInputComponent& p)
        {
            if (a.PlayerIndex != 0 && a.PlayerIndex != p.PlayerIndex)
                return 0.0f;
            float value = 0.0f;
            if (p.UseKeyboardMouse)
            {
                if (a.IsChord)
                {
                    bool all = !a.Keys.empty();
                    for (KeyCode k : a.Keys) all &= GameInput::IsKeyHeld(k);
                    if (all) value = 1.0f;
                }
                else
                {
                    for (KeyCode k : a.Keys)
                        if (GameInput::IsKeyHeld(k)) value = 1.0f;
                }
                for (MouseCode b : a.MouseButtons)
                    if (GameInput::IsMouseHeld(b)) value = 1.0f;
            }
            if (p.PlayerIndex >= 0 && Input::IsGamepadConnected(p.PlayerIndex))
            {
                for (int b : a.GamepadButtons)
                    if (Input::IsGamepadButtonPressed(b, p.PlayerIndex)) value = 1.0f;
                for (int ax : a.GamepadAxes)
                {
                    float v = Deadzone(Input::GetGamepadAxis(ax, p.PlayerIndex), a.DeadZone);
                    if (std::fabs(v) > std::fabs(value)) value = v;
                }
            }
            return value * a.Scale;
        }

        void Send(Scene& scene, entt::entity e, const std::string& method, const CQS::Value* args, int argc)
        {
            scene.InvokeScriptMethod((uint32_t)e, method.c_str(), args, argc);
        }

        // Broadcast Messages: nesne ve bütün çocukları
        void Broadcast(Scene& scene, entt::entity e, const std::string& method, const CQS::Value* args, int argc)
        {
            Send(scene, e, method, args, argc);
            for (Entity child : scene.GetEntityChildren(Entity{ e, &scene }))
                Broadcast(scene, (entt::entity)child, method, args, argc);
        }

        // Actions varlığıyla oyuncu: eylemleri değerlendir, olayları betiklere gönder. false: varlık / harita yok
        bool UpdateAssetPlayer(Scene& scene, entt::entity e, PlayerInputComponent& p, bool& hasMove, bool& hasLook)
        {
            const InputActionAssetData* asset = InputActionAssets::Get(p.Actions);
            const InputActionMapDef* map = asset ? asset->FindMapOrFirst(p.ActionMap) : nullptr;
            if (!map)
                return false;
            const uint64_t stamp = InputActionAssets::Stamp(p.Actions);
            auto& st = p.AssetState;
            if (st.Asset != p.Actions || st.Map != map->Name || st.Scheme != p.DefaultScheme || st.Stamp != stamp)
            {
                st.Reset();
                st.Asset = p.Actions;
                st.Map = map->Name;
                st.Scheme = p.DefaultScheme;
                st.Stamp = stamp;
            }
            std::vector<InputActionEvent> events;
            InputActionRuntime::Evaluate(*map, p.DefaultScheme, InputDeviceFilter{ p.UseKeyboardMouse, p.PlayerIndex }, InputActionRuntime::Now(), st, &events);
            for (const auto& [name, a] : st.Actions)
            {
                p.Values[name] = a.Vector ? a.Magnitude : a.Value.x;
                hasMove |= name == "Move";
                hasLook |= name == "Look";
            }
            if (hasMove)
                p.Move = st.Actions["Move"].Value;
            if (hasLook)
                p.Look = st.Actions["Look"].Value;
            if (p.Behavior == PlayerInputBehavior::None)
                return true;
            for (const auto& ev : events)
            {
                // Unity Send / Broadcast Messages: gerçekleşince; Value eylemleri bırakılınca da (sıfır değerle)
                const bool send = ev.Phase == InputActionPhase::Performed || (ev.Phase == InputActionPhase::Canceled && ev.Kind == InputActionKind::Value);
                if (!send)
                    continue;
                CQS::Value args[2] = { CQS::Value::MakeFloat((double)ev.Value.x), CQS::Value::MakeFloat((double)ev.Value.y) };
                if (p.Behavior == PlayerInputBehavior::BroadcastMessages)
                    Broadcast(scene, e, "On" + ev.Action, args, ev.Vector ? 2 : 1);
                else
                    Send(scene, e, "On" + ev.Action, args, ev.Vector ? 2 : 1);
            }
            return true;
        }
    }

    namespace PlayerInputs
    {
        const InputActionState* FindState(const PlayerInputComponent& c, const std::string& action)
        {
            if (c.Actions.empty())
                return nullptr;
            auto it = c.AssetState.Actions.find(action);
            return it != c.AssetState.Actions.end() ? &it->second : nullptr;
        }

        glm::vec2 GetVector(const PlayerInputComponent& c, const std::string& action)
        {
            if (const auto* st = FindState(c, action))
                return st->Value;
            if (action == "Move") return c.Move;
            if (action == "Look") return c.Look;
            auto it = c.Values.find(action);
            return glm::vec2(it != c.Values.end() ? it->second : 0.0f, 0.0f);
        }

        void SwitchMap(PlayerInputComponent& c, const std::string& map)
        {
            c.ActionMap = map;
            c.AssetState.Reset();
        }

        void SwitchScheme(PlayerInputComponent& c, const std::string& scheme)
        {
            c.DefaultScheme = scheme;
            c.AssetState.Reset();
        }

        float GetValue(const PlayerInputComponent& c, const std::string& action)
        {
            if (const auto* st = FindState(c, action))
                return st->Vector ? st->Magnitude : st->Value.x;
            if (action == "Move") return glm::length(c.Move);
            auto it = c.Values.find(action);
            return it != c.Values.end() ? it->second : 0.0f;
        }

        bool WasPressed(const PlayerInputComponent& c, const std::string& action)
        {
            if (const auto* st = FindState(c, action))
                return st->PressedThisFrame;
            auto it = c.Values.find(action);
            auto pt = c.Previous.find(action);
            float now = it != c.Values.end() ? it->second : 0.0f, before = pt != c.Previous.end() ? pt->second : 0.0f;
            return now != 0.0f && before == 0.0f;
        }

        bool WasReleased(const PlayerInputComponent& c, const std::string& action)
        {
            if (const auto* st = FindState(c, action))
                return st->ReleasedThisFrame;
            auto it = c.Values.find(action);
            auto pt = c.Previous.find(action);
            float now = it != c.Values.end() ? it->second : 0.0f, before = pt != c.Previous.end() ? pt->second : 0.0f;
            return now == 0.0f && before != 0.0f;
        }

        uint32_t JoinPlayer(Scene& scene, uint32_t manager, int device)
        {
            entt::entity m = (entt::entity)manager;
            if (!scene.m_Registry.valid(m) || !scene.m_Registry.all_of<PlayerInputManagerComponent>(m))
                return 0;
            auto& mgr = scene.m_Registry.get<PlayerInputManagerComponent>(m);
            if ((int)mgr.JoinedDevices.size() >= std::max(mgr.MaxPlayers, 1) || mgr.PlayerPrefab.empty())
                return 0;
            if (std::find(mgr.JoinedDevices.begin(), mgr.JoinedDevices.end(), device) != mgr.JoinedDevices.end())
                return 0;
            Entity player = PrefabSerializer::Instantiate(scene, SceneSerializer::ResolveAssetPath(mgr.PlayerPrefab));
            if (!player)
                return 0;
            auto& pi = player.HasComponent<PlayerInputComponent>() ? player.GetComponent<PlayerInputComponent>() : player.AddComponent<PlayerInputComponent>();
            pi.PlayerIndex = device;
            pi.UseKeyboardMouse = device < 0;
            mgr.JoinedDevices.push_back(device);
            mgr.PlayerCount = (int)mgr.JoinedDevices.size();
            CQS::Value arg = CQS::Value::MakeInt((int64_t)mgr.PlayerCount - 1);
            Send(scene, m, "OnPlayerJoined", &arg, 1);
            return (uint32_t)player;
        }

        void Update(Scene& scene)
        {
            // ---- Project-wide Actions (Project Settings > Input): betiklerin genel eylem sorguları için
            InputActionRuntime::UpdateProjectWide();

            // ---- Yerel çok oyunculu katılım
            auto managers = scene.m_Registry.view<PlayerInputManagerComponent>();
            for (auto m : managers)
            {
                if (!scene.IsEntityActive(m))
                    continue;
                auto& mgr = managers.get<PlayerInputManagerComponent>(m);
                if (mgr.JoinBehavior != PlayerJoinBehavior::JoinOnButtonPress)
                    continue;
                for (int pad = 0; pad < 16; ++pad)
                {
                    if (!Input::IsGamepadConnected(pad))
                        continue;
                    if (std::find(mgr.JoinedDevices.begin(), mgr.JoinedDevices.end(), pad) != mgr.JoinedDevices.end())
                        continue;
                    bool pressed = false;
                    for (int b = 0; b < 15 && !pressed; ++b)
                        pressed = Input::IsGamepadButtonDown(b, pad);
                    if (pressed)
                        JoinPlayer(scene, (uint32_t)m, pad);
                }
                if (mgr.AllowKeyboardPlayer && std::find(mgr.JoinedDevices.begin(), mgr.JoinedDevices.end(), -1) == mgr.JoinedDevices.end() &&
                    (GameInput::IsKeyDown(Key::Enter) || GameInput::IsKeyDown(Key::Space)))
                    JoinPlayer(scene, (uint32_t)m, -1);
            }

            // ---- Oyuncular: eylemleri değerlendir, değişenleri betiklere gönder
            auto players = scene.m_Registry.view<PlayerInputComponent>();
            for (auto e : players)
            {
                if (!scene.IsEntityActive(e))
                    continue;
                auto& p = players.get<PlayerInputComponent>(e);
                p.Previous = p.Values;
                bool hasMove = false, hasLook = false;
                // Actions varlığı atanmışsa eylemler, aşamalar ve olaylar oradan (Unity Player Input > Actions)
                const bool assetPlayer = !p.Actions.empty() && UpdateAssetPlayer(scene, e, p, hasMove, hasLook);
                const InputContext* ctx = assetPlayer ? nullptr : Input::FindContext(p.ActionMap);
                if (ctx && ctx->Enabled)
                    for (const auto& [name, action] : ctx->Actions)
                    {
                        hasMove |= name == "Move";
                        hasLook |= name == "Look";
                        p.Values[name] = EvaluateAction(action, p);
                    }

                // Hareket / bakış vektörleri (haritada yoksa üretilir)
                glm::vec2 move(0.0f), look(0.0f);
                if (p.GenerateMoveLook)
                {
                    if (p.UseKeyboardMouse)
                    {
                        move.x += (GameInput::IsKeyHeld(Key::D) || GameInput::IsKeyHeld(Key::Right) ? 1.0f : 0.0f) - (GameInput::IsKeyHeld(Key::A) || GameInput::IsKeyHeld(Key::Left) ? 1.0f : 0.0f);
                        move.y += (GameInput::IsKeyHeld(Key::W) || GameInput::IsKeyHeld(Key::Up) ? 1.0f : 0.0f) - (GameInput::IsKeyHeld(Key::S) || GameInput::IsKeyHeld(Key::Down) ? 1.0f : 0.0f);
                        look += GameInput::GetMouseDelta() * glm::vec2(1.0f, -1.0f);
                    }
                    if (p.PlayerIndex >= 0 && Input::IsGamepadConnected(p.PlayerIndex))
                    {
                        glm::vec2 ls = Input::GetGamepadLeftStick(p.PlayerIndex), rs = Input::GetGamepadRightStick(p.PlayerIndex);
                        move += glm::vec2(Deadzone(ls.x, 0.2f), -Deadzone(ls.y, 0.2f));
                        look += glm::vec2(Deadzone(rs.x, 0.2f), -Deadzone(rs.y, 0.2f)) * 10.0f;
                    }
                    if (glm::length(move) > 1.0f)
                        move = glm::normalize(move);
                }
                p.PrevMove = p.Move;
                if (!hasMove)
                    p.Move = move;
                if (!hasLook)
                    p.Look = look;

                if (p.Behavior == PlayerInputBehavior::None)
                    continue;
                auto deliver = [&](const std::string& method, const CQS::Value* args, int argc) {
                    if (p.Behavior == PlayerInputBehavior::BroadcastMessages)
                        Broadcast(scene, e, method, args, argc);
                    else
                        Send(scene, e, method, args, argc);
                };
                for (const auto& [name, value] : (assetPlayer ? std::unordered_map<std::string, float>{} : p.Values))
                {
                    auto prev = p.Previous.find(name);
                    const float before = prev != p.Previous.end() ? prev->second : 0.0f;
                    if (value != before)
                    {
                        CQS::Value arg = CQS::Value::MakeFloat((double)value);
                        deliver("On" + name, &arg, 1);
                    }
                }
                if (p.GenerateMoveLook && !hasMove && p.Move != p.PrevMove)
                {
                    CQS::Value args[2] = { CQS::Value::MakeFloat((double)p.Move.x), CQS::Value::MakeFloat((double)p.Move.y) };
                    deliver("OnMove", args, 2);
                }
                if (p.GenerateMoveLook && !hasLook && p.Look != glm::vec2(0.0f))
                {
                    CQS::Value args[2] = { CQS::Value::MakeFloat((double)p.Look.x), CQS::Value::MakeFloat((double)p.Look.y) };
                    deliver("OnLook", args, 2);
                }
            }
        }
    }

    void RegisterPlayerInputComponents()
    {
        using P = PlayerInputComponent;
        ComponentDef<P>("Player Input", "PlayerInputComponent", "Input", ICON_FA_GAMEPAD)
            .Describe("Binds an action map to this player: from an Input Actions asset (Actions slot) or, when the slot is empty, from the project's input settings. Sends On<Action>(value), OnMove(x, y) and OnLook(dx, dy) to this object's scripts. Each player reads its own gamepad.")
            .Field("Actions", "Actions", &P::Actions, FieldKind::Asset).Filter(InputActionAssets::Extension).Tooltip("Input Actions asset (.cqinputactions). Empty = Project Settings > Input.")
            .Field("ActionMap", "Default Map", &P::ActionMap).Tooltip("Action map to use (e.g. Player, UI, Vehicle). With an Actions asset, an unknown name uses the asset's first map.")
            .Field("DefaultScheme", "Default Scheme", &P::DefaultScheme).Tooltip("Control scheme of the Actions asset (e.g. Keyboard&Mouse, Gamepad). Empty = bindings of every scheme.")
            .Field("PlayerIndex", "Player Index (Gamepad)", &P::PlayerIndex).Range(-1.0f, 15.0f, 1.0f).Tooltip("-1 = no gamepad.")
            .Field("UseKeyboardMouse", "Keyboard & Mouse", &P::UseKeyboardMouse)
            .Field("Behavior", "Behavior", &P::Behavior).Options({ "Send Messages", "None (poll from scripts)", "Broadcast Messages" })
            .Tooltip("Send Messages: On<Action> on this object's scripts. Broadcast Messages: also on its children.")
            .Field("GenerateMoveLook", "Generate Move / Look", &P::GenerateMoveLook).Tooltip("WASD / arrows / left stick -> OnMove, mouse / right stick -> OnLook.");

        using M = PlayerInputManagerComponent;
        ComponentDef<M>("Player Input Manager", "PlayerInputManagerComponent", "Input", ICON_FA_USERS)
            .Describe("Local multiplayer: spawns the player prefab when a new gamepad (or the keyboard) presses a button and assigns that device to it.")
            .Field("PlayerPrefab", "Player Prefab", &M::PlayerPrefab, FieldKind::Asset).Filter(".cqprefab")
            .Field("MaxPlayers", "Max Players", &M::MaxPlayers).Range(1.0f, 16.0f, 1.0f)
            .Field("JoinBehavior", "Join Behavior", &M::JoinBehavior).Options({ "Join On Button Press", "Manual (scripts)" })
            .Field("AllowKeyboardPlayer", "Keyboard Player", &M::AllowKeyboardPlayer)
            .Field("PlayerCount", "Players", &M::PlayerCount).ReadOnly();
    }
}
