#pragma once

#include "Core/Base/Base.h"
#include "Core/Input/InputActionAssets.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Input Actions çalışma zamanı (Unity Input System davranışı):
//  - Kontroller oyuncunun aygıtlarından okunur (klavye + fare ve / veya kendi gamepad'i); gamepad çubuklarına Unity gibi
//    varsayılan Stick Deadzone (0.125 .. 0.925) uygulanır.
//  - Birden çok bağlamada en çok basılan kontrol eylemi sürer (Unity çakışma çözümü).
//  - Aşamalar: Waiting -> Started -> Performed -> Canceled. Value eylemi kontrol ilk oynadığında Started + Performed,
//    her değer değişiminde Performed, bırakılınca Canceled; Button eylemi basma noktasını (0.5) geçince Started +
//    Performed, bırakma noktasının (0.375) altına düşünce Canceled; Pass Through her değişiklikte Performed.
//  - Etkileşimler (Hold, Tap, Slow Tap, Multi Tap, Press) bağlamada ya da eylemde; birden çok etkileşimde ilk başlayan
//    eylemi sürer, o iptal olursa sıradaki başlamış etkileşime geçilir.
namespace Conqueror
{
    enum class InputActionPhase : int { Disabled = 0, Waiting, Started, Performed, Canceled };

    // Oyuncunun okuyabildiği aygıtlar
    struct InputDeviceFilter
    {
        bool KeyboardMouse = true;
        int Gamepad = 0;   // >= 0: o gamepad, -1: yok, -2: bağlı her gamepad
    };

    struct InputActionEvent
    {
        std::string Action;
        InputActionPhase Phase = InputActionPhase::Waiting;
        glm::vec2 Value{ 0.0f };
        bool Vector = false;
        InputActionKind Kind = InputActionKind::Button;
    };

    struct InputInteractionState
    {
        InputActionPhase Phase = InputActionPhase::Waiting;
        bool Pressed = false;
        bool Performed = false;
        float PressTime = -1.0f;
        float ReleaseTime = -1.0f;
        int Taps = 0;
    };

    struct InputActionState
    {
        InputActionPhase Phase = InputActionPhase::Waiting;
        glm::vec2 Value{ 0.0f };
        float Magnitude = 0.0f;
        bool Vector = false;
        bool Pressed = false;              // basma noktasının üstünde
        bool PressedThisFrame = false, ReleasedThisFrame = false;
        bool StartedThisFrame = false, PerformedThisFrame = false, CanceledThisFrame = false;
        float StartTime = -1.0f;
        float Duration = 0.0f;             // son Started -> Performed / Canceled süresi
        int ActiveInteraction = -1;
        std::string InteractionSource;     // sürülen etkileşim tanımı (bağlamadan ya da eylemden)
        std::vector<InputInteractionState> Interactions;
        bool Initialized = false;
    };

    struct InputActionPlayerState
    {
        std::string Asset, Map, Scheme;
        uint64_t Stamp = 0;
        std::unordered_map<std::string, InputActionState> Actions;
        void Reset()
        {
            Actions.clear();
            Stamp = 0;
        }
    };

    namespace InputActionRuntime
    {
        CQ_API bool DeviceAvailable(const std::string& device, const InputDeviceFilter& devices);
        // Kontrol değeri: 2B kontrollerde (x, y), diğerlerinde x
        CQ_API glm::vec2 ReadControl(const InputControlInfo& control, const InputDeviceFilter& devices);
        CQ_API glm::vec2 ApplyProcessors(const std::string& processors, glm::vec2 value, bool vector);
        // Haritanın bütün eylemlerini değerlendir; olaylar (Started / Performed / Canceled) sırasıyla events'e eklenir
        CQ_API void Evaluate(const InputActionMapDef& map, const std::string& scheme, const InputDeviceFilter& devices, float time,
                             InputActionPlayerState& state, std::vector<InputActionEvent>* events);
        CQ_API const char* PhaseName(InputActionPhase phase);
        // Girdi zamanı (saniye, oyunun zaman ölçeğinden bağımsız; Unity'de girdi gerçek zamanla ölçülür)
        CQ_API float Now();

        // Project-wide Actions (Project Settings > Input): her karede bütün haritalar, klavye + fare + her gamepad
        CQ_API void SetProjectWideAsset(const std::string& path);
        CQ_API const std::string& GetProjectWideAsset();
        CQ_API void UpdateProjectWide();
        // "Jump" (ilk eşleşen harita) ya da "Player/Jump"
        CQ_API const InputActionState* FindProjectWide(const std::string& action);
    }
}
