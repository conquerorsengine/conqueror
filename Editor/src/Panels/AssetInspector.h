#pragma once

#include <string>

namespace Conqueror::Editor::AssetInspector
{
    // İçerik Tarayıcısında seçilen dosyanın Inspector görünümü:
    // dokular -> içe aktarma ayarları + önizleme (kanallar, mip), diğerleri -> dosya bilgisi
    void Draw(const std::string& path);
    bool IsTexture(const std::string& path);
    // Otomasyon / araçlar: ayarları yaz ve yüklü dokuyu yerinde yenile
    bool ApplyTextureSettings(const std::string& path);
    // Dışarıdan değişen ayarlardan sonra denetçi durumunu yeniden oku
    void Invalidate();
}
