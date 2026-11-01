#pragma once

#include "Core/Base/Base.h"
#include "Mesh.h"

#include <array>
#include <cstdint>
#include <memory>

// Otomatik LOD üretimi (Unreal "LOD Settings / Reduction Settings"): MeshSimplifier ile her seviye bir öncekinin
// Reduction oranında üçgenle üretilir ve meshe bağlanır; renderer ekran boyutuna göre seçer. Büyük meshler
// arka plan iş parçacığında sadeleştirilir, GPU tamponları ana iş parçacığında (Update) kurulur.
namespace Conqueror::MeshLOD
{
    struct Settings
    {
        int Levels = 3;                                   // ek seviye sayısı (1..4)
        float Reduction = 0.5f;                           // seviye başına kalan üçgen oranı
        std::array<float, 4> ScreenSizes{ 0.35f, 0.15f, 0.06f, 0.025f };   // seviye i+1'e geçiş eşiği
    };

    CQ_API uint64_t Signature(const Settings& settings);
    // Eşzamanlı üretim (araçlar / betikler); üretilen ek seviye sayısı
    CQ_API int Generate(const std::shared_ptr<Mesh>& mesh, const Settings& settings);
    // Arka planda üret (aynı mesh için bekleyen iş varsa yenisi açılmaz); true: iş sırada ya da zaten hazır
    CQ_API bool RequestAsync(const std::shared_ptr<Mesh>& mesh, const Settings& settings);
    CQ_API bool IsPending(const Mesh* mesh);
    // Biten işlerin GPU meshlerini kur (her karede ana iş parçacığında)
    CQ_API void Update();
    CQ_API void Clear(const std::shared_ptr<Mesh>& mesh);
}
