#pragma once

#include "Renderer/RHI/TextureSettings.h"

#include <string>

namespace Conqueror::Editor::AssetInspector
{
    // İçerik Tarayıcısında seçilen dosyanın Inspector görünümü:
    // dokular -> içe aktarma ayarları + önizleme (kanallar, mip), diğerleri -> dosya bilgisi
    void Draw(const std::string& path);
    bool IsTexture(const std::string& path);
    // Otomasyon / araçlar: ayarları yaz ve yüklü dokuyu yerinde yenile
    bool ApplyTextureSettings(const std::string& path);
    // Apply'ın görsel dışı sonuçları: Sprite Mode / Mesh Type / Extrude Edges (.cqsprite), Texture Shape: Cube (.cqcubemap)
    void ApplyImportSideEffects(const std::string& path, const TextureImportSettings& settings);
    // Dışarıdan değişen ayarlardan sonra denetçi durumunu yeniden oku
    void Invalidate();
}
