#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

// Unreal Level Streaming / World Partition (sade): başka bir sahne dosyasının nesneleri etkin sahneye "ek olarak"
// yüklenir (ortam / ışık ayarları yüklenmez, kalıcı seviyeninkiler geçerli) ve "[Level] Ad" kökü altında toplanır;
// boşaltmak o kökü siler. Level Streaming Volume: oyuncu hacme girince seviye yüklenir, çıkınca (gecikmeyle) boşaltılır.
namespace Conqueror
{
    class Scene;
    class Entity;

    // Ek yüklenmiş seviyenin kök varlığında
    struct StreamedLevelComponent
    {
        std::string Source;              // proje göreli .cqscene yolu
        std::string LevelName;
    };

    struct LevelStreamingVolumeComponent
    {
        std::string Level;               // .cqscene
        glm::vec3 Size{ 20.0f, 10.0f, 20.0f };   // kutu (varlığın ölçeğiyle çarpılır)
        std::string ActivatorTag = "Player";      // boş = ana kamera
        bool UnloadWhenOutside = true;
        float UnloadDelay = 1.0f;
        glm::vec3 Offset{ 0.0f };        // yüklenen seviyenin dünya kayması

        // Çalışma zamanı
        bool Inside = false;
        bool Loaded = false;
        float OutsideTimer = 0.0f;
    };

    namespace LevelStreaming
    {
        // Seviyeyi ek olarak yükle (zaten yüklüyse mevcut kökü döndürür); kök varlık ya da NullEntity
        CQ_API uint32_t LoadAdditive(Scene& scene, const std::string& path, const glm::vec3& offset = glm::vec3(0.0f));
        CQ_API bool Unload(Scene& scene, const std::string& nameOrPath);
        CQ_API bool IsLoaded(Scene& scene, const std::string& nameOrPath);
        CQ_API std::vector<std::string> GetLoaded(Scene& scene);
        CQ_API void Update(Scene& scene, float dt);
        CQ_API void ClearCache();
    }

    void RegisterLevelStreamingComponents();
}
