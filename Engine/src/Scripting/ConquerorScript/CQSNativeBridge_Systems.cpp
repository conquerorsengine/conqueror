#include "Core/Audio/AudioRandomContainer.h"
#include "Scene/MeshRendererMaterial.h"
#include "Renderer/Utilities/Renderer3D/Material.h"
#include "CQSNativeBridge.h"
#include <algorithm>
#include <cctype>
#include "Core/Input/GameInput.h"
#include "CQSVM.h"
#include "CQSEngine.h"
#include "Scripting/ScriptContext.h"
#include "Core/Logging/Log.h"
#include "Core/Input/Input.h"
#include "Core/Input/InputActionRuntime.h"
#include "Core/Time/TimeManager.h"
#include "Core/Audio/AudioEngine.h"
#include "Core/Audio/AudioSource.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include "AISystem/Pathfinding/AStar.h"
#include "AISystem/Navigation/NavMesh.h"
#include "AISystem/Navigation/NavAgents.h"
#include "Networking/NetworkManager.h"
#include "Networking/RPC/RPCSystem.h"
#include "Core/Debug/DebugDraw.h"
#include "Core/Debug/DebugSolidDraw.h"
#include "Core/Debug/DebugPalette.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace Conqueror::CQS
{
    static KeyCode StringToKeyCode(const std::string& keyName)
    {
        static const std::unordered_map<std::string, KeyCode> keyMap = {
            // Letters (A-Z)
            {"A", Key::A}, {"B", Key::B}, {"C", Key::C}, {"D", Key::D}, {"E", Key::E}, {"F", Key::F},
            {"G", Key::G}, {"H", Key::H}, {"I", Key::I}, {"J", Key::J}, {"K", Key::K}, {"L", Key::L},
            {"M", Key::M}, {"N", Key::N}, {"O", Key::O}, {"P", Key::P}, {"Q", Key::Q}, {"R", Key::R},
            {"S", Key::S}, {"T", Key::T}, {"U", Key::U}, {"V", Key::V}, {"W", Key::W}, {"X", Key::X},
            {"Y", Key::Y}, {"Z", Key::Z},
            
            // Numbers (0-9)
            {"0", Key::D0}, {"1", Key::D1}, {"2", Key::D2}, {"3", Key::D3}, {"4", Key::D4},
            {"5", Key::D5}, {"6", Key::D6}, {"7", Key::D7}, {"8", Key::D8}, {"9", Key::D9},
            
            // Modifiers
            {"LeftControl", Key::LeftControl}, {"RightControl", Key::RightControl}, {"Ctrl", Key::LeftControl},
            {"LeftShift", Key::LeftShift}, {"RightShift", Key::RightShift}, {"Shift", Key::LeftShift},
            {"LeftAlt", Key::LeftAlt}, {"RightAlt", Key::RightAlt}, {"Alt", Key::LeftAlt},
            
            // Specials
            {"Space", Key::Space}, {"Enter", Key::Enter}, {"Escape", Key::Escape},
            {"Backspace", Key::Backspace}, {"Tab", Key::Tab},
            {"ArrowUp", Key::Up}, {"ArrowDown", Key::Down}, {"ArrowLeft", Key::Left}, {"ArrowRight", Key::Right},
            // Yaygın diğer adlar (Unity / Godot alışkanlıkları)
            {"Up", Key::Up}, {"Down", Key::Down}, {"Left", Key::Left}, {"Right", Key::Right},
            {"UpArrow", Key::Up}, {"DownArrow", Key::Down}, {"LeftArrow", Key::Left}, {"RightArrow", Key::Right},
            {"Return", Key::Enter}, {"Esc", Key::Escape}, {"Delete", Key::Delete}, {"Insert", Key::Insert},
            {"Home", Key::Home}, {"End", Key::End}, {"PageUp", Key::PageUp}, {"PageDown", Key::PageDown},
            {"Comma", Key::Comma}, {"Minus", Key::Minus}, {"Period", Key::Period}, {"Slash", Key::Slash},
            {"Semicolon", Key::Semicolon}, {"Equal", Key::Equal}, {"Control", Key::LeftControl},
            {"Keypad0", Key::KP0}, {"Keypad1", (KeyCode)321}, {"Keypad2", (KeyCode)322}, {"Keypad3", (KeyCode)323},
            {"Keypad4", (KeyCode)324}, {"Keypad5", (KeyCode)325}, {"Keypad6", (KeyCode)326}, {"Keypad7", (KeyCode)327},
            {"Keypad8", (KeyCode)328}, {"Keypad9", (KeyCode)329}, {"KeypadEnter", (KeyCode)335},
            
            // Function Keys
            {"F1", Key::F1}, {"F2", Key::F2}, {"F3", Key::F3}, {"F4", Key::F4}, {"F5", Key::F5},
            {"F6", Key::F6}, {"F7", Key::F7}, {"F8", Key::F8}, {"F9", Key::F9}, {"F10", Key::F10},
            {"F11", Key::F11}, {"F12", Key::F12}
        };
        
        auto it = keyMap.find(keyName);
        if (it != keyMap.end()) return it->second;
        // Büyük/küçük harf duyarsız ikinci deneme ("space", "leftshift")
        {
            std::string lower = keyName;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            for (const auto& [name, code] : keyMap)
            {
                std::string n = name;
                std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                if (n == lower) return code;
            }
        }
        
        try {
            return (KeyCode)std::stoi(keyName);
        } catch (...) {}
        
        return (KeyCode)0;
    }

    // Duplicate helper — same as in main .cpp (internal linkage)
    static Entity GetCurrentEntity_S()
    {
        auto* scene = ScriptContext::GetActiveScene();
        uint32_t id = ScriptContext::GetCurrentEntity();
        if (!scene || id == ScriptContext::NullEntity) return Entity{};
        entt::entity handle = static_cast<entt::entity>(id);
        if (!scene->m_Registry.valid(handle)) return Entity{};
        return Entity{ handle, scene };
    }

    static CQSMapObject* MakeV2(float x, float y)
    {
        auto* m = new CQSMapObject();
        m->Entries["x"] = Value::MakeFloat(x);
        m->Entries["y"] = Value::MakeFloat(y);
        return m;
    }

    // ════════════════════════════════════════
    //  INPUT: KEYBOARD
    // ════════════════════════════════════════
    Value CQSNativeBridge::IsKeyPressed(int argCount, Value* args)
    {
        if (argCount > 0)
        {
            if (args[0].IsString())
                return Value::MakeBool(GameInput::IsKeyHeld(StringToKeyCode(args[0].AsString()->Value)));
            else if (args[0].IsNumber())
                return Value::MakeBool(GameInput::IsKeyHeld(static_cast<KeyCode>((int)args[0].ToNumber())));
        }
        return Value::MakeBool(false);
    }
    Value CQSNativeBridge::IsKeyDown(int argCount, Value* args)
    {
        if (argCount > 0)
        {
            if (args[0].IsString())
                return Value::MakeBool(GameInput::IsKeyDown(StringToKeyCode(args[0].AsString()->Value)));
            else if (args[0].IsNumber())
                return Value::MakeBool(GameInput::IsKeyDown(static_cast<KeyCode>((int)args[0].ToNumber())));
        }
        return Value::MakeBool(false);
    }
    Value CQSNativeBridge::IsKeyUp(int argCount, Value* args)
    {
        if (argCount > 0)
        {
            if (args[0].IsString())
                return Value::MakeBool(GameInput::IsKeyUp(StringToKeyCode(args[0].AsString()->Value)));
            else if (args[0].IsNumber())
                return Value::MakeBool(GameInput::IsKeyUp(static_cast<KeyCode>((int)args[0].ToNumber())));
        }
        return Value::MakeBool(false);
    }

    // ════════════════════════════════════════
    //  INPUT: MOUSE
    // ════════════════════════════════════════
    Value CQSNativeBridge::IsMouseButtonPressed(int argCount, Value* args)
    {
        if (argCount > 0 && args[0].IsNumber())
            return Value::MakeBool(GameInput::IsMouseHeld(static_cast<MouseCode>((int)args[0].ToNumber())));
        return Value::MakeBool(false);
    }
    Value CQSNativeBridge::IsMouseButtonDown(int argCount, Value* args)
    {
        if (argCount > 0 && args[0].IsNumber())
            return Value::MakeBool(GameInput::IsMouseDown(static_cast<MouseCode>((int)args[0].ToNumber())));
        return Value::MakeBool(false);
    }
    Value CQSNativeBridge::IsMouseButtonUp(int argCount, Value* args)
    {
        if (argCount > 0 && args[0].IsNumber())
            return Value::MakeBool(GameInput::IsMouseUp(static_cast<MouseCode>((int)args[0].ToNumber())));
        return Value::MakeBool(false);
    }
    Value CQSNativeBridge::GetMousePosition(int, Value*)
    {
        glm::vec2 p = GameInput::GetMousePosition();
        return Value::MakeObject(MakeV2(p.x, p.y));
    }
    Value CQSNativeBridge::GetMouseX(int, Value*) { return Value::MakeFloat(GameInput::GetMousePosition().x); }
    Value CQSNativeBridge::GetMouseY(int, Value*) { return Value::MakeFloat(GameInput::GetMousePosition().y); }
    Value CQSNativeBridge::GetMouseDelta(int, Value*)
    {
        glm::vec2 d = GameInput::GetMouseDelta();
        return Value::MakeObject(MakeV2(d.x, d.y));
    }
    Value CQSNativeBridge::GetMouseScrollDelta(int, Value*) { return Value::MakeFloat(GameInput::GetScroll()); }
    Value CQSNativeBridge::SetCursorMode(int argCount, Value* args)
    {
        if (argCount > 0 && args[0].IsNumber()) Input::SetCursorMode((int)args[0].ToNumber());
        return Value::MakeNull();
    }

    // ════════════════════════════════════════
    //  INPUT: GAMEPAD
    // ════════════════════════════════════════
    // (pad, button) belgelenen sıra; tek argümanla (button) 0 numaralı kol kullanılır.
    // Eksik argümanlar VM tarafından null ile doldurulur.
    static bool GamepadArgs(int argCount, Value* args, int& pad, int& index)
    {
        if (argCount >= 2 && args[0].IsNumber() && args[1].IsNumber())
        {
            pad = (int)args[0].ToNumber();
            index = (int)args[1].ToNumber();
            return true;
        }
        if (argCount >= 1 && args[0].IsNumber())
        {
            pad = 0;
            index = (int)args[0].ToNumber();
            return true;
        }
        return false;
    }
    static int GamepadPad(int argCount, Value* args)
    {
        return (argCount > 0 && args[0].IsNumber()) ? (int)args[0].ToNumber() : 0;
    }
    Value CQSNativeBridge::IsGamepadConnected(int argCount, Value* args) { return Value::MakeBool(Input::IsGamepadConnected(GamepadPad(argCount, args))); }
    Value CQSNativeBridge::IsGamepadButtonPressed(int argCount, Value* args)
    {
        int pad = 0, button = 0;
        if (GamepadArgs(argCount, args, pad, button))
            return Value::MakeBool(Input::IsGamepadButtonPressed(button, pad));
        return Value::MakeBool(false);
    }
    Value CQSNativeBridge::GetGamepadAxis(int argCount, Value* args)
    {
        int pad = 0, axis = 0;
        if (GamepadArgs(argCount, args, pad, axis))
            return Value::MakeFloat(Input::GetGamepadAxis(axis, pad));
        return Value::MakeFloat(0.0);
    }
    Value CQSNativeBridge::GetGamepadLeftStick(int argCount, Value* args)
    {
        glm::vec2 s = Input::GetGamepadLeftStick(GamepadPad(argCount, args));
        return Value::MakeObject(MakeV2(s.x, s.y));
    }
    Value CQSNativeBridge::GetGamepadRightStick(int argCount, Value* args)
    {
        glm::vec2 s = Input::GetGamepadRightStick(GamepadPad(argCount, args));
        return Value::MakeObject(MakeV2(s.x, s.y));
    }
    Value CQSNativeBridge::GetGamepadLeftTrigger(int argCount, Value* args) { return Value::MakeFloat(Input::GetGamepadLeftTrigger(GamepadPad(argCount, args))); }
    Value CQSNativeBridge::GetGamepadRightTrigger(int argCount, Value* args) { return Value::MakeFloat(Input::GetGamepadRightTrigger(GamepadPad(argCount, args))); }
    Value CQSNativeBridge::SetGamepadVibration(int argCount, Value* args)
    {
        if (argCount >= 2) Input::SetGamepadVibration((float)args[0].ToNumber(), (float)args[1].ToNumber(), 0);
        return Value::MakeNull();
    }

    // ════════════════════════════════════════
    //  AUDIO
    // ════════════════════════════════════════
    Value CQSNativeBridge::AudioPlay(int, Value*)
    {
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeNull();
        if (!e.HasComponent<AudioSourceComponent>()) return Value::MakeNull();
        auto& asc = e.GetComponent<AudioSourceComponent>();
        if (AudioRandomContainers::IsAsset(asc.FilePath))
        {
            asc.ContainerRequest = true;   // Audio Random Container: her Play yeni klip seçer
            asc.IsPlaying = true;
        }
        else if (asc.RuntimeSound)
        {
            auto* src = static_cast<AudioSource*>(asc.RuntimeSound);
            src->Play();
            asc.IsPlaying = true;
        }
        return Value::MakeNull();
    }
    Value CQSNativeBridge::AudioStop(int, Value*)
    {
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeNull();
        if (!e.HasComponent<AudioSourceComponent>()) return Value::MakeNull();
        auto& asc = e.GetComponent<AudioSourceComponent>();
        if (asc.RuntimeSound)
        {
            auto* src = static_cast<AudioSource*>(asc.RuntimeSound);
            src->Stop();
            asc.IsPlaying = false;
        }
        return Value::MakeNull();
    }
    Value CQSNativeBridge::AudioPause(int, Value*)
    {
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeNull();
        if (!e.HasComponent<AudioSourceComponent>()) return Value::MakeNull();
        auto& asc = e.GetComponent<AudioSourceComponent>();
        if (asc.RuntimeSound) static_cast<AudioSource*>(asc.RuntimeSound)->Pause();
        return Value::MakeNull();
    }
    Value CQSNativeBridge::AudioSetVolume(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeNull();
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeNull();
        if (!e.HasComponent<AudioSourceComponent>()) return Value::MakeNull();
        auto& asc = e.GetComponent<AudioSourceComponent>();
        asc.Volume = (float)args[0].ToNumber();
        if (asc.RuntimeSound) static_cast<AudioSource*>(asc.RuntimeSound)->SetVolume(asc.Volume);
        return Value::MakeNull();
    }
    Value CQSNativeBridge::AudioGetVolume(int, Value*)
    {
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeFloat(0);
        if (!e.HasComponent<AudioSourceComponent>()) return Value::MakeFloat(0);
        return Value::MakeFloat(e.GetComponent<AudioSourceComponent>().Volume);
    }
    Value CQSNativeBridge::AudioSetPitch(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeNull();
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeNull();
        if (!e.HasComponent<AudioSourceComponent>()) return Value::MakeNull();
        auto& asc = e.GetComponent<AudioSourceComponent>();
        asc.Pitch = (float)args[0].ToNumber();
        if (asc.RuntimeSound) static_cast<AudioSource*>(asc.RuntimeSound)->SetPitch(asc.Pitch);
        return Value::MakeNull();
    }
    Value CQSNativeBridge::AudioSetLoop(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeNull();
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeNull();
        if (!e.HasComponent<AudioSourceComponent>()) return Value::MakeNull();
        auto& asc = e.GetComponent<AudioSourceComponent>();
        asc.Loop = args[0].IsTruthy();
        if (asc.RuntimeSound) static_cast<AudioSource*>(asc.RuntimeSound)->SetLooping(asc.Loop);
        return Value::MakeNull();
    }
    Value CQSNativeBridge::AudioIsPlaying(int, Value*)
    {
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeBool(false);
        if (!e.HasComponent<AudioSourceComponent>()) return Value::MakeBool(false);
        return Value::MakeBool(e.GetComponent<AudioSourceComponent>().IsPlaying);
    }
    Value CQSNativeBridge::AudioSetMasterVolume(int argCount, Value* args)
    {
        if (argCount > 0) AudioEngine::Get().SetMasterVolume((float)args[0].ToNumber());
        return Value::MakeNull();
    }
    Value CQSNativeBridge::AudioGetMasterVolume(int, Value*)
    {
        return Value::MakeFloat(AudioEngine::Get().GetMasterVolume());
    }

    // ════════════════════════════════════════
    //  TIME
    // ════════════════════════════════════════
    Value CQSNativeBridge::GetDeltaTime(int, Value*) { return Value::MakeFloat(TimeManager::GetDeltaTime()); }
    Value CQSNativeBridge::GetUnscaledDeltaTime(int, Value*) { return Value::MakeFloat(TimeManager::GetUnscaledDeltaTime()); }
    Value CQSNativeBridge::GetTime(int, Value*) { return Value::MakeFloat(TimeManager::GetTime()); }
    Value CQSNativeBridge::GetFrameCount(int, Value*) { return Value::MakeInt((int64_t)TimeManager::GetFrameCount()); }
    Value CQSNativeBridge::GetTimeScale(int, Value*) { return Value::MakeFloat(TimeManager::GetTimeScale()); }
    Value CQSNativeBridge::SetTimeScale(int argCount, Value* args)
    {
        if (argCount > 0) TimeManager::SetTimeScale((float)args[0].ToNumber());
        return Value::MakeNull();
    }

    // ════════════════════════════════════════
    //  SCENE / ENTITY
    // ════════════════════════════════════════
    Value CQSNativeBridge::FindEntityByTag(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeNull();
        auto* scene = ScriptContext::GetActiveScene();
        if (!scene) return Value::MakeNull();
        std::string tag = args[0].ToString();
        Entity found = scene->FindEntityByTag(tag);
        if (!found) return Value::MakeNull();
        return Value::MakeInt((int64_t)(uint32_t)found);
    }
    Value CQSNativeBridge::GetEntityTag(int, Value*)
    {
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeString("");
        return Value::MakeString(e.GetComponent<TagComponent>().GameTag);
    }
    Value CQSNativeBridge::SetEntityTag(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeNull();
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeNull();
        e.GetComponent<TagComponent>().GameTag = args[0].ToString();
        return Value::MakeNull();
    }
    Value CQSNativeBridge::GetEntityName(int, Value*)
    {
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeString("");
        return Value::MakeString(e.GetComponent<TagComponent>().Tag);
    }
    Value CQSNativeBridge::DestroyEntity(int, Value*)
    {
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeNull();
        auto* scene = ScriptContext::GetActiveScene();
        if (scene) scene->DestroyEntity(e);
        return Value::MakeNull();
    }
    Value CQSNativeBridge::CreateEntity(int argCount, Value* args)
    {
        auto* scene = ScriptContext::GetActiveScene();
        if (!scene) return Value::MakeNull();
        std::string name = (argCount > 0) ? args[0].ToString() : "New Entity";
        Entity created = scene->CreateEntity(name);
        return Value::MakeInt((int64_t)(uint32_t)created);
    }

    // ════════════════════════════════════════
    //  COMPONENT QUERIES
    // ════════════════════════════════════════
    Value CQSNativeBridge::HasRigidbody(int, Value*)
    {
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeBool(false);
        return Value::MakeBool(e.HasComponent<RigidbodyComponent>());
    }
    Value CQSNativeBridge::HasAudioSource(int, Value*)
    {
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeBool(false);
        return Value::MakeBool(e.HasComponent<AudioSourceComponent>());
    }
    Value CQSNativeBridge::HasAnimator(int, Value*)
    {
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeBool(false);
        return Value::MakeBool(e.HasComponent<AnimatorComponent>());
    }
    Value CQSNativeBridge::HasCamera(int, Value*)
    {
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeBool(false);
        return Value::MakeBool(e.HasComponent<CameraComponent>());
    }

    // ════════════════════════════════════════
    //  ANIMATOR
    // ════════════════════════════════════════
    Value CQSNativeBridge::AnimatorPlay(int, Value*)
    {
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeNull();
        if (e.HasComponent<AnimatorComponent>()) e.GetComponent<AnimatorComponent>().Playing = true;
        return Value::MakeNull();
    }
    Value CQSNativeBridge::AnimatorStop(int, Value*)
    {
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeNull();
        if (e.HasComponent<AnimatorComponent>()) e.GetComponent<AnimatorComponent>().Playing = false;
        return Value::MakeNull();
    }
    Value CQSNativeBridge::AnimatorSetSpeed(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeNull();
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeNull();
        if (e.HasComponent<AnimatorComponent>()) e.GetComponent<AnimatorComponent>().Speed = (float)args[0].ToNumber();
        return Value::MakeNull();
    }
    Value CQSNativeBridge::AnimatorGetSpeed(int, Value*)
    {
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeFloat(1.0);
        if (e.HasComponent<AnimatorComponent>()) return Value::MakeFloat(e.GetComponent<AnimatorComponent>().Speed);
        return Value::MakeFloat(1.0);
    }
    Value CQSNativeBridge::AnimatorSetClip(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeNull();
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeNull();
        if (e.HasComponent<AnimatorComponent>()) e.GetComponent<AnimatorComponent>().ActiveClipIndex = (int)args[0].ToNumber();
        return Value::MakeNull();
    }

    // ════════════════════════════════════════
    //  ANIMATION COMPONENT
    // ════════════════════════════════════════
    Value CQSNativeBridge::HasAnimationComponent(int, Value*)
    {
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeBool(false);
        return Value::MakeBool(e.HasComponent<AnimationComponent>());
    }

    Value CQSNativeBridge::AnimSetFloat(int argCount, Value* args)
    {
        if (argCount < 2) return Value::MakeNull();
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeNull();
        if (!e.HasComponent<AnimationComponent>()) return Value::MakeNull();
        auto& ac = e.GetComponent<AnimationComponent>();
        std::string name = args[0].ToString();
        float value = (float)args[1].ToNumber();
        for (auto& param : ac.Parameters)
        {
            if (param.Name == name && param.Type == AnimParameterType::Float)
            {
                param.FloatValue = value;
                break;
            }
        }
        return Value::MakeNull();
    }

    Value CQSNativeBridge::AnimSetBool(int argCount, Value* args)
    {
        if (argCount < 2) return Value::MakeNull();
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeNull();
        if (!e.HasComponent<AnimationComponent>()) return Value::MakeNull();
        auto& ac = e.GetComponent<AnimationComponent>();
        std::string name = args[0].ToString();
        bool value = args[1].IsTruthy();
        for (auto& param : ac.Parameters)
        {
            if (param.Name == name && param.Type == AnimParameterType::Bool)
            {
                param.BoolValue = value;
                break;
            }
        }
        return Value::MakeNull();
    }

    Value CQSNativeBridge::AnimSetInt(int argCount, Value* args)
    {
        if (argCount < 2) return Value::MakeNull();
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeNull();
        if (!e.HasComponent<AnimationComponent>()) return Value::MakeNull();
        auto& ac = e.GetComponent<AnimationComponent>();
        std::string name = args[0].ToString();
        int value = (int)args[1].ToNumber();
        for (auto& param : ac.Parameters)
        {
            if (param.Name == name && param.Type == AnimParameterType::Int)
            {
                param.IntValue = value;
                break;
            }
        }
        return Value::MakeNull();
    }

    Value CQSNativeBridge::AnimSetTrigger(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeNull();
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeNull();
        if (!e.HasComponent<AnimationComponent>()) return Value::MakeNull();
        auto& ac = e.GetComponent<AnimationComponent>();
        std::string name = args[0].ToString();
        for (auto& param : ac.Parameters)
        {
            if (param.Name == name && param.Type == AnimParameterType::Trigger)
            {
                param.FloatValue = 1.f;
                break;
            }
        }
        return Value::MakeNull();
    }

    Value CQSNativeBridge::AnimGetFloat(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeFloat(0.0);
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeFloat(0.0);
        if (!e.HasComponent<AnimationComponent>()) return Value::MakeFloat(0.0);
        auto& ac = e.GetComponent<AnimationComponent>();
        std::string name = args[0].ToString();
        for (const auto& param : ac.Parameters)
        {
            if (param.Name == name && param.Type == AnimParameterType::Float)
                return Value::MakeFloat(param.FloatValue);
        }
        return Value::MakeFloat(0.0);
    }

    Value CQSNativeBridge::AnimGetBool(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeBool(false);
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeBool(false);
        if (!e.HasComponent<AnimationComponent>()) return Value::MakeBool(false);
        auto& ac = e.GetComponent<AnimationComponent>();
        std::string name = args[0].ToString();
        for (const auto& param : ac.Parameters)
        {
            if (param.Name == name && param.Type == AnimParameterType::Bool)
                return Value::MakeBool(param.BoolValue);
        }
        return Value::MakeBool(false);
    }

    Value CQSNativeBridge::AnimSetSpeed(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeNull();
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeNull();
        if (e.HasComponent<AnimationComponent>()) e.GetComponent<AnimationComponent>().Speed = (float)args[0].ToNumber();
        return Value::MakeNull();
    }

    Value CQSNativeBridge::AnimPlay(int, Value*)
    {
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeNull();
        if (e.HasComponent<AnimationComponent>()) e.GetComponent<AnimationComponent>().Playing = true;
        return Value::MakeNull();
    }

    Value CQSNativeBridge::AnimStop(int, Value*)
    {
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeNull();
        if (e.HasComponent<AnimationComponent>()) e.GetComponent<AnimationComponent>().Playing = false;
        return Value::MakeNull();
    }

    // ════════════════════════════════════════
    //  RENDERER
    // ════════════════════════════════════════
    Value CQSNativeBridge::SetColor(int argCount, Value* args)
    {
        if (argCount < 4) return Value::MakeNull();
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeNull();
        glm::vec4 color((float)args[0].ToNumber(), (float)args[1].ToNumber(), (float)args[2].ToNumber(), (float)args[3].ToNumber());
        if (e.HasComponent<MeshRendererComponent>()) MeshRendererMaterial::SetColor(e.GetComponent<MeshRendererComponent>(), color);
        else if (e.HasComponent<SpriteRendererComponent>()) e.GetComponent<SpriteRendererComponent>().Color = color;
        return Value::MakeNull();
    }
    Value CQSNativeBridge::GetColor(int, Value*)
    {
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeNull();
        glm::vec4 c(1.0f);
        if (e.HasComponent<MeshRendererComponent>()) c = MeshRendererMaterial::GetColor(e.GetComponent<MeshRendererComponent>());
        else if (e.HasComponent<SpriteRendererComponent>()) c = e.GetComponent<SpriteRendererComponent>().Color;
        auto* m = new CQSMapObject();
        m->Entries["r"] = Value::MakeFloat(c.r);
        m->Entries["g"] = Value::MakeFloat(c.g);
        m->Entries["b"] = Value::MakeFloat(c.b);
        m->Entries["a"] = Value::MakeFloat(c.a);
        return Value::MakeObject(m);
    }
    Value CQSNativeBridge::SetVisible(int argCount, Value* args)
    {
        // ActiveComponent bayrağını kullanır: kapalı obje hiçbir kameraya
        // çizilmez, gölge bırakmaz. Bayrak yoksa eski scale hilesine düşülür.
        if (argCount < 1) return Value::MakeNull();
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeNull();
        bool vis = args[0].IsTruthy();
        if (!vis)
        {
            if (!e.HasComponent<ActiveComponent>())
                e.AddComponent<ActiveComponent>();
            e.GetComponent<ActiveComponent>().Active = false;
            return Value::MakeNull();
        }
        if (e.HasComponent<ActiveComponent>())
            e.GetComponent<ActiveComponent>().Active = true;
        return Value::MakeNull();
    }

    // ════════════════════════════════════════
    //  CAMERA
    // ════════════════════════════════════════
    Value CQSNativeBridge::GetCameraFOV(int, Value*)
    {
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeFloat(45.0);
        if (!e.HasComponent<CameraComponent>()) return Value::MakeFloat(45.0);
        return Value::MakeFloat(glm::degrees(e.GetComponent<CameraComponent>().Camera.GetPerspectiveVerticalFOV()));
    }
    Value CQSNativeBridge::SetCameraFOV(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeNull();
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeNull();
        if (!e.HasComponent<CameraComponent>()) return Value::MakeNull();
        e.GetComponent<CameraComponent>().Camera.SetPerspectiveVerticalFOV(glm::radians((float)args[0].ToNumber()));
        return Value::MakeNull();
    }

    // ════════════════════════════════════════
    //  NAVIGATION
    // ════════════════════════════════════════
    Value CQSNativeBridge::NavSetDestination(int argCount, Value* args)
    {
        if (argCount < 3) return Value::MakeBool(false);
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeBool(false);
        auto* scene = ScriptContext::GetActiveScene();
        if (!scene || !e.HasComponent<NavMeshAgentComponent>())
        {
            CQ_CORE_WARN("[Nav] '{0}' has no NavMesh Agent", e.GetComponent<TagComponent>().Tag);
            return Value::MakeBool(false);
        }
        glm::vec3 target((float)args[0].ToNumber(), (float)args[1].ToNumber(), (float)args[2].ToNumber());
        return Value::MakeBool(NavAgents::SetDestination(*scene, (uint32_t)e, target));
    }

    Value CQSNativeBridge::NavBake(int, Value*)
    {
        auto* scene = ScriptContext::GetActiveScene();
        if (!scene) return Value::MakeNull();
        NavAgents::BakeCached(*scene);
        return Value::MakeInt((int64_t)scene->GetNavMesh().GetCellCount());
    }

    Value CQSNativeBridge::NavStop(int, Value*)
    {
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeNull();
        if (e.HasComponent<NavMeshAgentComponent>())
        {
            auto& a = e.GetComponent<NavMeshAgentComponent>();
            a.IsStopped = true;
        }
        return Value::MakeNull();
    }

    Value CQSNativeBridge::NavIsStopped(int, Value*)
    {
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeBool(true);
        if (e.HasComponent<NavMeshAgentComponent>()) return Value::MakeBool(e.GetComponent<NavMeshAgentComponent>().IsStopped);
        return Value::MakeBool(true);
    }

    Value CQSNativeBridge::NavHasPath(int, Value*)
    {
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeBool(false);
        if (e.HasComponent<NavMeshAgentComponent>()) return Value::MakeBool(e.GetComponent<NavMeshAgentComponent>().HasPath);
        return Value::MakeBool(false);
    }

    Value CQSNativeBridge::NavGetSpeed(int, Value*)
    {
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeFloat(0);
        if (e.HasComponent<NavMeshAgentComponent>()) return Value::MakeFloat(e.GetComponent<NavMeshAgentComponent>().Speed);
        return Value::MakeFloat(0);
    }

    Value CQSNativeBridge::NavSetSpeed(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeNull();
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeNull();
        if (e.HasComponent<NavMeshAgentComponent>()) e.GetComponent<NavMeshAgentComponent>().Speed = (float)args[0].ToNumber();
        return Value::MakeNull();
    }

    // ════════════════════════════════════════
    //  NETWORKING
    // ════════════════════════════════════════

    Value CQSNativeBridge::NetStartServer(int argCount, Value* args)
    {
        uint16_t port = (argCount >= 1) ? (uint16_t)args[0].ToNumber() : CQ_DEFAULT_PORT;
        bool ok = NetworkManager::Get().StartServer(port);
        return Value::MakeBool(ok);
    }

    Value CQSNativeBridge::NetConnect(int argCount, Value* args)
    {
        if (argCount < 2) return Value::MakeBool(false);
        std::string host = args[0].ToString();
        uint16_t port = (uint16_t)args[1].ToNumber();
        bool ok = NetworkManager::Get().ConnectToServer(host, port);
        return Value::MakeBool(ok);
    }

    Value CQSNativeBridge::NetDisconnect(int, Value*)
    {
        auto& mgr = NetworkManager::Get();
        if (mgr.IsServer()) mgr.StopServer();
        else mgr.DisconnectFromServer();
        return Value::MakeNull();
    }

    Value CQSNativeBridge::NetSend(int argCount, Value* args)
    {
        if (argCount < 2) return Value::MakeNull();
        PeerID peerID = (PeerID)args[0].ToNumber();
        std::string msg = args[1].ToString();

        Packet pkt(MessageType::UserMessage);
        pkt.WriteString(msg);
        NetworkManager::Get().SendPacket(peerID, pkt);
        return Value::MakeNull();
    }

    Value CQSNativeBridge::NetBroadcast(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeNull();
        std::string msg = args[0].ToString();

        Packet pkt(MessageType::UserMessage);
        pkt.WriteString(msg);
        NetworkManager::Get().BroadcastPacket(pkt);
        return Value::MakeNull();
    }

    Value CQSNativeBridge::NetIsServer(int, Value*)
    {
        return Value::MakeBool(NetworkManager::Get().IsServer());
    }

    Value CQSNativeBridge::NetIsClient(int, Value*)
    {
        return Value::MakeBool(NetworkManager::Get().IsClient());
    }

    Value CQSNativeBridge::NetIsConnected(int, Value*)
    {
        return Value::MakeBool(NetworkManager::Get().IsConnected());
    }

    Value CQSNativeBridge::NetGetPeerCount(int, Value*)
    {
        return Value::MakeInt(static_cast<int64_t>(NetworkManager::Get().GetPeerCount()));
    }

    // ════════════════════════════════════════
    //  NETWORKING (Advanced)
    // ════════════════════════════════════════

    Value CQSNativeBridge::NetEnableEncryption(int, Value*)
    {
        bool ok = NetworkManager::Get().EnableEncryption();
        if (ok)
            CQ_CORE_INFO("[CQS:Net] Encryption enabled (AES-256-GCM + RSA-2048).");
        else
            CQ_CORE_WARN("[CQS:Net] Failed to enable encryption.");
        return Value::MakeBool(ok);
    }

    Value CQSNativeBridge::NetIsEncryptionEnabled(int, Value*)
    {
        return Value::MakeBool(NetworkManager::Get().IsEncryptionEnabled());
    }

    Value CQSNativeBridge::NetOpenNATPort(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeBool(false);
        uint16_t port = (uint16_t)args[0].ToNumber();

        auto& nat = NetworkManager::Get().GetNAT();
        if (!nat.IsAvailable())
        {
            CQ_CORE_INFO("[CQS:NAT] Discovering UPnP devices...");
            if (!nat.Discover())
            {
                CQ_CORE_WARN("[CQS:NAT] No UPnP router found on network.");
                return Value::MakeBool(false);
            }
        }

        bool ok = nat.OpenPort(port, port, "UDP", "Conqueror Engine");
        if (ok)
            CQ_CORE_INFO("[CQS:NAT] Port {0}/UDP opened on router.", port);
        else
            CQ_CORE_WARN("[CQS:NAT] Failed to open port {0}.", port);
        return Value::MakeBool(ok);
    }

    Value CQSNativeBridge::NetCloseNATPort(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeBool(false);
        uint16_t port = (uint16_t)args[0].ToNumber();

        auto& nat = NetworkManager::Get().GetNAT();
        bool ok = nat.ClosePort(port, "UDP");
        if (ok)
            CQ_CORE_INFO("[CQS:NAT] Port {0}/UDP closed.", port);
        return Value::MakeBool(ok);
    }

    Value CQSNativeBridge::NetGetExternalIP(int, Value*)
    {
        auto& nat = NetworkManager::Get().GetNAT();
        if (!nat.IsAvailable())
        {
            if (!nat.Discover())
                return Value::MakeString("unavailable");
        }
        std::string ip = nat.GetExternalIP();
        CQ_CORE_INFO("[CQS:NAT] External IP: {0}", ip);
        return Value::MakeString(ip);
    }

    Value CQSNativeBridge::NetCallRPC(int argCount, Value* args)
    {
        if (argCount < 1) return Value::MakeNull();
        std::string message = args[0].ToString();

        // Auto-register a test RPC handler if not already registered
        static bool registered = false;
        if (!registered)
        {
            RPCSystem::Get().Register("TestRPC", [](PeerID sender, Packet& pkt) {
                std::string msg = pkt.ReadString();
                CQ_CORE_INFO("[RPC Test] Received from peer {0}: {1}", sender, msg);
            });
            registered = true;
        }

        // Build args packet with the message
        Packet argsPkt;
        argsPkt.WriteString(message);

        // Call on everyone (server + all clients)
        RPCSystem::Get().CallEveryone("TestRPC", argsPkt);
        CQ_CORE_INFO("[CQS:RPC] Sent TestRPC: {0}", message);
        return Value::MakeNull();
    }

    Value CQSNativeBridge::InputCreateContext(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsNumber()) return Value::MakeNull();
        Input::CreateContext(args[0].AsString()->Value, (int)args[1].ToNumber());
        return Value::MakeNull();
    }
    Value CQSNativeBridge::InputSwitchContext(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeNull();
        Input::SwitchContext(args[0].AsString()->Value);
        return Value::MakeNull();
    }
    Value CQSNativeBridge::InputPushContext(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeNull();
        Input::PushContext(args[0].AsString()->Value);
        return Value::MakeNull();
    }
    Value CQSNativeBridge::InputPopContext(int argCount, Value* args)
    {
        Input::PopContext();
        return Value::MakeNull();
    }
    Value CQSNativeBridge::InputBindAction(int argCount, Value* args)
    {
        if (argCount < 3 || !args[0].IsString() || !args[1].IsString()) return Value::MakeNull();
        
        KeyCode key = (KeyCode)0;
        if (args[2].IsString())
            key = StringToKeyCode(args[2].AsString()->Value);
        else if (args[2].IsNumber())
            key = (KeyCode)(int)args[2].ToNumber();
            
        std::vector<KeyCode> keys = { key };
        Input::BindAction(args[0].AsString()->Value, args[1].AsString()->Value, InputActionType::Button, keys);
        return Value::MakeNull();
    }
    Value CQSNativeBridge::InputBindChord(int argCount, Value* args)
    {
        if (argCount < 4 || !args[0].IsString() || !args[1].IsString()) return Value::MakeNull();
        
        KeyCode mod = (KeyCode)0;
        if (args[2].IsString())
            mod = StringToKeyCode(args[2].AsString()->Value);
        else if (args[2].IsNumber())
            mod = (KeyCode)(int)args[2].ToNumber();
            
        KeyCode key = (KeyCode)0;
        if (args[3].IsString())
            key = StringToKeyCode(args[3].AsString()->Value);
        else if (args[3].IsNumber())
            key = (KeyCode)(int)args[3].ToNumber();
            
        std::vector<KeyCode> mods = { mod };
        Input::BindChord(args[0].AsString()->Value, args[1].AsString()->Value, mods, key);
        return Value::MakeNull();
    }
    // Project-wide Actions atanmışsa eylemler önce oradan ("Jump" ya da "Player/Jump"), yoksa proje girdi haritasından
    Value CQSNativeBridge::InputGetAction(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeBool(false);
        if (const auto* st = InputActionRuntime::FindProjectWide(args[0].AsString()->Value))
            return Value::MakeBool(st->Pressed);
        return Value::MakeBool(Input::GetActionButton(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::InputGetActionState(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeInt(0);
        if (const auto* st = InputActionRuntime::FindProjectWide(args[0].AsString()->Value))
        {
            InputTriggerState s = st->PressedThisFrame ? InputTriggerState::Started
                                : st->ReleasedThisFrame ? InputTriggerState::Completed
                                : st->Pressed ? InputTriggerState::Ongoing : InputTriggerState::None;
            return Value::MakeInt((int)s);
        }
        return Value::MakeInt((int)Input::GetActionState(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::InputGetActionAxis1D(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeFloat(0.0);
        if (const auto* st = InputActionRuntime::FindProjectWide(args[0].AsString()->Value))
            return Value::MakeFloat(st->Vector ? st->Magnitude : st->Value.x);
        return Value::MakeFloat(Input::GetActionAxis1D(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::InputGetActionAxis2D(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeObject(MakeV2(0,0));
        if (const auto* st = InputActionRuntime::FindProjectWide(args[0].AsString()->Value))
            return Value::MakeObject(MakeV2(st->Value.x, st->Value.y));
        glm::vec2 v = Input::GetActionAxis2D(args[0].AsString()->Value);
        return Value::MakeObject(MakeV2(v.x, v.y));
    }
    Value CQSNativeBridge::InputIsComboTriggered(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeBool(false);
        return Value::MakeBool(Input::IsComboTriggered(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::InputGetAnyKey(int argCount, Value* args)
    {
        return Value::MakeString(Input::GetKeyName(Input::GetAnyKeyPressed()));
    }

    // ── Logging ──
    Value CQSNativeBridge::LogRegisterCategory(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsNumber()) return Value::MakeNull();
        Log::RegisterCategory(args[0].AsString()->Value, (LogLevel)(int)args[1].ToNumber());
        return Value::MakeNull();
    }
    Value CQSNativeBridge::LogSetLevel(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsNumber()) return Value::MakeNull();
        Log::SetCategoryLevel(args[0].AsString()->Value, (LogLevel)(int)args[1].ToNumber());
        return Value::MakeNull();
    }
    Value CQSNativeBridge::LogGenerateCrashDump(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeNull();
        Log::GenerateCrashDump(args[0].AsString()->Value);
        return Value::MakeNull();
    }
    Value CQSNativeBridge::LogGetHistoryCount(int argCount, Value* args)
    {
        return Value::MakeInt((int)Log::GetLogHistory().size());
    }
    Value CQSNativeBridge::LogJSON(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsString()) return Value::MakeNull();
        Log::LogJSON(args[0].AsString()->Value, args[1].AsString()->Value);
        return Value::MakeNull();
    }
    Value CQSNativeBridge::LogPushContext(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeNull();
        Log::PushContext(args[0].AsString()->Value);
        return Value::MakeNull();
    }
    Value CQSNativeBridge::LogPopContext(int, Value*)
    {
        Log::PopContext();
        return Value::MakeNull();
    }
    Value CQSNativeBridge::LogSendWebhookAlert(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeNull();
        Log::SendWebhookAlert(args[0].AsString()->Value);
        return Value::MakeNull();
    }
    Value CQSNativeBridge::LogEnableNetworkBroadcaster(int argCount, Value* args)
    {
        int port = (argCount > 0 && args[0].IsNumber()) ? (int)args[0].ToNumber() : 49152;
        Log::EnableNetworkBroadcaster(port);
        return Value::MakeNull();
    }
    Value CQSNativeBridge::LogMetricIncrement(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeNull();
        double amount = (argCount > 1 && args[1].IsNumber()) ? args[1].ToNumber() : 1.0;
        Log::MetricIncrement(args[0].AsString()->Value, amount);
        return Value::MakeNull();
    }
    Value CQSNativeBridge::LogMetricSetGauge(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsNumber()) return Value::MakeNull();
        Log::MetricSetGauge(args[0].AsString()->Value, args[1].ToNumber());
        return Value::MakeNull();
    }
    Value CQSNativeBridge::LogMetricRecordTime(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString() || !args[1].IsNumber()) return Value::MakeNull();
        Log::MetricRecordTime(args[0].AsString()->Value, args[1].ToNumber());
        return Value::MakeNull();
    }
    Value CQSNativeBridge::LogDumpMetricsToLog(int, Value*)
    {
        Log::DumpMetricsToLog();
        return Value::MakeNull();
    }
    Value CQSNativeBridge::LogSetCategoryEnabled(int argCount, Value* args)
    {
        if (argCount < 2 || !args[0].IsString()) return Value::MakeNull();
        Log::SetCategoryEnabled(args[0].AsString()->Value, args[1].IsTruthy());
        return Value::MakeNull();
    }
    Value CQSNativeBridge::LogIsCategoryEnabled(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeBool(false);
        return Value::MakeBool(Log::IsCategoryEnabled(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::LogSetGlobalLevel(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsNumber()) return Value::MakeNull();
        Log::SetGlobalLevel((LogLevel)(int)args[0].ToNumber());
        return Value::MakeNull();
    }
    Value CQSNativeBridge::LogGetGlobalLevel(int, Value*)
    {
        return Value::MakeInt((int)Log::GetGlobalLevel());
    }
    Value CQSNativeBridge::LogExportHistoryToHtml(int argCount, Value* args)
    {
        std::string path = (argCount > 0 && args[0].IsString()) ? args[0].AsString()->Value : "LogExport.html";
        Log::ExportHistoryToHtml(path);
        return Value::MakeNull();
    }
    Value CQSNativeBridge::LogExportHistoryToJson(int argCount, Value* args)
    {
        std::string path = (argCount > 0 && args[0].IsString()) ? args[0].AsString()->Value : "LogExport.json";
        Log::ExportHistoryToJson(path);
        return Value::MakeNull();
    }
    Value CQSNativeBridge::LogExportProfilerSession(int argCount, Value* args)
    {
        std::string path = (argCount > 0 && args[0].IsString()) ? args[0].AsString()->Value : "profile.json";
        Log::ExportProfilerSession(path);
        return Value::MakeNull();
    }
    Value CQSNativeBridge::LogClearLogHistory(int, Value*)
    {
        Log::ClearLogHistory();
        return Value::MakeNull();
    }
    Value CQSNativeBridge::LogFlushAll(int, Value*)
    {
        Log::FlushAll();
        return Value::MakeNull();
    }
    Value CQSNativeBridge::LogDisableNetworkBroadcaster(int, Value*)
    {
        Log::DisableNetworkBroadcaster();
        return Value::MakeNull();
    }
    Value CQSNativeBridge::LogGetCurrentContext(int, Value*)
    {
        return Value::MakeString(Log::GetCurrentContext());
    }

    // ── DebugDraw ──
    static glm::vec3 CQSDebugToVec3(const Value& value, const glm::vec3& fallback = glm::vec3(0.0f))
    {
        if (!value.IsMap())
            return fallback;

        auto* map = value.AsMap();
        auto read = [&](const char* key, float defaultValue)
        {
            auto it = map->Entries.find(key);
            return it != map->Entries.end() ? static_cast<float>(it->second.ToNumber()) : defaultValue;
        };

        return glm::vec3(read("x", fallback.x), read("y", fallback.y), read("z", fallback.z));
    }

    static float CQSDebugToFloat(const Value& value, float fallback = 0.0f)
    {
        if (value.IsNumber())
            return static_cast<float>(value.ToNumber());
        return fallback;
    }

    static std::string CQSDebugToString(const Value& value)
    {
        if (value.IsString())
            return value.AsString()->Value;
        return {};
    }

    Value CQSNativeBridge::DebugDrawLine(int argCount, Value* args)
    {
        if (argCount < 3)
            return Value::MakeNull();

        glm::vec3 start = CQSDebugToVec3(args[0]);
        glm::vec3 end = CQSDebugToVec3(args[1]);
        float duration = CQSDebugToFloat(args[2], 0.0f);

        DebugDraw::Line(start, end, DebugPalette::White, duration, true);
        return Value::MakeNull();
    }

    Value CQSNativeBridge::DebugDrawBox(int argCount, Value* args)
    {
        if (argCount < 3)
            return Value::MakeNull();

        glm::vec3 center = CQSDebugToVec3(args[0]);
        glm::vec3 size = CQSDebugToVec3(args[1], glm::vec3(1.0f));
        float duration = CQSDebugToFloat(args[2], 0.0f);

        DebugDraw::Box(center, size, DebugPalette::BoundsColor, duration, true);
        return Value::MakeNull();
    }

    Value CQSNativeBridge::DebugDrawSolidBox(int argCount, Value* args)
    {
        if (argCount < 3)
            return Value::MakeNull();

        glm::vec3 center = CQSDebugToVec3(args[0]);
        glm::vec3 size = CQSDebugToVec3(args[1], glm::vec3(1.0f));
        float duration = CQSDebugToFloat(args[2], 0.0f);

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), center) * glm::scale(glm::mat4(1.0f), size);
        DebugDraw::SolidBox(transform, DebugPalette::SolidBounds, duration, true);
        return Value::MakeNull();
    }

    Value CQSNativeBridge::DebugDrawSphere(int argCount, Value* args)
    {
        if (argCount < 3)
            return Value::MakeNull();

        glm::vec3 center = CQSDebugToVec3(args[0]);
        float radius = CQSDebugToFloat(args[1], 0.5f);
        float duration = CQSDebugToFloat(args[2], 0.0f);

        DebugDraw::Sphere(center, radius, DebugPalette::BoundsColor, duration, true);
        return Value::MakeNull();
    }

    Value CQSNativeBridge::DebugDrawSolidSphere(int argCount, Value* args)
    {
        if (argCount < 3)
            return Value::MakeNull();

        glm::vec3 center = CQSDebugToVec3(args[0]);
        float radius = CQSDebugToFloat(args[1], 0.5f);
        float duration = CQSDebugToFloat(args[2], 0.0f);

        DebugDraw::SolidSphere(center, radius, DebugPalette::SolidBounds, duration, true);
        return Value::MakeNull();
    }

    Value CQSNativeBridge::DebugDrawText3D(int argCount, Value* args)
    {
        if (argCount < 3 || !args[1].IsString())
            return Value::MakeNull();

        glm::vec3 position = CQSDebugToVec3(args[0]);
        std::string text = CQSDebugToString(args[1]);
        float duration = CQSDebugToFloat(args[2], 0.0f);

        DebugDraw::Text3D(position, text, DebugPalette::White, 0.08f, duration, false);
        return Value::MakeNull();
    }

    Value CQSNativeBridge::DebugDrawRay(int argCount, Value* args)
    {
        if (argCount < 3)
            return Value::MakeNull();

        glm::vec3 origin = CQSDebugToVec3(args[0]);
        glm::vec3 direction = CQSDebugToVec3(args[1], glm::vec3(0.0f, 0.0f, 1.0f));
        float length = CQSDebugToFloat(args[2], 1.0f);

        DebugDraw::Ray(origin, direction, length, DebugPalette::Red, 0.0f, true);
        return Value::MakeNull();
    }

    Value CQSNativeBridge::DebugDrawCapsule(int argCount, Value* args)
    {
        if (argCount < 3)
            return Value::MakeNull();

        glm::vec3 center = CQSDebugToVec3(args[0]);
        float radius = CQSDebugToFloat(args[1], 0.5f);
        float height = CQSDebugToFloat(args[2], 2.0f);

        DebugDraw::Capsule(center, radius, height, DebugPalette::BoundsColor, 0.0f, true);
        return Value::MakeNull();
    }

    Value CQSNativeBridge::DebugDrawGrid(int argCount, Value* args)
    {
        if (argCount < 3)
            return Value::MakeNull();

        float size = CQSDebugToFloat(args[0], 10.0f);
        float step = CQSDebugToFloat(args[1], 1.0f);
        float duration = CQSDebugToFloat(args[2], 0.0f);

        DebugDraw::Grid(size, step, DebugPalette::GridColor, duration, true);
        return Value::MakeNull();
    }

    Value CQSNativeBridge::DebugDrawCoordinateAxes(int argCount, Value* args)
    {
        if (argCount < 2)
            return Value::MakeNull();

        glm::vec3 origin = CQSDebugToVec3(args[0]);
        float length = CQSDebugToFloat(args[1], 1.0f);

        DebugDraw::CoordinateAxes(origin, length, 0.0f, true);
        return Value::MakeNull();
    }

    Value CQSNativeBridge::DebugDrawVelocity(int argCount, Value* args)
    {
        if (argCount < 3)
            return Value::MakeNull();

        glm::vec3 origin = CQSDebugToVec3(args[0]);
        glm::vec3 velocity = CQSDebugToVec3(args[1]);
        float scale = CQSDebugToFloat(args[2], 1.0f);

        DebugDraw::Velocity(origin, velocity, scale, DebugPalette::VelocityColor, 0.0f, true);
        return Value::MakeNull();
    }

    Value CQSNativeBridge::DebugDrawClear(int, Value*)
    {
        DebugDraw::Clear();
        return Value::MakeNull();
    }

    Value CQSNativeBridge::DebugDrawClearTimed(int, Value*)
    {
        DebugDraw::ClearTimed();
        return Value::MakeNull();
    }

    Value CQSNativeBridge::DebugDrawSetEnabled(int argCount, Value* args)
    {
        if (argCount < 1)
            return Value::MakeNull();

        DebugDraw::SetEnabled(args[0].IsTruthy());
        return Value::MakeNull();
    }

    Value CQSNativeBridge::DebugDrawSetLineWidth(int argCount, Value* args)
    {
        if (argCount < 1)
            return Value::MakeNull();

        float width = CQSDebugToFloat(args[0], 2.0f);
        DebugDraw::SetLineWidth(width);
        return Value::MakeNull();
    }

    // ── DebugDraw with color ──
    static glm::vec4 CQSDebugToColor(const Value& value, const glm::vec4& fallback = glm::vec4(1.0f))
    {
        if (value.IsMap())
        {
            auto* map = value.AsMap();
            auto read = [&](const char* key, float def) {
                auto it = map->Entries.find(key);
                return it != map->Entries.end() ? static_cast<float>(it->second.ToNumber()) : def;
            };
            return glm::vec4(read("r", fallback.r), read("g", fallback.g), read("b", fallback.b), read("a", fallback.a));
        }
        return fallback;
    }

    Value CQSNativeBridge::DebugDrawLineC(int argCount, Value* args)
    {
        if (argCount < 3) return Value::MakeNull();
        glm::vec3 start = CQSDebugToVec3(args[0]);
        glm::vec3 end = CQSDebugToVec3(args[1]);
        float duration = CQSDebugToFloat(args[2], 0.0f);
        glm::vec4 color = argCount >= 4 ? CQSDebugToColor(args[3]) : DebugPalette::White;
        DebugDraw::Line(start, end, color, duration, true);
        return Value::MakeNull();
    }

    Value CQSNativeBridge::DebugDrawSolidBoxC(int argCount, Value* args)
    {
        if (argCount < 3) return Value::MakeNull();
        glm::vec3 center = CQSDebugToVec3(args[0]);
        glm::vec3 size = CQSDebugToVec3(args[1], glm::vec3(1.0f));
        float duration = CQSDebugToFloat(args[2], 0.0f);
        glm::vec4 color = argCount >= 4 ? CQSDebugToColor(args[3]) : DebugPalette::SolidBounds;
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), center) * glm::scale(glm::mat4(1.0f), size);
        DebugDraw::SolidBox(transform, color, duration, true);
        return Value::MakeNull();
    }

    Value CQSNativeBridge::DebugDrawSolidSphereC(int argCount, Value* args)
    {
        if (argCount < 3) return Value::MakeNull();
        glm::vec3 center = CQSDebugToVec3(args[0]);
        float radius = CQSDebugToFloat(args[1], 0.5f);
        float duration = CQSDebugToFloat(args[2], 0.0f);
        glm::vec4 color = argCount >= 4 ? CQSDebugToColor(args[3]) : DebugPalette::SolidBounds;
        DebugDraw::SolidSphere(center, radius, color, duration, true);
        return Value::MakeNull();
    }

    Value CQSNativeBridge::DebugDrawTriangleC(int argCount, Value* args)
    {
        if (argCount < 4) return Value::MakeNull();
        glm::vec3 a = CQSDebugToVec3(args[0]);
        glm::vec3 b = CQSDebugToVec3(args[1]);
        glm::vec3 c = CQSDebugToVec3(args[2]);
        float duration = CQSDebugToFloat(args[3], 0.0f);
        glm::vec4 color = argCount >= 5 ? CQSDebugToColor(args[4]) : DebugPalette::White;
        DebugDraw::Triangle(a, b, c, color, duration, true);
        return Value::MakeNull();
    }

    Value CQSNativeBridge::DebugDrawSolidTriangleC(int argCount, Value* args)
    {
        if (argCount < 4) return Value::MakeNull();
        glm::vec3 a = CQSDebugToVec3(args[0]);
        glm::vec3 b = CQSDebugToVec3(args[1]);
        glm::vec3 c = CQSDebugToVec3(args[2]);
        float duration = CQSDebugToFloat(args[3], 0.0f);
        glm::vec4 color = argCount >= 5 ? CQSDebugToColor(args[4]) : DebugPalette::White;
        DebugSolidDraw::Triangle(a, b, c, color, true);
        return Value::MakeNull();
    }

    Value CQSNativeBridge::DebugDrawPointC(int argCount, Value* args)
    {
        if (argCount < 3) return Value::MakeNull();
        glm::vec3 pos = CQSDebugToVec3(args[0]);
        float size = CQSDebugToFloat(args[1], 0.1f);
        float duration = CQSDebugToFloat(args[2], 0.0f);
        glm::vec4 color = argCount >= 4 ? CQSDebugToColor(args[3]) : DebugPalette::White;
        DebugDraw::Point(pos, size, color, duration, true);
        return Value::MakeNull();
    }

    // ── Viewport Helpers ──
    Value CQSNativeBridge::IsViewportHovered(int, Value*)
    {
        auto* scene = ScriptContext::GetActiveScene();
        if (!scene) return Value::MakeBool(false);
        return Value::MakeBool(scene->m_ViewportHovered);
    }

    // Betiklerin gördüğü kamera: editör kamerası seçiliyse o, değilse Primary kamera, o da yoksa editör kamerası
    static bool ScriptViewProjection(Scene* scene, glm::mat4& viewProj)
    {
        bool found = false;
        if (scene->GetUseEditorCameraForScripts() && scene->m_HasEditorCamera)
        {
            viewProj = scene->m_EditorViewProjection;
            found = true;
        }
        if (!found)
        {
            auto camView = scene->m_Registry.view<CameraComponent>();
            for (auto entity : camView)
            {
                auto& cc = camView.get<CameraComponent>(entity);
                if (!cc.Primary)
                    continue;
                auto& tc = scene->m_Registry.get<TransformComponent>(entity);
                viewProj = cc.Camera.GetProjection() * glm::inverse(tc.WorldTransform);
                found = true;
                break;
            }
        }
        if (!found && scene->m_HasEditorCamera)
        {
            viewProj = scene->m_EditorViewProjection;
            found = true;
        }
        return found;
    }

    bool ScreenPointToWorldRay(Scene* scene, float sx, float sy, glm::vec3& origin, glm::vec3& direction)
    {
        if (!scene)
            return false;
        float vw = scene->m_ViewportBoundsMax.x - scene->m_ViewportBoundsMin.x;
        float vh = scene->m_ViewportBoundsMax.y - scene->m_ViewportBoundsMin.y;
        if (vw <= 0 || vh <= 0)
            return false;
        float rx = sx - scene->m_ViewportBoundsMin.x;
        float ry = sy - scene->m_ViewportBoundsMin.y;
        glm::mat4 viewProj{ 1.0f };
        if (!ScriptViewProjection(scene, viewProj))
            return false;
        glm::mat4 inv = glm::inverse(viewProj);
        glm::vec2 ndc((2.0f * rx) / vw - 1.0f, 1.0f - (2.0f * ry) / vh);
        glm::vec4 n = inv * glm::vec4(ndc, -1.0f, 1.0f), f = inv * glm::vec4(ndc, 1.0f, 1.0f);
        if (std::abs(n.w) < 1e-8f || std::abs(f.w) < 1e-8f)
            return false;
        origin = glm::vec3(n) / n.w;
        glm::vec3 d = glm::vec3(f) / f.w - origin;
        if (glm::dot(d, d) < 1e-12f)
            return false;
        direction = glm::normalize(d);
        return true;
    }

    bool WorldToScreenPoint(Scene* scene, const glm::vec3& world, glm::vec2& screen, glm::vec2& uv, float& depth)
    {
        if (!scene)
            return false;
        float vw = scene->m_ViewportBoundsMax.x - scene->m_ViewportBoundsMin.x;
        float vh = scene->m_ViewportBoundsMax.y - scene->m_ViewportBoundsMin.y;
        glm::mat4 viewProj{ 1.0f };
        if (vw <= 0 || vh <= 0 || !ScriptViewProjection(scene, viewProj))
            return false;
        glm::vec4 clip = viewProj * glm::vec4(world, 1.0f);
        depth = clip.w;
        if (std::abs(clip.w) < 1e-8f)
            return false;
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        uv = { (ndc.x + 1.0f) * 0.5f, (1.0f - ndc.y) * 0.5f };
        screen = { scene->m_ViewportBoundsMin.x + uv.x * vw, scene->m_ViewportBoundsMin.y + uv.y * vh };
        return clip.w > 0.0f;
    }

    Value CQSNativeBridge::ViewportScreenToWorld(int argCount, Value* args)
    {
        if (argCount < 2) return Value::MakeNull();
        auto* scene = ScriptContext::GetActiveScene();
        if (!scene) return Value::MakeNull();

        float sx = CQSDebugToFloat(args[0]);
        float sy = CQSDebugToFloat(args[1]);
        float planeZ = argCount >= 3 ? CQSDebugToFloat(args[2], 0.0f) : 0.0f;

        float vw = scene->m_ViewportBoundsMax.x - scene->m_ViewportBoundsMin.x;
        float vh = scene->m_ViewportBoundsMax.y - scene->m_ViewportBoundsMin.y;
        if (vw <= 0 || vh <= 0) return Value::MakeNull();

        float rx = sx - scene->m_ViewportBoundsMin.x;
        float ry = sy - scene->m_ViewportBoundsMin.y;

        if (rx < 0 || ry < 0 || rx >= vw || ry >= vh)
            return Value::MakeNull();

        glm::mat4 viewProj{1.0f};
        bool found = false;

        if (scene->GetUseEditorCameraForScripts() && scene->m_HasEditorCamera)
        {
            viewProj = scene->m_EditorViewProjection;
            found = true;
        }

        if (!found)
        {
            auto camView = scene->m_Registry.view<CameraComponent>();
            for (auto entity : camView)
            {
                auto& cc = camView.get<CameraComponent>(entity);
                if (cc.Primary)
                {
                    auto& tc = scene->m_Registry.get<TransformComponent>(entity);
                    glm::mat4 view = glm::inverse(tc.WorldTransform);
                    glm::mat4 proj = cc.Camera.GetProjection();
                    viewProj = proj * view;
                    found = true;
                    break;
                }
            }
        }

        if (!found && scene->m_HasEditorCamera)
        {
            viewProj = scene->m_EditorViewProjection;
            found = true;
        }

        if (!found) return Value::MakeNull();

        glm::mat4 invVP = glm::inverse(viewProj);

        glm::vec2 ndc;
        ndc.x = (2.0f * rx) / vw - 1.0f;
        ndc.y = 1.0f - (2.0f * ry) / vh;

        glm::vec4 nearPoint = invVP * glm::vec4(ndc.x, ndc.y, -1.0f, 1.0f);
        nearPoint /= nearPoint.w;

        glm::vec4 farPoint = invVP * glm::vec4(ndc.x, ndc.y, 1.0f, 1.0f);
        farPoint /= farPoint.w;

        glm::vec3 rayOrigin = glm::vec3(nearPoint);
        glm::vec3 rayEnd = glm::vec3(farPoint);
        glm::vec3 rayDir = rayEnd - rayOrigin;

        float denom = rayDir.z;
        if (std::abs(denom) < 1e-6f)
        {
            rayDir = glm::normalize(rayDir);
            if (std::abs(rayDir.y) > 1e-6f)
            {
                float tY = (planeZ - rayOrigin.y) / rayDir.y;
                glm::vec3 worldPos = rayOrigin + tY * rayDir;
                auto* map = new CQSMapObject();
                map->Entries["x"] = Value::MakeFloat(worldPos.x);
                map->Entries["y"] = Value::MakeFloat(worldPos.y);
                map->Entries["z"] = Value::MakeFloat(planeZ);
                return Value::MakeObject(map);
            }
            return Value::MakeNull();
        }

        float t = (planeZ - rayOrigin.z) / denom;
        glm::vec3 worldPos = rayOrigin + t * rayDir;

        if (std::isnan(worldPos.x) || std::isinf(worldPos.x) ||
            std::isnan(worldPos.y) || std::isinf(worldPos.y))
            return Value::MakeNull();

        auto* map = new CQSMapObject();
        map->Entries["x"] = Value::MakeFloat(worldPos.x);
        map->Entries["y"] = Value::MakeFloat(worldPos.y);
        map->Entries["z"] = Value::MakeFloat(planeZ);
        return Value::MakeObject(map);
    }

    Value CQSNativeBridge::GetViewportBoundsMin(int, Value*)
    {
        auto* scene = ScriptContext::GetActiveScene();
        if (!scene) return Value::MakeNull();
        auto* map = new CQSMapObject();
        map->Entries["x"] = Value::MakeFloat(scene->m_ViewportBoundsMin.x);
        map->Entries["y"] = Value::MakeFloat(scene->m_ViewportBoundsMin.y);
        return Value::MakeObject(map);
    }

    Value CQSNativeBridge::GetViewportBoundsMax(int, Value*)
    {
        auto* scene = ScriptContext::GetActiveScene();
        if (!scene) return Value::MakeNull();
        auto* map = new CQSMapObject();
        map->Entries["x"] = Value::MakeFloat(scene->m_ViewportBoundsMax.x);
        map->Entries["y"] = Value::MakeFloat(scene->m_ViewportBoundsMax.y);
        return Value::MakeObject(map);
    }

    Value CQSNativeBridge::GetViewportSize(int, Value*)
    {
        auto* scene = ScriptContext::GetActiveScene();
        if (!scene) return Value::MakeNull();
        auto* map = new CQSMapObject();
        map->Entries["x"] = Value::MakeFloat((double)scene->m_ViewportWidth);
        map->Entries["y"] = Value::MakeFloat((double)scene->m_ViewportHeight);
        return Value::MakeObject(map);
    }
}
