#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

namespace Conqueror
{
    // Ateşle-unut sesler (efekt, arayüz, adım sesi): aynı ses üst üste çalabilir, bitince kendiliğinden silinir.
    // Kısa dosyalar bir kez çözülür ve miniaudio kaynak yöneticisinde paylaşılır; çıkış mikser grubuna gider.
    class CQ_API AudioOneShots
    {
    public:
        static constexpr int MaxVoices = 64;      // aşılırsa en eski ses kesilir

        // path: mutlak ya da çalışma klasörüne göreli dosya. Döndürür: tutamaç (0: çalınamadı)
        static uint32_t Play(const std::string& path, float volume = 1.0f, float pitch = 1.0f, float pan = 0.0f,
                             const std::string& group = "SFX");
        // 3B konumlu (dinleyiciye uzaklıkla azalır)
        static uint32_t PlayAt(const std::string& path, const glm::vec3& position, float volume = 1.0f, float pitch = 1.0f,
                               const std::string& group = "SFX", float minDistance = 1.0f, float maxDistance = 50.0f);
        static bool IsPlaying(uint32_t handle);
        static void Stop(uint32_t handle);
        static void StopAll();
        static int GetActiveCount();

        // Biten sesleri temizle (oyun döngüsü her karede çağırır)
        static void Update();
        // Ses motoru kapanmadan önce
        static void Shutdown();
    };
}
