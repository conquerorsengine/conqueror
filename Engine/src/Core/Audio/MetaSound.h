#pragma once

#include "Core/Base/Base.h"

#include <cstdint>
#include <string>

// MetaSounds (Unreal): .cqmetasound düğüm grafiği sesi örnek örnek üretir (dosya çalmaz): Oscillator, Envelope (ADSR), Filter,
// Mixer, Delay, Input (betikten değiştirilen parametre) -> Output. Ses iş parçacığında miniaudio veri kaynağı olarak çalışır,
// 3B konumlanır ve mikser grubuna gider.
namespace Conqueror
{
    class Scene;
    namespace Scripting { class NativeRegistry; }

    struct MetaSoundSourceComponent
    {
        std::string Asset;                    // .cqmetasound
        bool PlayOnAwake = true;
        float Volume = 1.0f;
        bool Spatialize = true;
        float MinDistance = 1.0f;
        float MaxDistance = 50.0f;
        std::string Group = "SFX";            // Audio Mixer grubu (yoksa Master)
        // Bilgi
        bool Playing = false;
        float PeakLevel = 0.0f;               // son ses bloğunun tepe değeri
        bool AutoStarted = false;             // Play On Awake bu oturumda yapıldı
    };

    namespace MetaSounds
    {
        constexpr const char* Kind = "MetaSound";
        constexpr const char* Extension = ".cqmetasound";
        // Oyunda her kare: oynat / konum / bitiş
        CQ_API void Update(Scene& scene);
        CQ_API void StopAll(Scene& scene);
        CQ_API bool Play(Scene& scene, uint32_t entity);
        CQ_API void Stop(Scene& scene, uint32_t entity);
        CQ_API bool SetFloat(Scene& scene, uint32_t entity, const std::string& name, float value);
        void RegisterNatives(Scripting::NativeRegistry* vm);
    }

    void RegisterMetaSoundComponents();
}
