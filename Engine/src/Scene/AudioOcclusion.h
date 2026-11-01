#pragma once

#include "Core/Base/Base.h"

#include <string>

// Unreal ses zayıflatma ayarlarının iki parçası:
//  - Occlusion: dinleyici ile kaynak arasına fizik ışını atılır; arada çarpıştırıcı varsa ses yumuşakça kısılır
//  - Sound Concurrency: aynı gruptaki eşzamanlı ses sayısı sınırlanır (en eski / en uzak / en sessiz durur ya da yenisi engellenir)
namespace Conqueror
{
    class Scene;

    struct SoundOcclusionComponent
    {
        bool Enabled = true;
        float OccludedVolume = 0.3f;      // tamamen örtülünce ses çarpanı
        float InterpolationTime = 0.15f;  // geçiş süresi (s)
        float CheckInterval = 0.05f;      // ışın sıklığı (s)

        // Çalışma zamanı
        float Occlusion = 0.0f;           // 0..1 (yumuşatılmış)
        bool Occluded = false;            // son ışın sonucu
        float Gain = 1.0f;                // kaynağa uygulanan ses çarpanı
        float Timer = 0.0f;
    };

    enum class ConcurrencyRule : int { StopOldest = 0, StopFarthest, StopQuietest, PreventNew };

    struct SoundConcurrencyComponent
    {
        std::string Group = "Default";
        int MaxCount = 4;
        ConcurrencyRule Rule = ConcurrencyRule::StopOldest;

        // Çalışma zamanı
        double StartTime = -1.0;          // çalmaya başladığı an (oyun zamanı)
        int Stopped = 0;                  // sınır yüzünden durdurulma sayısı
    };

    namespace AudioOcclusion
    {
        // Oyun modunda, ses kaynakları güncellenmeden önce
        CQ_API void Update(Scene& scene, float dt, double time);
        CQ_API float GainOf(Scene& scene, uint32_t entity);
    }

    void RegisterAudioOcclusionComponents();
}
