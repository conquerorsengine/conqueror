#pragma once

#include "Core/Audio/AudioRandomContainer.h"
#include "Core/Base/Base.h"

#include <cstdint>
#include <string>

// Sound Cue (.cqsoundcue, Unreal Sound Cue): düğüm grafiği her çalışta bir ses üretir (Random, Mixer, Modulator, Delay ...).
// Audio Source'un Clip yuvasına atanır.
namespace Conqueror::SoundCues
{
    constexpr const char* Extension = ".cqsoundcue";
    constexpr const char* Kind = "SoundCue";

    // Sıradaki ses: ilk gecikmesiz klip kaynakta çalar, diğerleri tek seferlik ses olur. false: kaynakta çalacak klip yok
    CQ_API bool Next(const std::string& path, uint64_t owner, AudioRandomContainers::Pick& out);
    // Gecikmeli klipler (her kare)
    CQ_API void UpdatePending(float dt);
    CQ_API void Reset();
}
