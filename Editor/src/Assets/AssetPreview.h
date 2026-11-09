#pragma once

#include <filesystem>
#include <memory>

namespace Conqueror
{
    class Texture2D;
}

namespace Conqueror::Editor::AssetPreview
{
    // Model ve mesh dosyaları için gerçek küçük resim (Content Browser ve nesne seçici)
    bool CanPreview(const std::filesystem::path& path);
    // Hazır değilse nullptr (kare başına tek model hazırlanır, sonraki karelerde döner)
    std::shared_ptr<Texture2D> Get(const std::filesystem::path& path);
}
