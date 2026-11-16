#pragma once

#include "Core/Base/Base.h"
#include "Scene/Entity.h"

#include <filesystem>
#include <string>

// UI Widget (199, Unreal Widget Blueprint / Unity UI Document gibi): ekran üstü UI kendi dosyasında (.cqwidget, kökü Canvas olan
// prefab biçimi) yaşar, sahnede yalnız onu gösteren bir "UI Widget" bileşeni durur. Oyun başlarken (betiklerden önce) widget bileşenin
// altına kurulur; aynı HUD her sahnede kullanılır. UI Editor widget'ı Prefab Mode gibi kendi aşamasında açar (Hierarchy widget'ın
// ağacını gösterir).
namespace Conqueror
{
    class Scene;

    struct UIWidgetComponent
    {
        std::string Widget;       // .cqwidget (proje göreli)
        // Çalışma zamanı
        bool Spawned = false;
    };

    namespace UIWidgets
    {
        constexpr const char* Extension = ".cqwidget";
        // Oyun başı: her UI Widget bileşeni widget'ını altına kurar (kurulu olanlar atlanır)
        CQ_API void InstantiateAll(Scene& scene);
        // Yeni widget dosyası: Canvas + Canvas Scaler (1920 x 1080 referans)
        CQ_API bool CreateFile(const std::filesystem::path& path);
        // Sahnedeki canvas'ı (alt ağacıyla) widget dosyasına yaz
        CQ_API bool SaveCanvas(Entity canvas, const std::filesystem::path& path);
        void RegisterComponents();
    }
}
