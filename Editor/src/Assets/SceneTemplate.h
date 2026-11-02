#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Conqueror::Editor::SceneTemplates
{
    // Sahne şablonu varlığı (.cqscenetemplate): yeni sahneler bu sahnenin kopyası olarak başlar.
    // File > New Scene penceresi yerleşik şablonlarla birlikte projedeki tüm şablonları listeler.
    struct SceneTemplate
    {
        std::string Name;
        std::string Description;
        std::string ScenePath;       // proje göreli (Assets/...)
        std::string ThumbnailPath;   // proje göreli PNG (boş = yok)
        bool Pinned = false;         // New Scene penceresinde üstte
    };

    constexpr const char* Extension = ".cqscenetemplate";

    bool Load(const std::filesystem::path& path, SceneTemplate& out);
    bool Save(const std::filesystem::path& path, const SceneTemplate& t);
    // Projedeki tüm şablon dosyaları (sabitlenmişler önce, sonra ada göre)
    std::vector<std::filesystem::path> FindAll();
    // Proje göreli / mutlak yol
    std::filesystem::path Resolve(const std::string& projectRelative);
    std::string ToProjectRelative(const std::filesystem::path& absolute);

    // Snapshot düğmesi: Game / Scene görünümünü PNG'ye yazan işlev (EditorLayer bağlar)
    void SetSnapshotProvider(std::function<bool(const std::filesystem::path&)> provider);
    // Inspector (dosya seçiliyken)
    void DrawInspector(const std::string& path);
}
