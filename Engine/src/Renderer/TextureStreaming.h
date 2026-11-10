#pragma once

#include "Core/Base/Base.h"
#include "Core/Project/ProjectSettings.h"

#include <cstdint>

namespace Conqueror
{
    class Scene;
    class Texture2D;
    struct RenderCamera;

    // Unity Quality Settings > Texture Streaming (Mipmap Streaming): ayarlar ProjectSettings::Quality().Streaming'de.
    // İçe aktarmada "Streaming Mipmaps" açık dokular GPU'ya yalnız kameraların gerektirdiği mip seviyesine kadar yüklenir:
    // uzaktaki nesnenin dokusu küçük kalır, yaklaşınca arka planda ayrıntılı seviye çözülüp yüklenir. Toplam doku belleği
    // bütçeyi aşarsa önce düşük öncelikli dokular küçülür

    // Unity Streaming Controller: kameraya eklenir. Add All Cameras kapalıyken yalnız bu kameraların gördüğü dokular
    // yüklenir. Mip Bias > 0 daha kaba seviye (bellek tasarrufu), < 0 daha ayrıntılı. Preload: kamera çizilmeden önce
    // (ör. kamera geçişinden önce) dokuları yüklet (StreamingSetPreloading)
    struct StreamingControllerComponent
    {
        float StreamingMipBias = 0.0f;
        float PreloadTimeLeft = 0.0f;   // çalışma zamanı (saniye)
    };

    namespace TextureStreaming
    {
        struct Stats
        {
            bool Enabled = false;
            int StreamingTextures = 0;
            int PendingLoads = 0;
            float StreamingMB = 0.0f;   // akıştaki dokuların şu anki belleği
            float DesiredMB = 0.0f;     // istenen seviyelerdeki belleği
            float TotalMB = 0.0f;       // tüm dokular
            float BudgetMB = 0.0f;
        };

        CQ_API const TextureStreamingSettings& Settings();
        // Yeni yüklenen akış dokusunun başlangıç seviyesi (akış kapalıysa 0 = tam)
        CQ_API int InitialLevel(uint32_t width, uint32_t height);
        CQ_API void Register(Texture2D* texture);
        CQ_API void Unregister(Texture2D* texture);
        // Bir görünümün (kamera) gerektirdiği seviyeler: sahnedeki Mesh Renderer'ların uzaklığı, ağ UV yoğunluğu, malzeme
        // Tiling'i ve ekran yüksekliğinden
        CQ_API void RequestView(Scene& scene, const RenderCamera& camera, float mipBias);
        // Preloading süresi süren Streaming Controller kameraları (oyun görünümü çizilirken)
        CQ_API void RequestPreloads(Scene& scene, uint32_t width, uint32_t height);
        // Karede bir kez (kare başında): biten çözmeleri yükler, istenen seviyeleri bütçeyle uygular
        CQ_API void Tick();
        CQ_API Stats GetStats();
        CQ_API int PendingCount();
    }

    void RegisterTextureStreamingComponents();
}
