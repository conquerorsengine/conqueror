#include "PlayerInput.h"
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
    }

    namespace PlayerInputs
    {
        float GetValue(const PlayerInputComponent& c, const std::string& action)
        {
            if (action == "Move") return glm::length(c.Move);
            auto it = c.Values.find(action);
            return it != c.Values.end() ? it->second : 0.0f;
        }

        bool WasPressed(const PlayerInputComponent& c, const std::string& action)
        {
            auto it = c.Values.find(action);
            auto pt = c.Previous.find(action);
            float now = it != c.Values.end() ? it->second : 0.0f, before = pt != c.Previous.end() ? pt->second : 0.0f;
            return now != 0.0f && before == 0.0f;
        }

        bool WasReleased(const PlayerInputComponent& c, const std::string& action)
        {
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
                const InputContext* ctx = Input::FindContext(p.ActionMap);
                bool hasMove = false, hasLook = false;
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
                p.Move = move;
                p.Look = look;

                if (p.Behavior != PlayerInputBehavior::SendMessages)
                    continue;
                for (const auto& [name, value] : p.Values)
                {
                    auto prev = p.Previous.find(name);
                    const float before = prev != p.Previous.end() ? prev->second : 0.0f;
                    if (value != before)
                    {
                        CQS::Value arg = CQS::Value::MakeFloat((double)value);
                        Send(scene, e, "On" + name, &arg, 1);
                    }
                }
                if (p.GenerateMoveLook && !hasMove && p.Move != p.PrevMove)
                {
                    CQS::Value args[2] = { CQS::Value::MakeFloat((double)p.Move.x), CQS::Value::MakeFloat((double)p.Move.y) };
                    Send(scene, e, "OnMove", args, 2);
                }
                if (p.GenerateMoveLook && !hasLook && p.Look != glm::vec2(0.0f))
                {
                    CQS::Value args[2] = { CQS::Value::MakeFloat((double)p.Look.x), CQS::Value::MakeFloat((double)p.Look.y) };
                    Send(scene, e, "OnLook", args, 2);
                }
            }
        }
    }

    void RegisterPlayerInputComponents()
    {
        using P = PlayerInputComponent;
        ComponentDef<P>("Player Input", "PlayerInputComponent", "Input", ICON_FA_GAMEPAD)
            .Describe("Binds an action map of the project's input settings to this player. Sends On<Action>(value), OnMove(x, y) and OnLook(dx, dy) to this object's scripts. Each player reads its own gamepad.")
            .Field("ActionMap", "Action Map", &P::ActionMap).Tooltip("Context name in the input settings (e.g. Gameplay, Vehicle).")
            .Field("PlayerIndex", "Player Index (Gamepad)", &P::PlayerIndex).Range(-1.0f, 15.0f, 1.0f).Tooltip("-1 = no gamepad.")
            .Field("UseKeyboardMouse", "Keyboard & Mouse", &P::UseKeyboardMouse)
            .Field("Behavior", "Behavior", &P::Behavior).Options({ "Send Messages", "None (poll from scripts)" })
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
