#pragma once

#include "Core/Base/Base.h"

#include <cstdint>

// Auto LOD (Unreal Static Mesh "LOD Settings"): bileşenin bulunduğu modelin meshleri için sadeleştirilmiş LOD
// zinciri üretilir (QEM). LOD'lar mesh varlığına bağlıdır: aynı modeli kullanan tüm nesneler paylaşır.
namespace Conqueror
{
    class Scene;

    struct AutoLODComponent
    {
        int Levels = 3;                  // ek seviye (1..4)
        float Reduction = 0.5f;          // seviye başına kalan üçgen oranı
        float ScreenSize1 = 0.35f;       // ekran boyutu (yarıçap / uzaklık * izdüşüm) bunun altına inince LOD1
        float ScreenSize2 = 0.15f;
        float ScreenSize3 = 0.06f;
        float ScreenSize4 = 0.025f;

        // Bilgi (çalışma zamanı)
        int Triangles0 = 0, Triangles1 = 0, Triangles2 = 0, Triangles3 = 0, Triangles4 = 0;
        int CurrentLOD = 0;
        bool Pending = false;
    };

    namespace AutoLOD
    {
        // Bekleyen üretimleri tamamla, bileşenlerin istediği LOD'ları başlat, bilgi alanlarını güncelle
        CQ_API void Update(Scene& scene);
        // Eşzamanlı üretim (betik / araç): üretilen seviye sayısı
        CQ_API int GenerateNow(Scene& scene, uint32_t entity);
    }

    void RegisterAutoLODComponents();
}
