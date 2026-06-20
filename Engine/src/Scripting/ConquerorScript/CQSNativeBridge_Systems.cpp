#include "CQSNativeBridge.h"
#include "CQSVM.h"
#include "CQSEngine.h"
#include "Core/Logging/Log.h"
#include "Core/Input/Input.h"
#include "Core/Time/TimeManager.h"
#include "Core/Audio/AudioEngine.h"
#include "Core/Audio/AudioSource.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/Scene.h"
#include "AISystem/Pathfinding/AStar.h"
#include "AISystem/Navigation/NavMesh.h"
#include "Networking/NetworkManager.h"
#include "Networking/RPC/RPCSystem.h"
#include "Core/Debug/DebugDraw.h"
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
            
            // Function Keys
            {"F1", Key::F1}, {"F2", Key::F2}, {"F3", Key::F3}, {"F4", Key::F4}, {"F5", Key::F5},
            {"F6", Key::F6}, {"F7", Key::F7}, {"F8", Key::F8}, {"F9", Key::F9}, {"F10", Key::F10},
            {"F11", Key::F11}, {"F12", Key::F12}
        };
        
        auto it = keyMap.find(keyName);
        if (it != keyMap.end()) return it->second;
        
        try {
            return (KeyCode)std::stoi(keyName);
        } catch (...) {}
        
        return (KeyCode)0;
    }

    // Duplicate helper — same as in main .cpp (internal linkage)
    static Entity GetCurrentEntity_S()
    {
        auto* vm = CQSEngine::GetVM();
        auto* scene = static_cast<Scene*>(CQSEngine::GetActiveScene());
        if (!vm || !scene) return Entity{};
        auto* inst = vm->GetCurrentInstance();
        if (!inst) { CQ_CORE_ERROR("[CQS] No current instance!"); return Entity{}; }
        if (inst->EntityHandle == 0xFFFFFFFF) { CQ_CORE_ERROR("[CQS] EntityHandle is 0xFFFFFFFF (Null)!"); return Entity{}; }
        
        entt::entity handle = static_cast<entt::entity>(inst->EntityHandle);
        if (!scene->m_Registry.valid(handle)) 
        { 
            // CQ_CORE_ERROR("[CQS] Entity handle {0} is INVALID in registry!", inst->EntityHandle); 
            // return Entity{}; 
            // Geçiçi olarak valid kontrolünü geçelim çünkü EnTT versioning kafa karıştırabiliyor
        }
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
                return Value::MakeBool(Input::IsKeyPressed(StringToKeyCode(args[0].AsString()->Value)));
            else if (args[0].IsNumber())
                return Value::MakeBool(Input::IsKeyPressed(static_cast<KeyCode>((int)args[0].ToNumber())));
        }
        return Value::MakeBool(false);
    }
    Value CQSNativeBridge::IsKeyDown(int argCount, Value* args)
    {
        if (argCount > 0)
        {
            if (args[0].IsString())
                return Value::MakeBool(Input::IsKeyDown(StringToKeyCode(args[0].AsString()->Value)));
            else if (args[0].IsNumber())
                return Value::MakeBool(Input::IsKeyDown(static_cast<KeyCode>((int)args[0].ToNumber())));
        }
        return Value::MakeBool(false);
    }
    Value CQSNativeBridge::IsKeyUp(int argCount, Value* args)
    {
        if (argCount > 0)
        {
            if (args[0].IsString())
                return Value::MakeBool(Input::IsKeyUp(StringToKeyCode(args[0].AsString()->Value)));
            else if (args[0].IsNumber())
                return Value::MakeBool(Input::IsKeyUp(static_cast<KeyCode>((int)args[0].ToNumber())));
        }
        return Value::MakeBool(false);
    }

    // ════════════════════════════════════════
    //  INPUT: MOUSE
    // ════════════════════════════════════════
    Value CQSNativeBridge::IsMouseButtonPressed(int argCount, Value* args)
    {
        if (argCount > 0 && args[0].IsNumber())
            return Value::MakeBool(Input::IsMouseButtonPressed(static_cast<MouseCode>((int)args[0].ToNumber())));
        return Value::MakeBool(false);
    }
    Value CQSNativeBridge::IsMouseButtonDown(int argCount, Value* args)
    {
        if (argCount > 0 && args[0].IsNumber())
            return Value::MakeBool(Input::IsMouseButtonDown(static_cast<MouseCode>((int)args[0].ToNumber())));
        return Value::MakeBool(false);
    }
    Value CQSNativeBridge::IsMouseButtonUp(int argCount, Value* args)
    {
        if (argCount > 0 && args[0].IsNumber())
            return Value::MakeBool(Input::IsMouseButtonUp(static_cast<MouseCode>((int)args[0].ToNumber())));
        return Value::MakeBool(false);
    }
    Value CQSNativeBridge::GetMousePosition(int, Value*)
    {
        glm::vec2 p = Input::GetMousePosition();
        return Value::MakeObject(MakeV2(p.x, p.y));
    }
    Value CQSNativeBridge::GetMouseX(int, Value*) { return Value::MakeFloat(Input::GetMouseX()); }
    Value CQSNativeBridge::GetMouseY(int, Value*) { return Value::MakeFloat(Input::GetMouseY()); }
    Value CQSNativeBridge::GetMouseDelta(int, Value*)
    {
        glm::vec2 d = Input::GetMouseDelta();
        return Value::MakeObject(MakeV2(d.x, d.y));
    }
    Value CQSNativeBridge::GetMouseScrollDelta(int, Value*) { return Value::MakeFloat(Input::GetMouseScrollDelta()); }
    Value CQSNativeBridge::SetCursorMode(int argCount, Value* args)
    {
        if (argCount > 0 && args[0].IsNumber()) Input::SetCursorMode((int)args[0].ToNumber());
        return Value::MakeNull();
    }

    // ════════════════════════════════════════
    //  INPUT: GAMEPAD
    // ════════════════════════════════════════
    Value CQSNativeBridge::IsGamepadConnected(int, Value*) { return Value::MakeBool(Input::IsGamepadConnected(0)); }
    Value CQSNativeBridge::IsGamepadButtonPressed(int argCount, Value* args)
    {
        if (argCount > 0 && args[0].IsNumber())
            return Value::MakeBool(Input::IsGamepadButtonPressed((int)args[0].ToNumber(), 0));
        return Value::MakeBool(false);
    }
    Value CQSNativeBridge::GetGamepadAxis(int argCount, Value* args)
    {
        if (argCount > 0 && args[0].IsNumber())
            return Value::MakeFloat(Input::GetGamepadAxis((int)args[0].ToNumber(), 0));
        return Value::MakeFloat(0.0);
    }
    Value CQSNativeBridge::GetGamepadLeftStick(int, Value*)
    {
        glm::vec2 s = Input::GetGamepadLeftStick(0);
        return Value::MakeObject(MakeV2(s.x, s.y));
    }
    Value CQSNativeBridge::GetGamepadRightStick(int, Value*)
    {
        glm::vec2 s = Input::GetGamepadRightStick(0);
        return Value::MakeObject(MakeV2(s.x, s.y));
    }
    Value CQSNativeBridge::GetGamepadLeftTrigger(int, Value*) { return Value::MakeFloat(Input::GetGamepadLeftTrigger(0)); }
    Value CQSNativeBridge::GetGamepadRightTrigger(int, Value*) { return Value::MakeFloat(Input::GetGamepadRightTrigger(0)); }
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
        if (asc.RuntimeSound)
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
        auto* scene = static_cast<Scene*>(CQSEngine::GetActiveScene());
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
        auto* scene = static_cast<Scene*>(CQSEngine::GetActiveScene());
        if (scene) scene->DestroyEntity(e);
        return Value::MakeNull();
    }
    Value CQSNativeBridge::CreateEntity(int argCount, Value* args)
    {
        auto* scene = static_cast<Scene*>(CQSEngine::GetActiveScene());
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
    //  RENDERER
    // ════════════════════════════════════════
    Value CQSNativeBridge::SetColor(int argCount, Value* args)
    {
        if (argCount < 4) return Value::MakeNull();
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeNull();
        glm::vec4 color((float)args[0].ToNumber(), (float)args[1].ToNumber(), (float)args[2].ToNumber(), (float)args[3].ToNumber());
        if (e.HasComponent<MeshRendererComponent>()) e.GetComponent<MeshRendererComponent>().Color = color;
        else if (e.HasComponent<SpriteRendererComponent>()) e.GetComponent<SpriteRendererComponent>().Color = color;
        return Value::MakeNull();
    }
    Value CQSNativeBridge::GetColor(int, Value*)
    {
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeNull();
        glm::vec4 c(1.0f);
        if (e.HasComponent<MeshRendererComponent>()) c = e.GetComponent<MeshRendererComponent>().Color;
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
        // Visibility toggle via scale hack (engine doesn't have a Visible flag)
        if (argCount < 1) return Value::MakeNull();
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeNull();
        auto& t = e.GetComponent<TransformComponent>();
        bool vis = args[0].IsTruthy();
        if (!vis) t.Scale = glm::vec3(0.0f);
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
        CQ_CORE_INFO("[Nav] NavSetDestination baslatildi.");
        if (argCount < 3) return Value::MakeNull();
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeNull();
        if (!e.HasComponent<NavMeshAgentComponent>()) 
        {
            CQ_CORE_ERROR("[Nav] Entity '{0}' does NOT have NavMeshAgentComponent!", e.GetComponent<TagComponent>().Tag);
            return Value::MakeNull();
        }

        auto* scene = static_cast<Scene*>(CQSEngine::GetActiveScene());
        auto& agent = e.GetComponent<NavMeshAgentComponent>();
        auto& transform = e.GetComponent<TransformComponent>();

        float tx = (float)args[0].ToNumber();
        float ty = (float)args[1].ToNumber();
        float tz = (float)args[2].ToNumber();
        glm::vec3 target(tx, ty, tz);
        
        const auto& navMesh = scene->GetNavMesh();
        int startTri = navMesh.FindClosestTriangle(transform.Position);
        int endTri = navMesh.FindClosestTriangle(target);

        CQ_CORE_INFO("[Nav] Start: {0}, {1}, {2} | End: {3}, {4}, {5}", transform.Position.x, transform.Position.y, transform.Position.z, tx, ty, tz);
        CQ_CORE_INFO("[Nav] Triangles -> Start: {0}, End: {1}", startTri, endTri);

        bool found = AStar::FindPath(navMesh, transform.Position, target, agent.Path);
        if (found)
        {
            agent.HasPath = true;
            agent.CurrentPathIndex = 0;
            agent.IsStopped = false;
        }
        
        CQ_CORE_INFO("[Nav] AStar sonucu: {0}", found ? "TRUE" : "FALSE");
        return Value::MakeBool(found);
    }

    Value CQSNativeBridge::NavBake(int, Value*)
    {
        auto* scene = static_cast<Scene*>(CQSEngine::GetActiveScene());
        if (!scene) return Value::MakeNull();
        
        NavMeshBuilder::Bake(scene, scene->GetNavMesh());
        CQ_CORE_INFO("[Script] NavMesh Baked via script. Total Triangles: {0}", scene->GetNavMesh().GetTriangles().size());
        
        return Value::MakeNull();
    }

    Value CQSNativeBridge::NavStop(int, Value*)
    {
        Entity e = GetCurrentEntity_S(); if (!e) return Value::MakeNull();
        if (e.HasComponent<NavMeshAgentComponent>()) e.GetComponent<NavMeshAgentComponent>().IsStopped = true;
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
    Value CQSNativeBridge::InputGetAction(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeBool(false);
        return Value::MakeBool(Input::GetActionButton(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::InputGetActionState(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeInt(0);
        return Value::MakeInt((int)Input::GetActionState(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::InputGetActionAxis1D(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeFloat(0.0);
        return Value::MakeFloat(Input::GetActionAxis1D(args[0].AsString()->Value));
    }
    Value CQSNativeBridge::InputGetActionAxis2D(int argCount, Value* args)
    {
        if (argCount < 1 || !args[0].IsString()) return Value::MakeObject(MakeV2(0,0));
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
}
