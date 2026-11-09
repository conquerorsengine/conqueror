#pragma once

#include "Core/Base/Base.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Conqueror
{
    // Audio Random Container (Unity 2023 Audio Random Container): her çalışta listeden bir klip seçer (adım sesleri,
    // silah sesleri, ortam). Audio Source'un Clip yuvasına atanır. Ses / perde her seferinde rastgele değişebilir;
    // Automatic tetiklemede kaynak belirli aralıklarla kendiliğinden yeni klip çalar.
    struct AudioRandomContainerAsset
    {
        std::vector<std::string> Clips;   // proje göreli ses dosyaları
        float VolumeDb = 0.0f;
        float VolumeRandomDb = 0.0f;      // +- aralık (dB)
        float PitchSemitones = 0.0f;
        float PitchRandomSemitones = 0.0f;// +- aralık (yarım ton)
        int Mode = 2;                     // 0 Sequential, 1 Shuffle, 2 Random
        int AvoidRepeat = 1;              // Random: son N klip tekrar seçilmez
        int Trigger = 0;                  // 0 Manual (Play ile), 1 Automatic (aralıkla)
        float Interval = 1.0f;            // Automatic: saniye
        float IntervalRandom = 0.0f;      // +- saniye
        int Count = 0;                    // Automatic: kaç kez (0 = sınırsız)
    };

    namespace AudioRandomContainers
    {
        constexpr const char* Extension = ".cqaudiorandom";
        CQ_API bool IsAsset(const std::string& path);
        CQ_API bool Load(const std::string& path, AudioRandomContainerAsset& out);
        CQ_API bool Save(const std::string& path, const AudioRandomContainerAsset& asset);

        struct Pick
        {
            std::string Clip;
            float VolumeScale = 1.0f;
            float PitchScale = 1.0f;
            bool Loop = false;      // Sound Cue: Looping düğümü
        };
        // Sıradaki klip (owner: kaynak başına sıra / karıştırma / geçmiş durumu)
        CQ_API bool Next(const std::string& path, uint64_t owner, Pick& out);
        CQ_API void Reset(uint64_t owner);
        CQ_API void ResetAll();   // oyun başında: sıralar baştan
        // Otomatik tetikleme aralığı (rastgele sapma dahil)
        CQ_API float NextInterval(const AudioRandomContainerAsset& asset);
    }
}
